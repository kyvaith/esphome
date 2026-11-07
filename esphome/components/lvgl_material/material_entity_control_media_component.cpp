#include "material_entity_control_component.h"
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

namespace esphome::lvgl_material {
namespace {
using Media = ControlMediaSnapshot;
using Facet = ControlMediaFacet;
using Op = ControlOperation;
enum Common : size_t { ROOT, TITLE, KIND, PHASE, TRACK, ARTIST, STATUS, VOLUME_SUMMARY };
enum Volume : size_t { VR, VT, VK, VO, VS, VB };
enum Choice : size_t { QR, QT, QK, QC, QS, QB, QA, QAL };
constexpr const char *ACTION_TEXT[] = {"Play",     "Pause",  "Previous",  "Next",        "Stop", "Turn on",
                                       "Turn off", "Volume", "Volume up", "Volume down", "Mute", "Shuffle on"};
constexpr uint8_t ACTION_KEY[] = {38, 39, 40, 41, 42, 43, 44, 30, 45, 46, 47, 49};
constexpr const char *FACET_TEXT[] = {"Volume", "Source", "Sound mode", "Repeat"};
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
void enabled(lv_obj_t *widget, bool value) {
  if (widget == nullptr || lv_obj_has_state(widget, LV_STATE_DISABLED) == !value)
    return;
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
Op operation(size_t index) { return static_cast<Op>(static_cast<unsigned>(Op::CONTROL_OPERATION_MEDIA_PLAY) + index); }
uint8_t action_target(const Media::Main &main, size_t index) {
  return index == 10 ? main.muted != 1 : index == 11 ? main.shuffle != 1 : 0;
}
}  // namespace

void MaterialEntityControlComponent::set_media_profile(lv_obj_t *r, lv_obj_t *t, lv_obj_t *k, lv_obj_t *p,
                                                       lv_obj_t *track, lv_obj_t *artist, lv_obj_t *s, lv_obj_t *volume,
                                                       const lv_font_t *track_font, const lv_font_t *artist_font,
                                                       const lv_font_t *volume_font) {
  if (!this->setup_ready_) {
    this->media_common_ = {r, t, k, p, track, artist, s, volume};
    this->media_compact_fonts_[0] = track_font;
    this->media_compact_fonts_[1] = artist_font;
    this->media_compact_fonts_[2] = volume_font;
  }
}
void MaterialEntityControlComponent::set_media_icon(size_t index, lv_obj_t *widget) {
  if (!this->setup_ready_ && index < this->media_icons_.size())
    this->media_icons_[index] = widget;
}
void MaterialEntityControlComponent::set_media_action(size_t index, lv_obj_t *button, lv_obj_t *caption) {
  if (!this->setup_ready_ && index < this->media_buttons_.size()) {
    this->media_buttons_[index] = button;
    this->media_labels_[index] = caption;
  }
}
void MaterialEntityControlComponent::set_media_facet(size_t index, lv_obj_t *button, lv_obj_t *caption) {
  if (!this->setup_ready_ && index < this->media_facets_.size()) {
    this->media_facets_[index] = button;
    this->media_facet_labels_[index] = caption;
  }
}
void MaterialEntityControlComponent::set_media_volume_profile(lv_obj_t *r, lv_obj_t *t, lv_obj_t *k, lv_obj_t *o,
                                                              lv_obj_t *s, lv_obj_t *b, const lv_font_t *compact) {
  if (!this->setup_ready_) {
    this->media_volume_ = {r, t, k, o, s, b};
    this->media_compact_fonts_[3] = compact;
  }
}
void MaterialEntityControlComponent::set_media_choice_profile(lv_obj_t *r, lv_obj_t *t, lv_obj_t *k, lv_obj_t *c,
                                                              lv_obj_t *s, lv_obj_t *b, lv_obj_t *a, lv_obj_t *al,
                                                              const lv_font_t *compact) {
  if (!this->setup_ready_) {
    this->media_choice_ = {r, t, k, c, s, b, a, al};
    this->media_compact_fonts_[4] = compact;
  }
}
void MaterialEntityControlComponent::set_media_choice_option(size_t index, lv_obj_t *button, lv_obj_t *caption) {
  if (!this->setup_ready_ && index < this->media_options_.size()) {
    this->media_options_[index] = button;
    this->media_option_labels_[index] = caption;
  }
}
void MaterialEntityControlComponent::set_media_enabled(bool value) {
  if (!this->setup_ready_)
    this->media_enabled_ = value;
}

bool MaterialEntityControlComponent::setup_media_profile_() {
  if (this->media_common_[ROOT] == nullptr)
    return !this->media_enabled_;
  if (!this->number_profile_)
    return false;
  const auto text = [](lv_obj_t *widget) { return widget && lv_obj_check_type(widget, &lv_label_class); };
  const auto profile = [&](const auto &widgets) {
    if (widgets[0] == nullptr || lv_obj_get_parent(widgets[0]) != this->root_)
      return false;
    for (auto *widget : widgets)
      if (widget == nullptr || !within(widgets[0], widget))
        return false;
    hidden(widgets[0], true);
    return true;
  };
  const auto button = [&](lv_obj_t *root, lv_obj_t *widget) {
    if (widget == nullptr || !within(root, widget))
      return false;
    // Borrowed Detail Choice objects already have this same owner callback.
    // Register it once; never create a competing input owner for shared objects.
    bool registered = false;
    for (uint32_t index = 0; index < lv_obj_get_event_count(widget); index++) {
      auto *descriptor = lv_obj_get_event_dsc(widget, index);
      registered |=
          lv_event_dsc_get_cb(descriptor) == input_event_cb_ && lv_event_dsc_get_user_data(descriptor) == this;
    }
    if (!registered)
      lv_obj_add_event_cb(widget, input_event_cb_, LV_EVENT_ALL, this);
    this->style_disabled_control_(widget);
    return true;
  };
  if (!profile(this->media_common_) || !profile(this->media_volume_) || !profile(this->media_choice_))
    return false;
  for (size_t index = 1; index < this->media_common_.size(); index++)
    if (!text(this->media_common_[index]))
      return false;
  for (auto index : {VT, VK, VO, VS})
    if (!text(this->media_volume_[index]))
      return false;
  for (auto index : {QT, QK, QC, QS, QAL})
    if (!text(this->media_choice_[index]))
      return false;
  for (auto *icon : this->media_icons_)
    if (icon == nullptr || !within(this->media_common_[ROOT], icon))
      return false;
  const auto controls = [&](const auto &buttons, const auto &labels, lv_obj_t *root) {
    for (size_t index = 0; index < buttons.size(); index++)
      if (!button(root, buttons[index]) || !text(labels[index]) || !within(buttons[index], labels[index]))
        return false;
    return true;
  };
  if (!controls(this->media_buttons_, this->media_labels_, this->media_common_[ROOT]) ||
      !controls(this->media_facets_, this->media_facet_labels_, this->media_common_[ROOT]) ||
      !controls(this->media_options_, this->media_option_labels_, this->media_choice_[QR]) ||
      !button(this->media_choice_[QR], this->media_choice_[QB]) ||
      !button(this->media_choice_[QR], this->media_choice_[QA]) ||
      !button(this->media_volume_[VR], this->media_volume_[VB]))
    return false;
  // This native button shares the live Scalar base. The default theme's
  // invisible disabled recolor must not animate beyond the boundary flush.
  auto *back = this->media_volume_[VB];
  const auto recolor = lv_obj_get_style_recolor(back, LV_PART_MAIN);
  lv_obj_set_style_recolor(back, recolor, LV_PART_MAIN);
  lv_obj_set_style_recolor_opa(back, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_recolor(back, recolor, static_cast<lv_style_selector_t>(LV_PART_MAIN) | LV_STATE_DISABLED);
  lv_obj_t *font_sources[] = {this->media_common_[TRACK], this->media_common_[ARTIST],
                              this->media_common_[VOLUME_SUMMARY], this->media_volume_[VO], this->media_choice_[QC]};
  for (size_t index = 0; index < std::size(font_sources); index++) {
    this->media_normal_fonts_[index] = lv_obj_get_style_text_font(font_sources[index], LV_PART_MAIN);
    if (this->media_compact_fonts_[index] == nullptr)
      this->media_compact_fonts_[index] = this->media_normal_fonts_[index];
  }
  if (this->media_enabled_)
    this->extra_configured_ |= 4U;
  return this->owner_.set_extra_profiles(this->extra_configured_);
}

bool MaterialEntityControlComponent::media_volume_active_() const { return this->owner_.is_media_volume(); }

void MaterialEntityControlComponent::restore_media_widgets_() {
  if (!this->media_widgets_active_)
    return;
  if (!this->owner_.has_selection()) {
    for (auto *button : this->media_buttons_)
      enabled(button, false);
    for (auto *button : this->media_facets_)
      enabled(button, false);
    for (auto *button : this->media_options_)
      enabled(button, false);
    enabled(this->media_choice_[QA], false);
    enabled(this->media_choice_[QB], false);
    enabled(this->media_volume_[VB], false);
    for (auto index : {PHASE, TRACK, ARTIST, VOLUME_SUMMARY})
      label(this->media_common_[index], "--");
    label(this->media_volume_[VO], "--");
    label(this->media_choice_[QC], "--");
    label(this->value_, "--");
    label(this->unknown_value_, "--");
    for (auto *status : {this->media_common_[STATUS], this->media_volume_[VS], this->media_choice_[QS]})
      this->extra_text_(status, this->text_(15, "Unknown"));
    hidden(this->hit_, true);
    return;
  }
  lv_obj_t *base[] = {this->title_,     this->hit_,        this->value_,       this->unknown_value_,
                      this->on_button_, this->off_button_, this->state_label_, this->status_label_};
  for (size_t index = 0; index < std::size(base); index++)
    hidden(base[index], (this->media_hidden_mask_ & (1U << index)) != 0);
  hidden(this->media_common_[ROOT], true);
  hidden(this->media_volume_[VR], true);
  hidden(this->media_choice_[QR], true);
  this->media_widgets_active_ = false;
  this->media_option_ = 255;
}

void MaterialEntityControlComponent::update_media_static_() {
  const auto *media = this->owner_.state().family_if<Media>();
  if (!this->owner_.is_media() || media == nullptr || this->owner_.is_dragging())
    return;
  lv_obj_t *base[] = {this->title_,     this->hit_,        this->value_,       this->unknown_value_,
                      this->on_button_, this->off_button_, this->state_label_, this->status_label_};
  if (!this->media_widgets_active_) {
    this->media_hidden_mask_ = 0;
    for (size_t index = 0; index < std::size(base); index++)
      if (base[index] && lv_obj_has_flag(base[index], LV_OBJ_FLAG_HIDDEN))
        this->media_hidden_mask_ |= 1U << index;
    this->media_widgets_active_ = true;
  }
  if (this->media_local_view_ != media->view_seq || this->media_option_generation_ != media->state_generation) {
    this->media_option_ = 255;
    this->media_local_view_ = media->view_seq;
    this->media_option_generation_ = media->state_generation;
  }
  const bool main_view = media->facet == Facet::MAIN, volume_view = media->facet == Facet::VOLUME;
  for (size_t index = 0; index < std::size(base); index++)
    hidden(base[index], !volume_view || index != 1);
  hidden(this->media_common_[ROOT], !main_view);
  hidden(this->media_volume_[VR], !volume_view);
  hidden(this->media_choice_[QR], main_view || volume_view);
  lv_obj_update_layout(this->root_);
  this->update_status_();
  const bool waiting = this->owner_.is_media_view_pending();
  const bool ready =
      this->owner_.is_view_ready() && this->owner_.media_view_ready() && !waiting && !this->owner_.is_command_pending();
  const char *status = this->text_(static_cast<uint8_t>(this->status_) + 2, this->get_status_key());
  if (waiting)
    status = this->text_(3, "Waiting");
  else if (this->owner_.last_result() == ControlResult::CONTROL_RESULT_SERVICE_COMPLETED)
    status = this->text_(14, "Service completed");
  else if (!media->available())
    status = this->text_(media->source == 1 ? 15 : 16, media->source == 1 ? "Unknown" : "Unavailable");
  const auto active_bg = configured_style_color_(this->on_button_, LV_STYLE_BG_COLOR),
             active_fg = configured_style_color_(this->minus_label_, LV_STYLE_TEXT_COLOR),
             neutral_bg = configured_style_color_(this->off_button_, LV_STYLE_BG_COLOR),
             neutral_fg = configured_style_color_(this->plus_label_, LV_STYLE_TEXT_COLOR);
  const auto color = [&](lv_obj_t *button, lv_obj_t *caption, bool active) {
    const auto bg = active ? active_bg : neutral_bg, fg = active ? active_fg : neutral_fg;
    if (!lv_color_eq(configured_style_color_(button, LV_STYLE_BG_COLOR), bg))
      lv_obj_set_style_bg_color(button, bg, LV_PART_MAIN);
    if (!lv_color_eq(configured_style_color_(button, LV_STYLE_TEXT_COLOR), fg))
      lv_obj_set_style_text_color(button, fg, LV_PART_MAIN);
    // Media owns the foreground of each button's immediate content, including SVG objects.
    // Their own theme style can override inherited text color, so set the local default explicitly.
    const uint32_t child_count = lv_obj_get_child_count(button);
    for (uint32_t index = 0; index < child_count; index++) {
      auto *child = lv_obj_get_child(button, index);
      if (!lv_color_eq(configured_style_color_(child, LV_STYLE_TEXT_COLOR), fg))
        lv_obj_set_style_text_color(child, fg, LV_PART_MAIN);
    }
    if (caption && !lv_color_eq(configured_style_color_(caption, LV_STYLE_TEXT_COLOR), fg))
      lv_obj_set_style_text_color(caption, fg, LV_PART_MAIN);
  };
  char text[32];
  bool complete = true;
  lv_obj_t *status_label = nullptr;
  if (main_view) {
    const auto &main = std::get<Media::Main>(media->data);
    status_label = this->media_common_[STATUS];
    complete &= this->extra_text_(this->media_common_[TITLE], this->owner_.state().title.bytes.data());
    complete &= this->extra_text_(this->media_common_[KIND], this->text_(18 + media->device_type, "Media player"));
    complete &= this->extra_text_(this->media_common_[PHASE], this->text_(22 + main.phase, "Unknown"));
    complete &= this->extra_text_(this->media_common_[TRACK],
                                  main.title.view().empty() ? this->text_(51, "No title") : main.title.bytes.data(),
                                  this->media_normal_fonts_[0], this->media_compact_fonts_[0]);
    complete &= this->extra_text_(this->media_common_[ARTIST],
                                  main.artist.view().empty() ? this->text_(52, "No artist") : main.artist.bytes.data(),
                                  this->media_normal_fonts_[1], this->media_compact_fonts_[1]);
    if (main.volume.known)
      std::snprintf(text, sizeof(text), "%s%%", main.volume.percent.bytes.data());
    else
      std::snprintf(text, sizeof(text), "--");
    const bool show_volume = (media->operations & (1U << 7)) != 0;
    hidden(this->media_common_[VOLUME_SUMMARY], !show_volume);
    if (show_volume)
      complete &= this->extra_text_(this->media_common_[VOLUME_SUMMARY], text, this->media_normal_fonts_[2],
                                    this->media_compact_fonts_[2]);
    for (size_t index = 0; index < this->media_icons_.size(); index++)
      hidden(this->media_icons_[index], index != media->device_type);
    size_t play = main.phase == 3 || main.phase == 5 ? 1 : 0;
    if ((media->operations & (1U << play)) == 0)
      play = 1 - play;
    size_t power = main.phase == 1 || main.phase == 6 || main.phase == 7 ? 5 : 6;
    if ((media->operations & (1U << power)) == 0)
      power = power == 5 ? 6 : 5;
    for (size_t index = 0; index < this->media_buttons_.size(); index++) {
      const bool shown = (media->operations & (1U << index)) != 0 && (index > 1 || index == play) &&
                         ((index != 5 && index != 6) || index == power);
      hidden(this->media_buttons_[index], !shown);
      const uint8_t key = index == 10 && main.muted == 1     ? 48
                          : index == 11 && main.shuffle == 1 ? 50
                                                             : ACTION_KEY[index];
      const bool caption_ok = this->extra_text_(this->media_labels_[index], this->text_(key, ACTION_TEXT[index]));
      const bool edit = index == 7 ? this->owner_.can_request_media_facet(Facet::VOLUME)
                                   : this->owner_.can_media(operation(index), action_target(main, index));
      enabled(this->media_buttons_[index], shown && ready && complete && caption_ok && edit);
      color(this->media_buttons_[index], this->media_labels_[index],
            index == play || (index == 10 && main.muted == 1) || (index == 11 && main.shuffle == 1));
    }
    for (size_t index = 0; index < this->media_facets_.size(); index++) {
      const auto facet = static_cast<Facet>(index + 2);
      const bool shown = (media->facets & (1U << (index + 1))) != 0;
      hidden(this->media_facets_[index], !shown);
      const bool caption_ok =
          this->extra_text_(this->media_facet_labels_[index], this->text_(31 + index, FACET_TEXT[index + 1]));
      enabled(this->media_facets_[index],
              shown && ready && complete && caption_ok && this->owner_.can_request_media_facet(facet));
      color(this->media_facets_[index], this->media_facet_labels_[index], false);
    }
  } else if (volume_view) {
    const auto &volume = std::get<Media::Volume>(media->data);
    status_label = this->media_volume_[VS];
    complete &= this->extra_text_(this->media_volume_[VT], this->owner_.state().title.bytes.data());
    complete &= this->extra_text_(this->media_volume_[VK], this->text_(30, "Volume"));
    if (volume.known)
      std::snprintf(text, sizeof(text), "%s%%", volume.percent.bytes.data());
    else
      std::snprintf(text, sizeof(text), "--");
    complete &=
        this->extra_text_(this->media_volume_[VO], text, this->media_normal_fonts_[3], this->media_compact_fonts_[3]);
    // Exact source observation belongs to VO. The existing Scalar bubble is
    // the sole integer preview; never expose the overlapping Light value.
    enabled(this->hit_, ready && complete && media->editable && this->owner_.is_scalar_ready());
    enabled(this->media_volume_[VB], !waiting && !this->owner_.is_command_pending());
  } else {
    const auto &choice = std::get<Media::Choice>(media->data);
    status_label = this->media_choice_[QS];
    complete &= this->extra_text_(this->media_choice_[QT], this->owner_.state().title.bytes.data());
    complete &=
        this->extra_text_(this->media_choice_[QK], this->text_(29 + static_cast<uint8_t>(media->facet), "Option"));
    const char *current =
        choice.index < choice.count ? choice.options[choice.index].bytes.data() : this->text_(54, "Unknown option");
    complete &= this->extra_text_(this->media_choice_[QC], current, this->media_normal_fonts_[4],
                                  this->media_compact_fonts_[4]);
    for (size_t index = 0; index < this->media_options_.size(); index++) {
      const bool shown = index < choice.count;
      hidden(this->media_options_[index], !shown);
      if (shown)
        complete &= this->extra_text_(this->media_option_labels_[index], choice.options[index].bytes.data());
    }
    for (size_t index = 0; index < choice.count; index++) {
      enabled(this->media_options_[index], ready && complete && media->editable);
      color(this->media_options_[index], this->media_option_labels_[index],
            index == (this->media_option_ == 255 ? choice.index : this->media_option_));
    }
    complete &= this->extra_text_(this->media_choice_[QAL], this->text_(35, "Apply"));
    enabled(this->media_choice_[QA],
            ready && complete && this->media_option_ != 255 &&
                this->owner_.can_media(Op::CONTROL_OPERATION_MEDIA_CHOICE, this->media_option_));
    enabled(this->media_choice_[QB], !waiting && !this->owner_.is_command_pending());
    color(this->media_choice_[QA], this->media_choice_[QAL], true);
  }
  this->extra_text_(status_label, complete ? status : this->text_(17, "Full observation cannot be displayed"));
}

void MaterialEntityControlComponent::reconcile_media_backend_() {
  if (this->media_handoff_preparing_ || !this->owner_.is_media() || this->owner_.is_media_view_pending())
    return;
  const bool native = !this->media_volume_active_();
  if ((native && !this->view_lifecycle_.is_backend(this)) ||
      (!native && this->view_lifecycle_.is_backend(this) && this->scalar_backend_))
    this->activate_view_backend_(native, true);
}

void MaterialEntityControlComponent::finish_media_handoff_() {
  if (this->media_handoff_preparing_ || this->media_handoff_generation_ == 0 || this->owner_.is_media_view_pending())
    return;
  const uint32_t generation = this->media_handoff_generation_;
  this->media_handoff_generation_ = 0;
  if (!this->owner_.is_media() || !this->is_selection_current(generation) || !this->is_presented_())
    return;
  // One resume attempt after the correlated result/timeout and static base
  // update. Failed drain/resume never becomes an automatic reopen loop.
  if (!this->owner_.is_view_ready())
    this->view_lifecycle_.resume(generation);
}

bool MaterialEntityControlComponent::request_media_facet_(Facet facet, uint32_t now) {
  if (this->media_intent_.generation != 0 || !this->owner_.can_request_media_facet(facet))
    return false;
  const auto *media = this->owner_.state().family_if<Media>();
  if (media == nullptr)
    return false;
  this->media_intent_ = {
      this->get_selection_generation(), media->state_generation, media->view_seq, now, media->facet, facet};
  // LVGL callbacks capture only a bounded intent; the owner loop drains the
  // presenter. No event pointer, queue, or second input owner survives release.
  return true;
}

void MaterialEntityControlComponent::dispatch_media_facet_() {
  if (this->media_intent_.generation == 0)
    return;
  const auto intent = this->media_intent_;
  this->media_intent_.generation = 0;
  const auto *media = this->owner_.state().family_if<Media>();
  const uint32_t now = App.get_loop_component_start_time();
  if (!this->owner_.is_media() || media == nullptr || !this->is_selection_current(intent.generation) ||
      !this->is_presented_() || !this->owner_.is_view_ready() || media->state_generation != intent.state_generation ||
      media->view_seq != intent.view_seq || media->facet != intent.source || now - intent.captured_at >= 1000 ||
      !this->owner_.can_request_media_facet(intent.target))
    return;
  this->media_handoff_generation_ = intent.generation;
  this->media_handoff_preparing_ = true;
  const bool drained = !this->media_volume_active_() || this->activate_view_backend_(true, true);
  this->media_handoff_preparing_ = false;
  if (!drained) {
    this->media_handoff_generation_ = 0;
    this->invalidate(ControlReason::CONTROL_REASON_NAVIGATION);
    return;
  }
  this->media_option_ = 255;
  media = this->owner_.state().family_if<Media>();
  const bool current = this->is_selection_current(intent.generation) && media != nullptr &&
                       media->state_generation == intent.state_generation && media->view_seq == intent.view_seq &&
                       media->facet == intent.source;
  if (!current || !this->owner_.request_media_facet(intent.target, now)) {
    this->reconcile_media_backend_();
    this->update_static_();
    this->finish_media_handoff_();
  }
}

void MaterialEntityControlComponent::media_input_event_(lv_event_code_t code, lv_obj_t *target, lv_indev_t *indev,
                                                        uint32_t now) {
  const auto *media = this->owner_.state().family_if<Media>();
  if (media == nullptr)
    return;
  const bool volume = media->facet == Facet::VOLUME && target == this->hit_;
  size_t action = 0, facet = 0, option = 0;
  while (action < this->media_buttons_.size() && this->media_buttons_[action] != target)
    action++;
  while (facet < this->media_facets_.size() && this->media_facets_[facet] != target)
    facet++;
  while (option < this->media_options_.size() && this->media_options_[option] != target)
    option++;
  const bool back = (media->facet == Facet::VOLUME && target == this->media_volume_[VB]) ||
                    (media->facet >= Facet::SOURCE && target == this->media_choice_[QB]);
  const bool navigation = back ||
                          (media->facet == Facet::MAIN && (action == 7 || facet < this->media_facets_.size())) ||
                          (media->facet >= Facet::SOURCE && option < this->media_options_.size());
  if (code == LV_EVENT_PRESSED) {
    if (this->captured_target_ != nullptr || indev == nullptr || lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER ||
        !this->is_presented_() || !lv_obj_is_visible(target) || lv_obj_has_state(target, LV_STATE_DISABLED) ||
        !this->owner_.is_view_ready() || this->owner_.is_command_pending() ||
        this->pointer_sequence_ == std::numeric_limits<uint32_t>::max())
      return;
#ifdef USE_LVGL_NAVIGATION_ADMISSION_GUARD
    if (this->view_lifecycle_.native_power_input_active())
      return;
#endif
    const uint32_t pointer = ++this->pointer_sequence_;
    bool accepted = false;
    if (navigation)
      accepted = this->owner_.begin_media_navigation(pointer, now);
    else if (volume)
      accepted = this->owner_.begin_media(pointer, Op::CONTROL_OPERATION_MEDIA_VOLUME, this->owner_.candidate(), now);
    else if (media->facet == Facet::MAIN && action < this->media_buttons_.size())
      accepted = this->owner_.begin_media(pointer, operation(action),
                                          action_target(std::get<Media::Main>(media->data), action), now);
    else if (media->facet >= Facet::SOURCE && target == this->media_choice_[QA])
      accepted = this->owner_.begin_media(pointer, Op::CONTROL_OPERATION_MEDIA_CHOICE, this->media_option_, now);
    if (!accepted)
      return;
    this->captured_target_ = target;
    this->captured_indev_ = indev;
    this->captured_generation_ = this->owner_.admission_generation();
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    this->captured_start_x_ = point.x;
    // Merely touching the rounded rail position must not quantize an exact
    // fractional source observation. A volume command needs pointer movement.
    this->unknown_drag_waiting_ = volume;
  }
  if (target != this->captured_target_ || indev != this->captured_indev_)
    return;
  if (!this->is_presented_() || !lv_obj_is_visible(target) ||
      this->captured_generation_ != this->owner_.admission_generation() || !this->owner_.is_dragging()) {
    this->cancel_pointer_();
    this->process_events_();
    return;
  }
  if (volume && (code == LV_EVENT_PRESSING || code == LV_EVENT_RELEASED)) {
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    if (point.x != this->captured_start_x_)
      this->unknown_drag_waiting_ = false;
    int32_t value;
    if (!this->unknown_drag_waiting_ && this->pointer_value_(indev, value))
      this->owner_.update_media_volume(this->pointer_sequence_, value, now);
  }
  if (code == LV_EVENT_RELEASED) {
    if (!volume) {
      lv_point_t point;
      lv_area_t bounds;
      lv_indev_get_point(indev, &point);
      lv_obj_get_coords(target, &bounds);
      if (point.x < bounds.x1 || point.x > bounds.x2 || point.y < bounds.y1 || point.y > bounds.y2) {
        this->cancel_pointer_();
        this->process_events_();
        return;
      }
    }
    this->captured_target_ = nullptr;
    this->captured_indev_ = nullptr;
    if (navigation && this->owner_.release_media_navigation(this->pointer_sequence_, now)) {
      if (back)
        this->request_media_facet_(Facet::MAIN, now);
      else if (media->facet == Facet::MAIN)
        this->request_media_facet_(action == 7 ? Facet::VOLUME : static_cast<Facet>(facet + 2), now);
      else if (option < std::get<Media::Choice>(media->data).count && media->editable)
        this->media_option_ = option;
    } else if (!navigation) {
      if (volume && this->unknown_drag_waiting_)
        this->owner_.cancel_drag();
      else {
        const uint32_t value = volume                        ? this->owner_.candidate()
                               : media->facet == Facet::MAIN ? action_target(std::get<Media::Main>(media->data), action)
                                                             : this->media_option_;
        this->owner_.release_media(this->pointer_sequence_, value, now);
      }
    }
    this->update_static_();
  }
  this->process_events_();
}

void MaterialEntityControlComponent::log_media_view(const char *phase) const {
#ifdef USE_LVGL_MATERIAL_SCALAR_PERFORMANCE_METRICS
  const auto *media = this->owner_.is_media() ? this->owner_.state().family_if<Media>() : nullptr;
  ESP_LOGI("scalar.performance",
           "%s media_active=%u media_retained=%u media_state_seq=%u media_generation=%u media_facet=%u "
           "media_view_seq=%u media_pending=%u media_ready=%u media_result=%u media_reason=%u",
           phase, static_cast<unsigned>(media != nullptr), static_cast<unsigned>(this->media_widgets_active_),
           static_cast<unsigned>(media != nullptr ? this->owner_.state().state_seq : 0),
           static_cast<unsigned>(media != nullptr ? media->state_generation : 0),
           static_cast<unsigned>(media != nullptr ? media->facet : Facet::MAIN),
           static_cast<unsigned>(media != nullptr ? media->view_seq : 0), this->owner_.is_media_view_pending(),
           this->owner_.media_view_ready(), static_cast<unsigned>(this->owner_.last_result()),
           static_cast<unsigned>(this->owner_.last_reason()));
#else
  (void) phase;
#endif
}
}  // namespace esphome::lvgl_material
#endif
