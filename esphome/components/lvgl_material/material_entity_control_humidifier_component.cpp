#include "material_entity_control_component.h"
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace esphome::lvgl_material {
namespace {
enum HumidifierWidget : size_t {
  HUM_ROOT,
  HUM_TITLE,
  HUM_KIND,
  HUM_TARGET,
  HUM_CURRENT,
  HUM_STATE,
  HUM_STATUS,
  HUM_TRACK,
  HUM_FILL,
  HUM_KNOB,
  HUM_MINIMUM,
  HUM_MAXIMUM,
  HUM_MINUS,
  HUM_PLUS,
  HUM_POWER,
  HUM_POWER_LABEL,
  HUM_MODE,
  HUM_MODE_LABEL,
  HUM_MODES,
  HUM_BACK
};
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
  for (auto *parent = lv_obj_get_parent(widget); parent != nullptr; parent = lv_obj_get_parent(parent))
    if (parent == root)
      return true;
  return false;
}
bool glyphs_available(lv_obj_t *label, std::string_view text) {
  const auto *font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
  for (size_t offset = 0; offset < text.size();) {
    const auto first = static_cast<uint8_t>(text[offset++]);
    uint32_t point = first;
    size_t tail = 0;
    if (first >= 0xc2 && first <= 0xdf) {
      point &= 0x1f;
      tail = 1;
    } else if (first >= 0xe0 && first <= 0xef) {
      point &= 0xf;
      tail = 2;
    } else if (first >= 0xf0) {
      point &= 7;
      tail = 3;
    }
    for (size_t next = 0; next < tail; next++)
      point = (point << 6) | (static_cast<uint8_t>(text[offset++]) & 0x3f);
    lv_font_glyph_dsc_t glyph{};
    if (font == nullptr || !lv_font_get_glyph_dsc(font, &glyph, point, 0) || glyph.is_placeholder)
      return false;
  }
  return true;
}
}  // namespace

void MaterialEntityControlComponent::set_humidifier_profile(lv_obj_t *root, lv_obj_t *title, lv_obj_t *kind,
                                                            lv_obj_t *target, lv_obj_t *current, lv_obj_t *state,
                                                            lv_obj_t *status, lv_obj_t *track, lv_obj_t *fill,
                                                            lv_obj_t *knob, lv_obj_t *minimum, lv_obj_t *maximum,
                                                            lv_obj_t *minus, lv_obj_t *plus, lv_obj_t *power,
                                                            lv_obj_t *power_label, lv_obj_t *mode, lv_obj_t *mode_label,
                                                            lv_obj_t *mode_root, lv_obj_t *mode_back) {
  if (!this->setup_ready_)
    this->humidifier_widgets_ = {root,  title,       kind, target,     current,   state,    status,
                                 track, fill,        knob, minimum,    maximum,   minus,    plus,
                                 power, power_label, mode, mode_label, mode_root, mode_back};
}

void MaterialEntityControlComponent::set_humidifier_mode_option(size_t index, lv_obj_t *button, lv_obj_t *text) {
  if (this->setup_ready_ || index >= this->humidifier_mode_buttons_.size())
    return;
  this->humidifier_mode_buttons_[index] = button;
  this->humidifier_mode_labels_[index] = text;
}

