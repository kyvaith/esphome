#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "esphome/components/image/image.h"
#include "esphome/components/lvgl/direct_scene_controller.h"
#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"

#include "lvgl.h"

#if defined(USE_ESP32_VARIANT_ESP32P4) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
#include "driver/ppa.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace esphome::lvgl_image_presenter {

class LvglImagePresenter : public Component, public image::JpegFrameConsumer, public lvgl::DirectSceneController {
 public:
  static constexpr size_t MAX_CAPTURE_EXCLUDED_OBJECTS = 4;

  void set_obj(lv_obj_t *obj) { this->obj_ = obj; }
  void add_capture_excluded_obj(lv_obj_t *obj) {
    if (obj == nullptr)
      return;
    for (size_t index = 0; index < this->capture_excluded_obj_count_; index++) {
      if (this->capture_excluded_objs_[index] == obj)
        return;
    }
    if (this->capture_excluded_obj_count_ < this->capture_excluded_objs_.size())
      this->capture_excluded_objs_[this->capture_excluded_obj_count_++] = obj;
  }
  void set_lvgl_component(lvgl::LvglComponent *component) { this->lvgl_component_ = component; }
  void set_source(image::Image *source) { this->source_ = source; }
  void set_phase_duration(uint32_t duration_ms) { this->phase_duration_ms_ = duration_ms; }
  void set_frame_interval(uint32_t interval_ms) { this->frame_interval_ms_ = interval_ms; }
  void set_direct(bool direct) { this->use_direct_ = direct; }
  void set_direct_jpeg(bool direct_jpeg) { this->use_direct_jpeg_ = direct_jpeg; }
  void set_defer_direct_session_until_frame(bool defer) { this->defer_direct_session_until_frame_ = defer; }
  void set_continuous(bool continuous) { this->continuous_ = continuous; }
  void set_crossfade(bool crossfade) { this->crossfade_ = crossfade; }
  void set_motion_enabled(bool enabled) { this->motion_enabled_ = enabled; }
  void set_zoom(uint16_t start, uint16_t end) {
    this->zoom_start_ = start;
    this->zoom_end_ = end;
  }
  void set_pan_limit(float pan_limit) { this->pan_limit_ = pan_limit; }
  void set_fade_through_black(bool enabled) { this->fade_through_black_ = enabled; }

  void setup() override;
  void loop() override;
  void on_shutdown() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR - 10.0f; }

  void restart();
  bool pause();
  bool pause_for_snapshot();
  bool complete_snapshot_handoff(uint32_t timeout_ms = 600);
  void resume();
  void reset_transform();
  bool prepare_transition();
  void cancel_transition();
  bool transition_to(image::Image *source, uint32_t duration_ms = 800);
  bool is_transition_pending_or_active() const;
  bool transition_failed() const;
  bool is_phase_complete() const { return this->phase_complete_.load(std::memory_order_acquire); }
  uint32_t presented_frames() const { return this->presented_frames_.load(std::memory_order_relaxed); }
  float measured_present_fps() const { return this->measured_present_fps_.load(std::memory_order_relaxed); }
  uint32_t last_present_us() const { return this->last_present_us_.load(std::memory_order_relaxed); }
  uint32_t direct_jpeg_frames() const { return this->direct_jpeg_frames_.load(std::memory_order_relaxed); }
  uint32_t last_direct_jpeg_decode_us() const {
    return this->last_direct_jpeg_decode_us_.load(std::memory_order_relaxed);
  }
  image::JpegFrameResult consume_jpeg_frame(const uint8_t *data, size_t size) override;
  bool freeze_direct();
  void resume_direct();
  bool suspend_for_direct_overlay() override;
  void resume_after_direct_overlay() override;
  size_t memory_usage_bytes() const { return 0; }
  void log_memory_usage(const char *phase) const;

 protected:
  enum class TransitionState : uint8_t {
    NONE,
    FADING_OUT,
    FADING_IN,
    CROSS_FADING,
  };

  void select_pan_direction_(bool forward);
  bool update_transform_(uint32_t elapsed_ms);
  void update_transition_(uint32_t delta_ms);
  void set_opacity_(lv_opa_t opacity);
  void switch_transition_source_();
  bool start_crossfade_(image::Image *source, uint32_t duration_ms);
  void finish_crossfade_();
  void cleanup_crossfade_(bool show_current_source);
  bool is_widget_visible_() const;
  bool is_widget_full_screen_() const;
  bool ensure_direct_session_();
  bool stop_direct_session_(uint32_t timeout_ms);
  bool update_direct_frame_(uint32_t elapsed_ms);
  bool calculate_direct_crop_(const image::ImageBufferLease &source, size_t target_width, size_t target_height,
                              uint32_t elapsed_ms, int *crop_x, int *crop_y, int *crop_width, int *crop_height,
                              uint16_t *scale_q4) const;
  bool render_direct_frame_(uint32_t elapsed_ms);
  bool present_pending_direct_frame_(uint32_t timeout_ms);
  void refresh_continuous_source_();
  void record_presented_frame_(uint32_t elapsed_us);
  void reset_direct_frame_cache_();
  void disable_direct_backend_(const char *reason);

