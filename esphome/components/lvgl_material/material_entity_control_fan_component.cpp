#include "material_entity_control_component.h"

#ifdef USE_LVGL_MATERIAL_FAN_CONTROLS

#include <cstdio>
#include <cstring>

namespace esphome::lvgl_material {
namespace {
enum FanWidget : size_t {
  FAN_ROOT,
  FAN_TITLE,
  FAN_VALUE,
  FAN_STATE,
  FAN_STATUS,
  FAN_POWER_BUTTON,
  FAN_POWER_LABEL,
  FAN_OSCILLATION_BUTTON,
  FAN_OSCILLATION_LABEL,
  FAN_DIRECTION_BUTTON,
  FAN_DIRECTION_LABEL,
  FAN_PRESET_BUTTON,
  FAN_PRESET_LABEL,
  FAN_PRESET_ROOT,
  FAN_PRESET_BACK
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

void MaterialEntityControlComponent::set_fan_profile(lv_obj_t *root, lv_obj_t *title, lv_obj_t *value, lv_obj_t *state,
                                                     lv_obj_t *status, lv_obj_t *power_button, lv_obj_t *power_label,
                                                     lv_obj_t *oscillation_button, lv_obj_t *oscillation_label,
                                                     lv_obj_t *direction_button, lv_obj_t *direction_label,
                                                     lv_obj_t *preset_button, lv_obj_t *preset_label,
                                                     lv_obj_t *preset_root, lv_obj_t *preset_back_button) {
  if (!this->setup_ready_)
    this->fan_widgets_ = {root,
                          title,
                          value,
                          state,
                          status,
                          power_button,
                          power_label,
                          oscillation_button,
                          oscillation_label,
                          direction_button,
                          direction_label,
                          preset_button,
                          preset_label,
                          preset_root,
                          preset_back_button};
}

void MaterialEntityControlComponent::set_fan_speed_button(size_t index, lv_obj_t *button, lv_obj_t *text) {
  if (this->setup_ready_ || index >= this->fan_speed_buttons_.size())
    return;
  this->fan_speed_buttons_[index] = button;
  this->fan_speed_labels_[index] = text;
}

void MaterialEntityControlComponent::set_fan_preset_option(size_t index, lv_obj_t *button, lv_obj_t *text) {
  if (this->setup_ready_ || index >= this->fan_preset_buttons_.size())
    return;
  this->fan_preset_buttons_[index] = button;
  this->fan_preset_labels_[index] = text;
}

bool MaterialEntityControlComponent::setup_fan_profile_() {
  auto &widgets = this->fan_widgets_;
  if (widgets[FAN_ROOT] == nullptr)
    return true;
  if (!this->number_profile_ || !within(this->root_, widgets[FAN_ROOT]))
    return false;
  for (size_t index = 0; index < widgets.size(); index++) {
    if (widgets[index] == nullptr || (index != FAN_ROOT && !within(widgets[FAN_ROOT], widgets[index])))
      return false;
    for (size_t prior = 0; prior < index; prior++)
      if (widgets[index] == widgets[prior])
        return false;
  }
  for (auto index : {FAN_TITLE, FAN_VALUE, FAN_STATE, FAN_STATUS, FAN_POWER_LABEL, FAN_OSCILLATION_LABEL,
                     FAN_DIRECTION_LABEL, FAN_PRESET_LABEL})
    if (!lv_obj_check_type(widgets[index], &lv_label_class))
      return false;
  for (auto index :
       {FAN_POWER_BUTTON, FAN_OSCILLATION_BUTTON, FAN_DIRECTION_BUTTON, FAN_PRESET_BUTTON, FAN_PRESET_BACK})
    if (!lv_obj_check_type(widgets[index], &lv_button_class))
      return false;
  if (!within(widgets[FAN_PRESET_ROOT], widgets[FAN_PRESET_BACK]))
    return false;
  if (this->fan_speed_caption_ != nullptr && (!lv_obj_check_type(this->fan_speed_caption_, &lv_label_class) ||
                                              !within(widgets[FAN_ROOT], this->fan_speed_caption_)))
    return false;
  for (size_t index = 0; index < this->fan_speed_buttons_.size(); index++) {
    for (const bool preset : {false, true}) {
      auto *button = preset ? this->fan_preset_buttons_[index] : this->fan_speed_buttons_[index];
      auto *text = preset ? this->fan_preset_labels_[index] : this->fan_speed_labels_[index];
      if (button == nullptr || text == nullptr || !lv_obj_check_type(button, &lv_button_class) ||
          !lv_obj_check_type(text, &lv_label_class) || !within(widgets[preset ? FAN_PRESET_ROOT : FAN_ROOT], button) ||
          !within(button, text))
        return false;
    }
  }
  for (auto index :
       {FAN_POWER_BUTTON, FAN_OSCILLATION_BUTTON, FAN_DIRECTION_BUTTON, FAN_PRESET_BUTTON, FAN_PRESET_BACK}) {
    this->style_disabled_control_(widgets[index]);
    lv_obj_add_event_cb(widgets[index], input_event_cb_, LV_EVENT_ALL, this);
  }
  for (auto *button : this->fan_speed_buttons_) {
    this->style_disabled_control_(button);
    lv_obj_add_event_cb(button, input_event_cb_, LV_EVENT_ALL, this);
  }
  for (auto *button : this->fan_preset_buttons_) {
    this->style_disabled_control_(button);
    lv_obj_add_event_cb(button, input_event_cb_, LV_EVENT_ALL, this);
  }
  hidden(widgets[FAN_ROOT], true);
  hidden(widgets[FAN_PRESET_ROOT], true);
  return this->owner_.set_fan_enabled(true);
}

bool MaterialEntityControlComponent::open_fan(uint32_t slot, std::string_view card_id, uint64_t revision) {
  return this->owner_.fan_enabled() &&
         this->open_entity_(slot, card_id, revision, ControlEntityKind::CONTROL_ENTITY_FAN);
}

bool MaterialEntityControlComponent::fan_glyphs_available_() const {
  const auto *selected_family = this->owner_.state().family_if<ControlSnapshot::Fan>();
  if (selected_family == nullptr)
    return false;
  const auto &fan = *selected_family;
  for (size_t index = 0; index < fan.caps.preset_count; index++) {
    const auto text = fan.caps.presets[index].view();
    if (!glyphs_available(this->fan_preset_labels_[index], text) ||
        !glyphs_available(this->fan_widgets_[FAN_PRESET_LABEL], text) ||
        (fan.caps.speed_count == 0 && !glyphs_available(this->fan_widgets_[FAN_VALUE], text)))
      return false;
  }
  return true;
}

void MaterialEntityControlComponent::restore_fan_widgets_() {
  if (!this->fan_widgets_active_)
    return;
  const std::array<lv_obj_t *, 8> widgets{this->hit_,        this->value_,       this->unknown_value_, this->on_button_,
                                          this->off_button_, this->state_label_, this->status_label_,  this->title_};
  for (size_t index = 0; index < widgets.size(); index++)
    hidden(widgets[index], (this->fan_hidden_mask_ & (1U << index)) != 0);
  hidden(this->fan_widgets_[FAN_ROOT], true);
  hidden(this->fan_widgets_[FAN_PRESET_ROOT], true);
  this->fan_widgets_active_ = this->fan_presets_visible_ = false;
}

void MaterialEntityControlComponent::update_fan_static_() {
  auto &widgets = this->fan_widgets_;
  const std::array<lv_obj_t *, 8> old{this->hit_,        this->value_,       this->unknown_value_, this->on_button_,
                                      this->off_button_, this->state_label_, this->status_label_,  this->title_};
  if (!this->fan_widgets_active_) {
    this->fan_hidden_mask_ = 0;
    for (size_t index = 0; index < old.size(); index++)
      if (old[index] != nullptr && lv_obj_has_flag(old[index], LV_OBJ_FLAG_HIDDEN))
        this->fan_hidden_mask_ |= 1U << index;
    this->fan_widgets_active_ = true;
    this->fan_presets_visible_ = false;
  }
  for (auto *widget : old)
    hidden(widget, true);
  hidden(widgets[FAN_ROOT], false);
  hidden(widgets[FAN_PRESET_ROOT], !this->fan_presets_visible_);
  const auto *selected_family = this->owner_.state().family_if<ControlSnapshot::Fan>();
  if (selected_family == nullptr)
    return;
  const auto &fan = *selected_family;
  const bool selected = this->owner_.has_selection();
  const bool ready = selected && this->owner_.is_view_ready() && !this->owner_.is_command_pending();
  const bool glyphs = selected && this->fan_glyphs_available_();
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
  if (selected)
    label(widgets[FAN_TITLE], this->owner_.state().title.bytes.data());
  const char *state = this->text_(16, "Unknown");
  if (selected && fan.available)
    state = fan.power == 1 ? this->text_(14, "On") : this->text_(15, "Off");
  label(widgets[FAN_STATE], state);
  char value[192];
  hidden(this->fan_speed_caption_, !selected || fan.caps.speed_count == 0);
  if (selected && fan.caps.speed_count != 0) {
    if (fan.percentage != ControlSnapshot::Fan::UNKNOWN)
      std::snprintf(value, sizeof(value), "%u%%", static_cast<unsigned>(fan.percentage));
    else
      std::snprintf(value, sizeof(value), "--");
  } else {
    const char *hero = state;
    if (selected && fan.caps.preset_count != 0) {
      hero = this->text_(16, "Unknown");
      if (fan.available && fan.preset_known && glyphs)
        hero = fan.preset_index == ControlSnapshot::Fan::UNKNOWN ? this->text_(17, "Manual")
                                                                 : fan.caps.presets[fan.preset_index].bytes.data();
    }
    std::snprintf(value, sizeof(value), "%s", hero);
  }
  label(widgets[FAN_VALUE], value);
  label(widgets[FAN_POWER_LABEL], fan.power == 1 ? this->text_(1, "Turn off") : this->text_(0, "Turn on"));
  enabled(widgets[FAN_POWER_BUTTON],
          selected && this->owner_.can_fan(ControlOperation::CONTROL_OPERATION_FAN_POWER, fan.power == 1 ? 0 : 1));
  color(widgets[FAN_POWER_BUTTON], widgets[FAN_POWER_LABEL], fan.power == 1);
  hidden(widgets[FAN_OSCILLATION_BUTTON], !fan.caps.oscillate);
  label(widgets[FAN_OSCILLATION_LABEL], fan.oscillating == 1   ? this->text_(20, "Oscillation on")
                                        : fan.oscillating == 0 ? this->text_(21, "Oscillation off")
                                                               : this->text_(26, "Oscillation"));
  enabled(widgets[FAN_OSCILLATION_BUTTON],
          selected &&
              this->owner_.can_fan(ControlOperation::CONTROL_OPERATION_FAN_OSCILLATION, fan.oscillating == 1 ? 0 : 1));
  color(widgets[FAN_OSCILLATION_BUTTON], widgets[FAN_OSCILLATION_LABEL], fan.oscillating == 1);
  hidden(widgets[FAN_DIRECTION_BUTTON], !fan.caps.direction);
  label(widgets[FAN_DIRECTION_LABEL], fan.direction == 0   ? this->text_(18, "Forward")
                                      : fan.direction == 1 ? this->text_(19, "Reverse")
                                                           : this->text_(27, "Direction"));
  enabled(
      widgets[FAN_DIRECTION_BUTTON],
      selected && this->owner_.can_fan(ControlOperation::CONTROL_OPERATION_FAN_DIRECTION, fan.direction == 0 ? 1 : 0));
  color(widgets[FAN_DIRECTION_BUTTON], widgets[FAN_DIRECTION_LABEL], fan.direction == 1);
  hidden(widgets[FAN_PRESET_BUTTON], fan.caps.preset_count == 0);
  const char *preset = this->text_(16, "Unknown");
  if (fan.preset_known && glyphs)
    preset = fan.preset_index == ControlSnapshot::Fan::UNKNOWN ? this->text_(17, "Manual")
                                                               : fan.caps.presets[fan.preset_index].bytes.data();
  std::snprintf(value, sizeof(value), "%s: %s", this->text_(22, "Mode"), preset);
  label(widgets[FAN_PRESET_LABEL], value);
  enabled(widgets[FAN_PRESET_BUTTON],
          ready && fan.available && fan.preset_known && glyphs && fan.caps.preset_count != 0);
  enabled(widgets[FAN_PRESET_BACK], ready);
  for (size_t index = 0; index < this->fan_speed_buttons_.size(); index++) {
    auto *speed = this->fan_speed_buttons_[index];
    hidden(speed, !selected || index >= fan.caps.speed_count);
    if (selected && index < fan.caps.speed_count) {
      std::snprintf(value, sizeof(value), "%u%%", static_cast<unsigned>(fan.caps.speeds[index]));
      label(this->fan_speed_labels_[index], value);
      enabled(speed,
              (ready && fan.available && fan.percentage == fan.caps.speeds[index]) ||
                  this->owner_.can_fan(ControlOperation::CONTROL_OPERATION_FAN_SPEED, static_cast<uint8_t>(index)));
      color(speed, this->fan_speed_labels_[index], fan.percentage == fan.caps.speeds[index]);
    }
    auto *option = this->fan_preset_buttons_[index];
    hidden(option, !selected || index >= fan.caps.preset_count);
    if (selected && index < fan.caps.preset_count) {
      label(this->fan_preset_labels_[index],
            glyphs ? fan.caps.presets[index].bytes.data() : this->text_(25, "Unsupported"));
      enabled(option, glyphs && ((ready && fan.available && fan.preset_known && fan.preset_index == index) ||
                                 this->owner_.can_fan(ControlOperation::CONTROL_OPERATION_FAN_PRESET,
                                                      static_cast<uint8_t>(index))));
      color(option, this->fan_preset_labels_[index], fan.preset_known && fan.preset_index == index);
    }
  }
  this->update_status_();
  const auto status = static_cast<uint8_t>(this->status_);
  const char *fallback =
      this->status_texts_[status].empty() ? this->get_status_key() : this->status_texts_[status].c_str();
  label(widgets[FAN_STATUS], this->text_(status + 2, fallback));
  this->texts_pending_ = false;
}

bool MaterialEntityControlComponent::begin_fan_input_(lv_obj_t *target, uint32_t pointer, uint32_t now) {
  const auto *selected_family = this->owner_.state().family_if<ControlSnapshot::Fan>();
  if (selected_family == nullptr)
    return false;
  const auto &fan = *selected_family;
  if (target == this->fan_widgets_[FAN_PRESET_BUTTON] || target == this->fan_widgets_[FAN_PRESET_BACK]) {
    if (target == this->fan_widgets_[FAN_PRESET_BUTTON] &&
        (!fan.available || !fan.preset_known || fan.caps.preset_count == 0 || !this->fan_glyphs_available_()))
      return false;
    return this->owner_.begin_fan_navigation(pointer, now);
  }
  ControlOperation operation;
  uint8_t value;
  if (target == this->fan_widgets_[FAN_POWER_BUTTON]) {
    operation = ControlOperation::CONTROL_OPERATION_FAN_POWER;
    value = fan.power == 1 ? 0 : 1;
  } else if (target == this->fan_widgets_[FAN_OSCILLATION_BUTTON]) {
    operation = ControlOperation::CONTROL_OPERATION_FAN_OSCILLATION;
    value = fan.oscillating == 1 ? 0 : 1;
  } else if (target == this->fan_widgets_[FAN_DIRECTION_BUTTON]) {
    operation = ControlOperation::CONTROL_OPERATION_FAN_DIRECTION;
    value = fan.direction == 0 ? 1 : 0;
  } else {
    for (size_t index = 0; index < this->fan_speed_buttons_.size(); index++) {
      if (target == this->fan_speed_buttons_[index])
        return this->owner_.begin_fan(pointer, ControlOperation::CONTROL_OPERATION_FAN_SPEED, index, now);
      if (target == this->fan_preset_buttons_[index])
        return this->fan_glyphs_available_() &&
               this->owner_.begin_fan(pointer, ControlOperation::CONTROL_OPERATION_FAN_PRESET, index, now);
    }
    return false;
  }
  return this->owner_.begin_fan(pointer, operation, value, now);
}

void MaterialEntityControlComponent::release_fan_input_(lv_obj_t *target, uint32_t pointer, uint32_t now) {
  if (target == this->fan_widgets_[FAN_PRESET_BUTTON] || target == this->fan_widgets_[FAN_PRESET_BACK]) {
    if (this->owner_.release_fan_navigation(pointer, now)) {
      this->fan_presets_visible_ = target == this->fan_widgets_[FAN_PRESET_BUTTON];
      this->update_static_();
    }
  } else if (this->owner_.release_fan(pointer, now)) {
    this->fan_presets_visible_ = false;
  }
}

}  // namespace esphome::lvgl_material

#endif
