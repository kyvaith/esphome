#include "material_entity_control_component.h"
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace esphome::lvgl_material {
namespace {
enum Widget : size_t {
  ROOT,
  KIND,
  TITLE,
  PREVIEW,
  STATUS,
  HUE_LABEL,
  HUE,
  SATURATION_LABEL,
  SATURATION,
  CANCEL,
  CANCEL_LABEL,
  APPLY,
  APPLY_LABEL,
  MAIN_BUTTON,
  MAIN_LABEL
};
static constexpr lv_style_const_prop_t COLOR_DISABLED_PROPS[] = {
    {.prop = LV_STYLE_RECOLOR_OPA, .value = {.num = LV_OPA_TRANSP}},
    {.prop = LV_STYLE_BG_OPA, .value = {.num = LV_OPA_COVER}},
    {.prop = LV_STYLE_TEXT_OPA, .value = {.num = LV_OPA_80}},
    LV_STYLE_CONST_PROPS_END};
static LV_STYLE_CONST_INIT(COLOR_DISABLED_STYLE, COLOR_DISABLED_PROPS);
bool within(lv_obj_t *root, lv_obj_t *object) {
  for (; object; object = lv_obj_get_parent(object))
    if (object == root)
      return true;
  return false;
}
void hidden(lv_obj_t *object, bool hide) {
  if (object == nullptr || lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN) == hide)
    return;
  if (hide)
    lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
}
void enabled(lv_obj_t *object, bool enable) {
  if (lv_obj_has_state(object, LV_STATE_DISABLED) == !enable)
    return;
  if (enable)
    lv_obj_remove_state(object, LV_STATE_DISABLED);
  else
    lv_obj_add_state(object, LV_STATE_DISABLED);
}
void color(lv_obj_t *object, lv_style_prop_t property, lv_color_t value, lv_style_selector_t selector = LV_PART_MAIN) {
  lv_style_value_t previous{}, desired{};
  if (lv_obj_get_local_style_prop(object, property, &previous, selector) == LV_STYLE_RES_FOUND &&
      lv_color_eq(previous.color, value))
    return;
  desired.color = value;
  lv_obj_set_local_style_prop(object, property, desired, selector);
}
bool label(lv_obj_t *object, const char *text, bool ellipsis = false) {
  const auto *font = lv_obj_get_style_text_font(object, LV_PART_MAIN);
  const std::string_view value(text);
  bool complete = font != nullptr;
  for (size_t start = 0; start <= value.size();) {
    const size_t end = value.find('\n', start);
    complete &= number_plain_utf8(value.substr(start, end == std::string_view::npos ? end : end - start));
    if (end == std::string_view::npos)
      break;
    start = end + 1;
  }
  if (complete) {
    for (size_t offset = 0; offset < value.size();) {
      uint32_t point = static_cast<uint8_t>(value[offset++]);
      if (point == '\n')
        continue;
      const size_t count = point >= 0xf0 ? 3 : point >= 0xe0 ? 2 : point >= 0xc0 ? 1 : 0;
      if (count)
        point &= count == 3 ? 7 : count == 2 ? 15 : 31;
      for (size_t i = 0; i < count; i++)
        point = (point << 6) | (static_cast<uint8_t>(value[offset++]) & 63);
      lv_font_glyph_dsc_t glyph{};
      if (!lv_font_get_glyph_dsc(font, &glyph, point, 0) || glyph.is_placeholder) {
        complete = false;
        break;
      }
    }
  }
  if (complete) {
    lv_point_t size{};
    const int32_t width = lv_obj_get_content_width(object), height = lv_obj_get_content_height(object);
    lv_text_get_size(&size, text, font, lv_obj_get_style_text_letter_space(object, LV_PART_MAIN),
                     lv_obj_get_style_text_line_space(object, LV_PART_MAIN), width, LV_TEXT_FLAG_NONE);
    complete =
        width > 0 && height > 0 &&
        (ellipsis && lv_label_get_long_mode(object) == LV_LABEL_LONG_MODE_DOTS ? height >= font->line_height
                                                                               : size.x <= width && size.y <= height);
  }
  const char *shown = complete ? text : "--";
  if (std::strcmp(lv_label_get_text(object), shown) != 0)
    lv_label_set_text(object, shown);
  return complete;
}
std::array<uint8_t, 3> preview_rgb(uint32_t hue, uint32_t saturation) {
  // V is always 255. This is a local color preview, never a brightness command.
  const uint32_t sector = hue / 60000, fraction = hue % 60000;
  const uint8_t low = (255ULL * (100000 - saturation) + 50000) / 100000;
  const uint8_t down = (255ULL * (6000000000ULL - uint64_t(saturation) * fraction) + 3000000000ULL) / 6000000000ULL;
  const uint8_t up =
      (255ULL * (6000000000ULL - uint64_t(saturation) * (60000 - fraction)) + 3000000000ULL) / 6000000000ULL;
  switch (sector) {
    case 0:
      return {255, up, low};
    case 1:
      return {down, 255, low};
    case 2:
      return {low, 255, up};
    case 3:
      return {low, down, 255};
    case 4:
      return {up, low, 255};
    default:
      return {255, low, down};
  }
}
}  // namespace

