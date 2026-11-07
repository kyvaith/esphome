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

#ifdef USE_LVGL_MATERIAL_TIMER_PANEL
namespace esphome::time {
class RealTimeClock;
}
#endif

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
#ifdef USE_LVGL_MATERIAL_THERMOSTAT
  void set_thermostat_view(lv_obj_t *root, lv_obj_t *arc, lv_obj_t *target, lv_obj_t *current, lv_obj_t *activity,
                           lv_obj_t *limits, lv_obj_t *title, lv_obj_t *knob, lv_obj_t *minus, lv_obj_t *plus);
  void populate_thermostat_features(ArduinoJson::JsonObject root, uint8_t version = 1);
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
  bool open_robot(uint32_t slot, std::string_view card_id, uint64_t revision);
  void set_robot_profile(lv_obj_t *root, lv_obj_t *title, lv_obj_t *kind, lv_obj_t *state, lv_obj_t *status,
                         lv_obj_t *suction, lv_obj_t *suction_label, lv_obj_t *options, lv_obj_t *back,
                         lv_obj_t *vacuum_icon, lv_obj_t *mower_icon, lv_obj_t *room, lv_obj_t *room_label,
                         lv_obj_t *options_title, lv_obj_t *apply, lv_obj_t *apply_label, lv_obj_t *battery);
  void set_robot_action(size_t index, lv_obj_t *button, lv_obj_t *label);
  void set_robot_option(size_t index, lv_obj_t *button, lv_obj_t *label);
#endif
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
  void set_light_detail_profile(lv_obj_t *root, lv_obj_t *kelvin, lv_obj_t *kelvin_label, lv_obj_t *effect,
                                lv_obj_t *effect_label, lv_obj_t *status);
  void set_climate_detail_profile(lv_obj_t *root, lv_obj_t *arc, lv_obj_t *target, lv_obj_t *current,
                                  lv_obj_t *activity, lv_obj_t *limits, lv_obj_t *title, lv_obj_t *knob,
                                  lv_obj_t *minus, lv_obj_t *plus, lv_obj_t *state, lv_obj_t *status, lv_obj_t *mode,
                                  lv_obj_t *mode_label, lv_obj_t *parameters, lv_obj_t *parameters_label,
                                  lv_obj_t *parameters_root, lv_obj_t *back);
  void set_detail_numeric_profile(lv_obj_t *root, lv_obj_t *title, lv_obj_t *kind, lv_obj_t *value, lv_obj_t *current,
                                  lv_obj_t *status, lv_obj_t *track, lv_obj_t *fill, lv_obj_t *knob, lv_obj_t *minimum,
                                  lv_obj_t *maximum, lv_obj_t *minus, lv_obj_t *plus, lv_obj_t *back, lv_obj_t *kelvin,
                                  lv_obj_t *humidity);
  void set_detail_choice_profile(lv_obj_t *root, lv_obj_t *title, lv_obj_t *kind, lv_obj_t *current, lv_obj_t *status,
                                 lv_obj_t *back, lv_obj_t *apply, lv_obj_t *apply_label);
  void set_detail_parameter(size_t index, lv_obj_t *button, lv_obj_t *label);
  void set_detail_option(size_t index, lv_obj_t *button, lv_obj_t *label);
  bool populate_detail_features(ArduinoJson::JsonObject root);
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
  void set_climate_range_profile(lv_obj_t *button, lv_obj_t *label, bool enabled);
  bool populate_detail_range_features(ArduinoJson::JsonObject root);
  bool bind_detail_range_features(std::string_view payload, ArduinoJson::JsonObject root);
  bool is_climate_range_view() const { return this->owner_.is_detail_range(); }
#endif
  bool bind_detail_features(std::string_view payload, ArduinoJson::JsonObject root);
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
  void set_light_color_profile(lv_obj_t *root, lv_obj_t *kind, lv_obj_t *title, lv_obj_t *preview, lv_obj_t *status,
                               lv_obj_t *hue_label, lv_obj_t *hue_slider, lv_obj_t *saturation_label,
                               lv_obj_t *saturation_slider, lv_obj_t *cancel, lv_obj_t *cancel_label, lv_obj_t *apply,
                               lv_obj_t *apply_label, lv_obj_t *main_button, lv_obj_t *main_label, uint8_t hit_padding,
                               bool enabled);
  bool populate_detail_color_features(ArduinoJson::JsonObject root);
  bool bind_detail_color_features(std::string_view payload, ArduinoJson::JsonObject root);
  bool is_light_color_view() const { return this->owner_.is_detail_color(); }
  void refresh_light_color_palette();
#endif
#ifdef USE_LVGL_MATERIAL_SCALAR_PERFORMANCE_METRICS
  void log_detail_view(const char *phase) const;
#endif
  bool open_light_detail(uint32_t slot, std::string_view card_id, uint64_t revision);
