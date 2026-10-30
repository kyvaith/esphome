#pragma once

#include "material_entity_control.h"
#include "material_entity_control_codec.h"
#include "esphome/components/lvgl/lvgl_esphome.h"
#include "material_control_view_lifecycle.h"
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
#ifndef USE_LVGL_NAVIGATION_ADMISSION_GUARD
#error "Number stepper requires the shared navigation admission lifecycle"
#endif
#include "esphome/components/lvgl/direct_scene_controller.h"
#endif
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/string_ref.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace esphome::lvgl_material {

enum class ControlUiStatus : uint8_t {
  CONTROL_UI_STATUS_DISABLED,
  CONTROL_UI_STATUS_WAITING,
  CONTROL_UI_STATUS_READY,
  CONTROL_UI_STATUS_PENDING,
  CONTROL_UI_STATUS_HA_ACCEPTED,
  CONTROL_UI_STATUS_STATE_MATCHES,
  CONTROL_UI_STATUS_ALREADY_MATCHED,
  CONTROL_UI_STATUS_NOT_CONFIRMED,
  CONTROL_UI_STATUS_REJECTED,
  CONTROL_UI_STATUS_EXPIRED,
  CONTROL_UI_STATUS_RENDERER_UNAVAILABLE,
  CONTROL_UI_STATUS_CONNECTION_LOST,
  CONTROL_UI_STATUS_COUNT
};

// Owns protocol and LVGL input on the main task. A renderer must acknowledge
// the exact initial request before brightness input is admitted.
class MaterialEntityControlComponent : public Component,
                                       public MaterialEntityControlSink
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
    ,
                                       public lvgl::NavigationAdmissionGuard,
                                       public lvgl::DirectSceneController,
                                       public ControlViewBackend
#endif
{
 public:
  explicit MaterialEntityControlComponent(lvgl::LvglComponent *lvgl) : lvgl_(lvgl), owner_(*this) {}
  void set_widgets(lv_obj_t *root, lv_obj_t *hit, lv_obj_t *title, lv_obj_t *value, lv_obj_t *unknown_value,
                   lv_obj_t *on_button, lv_obj_t *off_button);
  void set_state_label(lv_obj_t *label) { this->state_label_ = label; }
  void set_status_label(lv_obj_t *label) { this->status_label_ = label; }
  void set_on_text(const char *text) { this->on_text_ = StringRef(text); }
  void set_off_text(const char *text) { this->off_text_ = StringRef(text); }
  void set_status_text(uint8_t status, const char *text);
  template<typename F> void set_text_provider(F &&provider) { this->text_provider_ = std::forward<F>(provider); }
  void set_initial_enabled(bool enabled) { this->initial_enabled_ = enabled; }

  template<typename F> void add_on_message_callback(F &&callback) {
    this->message_callbacks_.add(std::forward<F>(callback));
  }
  template<typename F> void add_on_granted_callback(F &&callback) {
    this->granted_callbacks_.add(std::forward<F>(callback));
  }
  template<typename F> void add_on_closed_callback(F &&callback) {
    this->closed_callbacks_.add(std::forward<F>(callback));
  }
  template<typename F> void add_on_view_event_callback(F &&callback) {
    this->view_callbacks_.add(std::forward<F>(callback));
  }
  template<typename F> void add_on_render_request_callback(F &&callback) {
    this->render_callbacks_.add(std::forward<F>(callback));
  }
  template<typename F> void add_on_render_cancel_callback(F &&callback) {
    this->render_cancel_callbacks_.add(std::forward<F>(callback));
  }

  void setup() override;
  void loop() override;
  void on_shutdown() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR - 5.0f; }

  bool enable(bool enabled);
  bool receive(std::string_view payload);
  // Returned storage is borrowed until the next adapter call. Copy it into
  // the API response immediately. A bind response acknowledges source only.
  const char *capability_json();
  const char *bind_json(std::string_view payload);
  bool prepare_bind_response(std::string_view payload);
  bool populate_bind_response(ArduinoJson::JsonObject root);
  bool populate_status(ArduinoJson::JsonObject root);
  bool open(uint32_t slot, std::string_view card_id, uint64_t revision);
  bool set_view_ready(uint32_t generation, bool ready);
  bool render_ready(uint32_t request_id, bool ready);
  bool invalidate(ControlReason reason);
  bool disconnect();
  bool refresh_texts();
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  // All objects/fonts are existing configured widgets; no child is created.
  void set_number_profile(lvgl::LvglNavigation *navigation, lvgl::LvglApplication *application, lv_obj_t *minus_label,
                          lv_obj_t *plus_label, const lv_font_t *value_font);
  bool uses_generic_view() const { return this->number_profile_; }
  bool register_scalar_backend(ControlViewBackend *backend, lvgl::LvglNavigation *navigation,
                               lvgl::LvglApplication *application);
  bool is_registered_scalar_backend(const ControlViewBackend *backend) const {
    return this->number_profile_ && backend != nullptr && this->scalar_backend_ == backend;
  }
  bool open_number(uint32_t slot, std::string_view card_id, uint64_t revision);
  bool receive_entity(std::string_view payload);
  const char *capability_entity_json();
  const char *bind_entity_json(std::string_view payload);
  bool prepare_entity_bind_response(std::string_view payload);
  bool populate_entity_status(ArduinoJson::JsonObject root);
  bool populate_entity_bind_response(ArduinoJson::JsonObject root);
  bool resume_control_view(uint32_t generation) { return this->view_lifecycle_.resume(generation); }
  bool suspend_control_view(uint32_t timeout_ms = 250) {
    return this->view_lifecycle_.suspend_for_snapshot(timeout_ms);
  }
  bool prepare_navigation(lvgl::NavigationAdmissionKind kind, lvgl::LvglApplication *application) override;
  bool suspend_for_direct_overlay() override;
  void resume_after_direct_overlay() override;
#endif

  bool is_on() const { return this->owner_.state().on; }
  bool brightness_known() const { return this->owner_.state().brightness_known; }
  uint8_t brightness() const { return this->owner_.state().brightness_pct; }
  ControlUiStatus get_status() const { return this->status_; }
  const char *get_status_key() const;
  ControlReason last_reason() const { return this->owner_.last_reason(); }
  uint32_t get_render_request_id() const { return this->render_request_id_; }
  uint32_t get_selection_generation() const { return this->owner_.has_selection() ? this->selection_generation_ : 0; }
  bool is_selection_current(uint32_t generation) const {
    return generation != 0 && generation == this->get_selection_generation();
  }
  const MaterialEntityControl &owner() const { return this->owner_; }
#ifdef USE_LVGL_NAVIGATION_ADMISSION_GUARD
  ControlViewLifecycle &view_lifecycle() { return this->view_lifecycle_; }
  const ControlViewLifecycle &view_lifecycle() const { return this->view_lifecycle_; }
#endif

  bool emit(const ControlMessage &message) override;
  void view_event(ControlViewEvent event) override;

 protected:
  static void input_event_cb_(lv_event_t *event);
  void input_event_(lv_event_t *event);
  bool is_presented_() const;
  bool pointer_value_(lv_indev_t *indev, int32_t &value) const;
  void cancel_pointer_();
  void process_events_();
  void update_static_();
  void request_render_();
  void cancel_render_();
  void update_status_();
  bool serialize_(const ControlMessage &message);
  const char *text_(uint8_t index, const char *fallback) const;

#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  bool activate_view_backend_(bool number);
  bool open_entity_(uint32_t slot, std::string_view card_id, uint64_t revision, ControlEntityKind kind);
  bool setup_number_profile_();
  bool suspend_control_view_backend(uint32_t timeout_ms) override;
  bool resume_control_view_backend(uint32_t captured_generation) override;
  bool hide_control_view_alias() override { return true; }
  bool control_view_region_visible() const override { return this->root_ != nullptr && lv_obj_is_visible(this->root_); }
  void update_number_static_();
  void restore_light_widgets_();
  lvgl::LvglNavigation *navigation_{nullptr};
  lvgl::LvglApplication *application_{nullptr};
  ControlViewBackend *scalar_backend_{nullptr};
  lv_obj_t *minus_label_{nullptr};
  lv_obj_t *plus_label_{nullptr};
  const lv_font_t *number_value_font_{nullptr};
  const lv_font_t *light_value_font_{nullptr};
  const lv_font_t *light_unknown_font_{nullptr};
  bool number_profile_{false};
  bool number_widgets_active_{false};
  bool bind_response_generic_{false};
#endif

  lvgl::LvglComponent *lvgl_;
  MaterialEntityControl owner_;
#ifdef USE_LVGL_NAVIGATION_ADMISSION_GUARD
  ControlViewLifecycle view_lifecycle_{this};
#endif
  MaterialEntityControlCodec codec_;
  lv_obj_t *root_{nullptr};
  lv_obj_t *hit_{nullptr};
  lv_obj_t *title_{nullptr};
  lv_obj_t *value_{nullptr};
  lv_obj_t *unknown_value_{nullptr};
  lv_obj_t *on_button_{nullptr};
  lv_obj_t *off_button_{nullptr};
  lv_obj_t *state_label_{nullptr};
  lv_obj_t *status_label_{nullptr};
  lv_obj_t *captured_target_{nullptr};
  lv_indev_t *captured_indev_{nullptr};
  uint32_t captured_generation_{0};
  uint32_t pointer_sequence_{0};
  int32_t captured_start_x_{0};
  uint32_t events_{0};
  uint32_t render_request_id_{0};
  uint32_t pending_render_id_{0};
  uint32_t selection_generation_{0};
  uint32_t closed_generation_{0};
  StringRef on_text_{"On"};
  StringRef off_text_{"Off"};
  std::array<StringRef, static_cast<size_t>(ControlUiStatus::CONTROL_UI_STATUS_COUNT)> status_texts_{};
  std::array<char, CONTROL_MAX_PAYLOAD_BYTES + 1> output_{};
  ControlUiStatus status_{ControlUiStatus::CONTROL_UI_STATUS_DISABLED};
  bool initial_enabled_{false};
  bool setup_ready_{false};
  bool processing_events_{false};
  bool output_callback_active_{false};
  bool texts_pending_{false};
  bool unknown_drag_waiting_{false};
  bool bind_response_prepared_{false};
  ControlText<32> bind_response_channel_;
  std::function<const char *(uint8_t)> text_provider_;
  LazyCallbackManager<void(const char *)> message_callbacks_;
  LazyCallbackManager<void(uint32_t)> granted_callbacks_;
  LazyCallbackManager<void(uint32_t)> closed_callbacks_;
  LazyCallbackManager<void(uint8_t)> view_callbacks_;
  LazyCallbackManager<void(uint32_t, bool, uint8_t)> render_callbacks_;
  LazyCallbackManager<void()> render_cancel_callbacks_;
};

