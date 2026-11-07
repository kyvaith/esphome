#include "material_entity_control.h"
#include "material_entity_control_codec.h"
#include <cassert>
#include <iostream>
#include <string>
using namespace esphome::lvgl_material;
using Op = ControlOperation;
using Result = ControlResult;
struct Sink : MaterialEntityControlSink {
  ControlMessage last;
  unsigned count = 0;
  bool emit(const ControlMessage &m) override {
    last = m;
    count++;
    return true;
  }
  void view_event(ControlViewEvent) override {}
};
struct Fixture {
  Sink sink;
  MaterialEntityControl owner{sink};
  MaterialEntityControlCodec codec;
  ControlMessage state;
  uint32_t now = 1;
  ControlText<32> channel;
  Fixture(uint8_t type = 0) {
    channel.assign("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    assert(owner.set_enabled(true));
    assert(owner.bind_generic(channel));
    assert(owner.set_panel_types(7));
    ControlContext context;
    context.slot = 31;
    context.revision = 348;
    context.card_id.assign("panel-31");
    context.control_kind = ControlEntityKind::CONTROL_ENTITY_PANEL;
    assert(!owner.request_open(context, now++));
    assert(owner.accept_panel_features(channel, 1, 1U << type));
    assert(owner.request_open(context, now++));
    state.generic = true;
    state.kind = ControlKind::CONTROL_KIND_GRANT;
    state.identity = sink.last.identity;
    state.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    state.state.state_seq = 1;
    state.state.title.assign("Panel");
    state.state.emplace_family<ControlSnapshot::Panel>();
    auto &p = (*state.state.family_if<ControlSnapshot::Panel>());
    p.type = type;
    p.state_generation = 1;
    p.available = true;
    if (type == 0) {
      p.phase = 1;
      p.caps.start = p.caps.pause = p.caps.cancel = true;
      p.caps.initial_duration = 120;
      p.duration = 120;
      p.eligible_actions = 17;
      p.sampled_at = 1793790000;
    } else {
      p.power = 1;
      p.caps.turn_on = p.caps.turn_off = true;
      p.eligible_actions = 1;
      if (type == 1) {
        p.caps.activity_count = 2;
        p.caps.activities[0].assign("Music");
        p.caps.activities[1].assign("Cinema");
        p.eligible_actions |= 4;
      }
    }
    assert(control_valid_panel_snapshot(state.state));
    assert(owner.receive(state, now++));
    assert(owner.set_view_ready(true, now++));
    state.kind = ControlKind::CONTROL_KIND_STATE;
  }
  void send(Op op, uint32_t value = 0) {
    assert(owner.begin_panel(1, op, value, now++));
    assert(owner.release_panel(1, now++));
  }
  bool result(Result value) {
    ControlMessage m;
    m.generic = true;
    m.kind = ControlKind::CONTROL_KIND_RESULT;
    m.identity = state.identity;
    m.command_seq = owner.command_sequence();
    m.state.state_seq = state.state.state_seq;
    m.result = value;
    return owner.receive(m, now++);
  }
  void update() {
    state.state.state_seq++;
    assert(owner.receive(state, now++));
  }
};
int main() {
  for (uint8_t type = 0; type < 3; type++) {
    Fixture f(type);
    char buffer[2049];
    size_t size;
    ControlMessage parsed;
    assert(f.owner.is_panel() && f.owner.is_discrete());
    assert(f.codec.serialize_entity(f.state, buffer, sizeof(buffer), size));
    assert(f.codec.parse_entity({buffer, size}, parsed));
    assert(parsed.state == f.state.state);
    auto wire = std::string(buffer, size);
    wire.insert(wire.find("\"available\":"), "\"available\":true,");
    assert(!f.codec.parse_entity(wire, parsed));
    assert(!f.owner.accept_panel_features(f.channel, 1, 7));
    if (type == 0) {
      assert(!f.owner.can_panel(Op::CONTROL_OPERATION_PANEL_POWER, 1));
      assert(!f.owner.can_panel(Op::CONTROL_OPERATION_PANEL_DURATION, 0));
      f.send(Op::CONTROL_OPERATION_PANEL_DURATION, 120);
      assert(!f.result(Result::CONTROL_RESULT_STATE_MATCHES));
      assert(!f.result(Result::CONTROL_RESULT_ALREADY_MATCHED));
      assert(f.result(Result::CONTROL_RESULT_SERVICE_COMPLETED));
      f.send(Op::CONTROL_OPERATION_PANEL_DURATION, 120);
      assert(f.result(Result::CONTROL_RESULT_SERVICE_COMPLETED));
      f.send(Op::CONTROL_OPERATION_PANEL_START);
      assert(!f.result(Result::CONTROL_RESULT_SERVICE_COMPLETED));
      assert(f.result(Result::CONTROL_RESULT_STATE_MATCHES));
      auto &p = (*f.state.state.family_if<ControlSnapshot::Panel>());
      p.phase = 2;
      p.finishes_at = p.sampled_at + 120;
      p.remaining = 120;
      p.eligible_actions = 28;
      p.state_generation++;
      f.update();
      assert(f.owner.begin_panel(2, Op::CONTROL_OPERATION_PANEL_DURATION, 180, f.now++));
      p.sampled_at++;
      p.remaining--;
      f.update();
      assert(f.owner.is_dragging());
      assert(f.owner.release_panel(2, f.now++));
      assert(f.result(Result::CONTROL_RESULT_SERVICE_COMPLETED));
      assert(f.owner.begin_panel(3, Op::CONTROL_OPERATION_PANEL_PAUSE, 0, f.now++));
      p.state_generation += 2;
      f.update();
      assert(!f.owner.release_panel(3, f.now++));
      uint32_t remaining;
      bool sample;
      assert(panel_timer_remaining(p, true, p.sampled_at + 10, remaining, sample) && remaining == 109 && !sample);
      assert(panel_timer_remaining(p, true, p.sampled_at + 33, remaining, sample) && remaining == 119 && sample);
      assert(panel_timer_remaining(p, false, p.sampled_at, remaining, sample) && remaining == 119 && sample);
      p.finishes_at = p.sampled_at;
      p.remaining = 0;
      assert(panel_timer_remaining(p, true, p.sampled_at + 2, remaining, sample) && remaining == 0 && !sample);
      assert(p.phase == 2);
      p.duration = 172800;
      assert(control_valid_panel_snapshot(f.state.state));
      p.remaining = 1;
      assert(!control_valid_panel_snapshot(f.state.state));
    } else {
      assert(!f.owner.can_panel(Op::CONTROL_OPERATION_PANEL_START));
      f.send(Op::CONTROL_OPERATION_PANEL_POWER, 1);
      assert(!f.result(Result::CONTROL_RESULT_SERVICE_COMPLETED));
      assert(f.result(Result::CONTROL_RESULT_STATE_MATCHES));
      if (type == 1) {
        f.send(Op::CONTROL_OPERATION_PANEL_ACTIVITY, 1);
        assert(f.result(Result::CONTROL_RESULT_STATE_MATCHES));
        (*f.state.state.family_if<ControlSnapshot::Panel>()).power = 2;
        (*f.state.state.family_if<ControlSnapshot::Panel>()).eligible_actions = 2;
        (*f.state.state.family_if<ControlSnapshot::Panel>()).state_generation++;
        f.update();
        assert(!f.owner.can_panel(Op::CONTROL_OPERATION_PANEL_ACTIVITY, 1));
        assert(f.owner.can_panel(Op::CONTROL_OPERATION_PANEL_POWER, 0));
      }
    }
    assert(f.owner.bind_generic(f.channel));
    assert(f.owner.bound_panel_types() == 0);
    assert(!f.owner.has_selection());
  }
  {
    Fixture f;
    ControlText<32> channel;
    uint8_t types;
    const std::string good =
        R"({"v":1,"type":"entity_panel_features_v1","panel_version":1,"channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","panel_types":["timer","remote","power"]})";
    assert(f.codec.parse_panel_bind(good, channel, types) && types == 7 && channel == f.channel);
    for (
        auto bad :
        {good + std::string(513, ' '),
         std::string(
             R"({"v":1,"v":1,"type":"entity_panel_features_v1","panel_version":1,"channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","panel_types":["timer"]})"),
         std::string(
             R"({"v":1,"type":"entity_panel_features_v1","panel_version":true,"channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","panel_types":["timer"]})"),
         std::string(
             R"({"v":1,"type":"entity_panel_features_v1","panel_version":1,"channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","panel_types":["remote","timer"]})")})
      assert(!f.codec.parse_panel_bind(bad, channel, types));
  }
  {
    Fixture f(1);
    auto &p = (*f.state.state.family_if<ControlSnapshot::Panel>());
    p.caps.activity_count = 8;
    f.state.state.title.assign(std::string(160, '"'));
    f.state.identity.context.card_id.assign(std::string(64, 'x'));
    f.state.identity.context.revision = CONTROL_MAX_REVISION;
    f.state.identity.open_seq = f.state.state.state_seq = p.state_generation = CONTROL_MAX_SEQUENCE;
    for (unsigned i = 0; i < 8; i++)
      p.caps.activities[i].assign(std::string(31, '"') + char('a' + i));
    char buffer[2049];
    size_t size;
    ControlMessage parsed;
    assert(f.codec.serialize_entity(f.state, buffer, sizeof(buffer), size));
    assert(f.codec.parse_entity({buffer, size}, parsed));
    assert(size <= 2048 && f.codec.high_water() <= 5120);
    std::cout << "PANEL max_wire=" << size << " arena=" << f.codec.high_water() << "\n";
    for (unsigned op = 0; op < 7; op++) {
      auto m = f.state;
      m.kind = ControlKind::CONTROL_KIND_COMMAND;
      m.command_seq = 1;
      m.expected_state_generation = 1;
      m.panel_value = op == 4 ? 86400 : 1;
      m.operation = static_cast<Op>(static_cast<uint8_t>(Op::CONTROL_OPERATION_PANEL_START) + op);
      assert(f.codec.serialize_entity(m, buffer, sizeof(buffer), size));
      assert(f.codec.parse_entity({buffer, size}, parsed));
      assert(parsed.operation == m.operation);
    }
    auto m = f.state;
    m.kind = ControlKind::CONTROL_KIND_RESULT;
    m.command_seq = 1;
    m.result = Result::CONTROL_RESULT_SERVICE_COMPLETED;
    assert(f.codec.serialize_entity(m, buffer, sizeof(buffer), size));
    assert(f.codec.parse_entity({buffer, size}, parsed));
    m.identity.context.control_kind = ControlEntityKind::CONTROL_ENTITY_LIGHT;
    assert(!f.codec.serialize_entity(m, buffer, sizeof(buffer), size));
  }
  std::cout << "PANEL owner pass=1\n";
}
