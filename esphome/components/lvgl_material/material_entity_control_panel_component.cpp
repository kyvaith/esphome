#include "material_entity_control_component.h"
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
#ifdef USE_LVGL_MATERIAL_TIMER_PANEL
#include "esphome/components/time/real_time_clock.h"
#endif
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace esphome::lvgl_material {
namespace {
using Panel = ControlSnapshot::Panel;
using Op = ControlOperation;
enum Common : size_t { ROOT, TITLE, KIND, STATE };
enum Timer : size_t { REMAINING = 4, TIMER_STATUS, HOUR, MINUTE, SECOND };
enum Remote : size_t {
  STATUS = 4,
  POWER,
  POWER_LABEL,
  ACTIVITY,
  ACTIVITY_LABEL,
  ACTIVITY_VALUE,
  OPTIONS,
  OPTIONS_TITLE,
  BACK,
  APPLY,
  APPLY_LABEL
};
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
bool glyphs(lv_obj_t *widget, std::string_view text) {
  const auto *font = lv_obj_get_style_text_font(widget, LV_PART_MAIN);
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

void MaterialEntityControlComponent::set_timer_panel_profile(lv_obj_t *root, lv_obj_t *title, lv_obj_t *kind,
                                                             lv_obj_t *state, lv_obj_t *remaining, lv_obj_t *status,
                                                             lv_obj_t *hour, lv_obj_t *minute, lv_obj_t *second) {
  if (!this->setup_ready_)
    this->timer_panel_ = {root, title, kind, state, remaining, status, hour, minute, second};
}
void MaterialEntityControlComponent::set_timer_draft_button(size_t index, lv_obj_t *button) {
  if (!this->setup_ready_ && index < this->timer_draft_buttons_.size())
    this->timer_draft_buttons_[index] = button;
}
void MaterialEntityControlComponent::set_timer_panel_action(size_t index, lv_obj_t *button, lv_obj_t *text) {
  if (!this->setup_ready_ && index < this->timer_action_buttons_.size()) {
    this->timer_action_buttons_[index] = button;
    this->timer_action_labels_[index] = text;
  }
}
void MaterialEntityControlComponent::set_remote_panel_profile(lv_obj_t *root, lv_obj_t *title, lv_obj_t *kind,
                                                              lv_obj_t *state, lv_obj_t *status, lv_obj_t *power,
                                                              lv_obj_t *power_label, lv_obj_t *activity,
                                                              lv_obj_t *activity_label, lv_obj_t *activity_value,
                                                              lv_obj_t *options, lv_obj_t *options_title,
                                                              lv_obj_t *back, lv_obj_t *apply, lv_obj_t *apply_label) {
  if (!this->setup_ready_)
    this->remote_panel_ = {root,           title,          kind,    state,         status, power, power_label, activity,
                           activity_label, activity_value, options, options_title, back,   apply, apply_label};
}
void MaterialEntityControlComponent::set_remote_panel_option(size_t index, lv_obj_t *button, lv_obj_t *text) {
  if (!this->setup_ready_ && index < this->remote_option_buttons_.size()) {
    this->remote_option_buttons_[index] = button;
    this->remote_option_labels_[index] = text;
  }
}
void MaterialEntityControlComponent::set_power_panel_profile(lv_obj_t *root, lv_obj_t *title, lv_obj_t *kind,
                                                             lv_obj_t *state, lv_obj_t *status, lv_obj_t *power,
                                                             lv_obj_t *power_label, lv_obj_t *unknown_icon,
                                                             lv_obj_t *switch_icon, lv_obj_t *outlet_icon) {
  if (!this->setup_ready_)
    this->power_panel_ = {root, title, kind, state, status, power, power_label, unknown_icon, switch_icon, outlet_icon};
}
bool MaterialEntityControlComponent::setup_panel_profile_() {
  uint8_t mask = 0;
  const auto validate = [&](auto &widgets, uint8_t type) {
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
    hidden(widgets[ROOT], true);
    mask |= 1U << type;
    return true;
  };
  if (!validate(this->timer_panel_, 0) || !validate(this->remote_panel_, 1) || !validate(this->power_panel_, 2))
    return false;
  const auto is_label = [](lv_obj_t *widget) { return lv_obj_check_type(widget, &lv_label_class); };
  const auto button = [&](lv_obj_t *root, lv_obj_t *widget, lv_obj_t *text = nullptr) {
    if (widget == nullptr || !within(root, widget) || !lv_obj_check_type(widget, &lv_button_class) ||
        (text != nullptr && (!within(widget, text) || !is_label(text))))
      return false;
    if (lv_obj_check_type(widget, &lv_button_class))
      this->style_disabled_control_(widget);
    lv_obj_add_event_cb(widget, input_event_cb_, LV_EVENT_ALL, this);
    return true;
  };
  if (mask & 1) {
#ifdef USE_LVGL_MATERIAL_TIMER_PANEL
    if (this->panel_time_ == nullptr)
      return false;
#else
    return false;
#endif
    for (size_t index = 1; index < this->timer_panel_.size(); index++)
      if (!is_label(this->timer_panel_[index]))
        return false;
    for (auto *widget : this->timer_draft_buttons_)
      if (!button(this->timer_panel_[ROOT], widget))
        return false;
    for (size_t index = 0; index < this->timer_action_buttons_.size(); index++)
      if (!button(this->timer_panel_[ROOT], this->timer_action_buttons_[index], this->timer_action_labels_[index]))
        return false;
  }
  if (mask & 2) {
    auto &w = this->remote_panel_;
    for (auto index : {TITLE, KIND, STATE})
      if (!is_label(w[index]))
        return false;
    for (auto index : {STATUS, POWER_LABEL, ACTIVITY_LABEL, ACTIVITY_VALUE, OPTIONS_TITLE, APPLY_LABEL})
      if (!is_label(w[index]))
        return false;
    if (!button(w[ROOT], w[POWER], w[POWER_LABEL]) || !button(w[ROOT], w[ACTIVITY], w[ACTIVITY_LABEL]) ||
        !button(w[OPTIONS], w[BACK]) || !button(w[OPTIONS], w[APPLY], w[APPLY_LABEL]) ||
        !within(w[OPTIONS], w[OPTIONS_TITLE]))
      return false;
    for (size_t index = 0; index < this->remote_option_buttons_.size(); index++)
      if (!button(w[OPTIONS], this->remote_option_buttons_[index], this->remote_option_labels_[index]))
        return false;
    hidden(w[OPTIONS], true);
  }
  if (mask & 4) {
    auto &w = this->power_panel_;
    for (size_t index : {size_t{TITLE}, size_t{KIND}, size_t{STATE}, size_t{STATUS}, size_t{POWER_LABEL}})
      if (!is_label(w[index]))
        return false;
    if (!button(w[ROOT], w[POWER], w[POWER_LABEL]))
      return false;
  }
  return this->owner_.set_panel_types(mask);
}
bool MaterialEntityControlComponent::open_panel(uint32_t slot, std::string_view card_id, uint64_t revision) {
  return (this->owner_.bound_panel_types() != 0
#ifdef USE_LVGL_MATERIAL_EXTRA_CONTROLS
          || this->owner_.bound_extra_profiles() != 0
#endif
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
          || (this->owner_.bound_detail_profiles() & 2) != 0
#endif
          ) &&
         this->open_entity_(slot, card_id, revision, ControlEntityKind::CONTROL_ENTITY_PANEL);
}
bool MaterialEntityControlComponent::populate_panel_features(ArduinoJson::JsonObject root) {
  const uint8_t mask = this->setup_ready_ ? this->owner_.panel_types() : 0;
  root["v"] = 1;
  root["type"] = "entity_panel_features_v1";
  root["panel_version"] = mask ? 1 : 0;
  auto types = root["panel_types"].to<ArduinoJson::JsonArray>();
  const char *names[] = {"timer", "remote", "power"};
  for (size_t index = 0; index < 3; index++)
    if (mask & (1U << index))
      types.add(names[index]);
  return mask != 0;
}
bool MaterialEntityControlComponent::bind_panel_features(std::string_view payload, ArduinoJson::JsonObject root) {
  ControlText<32> channel;
  uint8_t types = 0;
  const bool accepted = this->setup_ready_ && !this->output_callback_active_ &&
                        this->codec_.parse_panel_bind(payload, channel, types) &&
                        this->owner_.accept_panel_features(channel, 1, types);
  root["v"] = 1;
  root["type"] = "entity_panel_features_v1";
  root["panel_version"] = accepted ? 1 : 0;
  root["channel"] = accepted ? channel.bytes.data() : "";
  root["accepted"] = accepted;
  auto values = root["panel_types"].to<ArduinoJson::JsonArray>();
  const char *names[] = {"timer", "remote", "power"};
  for (size_t index = 0; index < 3; index++)
    if (accepted && (types & (1U << index)))
      values.add(names[index]);
  return accepted;
}
bool MaterialEntityControlComponent::panel_activity_glyphs_available_() const {
  const auto *selected_family = this->owner_.state().family_if<ControlSnapshot::Panel>();
  if (selected_family == nullptr)
    return false;
  const auto &caps = selected_family->caps;
  for (size_t index = 0; index < caps.activity_count; index++)
    if (!glyphs(this->remote_option_labels_[index], caps.activities[index].view()) ||
        !glyphs(this->remote_panel_[ACTIVITY_VALUE], caps.activities[index].view()))
      return false;
  return true;
}
void MaterialEntityControlComponent::restore_panel_widgets_() {
  if (!this->panel_widgets_active_)
    return;
  const std::array<lv_obj_t *, 8> old{this->hit_,        this->value_,       this->unknown_value_, this->on_button_,
                                      this->off_button_, this->state_label_, this->status_label_,  this->title_};
  for (size_t index = 0; index < old.size(); index++)
    if (old[index] != nullptr)
      hidden(old[index], (this->panel_hidden_mask_ & (1U << index)) != 0);
  for (auto *root : {this->timer_panel_[ROOT], this->remote_panel_[ROOT], this->power_panel_[ROOT]})
    if (root != nullptr)
      hidden(root, true);
  this->panel_widgets_active_ = false;
  this->panel_modal_ = false;
  this->panel_selection_generation_ = 0;
}
ControlOperation MaterialEntityControlComponent::timer_action_(size_t index) const {
  if (index == 0 && this->timer_draft_ != 0)
    return Op::CONTROL_OPERATION_PANEL_DURATION;
  return static_cast<Op>(static_cast<uint8_t>(Op::CONTROL_OPERATION_PANEL_START) + index);
}
void MaterialEntityControlComponent::update_timer_clock_() {
  const auto *selected_family = this->owner_.state().family_if<ControlSnapshot::Panel>();
  if (selected_family == nullptr)
    return;
  const auto &panel = *selected_family;
  bool valid_clock = false;
  uint32_t utc = 0;
#ifdef USE_LVGL_MATERIAL_TIMER_PANEL
  if (this->panel_time_ != nullptr) {
    const auto now = this->panel_time_->utcnow();
    valid_clock = now.is_valid();
    utc = now.timestamp;
  }
#endif
  bool sample = false;
  uint32_t seconds = 0;
  char text[40];
  const bool known = panel_timer_remaining(panel, valid_clock, utc, seconds, sample);
  if (known)
    std::snprintf(text, sizeof(text), "%lu:%02lu:%02lu", static_cast<unsigned long>(seconds / 3600),
                  static_cast<unsigned long>((seconds / 60) % 60), static_cast<unsigned long>(seconds % 60));
  else
    std::snprintf(text, sizeof(text), "--:--:--");
  label(this->timer_panel_[REMAINING], text);
  const char *states[] = {"Unknown", "Idle", "Active", "Paused"};
  const char *phase = this->text_(panel.phase == 0 ? 16 : 21 + panel.phase, states[panel.phase]);
  const char *marker = known && panel.phase == 2 ? (sample         ? this->text_(31, "Sample")
                                                    : seconds == 0 ? this->text_(32, "Waiting for HA")
                                                                   : nullptr)
                                                 : nullptr;
  char state[128];
  if (marker != nullptr) {
    std::snprintf(state, sizeof(state), "%s · %s", phase, marker);
    phase = state;
  }
  label(this->timer_panel_[STATE], phase);
}
void MaterialEntityControlComponent::update_panel_static_() {
  const auto *selected_family = this->owner_.state().family_if<ControlSnapshot::Panel>();
  if (selected_family == nullptr)
    return;
  const auto &panel = *selected_family;
  const std::array<lv_obj_t *, 8> old{this->hit_,        this->value_,       this->unknown_value_, this->on_button_,
                                      this->off_button_, this->state_label_, this->status_label_,  this->title_};
  if (!this->panel_widgets_active_) {
    this->panel_hidden_mask_ = 0;
    for (size_t index = 0; index < old.size(); index++)
      if (old[index] != nullptr && lv_obj_has_flag(old[index], LV_OBJ_FLAG_HIDDEN))
        this->panel_hidden_mask_ |= 1U << index;
    this->panel_widgets_active_ = true;
  }
  if (this->panel_selection_generation_ != this->selection_generation_) {
    this->panel_selection_generation_ = this->selection_generation_;
    this->panel_modal_ = false;
    this->panel_option_ = Panel::UNKNOWN_INDEX;
    this->panel_option_generation_ = 0;
    this->timer_draft_ = panel.caps.initial_duration
                             ? panel.caps.initial_duration
                             : (panel.duration >= 1 && panel.duration <= 86400 ? panel.duration : 0);
  }
  for (auto *widget : old)
    if (widget != nullptr)
      hidden(widget, true);
  for (size_t index = 0; index < 3; index++) {
    auto *root = index == 0   ? this->timer_panel_[ROOT]
                 : index == 1 ? this->remote_panel_[ROOT]
                              : this->power_panel_[ROOT];
    if (root != nullptr)
      hidden(root, index != panel.type);
  }
  auto **w = panel.type == 0   ? this->timer_panel_.data()
             : panel.type == 1 ? this->remote_panel_.data()
                               : this->power_panel_.data();
  if (w[ROOT] == nullptr)
    return;
  label(w[TITLE], this->owner_.state().title.bytes.data());
  const uint8_t kind = panel.type == 0                ? 17
                       : panel.type == 1              ? 18
                       : panel.caps.device_class == 1 ? 19
                       : panel.caps.device_class == 2 ? 20
                                                      : 21;
  const char *kinds[] = {"Timer", "Remote", "Switch", "Outlet", "Power"};
  label(w[KIND], this->text_(kind, kinds[kind - 17]));
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
  char text[128];
  if (panel.type == 0) {
    this->update_timer_clock_();
    for (size_t index = 0; index < 3; index++) {
      const uint32_t divisor = index == 0 ? 3600 : index == 1 ? 60 : 1;
      if (this->timer_draft_ == 0)
        std::snprintf(text, sizeof(text), "--");
      else
        std::snprintf(text, sizeof(text), "%02lu",
                      static_cast<unsigned long>(index == 0 ? this->timer_draft_ / divisor
                                                            : (this->timer_draft_ / divisor) % 60));
      label(w[HOUR + index], text);
    }
    const bool edit = this->owner_.can_panel(Op::CONTROL_OPERATION_PANEL_DURATION, 1);
    for (size_t index = 0; index < this->timer_draft_buttons_.size(); index++)
      enabled(this->timer_draft_buttons_[index], edit && (index % 2 != 0 || this->timer_draft_ > 1));
    const char *actions[] = {"Start", "Resume", "Pause", "Cancel", "Restart"};
    const bool visible[] = {panel.phase == 1 && panel.caps.start, panel.phase == 3 && panel.caps.start,
                            panel.phase == 2 && panel.caps.pause,
                            (panel.phase == 2 || panel.phase == 3) && panel.caps.cancel,
                            (panel.phase == 2 || panel.phase == 3) && panel.caps.start};
    for (size_t index = 0; index < this->timer_action_buttons_.size(); index++) {
      auto *button = this->timer_action_buttons_[index];
      auto *caption = this->timer_action_labels_[index];
      hidden(button, !visible[index]);
      label(caption, this->text_(25 + index, actions[index]));
      enabled(button, this->owner_.can_panel(this->timer_action_(index), this->timer_draft_));
      color(button, caption, index == 0 || index == 1 || index == 4);
    }
  } else {
    label(w[STATE], this->text_(panel.power == 0   ? 16
                                : panel.power == 1 ? 15
                                                   : 14,
                                panel.power == 0   ? "Unknown"
                                : panel.power == 1 ? "Off"
                                                   : "On"));
    const uint32_t target = panel.power == 2 ? 0 : 1;
    hidden(w[POWER], !(panel.caps.turn_on || panel.caps.turn_off));
    label(w[POWER_LABEL], this->text_(target ? 0 : 1, target ? "Turn on" : "Turn off"));
    enabled(w[POWER], this->owner_.can_panel(Op::CONTROL_OPERATION_PANEL_POWER, target));
    color(w[POWER], w[POWER_LABEL], true);
    if (panel.type == 2)
      for (size_t index = 0; index < 3; index++)
        hidden(w[7 + index], index != panel.caps.device_class);
    else {
      const bool available_glyphs = this->panel_activity_glyphs_available_();
      const bool activity_ready = available_glyphs && this->owner_.can_panel(Op::CONTROL_OPERATION_PANEL_ACTIVITY, 0);
      hidden(w[ACTIVITY], panel.caps.activity_count == 0);
      hidden(w[ACTIVITY_VALUE], panel.caps.activity_count == 0);
      label(w[ACTIVITY_LABEL], this->text_(34, "Choose activity"));
      enabled(w[ACTIVITY], activity_ready);
      const char *activity = available_glyphs && panel.activity_index < panel.caps.activity_count
                                 ? panel.caps.activities[panel.activity_index].bytes.data()
                                 : this->text_(16, "Unknown");
      std::snprintf(text, sizeof(text), "%s: %s", this->text_(33, "Activity"), activity);
      label(w[ACTIVITY_VALUE], text);
      hidden(w[OPTIONS], !this->panel_modal_);
      label(w[OPTIONS_TITLE], this->text_(33, "Activity"));
      if (this->panel_option_generation_ != panel.state_generation)
        this->panel_option_ = Panel::UNKNOWN_INDEX;
      label(w[APPLY_LABEL], this->text_(35, "Apply"));
      enabled(w[APPLY],
              available_glyphs && this->owner_.can_panel(Op::CONTROL_OPERATION_PANEL_ACTIVITY, this->panel_option_));
      color(w[APPLY], w[APPLY_LABEL], true);
      enabled(w[BACK], this->owner_.is_view_ready() && !this->owner_.is_command_pending());
      for (size_t index = 0; index < this->remote_option_buttons_.size(); index++) {
        auto *button = this->remote_option_buttons_[index];
        auto *caption = this->remote_option_labels_[index];
        hidden(button, index >= panel.caps.activity_count);
        if (index >= panel.caps.activity_count)
          continue;
        label(caption, available_glyphs ? panel.caps.activities[index].bytes.data() : this->text_(38, "Unsupported"));
        enabled(button, activity_ready);
        color(button, caption, index == this->panel_option_);
      }
    }
  }
  this->update_status_();
  const auto status = static_cast<uint8_t>(this->status_);
  const char *fallback =
      this->status_texts_[status].empty() ? this->get_status_key() : this->status_texts_[status].c_str();
  const char *message = this->text_(status + 2, fallback);
  if (!panel.available)
    message = this->text_(37, "Unavailable");
  else if (this->owner_.last_result() == ControlResult::CONTROL_RESULT_SERVICE_COMPLETED)
    message = this->text_(36, "Command accepted by HA");
  label(w[panel.type == 0 ? size_t{TIMER_STATUS} : size_t{STATUS}], message);
  this->texts_pending_ = false;
}
bool MaterialEntityControlComponent::begin_panel_input_(lv_obj_t *target, uint32_t pointer, uint32_t now) {
  const auto *selected_family = this->owner_.state().family_if<ControlSnapshot::Panel>();
  if (selected_family == nullptr)
    return false;
  const auto &panel = *selected_family;
  if (panel.type == 0) {
    for (size_t index = 0; index < this->timer_draft_buttons_.size(); index++)
      if (target == this->timer_draft_buttons_[index])
        return this->owner_.can_panel(Op::CONTROL_OPERATION_PANEL_DURATION, 1) &&
               (index % 2 != 0 || this->timer_draft_ > 1) && this->owner_.begin_panel_navigation(pointer, now);
    for (size_t index = 0; index < this->timer_action_buttons_.size(); index++)
      if (target == this->timer_action_buttons_[index])
        return this->owner_.begin_panel(pointer, this->timer_action_(index), this->timer_draft_, now);
    return false;
  }
  auto **w = panel.type == 1 ? this->remote_panel_.data() : this->power_panel_.data();
  if (target == w[POWER])
    return !this->panel_modal_ &&
           this->owner_.begin_panel(pointer, Op::CONTROL_OPERATION_PANEL_POWER, panel.power == 2 ? 0 : 1, now);
  if (panel.type != 1)
    return false;
  if (target == w[BACK])
    return this->panel_modal_ && this->owner_.begin_panel_navigation(pointer, now);
  if (target == w[ACTIVITY])
    return !this->panel_modal_ && this->panel_activity_glyphs_available_() &&
           this->owner_.can_panel(Op::CONTROL_OPERATION_PANEL_ACTIVITY, 0) &&
           this->owner_.begin_panel_navigation(pointer, now);
  if (!this->panel_modal_ || !this->panel_activity_glyphs_available_())
    return false;
  if (target == w[APPLY])
    return this->panel_option_generation_ == panel.state_generation &&
           this->owner_.begin_panel(pointer, Op::CONTROL_OPERATION_PANEL_ACTIVITY, this->panel_option_, now);
  for (size_t index = 0; index < this->remote_option_buttons_.size(); index++)
    if (target == this->remote_option_buttons_[index])
      return this->owner_.can_panel(Op::CONTROL_OPERATION_PANEL_ACTIVITY, index) &&
             this->owner_.begin_panel_navigation(pointer, now);
  return false;
}
void MaterialEntityControlComponent::release_panel_input_(lv_obj_t *target, uint32_t pointer, uint32_t now) {
  const auto *selected_family = this->owner_.state().family_if<ControlSnapshot::Panel>();
  if (selected_family == nullptr)
    return;
  const auto &panel = *selected_family;
  if (panel.type == 0) {
    for (size_t index = 0; index < this->timer_draft_buttons_.size(); index++)
      if (target == this->timer_draft_buttons_[index]) {
        if (this->owner_.release_panel_navigation(pointer, now)) {
          const int32_t steps[] = {-3600, 3600, -60, 60, -1, 1};
          this->timer_draft_ = std::clamp<int32_t>(static_cast<int32_t>(this->timer_draft_) + steps[index], 1, 86400);
          this->update_static_();
        }
        return;
      }
  } else if (panel.type == 1) {
    auto &w = this->remote_panel_;
    if (target == w[ACTIVITY] || target == w[BACK]) {
      if (this->owner_.release_panel_navigation(pointer, now)) {
        this->panel_modal_ = target == w[ACTIVITY];
        this->panel_option_ = Panel::UNKNOWN_INDEX;
        this->panel_option_generation_ = 0;
        this->update_static_();
      }
      return;
    }
    for (size_t index = 0; index < this->remote_option_buttons_.size(); index++)
      if (target == this->remote_option_buttons_[index]) {
        if (this->owner_.release_panel_navigation(pointer, now)) {
          this->panel_option_ = index;
          this->panel_option_generation_ = panel.state_generation;
          this->update_static_();
        }
        return;
      }
  }
  if (this->owner_.release_panel(pointer, now))
    this->panel_modal_ = false;
}
}  // namespace esphome::lvgl_material
#endif