#ifdef USE_ESP32_VARIANT_ESP32P4
  bool sync_dma_source_for_ppa_(const image::ImageBufferLease &source) const;
#endif
#if defined(USE_ESP32_VARIANT_ESP32P4) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  bool create_crossfade_worker_();
  bool stop_crossfade_worker_(uint32_t timeout_ms = 800);
  static void crossfade_worker_trampoline_(void *arg);
  void crossfade_worker_();
  bool perform_scene_crossfade_(image::Image *source);
  void service_crossfade_completion_();
  void release_crossfade_frame_();
#endif

  lv_obj_t *obj_{nullptr};
  std::array<lv_obj_t *, MAX_CAPTURE_EXCLUDED_OBJECTS> capture_excluded_objs_{};
  size_t capture_excluded_obj_count_{0};
  lvgl::LvglComponent *lvgl_component_{nullptr};
  image::Image *source_{nullptr};
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
  bool crossfade_{false};
  bool motion_enabled_{true};
  bool paused_{false};
  bool use_direct_{false};
  bool continuous_{false};
  bool direct_frozen_{false};
  bool direct_overlay_suspended_{false};
  bool direct_backend_ready_{false};
  bool direct_session_active_{false};
  bool direct_backend_failed_{false};
  bool use_direct_jpeg_{false};
  bool defer_direct_session_until_frame_{false};
  bool direct_jpeg_registered_{false};
  std::atomic<bool> direct_session_deferred_{false};
  std::atomic<bool> direct_jpeg_frame_ready_{false};
  std::atomic<bool> direct_jpeg_frame_presented_{false};
  std::atomic<bool> accept_direct_jpeg_{false};
  std::atomic<bool> direct_jpeg_busy_{false};
  uint8_t direct_session_failures_{0};
  uint32_t direct_session_retry_after_ms_{0};
  TransitionState transition_state_{TransitionState::NONE};
  image::Image *transition_source_{nullptr};
  uint32_t transition_duration_ms_{800};
  uint32_t transition_elapsed_ms_{0};
  lv_opa_t base_opacity_{LV_OPA_COVER};
  bool transition_prepared_{false};
  lv_image_dsc_t previous_dsc_{};
  std::atomic<bool> transition_failed_{false};
  display::FrameBufferLease direct_target_lease_{};
  const uint8_t *last_direct_source_data_{nullptr};
  uint32_t last_direct_source_generation_{0};
  int last_direct_crop_x_{-1};
  int last_direct_crop_y_{-1};
  int last_direct_crop_width_{-1};
  int last_direct_crop_height_{-1};
  size_t direct_target_width_{0};
  size_t direct_target_height_{0};
  uint32_t last_fallback_source_generation_{0};
  std::atomic<uint32_t> presented_frames_{0};
  std::atomic<uint32_t> last_present_us_{0};
  std::atomic<float> measured_present_fps_{0.0f};
  std::atomic<uint32_t> direct_jpeg_frames_{0};
  std::atomic<uint32_t> last_direct_jpeg_decode_us_{0};
  uint32_t present_fps_window_started_ms_{0};
  uint32_t present_fps_window_frames_{0};

#ifdef USE_ESP32_VARIANT_ESP32P4
  ppa_client_handle_t direct_srm_client_{nullptr};
#endif
#if defined(USE_ESP32_VARIANT_ESP32P4) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  ppa_client_handle_t crossfade_blend_client_{nullptr};
  TaskHandle_t crossfade_worker_handle_{nullptr};
  StackType_t *crossfade_worker_stack_{nullptr};
  StaticTask_t crossfade_worker_storage_{};
  std::atomic<bool> crossfade_worker_ready_{false};
  std::atomic<bool> crossfade_worker_run_{false};
  std::atomic<bool> crossfade_worker_active_{false};
  std::atomic<bool> crossfade_worker_complete_{false};
  std::atomic<bool> crossfade_worker_success_{false};
  std::atomic<image::Image *> crossfade_source_{nullptr};
  uint8_t *crossfade_frame_{nullptr};
  size_t crossfade_frame_size_{0};
  size_t crossfade_frame_allocation_size_{0};
  std::atomic<bool> crossfade_direct_active_{false};
#endif
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
      this->parent_->transition_to(this->source_, this->duration_.value(x...));
  }

 protected:
  image::Image *source_{};
};

template<typename... Ts>
class LvglImagePresenterPrepareTransitionAction : public Action<Ts...>, public Parented<LvglImagePresenter> {
 public:
  void play(const Ts &...x) override { this->parent_->prepare_transition(); }
};

template<typename... Ts>
class LvglImagePresenterCancelTransitionAction : public Action<Ts...>, public Parented<LvglImagePresenter> {
 public:
  void play(const Ts &...x) override { this->parent_->cancel_transition(); }
};

}  // namespace esphome::lvgl_image_presenter
