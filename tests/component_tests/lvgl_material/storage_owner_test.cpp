#ifdef NDEBUG
#undef NDEBUG
#endif
#include "material_entity_control.h"
#include <cassert>
#include <iostream>
using namespace esphome::lvgl_material;
using Kind = ControlEntityKind;
using Op = ControlOperation;
struct Sink : MaterialEntityControlSink {
  MaterialEntityControl *owner{};
  ControlMessage last;
  unsigned count{};
  bool emit(const ControlMessage &message) override {
    last = message;
    ++count;
    // Every outgoing message must remain borrowed only synchronously; reentry
    // cannot replace its owner's retained storage during serialization/callback.
    assert(!owner->disconnect());
    assert(!owner->request_open(message.identity.context, 1));
    return true;
  }
  void view_event(ControlViewEvent) override { assert(!owner->disconnect()); }
};
struct Source {
  Kind kind;
  bool detail;
  ControlSnapshot state;
};
Source source(unsigned index) {
  Source out{Kind::CONTROL_ENTITY_LIGHT, false, {}};
  auto &s = out.state;
  s.title.assign("Storage family");
  s.state_seq = 1;
  switch (index) {
    case 0:
      s.caps.turn_on = s.caps.turn_off = true;
      break;
    case 1: {
      out.kind = Kind::CONTROL_ENTITY_NUMBER;
      auto &n = s.emplace_family<ControlNumberSnapshot>();
      n.state_generation = 1;
      n.available = true;
      n.domain.minimum = 0;
      n.domain.maximum = 10 * NUMBER_SCALE;
      n.domain.step = NUMBER_SCALE;
      n.domain.max_index = 10;
      n.index = 1;
      n.value_text.assign("1");
      break;
    }
    case 2: {
      out.kind = Kind::CONTROL_ENTITY_CHOICE;
      auto &v = s.emplace_family<ControlSnapshot::Choice>();
      v.state_generation = 1;
      v.available = true;
      v.count = 1;
      v.options[0].assign("A");
      break;
    }
    case 3: {
      out.kind = Kind::CONTROL_ENTITY_COVER;
      auto &v = s.emplace_family<ControlCoverSnapshot>();
      v.state_generation = 1;
      v.available = true;
      v.state = ControlCoverState::CONTROL_COVER_STATE_CLOSED;
      v.caps.open = v.caps.close = v.caps.stop = true;
      break;
    }
    case 4: {
      out.kind = Kind::CONTROL_ENTITY_FAN;
      auto &v = s.emplace_family<ControlSnapshot::Fan>();
      v.state_generation = 1;
      v.available = true;
      v.power = 0;
      v.caps.turn_on = true;
      break;
    }
    case 5: {
      out.kind = Kind::CONTROL_ENTITY_HUMIDIFIER;
      auto &v = s.emplace_family<ControlSnapshot::Humidifier>();
      v.state_generation = 1;
      v.available = true;
      v.power = 0;
      v.caps.turn_on = true;
      v.target_text.assign("--");
      break;
    }
    case 6: {
      out.kind = Kind::CONTROL_ENTITY_ROBOT;
      auto &v = s.emplace_family<ControlSnapshot::Robot>();
      v.state_generation = 1;
      v.available = true;
      v.activity = 2;
      v.caps.actions = v.eligible_actions = 1;
      break;
    }
    case 7: {
      out.kind = Kind::CONTROL_ENTITY_PANEL;
      auto &v = s.emplace_family<ControlSnapshot::Panel>();
      v.state_generation = 1;
      v.available = true;
      v.type = 2;
      v.power = 1;
      v.caps.turn_on = v.caps.turn_off = true;
      v.eligible_actions = 1;
      break;
    }
    default: {
      out.detail = true;
      auto &v = s.emplace_family<ControlDetailSnapshot>();
      v.present = true;
      v.state_generation = 1;
      if (index == 8) {
        s.caps.turn_on = s.caps.turn_off = true;
        v.available = true;
      } else {
        out.kind = Kind::CONTROL_ENTITY_PANEL;
        v.climate = true;
        v.data.emplace<ControlClimateMain>();
      }
      break;
    }
  }
  return out;
}
void disabled_queries(MaterialEntityControl &o, uint32_t now) {
  assert(!o.can_step(1));
  assert(!o.can_drag_thermostat());
  assert(!o.can_choose(0));
  assert(!o.can_apply_choice());
  assert(!o.can_cover(Op::CONTROL_OPERATION_COVER_OPEN));
  assert(!o.can_fan(Op::CONTROL_OPERATION_FAN_POWER, 1));
  assert(!o.can_humidifier(Op::CONTROL_OPERATION_HUMIDIFIER_POWER, 1));
  assert(!o.can_robot(Op::CONTROL_OPERATION_ROBOT_START));
  assert(!o.can_panel(Op::CONTROL_OPERATION_PANEL_POWER, 1));
  assert(!o.can_detail_number());
  assert(!o.can_detail_choice(0));
  assert(!o.can_request_facet(ControlFacet::CONTROL_FACET_MAIN));
  (void) o.active_numeric();
  assert(!o.begin_fan_navigation(1, now));
  assert(!o.begin_humidifier_navigation(1, now));
  assert(!o.begin_robot_navigation(1, now));
  assert(!o.begin_panel_navigation(1, now));
  assert(!o.begin_detail_navigation(1, now));
  assert(!o.begin_step(1, 1, now));
}
int main() {
  for (unsigned from = 0; from < 10; from++)
    for (unsigned to = 0; to < 10; to++) {
      Sink sink;
      MaterialEntityControl owner(sink);
      sink.owner = &owner;
      ControlText<32> channel;
      channel.assign("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
      assert(owner.set_enabled(true));
      assert(owner.bind_generic(channel));
      assert(owner.set_choice_enabled(true));
      assert(owner.set_cover_enabled(true));
      assert(owner.set_fan_enabled(true));
      assert(owner.set_humidifier_enabled(true));
      assert(owner.set_robot_enabled(true));
      assert(owner.set_panel_types(7));
      assert(owner.accept_panel_features(channel, 1, 7));
      assert(owner.set_detail_profiles(3));
      assert(owner.accept_detail_features(channel, 3));
      unsigned now = 1;
      for (unsigned type : {from, to}) {
        Source s = source(type);
        const ControlSnapshot before = owner.state();
        ControlContext context;
        context.card_id.assign("storage-family");
        context.slot = 2;
        context.revision = 1;
        context.control_kind = s.kind;
        const bool opened =
            s.detail && type == 8 ? owner.request_open_detail(context, now++) : owner.request_open(context, now++);
        if (!opened) {
          std::cerr << "open " << from << " -> " << to << " type " << type << "\n";
          return 1;
        }
        assert(owner.state() == before);
        assert(!owner.has_selection());
        disabled_queries(owner, now++);
        ControlMessage grant;
        grant.generic = true;
        grant.kind = ControlKind::CONTROL_KIND_GRANT;
        grant.identity = owner.identity();
        grant.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
        grant.state = s.state;
        auto wrong = grant;
        wrong.state = source(type == 1 ? 4 : 1).state;
        assert(!owner.receive(wrong, now++));
        assert(owner.state() == before);
        assert(!owner.has_selection());
        if (!owner.receive(grant, now++)) {
          std::cerr << "grant " << from << " -> " << to << " type " << type << "\n";
          return 2;
        }
        assert(owner.state() == s.state);
        assert(owner.has_selection());
        assert(owner.set_view_ready(true, now++));
        wrong.kind = ControlKind::CONTROL_KIND_STATE;
        wrong.state.state_seq = 2;
        const auto count = sink.count;
        assert(!owner.receive(wrong, now++));
        assert(owner.state() == s.state);
        assert(sink.count == count);
        assert(owner.has_selection());
        auto same = grant;
        same.kind = ControlKind::CONTROL_KIND_STATE;
        same.state.state_seq = 2;
        assert(owner.receive(same, now++));
        assert(owner.state() == same.state);
        assert(owner.invalidate_admission(ControlReason::CONTROL_REASON_NAVIGATION));
        assert(!owner.has_selection());
        assert(owner.state() == same.state);
        disabled_queries(owner, now++);
      }
    }
  std::cout << "PASS 100 ordered family transitions, pending/revoke retained storage, wrong-tag atomic rejection, "
               "callback reentry\n";
}
