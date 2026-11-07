#include "material_entity_control_component.h"
#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
#include "esphome/core/log.h"
#include <cstdio>
#include <cstring>
#include <limits>

namespace esphome::lvgl_material {
namespace {
using Snapshot = ControlDatetimeSnapshot;
using Input = ControlDatetimeInput;
using Mode = ControlDatetimeMode;
enum Common : size_t { ROOT, TITLE, KIND, SUMMARY, ZONE, UPDATED, STATUS, FIELDS, EDITOR, FIELD_NAME, DIGITS };
void hidden(lv_obj_t *object, bool value) {
  if (object == nullptr || lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN) == value)
    return;
  if (value)
    lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
}
void enabled(lv_obj_t *object, bool value) {
  if (lv_obj_has_state(object, LV_STATE_DISABLED) == !value)
    return;
  if (value)
    lv_obj_remove_state(object, LV_STATE_DISABLED);
  else
    lv_obj_add_state(object, LV_STATE_DISABLED);
}
bool within(lv_obj_t *root, lv_obj_t *object) {
  for (; object != nullptr; object = lv_obj_get_parent(object))
    if (object == root)
      return true;
  return false;
}
bool label(lv_obj_t *root, lv_obj_t *object) {
  return within(root, object) && lv_obj_check_type(object, &lv_label_class);
}
Input control_input(size_t index) {
  constexpr Input INPUTS[] = {Input::CONTROL_DATETIME_INPUT_STEP_DOWN, Input::CONTROL_DATETIME_INPUT_STEP_UP,
                              Input::CONTROL_DATETIME_INPUT_CANCEL, Input::CONTROL_DATETIME_INPUT_SAVE};
  return INPUTS[index];
}
Input key_input(size_t index) {
  return index < 10    ? Input::CONTROL_DATETIME_INPUT_DIGIT
         : index == 10 ? Input::CONTROL_DATETIME_INPUT_BACKSPACE
                       : Input::CONTROL_DATETIME_INPUT_ACCEPT_FIELD;
}
}  // namespace

void MaterialEntityControlComponent::set_datetime_profile(lv_obj_t *root, lv_obj_t *title, lv_obj_t *kind,
                                                          lv_obj_t *summary, lv_obj_t *zone, lv_obj_t *updated,
                                                          lv_obj_t *status, lv_obj_t *fields, lv_obj_t *editor,
                                                          lv_obj_t *field, lv_obj_t *digits) {
  if (!this->setup_ready_)
    this->datetime_common_ = {root, title, kind, summary, zone, updated, status, fields, editor, field, digits};
}
void MaterialEntityControlComponent::set_datetime_icon(size_t index, lv_obj_t *object) {
  if (!this->setup_ready_ && index < this->datetime_icons_.size())
    this->datetime_icons_[index] = object;
}
void MaterialEntityControlComponent::set_datetime_field(size_t index, lv_obj_t *button, lv_obj_t *caption,
                                                        lv_obj_t *value) {
  if (!this->setup_ready_ && index < this->datetime_fields_.size()) {
    this->datetime_fields_[index] = button;
    this->datetime_captions_[index] = caption;
    this->datetime_values_[index] = value;
  }
}
void MaterialEntityControlComponent::set_datetime_key(size_t index, lv_obj_t *button) {
  if (!this->setup_ready_ && index < this->datetime_keys_.size())
    this->datetime_keys_[index] = button;
}
void MaterialEntityControlComponent::set_datetime_control(size_t index, lv_obj_t *button) {
  if (!this->setup_ready_ && index < this->datetime_controls_.size())
    this->datetime_controls_[index] = button;
}
void MaterialEntityControlComponent::set_datetime_labels(lv_obj_t *cancel, lv_obj_t *save) {
  if (!this->setup_ready_)
    this->datetime_labels_ = {cancel, save};
}
void MaterialEntityControlComponent::set_datetime_field_layout(size_t index, int16_t x, int16_t y, int16_t height,
                                                               int16_t field_height) {
  if (!this->setup_ready_ && index < this->datetime_layouts_.size())
    this->datetime_layouts_[index] = {x, y, height, field_height};
}
void MaterialEntityControlComponent::set_datetime_enabled(bool value) {
  if (!this->setup_ready_)
    this->datetime_enabled_ = value;
}

