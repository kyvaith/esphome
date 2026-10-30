#include "material_entity_control_component.h"

#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER

#include "esphome/core/hal.h"
#include <cstring>

namespace esphome::lvgl_material {

bool ControlViewLifecycle::handoff_backend(ControlViewBackend *previous, ControlViewBackend *next) {
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

bool MaterialEntityControlComponent::activate_view_backend_(bool number) {
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
  return this->view_lifecycle_.handoff_backend(previous, next);
}

bool MaterialEntityControlComponent::open_entity_(uint32_t slot, std::string_view card_id, uint64_t revision,
                                                  ControlEntityKind kind) {
  if (!this->setup_ready_ || !this->number_profile_ || this->output_callback_active_ ||
      (!this->owner_.is_generic() && kind == ControlEntityKind::CONTROL_ENTITY_NUMBER) || slot < 2 || slot > 15)
    return false;
  ControlContext context;
  context.slot = static_cast<uint8_t>(slot);
  context.revision = revision;
  context.control_kind = kind;
  if (!context.card_id.assign(card_id) || !control_valid_context(context) ||
      !this->activate_view_backend_(kind == ControlEntityKind::CONTROL_ENTITY_NUMBER))
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
      !this->is_presented_() || (!this->owner_.is_number() && this->scalar_backend_ != nullptr))
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
  return root.size() == 6 && kinds.size() == 2;
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
  if (!this->codec_.parse_entity(payload, message))
    return false;
  switch (message.kind) {
    case ControlKind::CONTROL_KIND_GRANT:
    case ControlKind::CONTROL_KIND_STATE:
    case ControlKind::CONTROL_KIND_RESULT:
    case ControlKind::CONTROL_KIND_CLOSE:
    case ControlKind::CONTROL_KIND_CANCEL_OPEN:
      break;
    default:
      return false;
  }
  const bool held_number_step = this->owner_.is_number() && this->owner_.is_dragging();
  auto *const held_target = this->captured_target_;
  auto *const held_indev = this->captured_indev_;
  const uint32_t held_pointer = this->pointer_sequence_;
  const bool accepted = this->owner_.receive(message, millis());
  if (held_number_step && !this->owner_.is_dragging() && held_indev != nullptr &&
      (held_target == this->on_button_ || held_target == this->off_button_) && this->captured_target_ == held_target &&
      this->captured_indev_ == held_indev && this->pointer_sequence_ == held_pointer) {
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
  const auto &number = this->owner_.state().number;
  const bool known = selected && number.available;
  hidden(this->value_, !known);
  hidden(this->unknown_value_, known);
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
  const uint8_t status = static_cast<uint8_t>(this->status_);
  const char *fallback =
      this->status_texts_[status].empty() ? this->get_status_key() : this->status_texts_[status].c_str();
  set_label(this->status_label_, this->text_(status + 2, fallback));
  this->texts_pending_ = false;
}

}  // namespace esphome::lvgl_material

#endif
