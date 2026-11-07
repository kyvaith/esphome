#include "material_entity_control.h"
#ifdef USE_LVGL_MATERIAL_INFO_STATISTICS
#include <cmath>
#include <cstdlib>

namespace esphome::lvgl_material {
namespace {
using Extra = ControlExtraSnapshot;
bool axis_number(const ControlText<24> &text) {
  const auto value = text.view();
  if (value.empty())
    return false;
  size_t i = 0;
  if (value[i] == '-' || value[i] == '+')
    i++;
  const size_t whole = i;
  while (i < value.size() && value[i] >= '0' && value[i] <= '9')
    i++;
  if (i == whole)
    return false;
  if (i < value.size() && value[i] == '.') {
    const size_t fraction = ++i;
    while (i < value.size() && value[i] >= '0' && value[i] <= '9')
      i++;
    if (i == fraction)
      return false;
  }
  if (i < value.size() && (value[i] == 'e' || value[i] == 'E')) {
    i++;
    if (i < value.size() && (value[i] == '-' || value[i] == '+'))
      i++;
    const size_t exponent = i;
    while (i < value.size() && value[i] >= '0' && value[i] <= '9')
      i++;
    if (i == exponent)
      return false;
  }
  return i == value.size() && std::isfinite(std::strtod(text.bytes.data(), nullptr));
}
}  // namespace

bool control_valid_statistics(const Extra &extra) {
  const auto *s = std::get_if<Extra::Statistics>(&extra.data);
  if (extra.type != 8 || s == nullptr || !extra.updated.view().empty() || extra.source > 2 || s->status > 4 ||
      s->reason > 11 || s->current_status > 2 || s->window_end_hour < 24 || (s->valid_mask & 0xff000000U) != 0 ||
      !number_plain_utf8(s->unit.view()) || !number_plain_utf8(s->start.view()) ||
      !number_plain_utf8(s->end.view()) || !number_plain_utf8(s->zone.view()))
    return false;
  const bool hidden_labels = s->status == 3 && s->reason == 6;
  if (hidden_labels ? (!s->unit.view().empty() || !s->start.view().empty() || !s->end.view().empty() ||
                       !s->zone.view().empty())
                    : (s->start.view().empty() || s->end.view().empty() || s->zone.view().empty()))
    return false;
  const bool matrix = s->status == 0 ? extra.source == 0 && s->reason == 0
                      : s->status == 1 ? extra.source == 0 && s->reason == 0 && s->valid_mask != 0
                      : s->status == 2 ? extra.source == 0 && s->reason == 1
                      : s->status == 3 ? extra.source == 1 && s->reason >= 2 && s->reason <= 6
                                       : extra.source == 2 && s->reason >= 7 && s->reason <= 11;
  if (!matrix)
    return false;
  if (s->status == 1) {
    if (!axis_number(s->axis_min) || !axis_number(s->axis_max))
      return false;
    const double low = std::strtod(s->axis_min.bytes.data(), nullptr);
    const double high = std::strtod(s->axis_max.bytes.data(), nullptr);
    if (s->flat ? low != high : !(low < high))
      return false;
  } else if (s->valid_mask != 0 || s->flat || !s->axis_min.view().empty() || !s->axis_max.view().empty()) {
    return false;
  }
  for (size_t i = 0; i < s->points.size(); i++) {
    const auto &p = s->points[i];
    if ((s->valid_mask & (1U << i)) == 0) {
      if (p[0] != 0 || p[1] != 0 || p[2] != 0)
        return false;
    } else if (p[0] > p[1] || p[1] > p[2] || p[2] > 1000 ||
               (s->flat && (p[0] != 500 || p[1] != 500 || p[2] != 500))) {
      return false;
    }
  }
  return true;
}

bool MaterialEntityControl::set_info_statistics(bool enabled) {
  if (this->in_callback_ || (enabled && (this->extra_profiles_ & 1U) == 0))
    return false;
  if (this->statistics_enabled_ == enabled)
    return true;
  if (this->is_statistics())
    this->revoke_(ControlReason::CONTROL_REASON_UNSUPPORTED, true);
  this->statistics_enabled_ = enabled;
  this->statistics_peer_ = false;
  return true;
}

bool MaterialEntityControl::accept_info_statistics(const ControlText<32> &channel) {
  if (this->in_callback_ || !this->enabled_ || !this->generic_ || !this->statistics_enabled_ ||
      (this->extra_peer_profiles_ & 1U) == 0 || this->selected_ || this->open_pending_ ||
      !control_valid_hex(channel) || channel != this->channel_)
    return false;
  this->statistics_peer_ = true;
  return true;
}

bool MaterialEntityControl::is_statistics() const {
  const auto *extra = this->state_.family_if<Extra>();
  return this->is_extra() && extra != nullptr && extra->type == 8;
}
}  // namespace esphome::lvgl_material
#endif
