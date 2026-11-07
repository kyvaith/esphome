#include "material_entity_control.h"
#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS

namespace esphome::lvgl_material {
namespace {
using Action = ControlActionSnapshot;
using Op = ControlOperation;
}  // namespace

bool MaterialEntityControl::accept_action_state_(const ControlSnapshot &snapshot) {
  const auto *before = this->state_.family_if<Action>();
  const auto *after = snapshot.family_if<Action>();
  if (before == nullptr || after == nullptr)
    return false;
  if (!control_action_capabilities_equal(*before, *after)) {
    this->revoke_(ControlReason::CONTROL_REASON_CAPABILITIES_CHANGED, true);
    return false;
  }
  if (after->state_generation < before->state_generation)
    return false;
  if (after->state_generation == before->state_generation && !control_action_command_state_equal(*before, *after)) {
    this->revoke_(ControlReason::CONTROL_REASON_SELECTION_CHANGED, true);
    return false;
  }
  if (after->state_generation != before->state_generation) {
    this->cancel_drag();
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
    this->reset_action_parameter_(after);
#endif
  }
  return true;
}

bool MaterialEntityControl::can_action(Op operation) const {
  const auto *action = this->state_.family_if<Action>();
  const int index = static_cast<int>(operation) - static_cast<int>(Op::CONTROL_OPERATION_ACTION_PRESS);
  return this->is_action() && this->view_ready_ && !this->command_pending_ && action != nullptr &&
         (this->extra_peer_profiles_ & action->profile()) == action->profile() && index >= 0 && index < 7 &&
         (control_action_eligible(*action) & (1U << index)) != 0
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
         && (action->parameters_version == 0 ||
             (this->action_parameters_peer_ && (operation != Op::CONTROL_OPERATION_ACTION_RUN ||
                                                (this->action_parameter_ready_ && this->action_parameter_value_ <= 1 &&
                                                 this->action_parameter_revision_ != UINT32_MAX))))
#endif
      ;
}

bool MaterialEntityControl::begin_action(uint32_t pointer, Op operation, uint32_t now) {
  if (!this->can_input_(now) || this->dragging_ || !this->can_action(operation))
    return false;
  this->pointer_id_ = pointer;
  this->pressed_generation_ = this->state_.family_if<Action>()->state_generation;
  // Action is a mutually exclusive PANEL variant of this one owner. Reuse its
  // typed operation slots rather than retaining a second command controller.
  this->pressed_panel_operation_ = operation;
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
  this->action_parameter_choice_pressed_ = false;
  this->action_parameter_pressed_revision_ = this->action_parameter_revision_;
#endif

  this->dragging_ = true;
  return true;
}

bool MaterialEntityControl::release_action(uint32_t pointer, uint32_t now) {
  if (!this->can_input_(now) || !this->dragging_ || this->pointer_id_ != pointer)
    return false;
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
  if (this->action_parameter_choice_pressed_)
    return false;
#endif
  this->dragging_ = false;
  const auto *action = this->state_.family_if<Action>();
  if (action == nullptr || !this->can_action(this->pressed_panel_operation_) ||
      this->pressed_generation_ != action->state_generation || this->command_seq_ == CONTROL_MAX_SEQUENCE
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
      || (action->parameters_version != 0 && this->pressed_panel_operation_ == Op::CONTROL_OPERATION_ACTION_RUN &&
          this->action_parameter_pressed_revision_ != this->action_parameter_revision_)
#endif
  )
    return false;
  ControlMessage message;
  message.kind = ControlKind::CONTROL_KIND_COMMAND;
  message.identity = this->identity_;
  message.operation = this->pressed_panel_operation_;
  message.command_seq = ++this->command_seq_;
  message.expected_state_generation = action->state_generation;
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
  // Reuse typed PANEL command storage: marker and bool exist only for form Run.
  if (action->parameters_version == 1 && message.operation == Op::CONTROL_OPERATION_ACTION_RUN) {
    message.panel_value = 1;
    message.panel_enabled = this->action_parameter_value_ != 0;
  }
#endif

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

bool MaterialEntityControl::action_service_only_() const {
  if (!this->is_action())
    return false;
  switch (this->pending_panel_operation_) {
    case Op::CONTROL_OPERATION_ACTION_PRESS:
    case Op::CONTROL_OPERATION_ACTION_ACTIVATE:
    case Op::CONTROL_OPERATION_ACTION_RUN:
    case Op::CONTROL_OPERATION_ACTION_TRIGGER:
      return true;
    case Op::CONTROL_OPERATION_ACTION_DISABLE:
      return this->state_.family_if<Action>()->stop_actions == 1;
    default:
      return false;
  }
}
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS

bool MaterialEntityControl::set_action_parameters(bool enabled) {
  if (this->in_callback_ || (enabled && (this->extra_profiles_ & 24U) != 24U))
    return false;
  if (this->action_parameters_enabled_ == enabled)
    return true;
  if (this->is_action_parameters())
    this->revoke_(ControlReason::CONTROL_REASON_UNSUPPORTED, true);
  this->action_parameters_enabled_ = enabled;
  this->action_parameters_peer_ = false;
  return true;
}
bool MaterialEntityControl::accept_action_parameters(const ControlText<32> &channel) {
  if (this->in_callback_ || !this->enabled_ || !this->generic_ || !this->action_parameters_enabled_ ||
      (this->extra_peer_profiles_ & 24U) != 24U || this->selected_ || this->open_pending_ ||
      !control_valid_hex(channel) || channel != this->channel_)
    return false;
  this->action_parameters_peer_ = true;
  return true;
}
bool MaterialEntityControl::is_action_parameters() const {
  const auto *action = this->state_.family_if<Action>();
  return this->is_action() && action != nullptr && action->parameters_version == 1;
}
void MaterialEntityControl::reset_action_parameter_(const Action *action) {
  if (this->action_parameter_revision_ != UINT32_MAX)
    this->action_parameter_revision_++;
  this->action_parameter_value_ = action != nullptr && action->parameters_version == 1 && action->parameter_status == 0
                                      ? action->parameter_default
                                      : Action::UNKNOWN_BOOL;
  this->action_parameter_default_ = this->action_parameter_value_ <= 1;
  this->action_parameter_choice_pressed_ = false;
  this->action_parameter_ready_ = false;
}
void MaterialEntityControl::set_action_parameter_ready(bool ready) {
  if (this->in_callback_)
    return;
  if (!ready && this->action_parameter_ready_ && this->is_action_parameters()) {
    if (this->dragging_ &&
        (this->action_parameter_choice_pressed_ || this->pressed_panel_operation_ == Op::CONTROL_OPERATION_ACTION_RUN))
      this->cancel_drag();
    this->reset_action_parameter_(this->state_.family_if<Action>());
  }
  this->action_parameter_ready_ = ready;
}
bool MaterialEntityControl::can_choose_action_parameter() const {
  const auto *action = this->state_.family_if<Action>();
  return this->is_action_parameters() && this->action_parameters_peer_ && this->view_ready_ &&
         this->action_parameter_ready_ && !this->command_pending_ && action->available() && action->phase == 1 &&
         action->parameter_status == 0 && this->action_parameter_revision_ != UINT32_MAX;
}
bool MaterialEntityControl::set_action_parameter(bool value, uint32_t now) {
  if (!this->can_input_(now) || !this->can_choose_action_parameter())
    return false;
  // Every explicit choice advances the revision, including A -> B -> A while Run is held.
  this->action_parameter_revision_++;
  this->action_parameter_value_ = value;
  this->action_parameter_default_ = false;
  return true;
}
bool MaterialEntityControl::begin_action_parameter(uint32_t pointer, bool value, uint32_t now) {
  if (!this->can_input_(now) || this->dragging_ || !this->can_choose_action_parameter())
    return false;
  this->pointer_id_ = pointer;
  this->pressed_generation_ = this->state_.family_if<Action>()->state_generation;
  this->pressed_panel_value_ = value;
  this->action_parameter_choice_pressed_ = true;
  this->action_parameter_pressed_revision_ = this->action_parameter_revision_;
  this->dragging_ = true;
  return true;
}
bool MaterialEntityControl::release_action_parameter(uint32_t pointer, uint32_t now) {
  if (!this->can_input_(now) || !this->dragging_ || this->pointer_id_ != pointer ||
      !this->action_parameter_choice_pressed_)
    return false;
  this->dragging_ = false;
  this->action_parameter_choice_pressed_ = false;
  const auto *action = this->state_.family_if<Action>();
  if (action == nullptr || this->pressed_generation_ != action->state_generation ||
      this->action_parameter_pressed_revision_ != this->action_parameter_revision_)
    return false;
  return this->set_action_parameter(this->pressed_panel_value_ != 0, now);
}

#endif
}  // namespace esphome::lvgl_material
#endif
