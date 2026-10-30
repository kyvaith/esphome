#include "material_entity_control.h"

namespace esphome::lvgl_material {

bool MaterialEntityControl::emit_(const ControlMessage &message) {
  this->in_callback_ = true;
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  bool accepted;
  if (this->generic_) {
    ControlMessage wire = message;
    wire.generic = true;
    accepted = this->sink_.emit(wire);
  } else
    accepted = this->sink_.emit(message);
#else
  const bool accepted = this->sink_.emit(message);
#endif
  this->in_callback_ = false;
  return accepted;
}

void MaterialEntityControl::notify_(ControlViewEvent event) {
  this->in_callback_ = true;
  this->sink_.view_event(event);
  this->in_callback_ = false;
}

void MaterialEntityControl::sync_candidate_() {
  this->candidate_known_ = this->state_.brightness_known;
  this->candidate_ = this->state_.brightness_pct;
}

bool MaterialEntityControl::set_enabled(bool enabled) {
  if (this->in_callback_)
    return false;
  if (this->enabled_ == enabled)
    return true;
  if (!enabled) {
    this->revoke_(ControlReason::CONTROL_REASON_UNLOAD, true);
    this->channel_ = {};
  }
  this->enabled_ = enabled;
  return true;
}

bool MaterialEntityControl::bind(const ControlText<32> &channel) {
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  return this->bind_mode_(channel, false);
#else
  if (this->in_callback_ || !this->enabled_ || !control_valid_hex(channel))
    return false;
  if (channel == this->channel_)
    return true;
  this->revoke_(ControlReason::CONTROL_REASON_CONNECTION_LOST, false);
  this->channel_ = channel;
  this->open_seq_ = 0;
  return true;
#endif
}

bool MaterialEntityControl::request_open(const ControlContext &context, uint32_t now_ms) {
  if (this->in_callback_ || !this->enabled_ || !this->is_bound() || !control_valid_context(context) ||
      this->open_seq_ == CONTROL_MAX_SEQUENCE)
    return false;
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  if (static_cast<uint8_t>(context.control_kind) > 1 ||
      (!this->generic_ && context.control_kind != ControlEntityKind::CONTROL_ENTITY_LIGHT))
    return false;
#endif
  this->revoke_(ControlReason::CONTROL_REASON_SELECTION_CHANGED, true);
  this->identity_ = {};
  this->identity_.context = context;
  this->identity_.channel = this->channel_;
  this->identity_.open_seq = ++this->open_seq_;
  this->open_pending_ = true;
  this->open_generation_ = this->admission_generation_;
  this->opened_at_ = now_ms;
  ControlMessage request;
  request.kind = ControlKind::CONTROL_KIND_OPEN;
  request.identity = this->identity_;
  if (!this->emit_(request)) {
    this->disconnect();
    return false;
  }
  return true;
}

bool MaterialEntityControl::matches_open_(const ControlIdentity &identity) const {
  return identity.channel == this->channel_ && identity.channel == this->identity_.channel &&
         identity.context == this->identity_.context && identity.open_seq == this->identity_.open_seq;
}

bool MaterialEntityControl::receive(const ControlMessage &message, uint32_t now_ms) {
  if (this->in_callback_ || !this->enabled_)
    return false;
  this->tick(now_ms);
  if (message.kind == ControlKind::CONTROL_KIND_BIND) {
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
    return this->bind_mode_(message.identity.channel, message.generic);
#else
    return this->bind(message.identity.channel);
#endif
  }
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  if (message.generic != this->generic_)
    return false;
#endif
  if (!this->matches_open_(message.identity) || this->open_generation_ != this->admission_generation_)
    return false;
  if (message.kind == ControlKind::CONTROL_KIND_GRANT) {
    if (!this->open_pending_ || this->selected_ || !control_valid_hex(message.identity.token) ||
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
        !this->validate_snapshot_(message))
#else
        !control_valid_snapshot(message.state))
#endif
      return false;
    this->identity_ = message.identity;
    this->state_ = message.state;
    this->open_pending_ = false;
    this->selected_ = true;
    this->view_ready_ = false;
    this->scalar_ready_ = false;
    this->command_seq_ = 0;
    this->command_pending_ = false;
    this->last_result_ = ControlResult::CONTROL_RESULT_PENDING;
    this->last_reason_ = ControlReason::CONTROL_REASON_NONE;
    this->renewed_at_ = now_ms;
    this->sync_candidate_();
    this->notify_(ControlViewEvent::CONTROL_VIEW_EVENT_GRANTED);
    return true;
  }
  if (message.kind == ControlKind::CONTROL_KIND_CANCEL_OPEN && this->open_pending_)
    return this->revoke_(message.reason, false);
  if (!this->selected_ || !(message.identity == this->identity_))
    return false;
  if (message.kind == ControlKind::CONTROL_KIND_CLOSE)
    return this->revoke_(message.reason, false);
  if (message.kind == ControlKind::CONTROL_KIND_STATE) {
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
    if (!this->validate_snapshot_(message) || message.state.state_seq <= this->state_.state_seq)
      return false;
    if (this->is_number() && !this->accept_number_state_(message.state))
      return false;
#else
    if (!control_valid_snapshot(message.state) || message.state.state_seq <= this->state_.state_seq)
      return false;
#endif
    if (!(message.state.caps == this->state_.caps)) {
      this->revoke_(ControlReason::CONTROL_REASON_CAPABILITIES_CHANGED, true);
      return false;
    }
    this->state_ = message.state;
    this->renewed_at_ = now_ms;
    if (!this->dragging_)
      this->sync_candidate_();
    this->notify_(ControlViewEvent::CONTROL_VIEW_EVENT_STATE);
    return true;
  }
  if (message.kind == ControlKind::CONTROL_KIND_RESULT) {
    if (!this->command_pending_ || message.command_seq != this->command_seq_ || message.state.state_seq == 0 ||
        message.state.state_seq > CONTROL_MAX_SEQUENCE ||
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
        !(this->generic_ ? control_valid_generic_result(message.result, message.reason)
                         : control_valid_result(message.result, message.reason)))
#else
        !control_valid_result(message.result, message.reason))
#endif
      return false;
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
    // Ordered HA transport must publish authoritative Number state first.
    // Refuse future acknowledgements, retaining pending ownership; no queue.
    if (this->is_number() && message.state.state_seq > this->state_.state_seq)
      return false;
#endif
    if (this->last_result_ == ControlResult::CONTROL_RESULT_HA_ACCEPTED &&
        message.result == ControlResult::CONTROL_RESULT_PENDING)
      return false;
    this->last_result_ = message.result;
    this->last_reason_ = message.reason;
    if (message.result != ControlResult::CONTROL_RESULT_PENDING &&
        message.result != ControlResult::CONTROL_RESULT_HA_ACCEPTED) {
      this->command_pending_ = false;
      this->sync_candidate_();
    }
    this->notify_(ControlViewEvent::CONTROL_VIEW_EVENT_RESULT);
    return true;
  }
  return false;
}

