#include "material_entity_control_component.h"

#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER

#include "esphome/core/hal.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace esphome::lvgl_material {

void MaterialEntityControlComponent::style_disabled_control_(lv_obj_t *button) {
  constexpr lv_style_selector_t selector = static_cast<lv_style_selector_t>(LV_PART_MAIN) | LV_STATE_DISABLED;
  // The default theme mixes disabled colors toward grey. A local dark surface
  // and its inherited text opacity must remain a readable pair after filtering.
  lv_obj_set_style_recolor_opa(button, LV_OPA_TRANSP, selector);
  lv_obj_set_style_bg_opa(button, LV_OPA_COVER, selector);
  lv_obj_set_style_text_opa(button, LV_OPA_80, selector);
}

bool ControlViewLifecycle::handoff_backend(ControlViewBackend *previous, ControlViewBackend *next,
                                           bool retain_generation) {
  if (previous == nullptr || next == nullptr || this->backend_ != previous || this->overlay_generation_ != 0 ||
      this->closing_generation_ != 0 || this->cancelled_close_generation_ != 0)
    return false;
  const uint32_t captured = this->control_->owner().admission_generation();
  if (!this->suspend_for_snapshot() || this->backend_ != previous || !previous->hide_control_view_alias())
    return false;
  if (captured != this->control_->owner().admission_generation() || this->overlay_generation_ != 0 ||
      this->closing_generation_ != 0 || this->cancelled_close_generation_ != 0 || this->power_generation_ != 0 ||
      this->power_end_pending_ || this->control_->owner().is_view_ready())
    return false;
  this->backend_ = next;
  // Changing a presenter inside the same application does not retire its
  // captured selection. Close must still revoke it while a facet is loading.
  // A later application OPEN replaces this generation only on actual resume.
  if (!retain_generation)
    this->generation_ = 0;
  return true;
}

void MaterialEntityControlComponent::set_number_profile(lvgl::LvglNavigation *navigation,
                                                        lvgl::LvglApplication *application, lv_obj_t *minus_label,
                                                        lv_obj_t *plus_label, const lv_font_t *value_font) {
  if (this->setup_ready_)
    return;
  this->number_profile_ = true;
  this->navigation_ = navigation;
  this->application_ = application;
  this->minus_label_ = minus_label;
  this->plus_label_ = plus_label;
  this->number_value_font_ = value_font;
}

bool MaterialEntityControlComponent::setup_number_profile_() {
  if (!this->number_profile_)
    return true;
  if (this->navigation_ == nullptr || this->application_ == nullptr || this->minus_label_ == nullptr ||
      this->plus_label_ == nullptr || this->minus_label_ == this->plus_label_ || this->number_value_font_ == nullptr ||
      this->state_label_ == nullptr)
    return false;
  // The narrow Light hero font may lack minus/dot. Require actual configured
  // glyphs before admitting the generic profile, without allocating another font.
  lv_font_glyph_dsc_t glyph{};
  for (const char c : "0123456789-.") {
    if (c != 0 && !lv_font_get_glyph_dsc(this->number_value_font_, &glyph, static_cast<uint32_t>(c), 0))
      return false;
  }
  this->light_value_font_ = lv_obj_get_style_text_font(this->value_, LV_PART_MAIN);
  this->light_unknown_font_ = lv_obj_get_style_text_font(this->unknown_value_, LV_PART_MAIN);
#ifdef USE_LVGL_MATERIAL_THERMOSTAT
  if (!this->setup_thermostat_view_())
    return false;
#endif
  if (!this->view_lifecycle_.configure(this, this->navigation_, this->application_) ||
      !this->navigation_->add_admission_guard(this))
    return false;
  this->view_lifecycle_.attach_application_callbacks();
  return true;
}

bool MaterialEntityControlComponent::register_scalar_backend(ControlViewBackend *backend,
                                                             lvgl::LvglNavigation *navigation,
                                                             lvgl::LvglApplication *application) {
  if (!this->number_profile_ || backend == nullptr || navigation != this->navigation_ ||
      application != this->application_ || (this->scalar_backend_ != nullptr && this->scalar_backend_ != backend))
    return false;
  this->scalar_backend_ = backend;
  return true;
}

