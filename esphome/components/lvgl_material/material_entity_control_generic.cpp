#include "material_entity_control.h"

#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER

namespace esphome::lvgl_material {

bool control_valid_number_snapshot(const ControlSnapshot &snapshot) {
  const auto &number = snapshot.number;
  if (snapshot.state_seq == 0 || snapshot.state_seq > CONTROL_MAX_SEQUENCE || snapshot.ttl_ms != CONTROL_LEASE_MS ||
      !number_plain_utf8(snapshot.title.view()) || number.state_generation == 0 ||
      number.state_generation > CONTROL_MAX_SEQUENCE || !number.domain.valid() || snapshot.on ||
      snapshot.brightness_known || snapshot.caps.brightness || snapshot.caps.turn_on || snapshot.caps.turn_off)
    return false;
  if (!number.available)
    return number.value_text.view() == "--" && number.index == 0;
  int64_t value;
  char canonical[24];
  return number.domain.value_at(number.index, value) && number_format_decimal(value, canonical, sizeof(canonical)) &&
         number.value_text.view() == canonical;
}

bool control_valid_generic_result(ControlResult result, ControlReason reason) {
  return control_valid_result(result, reason) ||
         (result == ControlResult::CONTROL_RESULT_REJECTED && reason == ControlReason::CONTROL_REASON_STALE_STATE);
}

bool MaterialEntityControl::bind_generic(const ControlText<32> &channel) { return this->bind_mode_(channel, true); }

bool MaterialEntityControl::bind_mode_(const ControlText<32> &channel, bool generic) {
  if (this->in_callback_ || !this->enabled_ || !control_valid_hex(channel))
    return false;
  if (channel == this->channel_ && generic == this->generic_)
    return true;
  this->revoke_(ControlReason::CONTROL_REASON_CONNECTION_LOST, false);
  this->channel_ = channel;
  this->generic_ = generic;
  this->open_seq_ = 0;
  return true;
}

bool MaterialEntityControl::validate_snapshot_(const ControlMessage &message) const {
  return this->is_number() ? control_valid_number_snapshot(message.state) : control_valid_snapshot(message.state);
}

bool MaterialEntityControl::accept_number_state_(const ControlSnapshot &state) {
  const auto &previous = this->state_.number;
  const auto &next = state.number;
  if (!(previous.domain == next.domain)) {
    this->revoke_(ControlReason::CONTROL_REASON_CAPABILITIES_CHANGED, true);
    return false;
  }
  if (next.state_generation < previous.state_generation)
    return false;
  if (next.state_generation == previous.state_generation) {
    if (next.available != previous.available || next.index != previous.index) {
      this->revoke_(ControlReason::CONTROL_REASON_SELECTION_CHANGED, true);
      return false;
    }
    // Transport renewal/title-only changes must not cancel a valid press.
    return true;
  }
  // A higher generation with the same final index represents a coalesced ABA.
  this->cancel_drag();
  return true;
}

bool MaterialEntityControl::can_step(int8_t direction) const {
  if (!this->is_number() || !this->selected_ || !this->view_ready_ || this->command_pending_ ||
      !this->state_.number.available)
    return false;
  if (direction == -1)
    return this->state_.number.index != 0;
  return direction == 1 && this->state_.number.index < this->state_.number.domain.max_index;
}

bool MaterialEntityControl::begin_step(uint32_t pointer_id, int8_t direction, uint32_t now_ms) {
  if (!this->can_input_(now_ms) || this->dragging_ || !this->can_step(direction))
    return false;
  this->pointer_id_ = pointer_id;
  this->pressed_index_ = this->state_.number.index;
  this->pressed_generation_ = this->state_.number.state_generation;
  this->step_direction_ = direction;
  this->dragging_ = true;
  return true;
}

bool MaterialEntityControl::release_step(uint32_t pointer_id, uint32_t now_ms) {
  if (!this->can_input_(now_ms) || !this->is_number() || !this->dragging_ || this->pointer_id_ != pointer_id)
    return false;
  this->dragging_ = false;
  if (!this->can_step(this->step_direction_) || this->state_.number.index != this->pressed_index_ ||
      this->state_.number.state_generation != this->pressed_generation_ || this->command_seq_ == CONTROL_MAX_SEQUENCE)
    return false;
  ControlMessage message;
  message.kind = ControlKind::CONTROL_KIND_COMMAND;
  message.identity = this->identity_;
  message.command_seq = ++this->command_seq_;
  message.operation = ControlOperation::CONTROL_OPERATION_STEP;
  message.direction = this->step_direction_;
  message.expected_index = this->pressed_index_;
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
