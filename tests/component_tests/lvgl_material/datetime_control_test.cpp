#include "material_entity_control_codec.h"
#include "datetime_corpus.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <type_traits>

using namespace esphome::lvgl_material;
static_assert(std::is_trivially_copyable_v<ControlSnapshot>);
static_assert(std::is_nothrow_copy_assignable_v<ControlSnapshot>);
struct Sink : MaterialEntityControlSink {
  unsigned commands{0};
  ControlMessage last;
  bool fail{false};
  bool emit(const ControlMessage &message) override {
    if (message.kind == ControlKind::CONTROL_KIND_COMMAND) {
      this->commands++;
      this->last = message;
      return !this->fail;
    }
    return true;
  }
  void view_event(ControlViewEvent) override {}
};
const char *wire(const char *name) {
  for (const auto &item : CASES)
    if (std::strcmp(item.name, name) == 0)
      return item.raw;
  assert(false);
  return nullptr;
}

#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
using Input = ControlDatetimeInput;
using Snapshot = ControlDatetimeSnapshot;
constexpr Input SELECT = Input::CONTROL_DATETIME_INPUT_SELECT_FIELD, DIGIT = Input::CONTROL_DATETIME_INPUT_DIGIT,
                BACK = Input::CONTROL_DATETIME_INPUT_BACKSPACE, ACCEPT = Input::CONTROL_DATETIME_INPUT_ACCEPT_FIELD,
                DOWN = Input::CONTROL_DATETIME_INPUT_STEP_DOWN, UP = Input::CONTROL_DATETIME_INPUT_STEP_UP,
                CANCEL = Input::CONTROL_DATETIME_INPUT_CANCEL, SAVE = Input::CONTROL_DATETIME_INPUT_SAVE;
class BoundaryOwner : public MaterialEntityControl {
 public:
  using MaterialEntityControl::MaterialEntityControl;
  void revision_boundary() { this->datetime_.revision = CONTROL_MAX_SEQUENCE - 1; }
  void command_boundary() { this->command_seq_ = CONTROL_MAX_SEQUENCE; }
  static size_t draft_size() { return sizeof(DatetimeDraft); }
};
struct Session {
  Sink sink;
  BoundaryOwner owner{sink};
  ControlMessage message;
  uint32_t now{10};
  explicit Session(const char *name = "datetime-grant", uint16_t mask = 127) {
    MaterialEntityControlCodec codec;
    assert(codec.parse_entity(wire(name), this->message));
    assert(this->owner.set_enabled(true) && this->owner.set_extra_profiles(127));
    assert(this->owner.bind_generic(this->message.identity.channel));
    assert(this->owner.accept_extra_features(this->message.identity.channel, mask));
    assert(this->owner.request_open(this->message.identity.context, this->now++));
    this->message.identity = this->owner.identity();
    this->message.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    assert(this->owner.receive(this->message, this->now++) == ((mask & 64U) != 0));
    if ((mask & 64U) != 0)
      assert(this->owner.set_view_ready(true, this->now++));
  }
  bool act(Input input, uint8_t argument = 0) {
    return this->owner.begin_datetime_input(7, input, argument, this->now++) &&
           this->owner.release_datetime_input(7, this->now++);
  }
  bool renew() {
    this->message.kind = ControlKind::CONTROL_KIND_STATE;
    this->message.state.state_seq++;
    return this->owner.receive(this->message, this->now++);
  }
  bool result(ControlResult result, ControlReason reason = ControlReason::CONTROL_REASON_NONE, uint32_t ahead = 0) {
    ControlMessage message;
    message.generic = true;
    message.kind = ControlKind::CONTROL_KIND_RESULT;
    message.identity = this->owner.identity();
    message.command_seq = this->owner.command_sequence();
    message.state.state_seq = this->owner.state().state_seq + ahead;
    message.result = result;
    message.reason = reason;
    return this->owner.receive(message, this->now++);
  }
  Snapshot &state() { return *this->message.state.family_if<Snapshot>(); }
};

