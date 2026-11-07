#include "material_entity_control_codec.h"
#include "action_parameters_fixtures.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace esphome::lvgl_material;
using Action = ControlActionSnapshot;
using Op = ControlOperation;
struct Sink : MaterialEntityControlSink {
  ControlMessage last;
  unsigned commands{0};
  bool emit(const ControlMessage &m) override {
    last = m;
    commands += m.kind == ControlKind::CONTROL_KIND_COMMAND;
    return true;
  }
  void view_event(ControlViewEvent) override {}
};
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
struct TestOwner : MaterialEntityControl {
  using MaterialEntityControl::MaterialEntityControl;
  void saturate_next_choice() { action_parameter_revision_ = UINT32_MAX - 1; }
};
struct Harness {
  Sink sink;
  TestOwner owner{sink};
  MaterialEntityControlCodec codec;
  ControlText<32> channel;
  Harness(bool enabled = true, uint16_t peer = 24) {
    assert(channel.assign("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    assert(owner.set_enabled(true) && owner.set_extra_profiles(24) && owner.bind_generic(channel));
    assert(owner.set_action_parameters(enabled));
    assert(owner.accept_extra_features(channel, peer));
  }
  bool bind() { return owner.accept_action_parameters(channel); }
  bool open(uint8_t default_value = Action::UNKNOWN_BOOL, bool parameters = true) {
    ControlMessage m;
    assert(codec.parse_entity(ready_wire, m));
    m.state.family_if<Action>()->parameter_default = default_value;
    if (!parameters) {
      auto *a = m.state.family_if<Action>();
      a->parameters_version = 0;
      assert(a->parameter_label.assign(""));
      a->parameter_default = Action::UNKNOWN_BOOL;
    }
    assert(owner.request_open(m.identity.context, 1));
    m.kind = ControlKind::CONTROL_KIND_GRANT;
    m.identity = owner.identity();
    m.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    if (!owner.receive(m, 2))
      return false;
    assert(owner.set_view_ready(true, 3));
    owner.set_action_parameter_ready(true);
    return true;
  }
  ControlMessage next(bool semantic = false) {
    ControlMessage m;
    m.generic = true;
    m.kind = ControlKind::CONTROL_KIND_STATE;
    m.identity = owner.identity();
    m.state = owner.state();
    m.state.state_seq++;
    if (semantic)
      m.state.family_if<Action>()->state_generation++;
    return m;
  }
  ControlMessage result(ControlResult value) {
    ControlMessage m;
    m.generic = true;
    m.kind = ControlKind::CONTROL_KIND_RESULT;
    m.identity = owner.identity();
    m.command_seq = sink.last.command_seq;
    m.state.state_seq = owner.state().state_seq;
    m.result = value;
    char wire[2049];
    size_t size;
    ControlMessage decoded;
    assert(codec.serialize_entity(m, wire, sizeof(wire), size));
    assert(codec.parse_entity({wire, size}, decoded));
    return decoded;
  }
  void run(bool value) {
    assert(owner.begin_action(1, Op::CONTROL_OPERATION_ACTION_RUN, 4));
    assert(owner.release_action(1, 5));
    assert(!owner.release_action(1, 6) && sink.commands == 1);
    assert(sink.last.panel_value == 1 && sink.last.panel_enabled == value);
    char wire[2049];
    size_t size;
    ControlMessage decoded;
    assert(codec.serialize_entity(sink.last, wire, sizeof(wire), size));
    assert(codec.parse_entity({wire, size}, decoded) && decoded.panel_value == 1 && decoded.panel_enabled == value);
  }
};
#endif
int main() {
  MaterialEntityControlCodec codec;
  unsigned checked = 0;
  for (const auto &row : cases) {
    ControlMessage m;
    const bool accepted = codec.parse_entity(row.wire, m);
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
    const bool expected = row.accepted;
#else
    const bool expected = row.legacy && row.accepted;
#endif
    if (accepted != expected) {
      std::fprintf(stderr, "case %s accepted=%u expected=%u\n", row.name, accepted, expected);
      return 1;
    }
    if (accepted) {
      char wire[2049];
      size_t size;
      ControlMessage again;
      assert(codec.serialize_entity(m, wire, sizeof(wire), size));
      assert(codec.parse_entity({wire, size}, again));
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
      if (m.state.family_if<Action>())
        assert(*m.state.family_if<Action>() == *again.state.family_if<Action>());
      if (m.kind == ControlKind::CONTROL_KIND_COMMAND)
        assert(m.panel_value == again.panel_value && m.panel_enabled == again.panel_enabled);
#endif
    }
    checked++;
  }
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
  const char *bind =
      R"({"v":1,"type":"entity_action_parameters_v1","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","parameters_version":1})";
  ControlText<32> channel;
  assert(codec.parse_action_parameters_bind(bind, channel));
  for (const auto &wire : bad_binds) {
    auto old = channel;
    assert(!codec.parse_action_parameters_bind(wire, channel) && channel == old);
  }
  {
    Harness h(false);
    assert(!h.bind() && !h.open());
  }
  {
    Harness h(true, 8);
    assert(!h.bind() && !h.open());
  }
  {
    Harness h;
    assert(!h.open());
  }
  // A Run receipt confirms public dispatch, even after authoritative running/idle states.
  for (uint8_t value : {uint8_t(0), uint8_t(1)}) {
    for (unsigned transitions = 0; transitions <= 2; transitions++) {
      Harness h;
      assert(h.bind() && h.open(value));
      h.run(value);
      for (unsigned i = 0; i < transitions; i++) {
        auto state = h.next(true);
        auto *a = state.state.family_if<Action>();
        a->phase = i == 0 ? 2 : 1;
        a->eligible = control_action_eligible(*a);
        assert(h.owner.receive(state, 6 + i));
      }
      assert(h.owner.is_command_pending());
      auto result = h.result(ControlResult::CONTROL_RESULT_SERVICE_DISPATCHED);
      auto invalid = result;
      invalid.command_seq++;
      assert(!h.owner.receive(invalid, 8));
      invalid = result;
      invalid.state.state_seq++;
      assert(!h.owner.receive(invalid, 8));
      invalid = result;
      invalid.reason = ControlReason::CONTROL_REASON_SERVICE_ERROR;
      assert(!h.owner.receive(invalid, 8));
      invalid = result;
      invalid.identity.token.assign("cccccccccccccccccccccccccccccccc");
      assert(!h.owner.receive(invalid, 8));
      assert(h.owner.is_command_pending());
      assert(h.owner.receive(result, 8));
      assert(!h.owner.is_command_pending() && h.owner.last_result() == ControlResult::CONTROL_RESULT_SERVICE_DISPATCHED);
      assert(!h.owner.receive(result, 9) && h.sink.commands == 1);
      if (transitions == 1) {
        assert(h.owner.can_action(Op::CONTROL_OPERATION_ACTION_STOP));
        assert(h.owner.begin_action(2, Op::CONTROL_OPERATION_ACTION_STOP, 10) && h.owner.release_action(2, 11));
        assert(h.sink.commands == 2 && h.sink.last.panel_value == 0);
        auto stop = h.result(ControlResult::CONTROL_RESULT_SERVICE_DISPATCHED);
        assert(!h.owner.receive(stop, 12) && h.owner.is_command_pending());
        stop.result = ControlResult::CONTROL_RESULT_STATE_MATCHES;
        assert(h.owner.receive(stop, 13) && !h.owner.is_command_pending());
      }
    }
  }
  {
    Harness h;
    assert(h.bind() && h.open(0));
    h.run(false);
    for (auto status : {ControlResult::CONTROL_RESULT_SERVICE_COMPLETED, ControlResult::CONTROL_RESULT_STATE_MATCHES,
                        ControlResult::CONTROL_RESULT_ALREADY_MATCHED}) {
      auto result = h.result(status);
      assert(!h.owner.receive(result, 8) && h.owner.is_command_pending());
    }
    auto result = h.result(ControlResult::CONTROL_RESULT_SERVICE_DISPATCHED);
    assert(h.owner.receive(result, 9));
  }
  {
    Harness h;
    assert(h.bind() && h.open(Action::UNKNOWN_BOOL, false));
    assert(h.owner.begin_action(1, Op::CONTROL_OPERATION_ACTION_RUN, 4) && h.owner.release_action(1, 5));
    auto result = h.result(ControlResult::CONTROL_RESULT_SERVICE_DISPATCHED);
    assert(!h.owner.receive(result, 8) && h.owner.is_command_pending());
    result.result = ControlResult::CONTROL_RESULT_SERVICE_COMPLETED;
    assert(h.owner.receive(result, 9) && !h.owner.is_command_pending());
  }
  for (uint8_t value : {uint8_t(0), uint8_t(1)}) {
    Harness h;
    assert(h.bind() && h.open(value));
    assert(h.owner.action_parameter_value() == value && h.owner.action_parameter_is_default());
    h.run(value);
  }
  {
    Harness h;
    assert(h.bind() && h.open());
    assert(h.owner.action_parameter_value() == 255 && !h.owner.can_action(Op::CONTROL_OPERATION_ACTION_RUN));
    assert(h.owner.begin_action_parameter(3, false, 4) && h.owner.release_action_parameter(3, 5));
    assert(!h.owner.action_parameter_is_default() && h.sink.commands == 0);
    h.run(false);
  }
  {
    Harness h;
    assert(h.bind() && h.open(0));
    assert(h.owner.begin_action(1, Op::CONTROL_OPERATION_ACTION_RUN, 4));
    assert(h.owner.set_action_parameter(true, 5) && h.owner.set_action_parameter(false, 6));
    assert(!h.owner.release_action(1, 7) && h.sink.commands == 0);
  }
  {
    Harness h;
    assert(h.bind() && h.open(0));
    assert(h.owner.set_action_parameter(true, 4));
    auto m = h.next();
    m.state.family_if<Action>()->current = 7;
    m.state.family_if<Action>()->current_known = true;
    assert(h.owner.receive(m, 5) && h.owner.action_parameter_value() == 1);
    m = h.next(true);
    assert(h.owner.receive(m, 6));
    assert(h.owner.action_parameter_value() == 0 && h.owner.action_parameter_is_default());
    assert(!h.owner.can_action(Op::CONTROL_OPERATION_ACTION_RUN));
  }
  {
    Harness h;
    assert(h.bind() && h.open(1));
    assert(h.owner.begin_action(1, Op::CONTROL_OPERATION_ACTION_RUN, 4));
    h.owner.set_action_parameter_ready(false);
    assert(!h.owner.release_action(1, 5) && !h.owner.can_action(Op::CONTROL_OPERATION_ACTION_RUN));
    auto m = h.next(true);
    auto *a = m.state.family_if<Action>();
    a->phase = 2;
    a->eligible = control_action_eligible(*a);
    assert(h.owner.receive(m, 6));
    assert(h.owner.can_action(Op::CONTROL_OPERATION_ACTION_STOP));
    assert(h.owner.begin_action(2, Op::CONTROL_OPERATION_ACTION_STOP, 7) && h.owner.release_action(2, 8));
    assert(h.sink.last.panel_value == 0 && !h.sink.last.panel_enabled);
    char wire[2049];
    size_t size;
    assert(h.codec.serialize_entity(h.sink.last, wire, sizeof(wire), size));
    assert(std::strstr(wire, "parameter") == nullptr);
  }
  {
    Harness h;
    assert(h.bind() && h.open(0));
    assert(h.owner.set_action_parameter(true, 4));
    assert(h.owner.set_view_ready(false, 5));
    assert(h.owner.action_parameter_value() == 255);
    assert(h.owner.set_view_ready(true, 6));
    assert(h.owner.action_parameter_value() == 0);
    assert(!h.owner.can_action(Op::CONTROL_OPERATION_ACTION_RUN));
    assert(h.owner.disconnect() && !h.owner.action_parameters_bound() && h.owner.action_parameter_value() == 255);
  }
  {
    Harness h;
    assert(h.bind() && h.open());
    assert(h.owner.begin_action_parameter(1, true, 4));
    auto m = h.next(true);
    assert(h.owner.receive(m, 5));
    assert(!h.owner.release_action_parameter(1, 6) && h.sink.commands == 0);
  }
  {
    Harness h;
    assert(h.bind());
    assert(h.owner.accept_extra_features(h.channel, 24));
    assert(!h.owner.action_parameters_bound());
  }
  {
    Harness h;
    assert(h.bind() && h.open());
    assert(h.owner.set_action_parameter(true, 4));
    h.owner.set_action_parameter_ready(false);
    assert(h.owner.action_parameter_value() == 255);
    h.owner.set_action_parameter_ready(false);
    h.owner.set_action_parameter_ready(true);
    assert(!h.owner.can_action(Op::CONTROL_OPERATION_ACTION_RUN));
  }
  {
    Harness h;
    assert(h.bind() && h.open(0));
    h.owner.saturate_next_choice();
    assert(h.owner.set_action_parameter(true, 4));
    assert(!h.owner.can_action(Op::CONTROL_OPERATION_ACTION_RUN) && !h.owner.can_choose_action_parameter());
    assert(!h.owner.set_action_parameter(false, 5) && !h.owner.begin_action(1, Op::CONTROL_OPERATION_ACTION_RUN, 6));
  }
#endif
  std::printf("ActionParams cases=%u owner=%zu snapshot=%zu message=%zu action=%zu arena=%zu high_water=%zu PASS\n",
              checked, sizeof(MaterialEntityControl), sizeof(ControlSnapshot), sizeof(ControlMessage), sizeof(Action),
              sizeof(codec), codec.high_water());
}