bool MaterialEntityControlComponent::setup_datetime_profile_() {
  auto *root = this->datetime_common_[ROOT];
  if (!root)
    return !this->datetime_enabled_;
  if (!this->number_profile_ || lv_obj_get_parent(root) != this->root_)
    return false;
  for (size_t index = 1; index < this->datetime_common_.size(); index++) {
    if (!within(root, this->datetime_common_[index]))
      return false;
    if (index != FIELDS && index != EDITOR && !label(root, this->datetime_common_[index]))
      return false;
  }
  for (auto *icon : this->datetime_icons_)
    if (!within(root, icon))
      return false;
  for (const auto &layout : this->datetime_layouts_)
    if (layout.x < 0 || layout.y < 0 || layout.field_height <= 0 || layout.height < 2 * layout.field_height)
      return false;
  for (size_t index = 0; index < this->datetime_fields_.size(); index++) {
    auto *button = this->datetime_fields_[index];
    if (button == nullptr || lv_obj_get_parent(button) != this->datetime_common_[FIELDS] ||
        !label(button, this->datetime_captions_[index]) || !label(button, this->datetime_values_[index]))
      return false;
  }
  for (size_t index = 0; index < this->datetime_labels_.size(); index++)
    if (!label(this->datetime_controls_[index + 2], this->datetime_labels_[index]))
      return false;
  const auto bind = [this, root](auto &buttons) {
    for (auto *button : buttons) {
      if (!within(root, button))
        return false;
      lv_obj_add_event_cb(button, input_event_cb_, LV_EVENT_ALL, this);
      this->style_disabled_control_(button);
    }
    return true;
  };
  if (!bind(this->datetime_fields_) || !bind(this->datetime_keys_) || !bind(this->datetime_controls_))
    return false;
  hidden(root, true);
  if (this->datetime_enabled_)
    this->extra_configured_ |= 64U;
  return this->owner_.set_extra_profiles(this->extra_configured_);
}

void MaterialEntityControlComponent::restore_datetime_widgets_() {
  if (!this->datetime_widgets_active_)
    return;
  this->datetime_ready_ = false;
  if (!this->owner_.has_selection()) {
    for (auto *button : this->datetime_fields_)
      enabled(button, false);
    for (auto *button : this->datetime_keys_)
      enabled(button, false);
    for (auto *button : this->datetime_controls_)
      enabled(button, false);
    for (auto *value : this->datetime_values_)
      this->extra_text_(value, "--");
    hidden(this->datetime_common_[EDITOR], true);
    hidden(this->datetime_common_[FIELDS], true);
    hidden(this->datetime_common_[SUMMARY], false);
    this->extra_text_(this->datetime_common_[SUMMARY], "--");
    this->extra_text_(this->datetime_common_[ZONE], "");
    this->extra_text_(this->datetime_common_[UPDATED], "");
    this->extra_text_(this->datetime_common_[STATUS], this->text_(15, "Unknown"));
    return;
  }
  lv_obj_t *base[] = {this->title_,     this->hit_,        this->value_,       this->unknown_value_,
                      this->on_button_, this->off_button_, this->state_label_, this->status_label_};
  for (size_t index = 0; index < std::size(base); index++)
    hidden(base[index], (this->datetime_hidden_mask_ & (1U << index)) != 0);
  hidden(this->datetime_common_[ROOT], true);
  this->datetime_widgets_active_ = false;
}