unsigned owner_cases() {
  unsigned cases = 0;
  {  // One Save accepts active digits, no separate checkmark and no optimistic observed state.
    Session s;
    assert(s.act(SELECT, 5));
    assert(s.act(DIGIT, 5) && s.act(DIGIT, 9));
    assert(s.sink.commands == 0 && s.owner.datetime_draft().can_save);
    const auto observed = s.owner.state();
    assert(s.act(SAVE) && s.sink.commands == 1 && s.owner.is_command_pending());
    assert(s.sink.last.state.family_if<Snapshot>()->value.values[5] == 59);
    assert(s.sink.last.expected_state_generation == s.state().state_generation);
    assert(s.owner.state() == observed && !s.act(SAVE) && !s.act(SELECT, 0));
    assert(!s.result(ControlResult::CONTROL_RESULT_SERVICE_COMPLETED));
    assert(!s.result(ControlResult::CONTROL_RESULT_SERVICE_DISPATCHED));
    assert(!s.result(ControlResult::CONTROL_RESULT_STATE_MATCHES, ControlReason::CONTROL_REASON_NONE, 1));
    assert(s.result(ControlResult::CONTROL_RESULT_HA_ACCEPTED));
    assert(!s.result(ControlResult::CONTROL_RESULT_PENDING));
    s.state().value.values[5] = 59;
    s.state().state_generation++;
    assert(s.renew() && s.owner.is_command_pending());
    assert(s.result(ControlResult::CONTROL_RESULT_STATE_MATCHES) && !s.owner.is_command_pending());
    assert(!s.owner.datetime_draft().active && !s.result(ControlResult::CONTROL_RESULT_STATE_MATCHES));
    cases++;
  }
  {  // Metadata and lease renewals retain the exact held Save fence.
    Session s;
    assert(s.act(SELECT, 0) && s.act(DIGIT, 2) && s.act(DIGIT, 0) && s.act(DIGIT, 2) && s.act(DIGIT, 9));
    assert(s.owner.begin_datetime_input(7, SAVE, 0, s.now++));
    const auto draft = s.owner.datetime_draft();
    s.message.state.title.assign("New title");
    s.state().zone.assign("Changed/Zone");
    s.state().updated.assign("2026-11-06T01:02:03-03:30");
    assert(s.renew() && s.owner.is_dragging());
    assert(s.owner.datetime_draft().revision == draft.revision);
    assert(s.owner.release_datetime_input(7, s.now++) && s.sink.commands == 1);
    cases++;
  }
  for (bool aba : {false, true}) {
    Session s;
    assert(s.act(SELECT, 3) && s.act(DIGIT, 1));
    assert(s.owner.begin_datetime_input(7, SAVE, 0, s.now++));
    s.state().state_generation++;
    if (!aba)
      s.state().value.values[3] = (s.state().value.values[3] + 1) % 24;
    assert(s.renew() && !s.owner.is_dragging() && !s.owner.datetime_draft().active);
    assert(s.owner.datetime_draft().changed_elsewhere);
    assert(!s.owner.release_datetime_input(7, s.now++) && s.sink.commands == 0);
    cases++;
  }
  {  // Same-generation semantic changes retire; lower generations reject without mutation.
    Session s;
    assert(s.act(SELECT, 3));
    s.state().state_generation++;
    assert(s.renew());
    assert(s.act(SELECT, 3));
    s.state().state_generation--;
    assert(!s.renew() && s.owner.datetime_draft().active);
    s.state().state_generation++;
    s.state().value.values[3] = (s.state().value.values[3] + 1) % 24;
    assert(!s.renew() && !s.owner.has_selection() && !s.owner.datetime_draft().active);
    cases++;
  }
  for (bool mode : {false, true}) {
    Session s;
    assert(s.act(SELECT, 0));
    if (mode) {
      s.state().mode = ControlDatetimeMode::CONTROL_DATETIME_MODE_DATE;
      s.state().zone.assign("");
      s.state().value.known_mask = 7;
      s.state().value.values[3] = s.state().value.values[4] = s.state().value.values[5] = 0;
    } else {
      s.state().can_set = false;
    }
    assert(!s.renew() && !s.owner.has_selection());
    assert(s.owner.last_reason() == ControlReason::CONTROL_REASON_CAPABILITIES_CHANGED);
    cases++;
  }
  {  // Simple field validity and whole Gregorian validity have different edit boundaries.
    Session s("date-grant");
    assert(s.act(SELECT, 2) && s.act(DIGIT, 3) && s.act(DIGIT, 1));
    assert(s.act(SELECT, 1) && !s.owner.datetime_draft().can_save);  // February 31 retained.
    assert(s.act(DIGIT, 0) && !s.act(SELECT, 2) && !s.act(SAVE));
    assert(std::strcmp(s.owner.datetime_draft().digits.data(), "0") == 0);
    assert(s.act(BACK) && !s.act(ACCEPT) && !s.act(SAVE));
    assert(s.act(DIGIT, 2) && s.act(SELECT, 2));
    assert(s.act(DIGIT, 2) && s.act(DIGIT, 9) && s.owner.datetime_draft().can_save);
    assert(s.act(SAVE) && s.sink.commands == 1);
    assert(s.sink.last.state.family_if<Snapshot>()->value.known_mask == 7);
    cases++;
  }
  {  // Leap-year edits preserve the invalid day until explicitly repaired.
    Session s("date-grant");
    assert(s.act(SELECT, 2) && s.act(DIGIT, 2) && s.act(DIGIT, 9) && s.act(ACCEPT));
    assert(s.act(SELECT, 0) && s.act(DIGIT, 2) && s.act(DIGIT, 0) && s.act(DIGIT, 2) && s.act(DIGIT, 7));
    assert(!s.act(SAVE) && s.act(ACCEPT) && !s.owner.datetime_draft().tuple_valid);
    assert(s.owner.datetime_draft().value.values[2] == 29);
    assert(s.act(SELECT, 2) && s.act(DOWN) && s.act(SAVE) && s.sink.commands == 1);
    cases++;
  }
  {  // Bounded digit entry, no-op same-field selection, step and replace-on-first semantics.
    Session s;
    assert(s.act(SELECT, 0) && s.act(DIGIT, 1));
    auto view = s.owner.datetime_draft();
    assert(s.act(SELECT, 0) && s.owner.datetime_draft().revision == view.revision);
    assert(s.act(DIGIT, 2) && s.act(DIGIT, 3) && s.act(DIGIT, 4) && !s.act(DIGIT, 5));
    assert(s.act(UP) && std::strcmp(s.owner.datetime_draft().digits.data(), "1235") == 0);
    assert(s.act(DIGIT, 1) && std::strcmp(s.owner.datetime_draft().digits.data(), "1") == 0);
    assert(!s.act(DOWN) && s.act(BACK) && !s.act(ACCEPT));
    assert(s.act(CANCEL) && !s.owner.datetime_draft().active && s.sink.commands == 0);
    cases++;
  }
  {  // Mode excludes absent fields; seconds remain exact, and no wrap at 59.
    Session s("time-grant");
    assert(!s.act(SELECT, 0) && !s.act(SELECT, 2));
    assert(s.act(SELECT, 5) && s.act(DIGIT, 5) && s.act(DIGIT, 9));
    assert(!s.act(UP) && s.act(SAVE));
    assert(s.sink.last.state.family_if<Snapshot>()->value.known_mask == 56);
    cases++;
  }
  for (unsigned lifecycle = 0; lifecycle < 5; lifecycle++) {
    Session s;
    assert(s.act(SELECT, 0) && s.owner.begin_datetime_input(7, SAVE, 0, s.now++));
    if (lifecycle == 0)
      assert(s.owner.set_view_ready(false, s.now++));
    else if (lifecycle == 1)
      assert(s.owner.disconnect());
    else if (lifecycle == 2)
      assert(s.owner.invalidate_admission(ControlReason::CONTROL_REASON_VOICE));
    else if (lifecycle == 3)
      assert(s.owner.tick(s.now + 31000));
    else {
      s.state().source = 2;
      s.state().value = {};
      s.state().state_generation++;
      assert(s.renew());
    }
    assert(!s.owner.release_datetime_input(7, s.now++) && !s.owner.datetime_draft().active && s.sink.commands == 0);
    cases++;
  }
  {  // Pointer cancellation retains an unsent draft; stale pointer release cannot submit it.
    Session s;
    assert(s.act(SELECT, 0) && s.owner.begin_datetime_input(7, SAVE, 0, s.now++));
    assert(!s.owner.release_datetime_input(8, s.now++));
    s.owner.cancel_datetime_input();
    assert(!s.owner.release_datetime_input(7, s.now++) && s.owner.datetime_draft().active);
    assert(s.act(SAVE) && s.result(ControlResult::CONTROL_RESULT_ALREADY_MATCHED));
    assert(s.sink.commands == 1 && !s.owner.datetime_draft().active);
    cases++;
  }
  {  // Failed transport discards draft; no second service attempt and no late paint.
    Session s;
    assert(s.act(SELECT, 0));
    s.sink.fail = true;
    assert(!s.act(SAVE) && s.sink.commands == 1 && !s.owner.has_selection());
    assert(!s.renew() && !s.owner.datetime_draft().active);
    cases++;
  }
  {  // Negotiation is required independently of other optional families.
    Session s("datetime-grant", 63);
    assert(!s.owner.has_selection() && !s.act(SELECT, 0));
    Session only("date-grant", 64);
    assert(only.owner.is_datetime() && only.act(SELECT, 0));
    assert(!only.owner.begin_drag(1, only.now++) && !only.owner.begin_step(1, 1, only.now++));
    assert(!only.owner.command(ControlOperation::CONTROL_OPERATION_TURN_ON, only.now++));
    assert(only.sink.commands == 0);
    cases++;
  }
  {  // Exhaustion consumes no command and cannot wrap back into a live draft revision.
    Session s;
    assert(BoundaryOwner::draft_size() <= 64);
    assert(s.act(SELECT, 0));
    s.owner.revision_boundary();
    assert(!s.act(SAVE) && !s.owner.datetime_draft().active && s.sink.commands == 0);
    assert(!s.act(SELECT, 0));
    Session command;
    assert(command.act(SELECT, 0));
    command.owner.command_boundary();
    assert(!command.act(SAVE) && command.sink.commands == 0);
    cases++;
  }
  {  // A failed terminal drops pending edits; a new deliberate edit is required.
    Session s;
    assert(s.act(SELECT, 0) && s.act(SAVE));
    assert(s.result(ControlResult::CONTROL_RESULT_NOT_CONFIRMED, ControlReason::CONTROL_REASON_STATE_TIMEOUT));
    assert(!s.owner.datetime_draft().active && !s.act(SAVE));
    assert(s.act(SELECT, 0) && s.sink.commands == 1);
    cases++;
  }
  return cases;
}
#endif