void MaterialEntityControlComponent::set_light_color_profile(lv_obj_t *root, lv_obj_t *kind, lv_obj_t *title,
                                                             lv_obj_t *preview, lv_obj_t *status, lv_obj_t *hue_label,
                                                             lv_obj_t *hue_slider, lv_obj_t *saturation_label,
                                                             lv_obj_t *saturation_slider, lv_obj_t *cancel,
                                                             lv_obj_t *cancel_label, lv_obj_t *apply,
                                                             lv_obj_t *apply_label, lv_obj_t *main_button,
                                                             lv_obj_t *main_label, uint8_t hit_padding, bool enabled) {
  if (this->setup_ready_)
    return;
  this->light_color_widgets_ = {
      root,   kind,         title, preview,     status,      hue_label, hue_slider, saturation_label, saturation_slider,
      cancel, cancel_label, apply, apply_label, main_button, main_label};
  this->light_color_hit_padding_ = hit_padding;
  this->light_color_enabled_ = enabled;
}

bool MaterialEntityControlComponent::setup_light_color_profile_() {
  auto &widgets = this->light_color_widgets_;
  if (widgets[ROOT] == nullptr)
    return !this->light_color_enabled_;
  if (!lv_obj_check_type(widgets[ROOT], &lv_obj_class) || lv_obj_get_parent(widgets[ROOT]) != this->root_ ||
      this->light_color_hit_padding_ > 64)
    return false;
  for (size_t i = 0; i < widgets.size(); i++) {
    if (widgets[i] == nullptr || !within(i < MAIN_BUTTON ? widgets[ROOT] : this->light_detail_[0], widgets[i]))
      return false;
    for (size_t j = 0; j < i; j++)
      if (widgets[i] == widgets[j])
        return false;
  }
  for (auto index :
       {KIND, TITLE, PREVIEW, STATUS, HUE_LABEL, SATURATION_LABEL, CANCEL_LABEL, APPLY_LABEL, MAIN_LABEL}) {
    if (!lv_obj_check_type(widgets[index], &lv_label_class))
      return false;
    lv_obj_remove_flag(widgets[index], static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  }
  for (auto index : {HUE, SATURATION}) {
    if (!lv_obj_check_type(widgets[index], &lv_slider_class))
      return false;
    // Only the neutral root receives touch; the slider class never mutates
    // value/pressed drawing before the selected owner admits the pointer.
    lv_obj_remove_flag(widgets[index], static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_slider_set_range(widgets[index], 0, index == HUE ? 360000 : 100000);
  }
  lv_obj_add_flag(widgets[ROOT], static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK));
  lv_obj_remove_flag(widgets[ROOT], LV_OBJ_FLAG_SCROLLABLE);
  for (auto index : {ROOT, CANCEL, APPLY, MAIN_BUTTON})
    lv_obj_add_event_cb(widgets[index], input_event_cb_, LV_EVENT_ALL, this);
  for (auto index : {CANCEL, APPLY, MAIN_BUTTON}) {
    lv_obj_add_style(widgets[index], &COLOR_DISABLED_STYLE,
                     static_cast<lv_style_selector_t>(LV_PART_MAIN) | LV_STATE_DISABLED);
    lv_obj_set_style_recolor_opa(widgets[index], LV_OPA_TRANSP, LV_PART_MAIN);
  }
  this->hide_light_color_();
  hidden(widgets[MAIN_BUTTON], true);
  return this->owner_.set_detail_color_enabled(this->light_color_enabled_);
}

bool MaterialEntityControlComponent::populate_detail_color_features(ArduinoJson::JsonObject root) {
  const bool enabled = this->setup_ready_ && this->owner_.detail_color_enabled();
  root["v"] = 1;
  root["type"] = "entity_detail_color_v1";
  root["color_version"] = enabled ? 1 : 0;
  return enabled;
}

bool MaterialEntityControlComponent::bind_detail_color_features(std::string_view payload,
                                                                ArduinoJson::JsonObject root) {
  ControlText<32> channel;
  const bool accepted = this->setup_ready_ && !this->output_callback_active_ &&
                        this->codec_.parse_detail_color_bind(payload, channel) &&
                        this->owner_.accept_detail_color(channel);
  root["v"] = 1;
  root["type"] = "entity_detail_color_v1";
  root["color_version"] = accepted ? 1 : 0;
  root["channel"] = accepted ? channel.bytes.data() : "";
  root["accepted"] = accepted;
  return accepted;
}

void MaterialEntityControlComponent::hide_light_color_() {
  hidden(this->light_color_widgets_[ROOT], true);
  this->light_color_axis_ = 255;
  this->light_color_ready_ = false;
}

void MaterialEntityControlComponent::update_light_color_preview_() {
  if (!this->owner_.is_detail_color())
    return;
  auto &widgets = this->light_color_widgets_;
  const auto &detail = *this->owner_.state().family_if<ControlDetailSnapshot>();
  const auto &source = *detail.color();
  const auto &draft = this->owner_.detail_color_draft();
  const bool drafted = draft.active && draft.generation == detail.state_generation && draft.view_seq == detail.view_seq;
  const bool changed =
      drafted && (draft.hue_milli != source.hue_milli || draft.saturation_milli != source.saturation_milli);
  const uint32_t hue = drafted ? draft.hue_milli : source.hue_milli;
  const uint32_t saturation = drafted ? draft.saturation_milli : source.saturation_milli;
  lv_slider_set_value(widgets[HUE], source.known ? hue : 0, LV_ANIM_OFF);
  lv_slider_set_value(widgets[SATURATION], source.known ? saturation : 0, LV_ANIM_OFF);
  char text[180];
  bool complete = true;
  if (source.known) {
    const auto rgb = changed ? preview_rgb(hue, saturation) : source.rgb;
    const lv_color_t background = lv_color_make(rgb[0], rgb[1], rgb[2]);
    // Select the contrasting black/white label from the actual preview color.
    const auto linear = [](uint8_t channel) {
      const float value = channel / 255.0f;
      return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
    };
    const float luminance = 0.2126f * linear(rgb[0]) + 0.7152f * linear(rgb[1]) + 0.0722f * linear(rgb[2]);
    const lv_color_t foreground = luminance > 0.179f ? lv_color_black() : lv_color_white();
    color(widgets[PREVIEW], LV_STYLE_BG_COLOR, background);
    color(widgets[PREVIEW], LV_STYLE_TEXT_COLOR, foreground);
    std::snprintf(text, sizeof(text), "#%02X%02X%02X", rgb[0], rgb[1], rgb[2]);
    complete &= label(widgets[PREVIEW], text);
    const uint32_t hue_tenths = ((hue + 50) / 100) % 3600, saturation_tenths = (saturation + 50) / 100;
    const char *separator = this->text_(32, ".");
    std::snprintf(text, sizeof(text), "%s  %u%s%u\xC2\xB0", this->text_(15, "Hue"),
                  static_cast<unsigned>(hue_tenths / 10), separator, static_cast<unsigned>(hue_tenths % 10));
    complete &= label(widgets[HUE_LABEL], text);
    std::snprintf(text, sizeof(text), "%s  %u%s%u%%", this->text_(16, "Saturation"),
                  static_cast<unsigned>(saturation_tenths / 10), separator,
                  static_cast<unsigned>(saturation_tenths % 10));
    complete &= label(widgets[SATURATION_LABEL], text);
  } else {
    color(widgets[PREVIEW], LV_STYLE_BG_COLOR, configured_style_color_(this->off_button_, LV_STYLE_BG_COLOR));
    color(widgets[PREVIEW], LV_STYLE_TEXT_COLOR, configured_style_color_(this->plus_label_, LV_STYLE_TEXT_COLOR));
    complete &= label(widgets[PREVIEW], "--");
    complete &= label(widgets[HUE_LABEL], this->text_(15, "Hue"));
    complete &= label(widgets[SATURATION_LABEL], this->text_(16, "Saturation"));
  }
  this->light_color_ready_ &= complete;
}

void MaterialEntityControlComponent::update_light_color_static_() {
  auto &widgets = this->light_color_widgets_;
  if (!this->owner_.is_detail_color() || widgets[ROOT] == nullptr) {
    this->hide_light_color_();
    return;
  }
  hidden(widgets[ROOT], false);
  lv_obj_update_layout(this->root_);
  const auto &source = *this->owner_.state().family_if<ControlDetailSnapshot>()->color();
  this->light_color_ready_ = label(widgets[KIND], this->text_(14, "Light color"));
  this->light_color_ready_ &= label(widgets[TITLE], this->owner_.state().title.bytes.data(), true);
  this->light_color_ready_ &= label(widgets[CANCEL_LABEL], this->text_(26, "Cancel"));
  this->light_color_ready_ &=
      label(widgets[APPLY_LABEL],
            this->text_(source.source_mode == 3 ? 21 : 25, source.source_mode == 3 ? "Switch to color" : "Apply"));
  this->update_light_color_preview_();
  this->update_light_color_controls_();
}

void MaterialEntityControlComponent::update_light_color_controls_() {
  auto &widgets = this->light_color_widgets_;
  const auto &source = *this->owner_.state().family_if<ControlDetailSnapshot>()->color();
  this->update_status_();
  const auto &draft = this->owner_.detail_color_draft();
  const bool changed =
      draft.active && (draft.hue_milli != source.hue_milli || draft.saturation_milli != source.saturation_milli);
  const char *status = source.reason == 1        ? this->text_(22, "Turn the light on first")
                       : source.reason == 2      ? this->text_(28, "Unsupported color mode")
                       : source.reason == 3      ? this->text_(29, "Color unavailable")
                       : source.reason == 4      ? this->text_(30, "Brightness unavailable")
                       : source.reason == 5      ? this->text_(31, "Source unavailable")
                       : changed                 ? this->text_(18, "Unsaved color")
                       : source.source_mode == 3 ? this->text_(20, "Current white temperature")
                                                 : this->text_(19, "Current color");
  if (this->owner_.is_command_pending() || this->status_ == ControlUiStatus::CONTROL_UI_STATUS_NOT_CONFIRMED ||
      this->status_ == ControlUiStatus::CONTROL_UI_STATUS_REJECTED)
    status = this->text_(static_cast<uint8_t>(this->status_) + 2, this->get_status_key());
  this->light_color_ready_ &= label(widgets[STATUS], status);
  const bool navigation =
      this->owner_.detail_view_ready() && !this->owner_.is_detail_view_pending() && !this->owner_.is_command_pending();
  enabled(widgets[CANCEL], navigation);
  enabled(widgets[APPLY], this->light_color_ready_ && this->owner_.can_apply_detail_color());
  for (auto index : {HUE, SATURATION})
    enabled(widgets[index], this->light_color_ready_ && this->owner_.can_detail_color());
  const auto neutral = configured_style_color_(this->off_button_, LV_STYLE_BG_COLOR),
             neutral_text = configured_style_color_(this->plus_label_, LV_STYLE_TEXT_COLOR);
  color(widgets[CANCEL], LV_STYLE_BG_COLOR, neutral);
  color(widgets[CANCEL_LABEL], LV_STYLE_TEXT_COLOR, neutral_text);
  const bool apply = this->light_color_ready_ && this->owner_.can_apply_detail_color();
  color(widgets[APPLY], LV_STYLE_BG_COLOR,
        apply ? configured_style_color_(this->on_button_, LV_STYLE_BG_COLOR) : neutral);
  color(widgets[APPLY_LABEL], LV_STYLE_TEXT_COLOR,
        apply ? configured_style_color_(this->minus_label_, LV_STYLE_TEXT_COLOR) : neutral_text);
}

void MaterialEntityControlComponent::refresh_light_color_palette() {
  if (this->setup_ready_ && this->owner_.is_detail_color())
    this->update_light_color_static_();
}

bool MaterialEntityControlComponent::begin_light_color_input_(lv_obj_t *target, lv_indev_t *indev, uint32_t pointer,
                                                              uint32_t now) {
  auto &widgets = this->light_color_widgets_;
  if (target == widgets[CANCEL])
    return this->owner_.begin_detail_navigation(pointer, now);
  if (!this->light_color_ready_)
    return false;
  if (target == widgets[APPLY])
    return this->owner_.begin_detail_color_apply(pointer, now);
  if (target != widgets[ROOT] || indev == nullptr)
    return false;
  lv_point_t point;
  lv_indev_get_point(indev, &point);
  for (uint8_t axis = 0; axis < 2; axis++) {
    lv_area_t area;
    lv_obj_get_coords(widgets[axis == 0 ? HUE : SATURATION], &area);
    const int32_t pad = this->light_color_hit_padding_;
    if (point.x < area.x1 - pad || point.x > area.x2 + pad || point.y < area.y1 - pad || point.y > area.y2 + pad)
      continue;
    if (!this->owner_.begin_detail_color(pointer, now))
      return false;
    this->light_color_axis_ = axis;
    return true;
  }
  return false;
}

void MaterialEntityControlComponent::update_light_color_input_(lv_indev_t *indev) {
  if (this->light_color_axis_ > 1 || indev == nullptr || !this->owner_.is_dragging())
    return;
  lv_point_t point;
  lv_indev_get_point(indev, &point);
  lv_area_t area;
  lv_obj_get_coords(this->light_color_widgets_[this->light_color_axis_ == 0 ? HUE : SATURATION], &area);
  const int32_t travel = std::max<int32_t>(1, lv_area_get_width(&area) - 1);
  const int32_t position = std::clamp(point.x - area.x1, int32_t{0}, travel);
  const uint32_t maximum = this->light_color_axis_ == 0 ? 360000 : 100000;
  uint32_t value = (uint64_t(position) * maximum + travel / 2) / travel;
  if (this->light_color_axis_ == 0 && value == 360000)
    value = 0;
  const auto &draft = this->owner_.detail_color_draft();
  if ((this->light_color_axis_ == 0 ? draft.hue_milli : draft.saturation_milli) == value)
    return;
  if (this->owner_.update_detail_color(this->pointer_sequence_, this->light_color_axis_ == 0 ? value : draft.hue_milli,
                                       this->light_color_axis_ == 1 ? value : draft.saturation_milli)) {
    this->update_light_color_preview_();
    this->update_light_color_controls_();
  }
}

void MaterialEntityControlComponent::release_light_color_input_(lv_obj_t *target, uint32_t pointer, uint32_t now) {
  auto &widgets = this->light_color_widgets_;
  if (target == widgets[ROOT])
    this->owner_.release_detail_color(pointer, now);
  else if (target == widgets[APPLY])
    this->owner_.release_detail_color_apply(pointer, now);
  else if (target == widgets[CANCEL] && this->owner_.cancel_detail_color(pointer, now))
    this->request_detail_facet_(ControlFacet::CONTROL_FACET_MAIN, now);
  this->light_color_axis_ = 255;
  this->update_light_color_static_();
}
}  // namespace esphome::lvgl_material
#endif
