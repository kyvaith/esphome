#include "material_entity_control_codec.h"
#include "text_corpus.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace esphome::lvgl_material;
const char *wire(const char *name) {
  for (const auto &item : CASES)
    if (std::strcmp(item.name, name) == 0)
      return item.raw;
  assert(false);
  return nullptr;
}

#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
using Input = ControlTextInput;
using Snapshot = ControlTextSnapshot;
using Reason = ControlTextDraftReason;
constexpr Input BEGIN = Input::CONTROL_TEXT_INPUT_BEGIN_EDITOR, INSERT = Input::CONTROL_TEXT_INPUT_INSERT,
                BACK = Input::CONTROL_TEXT_INPUT_BACKSPACE, LEFT = Input::CONTROL_TEXT_INPUT_MOVE_LEFT,
                RIGHT = Input::CONTROL_TEXT_INPUT_MOVE_RIGHT, CURSOR = Input::CONTROL_TEXT_INPUT_SELECT_CURSOR,
                CANCEL = Input::CONTROL_TEXT_INPUT_CANCEL, SAVE = Input::CONTROL_TEXT_INPUT_SAVE,
                VIEW = Input::CONTROL_TEXT_INPUT_VIEW;
const std::vector<uint32_t> &repertoire() {
  static const auto values = [] {
    std::vector<uint32_t> result;
    for (uint32_t c = 32; c < 127; c++)
      result.push_back(c);
    for (uint32_t c : {0x104, 0x105, 0x106, 0x107, 0x118, 0x119, 0x141, 0x142, 0x143, 0x144, 0xd3, 0xf3, 0x15a, 0x15b,
                       0x179, 0x17a, 0x17b, 0x17c})
      result.push_back(c);
    std::sort(result.begin(), result.end());
    return result;
  }();
  return values;
}
struct Sink : MaterialEntityControlSink {
  unsigned commands{0}, closes{0};
  bool fail{false}, recurse{false};
  MaterialEntityControl *owner{nullptr};
  MaterialEntityControlCodec codec;
  ControlMessage last;
  std::string target;
  bool emit(const ControlMessage &message) override {
    if (message.kind == ControlKind::CONTROL_KIND_CLOSE) {
      char output[2049];
      size_t length;
      assert(codec.serialize_entity(message, output, sizeof(output), length));
      closes++;
    }
    assert(message.kind != ControlKind::CONTROL_KIND_COMMAND);
    return true;
  }
  bool emit_text(const ControlMessage &message, ControlTextValueView value) override {
    assert(value.present && owner->is_command_pending());
    char output[2049];
    size_t length;
    assert(codec.serialize_entity(message, output, sizeof(output), length, false, false, value));
    commands++;
    last = message;
    target = value.value;
    if (recurse) {
      assert(!owner->disconnect() && !owner->set_enabled(false));
      assert(!owner->begin_text_input(7, SAVE, 0, 1, 20));
      assert(!owner->receive(message, 20, value));
      assert(!owner->set_text_storage(nullptr, nullptr, 0));
    }
    return !fail;
  }
  void view_event(ControlViewEvent) override {}
};
struct BoundaryOwner : MaterialEntityControl {
  using MaterialEntityControl::MaterialEntityControl;
  void revision_boundary() { text_storage_->revision = CONTROL_MAX_SEQUENCE - 1; }
  void command_boundary() { command_seq_ = CONTROL_MAX_SEQUENCE; }
};
struct Session {
  Sink sink;
  BoundaryOwner owner{sink};
  ControlTextStorage storage;
  ControlMessage message;
  std::string observed;
  uint32_t now{10};
  explicit Session(const char *name = "public", bool negotiated = true) {
    sink.owner = &owner;
    assert(owner.set_text_storage(&storage, repertoire().data(), repertoire().size()));
    assert(owner.set_enabled(true) && owner.set_extra_profiles(255));
    assert(sink.codec.parse_entity(wire(name), message, false, false, &storage.rx));
    const auto channel = message.identity.channel;
    const auto context = message.identity.context;
    assert(owner.bind_generic(channel));
    assert(owner.accept_extra_features(channel, negotiated ? 255 : 127));
    assert(owner.request_open(context, now++));
    assert(sink.codec.parse_entity(wire(name), message, false, false, &storage.rx));
    observed = storage.rx.value.view();
    message.identity = owner.identity();
    message.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    assert(owner.receive(message, now++, storage.rx.view()) == negotiated);
    storage.rx.clear();
    if (negotiated)
      assert(owner.set_view_ready(true, now++));
  }
  Snapshot &state() { return *message.state.family_if<Snapshot>(); }
  bool act(Input input, uint32_t arg = 0) {
    return owner.begin_text_input(7, input, arg, 1, now++) && owner.release_text_input(7, 1, now++);
  }
  bool renew() {
    message.kind = ControlKind::CONTROL_KIND_STATE;
    message.state.state_seq++;
    return owner.receive(message, now++,
                         {state().available() ? std::string_view(observed) : std::string_view{}, state().available()});
  }
  bool result(ControlResult result, ControlReason reason = ControlReason::CONTROL_REASON_NONE, uint32_t ahead = 0) {
    ControlMessage m;
    m.generic = true;
    m.kind = ControlKind::CONTROL_KIND_RESULT;
    m.identity = owner.identity();
    m.command_seq = owner.command_sequence();
    m.state.state_seq = owner.state().state_seq + ahead;
    m.result = result;
    m.reason = reason;
    return owner.receive(m, now++);
  }
};

