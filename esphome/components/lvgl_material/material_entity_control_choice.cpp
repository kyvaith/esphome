#include "material_entity_control.h"

#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR

namespace esphome::lvgl_material {

bool control_valid_choice_snapshot(const ControlSnapshot &snapshot, bool allow_empty) {
  const auto *choice_payload = snapshot.family_if<ControlSnapshot::Choice>();
  if (choice_payload == nullptr)
    return false;
  const auto &choice = *choice_payload;
  if (snapshot.state_seq == 0 || snapshot.state_seq > CONTROL_MAX_SEQUENCE || snapshot.ttl_ms != CONTROL_LEASE_MS ||
      !number_plain_utf8(snapshot.title.view()) || choice.state_generation == 0 ||
      choice.state_generation > CONTROL_MAX_SEQUENCE || choice.count == 0 || choice.count > choice.options.size() ||
      snapshot.on || snapshot.brightness_known || snapshot.caps.brightness || snapshot.caps.turn_on ||
      snapshot.caps.turn_off ||
      (choice.available
           ? (choice.index >= choice.count && !(allow_empty && choice.index == ControlSnapshot::Choice::EMPTY_INDEX))
           : choice.index != 0))
    return false;
  for (size_t index = 0; index < choice.options.size(); index++) {
    const auto text = choice.options[index].view();
    if (index >= choice.count) {
      if (!text.empty())
        return false;
      continue;
    }
    if (text.empty() || !number_plain_utf8(text) || text == "unknown" || text == "unavailable")
      return false;
    for (size_t previous = 0; previous < index; previous++) {
      if (choice.options[previous].view() == text)
        return false;
    }
  }
  return true;
}

bool MaterialEntityControl::set_choice_empty_enabled(bool enabled) {
  if (this->in_callback_)
    return false;
  if (this->is_choice() && this->selected_)
    this->revoke_(ControlReason::CONTROL_REASON_UNSUPPORTED, true);
  this->choice_empty_configured_ = enabled;
  this->choice_empty_peer_ = false;
  return true;
}

bool MaterialEntityControl::accept_choice_features(const ControlText<32> &channel, uint32_t version) {
  if (this->in_callback_ || !this->enabled_ || !this->generic_ || !this->choice_empty_supported() || version != 1 ||
      !control_valid_hex(channel) || channel != this->channel_ || this->selected_ || this->open_pending_)
    return false;
  this->choice_empty_peer_ = true;
  return true;
}

bool MaterialEntityControl::set_choice_enabled(bool enabled) {
  if (this->in_callback_)
    return false;
  if (!enabled && this->is_choice())
    this->revoke_(ControlReason::CONTROL_REASON_UNSUPPORTED, true);
  if (this->choice_enabled_ != enabled)
    this->choice_empty_peer_ = false;
  this->choice_enabled_ = enabled;
  return true;
}

void MaterialEntityControl::sync_choice_candidate_() {
  const auto *choice_payload = this->state_.family_if<ControlSnapshot::Choice>();
  if (choice_payload == nullptr) {
    this->choice_candidate_known_ = false;
    this->choice_candidate_generation_ = 0;
    return;
  }
  const auto &choice = *choice_payload;
  if (this->choice_candidate_generation_ != choice.state_generation || !choice.available) {
    this->choice_candidate_ = choice.index;
    this->choice_candidate_known_ = choice.available && choice.index < choice.count;
    this->choice_candidate_generation_ = choice.state_generation;
  }
}

bool MaterialEntityControl::accept_choice_state_(const ControlSnapshot &state) {
  const auto *previous_payload = this->state_.family_if<ControlSnapshot::Choice>();
  if (previous_payload == nullptr)
    return false;
  const auto &previous = *previous_payload;
  const auto *next_payload = state.family_if<ControlSnapshot::Choice>();
  if (next_payload == nullptr)
    return false;
  const auto &next = *next_payload;
  if (previous.count != next.count || previous.options != next.options) {
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
    return true;
  }
  this->cancel_drag();
  return true;
}

bool MaterialEntityControl::can_choose(uint8_t index) const {
  return this->state_.family_if<ControlSnapshot::Choice>() != nullptr && this->is_choice() && this->choice_enabled_ &&
         this->selected_ && this->view_ready_ && !this->command_pending_ &&
         this->state_.family_if<ControlSnapshot::Choice>()->available &&
         index < this->state_.family_if<ControlSnapshot::Choice>()->count;
}

bool MaterialEntityControl::can_apply_choice() const {
  return this->choice_candidate_known_ && this->can_choose(this->choice_candidate_) &&
         this->choice_candidate_ != this->state_.family_if<ControlSnapshot::Choice>()->index;
}

bool MaterialEntityControl::begin_choice(uint32_t pointer_id, uint8_t index, uint32_t now_ms) {
  if (!this->can_input_(now_ms) || this->dragging_ || !this->can_choose(index))
    return false;
  this->pointer_id_ = pointer_id;
  this->pressed_index_ = this->state_.family_if<ControlSnapshot::Choice>()->index;
  this->pressed_generation_ = this->state_.family_if<ControlSnapshot::Choice>()->state_generation;
  this->pressed_choice_ = index;
  this->choice_applying_ = false;
  this->dragging_ = true;
  return true;
}

bool MaterialEntityControl::release_choice(uint32_t pointer_id, uint32_t now_ms) {
  if (!this->can_input_(now_ms) || !this->is_choice() || !this->dragging_ || this->choice_applying_ ||
      this->pointer_id_ != pointer_id)
    return false;
  this->dragging_ = false;
  if (!this->can_choose(this->pressed_choice_) ||
      this->state_.family_if<ControlSnapshot::Choice>()->index != this->pressed_index_ ||
      this->state_.family_if<ControlSnapshot::Choice>()->state_generation != this->pressed_generation_)
    return false;
  this->choice_candidate_ = this->pressed_choice_;
  this->choice_candidate_known_ = true;
  this->notify_(ControlViewEvent::CONTROL_VIEW_EVENT_PREVIEW);
  return true;
}

bool MaterialEntityControl::begin_choice_apply(uint32_t pointer_id, uint32_t now_ms) {
  if (!this->can_input_(now_ms) || this->dragging_ || !this->can_apply_choice())
    return false;
  this->pointer_id_ = pointer_id;
  this->pressed_index_ = this->state_.family_if<ControlSnapshot::Choice>()->index;
  this->pressed_generation_ = this->state_.family_if<ControlSnapshot::Choice>()->state_generation;
  this->pressed_choice_ = this->choice_candidate_;
  this->choice_applying_ = true;
  this->dragging_ = true;
  return true;
}

bool MaterialEntityControl::release_choice_apply(uint32_t pointer_id, uint32_t now_ms) {
  if (!this->can_input_(now_ms) || !this->is_choice() || !this->dragging_ || !this->choice_applying_ ||
      this->pointer_id_ != pointer_id)
    return false;
  this->dragging_ = false;
  if (!this->can_apply_choice() || this->choice_candidate_ != this->pressed_choice_ ||
      this->state_.family_if<ControlSnapshot::Choice>()->index != this->pressed_index_ ||
      this->state_.family_if<ControlSnapshot::Choice>()->state_generation != this->pressed_generation_ ||
      this->command_seq_ == CONTROL_MAX_SEQUENCE)
    return false;
  ControlMessage message;
  message.kind = ControlKind::CONTROL_KIND_COMMAND;
  message.identity = this->identity_;
  message.command_seq = ++this->command_seq_;
  message.operation = ControlOperation::CONTROL_OPERATION_CHOOSE;
  message.target_index = this->pressed_choice_;
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
