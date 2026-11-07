#include "material_entity_control.h"
#ifdef USE_LVGL_MATERIAL_INFO_AGENDA

namespace esphome::lvgl_material {
namespace {
using Extra = ControlExtraSnapshot;
using Agenda = Extra::Agenda;
}  // namespace

bool control_valid_agenda(const Extra &extra) {
  const auto *agenda = std::get_if<Agenda>(&extra.data);
  if (extra.type != 7 || agenda == nullptr || !extra.updated.view().empty() || agenda->status > 3 ||
      agenda->request_seq > CONTROL_MAX_SEQUENCE || agenda->count > 16 || agenda->truncated > 7 ||
      !number_plain_utf8(agenda->summary.view()) || !number_plain_utf8(agenda->start.view()) ||
      !number_plain_utf8(agenda->end.view()) || !number_plain_utf8(agenda->zone.view()) ||
      !number_plain_utf8(agenda->location.view()) || !number_plain_utf8(agenda->description.view()) ||
      (extra.available() ? agenda->status == 3 : agenda->status != 3))
    return false;
  if (extra.available() && agenda->status == 1 && agenda->count != 0)
    return agenda->index < agenda->count && agenda->all_day <= 1 && (!agenda->has_more || agenda->count == 16) &&
           !agenda->start.view().empty() && !agenda->end.view().empty() && !agenda->zone.view().empty();
  return agenda->count == 0 && agenda->index == 255 && !agenda->has_more && agenda->all_day == Extra::UNKNOWN_BOOL &&
         agenda->truncated == 0 && agenda->summary.view().empty() && agenda->start.view().empty() &&
         agenda->end.view().empty() && agenda->zone.view().empty() && agenda->location.view().empty() &&
         agenda->description.view().empty();
}

bool MaterialEntityControl::set_info_agenda(bool enabled) {
  if (this->in_callback_ || (enabled && (this->extra_profiles_ & 1U) == 0))
    return false;
  if (this->agenda_enabled_ == enabled)
    return true;
  if (this->is_agenda())
    this->revoke_(ControlReason::CONTROL_REASON_UNSUPPORTED, true);
  this->agenda_enabled_ = enabled;
  this->agenda_peer_ = false;
  return true;
}

bool MaterialEntityControl::accept_info_agenda(const ControlText<32> &channel) {
  if (this->in_callback_ || !this->enabled_ || !this->generic_ || !this->agenda_enabled_ ||
      (this->extra_peer_profiles_ & 1U) == 0 || this->selected_ || this->open_pending_ || this->agenda_page_pending() ||
      !control_valid_hex(channel) || channel != this->channel_)
    return false;
  this->agenda_peer_ = true;
  return true;
}

bool MaterialEntityControl::is_agenda() const {
  const auto *extra = this->state_.family_if<Extra>();
  return this->is_extra() && extra != nullptr && extra->type == 7;
}

bool MaterialEntityControl::can_agenda_page(int8_t direction) const {
  if (!this->is_agenda() || !this->agenda_peer_ || !this->view_ready_ || this->command_pending_ ||
      this->agenda_page_pending() || (direction != -1 && direction != 1))
    return false;
  const auto &extra = *this->state_.family_if<Extra>();
  const auto *agenda = std::get_if<Agenda>(&extra.data);
  return extra.available() && agenda != nullptr && agenda->status == 1 && agenda->count != 0 &&
         (direction < 0 ? agenda->index > 0 : agenda->index + 1 < agenda->count);
}

bool MaterialEntityControl::begin_agenda_page(uint32_t pointer, int8_t direction, uint32_t now) {
  if (!this->can_input_(now) || !this->can_agenda_page(direction) || this->dragging_)
    return false;
  const auto &extra = *this->state_.family_if<Extra>();
  const auto &agenda = std::get<Agenda>(extra.data);
  this->pointer_id_ = pointer;
  this->pressed_generation_ = extra.state_generation;
  this->pressed_index_ = agenda.index;
  this->agenda_pressed_request_ = agenda.request_seq;
  this->step_direction_ = direction;
  this->dragging_ = true;
  return true;
}

bool MaterialEntityControl::release_agenda_page(uint32_t pointer, uint32_t now) {
  if (!this->can_input_(now) || !this->is_agenda() || !this->dragging_ || this->pointer_id_ != pointer)
    return false;
  this->dragging_ = false;
  const auto &extra = *this->state_.family_if<Extra>();
  const auto &agenda = std::get<Agenda>(extra.data);
  if (!this->can_agenda_page(this->step_direction_) || extra.state_generation != this->pressed_generation_ ||
      agenda.index != this->pressed_index_ || agenda.request_seq != this->agenda_pressed_request_)
    return false;
  if (this->agenda_sequence_ == CONTROL_MAX_SEQUENCE) {
    this->revoke_(ControlReason::CONTROL_REASON_EXPIRED, true);
    return false;
  }
  ControlMessage message;
  message.kind = ControlKind::CONTROL_KIND_PAGE;
  message.identity = this->identity_;
  // These shared fields are wire request_seq/target_index only for PAGE.
  message.command_seq = ++this->agenda_sequence_;
  message.value = static_cast<uint8_t>(agenda.index + this->step_direction_);
  message.expected_state_generation = extra.state_generation;
  this->agenda_target_ = message.value;
  this->agenda_state_seq_ = this->state_.state_seq;
  this->agenda_at_ = now;
  this->agenda_flags_ = 1;
  if (!this->emit_(message)) {
    this->disconnect();
    return false;
  }
  this->notify_(ControlViewEvent::CONTROL_VIEW_EVENT_STATE);
  return true;
}

bool MaterialEntityControl::accept_agenda_state_(const ControlSnapshot &state) {
  const auto &previous = *this->state_.family_if<Extra>();
  const auto &next = *state.family_if<Extra>();
  const auto &before = std::get<Agenda>(previous.data);
  const auto &after = std::get<Agenda>(next.data);
  if (next.state_generation < previous.state_generation || after.request_seq > this->agenda_sequence_ ||
      after.request_seq < before.request_seq)
    return false;
  if (next.state_generation != previous.state_generation) {
    if (after.count != 0 && after.index != 0)
      return false;
    this->cancel_drag();
    this->agenda_flags_ = 0;
    return true;
  }
  if (before.index == after.index && before.request_seq == after.request_seq) {
    if (previous.source != next.source || !(before == after)) {
      this->revoke_(ControlReason::CONTROL_REASON_SELECTION_CHANGED, true);
      return false;
    }
    return true;
  }
  if (!this->agenda_page_pending() || after.request_seq != this->agenda_sequence_ ||
      after.index != this->agenda_target_ || previous.source != next.source || after.status != 1 ||
      before.count != after.count || before.has_more != after.has_more)
    return false;
  this->cancel_drag();
  return true;
}

void MaterialEntityControl::complete_agenda_page_() {
  if (!this->is_agenda() || (this->agenda_flags_ & 3U) != 3U)
    return;
  const auto &agenda = std::get<Agenda>(this->state_.family_if<Extra>()->data);
  if (agenda.request_seq == this->agenda_sequence_ && agenda.index == this->agenda_target_ &&
      this->state_.state_seq >= this->agenda_state_seq_)
    this->agenda_flags_ = 0;
}

bool MaterialEntityControl::receive_agenda_result_(const ControlMessage &message) {
  if (!this->is_agenda() || !this->agenda_page_pending() || message.command_seq != this->agenda_sequence_ ||
      message.value != this->agenda_target_)
    return false;
  if (!message.enabled) {
    switch (message.reason) {
      case ControlReason::CONTROL_REASON_UNSUPPORTED:
      case ControlReason::CONTROL_REASON_BUSY:
      case ControlReason::CONTROL_REASON_UNAUTHORIZED:
      case ControlReason::CONTROL_REASON_STALE_STATE:
      case ControlReason::CONTROL_REASON_STALE_SEQUENCE:
      case ControlReason::CONTROL_REASON_SEQUENCE_CONFLICT:
      case ControlReason::CONTROL_REASON_SELECTION_CHANGED:
      case ControlReason::CONTROL_REASON_CONNECTION_LOST:
      case ControlReason::CONTROL_REASON_ENTITY_UNAVAILABLE:
      case ControlReason::CONTROL_REASON_EXPIRED:
        break;
      default:
        return false;
    }
    if (message.state.state_seq == 0 || message.state.state_seq > CONTROL_MAX_SEQUENCE)
      return false;
    this->agenda_flags_ = 4;
  } else {
    if (message.reason != ControlReason::CONTROL_REASON_NONE || message.state.state_seq <= this->agenda_state_seq_ ||
        message.state.state_seq > CONTROL_MAX_SEQUENCE)
      return false;
    this->agenda_state_seq_ = message.state.state_seq;
    this->agenda_flags_ |= 2;
    this->complete_agenda_page_();
  }
  this->notify_(ControlViewEvent::CONTROL_VIEW_EVENT_STATE);
  return true;
}
}  // namespace esphome::lvgl_material
#endif