bool MaterialEntityControl::set_view_ready(bool ready, uint32_t now_ms) {
  if (this->in_callback_)
    return false;
  this->tick(now_ms);
  if (!ready) {
    this->view_ready_ = false;
    this->cancel_drag();
    return true;
  }
  if (!this->selected_ || this->open_generation_ != this->admission_generation_)
    return false;
  this->view_ready_ = true;
  return true;
}

bool MaterialEntityControl::can_input_(uint32_t now_ms) {
  if (this->in_callback_)
    return false;
  this->tick(now_ms);
  return this->enabled_ && this->selected_ && this->view_ready_ && !this->command_pending_;
}

bool MaterialEntityControl::set_scalar_ready(bool ready) {
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  if (ready && this->is_number())
    return false;
#endif
  if (this->in_callback_ || (ready && !this->selected_))
    return false;
  this->scalar_ready_ = ready;
  if (!ready)
    this->cancel_drag();
  return true;
}

bool MaterialEntityControl::begin_drag(uint32_t pointer_id, uint32_t now_ms) {
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  if (this->is_number())
    return false;
#endif
  if (!this->can_input_(now_ms) || !this->scalar_ready_ || this->dragging_ || !this->state_.caps.brightness)
    return false;
  this->dragging_ = true;
  this->pointer_id_ = pointer_id;
  this->drag_start_ = this->candidate_;
  this->drag_start_known_ = this->candidate_known_;
  return true;
}

bool MaterialEntityControl::update_candidate(uint32_t pointer_id, int32_t value, uint32_t now_ms) {
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  if (this->is_number())
    return false;
#endif
  if (!this->can_input_(now_ms) || !this->dragging_ || pointer_id != this->pointer_id_ || value < 0 || value > 100)
    return false;
  if (this->candidate_known_ && value == this->candidate_)
    return true;
  this->candidate_ = static_cast<uint8_t>(value);
  this->candidate_known_ = true;
  this->notify_(ControlViewEvent::CONTROL_VIEW_EVENT_PREVIEW);
  return true;
}

bool MaterialEntityControl::release(uint32_t pointer_id, uint32_t now_ms) {
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  if (this->is_number())
    return false;
#endif
  if (!this->can_input_(now_ms) || !this->dragging_ || pointer_id != this->pointer_id_)
    return false;
  this->dragging_ = false;
  if (!this->candidate_known_ || (this->drag_start_known_ && this->candidate_ == this->drag_start_)) {
    this->sync_candidate_();
    this->notify_(ControlViewEvent::CONTROL_VIEW_EVENT_CANCELLED);
    return true;
  }
  this->command_at_ = now_ms;
  return this->send_command_(ControlOperation::CONTROL_OPERATION_BRIGHTNESS, this->candidate_);
}

