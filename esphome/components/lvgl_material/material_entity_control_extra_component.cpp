#include "material_entity_control_component.h"
#ifdef USE_LVGL_MATERIAL_EXTRA_CONTROLS
#include "esphome/core/log.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

namespace esphome::lvgl_material {
namespace {
using Extra = ControlExtraSnapshot;
enum Common : size_t { ROOT, TITLE, KIND, UPDATED, STATUS };
enum Sensor : size_t { SENSOR_ROOT, VALUE, UNIT, SENSOR_UNKNOWN, TEMPERATURE, HUMIDITY };
enum State : size_t {
  STATE_ROOT,
  STATE_TEXT,
  BINARY_SURFACE,
  ALARM_SURFACE,
  LOCK_SURFACE,
  STATE_UNKNOWN,
  MOISTURE,
  ALARM,
  LOCKED,
  UNLOCKED,
  CALENDAR,
  LOCK_BUTTON,
  LOCK_LABEL
};
enum Calendar : size_t { CALENDAR_ROOT, BADGE, SUMMARY, START, END, ZONE, LOCATION, DESCRIPTION, EXCERPT };
void hidden(lv_obj_t *widget, bool value) {
  if (widget == nullptr || lv_obj_has_flag(widget, LV_OBJ_FLAG_HIDDEN) == value)
    return;
  if (value)
    lv_obj_add_flag(widget, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_remove_flag(widget, LV_OBJ_FLAG_HIDDEN);
}
void label(lv_obj_t *widget, const char *text) {
  if (std::strcmp(lv_label_get_text(widget), text) != 0)
    lv_label_set_text(widget, text);
}
void enabled(lv_obj_t *widget, bool value) {
  if (value)
    lv_obj_remove_state(widget, LV_STATE_DISABLED);
  else
    lv_obj_add_state(widget, LV_STATE_DISABLED);
}
bool within(lv_obj_t *root, lv_obj_t *widget) {
  for (auto *parent = widget; parent != nullptr; parent = lv_obj_get_parent(parent))
    if (parent == root)
      return true;
  return false;
}
bool glyphs(const lv_font_t *font, std::string_view text, bool multiline) {
  if (font == nullptr)
    return false;
  // Wire values stay single-line. A caller may explicitly compose complete
  // native lines; validate each line without admitting other control bytes.
  size_t start = 0;
  do {
    const size_t end = multiline ? text.find('\n', start) : std::string_view::npos;
    if (!number_plain_utf8(text.substr(start, end == std::string_view::npos ? end : end - start)))
      return false;
    if (end == std::string_view::npos)
      break;
    start = end + 1;
  } while (start <= text.size());
  for (size_t offset = 0; offset < text.size();) {
    const uint8_t first = text[offset++];
    if (multiline && first == '\n')
      continue;
    uint32_t point = first;
    size_t tail = 0;
    if (first >= 0xf0) {
      point &= 7;
      tail = 3;
    } else if (first >= 0xe0) {
      point &= 15;
      tail = 2;
    } else if (first >= 0xc0) {
      point &= 31;
      tail = 1;
    }
    for (size_t next = 0; next < tail; next++)
      point = (point << 6) | (static_cast<uint8_t>(text[offset++]) & 63);
    lv_font_glyph_dsc_t description{};
    if (!lv_font_get_glyph_dsc(font, &description, point, 0) || description.is_placeholder)
      return false;
  }
  return true;
}
bool fits(lv_obj_t *widget, const char *text, const lv_font_t *font, bool multiline = false) {
  if (!glyphs(font, text, multiline))
    return false;
  const int32_t width = lv_obj_get_content_width(widget), height = lv_obj_get_content_height(widget);
  if (width <= 0 || height <= 0)
    return false;
  lv_point_t size;
  lv_text_get_size(&size, text, font, lv_obj_get_style_text_letter_space(widget, LV_PART_MAIN),
                   lv_obj_get_style_text_line_space(widget, LV_PART_MAIN), width, LV_TEXT_FLAG_NONE);
  return size.x <= width && size.y <= height;
}
bool format_complete(char *buffer, size_t capacity, int length) {
  if (length >= 0 && static_cast<size_t>(length) < capacity)
    return true;
  std::strcpy(buffer, "--");
  return false;
}
}  // namespace

void MaterialEntityControlComponent::set_extra_profile(lv_obj_t *root, lv_obj_t *title, lv_obj_t *kind,
                                                       lv_obj_t *updated, lv_obj_t *status) {
  if (!this->setup_ready_)
    this->extra_common_ = {root, title, kind, updated, status};
}
void MaterialEntityControlComponent::set_extra_sensor_profile(lv_obj_t *root, lv_obj_t *value, lv_obj_t *unit,
                                                              lv_obj_t *unknown, lv_obj_t *temperature,
                                                              lv_obj_t *humidity, const lv_font_t *compact) {
  if (!this->setup_ready_) {
    this->extra_sensor_ = {root, value, unit, unknown, temperature, humidity};
    this->extra_compact_fonts_[0] = compact;
  }
}
void MaterialEntityControlComponent::set_extra_state_profile(lv_obj_t *root, lv_obj_t *state, lv_obj_t *binary_surface,
                                                             lv_obj_t *alarm_surface, lv_obj_t *lock_surface,
                                                             lv_obj_t *unknown, lv_obj_t *moisture, lv_obj_t *alarm,
                                                             lv_obj_t *locked, lv_obj_t *unlocked, lv_obj_t *calendar,
                                                             lv_obj_t *button, lv_obj_t *caption,
                                                             const lv_font_t *compact) {
  if (!this->setup_ready_) {
    this->extra_state_ = {root,  state,  binary_surface, alarm_surface, lock_surface, unknown, moisture,
                          alarm, locked, unlocked,       calendar,      button,       caption};
    this->extra_compact_fonts_[1] = compact;
  }
}
void MaterialEntityControlComponent::set_extra_calendar_profile(lv_obj_t *root, lv_obj_t *badge, lv_obj_t *summary,
                                                                lv_obj_t *start, lv_obj_t *end, lv_obj_t *zone,
                                                                lv_obj_t *location, lv_obj_t *description,
                                                                lv_obj_t *truncation, const lv_font_t *compact) {
  if (!this->setup_ready_) {
    this->extra_calendar_ = {root, badge, summary, start, end, zone, location, description, truncation};
    this->extra_compact_fonts_[2] = compact;
  }
}
void MaterialEntityControlComponent::set_extra_profiles(bool info, bool lock) {
  if (!this->setup_ready_)
    this->extra_configured_ = (info ? 1 : 0) | (lock ? 2 : 0);
}
bool MaterialEntityControlComponent::setup_extra_profile_() {
  if (this->extra_common_[ROOT] == nullptr)
    return this->extra_configured_ == 0;
  if (!this->number_profile_ || this->extra_configured_ == 0 ||
      lv_obj_get_parent(this->extra_common_[ROOT]) != this->root_)
    return false;
  const auto validate = [&](const auto &widgets, lv_obj_t *parent) {
    for (auto *widget : widgets)
      if (widget == nullptr || !within(widgets[0], widget))
        return false;
    if (widgets[0] != parent && lv_obj_get_parent(widgets[0]) != parent)
      return false;
    for (size_t i = 0; i < widgets.size(); i++)
      for (size_t j = 0; j < i; j++)
        if (widgets[i] == widgets[j])
          return false;
    hidden(widgets[0], true);
    return true;
  };
  if (!validate(this->extra_common_, this->extra_common_[ROOT]) ||
      !validate(this->extra_sensor_, this->extra_common_[ROOT]) ||
      !validate(this->extra_state_, this->extra_common_[ROOT]) ||
      !validate(this->extra_calendar_, this->extra_common_[ROOT]))
    return false;
  for (auto *widget : {this->extra_common_[TITLE], this->extra_common_[KIND], this->extra_common_[UPDATED],
                       this->extra_common_[STATUS], this->extra_sensor_[VALUE], this->extra_sensor_[UNIT],
                       this->extra_state_[STATE_TEXT], this->extra_state_[LOCK_LABEL]})
    if (!lv_obj_check_type(widget, &lv_label_class))
      return false;
  for (size_t i = 1; i < this->extra_calendar_.size(); i++)
    if (!lv_obj_check_type(this->extra_calendar_[i], &lv_label_class))
      return false;
  if (!lv_obj_check_type(this->extra_state_[LOCK_BUTTON], &lv_button_class) ||
      !within(this->extra_state_[LOCK_BUTTON], this->extra_state_[LOCK_LABEL]))
    return false;
  for (auto *widget : {this->extra_common_[UPDATED], this->extra_common_[STATUS], this->extra_sensor_[VALUE],
                       this->extra_sensor_[UNIT], this->extra_state_[STATE_TEXT]})
    lv_label_set_long_mode(widget, LV_LABEL_LONG_MODE_WRAP);
  for (size_t i = 1; i < this->extra_calendar_.size(); i++)
    lv_label_set_long_mode(this->extra_calendar_[i], LV_LABEL_LONG_MODE_WRAP);
  this->extra_normal_fonts_ = {lv_obj_get_style_text_font(this->extra_sensor_[VALUE], LV_PART_MAIN),
                               lv_obj_get_style_text_font(this->extra_state_[STATE_TEXT], LV_PART_MAIN),
                               lv_obj_get_style_text_font(this->extra_calendar_[SUMMARY], LV_PART_MAIN)};
  this->style_disabled_control_(this->extra_state_[LOCK_BUTTON]);
  lv_obj_add_event_cb(this->extra_state_[LOCK_BUTTON], input_event_cb_, LV_EVENT_ALL, this);
#ifdef USE_LVGL_MATERIAL_INFO_CONTEXT
  if (!this->setup_info_context_())
    return false;
#endif
  if (!this->owner_.set_extra_profiles(this->extra_configured_))
    return false;
#ifdef USE_LVGL_MATERIAL_INFO_AGENDA
  if (!this->setup_agenda_profile_())
    return false;
#endif
#ifdef USE_LVGL_MATERIAL_INFO_STATISTICS
  if (!this->setup_statistics_profile_())
    return false;
#endif
  return true;
}

bool MaterialEntityControlComponent::query_extra_features(std::string_view payload, ArduinoJson::JsonObject root) {
  return this->extra_features_(payload, root, false);
}
bool MaterialEntityControlComponent::bind_extra_features(std::string_view payload, ArduinoJson::JsonObject root) {
  return this->extra_features_(payload, root, true);
}
void MaterialEntityControlComponent::log_extra_view(const char *phase) const {
#ifdef USE_LVGL_MATERIAL_SCALAR_PERFORMANCE_METRICS
  const auto *extra = this->owner_.is_extra() ? this->owner_.state().family_if<Extra>() : nullptr;
  ESP_LOGI("scalar.performance",
           "%s extra_active=%u extra_retained=%u extra_state_seq=%u extra_generation=%u extra_type=%u "
           "extra_source=%u extra_lock_eligible=%u extra_updated_text=%s",
           phase, static_cast<unsigned>(extra != nullptr), static_cast<unsigned>(this->extra_widgets_active_),
           static_cast<unsigned>(extra != nullptr ? this->owner_.state().state_seq : 0),
           static_cast<unsigned>(extra != nullptr ? extra->state_generation : 0),
           static_cast<unsigned>(extra != nullptr ? extra->type : 255),
           static_cast<unsigned>(extra != nullptr ? extra->source : 255),
           static_cast<unsigned>(this->owner_.can_extra_lock()), extra != nullptr ? extra->updated.bytes.data() : "");
#ifdef USE_LVGL_MATERIAL_INFO_AGENDA
  this->log_agenda_view(phase);
#endif
#ifdef USE_LVGL_MATERIAL_INFO_STATISTICS
  this->log_statistics_view(phase);
#endif
#else
  (void) phase;
#endif
}
bool MaterialEntityControlComponent::extra_features_(std::string_view payload, ArduinoJson::JsonObject root,
                                                     bool bind) {
  ControlText<32> channel;
  uint16_t profiles = 0;
  const bool parsed = this->setup_ready_ && !this->output_callback_active_ &&
                      this->codec_.parse_extra_features(payload, channel, profiles, this->extra_configured_, bind);
  const bool accepted = parsed && (bind ? this->owner_.accept_extra_features(channel, profiles)
                                        : this->owner_.is_bound() && channel == this->owner_.bound_channel());
  if (!accepted)
    profiles = 0;
  root["v"] = 1;
  root["type"] = "entity_panel_extra_v1";
  root["channel"] = parsed ? channel.bytes.data() : "";
  if (bind)
    root["accepted"] = accepted;
  auto values = root["profiles"].to<ArduinoJson::JsonArray>();
  // Wire profiles are sorted by name, independently of stable profile bits.
  const auto append = [&](uint16_t bit, const char *name, uint8_t version = 1) {
    if ((profiles & bit) != 0) {
      auto profile = values.add<ArduinoJson::JsonObject>();
      profile["name"] = name;
      profile["version"] = version;
    }
  };
#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS
  append(8, "action", (profiles & 16U) != 0 ? 2 : 1);
#endif
#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
  append(64, "datetime");
#endif
  append(1, "info");
#ifdef USE_LVGL_MATERIAL_INFO_CONTEXT
  append(32, "info_context");
#endif
  append(2, "lock");
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
  append(4, "media");
#endif
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
  append(128, "text");
#endif
#ifdef USE_LVGL_MATERIAL_TILE_EVENT_CONTROLS
  append(256, "tile_event");
#endif
  return accepted;
}

bool MaterialEntityControlComponent::extra_text_(lv_obj_t *widget, const char *text, const lv_font_t *normal,
                                                 const lv_font_t *compact, bool multiline) {
  const auto *current = lv_obj_get_style_text_font(widget, LV_PART_MAIN);
  if (std::strcmp(lv_label_get_text(widget), text) == 0 && fits(widget, text, current, multiline))
    return true;
  if (normal == nullptr)
    normal = current;
  const auto *chosen = normal;
  bool complete = fits(widget, text, chosen, multiline);
  if (!complete && compact != nullptr) {
    chosen = compact;
    complete = fits(widget, text, chosen, multiline);
  }
  if (!complete) {
    chosen = compact != nullptr ? compact : normal;
    text = "--";
  }
  if (current != chosen)
    lv_obj_set_style_text_font(widget, chosen, LV_PART_MAIN);
  label(widget, text);
  return complete;
}

void MaterialEntityControlComponent::restore_extra_widgets_() {
#ifdef USE_LVGL_MATERIAL_INFO_STATISTICS
  this->clear_statistics_widgets_();
#endif
  if (!this->extra_widgets_active_)
    return;
#ifdef USE_LVGL_MATERIAL_INFO_AGENDA
  this->clear_agenda_widgets_();
#endif
  if (!this->owner_.has_selection()) {
#ifdef USE_LVGL_MATERIAL_INFO_CONTEXT
    this->clear_info_context_();
#endif
    hidden(this->extra_sensor_[SENSOR_ROOT], true);
    hidden(this->extra_calendar_[CALENDAR_ROOT], true);
    hidden(this->extra_state_[STATE_ROOT], false);
    for (size_t i = BINARY_SURFACE; i <= LOCK_BUTTON; i++)
      hidden(this->extra_state_[i], i != STATE_UNKNOWN);
    label(this->extra_state_[STATE_TEXT], "--");
    label(this->extra_common_[UPDATED], "");
    this->extra_text_(this->extra_common_[STATUS], this->owner_.last_reason() == ControlReason::CONTROL_REASON_EXPIRED
                                                       ? this->text_(25, "Observation expired")
                                                       : this->text_(23, "Unknown"));
    enabled(this->extra_state_[LOCK_BUTTON], false);
    return;
  }
  lv_obj_t *old[] = {this->title_,     this->hit_,        this->value_,       this->unknown_value_,
                     this->on_button_, this->off_button_, this->state_label_, this->status_label_};
  for (size_t i = 0; i < 8; i++)
    hidden(old[i], (this->extra_hidden_mask_ & (1U << i)) != 0);
  hidden(this->extra_common_[ROOT], true);
  this->extra_widgets_active_ = false;
}

void MaterialEntityControlComponent::update_extra_static_() {
  const auto *extra = this->owner_.state().family_if<Extra>();
  if (!this->owner_.is_extra() || extra == nullptr)
    return;
  lv_obj_t *old[] = {this->title_,     this->hit_,        this->value_,       this->unknown_value_,
                     this->on_button_, this->off_button_, this->state_label_, this->status_label_};
  if (!this->extra_widgets_active_) {
    this->extra_hidden_mask_ = 0;
    for (size_t i = 0; i < 8; i++)
      if (old[i] != nullptr && lv_obj_has_flag(old[i], LV_OBJ_FLAG_HIDDEN))
        this->extra_hidden_mask_ |= 1U << i;
    this->extra_widgets_active_ = true;
  }
  for (auto *widget : old)
    hidden(widget, true);
  hidden(this->extra_common_[ROOT], false);
#ifdef USE_LVGL_MATERIAL_INFO_STATISTICS
  if (extra->type == 8) {
#ifdef USE_LVGL_MATERIAL_INFO_AGENDA
    this->clear_agenda_widgets_();
#endif
#ifdef USE_LVGL_MATERIAL_INFO_CONTEXT
    this->clear_info_context_();
#endif
    this->update_statistics_static_(*extra);
    return;
  }
  this->clear_statistics_widgets_();
#endif
#ifdef USE_LVGL_MATERIAL_INFO_AGENDA
  if (extra->type == 7) {
#ifdef USE_LVGL_MATERIAL_INFO_CONTEXT
    this->clear_info_context_();
#endif
    this->update_agenda_static_(*extra);
    return;
  }
  this->clear_agenda_widgets_();
#endif
#ifdef USE_LVGL_MATERIAL_INFO_CONTEXT
  if (extra->type == 5 || extra->type == 6) {
    this->update_info_context_(*extra);
    return;
  }
  this->clear_info_context_();
#endif
  const auto *calendar = std::get_if<Extra::Calendar>(&extra->data);
  const bool event = calendar != nullptr && extra->available() && calendar->event == 0;
  hidden(this->extra_sensor_[SENSOR_ROOT], extra->type != 0);
  hidden(this->extra_state_[STATE_ROOT], extra->type == 0 || event);
  hidden(this->extra_calendar_[CALENDAR_ROOT], !event);
  std::array<bool, HUMIDITY - SENSOR_UNKNOWN + 1> sensor_hidden;
  std::array<bool, LOCK_BUTTON - BINARY_SURFACE + 1> state_hidden;
  sensor_hidden.fill(true);
  state_hidden.fill(true);
  lv_obj_update_layout(this->extra_common_[ROOT]);
  label(this->extra_common_[TITLE], this->owner_.state().title.bytes.data());
  bool complete = true;
  char text[256];
  if (extra->updated.view().empty())
    label(this->extra_common_[UPDATED], "");
  else {
    complete &= format_complete(
        text, sizeof(text),
        std::snprintf(text, sizeof(text), "%s: %s", this->text_(22, "HA update"), extra->updated.bytes.data()));
    complete &= this->extra_text_(this->extra_common_[UPDATED], text);
  }
  uint8_t kind = 14 + extra->type;
  const char *kind_fallback = "Sensor";
  if (const auto *sensor = std::get_if<Extra::Sensor>(&extra->data)) {
    size_t icon = SENSOR_UNKNOWN;
    if (sensor->device_class.view() == "temperature") {
      kind = 15;
      icon = TEMPERATURE;
      kind_fallback = "Temperature";
    } else if (sensor->device_class.view() == "humidity") {
      kind = 16;
      icon = HUMIDITY;
      kind_fallback = "Humidity";
    }
    sensor_hidden[icon - SENSOR_UNKNOWN] = false;
    const char *value = sensor->status == 0   ? sensor->value.bytes.data()
                        : sensor->status == 1 ? this->text_(26, "Empty value")
                        : sensor->status == 2 ? this->text_(27, "Invalid value")
                        : sensor->status == 3 ? this->text_(28, "Value too long")
                                              : this->text_(23, "Unknown");
    const bool unit_ok = fits(this->extra_sensor_[UNIT], sensor->unit.bytes.data(),
                              lv_obj_get_style_text_font(this->extra_sensor_[UNIT], LV_PART_MAIN));
    const bool value_ok = this->extra_text_(this->extra_sensor_[VALUE], unit_ok ? value : "--",
                                            this->extra_normal_fonts_[0], this->extra_compact_fonts_[0]);
    this->extra_text_(this->extra_sensor_[UNIT], unit_ok ? sensor->unit.bytes.data() : "");
    complete &= value_ok && unit_ok;
  } else if (const auto *state = std::get_if<Extra::State>(&extra->data)) {
    const bool alarm = extra->type == 3;
    const bool moisture = !alarm && state->device_class.view() == "moisture";
    kind = alarm ? 19 : moisture ? 18 : 17;
    kind_fallback = alarm ? "Alarm" : moisture ? "Moisture" : "Binary sensor";
    state_hidden[(alarm ? ALARM_SURFACE : BINARY_SURFACE) - BINARY_SURFACE] = false;
    state_hidden[(alarm ? ALARM : moisture ? MOISTURE : STATE_UNKNOWN) - BINARY_SURFACE] = false;
    const char *value = state->status == 0   ? state->text.bytes.data()
                        : state->status == 1 ? this->text_(29, "Empty state")
                        : state->status == 2 ? this->text_(30, "Invalid state")
                        : state->status == 3 ? this->text_(31, "State too long")
                                             : this->text_(23, "Unknown");
    complete &= this->extra_text_(this->extra_state_[STATE_TEXT], value, this->extra_normal_fonts_[1],
                                  this->extra_compact_fonts_[1]);
  } else if (calendar != nullptr) {
    kind = 20;
    kind_fallback = "Calendar";
    if (!event) {
      state_hidden[CALENDAR - BINARY_SURFACE] = false;
      const char *value = calendar->event == 1   ? this->text_(36, "No event")
                          : calendar->event == 2 ? this->text_(37, "Invalid event")
                                                 : this->text_(38, "Unknown event");
      complete &= this->extra_text_(this->extra_state_[STATE_TEXT], value, this->extra_normal_fonts_[1],
                                    this->extra_compact_fonts_[1]);
      for (size_t i = 1; i < this->extra_calendar_.size(); i++)
        label(this->extra_calendar_[i], "");
    } else {
      const char *badge = calendar->active == 1 ? this->text_(34, "In progress") : this->text_(33, "Event");
      complete &=
          format_complete(text, sizeof(text),
                          std::snprintf(text, sizeof(text), "%s%s%s", badge, calendar->all_day == 1 ? " · " : "",
                                        calendar->all_day == 1 ? this->text_(35, "All day") : ""));
      complete &= this->extra_text_(this->extra_calendar_[BADGE], text);
      complete &= this->extra_text_(this->extra_calendar_[SUMMARY], calendar->summary.bytes.data(),
                                    this->extra_normal_fonts_[2], this->extra_compact_fonts_[2]);
      complete &= format_complete(
          text, sizeof(text),
          std::snprintf(text, sizeof(text), "%s: %s", this->text_(39, "Start"), calendar->start.bytes.data()));
      complete &= this->extra_text_(this->extra_calendar_[START], text);
      complete &= format_complete(
          text, sizeof(text),
          std::snprintf(text, sizeof(text), "%s: %s", this->text_(40, "End"), calendar->end.bytes.data()));
      complete &= this->extra_text_(this->extra_calendar_[END], text);
      complete &= this->extra_text_(this->extra_calendar_[ZONE], calendar->zone.bytes.data());
      complete &= this->extra_text_(this->extra_calendar_[LOCATION], calendar->location.bytes.data());
      complete &= this->extra_text_(this->extra_calendar_[DESCRIPTION], calendar->description.bytes.data());
      complete &=
          this->extra_text_(this->extra_calendar_[EXCERPT], calendar->truncated ? this->text_(41, "Excerpt") : "");
    }
  } else if (const auto *lock = std::get_if<Extra::Lock>(&extra->data)) {
    kind = 21;
    kind_fallback = "Lock";
    state_hidden[LOCK_SURFACE - BINARY_SURFACE] = false;
    state_hidden[(lock->phase == 0                       ? LOCKED
                  : lock->phase == 1 || lock->phase == 4 ? UNLOCKED
                                                         : STATE_UNKNOWN) -
                 BINARY_SURFACE] = false;
    const char *fallback[] = {"Locked", "Unlocked", "Locking", "Unlocking", "Open", "Opening", "Jammed", "Unknown"};
    const bool state_ok =
        this->extra_text_(this->extra_state_[STATE_TEXT], this->text_(43 + lock->phase, fallback[lock->phase]),
                          this->extra_normal_fonts_[1], this->extra_compact_fonts_[1]);
    state_hidden[LOCK_BUTTON - BINARY_SURFACE] = !lock->capability;
    const bool caption_ok = this->extra_text_(this->extra_state_[LOCK_LABEL], this->text_(42, "Lock"));
    complete &= state_ok && caption_ok;
    enabled(this->extra_state_[LOCK_BUTTON], this->owner_.can_extra_lock() && state_ok && caption_ok);
    const bool active = !lv_obj_has_state(this->extra_state_[LOCK_BUTTON], LV_STATE_DISABLED);
    const auto bg = configured_style_color_(active ? this->on_button_ : this->off_button_, LV_STYLE_BG_COLOR);
    const auto fg = configured_style_color_(active ? this->minus_label_ : this->plus_label_, LV_STYLE_TEXT_COLOR);
    if (!lv_color_eq(lv_obj_get_style_bg_color(this->extra_state_[LOCK_BUTTON], LV_PART_MAIN), bg))
      lv_obj_set_style_bg_color(this->extra_state_[LOCK_BUTTON], bg, LV_PART_MAIN);
    if (!lv_color_eq(lv_obj_get_style_text_color(this->extra_state_[LOCK_LABEL], LV_PART_MAIN), fg))
      lv_obj_set_style_text_color(this->extra_state_[LOCK_LABEL], fg, LV_PART_MAIN);
  }
  for (size_t i = SENSOR_UNKNOWN; i <= HUMIDITY; i++)
    hidden(this->extra_sensor_[i], sensor_hidden[i - SENSOR_UNKNOWN]);
  for (size_t i = BINARY_SURFACE; i <= LOCK_BUTTON; i++)
    hidden(this->extra_state_[i], state_hidden[i - BINARY_SURFACE]);
  complete &= this->extra_text_(this->extra_common_[KIND], this->text_(kind, kind_fallback));
  this->update_status_();
  const bool idle = this->status_ == ControlUiStatus::CONTROL_UI_STATUS_READY ||
                    this->status_ == ControlUiStatus::CONTROL_UI_STATUS_STATE_MATCHES ||
                    this->status_ == ControlUiStatus::CONTROL_UI_STATUS_ALREADY_MATCHED;
  const char *status = idle ? "" : this->text_(static_cast<uint8_t>(this->status_) + 2, this->get_status_key());
  if (idle && !extra->available())
    status = this->text_(extra->source == 1 ? 23 : 24, extra->source == 1 ? "Unknown" : "Unavailable");
  if (!complete && idle)
    status = this->text_(32, "Full observation cannot be displayed");
  this->extra_text_(this->extra_common_[STATUS], status);
}
#ifdef USE_LVGL_MATERIAL_INFO_AGENDA
void MaterialEntityControlComponent::set_agenda_profile(lv_obj_t *previous, lv_obj_t *previous_label, lv_obj_t *next,
                                                        lv_obj_t *next_label, bool enabled) {
  if (!this->setup_ready_) {
    this->agenda_widgets_ = {previous, previous_label, next, next_label};
    this->agenda_configured_ = enabled;
  }
}
bool MaterialEntityControlComponent::setup_agenda_profile_() {
  if (this->agenda_widgets_[0] == nullptr)
    return !this->agenda_configured_;
  if ((this->extra_configured_ & 1U) == 0)
    return false;
  for (size_t i = 0; i < 4; i++) {
    auto *widget = this->agenda_widgets_[i];
    if (widget == nullptr || !within(this->extra_common_[ROOT], widget))
      return false;
    for (size_t j = 0; j < i; j++)
      if (widget == this->agenda_widgets_[j])
        return false;
    for (const auto *existing : this->extra_common_)
      if (existing == widget)
        return false;
    for (const auto *existing : this->extra_sensor_)
      if (existing == widget)
        return false;
    for (const auto *existing : this->extra_state_)
      if (existing == widget)
        return false;
    for (const auto *existing : this->extra_calendar_)
      if (existing == widget)
        return false;
#ifdef USE_LVGL_MATERIAL_INFO_CONTEXT
    for (const auto *existing : this->extra_person_)
      if (existing == widget)
        return false;
    for (const auto *existing : this->extra_sun_)
      if (existing == widget)
        return false;
#endif
    if (i % 2 == 0) {
      if (!lv_obj_check_type(widget, &lv_button_class) || lv_obj_get_parent(widget) != this->extra_common_[ROOT])
        return false;
      this->style_disabled_control_(widget);
      lv_obj_add_flag(widget, LV_OBJ_FLAG_PRESS_LOCK);
      lv_obj_add_event_cb(widget, input_event_cb_, LV_EVENT_ALL, this);
      hidden(widget, true);
    } else if (!lv_obj_check_type(widget, &lv_label_class) || !within(this->agenda_widgets_[i - 1], widget))
      return false;
  }
  return this->owner_.set_info_agenda(this->agenda_configured_);
}
bool MaterialEntityControlComponent::populate_info_agenda_features(ArduinoJson::JsonObject root) {
  const bool enabled = this->setup_ready_ && this->owner_.info_agenda_enabled();
  root["v"] = 1;
  root["type"] = "entity_info_agenda_v1";
  root["agenda_version"] = enabled ? 1 : 0;
  return enabled;
}
bool MaterialEntityControlComponent::bind_info_agenda_features(std::string_view payload, ArduinoJson::JsonObject root) {
  ControlText<32> channel;
  const bool accepted = this->setup_ready_ && !this->output_callback_active_ &&
                        this->codec_.parse_info_agenda_bind(payload, channel) &&
                        this->owner_.accept_info_agenda(channel);
  root["v"] = 1;
  root["type"] = "entity_info_agenda_v1";
  root["agenda_version"] = accepted ? 1 : 0;
  root["channel"] = accepted ? channel.bytes.data() : "";
  root["accepted"] = accepted;
  return accepted;
}
void MaterialEntityControlComponent::clear_agenda_widgets_() {
  this->agenda_ready_ = false;
  for (size_t i : {0U, 2U})
    hidden(this->agenda_widgets_[i], true);
  hidden(this->extra_common_[UPDATED], false);
  // Closing the selection must not retain event contents in hidden LVGL labels.
  if (!this->owner_.has_selection())
    for (size_t i = 1; i < this->extra_calendar_.size(); i++)
      label(this->extra_calendar_[i], "");
}
void MaterialEntityControlComponent::update_agenda_static_(const Extra &extra) {
  const auto &agenda = std::get<Extra::Agenda>(extra.data);
  const bool event = extra.available() && agenda.status == 1 && agenda.count != 0;
  hidden(this->extra_common_[UPDATED], true);
  label(this->extra_common_[UPDATED], "");
  hidden(this->extra_sensor_[SENSOR_ROOT], true);
  hidden(this->extra_calendar_[CALENDAR_ROOT], !event);
  hidden(this->extra_state_[STATE_ROOT], event);
  for (size_t i = BINARY_SURFACE; i <= LOCK_BUTTON; i++)
    hidden(this->extra_state_[i], i != CALENDAR);
  for (size_t i : {0U, 2U})
    hidden(this->agenda_widgets_[i], false);
  lv_obj_update_layout(this->extra_common_[ROOT]);
  label(this->extra_common_[TITLE], this->owner_.state().title.bytes.data());
  bool complete = this->extra_text_(this->extra_common_[KIND], this->text_(93, "Agenda"));
  complete &= this->extra_text_(this->agenda_widgets_[1], this->text_(94, "Previous"));
  complete &= this->extra_text_(this->agenda_widgets_[3], this->text_(95, "Next"));
  char text[192];
  if (event) {
    complete &= format_complete(text, sizeof(text),
                                std::snprintf(text, sizeof(text), "%u / %u%s \xC2\xB7 %s", unsigned(agenda.index + 1),
                                              unsigned(agenda.count), agenda.has_more ? "+" : "",
                                              agenda.all_day ? this->text_(35, "All day") : this->text_(33, "Event")));
    complete &= this->extra_text_(this->extra_calendar_[BADGE], text);
    complete &= this->extra_text_(this->extra_calendar_[SUMMARY], agenda.summary.bytes.data(),
                                  this->extra_normal_fonts_[2], this->extra_compact_fonts_[2]);
    complete &= format_complete(
        text, sizeof(text),
        std::snprintf(text, sizeof(text), "%s: %s", this->text_(39, "Start"), agenda.start.bytes.data()));
    complete &= this->extra_text_(this->extra_calendar_[START], text);
    complete &=
        format_complete(text, sizeof(text),
                        std::snprintf(text, sizeof(text), "%s: %s", this->text_(40, "End"), agenda.end.bytes.data()));
    complete &= this->extra_text_(this->extra_calendar_[END], text);
    complete &= this->extra_text_(this->extra_calendar_[ZONE], agenda.zone.bytes.data());
    complete &= this->extra_text_(this->extra_calendar_[LOCATION], agenda.location.bytes.data());
    complete &= this->extra_text_(this->extra_calendar_[DESCRIPTION], agenda.description.bytes.data());
    complete &= this->extra_text_(this->extra_calendar_[EXCERPT], agenda.truncated ? this->text_(41, "Excerpt") : "");
  } else {
    for (size_t i = 1; i < this->extra_calendar_.size(); i++)
      label(this->extra_calendar_[i], "");
    const char *state = !extra.available()
                            ? this->text_(extra.source == 2 ? 24 : 23, extra.source == 2 ? "Unavailable" : "Unknown")
                        : agenda.status == 0 ? this->text_(96, "Loading events")
                        : agenda.status == 2 ? this->text_(98, "Could not load events")
                                             : this->text_(97, "No events in this period");
    complete &= this->extra_text_(this->extra_state_[STATE_TEXT], state, this->extra_normal_fonts_[1],
                                  this->extra_compact_fonts_[1]);
  }
  const char *status = this->owner_.agenda_page_pending()  ? this->text_(96, "Loading events")
                       : this->owner_.agenda_page_failed() ? this->text_(100, "Could not change event")
                       : event && agenda.has_more          ? this->text_(99, "First 16 events")
                                                           : "";
  if (!complete)
    status = this->text_(32, "Full observation cannot be displayed");
  complete &= this->extra_text_(this->extra_common_[STATUS], status);
  this->agenda_ready_ = complete;
  for (size_t index = 0; index < 2; index++) {
    auto *button = this->agenda_widgets_[index * 2];
    auto *caption = this->agenda_widgets_[index * 2 + 1];
    const bool active = complete && this->owner_.can_agenda_page(index ? 1 : -1);
    enabled(button, active);
    const auto bg = configured_style_color_(active ? this->on_button_ : this->off_button_, LV_STYLE_BG_COLOR);
    const auto fg = configured_style_color_(active ? this->minus_label_ : this->plus_label_, LV_STYLE_TEXT_COLOR);
    if (!lv_color_eq(lv_obj_get_style_bg_color(button, LV_PART_MAIN), bg))
      lv_obj_set_style_bg_color(button, bg, LV_PART_MAIN);
    if (!lv_color_eq(lv_obj_get_style_text_color(caption, LV_PART_MAIN), fg))
      lv_obj_set_style_text_color(caption, fg, LV_PART_MAIN);
  }
}
void MaterialEntityControlComponent::agenda_input_event_(lv_event_code_t code, lv_obj_t *target, lv_indev_t *indev,
                                                         uint32_t now) {
  const int8_t direction = target == this->agenda_widgets_[0] ? -1 : target == this->agenda_widgets_[2] ? 1 : 0;
  if (!direction)
    return;
  if (code == LV_EVENT_PRESSED) {
    if (this->captured_target_ || !this->agenda_ready_ || !indev || lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER ||
        !this->is_presented_() || !lv_obj_is_visible(target) || lv_obj_has_state(target, LV_STATE_DISABLED) ||
        this->pointer_sequence_ == std::numeric_limits<uint32_t>::max())
      return;
#ifdef USE_LVGL_NAVIGATION_ADMISSION_GUARD
    if (this->view_lifecycle_.native_power_input_active())
      return;
#endif
    if (!this->owner_.begin_agenda_page(++this->pointer_sequence_, direction, now))
      return;
    this->captured_target_ = target;
    this->captured_indev_ = indev;
    this->captured_generation_ = this->owner_.admission_generation();
  }
  if (target != this->captured_target_ || indev != this->captured_indev_)
    return;
  lv_point_t point;
  lv_area_t bounds;
  lv_indev_get_point(indev, &point);
  lv_obj_get_coords(target, &bounds);
  if (!this->is_presented_() || !lv_obj_is_visible(target) || !this->owner_.is_dragging() ||
      this->captured_generation_ != this->owner_.admission_generation() || point.x < bounds.x1 || point.x > bounds.x2 ||
      point.y < bounds.y1 || point.y > bounds.y2) {
    this->cancel_pointer_();
    this->process_events_();
    return;
  }
  if (code == LV_EVENT_RELEASED) {
    this->captured_target_ = nullptr;
    this->captured_indev_ = nullptr;
    this->owner_.release_agenda_page(this->pointer_sequence_, now);
    this->update_static_();
  }
  this->process_events_();
}
void MaterialEntityControlComponent::log_agenda_view(const char *phase) const {
#ifdef USE_LVGL_MATERIAL_SCALAR_PERFORMANCE_METRICS
  const auto *extra = this->owner_.is_agenda() ? this->owner_.state().family_if<Extra>() : nullptr;
  const auto *agenda = extra ? std::get_if<Extra::Agenda>(&extra->data) : nullptr;
  ESP_LOGI("scalar.performance",
           "%s agenda_active=%u agenda_ready=%u agenda_pending=%u agenda_failed=%u "
           "agenda_generation=%u agenda_state_seq=%u agenda_request_seq=%u agenda_status=%u agenda_source=%u "
           "agenda_index=%u agenda_count=%u agenda_more=%u",
           phase, unsigned(agenda != nullptr), unsigned(this->agenda_ready_),
           unsigned(this->owner_.agenda_page_pending()), unsigned(this->owner_.agenda_page_failed()),
           unsigned(extra ? extra->state_generation : 0), unsigned(agenda ? this->owner_.state().state_seq : 0),
           unsigned(agenda ? agenda->request_seq : 0), unsigned(agenda ? agenda->status : 255),
           unsigned(extra ? extra->source : 255), unsigned(agenda ? agenda->index : 255),
           unsigned(agenda ? agenda->count : 0), unsigned(agenda && agenda->has_more));
#else
  (void) phase;
#endif
}
#endif

}  // namespace esphome::lvgl_material
#endif