bool MaterialEntityControlComponent::setup_humidifier_profile_() {
  auto &widgets = this->humidifier_widgets_;
  if (widgets[HUM_ROOT] == nullptr)
    return true;
  if (!this->number_profile_ || !within(this->root_, widgets[HUM_ROOT]))
    return false;
  for (size_t index = 0; index < widgets.size(); index++) {
    if (widgets[index] == nullptr || (index != HUM_ROOT && !within(widgets[HUM_ROOT], widgets[index])))
      return false;
    for (size_t prior = 0; prior < index; prior++)
      if (widgets[index] == widgets[prior])
        return false;
  }
  for (auto index : {HUM_TITLE, HUM_KIND, HUM_TARGET, HUM_CURRENT, HUM_STATE, HUM_STATUS, HUM_MINIMUM, HUM_MAXIMUM,
                     HUM_POWER_LABEL, HUM_MODE_LABEL})
    if (!lv_obj_check_type(widgets[index], &lv_label_class))
      return false;
  for (auto index : {HUM_MINUS, HUM_PLUS, HUM_POWER, HUM_MODE, HUM_BACK})
    if (!lv_obj_check_type(widgets[index], &lv_button_class))
      return false;
  if (lv_obj_get_parent(widgets[HUM_KNOB]) != widgets[HUM_TRACK] ||
      lv_obj_get_parent(widgets[HUM_FILL]) != widgets[HUM_TRACK] || !within(widgets[HUM_MODES], widgets[HUM_BACK]))
    return false;
  for (size_t index = 0; index < this->humidifier_mode_buttons_.size(); index++) {
    auto *button = this->humidifier_mode_buttons_[index];
    auto *text = this->humidifier_mode_labels_[index];
    if (button == nullptr || text == nullptr || !lv_obj_check_type(button, &lv_button_class) ||
        !lv_obj_check_type(text, &lv_label_class) || !within(widgets[HUM_MODES], button) || !within(button, text))
      return false;
    this->style_disabled_control_(button);
    lv_obj_add_event_cb(button, input_event_cb_, LV_EVENT_ALL, this);
  }
  for (auto index : {HUM_TRACK, HUM_MINUS, HUM_PLUS, HUM_POWER, HUM_MODE, HUM_BACK}) {
    if (index != HUM_TRACK)
      this->style_disabled_control_(widgets[index]);
    lv_obj_add_event_cb(widgets[index], input_event_cb_, LV_EVENT_ALL, this);
  }
  lv_obj_add_flag(widgets[HUM_TRACK], LV_OBJ_FLAG_PRESS_LOCK);
  hidden(widgets[HUM_ROOT], true);
  hidden(widgets[HUM_MODES], true);
  return this->owner_.set_humidifier_enabled(true);
}

bool MaterialEntityControlComponent::open_humidifier(uint32_t slot, std::string_view card_id, uint64_t revision) {
  return this->owner_.humidifier_enabled() &&
         this->open_entity_(slot, card_id, revision, ControlEntityKind::CONTROL_ENTITY_HUMIDIFIER);
}

bool MaterialEntityControlComponent::is_humidifier_track_(lv_obj_t *target) const {
  return target == this->humidifier_widgets_[HUM_TRACK];
}

bool MaterialEntityControlComponent::humidifier_glyphs_available_() const {
  const auto *selected_family = this->owner_.state().family_if<ControlSnapshot::Humidifier>();
  if (selected_family == nullptr)
    return false;
  const auto &humidity = *selected_family;
  for (size_t index = 0; index < humidity.caps.mode_count; index++) {
    const auto text = humidity.caps.modes[index].view();
    if (!glyphs_available(this->humidifier_mode_labels_[index], text) ||
        !glyphs_available(this->humidifier_widgets_[HUM_MODE_LABEL], text))
      return false;
  }
  return true;
}

void MaterialEntityControlComponent::restore_humidifier_widgets_() {
  if (!this->humidifier_widgets_active_)
    return;
  const std::array<lv_obj_t *, 8> widgets{this->hit_,        this->value_,       this->unknown_value_, this->on_button_,
                                          this->off_button_, this->state_label_, this->status_label_,  this->title_};
  for (size_t index = 0; index < widgets.size(); index++)
    hidden(widgets[index], (this->humidifier_hidden_mask_ & (1U << index)) != 0);
  hidden(this->humidifier_widgets_[HUM_ROOT], true);
  hidden(this->humidifier_widgets_[HUM_MODES], true);
  this->humidifier_widgets_active_ = this->humidifier_modes_visible_ = this->humidifier_pending_preview_ = false;
}