#endif
  bool uses_generic_view() const { return this->number_profile_; }
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
  bool open_panel(uint32_t slot, std::string_view card_id, uint64_t revision);
  void set_timer_panel_profile(lv_obj_t *root, lv_obj_t *title, lv_obj_t *kind, lv_obj_t *state, lv_obj_t *remaining,
                               lv_obj_t *status, lv_obj_t *hour, lv_obj_t *minute, lv_obj_t *second);
  void set_timer_draft_button(size_t index, lv_obj_t *button);
  void set_timer_panel_action(size_t index, lv_obj_t *button, lv_obj_t *label);
  void set_remote_panel_profile(lv_obj_t *root, lv_obj_t *title, lv_obj_t *kind, lv_obj_t *state, lv_obj_t *status,
                                lv_obj_t *power, lv_obj_t *power_label, lv_obj_t *activity, lv_obj_t *activity_label,
                                lv_obj_t *activity_value, lv_obj_t *options, lv_obj_t *options_title, lv_obj_t *back,
                                lv_obj_t *apply, lv_obj_t *apply_label);
  void set_remote_panel_option(size_t index, lv_obj_t *button, lv_obj_t *label);
  void set_power_panel_profile(lv_obj_t *root, lv_obj_t *title, lv_obj_t *kind, lv_obj_t *state, lv_obj_t *status,
                               lv_obj_t *power, lv_obj_t *power_label, lv_obj_t *unknown_icon, lv_obj_t *switch_icon,
                               lv_obj_t *outlet_icon);
#ifdef USE_LVGL_MATERIAL_EXTRA_CONTROLS
  void set_extra_profile(lv_obj_t *root, lv_obj_t *title, lv_obj_t *kind, lv_obj_t *updated, lv_obj_t *status);
  void set_extra_sensor_profile(lv_obj_t *root, lv_obj_t *value, lv_obj_t *unit, lv_obj_t *unknown,
                                lv_obj_t *temperature, lv_obj_t *humidity, const lv_font_t *compact);
  void set_extra_state_profile(lv_obj_t *root, lv_obj_t *state, lv_obj_t *binary_surface, lv_obj_t *alarm_surface,
                               lv_obj_t *lock_surface, lv_obj_t *unknown, lv_obj_t *moisture, lv_obj_t *alarm,
                               lv_obj_t *locked, lv_obj_t *unlocked, lv_obj_t *calendar, lv_obj_t *button,
                               lv_obj_t *caption, const lv_font_t *compact);
  void set_extra_calendar_profile(lv_obj_t *root, lv_obj_t *badge, lv_obj_t *summary, lv_obj_t *start, lv_obj_t *end,
                                  lv_obj_t *zone, lv_obj_t *location, lv_obj_t *description, lv_obj_t *truncation,
                                  const lv_font_t *compact);
  void set_extra_profiles(bool info, bool lock);
#ifdef USE_LVGL_MATERIAL_INFO_AGENDA
  void set_agenda_profile(lv_obj_t *previous, lv_obj_t *previous_label, lv_obj_t *next, lv_obj_t *next_label,
                          bool enabled);
  bool populate_info_agenda_features(ArduinoJson::JsonObject root);
  bool bind_info_agenda_features(std::string_view payload, ArduinoJson::JsonObject root);
  void log_agenda_view(const char *phase) const;
#endif
#ifdef USE_LVGL_MATERIAL_INFO_STATISTICS
  void set_statistics_profile(lv_obj_t *surface, bool enabled);
  bool populate_info_statistics_features(ArduinoJson::JsonObject root);
  bool bind_info_statistics_features(std::string_view payload, ArduinoJson::JsonObject root);
  void log_statistics_view(const char *phase) const;
#endif
#ifdef USE_LVGL_MATERIAL_INFO_CONTEXT
  void set_extra_person_profile(lv_obj_t *root, lv_obj_t *icon, lv_obj_t *surface, lv_obj_t *state, lv_obj_t *changed,
                                lv_obj_t *zone, const lv_font_t *compact_state = nullptr);
  void set_extra_sun_profile(lv_obj_t *root, lv_obj_t *icon, lv_obj_t *phase, lv_obj_t *rising_caption,
                             lv_obj_t *rising_value, lv_obj_t *setting_caption, lv_obj_t *setting_value,
                             lv_obj_t *elevation_caption, lv_obj_t *elevation_value, lv_obj_t *azimuth_caption,
                             lv_obj_t *azimuth_value, lv_obj_t *zone);
  void set_extra_info_context(bool enabled);
#endif
  bool query_extra_features(std::string_view payload, ArduinoJson::JsonObject root);
  bool bind_extra_features(std::string_view payload, ArduinoJson::JsonObject root);
  bool is_extra_view() const { return this->owner_.is_extra() || this->extra_widgets_active_; }
  void log_extra_view(const char *phase) const;
#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS
  void set_action_profile(lv_obj_t *root, lv_obj_t *title, lv_obj_t *kind, lv_obj_t *phase, lv_obj_t *last_used,
                          lv_obj_t *current, lv_obj_t *policy, lv_obj_t *status);
  void set_action_icon(size_t index, lv_obj_t *widget);
  void set_action_button(size_t index, lv_obj_t *button, lv_obj_t *label);
  void set_action_enabled(bool enabled);
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
  void set_action_parameters_enabled(bool enabled);
#endif
  bool populate_action_parameters_features(ArduinoJson::JsonObject root);
  bool bind_action_parameters_features(std::string_view payload, ArduinoJson::JsonObject root);

