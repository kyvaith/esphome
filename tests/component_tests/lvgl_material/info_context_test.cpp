#include "material_entity_control_codec.h"
#include "context_corpus.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <type_traits>

using namespace esphome::lvgl_material;
using Extra = ControlExtraSnapshot;
struct Sink : MaterialEntityControlSink {
  unsigned commands{0};
  bool emit(const ControlMessage &message) override {
    this->commands += message.kind == ControlKind::CONTROL_KIND_COMMAND;
    return true;
  }
  void view_event(ControlViewEvent) override {}
};
static_assert(std::is_trivially_copyable_v<ControlSnapshot>);
static_assert(std::is_nothrow_copy_assignable_v<ControlSnapshot>);
static_assert(sizeof(Extra) <= sizeof(ControlSnapshot::Robot));

const char *wire(const char *name) {
  for (const auto &item : CASES)
    if (std::strcmp(item.name, name) == 0)
      return item.raw;
  assert(false);
  return nullptr;
}

int main() {
  std::setbuf(stdout, nullptr);
  MaterialEntityControlCodec codec;
  unsigned valid = 0, invalid = 0, failures = 0, owner_cases = 0;
  size_t maximum_wire = 0;
  for (const auto &item : CASES) {
    ControlMessage message;
    message.identity.channel.assign("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee");
    message.generic = true;
    std::array<unsigned char, sizeof(message)> sentinel;
    std::memcpy(sentinel.data(), &message, sizeof(message));
    bool expected = item.valid;
#ifndef USE_LVGL_MATERIAL_INFO_CONTEXT
    expected &= !item.context;
#endif
    const bool parsed = codec.parse_entity(item.raw, message);
    if (parsed != expected) {
      std::printf("FAIL %s parse expected=%u actual=%u\n", item.name, expected, parsed);
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
    assert(codec.serialize_entity(message, output, sizeof(output), length));
    assert(length <= CONTROL_MAX_PAYLOAD_BYTES);
    maximum_wire = length > maximum_wire ? length : maximum_wire;
    ControlMessage again;
    assert(codec.parse_entity({output, length}, again));
    assert(again.state == message.state && again.identity == message.identity && again.kind == message.kind);
    std::printf("%s %s %s\n", item.context ? "WIRE" : "LEGACY", item.name, output);
    valid++;
  }

  ControlText<32> channel;
  channel.assign("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
#ifdef USE_LVGL_MATERIAL_INFO_CONTEXT
  constexpr uint8_t CONFIGURED = 63;
#else
  constexpr uint8_t CONFIGURED = 31;
#endif
  Sink sink;
  MaterialEntityControl owner(sink);
  assert(owner.set_enabled(true) && owner.set_extra_profiles(CONFIGURED) && owner.bind_generic(channel));
  assert(owner.accept_extra_features(channel, 31));
  assert(!owner.set_extra_profiles(32) && owner.extra_profiles() == CONFIGURED);
  assert(!owner.accept_extra_features(channel, 32) && owner.bound_extra_profiles() == 31);
  const char *offers[] = {"[{\"name\":\"action\",\"version\":2},{\"name\":\"info\",\"version\":1},"
                          "{\"name\":\"info_context\",\"version\":1},{\"name\":\"lock\",\"version\":1},"
                          "{\"name\":\"media\",\"version\":1}]",
                          "[{\"name\":\"info_context\",\"version\":1}]",
                          "[{\"name\":\"info\",\"version\":2},{\"name\":\"info_context\",\"version\":1}]",
                          "[{\"name\":\"info\",\"version\":1},{\"name\":\"info_context\",\"version\":2}]"};
  for (unsigned index = 0; index < 4; index++) {
    char raw[512];
    std::snprintf(raw, sizeof(raw), "{\"v\":1,\"type\":\"entity_panel_extra_v1\",\"channel\":\"%s\",\"profiles\":%s}",
                  channel.bytes.data(), offers[index]);
    for (const bool bind : {false, true}) {
      ControlText<32> parsed;
      parsed.assign("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee");
      uint16_t mask = 255;
      bool expected = index == 0 || (index == 3 && !bind);
#ifndef USE_LVGL_MATERIAL_INFO_CONTEXT
      expected = !bind;
#endif
      const bool result = codec.parse_extra_features(raw, parsed, mask, CONFIGURED, bind);
      assert(result == expected);
      if (result) {
        assert(mask == (index == 0 ? CONFIGURED : index == 3 ? 1 : 0));
      } else {
        assert(mask == 255 && parsed.view() == "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee");
      }
      assert(owner.bound_extra_profiles() == 31);
      owner_cases++;
    }
  }
#ifdef USE_LVGL_MATERIAL_INFO_CONTEXT
  static_assert(sizeof(Extra::Person) <= sizeof(Extra::Calendar) && sizeof(Extra::Sun) <= sizeof(Extra::Calendar));
  for (const char *name : {"person-home-en", "sun-day"}) {
    for (uint8_t bound : {1, 33}) {
      Sink local_sink;
      MaterialEntityControl local(local_sink);
      assert(local.set_enabled(true) && local.set_extra_profiles(63) && local.bind_generic(channel));
      assert(local.accept_extra_features(channel, bound));
      ControlMessage message;
      assert(codec.parse_entity(wire(name), message));
      assert(local.request_open(message.identity.context, 1));
      message.identity = local.identity();
      message.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
      assert(local.receive(message, 2) == (bound == 33));
      if (bound == 1) {
        assert(!local.has_selection());
        owner_cases++;
        continue;
      }
      assert(local.set_view_ready(true, 3) && local.is_extra());
      assert(!local.can_extra_lock() && !local.begin_extra_lock(1, 4) && !local.release_extra_lock(1, 5));
      assert(!local.command(ControlOperation::CONTROL_OPERATION_TURN_ON, 6));
      assert(!local.begin_step(1, 1, 7) && !local.begin_drag(1, 7));
      assert(!local.begin_action(1, ControlOperation::CONTROL_OPERATION_ACTION_TRIGGER, 7));
      assert(local_sink.commands == 0 && !local.is_command_pending());
      message.kind = ControlKind::CONTROL_KIND_STATE;
      message.state.state_seq++;
      auto &extra = *message.state.family_if<Extra>();
      extra.updated.assign("2026-12-31T23:59:59-03:30");
      if (auto *person = std::get_if<Extra::Person>(&extra.data)) {
        person->phase = 2;
        person->text.assign("Other complete zone");
      } else {
        std::get<Extra::Sun>(extra.data).phase = 2;
      }
      assert(local.receive(message, 8));
      assert(local.state() == message.state);  // Telemetry is not caps; same generation is valid.
      const auto saved = local.state();
      assert(!local.receive(message, 9) && local.state() == saved);
      // A source transition clears all domain values and advances generation.
      ControlMessage unavailable;
      assert(codec.parse_entity(wire(extra.type == 5 ? "person-unavailable" : "sun-unavailable"), unavailable));
      unavailable.kind = ControlKind::CONTROL_KIND_STATE;
      unavailable.identity = local.identity();
      unavailable.state.state_seq = saved.state_seq + 1;
      unavailable.state.family_if<Extra>()->state_generation = extra.state_generation + 1;
      assert(local.receive(unavailable, 10) && !local.can_extra_lock());
      assert(local.tick(31011) && !local.has_selection());
      assert(local.bind_generic(channel) && local.bound_extra_profiles() == 0);
      owner_cases++;
    }
  }
  // Typed alternatives copy/replace without retaining the previous family.
  ControlSnapshot state;
  state.emplace_family<Extra>().data.emplace<Extra::Person>().text.assign("Complete person");
  const auto saved = state;
  state.family_if<Extra>()->data.emplace<Extra::Sun>().elevation.assign("5e-324");
  assert(state != saved && std::get_if<Extra::Person>(&state.family_if<Extra>()->data) == nullptr);
  state = saved;
  assert(std::get<Extra::Person>(state.family_if<Extra>()->data).text.view() == "Complete person");
  state.clear_family();
  assert(state.has_no_family());
#endif
  std::printf("RESULT enabled=%u valid=%u invalid=%u owner=%u failures=%u arena=%zu wire=%zu "
              "extra=%zu snapshot=%zu message=%zu owner_bytes=%zu\n",
              CONFIGURED == 63, valid, invalid, owner_cases, failures, codec.high_water(), maximum_wire, sizeof(Extra),
              sizeof(ControlSnapshot), sizeof(ControlMessage), sizeof(MaterialEntityControl));
  return failures != 0;
}