bool MaterialEntityControlComponent::activate_view_backend_(bool number, bool retain_generation) {
  ControlViewBackend *next = number || this->scalar_backend_ == nullptr ? this : this->scalar_backend_;
  if (this->view_lifecycle_.is_backend(next))
    return true;
  if (this->navigation_->is_transition_active() || this->view_lifecycle_.overlay_generation() != 0)
    return false;
  ControlViewBackend *previous = next == this ? this->scalar_backend_ : this;
  // No retargeting before an old worker/source/alias has actually drained.
  // Failure leaves that backend, its borrowed resources and closed input intact.
  if (previous == nullptr || !this->view_lifecycle_.is_backend(previous))
    return false;
  return this->view_lifecycle_.handoff_backend(previous, next, retain_generation);
}

bool MaterialEntityControlComponent::open_entity_(uint32_t slot, std::string_view card_id, uint64_t revision,
                                                  ControlEntityKind kind) {
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
  if (this->text_preview_pending_)
    return false;
#endif
  if (!this->setup_ready_ || !this->number_profile_ || this->output_callback_active_ ||
      (!this->owner_.is_generic() && kind != ControlEntityKind::CONTROL_ENTITY_LIGHT) || slot < 2 || slot > 31)
    return false;
  ControlContext context;
  context.slot = static_cast<uint8_t>(slot);
  context.revision = revision;
  context.control_kind = kind;
  if (!context.card_id.assign(card_id) || !control_valid_context(context) ||
      !this->activate_view_backend_(kind != ControlEntityKind::CONTROL_ENTITY_LIGHT))
    return false;
  const bool accepted = this->owner_.request_open(context, millis());
  this->process_events_();
  this->update_static_();
  return accepted;
}

bool MaterialEntityControlComponent::open_number(uint32_t slot, std::string_view card_id, uint64_t revision) {
  return this->open_entity_(slot, card_id, revision, ControlEntityKind::CONTROL_ENTITY_NUMBER);
}

bool MaterialEntityControlComponent::prepare_navigation(lvgl::NavigationAdmissionKind kind,
                                                        lvgl::LvglApplication *application) {
  return !this->number_profile_ || this->view_lifecycle_.prepare_navigation(kind, application);
}

bool MaterialEntityControlComponent::suspend_for_direct_overlay() {
  return this->number_profile_ && this->view_lifecycle_.suspend_for_direct_overlay();
}

void MaterialEntityControlComponent::resume_after_direct_overlay() {
  if (this->number_profile_)
    this->view_lifecycle_.resume_after_direct_overlay();
}

bool MaterialEntityControlComponent::suspend_control_view_backend(uint32_t timeout_ms) {
  (void) timeout_ms;
  // The native participant owns no pixel/source/pause lease to drain.
  this->cancel_pointer_();
  return true;
}

bool MaterialEntityControlComponent::resume_control_view_backend(uint32_t generation) {
  if (!this->setup_ready_ || !this->number_profile_ || !this->view_lifecycle_.is_backend(this) ||
      this->view_lifecycle_.overlay_generation() != 0 || !this->is_selection_current(generation) ||
      !this->navigation_->is_application_open(this->application_) || this->navigation_->is_transition_active() ||
      !this->is_presented_() || ((!this->owner_.is_discrete()
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
                                || this->media_volume_active_()
#endif
                               ) && this->scalar_backend_ != nullptr))
    return false;
  this->view_lifecycle_.set_generation(generation);
  if (!this->set_view_ready(generation, true))
    return false;
  // Navigation has restored the coherent native base. This participant only
  // draws existing widgets through LVGL; it never acquires a framebuffer lease.
  lv_refr_now(lv_obj_get_display(this->root_));
  return this->is_selection_current(generation) && this->owner_.is_view_ready();
}

