#include "material_entity_control_codec.h"
#include "tile_event_fixtures.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace esphome::lvgl_material;
using Action = ControlActionSnapshot;
using Op = ControlOperation;
constexpr const char *OFFER =
    R"({"v":1,"type":"entity_panel_extra_v1","channel":"00000000000000000000000000000001","profiles":[{"name":"action","version":2},{"name":"datetime","version":1},{"name":"info","version":1},{"name":"info_context","version":1},{"name":"lock","version":1},{"name":"media","version":1},{"name":"text","version":1},{"name":"tile_event","version":1}]})";
struct Sink : MaterialEntityControlSink {
  ControlMessage last;
  unsigned commands{0};
  bool emit(const ControlMessage &message) override {
    last = message;
    commands += message.kind == ControlKind::CONTROL_KIND_COMMAND;
    return true;
  }
  void view_event(ControlViewEvent) override {}
};
struct Harness {
  Sink sink;
  MaterialEntityControl owner{sink};
  MaterialEntityControlCodec codec;
  ControlTextStorage storage;
  ControlText<32> channel;
  Harness(uint16_t offered = 24) {
    static constexpr uint32_t REPERTOIRE[] = {32};
    channel.assign("00000000000000000000000000000001");
    assert(owner.set_text_storage(&storage, REPERTOIRE, 1));
    assert(owner.set_enabled(true));
#ifdef USE_LVGL_MATERIAL_TILE_EVENT_CONTROLS
    assert(owner.set_extra_profiles(511));
#else
    assert(owner.set_extra_profiles(255));
#endif
    assert(owner.bind_generic(channel));
    assert(owner.accept_extra_features(channel, offered));
  }
  ControlMessage parse(const char *name) {
    for (const auto &row : cases) {
      if (std::strcmp(row.name, name) == 0) {
        ControlMessage message;
        assert(codec.parse_entity(row.wire, message));
        return message;
      }
    }
    assert(false);
    return {};
  }
  bool open(const char *name = "first-use") {
    auto message = parse(name);
    assert(owner.request_open(message.identity.context, 1));
    message.kind = ControlKind::CONTROL_KIND_GRANT;
    message.identity = owner.identity();
    message.identity.token.assign("00000000000000000000000000000002");
    if (!owner.receive(message, 2))
      return false;
    assert(owner.set_view_ready(true, 3));
    assert(owner.is_action() && !owner.is_panel() && !owner.is_extra());
    assert(sink.commands == 0);
    return true;
  }
  ControlMessage next() {
    ControlMessage message;
    message.generic = true;
    message.kind = ControlKind::CONTROL_KIND_STATE;
    message.identity = owner.identity();
    message.state = owner.state();
    message.state.state_seq++;
    message.state.family_if<Action>()->state_generation++;
    return message;
  }
};
int main() {
  MaterialEntityControlCodec codec;
  size_t peak = 0;
  unsigned valid = 0, invalid = 0, owner_cases = 0;
  for (const auto &row : cases) {
    ControlMessage message;
    bool expected = row.accepted;
#ifndef USE_LVGL_MATERIAL_TILE_EVENT_CONTROLS
    expected &= !row.tile;
#endif
    const bool parsed = codec.parse_entity(row.wire, message);
    if (parsed != expected) {
      std::cerr << "parse mismatch " << row.name << '\n';
      return 1;
    }
    if (parsed) {
      valid++;
      char output[CONTROL_MAX_PAYLOAD_BYTES + 1];
      size_t length = 0;
      assert(codec.serialize_entity(message, output, sizeof(output), length));
      ControlMessage again;
      assert(codec.parse_entity({output, length}, again));
      assert(message.identity == again.identity && message.state == again.state &&
             message.operation == again.operation);
      std::cout << row.name << '\t' << output << '\n';
    } else {
      invalid++;
    }
    peak = std::max(peak, codec.high_water());
  }
  assert(std::strlen(OFFER) == 341);
  for (bool bind : {false, true}) {
    ControlText<32> channel;
    uint16_t profiles = 0;
#ifdef USE_LVGL_MATERIAL_TILE_EVENT_CONTROLS
    assert(codec.parse_extra_features(OFFER, channel, profiles, 511, bind) && profiles == 511);
    assert(codec.parse_extra_features(OFFER, channel, profiles, 255, bind) == !bind);
#else
    assert(codec.parse_extra_features(OFFER, channel, profiles, 255, bind) == !bind);
#endif
    if (!bind)
      assert(profiles == 255);
    const char *single =
        R"({"v":1,"type":"entity_panel_extra_v1","channel":"00000000000000000000000000000001","profiles":[{"name":"tile_event","version":1}]})";
#ifdef USE_LVGL_MATERIAL_TILE_EVENT_CONTROLS
    assert(!codec.parse_extra_features(single, channel, profiles, 511, bind));
#else
    assert(codec.parse_extra_features(single, channel, profiles, 255, bind) == !bind);
#endif
    const char *unknown =
        R"({"v":1,"type":"entity_panel_extra_v1","channel":"00000000000000000000000000000001","profiles":[{"name":"future","version":1}]})";
    assert(codec.parse_extra_features(unknown, channel, profiles, 255, bind) == !bind);
    const char *old_action =
        R"({"v":1,"type":"entity_panel_extra_v1","channel":"00000000000000000000000000000001","profiles":[{"name":"action","version":1},{"name":"tile_event","version":1}]})";
#ifdef USE_LVGL_MATERIAL_TILE_EVENT_CONTROLS
    assert(!codec.parse_extra_features(old_action, channel, profiles, 511, bind));
#else
    assert(codec.parse_extra_features(old_action, channel, profiles, 255, bind) == !bind);
    if (!bind)
      assert(profiles == 8);
#endif
    owner_cases++;
  }
  {
    Harness h;
    assert(!h.owner.set_extra_profiles(256) && !h.owner.set_extra_profiles(264));
    assert(!h.owner.accept_extra_features(h.channel, 256) && !h.owner.accept_extra_features(h.channel, 264));
    owner_cases++;
  }
#ifdef USE_LVGL_MATERIAL_TILE_EVENT_CONTROLS
  for (uint16_t offered : {8, 24}) {
    Harness h(offered);
    assert(!h.open());
    assert(h.sink.commands == 0);
    owner_cases++;
  }
  for (const char *name : {"first-use", "timestamp", "read-only", "unknown", "unavailable"}) {
    Harness h(280);
    assert(h.open(name));
    const bool eligible = std::strcmp(name, "first-use") == 0 || std::strcmp(name, "timestamp") == 0;
    assert(h.owner.can_action(Op::CONTROL_OPERATION_ACTION_PRESS) == eligible);
    assert(!h.owner.can_action(Op::CONTROL_OPERATION_ACTION_RUN));
    assert(h.owner.begin_action(9, Op::CONTROL_OPERATION_ACTION_PRESS, 4) == eligible);
    if (eligible) {
      assert(!h.owner.release_action(10, 5));
      assert(h.owner.release_action(9, 6));
      assert(h.sink.commands == 1 &&
             h.sink.last.expected_state_generation == h.owner.state().family_if<Action>()->state_generation);
      if (std::strcmp(name, "first-use") == 0) {
        const auto expected = h.parse("press-command");
        assert(h.sink.last.identity == expected.identity && h.sink.last.command_seq == expected.command_seq &&
               h.sink.last.operation == expected.operation &&
               h.sink.last.expected_state_generation == expected.expected_state_generation);
      }
      assert(!h.owner.release_action(9, 7));
      assert(!h.owner.can_action(Op::CONTROL_OPERATION_ACTION_PRESS));
      auto result = h.parse("result-2");
      result.identity = h.owner.identity();
      result.command_seq = h.sink.last.command_seq;
      result.state.state_seq = h.owner.state().state_seq;
      result.result = ControlResult::CONTROL_RESULT_SERVICE_COMPLETED;
      assert(!h.owner.receive(result, 8) && h.owner.is_command_pending());
      result.result = ControlResult::CONTROL_RESULT_STATE_MATCHES;
      assert(!h.owner.receive(result, 8) && h.owner.is_command_pending());
      result.result = ControlResult::CONTROL_RESULT_SERVICE_DISPATCHED;
      assert(h.owner.receive(result, 8));
      assert(h.owner.last_result() == ControlResult::CONTROL_RESULT_SERVICE_DISPATCHED);
      assert(h.owner.can_action(Op::CONTROL_OPERATION_ACTION_PRESS));
    }
    owner_cases++;
  }
  for (unsigned fence = 0; fence < 4; fence++) {
    Harness h(280);
    assert(h.open());
    assert(h.owner.begin_action(9, Op::CONTROL_OPERATION_ACTION_PRESS, 4));
    if (fence == 0) {
      auto b = h.next();
      auto *action = b.state.family_if<Action>();
      action->source = 1;
      action->phase = 5;
      action->eligible = 0;
      assert(h.owner.receive(b, 5));
      auto a = h.next();
      *a.state.family_if<Action>() = *h.parse("first-use").state.family_if<Action>();
      a.state.family_if<Action>()->state_generation = 3;
      assert(h.owner.receive(a, 6));
    } else if (fence == 1) {
      assert(h.owner.set_view_ready(false, 5));
    } else if (fence == 2) {
      auto changed = h.next();
      changed.state.family_if<Action>()->operations = 0;
      changed.state.family_if<Action>()->eligible = 0;
      assert(!h.owner.receive(changed, 5));
      assert(!h.owner.has_selection());
    } else {
      assert(h.owner.set_extra_profiles(255));
      assert(!h.owner.has_selection());
    }
    assert(!h.owner.release_action(9, 7));
    assert(h.sink.commands == 0);
    owner_cases++;
  }
#else
  {
    ControlSnapshot snapshot;
    auto &action = snapshot.emplace_family<Action>();
    action.type = 5;
    assert(!control_valid_action_snapshot(snapshot));
    owner_cases++;
  }
#endif
  std::cerr << "PASS valid=" << valid << " rejected=" << invalid << " owner=" << owner_cases << " peak=" << peak
            << " snapshot=" << sizeof(ControlSnapshot) << " message=" << sizeof(ControlMessage)
            << " owner_size=" << sizeof(MaterialEntityControl) << '\n';
}