bool MaterialEntityControl::command(ControlOperation operation, uint32_t now_ms) {
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  if (this->is_number())
    return false;
#endif
  if (!this->can_input_(now_ms) || this->dragging_ ||
      (operation != ControlOperation::CONTROL_OPERATION_TURN_ON &&
       operation != ControlOperation::CONTROL_OPERATION_TURN_OFF))
    return false;
  this->command_at_ = now_ms;
  return this->send_command_(operation, 0);
}

bool MaterialEntityControl::send_command_(ControlOperation operation, uint8_t value) {
  if (this->command_seq_ == CONTROL_MAX_SEQUENCE ||
      (operation == ControlOperation::CONTROL_OPERATION_BRIGHTNESS && !this->state_.caps.brightness) ||
      (operation == ControlOperation::CONTROL_OPERATION_TURN_ON && !this->state_.caps.turn_on) ||
      (operation == ControlOperation::CONTROL_OPERATION_TURN_OFF && !this->state_.caps.turn_off))
    return false;
  ControlMessage message;
  message.kind = ControlKind::CONTROL_KIND_COMMAND;
  message.identity = this->identity_;
  message.command_seq = ++this->command_seq_;
  message.operation = operation;
  message.value = value;
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

bool MaterialEntityControl::cancel_drag() {
  if (this->in_callback_)
    return false;
  if (!this->dragging_)
    return true;
  this->dragging_ = false;
  this->sync_candidate_();
  this->notify_(ControlViewEvent::CONTROL_VIEW_EVENT_CANCELLED);
  return true;
}

bool MaterialEntityControl::revoke_(ControlReason reason, bool notify_peer) {
  ControlMessage close;
  close.kind = this->open_pending_ ? ControlKind::CONTROL_KIND_CANCEL_OPEN : ControlKind::CONTROL_KIND_CLOSE;
  close.identity = this->identity_;
  close.reason = reason;
  const bool had_selection = this->selected_ || this->open_pending_;
  this->selected_ = this->open_pending_ = this->view_ready_ = this->scalar_ready_ = this->dragging_ =
      this->command_pending_ = false;
  this->candidate_known_ = false;
  this->last_reason_ = reason;
  this->admission_generation_++;
  if (had_selection && notify_peer)
    this->emit_(close);
  if (had_selection)
    this->notify_(ControlViewEvent::CONTROL_VIEW_EVENT_CLOSED);
  return had_selection;
}

bool MaterialEntityControl::invalidate_admission(ControlReason reason) {
  if (this->in_callback_)
    return false;
  this->revoke_(reason, true);
  return true;
}

bool MaterialEntityControl::disconnect() {
  if (this->in_callback_)
    return false;
  this->revoke_(ControlReason::CONTROL_REASON_CONNECTION_LOST, false);
  this->channel_ = {};
  return true;
}

bool MaterialEntityControl::tick(uint32_t now_ms) {
  if (this->in_callback_)
    return false;
  if (this->selected_ && static_cast<uint32_t>(now_ms - this->renewed_at_) >= CONTROL_LEASE_MS)
    this->revoke_(ControlReason::CONTROL_REASON_EXPIRED, true);
  if (this->open_pending_ && static_cast<uint32_t>(now_ms - this->opened_at_) >= CONTROL_LEASE_MS)
    this->revoke_(ControlReason::CONTROL_REASON_EXPIRED, true);
  // The owner cannot identify which transport/HA phase timed out. Do not
  // invent service failure or automatically resend an ambiguous operation.
  if (this->command_pending_ && static_cast<uint32_t>(now_ms - this->command_at_) >= 13000) {
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
    if (this->is_number()) {
      // An ambiguous Number timeout cannot reopen inputs on an old index.
      this->revoke_(ControlReason::CONTROL_REASON_EXPIRED, true);
      this->last_result_ = ControlResult::CONTROL_RESULT_NOT_CONFIRMED;
      this->notify_(ControlViewEvent::CONTROL_VIEW_EVENT_RESULT);
      return true;
    }
#endif
    this->command_pending_ = false;
    this->last_result_ = ControlResult::CONTROL_RESULT_NOT_CONFIRMED;
    this->last_reason_ = ControlReason::CONTROL_REASON_NONE;
    this->sync_candidate_();
    this->notify_(ControlViewEvent::CONTROL_VIEW_EVENT_RESULT);
  }
  return true;
}

}  // namespace esphome::lvgl_material
