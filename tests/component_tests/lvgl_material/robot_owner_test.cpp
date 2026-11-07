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
  unsigned count{0};
  bool emit(const ControlMessage &message) override {
    last = message;
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
  uint32_t now{1};
  Fixture(bool mower = false) {
    ControlText<32> channel;
    channel.assign("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    assert(owner.set_enabled(true));
    assert(owner.bind_generic(channel));
    ControlContext context;
    context.slot = 31;
    context.revision = 348;
    context.card_id.assign("robot-31");
    context.control_kind = ControlEntityKind::CONTROL_ENTITY_ROBOT;
    assert(!owner.request_open(context, now++));
    assert(owner.set_robot_enabled(true));
    assert(owner.request_open(context, now++));
    state.generic = true;
    state.kind = ControlKind::CONTROL_KIND_GRANT;
    state.identity = sink.last.identity;
    state.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    state.state.title.assign("Robot");
    state.state.state_seq = 1;
    state.state.emplace_family<ControlSnapshot::Robot>();
    auto &r = (*state.state.family_if<ControlSnapshot::Robot>());
    r.type = mower ? 1 : 0;
    r.activity = 2;
    r.available = true;
    r.state_generation = 1;
    r.caps.actions = mower ? 11 : 63;
    r.eligible_actions = mower ? 1 : 49;
    r.battery_percent = 73;
    if (!mower) {
      r.caps.speed_count = 2;
      r.caps.speeds[0].assign("Quiet");
      r.caps.speeds[1].assign("Turbo");
      r.speed_index = 0;
      r.caps.area_count = 2;
      r.caps.areas[0].assign("Kitchen");
      r.caps.areas[1].assign("Study");
      r.area_enabled = true;
    }
    assert(control_valid_robot_snapshot(state.state));
    assert(owner.receive(state, now++));
    assert(owner.set_view_ready(true, now++));
    state.kind = ControlKind::CONTROL_KIND_STATE;
  }
  void update(uint8_t activity, uint8_t eligible, bool area = false) {
    state.state.state_seq++;
    (*state.state.family_if<ControlSnapshot::Robot>()).state_generation++;
    (*state.state.family_if<ControlSnapshot::Robot>()).activity = activity;
    (*state.state.family_if<ControlSnapshot::Robot>()).eligible_actions = eligible;
    (*state.state.family_if<ControlSnapshot::Robot>()).area_enabled = area;
    (*state.state.family_if<ControlSnapshot::Robot>()).available = activity != 0;
    assert(owner.receive(state, now++));
  }
  bool result(Result status) {
    ControlMessage m;
    m.generic = true;
    m.kind = ControlKind::CONTROL_KIND_RESULT;
    m.identity = state.identity;
    m.command_seq = owner.command_sequence();
    m.state.state_seq = state.state.state_seq;
    m.result = status;
    return owner.receive(m, now++);
  }
  void send(Op op, uint8_t index = 0) {
    assert(owner.begin_robot(1, op, index, now++));
    assert(owner.release_robot(1, now++));
  }
};
int main() {
  {
    Fixture f;
    assert(f.owner.is_robot() && f.owner.is_discrete());
    assert(f.owner.begin_robot(1, Op::CONTROL_OPERATION_ROBOT_START, 0, f.now++));
    f.state.state.state_seq++;
    (*f.state.state.family_if<ControlSnapshot::Robot>()).battery_percent = 72;
    assert(f.owner.receive(f.state, f.now++));
    assert(f.owner.is_dragging());
    assert(f.owner.release_robot(1, f.now++));
    assert(f.sink.last.operation == Op::CONTROL_OPERATION_ROBOT_START);
    assert(!f.result(Result::CONTROL_RESULT_SERVICE_COMPLETED));
    assert(f.result(Result::CONTROL_RESULT_STATE_MATCHES));
    assert(!f.owner.release_robot(1, f.now++));
    assert(f.owner.begin_robot(2, Op::CONTROL_OPERATION_ROBOT_START, 0, f.now++));
    f.state.state.state_seq++;
    (*f.state.state.family_if<ControlSnapshot::Robot>()).state_generation += 2;
    assert(f.owner.receive(f.state, f.now++));
    auto count = f.sink.count;
    assert(!f.owner.release_robot(2, f.now++));
    assert(f.sink.count == count);
    assert(f.owner.can_robot(Op::CONTROL_OPERATION_ROBOT_LOCATE));
    f.send(Op::CONTROL_OPERATION_ROBOT_LOCATE);
    assert(!f.result(Result::CONTROL_RESULT_STATE_MATCHES));
    assert(!f.result(Result::CONTROL_RESULT_ALREADY_MATCHED));
    assert(f.result(Result::CONTROL_RESULT_HA_ACCEPTED));
    assert(f.owner.is_command_pending());
    assert(f.result(Result::CONTROL_RESULT_SERVICE_COMPLETED));
    assert(!f.owner.is_command_pending());
    f.send(Op::CONTROL_OPERATION_ROBOT_AREA, 1);
    assert(f.sink.last.robot_target_index == 1);
    assert(!f.result(Result::CONTROL_RESULT_STATE_MATCHES));
    assert(f.result(Result::CONTROL_RESULT_SERVICE_COMPLETED));
    f.send(Op::CONTROL_OPERATION_ROBOT_SPEED, 1);
    assert(f.sink.last.expected_state_generation == 3);
    assert(f.result(Result::CONTROL_RESULT_STATE_MATCHES));
    f.state.state.state_seq++;
    (*f.state.state.family_if<ControlSnapshot::Robot>()).speed_index = 255;
    (*f.state.state.family_if<ControlSnapshot::Robot>()).state_generation++;
    assert(f.owner.receive(f.state, f.now++));
    assert(!f.owner.can_robot(Op::CONTROL_OPERATION_ROBOT_SPEED, 1));
    assert(f.owner.can_robot(Op::CONTROL_OPERATION_ROBOT_START));
  }
  {
    Fixture f;
    f.send(Op::CONTROL_OPERATION_ROBOT_START);
    assert(!f.owner.can_robot(Op::CONTROL_OPERATION_ROBOT_STOP));
    f.update(1, 30);
    assert(f.owner.can_robot(Op::CONTROL_OPERATION_ROBOT_PAUSE));
    assert(f.owner.can_robot(Op::CONTROL_OPERATION_ROBOT_STOP));
    assert(f.owner.begin_robot(2, Op::CONTROL_OPERATION_ROBOT_STOP, 0, f.now++));
    f.update(5, 22);
    assert(f.owner.is_dragging());
    assert(f.owner.release_robot(2, f.now++));
    assert(f.sink.last.operation == Op::CONTROL_OPERATION_ROBOT_STOP && f.sink.last.expected_state_generation == 3);
    assert(!f.owner.can_robot(Op::CONTROL_OPERATION_ROBOT_PAUSE));
    assert(!f.owner.can_robot(Op::CONTROL_OPERATION_ROBOT_STOP));
    f.owner.tick(f.now + 14000);
    assert(f.owner.is_command_pending());
    assert(f.result(Result::CONTROL_RESULT_HA_ACCEPTED));
    assert(f.result(Result::CONTROL_RESULT_STATE_MATCHES));
  }
  {
    Fixture f(true);
    assert(!f.owner.can_robot(Op::CONTROL_OPERATION_ROBOT_STOP));
    f.send(Op::CONTROL_OPERATION_ROBOT_START);
    f.update(7, 10);
    f.send(Op::CONTROL_OPERATION_ROBOT_PAUSE);
    assert(!f.owner.can_robot(Op::CONTROL_OPERATION_ROBOT_PAUSE));
    assert(f.result(Result::CONTROL_RESULT_STATE_MATCHES));
    (*f.state.state.family_if<ControlSnapshot::Robot>()).caps.actions |= 4;
    assert(!control_valid_robot_snapshot(f.state.state));
  }
  {
    Fixture f;
    f.send(Op::CONTROL_OPERATION_ROBOT_AREA, 0);
    f.update(1, 30);
    f.send(Op::CONTROL_OPERATION_ROBOT_PAUSE);
    assert(!f.owner.can_robot(Op::CONTROL_OPERATION_ROBOT_STOP));
    assert(f.result(Result::CONTROL_RESULT_STATE_MATCHES));
    f.update(2, 49, true);
    assert(f.owner.begin_robot(3, Op::CONTROL_OPERATION_ROBOT_START, 0, f.now++));
    f.state.state.state_seq++;
    (*f.state.state.family_if<ControlSnapshot::Robot>()).caps.areas[0].assign("Hall");
    assert(!f.owner.receive(f.state, f.now++));
    assert(!f.owner.has_selection());
    assert(!f.owner.release_robot(3, f.now++));
  }
  {
    Fixture f;
    char buffer[2049];
    size_t length;
    ControlMessage parsed;
    assert(f.codec.serialize_entity(f.state, buffer, sizeof(buffer), length));
    std::string wire(buffer, length);
    assert(f.codec.parse_entity(wire, parsed));
    assert(parsed.state == f.state.state);
    auto duplicate = wire;
    duplicate.insert(duplicate.find("\"actions\":"), "\"actions\":63,");
    assert(!f.codec.parse_entity(duplicate, parsed));
    auto invalid = wire;
    invalid.replace(invalid.find("\"battery_percent\":73"), 20, "\"battery_percent\":true");
    assert(!f.codec.parse_entity(invalid, parsed));
    for (unsigned op = 0; op < 8; op++) {
      ControlMessage command;
      command.generic = true;
      command.kind = ControlKind::CONTROL_KIND_COMMAND;
      command.identity = f.state.identity;
      command.command_seq = 1;
      command.expected_state_generation = 1;
      command.robot_target_index = 1;
      command.operation = static_cast<Op>(static_cast<uint8_t>(Op::CONTROL_OPERATION_ROBOT_START) + op);
      assert(f.codec.serialize_entity(command, buffer, sizeof(buffer), length));
      assert(f.codec.parse_entity({buffer, length}, parsed));
      assert(parsed.operation == command.operation);
      assert(op < 6 || parsed.robot_target_index == 1);
      std::string bad(buffer, length);
      bad.insert(bad.size() - 1, op < 6 ? ",\"target_index\":0" : ",\"entity_id\":\"vacuum.other\"");
      assert(!f.codec.parse_entity(bad, parsed));
    }
    ControlMessage result;
    result.generic = true;
    result.kind = ControlKind::CONTROL_KIND_RESULT;
    result.identity = f.state.identity;
    result.command_seq = 1;
    result.state.state_seq = 1;
    result.result = Result::CONTROL_RESULT_SERVICE_COMPLETED;
    assert(f.codec.serialize_entity(result, buffer, sizeof(buffer), length));
    assert(f.codec.parse_entity({buffer, length}, parsed));
    std::string bad(buffer, length);
    bad.replace(bad.find("\"robot\""), 7, "\"light\"");
    assert(!f.codec.parse_entity(bad, parsed));
    result.identity.context.control_kind = ControlEntityKind::CONTROL_ENTITY_LIGHT;
    assert(!f.codec.serialize_entity(result, buffer, sizeof(buffer), length));
    auto &r = (*f.state.state.family_if<ControlSnapshot::Robot>());
    r.caps.speed_count = r.caps.area_count = 8;
    f.state.state.state_seq = r.state_generation = f.state.identity.open_seq = CONTROL_MAX_SEQUENCE;
    f.state.identity.context.revision = CONTROL_MAX_REVISION;
    f.state.state.title.assign(std::string(160, '"'));
    f.state.identity.context.card_id.assign(std::string(64, 'x'));
    for (unsigned i = 0; i < 8; i++) {
      r.caps.speeds[i].assign(std::string(31, '"') + char('a' + i));
      r.caps.areas[i].assign(std::string(31, '\\') + char('a' + i));
    }
    assert(f.codec.serialize_entity(f.state, buffer, sizeof(buffer), length));
    assert(f.codec.parse_entity({buffer, length}, parsed));
    assert(length <= 2048 && f.codec.high_water() <= 5120);
    std::cout << "ROBOT max_wire=" << length << " arena=" << f.codec.high_water() << "\n";
    r.caps.areas[1] = r.caps.areas[0];
    assert(!control_valid_robot_snapshot(f.state.state));
    r.caps.areas[1].assign("unknown");
    assert(!control_valid_robot_snapshot(f.state.state));
    r.caps.areas[1].assign("unavailable");
    assert(!control_valid_robot_snapshot(f.state.state));
    r.caps.areas[1].assign("   ");
    assert(control_valid_robot_snapshot(f.state.state));
  }
  std::cout << "ROBOT owner pass=1\n";
}
