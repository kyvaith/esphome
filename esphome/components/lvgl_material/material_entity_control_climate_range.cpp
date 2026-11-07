#include "material_entity_control.h"
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
namespace esphome::lvgl_material {
bool control_valid_facet_range(const ControlFacetRange &range, bool available, bool editable) {
  const auto &domain = range.domain;
  const std::string_view unit(domain.unit.data());
  if (!domain.valid() ||
      (unit != "\xC2\xB0"
               "C" &&
       unit != "\xC2\xB0"
               "F") ||
      domain.minimum < -100 * NUMBER_SCALE || domain.maximum > 300 * NUMBER_SCALE || domain.minimum >= domain.maximum ||
      range.mode > 7 || range.activity > 8 || range.reason > 3 || available != (range.mode != 0) ||
      editable != (range.reason == 0))
    return false;
  if (range.known ? (range.low_index > range.high_index || range.high_index > domain.max_index)
                  : (range.low_index != 0 || range.high_index != 0))
    return false;
  if (!range.current.view().empty()) {
    int64_t value;
    char canonical[24];
    if (!number_parse_decimal(range.current.view(), value) || value < -100 * NUMBER_SCALE ||
        value > 300 * NUMBER_SCALE || !number_format_decimal(value, canonical, sizeof(canonical)) ||
        range.current.view() != canonical)
      return false;
  }
  switch (range.reason) {
    case 0:
      return range.mode == 4 && range.known;
    case 1:
      return range.mode != 0 && range.mode != 4;
    case 2:
      return range.mode == 4 && !range.known;
    default:
      return range.mode == 0 && !range.known && range.activity == 0 && range.current.view().empty();
  }
}
bool MaterialEntityControl::set_detail_range_enabled(bool enabled) {
  if (this->in_callback_ || (enabled && (this->detail_profiles_ & 2U) == 0))
    return false;
  if (enabled != this->detail_range_enabled_) {
    if (this->is_detail())
      this->revoke_(ControlReason::CONTROL_REASON_UNSUPPORTED, true);
    this->detail_range_peer_ = false;
    this->detail_range_enabled_ = enabled;
  }
  return true;
}
bool MaterialEntityControl::accept_detail_range(const ControlText<32> &channel) {
  if (this->in_callback_ || !this->enabled_ || !this->generic_ || !this->detail_range_enabled_ ||
      (this->detail_peer_profiles_ & 2U) == 0 || this->selected_ || this->open_pending_ || channel != this->channel_ ||
      !control_valid_hex(channel))
    return false;
  this->detail_range_peer_ = true;
  return true;
}
bool MaterialEntityControl::is_detail_range() const {
  const auto *detail = this->state_.family_if<ControlDetailSnapshot>();
  return this->is_detail() && detail != nullptr && detail->climate &&
         detail->facet == ControlFacet::CONTROL_FACET_RANGE && detail->range() != nullptr;
}
bool MaterialEntityControl::can_detail_range() const {
  if (!this->selected_ || !this->enabled_ || !this->view_ready_ || !this->detail_range_bound() ||
      !this->detail_view_ready_ || this->detail_view_pending_ || this->command_pending_ || !this->is_detail_range())
    return false;
  const auto &detail = *this->state_.family_if<ControlDetailSnapshot>();
  return detail.available && detail.editable && detail.range()->known && detail.range()->reason == 0;
}
bool MaterialEntityControl::begin_detail_range(uint32_t pointer, uint32_t now) {
  if (this->in_callback_)
    return false;
  this->tick(now);
  return this->can_detail_range() && this->begin_detail_navigation(pointer, now);
}
bool MaterialEntityControl::release_detail_range(uint32_t pointer, uint32_t low, uint32_t high, uint32_t now) {
  if (!this->release_detail_navigation(pointer, now) || !this->can_detail_range() ||
      this->command_seq_ == CONTROL_MAX_SEQUENCE)
    return false;
  const auto &source = *this->state_.family_if<ControlDetailSnapshot>()->range();
  if (low > high || high > source.domain.max_index || (low == source.low_index && high == source.high_index))
    return false;
  ControlMessage message;
  message.kind = ControlKind::CONTROL_KIND_COMMAND;
  message.identity = this->identity_;
  message.command_seq = ++this->command_seq_;
  message.operation = ControlOperation::CONTROL_OPERATION_FACET_RANGE;
  message.detail_open = true;
  message.facet = ControlFacet::CONTROL_FACET_RANGE;
  message.view_seq = this->detail_pressed_view_;
  message.expected_state_generation = this->pressed_generation_;
  auto &target = message.state.emplace_family<ControlDetailSnapshot>().data.emplace<ControlFacetRange>();
  target.low_index = low;
  target.high_index = high;
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
}  // namespace esphome::lvgl_material
#endif
