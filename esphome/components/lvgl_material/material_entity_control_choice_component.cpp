#include "material_entity_control_component.h"

#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR

#include <cstdio>
#include <cstring>

namespace esphome::lvgl_material {
bool MaterialEntityControlComponent::populate_choice_features(ArduinoJson::JsonObject root) {
  root.clear();
  root["v"] = 1;
  root["type"] = "entity_choice_features_v1";
  root["empty_choice_version"] =
      this->setup_ready_ && !this->is_failed() && this->owner_.is_enabled() && this->owner_.choice_empty_supported()
          ? 1
          : 0;
  return root.size() == 3;
}
bool MaterialEntityControlComponent::bind_choice_features(std::string_view channel, uint32_t version,
                                                          ArduinoJson::JsonObject root) {
  ControlText<32> parsed;
  const bool accepted = this->setup_ready_ && !this->output_callback_active_ && parsed.assign(channel) &&
                        this->owner_.accept_choice_features(parsed, version);
  root.clear();
  root["v"] = 1;
  root["type"] = "entity_choice_features_v1";
  root["accepted"] = accepted;
  root["channel"] = accepted ? this->owner_.bound_channel().bytes.data() : "";
  root["empty_choice_version"] = accepted ? 1 : 0;
  return root.size() == 5;
}
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
  for (auto *parent = lv_obj_get_parent(widget); parent != nullptr; parent = lv_obj_get_parent(parent)) {
    if (parent == root)
      return true;
  }
  return false;
}
}  // namespace

void MaterialEntityControlComponent::set_choice_profile(lv_obj_t *root, lv_obj_t *apply, lv_obj_t *apply_label,
                                                        lv_obj_t *current_label, lv_obj_t *status_label,
                                                        lv_obj_t *neutral_reference) {
  if (this->setup_ready_)
    return;
  this->choice_root_ = root;
  this->choice_apply_ = apply;
  this->choice_apply_label_ = apply_label;
  this->choice_current_label_ = current_label;
  this->choice_status_label_ = status_label;
  this->choice_neutral_reference_ = neutral_reference;
}

void MaterialEntityControlComponent::set_choice_option(size_t index, lv_obj_t *button, lv_obj_t *text) {
  if (this->setup_ready_ || index >= this->choice_buttons_.size())
    return;
  this->choice_buttons_[index] = button;
  this->choice_labels_[index] = text;
}

bool MaterialEntityControlComponent::setup_choice_profile_() {
  if (this->choice_root_ == nullptr)
    return true;
  if (!this->number_profile_ || this->choice_apply_ == nullptr || this->choice_apply_label_ == nullptr ||
      this->choice_current_label_ == nullptr || this->choice_status_label_ == nullptr ||
      this->choice_neutral_reference_ == nullptr || this->choice_neutral_reference_ == this->choice_apply_ ||
      !lv_obj_check_type(this->choice_apply_, &lv_button_class) ||
      !lv_obj_check_type(this->choice_apply_label_, &lv_label_class) ||
      !lv_obj_check_type(this->choice_current_label_, &lv_label_class) ||
      !lv_obj_check_type(this->choice_status_label_, &lv_label_class) || !within(this->root_, this->choice_root_) ||
      !within(this->choice_root_, this->choice_apply_) || !within(this->root_, this->choice_neutral_reference_) ||
      !within(this->choice_apply_, this->choice_apply_label_) ||
      !within(this->choice_root_, this->choice_current_label_) ||
      !within(this->choice_root_, this->choice_status_label_))
    return false;
  for (size_t index = 0; index < this->choice_buttons_.size(); index++) {
    auto *button = this->choice_buttons_[index];
    auto *text = this->choice_labels_[index];
    if (button == nullptr || text == nullptr || button == this->choice_apply_ ||
        !lv_obj_check_type(button, &lv_button_class) || !lv_obj_check_type(text, &lv_label_class) ||
        !within(this->choice_root_, button) || !within(button, text))
      return false;
    for (size_t previous = 0; previous < index; previous++) {
      if (button == this->choice_buttons_[previous] || text == this->choice_labels_[previous])
        return false;
    }
  }
  for (auto *button : this->choice_buttons_)
    lv_obj_add_event_cb(button, input_event_cb_, LV_EVENT_ALL, this);
  lv_obj_add_event_cb(this->choice_apply_, input_event_cb_, LV_EVENT_ALL, this);
  hidden(this->choice_root_, true);
  return this->owner_.set_choice_enabled(true);
}

bool MaterialEntityControlComponent::open_choice(uint32_t slot, std::string_view card_id, uint64_t revision) {
  return this->owner_.choice_enabled() &&
         this->open_entity_(slot, card_id, revision, ControlEntityKind::CONTROL_ENTITY_CHOICE);
}

size_t MaterialEntityControlComponent::choice_target_index_(lv_obj_t *target) const {
  for (size_t index = 0; index < this->choice_buttons_.size(); index++) {
    if (this->choice_buttons_[index] == target)
      return index;
  }
  return this->choice_buttons_.size();
}

bool MaterialEntityControlComponent::choice_glyphs_available_() const {
  const auto *selected_family = this->owner_.state().family_if<ControlSnapshot::Choice>();
  if (selected_family == nullptr)
    return false;
  const auto &choice = *selected_family;
  for (size_t index = 0; index < choice.count; index++) {
    const auto *font = lv_obj_get_style_text_font(this->choice_labels_[index], LV_PART_MAIN);
    const auto text = choice.options[index].view();
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
  }
  return true;
}

