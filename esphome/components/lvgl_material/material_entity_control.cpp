#include "material_entity_control.h"

namespace esphome::lvgl_material {

bool MaterialEntityControl::emit_(ControlMessage &message
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
                                  ,
                                  ControlTextValueView text_value
#endif
) {
  this->in_callback_ = true;
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  // Each caller owns its local outgoing message. Set its wire mode in place;
  // copying the full snapshot here multiplies stack use during nested revoke.
  if (this->generic_)
    message.generic = true;
#endif
  const bool accepted =
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
      message.kind == ControlKind::CONTROL_KIND_COMMAND &&
              message.operation == ControlOperation::CONTROL_OPERATION_TEXT_SET
          ? this->sink_.emit_text(message, text_value)
          :
#endif
          this->sink_.emit(message);
  this->in_callback_ = false;
  return accepted;
}

void MaterialEntityControl::notify_(ControlViewEvent event) {
  this->in_callback_ = true;
  this->sink_.view_event(event);
  this->in_callback_ = false;
}

void MaterialEntityControl::sync_candidate_() {
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
  if (this->is_media()) {
    const auto *media = this->state_.family_if<ControlMediaSnapshot>();
    const auto *volume = std::get_if<ControlMediaSnapshot::Volume>(&media->data);
    this->candidate_known_ = volume != nullptr && volume->known;
    this->candidate_ = this->candidate_known_ ? (volume->rail + 50) / 100 : 0;
    return;
  }
#endif
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
  if (this->is_choice()) {
    this->sync_choice_candidate_();
    return;
  }
#endif
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
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
    this->panel_peer_types_ = 0;
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

#endif
#endif
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
    this->detail_peer_profiles_ = 0;
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
    this->detail_range_peer_ = false;
#endif
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
    this->detail_color_peer_ = false;
    this->clear_detail_color_draft_();
#endif
#endif
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
  bool supported = context.control_kind == ControlEntityKind::CONTROL_ENTITY_LIGHT ||
                   context.control_kind == ControlEntityKind::CONTROL_ENTITY_NUMBER;
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
  supported |= context.control_kind == ControlEntityKind::CONTROL_ENTITY_CHOICE && this->choice_enabled_;
#endif
#ifdef USE_LVGL_MATERIAL_COVER_CONTROLS
  supported |= context.control_kind == ControlEntityKind::CONTROL_ENTITY_COVER && this->cover_enabled_;
#endif
#ifdef USE_LVGL_MATERIAL_FAN_CONTROLS
  supported |= context.control_kind == ControlEntityKind::CONTROL_ENTITY_FAN && this->fan_enabled_;
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS
  supported |= context.control_kind == ControlEntityKind::CONTROL_ENTITY_HUMIDIFIER && this->humidifier_enabled_;
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
  supported |= context.control_kind == ControlEntityKind::CONTROL_ENTITY_ROBOT && this->robot_enabled_;
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
  supported |=
      context.control_kind == ControlEntityKind::CONTROL_ENTITY_PANEL && (this->panel_peer_types_ != 0
#ifdef USE_LVGL_MATERIAL_EXTRA_CONTROLS
                                                                          || this->extra_peer_profiles_ != 0
#endif
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
                                                                          || (this->detail_peer_profiles_ & 2) != 0
#endif
                                                                         );
#endif
  if (!supported || (!this->generic_ && context.control_kind != ControlEntityKind::CONTROL_ENTITY_LIGHT))
    return false;
#endif
  this->revoke_(ControlReason::CONTROL_REASON_SELECTION_CHANGED, true);
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
  this->media_view_sequence_ = 0;
  this->media_view_pending_ = this->media_view_ready_ = false;
#endif
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
  this->detail_open_ = this->detail_open_requested_;
  this->detail_view_sequence_ = 0;
  this->detail_view_pending_ = this->detail_view_ready_ = false;
#endif
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
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
  request.detail_open = this->detail_open_;
#endif
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

bool MaterialEntityControl::receive(const ControlMessage &message, uint32_t now_ms
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
                                    ,
                                    ControlTextValueView text_value
#endif
) {
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
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
  if (message.kind == ControlKind::CONTROL_KIND_GRANT || message.kind == ControlKind::CONTROL_KIND_STATE) {
    if (message.state.family_if<ControlTextSnapshot>() != nullptr) {
      if (this->text_storage_ == nullptr ||
          !control_valid_text_snapshot(message.state, text_value, this->text_storage_->repertoire,
                                       this->text_storage_->repertoire_size))
        return false;
    } else if (text_value.present || !text_value.value.empty()) {
      return false;
    }
  } else if (text_value.present || !text_value.value.empty()) {
    return false;
  }
#endif
  if (message.kind == ControlKind::CONTROL_KIND_GRANT) {
    if (!this->open_pending_ || this->selected_ || !control_valid_hex(message.identity.token) ||
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
        !this->validate_snapshot_(message))
#else
        !control_valid_snapshot(message.state))
#endif
      return false;
#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
    this->datetime_ = {};
#endif
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
    this->commit_text_state_(message.state, text_value, true);
#endif
    this->identity_ = message.identity;
    this->state_ = message.state;
    this->open_pending_ = false;
    this->selected_ = true;
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
    this->reset_action_parameter_(this->state_.family_if<ControlActionSnapshot>());
#endif

    this->view_ready_ = false;
    this->scalar_ready_ = false;
#ifdef USE_LVGL_MATERIAL_INFO_AGENDA
    this->agenda_sequence_ = this->agenda_flags_ = 0;
#endif
    this->command_seq_ = 0;
    this->command_pending_ = false;
    this->last_result_ = ControlResult::CONTROL_RESULT_PENDING;
    this->last_reason_ = ControlReason::CONTROL_REASON_NONE;
    this->renewed_at_ = now_ms;
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
    this->media_view_ready_ = message.state.family_if<ControlMediaSnapshot>() != nullptr;
#endif
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
    this->detail_view_ready_ = message.state.family_if<ControlDetailSnapshot>() != nullptr;
#endif
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
    this->choice_candidate_generation_ = 0;
#endif
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
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
  if (message.kind == ControlKind::CONTROL_KIND_FACET_RESULT && this->is_media())
    return this->receive_media_facet_result_(message);
#endif
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
  if (message.kind == ControlKind::CONTROL_KIND_FACET_RESULT)
    return this->receive_facet_result_(message);
#endif
#ifdef USE_LVGL_MATERIAL_INFO_AGENDA
  if (message.kind == ControlKind::CONTROL_KIND_PAGE_RESULT)
    return this->receive_agenda_result_(message);
#endif
  if (message.kind == ControlKind::CONTROL_KIND_STATE) {
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
    if (!this->validate_snapshot_(message) || message.state.state_seq <= this->state_.state_seq)
      return false;
#ifdef USE_LVGL_MATERIAL_COVER_CONTROLS
    if (this->is_cover() && !this->accept_cover_state_(message.state))
      return false;
#endif
#ifdef USE_LVGL_MATERIAL_FAN_CONTROLS
    if (this->is_fan() && !this->accept_fan_state_(message.state))
      return false;
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS
    if (this->is_humidifier() && !this->accept_humidifier_state_(message.state))
      return false;
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
    if (this->is_robot() && !this->accept_robot_state_(message.state))
      return false;
#endif
#ifdef USE_LVGL_MATERIAL_EXTRA_CONTROLS
    if (this->is_extra() && !this->accept_extra_state_(message.state))
      return false;
#endif
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
    if (this->is_text() && !this->accept_text_state_(message.state, text_value))
      return false;
#endif
#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
    if (this->is_datetime() && !this->accept_datetime_state_(message.state))
      return false;
#endif
#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS
    if (this->is_action() && !this->accept_action_state_(message.state))
      return false;
#endif
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
    if (this->is_media() && !this->accept_media_state_(message.state))
      return false;
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
    if (this->is_panel()
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
        && !this->is_detail()
#endif
        && !this->accept_panel_state_(message.state))
      return false;
#endif
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
    if (this->is_detail() && !this->accept_detail_state_(message.state))
      return false;
#endif
    if (this->is_number() && !this->accept_number_state_(message.state))
      return false;
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
    if (this->is_choice() && !this->accept_choice_state_(message.state))
      return false;
#endif
#else
    if (!control_valid_snapshot(message.state) || message.state.state_seq <= this->state_.state_seq)
      return false;
#endif
    if (!(message.state.caps == this->state_.caps)) {
      this->revoke_(ControlReason::CONTROL_REASON_CAPABILITIES_CHANGED, true);
      return false;
    }
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
    this->commit_text_state_(message.state, text_value, false);
#endif
    this->state_ = message.state;
#ifdef USE_LVGL_MATERIAL_INFO_AGENDA
    this->complete_agenda_page_();
#endif
    this->renewed_at_ = now_ms;
    if (!this->dragging_)
      this->sync_candidate_();
    this->notify_(ControlViewEvent::CONTROL_VIEW_EVENT_STATE);
    return true;
  }
  if (message.kind == ControlKind::CONTROL_KIND_RESULT) {
#ifdef USE_LVGL_MATERIAL_EXTRA_CONTROLS
    if (this->is_extra() && message.result == ControlResult::CONTROL_RESULT_ALREADY_MATCHED)
      return false;
#endif
    [[maybe_unused]] bool service_only = false;
    [[maybe_unused]] bool dispatch_only = false;
#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS
    service_only |= this->action_service_only_();
    dispatch_only = this->is_action() && (this->extra_peer_profiles_ & 16U) != 0 &&
                    this->state_.family_if<ControlActionSnapshot>()->type == 4 &&
                    this->pending_panel_operation_ == ControlOperation::CONTROL_OPERATION_ACTION_TRIGGER;
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
    dispatch_only |= this->action_parameters_peer_ && this->is_action_parameters() &&
                     this->pending_panel_operation_ == ControlOperation::CONTROL_OPERATION_ACTION_RUN;
#endif
#ifdef USE_LVGL_MATERIAL_TILE_EVENT_CONTROLS
    dispatch_only |= this->is_action() && (this->extra_peer_profiles_ & 280U) == 280U &&
                     this->state_.family_if<ControlActionSnapshot>()->type == ControlActionSnapshot::TILE_EVENT_TYPE &&
                     this->pending_panel_operation_ == ControlOperation::CONTROL_OPERATION_ACTION_PRESS;
#endif
    if (dispatch_only && message.result == ControlResult::CONTROL_RESULT_SERVICE_COMPLETED)
      return false;
#endif
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
    service_only |=
        this->is_media() && (this->media_operation_ == ControlOperation::CONTROL_OPERATION_MEDIA_PREVIOUS ||
                             this->media_operation_ == ControlOperation::CONTROL_OPERATION_MEDIA_NEXT ||
                             this->media_operation_ == ControlOperation::CONTROL_OPERATION_MEDIA_VOLUME_UP ||
                             this->media_operation_ == ControlOperation::CONTROL_OPERATION_MEDIA_VOLUME_DOWN);
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
    service_only |=
        this->is_robot() && (this->pending_robot_operation_ == ControlOperation::CONTROL_OPERATION_ROBOT_LOCATE ||
                             this->pending_robot_operation_ == ControlOperation::CONTROL_OPERATION_ROBOT_AREA);
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
    service_only |= this->is_panel() && this->state_.family_if<ControlSnapshot::Panel>() != nullptr &&
                    this->state_.family_if<ControlSnapshot::Panel>()->type == 0 &&
                    this->pending_panel_operation_ == ControlOperation::CONTROL_OPERATION_PANEL_DURATION;
#endif
    if (service_only && (message.result == ControlResult::CONTROL_RESULT_STATE_MATCHES ||
                         message.result == ControlResult::CONTROL_RESULT_ALREADY_MATCHED))
      return false;
#ifdef USE_LVGL_MATERIAL_COVER_PANEL
    // Standard HA Cover has no independent tilt-motion feedback. A successful
    // service call or unchanged position cannot prove that tilt has stopped.
    if (this->is_cover() && this->cover_panel_active() &&
        this->pending_cover_operation_ == ControlOperation::CONTROL_OPERATION_COVER_STOP_TILT &&
        (message.result == ControlResult::CONTROL_RESULT_STATE_MATCHES ||
         message.result == ControlResult::CONTROL_RESULT_ALREADY_MATCHED))
      return false;
#endif
    if (!this->command_pending_ || message.command_seq != this->command_seq_ || message.state.state_seq == 0 ||
        message.state.state_seq > CONTROL_MAX_SEQUENCE ||
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
        !(this->generic_ ? control_valid_generic_result(message.result, message.reason, service_only, dispatch_only)
                         : control_valid_result(message.result, message.reason)))
#else
        !control_valid_result(message.result, message.reason))
#endif
      return false;
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
    // Ordered HA transport must publish authoritative Number state first.
    // Refuse future acknowledgements, retaining pending ownership; no queue.
    if (this->is_discrete() && message.state.state_seq > this->state_.state_seq)
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
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
      if (this->is_detail_color())
        this->clear_detail_color_draft_();
#endif
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
      if (this->is_text())
        this->clear_text_draft_();
#endif
#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
      if (this->is_datetime())
        this->clear_datetime_draft_();
#endif
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
      this->choice_candidate_generation_ = 0;
#endif
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
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
    this->reset_action_parameter_();
#endif
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
    this->clear_detail_color_draft_();
#endif
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
    if (this->is_text())
      this->clear_text_draft_();
#endif
#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
    if (this->is_datetime())
      this->clear_datetime_draft_();
#endif
    this->cancel_drag();
    return true;
  }
  if (!this->selected_ || this->open_generation_ != this->admission_generation_)
    return false;
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
  if (!this->view_ready_)
    this->reset_action_parameter_(this->state_.family_if<ControlActionSnapshot>());
#endif
  this->view_ready_ = true;
  return true;
}

bool MaterialEntityControl::can_input_(uint32_t now_ms) {
  if (this->in_callback_)
    return false;
  this->tick(now_ms);
  return this->enabled_ && this->selected_ && this->view_ready_ && !this->command_pending_
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
         && (!this->is_media() || (!this->media_view_pending_ && this->media_view_ready_))
#endif
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
         && (!this->is_detail() || (!this->detail_view_pending_ && this->detail_view_ready_))
#endif
      ;
}

bool MaterialEntityControl::set_scalar_ready(bool ready) {
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  if (ready && this->is_discrete()
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
      && !this->is_media_volume()
#endif
  )
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
  if (this->is_discrete())
    return false;
#endif
  if (!this->can_input_(now_ms) || !this->scalar_ready_ || this->dragging_ || !this->state_.caps.brightness)
    return false;
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
  if (this->is_detail()) {
    const auto *detail = this->state_.family_if<ControlDetailSnapshot>();
    if (detail == nullptr)
      return false;
    this->pressed_generation_ = detail->state_generation;
  }
#endif
  this->dragging_ = true;
  this->pointer_id_ = pointer_id;
  this->drag_start_ = this->candidate_;
  this->drag_start_known_ = this->candidate_known_;
  return true;
}

bool MaterialEntityControl::update_candidate(uint32_t pointer_id, int32_t value, uint32_t now_ms) {
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  if (this->is_discrete())
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
  if (this->is_discrete())
    return false;
#endif
  if (!this->can_input_(now_ms) || !this->dragging_ || pointer_id != this->pointer_id_)
    return false;
  this->dragging_ = false;
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
  if (this->is_detail() &&
      (this->state_.family_if<ControlDetailSnapshot>() == nullptr ||
       this->pressed_generation_ != this->state_.family_if<ControlDetailSnapshot>()->state_generation))
    return false;
#endif
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
  if (this->is_discrete())
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
  message.operation = operation;
  message.value = value;
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
  if (this->is_detail()) {
    if (this->state_.family_if<ControlDetailSnapshot>() == nullptr ||
        this->state_.family_if<ControlDetailSnapshot>()->climate ||
        this->state_.family_if<ControlDetailSnapshot>()->facet != ControlFacet::CONTROL_FACET_MAIN ||
        this->detail_view_pending_ || !this->detail_view_ready_)
      return false;
    message.view_seq = this->state_.family_if<ControlDetailSnapshot>()->view_seq;
    message.expected_state_generation = this->state_.family_if<ControlDetailSnapshot>()->state_generation;
    message.detail_open = true;
  }
#endif
  message.command_seq = ++this->command_seq_;
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
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
  this->reset_action_parameter_();
#endif

#ifdef USE_LVGL_MATERIAL_INFO_AGENDA
  this->agenda_flags_ = 0;
  if (this->is_agenda())
    this->state_.clear_family();
#endif
#ifdef USE_LVGL_MATERIAL_INFO_STATISTICS
  if (this->is_statistics())
    this->state_.clear_family();
#endif
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
  this->clear_detail_color_draft_();
#endif
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
  this->clear_text_storage_();
#endif
#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
  this->clear_datetime_draft_();
#endif
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
  this->media_view_pending_ = this->media_view_ready_ = false;
#endif
  ControlMessage close;
  close.kind = this->open_pending_ ? ControlKind::CONTROL_KIND_CANCEL_OPEN : ControlKind::CONTROL_KIND_CLOSE;
  close.identity = this->identity_;
  close.reason = reason;
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
  close.detail_open = this->detail_open_;
  this->detail_open_ = this->detail_view_pending_ = this->detail_view_ready_ = false;
#endif
  const bool had_selection = this->selected_ || this->open_pending_;
  this->selected_ = this->open_pending_ = this->view_ready_ = this->scalar_ready_ = this->dragging_ =
      this->command_pending_ = false;
  this->candidate_known_ = false;
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
  this->choice_candidate_known_ = this->choice_applying_ = false;
  this->choice_candidate_generation_ = 0;
#endif
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
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
  this->panel_peer_types_ = 0;
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

#endif
#endif
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
  this->detail_peer_profiles_ = 0;
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
  this->detail_range_peer_ = false;
#endif
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
  this->detail_color_peer_ = false;
  this->clear_detail_color_draft_();
#endif
#endif
#ifdef USE_LVGL_MATERIAL_COVER_PRIORITY_STOP
  this->cover_priority_stop_peer_ = false;
#ifdef USE_LVGL_MATERIAL_COVER_PANEL
  this->cover_panel_peer_ = false;
#endif
#endif
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
  this->choice_empty_peer_ = false;
#endif
  return true;
}

bool MaterialEntityControl::tick(uint32_t now_ms) {
  if (this->in_callback_)
    return false;
  if (this->selected_ && static_cast<uint32_t>(now_ms - this->renewed_at_) >= CONTROL_LEASE_MS)
    this->revoke_(ControlReason::CONTROL_REASON_EXPIRED, true);
  if (this->open_pending_ && static_cast<uint32_t>(now_ms - this->opened_at_) >= CONTROL_LEASE_MS)
    this->revoke_(ControlReason::CONTROL_REASON_EXPIRED, true);
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
  if (this->media_view_pending_ && static_cast<uint32_t>(now_ms - this->media_view_at_) >= 10000) {
    this->media_view_pending_ = this->media_view_ready_ = false;
    this->last_reason_ = ControlReason::CONTROL_REASON_EXPIRED;
    this->notify_(ControlViewEvent::CONTROL_VIEW_EVENT_STATE);
  }
#endif
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
  if (this->detail_view_pending_ && static_cast<uint32_t>(now_ms - this->detail_view_at_) >= 10000) {
    this->detail_view_pending_ = false;
    this->detail_view_ready_ = false;
    this->last_reason_ = ControlReason::CONTROL_REASON_EXPIRED;
    this->notify_(ControlViewEvent::CONTROL_VIEW_EVENT_STATE);
  }
#endif
#ifdef USE_LVGL_MATERIAL_INFO_AGENDA
  if (this->agenda_page_pending() && static_cast<uint32_t>(now_ms - this->agenda_at_) >= 13000) {
    this->agenda_flags_ = 4;
    this->notify_(ControlViewEvent::CONTROL_VIEW_EVENT_STATE);
  }
#endif
  // The owner cannot identify which transport/HA phase timed out. Do not
  // invent service failure or automatically resend an ambiguous operation.
  uint32_t command_timeout = 13000;
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
  if (this->is_robot() && (this->pending_robot_operation_ == ControlOperation::CONTROL_OPERATION_ROBOT_PAUSE ||
                           this->pending_robot_operation_ == ControlOperation::CONTROL_OPERATION_ROBOT_STOP))
    command_timeout = 28000;
#endif
#ifdef USE_LVGL_MATERIAL_COVER_PRIORITY_STOP
  if (this->is_cover() && this->cover_priority_stop_active() &&
      (this->pending_cover_operation_ == ControlOperation::CONTROL_OPERATION_COVER_STOP
#ifdef USE_LVGL_MATERIAL_COVER_PANEL
       || this->pending_cover_operation_ == ControlOperation::CONTROL_OPERATION_COVER_STOP_TILT
#endif
       ))
    command_timeout = 28000;
#endif
  if (this->command_pending_ && static_cast<uint32_t>(now_ms - this->command_at_) >= command_timeout) {
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
    if (this->is_discrete()) {
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
