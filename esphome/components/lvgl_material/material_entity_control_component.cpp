#include "material_entity_control_component.h"

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cstdio>
#include <cstring>
#include <limits>

namespace esphome::lvgl_material {

#ifdef USE_LVGL_NAVIGATION_ADMISSION_GUARD
bool ControlViewLifecycle::configure(ControlViewBackend *backend, lvgl::LvglNavigation *navigation,
                                     lvgl::LvglApplication *application) {
  // A disabled or unrelated presenter must not steal a live view's backend.
  if (this->backend_ != nullptr && this->backend_ != backend)
    return false;
  this->backend_ = backend;
  this->navigation_ = navigation;
  this->application_ = application;
  return true;
}

void ControlViewLifecycle::attach_application_callbacks() {
  this->application_->add_on_prepare_close_callback([this]() {
    // Capture the generation actually admitted into this view, never a newer
    // grant that may arrive while the old view is transitioning away.
    this->closing_generation_ = this->generation_;
    this->cancelled_close_generation_ = 0;
  });
  this->application_->add_on_closed_callback([this]() {
    const uint32_t captured = this->closing_generation_;
    this->closing_generation_ = this->cancelled_close_generation_ = 0;
    if (this->control_->is_selection_current(captured))
      this->control_->invalidate(ControlReason::CONTROL_REASON_NAVIGATION);
  });
  this->application_->add_on_close_cancelled_callback([this]() {
    // Only this actual lifecycle event admits one bounded recovery attempt.
    // No capture/overlay/failure notification can create this pending intent.
    this->cancelled_close_generation_ = this->closing_generation_;
    this->closing_generation_ = 0;
  });
}

bool ControlViewLifecycle::suspend_for_snapshot(uint32_t timeout_ms) {
  this->cancel_native_power_input();
  const auto selection = this->control_->get_selection_generation();
  if (selection != 0)
    this->control_->set_view_ready(selection, false);
  return this->backend_ != nullptr && this->backend_->suspend_control_view_backend(timeout_ms);
}

bool ControlViewLifecycle::resume(uint32_t generation) {
  return this->backend_ != nullptr && this->backend_->resume_control_view_backend(generation);
}

bool ControlViewLifecycle::prepare_navigation(lvgl::NavigationAdmissionKind kind, lvgl::LvglApplication *application) {
  const bool opening_own = kind == lvgl::NavigationAdmissionKind::OPEN && application == this->application_;
  if (opening_own)
    this->closing_generation_ = 0;
  if (!opening_own && kind != lvgl::NavigationAdmissionKind::CAPTURE && kind != lvgl::NavigationAdmissionKind::CLOSE &&
      kind != lvgl::NavigationAdmissionKind::TAKEOVER && this->control_->owner().has_selection())
    this->control_->invalidate(ControlReason::CONTROL_REASON_NAVIGATION);
  if (!this->suspend_for_snapshot())
    return false;
  // An old session's frozen thumbnail must not seed another selection's opening.
  return !opening_own || this->backend_->hide_control_view_alias();
}

bool ControlViewLifecycle::suspend_for_direct_overlay() {
  this->overlay_generation_ = this->control_->get_selection_generation();
  return this->suspend_for_snapshot();
}

void ControlViewLifecycle::resume_after_direct_overlay() {
  const uint32_t captured = this->overlay_generation_;
  this->overlay_generation_ = 0;
  if (captured != 0 && this->control_->is_selection_current(captured))
    this->resume(captured);
}

bool ControlViewLifecycle::begin_native_power_input(uint32_t generation) {
  if (this->backend_ == nullptr || !this->backend_->has_native_power_fence())
    return true;
  if (this->power_generation_ != 0 || !this->control_->is_selection_current(generation) ||
      !this->control_->owner().is_view_ready())
    return false;
  if (!this->backend_->begin_native_power_input(generation)) {
    this->control_->set_view_ready(generation, false);
    return false;
  }
  this->power_generation_ = generation;
  this->power_end_pending_ = false;
  return true;
}

void ControlViewLifecycle::finish_native_power_input(uint32_t generation) {
  if (this->power_generation_ == generation && generation != 0)
    this->power_end_pending_ = true;
}

void ControlViewLifecycle::loop() {
  if (this->power_end_pending_) {
    const uint32_t captured = this->power_generation_;
    this->cancel_native_power_input();
    if (this->control_->is_selection_current(captured) && this->control_->owner().is_view_ready()) {
      const bool presented =
          this->overlay_generation_ == 0 && this->navigation_->is_application_open(this->application_) &&
          !this->navigation_->is_transition_active() && this->backend_->control_view_region_visible();
      if (!presented || !this->backend_->finish_native_power_input(captured))
        this->control_->set_view_ready(captured, false);
    }
  }
  if (this->cancelled_close_generation_ != 0) {
    const uint32_t captured = this->cancelled_close_generation_;
    if (!this->control_->is_selection_current(captured) || this->overlay_generation_ != 0 ||
        !this->navigation_->is_application_open(this->application_) || !this->backend_->control_view_region_visible()) {
      this->cancelled_close_generation_ = 0;
    } else if (!this->navigation_->is_transition_active()) {
      this->cancelled_close_generation_ = 0;
      this->resume(captured);  // Exactly one settled, captured-generation attempt.
    }
  }
}
#endif

static constexpr uint32_t EVENT_BIT(ControlViewEvent event) { return 1U << static_cast<uint8_t>(event); }

void MaterialEntityControlComponent::set_widgets(lv_obj_t *root, lv_obj_t *hit, lv_obj_t *title, lv_obj_t *value,
                                                 lv_obj_t *unknown_value, lv_obj_t *on_button, lv_obj_t *off_button) {
  this->root_ = root;
  this->hit_ = hit;
  this->title_ = title;
  this->value_ = value;
  this->unknown_value_ = unknown_value;
  this->on_button_ = on_button;
  this->off_button_ = off_button;
}

void MaterialEntityControlComponent::set_status_text(uint8_t status, const char *text) {
  if (status < this->status_texts_.size())
    this->status_texts_[status] = StringRef(text);
}

void MaterialEntityControlComponent::setup() {
  if (this->lvgl_ == nullptr || this->root_ == nullptr || this->hit_ == nullptr || this->title_ == nullptr ||
      this->value_ == nullptr || this->unknown_value_ == nullptr || this->on_button_ == nullptr ||
      this->off_button_ == nullptr || this->hit_ == this->on_button_ || this->hit_ == this->off_button_ ||
      this->on_button_ == this->off_button_) {
    this->mark_failed(LOG_STR("Invalid control widget bindings"));
    return;
  }
  // A native slider or button can mutate its pressed/value drawing on every
  // motion. Only a neutral object is admitted for this fixed hit surface.
  if (!lv_obj_check_type(this->hit_, &lv_obj_class)) {
    this->mark_failed(LOG_STR("Control hit surface must be a neutral object"));
    return;
  }
  lv_obj_add_flag(this->hit_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(this->hit_, LV_OBJ_FLAG_SCROLLABLE);
  for (auto *widget : {this->hit_, this->on_button_, this->off_button_})
    lv_obj_add_event_cb(widget, input_event_cb_, LV_EVENT_ALL, this);
  lv_obj_add_event_cb(this->root_, input_event_cb_, LV_EVENT_DELETE, this);
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  if (!this->setup_number_profile_()) {
    this->mark_failed(LOG_STR("Invalid native Number profile"));
    return;
  }
#endif
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
  if (!this->setup_choice_profile_()) {
    this->mark_failed(LOG_STR("Invalid choice profile"));
    return;
  }
#endif
#ifdef USE_LVGL_MATERIAL_FAN_CONTROLS
  if (!this->setup_fan_profile_()) {
    this->mark_failed(LOG_STR("Invalid fan profile"));
    return;
  }
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS
  if (!this->setup_humidifier_profile_()) {
    this->mark_failed(LOG_STR("Invalid humidifier profile"));
    return;
  }
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
  if (!this->setup_robot_profile_()) {
    this->mark_failed(LOG_STR("Invalid robot profile"));
    return;
  }
#endif
#ifdef USE_LVGL_MATERIAL_EXTRA_CONTROLS
  if (!this->setup_extra_profile_()) {
    this->mark_failed(LOG_STR("Invalid extra panel profile"));
    return;
  }
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
  if (!this->setup_panel_profile_()) {
    this->mark_failed(LOG_STR("Invalid panel profile"));
    return;
  }
#endif
#ifdef USE_LVGL_MATERIAL_COVER_CONTROLS
  if (!this->setup_cover_profile_()) {
    this->mark_failed(LOG_STR("Invalid cover profile"));
    return;
  }
#endif
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
  if (!this->setup_detail_profile_()) {
    this->mark_failed(LOG_STR("Invalid detail profile"));
    return;
  }
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
  if (!this->setup_climate_range_profile_()) {
    this->mark_failed(LOG_STR("Invalid Climate Range profile"));
    return;
  }
#endif
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
  if (!this->setup_light_color_profile_()) {
    this->mark_failed(LOG_STR("Invalid Light Color profile"));
    return;
  }
#endif
#endif
#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
  if (!this->setup_datetime_profile_()) {
    this->mark_failed(LOG_STR("Invalid datetime profile"));
    return;
  }
#endif
#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS
  if (!this->setup_action_profile_()) {
    this->mark_failed(LOG_STR("Invalid action profile"));
    return;
  }
#endif
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
  if (!this->setup_media_profile_()) {
    this->mark_failed(LOG_STR("Invalid media profile"));
    return;
  }
#endif
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
  if (!this->setup_text_profile_()) {
    this->mark_failed(LOG_STR("Invalid text profile"));
    return;
  }
#endif
  this->setup_ready_ = true;
  this->enable(this->initial_enabled_);
  this->update_static_();
}

void MaterialEntityControlComponent::loop() {
  if (!this->setup_ready_)
    return;
  this->owner_.tick(App.get_loop_component_start_time());
  if (this->owner_.is_view_ready() && !lv_obj_is_visible(this->root_)) {
    // Hidden views immediately lose admission. A future late callback
    // must not paint or enable an application that is no longer presented.
    this->invalidate(ControlReason::CONTROL_REASON_NAVIGATION);
  }
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
  this->dispatch_detail_facet_();
#endif
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
  this->dispatch_media_facet_();
#endif
  this->process_events_();
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  if (this->number_profile_ && this->view_lifecycle_.is_backend(this))
    this->view_lifecycle_.loop();
#endif
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
  if (this->text_preview_pending_)
    this->service_text_preview_();
#endif
#ifdef USE_LVGL_MATERIAL_TIMER_PANEL
  const auto *panel = this->owner_.state().family_if<ControlSnapshot::Panel>();
  if (App.get_loop_component_start_time() - this->timer_clock_updated_ >= 1000 && this->owner_.is_panel() &&
      panel != nullptr && panel->type == 0 && this->owner_.is_view_ready() && !this->owner_.is_dragging() &&
      this->is_presented_()) {
    this->timer_clock_updated_ = App.get_loop_component_start_time();
    this->update_timer_clock_();
  }
#endif
}

void MaterialEntityControlComponent::on_shutdown() {
  if (!this->setup_ready_)
    return;
  this->invalidate(ControlReason::CONTROL_REASON_UNLOAD);
  this->owner_.set_enabled(false);
  this->setup_ready_ = false;
}

void MaterialEntityControlComponent::dump_config() {
  ESP_LOGCONFIG("lvgl_material.entity_control", "Bounded entity control (opt-in)");
}

bool MaterialEntityControlComponent::enable(bool enabled) {
  if (!this->setup_ready_ || this->output_callback_active_ || (enabled && this->message_callbacks_.empty()))
    return false;
  const bool accepted = this->owner_.set_enabled(enabled);
  this->process_events_();
  this->update_static_();
  return accepted;
}

bool MaterialEntityControlComponent::serialize_(const ControlMessage &message) {
  size_t length = 0;
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  const bool accepted =
      message.generic
          ? this->number_profile_ &&
                this->codec_.serialize_entity(message, this->output_.data(), this->output_.size(), length,
                                              this->owner_.choice_empty_active(), this->owner_.cover_panel_active())
          : this->codec_.serialize(message, this->output_.data(), this->output_.size(), length);
  if (!accepted)
#else
  if (!this->codec_.serialize(message, this->output_.data(), this->output_.size(), length))
#endif
    return false;
  this->output_[length] = 0;
  return true;
}

const char *MaterialEntityControlComponent::capability_json() {
  if (this->output_callback_active_)
    return nullptr;
  ControlMessage message;
  message.kind = ControlKind::CONTROL_KIND_CAPABILITY;
  message.enabled = this->setup_ready_ && this->owner_.is_enabled() && !this->is_failed();
  return this->serialize_(message) ? this->output_.data() : nullptr;
}

const char *MaterialEntityControlComponent::bind_json(std::string_view payload) {
  if (!this->setup_ready_ || this->output_callback_active_)
    return nullptr;
  this->bind_response_prepared_ = false;
  ControlMessage message;
  if (!this->codec_.parse(payload, message) || message.kind != ControlKind::CONTROL_KIND_BIND ||
      !this->owner_.receive(message, millis()))
    return nullptr;
  this->process_events_();
  this->update_static_();
  return this->serialize_(message) ? this->output_.data() : nullptr;
}

bool MaterialEntityControlComponent::prepare_bind_response(std::string_view payload) {
  const char *response = this->bind_json(payload);
  ControlMessage message;
  if (response == nullptr || !this->codec_.parse(response, message))
    return false;
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  this->bind_response_generic_ = false;
#endif
  this->bind_response_channel_ = message.identity.channel;
  this->bind_response_prepared_ = true;
  return true;
}

bool MaterialEntityControlComponent::populate_status(ArduinoJson::JsonObject root) {
  if (this->output_callback_active_)
    return false;
  root.clear();
  root["v"] = 1;
  root["type"] = "light_brightness_v1";
  root["kind"] = "capability";
  root["enabled"] = this->setup_ready_ && this->owner_.is_enabled() && !this->is_failed();
  root["max_payload_bytes"] = CONTROL_MAX_PAYLOAD_BYTES;
  return root.size() == 5 && root["v"].is<int>() && root["v"].as<int>() == 1 && root["enabled"].is<bool>() &&
         root["max_payload_bytes"].as<size_t>() == CONTROL_MAX_PAYLOAD_BYTES;
}

bool MaterialEntityControlComponent::populate_bind_response(ArduinoJson::JsonObject root) {
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  if (this->bind_response_generic_)
    return false;
#endif
  if (!this->bind_response_prepared_ || this->output_callback_active_ || !this->setup_ready_ ||
      !this->owner_.is_bound())
    return false;
  this->bind_response_prepared_ = false;
  root.clear();
  root["v"] = 1;
  root["type"] = "light_brightness_v1";
  root["kind"] = "bind";
  root["channel"] = this->bind_response_channel_.bytes.data();
  const char *channel = root["channel"].as<const char *>();
  return root.size() == 4 && channel != nullptr && this->bind_response_channel_.view() == channel;
}

bool MaterialEntityControlComponent::receive(std::string_view payload) {
  if (!this->setup_ready_ || this->output_callback_active_)
    return false;
  ControlMessage message;
  if (!this->codec_.parse(payload, message))
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
  const bool accepted = this->owner_.receive(message, millis());
  this->process_events_();
  return accepted;
}

bool MaterialEntityControlComponent::open(uint32_t slot, std::string_view card_id, uint64_t revision) {
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  if (this->number_profile_)
    return this->open_entity_(slot, card_id, revision, ControlEntityKind::CONTROL_ENTITY_LIGHT);
#endif
  if (!this->setup_ready_ || this->output_callback_active_ || slot < 2 || slot > 31)
    return false;
  ControlContext context;
  context.slot = static_cast<uint8_t>(slot);
  context.revision = revision;
  if (!context.card_id.assign(card_id))
    return false;
  const bool accepted = this->owner_.request_open(context, millis());
  this->process_events_();
  this->update_static_();
  return accepted;
}

bool MaterialEntityControlComponent::set_view_ready(uint32_t generation, bool ready) {
  if (!this->setup_ready_ || this->output_callback_active_ || !this->is_selection_current(generation) ||
      (ready && !this->is_presented_()))
    return false;
  const bool accepted = this->owner_.set_view_ready(ready, millis());
  if (accepted) {
    if (ready)
      this->request_render_();
    else {
      this->cancel_pointer_();
      this->owner_.set_scalar_ready(false);
      this->cancel_render_();
    }
  }
  this->process_events_();
  this->update_static_();
  return accepted;
}

bool MaterialEntityControlComponent::render_ready(uint32_t request_id, bool ready) {
  if (!this->setup_ready_ || this->output_callback_active_ || request_id == 0 ||
      request_id != this->pending_render_id_ || !this->owner_.has_selection() || !this->owner_.is_view_ready() ||
      !this->is_presented_())
    return false;
  this->pending_render_id_ = 0;
  const bool accepted = this->owner_.set_scalar_ready(ready);
  this->process_events_();
  this->update_static_();
  return accepted;
}

bool MaterialEntityControlComponent::invalidate(ControlReason reason) {
  if (!this->setup_ready_ || this->output_callback_active_)
    return false;
  const bool accepted = this->owner_.invalidate_admission(reason);
  this->cancel_pointer_();
  this->cancel_render_();
  this->process_events_();
  this->update_static_();
  return accepted;
}

bool MaterialEntityControlComponent::disconnect() {
  if (!this->setup_ready_ || this->output_callback_active_)
    return false;
  const bool accepted = this->owner_.disconnect();
  this->bind_response_prepared_ = false;
  this->cancel_pointer_();
  this->cancel_render_();
  this->process_events_();
  this->update_static_();
  return accepted;
}

bool MaterialEntityControlComponent::refresh_texts() {
  if (!this->setup_ready_ || this->output_callback_active_)
    return false;
  this->texts_pending_ = true;
  if (!this->owner_.is_dragging())
    this->update_static_();
  return true;
}

#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
bool MaterialEntityControlComponent::emit_text(const ControlMessage &message, ControlTextValueView value) {
  if (this->output_callback_active_ || this->message_callbacks_.empty() || !this->number_profile_ || !message.generic ||
      this->text_storage_ == nullptr)
    return false;
  size_t length = 0;
  if (!this->codec_.serialize_entity(message, this->output_.data(), this->output_.size(), length,
                                     this->owner_.choice_empty_active(), this->owner_.cover_panel_active(), value))
    return false;
  this->output_[length] = 0;
  this->output_callback_active_ = true;
  this->message_callbacks_.call(this->output_.data());
  this->output_callback_active_ = false;
  return true;
}
#endif

bool MaterialEntityControlComponent::emit(const ControlMessage &message) {
  if (this->output_callback_active_ || this->message_callbacks_.empty() || !this->serialize_(message))
    return false;
  this->output_callback_active_ = true;
  this->message_callbacks_.call(this->output_.data());
  this->output_callback_active_ = false;
  // This is synchronous dispatch to configured transport automation only.
  // It is neither HA acceptance nor evidence of physical application.
  return true;
}

void MaterialEntityControlComponent::view_event(ControlViewEvent event) {
  if (event == ControlViewEvent::CONTROL_VIEW_EVENT_GRANTED)
    this->selection_generation_ = this->owner_.admission_generation();
  else if (event == ControlViewEvent::CONTROL_VIEW_EVENT_CLOSED) {
    this->closed_generation_ = this->selection_generation_;
    this->selection_generation_ = 0;
  }
  this->events_ |= EVENT_BIT(event);
}

void MaterialEntityControlComponent::cancel_render_() {
  this->pending_render_id_ = 0;
  this->render_cancel_callbacks_.call();
}

void MaterialEntityControlComponent::request_render_() {
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  if (this->owner_.is_discrete()
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
      && !this->media_volume_active_()
#endif
  )
    return;
#endif
  if (!this->owner_.has_selection() || !this->owner_.is_view_ready() || !this->is_presented_())
    return;
  if (this->render_request_id_ == std::numeric_limits<uint32_t>::max() || this->render_callbacks_.empty()) {
    this->owner_.set_scalar_ready(false);
    this->pending_render_id_ = 0;
    return;
  }
  this->pending_render_id_ = ++this->render_request_id_;
  this->render_callbacks_.call(this->pending_render_id_, this->owner_.candidate_known(), this->owner_.candidate());
}

void MaterialEntityControlComponent::process_events_() {
  if (this->processing_events_)
    return;
  this->processing_events_ = true;
  while (this->events_ != 0) {
    const uint32_t events = this->events_;
    this->events_ = 0;
    if (events & EVENT_BIT(ControlViewEvent::CONTROL_VIEW_EVENT_CLOSED)) {
      this->cancel_pointer_();
      this->cancel_render_();
      this->closed_callbacks_.call(this->closed_generation_);
    }
    if ((events & EVENT_BIT(ControlViewEvent::CONTROL_VIEW_EVENT_GRANTED)) && this->owner_.has_selection())
      this->granted_callbacks_.call(this->selection_generation_);
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
    this->reconcile_detail_backend_();
#endif
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
    this->reconcile_media_backend_();
#endif
    bool native_update = (events & ~EVENT_BIT(ControlViewEvent::CONTROL_VIEW_EVENT_PREVIEW)) != 0;
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
    native_update = native_update || this->owner_.is_choice();
#endif
    if (native_update)
      this->update_static_();
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
    // The chosen root must be visible before its presenter can resume. A
    // facet receipt is not an application grant and has no on_granted action.
    this->finish_detail_handoff_();
#endif
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
    this->finish_media_handoff_();
#endif
    if ((events & (EVENT_BIT(ControlViewEvent::CONTROL_VIEW_EVENT_PREVIEW) |
                   EVENT_BIT(ControlViewEvent::CONTROL_VIEW_EVENT_SENDING))) ||
        ((events & (EVENT_BIT(ControlViewEvent::CONTROL_VIEW_EVENT_CANCELLED) |
                    EVENT_BIT(ControlViewEvent::CONTROL_VIEW_EVENT_RESULT) |
                    EVENT_BIT(ControlViewEvent::CONTROL_VIEW_EVENT_STATE))) &&
         !this->owner_.is_command_pending()))
      this->request_render_();
    // PREVIEW is consumed only by the bounded renderer. Product callbacks
    // cannot accidentally turn pointer motion into native label redraws.
    for (uint8_t index = 0; index <= static_cast<uint8_t>(ControlViewEvent::CONTROL_VIEW_EVENT_CLOSED); index++) {
      if (index != static_cast<uint8_t>(ControlViewEvent::CONTROL_VIEW_EVENT_PREVIEW) && (events & (1U << index)))
        this->view_callbacks_.call(index);
    }
  }
  this->processing_events_ = false;
}

const char *MaterialEntityControlComponent::text_(uint8_t index, const char *fallback) const {
  if (!this->text_provider_)
    return fallback;
  const char *text = this->text_provider_(index);
  if (text == nullptr)
    return fallback;
  size_t size = 0;
  while (size <= 160 && text[size] != 0)
    size++;
  ControlSnapshot candidate;
  candidate.state_seq = 1;
  if (size > 160 || !candidate.title.assign({text, size}) || !control_valid_snapshot(candidate))
    return fallback;
  return text;
}

void MaterialEntityControlComponent::update_status_() {
  bool scalar_view = true;
  bool scalar_control = this->owner_.state().caps.brightness;
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
  if (this->owner_.is_media())
    scalar_control = this->media_volume_active_();
#endif
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
  if (this->owner_.is_detail()) {
    const auto *detail = this->owner_.state().family_if<ControlDetailSnapshot>();
    scalar_view = detail != nullptr && !detail->climate && detail->facet == ControlFacet::CONTROL_FACET_MAIN;
  }
#endif
  if (!this->owner_.is_enabled())
    this->status_ = ControlUiStatus::CONTROL_UI_STATUS_DISABLED;
  else if (!this->owner_.is_bound())
    this->status_ = ControlUiStatus::CONTROL_UI_STATUS_CONNECTION_LOST;
  else if (!this->owner_.has_selection())
    this->status_ = this->owner_.last_reason() == ControlReason::CONTROL_REASON_EXPIRED
                        ? ControlUiStatus::CONTROL_UI_STATUS_EXPIRED
                        : ControlUiStatus::CONTROL_UI_STATUS_WAITING;
  else if (this->owner_.is_command_pending())
    this->status_ = this->owner_.last_result() == ControlResult::CONTROL_RESULT_HA_ACCEPTED
                        ? ControlUiStatus::CONTROL_UI_STATUS_HA_ACCEPTED
                        : ControlUiStatus::CONTROL_UI_STATUS_PENDING;
  else if (this->owner_.last_result() == ControlResult::CONTROL_RESULT_STATE_MATCHES)
    this->status_ = ControlUiStatus::CONTROL_UI_STATUS_STATE_MATCHES;
  else if (this->owner_.last_result() == ControlResult::CONTROL_RESULT_ALREADY_MATCHED)
    this->status_ = ControlUiStatus::CONTROL_UI_STATUS_ALREADY_MATCHED;
  else if (this->owner_.last_result() == ControlResult::CONTROL_RESULT_NOT_CONFIRMED)
    this->status_ = ControlUiStatus::CONTROL_UI_STATUS_NOT_CONFIRMED;
  else if (this->owner_.last_result() == ControlResult::CONTROL_RESULT_REJECTED)
    this->status_ = ControlUiStatus::CONTROL_UI_STATUS_REJECTED;
  else if (scalar_view && this->owner_.is_view_ready() && scalar_control && !this->owner_.is_scalar_ready())
    this->status_ = ControlUiStatus::CONTROL_UI_STATUS_RENDERER_UNAVAILABLE;
  else
    this->status_ = ControlUiStatus::CONTROL_UI_STATUS_READY;
}

const char *MaterialEntityControlComponent::get_status_key() const {
  switch (this->status_) {
    case ControlUiStatus::CONTROL_UI_STATUS_DISABLED:
      return "disabled";
    case ControlUiStatus::CONTROL_UI_STATUS_WAITING:
      return "waiting";
    case ControlUiStatus::CONTROL_UI_STATUS_READY:
      return "ready";
    case ControlUiStatus::CONTROL_UI_STATUS_PENDING:
      return "pending";
    case ControlUiStatus::CONTROL_UI_STATUS_HA_ACCEPTED:
      return "ha_accepted";
    case ControlUiStatus::CONTROL_UI_STATUS_STATE_MATCHES:
      return "state_matches";
    case ControlUiStatus::CONTROL_UI_STATUS_ALREADY_MATCHED:
      return "already_matched";
    case ControlUiStatus::CONTROL_UI_STATUS_NOT_CONFIRMED:
      return "not_confirmed";
    case ControlUiStatus::CONTROL_UI_STATUS_REJECTED:
      return "rejected";
    case ControlUiStatus::CONTROL_UI_STATUS_EXPIRED:
      return "expired";
    case ControlUiStatus::CONTROL_UI_STATUS_RENDERER_UNAVAILABLE:
      return "renderer_unavailable";
    case ControlUiStatus::CONTROL_UI_STATUS_CONNECTION_LOST:
      return "connection_lost";
    default:
      return "disabled";
  }
}

void MaterialEntityControlComponent::update_static_() {
  if (!this->setup_ready_ || this->owner_.is_dragging())
    return;
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
  if (!this->owner_.is_text()) {
    this->restore_text_widgets_();
    if (this->text_widgets_active_)
      return;
  }
#endif
#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
  if (!this->owner_.is_datetime()) {
    this->restore_datetime_widgets_();
    if (this->datetime_widgets_active_)
      return;
  }
#endif
#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS
  if (!this->owner_.is_action()) {
    this->restore_action_widgets_();
    if (this->action_widgets_active_)
      return;
  }
#endif
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
  if (!this->owner_.is_media()) {
    this->restore_media_widgets_();
    if (this->media_widgets_active_)
      return;
  }
#endif
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
  if (!this->owner_.is_detail()) {
    this->restore_detail_widgets_();
    // A pending OPEN is not fresh state. Keep the retired Detail neutral until
    // GRANT restores its original flags before the next family takes ownership.
    if (this->detail_widgets_active_)
      return;
  }
#endif
#ifdef USE_LVGL_MATERIAL_EXTRA_CONTROLS
  if (!this->owner_.is_extra()) {
    this->restore_extra_widgets_();
    if (this->extra_widgets_active_)
      return;
  }
#endif
  // Retire the previous child group before another group saves base visibility.
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
  if (!this->owner_.is_panel())
    this->restore_panel_widgets_();
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
  if (!this->owner_.is_robot())
    this->restore_robot_widgets_();
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS
  if (!this->owner_.is_humidifier())
    this->restore_humidifier_widgets_();
#endif
#ifdef USE_LVGL_MATERIAL_FAN_CONTROLS
  if (!this->owner_.is_fan())
    this->restore_fan_widgets_();
#endif
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
  if (!this->owner_.is_choice())
    this->restore_choice_widgets_();
#endif
#ifdef USE_LVGL_MATERIAL_COVER_CONTROLS
  if (!this->owner_.is_cover())
    this->restore_cover_widgets_();
#endif
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  if (!this->owner_.is_number())
    this->restore_light_widgets_();
#endif
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
  if (this->owner_.is_text()) {
    this->update_text_static_();
    return;
  }
#endif
#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
  if (this->owner_.is_datetime()) {
    this->update_datetime_static_();
    return;
  }
#endif
#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS
  if (this->owner_.is_action()) {
    this->update_action_static_();
    return;
  }
#endif
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
  if (this->owner_.is_media()) {
    this->update_media_static_();
    return;
  }
#endif
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
  if (this->owner_.is_detail() && this->update_detail_static_())
    return;
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
  if (this->owner_.is_robot()) {
    this->update_robot_static_();
    return;
  }
#endif
#ifdef USE_LVGL_MATERIAL_EXTRA_CONTROLS
  if (this->owner_.is_extra()) {
    this->update_extra_static_();
    return;
  }
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
  if (this->owner_.is_panel()) {
    this->update_panel_static_();
    return;
  }
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS
  if (this->owner_.is_humidifier()) {
    this->update_humidifier_static_();
    return;
  }
#endif
#ifdef USE_LVGL_MATERIAL_FAN_CONTROLS
  if (this->owner_.is_fan()) {
    this->update_fan_static_();
    return;
  }
#endif
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
  if (this->owner_.is_choice()) {
    this->update_choice_static_();
    return;
  }
#endif
#ifdef USE_LVGL_MATERIAL_COVER_CONTROLS
  if (this->owner_.is_cover()) {
    this->update_cover_static_();
    return;
  }
#endif
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  if (this->number_profile_) {
    if (this->owner_.is_number()) {
      this->update_number_static_();
      return;
    }
    this->restore_light_widgets_();
  }
#endif
  const auto set_label = [](lv_obj_t *label, const char *text) {
    if (label != nullptr && std::strcmp(lv_label_get_text(label), text) != 0)
      lv_label_set_text(label, text);
  };
  const auto hidden = [](lv_obj_t *widget, bool hide) {
    if (hide != lv_obj_has_flag(widget, LV_OBJ_FLAG_HIDDEN)) {
      if (hide)
        lv_obj_add_flag(widget, LV_OBJ_FLAG_HIDDEN);
      else
        lv_obj_remove_flag(widget, LV_OBJ_FLAG_HIDDEN);
    }
  };
  const bool selected = this->owner_.has_selection();
  const bool known = selected && this->owner_.state().brightness_known;
  if (selected)
    set_label(this->title_, this->owner_.state().title.bytes.data());
  hidden(this->value_, !known);
  hidden(this->unknown_value_, known);
  if (known) {
    char number[5];
    std::snprintf(number, sizeof(number), "%u%%", static_cast<unsigned>(this->owner_.state().brightness_pct));
    set_label(this->value_, number);
  }
  if (selected) {
    uint8_t state_text = this->owner_.state().on ? 0 : 1;
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
    if (this->owner_.is_detail())
      state_text += 55;
#endif
    set_label(this->state_label_,
              this->text_(state_text, this->owner_.state().on ? this->on_text_.c_str() : this->off_text_.c_str()));
  }
  const bool ready = selected && this->owner_.is_view_ready() && !this->owner_.is_command_pending();
  for (auto *button : {this->on_button_, this->off_button_}) {
    const bool supported =
        button == this->on_button_ ? this->owner_.state().caps.turn_on : this->owner_.state().caps.turn_off;
    if (ready && supported)
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

bool MaterialEntityControlComponent::is_presented_() const {
  return this->root_ != nullptr && lv_obj_is_visible(this->root_) && !this->lvgl_->is_paused();
}

bool MaterialEntityControlComponent::pointer_value_(lv_indev_t *indev, int32_t &value) const {
  if (indev == nullptr || lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER)
    return false;
  lv_area_t bounds;
  lv_obj_get_coords(this->hit_, &bounds);
  const int32_t width = bounds.x2 - bounds.x1;
  if (width <= 0 || width > std::numeric_limits<int16_t>::max())
    return false;
  lv_point_t point;
  lv_indev_get_point(indev, &point);
  const int32_t x = point.x < bounds.x1 ? 0 : (point.x > bounds.x2 ? width : point.x - bounds.x1);
  value = (x * 100 + width / 2) / width;
  return true;
}

void MaterialEntityControlComponent::cancel_pointer_() {
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
  this->light_color_axis_ = 255;
#endif
#ifdef USE_LVGL_NAVIGATION_ADMISSION_GUARD
  if (this->captured_target_ == this->on_button_ || this->captured_target_ == this->off_button_)
    this->view_lifecycle_.finish_native_power_input(this->captured_generation_);
#endif
  this->captured_target_ = nullptr;
  this->captured_indev_ = nullptr;
  this->unknown_drag_waiting_ = false;
  this->owner_.cancel_drag();
}

void MaterialEntityControlComponent::input_event_cb_(lv_event_t *event) {
  static_cast<MaterialEntityControlComponent *>(lv_event_get_user_data(event))->input_event_(event);
}

void MaterialEntityControlComponent::input_event_(lv_event_t *event) {
  const auto code = lv_event_get_code(event);
  auto *target = lv_event_get_target_obj(event);
  if (code == LV_EVENT_DELETE) {
    this->disconnect();
    this->setup_ready_ = false;
    this->status_set_warning();
    return;
  }
  if (!this->setup_ready_ || this->output_callback_active_)
    return;
  auto *indev = lv_event_get_indev(event);
  const uint32_t now = millis();
#ifdef USE_LVGL_MATERIAL_THERMOSTAT
  const bool thermostat_knob = target == this->thermostat_knob_;
#endif
  if (code == LV_EVENT_PRESS_LOST) {
    if (target == this->captured_target_) {
      this->cancel_pointer_();
      this->process_events_();
    }
    return;
  }
#ifdef USE_LVGL_MATERIAL_INFO_AGENDA
  if (this->owner_.is_agenda()) {
    this->agenda_input_event_(code, target, indev, now);
    return;
  }
#endif
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
  if (this->owner_.is_text()) {
    this->text_input_event_(code, target, indev, now);
    return;
  }
#endif
#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
  if (this->owner_.is_datetime()) {
    this->datetime_input_event_(code, target, indev, now);
    return;
  }
#endif
#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS
  if (this->owner_.is_action()) {
    this->action_input_event_(code, target, indev, now);
    return;
  }
#endif
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
  if (this->owner_.is_media()) {
    this->media_input_event_(code, target, indev, now);
    return;
  }
#endif
  if (code == LV_EVENT_PRESSED) {
    bool priority_stop = false;
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
    priority_stop =
        this->owner_.is_robot() && ((target == this->robot_action_buttons_[1] &&
                                     this->owner_.can_robot(ControlOperation::CONTROL_OPERATION_ROBOT_PAUSE)) ||
                                    (target == this->robot_action_buttons_[2] &&
                                     this->owner_.can_robot(ControlOperation::CONTROL_OPERATION_ROBOT_STOP)));
#endif
#ifdef USE_LVGL_MATERIAL_COVER_PRIORITY_STOP
    priority_stop |= this->owner_.is_cover() && target == this->cover_buttons_[1] &&
                     this->owner_.can_cover(ControlOperation::CONTROL_OPERATION_COVER_STOP);
#ifdef USE_LVGL_MATERIAL_COVER_PANEL
    priority_stop |= this->owner_.is_cover() && target == this->cover_tilt_buttons_[1] &&
                     this->owner_.can_cover(ControlOperation::CONTROL_OPERATION_COVER_STOP_TILT);
#endif
#endif
    if (this->captured_target_ != nullptr || indev == nullptr || lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER ||
        !this->is_presented_() || !lv_obj_is_visible(target) || !this->owner_.is_view_ready() ||
        !this->owner_.has_selection() || (this->owner_.is_command_pending() && !priority_stop) ||
        this->pointer_sequence_ == std::numeric_limits<uint32_t>::max())
      return;
#ifdef USE_LVGL_NAVIGATION_ADMISSION_GUARD
    if (this->view_lifecycle_.native_power_input_active())
      return;
    if ((target == this->on_button_ || target == this->off_button_) &&
        !this->view_lifecycle_.begin_native_power_input(this->owner_.admission_generation()))
      return;
#endif
    const uint32_t pointer = ++this->pointer_sequence_;
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
    if (this->owner_.is_detail() && !this->begin_detail_input_(target, indev, pointer, now)) {
      if (target == this->on_button_ || target == this->off_button_)
        this->view_lifecycle_.finish_native_power_input(this->owner_.admission_generation());
      return;
    }
#endif
#ifdef USE_LVGL_MATERIAL_EXTRA_CONTROLS
    if (this->owner_.is_extra() && (target != this->extra_state_[11] || !this->owner_.begin_extra_lock(pointer, now)))
      return;
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
    if (this->owner_.is_panel() && !this->begin_panel_input_(target, pointer, now))
      return;
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
    if (this->owner_.is_robot() && !this->begin_robot_input_(target, pointer, now))
      return;
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS
    if (this->owner_.is_humidifier() && !this->begin_humidifier_input_(target, indev, pointer, now))
      return;
#endif
#ifdef USE_LVGL_MATERIAL_FAN_CONTROLS
    if (this->owner_.is_fan() && !this->begin_fan_input_(target, pointer, now))
      return;
#endif
#ifdef USE_LVGL_MATERIAL_COVER_CONTROLS
    if (this->owner_.is_cover()) {
#ifdef USE_LVGL_MATERIAL_COVER_PANEL
      if (this->owner_.cover_panel_active()) {
        if (!this->begin_cover_panel_input_(target, indev, pointer, now))
          return;
      } else {
#endif
        size_t index = 0;
        while (index < this->cover_buttons_.size() && this->cover_buttons_[index] != target)
          index++;
        if (index == this->cover_buttons_.size() ||
            !this->owner_.begin_cover(pointer,
                                      static_cast<ControlOperation>(
                                          static_cast<uint8_t>(ControlOperation::CONTROL_OPERATION_COVER_OPEN) + index),
                                      now))
          return;
#ifdef USE_LVGL_MATERIAL_COVER_PANEL
      }
#endif
    }
#endif
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
    if (this->owner_.is_choice()) {
      if (!this->choice_glyphs_available_())
        return;
      if (target == this->choice_apply_) {
        if (!this->owner_.begin_choice_apply(pointer, now))
          return;
      } else {
        const size_t index = this->choice_target_index_(target);
        if (index >= this->choice_buttons_.size() ||
            !this->owner_.begin_choice(pointer, static_cast<uint8_t>(index), now))
          return;
      }
    }
#endif
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
    if (this->owner_.is_number()) {
      bool minus = target == this->on_button_, plus = target == this->off_button_;
#ifdef USE_LVGL_MATERIAL_THERMOSTAT
      minus |= target == this->thermostat_minus_;
      plus |= target == this->thermostat_plus_;
      if (thermostat_knob) {
        if (!this->begin_thermostat_input_(indev, pointer, now))
          return;
      } else
#endif
          if ((!minus && !plus) || !this->owner_.begin_step(pointer, minus ? -1 : 1, now))
        return;
    }
#endif
    if (target == this->hit_ && !this->owner_.begin_drag(pointer, now))
      return;
    this->captured_target_ = target;
    this->captured_indev_ = indev;
    this->captured_generation_ = this->owner_.admission_generation();
    lv_point_t start;
    lv_indev_get_point(indev, &start);
    this->captured_start_x_ = start.x;
    this->unknown_drag_waiting_ = target == this->hit_ && !this->owner_.candidate_known();
  }
  if (target != this->captured_target_ || indev != this->captured_indev_)
    return;
  if (!this->is_presented_() || !lv_obj_is_visible(target) ||
      this->captured_generation_ != this->owner_.admission_generation()) {
    this->cancel_pointer_();
    this->process_events_();
    return;
  }
  if ((code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) && target == this->hit_) {
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    if (point.x != this->captured_start_x_)
      this->unknown_drag_waiting_ = false;
    int32_t value;
    if (!this->unknown_drag_waiting_ && this->pointer_value_(indev, value))
      this->owner_.update_candidate(this->pointer_sequence_, value, now);
  }
#ifdef USE_LVGL_MATERIAL_THERMOSTAT
  if (thermostat_knob && (code == LV_EVENT_PRESSING || code == LV_EVENT_RELEASED))
    this->update_thermostat_input_(indev);
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS
  if (this->owner_.is_humidifier() && this->is_humidifier_track_(target) &&
      (code == LV_EVENT_PRESSING || code == LV_EVENT_RELEASED))
    this->update_humidifier_input_(indev, this->pointer_sequence_, now);
#endif
#ifdef USE_LVGL_MATERIAL_COVER_PANEL
  if (this->owner_.is_cover() && this->owner_.cover_panel_active() && this->is_cover_track_(target) &&
      (code == LV_EVENT_PRESSING || code == LV_EVENT_RELEASED))
    this->update_cover_input_(target, indev, this->pointer_sequence_, now);
#endif
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
  if (this->is_detail_drag_(target) && (code == LV_EVENT_PRESSING || code == LV_EVENT_RELEASED))
    this->update_detail_input_(indev);
#endif
  if (code == LV_EVENT_RELEASED) {
#if defined(USE_LVGL_MATERIAL_ROBOT_CONTROLS) || defined(USE_LVGL_MATERIAL_PANEL_CONTROLS)
    bool bounded_button = false;
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
    bounded_button |= this->owner_.is_robot();
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
    bounded_button |= this->owner_.is_panel();
#ifdef USE_LVGL_MATERIAL_EXTRA_CONTROLS
    bounded_button |= this->owner_.is_extra();
#endif
#endif
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
    if (this->owner_.is_detail())
      bounded_button = target != this->hit_ && !this->is_detail_drag_(target);
#endif
    if (bounded_button) {
      // LVGL's press lock can deliver RELEASED outside the original button.
      // A drag away must retire this captured intent, not execute it.
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
#endif
    if (target == this->hit_) {
      lv_point_t point;
      lv_indev_get_point(indev, &point);
      if (point.x != this->captured_start_x_)
        this->unknown_drag_waiting_ = false;
      int32_t value;
      if (!this->unknown_drag_waiting_ && this->pointer_value_(indev, value))
        this->owner_.update_candidate(this->pointer_sequence_, value, now);
    }
    this->captured_target_ = nullptr;
    this->captured_indev_ = nullptr;
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
    if (this->owner_.is_detail())
      this->release_detail_input_(target, this->pointer_sequence_, now);
    else
#endif
#ifdef USE_LVGL_MATERIAL_EXTRA_CONTROLS
        if (this->owner_.is_extra())
      this->owner_.release_extra_lock(this->pointer_sequence_, now);
    else
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
        if (this->owner_.is_panel())
      this->release_panel_input_(target, this->pointer_sequence_, now);
    else
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
        if (this->owner_.is_robot())
      this->release_robot_input_(target, this->pointer_sequence_, now);
    else
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS
        if (this->owner_.is_humidifier())
      this->release_humidifier_input_(target, this->pointer_sequence_, now);
    else
#endif
#ifdef USE_LVGL_MATERIAL_FAN_CONTROLS
        if (this->owner_.is_fan())
      this->release_fan_input_(target, this->pointer_sequence_, now);
    else
#endif
#ifdef USE_LVGL_MATERIAL_COVER_CONTROLS
        if (this->owner_.is_cover())
      this->owner_.release_cover(this->pointer_sequence_, now);
    else
#endif
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
        if (this->owner_.is_choice()) {
      if (target == this->choice_apply_)
        this->owner_.release_choice_apply(this->pointer_sequence_, now);
      else
        this->owner_.release_choice(this->pointer_sequence_, now);
    } else
#endif
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
        if (this->owner_.is_number()) {
#ifdef USE_LVGL_MATERIAL_THERMOSTAT
      if (thermostat_knob) {
        this->thermostat_pending_preview_ =
            this->owner_.release_thermostat_drag(this->pointer_sequence_, this->thermostat_preview_index_, now);
        if (!this->thermostat_pending_preview_)
          this->update_static_();
      } else
#endif
        this->owner_.release_step(this->pointer_sequence_, now);
    } else
#endif
        if (target == this->hit_)
      this->owner_.release(this->pointer_sequence_, now);
    else if (target == this->on_button_)
      this->owner_.command(ControlOperation::CONTROL_OPERATION_TURN_ON, now);
    else if (target == this->off_button_)
      this->owner_.command(ControlOperation::CONTROL_OPERATION_TURN_OFF, now);
#ifdef USE_LVGL_NAVIGATION_ADMISSION_GUARD
    if (target == this->on_button_ || target == this->off_button_)
      this->view_lifecycle_.finish_native_power_input(this->captured_generation_);
#endif
  }
  this->process_events_();
}

}  // namespace esphome::lvgl_material
