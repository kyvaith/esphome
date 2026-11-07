#include "material_entity_control.h"

#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER

namespace esphome::lvgl_material {

bool control_valid_number_snapshot(const ControlSnapshot &snapshot) {
  const auto *number_payload = snapshot.family_if<ControlNumberSnapshot>();
  if (number_payload == nullptr)
    return false;
  const auto &number = *number_payload;
  bool domain_valid = number.domain.valid();
#ifdef USE_LVGL_MATERIAL_THERMOSTAT
  domain_valid |= number.thermostat.present && !number.available && number.domain == MaterialNumberDomain{};
#endif
  if (snapshot.state_seq == 0 || snapshot.state_seq > CONTROL_MAX_SEQUENCE || snapshot.ttl_ms != CONTROL_LEASE_MS ||
      !number_plain_utf8(snapshot.title.view()) || number.state_generation == 0 ||
      number.state_generation > CONTROL_MAX_SEQUENCE || !domain_valid || snapshot.on || snapshot.brightness_known ||
      snapshot.caps.brightness || snapshot.caps.turn_on || snapshot.caps.turn_off)
    return false;
  if (!number.available)
    return number.value_text.view() == "--" && number.index == 0;
  int64_t value;
  char canonical[24];
  return number.domain.value_at(number.index, value) && number_format_decimal(value, canonical, sizeof(canonical)) &&
         number.value_text.view() == canonical;
}

bool control_valid_generic_result(ControlResult result, ControlReason reason,
                                  [[maybe_unused]] bool allow_service_completed,
                                  [[maybe_unused]] bool allow_service_dispatched) {
  return control_valid_result(result, reason) ||
#if defined(USE_LVGL_MATERIAL_ROBOT_CONTROLS) || defined(USE_LVGL_MATERIAL_PANEL_CONTROLS)
         (allow_service_completed && result == ControlResult::CONTROL_RESULT_SERVICE_COMPLETED &&
          reason == ControlReason::CONTROL_REASON_NONE) ||
#endif
#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS
         (allow_service_dispatched && result == ControlResult::CONTROL_RESULT_SERVICE_DISPATCHED &&
          reason == ControlReason::CONTROL_REASON_NONE) ||
#endif
         (result == ControlResult::CONTROL_RESULT_REJECTED && reason == ControlReason::CONTROL_REASON_STALE_STATE);
}

bool MaterialEntityControl::bind_generic(const ControlText<32> &channel) { return this->bind_mode_(channel, true); }

bool MaterialEntityControl::bind_mode_(const ControlText<32> &channel, bool generic) {
  if (this->in_callback_ || !this->enabled_ || !control_valid_hex(channel))
    return false;
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
  this->detail_peer_profiles_ = 0;
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
  this->detail_color_peer_ = false;
  this->clear_detail_color_draft_();
#endif
  if (this->is_detail())
    this->revoke_(ControlReason::CONTROL_REASON_CONNECTION_LOST, false);
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
  this->panel_peer_types_ = 0;
  if (this->is_panel())
    this->revoke_(ControlReason::CONTROL_REASON_CONNECTION_LOST, false);
#endif
#ifdef USE_LVGL_MATERIAL_EXTRA_CONTROLS
  this->extra_peer_profiles_ = 0;
#ifdef USE_LVGL_MATERIAL_INFO_AGENDA
  this->agenda_peer_ = false;
#endif
#ifdef USE_LVGL_MATERIAL_INFO_STATISTICS
  this->statistics_peer_ = false;
#endif
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
  this->action_parameters_peer_ = false;
#endif

  if (this->is_extra()
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
      || this->is_text()
#endif
#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
      || this->is_datetime()
#endif
#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS
      || this->is_action()
#endif
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
      || this->is_media()
#endif
  )
    this->revoke_(ControlReason::CONTROL_REASON_CONNECTION_LOST, false);
#endif
  if (channel == this->channel_ && generic == this->generic_)
    return true;
  this->revoke_(ControlReason::CONTROL_REASON_CONNECTION_LOST, false);
  this->channel_ = channel;
  this->generic_ = generic;
#ifdef USE_LVGL_MATERIAL_COVER_PRIORITY_STOP
  this->cover_priority_stop_peer_ = false;
#ifdef USE_LVGL_MATERIAL_COVER_PANEL
  this->cover_panel_peer_ = false;
#endif
#endif
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
  this->choice_empty_peer_ = false;
#endif
  this->open_seq_ = 0;
  return true;
}

bool MaterialEntityControl::validate_snapshot_(const ControlMessage &message) const {
  if (!message.state.matches_family(message.identity.context.control_kind))
    return false;
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
  if (message.state.family_if<ControlTextSnapshot>() != nullptr || this->is_text())
    return message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_PANEL &&
           this->text_storage_ != nullptr && (this->extra_peer_profiles_ & 128U) != 0 &&
           control_valid_text_metadata(message.state);
#endif
#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
  if (message.state.family_if<ControlDatetimeSnapshot>() != nullptr || this->is_datetime())
    return message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_PANEL &&
           (this->extra_peer_profiles_ & 64U) != 0 && control_valid_datetime_snapshot(message.state);
#endif
#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS
  if (message.state.family_if<ControlActionSnapshot>() != nullptr || this->is_action()) {
    const auto *action = message.state.family_if<ControlActionSnapshot>();
    return message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_PANEL && action != nullptr &&
           (this->extra_peer_profiles_ & action->profile()) == action->profile() &&
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
           (action->parameters_version == 0 || this->action_parameters_peer_) &&
#endif
           control_valid_action_snapshot(message.state);
  }
#endif
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
  if (message.state.family_if<ControlMediaSnapshot>() != nullptr || this->is_media())
    return this->validate_media_snapshot_(message);
#endif
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
  if (message.state.family_if<ControlDetailSnapshot>() != nullptr || this->detail_open_ || this->is_detail())
    return this->validate_detail_snapshot_(message);
#endif
#ifdef USE_LVGL_MATERIAL_EXTRA_CONTROLS
  if (message.state.family_if<ControlExtraSnapshot>() != nullptr || this->is_extra()) {
    const auto *extra = message.state.family_if<ControlExtraSnapshot>();
    return extra != nullptr && control_valid_extra_snapshot(message.state) &&
#ifdef USE_LVGL_MATERIAL_INFO_AGENDA
           (extra->type != 7 ||
            (this->agenda_peer_ && (message.kind != ControlKind::CONTROL_KIND_GRANT ||
                                    (std::get<ControlExtraSnapshot::Agenda>(extra->data).request_seq == 0 &&
                                     (std::get<ControlExtraSnapshot::Agenda>(extra->data).index == 0 ||
                                      std::get<ControlExtraSnapshot::Agenda>(extra->data).count == 0))))) &&
#endif
#ifdef USE_LVGL_MATERIAL_INFO_STATISTICS
           (extra->type != 8 || this->statistics_peer_) &&
#endif
           (this->extra_peer_profiles_ & extra->profile()) == extra->profile();
  }
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
  if (this->is_panel())
    return control_valid_panel_snapshot(message.state) &&
           (this->panel_peer_types_ & (1U << message.state.family_if<ControlSnapshot::Panel>()->type));
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
  if (this->is_robot())
    return control_valid_robot_snapshot(message.state);
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS
  if (this->is_humidifier())
    return control_valid_humidifier_snapshot(message.state);
#endif
#ifdef USE_LVGL_MATERIAL_FAN_CONTROLS
  if (this->is_fan())
    return control_valid_fan_snapshot(message.state);
#endif
#ifdef USE_LVGL_MATERIAL_COVER_CONTROLS
  if (this->is_cover())
    return control_valid_cover_snapshot(message.state, this->cover_panel_active());
#endif
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
  if (this->is_choice())
    return control_valid_choice_snapshot(message.state, this->choice_empty_active());
#endif
  return this->is_number() ? control_valid_number_snapshot(message.state) : control_valid_snapshot(message.state);
}

bool MaterialEntityControl::accept_number_state_(const ControlSnapshot &state) {
  const auto *previous_payload = this->state_.family_if<ControlNumberSnapshot>();
  if (previous_payload == nullptr)
    return false;
  const auto &previous = *previous_payload;
  const auto *next_payload = state.family_if<ControlNumberSnapshot>();
  if (next_payload == nullptr)
    return false;
  const auto &next = *next_payload;
  if (!(previous.domain == next.domain)
#ifdef USE_LVGL_MATERIAL_THERMOSTAT
      || previous.thermostat.present != next.thermostat.present ||
      previous.thermostat.fahrenheit != next.thermostat.fahrenheit || previous.thermostat.dial != next.thermostat.dial
#endif
  ) {
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
  if (this->state_.family_if<ControlNumberSnapshot>() == nullptr || !this->is_number() || !this->selected_ ||
      !this->view_ready_ || this->command_pending_ || !this->state_.family_if<ControlNumberSnapshot>()->available)
    return false;
  if (direction == -1)
    return this->state_.family_if<ControlNumberSnapshot>()->index != 0;
  return direction == 1 && this->state_.family_if<ControlNumberSnapshot>()->index <
                               this->state_.family_if<ControlNumberSnapshot>()->domain.max_index;
}

bool MaterialEntityControl::begin_step(uint32_t pointer_id, int8_t direction, uint32_t now_ms) {
  if (!this->can_step(direction) || !this->begin_number_input_(pointer_id, now_ms))
    return false;
  this->step_direction_ = direction;
  return true;
}

bool MaterialEntityControl::begin_number_input_(uint32_t pointer_id, uint32_t now_ms) {
  if (!this->can_input_(now_ms) || this->dragging_ || !this->is_number() ||
      this->state_.family_if<ControlNumberSnapshot>() == nullptr)
    return false;
  this->pointer_id_ = pointer_id;
  this->pressed_index_ = this->state_.family_if<ControlNumberSnapshot>()->index;
  this->pressed_generation_ = this->state_.family_if<ControlNumberSnapshot>()->state_generation;
  this->dragging_ = true;
  return true;
}

bool MaterialEntityControl::release_step(uint32_t pointer_id, uint32_t now_ms) {
  ControlMessage message;
  message.operation = ControlOperation::CONTROL_OPERATION_STEP;
  message.direction = this->step_direction_;
  return this->release_number_input_(pointer_id, message, now_ms);
}

#ifdef USE_LVGL_MATERIAL_THERMOSTAT
bool MaterialEntityControl::can_drag_thermostat() const {
  const auto *number_payload = this->state_.family_if<ControlNumberSnapshot>();
  if (number_payload == nullptr)
    return false;
  const auto &number = *number_payload;
  return this->is_number() && this->selected_ && this->view_ready_ && !this->command_pending_ &&
         number.thermostat.dial && number.available && number.domain.max_index != 0;
}

bool MaterialEntityControl::begin_thermostat_drag(uint32_t pointer_id, uint32_t now_ms) {
  return this->can_drag_thermostat() && this->begin_number_input_(pointer_id, now_ms);
}

bool MaterialEntityControl::release_thermostat_drag(uint32_t pointer_id, uint32_t index, uint32_t now_ms) {
  ControlMessage message;
  message.operation = ControlOperation::CONTROL_OPERATION_THERMOSTAT_TARGET;
  message.thermostat_target_index = index;
  return this->release_number_input_(pointer_id, message, now_ms);
}
#endif

bool MaterialEntityControl::release_number_input_(uint32_t pointer_id, ControlMessage &message, uint32_t now_ms) {
  if (!this->can_input_(now_ms) || !this->is_number() || !this->dragging_ || this->pointer_id_ != pointer_id ||
      this->state_.family_if<ControlNumberSnapshot>() == nullptr)
    return false;
  this->dragging_ = false;
  if (this->state_.family_if<ControlNumberSnapshot>()->index != this->pressed_index_ ||
      this->state_.family_if<ControlNumberSnapshot>()->state_generation != this->pressed_generation_ ||
      this->command_seq_ == CONTROL_MAX_SEQUENCE)
    return false;
#ifdef USE_LVGL_MATERIAL_THERMOSTAT
  if (message.operation == ControlOperation::CONTROL_OPERATION_THERMOSTAT_TARGET) {
    if (!this->can_drag_thermostat() || message.thermostat_target_index == this->pressed_index_ ||
        message.thermostat_target_index > this->state_.family_if<ControlNumberSnapshot>()->domain.max_index)
      return false;
  } else
#endif
      if (!this->can_step(message.direction))
    return false;
  message.kind = ControlKind::CONTROL_KIND_COMMAND;
  message.identity = this->identity_;
  message.command_seq = ++this->command_seq_;
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
