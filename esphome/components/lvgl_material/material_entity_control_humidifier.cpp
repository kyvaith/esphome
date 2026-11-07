#include "material_entity_control.h"

#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS

namespace esphome::lvgl_material {

bool control_valid_humidifier_snapshot(const ControlSnapshot &snapshot) {
  const auto *humidity_payload = snapshot.family_if<ControlSnapshot::Humidifier>();
  if (humidity_payload == nullptr)
    return false;
  const auto &humidity = *humidity_payload;
  constexpr auto unknown = ControlSnapshot::Humidifier::UNKNOWN;
  if (snapshot.state_seq == 0 || snapshot.state_seq > CONTROL_MAX_SEQUENCE || snapshot.ttl_ms != CONTROL_LEASE_MS ||
      !number_plain_utf8(snapshot.title.view()) || humidity.state_generation == 0 ||
      humidity.state_generation > CONTROL_MAX_SEQUENCE || snapshot.on || snapshot.brightness_known ||
      snapshot.caps.brightness || snapshot.caps.turn_on || snapshot.caps.turn_off ||
      (humidity.power > 1 && humidity.power != unknown) || humidity.available != (humidity.power != unknown) ||
      (humidity.device_class > 1 && humidity.device_class != unknown) ||
      humidity.caps.mode_count > humidity.caps.modes.size() ||
      (humidity.mode_index != unknown && humidity.mode_index >= humidity.caps.mode_count))
    return false;
  const bool domain = humidity.domain.valid();
  if (!domain && !(humidity.domain == MaterialNumberDomain{}))
    return false;
  if (domain && (humidity.domain.minimum < 0 || humidity.domain.maximum > 100 * NUMBER_SCALE ||
                 humidity.domain.minimum % NUMBER_SCALE || humidity.domain.maximum % NUMBER_SCALE ||
                 humidity.domain.step % NUMBER_SCALE || humidity.domain.max_index > 100 ||
                 std::string_view(humidity.domain.unit.data()) != "%"))
    return false;
  if (!humidity.available &&
      (humidity.index != unknown || humidity.mode_index != unknown || !humidity.current.view().empty()))
    return false;
  int64_t value;
  char canonical[24];
  if (humidity.index == unknown) {
    if (humidity.target_text.view() != "--")
      return false;
  } else if (!domain || !humidity.domain.value_at(humidity.index, value) ||
             !number_format_decimal(value, canonical, sizeof(canonical)) || humidity.target_text.view() != canonical) {
    return false;
  }
  if (!humidity.current.view().empty() &&
      (!number_parse_decimal(humidity.current.view(), value) || value < 0 || value > 100 * NUMBER_SCALE ||
       !number_format_decimal(value, canonical, sizeof(canonical)) || humidity.current.view() != canonical))
    return false;
  for (size_t index = 0; index < humidity.caps.mode_count; index++) {
    const auto mode = humidity.caps.modes[index].view();
    if (mode.empty() || !number_plain_utf8(mode))
      return false;
    for (size_t previous = 0; previous < index; previous++)
      if (humidity.caps.modes[index] == humidity.caps.modes[previous])
        return false;
  }
  return true;
}

bool MaterialEntityControl::set_humidifier_enabled(bool enabled) {
  if (this->in_callback_)
    return false;
  if (!enabled && this->is_humidifier())
    this->revoke_(ControlReason::CONTROL_REASON_UNSUPPORTED, true);
  this->humidifier_enabled_ = enabled;
  return true;
}

bool MaterialEntityControl::accept_humidifier_state_(const ControlSnapshot &state) {
  const auto *previous_payload = this->state_.family_if<ControlSnapshot::Humidifier>();
  if (previous_payload == nullptr)
    return false;
  const auto &previous = *previous_payload;
  const auto *next_payload = state.family_if<ControlSnapshot::Humidifier>();
  if (next_payload == nullptr)
    return false;
  const auto &next = *next_payload;
  if (!(previous.caps == next.caps) || !(previous.domain == next.domain) ||
      previous.device_class != next.device_class) {
    this->revoke_(ControlReason::CONTROL_REASON_CAPABILITIES_CHANGED, true);
    return false;
  }
  if (next.state_generation < previous.state_generation)
    return false;
  if (next.state_generation == previous.state_generation) {
    if (next.power != previous.power || next.available != previous.available || next.index != previous.index ||
        next.mode_index != previous.mode_index || !(next.target_text == previous.target_text)) {
      this->revoke_(ControlReason::CONTROL_REASON_SELECTION_CHANGED, true);
      return false;
    }
    // A current-humidity measurement is display-only, not an input generation.
    return true;
  }
  this->cancel_drag();
  return true;
}

bool MaterialEntityControl::can_humidifier(ControlOperation operation, uint8_t target, bool allow_same) const {
  const auto *humidity_payload = this->state_.family_if<ControlSnapshot::Humidifier>();
  if (humidity_payload == nullptr)
    return false;
  const auto &humidity = *humidity_payload;
  constexpr auto unknown = ControlSnapshot::Humidifier::UNKNOWN;
  if (!this->is_humidifier() || !this->humidifier_enabled_ || !this->selected_ || !this->view_ready_ ||
      this->command_pending_ || !humidity.available)
    return false;
  switch (operation) {
    case ControlOperation::CONTROL_OPERATION_HUMIDIFIER_POWER:
      return target <= 1 && (allow_same || target != humidity.power) &&
             (target == 1 ? humidity.caps.turn_on : humidity.caps.turn_off);
    case ControlOperation::CONTROL_OPERATION_HUMIDIFIER_TARGET:
      return humidity.index != unknown && humidity.domain.valid() && target <= humidity.domain.max_index &&
             (allow_same || target != humidity.index);
    case ControlOperation::CONTROL_OPERATION_HUMIDIFIER_MODE:
      return humidity.mode_index != unknown && target < humidity.caps.mode_count &&
             (allow_same || target != humidity.mode_index);
    default:
      return false;
  }
}

bool MaterialEntityControl::begin_humidifier_navigation(uint32_t pointer, uint32_t now) {
  if (this->state_.family_if<ControlSnapshot::Humidifier>() == nullptr || !this->is_humidifier() ||
      !this->can_input_(now) || this->dragging_)
    return false;
  this->pointer_id_ = pointer;
  this->pressed_generation_ = this->state_.family_if<ControlSnapshot::Humidifier>()->state_generation;
  this->dragging_ = true;
  return true;
}

bool MaterialEntityControl::begin_humidifier(uint32_t pointer, ControlOperation operation, uint8_t target,
                                             uint32_t now) {
  if (!this->can_humidifier(operation, target, operation == ControlOperation::CONTROL_OPERATION_HUMIDIFIER_TARGET) ||
      !this->begin_humidifier_navigation(pointer, now))
    return false;
  this->pressed_humidifier_operation_ = operation;
  this->pressed_humidifier_target_ = target;
  return true;
}

bool MaterialEntityControl::update_humidifier_target(uint32_t pointer, uint8_t target, uint32_t now) {
  if (!this->can_input_(now) || !this->dragging_ || this->pointer_id_ != pointer ||
      this->pressed_humidifier_operation_ != ControlOperation::CONTROL_OPERATION_HUMIDIFIER_TARGET ||
      !this->can_humidifier(ControlOperation::CONTROL_OPERATION_HUMIDIFIER_TARGET, target, true))
    return false;
  this->pressed_humidifier_target_ = target;
  return true;
}

bool MaterialEntityControl::release_humidifier_navigation(uint32_t pointer, uint32_t now) {
  if (!this->can_input_(now) || this->state_.family_if<ControlSnapshot::Humidifier>() == nullptr ||
      !this->is_humidifier() || !this->dragging_ || this->pointer_id_ != pointer)
    return false;
  this->dragging_ = false;
  return this->state_.family_if<ControlSnapshot::Humidifier>()->state_generation == this->pressed_generation_;
}

bool MaterialEntityControl::release_humidifier(uint32_t pointer, uint32_t now) {
  if (!this->release_humidifier_navigation(pointer, now) ||
      !this->can_humidifier(this->pressed_humidifier_operation_, this->pressed_humidifier_target_) ||
      this->command_seq_ == CONTROL_MAX_SEQUENCE)
    return false;
  ControlMessage message;
  message.kind = ControlKind::CONTROL_KIND_COMMAND;
  message.identity = this->identity_;
  message.command_seq = ++this->command_seq_;
  message.operation = this->pressed_humidifier_operation_;
  message.humidifier_target_index = this->pressed_humidifier_target_;
  message.expected_state_generation = this->pressed_generation_;
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