#ifdef USE_LVGL_MATERIAL_TILE_EVENT_CONTROLS
  void set_tile_event_enabled(bool enabled);
  bool is_tile_event_view() const {
    const auto *action = this->owner_.state().family_if<ControlActionSnapshot>();
    return this->owner_.is_action() && action != nullptr && action->type == ControlActionSnapshot::TILE_EVENT_TYPE;
  }
#endif
  bool is_action_view() const { return this->owner_.is_action() || this->action_widgets_active_; }
  void log_action_view(const char *label) const;
#endif
#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
  void set_datetime_profile(lv_obj_t *root, lv_obj_t *title, lv_obj_t *kind, lv_obj_t *summary, lv_obj_t *zone,
                            lv_obj_t *updated, lv_obj_t *status, lv_obj_t *fields_root, lv_obj_t *editor_root,
                            lv_obj_t *editor_field, lv_obj_t *editor_digits);
  void set_datetime_icon(size_t index, lv_obj_t *widget);
  void set_datetime_field(size_t index, lv_obj_t *button, lv_obj_t *caption, lv_obj_t *value);
  void set_datetime_key(size_t index, lv_obj_t *button);
  void set_datetime_control(size_t index, lv_obj_t *button);
  void set_datetime_labels(lv_obj_t *cancel, lv_obj_t *save);
  void set_datetime_field_layout(size_t index, int16_t x, int16_t y, int16_t height, int16_t field_height);
  void set_datetime_enabled(bool enabled);
  bool is_datetime_view() const { return this->owner_.is_datetime() || this->datetime_widgets_active_; }
  void log_datetime_view(const char *label) const;
#endif
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
  void set_media_profile(lv_obj_t *root, lv_obj_t *title, lv_obj_t *kind, lv_obj_t *phase, lv_obj_t *track,
                         lv_obj_t *artist, lv_obj_t *status, lv_obj_t *volume_summary, const lv_font_t *track_compact,
                         const lv_font_t *artist_compact, const lv_font_t *volume_compact);
  void set_media_icon(size_t index, lv_obj_t *widget);
  void set_media_action(size_t index, lv_obj_t *button, lv_obj_t *label);
  void set_media_facet(size_t index, lv_obj_t *button, lv_obj_t *label);
  void set_media_volume_profile(lv_obj_t *root, lv_obj_t *title, lv_obj_t *kind, lv_obj_t *observed, lv_obj_t *status,
                                lv_obj_t *back, const lv_font_t *compact);
  void set_media_choice_profile(lv_obj_t *root, lv_obj_t *title, lv_obj_t *kind, lv_obj_t *current, lv_obj_t *status,
                                lv_obj_t *back, lv_obj_t *apply, lv_obj_t *apply_label, const lv_font_t *compact);
  void set_media_choice_option(size_t index, lv_obj_t *button, lv_obj_t *label);
  void set_media_enabled(bool enabled);
  bool is_media_view() const { return this->owner_.is_media() || this->media_widgets_active_; }
  void log_media_view(const char *phase) const;
#endif
#endif
  bool populate_panel_features(ArduinoJson::JsonObject root);
  bool bind_panel_features(std::string_view payload, ArduinoJson::JsonObject root);
#ifdef USE_LVGL_MATERIAL_TIMER_PANEL
  void set_panel_time(time::RealTimeClock *clock) {
    if (!this->setup_ready_)
      this->panel_time_ = clock;
  }
#endif
#endif
  bool register_scalar_backend(ControlViewBackend *backend, lvgl::LvglNavigation *navigation,
                               lvgl::LvglApplication *application);
  bool is_registered_scalar_backend(const ControlViewBackend *backend) const {
    return this->number_profile_ && backend != nullptr && this->scalar_backend_ == backend;
  }
  bool open_number(uint32_t slot, std::string_view card_id, uint64_t revision);
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
  void set_choice_profile(lv_obj_t *root, lv_obj_t *apply, lv_obj_t *apply_label, lv_obj_t *current_label,
                          lv_obj_t *status_label, lv_obj_t *neutral_reference);
  void set_choice_option(size_t index, lv_obj_t *button, lv_obj_t *label);
  bool open_choice(uint32_t slot, std::string_view card_id, uint64_t revision);
  void set_choice_empty_enabled(bool enabled) { this->owner_.set_choice_empty_enabled(enabled); }
  bool populate_choice_features(ArduinoJson::JsonObject root);
  bool bind_choice_features(std::string_view channel, uint32_t version, ArduinoJson::JsonObject root);
#endif
#ifdef USE_LVGL_MATERIAL_COVER_CONTROLS
  void set_cover_profile(lv_obj_t *root, lv_obj_t *current_label, lv_obj_t *status_label);
  void set_cover_button(size_t index, lv_obj_t *button, lv_obj_t *label);
#ifdef USE_LVGL_MATERIAL_COVER_PANEL
  void set_cover_panel_profile(lv_obj_t *title, lv_obj_t *kind, lv_obj_t *state, lv_obj_t *position_group,
                               lv_obj_t *position_track, lv_obj_t *position_fill, lv_obj_t *position_knob,
                               lv_obj_t *tilt_group, lv_obj_t *tilt_label, lv_obj_t *tilt_track, lv_obj_t *tilt_fill,
                               lv_obj_t *tilt_knob);
  void set_cover_tilt_button(size_t index, lv_obj_t *button, lv_obj_t *label);
  void set_cover_panel_icon(size_t device_class, lv_obj_t *widget);
  bool populate_cover_panel_features(ArduinoJson::JsonObject root);
  bool bind_cover_panel_features(std::string_view channel, uint32_t version, ArduinoJson::JsonObject root);
