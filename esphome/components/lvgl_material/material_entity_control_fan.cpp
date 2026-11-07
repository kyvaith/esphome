#include "material_entity_control.h"

#ifdef USE_LVGL_MATERIAL_FAN_CONTROLS

namespace esphome::lvgl_material {

bool control_valid_fan_snapshot(const ControlSnapshot &snapshot) {
  const auto *fan_payload = snapshot.family_if<ControlSnapshot::Fan>();
  if (fan_payload == nullptr)
    return false;
  const auto &fan = *fan_payload;
  constexpr auto unknown = ControlSnapshot::Fan::UNKNOWN;
  if (snapshot.state_seq == 0 || snapshot.state_seq > CONTROL_MAX_SEQUENCE || snapshot.ttl_ms != CONTROL_LEASE_MS ||
      !number_plain_utf8(snapshot.title.view()) || fan.state_generation == 0 ||
      fan.state_generation > CONTROL_MAX_SEQUENCE || snapshot.on || snapshot.brightness_known ||
      snapshot.caps.brightness || snapshot.caps.turn_on || snapshot.caps.turn_off ||
      (fan.power > 1 && fan.power != unknown) || fan.available != (fan.power != unknown) ||
      (fan.percentage > 100 && fan.percentage != unknown) || (fan.oscillating > 1 && fan.oscillating != unknown) ||
      (fan.direction > 1 && fan.direction != unknown) || fan.caps.speed_count > fan.caps.speeds.size() ||
      fan.caps.preset_count > fan.caps.presets.size() || (!fan.preset_known && fan.preset_index != unknown) ||
      (fan.preset_index != unknown && fan.preset_index >= fan.caps.preset_count))
    return false;
  if (!fan.available && (fan.percentage != unknown || fan.preset_known || fan.preset_index != unknown ||
                         fan.oscillating != unknown || fan.direction != unknown))
    return false;
  if ((!fan.caps.oscillate && fan.oscillating != unknown) || (!fan.caps.direction && fan.direction != unknown))
    return false;
  if (fan.caps.speed_count != 0 &&
      (fan.caps.speed_count < 2 || fan.caps.speeds[0] != 0 || fan.caps.speeds[fan.caps.speed_count - 1] != 100))
    return false;
  for (size_t index = 0; index < fan.caps.speed_count; index++)
    if (fan.caps.speeds[index] > 100 || (index != 0 && fan.caps.speeds[index - 1] >= fan.caps.speeds[index]))
      return false;
  for (size_t index = 0; index < fan.caps.preset_count; index++) {
    const auto value = fan.caps.presets[index].view();
    if (value.empty() || !number_plain_utf8(value))
      return false;
    for (size_t previous = 0; previous < index; previous++)
      if (fan.caps.presets[previous] == fan.caps.presets[index])
        return false;
  }
  return true;
}

bool MaterialEntityControl::set_fan_enabled(bool enabled) {
  if (this->in_callback_)
    return false;
  if (!enabled && this->is_fan())
    this->revoke_(ControlReason::CONTROL_REASON_UNSUPPORTED, true);
  this->fan_enabled_ = enabled;
  return true;
}

bool MaterialEntityControl::accept_fan_state_(const ControlSnapshot &state) {
  const auto *previous_payload = this->state_.family_if<ControlSnapshot::Fan>();
  if (previous_payload == nullptr)
    return false;
  const auto &previous = *previous_payload;
  const auto *next_payload = state.family_if<ControlSnapshot::Fan>();
  if (next_payload == nullptr)
    return false;
  const auto &next = *next_payload;
  if (!(previous.caps == next.caps)) {
    this->revoke_(ControlReason::CONTROL_REASON_CAPABILITIES_CHANGED, true);
    return false;
  }
  if (next.state_generation < previous.state_generation)
    return false;
  if (next.state_generation == previous.state_generation) {
    if (!(next == previous)) {
      this->revoke_(ControlReason::CONTROL_REASON_SELECTION_CHANGED, true);
      return false;
    }
    return true;
  }
  this->cancel_drag();
  return true;
}

bool MaterialEntityControl::can_fan(ControlOperation operation, uint8_t target) const {
  const auto *fan_payload = this->state_.family_if<ControlSnapshot::Fan>();
  if (fan_payload == nullptr)
    return false;
  const auto &fan = *fan_payload;
  constexpr auto unknown = ControlSnapshot::Fan::UNKNOWN;
  if (!this->is_fan() || !this->fan_enabled_ || !this->selected_ || !this->view_ready_ || this->command_pending_ ||
      !fan.available)
    return false;
  switch (operation) {
    case ControlOperation::CONTROL_OPERATION_FAN_POWER:
      return target <= 1 && target != fan.power && (target == 1 ? fan.caps.turn_on : fan.caps.turn_off);
    case ControlOperation::CONTROL_OPERATION_FAN_SPEED:
      // Observed percentages can differ from the service lattice (for example
      // a three-speed entity reports 67 while the exact target catalog uses 66).
      // Keep the observation unchanged and send only an explicit catalog target.
      // An active preset may intentionally omit percentage. Its known catalog
      // entry still permits returning to a supported absolute manual speed.
      return (fan.percentage != unknown || (fan.preset_known && fan.preset_index < fan.caps.preset_count)) &&
             target < fan.caps.speed_count && fan.caps.speeds[target] != fan.percentage;
    case ControlOperation::CONTROL_OPERATION_FAN_PRESET:
      return fan.preset_known && target < fan.caps.preset_count && target != fan.preset_index;
    case ControlOperation::CONTROL_OPERATION_FAN_OSCILLATION:
      return fan.caps.oscillate && fan.oscillating != unknown && target <= 1 && target != fan.oscillating;
    case ControlOperation::CONTROL_OPERATION_FAN_DIRECTION:
      return fan.caps.direction && fan.direction != unknown && target <= 1 && target != fan.direction;
    default:
      return false;
  }
}

bool MaterialEntityControl::begin_fan_navigation(uint32_t pointer_id, uint32_t now_ms) {
  if (this->state_.family_if<ControlSnapshot::Fan>() == nullptr || !this->is_fan() || !this->can_input_(now_ms) ||
      this->dragging_)
    return false;
  this->pointer_id_ = pointer_id;
  this->pressed_generation_ = this->state_.family_if<ControlSnapshot::Fan>()->state_generation;
  this->dragging_ = true;
  return true;
}

bool MaterialEntityControl::begin_fan(uint32_t pointer_id, ControlOperation operation, uint8_t target,
                                      uint32_t now_ms) {
  if (!this->can_fan(operation, target) || !this->begin_fan_navigation(pointer_id, now_ms))
    return false;
  this->pressed_fan_operation_ = operation;
  this->pressed_fan_target_ = target;
  return true;
}

bool MaterialEntityControl::release_fan_navigation(uint32_t pointer_id, uint32_t now_ms) {
  if (!this->can_input_(now_ms) || this->state_.family_if<ControlSnapshot::Fan>() == nullptr || !this->is_fan() ||
      !this->dragging_ || this->pointer_id_ != pointer_id)
    return false;
  this->dragging_ = false;
  return this->state_.family_if<ControlSnapshot::Fan>()->state_generation == this->pressed_generation_;
}

bool MaterialEntityControl::release_fan(uint32_t pointer_id, uint32_t now_ms) {
  if (!this->release_fan_navigation(pointer_id, now_ms) ||
      !this->can_fan(this->pressed_fan_operation_, this->pressed_fan_target_) ||
      this->command_seq_ == CONTROL_MAX_SEQUENCE)
    return false;
  ControlMessage message;
  message.kind = ControlKind::CONTROL_KIND_COMMAND;
  message.identity = this->identity_;
  message.command_seq = ++this->command_seq_;
  message.operation = this->pressed_fan_operation_;
  message.fan_target_index = this->pressed_fan_target_;
  message.expected_state_generation = this->pressed_generation_;
  this->command_at_ = now_ms;
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
