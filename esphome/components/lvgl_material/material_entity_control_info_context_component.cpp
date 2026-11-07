#include "material_entity_control_component.h"
#ifdef USE_LVGL_MATERIAL_INFO_CONTEXT
#include <cstdio>
#include <cstring>

namespace esphome::lvgl_material {
namespace {
enum Person : size_t { PERSON_ROOT, PERSON_ICON, PERSON_SURFACE, PERSON_STATE, PERSON_CHANGED, PERSON_ZONE };
enum Sun : size_t {
  SUN_ROOT, SUN_ICON, SUN_PHASE, RISING_CAPTION, RISING_VALUE, SETTING_CAPTION, SETTING_VALUE,
  ELEVATION_CAPTION, ELEVATION_VALUE, AZIMUTH_CAPTION, AZIMUTH_VALUE, SUN_ZONE
};
void hidden(lv_obj_t *widget, bool value) {
  if (widget == nullptr || lv_obj_has_flag(widget, LV_OBJ_FLAG_HIDDEN) == value)
    return;
  if (value)
    lv_obj_add_flag(widget, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_remove_flag(widget, LV_OBJ_FLAG_HIDDEN);
}
void clear(lv_obj_t *widget) {
  if (widget != nullptr && lv_label_get_text(widget)[0] != '\0')
    lv_label_set_text(widget, "");
}
bool within(lv_obj_t *root, lv_obj_t *widget) {
  for (auto *parent = widget; parent != nullptr; parent = lv_obj_get_parent(parent))
    if (parent == root)
      return true;
  return false;
}
}  // namespace

void MaterialEntityControlComponent::set_extra_person_profile(lv_obj_t *root, lv_obj_t *icon, lv_obj_t *surface,
                                                              lv_obj_t *state, lv_obj_t *changed, lv_obj_t *zone,
                                                              const lv_font_t *compact_state) {
  if (!this->setup_ready_) {
    this->extra_person_ = {root, icon, surface, state, changed, zone};
    this->extra_person_compact_ = compact_state;
  }
}
void MaterialEntityControlComponent::set_extra_sun_profile(lv_obj_t *root, lv_obj_t *icon, lv_obj_t *phase,
                                                           lv_obj_t *rising_caption, lv_obj_t *rising_value,
                                                           lv_obj_t *setting_caption, lv_obj_t *setting_value,
                                                           lv_obj_t *elevation_caption, lv_obj_t *elevation_value,
                                                           lv_obj_t *azimuth_caption, lv_obj_t *azimuth_value,
                                                           lv_obj_t *zone) {
  if (!this->setup_ready_)
    this->extra_sun_ = {root, icon, phase, rising_caption, rising_value, setting_caption, setting_value,
                         elevation_caption, elevation_value, azimuth_caption, azimuth_value, zone};
}
void MaterialEntityControlComponent::set_extra_info_context(bool enabled) {
  if (!this->setup_ready_)
    this->extra_configured_ = (this->extra_configured_ & ~32U) | (enabled ? 32U : 0U);
}
bool MaterialEntityControlComponent::setup_info_context_() {
  if (this->extra_person_[PERSON_ROOT] == nullptr && this->extra_sun_[SUN_ROOT] == nullptr)
    return (this->extra_configured_ & 32U) == 0;
  if ((this->extra_configured_ & 32U) != 0 && (this->extra_configured_ & 1U) == 0)
    return false;
  const auto validate = [&](const auto &widgets, size_t first_label) {
    if (widgets[0] == nullptr || lv_obj_get_parent(widgets[0]) != this->extra_common_[0])
      return false;
    for (size_t i = 0; i < widgets.size(); i++) {
      if (widgets[i] == nullptr || !within(widgets[0], widgets[i]))
        return false;
      for (size_t j = 0; j < i; j++)
        if (widgets[i] == widgets[j])
          return false;
      if (i >= first_label) {
        if (!lv_obj_check_type(widgets[i], &lv_label_class))
          return false;
        lv_label_set_long_mode(widgets[i], LV_LABEL_LONG_MODE_WRAP);
      }
      // These profiles observe state; no child is an action or scroll target.
      lv_obj_remove_flag(widgets[i], static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    }
    return true;
  };
  if (!validate(this->extra_person_, PERSON_STATE) || !validate(this->extra_sun_, SUN_PHASE))
    return false;
  this->extra_person_normal_ = lv_obj_get_style_text_font(this->extra_person_[PERSON_STATE], LV_PART_MAIN);
  this->clear_info_context_();
  return true;
}
void MaterialEntityControlComponent::clear_info_context_() {
  hidden(this->extra_person_[PERSON_ROOT], true);
  hidden(this->extra_sun_[SUN_ROOT], true);
  for (size_t i = PERSON_STATE; i < this->extra_person_.size(); i++)
    clear(this->extra_person_[i]);
  for (size_t i = SUN_PHASE; i < this->extra_sun_.size(); i++)
    clear(this->extra_sun_[i]);
  if (this->extra_person_[PERSON_ROOT] != nullptr) {
    const auto bg = configured_style_color_(this->off_button_, LV_STYLE_BG_COLOR);
    const auto fg = configured_style_color_(this->plus_label_, LV_STYLE_TEXT_COLOR);
    if (!lv_color_eq(lv_obj_get_style_bg_color(this->extra_person_[PERSON_SURFACE], LV_PART_MAIN), bg))
      lv_obj_set_style_bg_color(this->extra_person_[PERSON_SURFACE], bg, LV_PART_MAIN);
    if (!lv_color_eq(lv_obj_get_style_text_color(this->extra_person_[PERSON_ICON], LV_PART_MAIN), fg))
      lv_obj_set_style_text_color(this->extra_person_[PERSON_ICON], fg, LV_PART_MAIN);
  }
}

bool MaterialEntityControlComponent::info_context_date_(lv_obj_t *widget, uint8_t status, std::string_view value,
                                                        uint8_t prefix) {
  char buffer[160];
  const char *heading = prefix == 68 ? this->text_(68, "HA state since")
                        : prefix == 79 ? this->text_(79, "HA update") : "";
  const char *separator = prefix == 68 ? "\n" : prefix == 79 ? " · " : "";
  int length;
  if (status == 0 && control_info_context_datetime(value)) {
    static constexpr const char *MONTHS[] = {"January", "February", "March", "April", "May", "June", "July",
                                             "August", "September", "October", "November", "December"};
    const unsigned month = (value[5] - '0') * 10 + value[6] - '0';
    const unsigned day = (value[8] - '0') * 10 + value[9] - '0';
    length = std::snprintf(buffer, sizeof(buffer), "%s%s%u %s %.4s · %.5s\nUTC%.6s", heading, separator, day,
                           this->text_(80 + month, MONTHS[month - 1]), value.data(), value.data() + 11,
                           value.data() + 19);
  } else {
    const char *message = status == 1 ? this->text_(69, "Time unavailable")
                          : status == 3 ? this->text_(28, "Value too long")
                          : status == 4 ? this->text_(71, "Waiting for an updated time")
                                        : this->text_(70, "Date unavailable");
    length = std::snprintf(buffer, sizeof(buffer), "%s%s%s", heading, separator, message);
  }
  const bool complete = length >= 0 && static_cast<size_t>(length) < sizeof(buffer);
  return this->extra_text_(widget, complete ? buffer : "--", nullptr, nullptr, true) && complete;
}

void MaterialEntityControlComponent::update_info_context_(const ControlExtraSnapshot &extra) {
  hidden(this->extra_sensor_[0], true);
  hidden(this->extra_state_[0], true);
  hidden(this->extra_calendar_[0], true);
  const auto *person = std::get_if<ControlExtraSnapshot::Person>(&extra.data);
  const auto *sun = std::get_if<ControlExtraSnapshot::Sun>(&extra.data);
  hidden(this->extra_person_[PERSON_ROOT], person == nullptr);
  hidden(this->extra_sun_[SUN_ROOT], sun == nullptr);
  lv_obj_update_layout(this->extra_common_[0]);
  bool complete = this->extra_text_(this->extra_common_[1], this->owner_.state().title.bytes.data());
  complete &= this->extra_text_(this->extra_common_[2], person != nullptr ? this->text_(64, "Person")
                                                                        : this->text_(65, "Sun"));
  if (extra.updated.view().empty())
    clear(this->extra_common_[3]);
  else
    complete &= this->info_context_date_(this->extra_common_[3], 0, extra.updated.view(), 79);
  if (person != nullptr) {
    const bool home = extra.available() && person->phase == 0;
    const auto bg = configured_style_color_(home ? this->on_button_ : this->off_button_, LV_STYLE_BG_COLOR);
    const auto fg = configured_style_color_(home ? this->minus_label_ : this->plus_label_, LV_STYLE_TEXT_COLOR);
    if (!lv_color_eq(lv_obj_get_style_bg_color(this->extra_person_[PERSON_SURFACE], LV_PART_MAIN), bg))
      lv_obj_set_style_bg_color(this->extra_person_[PERSON_SURFACE], bg, LV_PART_MAIN);
    if (!lv_color_eq(lv_obj_get_style_text_color(this->extra_person_[PERSON_ICON], LV_PART_MAIN), fg))
      lv_obj_set_style_text_color(this->extra_person_[PERSON_ICON], fg, LV_PART_MAIN);
    const char *state = person->status == 0 ? person->text.bytes.data()
                        : person->status == 1 ? this->text_(29, "Empty state")
                        : person->status == 2 ? this->text_(30, "Invalid state")
                        : person->status == 3 ? this->text_(31, "State too long")
                                              : this->text_(23, "Unknown");
    complete &= this->extra_text_(this->extra_person_[PERSON_STATE], state, this->extra_person_normal_,
                                  this->extra_person_compact_);
    complete &= this->info_context_date_(this->extra_person_[PERSON_CHANGED], person->changed_status,
                                         person->changed.view(), 68);
    complete &= this->extra_text_(this->extra_person_[PERSON_ZONE], person->zone.bytes.data());
  } else if (sun != nullptr) {
    const char *phase = sun->phase == 0 ? this->text_(72, "Above horizon")
                        : sun->phase == 1 ? this->text_(73, "Below horizon") : this->text_(23, "Unknown");
    complete &= this->extra_text_(this->extra_sun_[SUN_PHASE], phase);
    complete &= this->extra_text_(this->extra_sun_[RISING_CAPTION], this->text_(74, "Next sunrise"));
    complete &= this->extra_text_(this->extra_sun_[SETTING_CAPTION], this->text_(75, "Next sunset"));
    complete &= this->extra_text_(this->extra_sun_[ELEVATION_CAPTION], this->text_(76, "Elevation"));
    complete &= this->extra_text_(this->extra_sun_[AZIMUTH_CAPTION], this->text_(77, "Azimuth"));
    complete &= this->info_context_date_(this->extra_sun_[RISING_VALUE], sun->rising_status, sun->rising.view());
    complete &= this->info_context_date_(this->extra_sun_[SETTING_VALUE], sun->setting_status, sun->setting.view());
    const auto angle = [&](size_t index, uint8_t status, const char *value) {
      if (status != 0)
        return this->extra_text_(this->extra_sun_[index], status == 1 ? this->text_(23, "Unknown")
                                                        : status == 2 ? this->text_(27, "Invalid value")
                                                                      : this->text_(28, "Value too long"));
      char buffer[28];
      const int length = std::snprintf(buffer, sizeof(buffer), "%s°", value);
      const bool fits = length >= 0 && static_cast<size_t>(length) < sizeof(buffer);
      return this->extra_text_(this->extra_sun_[index], fits ? buffer : "--") && fits;
    };
    complete &= angle(ELEVATION_VALUE, sun->elevation_status, sun->elevation.bytes.data());
    complete &= angle(AZIMUTH_VALUE, sun->azimuth_status, sun->azimuth.bytes.data());
    complete &= this->extra_text_(this->extra_sun_[SUN_ZONE], sun->zone.bytes.data());
  }
  this->update_status_();
  const bool idle = this->status_ == ControlUiStatus::CONTROL_UI_STATUS_READY ||
                    this->status_ == ControlUiStatus::CONTROL_UI_STATUS_STATE_MATCHES ||
                    this->status_ == ControlUiStatus::CONTROL_UI_STATUS_ALREADY_MATCHED;
  const char *status = idle ? this->text_(80, "Information from Home Assistant")
                           : this->text_(static_cast<uint8_t>(this->status_) + 2, this->get_status_key());
  if (idle && !extra.available())
    status = extra.source == 1 ? this->text_(23, "Unknown") : this->text_(24, "Unavailable");
  if (idle && !complete)
    status = this->text_(32, "Full observation cannot be displayed");
  this->extra_text_(this->extra_common_[4], status);
}
}  // namespace esphome::lvgl_material
#endif