#endif
  bool open_cover(uint32_t slot, std::string_view card_id, uint64_t revision);
#endif
#ifdef USE_LVGL_MATERIAL_COVER_PRIORITY_STOP
  void set_cover_priority_stop_enabled(bool enabled) { this->owner_.set_cover_priority_stop_enabled(enabled); }
  bool populate_cover_features(ArduinoJson::JsonObject root);
  bool bind_cover_features(std::string_view channel, uint32_t version, ArduinoJson::JsonObject root);
#endif
  bool receive_entity(std::string_view payload);
#ifdef USE_LVGL_MATERIAL_FAN_CONTROLS
  void set_fan_profile(lv_obj_t *root, lv_obj_t *title, lv_obj_t *value, lv_obj_t *state, lv_obj_t *status,
                       lv_obj_t *power_button, lv_obj_t *power_label, lv_obj_t *oscillation_button,
                       lv_obj_t *oscillation_label, lv_obj_t *direction_button, lv_obj_t *direction_label,
                       lv_obj_t *preset_button, lv_obj_t *preset_label, lv_obj_t *preset_root,
                       lv_obj_t *preset_back_button);
  void set_fan_speed_button(size_t index, lv_obj_t *button, lv_obj_t *label);
  void set_fan_speed_caption(lv_obj_t *label) {
    if (!this->setup_ready_)
      this->fan_speed_caption_ = label;
  }
  void set_fan_preset_option(size_t index, lv_obj_t *button, lv_obj_t *label);
  bool open_fan(uint32_t slot, std::string_view card_id, uint64_t revision);
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS
  void set_humidifier_profile(lv_obj_t *root, lv_obj_t *title, lv_obj_t *kind, lv_obj_t *target, lv_obj_t *current,
                              lv_obj_t *state, lv_obj_t *status, lv_obj_t *track, lv_obj_t *fill, lv_obj_t *knob,
                              lv_obj_t *minimum, lv_obj_t *maximum, lv_obj_t *minus, lv_obj_t *plus, lv_obj_t *power,
                              lv_obj_t *power_label, lv_obj_t *mode, lv_obj_t *mode_label, lv_obj_t *mode_root,
                              lv_obj_t *mode_back);
  void set_humidifier_mode_option(size_t index, lv_obj_t *button, lv_obj_t *label);
  bool open_humidifier(uint32_t slot, std::string_view card_id, uint64_t revision);
#endif
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

#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
  void set_text_profile(lv_obj_t *root, lv_obj_t *title, lv_obj_t *kind, lv_obj_t *icon, lv_obj_t *surface,
                        lv_obj_t *viewport, lv_obj_t *cursor, lv_obj_t *count, lv_obj_t *status, lv_obj_t *cancel_label,
                        lv_obj_t *save_label);
  void set_text_line(size_t index, lv_obj_t *object);
  void set_text_keyboard(size_t index, lv_obj_t *object, uint8_t count);
  void set_text_control(size_t index, lv_obj_t *object);
  void set_text_key_label(size_t index, lv_obj_t *object);
  void set_text_map(size_t map, size_t row, const char *const *labels, const uint32_t *values);
  void set_text_repertoire(const uint32_t *points, size_t count);
  void set_text_layout(size_t index, int16_t surface_y, int16_t surface_height, int16_t viewport_y,
                       uint8_t visible_lines, int16_t count_y, int16_t up_y, int16_t down_y, int16_t save_x);
  void set_text_line_height(uint16_t height);
  void set_text_enabled(bool enabled);
  template<typename F> void set_text_preview_retire(F &&callback) {
    this->text_preview_retire_ = std::forward<F>(callback);
  }
  bool text_preview_pending() const { return this->text_preview_pending_; }
  bool is_text_view() const { return this->owner_.is_text() || this->text_widgets_active_; }
  void log_text_view(const char *label) const;
  bool emit_text(const ControlMessage &message, ControlTextValueView value) override;
#endif

  bool emit(const ControlMessage &message) override;
  void view_event(ControlViewEvent event) override;

 protected:
  // Declarative colors and Material theme updates are local DEFAULT styles.
  // Ordinary LVGL getters resolve the reference widget's current state, so a
  // retained DISABLED/PRESSED style must not become another widget's palette.
  // Preserve resolved-style compatibility when no explicit local base exists.
  static lv_color_t configured_style_color_(lv_obj_t *object, lv_style_prop_t property) {
    lv_style_value_t value{};
    const auto selector =
        static_cast<lv_style_selector_t>(LV_PART_MAIN) | static_cast<lv_style_selector_t>(LV_STATE_DEFAULT);
    if (lv_obj_get_local_style_prop(object, property, &value, selector) == LV_STYLE_RES_FOUND)
      return value.color;
    return lv_obj_get_style_prop(object, LV_PART_MAIN, property).color;
  }
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
  bool setup_detail_profile_();
  bool update_detail_static_();
  void restore_detail_widgets_();
  bool begin_detail_input_(lv_obj_t *target, lv_indev_t *indev, uint32_t pointer, uint32_t now);
  void release_detail_input_(lv_obj_t *target, uint32_t pointer, uint32_t now);
  void update_detail_input_(lv_indev_t *indev);
  bool is_detail_drag_(lv_obj_t *target) const;
  bool detail_catalogue_glyphs_() const;
  bool request_detail_facet_(ControlFacet facet, uint32_t now);
  void dispatch_detail_facet_();
  void show_detail_number_(uint32_t index, bool preview);
  ControlNumericView detail_numeric_view_() const;
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
  bool setup_climate_range_profile_();
  std::array<lv_obj_t *, 2> climate_range_widgets_{};
  bool climate_range_enabled_{false}, climate_range_upper_{false};