const char *MaterialEntityControlComponent::capability_entity_json() {
  if (!this->number_profile_ || this->output_callback_active_)
    return nullptr;
  ControlMessage message;
  message.generic = true;
  message.kind = ControlKind::CONTROL_KIND_CAPABILITY;
  message.enabled = this->setup_ready_ && this->owner_.is_enabled() && !this->is_failed();
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
  message.choice_enabled = this->owner_.choice_enabled();
#endif
#ifdef USE_LVGL_MATERIAL_COVER_CONTROLS
  message.cover_enabled = this->owner_.cover_enabled();
#endif
#ifdef USE_LVGL_MATERIAL_FAN_CONTROLS
  message.fan_enabled = this->owner_.fan_enabled();
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS
  message.humidifier_enabled = this->owner_.humidifier_enabled();
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
  message.robot_enabled = this->owner_.robot_enabled();
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
  message.panel_enabled = (this->owner_.panel_types() != 0
#ifdef USE_LVGL_MATERIAL_EXTRA_CONTROLS
                           || this->owner_.extra_profiles() != 0
#endif
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
                           || (this->owner_.detail_profiles() & 2) != 0
#endif
  );
#endif
  return this->serialize_(message) ? this->output_.data() : nullptr;
}

const char *MaterialEntityControlComponent::bind_entity_json(std::string_view payload) {
  if (!this->setup_ready_ || !this->number_profile_ || this->output_callback_active_)
    return nullptr;
  this->bind_response_prepared_ = false;
  ControlMessage message;
  if (!this->codec_.parse_entity(payload, message) || message.kind != ControlKind::CONTROL_KIND_BIND ||
      !this->owner_.receive(message, millis()))
    return nullptr;
  this->process_events_();
  this->update_static_();
  return this->serialize_(message) ? this->output_.data() : nullptr;
}

bool MaterialEntityControlComponent::prepare_entity_bind_response(std::string_view payload) {
  const char *response = this->bind_entity_json(payload);
  ControlMessage message;
  if (response == nullptr || !this->codec_.parse_entity(response, message))
    return false;
  this->bind_response_channel_ = message.identity.channel;
  this->bind_response_generic_ = true;
  this->bind_response_prepared_ = true;
  return true;
}

bool MaterialEntityControlComponent::populate_entity_status(ArduinoJson::JsonObject root) {
  if (!this->number_profile_ || this->output_callback_active_)
    return false;
  root.clear();
  root["v"] = 1;
  root["type"] = "entity_control_v1";
  root["kind"] = "capability";
  root["enabled"] = this->setup_ready_ && this->owner_.is_enabled() && !this->is_failed();
  root["max_payload_bytes"] = CONTROL_MAX_PAYLOAD_BYTES;
  auto kinds = root["control_kinds"].to<ArduinoJson::JsonArray>();
  kinds.add("light");
  kinds.add("number");
  size_t expected = 2;
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
  if (this->owner_.choice_enabled()) {
    kinds.add("choice");
    expected++;
  }
#endif
#ifdef USE_LVGL_MATERIAL_COVER_CONTROLS
  if (this->owner_.cover_enabled()) {
    kinds.add("cover");
    expected++;
  }
#endif
#ifdef USE_LVGL_MATERIAL_FAN_CONTROLS
  if (this->owner_.fan_enabled()) {
    kinds.add("fan");
    expected++;
  }
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS
  if (this->owner_.humidifier_enabled()) {
    kinds.add("humidifier");
    expected++;
  }
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
  if (this->owner_.robot_enabled()) {
    kinds.add("robot");
    expected++;
  }
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
  if ((this->owner_.panel_types() != 0
#ifdef USE_LVGL_MATERIAL_EXTRA_CONTROLS
       || this->owner_.extra_profiles() != 0
#endif
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
       || (this->owner_.detail_profiles() & 2) != 0
#endif
       )) {
    kinds.add("panel");
    expected++;
  }
#endif
  return root.size() == 6 && kinds.size() == expected;
}

bool MaterialEntityControlComponent::populate_entity_bind_response(ArduinoJson::JsonObject root) {
  if (!this->bind_response_prepared_ || !this->bind_response_generic_ || this->output_callback_active_ ||
      !this->setup_ready_ || !this->number_profile_ || !this->owner_.is_bound() || !this->owner_.is_generic())
    return false;
  this->bind_response_prepared_ = this->bind_response_generic_ = false;
  root.clear();
  root["v"] = 1;
  root["type"] = "entity_control_v1";
  root["kind"] = "bind";
  root["channel"] = this->bind_response_channel_.bytes.data();
  const char *channel = root["channel"].as<const char *>();
  return root.size() == 4 && channel != nullptr && this->bind_response_channel_.view() == channel;
}