#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
// Memberless opt-in type marker for the declarative DirectSceneController ID.
// There is still exactly one base owner, codec, view lifecycle and input path.
class MaterialGenericEntityControlComponent : public MaterialEntityControlComponent {
 public:
  using MaterialEntityControlComponent::MaterialEntityControlComponent;
};
#endif

template<typename... Ts> class MaterialEntityControlPayloadAction : public Action<Ts...> {
 public:
  MaterialEntityControlPayloadAction(MaterialEntityControlComponent *parent, bool bind)
      : parent_(parent), bind_(bind) {}
  TEMPLATABLE_VALUE(std::string, payload)
  void play(const Ts &...x) override {
    if (this->bind_)
      this->parent_->bind_json(this->payload_.value(x...));
    else
      this->parent_->receive(this->payload_.value(x...));
  }

 protected:
  MaterialEntityControlComponent *parent_;
  bool bind_;
};

template<typename... Ts> class MaterialEntityControlOpenAction : public Action<Ts...> {
 public:
  explicit MaterialEntityControlOpenAction(MaterialEntityControlComponent *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(uint32_t, slot)
  TEMPLATABLE_VALUE(std::string, card_id)
  TEMPLATABLE_VALUE(uint64_t, revision)
  void play(const Ts &...x) override {
    this->parent_->open(this->slot_.value(x...), this->card_id_.value(x...), this->revision_.value(x...));
  }

 protected:
  MaterialEntityControlComponent *parent_;
};

template<typename... Ts> class MaterialEntityControlFlagAction : public Action<Ts...> {
 public:
  MaterialEntityControlFlagAction(MaterialEntityControlComponent *parent, bool enable)
      : parent_(parent), enable_(enable) {}
  TEMPLATABLE_VALUE(bool, value)
  TEMPLATABLE_VALUE(uint32_t, generation)
  void play(const Ts &...x) override {
    if (this->enable_)
      this->parent_->enable(this->value_.value(x...));
    else
      this->parent_->set_view_ready(this->generation_.value(x...), this->value_.value(x...));
  }

 protected:
  MaterialEntityControlComponent *parent_;
  bool enable_;
};

template<typename... Ts> class MaterialEntityControlInvalidateAction : public Action<Ts...> {
 public:
  MaterialEntityControlInvalidateAction(MaterialEntityControlComponent *parent, ControlReason reason)
      : parent_(parent), reason_(reason) {}
  void play(const Ts &...) override { this->parent_->invalidate(this->reason_); }

 protected:
  MaterialEntityControlComponent *parent_;
  ControlReason reason_;
};

template<typename... Ts> class MaterialEntityControlDisconnectAction : public Action<Ts...> {
 public:
  explicit MaterialEntityControlDisconnectAction(MaterialEntityControlComponent *parent) : parent_(parent) {}
  void play(const Ts &...) override { this->parent_->disconnect(); }

 protected:
  MaterialEntityControlComponent *parent_;
};

template<typename... Ts> class MaterialEntityControlRefreshTextsAction : public Action<Ts...> {
 public:
  explicit MaterialEntityControlRefreshTextsAction(MaterialEntityControlComponent *parent) : parent_(parent) {}
  void play(const Ts &...) override { this->parent_->refresh_texts(); }

 protected:
  MaterialEntityControlComponent *parent_;
};

template<typename... Ts> class MaterialEntityControlRenderReadyAction : public Action<Ts...> {
 public:
  explicit MaterialEntityControlRenderReadyAction(MaterialEntityControlComponent *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(uint32_t, request_id)
  TEMPLATABLE_VALUE(bool, value)
  void play(const Ts &...x) override {
    this->parent_->render_ready(this->request_id_.value(x...), this->value_.value(x...));
  }

 protected:
  MaterialEntityControlComponent *parent_;
};

}  // namespace esphome::lvgl_material
