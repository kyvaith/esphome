#pragma once

#include "esphome/components/lvgl/direct_scene_controller.h"
#include "esphome/components/lvgl/lottie_loader.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace esphome::lvgl_material {

class MaterialDirectStateLayer;

class MaterialWeatherPresenter : public Component, public lvgl::DirectSceneController {
 public:
  void set_root(lv_obj_t *root) { this->root_ = root; }
  void set_temperature_label(lv_obj_t *label) { this->temperature_label_ = label; }
  void set_condition_label(lv_obj_t *label) { this->condition_label_ = label; }
  void set_detail_label(lv_obj_t *label) { this->detail_label_ = label; }
  void set_interaction_widget(lv_obj_t *widget) { this->interaction_widget_ = widget; }
  void set_interaction_state_layer(MaterialDirectStateLayer *state_layer) {
    this->interaction_state_layer_ = state_layer;
  }
  void set_animation_layer(lv_obj_t *layer) { this->animation_layer_ = layer; }
  void set_snapshot_page(lv_obj_t *page) { this->snapshot_page_ = page; }
  void set_snapshot_region(lv_obj_t *region) { this->snapshot_region_ = region; }
  void set_snapshot_refresh_delay(uint32_t delay_ms) { this->snapshot_refresh_delay_ms_ = delay_ms; }
  void set_resume_delay(uint32_t delay_ms) { this->resume_delay_ms_ = delay_ms; }
  void add_animation(const std::string &key, lv_obj_t *widget);
  void add_on_animation_complete_callback(std::function<void()> &&callback) {
    this->animation_complete_callbacks_.add(std::move(callback));
  }

  void setup() override;
  void loop() override;
  void on_shutdown() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR - 5.0f; }

  void update(const std::string &condition, bool is_day, float temperature, float apparent_temperature, float humidity);
  void restart_on_wake();
  void suspend_for_navigation();
  void release_for_application();
  void resume_after_navigation(bool page_visible);
  // Application close is a distinct lifecycle edge from a Home presentation.
  // Keeping it explicit prevents a late navigation callback from reviving the
  // weather worker underneath Player, Gallery, Camera, or Voice.
  void resume_after_application(bool page_visible);
  void log_state(const char *reason = "manual");
  bool suspend_for_direct_overlay() override;
  void resume_after_direct_overlay() override;
  bool is_animation_complete();
  bool prepare_snapshot_frame(uint32_t timeout_ms = 250);
  bool refresh_snapshot_now(bool force_widget_render = false);

 protected:
  struct AnimationBinding {
    std::string key;
    lv_obj_t *widget{nullptr};
    lvgl::LottieContext *context{nullptr};
    lv_obj_t *original_parent{nullptr};
    lv_coord_t original_x{0};
    lv_coord_t original_y{0};
    bool on_top_layer{false};
  };

  static std::string normalize_condition_(const std::string &condition);
  static std::string display_condition_(const std::string &condition);
  std::string select_animation_(const std::string &condition, bool is_day) const;
  AnimationBinding *find_animation_(const std::string &key);
  bool ensure_animation_bindings_();
  bool is_root_presented_() const;
  bool promote_animation_to_top_layer_(AnimationBinding &animation);
  bool restore_animation_to_page_(AnimationBinding &animation);
  void apply_animation_visibility_(bool page_visible);
  static void interaction_event_cb_(lv_event_t *event);
  void set_interaction_pressed_(bool pressed);
  void finish_interaction_release_();
  void sync_interaction_layer_();
  void request_snapshot_refresh_();

  lv_obj_t *root_{nullptr};
  lv_obj_t *temperature_label_{nullptr};
  lv_obj_t *condition_label_{nullptr};
  lv_obj_t *detail_label_{nullptr};
  lv_obj_t *interaction_widget_{nullptr};
  lv_obj_t *snapshot_page_{nullptr};
  lv_obj_t *snapshot_region_{nullptr};
  // A transparent page-owned layer keeps the live weather artwork above the
  // static clock/chip while remaining inside the same LVGL scene. Falling back
  // to lv_layer_top() is retained for older configurations without a layer.
  lv_obj_t *animation_layer_{nullptr};
  MaterialDirectStateLayer *interaction_state_layer_{nullptr};
  lv_obj_t *interaction_layer_{nullptr};
  std::vector<AnimationBinding> animations_;
  std::string selected_animation_;
  std::string applied_animation_;
  bool animation_visible_{false};
  // A root can be visible underneath the boot overlay. Do not start a
  // one-shot weather animation until navigation explicitly presents Home.
  bool page_visible_{false};
  bool animation_bindings_ready_{false};
  bool animation_bindings_wait_logged_{false};
  uint32_t animation_retry_at_{0};
  bool animation_completion_reported_{false};
  bool navigation_suspended_{false};
  bool application_suspended_{false};
  bool navigation_resume_pending_{false};
  bool direct_overlay_suspended_{false};
  bool direct_overlay_resume_animation_{false};
  bool snapshot_refresh_pending_{false};
  bool interaction_pressed_{false};
  bool interaction_release_pending_{false};
  bool interaction_restore_submitted_{false};
  uint32_t interaction_pressed_at_{0};
  uint32_t navigation_resume_at_{0};
  uint32_t navigation_suspended_at_{0};
  uint32_t direct_overlay_suspended_at_{0};
  uint32_t snapshot_refresh_at_{0};
  uint32_t resume_delay_ms_{300};
  uint32_t snapshot_refresh_delay_ms_{120};
  lvgl::LottieContext *interaction_context_{nullptr};
  uint32_t last_visibility_check_{0};
  CallbackManager<void()> animation_complete_callbacks_;
};

template<typename... Ts> class MaterialWeatherSuspendAction : public Action<Ts...> {
 public:
  explicit MaterialWeatherSuspendAction(MaterialWeatherPresenter *parent) : parent_(parent) {}

 protected:
  void play(const Ts &...x) override { this->parent_->suspend_for_navigation(); }
  MaterialWeatherPresenter *parent_;
};

template<typename... Ts> class MaterialWeatherResumeAction : public Action<Ts...> {
 public:
  explicit MaterialWeatherResumeAction(MaterialWeatherPresenter *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(bool, page_visible)

 protected:
  void play(const Ts &...x) override { this->parent_->resume_after_navigation(this->page_visible_.value(x...)); }
  MaterialWeatherPresenter *parent_;
};

template<typename... Ts> class MaterialWeatherUpdateAction : public Action<Ts...> {
 public:
  explicit MaterialWeatherUpdateAction(MaterialWeatherPresenter *parent) : parent_(parent) {}

  TEMPLATABLE_VALUE(std::string, condition)
  TEMPLATABLE_VALUE(bool, is_day)
  TEMPLATABLE_VALUE(float, temperature)
  TEMPLATABLE_VALUE(float, apparent_temperature)
  TEMPLATABLE_VALUE(float, humidity)

 protected:
  void play(const Ts &...x) override {
    this->parent_->update(this->condition_.value(x...), this->is_day_.value(x...), this->temperature_.value(x...),
                          this->apparent_temperature_.value(x...), this->humidity_.value(x...));
  }

  MaterialWeatherPresenter *parent_;
};

}  // namespace esphome::lvgl_material