bool MaterialEntityControlComponent::receive_entity(std::string_view payload) {
  if (!this->setup_ready_ || !this->number_profile_ || this->output_callback_active_)
    return false;
  ControlMessage message;
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
  // Clear on every exit, including shape rejection and lease revocation. This
  // separate RX staging survives codec reset and is never a displayed string.
  struct TextRxTransaction {
    ControlTextRx *rx;
    ~TextRxTransaction() {
      if (this->rx != nullptr)
        this->rx->clear();
    }
  } text_rx{this->text_storage_ != nullptr ? &this->text_storage_->rx : nullptr};
#endif
  if (!this->codec_.parse_entity(payload, message, this->owner_.choice_empty_active(), this->owner_.cover_panel_active()
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
                                                                                           ,
                                 text_rx.rx
#endif
                                 ))
    return false;
#ifdef USE_LVGL_MATERIAL_THERMOSTAT
  if (const auto *number = message.state.family_if<ControlNumberSnapshot>();
      number != nullptr && number->thermostat.present && this->thermostat_root_ == nullptr)
    return false;
#endif
  switch (message.kind) {
    case ControlKind::CONTROL_KIND_GRANT:
    case ControlKind::CONTROL_KIND_STATE:
    case ControlKind::CONTROL_KIND_RESULT:
    case ControlKind::CONTROL_KIND_CLOSE:
    case ControlKind::CONTROL_KIND_CANCEL_OPEN:
#ifdef USE_LVGL_MATERIAL_INFO_AGENDA
    case ControlKind::CONTROL_KIND_PAGE_RESULT:
#endif
#if defined(USE_LVGL_MATERIAL_DETAIL_CONTROLS) || defined(USE_LVGL_MATERIAL_MEDIA_CONTROLS)
    case ControlKind::CONTROL_KIND_FACET_RESULT:
#endif
      break;
    default:
      return false;
  }
  const bool held_discrete_input = this->owner_.is_discrete() && this->owner_.is_dragging();
  auto *const held_target = this->captured_target_;
  auto *const held_indev = this->captured_indev_;
  const uint32_t held_pointer = this->pointer_sequence_;
  const bool accepted = this->owner_.receive(
      message, millis()
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
                   ,
      text_rx.rx != nullptr ? text_rx.rx->view() : ControlTextValueView {}
#endif
  );
  if (held_discrete_input && !this->owner_.is_dragging() && held_indev != nullptr &&
      this->captured_target_ == held_target && this->captured_indev_ == held_indev &&
      this->pointer_sequence_ == held_pointer) {
    // A semantic state change cancels the owned step before native widgets may
    // become disabled. LVGL suppresses RELEASED on a disabled button, so retire
    // both captures now. The same physical contact cannot become a new press.
    this->cancel_pointer_();
    lv_indev_wait_release(held_indev);
    lv_indev_reset(held_indev, held_target);
  }
  this->process_events_();
  return accepted;
}

void MaterialEntityControlComponent::restore_light_widgets_() {
  if (!this->number_widgets_active_)
    return;
  lv_obj_set_style_text_font(this->value_, this->light_value_font_, LV_PART_MAIN);
  lv_obj_set_style_text_font(this->unknown_value_, this->light_unknown_font_, LV_PART_MAIN);
  lv_label_set_text(this->minus_label_, this->text_(0, this->on_text_.c_str()));
  lv_label_set_text(this->plus_label_, this->text_(1, this->off_text_.c_str()));
  lv_obj_remove_flag(this->hit_, LV_OBJ_FLAG_HIDDEN);
#ifdef USE_LVGL_MATERIAL_THERMOSTAT
  if (this->thermostat_root_ != nullptr && !lv_obj_has_flag(this->thermostat_root_, LV_OBJ_FLAG_HIDDEN)) {
    this->thermostat_pending_preview_ = false;
    lv_obj_add_flag(this->thermostat_root_, LV_OBJ_FLAG_HIDDEN);
    for (auto *widget : {this->state_label_, this->title_, this->on_button_, this->off_button_})
      lv_obj_remove_flag(widget, LV_OBJ_FLAG_HIDDEN);
  }
#endif
  this->number_widgets_active_ = false;
}