void MaterialEntityControlComponent::update_datetime_static_() {
  const auto *state = this->owner_.state().family_if<Snapshot>();
  if (!this->owner_.is_datetime() || state == nullptr || this->owner_.is_dragging())
    return;
  lv_obj_t *base[] = {this->title_,     this->hit_,        this->value_,       this->unknown_value_,
                      this->on_button_, this->off_button_, this->state_label_, this->status_label_};
  if (!this->datetime_widgets_active_) {
    this->datetime_hidden_mask_ = 0;
    for (size_t index = 0; index < std::size(base); index++)
      if (base[index] && lv_obj_has_flag(base[index], LV_OBJ_FLAG_HIDDEN))
        this->datetime_hidden_mask_ |= 1U << index;
    this->datetime_widgets_active_ = true;
  }
  for (auto *object : base)
    hidden(object, true);
  hidden(this->datetime_common_[ROOT], false);
  hidden(this->datetime_common_[FIELDS], false);
  const auto draft = this->owner_.datetime_draft();
  const bool editing = draft.editor_open, has_date = state->mode != Mode::CONTROL_DATETIME_MODE_TIME,
             has_time = state->mode != Mode::CONTROL_DATETIME_MODE_DATE;
  const auto &layout = this->datetime_layouts_[editing ? 1 : 0];
  lv_obj_set_pos(this->datetime_common_[FIELDS], layout.x, layout.y);
  lv_obj_set_height(this->datetime_common_[FIELDS], layout.height);
  hidden(this->datetime_common_[EDITOR], !editing);
  for (auto index : {SUMMARY, ZONE, UPDATED})
    hidden(this->datetime_common_[index], editing);
  hidden(this->datetime_icons_[0], editing || !has_date);
  hidden(this->datetime_icons_[1], editing || has_date);
  for (size_t index = 0; index < this->datetime_fields_.size(); index++) {
    auto *button = this->datetime_fields_[index];
    const bool shown = index < 3 ? has_date : has_time;
    hidden(button, !shown);
    lv_obj_set_height(button, layout.field_height);
    lv_obj_set_y(button, has_date && has_time ? (index < 3 ? 0 : layout.height - layout.field_height)
                                              : (layout.height - layout.field_height) / 2);
  }
  lv_obj_update_layout(this->root_);
  bool complete = this->extra_text_(this->datetime_common_[TITLE], this->owner_.state().title.bytes.data());
  const auto kind = has_date ? (has_time ? 2 : 0) : 1;
  constexpr const char *KINDS[] = {"Date", "Time", "Date and time"};
  complete &= this->extra_text_(this->datetime_common_[KIND], this->text_(18 + kind, KINDS[kind]));
  const auto &value = draft.active ? draft.value : state->value;
  constexpr const char *FIELDS_TEXT[] = {"Year", "Month", "Day", "Hour", "Minute", "Second"};
  char text[256];
  for (size_t index = 0; index < this->datetime_fields_.size(); index++) {
    const bool shown = index < 3 ? has_date : has_time;
    complete &= this->extra_text_(this->datetime_captions_[index], this->text_(21 + index, FIELDS_TEXT[index]));
    if (shown && state->available() && (value.known_mask & (1U << index)))
      std::snprintf(text, sizeof(text), index == 0 ? "%04u" : "%02u", unsigned(value.values[index]));
    else
      std::snprintf(text, sizeof(text), "--");
    complete &= this->extra_text_(this->datetime_values_[index], text);
  }
  if (editing) {
    const auto field = draft.selected_field;
    complete &=
        field < 6 && this->extra_text_(this->datetime_common_[FIELD_NAME], this->text_(21 + field, FIELDS_TEXT[field]));
    complete &= this->extra_text_(this->datetime_common_[DIGITS], draft.digits.data());
  } else {
    int size = 0;
    if (!state->available())
      size = std::snprintf(text, sizeof(text), "--");
    else if (has_date) {
      const auto month = value.values[1];
      constexpr const char *MONTHS[] = {"January", "February", "March",     "April",   "May",      "June",
                                        "July",    "August",   "September", "October", "November", "December"};
      size = month >= 1 && month <= 12
                 ? std::snprintf(text, sizeof(text), "%u %s %u", unsigned(value.values[2]),
                                 this->text_(34 + month, MONTHS[month - 1]), unsigned(value.values[0]))
                 : -1;
      if (has_time && size >= 0 && size < int(sizeof(text))) {
        const int remaining =
            std::snprintf(text + size, sizeof(text) - size, "\n%02u:%02u:%02u", unsigned(value.values[3]),
                          unsigned(value.values[4]), unsigned(value.values[5]));
        size = remaining < 0 ? -1 : size + remaining;
      }
    } else
      size = std::snprintf(text, sizeof(text), "%02u:%02u:%02u", unsigned(value.values[3]), unsigned(value.values[4]),
                           unsigned(value.values[5]));
    complete &= size >= 0 && size < int(sizeof(text));
    complete &= this->extra_text_(this->datetime_common_[SUMMARY], size >= 0 && size < int(sizeof(text)) ? text : "--",
                                  nullptr, nullptr, true);
    complete &= this->extra_text_(this->datetime_common_[ZONE], state->zone.bytes.data());
    // The wire timestamp is validated by the codec. Its exact offset is retained;
    // presentation to a minute does not reinterpret the wall tuple as an instant.
    const auto *updated = state->updated.bytes.data();
    if (std::strlen(updated) == 25) {
      const auto number = [updated](size_t p, size_t count) {
        unsigned n = 0;
        for (size_t i = 0; i < count; i++)
          n = n * 10 + unsigned(updated[p + i] - '0');
        return n;
      };
      const auto month = number(5, 2);
      constexpr const char *MONTHS[] = {"January", "February", "March",     "April",   "May",      "June",
                                        "July",    "August",   "September", "October", "November", "December"};
      size = month >= 1 && month <= 12
                 ? std::snprintf(text, sizeof(text), "%s: %u %s %u · %02u:%02u\nUTC%s", this->text_(34, "Updated"),
                                 number(8, 2), this->text_(34 + month, MONTHS[month - 1]), number(0, 4), number(11, 2),
                                 number(14, 2), updated + 19)
                 : -1;
    } else {
      text[0] = 0;
      size = 0;
    }
    complete &= size >= 0 && size < int(sizeof(text));
    complete &= this->extra_text_(this->datetime_common_[UPDATED], size >= 0 && size < int(sizeof(text)) ? text : "--",
                                  nullptr, nullptr, true);
  }
  complete &= this->extra_text_(this->datetime_labels_[0], this->text_(33, "Cancel"));
  complete &= this->extra_text_(this->datetime_labels_[1], this->text_(32, "Save"));
  const auto active_bg = configured_style_color_(this->on_button_, LV_STYLE_BG_COLOR),
             active_fg = configured_style_color_(this->minus_label_, LV_STYLE_TEXT_COLOR),
             neutral_bg = configured_style_color_(this->off_button_, LV_STYLE_BG_COLOR),
             neutral_fg = configured_style_color_(this->plus_label_, LV_STYLE_TEXT_COLOR);
  for (auto *icon : this->datetime_icons_)
    if (!lv_color_eq(configured_style_color_(icon, LV_STYLE_TEXT_COLOR), active_bg))
      lv_obj_set_style_text_color(icon, active_bg, LV_PART_MAIN);
  const auto palette = [&](lv_obj_t *button, bool active) {
    const auto bg = active ? active_bg : neutral_bg, fg = active ? active_fg : neutral_fg;
    if (!lv_color_eq(configured_style_color_(button, LV_STYLE_BG_COLOR), bg))
      lv_obj_set_style_bg_color(button, bg, LV_PART_MAIN);
    if (!lv_color_eq(configured_style_color_(button, LV_STYLE_TEXT_COLOR), fg))
      lv_obj_set_style_text_color(button, fg, LV_PART_MAIN);
    // Media's owned-content rule also applies here: local child styles cannot
    // override the foreground selected for this button's current palette.
    for (uint32_t child = 0; child < lv_obj_get_child_count(button); child++) {
      auto *object = lv_obj_get_child(button, child);
      if (!lv_color_eq(configured_style_color_(object, LV_STYLE_TEXT_COLOR), fg))
        lv_obj_set_style_text_color(object, fg, LV_PART_MAIN);
    }
  };
  this->update_status_();
  const char *status = this->text_(static_cast<uint8_t>(this->status_) + 2, this->get_status_key());
  if (this->owner_.is_bound() && !this->owner_.is_command_pending() && this->owner_.is_enabled()) {
    if (!state->available())
      status = this->text_(state->source == 1 ? 15 : 16, state->source == 1 ? "Unknown" : "Unavailable");
    else if (!state->can_set)
      status = this->text_(31, "Read only");
    else if (editing && !draft.field_valid)
      status = this->text_(28, "Correct this field");
    else if (draft.active && !draft.tuple_valid)
      status = this->text_(29, "Correct the date or time");
    else if (draft.changed_elsewhere)
      status = this->text_(30, "Value changed in Home Assistant");
    else if (draft.dirty)
      status = this->text_(27, "Unsaved changes");
  }
  complete &= this->extra_text_(this->datetime_common_[STATUS],
                                complete ? status : this->text_(17, "Full value cannot be displayed"));
  this->datetime_ready_ = complete;
  // Disabled controls retain opaque backgrounds; remove their action accent
  // explicitly, including local foreground styles on labels and SVG children.
  for (size_t i = 0; i < this->datetime_fields_.size(); i++) {
    const bool allow = complete && this->owner_.can_datetime_input(Input::CONTROL_DATETIME_INPUT_SELECT_FIELD, i);
    palette(this->datetime_fields_[i], allow && editing && i == draft.selected_field);
    enabled(this->datetime_fields_[i], allow);
  }
  for (size_t i = 0; i < this->datetime_keys_.size(); i++) {
    const bool allow = complete && editing && this->owner_.can_datetime_input(key_input(i), i < 10 ? i : 0);
    palette(this->datetime_keys_[i], allow && i == 11);
    enabled(this->datetime_keys_[i], allow);
  }
  for (size_t i = 0; i < this->datetime_controls_.size(); i++) {
    const bool allow = complete && this->owner_.can_datetime_input(control_input(i), 0);
    hidden(this->datetime_controls_[i], i < 2 && !editing);
    palette(this->datetime_controls_[i], allow && i == 3);
    enabled(this->datetime_controls_[i], allow);
  }
}