void MaterialEntityControlComponent::show_humidifier_target_(uint8_t index) {
  const auto *selected_family = this->owner_.state().family_if<ControlSnapshot::Humidifier>();
  if (selected_family == nullptr)
    return;
  const auto &humidity = *selected_family;
  auto &widgets = this->humidifier_widgets_;
  int64_t value;
  char numeric[24], text[96];
  const bool known = this->owner_.has_selection() && index != ControlSnapshot::Humidifier::UNKNOWN &&
                     humidity.domain.value_at(index, value) && number_format_decimal(value, numeric, sizeof(numeric));
  std::snprintf(text, sizeof(text), "%s: %s%%", this->text_(20, "Target"), known ? numeric : "--");
  label(widgets[HUM_TARGET], text);
  const int32_t travel =
      std::max(int32_t{0}, lv_obj_get_width(widgets[HUM_TRACK]) - lv_obj_get_width(widgets[HUM_KNOB]));
  const int32_t position = known && humidity.domain.max_index != 0
                               ? (index * travel + humidity.domain.max_index / 2) / humidity.domain.max_index
                               : 0;
  if (lv_obj_get_x(widgets[HUM_KNOB]) != position)
    lv_obj_set_x(widgets[HUM_KNOB], position);
  if (lv_obj_get_width(widgets[HUM_FILL]) != position)
    lv_obj_set_width(widgets[HUM_FILL], position);
  hidden(widgets[HUM_KNOB], !known);
}

