#include "material_entity_control.h"
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR

namespace esphome::lvgl_material {
bool control_valid_facet_color(const ControlFacetColor &color, bool editable) {
  if (color.write_mode > 2 || color.source_mode > 5 || color.reason > 5 || color.hue_milli > 359999 ||
      color.saturation_milli > 100000 || editable != (color.reason == 0))
    return false;
  const bool known = color.reason == 0 || color.reason == 4;
  if (color.known != known ||
      (!known && (color.hue_milli != 0 || color.saturation_milli != 0 || color.rgb != std::array<uint8_t, 3>{})))
    return false;
  if (color.reason == 1 || color.reason == 5)
    return color.source_mode == 5;
  if (color.reason == 2)
    return color.source_mode == 4 || color.source_mode == 5;
  return color.source_mode <= 3;
}

bool MaterialEntityControl::set_detail_color_enabled(bool enabled) {
  if (this->in_callback_ || (enabled && (this->detail_profiles_ & 1U) == 0))
    return false;
  if (enabled != this->detail_color_enabled_) {
    if (this->is_detail())
      this->revoke_(ControlReason::CONTROL_REASON_UNSUPPORTED, true);
    this->detail_color_peer_ = false;
    this->clear_detail_color_draft_();
    this->detail_color_enabled_ = enabled;
  }
  return true;
}

bool MaterialEntityControl::accept_detail_color(const ControlText<32> &channel) {
  if (this->in_callback_ || !this->enabled_ || !this->generic_ || !this->detail_color_enabled_ ||
      (this->detail_peer_profiles_ & 1U) == 0 || this->selected_ || this->open_pending_ || channel != this->channel_ ||
      !control_valid_hex(channel))
    return false;
  this->detail_color_peer_ = true;
  return true;
}

bool MaterialEntityControl::is_detail_color() const {
  const auto *detail = this->state_.family_if<ControlDetailSnapshot>();
  return this->is_detail() && detail != nullptr && !detail->climate &&
         detail->facet == ControlFacet::CONTROL_FACET_COLOR && detail->color() != nullptr;
}

bool MaterialEntityControl::can_detail_color() const {
  if (!this->selected_ || !this->enabled_ || !this->view_ready_ || !this->detail_color_bound() ||
      !this->detail_view_ready_ || this->detail_view_pending_ || this->command_pending_ || !this->is_detail_color())
    return false;
  const auto &detail = *this->state_.family_if<ControlDetailSnapshot>();
  return detail.available && detail.editable && detail.color()->known && detail.color()->reason == 0;
}

bool MaterialEntityControl::detail_color_draft_current_() const {
  const auto *detail = this->state_.family_if<ControlDetailSnapshot>();
  return this->is_detail_color() && this->detail_color_draft_.active &&
         this->detail_color_draft_.generation == detail->state_generation &&
         this->detail_color_draft_.view_seq == detail->view_seq;
}

bool MaterialEntityControl::can_apply_detail_color() const {
  if (!this->can_detail_color() || this->command_seq_ == CONTROL_MAX_SEQUENCE)
    return false;
  const auto &source = *this->state_.family_if<ControlDetailSnapshot>()->color();
  // Applying CT is an explicit switch to color, even at its observed seed.
  return source.source_mode == 3 || (this->detail_color_draft_current_() &&
                                     (this->detail_color_draft_.hue_milli != source.hue_milli ||
                                      this->detail_color_draft_.saturation_milli != source.saturation_milli));
}

void MaterialEntityControl::clear_detail_color_draft_() { this->detail_color_draft_ = {}; }

bool MaterialEntityControl::begin_detail_color(uint32_t pointer, uint32_t now) {
  if (this->in_callback_)
    return false;
  this->tick(now);
  if (!this->can_detail_color() || !this->begin_detail_navigation(pointer, now))
    return false;
  if (!this->detail_color_draft_current_()) {
    const auto &detail = *this->state_.family_if<ControlDetailSnapshot>();
    const auto &source = *detail.color();
    this->detail_color_draft_ = {source.hue_milli, source.saturation_milli, detail.state_generation, detail.view_seq,
                                 true};
  }
  return true;
}

bool MaterialEntityControl::update_detail_color(uint32_t pointer, uint32_t hue_milli, uint32_t saturation_milli) {
  if (this->in_callback_ || !this->dragging_ || pointer != this->pointer_id_ || !this->can_detail_color() ||
      !this->detail_color_draft_current_() || this->pressed_generation_ != this->detail_color_draft_.generation ||
      this->detail_pressed_view_ != this->detail_color_draft_.view_seq || hue_milli > 359999 ||
      saturation_milli > 100000)
    return false;
  this->detail_color_draft_.hue_milli = hue_milli;
  this->detail_color_draft_.saturation_milli = saturation_milli;
  return true;
}

bool MaterialEntityControl::release_detail_color(uint32_t pointer, uint32_t now) {
  return this->release_detail_navigation(pointer, now) && this->can_detail_color() &&
         this->detail_color_draft_current_();
}

bool MaterialEntityControl::begin_detail_color_apply(uint32_t pointer, uint32_t now) {
  if (this->in_callback_)
    return false;
  this->tick(now);
  return this->can_apply_detail_color() && this->begin_detail_color(pointer, now);
}

bool MaterialEntityControl::release_detail_color_apply(uint32_t pointer, uint32_t now) {
  if (!this->release_detail_navigation(pointer, now) || !this->can_apply_detail_color() ||
      !this->detail_color_draft_current_())
    return false;
  ControlMessage message;
  message.kind = ControlKind::CONTROL_KIND_COMMAND;
  message.identity = this->identity_;
  message.command_seq = ++this->command_seq_;
  message.operation = ControlOperation::CONTROL_OPERATION_FACET_COLOR;
  message.detail_open = true;
  message.facet = ControlFacet::CONTROL_FACET_COLOR;
  message.view_seq = this->detail_pressed_view_;
  message.expected_state_generation = this->pressed_generation_;
  auto &target = message.state.emplace_family<ControlDetailSnapshot>().data.emplace<ControlFacetColor>();
  target.hue_milli = this->detail_color_draft_.hue_milli;
  target.saturation_milli = this->detail_color_draft_.saturation_milli;
  this->command_at_ = now;
  this->command_pending_ = true;
  this->last_result_ = ControlResult::CONTROL_RESULT_PENDING;
  this->last_reason_ = ControlReason::CONTROL_REASON_NONE;
  if (!this->emit_(message)) {
    this->command_pending_ = false;
    this->disconnect();
    return false;
  }
  this->notify_(ControlViewEvent::CONTROL_VIEW_EVENT_SENDING);
  return true;
}

bool MaterialEntityControl::cancel_detail_color(uint32_t pointer, uint32_t now) {
  if (!this->release_detail_navigation(pointer, now))
    return false;
  this->clear_detail_color_draft_();
  this->notify_(ControlViewEvent::CONTROL_VIEW_EVENT_CANCELLED);
  return true;
}
}  // namespace esphome::lvgl_material
#endif