void MaterialEntityControlComponent::datetime_input_event_(lv_event_code_t code, lv_obj_t *target, lv_indev_t *indev,
                                                           uint32_t now) {
  Input action = Input::CONTROL_DATETIME_INPUT_CANCEL;
  uint8_t argument = 0;
  bool found = false;
  for (size_t i = 0; i < this->datetime_fields_.size(); i++)
    if (target == this->datetime_fields_[i]) {
      action = Input::CONTROL_DATETIME_INPUT_SELECT_FIELD;
      argument = i;
      found = true;
    }
  for (size_t i = 0; i < this->datetime_keys_.size(); i++)
    if (target == this->datetime_keys_[i]) {
      action = key_input(i);
      argument = i < 10 ? i : 0;
      found = true;
    }
  for (size_t i = 0; i < this->datetime_controls_.size(); i++)
    if (target == this->datetime_controls_[i]) {
      action = control_input(i);
      found = true;
    }
  if (!found)
    return;
  if (code == LV_EVENT_PRESSED) {
    if (this->captured_target_ || !this->datetime_ready_ || !indev ||
        lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER || !this->is_presented_() || !lv_obj_is_visible(target) ||
        lv_obj_has_state(target, LV_STATE_DISABLED) || this->pointer_sequence_ == std::numeric_limits<uint32_t>::max())
      return;
#ifdef USE_LVGL_NAVIGATION_ADMISSION_GUARD
    if (this->view_lifecycle_.native_power_input_active())
      return;
#endif
    if (!this->owner_.begin_datetime_input(++this->pointer_sequence_, action, argument, now))
      return;
    this->captured_target_ = target;
    this->captured_indev_ = indev;
    this->captured_generation_ = this->owner_.admission_generation();
  }
  if (target != this->captured_target_ || indev != this->captured_indev_)
    return;
  if (!this->is_presented_() || !lv_obj_is_visible(target) ||
      this->captured_generation_ != this->owner_.admission_generation() || !this->owner_.is_dragging()) {
    this->cancel_pointer_();
    this->process_events_();
    return;
  }
  if (code == LV_EVENT_RELEASED) {
    lv_point_t point;
    lv_area_t bounds;
    lv_indev_get_point(indev, &point);
    lv_obj_get_coords(target, &bounds);
    this->captured_target_ = nullptr;
    this->captured_indev_ = nullptr;
    if (point.x < bounds.x1 || point.x > bounds.x2 || point.y < bounds.y1 || point.y > bounds.y2)
      this->owner_.cancel_drag();
    else
      this->owner_.release_datetime_input(this->pointer_sequence_, now);
    this->update_static_();
  }
  this->process_events_();
}

