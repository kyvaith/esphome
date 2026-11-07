#include "material_entity_control.h"
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS

namespace esphome::lvgl_material {
namespace {
using Media = ControlMediaSnapshot;
using Facet = ControlMediaFacet;
using Op = ControlOperation;

bool supports(const Media &media, Facet facet) {
  const auto index = static_cast<uint8_t>(facet);
  return index == 0 || (index <= 4 && (media.facets & (1U << (index - 1))) != 0);
}

bool metadata_equal(const Media &first, const Media &second) {
  if (first.device_type != second.device_type || first.operations != second.operations || first.facets != second.facets)
    return false;
  if (first.facet != second.facet)
    return true;
  if (first.data.index() != second.data.index())
    return false;
  if (const auto *choice = std::get_if<Media::Choice>(&first.data)) {
    const auto *next = std::get_if<Media::Choice>(&second.data);
    return choice->count == next->count && choice->options == next->options;
  }
  return true;
}

bool volume_equal(const Media::Volume &first, const Media::Volume &second) {
  return first.known == second.known && first.raw == second.raw;
}

bool semantic_equal(const Media &first, const Media &second) {
  if (first.source != second.source)
    return false;
  if (first.facet != second.facet)
    return true;
  if (first.editable != second.editable)
    return false;
  if (const auto *main = std::get_if<Media::Main>(&first.data)) {
    const auto *next = std::get_if<Media::Main>(&second.data);
    return main->phase == next->phase && volume_equal(main->volume, next->volume) && main->muted == next->muted &&
           main->shuffle == next->shuffle;
  }
  if (const auto *volume = std::get_if<Media::Volume>(&first.data))
    return volume_equal(*volume, std::get<Media::Volume>(second.data));
  return std::get<Media::Choice>(first.data).index == std::get<Media::Choice>(second.data).index;
}

bool same_percent(std::string_view raw, uint32_t percent) {
  if (percent == 0)
    return raw == "0";
  if (percent == 100)
    return raw == "1";
  const char text[] = {'0', '.', static_cast<char>('0' + percent / 10), static_cast<char>('0' + percent % 10)};
  return raw == std::string_view(text, percent % 10 == 0 ? 3 : 4);
}
}  // namespace

bool MaterialEntityControl::validate_media_snapshot_(const ControlMessage &message) const {
  const auto *media = message.state.family_if<Media>();
  return this->identity_.context.control_kind == ControlEntityKind::CONTROL_ENTITY_PANEL &&
         (this->extra_peer_profiles_ & 4U) != 0 && media != nullptr && control_valid_media_snapshot(message.state) &&
         (message.kind != ControlKind::CONTROL_KIND_GRANT || (media->facet == Facet::MAIN && media->view_seq == 0));
}

bool MaterialEntityControl::accept_media_state_(const ControlSnapshot &snapshot) {
  const auto *before = this->state_.family_if<Media>();
  const auto *after = snapshot.family_if<Media>();
  if (before == nullptr || after == nullptr)
    return false;
  const bool requested = this->media_view_pending_ && after->view_seq == this->media_view_sequence_ &&
                         after->facet == this->media_requested_facet_;
  const bool current = after->view_seq == before->view_seq && after->facet == before->facet;
  if (!requested && !current)
    return false;
  if (!metadata_equal(*before, *after)) {
    this->revoke_(ControlReason::CONTROL_REASON_CAPABILITIES_CHANGED, true);
    return false;
  }
  if (after->state_generation < before->state_generation)
    return false;
  if (after->state_generation == before->state_generation && !semantic_equal(*before, *after)) {
    this->revoke_(ControlReason::CONTROL_REASON_SELECTION_CHANGED, true);
    return false;
  }
  if (after->state_generation != before->state_generation || !current)
    this->cancel_drag();
  if (requested) {
    if (this->media_view_state_seq_ == 0)
      this->media_view_state_seq_ = snapshot.state_seq;
    this->media_view_ready_ = false;
  }
  return true;
}

bool MaterialEntityControl::can_request_media_facet(Facet facet) const {
  const auto *media = this->state_.family_if<Media>();
  return this->enabled_ && this->is_media() && media != nullptr && !this->command_pending_ &&
         !this->media_view_pending_ && this->media_view_sequence_ < CONTROL_MAX_SEQUENCE && supports(*media, facet) &&
         (!this->media_view_ready_ || media->facet != facet);
}

bool MaterialEntityControl::request_media_facet(Facet facet, uint32_t now) {
  if (this->in_callback_)
    return false;
  this->tick(now);
  if (!this->can_request_media_facet(facet))
    return false;
  this->cancel_drag();
  this->media_requested_facet_ = facet;
  this->media_view_sequence_++;
  this->media_view_state_seq_ = 0;
  this->media_view_pending_ = true;
  this->media_view_at_ = now;
  ControlMessage message;
  message.kind = ControlKind::CONTROL_KIND_FACET;
  message.identity = this->identity_;
  auto &media = message.state.emplace_family<Media>();
  media.facet = facet;
  media.view_seq = this->media_view_sequence_;
  if (!this->emit_(message)) {
    this->disconnect();
    return false;
  }
  this->notify_(ControlViewEvent::CONTROL_VIEW_EVENT_STATE);
  return true;
}

bool MaterialEntityControl::receive_media_facet_result_(const ControlMessage &message) {
  const auto *result = message.state.family_if<Media>();
  const auto *media = this->state_.family_if<Media>();
  if (!this->is_media() || result == nullptr || media == nullptr || !this->media_view_pending_ ||
      result->view_seq != this->media_view_sequence_ || result->facet != this->media_requested_facet_ ||
      message.state.state_seq == 0 || message.state.state_seq > this->state_.state_seq)
    return false;
  if (result->editable) {
    if (message.reason != ControlReason::CONTROL_REASON_NONE || this->media_view_state_seq_ == 0 ||
        message.state.state_seq < this->media_view_state_seq_ || media->view_seq != result->view_seq ||
        media->facet != result->facet)
      return false;
    this->media_view_ready_ = true;
  } else {
    if (message.reason == ControlReason::CONTROL_REASON_NONE || this->media_view_state_seq_ != 0)
      return false;
    this->last_reason_ = message.reason;
  }
  this->media_view_pending_ = false;
  this->notify_(ControlViewEvent::CONTROL_VIEW_EVENT_STATE);
  return true;
}

bool MaterialEntityControl::begin_media_navigation(uint32_t pointer, uint32_t now) {
  if (this->in_callback_)
    return false;
  this->tick(now);
  const auto *media = this->state_.family_if<Media>();
  if (!this->enabled_ || !this->is_media() || !this->view_ready_ || this->command_pending_ ||
      this->media_view_pending_ || this->dragging_ || media == nullptr)
    return false;
  this->dragging_ = true;
  this->pointer_id_ = pointer;
  this->pressed_generation_ = media->state_generation;
  this->media_pressed_view_ = media->view_seq;
  this->media_operation_ = Op::CONTROL_OPERATION_BRIGHTNESS;  // Not a Media command; navigation-only token.
  return true;
}

bool MaterialEntityControl::release_media_navigation(uint32_t pointer, uint32_t now) {
  if (this->in_callback_)
    return false;
  this->tick(now);
  const auto *media = this->state_.family_if<Media>();
  if (!this->is_media() || !this->view_ready_ || this->command_pending_ || this->media_view_pending_ ||
      !this->dragging_ || this->pointer_id_ != pointer || media == nullptr)
    return false;
  this->dragging_ = false;
  return this->media_operation_ == Op::CONTROL_OPERATION_BRIGHTNESS &&
         this->pressed_generation_ == media->state_generation && this->media_pressed_view_ == media->view_seq;
}

bool MaterialEntityControl::can_media(Op operation, uint32_t target) const {
  const auto *media = this->state_.family_if<Media>();
  if (!this->is_media() || media == nullptr || !this->view_ready_ || this->command_pending_ ||
      this->media_view_pending_ || !this->media_view_ready_ || !media->available())
    return false;
  if (operation == Op::CONTROL_OPERATION_MEDIA_VOLUME) {
    const auto *volume = std::get_if<Media::Volume>(&media->data);
    return media->facet == Facet::VOLUME && media->editable && volume != nullptr && target <= 100 && this->scalar_ready_;
  }
  if (operation == Op::CONTROL_OPERATION_MEDIA_CHOICE) {
    const auto *choice = std::get_if<Media::Choice>(&media->data);
    return media->facet >= Facet::SOURCE && media->editable && choice != nullptr && target < choice->count &&
           target != choice->index;
  }
  const auto *main = std::get_if<Media::Main>(&media->data);
  const auto index = static_cast<int>(operation) - static_cast<int>(Op::CONTROL_OPERATION_MEDIA_PLAY);
  if (media->facet != Facet::MAIN || main == nullptr || index < 0 || index >= 12 ||
      (media->eligible & (1U << index)) == 0)
    return false;
  if (operation == Op::CONTROL_OPERATION_MEDIA_MUTE || operation == Op::CONTROL_OPERATION_MEDIA_SHUFFLE)
    return target <= 1 && target != (operation == Op::CONTROL_OPERATION_MEDIA_MUTE ? main->muted : main->shuffle);
  return target == 0;
}

bool MaterialEntityControl::begin_media(uint32_t pointer, Op operation, uint32_t target, uint32_t now) {
  if (!this->can_input_(now) || this->dragging_ || !this->can_media(operation, target))
    return false;
  const auto *media = this->state_.family_if<Media>();
  this->pointer_id_ = pointer;
  this->pressed_generation_ = media->state_generation;
  this->media_pressed_view_ = media->view_seq;
  this->media_operation_ = operation;
  this->pressed_index_ = target;
  this->dragging_ = true;
  return true;
}

bool MaterialEntityControl::update_media_volume(uint32_t pointer, uint32_t target, uint32_t now) {
  if (!this->can_input_(now) || !this->dragging_ || this->pointer_id_ != pointer ||
      this->media_operation_ != Op::CONTROL_OPERATION_MEDIA_VOLUME || !this->can_media(this->media_operation_, target))
    return false;
  if (this->candidate_known_ && this->candidate_ == target)
    return true;
  this->candidate_known_ = true;
  this->candidate_ = target;
  this->notify_(ControlViewEvent::CONTROL_VIEW_EVENT_PREVIEW);
  return true;
}

bool MaterialEntityControl::release_media(uint32_t pointer, uint32_t target, uint32_t now) {
  if (!this->can_input_(now) || !this->dragging_ || this->pointer_id_ != pointer)
    return false;
  this->dragging_ = false;
  const auto *media = this->state_.family_if<Media>();
  if (media == nullptr || !this->can_media(this->media_operation_, target) ||
      this->pressed_generation_ != media->state_generation || this->media_pressed_view_ != media->view_seq ||
      (this->media_operation_ != Op::CONTROL_OPERATION_MEDIA_VOLUME && target != this->pressed_index_) ||
      this->command_seq_ == CONTROL_MAX_SEQUENCE)
    return false;
  if (this->media_operation_ == Op::CONTROL_OPERATION_MEDIA_VOLUME &&
      same_percent(std::get<Media::Volume>(media->data).raw.view(), target))
    return false;
  ControlMessage message;
  message.kind = ControlKind::CONTROL_KIND_COMMAND;
  message.identity = this->identity_;
  message.operation = this->media_operation_;
  message.command_seq = ++this->command_seq_;
  message.expected_state_generation = media->state_generation;
  message.value = target;
  auto &command = message.state.emplace_family<Media>();
  command.facet = media->facet;
  command.view_seq = media->view_seq;
  if (this->media_operation_ == Op::CONTROL_OPERATION_MEDIA_VOLUME)
    command.data.emplace<Media::Volume>().raw = std::get<Media::Volume>(media->data).raw;
  else if (this->media_operation_ == Op::CONTROL_OPERATION_MEDIA_CHOICE)
    message.expected_index = std::get<Media::Choice>(media->data).index;
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