unsigned owner_cases() {
  unsigned cases = 0;
  {
    Session s;
    assert(s.act(BEGIN));
    assert(!s.act(SAVE));
    assert(s.sink.commands == 0);
    assert(s.act(INSERT, '!'));
    const auto prior = s.owner.state();
    assert(s.act(SAVE) && s.sink.commands == 1 && s.owner.is_command_pending());
    assert(s.sink.target == s.observed + "!" && s.sink.last.expected_state_generation == 1);
    assert(s.owner.state() == prior && !s.act(SAVE) && !s.act(BACK));
    assert(!s.result(ControlResult::CONTROL_RESULT_SERVICE_COMPLETED));
    assert(!s.result(ControlResult::CONTROL_RESULT_SERVICE_DISPATCHED));
    assert(!s.result(ControlResult::CONTROL_RESULT_STATE_MATCHES, ControlReason::CONTROL_REASON_NONE, 1));
    // Authoritative state can arrive before callback/result; pending stays owned.
    s.observed = s.sink.target;
    s.state().state_generation++;
    assert(s.renew() && s.owner.is_command_pending());
    assert(s.result(ControlResult::CONTROL_RESULT_HA_ACCEPTED));
    assert(!s.result(ControlResult::CONTROL_RESULT_PENDING));
    assert(s.result(ControlResult::CONTROL_RESULT_STATE_MATCHES) && !s.owner.is_command_pending());
    assert(!s.owner.text_draft().active && s.owner.text_draft().source == s.observed);
    assert(!s.result(ControlResult::CONTROL_RESULT_STATE_MATCHES));
    cases++;
  }
  {
    Session s;
    s.sink.recurse = true;
    assert(s.act(BEGIN) && s.act(INSERT, 'Q') && s.act(SAVE));
    assert(s.sink.commands == 1 && s.owner.is_command_pending());
    cases++;
  }
  {
    Session s;
    assert(s.act(BEGIN) && s.act(INSERT, '!'));
    assert(s.owner.begin_text_input(7, SAVE, 0, 1, s.now++));
    const auto revision = s.owner.text_draft().revision;
    s.message.state.title.assign("Cosmetic title");
    assert(s.renew() && s.owner.is_dragging() && s.owner.text_draft().revision == revision);
    assert(s.owner.release_text_input(7, 1, s.now++) && s.sink.commands == 1);
    cases++;
  }
  for (bool aba : {false, true}) {
    Session s;
    assert(s.act(BEGIN) && s.act(INSERT, '!'));
    assert(s.owner.begin_text_input(7, SAVE, 0, 1, s.now++));
    s.state().state_generation++;
    if (!aba)
      s.observed = "Different";
    assert(s.renew() && !s.owner.is_dragging() && !s.owner.text_draft().active);
    assert(s.owner.text_draft().changed_elsewhere && !s.owner.release_text_input(7, 1, s.now++));
    assert(s.sink.commands == 0);
    cases++;
  }
  {
    Session s;
    assert(s.act(BEGIN));
    s.state().state_generation++;
    assert(s.renew());
    assert(s.act(BEGIN));
    s.state().state_generation--;
    assert(!s.renew() && s.owner.text_draft().active);
    s.state().state_generation++;
    s.observed = "Same generation contradiction";
    assert(!s.renew() && !s.owner.has_selection() && s.storage.source.view().empty());
    cases++;
  }
  for (bool hidden : {false, true}) {
    Session s;
    assert(s.act(BEGIN));
    s.storage.rx.value.assign("staged");
    s.storage.rx.present = true;
    if (hidden) {
      s.state().source = 3;
      s.state().reason = 1;
      s.state().can_set = false;
    } else {
      s.state().maximum--;
    }
    s.state().state_generation++;
    assert(!s.renew() && !s.owner.has_selection());
    assert(s.storage.source.view().empty() && s.storage.draft.view().empty() && !s.storage.rx.present);
    assert(s.owner.text_draft().source.empty());
    cases++;
  }
  {
    Session s("empty");
    assert(s.act(BEGIN));
    assert(!s.act(SAVE) && !s.act(BACK));
    assert(s.act(INSERT, 0x104) && s.owner.text_draft().value == "Ą" && s.owner.text_draft().cursor == 1);
    assert(s.act(INSERT, 'x') && s.act(LEFT) && s.act(BACK));
    assert(s.owner.text_draft().value == "x" && s.owner.text_draft().cursor == 0);
    assert(s.act(RIGHT) && s.act(BACK));
    assert(s.owner.text_draft().value.empty() && !s.act(SAVE));
    assert(!s.act(INSERT, 0x1f600) && !s.act(INSERT, 0x301) && !s.act(INSERT, '\n'));
    assert(s.act(CANCEL) && s.sink.commands == 0);
    cases++;
  }
  {
    Session s("spaces");
    assert(s.act(BEGIN) && s.act(CURSOR, 0) && s.act(INSERT, ' '));
    assert(s.owner.text_draft().value == " " + s.observed);
    assert(s.act(SAVE));
    assert(s.sink.target == " " + s.observed);
    cases++;
  }
  {
    Session s("empty");
    assert(s.act(BEGIN));
    for (char c : std::string("unknown"))
      assert(s.act(INSERT, c));
    assert(!s.owner.text_draft().valid && !s.act(SAVE));
    assert(s.owner.text_draft().reason == Reason::CONTROL_TEXT_DRAFT_REASON_RESERVED);
    assert(s.act(CANCEL) && s.sink.commands == 0);
    cases++;
  }
  {
    Session s;
    assert(s.act(BEGIN));
    assert(s.owner.begin_text_input(7, INSERT, 'Z', 1, s.now++));
    assert(!s.owner.release_text_input(8, 1, s.now++));
    assert(!s.owner.release_text_input(7, 2, s.now++));
    assert(s.owner.text_draft().value == s.observed && !s.owner.is_dragging());
    cases++;
  }
  {
    Session s("readonly");
    assert(s.act(VIEW));
    assert(!s.act(BEGIN) && !s.act(INSERT, 'x') && !s.act(SAVE));
    assert(s.owner.begin_text_input(7, VIEW, 0, 1, s.now++));
    s.state().state_generation++;
    assert(s.renew());
    assert(!s.owner.release_text_input(7, 1, s.now++) && s.sink.commands == 0);
    cases++;
  }
  for (const char *name : {"unknown", "unavailable", "password", "pattern", "bounds", "characters"}) {
    Session s(name);
    assert(!s.act(VIEW) && !s.act(BEGIN) && !s.act(SAVE));
    assert(s.storage.source.view().empty() && s.owner.text_draft().value.empty() && s.sink.commands == 0);
    cases++;
  }
  {
    // Even a read-only transition out of withheld mode requires a fresh lease.
    Session s("password");
    s.state().source = 0;
    s.state().reason = 0;
    s.state().state_generation++;
    s.observed = "Public again";
    assert(!s.renew() && !s.owner.has_selection() && s.storage.source.view().empty());
    assert(s.owner.last_reason() == ControlReason::CONTROL_REASON_CAPABILITIES_CHANGED);
    cases++;
  }
  {
    Session s("public", false);
    assert(!s.owner.has_selection() && !s.act(BEGIN));
    cases++;
  }
  {
    Session s;
    assert(s.act(BEGIN) && s.act(INSERT, '!'));
    s.sink.fail = true;
    assert(!s.act(SAVE) && s.sink.commands == 1 && !s.owner.has_selection());
    assert(s.storage.source.view().empty() && s.storage.draft.view().empty());
    cases++;
  }
  {
    Session s;
    assert(s.act(BEGIN));
    s.owner.revision_boundary();
    assert(!s.act(INSERT, '!'));
    assert(s.sink.commands == 0);
    cases++;
  }
  {
    Session s;
    assert(s.act(BEGIN) && s.act(INSERT, '!'));
    s.owner.command_boundary();
    assert(!s.act(SAVE));
    cases++;
  }
  {
    Session s;
    assert(s.act(BEGIN));
    assert(s.owner.begin_text_input(7, INSERT, '!', 1, s.now++));
    assert(!s.owner.release_text_input(7, 1, s.now + CONTROL_LEASE_MS));
    assert(!s.owner.has_selection());
    cases++;
  }
  {
    Session s;
    assert(s.act(BEGIN) && s.act(INSERT, '!') && s.act(SAVE));
    assert(s.owner.tick(s.now + 13000) && !s.owner.has_selection() && s.sink.commands == 1);
    cases++;
  }
  {
    Session s;
    assert(s.act(BEGIN) && s.act(INSERT, '!'));
    assert(s.owner.set_view_ready(false, s.now++) && !s.owner.text_draft().active);
    assert(!s.act(SAVE) && s.sink.commands == 0);
    cases++;
  }
  {
    Session s;
    assert(s.act(BEGIN));
    auto view = s.owner.text_draft();
    assert(s.act(CURSOR, view.cursor) && s.owner.text_draft().revision == view.revision);
    assert(s.act(VIEW) && s.owner.text_draft().revision == view.revision);
    cases++;
  }
  {
    Session s("max-pl");
    assert(s.act(BEGIN) && s.owner.text_draft().codepoints == 255);
    assert(!s.act(INSERT, 'x'));
    assert(s.act(BACK) && s.act(INSERT, ' '));
    assert(s.act(SAVE));
    assert(s.sink.target.size() == 509);
    cases++;
  }
  {
    Session s("empty");
    assert(s.act(BEGIN));
    // Max10 source permits locally invalid drafts, which remain fully visible and cannot Save.
    for (unsigned i = 0; i <= s.state().maximum; i++)
      assert(s.act(INSERT, 'x'));
    assert(!s.act(SAVE) && !s.owner.text_draft().valid);
    assert(s.act(BACK) && s.act(SAVE));
    cases++;
  }
  {
    Session s;
    assert(s.act(BEGIN));
    auto value = s.observed;
    value.push_back('\n');
    s.observed = value;
    s.state().state_generation++;
    assert(!s.renew() && s.owner.text_draft().active && s.owner.has_selection());
    cases++;
  }
  {
    Session s;
    assert(s.act(BEGIN));
    // Parsing releases its DOM before receive's tick emits an expiry close through the same codec.
    assert(s.sink.codec.parse_entity(wire("public"), s.message, false, false, &s.storage.rx));
    s.message.identity = s.owner.identity();
    s.message.kind = ControlKind::CONTROL_KIND_STATE;
    s.message.state.state_seq++;
    assert(!s.owner.receive(s.message, s.now + CONTROL_LEASE_MS, s.storage.rx.view()));
    assert(s.sink.closes == 1 && !s.owner.has_selection());
    cases++;
  }
  return cases;
}

