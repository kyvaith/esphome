#include "material_entity_control_codec.h"
#include "action_owner_fixtures.h"
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
struct Harness {
  Sink sink;
  MaterialEntityControl owner{sink};
  MaterialEntityControlCodec codec;
  ControlText<32> channel;
  Harness(uint8_t offered = 8) {
    channel.assign("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    assert(owner.set_enabled(true) && owner.set_extra_profiles(31) && owner.bind_generic(channel));
    if (offered)
      assert(owner.accept_extra_features(channel, offered));
  }
  ControlMessage parse(const char *name) {
    const char *wire = nullptr;
    for (const auto &fixture : fixtures)
      if (std::strcmp(name, fixture.name) == 0)
        wire = fixture.wire;
    assert(wire);
    ControlMessage m;
    assert(codec.parse_entity(wire, m));
    return m;
  }
  bool open(const char *name) {
    auto m = parse(name);
    if (!owner.request_open(m.identity.context, 1))
      return false;
    m.kind = ControlKind::CONTROL_KIND_GRANT;
    m.identity = owner.identity();
    m.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    if (!owner.receive(m, 2))
      return false;
    assert(owner.set_view_ready(true, 3));
    assert(owner.is_action() && owner.is_discrete() && !owner.is_extra() && !owner.is_panel() && !owner.is_media());
    return true;
  }
  ControlMessage next() {
    ControlMessage m;
    m.generic = true;
    m.kind = ControlKind::CONTROL_KIND_STATE;
    m.identity = owner.identity();
    m.state = owner.state();
    m.state.state_seq++;
    return m;
  }
  bool state(ControlMessage &m) {
    auto *a = m.state.family_if<Action>();
    a->eligible = control_action_eligible(*a);
    return owner.receive(m, 6);
  }
  bool result(ControlResult r) {
    ControlMessage m;
    m.generic = true;
    m.kind = ControlKind::CONTROL_KIND_RESULT;
    m.identity = owner.identity();
    m.command_seq = sink.last.command_seq;
    m.state.state_seq = owner.state().state_seq;
    m.result = r;
    return owner.receive(m, 8);
  }
  void command(Op op) {
    assert(owner.begin_action(5, op, 4));
    assert(owner.release_action(5, 5));
    assert(sink.commands == 1);
    char bytes[2049];
    size_t size;
    assert(codec.serialize_entity(sink.last, bytes, sizeof(bytes), size));
    ControlMessage decoded;
    assert(codec.parse_entity({bytes, size}, decoded));
    assert(decoded.operation == op &&
           decoded.expected_state_generation == owner.state().family_if<Action>()->state_generation &&
           decoded.identity == owner.identity());
  }
};
int main() {
  unsigned cases = 0;
  {
    Harness h;
    ControlText<32> channel;
    uint16_t mask = 0;
    assert(
        h.codec.parse_extra_features(
            R"({"v":1,"type":"entity_panel_extra_v1","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","profiles":[{"name":"action","version":1}]})",
            channel, mask, 15, true) &&
        mask == 8);
    assert(!h.owner.set_extra_profiles(16));
    assert(!h.owner.accept_extra_features(h.channel, 16));
    cases++;
  }
  for (bool bind : {false, true}) {
    Harness h;
    ControlText<32> channel;
    uint16_t mask = 0;
    const char *v2 =
        R"({"v":1,"type":"entity_panel_extra_v1","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","profiles":[{"name":"action","version":2},{"name":"info","version":1},{"name":"lock","version":1},{"name":"media","version":1}]})";
    assert(h.codec.parse_extra_features(v2, channel, mask, 31, bind) && mask == 31);
    assert(h.codec.parse_extra_features(v2, channel, mask, 15, bind) == !bind);
    if (!bind)
      assert(mask == 7);
    cases++;
  }
  for (uint8_t offered : {0, 1, 4}) {
    Harness h(offered);
    assert(!h.open("button-first-use.json"));
    cases++;
  }
  for (uint8_t offered : {8, 24}) {
    Harness h(offered);
    assert(h.open("automation-enabled.json"));
    h.command(Op::CONTROL_OPERATION_ACTION_TRIGGER);
    assert(!h.result(ControlResult::CONTROL_RESULT_STATE_MATCHES));
    assert(h.result(ControlResult::CONTROL_RESULT_SERVICE_DISPATCHED) == (offered == 24));
    assert(h.owner.is_command_pending() == (offered == 8));
    if (offered == 8)
      assert(h.result(ControlResult::CONTROL_RESULT_SERVICE_COMPLETED));
    else {
      assert(!h.result(ControlResult::CONTROL_RESULT_SERVICE_DISPATCHED));
      assert(h.owner.can_action(Op::CONTROL_OPERATION_ACTION_DISABLE));
      assert(h.owner.begin_action(6, Op::CONTROL_OPERATION_ACTION_DISABLE, 9));
      assert(h.owner.release_action(6, 10));
      assert(h.sink.commands == 2);
      assert(!h.result(ControlResult::CONTROL_RESULT_SERVICE_DISPATCHED));
    }
    cases++;
  }
  {
    Harness h(24);
    assert(h.open("automation-enabled.json"));
    assert(!h.result(ControlResult::CONTROL_RESULT_SERVICE_DISPATCHED));
    h.command(Op::CONTROL_OPERATION_ACTION_TRIGGER);
    assert(!h.result(ControlResult::CONTROL_RESULT_SERVICE_COMPLETED));
    ControlMessage m;
    m.generic = true;
    m.kind = ControlKind::CONTROL_KIND_RESULT;
    m.identity = h.owner.identity();
    m.command_seq = h.sink.last.command_seq;
    m.state.state_seq = h.owner.state().state_seq + 1;
    m.result = ControlResult::CONTROL_RESULT_SERVICE_DISPATCHED;
    assert(!h.owner.receive(m, 8));
    m.state.state_seq--;
    m.command_seq++;
    assert(!h.owner.receive(m, 8));
    m.command_seq--;
    m.reason = ControlReason::CONTROL_REASON_SERVICE_ERROR;
    assert(!h.owner.receive(m, 8));
    m.reason = ControlReason::CONTROL_REASON_NONE;
    assert(h.owner.receive(m, 8));
    auto state = h.next();
    state.state.family_if<Action>()->current = 1;
    assert(h.state(state));
    assert(h.owner.last_result() == ControlResult::CONTROL_RESULT_SERVICE_DISPATCHED);
    assert(!h.owner.is_command_pending());
    cases++;
  }
  for (const char *name : {"button-first-use.json", "scene-invalid-timestamp.json", "script-idle.json",
                           "script-running.json", "automation-disabled-stop-request.json"}) {
    Harness h(24);
    assert(h.open(name));
    const auto *a = h.owner.state().family_if<Action>();
    const auto op = a->type <= 1    ? Op::CONTROL_OPERATION_ACTION_PRESS
                    : a->type == 2  ? Op::CONTROL_OPERATION_ACTION_ACTIVATE
                    : a->type == 4  ? Op::CONTROL_OPERATION_ACTION_DISABLE
                    : a->phase == 2 ? Op::CONTROL_OPERATION_ACTION_STOP
                                    : Op::CONTROL_OPERATION_ACTION_RUN;
    h.command(op);
    assert(!h.result(ControlResult::CONTROL_RESULT_SERVICE_DISPATCHED));
    assert(h.owner.is_command_pending());
    cases++;
  }
  for (const auto *name :
       {"button-first-use.json", "scene-invalid-timestamp.json", "script-idle.json", "automation-enabled.json",
        "automation-disabled-stop-request.json", "automation-disabled-stop-unknown-count.json"}) {
    Harness h;
    assert(h.open(name));
    auto *a = h.owner.state().family_if<Action>();
    const auto op = a->type <= 1           ? Op::CONTROL_OPERATION_ACTION_PRESS
                    : a->type == 2         ? Op::CONTROL_OPERATION_ACTION_ACTIVATE
                    : a->type == 3         ? Op::CONTROL_OPERATION_ACTION_RUN
                    : a->stop_actions == 1 ? Op::CONTROL_OPERATION_ACTION_DISABLE
                                           : Op::CONTROL_OPERATION_ACTION_TRIGGER;
    h.command(op);
    assert(!h.result(ControlResult::CONTROL_RESULT_ALREADY_MATCHED));
    assert(!h.result(ControlResult::CONTROL_RESULT_STATE_MATCHES));
    assert(h.owner.is_command_pending());
    assert(h.result(ControlResult::CONTROL_RESULT_SERVICE_COMPLETED));
    assert(!h.owner.is_command_pending());
    cases++;
  }
  {
    Harness h;
    assert(h.open("button-first-use.json"));
    assert(h.owner.begin_action(9, Op::CONTROL_OPERATION_ACTION_PRESS, 4));
    auto m = h.next();
    auto *a = m.state.family_if<Action>();
    a->last_used_status = 0;
    a->last_used.assign("2026-11-05T12:00:00+01:00");
    assert(h.state(m));
    assert(h.owner.is_dragging());
    assert(h.owner.release_action(9, 7));
    assert(h.sink.commands == 1);
    cases++;
  }
  for (bool aba : {false, true}) {
    Harness h;
    assert(h.open("script-idle.json"));
    assert(h.owner.begin_action(9, Op::CONTROL_OPERATION_ACTION_RUN, 4));
    auto m = h.next();
    auto *a = m.state.family_if<Action>();
    a->state_generation++;
    if (!aba)
      a->phase = 2;
    assert(h.state(m));
    assert(!h.owner.is_dragging());
    assert(!h.owner.release_action(9, 7));
    assert(h.sink.commands == 0);
    cases++;
  }
  {
    Harness h;
    assert(h.open("script-idle.json"));
    assert(h.owner.begin_action(9, Op::CONTROL_OPERATION_ACTION_RUN, 4));
    auto m = h.next();
    m.state.family_if<Action>()->run_blocked = 1;
    assert(!h.state(m));
    assert(!h.owner.has_selection());
    assert(!h.owner.release_action(9, 7));
    assert(h.sink.commands == 0);
    cases++;
  }
  {
    Harness h;
    assert(h.open("automation-enabled.json"));
    assert(h.owner.begin_action(9, Op::CONTROL_OPERATION_ACTION_TRIGGER, 4));
    auto m = h.next();
    auto *a = m.state.family_if<Action>();
    a->source = 1;
    a->phase = 5;
    a->last_used_status = 1;
    a->last_used.assign("");
    a->current_known = false;
    a->current = 0;
    a->state_generation++;
    assert(h.state(m));
    assert(!h.owner.release_action(9, 7));
    assert(h.sink.commands == 0);
    cases++;
  }
  for (const char *name : {"script-required.json", "script-metadata-unknown.json", "script-no-run-service.json",
                           "script-unknown.json", "button-unavailable.json"}) {
    Harness h;
    assert(h.open(name));
    auto *a = h.owner.state().family_if<Action>();
    assert(!h.owner.can_action(a->type == 3 ? Op::CONTROL_OPERATION_ACTION_RUN : Op::CONTROL_OPERATION_ACTION_PRESS));
    cases++;
  }
  {
    Harness h;
    assert(h.open("script-running.json"));
    h.command(Op::CONTROL_OPERATION_ACTION_STOP);
    assert(!h.result(ControlResult::CONTROL_RESULT_SERVICE_COMPLETED));
    auto m = h.next();
    auto *a = m.state.family_if<Action>();
    a->phase = 1;
    a->state_generation++;
    assert(h.state(m));
    assert(h.result(ControlResult::CONTROL_RESULT_STATE_MATCHES));
    cases++;
  }
  {
    Harness h;
    assert(h.open("button-first-use.json"));
    h.command(Op::CONTROL_OPERATION_ACTION_PRESS);
    ControlMessage close;
    close.generic = true;
    close.kind = ControlKind::CONTROL_KIND_CLOSE;
    close.identity = h.owner.identity();
    close.reason = ControlReason::CONTROL_REASON_ENTITY_UNAVAILABLE;
    assert(h.owner.receive(close, 7));
    assert(!h.result(ControlResult::CONTROL_RESULT_SERVICE_COMPLETED));
    assert(!h.owner.has_selection());
    cases++;
  }
  std::printf("PASS action_owner cases=%u owner=%zu snapshot=%zu message=%zu action=%zu\n", cases,
              sizeof(MaterialEntityControl), sizeof(ControlSnapshot), sizeof(ControlMessage), sizeof(Action));
}