void MaterialEntityControlComponent::restore_choice_widgets_() {
  if (!this->choice_widgets_active_)
    return;
  std::array<lv_obj_t *, 7> widgets{this->hit_,        this->value_,       this->unknown_value_, this->on_button_,
                                    this->off_button_, this->state_label_, this->status_label_};
  for (size_t index = 0; index < widgets.size(); index++)
    hidden(widgets[index], (this->choice_hidden_mask_ & (1U << index)) != 0);
  hidden(this->choice_root_, true);
  this->choice_widgets_active_ = false;
}

void MaterialEntityControlComponent::update_choice_static_() {
  const std::array<lv_obj_t *, 7> widgets{this->hit_,        this->value_,       this->unknown_value_, this->on_button_,
                                          this->off_button_, this->state_label_, this->status_label_};
  if (!this->choice_widgets_active_) {
    this->choice_hidden_mask_ = 0;
    for (size_t index = 0; index < widgets.size(); index++) {
      if (widgets[index] != nullptr && lv_obj_has_flag(widgets[index], LV_OBJ_FLAG_HIDDEN))
        this->choice_hidden_mask_ |= 1U << index;
    }
    this->choice_widgets_active_ = true;
  }
  for (auto *widget : widgets)
    hidden(widget, true);
  hidden(this->choice_root_, false);
  const bool selected = this->owner_.has_selection();
  const auto *selected_family = this->owner_.state().family_if<ControlSnapshot::Choice>();
  if (selected_family == nullptr)
    return;
  const auto &choice = *selected_family;
  const bool glyphs = selected && this->choice_glyphs_available_();
  const auto active_bg = configured_style_color_(this->choice_apply_, LV_STYLE_BG_COLOR);
  const auto active_fg = configured_style_color_(this->choice_apply_, LV_STYLE_TEXT_COLOR);
  const auto neutral_bg = configured_style_color_(this->choice_neutral_reference_, LV_STYLE_BG_COLOR);
  const auto neutral_fg = configured_style_color_(this->choice_neutral_reference_, LV_STYLE_TEXT_COLOR);
  if (selected)
    label(this->title_, this->owner_.state().title.bytes.data());
  for (size_t index = 0; index < this->choice_buttons_.size(); index++) {
    auto *button = this->choice_buttons_[index];
    hidden(button, !selected || index >= choice.count);
    if (!selected || index >= choice.count)
      continue;
    label(this->choice_labels_[index], choice.options[index].bytes.data());
    const bool candidate = this->owner_.choice_candidate_known() && index == this->owner_.choice_candidate();
    const auto background = candidate ? active_bg : neutral_bg;
    const auto foreground = candidate ? active_fg : neutral_fg;
    if (!lv_color_eq(lv_obj_get_style_bg_color(button, LV_PART_MAIN), background))
      lv_obj_set_style_bg_color(button, background, LV_PART_MAIN);
    if (!lv_color_eq(lv_obj_get_style_text_color(this->choice_labels_[index], LV_PART_MAIN), foreground))
      lv_obj_set_style_text_color(this->choice_labels_[index], foreground, LV_PART_MAIN);
    if (glyphs && this->owner_.can_choose(static_cast<uint8_t>(index)))
      lv_obj_remove_state(button, LV_STATE_DISABLED);
    else
      lv_obj_add_state(button, LV_STATE_DISABLED);
  }
  char current[196];
  const char *value = selected && choice.available ? (choice.index == ControlSnapshot::Choice::EMPTY_INDEX
                                                          ? this->text_(22, "No active option")
                                                          : choice.options[choice.index].bytes.data())
                                                   : this->text_(19, "Unknown");
  std::snprintf(current, sizeof(current), "%s: %s", this->text_(16, "Current"), value);
  label(this->choice_current_label_, current);
  label(this->choice_apply_label_, this->text_(17, "Apply"));
  if (glyphs && this->owner_.can_apply_choice())
    lv_obj_remove_state(this->choice_apply_, LV_STATE_DISABLED);
  else
    lv_obj_add_state(this->choice_apply_, LV_STATE_DISABLED);
  this->update_status_();
  if (selected && !choice.available && !this->owner_.is_command_pending())
    this->status_ = ControlUiStatus::CONTROL_UI_STATUS_WAITING;
  // A previous confirmed command describes the current value, not a new local
  // candidate. Keep pending/error feedback, but prompt Apply for a fresh choice.
  if (this->owner_.can_apply_choice() && (this->status_ == ControlUiStatus::CONTROL_UI_STATUS_STATE_MATCHES ||
                                          this->status_ == ControlUiStatus::CONTROL_UI_STATUS_ALREADY_MATCHED))
    this->status_ = ControlUiStatus::CONTROL_UI_STATUS_READY;
  const auto status = static_cast<uint8_t>(this->status_);
  const char *fallback =
      this->status_texts_[status].empty() ? this->get_status_key() : this->status_texts_[status].c_str();
  const char *message = this->text_(status + 2, fallback);
  if (selected && !glyphs)
    message = this->text_(18, "Unsupported text");
  else if (selected && !choice.available)
    message = this->text_(20, "Current option unavailable");
  else if (!selected && this->owner_.last_reason() == ControlReason::CONTROL_REASON_CAPABILITIES_CHANGED)
    message = this->text_(21, "Options changed - reopen");
  label(this->choice_status_label_, message);
  this->texts_pending_ = false;
}

}  // namespace esphome::lvgl_material

#endif