void utf8_cases() {
  for (const std::string &s :
       {std::string("\xc0\x80", 2), std::string("\xe0\x80\x80", 3), std::string("\xed\xa0\x80", 3),
        std::string("\xf4\x90\x80\x80", 4), std::string("\xf0\x90", 2), std::string("\x80", 1)}) {
    uint16_t n;
    assert(!control_text_measure(s, n));
  }
  uint16_t n;
  assert(control_text_measure("AĄ𐀀", n) && n == 3);
  size_t off;
  assert(control_text_offset("AĄ𐀀", 2, off) && off == 3);
  assert(control_text_validate(" A ", 0, 255, repertoire().data(), repertoire().size()) ==
         Reason::CONTROL_TEXT_DRAFT_REASON_NONE);
}
#endif

int main() {
  unsigned valid = 0, invalid = 0;
  size_t wire_max = 0, accepted_max = 0, text_max = 0, high_water = 0;
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
  FILE *serialized = nullptr;
  if (const char *path = std::getenv("ESPHOME_TEST_TEXT_SERIALIZED"))
    serialized = std::fopen(path, "wb");
#endif
  for (const auto &c : CASES) {
    MaterialEntityControlCodec codec;
    ControlMessage message;
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
    ControlTextRx rx;
    const bool accepted = codec.parse_entity(c.raw, message, false, false, &rx);
    const bool expected = c.valid;
#else
    const bool accepted = codec.parse_entity(c.raw, message);
    const bool expected = c.valid && !c.text;
#endif
    if (accepted != expected) {
      std::fprintf(stderr, "decode mismatch %s expected%d got%d highwater%zu\n", c.name, expected, accepted,
                   codec.high_water());
      return 2;
    }
    high_water = std::max(high_water, codec.high_water());
    wire_max = std::max(wire_max, std::strlen(c.raw));
    if (!accepted) {
      invalid++;
      continue;
    }
    valid++;
    accepted_max = std::max(accepted_max, std::strlen(c.raw));
    if (c.text)
      text_max = std::max(text_max, std::strlen(c.raw));
    char encoded[2049];
    size_t length;
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
    assert(codec.serialize_entity(message, encoded, sizeof(encoded), length, false, false, rx.view()));
    if (serialized != nullptr && c.text)
      std::fprintf(serialized, "{\"name\":\"%s\",\"value\":%s}\n", c.name, encoded);
    ControlMessage roundtrip;
    ControlTextRx rr;
    assert(codec.parse_entity({encoded, length}, roundtrip, false, false, &rr));
    assert(rr.present == rx.present && rr.value == rx.value);
#else
    assert(codec.serialize_entity(message, encoded, sizeof(encoded), length));
    ControlMessage roundtrip;
    assert(codec.parse_entity({encoded, length}, roundtrip));
#endif
    assert(roundtrip.identity == message.identity && roundtrip.kind == message.kind &&
           roundtrip.state == message.state);
    assert(length <= CONTROL_MAX_PAYLOAD_BYTES && codec.high_water() <= 5120);
    high_water = std::max(high_water, codec.high_water());
  }
  unsigned owners = 0;
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
  if (serialized != nullptr)
    std::fclose(serialized);
  utf8_cases();
  owners = owner_cases();
  MaterialEntityControlCodec codec;
  ControlMessage message;
  ControlTextRx rx;
  assert(!codec.parse_entity(wire("public"), message));  // Explicit staging required.
  assert(codec.parse_entity(wire("public"), message, false, false, &rx));
  char out[2049];
  size_t length;
  assert(!codec.serialize_entity(message, out, sizeof(out), length));  // No implicit source copy.
  assert(codec.parse_entity(wire("empty"), message, false, false, &rx));
  assert(codec.serialize_entity(message, out, sizeof(out), length, false, false, {std::string_view{}, true}));
  assert(codec.parse_entity({out, length}, message, false, false, &rx) && rx.present && rx.value.view().empty());
  Sink sink;
  MaterialEntityControl owner(sink);
  assert(!owner.set_extra_profiles(128));
  ControlText<32> channel;
  uint16_t profiles = 0;
  const char *bind = "{\"v\":1,\"type\":\"entity_panel_extra_v1\",\"channel\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\","
                     "\"profiles\":[{\"name\":\"text\",\"version\":1}]}";
  assert(codec.parse_extra_features(bind, channel, profiles, 128, true) && profiles == 128);
  assert(!codec.parse_extra_features(bind, channel, profiles, 127, true));
  assert(codec.parse_extra_features(bind, channel, profiles, 127, false) && profiles == 0);
  std::printf("STORAGE %zu\n", sizeof(ControlTextStorage));
#endif
  std::printf("RESULT valid=%u invalid=%u owner=%u maximum_input_bytes=%zu maximum_accepted_bytes=%zu "
              "maximum_text_bytes=%zu arena_highwater=%zu\n",
              valid, invalid, owners, wire_max, accepted_max, text_max, high_water);
  std::printf("LAYOUT {\"snapshot\":%zu,\"message\":%zu,\"owner\":%zu}\n", sizeof(ControlSnapshot),
              sizeof(ControlMessage), sizeof(MaterialEntityControl));
}