void MaterialEntityControlComponent::update_number_static_() {
  const auto set_label = [](lv_obj_t *label, const char *text) {
    if (label != nullptr && std::strcmp(lv_label_get_text(label), text) != 0)
      lv_label_set_text(label, text);
  };
  const auto hidden = [](lv_obj_t *widget, bool hide) {
    if (hide == lv_obj_has_flag(widget, LV_OBJ_FLAG_HIDDEN))
      return;
    if (hide)
      lv_obj_add_flag(widget, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_remove_flag(widget, LV_OBJ_FLAG_HIDDEN);
  };
  if (!this->number_widgets_active_) {
    lv_obj_set_style_text_font(this->value_, this->number_value_font_, LV_PART_MAIN);
    lv_obj_set_style_text_font(this->unknown_value_, this->number_value_font_, LV_PART_MAIN);
    lv_obj_add_flag(this->hit_, LV_OBJ_FLAG_HIDDEN);
    this->number_widgets_active_ = true;
  }
  set_label(this->minus_label_, this->text_(0, "-"));
  set_label(this->plus_label_, this->text_(1, "+"));
  const bool selected = this->owner_.has_selection();
  const auto *selected_family = this->owner_.state().family_if<ControlNumberSnapshot>();
  if (selected_family == nullptr)
    return;
  const auto &number = *selected_family;
  const bool known = selected && number.available;
  bool thermostat = false;
#ifdef USE_LVGL_MATERIAL_THERMOSTAT
  thermostat = selected && number.thermostat.present;
  if (this->thermostat_root_ != nullptr) {
    hidden(this->thermostat_root_, !thermostat);
    for (auto *widget : {this->state_label_, this->title_, this->on_button_, this->off_button_})
      hidden(widget, thermostat);
    if (thermostat)
      this->update_thermostat_view_(number.thermostat);
  }
#endif
  hidden(this->value_, thermostat || !known);
  hidden(this->unknown_value_, thermostat || known);
  if (selected)
    set_label(this->title_, this->owner_.state().title.bytes.data());
  // The owner/wire retains canonical decimal text. Only this native label may
  // substitute an exact single-byte separator supplied by the product locale.
  auto display_value = number.value_text.bytes;
  const char *separator = this->text_(15, ".");
  const auto *value_font = lv_obj_get_style_text_font(this->value_, LV_PART_MAIN);
  lv_font_glyph_dsc_t separator_glyph{};
  if (known && separator[0] == ',' && separator[1] == '\0' && value_font != nullptr &&
      lv_font_get_glyph_dsc(value_font, &separator_glyph, ',', 0) && !separator_glyph.is_placeholder) {
    for (char &character : display_value) {
      if (character == '.')
        character = ',';
    }
  }
  set_label(this->value_, known ? display_value.data() : "--");
  const char *unit = selected ? number.domain.unit.data() : "";
  const auto *unit_font = lv_obj_get_style_text_font(this->state_label_, LV_PART_MAIN);
  // Descriptor text is validated UTF-8 and at most32bytes. A missing unit glyph
  // has a bounded product-supplied fallback; never load another font here.
  for (size_t offset = 0; unit[offset] != 0;) {
    const auto first = static_cast<uint8_t>(unit[offset++]);
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
    for (size_t index = 0; index < tail; index++)
      point = (point << 6) | (static_cast<uint8_t>(unit[offset++]) & 0x3f);
    lv_font_glyph_dsc_t glyph{};
    if (unit_font == nullptr || !lv_font_get_glyph_dsc(unit_font, &glyph, point, 0)) {
      unit = this->text_(14, "?");
      break;
    }
  }
  set_label(this->state_label_, unit);
  for (auto *button : {this->on_button_, this->off_button_}) {
    if (this->owner_.can_step(button == this->on_button_ ? -1 : 1))
      lv_obj_remove_state(button, LV_STATE_DISABLED);
    else
      lv_obj_add_state(button, LV_STATE_DISABLED);
  }
  this->update_status_();
  // A retained successful command is history, not evidence that the current
  // value is still available. Preserve pending/error feedback and the protocol
  // result; only replace an idle/success caption while the value is unknown.
  const bool unavailable = selected && !number.available &&
                           (this->status_ == ControlUiStatus::CONTROL_UI_STATUS_READY ||
                            this->status_ == ControlUiStatus::CONTROL_UI_STATUS_STATE_MATCHES ||
                            this->status_ == ControlUiStatus::CONTROL_UI_STATUS_ALREADY_MATCHED);
  if (unavailable)
    this->status_ = ControlUiStatus::CONTROL_UI_STATUS_WAITING;
  const uint8_t status = static_cast<uint8_t>(this->status_);
  const char *fallback =
      this->status_texts_[status].empty() ? this->get_status_key() : this->status_texts_[status].c_str();
  const char *message = unavailable ? this->text_(16, "Current value unavailable") : this->text_(status + 2, fallback);
#ifdef USE_LVGL_MATERIAL_THERMOSTAT
  if (thermostat && !number.domain.valid() &&
      (unavailable || this->status_ == ControlUiStatus::CONTROL_UI_STATUS_WAITING))
    message = this->text_(18, "Target adjustment unavailable");
  if (thermostat && this->status_ == ControlUiStatus::CONTROL_UI_STATUS_READY && this->owner_.can_drag_thermostat())
    message = this->text_(29, "Drag the knob or use + and -");
#endif
  if (unavailable && message[0] == '\0')
    message = "Current value unavailable";
  set_label(this->status_label_, message);
  this->texts_pending_ = false;
}

#ifdef USE_LVGL_MATERIAL_THERMOSTAT
void MaterialEntityControlComponent::set_thermostat_view(lv_obj_t *root, lv_obj_t *arc, lv_obj_t *target,
                                                         lv_obj_t *current, lv_obj_t *activity, lv_obj_t *limits,
                                                         lv_obj_t *title, lv_obj_t *knob, lv_obj_t *minus,
                                                         lv_obj_t *plus) {
  this->thermostat_root_ = root;
  this->thermostat_arc_ = arc;
  this->thermostat_target_ = target;
  this->thermostat_current_ = current;
  this->thermostat_activity_ = activity;
  this->thermostat_limits_ = limits;
  this->thermostat_title_ = title;
  this->thermostat_knob_ = knob;
  this->thermostat_minus_ = minus;
  this->thermostat_plus_ = plus;
}

bool MaterialEntityControlComponent::setup_thermostat_view_() {
  if (this->thermostat_root_ == nullptr)
    return true;
  if (lv_obj_get_parent(this->thermostat_root_) != this->root_ || this->thermostat_arc_ == nullptr ||
      !lv_obj_check_type(this->thermostat_arc_, &lv_arc_class) ||
      lv_obj_get_parent(this->thermostat_arc_) != this->thermostat_root_)
    return false;
  for (auto *label : {this->thermostat_target_, this->thermostat_current_, this->thermostat_activity_,
                      this->thermostat_limits_, this->thermostat_title_}) {
    if (label == nullptr || !lv_obj_check_type(label, &lv_label_class) ||
        lv_obj_get_parent(label) != this->thermostat_root_)
      return false;
  }
  for (auto *input : {this->thermostat_knob_, this->thermostat_minus_, this->thermostat_plus_}) {
    if (input == nullptr || lv_obj_get_parent(input) != this->thermostat_root_)
      return false;
    lv_obj_add_event_cb(input, input_event_cb_, LV_EVENT_ALL, this);
  }
  lv_obj_add_flag(this->thermostat_knob_, LV_OBJ_FLAG_PRESS_LOCK);
  lv_obj_set_ext_click_area(this->thermostat_knob_, 12);
  lv_obj_add_flag(this->thermostat_root_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(this->thermostat_arc_, LV_OBJ_FLAG_CLICKABLE);
  lv_arc_set_range(this->thermostat_arc_, 0, 1000);
  return true;
}

void MaterialEntityControlComponent::populate_thermostat_features(ArduinoJson::JsonObject root, uint8_t version) {
  if (!this->setup_ready_ || this->thermostat_root_ == nullptr || !this->owner_.is_bound() ||
      !this->owner_.is_generic())
    return;
  root["thermostat_version"] = version;
  root["channel"] = this->owner_.bound_channel().bytes.data();
}

void MaterialEntityControlComponent::update_thermostat_view_(const ControlThermostatSnapshot &snapshot) {
  const auto set_label = [](lv_obj_t *label, const char *text) {
    if (std::strcmp(lv_label_get_text(label), text) != 0)
      lv_label_set_text(label, text);
  };
  const bool comma = std::strcmp(this->text_(15, "."), ",") == 0;
  const auto localize = [comma](const auto &source) {
    auto text = source.bytes;
    if (comma) {
      for (char &character : text) {
        if (character == '.')
          character = ',';
      }
    }
    return text;
  };
  const auto current = localize(snapshot.current);
  const auto minimum = localize(snapshot.minimum);
  const auto maximum = localize(snapshot.maximum);
  const char *unit = snapshot.fahrenheit ? "°F" : "°C";
  char text[80];
  const auto &state = this->owner_.state();
  set_label(this->thermostat_title_, state.title.bytes.data());
  std::snprintf(text, sizeof(text), "%s %s %s", this->text_(17, "Now"), current[0] != 0 ? current.data() : "--", unit);
  set_label(this->thermostat_current_, text);
  set_label(this->thermostat_activity_, this->text_(19 + snapshot.activity, ""));
  std::snprintf(text, sizeof(text), "%s – %s %s", minimum.data(), maximum.data(), unit);
  set_label(this->thermostat_limits_, minimum[0] != 0 && maximum[0] != 0 ? text : "");
  if (this->owner_.can_drag_thermostat()) {
    lv_obj_add_flag(this->thermostat_knob_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_state(this->thermostat_knob_, LV_STATE_DISABLED);
  } else {
    lv_obj_remove_flag(this->thermostat_knob_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_state(this->thermostat_knob_, LV_STATE_DISABLED);
  }
  for (auto *button : {this->thermostat_minus_, this->thermostat_plus_}) {
    if (this->owner_.can_step(button == this->thermostat_minus_ ? -1 : 1))
      lv_obj_remove_state(button, LV_STATE_DISABLED);
    else
      lv_obj_add_state(button, LV_STATE_DISABLED);
  }
  if (this->thermostat_pending_preview_ && this->owner_.is_command_pending()) {
    this->show_thermostat_preview_();
    return;
  }
  this->thermostat_pending_preview_ = false;
  int32_t level = snapshot.level == ControlThermostatSnapshot::UNKNOWN_LEVEL ? -1 : snapshot.level * 10;
  const auto *number = state.family_if<ControlNumberSnapshot>();
  if (number != nullptr && number->available && number->domain.max_index != 0)
    level = number->index * 1000 / number->domain.max_index;
  this->update_thermostat_target_(snapshot.target.bytes.data(), snapshot.fahrenheit, level);
}

void MaterialEntityControlComponent::update_thermostat_target_(const char *target, bool fahrenheit, int32_t level) {
  char text[36];
  std::snprintf(text, sizeof(text), "%s %s", target[0] != 0 ? target : "--", fahrenheit ? "°F" : "°C");
  if (std::strcmp(this->text_(15, "."), ",") == 0) {
    if (auto *decimal = std::strchr(text, '.'); decimal != nullptr)
      *decimal = ',';
  }
  if (std::strcmp(this->thermostat_value_text_.data(), text) != 0) {
    std::strcpy(this->thermostat_value_text_.data(), text);
    lv_label_set_text_static(this->thermostat_target_, this->thermostat_value_text_.data());
  }
  const int32_t arc_value = level < 0 ? 0 : level;
  const bool changed = lv_arc_get_value(this->thermostat_arc_) != arc_value;
  if (changed)
    lv_arc_set_value(this->thermostat_arc_, arc_value);
  if (level < 0) {
    lv_obj_add_flag(this->thermostat_knob_, LV_OBJ_FLAG_HIDDEN);
  } else if (changed || lv_obj_has_flag(this->thermostat_knob_, LV_OBJ_FLAG_HIDDEN)) {
    lv_obj_remove_flag(this->thermostat_knob_, LV_OBJ_FLAG_HIDDEN);
    lv_arc_align_obj_to_angle(this->thermostat_arc_, this->thermostat_knob_, 0);
  }
}

bool MaterialEntityControlComponent::thermostat_pointer_angle_(lv_indev_t *indev, int32_t &angle) const {
  return arc_pointer_angle_(this->thermostat_arc_, indev, angle);
}
uint32_t MaterialEntityControlComponent::arc_preview_index_(int32_t angle, int32_t span, uint32_t max_index) {
  return span > 0 ? (static_cast<uint64_t>(std::clamp(angle, int32_t{0}, span)) * max_index + span / 2) / span : 0;
}
bool MaterialEntityControlComponent::arc_pointer_angle_(lv_obj_t *arc, lv_indev_t *indev, int32_t &angle) {
  lv_area_t bounds;
  lv_obj_get_coords(arc, &bounds);
  lv_point_t point;
  lv_indev_get_point(indev, &point);
  const int32_t x = point.x - (bounds.x1 + bounds.x2) / 2;
  const int32_t y = point.y - (bounds.y1 + bounds.y2) / 2;
  const int32_t dead_radius = std::min(lv_area_get_width(&bounds), lv_area_get_height(&bounds)) / 4;
  if (x * x + y * y <= dead_radius * dead_radius)
    return false;
  angle = lv_atan2(y, x);
  return true;
}

bool MaterialEntityControlComponent::begin_thermostat_input_(lv_indev_t *indev, uint32_t pointer, uint32_t now) {
  const auto start = lv_arc_get_bg_angle_start(this->thermostat_arc_);
  const auto end = lv_arc_get_bg_angle_end(this->thermostat_arc_);
  this->thermostat_span_ = static_cast<int32_t>(end - start);
  if (this->thermostat_span_ <= 0)
    this->thermostat_span_ += 360;
  if (!this->thermostat_pointer_angle_(indev, this->thermostat_last_angle_) ||
      !this->owner_.begin_thermostat_drag(pointer, now))
    return false;
  const auto *selected_family = this->owner_.state().family_if<ControlNumberSnapshot>();
  if (selected_family == nullptr)
    return false;
  const auto &number = *selected_family;
  this->thermostat_angle_valid_ = true;
  this->thermostat_preview_index_ = number.index;
  this->thermostat_angle_ =
      (number.index * this->thermostat_span_ + number.domain.max_index / 2) / number.domain.max_index;
  if (this->status_label_ != nullptr)
    lv_label_set_text(this->status_label_, this->text_(28, "Preview. Release to set target."));
  return true;
}

void MaterialEntityControlComponent::update_thermostat_input_(lv_indev_t *indev) {
  const auto *number = this->owner_.state().family_if<ControlNumberSnapshot>();
  if (number == nullptr)
    return;
  int32_t angle;
  if (!this->thermostat_pointer_angle_(indev, angle)) {
    this->thermostat_angle_valid_ = false;
    return;
  }
  const int32_t delta = (angle - this->thermostat_last_angle_ + 540) % 360 - 180;
  this->thermostat_last_angle_ = angle;
  if (!this->thermostat_angle_valid_) {
    this->thermostat_angle_valid_ = true;
    return;
  }
  if (delta == 0)
    return;
  this->thermostat_angle_ = std::clamp(this->thermostat_angle_ + delta, int32_t{0}, this->thermostat_span_);
  const auto max_index = number->domain.max_index;
  const uint32_t index = arc_preview_index_(this->thermostat_angle_, this->thermostat_span_, max_index);
  if (index != this->thermostat_preview_index_) {
    this->thermostat_preview_index_ = index;
    this->show_thermostat_preview_();
  }
}

void MaterialEntityControlComponent::show_thermostat_preview_() {
  const auto *selected_family = this->owner_.state().family_if<ControlNumberSnapshot>();
  if (selected_family == nullptr)
    return;
  const auto &number = *selected_family;
  int64_t value;
  char text[24];
  if (number.domain.max_index != 0 && number.domain.value_at(this->thermostat_preview_index_, value) &&
      number_format_decimal(value, text, sizeof(text)))
    this->update_thermostat_target_(text, number.thermostat.fahrenheit,
                                    this->thermostat_preview_index_ * 1000 / number.domain.max_index);
}
#endif

}  // namespace esphome::lvgl_material

#endif