#endif
  void reconcile_detail_backend_();
  void finish_detail_handoff_();
  std::array<lv_obj_t *, 6> light_detail_{};
  std::array<lv_obj_t *, 18> climate_detail_{};
  std::array<lv_obj_t *, 16> detail_numeric_{};
  std::array<lv_obj_t *, 8> detail_choice_{};
  std::array<lv_obj_t *, 5> detail_parameters_{}, detail_parameter_labels_{};
  std::array<lv_obj_t *, 8> detail_options_{}, detail_option_labels_{};
  uint32_t detail_preview_index_{0}, detail_option_generation_{0}, detail_local_view_{0};
  uint32_t detail_handoff_generation_{0};
  struct DetailViewIntent {
    uint32_t generation{0}, state_generation{0}, view_seq{0}, captured_at{0};
    ControlFacet source{ControlFacet::CONTROL_FACET_MAIN}, target{ControlFacet::CONTROL_FACET_MAIN};
  } detail_intent_;
  uint8_t detail_option_{255}, detail_hidden_mask_{0};
  bool detail_widgets_active_{false}, detail_parameters_open_{false}, detail_climate_drag_{false};
  bool detail_handoff_preparing_{false};
#endif
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
  bool activate_view_backend_(bool number, bool retain_generation = false);
  bool open_entity_(uint32_t slot, std::string_view card_id, uint64_t revision, ControlEntityKind kind);
  bool setup_number_profile_();
  bool suspend_control_view_backend(uint32_t timeout_ms) override;
  bool resume_control_view_backend(uint32_t captured_generation) override;
  bool hide_control_view_alias() override { return true; }
  bool control_view_region_visible() const override { return this->root_ != nullptr && lv_obj_is_visible(this->root_); }
  void update_number_static_();
  void restore_light_widgets_();
  static void style_disabled_control_(lv_obj_t *button);
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
#ifdef USE_LVGL_MATERIAL_EXTRA_CONTROLS
#ifdef USE_LVGL_MATERIAL_INFO_AGENDA
  bool setup_agenda_profile_();
  void clear_agenda_widgets_();
  void update_agenda_static_(const ControlExtraSnapshot &extra);
  void agenda_input_event_(lv_event_code_t code, lv_obj_t *target, lv_indev_t *indev, uint32_t now);
  std::array<lv_obj_t *, 4> agenda_widgets_{};
  bool agenda_configured_{false}, agenda_ready_{false};
#endif
#ifdef USE_LVGL_MATERIAL_INFO_STATISTICS
  bool setup_statistics_profile_();
  void clear_statistics_widgets_();
  void update_statistics_static_(const ControlExtraSnapshot &extra);
  static void statistics_draw_cb_(lv_event_t *event);
  void draw_statistics_(lv_layer_t *layer);
  lv_obj_t *statistics_surface_{nullptr};
  bool statistics_configured_{false}, statistics_ready_{false};
#endif
  bool setup_extra_profile_();
  void update_extra_static_();
  void restore_extra_widgets_();
  bool extra_features_(std::string_view payload, ArduinoJson::JsonObject root, bool bind);
  bool extra_text_(lv_obj_t *label, const char *text, const lv_font_t *normal = nullptr,
                   const lv_font_t *compact = nullptr, bool multiline = false);
  std::array<lv_obj_t *, 5> extra_common_{};
  std::array<lv_obj_t *, 6> extra_sensor_{};
  std::array<lv_obj_t *, 13> extra_state_{};
  std::array<lv_obj_t *, 9> extra_calendar_{};
  std::array<const lv_font_t *, 3> extra_normal_fonts_{}, extra_compact_fonts_{};
  uint16_t extra_configured_{0};
  uint8_t extra_hidden_mask_{0};
  bool extra_widgets_active_{false};
#ifdef USE_LVGL_MATERIAL_INFO_CONTEXT
  bool setup_info_context_();
  void clear_info_context_();
  void update_info_context_(const ControlExtraSnapshot &extra);
  bool info_context_date_(lv_obj_t *label, uint8_t status, std::string_view value, uint8_t prefix = 0);
  std::array<lv_obj_t *, 6> extra_person_{};
  std::array<lv_obj_t *, 12> extra_sun_{};
  const lv_font_t *extra_person_normal_{nullptr}, *extra_person_compact_{nullptr};
