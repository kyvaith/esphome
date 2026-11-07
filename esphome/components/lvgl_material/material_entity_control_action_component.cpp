#include "material_entity_control_component.h"
#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS
#include "material_action_timestamp.h"
#include "esphome/core/log.h"
#include <cstdio>
#include <cstring>
#include <limits>

namespace esphome::lvgl_material {
namespace {
using Action = ControlActionSnapshot;
using Op = ControlOperation;
enum Common : size_t { ROOT, TITLE, KIND, PHASE, LAST, CURRENT, POLICY, STATUS };
void hidden(lv_obj_t *widget, bool value) {
  if (widget == nullptr || lv_obj_has_flag(widget, LV_OBJ_FLAG_HIDDEN) == value)
    return;
  if (value)
    lv_obj_add_flag(widget, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_remove_flag(widget, LV_OBJ_FLAG_HIDDEN);
}
void enabled(lv_obj_t *widget, bool value) {
  if (lv_obj_has_state(widget, LV_STATE_DISABLED) == !value)
    return;
  if (value)
    lv_obj_remove_state(widget, LV_STATE_DISABLED);
  else
    lv_obj_add_state(widget, LV_STATE_DISABLED);
}
bool within(lv_obj_t *root, lv_obj_t *widget) {
  for (; widget != nullptr; widget = lv_obj_get_parent(widget))
    if (widget == root)
      return true;
  return false;
}
Op operation(uint8_t index) {
  return static_cast<Op>(static_cast<unsigned>(Op::CONTROL_OPERATION_ACTION_PRESS) + index);
}
}  // namespace

void MaterialEntityControlComponent::set_action_profile(lv_obj_t *r, lv_obj_t *t, lv_obj_t *k, lv_obj_t *p, lv_obj_t *l,
                                                        lv_obj_t *c, lv_obj_t *policy, lv_obj_t *s) {
  if (!this->setup_ready_)
    this->action_common_ = {r, t, k, p, l, c, policy, s};
}
void MaterialEntityControlComponent::set_action_icon(size_t index, lv_obj_t *widget) {
  if (!this->setup_ready_ && index < this->action_icons_.size())
    this->action_icons_[index] = widget;
}
void MaterialEntityControlComponent::set_action_button(size_t index, lv_obj_t *button, lv_obj_t *label) {
  if (!this->setup_ready_ && index < this->action_buttons_.size()) {
    this->action_buttons_[index] = button;
    this->action_labels_[index] = label;
  }
}
void MaterialEntityControlComponent::set_action_enabled(bool enabled) {
  if (!this->setup_ready_)
    this->action_enabled_ = enabled;
}

#ifdef USE_LVGL_MATERIAL_TILE_EVENT_CONTROLS
void MaterialEntityControlComponent::set_tile_event_enabled(bool enabled) {
  if (!this->setup_ready_)
    this->tile_event_enabled_ = enabled;
}
#endif

bool MaterialEntityControlComponent::setup_action_profile_() {
#ifdef USE_LVGL_MATERIAL_TILE_EVENT_CONTROLS
  if (this->tile_event_enabled_ && !this->action_enabled_)
    return false;
#endif
  auto *root = this->action_common_[ROOT];
  if (root == nullptr)
    return !this->action_enabled_;
  if (!this->number_profile_ || lv_obj_get_parent(root) != this->root_)
    return false;
  for (size_t index = 1; index < this->action_common_.size(); index++)
    if (!within(root, this->action_common_[index]) || !lv_obj_check_type(this->action_common_[index], &lv_label_class))
      return false;
  for (auto *icon : this->action_icons_)
    if (!within(root, icon))
      return false;
  for (size_t index = 0; index < this->action_buttons_.size(); index++) {
    auto *button = this->action_buttons_[index];
    if (!within(root, button) || !within(button, this->action_labels_[index]) ||
        !lv_obj_check_type(this->action_labels_[index], &lv_label_class))
      return false;
    lv_obj_add_event_cb(button, input_event_cb_, LV_EVENT_ALL, this);
    this->style_disabled_control_(button);
  }
  hidden(root, true);
  if (this->action_enabled_)
    this->extra_configured_ |= 24U;
#ifdef USE_LVGL_MATERIAL_TILE_EVENT_CONTROLS
  if (this->tile_event_enabled_)
    this->extra_configured_ |= 256U;
#endif
  return this->owner_.set_extra_profiles(this->extra_configured_)
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
         && this->owner_.set_action_parameters(this->action_parameters_configured_)
#endif
      ;
}

#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS

void MaterialEntityControlComponent::set_action_parameters_enabled(bool enabled) {
  if (!this->setup_ready_)
    this->action_parameters_configured_ = enabled;
}
#endif
bool MaterialEntityControlComponent::populate_action_parameters_features(ArduinoJson::JsonObject root) {
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
  const bool enabled = this->setup_ready_ && this->owner_.action_parameters_enabled();
#else
  const bool enabled = false;
#endif
  root["v"] = 1;
  root["type"] = "entity_action_parameters_v1";
  root["parameters_version"] = enabled ? 1 : 0;
  return enabled;
}
bool MaterialEntityControlComponent::bind_action_parameters_features(std::string_view payload,
                                                                     ArduinoJson::JsonObject root) {
  ControlText<32> channel;
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
  const bool accepted = this->setup_ready_ && !this->output_callback_active_ &&
                        this->codec_.parse_action_parameters_bind(payload, channel) &&
                        this->owner_.accept_action_parameters(channel);
#else
  (void) payload;
  const bool accepted = false;
#endif
  root["v"] = 1;
  root["type"] = "entity_action_parameters_v1";
  root["parameters_version"] = accepted ? 1 : 0;
  root["channel"] = accepted ? channel.bytes.data() : "";
  root["accepted"] = accepted;
  return accepted;
}
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
bool MaterialEntityControlComponent::format_action_parameter_policy_(const Action &action, char *text,
                                                                     size_t capacity) const {
  const auto value = this->owner_.action_parameter_value();
  const int length =
      std::snprintf(text, capacity, "%s\n%s%s%s", action.parameter_label.bytes.data(),
                    this->owner_.action_parameter_is_default() ? this->text_(61, "Default")
                    : value <= 1                               ? this->text_(62, "Selected")
                                                               : this->text_(60, "Choose Yes or No"),
                    value <= 1 ? ": " : "", value <= 1 ? this->text_(58 + value, value ? "Yes" : "No") : "");
  if (length < 0 || static_cast<size_t>(length) >= capacity) {
    if (capacity >= 3)
      std::strcpy(text, "--");
    return false;
  }
  return true;
}
bool MaterialEntityControlComponent::apply_action_parameters_layout_(bool parameters) {
  if (parameters == this->action_parameters_layout_active_)
    return true;
  auto *parent = lv_obj_get_parent(this->action_buttons_[0]);
  if (parent == nullptr || lv_obj_get_style_layout(parent, LV_PART_MAIN) != LV_LAYOUT_NONE)
    return false;
  lv_obj_update_layout(parent);
  if (!this->action_parameters_layout_saved_) {
    for (size_t i = 0; i < this->action_buttons_.size(); i++) {
      auto *button = this->action_buttons_[i];
      const auto align = lv_obj_get_style_align(button, LV_PART_MAIN);
      if (lv_obj_get_parent(button) != parent || (align != LV_ALIGN_TOP_LEFT && align != LV_ALIGN_DEFAULT))
        return false;
      this->action_original_y_[i] = lv_obj_get_style_y(button, LV_PART_MAIN);
      if (LV_COORD_IS_PCT(this->action_original_y_[i]) || this->action_original_y_[i] == LV_SIZE_CONTENT)
        return false;
    }
    if (this->action_original_y_[1] != this->action_original_y_[2])
      return false;
    this->action_parameters_layout_saved_ = true;
  }
  std::array<lv_area_t, 3> areas;
  lv_area_t content;
  lv_obj_get_content_coords(parent, &content);
  for (size_t i = 0; i < this->action_buttons_.size(); i++) {
    auto *button = this->action_buttons_[i];
    lv_obj_get_coords(button, &areas[i]);
    const int32_t y = parameters ? this->action_original_y_[i == 0 ? 1 : 0] : this->action_original_y_[i];
    const int32_t delta = y - lv_obj_get_style_y(button, LV_PART_MAIN);
    areas[i].y1 += delta;
    areas[i].y2 += delta;
    if (areas[i].x1 < content.x1 || areas[i].x2 > content.x2 || areas[i].y1 < content.y1 || areas[i].y2 > content.y2)
      return false;
    for (size_t j = 0; j < i; j++)
      if (areas[i].x1 <= areas[j].x2 && areas[j].x1 <= areas[i].x2 && areas[i].y1 <= areas[j].y2 &&
          areas[j].y1 <= areas[i].y2)
        return false;
  }
  for (size_t i = 0; i < this->action_buttons_.size(); i++)
    lv_obj_set_style_y(this->action_buttons_[i],
                       parameters ? this->action_original_y_[i == 0 ? 1 : 0] : this->action_original_y_[i],
                       LV_PART_MAIN);
  this->action_parameters_layout_active_ = parameters;
  lv_obj_update_layout(parent);
  return true;
}

#endif

void MaterialEntityControlComponent::restore_action_widgets_() {
  if (!this->action_widgets_active_)
    return;
  this->action_ready_ = false;
  if (!this->owner_.has_selection()) {
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
    // Keep the last visible composition intact until navigation has retired its preview.
    if (!lv_obj_is_visible(this->action_common_[ROOT]))
      this->apply_action_parameters_layout_(false);
#endif

    for (auto *button : this->action_buttons_)
      enabled(button, false);
    for (auto index : {PHASE, LAST, CURRENT, POLICY})
      this->extra_text_(this->action_common_[index], index == PHASE ? "--" : "");
    this->extra_text_(this->action_common_[STATUS], this->text_(15, "Unknown"));
    return;
  }
  lv_obj_t *base[] = {this->title_,     this->hit_,        this->value_,       this->unknown_value_,
                      this->on_button_, this->off_button_, this->state_label_, this->status_label_};
  for (size_t index = 0; index < std::size(base); index++)
    hidden(base[index], (this->action_hidden_mask_ & (1U << index)) != 0);
  hidden(this->action_common_[ROOT], true);
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
  this->apply_action_parameters_layout_(false);
#endif
  this->action_widgets_active_ = false;
}

void MaterialEntityControlComponent::update_action_static_() {
  const auto *action = this->owner_.state().family_if<Action>();
  if (!this->owner_.is_action() || action == nullptr || this->owner_.is_dragging())
    return;
  lv_obj_t *base[] = {this->title_,     this->hit_,        this->value_,       this->unknown_value_,
                      this->on_button_, this->off_button_, this->state_label_, this->status_label_};
  if (!this->action_widgets_active_) {
    this->action_hidden_mask_ = 0;
    for (size_t index = 0; index < std::size(base); index++)
      if (base[index] && lv_obj_has_flag(base[index], LV_OBJ_FLAG_HIDDEN))
        this->action_hidden_mask_ |= 1U << index;
    this->action_widgets_active_ = true;
  }
  for (auto *widget : base)
    hidden(widget, true);
  bool parameter_fit = true;
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
  const bool parameters = action->parameters_version == 1;
  parameter_fit = this->apply_action_parameters_layout_(parameters);
#endif
  hidden(this->action_common_[ROOT], false);
  const bool tile_event = action->type == Action::TILE_EVENT_TYPE;
  const uint8_t icon = action->type == 2 ? 1 : action->type == 3 ? 2 : action->type == 4 ? 3 : 0;
  for (size_t index = 0; index < this->action_icons_.size(); index++)
    hidden(this->action_icons_[index], index != icon);
  lv_obj_update_layout(this->root_);
  bool complete = this->extra_text_(this->action_common_[TITLE], this->owner_.state().title.bytes.data());
  constexpr const char *KINDS[] = {"Action", "Scene", "Script", "Automation"};
  complete &= this->extra_text_(this->action_common_[KIND], this->text_(18 + icon, tile_event ? "Event" : KINDS[icon]));
  const char *phase = "";
  if (!action->available())
    phase = this->text_(action->source == 1 ? 15 : 16, action->source == 1 ? "Unknown" : "Unavailable");
  else if (action->phase != 0) {
    constexpr const char *PHASES[] = {"Idle", "Running", "Enabled", "Disabled"};
    phase = this->text_(21 + action->phase, PHASES[action->phase - 1]);
  }
  complete &= this->extra_text_(this->action_common_[PHASE], phase);
  // Each provider string is bounded to160 bytes; the longest composition has
  // two complete strings plus a separator. Never truncate policy or history.
  char text[322];
  action_timestamp::Parts last_parts{};
  if (action->last_used_status == 0 && action_timestamp::parse(action->last_used.view(), last_parts)) {
    constexpr const char *MONTHS[] = {"January", "February", "March", "April", "May", "June", "July",
                                    "August", "September", "October", "November", "December"};
    if (!action_timestamp::format(last_parts, this->text_(26, "Last used"),
                                  this->text_(45 + last_parts.month, MONTHS[last_parts.month - 1]), text, sizeof(text))) {
      std::strcpy(text, "--");
      complete = false;
    }
  } else {
    constexpr const char *LAST_STATUS[] = {"Unknown", "Invalid timestamp", "Timestamp too long"};
    const uint8_t status = action->last_used_status == 0 ? 2 : action->last_used_status;
    std::snprintf(text, sizeof(text), "%s\n%s", this->text_(26, "Last used"),
                  this->text_(26 + status, LAST_STATUS[status - 1]));
  }
  complete &= this->extra_text_(this->action_common_[LAST], text, nullptr, nullptr, true);
  if (action->current_known)
    std::snprintf(text, sizeof(text), "%s: %u", this->text_(30, "Current"), static_cast<unsigned>(action->current));
  else
    text[0] = 0;
  complete &= this->extra_text_(this->action_common_[CURRENT], text);
  hidden(this->action_common_[CURRENT], !action->current_known);
  text[0] = 0;
  if (action->type == 4)
    std::snprintf(text, sizeof(text), "%s\n%s",
                  this->text_(38 + action->skip_condition,
                              action->skip_condition ? "Run without conditions" : "Run with conditions"),
                  this->text_(40 + action->stop_actions, action->stop_actions ? "Disable requests stopping actions"
                                                                              : "Disable does not stop actions"));
  else if (action->type == 3 && action->run_blocked != 0) {
    constexpr const char *BLOCKED[] = {"Needs parameters", "Metadata unavailable", "Service unavailable"};
    std::snprintf(text, sizeof(text), "%s", this->text_(41 + action->run_blocked, BLOCKED[action->run_blocked - 1]));
  }
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
  if (parameters && action->parameter_status == 0) {
    parameter_fit &= this->format_action_parameter_policy_(*action, text, sizeof(text));
    parameter_fit &= this->extra_text_(this->action_common_[POLICY], text, nullptr, nullptr, true);
  } else

#endif
    complete &= this->extra_text_(this->action_common_[POLICY], text, nullptr, nullptr, true);
  hidden(this->action_common_[POLICY], text[0] == 0);
  this->action_operations_.fill(255);
  if (action->type <= 2 || tile_event)
    this->action_operations_[0] = action->type == 2 ? 1 : 0;
  else if (action->type == 3)
    this->action_operations_[0] = action->phase == 2 ? 3 : 2;
  else {
    this->action_operations_[0] = 6;
    if (action->phase == 4 || !action->available())
      this->action_operations_[1] = 4;
    if (action->phase == 3 || action->stop_actions == 1 || !action->available())
      this->action_operations_[2] = 5;
  }
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
  if (parameters && action->parameter_status == 0) {
    this->action_operations_[1] = 253;  // Local No and Yes, never wire operations.
    this->action_operations_[2] = 254;
  }
#endif
  constexpr const char *LABELS[] = {"Press", "Activate", "Run", "Stop", "Enable", "Disable", "Run"};
  const auto active_bg = configured_style_color_(this->on_button_, LV_STYLE_BG_COLOR),
             active_fg = configured_style_color_(this->minus_label_, LV_STYLE_TEXT_COLOR),
             neutral_bg = configured_style_color_(this->off_button_, LV_STYLE_BG_COLOR),
             neutral_fg = configured_style_color_(this->plus_label_, LV_STYLE_TEXT_COLOR);
  for (size_t index = 0; index < this->action_buttons_.size(); index++) {
    auto *button = this->action_buttons_[index];
    const auto op = this->action_operations_[index];
    const bool choice = op == 253 || op == 254;
    const bool supported = choice || (op < 7 && (action->operations & (1U << op)) != 0);
    hidden(button, !supported);
    if (!supported) {
      enabled(button, false);
      continue;
    }
    uint8_t key = choice ? 58 + (op == 254) : op == 6 ? 33 : 31 + op;
    if (op == 5 && action->stop_actions == 1)
      key = 37;
    const bool caption_ok =
        this->extra_text_(this->action_labels_[index], this->text_(key, choice       ? (op == 254 ? "Yes" : "No")
                                                                        : tile_event ? "Send event"
                                                                        : key == 37  ? "Stop and disable"
                                                                                     : LABELS[op]));
    if (choice)
      parameter_fit &= caption_ok;
    else
      complete &= caption_ok;
    bool active = index == 0;
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
    if (parameters && index == 0 && op == 2)
      active &= complete && parameter_fit && this->owner_.can_action(operation(op));
    active |= choice && this->owner_.action_parameter_value() == uint8_t(op == 254);
#endif
    const auto bg = active ? active_bg : neutral_bg, fg = active ? active_fg : neutral_fg;
    if (!lv_color_eq(configured_style_color_(button, LV_STYLE_BG_COLOR), bg))
      lv_obj_set_style_bg_color(button, bg, LV_PART_MAIN);
    if (!lv_color_eq(configured_style_color_(button, LV_STYLE_TEXT_COLOR), fg))
      lv_obj_set_style_text_color(button, fg, LV_PART_MAIN);
    for (uint32_t child = 0; child < lv_obj_get_child_count(button); child++) {
      auto *object = lv_obj_get_child(button, child);
      if (!lv_color_eq(configured_style_color_(object, LV_STYLE_TEXT_COLOR), fg))
        lv_obj_set_style_text_color(object, fg, LV_PART_MAIN);
    }
    if (!lv_color_eq(configured_style_color_(this->action_labels_[index], LV_STYLE_TEXT_COLOR), fg))
      lv_obj_set_style_text_color(this->action_labels_[index], fg, LV_PART_MAIN);
  }
  this->update_status_();
  const char *status = this->text_(static_cast<uint8_t>(this->status_) + 2, this->get_status_key());
  if (this->owner_.last_result() == ControlResult::CONTROL_RESULT_SERVICE_COMPLETED)
    status = this->text_(14, "Home Assistant service completed");
  else if (this->owner_.last_result() == ControlResult::CONTROL_RESULT_SERVICE_DISPATCHED)
    status = this->text_(45, tile_event ? "Event sent" : "Request sent to HA");
  else if (!action->available())
    status = this->text_(action->source == 1 ? 15 : 16, action->source == 1 ? "Unknown" : "Unavailable");
  complete &=
      this->extra_text_(this->action_common_[STATUS],
                        complete && parameter_fit ? status : this->text_(17, "Full observation cannot be displayed"));
  this->action_ready_ = complete;
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
  const auto rendered_value = this->owner_.action_parameter_value();
  const bool rendered_default = this->owner_.action_parameter_is_default();
  this->owner_.set_action_parameter_ready(complete && parameter_fit);
  if (parameters && action->parameter_status == 0 &&
      (rendered_value != this->owner_.action_parameter_value() ||
       rendered_default != this->owner_.action_parameter_is_default())) {
    // A readiness loss resets the draft. Do not leave a stale consequential caption visible.
    this->format_action_parameter_policy_(*action, text, sizeof(text));
    this->extra_text_(this->action_common_[POLICY], text, nullptr, nullptr, true);
    for (size_t index = 1; index < this->action_buttons_.size(); index++) {
      auto *button = this->action_buttons_[index];
      const bool selected = this->owner_.action_parameter_value() == index - 1;
      const auto bg = selected ? active_bg : neutral_bg, fg = selected ? active_fg : neutral_fg;
      lv_obj_set_style_bg_color(button, bg, LV_PART_MAIN);
      lv_obj_set_style_text_color(button, fg, LV_PART_MAIN);
      for (uint32_t child = 0; child < lv_obj_get_child_count(button); child++)
        lv_obj_set_style_text_color(lv_obj_get_child(button, child), fg, LV_PART_MAIN);
    }
  }
#endif

  for (size_t index = 0; index < this->action_buttons_.size(); index++) {
    const auto op = this->action_operations_[index];
    bool allowed = op < 7 && this->owner_.can_action(operation(op));
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
    if (parameters) {
      if (op == 253 || op == 254)
        allowed = parameter_fit && this->owner_.can_choose_action_parameter();
      else if (op == 2)
        allowed &= parameter_fit;
    }
#endif
    enabled(this->action_buttons_[index], complete && allowed);
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
    if (parameters && index == 0 && op == 2) {
      // A required choice must be visibly unavailable, including after readiness changes above.
      const auto bg = complete && allowed ? active_bg : neutral_bg;
      const auto fg = complete && allowed ? active_fg : neutral_fg;
      auto *button = this->action_buttons_[index];
      if (!lv_color_eq(configured_style_color_(button, LV_STYLE_BG_COLOR), bg))
        lv_obj_set_style_bg_color(button, bg, LV_PART_MAIN);
      if (!lv_color_eq(configured_style_color_(button, LV_STYLE_TEXT_COLOR), fg))
        lv_obj_set_style_text_color(button, fg, LV_PART_MAIN);
      for (uint32_t child = 0; child < lv_obj_get_child_count(button); child++) {
        auto *object = lv_obj_get_child(button, child);
        if (!lv_color_eq(configured_style_color_(object, LV_STYLE_TEXT_COLOR), fg))
          lv_obj_set_style_text_color(object, fg, LV_PART_MAIN);
      }
    }
#endif
  }
}

void MaterialEntityControlComponent::action_input_event_(lv_event_code_t code, lv_obj_t *target, lv_indev_t *indev,
                                                         uint32_t now) {
  size_t index = 0;
  while (index < this->action_buttons_.size() && this->action_buttons_[index] != target)
    index++;
  if (index == this->action_buttons_.size())
    return;
  if (code == LV_EVENT_PRESSED) {
    if (this->captured_target_ || !this->action_ready_ || !indev || lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER ||
        !this->is_presented_() || !lv_obj_is_visible(target) || lv_obj_has_state(target, LV_STATE_DISABLED) ||
        this->pointer_sequence_ == std::numeric_limits<uint32_t>::max())
      return;
#ifdef USE_LVGL_NAVIGATION_ADMISSION_GUARD
    if (this->view_lifecycle_.native_power_input_active())
      return;
#endif
    const auto pointer = ++this->pointer_sequence_;
    const auto op = this->action_operations_[index];
    const bool admitted =
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
        (op == 253 || op == 254) ? this->owner_.begin_action_parameter(pointer, op == 254, now) :
#endif
                                 this->owner_.begin_action(pointer, operation(op), now);
    if (!admitted)
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
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
        if (this->action_operations_[index] == 253 || this->action_operations_[index] == 254)
      this->owner_.release_action_parameter(this->pointer_sequence_, now);
    else
#endif
      this->owner_.release_action(this->pointer_sequence_, now);
    this->update_static_();
  }
  this->process_events_();
}

void MaterialEntityControlComponent::log_action_view(const char *label) const {
#ifdef USE_LVGL_MATERIAL_SCALAR_PERFORMANCE_METRICS
  const auto *action = this->owner_.is_action() ? this->owner_.state().family_if<Action>() : nullptr;
  ESP_LOGI("scalar.performance",
           "%s action_active=%u action_retained=%u action_ready=%u action_state_seq=%u action_generation=%u "
           "action_type=%u action_source=%u action_phase=%u action_result=%u action_reason=%u",
           label, unsigned(action != nullptr), unsigned(this->action_widgets_active_), unsigned(this->action_ready_),
           unsigned(action ? this->owner_.state().state_seq : 0), unsigned(action ? action->state_generation : 0),
           unsigned(action ? action->type : 255), unsigned(action ? action->source : 255),
           unsigned(action ? action->phase : 255), unsigned(this->owner_.last_result()),
           unsigned(this->owner_.last_reason()));
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
  ESP_LOGI("scalar.performance",
           "%s action_parameters_version=%u action_parameter_status=%u action_parameter_ready=%u "
           "action_parameter_value=%u action_parameter_default=%u action_parameter_revision=%u",
           label, unsigned(action ? action->parameters_version : 0), unsigned(action ? action->parameter_status : 255),
           unsigned(this->owner_.action_parameter_ready()), unsigned(this->owner_.action_parameter_value()),
           unsigned(this->owner_.action_parameter_is_default()), unsigned(this->owner_.action_parameter_revision()));
#endif
#else
  (void) label;
#endif
}
}  // namespace esphome::lvgl_material
#endif
