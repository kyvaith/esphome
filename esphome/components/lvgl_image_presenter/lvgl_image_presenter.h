#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "esphome/components/image/image.h"
#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"

#include "lvgl.h"

#ifdef USE_ESP32_VARIANT_ESP32P4
#include "driver/ppa.h"
#endif

namespace esphome::lvgl_image_presenter {

class LvglImagePresenter : public Component, public image::JpegFrameConsumer {
 public:
  void set_obj(lv_obj_t *obj) { this->obj_ = obj; }
  void set_lvgl_component(lvgl::LvglComponent *component) { this->lvgl_component_ = component; }
  void set_source(image::Image *source) { this->source_ = source; }
  void set_phase_duration(uint32_t duration_ms) { this->phase_duration_ms_ = duration_ms; }
  void set_frame_interval(uint32_t interval_ms) { this->frame_interval_ms_ = interval_ms; }
  void set_direct(bool direct) { this->use_direct_ = direct; }
  void set_direct_jpeg(bool direct_jpeg) { this->use_direct_jpeg_ = direct_jpeg; }
  void set_continuous(bool continuous) { this->continuous_ = continuous; }
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

  lv_obj_t *obj_{nullptr};
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
  bool paused_{false};
  bool use_direct_{false};
  bool continuous_{false};
  bool direct_frozen_{false};
  bool direct_backend_ready_{false};
  bool direct_session_active_{false};
  bool direct_backend_failed_{false};
  bool use_direct_jpeg_{false};
  bool direct_jpeg_registered_{false};
  std::atomic<bool> accept_direct_jpeg_{false};
  std::atomic<bool> direct_jpeg_busy_{false};
  uint8_t direct_session_failures_{0};
  uint32_t direct_session_retry_after_ms_{0};
  TransitionState transition_state_{TransitionState::NONE};
  image::Image *transition_source_{nullptr};
  uint32_t transition_duration_ms_{800};
  uint32_t transition_elapsed_ms_{0};
  lv_opa_t base_opacity_{LV_OPA_COVER};
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

}  // namespace esphome::lvgl_image_presenter