void MaterialEntityControlComponent::log_datetime_view(const char *phase) const {
#ifdef USE_LVGL_MATERIAL_SCALAR_PERFORMANCE_METRICS
  const auto *state = this->owner_.is_datetime() ? this->owner_.state().family_if<Snapshot>() : nullptr;
  const auto draft = this->owner_.datetime_draft();
  ESP_LOGI("scalar.performance",
           "%s datetime_active=%u datetime_retained=%u datetime_ready=%u datetime_state_seq=%u "
           "datetime_generation=%u datetime_mode=%u datetime_source=%u datetime_draft=%u datetime_editor=%u "
           "datetime_field=%u datetime_revision=%u datetime_can_save=%u datetime_result=%u",
           phase, unsigned(state != nullptr), unsigned(this->datetime_widgets_active_), unsigned(this->datetime_ready_),
           unsigned(state ? this->owner_.state().state_seq : 0), unsigned(state ? state->state_generation : 0),
           unsigned(state ? state->mode : Mode::CONTROL_DATETIME_MODE_DATE), unsigned(state ? state->source : 255),
           unsigned(draft.active), unsigned(draft.editor_open), unsigned(draft.selected_field),
           unsigned(draft.revision), unsigned(draft.can_save), unsigned(this->owner_.last_result()));
#else
  (void) phase;
#endif
}
}  // namespace esphome::lvgl_material
#endif
