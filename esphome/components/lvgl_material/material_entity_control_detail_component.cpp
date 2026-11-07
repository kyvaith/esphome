#include "material_entity_control_component.h"
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
#include "esphome/core/log.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace esphome::lvgl_material {
namespace {
using Facet = ControlFacet;
enum L : size_t { LR, LK, LKL, LE, LEL, LS };
enum C : size_t { CR, CA, CT, CC, CAC, CL, CTI, CK, CM, CP, CST, CS, CMODE, CMODEL, CPARAM, CPARAML, CPARAMR, CBACK };
enum N : size_t { NR, NT, NK, NV, NC, NS, NTRACK, NF, NKNOB, NMIN, NMAX, NM, NP, NB, NKI, NHI };
enum Q : size_t { QR, QT, QK, QC, QS, QB, QA, QAL };
void hidden(lv_obj_t *w, bool value) {
  if (w == nullptr || lv_obj_has_flag(w, LV_OBJ_FLAG_HIDDEN) == value)
    return;
  if (value)
    lv_obj_add_flag(w, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_remove_flag(w, LV_OBJ_FLAG_HIDDEN);
}
void label(lv_obj_t *w, const char *text) {
  if (w != nullptr && std::strcmp(lv_label_get_text(w), text) != 0)
    lv_label_set_text(w, text);
}
void enabled(lv_obj_t *w, bool value) {
  if (value)
    lv_obj_remove_state(w, LV_STATE_DISABLED);
  else
    lv_obj_add_state(w, LV_STATE_DISABLED);
}
bool within(lv_obj_t *root, lv_obj_t *w) {
  for (auto *p = w; p != nullptr; p = lv_obj_get_parent(p))
    if (p == root)
      return true;
  return false;
}
uint8_t facet_text(Facet f) {
  switch (f) {
    case Facet::CONTROL_FACET_KELVIN:
      return 20;
    case Facet::CONTROL_FACET_EFFECT:
      return 21;
    case Facet::CONTROL_FACET_HUMIDITY:
      return 24;
    case Facet::CONTROL_FACET_HVAC_MODE:
      return 23;
    case Facet::CONTROL_FACET_PRESET_MODE:
      return 25;
    case Facet::CONTROL_FACET_FAN_MODE:
      return 26;
    case Facet::CONTROL_FACET_SWING_MODE:
      return 27;
    case Facet::CONTROL_FACET_SWING_HORIZONTAL_MODE:
      return 28;
    default:
      return 15;
  }
}
constexpr Facet PARAMETERS[] = {Facet::CONTROL_FACET_HUMIDITY, Facet::CONTROL_FACET_PRESET_MODE,
                                Facet::CONTROL_FACET_FAN_MODE, Facet::CONTROL_FACET_SWING_MODE,
                                Facet::CONTROL_FACET_SWING_HORIZONTAL_MODE};
bool glyphs(lv_obj_t *w, std::string_view text) {
  const auto *font = lv_obj_get_style_text_font(w, LV_PART_MAIN);
  for (size_t i = 0; i < text.size();) {
    uint32_t point = static_cast<uint8_t>(text[i++]);
    size_t tail = 0;
    if (point >= 0xf0) {
      point &= 7;
      tail = 3;
    } else if (point >= 0xe0) {
      point &= 15;
      tail = 2;
    } else if (point >= 0xc0) {
      point &= 31;
      tail = 1;
    }
    for (size_t j = 0; j < tail; j++)
      point = (point << 6) | (static_cast<uint8_t>(text[i++]) & 63);
    lv_font_glyph_dsc_t g{};
    if (!font || !lv_font_get_glyph_dsc(font, &g, point, 0) || g.is_placeholder)
      return false;
  }
  return true;
}
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
bool single_line_fits(lv_obj_t *label, const char *text) {
  const auto *font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
  if (font == nullptr || !glyphs(label, text))
    return false;
  lv_point_t size;
  lv_text_get_size(&size, text, font, lv_obj_get_style_text_letter_space(label, LV_PART_MAIN),
                   lv_obj_get_style_text_line_space(label, LV_PART_MAIN), LV_COORD_MAX, LV_TEXT_FLAG_EXPAND);
  return size.x <= lv_obj_get_content_width(label) && size.y <= lv_obj_get_content_height(label);
}
#endif
}  // namespace
void MaterialEntityControlComponent::set_light_detail_profile(lv_obj_t *r, lv_obj_t *k, lv_obj_t *kl, lv_obj_t *e,
                                                              lv_obj_t *el, lv_obj_t *s) {
  if (!this->setup_ready_)
    this->light_detail_ = {r, k, kl, e, el, s};
}
void MaterialEntityControlComponent::set_climate_detail_profile(lv_obj_t *r, lv_obj_t *a, lv_obj_t *t, lv_obj_t *c,
                                                                lv_obj_t *ac, lv_obj_t *l, lv_obj_t *ti, lv_obj_t *k,
                                                                lv_obj_t *m, lv_obj_t *p, lv_obj_t *st, lv_obj_t *s,
                                                                lv_obj_t *mode, lv_obj_t *model, lv_obj_t *pa,
                                                                lv_obj_t *pal, lv_obj_t *pr, lv_obj_t *b) {
  if (!this->setup_ready_)
    this->climate_detail_ = {r, a, t, c, ac, l, ti, k, m, p, st, s, mode, model, pa, pal, pr, b};
}
void MaterialEntityControlComponent::set_detail_numeric_profile(lv_obj_t *r, lv_obj_t *t, lv_obj_t *k, lv_obj_t *v,
                                                                lv_obj_t *c, lv_obj_t *s, lv_obj_t *tr, lv_obj_t *f,
                                                                lv_obj_t *kn, lv_obj_t *mn, lv_obj_t *mx, lv_obj_t *m,
                                                                lv_obj_t *p, lv_obj_t *b, lv_obj_t *ki, lv_obj_t *hi) {
  if (!this->setup_ready_)
    this->detail_numeric_ = {r, t, k, v, c, s, tr, f, kn, mn, mx, m, p, b, ki, hi};
}
void MaterialEntityControlComponent::set_detail_choice_profile(lv_obj_t *r, lv_obj_t *t, lv_obj_t *k, lv_obj_t *c,
                                                               lv_obj_t *s, lv_obj_t *b, lv_obj_t *a, lv_obj_t *al) {
  if (!this->setup_ready_)
    this->detail_choice_ = {r, t, k, c, s, b, a, al};
}
void MaterialEntityControlComponent::set_detail_parameter(size_t index, lv_obj_t *b, lv_obj_t *l) {
  if (!this->setup_ready_ && index < 5) {
    this->detail_parameters_[index] = b;
    this->detail_parameter_labels_[index] = l;
  }
}
void MaterialEntityControlComponent::set_detail_option(size_t index, lv_obj_t *b, lv_obj_t *l) {
  if (!this->setup_ready_ && index < 8) {
    this->detail_options_[index] = b;
    this->detail_option_labels_[index] = l;
  }
}
bool MaterialEntityControlComponent::setup_detail_profile_() {
  if (this->light_detail_[LR] == nullptr && this->climate_detail_[CR] == nullptr)
    return true;
  if (!this->number_profile_)
    return false;
  const auto profile = [&](const auto &widgets) {
    if (widgets[0] == nullptr)
      return false;
    for (auto *w : widgets)
      if (w == nullptr || !within(widgets[0], w))
        return false;
    hidden(widgets[0], true);
    return lv_obj_get_parent(widgets[0]) == this->root_;
  };
  const auto text = [](lv_obj_t *w) { return w && lv_obj_check_type(w, &lv_label_class); };
  const auto button = [&](lv_obj_t *root, lv_obj_t *w) {
    if (!w || !within(root, w))
      return false;
    lv_obj_add_event_cb(w, input_event_cb_, LV_EVENT_ALL, this);
    this->style_disabled_control_(w);
    return true;
  };
  if (!profile(this->detail_numeric_) || !profile(this->detail_choice_))
    return false;
  auto &n = this->detail_numeric_;
  for (auto i : {NT, NK, NV, NC, NS, NMIN, NMAX})
    if (!text(n[i]))
      return false;
  const auto track_opacity = lv_obj_get_style_bg_opa(n[NTRACK], LV_PART_MAIN);
  for (auto i : {NTRACK, NKNOB, NM, NP, NB})
    if (!button(n[NR], n[i]))
      return false;
  // The rail's hit area is not a button surface. Preserve its configured
  // transparency when disabled instead of exposing the theme's background.
  lv_obj_set_style_bg_opa(n[NTRACK], track_opacity, static_cast<lv_style_selector_t>(LV_PART_MAIN) | LV_STATE_DISABLED);
  lv_obj_add_flag(n[NTRACK], LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_add_flag(n[NKNOB], LV_OBJ_FLAG_PRESS_LOCK);
  auto &q = this->detail_choice_;
  for (auto i : {QT, QK, QC, QS, QAL})
    if (!text(q[i]))
      return false;
  if (!button(q[QR], q[QB]) || !button(q[QR], q[QA]))
    return false;
  for (size_t i = 0; i < 8; i++)
    if (!button(q[QR], this->detail_options_[i]) || !text(this->detail_option_labels_[i]) ||
        !within(this->detail_options_[i], this->detail_option_labels_[i]))
      return false;
  uint8_t mask = 0;
  if (this->light_detail_[LR]) {
    auto &l = this->light_detail_;
    if (!profile(l) || !button(l[LR], l[LK]) || !button(l[LR], l[LE]) || !text(l[LKL]) || !text(l[LEL]) || !text(l[LS]))
      return false;
    // These native controls share the live Scalar base. The default theme's
    // invisible disabled recolor must not keep animating after the boundary
    // flush, even though its opacity is already zero.
    for (auto index : {LK, LE}) {
      const auto recolor = lv_obj_get_style_recolor(l[index], LV_PART_MAIN);
      lv_obj_set_style_recolor(l[index], recolor, LV_PART_MAIN);
      lv_obj_set_style_recolor_opa(l[index], LV_OPA_TRANSP, LV_PART_MAIN);
      lv_obj_set_style_recolor(l[index], recolor, static_cast<lv_style_selector_t>(LV_PART_MAIN) | LV_STATE_DISABLED);
    }
    mask |= 1;
  }
  if (this->climate_detail_[CR]) {
    auto &c = this->climate_detail_;
    if (!profile(c) || !lv_obj_check_type(c[CA], &lv_arc_class))
      return false;
    for (auto i : {CT, CC, CAC, CL, CTI, CST, CS, CMODEL, CPARAML})
      if (!text(c[i]))
        return false;
    for (auto i : {CK, CM, CP, CMODE, CPARAM, CBACK})
      if (!button(c[CR], c[i]))
        return false;
    for (size_t i = 0; i < 5; i++)
      if (!button(c[CPARAMR], this->detail_parameters_[i]) || !text(this->detail_parameter_labels_[i]))
        return false;
    lv_obj_add_flag(c[CK], LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_set_ext_click_area(c[CK], 12);
    lv_obj_remove_flag(c[CA], LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_range(c[CA], 0, 1000);
    mask |= 2;
  }
  return this->owner_.set_detail_profiles(mask);
}
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
void MaterialEntityControlComponent::set_climate_range_profile(lv_obj_t *button, lv_obj_t *label, bool enabled) {
  if (!this->setup_ready_) {
    this->climate_range_widgets_ = {button, label};
    this->climate_range_enabled_ = enabled;
  }
}
bool MaterialEntityControlComponent::setup_climate_range_profile_() {
  auto &widgets = this->climate_range_widgets_;
  if (!widgets[0] && !widgets[1])
    return !this->climate_range_enabled_;
  if (!(this->owner_.detail_profiles() & 2U) || !widgets[0] || !widgets[1] || widgets[0] == widgets[1] ||
      !within(this->climate_detail_[CR], widgets[0]) || !within(widgets[0], widgets[1]) ||
      !lv_obj_check_type(widgets[1], &lv_label_class))
    return false;
  lv_obj_add_event_cb(widgets[0], input_event_cb_, LV_EVENT_ALL, this);
  this->style_disabled_control_(widgets[0]);
  hidden(widgets[0], true);
  return this->owner_.set_detail_range_enabled(this->climate_range_enabled_);
}
bool MaterialEntityControlComponent::populate_detail_range_features(ArduinoJson::JsonObject root) {
  const bool enabled = this->setup_ready_ && this->owner_.detail_range_enabled();
  root["v"] = 1;
  root["type"] = "entity_detail_range_v1";
  root["range_version"] = enabled ? 1 : 0;
  return enabled;
}
bool MaterialEntityControlComponent::bind_detail_range_features(std::string_view payload,
                                                                ArduinoJson::JsonObject root) {
  ControlText<32> channel;
  const bool accepted = this->setup_ready_ && !this->output_callback_active_ &&
                        this->codec_.parse_detail_range_bind(payload, channel) &&
                        this->owner_.accept_detail_range(channel);
  root["v"] = 1;
  root["type"] = "entity_detail_range_v1";
  root["range_version"] = accepted ? 1 : 0;
  root["channel"] = accepted ? channel.bytes.data() : "";
  root["accepted"] = accepted;
  return accepted;
}
#endif
bool MaterialEntityControlComponent::populate_detail_features(ArduinoJson::JsonObject root) {
  const uint8_t mask = this->setup_ready_ ? this->owner_.detail_profiles() : 0;
  root["v"] = 1;
  root["type"] = "entity_detail_features_v1";
  root["detail_version"] = mask ? 1 : 0;
  root["light_detail_version"] = (mask & 1) ? 1 : 0;
  root["climate_panel_version"] = (mask & 2) ? 1 : 0;
  return mask != 0;
}
bool MaterialEntityControlComponent::bind_detail_features(std::string_view payload, ArduinoJson::JsonObject root) {
  ControlText<32> channel;
  uint8_t mask = 0;
  const bool accepted = this->setup_ready_ && !this->output_callback_active_ &&
                        this->codec_.parse_detail_bind(payload, channel, mask) &&
                        this->owner_.accept_detail_features(channel, mask);
  if (!accepted)
    mask = 0;
  root["v"] = 1;
  root["type"] = "entity_detail_features_v1";
  root["detail_version"] = mask ? 1 : 0;
  root["light_detail_version"] = (mask & 1) ? 1 : 0;
  root["climate_panel_version"] = (mask & 2) ? 1 : 0;
  root["channel"] = accepted ? channel.bytes.data() : "";
  root["accepted"] = accepted;
  return accepted;
}
bool MaterialEntityControlComponent::open_light_detail(uint32_t slot, std::string_view card, uint64_t revision) {
  if (!this->setup_ready_ || !this->number_profile_ || this->output_callback_active_ || slot < 2 || slot > 31 ||
      (this->owner_.bound_detail_profiles() & 1) == 0)
    return false;
  ControlContext context;
  context.slot = slot;
  context.revision = revision;
  context.control_kind = ControlEntityKind::CONTROL_ENTITY_LIGHT;
  if (!context.card_id.assign(card) || !control_valid_context(context) || !this->activate_view_backend_(false))
    return false;
  const bool result = this->owner_.request_open_detail(context, millis());
  this->process_events_();
  this->update_static_();
  return result;
}
void MaterialEntityControlComponent::restore_detail_widgets_() {
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
  this->hide_light_color_();
#endif
  if (!this->detail_widgets_active_)
    return;
  lv_obj_t *old[] = {this->title_,     this->hit_,        this->value_,       this->unknown_value_,
                     this->on_button_, this->off_button_, this->state_label_, this->status_label_};
  const bool retired = !this->owner_.has_selection();
  for (size_t i = 0; i < 8; i++) {
    // The common Light path owns these dynamic labels. A revoked Detail view
    // has no current reading, including the Scalar alias retained for drain.
    if (retired && (i == 2 || i == 3))
      continue;
    hidden(old[i], (retired && i == 1) || (this->detail_hidden_mask_ & (1U << i)) != 0);
  }
  if (retired) {
    hidden(this->value_, true);
    hidden(this->unknown_value_, false);
    label(this->state_label_, "--");
    enabled(this->on_button_, false);
    enabled(this->off_button_, false);
    this->update_status_();
    label(this->status_label_, this->text_(static_cast<uint8_t>(this->status_) + 2, this->get_status_key()));
  }
  for (auto *r :
       {this->light_detail_[LR], this->climate_detail_[CR], this->detail_numeric_[NR], this->detail_choice_[QR]})
    hidden(r, true);
  // Keep the original visibility until a fresh selection replaces this view.
  // Repeated closed refreshes must not reveal a stale retained Scalar frame.
  this->detail_widgets_active_ = retired;
  this->detail_parameters_open_ = false;
  this->detail_option_ = 255;
  this->detail_local_view_ = 0;
}
void MaterialEntityControlComponent::reconcile_detail_backend_() {
  if (this->detail_handoff_preparing_ || !this->owner_.has_selection() || !this->owner_.is_detail() ||
      this->owner_.is_detail_view_pending())
    return;
  const bool native = this->owner_.is_discrete();
  if ((native && !this->view_lifecycle_.is_backend(this)) ||
      (!native && this->view_lifecycle_.is_backend(this) && this->scalar_backend_))
    this->activate_view_backend_(native, true);
}
void MaterialEntityControlComponent::finish_detail_handoff_() {
  if (this->detail_handoff_preparing_ || this->detail_handoff_generation_ == 0 || this->owner_.is_detail_view_pending())
    return;
  const uint32_t captured = this->detail_handoff_generation_;
  this->detail_handoff_generation_ = 0;
  if (!this->owner_.is_detail() || !this->is_selection_current(captured) || !this->is_presented_())
    return;
  // Exactly one attempt after the correlated receipt, rejection or timeout.
  // Failed drain/resume stays closed; no retry loop may reopen a stale view.
  const bool resumed = this->owner_.is_view_ready() || this->view_lifecycle_.resume(captured);
#ifdef USE_LVGL_MATERIAL_SCALAR_PERFORMANCE_METRICS
  const auto *detail = this->owner_.state().family_if<ControlDetailSnapshot>();
  ESP_LOGI("lvgl_material.detail", "facet resume=%u pending=%u ready=%u facet=%u view=%u", resumed,
           this->owner_.is_detail_view_pending(), this->owner_.detail_view_ready(),
           static_cast<unsigned>(detail != nullptr ? detail->facet : Facet::CONTROL_FACET_MAIN),
           static_cast<unsigned>(detail != nullptr ? detail->view_seq : 0));
#else
  (void) resumed;
#endif
}
bool MaterialEntityControlComponent::request_detail_facet_(Facet facet, uint32_t now) {
  if (this->detail_intent_.generation != 0 || !this->owner_.can_request_facet(facet))
    return false;
  const auto *selected_family = this->owner_.state().family_if<ControlDetailSnapshot>();
  if (selected_family == nullptr)
    return false;
  const auto &d = *selected_family;
  this->detail_intent_ = {this->get_selection_generation(), d.state_generation, d.view_seq, now, d.facet, facet};
#ifdef USE_LVGL_MATERIAL_SCALAR_PERFORMANCE_METRICS
  ESP_LOGI("lvgl_material.detail", "facet queued=%u", static_cast<unsigned>(facet));
#endif
  // Drain/refresh belongs to the owner loop, never a reentrant LVGL RELEASE.
  // There is one captured intent, no queue, retry or borrowed event pointer.
  return true;
}
void MaterialEntityControlComponent::dispatch_detail_facet_() {
  if (this->detail_intent_.generation == 0)
    return;
  const auto intent = this->detail_intent_;
  this->detail_intent_.generation = 0;
  const auto *selected_family = this->owner_.state().family_if<ControlDetailSnapshot>();
  if (selected_family == nullptr)
    return;
  const auto &d = *selected_family;
  if (!this->is_selection_current(intent.generation) || !this->is_presented_() || !this->owner_.is_view_ready() ||
      d.state_generation != intent.state_generation || d.view_seq != intent.view_seq || d.facet != intent.source ||
      millis() - intent.captured_at >= 1000 || !this->owner_.can_request_facet(intent.target)) {
#ifdef USE_LVGL_MATERIAL_SCALAR_PERFORMANCE_METRICS
    ESP_LOGI("lvgl_material.detail", "facet intent cancelled before handoff");
#endif
    return;
  }
  this->detail_handoff_generation_ = intent.generation;
  this->detail_handoff_preparing_ = true;
  const bool drained = this->owner_.is_discrete() || intent.target == Facet::CONTROL_FACET_MAIN ||
                       this->activate_view_backend_(true, true);
  this->detail_handoff_preparing_ = false;
#ifdef USE_LVGL_MATERIAL_SCALAR_PERFORMANCE_METRICS
  ESP_LOGI("lvgl_material.detail", "facet request=%u drained=%u selected=%u view_ready=%u",
           static_cast<unsigned>(intent.target), drained, this->owner_.has_selection(), this->owner_.is_view_ready());
#endif
  if (!drained) {
    this->detail_handoff_generation_ = 0;
    // A failed ownership transfer cannot leave an apparently live session.
    // Close uses its existing drain path; a fresh OPEN can retry explicitly.
    this->invalidate(ControlReason::CONTROL_REASON_NAVIGATION);
    return;
  }
  this->detail_parameters_open_ = false;
  this->detail_option_ = 255;
  const auto *current_detail = this->owner_.state().family_if<ControlDetailSnapshot>();
  const bool current = this->is_selection_current(intent.generation) && current_detail != nullptr &&
                       current_detail->state_generation == intent.state_generation &&
                       current_detail->view_seq == intent.view_seq && current_detail->facet == intent.source;
  const bool requested = current && this->owner_.request_facet(intent.target, millis());
#ifdef USE_LVGL_MATERIAL_SCALAR_PERFORMANCE_METRICS
  ESP_LOGI("lvgl_material.detail", "facet emitted=%u pending=%u selected=%u", requested,
           this->owner_.is_detail_view_pending(), this->owner_.has_selection());
#endif
  if (!requested) {
    this->reconcile_detail_backend_();
    this->update_static_();
    this->finish_detail_handoff_();
  }
}
#ifdef USE_LVGL_MATERIAL_SCALAR_PERFORMANCE_METRICS
void MaterialEntityControlComponent::log_detail_view(const char *phase) const {
  const auto *detail = this->owner_.state().family_if<ControlDetailSnapshot>();
  ESP_LOGI("scalar.performance", "%s detail_pending=%u detail_ready=%u detail_facet=%u detail_view_seq=%u", phase,
           this->owner_.is_detail_view_pending(), this->owner_.detail_view_ready(),
           static_cast<unsigned>(detail != nullptr ? detail->facet : Facet::CONTROL_FACET_MAIN),
           static_cast<unsigned>(detail != nullptr ? detail->view_seq : 0));
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
  const auto *color = detail != nullptr ? detail->color() : nullptr;
  const auto &draft = this->owner_.detail_color_draft();
  ESP_LOGI("scalar.performance",
           "%s color_active=%u color_ready=%u color_known=%u color_source=%u color_reason=%u color_hue=%u "
           "color_saturation=%u color_draft=%u color_draft_hue=%u color_draft_saturation=%u color_can_apply=%u",
           phase, this->owner_.is_detail_color(), this->light_color_ready_, color != nullptr && color->known,
           color != nullptr ? color->source_mode : 5, color != nullptr ? color->reason : 5,
           static_cast<unsigned>(color != nullptr ? color->hue_milli : 0),
           static_cast<unsigned>(color != nullptr ? color->saturation_milli : 0), draft.active,
           static_cast<unsigned>(draft.hue_milli), static_cast<unsigned>(draft.saturation_milli),
           this->owner_.can_apply_detail_color());
#endif
}
#endif
bool MaterialEntityControlComponent::detail_catalogue_glyphs_() const {
  const auto *detail = this->owner_.state().family_if<ControlDetailSnapshot>();
  const auto *c = detail != nullptr ? detail->choice() : nullptr;
  if (!c)
    return false;
  for (size_t i = 0; i < c->count; i++)
    if (!glyphs(this->detail_option_labels_[i], c->options[i].view()) ||
        !glyphs(this->detail_choice_[QC], c->options[i].view()))
      return false;
  return true;
}
ControlNumericView MaterialEntityControlComponent::detail_numeric_view_() const {
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
  if (this->owner_.is_detail_range()) {
    const auto &detail = *this->owner_.state().family_if<ControlDetailSnapshot>();
    const auto &range = *detail.range();
    return {&range.domain, this->climate_range_upper_ ? range.high_index : range.low_index, range.known,
            detail.editable};
  }
#endif
  return this->owner_.active_numeric();
}
void MaterialEntityControlComponent::show_detail_number_(uint32_t index, bool preview) {
  const auto v = this->detail_numeric_view_();
  const auto *selected_family = this->owner_.state().family_if<ControlDetailSnapshot>();
  if (selected_family == nullptr)
    return;
  const auto &d = *selected_family;
  const bool main = d.climate && d.facet == Facet::CONTROL_FACET_MAIN;
  bool dial = main;
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
  const bool range_view = this->owner_.is_detail_range();
  dial |= range_view;
  preview |= range_view && v.known;
#endif
  char text[72], value[24];
  bool known = v.known;
  const char *unit = v.domain && v.domain->valid() ? v.domain->unit.data() : "";
  if (preview && v.domain) {
    int64_t actual;
    known = v.domain->value_at(index, actual) && number_format_decimal(actual, value, sizeof(value));
  } else if (const auto *n = d.numeric()) {
    std::snprintf(value, sizeof(value), "%s", n->known ? n->value.bytes.data() : "--");
  } else
    known = false;
  if (main) {
    const auto &m = std::get<ControlClimateMain>(d.data);
    unit = m.unit == 1 ? "°C" : m.unit == 2 ? "°F" : "";
    if (!preview)
      std::snprintf(value, sizeof(value), "%s", m.target.view().empty() ? "--" : m.target.bytes.data());
  }
  std::snprintf(text, sizeof(text), "%s %s", known || main ? value : "--", unit);
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
  bool main_pair = false;
  if (this->climate_range_enabled_ && main && !preview && d.available && !known) {
    const auto &m = std::get<ControlClimateMain>(d.data);
    int64_t low, high;
    if (m.mode == 4 && m.unit != 0 && m.target.view().empty() && number_parse_decimal(m.low.view(), low) &&
        number_parse_decimal(m.high.view(), high) && low <= high) {
      // MAIN remains read-only; displaying a pair must not create a scalar target.
      const int length = std::snprintf(text, sizeof(text), "%s - %s %s", m.low.bytes.data(), m.high.bytes.data(), unit);
      main_pair = length >= 0 && static_cast<size_t>(length) < sizeof(text);
    }
  }
#endif
  if (std::strcmp(this->text_(36, "."), ",") == 0)
    for (char *p = text; *p; p++)
      if (*p == '.')
        *p = ',';
  if (dial) {
    auto &c = this->climate_detail_;
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
    // Extreme precision stays complete in the existing limits label below.
    label(c[CT], main_pair && !single_line_fits(c[CT], text) ? this->text_(19, "Range") : text);
#else
    label(c[CT], text);
#endif
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
    if (range_view) {
      const auto &range = *d.range();
      char low[24] = "--", high[24] = "--";
      int64_t actual;
      if (range.known) {
        range.domain.value_at(this->climate_range_upper_ ? range.low_index : index, actual);
        number_format_decimal(actual, low, sizeof(low));
        range.domain.value_at(this->climate_range_upper_ ? index : range.high_index, actual);
        number_format_decimal(actual, high, sizeof(high));
      }
      std::snprintf(text, sizeof(text), "%s - %s %s", low, high, range.domain.unit.data());
      if (std::strcmp(this->text_(36, "."), ",") == 0)
        for (char *p = text; *p; ++p)
          if (*p == '.')
            *p = ',';
      label(c[CL], text);
    }
#endif
    const bool position = v.domain && v.domain->valid() && known;
    const int32_t level =
        position && v.domain->max_index ? static_cast<uint64_t>(index) * 1000 / v.domain->max_index : 0;
    lv_arc_set_value(c[CA], level);
    hidden(c[CK], !position);
    if (position)
      lv_arc_align_obj_to_angle(c[CA], c[CK], 0);
  } else {
    auto &n = this->detail_numeric_;
    label(n[NV], text);
    const int32_t travel = std::max<int32_t>(0, lv_obj_get_width(n[NTRACK]) - lv_obj_get_width(n[NKNOB]));
    const int32_t x =
        v.domain && v.domain->max_index && known ? static_cast<uint64_t>(index) * travel / v.domain->max_index : 0;
    lv_obj_set_x(n[NKNOB], x);
    lv_obj_set_width(n[NF], x);
    hidden(n[NKNOB], !known);
  }
}
bool MaterialEntityControlComponent::update_detail_static_() {
  if (!this->owner_.is_detail() || !this->owner_.has_selection())
    return false;
  const auto *selected_family = this->owner_.state().family_if<ControlDetailSnapshot>();
  if (selected_family == nullptr)
    return false;
  const auto &d = *selected_family;
  lv_obj_t *old[] = {this->title_,     this->hit_,        this->value_,       this->unknown_value_,
                     this->on_button_, this->off_button_, this->state_label_, this->status_label_};
  if (!this->detail_widgets_active_) {
    this->detail_hidden_mask_ = 0;
    for (size_t i = 0; i < 8; i++)
      if (old[i] && lv_obj_has_flag(old[i], LV_OBJ_FLAG_HIDDEN))
        this->detail_hidden_mask_ |= 1U << i;
    this->detail_widgets_active_ = true;
    this->detail_local_view_ = d.view_seq;
    this->detail_option_ = 255;
  }
  const bool light_main = !d.climate && d.facet == Facet::CONTROL_FACET_MAIN;
  const bool climate_main = d.climate && d.facet == Facet::CONTROL_FACET_MAIN;
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
  const bool range_view = this->owner_.is_detail_range();
#else
  constexpr bool range_view = false;
#endif
  const bool numeric = !light_main && !climate_main && d.numeric();
  const bool choice = d.choice() != nullptr;
  if (d.view_seq != this->detail_local_view_) {
    this->detail_option_ = 255;
    this->detail_parameters_open_ = false;
    this->detail_local_view_ = d.view_seq;
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
    this->climate_range_upper_ = false;
#endif
  }
  if (this->detail_option_generation_ != d.state_generation) {
    this->detail_option_ = 255;
    this->detail_option_generation_ = d.state_generation;
  }
  for (size_t i = 0; i < 8; i++) {
    // The regular Light view owns known/unknown value visibility. Restoring
    // their initial flags here would hide/show an unchanged value on every
    // completion, invalidating the Scalar base after its accepted render.
    if (light_main && (i == 2 || i == 3))
      continue;
    hidden(old[i], !light_main || i == 7 || (this->detail_hidden_mask_ & (1U << i)) != 0);
  }
  hidden(this->light_detail_[LR], !light_main);
  hidden(this->climate_detail_[CR], !climate_main && !range_view);
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
  hidden(this->climate_range_widgets_[0],
         !(d.climate && this->owner_.detail_range_bound() && (d.facets & 128U) && (climate_main || range_view)));
#endif
  hidden(this->detail_numeric_[NR], !numeric);
  hidden(this->detail_choice_[QR], !choice);
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
  if (this->owner_.is_detail_color()) {
    this->update_light_color_static_();
    return true;
  }
  this->hide_light_color_();
#endif
  const bool navigate = !this->owner_.is_detail_view_pending() && !this->owner_.is_command_pending();
  const bool ready =
      this->owner_.detail_view_ready() && !this->owner_.is_detail_view_pending() && !this->owner_.is_command_pending();
  this->update_status_();
  const char *status = this->owner_.is_detail_view_pending()
                           ? this->text_(33, "Loading")
                           : this->text_(static_cast<uint8_t>(this->status_) + 2, this->get_status_key());
  if (!d.available && !this->owner_.is_detail_view_pending() &&
      (this->status_ == ControlUiStatus::CONTROL_UI_STATUS_READY ||
       this->status_ == ControlUiStatus::CONTROL_UI_STATUS_STATE_MATCHES ||
       this->status_ == ControlUiStatus::CONTROL_UI_STATUS_ALREADY_MATCHED))
    status = this->text_(16, "Unknown");
  const auto active_bg = configured_style_color_(this->on_button_, LV_STYLE_BG_COLOR),
             active_fg = configured_style_color_(this->minus_label_, LV_STYLE_TEXT_COLOR),
             neutral_bg = configured_style_color_(this->off_button_, LV_STYLE_BG_COLOR),
             neutral_fg = configured_style_color_(this->plus_label_, LV_STYLE_TEXT_COLOR);
  const auto color = [&](lv_obj_t *b, lv_obj_t *t, bool active) {
    active &= !lv_obj_has_state(b, LV_STATE_DISABLED);
    const auto bg = active ? active_bg : neutral_bg, fg = active ? active_fg : neutral_fg;
    if (!lv_color_eq(lv_obj_get_style_bg_color(b, LV_PART_MAIN), bg))
      lv_obj_set_style_bg_color(b, bg, LV_PART_MAIN);
    if (t && !lv_color_eq(lv_obj_get_style_text_color(t, LV_PART_MAIN), fg))
      lv_obj_set_style_text_color(t, fg, LV_PART_MAIN);
  };
  char text[160];
  if (light_main) {
    auto &l = this->light_detail_;
    hidden(l[LK], !(d.facets & 1));
    hidden(l[LE], !(d.facets & 2));
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
    const bool color_visible = this->owner_.detail_color_bound() && (d.facets & 4U) != 0;
    hidden(this->light_color_widgets_[13], !color_visible);
    if (this->light_color_widgets_[13] != nullptr) {
      enabled(this->light_color_widgets_[13], color_visible && navigate);
      label(this->light_color_widgets_[14], this->text_(59, "Color"));
    }
    label(l[LKL], this->text_(color_visible ? 60 : 20, color_visible ? "White" : "White temperature"));
#else
    label(l[LKL], this->text_(20, "White temperature"));
#endif
    label(l[LEL], this->text_(21, "Effects"));
    enabled(l[LK], navigate);
    enabled(l[LE], navigate);
    label(l[LS], status);
    return false;
  }
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
  if (range_view) {
    auto &c = this->climate_detail_;
    const auto &range = *d.range();
    const auto view = this->detail_numeric_view_();
    const bool held =
        this->owner_.is_dragging() &&
        (this->captured_target_ == c[CK] || this->captured_target_ == c[CM] || this->captured_target_ == c[CP]);
    const uint32_t index = held ? this->detail_preview_index_ : view.index;
    const bool edit = ready && this->owner_.can_detail_range();
    label(c[CTI], this->owner_.state().title.bytes.data());
    label(c[CST], this->text_(this->climate_range_upper_ ? 64 : 63,
                              this->climate_range_upper_ ? "Upper limit" : "Lower limit"));
    std::snprintf(text, sizeof(text), "%s: %s", this->text_(57, "Activity"),
                  this->text_(45 + range.activity, "Unknown"));
    label(c[CAC], text);
    std::snprintf(text, sizeof(text), "%s: %s %s", this->text_(17, "Current"),
                  range.current.view().empty() ? "--" : range.current.bytes.data(), range.domain.unit.data());
    if (std::strcmp(this->text_(36, "."), ",") == 0)
      for (char *p = text; *p; ++p)
        if (*p == '.')
          *p = ',';
    label(c[CC], text);
    enabled(c[CK], edit);
    if (edit)
      lv_obj_add_flag(c[CK], LV_OBJ_FLAG_CLICKABLE);
    else
      lv_obj_remove_flag(c[CK], LV_OBJ_FLAG_CLICKABLE);
    enabled(c[CM], edit && index > (this->climate_range_upper_ ? range.low_index : 0));
    enabled(c[CP], edit && index < (this->climate_range_upper_ ? range.domain.max_index : range.high_index));
    color(c[CM], nullptr, false);
    color(c[CP], nullptr, false);
    hidden(c[CMODE], false);
    hidden(c[CPARAM], false);
    hidden(c[CPARAMR], true);
    enabled(c[CMODE], ready);
    enabled(c[CPARAM], ready);
    enabled(this->climate_range_widgets_[0], navigate);
    label(c[CMODEL], this->text_(61, "Lower"));
    label(c[CPARAML], this->text_(62, "Upper"));
    label(this->climate_range_widgets_[1], this->text_(29, "Back"));
    color(c[CMODE], c[CMODEL], !this->climate_range_upper_);
    color(c[CPARAM], c[CPARAML], this->climate_range_upper_);
    color(this->climate_range_widgets_[0], this->climate_range_widgets_[1], false);
    label(c[CS], this->status_ != ControlUiStatus::CONTROL_UI_STATUS_READY ? status
                 : range.reason == 1 ? this->text_(65, "Select heat/cool mode to edit")
                 : range.reason == 2 ? this->text_(66, "Range unavailable")
                 : range.reason == 3 ? this->text_(32, "Unavailable")
                                     : status);
    this->show_detail_number_(index, held);
    return true;
  }
#endif
  if (climate_main) {
    auto &c = this->climate_detail_;
    const auto &m = std::get<ControlClimateMain>(d.data);
    label(c[CTI], this->owner_.state().title.bytes.data());
    std::snprintf(text, sizeof(text), "%s: %s", this->text_(23, "Mode"), this->text_(37 + m.mode, "Unknown"));
    label(c[CST], text);
    std::snprintf(text, sizeof(text), "%s: %s", this->text_(57, "Activity"), this->text_(45 + m.activity, "Unknown"));
    label(c[CAC], text);
    const char *unit = m.unit == 1 ? "°C" : m.unit == 2 ? "°F" : "";
    std::snprintf(text, sizeof(text), "%s: %s %s", this->text_(17, "Current"),
                  m.current.view().empty() ? "--" : m.current.bytes.data(), unit);
    if (std::strcmp(this->text_(36, "."), ",") == 0)
      for (char *p = text; *p; p++)
        if (*p == '.')
          *p = ',';
    label(c[CC], text);
    const bool range = !m.low.view().empty() || !m.high.view().empty();
    const auto &lo = range ? m.low : m.minimum;
    const auto &hi = range ? m.high : m.maximum;
    std::snprintf(text, sizeof(text), "%s%s%s - %s %s", range ? this->text_(19, "Range") : "", range ? ": " : "",
                  lo.view().empty() ? "--" : lo.bytes.data(), hi.view().empty() ? "--" : hi.bytes.data(), unit);
    if (std::strcmp(this->text_(36, "."), ",") == 0)
      for (char *p = text; *p; p++)
        if (*p == '.')
          *p = ',';
    label(c[CL], lo.view().empty() && hi.view().empty() ? "" : text);
    const auto v = this->detail_numeric_view_();
    const bool edit = ready && this->owner_.can_detail_number();
    enabled(c[CK], edit && v.domain->max_index > 0);
    if (edit && v.domain->max_index > 0)
      lv_obj_add_flag(c[CK], LV_OBJ_FLAG_CLICKABLE);
    else
      lv_obj_remove_flag(c[CK], LV_OBJ_FLAG_CLICKABLE);
    enabled(c[CM], edit && v.index > 0);
    enabled(c[CP], edit && v.index < v.domain->max_index);
    color(c[CM], nullptr, false);
    color(c[CP], nullptr, false);
    this->show_detail_number_(v.index, false);
    hidden(c[CMODE], !(d.facets & 4));
    enabled(c[CMODE], navigate);
    label(c[CMODEL], this->text_(23, "Mode"));
    hidden(c[CPARAM], !(d.facets & 122));
    enabled(c[CPARAM], navigate);
    label(c[CPARAML], this->text_(22, "Parameters"));
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
    color(c[CMODE], c[CMODEL], false);
    color(c[CPARAM], c[CPARAML], false);
    if (this->climate_range_widgets_[0]) {
      label(this->climate_range_widgets_[1], this->text_(19, "Range"));
      enabled(this->climate_range_widgets_[0], navigate && this->owner_.detail_range_bound());
      color(this->climate_range_widgets_[0], this->climate_range_widgets_[1], false);
    }
#endif
    label(c[CS], this->status_ == ControlUiStatus::CONTROL_UI_STATUS_READY && d.available && !m.target.view().empty() &&
                         !d.editable
                     ? this->text_(58, "Target is read-only")
                     : status);
    hidden(c[CPARAMR], !this->detail_parameters_open_);
    for (size_t i = 0; i < 5; i++) {
      hidden(this->detail_parameters_[i], !control_detail_facet_supported(d, PARAMETERS[i]));
      label(this->detail_parameter_labels_[i], this->text_(facet_text(PARAMETERS[i]), "Parameter"));
      enabled(this->detail_parameters_[i], navigate);
    }
    return true;
  }
  if (numeric) {
    auto &n = this->detail_numeric_;
    const auto &v = *d.numeric();
    label(n[NT], this->owner_.state().title.bytes.data());
    label(n[NK], this->text_(facet_text(d.facet), "Target"));
    hidden(n[NKI], d.facet != Facet::CONTROL_FACET_KELVIN);
    hidden(n[NHI], d.facet != Facet::CONTROL_FACET_HUMIDITY);
    char lo[24], hi[24];
    number_format_decimal(v.domain.minimum, lo, sizeof(lo));
    number_format_decimal(v.domain.maximum, hi, sizeof(hi));
    label(n[NMIN], lo);
    label(n[NMAX], hi);
    std::snprintf(text, sizeof(text), "%s: %s %%", this->text_(17, "Current"),
                  v.current.view().empty() ? "--" : v.current.bytes.data());
    if (std::strcmp(this->text_(36, "."), ",") == 0)
      for (char *p = text; *p; p++)
        if (*p == '.')
          *p = ',';
    label(n[NC], d.facet == Facet::CONTROL_FACET_HUMIDITY ? text : "");
    label(n[NS], status);
    const bool edit = ready && this->owner_.can_detail_number();
    for (auto i : {NTRACK, NKNOB})
      enabled(n[i], edit && v.domain.max_index > 0);
    enabled(n[NM], edit && v.index > 0);
    enabled(n[NP], edit && v.index < v.domain.max_index);
    this->show_detail_number_(v.index, false);
    return true;
  }
  if (choice) {
    auto &q = this->detail_choice_;
    const auto &v = *d.choice();
    const bool render = this->detail_catalogue_glyphs_();
    label(q[QT], this->owner_.state().title.bytes.data());
    label(q[QK], this->text_(facet_text(d.facet), "Mode"));
    label(q[QC], !render             ? this->text_(31, "Unavailable")
                 : v.index < v.count ? v.options[v.index].bytes.data()
                                     : this->text_(v.known ? 35 : 16, v.known ? "None" : "Unknown"));
    label(q[QS], !render                                                     ? this->text_(12, "Renderer unavailable")
                 : this->status_ == ControlUiStatus::CONTROL_UI_STATUS_READY ? this->text_(34, "Choose an option")
                                                                             : status);
    label(q[QAL], this->text_(30, "Apply"));
    enabled(q[QA],
            ready && render && this->detail_option_ < v.count && this->owner_.can_detail_choice(this->detail_option_));
    color(q[QA], q[QAL], true);
    for (size_t i = 0; i < 8; i++) {
      hidden(this->detail_options_[i], i >= v.count);
      if (i >= v.count)
        continue;
      label(this->detail_option_labels_[i], render ? v.options[i].bytes.data() : "--");
      enabled(this->detail_options_[i], ready && render && this->owner_.can_detail_choice(i));
      color(this->detail_options_[i], this->detail_option_labels_[i],
            i == (this->detail_option_ < v.count ? this->detail_option_ : v.index));
    }
    return true;
  }
  return true;
}
bool MaterialEntityControlComponent::is_detail_drag_(lv_obj_t *t) const {
  return this->owner_.is_detail() &&
         (t == this->detail_numeric_[NTRACK] || t == this->detail_numeric_[NKNOB] || t == this->climate_detail_[CK]
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
          || (this->owner_.is_detail_color() && t == this->light_color_widgets_[0])
#endif
         );
}
bool MaterialEntityControlComponent::begin_detail_input_(lv_obj_t *t, lv_indev_t *indev, uint32_t pointer,
                                                         uint32_t now) {
  if (this->detail_intent_.generation != 0)
    return false;
  const auto *selected_family = this->owner_.state().family_if<ControlDetailSnapshot>();
  if (selected_family == nullptr)
    return false;
  const auto &d = *selected_family;
  const bool main = d.facet == Facet::CONTROL_FACET_MAIN;
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
  if (this->owner_.is_detail_color())
    return this->begin_light_color_input_(t, indev, pointer, now);
  if (!d.climate && main && t == this->light_color_widgets_[13])
    return this->owner_.detail_color_bound() && control_detail_facet_supported(d, Facet::CONTROL_FACET_COLOR) &&
           this->owner_.begin_detail_navigation(pointer, now);
#endif
  if (!d.climate && main) {
    if (t == this->hit_)
      return true;
    if (t == this->on_button_ || t == this->off_button_ || t == this->light_detail_[LK] || t == this->light_detail_[LE])
      return this->owner_.begin_detail_navigation(pointer, now);
    return false;
  }
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
  const bool range_view = this->owner_.is_detail_range();
  if (d.climate && main && t == this->climate_range_widgets_[0])
    return this->owner_.detail_range_bound() && control_detail_facet_supported(d, Facet::CONTROL_FACET_RANGE) &&
           this->owner_.begin_detail_navigation(pointer, now);
#else
  constexpr bool range_view = false;
#endif
  if (d.climate && (main || range_view)) {
    auto &c = this->climate_detail_;
    if (this->detail_parameters_open_) {
      if (t == c[CBACK])
        return this->owner_.begin_detail_navigation(pointer, now);
      for (size_t i = 0; i < 5; i++)
        if (t == this->detail_parameters_[i] && control_detail_facet_supported(d, PARAMETERS[i]))
          return this->owner_.begin_detail_navigation(pointer, now);
      return false;
    }
    if (t == c[CMODE] || t == c[CPARAM]
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
        || (range_view && t == this->climate_range_widgets_[0])
#endif
    )
      return this->owner_.begin_detail_navigation(pointer, now);
    if (t != c[CK] && t != c[CM] && t != c[CP])
      return false;
    if (!(
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
            range_view ? this->owner_.begin_detail_range(pointer, now) :
#endif
                       this->owner_.begin_detail_number(pointer, now)))
      return false;
    const auto v = this->detail_numeric_view_();
    this->detail_preview_index_ = v.index;
    this->detail_climate_drag_ = t == c[CK];
    uint32_t minimum = 0, maximum = v.domain->max_index;
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
    if (range_view) {
      minimum = this->climate_range_upper_ ? d.range()->low_index : 0;
      maximum = this->climate_range_upper_ ? maximum : d.range()->high_index;
    }
#endif
    if (t == c[CM]) {
      if (v.index == minimum) {
        this->owner_.cancel_drag();
        return false;
      }
      this->detail_preview_index_--;
    }
    if (t == c[CP]) {
      if (v.index == maximum) {
        this->owner_.cancel_drag();
        return false;
      }
      this->detail_preview_index_++;
    }
    if (t == c[CK]) {
      if (v.domain->max_index == 0 || !arc_pointer_angle_(c[CA], indev, this->thermostat_last_angle_)) {
        this->owner_.cancel_drag();
        return false;
      }
      this->thermostat_span_ = static_cast<int32_t>(lv_arc_get_bg_angle_end(c[CA]) - lv_arc_get_bg_angle_start(c[CA]));
      if (this->thermostat_span_ <= 0)
        this->thermostat_span_ += 360;
      this->thermostat_angle_ = static_cast<uint64_t>(v.index) * this->thermostat_span_ / v.domain->max_index;
      this->thermostat_angle_valid_ = true;
    }
    return true;
  }
  if (d.choice()) {
    auto &q = this->detail_choice_;
    if (t == q[QB])
      return this->owner_.begin_detail_navigation(pointer, now);
    if (!this->detail_catalogue_glyphs_())
      return false;
    if (t == q[QA])
      return this->detail_option_generation_ == d.state_generation &&
             this->owner_.begin_detail_choice(pointer, this->detail_option_, now);
    for (size_t i = 0; i < 8; i++)
      if (t == this->detail_options_[i] && this->owner_.can_detail_choice(i))
        return this->owner_.begin_detail_navigation(pointer, now);
    return false;
  }
  auto &n = this->detail_numeric_;
  if (t == n[NB])
    return this->owner_.begin_detail_navigation(pointer, now);
  if (t != n[NTRACK] && t != n[NKNOB] && t != n[NM] && t != n[NP])
    return false;
  if (!this->owner_.begin_detail_number(pointer, now))
    return false;
  const auto v = this->detail_numeric_view_();
  this->detail_preview_index_ = v.index;
  this->detail_climate_drag_ = false;
  if (t == n[NM]) {
    if (!v.index) {
      this->owner_.cancel_drag();
      return false;
    }
    this->detail_preview_index_--;
  }
  if (t == n[NP]) {
    if (v.index == v.domain->max_index) {
      this->owner_.cancel_drag();
      return false;
    }
    this->detail_preview_index_++;
  }
  return true;
}
void MaterialEntityControlComponent::update_detail_input_(lv_indev_t *indev) {
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
  if (this->owner_.is_detail_color()) {
    this->update_light_color_input_(indev);
    return;
  }
#endif
  if (!this->owner_.is_dragging())
    return;
  const auto v = this->detail_numeric_view_();
  if (!v.domain || !v.domain->valid())
    return;
  uint32_t index = this->detail_preview_index_;
  if (this->detail_climate_drag_) {
    int32_t angle;
    if (!arc_pointer_angle_(this->climate_detail_[CA], indev, angle)) {
      this->thermostat_angle_valid_ = false;
      return;
    }
    const int32_t delta = (angle - this->thermostat_last_angle_ + 540) % 360 - 180;
    this->thermostat_last_angle_ = angle;
    if (!this->thermostat_angle_valid_) {
      this->thermostat_angle_valid_ = true;
      return;
    }
    this->thermostat_angle_ = std::clamp(this->thermostat_angle_ + delta, int32_t{0}, this->thermostat_span_);
    index = arc_preview_index_(this->thermostat_angle_, this->thermostat_span_, v.domain->max_index);
  } else {
    lv_area_t bounds;
    lv_obj_get_coords(this->detail_numeric_[NTRACK], &bounds);
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    const int32_t half = lv_obj_get_width(this->detail_numeric_[NKNOB]) / 2,
                  travel = std::max<int32_t>(1, lv_area_get_width(&bounds) - 2 * half);
    const int32_t x = std::clamp(point.x - bounds.x1 - half, int32_t{0}, travel);
    index = (static_cast<uint64_t>(x) * v.domain->max_index + travel / 2) / travel;
  }
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
  if (this->owner_.is_detail_range()) {
    const auto &range = *this->owner_.state().family_if<ControlDetailSnapshot>()->range();
    const uint32_t unclamped = index;
    index = std::clamp(index, this->climate_range_upper_ ? range.low_index : 0,
                       this->climate_range_upper_ ? range.domain.max_index : range.high_index);
    // Rebase at the bound: reversing the finger immediately moves this endpoint.
    if (unclamped != index)
      this->thermostat_angle_ = static_cast<uint64_t>(index) * this->thermostat_span_ / range.domain.max_index;
  }
#endif
  if (index != this->detail_preview_index_) {
    this->detail_preview_index_ = index;
    this->show_detail_number_(index, true);
  }
}
void MaterialEntityControlComponent::release_detail_input_(lv_obj_t *t, uint32_t pointer, uint32_t now) {
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
  if (this->owner_.is_detail_color()) {
    this->release_light_color_input_(t, pointer, now);
    return;
  }
#endif
  const auto *selected_family = this->owner_.state().family_if<ControlDetailSnapshot>();
  if (selected_family == nullptr)
    return;
  const auto &d = *selected_family;
  if (!d.climate && d.facet == Facet::CONTROL_FACET_MAIN) {
    if (t == this->hit_) {
      this->owner_.release(pointer, now);
      return;
    }
    if (!this->owner_.release_detail_navigation(pointer, now))
      return;
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
    if (t == this->light_color_widgets_[13]) {
      this->request_detail_facet_(Facet::CONTROL_FACET_COLOR, now);
      return;
    }
#endif
    if (t == this->on_button_ || t == this->off_button_)
      this->owner_.command(t == this->on_button_ ? ControlOperation::CONTROL_OPERATION_TURN_ON
                                                 : ControlOperation::CONTROL_OPERATION_TURN_OFF,
                           now);
    else
      this->request_detail_facet_(
          t == this->light_detail_[LK] ? Facet::CONTROL_FACET_KELVIN : Facet::CONTROL_FACET_EFFECT, now);
    return;
  }
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
  if (this->owner_.is_detail_range()) {
    auto &c = this->climate_detail_;
    const auto &range = *d.range();
    if (t == c[CK] || t == c[CM] || t == c[CP]) {
      this->owner_.release_detail_range(
          pointer, this->climate_range_upper_ ? range.low_index : this->detail_preview_index_,
          this->climate_range_upper_ ? this->detail_preview_index_ : range.high_index, now);
    } else if (this->owner_.release_detail_navigation(pointer, now)) {
      if (t == this->climate_range_widgets_[0])
        this->request_detail_facet_(Facet::CONTROL_FACET_MAIN, now);
      else if (t == c[CMODE] || t == c[CPARAM])
        this->climate_range_upper_ = t == c[CPARAM];
    }
    this->update_static_();
    return;
  }
#endif
  if (d.climate && d.facet == Facet::CONTROL_FACET_MAIN) {
    auto &c = this->climate_detail_;
    if (t == c[CK] || t == c[CM] || t == c[CP]) {
      this->owner_.release_detail_number(pointer, this->detail_preview_index_, now);
      return;
    }
    if (!this->owner_.release_detail_navigation(pointer, now))
      return;
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
    if (t == this->climate_range_widgets_[0]) {
      this->request_detail_facet_(Facet::CONTROL_FACET_RANGE, now);
      return;
    }
#endif
    if (t == c[CBACK])
      this->detail_parameters_open_ = false;
    else if (t == c[CPARAM])
      this->detail_parameters_open_ = true;
    else if (t == c[CMODE])
      this->request_detail_facet_(Facet::CONTROL_FACET_HVAC_MODE, now);
    else
      for (size_t i = 0; i < 5; i++)
        if (t == this->detail_parameters_[i])
          this->request_detail_facet_(PARAMETERS[i], now);
    this->update_static_();
    return;
  }
  if (d.choice()) {
    auto &q = this->detail_choice_;
    if (t == q[QA]) {
      this->owner_.release_detail_choice(pointer, now);
      return;
    }
    if (!this->owner_.release_detail_navigation(pointer, now))
      return;
    if (t == q[QB])
      this->request_detail_facet_(Facet::CONTROL_FACET_MAIN, now);
    else
      for (size_t i = 0; i < 8; i++)
        if (t == this->detail_options_[i]) {
          this->detail_option_ = i;
          this->detail_option_generation_ = d.state_generation;
        }
    this->update_static_();
    return;
  }
  if (t == this->detail_numeric_[NB]) {
    if (this->owner_.release_detail_navigation(pointer, now))
      this->request_detail_facet_(Facet::CONTROL_FACET_MAIN, now);
  } else
    this->owner_.release_detail_number(pointer, this->detail_preview_index_, now);
}
}  // namespace esphome::lvgl_material
#endif
