#include "material_entity_control_component.h"
#ifdef USE_LVGL_MATERIAL_INFO_STATISTICS
#include "esphome/core/log.h"
#include <algorithm>
#include <cstring>

namespace esphome::lvgl_material {
namespace {
using Extra = ControlExtraSnapshot;
using Statistics = Extra::Statistics;
enum Common : size_t { ROOT, TITLE, KIND, UPDATED, STATUS };
struct Geometry {
  lv_area_t bounds, plot;
  int32_t line, gap;
  bool valid;
};
Geometry geometry(lv_obj_t *surface) {
  Geometry g{};
  lv_obj_get_content_coords(surface, &g.bounds);
  const auto *font = lv_obj_get_style_text_font(surface, LV_PART_MAIN);
  g.line = font != nullptr ? lv_font_get_line_height(font) : 0;
  g.gap = std::max<int32_t>(0, lv_obj_get_style_pad_row(surface, LV_PART_MAIN));
  const int32_t line_space = lv_obj_get_style_text_line_space(surface, LV_PART_MAIN);
  const int32_t two_rows = 2 * g.line + line_space;
  g.plot = g.bounds;
  g.plot.y1 += 3 * (g.line + g.gap);
  g.plot.y2 -= 5 * g.line + 4 * g.gap + line_space;
  g.valid = g.line > 0 && lv_area_get_width(&g.bounds) > g.line * 4 &&
            two_rows > 0 && lv_area_get_height(&g.plot) >= std::max(g.line * 3, two_rows);
  return g;
}
void hidden(lv_obj_t *widget, bool hide) {
  if (widget == nullptr || lv_obj_has_flag(widget, LV_OBJ_FLAG_HIDDEN) == hide)
    return;
  if (hide)
    lv_obj_add_flag(widget, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_remove_flag(widget, LV_OBJ_FLAG_HIDDEN);
}
void label(lv_obj_t *widget, const char *text) {
  if (std::strcmp(lv_label_get_text(widget), text) != 0)
    lv_label_set_text(widget, text);
}
bool fits(lv_obj_t *surface, const char *text, int32_t rows, const Geometry &g) {
  const auto *font = lv_obj_get_style_text_font(surface, LV_PART_MAIN);
  if (!g.valid || !number_plain_utf8(text))
    return false;
  // These inputs have already passed the strict UTF-8 boundary. Check actual generated glyphs too.
  const std::string_view bytes(text);
  for (size_t i = 0; i < bytes.size();) {
    const uint8_t first = bytes[i++];
    uint32_t point = first;
    size_t trailing = 0;
    if (first >= 0xf0) { point &= 7; trailing = 3; }
    else if (first >= 0xe0) { point &= 15; trailing = 2; }
    else if (first >= 0xc0) { point &= 31; trailing = 1; }
    while (trailing-- != 0)
      point = (point << 6) | (static_cast<uint8_t>(bytes[i++]) & 63);
    lv_font_glyph_dsc_t glyph{};
    if (!lv_font_get_glyph_dsc(font, &glyph, point, 0) || glyph.is_placeholder)
      return false;
  }
  lv_point_t size;
  const int32_t spacing = lv_obj_get_style_text_line_space(surface, LV_PART_MAIN);
  lv_text_get_size(&size, text, font, lv_obj_get_style_text_letter_space(surface, LV_PART_MAIN), spacing,
                   lv_area_get_width(&g.bounds), rows == 1 ? LV_TEXT_FLAG_EXPAND : LV_TEXT_FLAG_NONE);
  return size.x <= lv_area_get_width(&g.bounds) && size.y <= rows * g.line + (rows - 1) * spacing;
}
}  // namespace

void MaterialEntityControlComponent::set_statistics_profile(lv_obj_t *surface, bool enabled) {
  if (!this->setup_ready_) {
    this->statistics_surface_ = surface;
    this->statistics_configured_ = enabled;
  }
}
bool MaterialEntityControlComponent::setup_statistics_profile_() {
  auto *surface = this->statistics_surface_;
  if (surface == nullptr)
    return !this->statistics_configured_;
  if ((this->extra_configured_ & 1U) == 0 || !lv_obj_check_type(surface, &lv_obj_class) ||
      lv_obj_get_parent(surface) != this->extra_common_[ROOT] || lv_obj_get_child_count(surface) != 0 ||
      lv_obj_has_flag(surface, LV_OBJ_FLAG_CLICKABLE) || lv_obj_has_flag(surface, LV_OBJ_FLAG_SCROLLABLE))
    return false;
  for (const auto *existing : {this->extra_sensor_[0], this->extra_state_[0], this->extra_calendar_[0]})
    if (existing == surface)
      return false;
  lv_obj_add_event_cb(surface, statistics_draw_cb_, LV_EVENT_DRAW_MAIN, this);
  hidden(surface, true);
  return this->owner_.set_info_statistics(this->statistics_configured_);
}
bool MaterialEntityControlComponent::populate_info_statistics_features(ArduinoJson::JsonObject root) {
  const bool enabled = this->setup_ready_ && this->owner_.info_statistics_enabled();
  root["v"] = 1;
  root["type"] = "entity_info_statistics_v1";
  root["statistics_version"] = enabled ? 1 : 0;
  return enabled;
}
bool MaterialEntityControlComponent::bind_info_statistics_features(std::string_view payload,
                                                                 ArduinoJson::JsonObject root) {
  ControlText<32> channel;
  const bool accepted = this->setup_ready_ && !this->output_callback_active_ &&
                        this->codec_.parse_info_statistics_bind(payload, channel) &&
                        this->owner_.accept_info_statistics(channel);
  root["v"] = 1;
  root["type"] = "entity_info_statistics_v1";
  root["statistics_version"] = accepted ? 1 : 0;
  root["channel"] = accepted ? channel.bytes.data() : "";
  root["accepted"] = accepted;
  return accepted;
}
void MaterialEntityControlComponent::clear_statistics_widgets_() {
  this->statistics_ready_ = false;
  hidden(this->statistics_surface_, true);
  hidden(this->extra_common_[UPDATED], false);
}
void MaterialEntityControlComponent::update_statistics_static_(const Extra &extra) {
  const auto &s = std::get<Statistics>(extra.data);
  hidden(this->extra_common_[UPDATED], true);
  label(this->extra_common_[UPDATED], "");
  for (auto *root : {this->extra_sensor_[0], this->extra_state_[0], this->extra_calendar_[0]})
    hidden(root, true);
  hidden(this->statistics_surface_, false);
  label(this->extra_common_[TITLE], this->owner_.state().title.bytes.data());
  const bool kind_fit = this->extra_text_(this->extra_common_[KIND], this->text_(101, "Hourly statistics"));
  lv_obj_update_layout(this->extra_common_[ROOT]);
  const auto g = geometry(this->statistics_surface_);
  bool complete = kind_fit && g.valid;
  const char *legend = this->text_(102, "Mean / minimum - maximum");
  for (const char *text : {s.axis_min.bytes.data(), s.axis_max.bytes.data(), s.unit.bytes.data(),
                           s.start.bytes.data(), s.end.bytes.data(), legend})
    complete &= fits(this->statistics_surface_, text, 1, g);
  complete &= fits(this->statistics_surface_, s.zone.bytes.data(), 2, g);
  const char *message = s.status == 0 ? this->text_(103, "Loading statistics")
                        : s.status == 2 ? this->text_(104, "No hourly statistics")
                        : s.status == 3 ? this->text_(105, "Statistics not supported")
                        : s.status == 4 ? this->text_(106, "Statistics unavailable") : "";
  complete &= fits(this->statistics_surface_, message, 2, g);
  this->statistics_ready_ = complete;
  this->update_status_();
  const char *status = !complete || s.reason == 6 ? this->text_(109, "Labels do not fit")
                      : s.status == 1 && s.current_status == 1 ? this->text_(107, "Current reading unknown")
                      : s.status == 1 && s.current_status == 2 ? this->text_(108, "Current sensor unavailable") : "";
  this->extra_text_(this->extra_common_[STATUS], status);
  // Called on accepted state/lifecycle/theme events, never a plot polling timer.
  lv_obj_invalidate(this->statistics_surface_);
}
void MaterialEntityControlComponent::statistics_draw_cb_(lv_event_t *event) {
  auto *component = static_cast<MaterialEntityControlComponent *>(lv_event_get_user_data(event));
  if (component != nullptr)
    component->draw_statistics_(lv_event_get_layer(event));
}
void MaterialEntityControlComponent::draw_statistics_(lv_layer_t *layer) {
  if (layer == nullptr || !this->statistics_ready_ || !this->owner_.is_statistics())
    return;
  const auto *extra = this->owner_.state().family_if<Extra>();
  const auto *s = extra != nullptr ? std::get_if<Statistics>(&extra->data) : nullptr;
  if (s == nullptr)
    return;
  const auto g = geometry(this->statistics_surface_);
  if (!g.valid)
    return;
  lv_draw_label_dsc_t text;
  lv_draw_label_dsc_init(&text);
  lv_obj_init_draw_label_dsc(this->statistics_surface_, LV_PART_MAIN, &text);
  text.text_local = false;
  const auto draw_text = [&](const char *value, int32_t y, int32_t rows, lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
    lv_area_t area = g.bounds;
    area.y1 = y;
    area.y2 = y + rows * g.line + (rows - 1) * text.line_space - 1;
    text.text = value;
    text.align = align;
    text.flag = rows == 1 ? LV_TEXT_FLAG_EXPAND : LV_TEXT_FLAG_NONE;
    lv_draw_label(layer, &text, &area);
  };
  // All descriptor strings reference the current owner snapshot or persistent product translations.
  // No local text buffer or retained pointer crosses a source replacement or an LVGL refresh.
  draw_text(s->unit.bytes.data(), g.bounds.y1, 1);
  draw_text(this->text_(102, "Mean / minimum - maximum"), g.bounds.y1 + g.line + g.gap, 1);
  draw_text(s->axis_max.bytes.data(), g.bounds.y1 + 2 * (g.line + g.gap), 1, LV_TEXT_ALIGN_RIGHT);
  int32_t y = g.plot.y2 + g.gap + 1;
  draw_text(s->axis_min.bytes.data(), y, 1, LV_TEXT_ALIGN_RIGHT);
  y += g.line + g.gap;
  draw_text(s->start.bytes.data(), y, 1);
  y += g.line + g.gap;
  draw_text(s->end.bytes.data(), y, 1);
  y += g.line + g.gap;
  draw_text(s->zone.bytes.data(), y, 2);
  if (s->status != 1) {
    const char *message = s->status == 0 ? this->text_(103, "Loading statistics")
                          : s->status == 2 ? this->text_(104, "No hourly statistics")
                          : s->status == 3 ? this->text_(105, "Statistics not supported")
                                           : this->text_(106, "Statistics unavailable");
    const int32_t message_height = 2 * g.line + text.line_space;
    draw_text(message, g.plot.y1 + (lv_area_get_height(&g.plot) - message_height) / 2, 2, LV_TEXT_ALIGN_CENTER);
    return;
  }
  lv_draw_line_dsc_t line;
  lv_draw_line_dsc_init(&line);
  line.color = configured_style_color_(this->on_button_, LV_STYLE_BG_COLOR);
  line.opa = LV_OPA_COVER;
  line.round_start = line.round_end = true;
  // Use inline endpoints: LVGL copies descriptors, not a heap array of polyline points.
  const auto point = [&](size_t slot, uint16_t value) {
    const int32_t x = g.plot.x1 + static_cast<int32_t>((2 * slot + 1) * (lv_area_get_width(&g.plot) - 1) / 48);
    const int32_t y =
        g.plot.y2 - static_cast<int32_t>(static_cast<int64_t>(value) * (lv_area_get_height(&g.plot) - 1) / 1000);
    return lv_point_precise_t{static_cast<lv_value_precise_t>(x), static_cast<lv_value_precise_t>(y)};
  };
  for (size_t i = 0; i < s->points.size(); i++) {
    if ((s->valid_mask & (1U << i)) == 0)
      continue;
    line.width = 1;
    line.p1 = point(i, s->points[i][0]);
    line.p2 = point(i, s->points[i][2]);
    lv_draw_line(layer, &line);
    line.width = 3;
    if (i > 0 && (s->valid_mask & (1U << (i - 1))) != 0) {
      line.p1 = point(i - 1, s->points[i - 1][1]);
      line.p2 = point(i, s->points[i][1]);
      lv_draw_line(layer, &line);
    }
    if ((i == 0 || (s->valid_mask & (1U << (i - 1))) == 0) &&
        (i == 23 || (s->valid_mask & (1U << (i + 1))) == 0)) {
      line.p1 = point(i, s->points[i][1]);
      line.p2 = line.p1;
      line.p1.x -= 2;
      line.p2.x += 2;
      lv_draw_line(layer, &line);
    }
  }
}
void MaterialEntityControlComponent::log_statistics_view(const char *phase) const {
#ifdef USE_LVGL_MATERIAL_SCALAR_PERFORMANCE_METRICS
  const auto *extra = this->owner_.is_statistics() ? this->owner_.state().family_if<Extra>() : nullptr;
  const auto *s = extra != nullptr ? std::get_if<Statistics>(&extra->data) : nullptr;
  ESP_LOGI("scalar.performance", "%s statistics_active=%u statistics_ready=%u statistics_status=%u statistics_reason=%u "
           "statistics_mask=%lu statistics_hour=%lu statistics_current=%u", phase,
           static_cast<unsigned>(s != nullptr), static_cast<unsigned>(this->statistics_ready_),
           static_cast<unsigned>(s != nullptr ? s->status : 255), static_cast<unsigned>(s != nullptr ? s->reason : 255),
           static_cast<unsigned long>(s != nullptr ? s->valid_mask : 0),
           static_cast<unsigned long>(s != nullptr ? s->window_end_hour : 0),
           static_cast<unsigned>(s != nullptr ? s->current_status : 255));
#else
  (void) phase;
#endif
}
}  // namespace esphome::lvgl_material
#endif