int main() {
  std::setbuf(stdout, nullptr);
  MaterialEntityControlCodec codec;
  unsigned valid = 0, invalid = 0, failures = 0;
  size_t maximum_wire = 0;
  for (const auto &item : CASES) {
    ControlMessage message;
    message.identity.channel.assign("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee");
    std::array<unsigned char, sizeof(message)> sentinel;
    std::memcpy(sentinel.data(), &message, sizeof(message));
    bool expected = item.valid;
#ifndef USE_LVGL_MATERIAL_DATETIME_CONTROLS
    expected &= !item.datetime;
#endif
    const bool parsed = codec.parse_entity(item.raw, message);
    if (parsed != expected) {
      std::printf("FAIL %s expected=%u actual=%u\n", item.name, expected, parsed);
      failures++;
      continue;
    }
    if (!parsed) {
      assert(std::memcmp(sentinel.data(), &message, sizeof(message)) == 0);
      invalid++;
      continue;
    }
    char output[2049];
    size_t length;
    if (!codec.serialize_entity(message, output, sizeof(output), length)) {
      std::printf("FAIL serialize %s\n", item.name);
      failures++;
      continue;
    }
    maximum_wire = length > maximum_wire ? length : maximum_wire;
    ControlMessage again;
    assert(codec.parse_entity({output, length}, again));
    assert(again.kind == message.kind && again.identity == message.identity && again.state == message.state);
    assert(again.operation == message.operation &&
           again.expected_state_generation == message.expected_state_generation);
    if (item.datetime)
      std::printf("WIRE %s %s\n", item.name, output);
    valid++;
  }
  ControlText<32> channel;
  channel.assign("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
  constexpr uint8_t CONFIGURED = 127;
#else
  constexpr uint8_t CONFIGURED = 63;
#endif
  Sink sink;
  MaterialEntityControl owner(sink);
  assert(owner.set_enabled(true) && owner.set_extra_profiles(CONFIGURED) && owner.bind_generic(channel));
  assert(owner.accept_extra_features(channel, 63));
  const char *offers[] = {
      ("[{\"name\":\"action\",\"version\":2},{\"name\":\"datetime\",\"version\":1},{\"name\":\"info\",\"version\":1},"
       "{\"name\":\"info_context\",\"version\":1},{\"name\":\"lock\",\"version\":1},{\"name\":\"media\",\"version\":1}"
       "]"),
      "[{\"name\":\"datetime\",\"version\":1}]", "[{\"name\":\"datetime\",\"version\":2}]",
      "[{\"name\":\"datetime\",\"version\":1},{\"name\":\"datetime\",\"version\":1}]",
      "[{\"name\":\"info_context\",\"version\":1},{\"name\":\"datetime\",\"version\":1}]"};
  for (unsigned index = 0; index < 5; index++) {
    char raw[512];
    std::snprintf(raw, sizeof(raw), "{\"v\":1,\"type\":\"entity_panel_extra_v1\",\"channel\":\"%s\",\"profiles\":%s}",
                  channel.bytes.data(), offers[index]);
    for (const bool bind : {false, true}) {
      ControlText<32> parsed;
      parsed.assign("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee");
      uint16_t mask = 255;
      bool expected = index < 2 || (index == 2 && !bind);
#ifndef USE_LVGL_MATERIAL_DATETIME_CONTROLS
      expected = index < 3 && !bind;
#endif
      const bool result = codec.parse_extra_features(raw, parsed, mask, CONFIGURED, bind);
      assert(result == expected);
      if (result)
        assert(mask == (index == 0 ? CONFIGURED : index == 1 ? (CONFIGURED & 64U) : 0));
      else
        assert(mask == 255 && parsed.view() == "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee");
      assert(owner.bound_extra_profiles() == 63);
    }
  }
  unsigned owners = 0;
#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
  if (failures == 0)
    owners = owner_cases();
#endif
  std::printf("RESULT valid=%u invalid=%u failures=%u owner=%u negotiation=10 max_wire=%zu arena_high=%zu\n", valid,
              invalid, failures, owners, maximum_wire, codec.high_water());
  std::printf("LAYOUT {\"snapshot\":%zu,\"message\":%zu,\"owner\":%zu}\n", sizeof(ControlSnapshot),
              sizeof(ControlMessage), sizeof(MaterialEntityControl));
  return failures != 0;
}
