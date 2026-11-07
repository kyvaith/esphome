#include "material_entity_control_component.h"
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
#include <cstdio>
#include <cstring>
namespace esphome::lvgl_material {
namespace {
enum RobotWidget : size_t {
  ROOT,
  TITLE,
  KIND,
  STATE,
  STATUS,
  SUCTION,
  SUCTION_LABEL,
  OPTIONS,
  BACK,
  VACUUM_ICON,
  MOWER_ICON,
  ROOM,
  ROOM_LABEL,
  OPTIONS_TITLE,
  APPLY,
  APPLY_LABEL,
  BATTERY
};
using Op = ControlOperation;
void hidden(lv_obj_t *widget, bool value) {
  if (lv_obj_has_flag(widget, LV_OBJ_FLAG_HIDDEN) == value)
    return;
  if (value)
    lv_obj_add_flag(widget, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_remove_flag(widget, LV_OBJ_FLAG_HIDDEN);
}
void enabled(lv_obj_t *widget, bool value) {
  if (value)
    lv_obj_remove_state(widget, LV_STATE_DISABLED);
  else
    lv_obj_add_state(widget, LV_STATE_DISABLED);
}
void label(lv_obj_t *widget, const char *text) {
  if (std::strcmp(lv_label_get_text(widget), text) != 0)
    lv_label_set_text(widget, text);
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

void MaterialEntityControlComponent::set_robot_profile(lv_obj_t *root, lv_obj_t *title, lv_obj_t *kind, lv_obj_t *state,
                                                       lv_obj_t *status, lv_obj_t *suction, lv_obj_t *suction_label,
                                                       lv_obj_t *options, lv_obj_t *back, lv_obj_t *vacuum_icon,
                                                       lv_obj_t *mower_icon, lv_obj_t *room, lv_obj_t *room_label,
                                                       lv_obj_t *options_title, lv_obj_t *apply, lv_obj_t *apply_label,
                                                       lv_obj_t *battery) {
  if (!this->setup_ready_)
    this->robot_widgets_ = {root,          title,         kind,  state,       status,     suction,
                            suction_label, options,       back,  vacuum_icon, mower_icon, room,
                            room_label,    options_title, apply, apply_label, battery};
}
void MaterialEntityControlComponent::set_robot_action(size_t index, lv_obj_t *button, lv_obj_t *text) {
  if (this->setup_ready_ || index >= this->robot_action_buttons_.size())
    return;
  this->robot_action_buttons_[index] = button;
  this->robot_action_labels_[index] = text;
}
void MaterialEntityControlComponent::set_robot_option(size_t index, lv_obj_t *button, lv_obj_t *text) {
  if (this->setup_ready_ || index >= this->robot_option_buttons_.size())
    return;
  this->robot_option_buttons_[index] = button;
  this->robot_option_labels_[index] = text;
}
bool MaterialEntityControlComponent::setup_robot_profile_() {
  auto &widgets = this->robot_widgets_;
  if (widgets[ROOT] == nullptr)
    return true;
  if (!this->number_profile_ || !within(this->root_, widgets[ROOT]))
    return false;
  for (size_t index = 0; index < widgets.size(); index++) {
    if (widgets[index] == nullptr || (index != ROOT && !within(widgets[ROOT], widgets[index])))
      return false;
    for (size_t previous = 0; previous < index; previous++)
      if (widgets[index] == widgets[previous])
        return false;
  }
  for (auto index : {TITLE, KIND, STATE, STATUS, SUCTION_LABEL, ROOM_LABEL, OPTIONS_TITLE, APPLY_LABEL, BATTERY})
    if (!lv_obj_check_type(widgets[index], &lv_label_class))
      return false;
  for (auto index : {SUCTION, ROOM, BACK, APPLY}) {
    if (!lv_obj_check_type(widgets[index], &lv_button_class))
      return false;
    if (lv_obj_check_type(widgets[index], &lv_button_class))
      this->style_disabled_control_(widgets[index]);
    lv_obj_add_event_cb(widgets[index], input_event_cb_, LV_EVENT_ALL, this);
  }
  if (!within(widgets[OPTIONS], widgets[BACK]) || !within(widgets[OPTIONS], widgets[APPLY]) ||
      !within(widgets[OPTIONS], widgets[OPTIONS_TITLE]))
    return false;
  for (bool options : {false, true}) {
    const size_t count = options ? this->robot_option_buttons_.size() : this->robot_action_buttons_.size();
    for (size_t index = 0; index < count; index++) {
      auto *button = options ? this->robot_option_buttons_[index] : this->robot_action_buttons_[index];
      auto *text = options ? this->robot_option_labels_[index] : this->robot_action_labels_[index];
      if (button == nullptr || text == nullptr || !lv_obj_check_type(button, &lv_button_class) ||
          !lv_obj_check_type(text, &lv_label_class) || !within(widgets[options ? OPTIONS : ROOT], button) ||
          !within(button, text))
        return false;
      if (lv_obj_check_type(button, &lv_button_class))
        this->style_disabled_control_(button);
      lv_obj_add_event_cb(button, input_event_cb_, LV_EVENT_ALL, this);
    }
  }
  hidden(widgets[ROOT], true);
  hidden(widgets[OPTIONS], true);
  return this->owner_.set_robot_enabled(true);
}
bool MaterialEntityControlComponent::open_robot(uint32_t slot, std::string_view card_id, uint64_t revision) {
  return this->owner_.robot_enabled() &&
         this->open_entity_(slot, card_id, revision, ControlEntityKind::CONTROL_ENTITY_ROBOT);
}
bool MaterialEntityControlComponent::robot_glyphs_available_(bool area) const {
  const auto *selected_family = this->owner_.state().family_if<ControlSnapshot::Robot>();
  if (selected_family == nullptr)
    return false;
  const auto &caps = selected_family->caps;
  const auto &options = area ? caps.areas : caps.speeds;
  const size_t count = area ? caps.area_count : caps.speed_count;
  for (size_t index = 0; index < count; index++)
    if (!glyphs_available(this->robot_option_labels_[index], options[index].view()) ||
        (!area && !glyphs_available(this->robot_widgets_[SUCTION_LABEL], options[index].view())))
      return false;
  return true;
}
void MaterialEntityControlComponent::restore_robot_widgets_() {
  if (!this->robot_widgets_active_)
    return;
  const std::array<lv_obj_t *, 8> old{this->hit_,        this->value_,       this->unknown_value_, this->on_button_,
                                      this->off_button_, this->state_label_, this->status_label_,  this->title_};
  for (size_t index = 0; index < old.size(); index++)
    if (old[index] != nullptr)
      hidden(old[index], (this->robot_hidden_mask_ & (1U << index)) != 0);
  hidden(this->robot_widgets_[ROOT], true);
  hidden(this->robot_widgets_[OPTIONS], true);
  this->robot_widgets_active_ = false;
  this->robot_modal_ = 0;
  this->robot_room_index_ = ControlSnapshot::Robot::UNKNOWN;
  this->robot_room_generation_ = 0;
}
void MaterialEntityControlComponent::update_robot_static_() {
  auto &widgets = this->robot_widgets_;
  const std::array<lv_obj_t *, 8> old{this->hit_,        this->value_,       this->unknown_value_, this->on_button_,
                                      this->off_button_, this->state_label_, this->status_label_,  this->title_};
  if (!this->robot_widgets_active_) {
    this->robot_hidden_mask_ = 0;
    for (size_t index = 0; index < old.size(); index++)
      if (old[index] != nullptr && lv_obj_has_flag(old[index], LV_OBJ_FLAG_HIDDEN))
        this->robot_hidden_mask_ |= 1U << index;
    this->robot_widgets_active_ = true;
    this->robot_modal_ = 0;
    this->robot_room_index_ = ControlSnapshot::Robot::UNKNOWN;
    this->robot_room_generation_ = 0;
  }
  for (auto *widget : old)
    if (widget != nullptr)
      hidden(widget, true);
  hidden(widgets[ROOT], false);
  hidden(widgets[OPTIONS], this->robot_modal_ == 0);
  const auto *selected_family = this->owner_.state().family_if<ControlSnapshot::Robot>();
  if (selected_family == nullptr)
    return;
  const auto &robot = *selected_family;
  const bool selected = this->owner_.has_selection();
  const bool ready = selected && this->owner_.is_view_ready() && !this->owner_.is_command_pending();
  const bool mower = robot.type == 1;
  const bool area = this->robot_modal_ == 2;
  const bool speed_glyphs = selected && this->robot_glyphs_available_(false);
  const bool area_glyphs = selected && this->robot_glyphs_available_(true);
  if (selected)
    label(widgets[TITLE], this->owner_.state().title.bytes.data());
  label(widgets[KIND], this->text_(mower ? 17 : 16, mower ? "Mower" : "Vacuum"));
  const char *states[] = {"Unknown", "Cleaning", "Docked", "Idle", "Paused", "Returning", "Error", "Mowing"};
  const uint8_t activity = selected ? robot.activity : 0;
  label(widgets[STATE], this->text_(18 + activity, states[activity]));
  hidden(widgets[VACUUM_ICON], mower);
  hidden(widgets[MOWER_ICON], !mower);
  char text[128];
  hidden(widgets[BATTERY], !selected || robot.battery_percent == ControlSnapshot::Robot::UNKNOWN);
  if (selected && robot.battery_percent != ControlSnapshot::Robot::UNKNOWN) {
    std::snprintf(text, sizeof(text), "%s: %u%%", this->text_(39, "Battery"), robot.battery_percent);
    label(widgets[BATTERY], text);
  }
  const auto active_bg = lv_obj_get_style_bg_color(this->on_button_, LV_PART_MAIN);
  const auto active_fg = lv_obj_get_style_text_color(this->minus_label_, LV_PART_MAIN);
  const auto neutral_bg = lv_obj_get_style_bg_color(this->off_button_, LV_PART_MAIN);
  const auto neutral_fg = lv_obj_get_style_text_color(this->plus_label_, LV_PART_MAIN);
  const auto color = [&](lv_obj_t *button, lv_obj_t *text, bool active) {
    active &= !lv_obj_has_state(button, LV_STATE_DISABLED);
    const auto bg = active ? active_bg : neutral_bg;
    const auto fg = active ? active_fg : neutral_fg;
    if (!lv_color_eq(lv_obj_get_style_bg_color(button, LV_PART_MAIN), bg))
      lv_obj_set_style_bg_color(button, bg, LV_PART_MAIN);
    if (!lv_color_eq(lv_obj_get_style_text_color(text, LV_PART_MAIN), fg))
      lv_obj_set_style_text_color(text, fg, LV_PART_MAIN);
  };
  const char *actions[] = {mower ? "Start mowing" : "Start cleaning", "Pause", "Stop", "Dock", "Locate", "Clean spot"};
  for (size_t index = 0; index < this->robot_action_buttons_.size(); index++) {
    auto *button = this->robot_action_buttons_[index];
    auto *text = this->robot_action_labels_[index];
    hidden(button, !selected || (robot.caps.actions & (1U << index)) == 0);
    label(text, this->text_(index == 0 ? (mower ? 27 : 26) : 27 + index, actions[index]));
    enabled(button,
            this->owner_.can_robot(static_cast<Op>(static_cast<uint8_t>(Op::CONTROL_OPERATION_ROBOT_START) + index)));
    color(button, text, index == 0 || index == 3);
  }
  hidden(widgets[SUCTION], mower || robot.caps.speed_count == 0);
  hidden(widgets[ROOM], mower || robot.caps.area_count == 0);
  const char *speed = speed_glyphs && robot.speed_index < robot.caps.speed_count
                          ? robot.caps.speeds[robot.speed_index].bytes.data()
                          : this->text_(18, "Unknown");
  std::snprintf(text, sizeof(text), "%s: %s", this->text_(33, "Suction"), speed);
  label(widgets[SUCTION_LABEL], text);
  label(widgets[ROOM_LABEL], this->text_(37, "Room"));
  enabled(widgets[SUCTION],
          speed_glyphs && this->owner_.can_robot(Op::CONTROL_OPERATION_ROBOT_SPEED, robot.speed_index, true));
  enabled(widgets[ROOM], area_glyphs && this->owner_.can_robot(Op::CONTROL_OPERATION_ROBOT_AREA, 0));
  label(widgets[OPTIONS_TITLE], this->text_(area ? 37 : 33, area ? "Room" : "Suction"));
  hidden(widgets[APPLY], !area);
  label(widgets[APPLY_LABEL], this->text_(40, "Clean room"));
  if (!selected || this->robot_room_generation_ != robot.state_generation)
    this->robot_room_index_ = ControlSnapshot::Robot::UNKNOWN;
  enabled(widgets[APPLY],
          area_glyphs && this->owner_.can_robot(Op::CONTROL_OPERATION_ROBOT_AREA, this->robot_room_index_));
  color(widgets[APPLY], widgets[APPLY_LABEL], true);
  enabled(widgets[BACK], ready);
  const auto &options = area ? robot.caps.areas : robot.caps.speeds;
  const size_t count = area ? robot.caps.area_count : robot.caps.speed_count;
  for (size_t index = 0; index < this->robot_option_buttons_.size(); index++) {
    auto *button = this->robot_option_buttons_[index];
    auto *text = this->robot_option_labels_[index];
    hidden(button, !selected || index >= count);
    if (index >= count)
      continue;
    const bool glyphs = area ? area_glyphs : speed_glyphs;
    label(text, glyphs ? options[index].bytes.data() : this->text_(34, "Unsupported"));
    enabled(button, glyphs && (area ? this->owner_.can_robot(Op::CONTROL_OPERATION_ROBOT_AREA, index)
                                    : this->owner_.can_robot(Op::CONTROL_OPERATION_ROBOT_SPEED, index, true)));
    color(button, text, index == (area ? this->robot_room_index_ : robot.speed_index));
  }
  this->update_status_();
  const auto status = static_cast<uint8_t>(this->status_);
  const char *fallback =
      this->status_texts_[status].empty() ? this->get_status_key() : this->status_texts_[status].c_str();
  const char *message = this->text_(status + 2, fallback);
  if (selected && !robot.available)
    message = this->text_(35, "Robot unavailable");
  else if (this->owner_.last_result() == ControlResult::CONTROL_RESULT_SERVICE_COMPLETED)
    message = this->text_(36, "Command accepted by HA");
  label(widgets[STATUS], message);
  this->texts_pending_ = false;
}
bool MaterialEntityControlComponent::begin_robot_input_(lv_obj_t *target, uint32_t pointer, uint32_t now) {
  auto &widgets = this->robot_widgets_;
  const auto *selected_family = this->owner_.state().family_if<ControlSnapshot::Robot>();
  if (selected_family == nullptr)
    return false;
  const auto &robot = *selected_family;
  if (target == widgets[BACK])
    return this->robot_modal_ != 0 && this->owner_.begin_robot_navigation(pointer, now);
  if (target == widgets[SUCTION] || target == widgets[ROOM]) {
    if (this->robot_modal_ != 0)
      return false;
    const bool area = target == widgets[ROOM];
    if (!this->robot_glyphs_available_(area) ||
        !this->owner_.can_robot(area ? Op::CONTROL_OPERATION_ROBOT_AREA : Op::CONTROL_OPERATION_ROBOT_SPEED,
                                area ? 0 : robot.speed_index, true))
      return false;
    return this->owner_.begin_robot_navigation(pointer, now);
  }
  if (target == widgets[APPLY]) {
    if (this->robot_modal_ != 2 || this->robot_room_generation_ != robot.state_generation ||
        !this->robot_glyphs_available_(true))
      return false;
    return this->owner_.begin_robot(pointer, Op::CONTROL_OPERATION_ROBOT_AREA, this->robot_room_index_, now);
  }
  for (size_t index = 0; index < this->robot_action_buttons_.size(); index++)
    if (target == this->robot_action_buttons_[index])
      return this->robot_modal_ == 0 &&
             this->owner_.begin_robot(
                 pointer, static_cast<Op>(static_cast<uint8_t>(Op::CONTROL_OPERATION_ROBOT_START) + index), 0, now);
  for (size_t index = 0; index < this->robot_option_buttons_.size(); index++) {
    if (target != this->robot_option_buttons_[index])
      continue;
    if (this->robot_modal_ == 2)
      return this->robot_glyphs_available_(true) && this->owner_.can_robot(Op::CONTROL_OPERATION_ROBOT_AREA, index) &&
             this->owner_.begin_robot_navigation(pointer, now);
    return this->robot_modal_ == 1 && this->robot_glyphs_available_(false) &&
           this->owner_.begin_robot(pointer, Op::CONTROL_OPERATION_ROBOT_SPEED, index, now);
  }
  return false;
}
void MaterialEntityControlComponent::release_robot_input_(lv_obj_t *target, uint32_t pointer, uint32_t now) {
  auto &widgets = this->robot_widgets_;
  if (target == widgets[SUCTION] || target == widgets[ROOM] || target == widgets[BACK]) {
    if (this->owner_.release_robot_navigation(pointer, now)) {
      this->robot_modal_ = target == widgets[ROOM] ? 2 : target == widgets[SUCTION] ? 1 : 0;
      this->robot_room_index_ = ControlSnapshot::Robot::UNKNOWN;
      this->robot_room_generation_ = 0;
      this->update_static_();
    }
    return;
  }
  if (this->robot_modal_ == 2 && target != widgets[APPLY]) {
    for (size_t index = 0; index < this->robot_option_buttons_.size(); index++)
      if (target == this->robot_option_buttons_[index] && this->owner_.release_robot_navigation(pointer, now)) {
        const auto *robot = this->owner_.state().family_if<ControlSnapshot::Robot>();
        if (robot == nullptr)
          return;
        this->robot_room_index_ = index;
        this->robot_room_generation_ = robot->state_generation;
        this->update_static_();
        return;
      }
  }
  if (this->owner_.release_robot(pointer, now))
    this->robot_modal_ = 0;
}
}  // namespace esphome::lvgl_material
#endif