void MaterialEntityControlComponent::update_humidifier_static_() {
  auto &widgets = this->humidifier_widgets_;
  const std::array<lv_obj_t *, 8> old{this->hit_,        this->value_,       this->unknown_value_, this->on_button_,
                                      this->off_button_, this->state_label_, this->status_label_,  this->title_};
  if (!this->humidifier_widgets_active_) {
    this->humidifier_hidden_mask_ = 0;
    for (size_t index = 0; index < old.size(); index++)
      if (old[index] != nullptr && lv_obj_has_flag(old[index], LV_OBJ_FLAG_HIDDEN))
        this->humidifier_hidden_mask_ |= 1U << index;
    this->humidifier_widgets_active_ = true;
    this->humidifier_modes_visible_ = this->humidifier_pending_preview_ = false;
  }
  for (auto *widget : old)
    hidden(widget, true);
  hidden(widgets[HUM_ROOT], false);
  hidden(widgets[HUM_MODES], !this->humidifier_modes_visible_);
  const auto *selected_family = this->owner_.state().family_if<ControlSnapshot::Humidifier>();
  if (selected_family == nullptr)
    return;
  const auto &humidity = *selected_family;
  const bool selected = this->owner_.has_selection();
  const bool ready = selected && this->owner_.is_view_ready() && !this->owner_.is_command_pending();
  const bool glyphs = selected && this->humidifier_glyphs_available_();
  if (selected)
    label(widgets[HUM_TITLE], this->owner_.state().title.bytes.data());
  label(widgets[HUM_KIND], humidity.device_class == 0   ? this->text_(17, "Humidifier")
                           : humidity.device_class == 1 ? this->text_(18, "Dehumidifier")
                                                        : this->text_(19, "Humidity"));
  label(widgets[HUM_STATE], selected && humidity.available
                                ? humidity.power == 1 ? this->text_(14, "On") : this->text_(15, "Off")
                                : this->text_(16, "Unknown"));
  char text[128], numeric[24];
  std::snprintf(text, sizeof(text), "%s: %s%%", this->text_(21, "Current"),
                selected && !humidity.current.view().empty() ? humidity.current.bytes.data() : "--");
  label(widgets[HUM_CURRENT], text);
  if (!this->owner_.is_command_pending())
    this->humidifier_pending_preview_ = false;
  this->show_humidifier_target_(this->humidifier_pending_preview_ ? this->humidifier_preview_index_ : humidity.index);
  const bool adjustable =
      humidity.domain.max_index != 0 &&
      this->owner_.can_humidifier(ControlOperation::CONTROL_OPERATION_HUMIDIFIER_TARGET, humidity.index, true);
  enabled(widgets[HUM_TRACK], adjustable);
  enabled(widgets[HUM_MINUS], adjustable && humidity.index != 0);
  enabled(widgets[HUM_PLUS], adjustable && humidity.index < humidity.domain.max_index);
  for (bool maximum : {false, true}) {
    const bool valid =
        humidity.domain.valid() &&
        number_format_decimal(maximum ? humidity.domain.maximum : humidity.domain.minimum, numeric, sizeof(numeric));
    std::snprintf(text, sizeof(text), "%s%%", valid ? numeric : "--");
    label(widgets[maximum ? HUM_MAXIMUM : HUM_MINIMUM], text);
  }
  const auto active_bg = lv_obj_get_style_bg_color(this->on_button_, LV_PART_MAIN);
  const auto active_fg = lv_obj_get_style_text_color(this->minus_label_, LV_PART_MAIN);
  const auto neutral_bg = lv_obj_get_style_bg_color(this->off_button_, LV_PART_MAIN);
  const auto neutral_fg = lv_obj_get_style_text_color(this->plus_label_, LV_PART_MAIN);
  const auto color = [&](lv_obj_t *button, lv_obj_t *text, bool active) {
    active &= !lv_obj_has_state(button, LV_STATE_DISABLED);
    const auto background = active ? active_bg : neutral_bg;
    const auto foreground = active ? active_fg : neutral_fg;
    if (!lv_color_eq(lv_obj_get_style_bg_color(button, LV_PART_MAIN), background))
      lv_obj_set_style_bg_color(button, background, LV_PART_MAIN);
    if (!lv_color_eq(lv_obj_get_style_text_color(text, LV_PART_MAIN), foreground))
      lv_obj_set_style_text_color(text, foreground, LV_PART_MAIN);
  };
  label(widgets[HUM_POWER_LABEL], humidity.power == 1 ? this->text_(1, "Turn off") : this->text_(0, "Turn on"));
  enabled(widgets[HUM_POWER], this->owner_.can_humidifier(ControlOperation::CONTROL_OPERATION_HUMIDIFIER_POWER,
                                                          humidity.power == 1 ? 0 : 1));
  color(widgets[HUM_POWER], widgets[HUM_POWER_LABEL], humidity.power == 1);
  hidden(widgets[HUM_MODE], humidity.caps.mode_count == 0);
  const char *mode = glyphs && humidity.mode_index < humidity.caps.mode_count
                         ? humidity.caps.modes[humidity.mode_index].bytes.data()
                         : this->text_(16, "Unknown");
  std::snprintf(text, sizeof(text), "%s: %s", this->text_(22, "Mode"), mode);
  label(widgets[HUM_MODE_LABEL], text);
  enabled(widgets[HUM_MODE], ready && glyphs && humidity.available && humidity.mode_index < humidity.caps.mode_count);
  enabled(widgets[HUM_BACK], ready);
  for (size_t index = 0; index < this->humidifier_mode_buttons_.size(); index++) {
    auto *button = this->humidifier_mode_buttons_[index];
    auto *mode_label = this->humidifier_mode_labels_[index];
    hidden(button, !selected || index >= humidity.caps.mode_count);
    if (index < humidity.caps.mode_count) {
      label(mode_label, glyphs ? humidity.caps.modes[index].bytes.data() : this->text_(25, "Unsupported"));
      enabled(button,
              glyphs && this->owner_.can_humidifier(ControlOperation::CONTROL_OPERATION_HUMIDIFIER_MODE, index, true));
      color(button, mode_label, humidity.mode_index == index);
    }
  }
  this->update_status_();
  const auto status = static_cast<uint8_t>(this->status_);
  const char *fallback =
      this->status_texts_[status].empty() ? this->get_status_key() : this->status_texts_[status].c_str();
  label(widgets[HUM_STATUS], this->text_(status + 2, fallback));
  this->texts_pending_ = false;
}