#endif
#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
  bool apply_action_parameters_layout_(bool parameters);
  bool format_action_parameter_policy_(const ControlActionSnapshot &action, char *text, size_t capacity) const;
  std::array<int32_t, 3> action_original_y_{};
  bool action_parameters_configured_{false}, action_parameters_layout_saved_{false},
      action_parameters_layout_active_{false};
#endif
  bool setup_action_profile_();
  void update_action_static_();
  void restore_action_widgets_();
  void action_input_event_(lv_event_code_t code, lv_obj_t *target, lv_indev_t *indev, uint32_t now);
  std::array<lv_obj_t *, 8> action_common_{};
  std::array<lv_obj_t *, 4> action_icons_{};
  std::array<lv_obj_t *, 3> action_buttons_{}, action_labels_{};
  std::array<uint8_t, 3> action_operations_{};
  uint8_t action_hidden_mask_{0};
  bool action_enabled_{false}, action_widgets_active_{false}, action_ready_{false};
#ifdef USE_LVGL_MATERIAL_TILE_EVENT_CONTROLS
  bool tile_event_enabled_{false};
#endif
#endif
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
  bool setup_text_profile_();
  void service_text_preview_();
  void update_text_static_();
  void restore_text_widgets_();
  void text_input_event_(lv_event_code_t code, lv_obj_t *target, lv_indev_t *indev, uint32_t now);
  bool text_render_value_(std::string_view value, uint16_t cursor, bool editing);
  uint16_t text_cursor_at_(int32_t x, uint16_t line) const;
  uint16_t text_cursor_line_(uint16_t cursor) const;
  int32_t text_cursor_x_(uint16_t cursor) const;
  void text_clear_value_();
  void text_apply_map_();
  uint8_t text_map_index_() const;
  std::array<lv_obj_t *, 11> text_common_{};
  std::array<lv_obj_t *, 6> text_lines_{};
  std::array<lv_obj_t *, 3> text_keyboards_{}, text_key_labels_{};
  std::array<lv_obj_t *, 11> text_controls_{};
  std::array<uint8_t, 3> text_row_lengths_{};
  struct TextKeyMap {
    const char *const *labels{nullptr};
    const uint32_t *values{nullptr};
  };
  std::array<std::array<TextKeyMap, 3>, 6> text_maps_{};
  struct TextLayout {
    int16_t surface_y{0}, surface_height{0}, viewport_y{0};
    uint8_t visible_lines{0};
    int16_t count_y{0}, up_y{0}, down_y{0}, save_x{0};
  };
  std::array<TextLayout, 2> text_layouts_{};
  struct TextUiStorage {
    std::array<char, 1027> visible{};
    std::array<uint16_t, 256> offsets{};
  };
  RAMUniquePtr<ControlTextStorage> text_storage_;
  RAMUniquePtr<TextUiStorage> text_ui_storage_;
  const uint32_t *text_repertoire_{nullptr};
  size_t text_repertoire_count_{0};
  const lv_font_t *text_render_font_{nullptr};
  uint32_t text_render_revision_{0}, text_render_generation_{0};
  int32_t text_render_width_{0}, text_render_spacing_{0};
  uint16_t text_render_first_{0}, text_render_cursor_{0};
  bool text_render_valid_{false}, text_render_editing_{false};
  uint32_t text_map_generation_{1}, text_display_generation_{0};
  uint16_t text_first_line_{0}, text_line_count_{1}, text_line_height_{0};
  uint8_t text_applied_map_{255}, text_hidden_mask_{0}, text_symbol_page_{0}, text_local_action_{0},
      text_pressed_key_{255};
  bool text_enabled_{false}, text_widgets_active_{false}, text_ready_{false};
  std::function<bool()> text_preview_retire_;
  bool text_preview_pending_{false}, text_value_retained_{false};
  bool text_upper_{false}, text_alternate_{false}, text_was_editing_{false};
#endif

#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
  bool setup_datetime_profile_();
  void update_datetime_static_();
  void restore_datetime_widgets_();
  void datetime_input_event_(lv_event_code_t code, lv_obj_t *target, lv_indev_t *indev, uint32_t now);
  std::array<lv_obj_t *, 11> datetime_common_{};
  std::array<lv_obj_t *, 2> datetime_icons_{}, datetime_labels_{};
  std::array<lv_obj_t *, 6> datetime_fields_{}, datetime_captions_{}, datetime_values_{};
  std::array<lv_obj_t *, 12> datetime_keys_{};
  std::array<lv_obj_t *, 4> datetime_controls_{};
  struct DatetimeFieldLayout {
    int16_t x{0}, y{0}, height{0}, field_height{0};
  };
  std::array<DatetimeFieldLayout, 2> datetime_layouts_{};
  uint8_t datetime_hidden_mask_{0};
  bool datetime_enabled_{false}, datetime_widgets_active_{false}, datetime_ready_{false};
