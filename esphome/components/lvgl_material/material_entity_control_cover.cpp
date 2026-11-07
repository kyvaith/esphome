#include "material_entity_control.h"

#ifdef USE_LVGL_MATERIAL_COVER_CONTROLS

namespace esphome::lvgl_material {
namespace {
[[maybe_unused]] bool cover_stop_matches(ControlOperation stop, ControlOperation movement) {
  if (stop == ControlOperation::CONTROL_OPERATION_COVER_STOP)
    return movement == ControlOperation::CONTROL_OPERATION_COVER_OPEN ||
           movement == ControlOperation::CONTROL_OPERATION_COVER_CLOSE
#ifdef USE_LVGL_MATERIAL_COVER_PANEL
           || movement == ControlOperation::CONTROL_OPERATION_COVER_SET_POSITION
#endif
        ;
#ifdef USE_LVGL_MATERIAL_COVER_PANEL
  if (stop == ControlOperation::CONTROL_OPERATION_COVER_STOP_TILT)
    return movement == ControlOperation::CONTROL_OPERATION_COVER_OPEN_TILT ||
           movement == ControlOperation::CONTROL_OPERATION_COVER_CLOSE_TILT ||
           movement == ControlOperation::CONTROL_OPERATION_COVER_SET_TILT_POSITION;
#endif
  return false;
}
[[maybe_unused]] bool cover_is_stop(ControlOperation operation) {
  return operation == ControlOperation::CONTROL_OPERATION_COVER_STOP
#ifdef USE_LVGL_MATERIAL_COVER_PANEL
         || operation == ControlOperation::CONTROL_OPERATION_COVER_STOP_TILT
#endif
      ;
}
#ifdef USE_LVGL_MATERIAL_COVER_PANEL
bool cover_is_target(ControlOperation operation) {
  return operation == ControlOperation::CONTROL_OPERATION_COVER_SET_POSITION ||
         operation == ControlOperation::CONTROL_OPERATION_COVER_SET_TILT_POSITION;
}
#endif
}  // namespace

#ifdef USE_LVGL_MATERIAL_COVER_PRIORITY_STOP
bool MaterialEntityControl::set_cover_priority_stop_enabled(bool enabled) {
  if (this->in_callback_ || this->selected_ || this->open_pending_)
    return false;
  this->cover_priority_stop_configured_ = enabled;
  this->cover_priority_stop_peer_ = false;
#ifdef USE_LVGL_MATERIAL_COVER_PANEL
  this->cover_panel_peer_ = false;
#endif
  return true;
}

bool MaterialEntityControl::accept_cover_features(const ControlText<32> &channel, uint32_t version) {
  if (this->in_callback_ || !this->enabled_ || !this->generic_ || !this->cover_priority_stop_supported() ||
      !control_valid_hex(channel) || channel != this->channel_ || version != 1 || this->selected_ ||
      this->open_pending_)
    return false;
  this->cover_priority_stop_peer_ = true;
  return true;
}
#endif

bool control_valid_cover_snapshot(const ControlSnapshot &snapshot, bool allow_panel) {
  const auto *cover_payload = snapshot.family_if<ControlCoverSnapshot>();
  if (cover_payload == nullptr)
    return false;
  const auto &cover = *cover_payload;
  const auto state = static_cast<uint8_t>(cover.state);
  bool capability = cover.caps.open || cover.caps.stop || cover.caps.close;
#ifdef USE_LVGL_MATERIAL_COVER_PANEL
  if (cover.panel.present != allow_panel)
    return false;
  if (cover.panel.present) {
    const auto &panel = cover.panel;
    if (panel.device_class > 10 || (!panel.tilt_known && panel.tilt_position != 0) ||
        (panel.tilt_known && (!cover.available || panel.tilt_position > 100)))
      return false;
    capability |= panel.caps.set_position || panel.caps.open_tilt || panel.caps.close_tilt || panel.caps.stop_tilt ||
                  panel.caps.set_tilt_position;
  } else if (!(cover.panel == ControlCoverSnapshot::Panel{})) {
    return false;
  }
#else
  if (allow_panel)
    return false;
#endif
  return snapshot.state_seq >= 1 && snapshot.state_seq <= CONTROL_MAX_SEQUENCE && snapshot.ttl_ms == CONTROL_LEASE_MS &&
         number_plain_utf8(snapshot.title.view()) && cover.state_generation >= 1 &&
         cover.state_generation <= CONTROL_MAX_SEQUENCE &&
         state <= static_cast<uint8_t>(ControlCoverState::CONTROL_COVER_STATE_CLOSING) &&
         cover.available == (cover.state != ControlCoverState::CONTROL_COVER_STATE_UNKNOWN) &&
         (!cover.position_known || (cover.available && cover.position <= 100)) &&
         (cover.position_known || cover.position == 0) && capability && !snapshot.on && !snapshot.brightness_known &&
         !snapshot.caps.brightness && !snapshot.caps.turn_on && !snapshot.caps.turn_off;
}

bool MaterialEntityControl::set_cover_enabled(bool enabled) {
  if (this->in_callback_)
    return false;
  if (!enabled && this->is_cover())
    this->revoke_(ControlReason::CONTROL_REASON_UNSUPPORTED, true);
  this->cover_enabled_ = enabled;
  return true;
}

bool MaterialEntityControl::accept_cover_state_(const ControlSnapshot &state) {
  const auto *previous_payload = this->state_.family_if<ControlCoverSnapshot>();
  if (previous_payload == nullptr)
    return false;
  const auto &previous = *previous_payload;
  const auto *next_payload = state.family_if<ControlCoverSnapshot>();
  if (next_payload == nullptr)
    return false;
  const auto &next = *next_payload;
  if (!(previous.caps == next.caps)
#ifdef USE_LVGL_MATERIAL_COVER_PANEL
      || previous.panel.present != next.panel.present || previous.panel.device_class != next.panel.device_class ||
      !(previous.panel.caps == next.panel.caps)
#endif
  ) {
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
  // Stop is an absolute non-movement intent in the same admitted selection.
  // Position feedback must not make a moving cover impossible to stop. Unknown
  // state and every metadata change still retire the contact.
#ifdef USE_LVGL_MATERIAL_COVER_PRIORITY_STOP
  if (this->dragging_ && this->cover_priority_stop_active() && next.available &&
      cover_is_stop(this->pressed_cover_operation_)) {
    this->pressed_generation_ = next.state_generation;
    return true;
  }
#endif
  // Includes coalesced A -> B -> A for movement buttons.
  this->cancel_drag();
  return true;
}

bool MaterialEntityControl::can_cover(ControlOperation operation) const {
  const auto *cover_payload = this->state_.family_if<ControlCoverSnapshot>();
  if (cover_payload == nullptr)
    return false;
  const auto &cover = *cover_payload;
  if (!this->is_cover() || !this->cover_enabled_ || !this->selected_ || !this->view_ready_ || !cover.available)
    return false;
  if (this->command_pending_) {
#ifdef USE_LVGL_MATERIAL_COVER_PRIORITY_STOP
    const bool supported_stop = (operation == ControlOperation::CONTROL_OPERATION_COVER_STOP && cover.caps.stop)
#ifdef USE_LVGL_MATERIAL_COVER_PANEL
                                || (operation == ControlOperation::CONTROL_OPERATION_COVER_STOP_TILT &&
                                    this->cover_panel_active() && cover.panel.caps.stop_tilt)
#endif
        ;
    return supported_stop && this->cover_priority_stop_active() &&
           cover_stop_matches(operation, this->pending_cover_operation_);
#else
    return false;
#endif
  }
  const bool opening = cover.state == ControlCoverState::CONTROL_COVER_STATE_OPENING;
  const bool closing = cover.state == ControlCoverState::CONTROL_COVER_STATE_CLOSING;
#ifdef USE_LVGL_MATERIAL_COVER_PANEL
  if (this->cover_panel_active()) {
    switch (operation) {
      case ControlOperation::CONTROL_OPERATION_COVER_SET_POSITION:
        return cover.panel.caps.set_position && cover.position_known;
      case ControlOperation::CONTROL_OPERATION_COVER_SET_TILT_POSITION:
        return cover.panel.caps.set_tilt_position && cover.panel.tilt_known;
      case ControlOperation::CONTROL_OPERATION_COVER_OPEN_TILT:
        return cover.panel.caps.open_tilt && (!cover.panel.tilt_known || cover.panel.tilt_position != 100);
      case ControlOperation::CONTROL_OPERATION_COVER_CLOSE_TILT:
        return cover.panel.caps.close_tilt && (!cover.panel.tilt_known || cover.panel.tilt_position != 0);
      case ControlOperation::CONTROL_OPERATION_COVER_STOP_TILT:
        return cover.panel.caps.stop_tilt;
      default:
        break;
    }
  }
#endif
  switch (operation) {
    case ControlOperation::CONTROL_OPERATION_COVER_OPEN:
      return cover.caps.open && !opening &&
             (closing || cover.state != ControlCoverState::CONTROL_COVER_STATE_OPEN || !cover.position_known ||
              cover.position != 100);
    case ControlOperation::CONTROL_OPERATION_COVER_CLOSE:
      return cover.caps.close && !closing && cover.state != ControlCoverState::CONTROL_COVER_STATE_CLOSED;
    case ControlOperation::CONTROL_OPERATION_COVER_STOP:
      return cover.caps.stop && (opening || closing);
    default:
      return false;
  }
}

bool MaterialEntityControl::begin_cover(uint32_t pointer_id, ControlOperation operation, uint32_t now_ms) {
#ifdef USE_LVGL_MATERIAL_COVER_PANEL
  if (cover_is_target(operation))
    return false;
#endif
  return this->begin_cover_input_(pointer_id, operation, now_ms);
}

bool MaterialEntityControl::begin_cover_input_(uint32_t pointer_id, ControlOperation operation, uint32_t now_ms) {
  if (this->in_callback_)
    return false;
  this->tick(now_ms);
  if (!this->enabled_ || this->dragging_ || !this->can_cover(operation))
    return false;
  this->pointer_id_ = pointer_id;
  this->pressed_generation_ = this->state_.family_if<ControlCoverSnapshot>()->state_generation;
  this->pressed_cover_operation_ = operation;
  this->dragging_ = true;
  return true;
}

bool MaterialEntityControl::release_cover(uint32_t pointer_id, uint32_t now_ms) {
  if (this->in_callback_)
    return false;
  this->tick(now_ms);
  if (!this->enabled_ || !this->is_cover() || !this->dragging_ || this->pointer_id_ != pointer_id)
    return false;
  this->dragging_ = false;
  if (!this->can_cover(this->pressed_cover_operation_) ||
      this->state_.family_if<ControlCoverSnapshot>()->state_generation != this->pressed_generation_ ||
      this->command_seq_ == CONTROL_MAX_SEQUENCE)
    return false;
  ControlMessage message;
  message.kind = ControlKind::CONTROL_KIND_COMMAND;
  message.identity = this->identity_;
  message.operation = this->pressed_cover_operation_;
#ifdef USE_LVGL_MATERIAL_COVER_PANEL
  if (cover_is_target(message.operation)) {
    const auto *cover_payload = this->state_.family_if<ControlCoverSnapshot>();
    if (cover_payload == nullptr)
      return false;
    const auto &cover = *cover_payload;
    const uint8_t current = message.operation == ControlOperation::CONTROL_OPERATION_COVER_SET_POSITION
                                ? cover.position
                                : cover.panel.tilt_position;
    if (this->pressed_cover_target_ == current || this->pressed_cover_target_ > 100)
      return false;
    message.cover_target_index = this->pressed_cover_target_;
  }
#endif
  message.command_seq = ++this->command_seq_;
  message.expected_state_generation = this->pressed_generation_;
  this->command_at_ = now_ms;
  this->command_pending_ = true;
  this->pending_cover_operation_ = message.operation;
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

#ifdef USE_LVGL_MATERIAL_COVER_PANEL
bool MaterialEntityControl::set_cover_panel_enabled(bool enabled) {
  if (this->in_callback_ || this->selected_ || this->open_pending_)
    return false;
  this->cover_panel_configured_ = enabled;
  this->cover_panel_peer_ = false;
  return true;
}

bool MaterialEntityControl::accept_cover_panel_features(const ControlText<32> &channel, uint32_t version) {
  if (this->in_callback_ || !this->cover_panel_supported() || !this->cover_priority_stop_active() ||
      !control_valid_hex(channel) || channel != this->channel_ || version != 1 || this->selected_ ||
      this->open_pending_)
    return false;
  this->cover_panel_peer_ = true;
  return true;
}

bool MaterialEntityControl::begin_cover_target(uint32_t pointer, bool tilt, uint8_t target, uint32_t now) {
  const auto operation = tilt ? ControlOperation::CONTROL_OPERATION_COVER_SET_TILT_POSITION
                              : ControlOperation::CONTROL_OPERATION_COVER_SET_POSITION;
  if (target > 100 || !this->begin_cover_input_(pointer, operation, now))
    return false;
  this->pressed_cover_target_ = target;
  return true;
}

bool MaterialEntityControl::update_cover_target(uint32_t pointer, uint8_t target, uint32_t now) {
  if (!this->can_input_(now) || !this->dragging_ || this->pointer_id_ != pointer || target > 100 ||
      !cover_is_target(this->pressed_cover_operation_) || !this->can_cover(this->pressed_cover_operation_))
    return false;
  this->pressed_cover_target_ = target;
  return true;
}
#endif

}  // namespace esphome::lvgl_material
#endif