bool MaterialEntityControlComponent::begin_humidifier_input_(lv_obj_t *target, lv_indev_t *indev, uint32_t pointer,
                                                             uint32_t now) {
  const auto *selected_family = this->owner_.state().family_if<ControlSnapshot::Humidifier>();
  if (selected_family == nullptr)
    return false;
  const auto &humidity = *selected_family;
  const auto &widgets = this->humidifier_widgets_;
  this->humidifier_pending_preview_ = false;
  if (target == widgets[HUM_MODE] || target == widgets[HUM_BACK]) {
    if (target == widgets[HUM_MODE] && (!humidity.available || humidity.mode_index >= humidity.caps.mode_count ||
                                        !this->humidifier_glyphs_available_()))
      return false;
    return this->owner_.begin_humidifier_navigation(pointer, now);
  }
  if (target == widgets[HUM_TRACK]) {
    if (humidity.domain.max_index == 0 ||
        !this->owner_.begin_humidifier(pointer, ControlOperation::CONTROL_OPERATION_HUMIDIFIER_TARGET, humidity.index,
                                       now))
      return false;
    this->humidifier_preview_index_ = humidity.index;
    this->update_humidifier_input_(indev, pointer, now);
    return true;
  }
  if (target == widgets[HUM_MINUS] || target == widgets[HUM_PLUS]) {
    const bool minus = target == widgets[HUM_MINUS];
    if (humidity.index == ControlSnapshot::Humidifier::UNKNOWN ||
        (minus ? humidity.index == 0 : humidity.index >= humidity.domain.max_index))
      return false;
    this->humidifier_preview_index_ = humidity.index + (minus ? -1 : 1);
    return this->owner_.begin_humidifier(pointer, ControlOperation::CONTROL_OPERATION_HUMIDIFIER_TARGET,
                                         this->humidifier_preview_index_, now);
  }
  if (target == widgets[HUM_POWER])
    return this->owner_.begin_humidifier(pointer, ControlOperation::CONTROL_OPERATION_HUMIDIFIER_POWER,
                                         humidity.power == 1 ? 0 : 1, now);
  for (size_t index = 0; index < this->humidifier_mode_buttons_.size(); index++)
    if (target == this->humidifier_mode_buttons_[index])
      return this->humidifier_glyphs_available_() &&
             this->owner_.begin_humidifier(pointer, ControlOperation::CONTROL_OPERATION_HUMIDIFIER_MODE, index, now);
  return false;
}

void MaterialEntityControlComponent::update_humidifier_input_(lv_indev_t *indev, uint32_t pointer, uint32_t now) {
  const auto *humidity = this->owner_.state().family_if<ControlSnapshot::Humidifier>();
  if (humidity == nullptr)
    return;
  const auto &widgets = this->humidifier_widgets_;
  lv_area_t bounds;
  lv_point_t point;
  lv_obj_get_coords(widgets[HUM_TRACK], &bounds);
  lv_indev_get_point(indev, &point);
  const int32_t knob = lv_obj_get_width(widgets[HUM_KNOB]);
  const int32_t travel = lv_area_get_width(&bounds) - knob;
  if (travel <= 0)
    return;
  const uint32_t offset = std::clamp(point.x - bounds.x1 - knob / 2, int32_t{0}, travel);
  const uint8_t index = (offset * humidity->domain.max_index + travel / 2) / travel;
  if (this->owner_.update_humidifier_target(pointer, index, now) && index != this->humidifier_preview_index_) {
    this->humidifier_preview_index_ = index;
    this->show_humidifier_target_(index);
  }
}

void MaterialEntityControlComponent::release_humidifier_input_(lv_obj_t *target, uint32_t pointer, uint32_t now) {
  const auto &widgets = this->humidifier_widgets_;
  if (target == widgets[HUM_MODE] || target == widgets[HUM_BACK]) {
    if (this->owner_.release_humidifier_navigation(pointer, now)) {
      this->humidifier_modes_visible_ = target == widgets[HUM_MODE];
      this->update_static_();
    }
  } else {
    const bool accepted = this->owner_.release_humidifier(pointer, now);
    this->humidifier_pending_preview_ =
        accepted && (target == widgets[HUM_TRACK] || target == widgets[HUM_MINUS] || target == widgets[HUM_PLUS]);
    if (accepted)
      this->humidifier_modes_visible_ = false;
    this->update_static_();
  }
}

}  // namespace esphome::lvgl_material
#endif
