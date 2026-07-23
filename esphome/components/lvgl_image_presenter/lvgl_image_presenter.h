#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "esphome/components/image/image.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"

#include "lvgl.h"

namespace esphome::lvgl_image_presenter {

class LvglImagePresenter : public Component {
 public:
  void set_obj(lv_obj_t *obj) { this->obj_ = obj; }
  void set_phase_duration(uint32_t duration_ms) { this->phase_duration_ms_ = duration_ms; }
  void set_frame_interval(uint32_t interval_ms) { this->frame_interval_ms_ = interval_ms; }
  void set_direct(bool direct) { this->use_direct_ = direct; }
  void set_zoom(uint16_t start, uint16_t end) {
    this->zoom_start_ = start;
    this->zoom_end_ = end;
  }
  void set_pan_limit(float pan_limit) { this->pan_limit_ = pan_limit; }
  void set_fade_through_black(bool enabled) { this->fade_through_black_ = enabled; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR - 10.0f; }

  void restart();
  bool pause();
  bool pause_for_snapshot();
  bool complete_snapshot_handoff(uint32_t timeout_ms = 600);
  void resume();
  void reset_transform();
  bool transition_to(const lv_image_dsc_t *source, uint32_t duration_ms = 800);
  bool is_transition_pending_or_active() const;
  bool transition_failed() const;
  bool is_phase_complete() const { return this->phase_complete_.load(std::memory_order_acquire); }
  bool freeze_direct();
  void resume_direct();
  size_t memory_usage_bytes() const { return 0; }
  void log_memory_usage(const char *phase) const;

 protected:
  enum class TransitionState : uint8_t {
    NONE,
    FADING_OUT,
    FADING_IN,
  };

  void select_pan_direction_(bool forward);
  bool update_transform_(uint32_t elapsed_ms);
  void update_transition_(uint32_t delta_ms);
  void set_opacity_(lv_opa_t opacity);
  void switch_transition_source_();

  lv_obj_t *obj_{nullptr};
  uint32_t phase_duration_ms_{18000};
  uint32_t frame_interval_ms_{33};
  uint32_t last_loop_ms_{0};
  uint32_t phase_elapsed_ms_{0};
  uint16_t zoom_start_{256};
  uint16_t zoom_end_{288};
  float pan_limit_{0.75f};
  float target_x_{0.0f};
  float target_y_{0.0f};
  int32_t geometry_source_width_{0};
  int32_t geometry_source_height_{0};
  int32_t geometry_viewport_width_{0};
  int32_t geometry_viewport_height_{0};
  bool zooming_in_{true};
  bool pan_forward_{true};
  std::atomic<bool> phase_complete_{false};
  bool fade_through_black_{false};
  bool paused_{false};
  bool use_direct_{false};
  bool direct_frozen_{false};
  TransitionState transition_state_{TransitionState::NONE};
  const lv_image_dsc_t *transition_source_{nullptr};
  uint32_t transition_duration_ms_{800};
  uint32_t transition_elapsed_ms_{0};
  lv_opa_t base_opacity_{LV_OPA_COVER};
  std::atomic<bool> transition_failed_{false};
};

template<typename... Ts>
class LvglImagePresenterRestartAction : public Action<Ts...>, public Parented<LvglImagePresenter> {
 public:
  void play(const Ts &...x) override { this->parent_->restart(); }
};

template<typename... Ts>
class LvglImagePresenterPauseAction : public Action<Ts...>, public Parented<LvglImagePresenter> {
 public:
  void play(const Ts &...x) override { this->parent_->pause(); }
};

template<typename... Ts>
class LvglImagePresenterResumeAction : public Action<Ts...>, public Parented<LvglImagePresenter> {
 public:
  void play(const Ts &...x) override { this->parent_->resume(); }
};

template<typename... Ts>
class LvglImagePresenterResetAction : public Action<Ts...>, public Parented<LvglImagePresenter> {
 public:
  void play(const Ts &...x) override { this->parent_->reset_transform(); }
};

template<typename... Ts>
class LvglImagePresenterPauseForSnapshotAction : public Action<Ts...>, public Parented<LvglImagePresenter> {
 public:
  void play(const Ts &...x) override { this->parent_->pause_for_snapshot(); }
};

template<typename... Ts>
class LvglImagePresenterCompleteSnapshotHandoffAction : public Action<Ts...>, public Parented<LvglImagePresenter> {
 public:
  void play(const Ts &...x) override { this->parent_->complete_snapshot_handoff(); }
};

template<typename... Ts>
class LvglImagePresenterTransitionAction : public Action<Ts...>, public Parented<LvglImagePresenter> {
 public:
  void set_source(image::Image *source) { this->source_ = source; }
  TEMPLATABLE_VALUE(uint32_t, duration)

  void play(const Ts &...x) override {
    if (this->source_ != nullptr)
      this->parent_->transition_to(this->source_->get_lv_image_dsc(), this->duration_.value(x...));
  }

 protected:
  image::Image *source_{};
};

}  // namespace esphome::lvgl_image_presenter
