#include "material_entity_control_component.h"

#ifdef USE_LVGL_MATERIAL_COVER_CONTROLS
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace esphome::lvgl_material {
#ifdef USE_LVGL_MATERIAL_COVER_PRIORITY_STOP
bool MaterialEntityControlComponent::populate_cover_features(ArduinoJson::JsonObject root) {
  root.clear();
  root["v"] = 1;
  root["type"] = "entity_control_features_v1";
  root["cover_stop_version"] = this->setup_ready_ && !this->is_failed() && this->owner_.is_enabled() &&
                                       this->owner_.cover_priority_stop_supported()
                                   ? 1
                                   : 0;
  return root.size() == 3;
}

bool MaterialEntityControlComponent::bind_cover_features(std::string_view channel, uint32_t version,
                                                         ArduinoJson::JsonObject root) {
  ControlText<32> parsed;
  const bool accepted = this->setup_ready_ && !this->output_callback_active_ && parsed.assign(channel) &&
                        this->owner_.accept_cover_features(parsed, version);
  root.clear();
  root["v"] = 1;
  root["type"] = "entity_control_features_v1";
  root["accepted"] = accepted;
  root["channel"] = accepted ? this->owner_.bound_channel().bytes.data() : "";
  root["cover_stop_version"] = accepted ? 1 : 0;
  return root.size() == 5;
}
#endif
namespace {
void hidden(lv_obj_t *widget, bool value) {
  if (widget == nullptr || lv_obj_has_flag(widget, LV_OBJ_FLAG_HIDDEN) == value)
    return;
  if (value)
    lv_obj_add_flag(widget, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_remove_flag(widget, LV_OBJ_FLAG_HIDDEN);
}
void label(lv_obj_t *widget, const char *text) {
  if (widget != nullptr && std::strcmp(lv_label_get_text(widget), text) != 0)
    lv_label_set_text(widget, text);
}
bool within(lv_obj_t *root, lv_obj_t *widget) {
  for (auto *parent = lv_obj_get_parent(widget); parent != nullptr; parent = lv_obj_get_parent(parent))
    if (parent == root)
      return true;
  return false;
}
}  // namespace

void MaterialEntityControlComponent::set_cover_profile(lv_obj_t *root, lv_obj_t *current_label,
                                                       lv_obj_t *status_label) {
  if (this->setup_ready_)
    return;
  this->cover_root_ = root;
  this->cover_current_label_ = current_label;
  this->cover_status_label_ = status_label;
}
void MaterialEntityControlComponent::set_cover_button(size_t index, lv_obj_t *button, lv_obj_t *text) {
  if (this->setup_ready_ || index >= this->cover_buttons_.size())
    return;
  this->cover_buttons_[index] = button;
  this->cover_labels_[index] = text;
}
bool MaterialEntityControlComponent::setup_cover_profile_() {
  if (this->cover_root_ == nullptr)
    return true;
  if (!this->number_profile_ || this->cover_current_label_ == nullptr || this->cover_status_label_ == nullptr ||
      !lv_obj_check_type(this->cover_current_label_, &lv_label_class) ||
      !lv_obj_check_type(this->cover_status_label_, &lv_label_class) || !within(this->root_, this->cover_root_) ||
      !within(this->cover_root_, this->cover_current_label_) || !within(this->cover_root_, this->cover_status_label_))
    return false;
  for (size_t index = 0; index < this->cover_buttons_.size(); index++) {
    auto *button = this->cover_buttons_[index];
    auto *text = this->cover_labels_[index];
    if (button == nullptr || text == nullptr || !lv_obj_check_type(button, &lv_button_class) ||
        !lv_obj_check_type(text, &lv_label_class) || !within(this->cover_root_, button) || !within(button, text))
      return false;
    for (size_t previous = 0; previous < index; previous++)
      if (button == this->cover_buttons_[previous] || text == this->cover_labels_[previous])
        return false;
  }
  for (auto *button : this->cover_buttons_)
    lv_obj_add_event_cb(button, input_event_cb_, LV_EVENT_ALL, this);
  hidden(this->cover_root_, true);
  return this->owner_.set_cover_enabled(true)
#ifdef USE_LVGL_MATERIAL_COVER_PANEL
         && this->setup_cover_panel_()
#endif
      ;
}
bool MaterialEntityControlComponent::open_cover(uint32_t slot, std::string_view card_id, uint64_t revision) {
  return this->owner_.cover_enabled() &&
         this->open_entity_(slot, card_id, revision, ControlEntityKind::CONTROL_ENTITY_COVER);
}
void MaterialEntityControlComponent::restore_cover_widgets_() {
  if (!this->cover_widgets_active_)
    return;
  const std::array<lv_obj_t *, 8> widgets{this->hit_,        this->value_,       this->unknown_value_, this->on_button_,
                                          this->off_button_, this->state_label_, this->status_label_,  this->title_};
  for (size_t index = 0; index < widgets.size(); index++)
    hidden(widgets[index], (this->cover_hidden_mask_ & (1U << index)) != 0);
  hidden(this->cover_root_, true);
  this->cover_widgets_active_ = false;
}
void MaterialEntityControlComponent::update_cover_static_() {
  const std::array<lv_obj_t *, 8> widgets{this->hit_,        this->value_,       this->unknown_value_, this->on_button_,
                                          this->off_button_, this->state_label_, this->status_label_,  this->title_};
  if (!this->cover_widgets_active_) {
    this->cover_hidden_mask_ = 0;
    for (size_t index = 0; index < widgets.size(); index++)
      if (widgets[index] != nullptr && lv_obj_has_flag(widgets[index], LV_OBJ_FLAG_HIDDEN))
        this->cover_hidden_mask_ |= 1U << index;
    this->cover_widgets_active_ = true;
  }
  for (auto *widget : widgets)
    hidden(widget, true);
  hidden(this->title_, false);
  hidden(this->cover_root_, false);
  const auto *selected_family = this->owner_.state().family_if<ControlCoverSnapshot>();
  if (selected_family == nullptr)
    return;
  const auto &cover = *selected_family;
  const bool selected = this->owner_.has_selection();
  if (selected)
    label(this->title_, this->owner_.state().title.bytes.data());
  const char *states[] = {"Unknown", "Open", "Opening", "Closed", "Closing"};
  const size_t state_index = selected ? static_cast<size_t>(cover.state) : 0;
  const char *state = this->text_(static_cast<uint8_t>(16 + state_index), states[state_index]);
  char current[192];
  if (selected && cover.position_known)
    std::snprintf(current, sizeof(current), "%s  \xC2\xB7  %u%%", state, static_cast<unsigned>(cover.position));
  else
    std::snprintf(current, sizeof(current), "%s", state);
  label(this->cover_current_label_, current);
#ifdef USE_LVGL_MATERIAL_COVER_PANEL
  if (this->cover_panel_widgets_[0] != nullptr)
    this->update_cover_panel_();
#endif
  const char *labels[] = {"Open", "Stop", "Close"};
  for (size_t index = 0; index < this->cover_buttons_.size(); index++) {
    auto *button = this->cover_buttons_[index];
    auto *text = this->cover_labels_[index];
    label(text, this->text_(static_cast<uint8_t>(21 + index), labels[index]));
    const auto operation =
        static_cast<ControlOperation>(static_cast<uint8_t>(ControlOperation::CONTROL_OPERATION_COVER_OPEN) + index);
    const bool available = selected && this->owner_.can_cover(operation);
    if (available)
      lv_obj_remove_state(button, LV_STATE_DISABLED);
    else
      lv_obj_add_state(button, LV_STATE_DISABLED);
    bool neutral = index == 1;
#ifdef USE_LVGL_MATERIAL_COVER_PANEL
    neutral |= this->owner_.cover_panel_active() && !available;
#endif
    auto *reference = neutral ? this->off_button_ : this->on_button_;
    const auto background = lv_obj_get_style_bg_color(reference, LV_PART_MAIN);
    auto *reference_label = neutral ? this->plus_label_ : this->minus_label_;
    const auto foreground = lv_obj_get_style_text_color(reference_label, LV_PART_MAIN);
    if (!lv_color_eq(lv_obj_get_style_bg_color(button, LV_PART_MAIN), background))
      lv_obj_set_style_bg_color(button, background, LV_PART_MAIN);
    if (!lv_color_eq(lv_obj_get_style_text_color(text, LV_PART_MAIN), foreground))
      lv_obj_set_style_text_color(text, foreground, LV_PART_MAIN);
  }
  this->update_status_();
  const auto status = static_cast<uint8_t>(this->status_);
  const char *fallback =
      this->status_texts_[status].empty() ? this->get_status_key() : this->status_texts_[status].c_str();
  const char *message = this->text_(status + 2, fallback);
  if (selected && !cover.available)
    message = this->text_(24, "Cover unavailable");
  else if (!selected && this->owner_.last_reason() == ControlReason::CONTROL_REASON_CAPABILITIES_CHANGED)
    message = this->text_(25, "Capabilities changed - reopen");
  label(this->cover_status_label_, message);
  this->texts_pending_ = false;
}
#ifdef USE_LVGL_MATERIAL_COVER_PANEL
namespace {
enum CoverPanelWidget : size_t {
  COVER_TITLE,
  COVER_KIND,
  COVER_STATE,
  COVER_POSITION_GROUP,
  COVER_POSITION_TRACK,
  COVER_POSITION_FILL,
  COVER_POSITION_KNOB,
  COVER_TILT_GROUP,
  COVER_TILT_LABEL,
  COVER_TILT_TRACK,
  COVER_TILT_FILL,
  COVER_TILT_KNOB
};
void enabled(lv_obj_t *widget, bool value) {
  if (value)
    lv_obj_remove_state(widget, LV_STATE_DISABLED);
  else
    lv_obj_add_state(widget, LV_STATE_DISABLED);
}
constexpr ControlOperation TILT_OPERATIONS[] = {ControlOperation::CONTROL_OPERATION_COVER_OPEN_TILT,
                                                ControlOperation::CONTROL_OPERATION_COVER_STOP_TILT,
                                                ControlOperation::CONTROL_OPERATION_COVER_CLOSE_TILT};
}  // namespace

void MaterialEntityControlComponent::set_cover_panel_profile(lv_obj_t *title, lv_obj_t *kind, lv_obj_t *state,
                                                             lv_obj_t *position_group, lv_obj_t *position_track,
                                                             lv_obj_t *position_fill, lv_obj_t *position_knob,
                                                             lv_obj_t *tilt_group, lv_obj_t *tilt_label,
                                                             lv_obj_t *tilt_track, lv_obj_t *tilt_fill,
                                                             lv_obj_t *tilt_knob) {
  if (!this->setup_ready_)
    this->cover_panel_widgets_ = {title,         kind,       state,      position_group, position_track, position_fill,
                                  position_knob, tilt_group, tilt_label, tilt_track,     tilt_fill,      tilt_knob};
}

void MaterialEntityControlComponent::set_cover_tilt_button(size_t index, lv_obj_t *button, lv_obj_t *text) {
  if (this->setup_ready_ || index >= this->cover_tilt_buttons_.size())
    return;
  this->cover_tilt_buttons_[index] = button;
  this->cover_tilt_labels_[index] = text;
}

void MaterialEntityControlComponent::set_cover_panel_icon(size_t device_class, lv_obj_t *widget) {
  if (!this->setup_ready_ && device_class < this->cover_panel_icons_.size())
    this->cover_panel_icons_[device_class] = widget;
}

bool MaterialEntityControlComponent::populate_cover_panel_features(ArduinoJson::JsonObject root) {
  root.clear();
  root["v"] = 1;
  root["type"] = "entity_cover_features_v1";
  root["cover_panel_version"] =
      this->setup_ready_ && !this->is_failed() && this->owner_.is_enabled() && this->owner_.cover_panel_supported() ? 1
                                                                                                                    : 0;
  return root.size() == 3;
}

bool MaterialEntityControlComponent::bind_cover_panel_features(std::string_view channel, uint32_t version,
                                                               ArduinoJson::JsonObject root) {
  ControlText<32> parsed;
  const bool accepted = this->setup_ready_ && !this->output_callback_active_ && parsed.assign(channel) &&
                        this->owner_.accept_cover_panel_features(parsed, version);
  root.clear();
  root["v"] = 1;
  root["type"] = "entity_cover_features_v1";
  root["accepted"] = accepted;
  root["channel"] = accepted ? this->owner_.bound_channel().bytes.data() : "";
  root["cover_panel_version"] = accepted ? 1 : 0;
  return root.size() == 5;
}

bool MaterialEntityControlComponent::setup_cover_panel_() {
  auto &widgets = this->cover_panel_widgets_;
  if (widgets[COVER_TITLE] == nullptr)
    return true;
  if (!this->owner_.cover_priority_stop_supported())
    return false;
  for (size_t index = 0; index < widgets.size(); index++) {
    if (widgets[index] == nullptr || !within(this->cover_root_, widgets[index]))
      return false;
    for (size_t prior = 0; prior < index; prior++)
      if (widgets[index] == widgets[prior])
        return false;
  }
  for (auto index : {COVER_TITLE, COVER_KIND, COVER_STATE, COVER_TILT_LABEL})
    if (!lv_obj_check_type(widgets[index], &lv_label_class))
      return false;
  for (auto *icon : this->cover_panel_icons_)
    if (icon != nullptr && !within(this->cover_root_, icon))
      return false;
  for (bool tilt : {false, true}) {
    auto *track = widgets[tilt ? COVER_TILT_TRACK : COVER_POSITION_TRACK];
    auto *group = widgets[tilt ? COVER_TILT_GROUP : COVER_POSITION_GROUP];
    if (!within(group, track) || lv_obj_get_parent(widgets[tilt ? COVER_TILT_FILL : COVER_POSITION_FILL]) != track ||
        lv_obj_get_parent(widgets[tilt ? COVER_TILT_KNOB : COVER_POSITION_KNOB]) != track)
      return false;
    lv_obj_add_flag(track, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(track, input_event_cb_, LV_EVENT_ALL, this);
  }
  for (size_t index = 0; index < this->cover_tilt_buttons_.size(); index++) {
    auto *button = this->cover_tilt_buttons_[index];
    auto *text = this->cover_tilt_labels_[index];
    if (button == nullptr || text == nullptr || !lv_obj_check_type(button, &lv_button_class) ||
        !lv_obj_check_type(text, &lv_label_class) || !within(widgets[COVER_TILT_GROUP], button) ||
        !within(button, text))
      return false;
    lv_obj_add_event_cb(button, input_event_cb_, LV_EVENT_ALL, this);
  }
  return this->owner_.set_cover_panel_enabled(true);
}

void MaterialEntityControlComponent::show_cover_axis_(bool tilt, bool known, uint8_t value) {
  auto &widgets = this->cover_panel_widgets_;
  char current[128];
  if (known)
    std::snprintf(current, sizeof(current), "%s: %u%%", this->text_(tilt ? 27 : 26, tilt ? "Tilt" : "Position"), value);
  else
    std::snprintf(current, sizeof(current), "%s: --", this->text_(tilt ? 27 : 26, tilt ? "Tilt" : "Position"));
  label(tilt ? widgets[COVER_TILT_LABEL] : this->cover_current_label_, current);
  auto *track = widgets[tilt ? COVER_TILT_TRACK : COVER_POSITION_TRACK];
  auto *knob = widgets[tilt ? COVER_TILT_KNOB : COVER_POSITION_KNOB];
  auto *fill = widgets[tilt ? COVER_TILT_FILL : COVER_POSITION_FILL];
  const int32_t travel = std::max(int32_t{0}, lv_obj_get_width(track) - lv_obj_get_width(knob));
  const int32_t x = known ? (value * travel + 50) / 100 : 0;
  if (lv_obj_get_x(knob) != x)
    lv_obj_set_x(knob, x);
  if (lv_obj_get_width(fill) != x)
    lv_obj_set_width(fill, x);
  hidden(knob, !known);
}

void MaterialEntityControlComponent::update_cover_panel_() {
  auto &widgets = this->cover_panel_widgets_;
  const auto *selected_family = this->owner_.state().family_if<ControlCoverSnapshot>();
  if (selected_family == nullptr)
    return;
  const auto &cover = *selected_family;
  const auto &panel = cover.panel;
  const bool active = this->owner_.cover_panel_active();
  const bool selected = this->owner_.has_selection();
  hidden(this->title_, active);
  hidden(this->cover_current_label_, active && !(cover.position_known || panel.caps.set_position));
  hidden(widgets[COVER_TILT_LABEL], active && !(panel.tilt_known || panel.caps.set_tilt_position));
  if (active != this->cover_disabled_styles_active_) {
    // Keep legacy profile local styles byte-for-byte, including properties that
    // were absent. Only the explicitly negotiated panel uses the readable skin.
    constexpr lv_style_selector_t selector = static_cast<lv_style_selector_t>(LV_PART_MAIN) | LV_STATE_DISABLED;
    constexpr lv_style_prop_t properties[] = {LV_STYLE_RECOLOR_OPA, LV_STYLE_BG_OPA, LV_STYLE_TEXT_OPA};
    for (size_t index = 0; index < this->cover_disabled_styles_.size(); index++) {
      auto *button = index < 3 ? this->cover_buttons_[index] : this->cover_tilt_buttons_[index - 3];
      for (size_t property = 0; property < 3; property++) {
        auto &saved = this->cover_disabled_styles_[index][property];
        lv_style_value_t value{};
        if (active)
          saved = lv_obj_get_local_style_prop(button, properties[property], &value, selector) ? value.num : 256;
        else if (saved == 256)
          lv_obj_remove_local_style_prop(button, properties[property], selector);
        else {
          value.num = saved;
          lv_obj_set_local_style_prop(button, properties[property], value, selector);
        }
      }
      if (active)
        this->style_disabled_control_(button);
    }
    this->cover_disabled_styles_active_ = active;
  }
  for (auto index : {COVER_TITLE, COVER_KIND, COVER_STATE})
    hidden(widgets[index], !active);
  hidden(widgets[COVER_POSITION_GROUP], active && !(cover.position_known || cover.caps.open || cover.caps.stop ||
                                                    cover.caps.close || panel.caps.set_position));
  hidden(widgets[COVER_TILT_GROUP], !active || !(panel.tilt_known || panel.caps.open_tilt || panel.caps.stop_tilt ||
                                                 panel.caps.close_tilt || panel.caps.set_tilt_position));
  hidden(widgets[COVER_POSITION_TRACK], !active || !panel.caps.set_position);
  hidden(widgets[COVER_TILT_TRACK], !active || !panel.caps.set_tilt_position);
  const bool primary_caps[] = {cover.caps.open, cover.caps.stop, cover.caps.close};
  const bool tilt_caps[] = {panel.caps.open_tilt, panel.caps.stop_tilt, panel.caps.close_tilt};
  for (size_t index = 0; index < this->cover_buttons_.size(); index++) {
    hidden(this->cover_buttons_[index], active && !primary_caps[index]);
    hidden(this->cover_tilt_buttons_[index], !active || !tilt_caps[index]);
  }
  auto *icon = this->cover_panel_icons_[selected ? panel.device_class : 0];
  if (icon == nullptr)
    icon = this->cover_panel_icons_[0];
  for (auto *candidate : this->cover_panel_icons_)
    hidden(candidate, !active || candidate != icon);
  if (!active)
    return;
  const char *classes[] = {"Cover",  "Awning", "Blind", "Curtain", "Damper", "Door",
                           "Garage", "Gate",   "Shade", "Shutter", "Window"};
  const auto device_class = selected ? panel.device_class : 0;
  label(widgets[COVER_KIND], this->text_(31 + device_class, classes[device_class]));
  if (selected)
    label(widgets[COVER_TITLE], this->owner_.state().title.bytes.data());
  const char *states[] = {"Unknown", "Open", "Opening", "Closed", "Closing"};
  const auto state = selected ? static_cast<uint8_t>(cover.state) : 0;
  label(widgets[COVER_STATE], this->text_(16 + state, states[state]));
  if (!this->owner_.is_dragging() && !this->owner_.is_command_pending())
    this->cover_preview_valid_ = false;
  // Flex visibility is product policy. Settle it before using its actual rail geometry.
  lv_obj_update_layout(this->cover_root_);
  for (bool tilt : {false, true}) {
    const bool preview = this->cover_preview_valid_ && this->cover_preview_tilt_ == tilt;
    this->show_cover_axis_(tilt, preview || (selected && (tilt ? panel.tilt_known : cover.position_known)),
                           preview ? this->cover_preview_
                           : tilt  ? panel.tilt_position
                                   : cover.position);
    enabled(widgets[tilt ? COVER_TILT_TRACK : COVER_POSITION_TRACK],
            this->owner_.can_cover(tilt ? ControlOperation::CONTROL_OPERATION_COVER_SET_TILT_POSITION
                                        : ControlOperation::CONTROL_OPERATION_COVER_SET_POSITION));
  }
  const char *labels[] = {"Open tilt", "Stop tilt", "Close tilt"};
  for (size_t index = 0; index < this->cover_tilt_buttons_.size(); index++) {
    auto *button = this->cover_tilt_buttons_[index];
    auto *text = this->cover_tilt_labels_[index];
    label(text, this->text_(28 + index, labels[index]));
    const bool available = selected && this->owner_.can_cover(TILT_OPERATIONS[index]);
    enabled(button, available);
    const bool neutral = index == 1 || !available;
    const auto bg = lv_obj_get_style_bg_color(neutral ? this->off_button_ : this->on_button_, LV_PART_MAIN);
    const auto fg = lv_obj_get_style_text_color(neutral ? this->plus_label_ : this->minus_label_, LV_PART_MAIN);
    if (!lv_color_eq(lv_obj_get_style_bg_color(button, LV_PART_MAIN), bg))
      lv_obj_set_style_bg_color(button, bg, LV_PART_MAIN);
    if (!lv_color_eq(lv_obj_get_style_text_color(text, LV_PART_MAIN), fg))
      lv_obj_set_style_text_color(text, fg, LV_PART_MAIN);
  }
}

bool MaterialEntityControlComponent::is_cover_track_(lv_obj_t *target) const {
  return target == this->cover_panel_widgets_[COVER_POSITION_TRACK] ||
         target == this->cover_panel_widgets_[COVER_TILT_TRACK];
}

bool MaterialEntityControlComponent::begin_cover_panel_input_(lv_obj_t *target, lv_indev_t *indev, uint32_t pointer,
                                                              uint32_t now) {
  this->cover_preview_valid_ = false;
  const auto *selected_family = this->owner_.state().family_if<ControlCoverSnapshot>();
  if (selected_family == nullptr)
    return false;
  const auto &cover = *selected_family;
  if (this->is_cover_track_(target)) {
    const bool tilt = target == this->cover_panel_widgets_[COVER_TILT_TRACK];
    const uint8_t current = tilt ? cover.panel.tilt_position : cover.position;
    if (!this->owner_.begin_cover_target(pointer, tilt, current, now))
      return false;
    this->cover_preview_tilt_ = tilt;
    this->cover_preview_ = current;
    this->update_cover_input_(target, indev, pointer, now);
    return true;
  }
  for (size_t index = 0; index < this->cover_buttons_.size(); index++) {
    if (target == this->cover_buttons_[index])
      return this->owner_.begin_cover(
          pointer,
          static_cast<ControlOperation>(static_cast<uint8_t>(ControlOperation::CONTROL_OPERATION_COVER_OPEN) + index),
          now);
    if (target == this->cover_tilt_buttons_[index])
      return this->owner_.begin_cover(pointer, TILT_OPERATIONS[index], now);
  }
  return false;
}

void MaterialEntityControlComponent::update_cover_input_(lv_obj_t *target, lv_indev_t *indev, uint32_t pointer,
                                                         uint32_t now) {
  const bool tilt = target == this->cover_panel_widgets_[COVER_TILT_TRACK];
  auto *knob = this->cover_panel_widgets_[tilt ? COVER_TILT_KNOB : COVER_POSITION_KNOB];
  lv_area_t bounds;
  lv_point_t point;
  lv_obj_get_coords(target, &bounds);
  lv_indev_get_point(indev, &point);
  const int32_t width = lv_obj_get_width(knob);
  const int32_t travel = lv_area_get_width(&bounds) - width;
  if (travel <= 0)
    return;
  const uint8_t value = (std::clamp(point.x - bounds.x1 - width / 2, int32_t{0}, travel) * 100 + travel / 2) / travel;
  if (this->owner_.update_cover_target(pointer, value, now)) {
    this->cover_preview_valid_ = true;
    if (this->cover_preview_ != value) {
      this->cover_preview_ = value;
      this->show_cover_axis_(tilt, true, value);
    }
  }
}
#endif
}  // namespace esphome::lvgl_material
#endif