#endif
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
  bool setup_media_profile_();
  void update_media_static_();
  void restore_media_widgets_();
  bool media_volume_active_() const;
  void media_input_event_(lv_event_code_t code, lv_obj_t *target, lv_indev_t *indev, uint32_t now);
  bool request_media_facet_(ControlMediaFacet facet, uint32_t now);
  void dispatch_media_facet_();
  void reconcile_media_backend_();
  void finish_media_handoff_();
  std::array<lv_obj_t *, 8> media_common_{};
  std::array<lv_obj_t *, 4> media_icons_{};
  std::array<lv_obj_t *, 12> media_buttons_{}, media_labels_{};
  std::array<lv_obj_t *, 3> media_facets_{}, media_facet_labels_{};
  std::array<lv_obj_t *, 6> media_volume_{};
  std::array<lv_obj_t *, 8> media_choice_{};
  std::array<lv_obj_t *, 8> media_options_{}, media_option_labels_{};
  std::array<const lv_font_t *, 5> media_normal_fonts_{}, media_compact_fonts_{};
  uint8_t media_hidden_mask_{0}, media_option_{255};
  uint32_t media_local_view_{0}, media_option_generation_{0}, media_handoff_generation_{0};
  struct MediaViewIntent {
    uint32_t generation{0}, state_generation{0}, view_seq{0}, captured_at{0};
    ControlMediaFacet source{ControlMediaFacet::MAIN}, target{ControlMediaFacet::MAIN};
  } media_intent_;
  bool media_enabled_{false}, media_widgets_active_{false}, media_handoff_preparing_{false};
#endif
#endif
  bool setup_panel_profile_();
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
  bool setup_light_color_profile_();
  void update_light_color_static_();
  void update_light_color_preview_();
  void update_light_color_controls_();
  void hide_light_color_();
  bool begin_light_color_input_(lv_obj_t *target, lv_indev_t *indev, uint32_t pointer, uint32_t now);
  void update_light_color_input_(lv_indev_t *indev);
  void release_light_color_input_(lv_obj_t *target, uint32_t pointer, uint32_t now);
  std::array<lv_obj_t *, 15> light_color_widgets_{};
  uint8_t light_color_hit_padding_{20}, light_color_axis_{255};
  bool light_color_enabled_{false}, light_color_ready_{false};
#endif
  void update_panel_static_();
  void restore_panel_widgets_();
  bool begin_panel_input_(lv_obj_t *target, uint32_t pointer, uint32_t now);
  void release_panel_input_(lv_obj_t *target, uint32_t pointer, uint32_t now);
  bool panel_activity_glyphs_available_() const;
  void update_timer_clock_();
  ControlOperation timer_action_(size_t index) const;
  std::array<lv_obj_t *, 9> timer_panel_{};
  std::array<lv_obj_t *, 15> remote_panel_{};
  std::array<lv_obj_t *, 10> power_panel_{};
  std::array<lv_obj_t *, 6> timer_draft_buttons_{};
  std::array<lv_obj_t *, 5> timer_action_buttons_{}, timer_action_labels_{};
  std::array<lv_obj_t *, 8> remote_option_buttons_{}, remote_option_labels_{};
  uint32_t timer_draft_{0}, timer_clock_updated_{0}, panel_selection_generation_{0}, panel_option_generation_{0};
  uint8_t panel_hidden_mask_{0}, panel_option_{ControlSnapshot::Panel::UNKNOWN_INDEX};
  bool panel_widgets_active_{false}, panel_modal_{false};
#ifdef USE_LVGL_MATERIAL_TIMER_PANEL
  time::RealTimeClock *panel_time_{nullptr};
#endif
#endif
#ifdef USE_LVGL_MATERIAL_THERMOSTAT
  bool setup_thermostat_view_();
  void update_thermostat_view_(const ControlThermostatSnapshot &snapshot);
  void update_thermostat_target_(const char *target, bool fahrenheit, int32_t level);
  void show_thermostat_preview_();
  static bool arc_pointer_angle_(lv_obj_t *arc, lv_indev_t *indev, int32_t &angle);
  static uint32_t arc_preview_index_(int32_t angle, int32_t span, uint32_t max_index);
  bool thermostat_pointer_angle_(lv_indev_t *indev, int32_t &angle) const;
  bool begin_thermostat_input_(lv_indev_t *indev, uint32_t pointer, uint32_t now);
  void update_thermostat_input_(lv_indev_t *indev);
  lv_obj_t *thermostat_root_{nullptr};
  lv_obj_t *thermostat_arc_{nullptr};
  lv_obj_t *thermostat_target_{nullptr};
  lv_obj_t *thermostat_current_{nullptr};
  lv_obj_t *thermostat_activity_{nullptr};
  lv_obj_t *thermostat_limits_{nullptr};
  lv_obj_t *thermostat_title_{nullptr};
  lv_obj_t *thermostat_knob_{nullptr};
  lv_obj_t *thermostat_minus_{nullptr};
  lv_obj_t *thermostat_plus_{nullptr};
  std::array<char, 36> thermostat_value_text_{};
  uint32_t thermostat_preview_index_{0};
  int32_t thermostat_angle_{0};
  int32_t thermostat_last_angle_{0};
  int32_t thermostat_span_{0};
  bool thermostat_angle_valid_{false};
  bool thermostat_pending_preview_{false};
