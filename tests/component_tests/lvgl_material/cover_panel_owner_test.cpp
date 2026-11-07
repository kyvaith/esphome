#include "material_entity_control.h"
#include "material_entity_control_codec.h"
#include <cassert>
#include <string>
using namespace esphome::lvgl_material;
using Op = ControlOperation;
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
int main() {
  Sink sink;
  MaterialEntityControl owner(sink);
  MaterialEntityControlCodec codec;
  ControlText<32> a, b;
  a.assign("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  b.assign("cccccccccccccccccccccccccccccccc");
  assert(owner.set_enabled(true) && owner.set_cover_enabled(true) && owner.bind_generic(a));
  assert(owner.set_cover_panel_enabled(true));
  assert(!owner.accept_cover_panel_features(a, 1));
  assert(owner.set_cover_priority_stop_enabled(true));
  assert(!owner.accept_cover_panel_features(a, 1));
  assert(owner.accept_cover_features(a, 1));
  assert(!owner.accept_cover_panel_features(b, 1) && !owner.accept_cover_panel_features(a, 2));
  assert(owner.accept_cover_panel_features(a, 1));
  ControlContext context;
  context.slot = 17;
  context.revision = 87;
  context.card_id.assign("blind");
  context.control_kind = ControlEntityKind::CONTROL_ENTITY_COVER;
  assert(owner.request_open(context, 1));
  ControlMessage value;
  value.generic = true;
  value.kind = ControlKind::CONTROL_KIND_GRANT;
  value.identity = sink.last.identity;
  value.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
  value.state.title.assign("Blind");
  value.state.state_seq = 1;
  value.state.emplace_family<ControlCoverSnapshot>();
  auto &cover = (*value.state.family_if<ControlCoverSnapshot>());
  cover.available = true;
  cover.state = ControlCoverState::CONTROL_COVER_STATE_OPEN;
  cover.state_generation = 1;
  cover.position_known = true;
  cover.position = 40;
  cover.caps = {true, true, true};
  cover.panel.present = true;
  cover.panel.device_class = 2;
  cover.panel.tilt_known = true;
  cover.panel.tilt_position = 30;
  cover.panel.caps = {true, true, true, true, true};
  assert(control_valid_cover_snapshot(value.state, true));
  assert(!control_valid_cover_snapshot(value.state));
  char buffer[2049];
  size_t length;
  ControlMessage parsed;
  assert(codec.serialize_entity(value, buffer, sizeof(buffer), length, false, true));
  std::string wire(buffer, length);
  assert(codec.parse_entity(wire, parsed, false, true) && parsed.state == value.state);
  assert(!codec.parse_entity(wire, parsed));
  for (const auto &needle : {std::string("\"version\":1"), std::string("\"set_position\":true")}) {
    auto bad = wire;
    bad.insert(bad.find(needle), needle + ",");
    assert(!codec.parse_entity(bad, parsed, false, true));
  }
  auto bad = wire;
  bad.replace(bad.find("\"tilt_position\":30"), 18, "\"tilt_position\":true");
  assert(!codec.parse_entity(bad, parsed, false, true));
  assert(owner.receive(value, 2) && owner.set_view_ready(true, 3));
  assert(!owner.accept_cover_panel_features(a, 1));
  assert(!owner.begin_cover(1, Op::CONTROL_OPERATION_COVER_SET_POSITION, 4));
  assert(owner.begin_cover_target(1, false, 40, 4));
  assert(owner.update_cover_target(1, 72, 5));
  const auto before = sink.count;
  assert(sink.count == before && owner.release_cover(1, 6));
  assert(sink.count == before + 1 && sink.last.cover_target_index == 72);
  assert(codec.serialize_entity(sink.last, buffer, sizeof(buffer), length, false, true));
  wire.assign(buffer, length);
  assert(wire.find("\"target_index\":72") != std::string::npos);
  assert(codec.parse_entity(wire, parsed, false, true));
  assert(!codec.parse_entity(wire, parsed));
  assert(owner.can_cover(Op::CONTROL_OPERATION_COVER_STOP));
  assert(!owner.can_cover(Op::CONTROL_OPERATION_COVER_STOP_TILT));
  assert(owner.begin_cover(2, Op::CONTROL_OPERATION_COVER_STOP, 7));
  value.kind = ControlKind::CONTROL_KIND_STATE;
  value.state.state_seq++;
  cover.state_generation++;
  cover.state = ControlCoverState::CONTROL_COVER_STATE_OPENING;
  cover.position = 45;
  assert(owner.receive(value, 8) && owner.is_dragging());
  assert(owner.release_cover(2, 9) && sink.last.expected_state_generation == 2);
  assert(!owner.can_cover(Op::CONTROL_OPERATION_COVER_STOP));
  ControlMessage result;
  result.generic = true;
  result.kind = ControlKind::CONTROL_KIND_RESULT;
  result.identity = value.identity;
  result.command_seq = owner.command_sequence();
  result.state.state_seq = value.state.state_seq;
  result.result = ControlResult::CONTROL_RESULT_STATE_MATCHES;
  assert(owner.receive(result, 10));
  // Tilt setters use the same single fence; a primary Stop cannot supersede them.
  assert(owner.begin_cover_target(3, true, 60, 11) && owner.release_cover(3, 12));
  assert(!owner.can_cover(Op::CONTROL_OPERATION_COVER_STOP));
  assert(owner.can_cover(Op::CONTROL_OPERATION_COVER_STOP_TILT));
  assert(owner.begin_cover(4, Op::CONTROL_OPERATION_COVER_STOP_TILT, 13));
  value.state.state_seq++;
  cover.state_generation++;
  cover.panel.tilt_position = 40;
  assert(owner.receive(value, 14) && owner.is_dragging());
  assert(owner.release_cover(4, 15));
  assert(!owner.can_cover(Op::CONTROL_OPERATION_COVER_STOP_TILT));
  result.command_seq = owner.command_sequence();
  result.state.state_seq = value.state.state_seq;
  assert(!owner.receive(result, 16));  // No independently observed tilt stopped signal in v1.
  result.result = ControlResult::CONTROL_RESULT_ALREADY_MATCHED;
  assert(!owner.receive(result, 17));
  result.result = ControlResult::CONTROL_RESULT_HA_ACCEPTED;
  assert(owner.receive(result, 18));
  result.result = ControlResult::CONTROL_RESULT_NOT_CONFIRMED;
  result.reason = ControlReason::CONTROL_REASON_STATE_TIMEOUT;
  assert(owner.receive(result, 19));
  // ABA retires an absolute-target contact; unchanged-generation heartbeat preserves it.
  assert(owner.begin_cover_target(5, false, 90, 20));
  value.state.state_seq++;
  assert(owner.receive(value, 21) && owner.is_dragging());
  value.state.state_seq++;
  cover.state_generation += 2;
  assert(owner.receive(value, 22));
  assert(!owner.release_cover(5, 23));
  cover.panel.tilt_known = false;
  cover.panel.tilt_position = 0;
  value.state.state_seq++;
  cover.state_generation++;
  assert(owner.receive(value, 24));
  assert(!owner.can_cover(Op::CONTROL_OPERATION_COVER_SET_TILT_POSITION));
  assert(owner.can_cover(Op::CONTROL_OPERATION_COVER_SET_POSITION));
  assert(owner.can_cover(Op::CONTROL_OPERATION_COVER_STOP_TILT));
  // Reconnect re-negotiates both features. Legacy all-false capabilities stay invalid.
  owner.disconnect();
  assert(owner.bind_generic(a) && !owner.cover_panel_active());
  assert(!owner.accept_cover_panel_features(a, 1));
  auto legacy = value;
  (*legacy.state.family_if<ControlCoverSnapshot>()).panel = {};
  assert(control_valid_cover_snapshot(legacy.state));
  assert(codec.serialize_entity(legacy, buffer, sizeof(buffer), length));
  assert(std::string(buffer, length).find("cover_panel") == std::string::npos);
  (*legacy.state.family_if<ControlCoverSnapshot>()).caps = {};
  assert(!control_valid_cover_snapshot(legacy.state));
  (*value.state.family_if<ControlCoverSnapshot>()).caps = {};
  assert(control_valid_cover_snapshot(value.state, true));
  (*value.state.family_if<ControlCoverSnapshot>()).panel.caps = {};
  assert(!control_valid_cover_snapshot(value.state, true));
}
