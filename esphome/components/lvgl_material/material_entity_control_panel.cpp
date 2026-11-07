#include "material_entity_control.h"
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
#include <algorithm>
namespace esphome::lvgl_material {
namespace {
using Panel = ControlSnapshot::Panel;
using Op = ControlOperation;
uint8_t eligible(const Panel &panel) {
  if (!panel.available)
    return 0;
  const auto &caps = panel.caps;
  if (panel.type == 0) {
    uint8_t mask = caps.start ? 16 : 0;
    if (panel.phase == 1 && caps.start)
      mask |= 1;
    if (panel.phase == 3 && caps.start)
      mask |= 2;
    if (panel.phase == 2 && caps.pause)
      mask |= 4;
    if ((panel.phase == 2 || panel.phase == 3) && caps.cancel)
      mask |= 8;
    return mask;
  }
  uint8_t mask = panel.power == 1 && caps.turn_on ? 1 : panel.power == 2 && caps.turn_off ? 2 : 0;
  if (panel.type == 1 && caps.activity_count != 0 && (panel.power == 1 || panel.activity_index != Panel::UNKNOWN_INDEX))
    mask |= 4;
  return mask;
}
}  // namespace
bool control_valid_panel_snapshot(const ControlSnapshot &snapshot) {
  const auto *panel_payload = snapshot.family_if<ControlSnapshot::Panel>();
  if (panel_payload == nullptr)
    return false;
  const auto &panel = *panel_payload;
  const auto &caps = panel.caps;
  constexpr auto unknown = Panel::UNKNOWN_VALUE;
  if (snapshot.state_seq == 0 || snapshot.state_seq > CONTROL_MAX_SEQUENCE || snapshot.ttl_ms != CONTROL_LEASE_MS ||
      !number_plain_utf8(snapshot.title.view()) || panel.state_generation == 0 ||
      panel.state_generation > CONTROL_MAX_SEQUENCE || snapshot.on || snapshot.brightness_known ||
      snapshot.caps.brightness || snapshot.caps.turn_on || snapshot.caps.turn_off || panel.type > 2 ||
      panel.eligible_actions != eligible(panel) || caps.activity_count > caps.activities.size())
    return false;
  if (panel.type == 0) {
    if (panel.phase > 3 || panel.available != (panel.phase != 0) || panel.power != 0 ||
        panel.activity_index != Panel::UNKNOWN_INDEX || caps.turn_on || caps.turn_off || caps.activity_count != 0 ||
        caps.device_class != 0 || (!caps.start && !caps.pause && !caps.cancel) || caps.initial_duration > 86400 ||
        (panel.duration != unknown && panel.duration > CONTROL_MAX_SEQUENCE) ||
        (panel.remaining != unknown && panel.remaining > CONTROL_MAX_SEQUENCE) || panel.sampled_at < Panel::MIN_UTC ||
        panel.sampled_at > Panel::MAX_UTC)
      return false;
    if (panel.phase == 0 && (panel.duration != unknown || panel.remaining != unknown || panel.finishes_at != 0))
      return false;
    if (panel.phase == 1 && (panel.remaining != unknown || panel.finishes_at != 0))
      return false;
    if (panel.phase == 3 && panel.finishes_at != 0)
      return false;
    if (panel.phase == 2) {
      if (panel.finishes_at == 0)
        return panel.remaining == unknown;
      if (panel.finishes_at < Panel::MIN_UTC || panel.finishes_at > Panel::MAX_UTC)
        return false;
      const auto seconds = std::max<int64_t>(0, int64_t{panel.finishes_at} - panel.sampled_at);
      return seconds <= CONTROL_MAX_SEQUENCE && panel.remaining == seconds;
    }
    return true;
  }
  if (panel.power > 2 || panel.available != (panel.power != 0) || panel.phase != 0 || caps.start || caps.pause ||
      caps.cancel || caps.initial_duration != 0 || panel.duration != unknown || panel.remaining != unknown ||
      panel.finishes_at != 0 || panel.sampled_at != 0 || (!caps.turn_on && !caps.turn_off) || caps.device_class > 2)
    return false;
  if (panel.type == 2)
    return caps.activity_count == 0 && panel.activity_index == Panel::UNKNOWN_INDEX;
  if (caps.device_class != 0 || (caps.activity_count != 0 && !caps.turn_on) ||
      (panel.activity_index != Panel::UNKNOWN_INDEX &&
       (panel.power != 2 || panel.activity_index >= caps.activity_count)))
    return false;
  for (size_t index = 0; index < caps.activity_count; index++) {
    const auto text = caps.activities[index].view();
    if (text.empty() || !number_plain_utf8(text) || text == "unknown" || text == "unavailable")
      return false;
    for (size_t previous = 0; previous < index; previous++)
      if (caps.activities[previous] == caps.activities[index])
        return false;
  }
  return true;
}

bool panel_timer_remaining(const ControlSnapshot::Panel &panel, bool clock_valid, uint32_t utc_seconds,
                           uint32_t &remaining, bool &sample_only) {
  sample_only = false;
  if (panel.type != 0 || !panel.available || panel.remaining == Panel::UNKNOWN_VALUE)
    return false;
  remaining = panel.remaining;
  if (panel.phase != 2)
    return panel.phase == 3;
  const auto delta = int64_t{utc_seconds} - panel.sampled_at;
  if (!clock_valid || panel.finishes_at == 0 || delta < -2 || delta > (CONTROL_LEASE_MS + 999) / 1000 + 2) {
    sample_only = true;
    return true;
  }
  remaining =
      static_cast<uint32_t>(std::clamp<int64_t>(int64_t{panel.finishes_at} - utc_seconds, 0, CONTROL_MAX_SEQUENCE));
  return true;
}

bool MaterialEntityControl::set_panel_types(uint8_t types) {
  if (this->in_callback_ || types > 7)
    return false;
  if (this->panel_types_ == types)
    return true;
  if (this->is_panel())
    this->revoke_(ControlReason::CONTROL_REASON_UNSUPPORTED, true);
  this->panel_types_ = types;
  this->panel_peer_types_ = 0;
  return true;
}
bool MaterialEntityControl::accept_panel_features(const ControlText<32> &channel, uint8_t version, uint8_t types) {
  if (this->in_callback_ || !this->enabled_ || !this->generic_ || this->selected_ || this->open_pending_ ||
      !control_valid_hex(channel) || channel != this->channel_ || version != 1 || types == 0 ||
      (types & ~this->panel_types_) != 0)
    return false;
  this->panel_peer_types_ = types;
  return true;
}
bool MaterialEntityControl::accept_panel_state_(const ControlSnapshot &state) {
  const auto *previous_payload = this->state_.family_if<ControlSnapshot::Panel>();
  if (previous_payload == nullptr)
    return false;
  const auto &previous = *previous_payload;
  const auto *next_payload = state.family_if<ControlSnapshot::Panel>();
  if (next_payload == nullptr)
    return false;
  const auto &next = *next_payload;
  if (previous.type != next.type || !(previous.caps == next.caps)) {
    this->revoke_(ControlReason::CONTROL_REASON_CAPABILITIES_CHANGED, true);
    return false;
  }
  if (next.state_generation < previous.state_generation)
    return false;
  if (next.state_generation == previous.state_generation) {
    const bool timer_change = next.type == 0 && (next.phase != previous.phase || next.duration != previous.duration ||
                                                 next.finishes_at != previous.finishes_at ||
                                                 (next.phase == 3 && next.remaining != previous.remaining));
    if (timer_change || next.available != previous.available || next.eligible_actions != previous.eligible_actions ||
        next.power != previous.power || next.activity_index != previous.activity_index) {
      this->revoke_(ControlReason::CONTROL_REASON_SELECTION_CHANGED, true);
      return false;
    }
    return true;
  }
  this->cancel_drag();
  return true;
}
bool MaterialEntityControl::can_panel(ControlOperation operation, uint32_t value) const {
  const auto *panel_payload = this->state_.family_if<ControlSnapshot::Panel>();
  if (panel_payload == nullptr)
    return false;
  const auto &panel = *panel_payload;
  if (!this->is_panel() || !this->selected_ || !this->view_ready_ || this->command_pending_ || !panel.available ||
      (this->panel_peer_types_ & (1U << panel.type)) == 0)
    return false;
  if (panel.type == 0) {
    if (operation < Op::CONTROL_OPERATION_PANEL_START || operation > Op::CONTROL_OPERATION_PANEL_DURATION)
      return false;
    const auto bit = 1U << (static_cast<uint8_t>(operation) - static_cast<uint8_t>(Op::CONTROL_OPERATION_PANEL_START));
    return (panel.eligible_actions & bit) != 0 &&
           (operation != Op::CONTROL_OPERATION_PANEL_DURATION || (value >= 1 && value <= 86400));
  }
  if (operation == Op::CONTROL_OPERATION_PANEL_POWER)
    return value <= 1 && (panel.eligible_actions & (value ? 1 : 2)) != 0;
  return panel.type == 1 && operation == Op::CONTROL_OPERATION_PANEL_ACTIVITY && (panel.eligible_actions & 4) != 0 &&
         value < panel.caps.activity_count;
}
bool MaterialEntityControl::begin_panel_navigation(uint32_t pointer, uint32_t now) {
  if (!this->can_input_(now) || this->state_.family_if<ControlSnapshot::Panel>() == nullptr || !this->is_panel() ||
      !this->state_.family_if<ControlSnapshot::Panel>()->available || this->dragging_)
    return false;
  this->pointer_id_ = pointer;
  this->pressed_generation_ = this->state_.family_if<ControlSnapshot::Panel>()->state_generation;
  this->dragging_ = true;
  return true;
}
bool MaterialEntityControl::release_panel_navigation(uint32_t pointer, uint32_t now) {
  if (!this->can_input_(now) || this->state_.family_if<ControlSnapshot::Panel>() == nullptr || !this->is_panel() ||
      !this->dragging_ || this->pointer_id_ != pointer)
    return false;
  this->dragging_ = false;
  return this->pressed_generation_ == this->state_.family_if<ControlSnapshot::Panel>()->state_generation;
}
bool MaterialEntityControl::begin_panel(uint32_t pointer, ControlOperation operation, uint32_t value, uint32_t now) {
  if (this->in_callback_)
    return false;
  this->tick(now);
  if (!this->enabled_ || this->dragging_ || !this->can_panel(operation, value))
    return false;
  this->pointer_id_ = pointer;
  this->pressed_generation_ = this->state_.family_if<ControlSnapshot::Panel>()->state_generation;
  this->pressed_panel_operation_ = operation;
  this->pressed_panel_value_ = value;
  this->dragging_ = true;
  return true;
}
bool MaterialEntityControl::release_panel(uint32_t pointer, uint32_t now) {
  if (this->in_callback_)
    return false;
  this->tick(now);
  if (!this->enabled_ || !this->is_panel() || !this->dragging_ || this->pointer_id_ != pointer)
    return false;
  this->dragging_ = false;
  if (!this->can_panel(this->pressed_panel_operation_, this->pressed_panel_value_) ||
      this->pressed_generation_ != this->state_.family_if<ControlSnapshot::Panel>()->state_generation ||
      this->command_seq_ == CONTROL_MAX_SEQUENCE)
    return false;
  ControlMessage message;
  message.kind = ControlKind::CONTROL_KIND_COMMAND;
  message.identity = this->identity_;
  message.command_seq = ++this->command_seq_;
  message.operation = this->pressed_panel_operation_;
  message.panel_value = this->pressed_panel_value_;
  message.expected_state_generation = this->pressed_generation_;
  this->pending_panel_operation_ = message.operation;
  this->command_at_ = now;
  this->command_pending_ = true;
  this->last_result_ = ControlResult::CONTROL_RESULT_PENDING;
  this->last_reason_ = ControlReason::CONTROL_REASON_NONE;
  if (!this->emit_(message)) {
    this->command_pending_ = false;
    this->last_result_ = ControlResult::CONTROL_RESULT_REJECTED;
    this->last_reason_ = ControlReason::CONTROL_REASON_CONNECTION_LOST;
    this->disconnect();
    this->notify_(ControlViewEvent::CONTROL_VIEW_EVENT_RESULT);
    return false;
  }
  this->notify_(ControlViewEvent::CONTROL_VIEW_EVENT_SENDING);
  return true;
}
}  // namespace esphome::lvgl_material
#endif