#endif
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
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
  bool setup_choice_profile_();
  void update_choice_static_();
  void restore_choice_widgets_();
  size_t choice_target_index_(lv_obj_t *target) const;
  bool choice_glyphs_available_() const;
  std::array<lv_obj_t *, ControlSnapshot::Choice::MAX_OPTIONS> choice_buttons_{};
  std::array<lv_obj_t *, ControlSnapshot::Choice::MAX_OPTIONS> choice_labels_{};
  lv_obj_t *choice_root_{nullptr};
  lv_obj_t *choice_apply_{nullptr};
  lv_obj_t *choice_apply_label_{nullptr};
  lv_obj_t *choice_current_label_{nullptr};
  lv_obj_t *choice_status_label_{nullptr};
  lv_obj_t *choice_neutral_reference_{nullptr};
  uint8_t choice_hidden_mask_{0};
  bool choice_widgets_active_{false};
#endif

#ifdef USE_LVGL_MATERIAL_COVER_CONTROLS
  bool setup_cover_profile_();
  void update_cover_static_();
  void restore_cover_widgets_();
  std::array<lv_obj_t *, 3> cover_buttons_{};
  std::array<lv_obj_t *, 3> cover_labels_{};
  lv_obj_t *cover_root_{nullptr};
  lv_obj_t *cover_current_label_{nullptr};
  lv_obj_t *cover_status_label_{nullptr};
  uint8_t cover_hidden_mask_{0};
  bool cover_widgets_active_{false};
#ifdef USE_LVGL_MATERIAL_COVER_PANEL
  bool setup_cover_panel_();
  void update_cover_panel_();
  bool begin_cover_panel_input_(lv_obj_t *target, lv_indev_t *indev, uint32_t pointer, uint32_t now);
  bool is_cover_track_(lv_obj_t *target) const;
  void update_cover_input_(lv_obj_t *target, lv_indev_t *indev, uint32_t pointer, uint32_t now);
  void show_cover_axis_(bool tilt, bool known, uint8_t value);
  std::array<lv_obj_t *, 12> cover_panel_widgets_{};
  std::array<lv_obj_t *, 3> cover_tilt_buttons_{};
  std::array<lv_obj_t *, 3> cover_tilt_labels_{};
  std::array<lv_obj_t *, 11> cover_panel_icons_{};
  std::array<std::array<uint16_t, 3>, 6> cover_disabled_styles_{};
  bool cover_disabled_styles_active_{false};
  bool cover_preview_tilt_{false};
  bool cover_preview_valid_{false};
  uint8_t cover_preview_{0};
#endif
#endif
  lvgl::LvglComponent *lvgl_;
#ifdef USE_LVGL_MATERIAL_FAN_CONTROLS
  bool setup_fan_profile_();
  void update_fan_static_();
  void restore_fan_widgets_();
  bool begin_fan_input_(lv_obj_t *target, uint32_t pointer, uint32_t now);
  void release_fan_input_(lv_obj_t *target, uint32_t pointer, uint32_t now);
  bool fan_glyphs_available_() const;
  std::array<lv_obj_t *, 15> fan_widgets_{};
  std::array<lv_obj_t *, ControlSnapshot::Fan::MAX_OPTIONS> fan_speed_buttons_{}, fan_speed_labels_{};
  lv_obj_t *fan_speed_caption_{nullptr};
  std::array<lv_obj_t *, ControlSnapshot::Fan::MAX_OPTIONS> fan_preset_buttons_{}, fan_preset_labels_{};
  uint8_t fan_hidden_mask_{0};
  bool fan_widgets_active_{false}, fan_presets_visible_{false};
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
  bool setup_robot_profile_();
  void update_robot_static_();
  void restore_robot_widgets_();
  bool begin_robot_input_(lv_obj_t *target, uint32_t pointer, uint32_t now);
  void release_robot_input_(lv_obj_t *target, uint32_t pointer, uint32_t now);
  bool robot_glyphs_available_(bool area) const;
  std::array<lv_obj_t *, 17> robot_widgets_{};
  std::array<lv_obj_t *, 6> robot_action_buttons_{}, robot_action_labels_{};
  std::array<lv_obj_t *, 8> robot_option_buttons_{}, robot_option_labels_{};
  uint32_t robot_room_generation_{0};
  uint8_t robot_hidden_mask_{0}, robot_modal_{0}, robot_room_index_{ControlSnapshot::Robot::UNKNOWN};
  bool robot_widgets_active_{false};
#endif
  MaterialEntityControl owner_;
#ifdef USE_LVGL_NAVIGATION_ADMISSION_GUARD
  ControlViewLifecycle view_lifecycle_{this};
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS
  std::array<lv_obj_t *, 20> humidifier_widgets_{};
  std::array<lv_obj_t *, 8> humidifier_mode_buttons_{}, humidifier_mode_labels_{};
  uint8_t humidifier_hidden_mask_{0}, humidifier_preview_index_{0};
  bool humidifier_widgets_active_{false}, humidifier_modes_visible_{false}, humidifier_pending_preview_{false};
  bool setup_humidifier_profile_();
  bool is_humidifier_track_(lv_obj_t *target) const;
  bool humidifier_glyphs_available_() const;
  void restore_humidifier_widgets_();
  void update_humidifier_static_();
  bool begin_humidifier_input_(lv_obj_t *target, lv_indev_t *indev, uint32_t pointer, uint32_t now);
  void update_humidifier_input_(lv_indev_t *indev, uint32_t pointer, uint32_t now);
  void release_humidifier_input_(lv_obj_t *target, uint32_t pointer, uint32_t now);
  void show_humidifier_target_(uint8_t index);
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
