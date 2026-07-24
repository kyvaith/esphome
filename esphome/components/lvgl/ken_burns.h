#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "esphome/core/component.h"

#include "lvgl.h"

#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
#include "driver/ppa.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace esphome::lvgl {

class LvglComponent;

/** Drives a slow, allocation-free pan animation on an LVGL image.
 *
 * The controller only changes the image transform. Rendering remains in LVGL's
 * draw pipeline, allowing hardware image draw units such as ESP32-P4 PPA SRM to
 * handle the transformed pixels.
 */
class KenBurnsController : public Component {
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
  size_t memory_usage_bytes() const;
  void log_memory_usage(const char *phase) const;

 protected:
  void choose_target_();
  void select_pan_direction_(bool forward);
  bool update_transform_(uint32_t elapsed_ms);
  bool update_direct_frame_(uint32_t elapsed_ms);
  bool calculate_direct_crop_(const lv_image_dsc_t *source, uint32_t elapsed_ms, int *crop_x, int *crop_y,
                               int *crop_width, int *crop_height, uint8_t *subpixel_alpha = nullptr,
                               bool *subpixel_vertical = nullptr) const;
  bool render_direct_frame_(const lv_image_dsc_t *source, LvglComponent *component, uint32_t elapsed_ms);
  void reset_direct_frame_cache_();
  const lv_image_dsc_t *get_source_descriptor_() const;
  LvglComponent *get_lvgl_component_() const;

#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  bool start_direct_worker_();
  bool stop_direct_worker_(uint32_t timeout_ms = 600);
  static void direct_worker_trampoline_(void *arg);
  void direct_worker_();
  bool perform_direct_transition_(const lv_image_dsc_t *source);
  bool ensure_transition_buffers_(const lv_image_dsc_t *incoming_source);
  void release_transition_buffers_();
  bool ensure_subpixel_scratch_(size_t required_size);
  void release_subpixel_scratch_();

  TaskHandle_t direct_worker_handle_{nullptr};
  StackType_t *direct_worker_stack_{nullptr};
  StaticTask_t direct_worker_storage_{};
  std::atomic<bool> direct_worker_run_{false};
  std::atomic<bool> direct_worker_active_{false};
  std::atomic<bool> direct_worker_failed_{false};
  std::atomic<bool> direct_start_pending_{false};
  std::atomic<bool> transition_requested_{false};
  std::atomic<bool> transition_active_{false};
  std::atomic<bool> transition_failed_{false};
  std::atomic<const lv_image_dsc_t *> transition_source_{nullptr};
  std::atomic<uint32_t> transition_duration_ms_{800};
  ppa_client_handle_t direct_srm_client_{nullptr};
  ppa_client_handle_t direct_blend_client_{nullptr};
  uint8_t *transition_old_frame_{nullptr};
  uint8_t *transition_new_frame_{nullptr};
  bool transition_old_frame_owned_{false};
  bool transition_new_frame_owned_{false};
  size_t transition_old_frame_size_{0};
  size_t transition_frame_size_{0};
  uint8_t *subpixel_scratch_{nullptr};
  size_t subpixel_scratch_size_{0};
  lv_image_dsc_t direct_source_{};
  LvglComponent *direct_component_{nullptr};
  const uint8_t *last_direct_source_data_{nullptr};
  int last_direct_crop_x_{-1};
  int last_direct_crop_y_{-1};
  int last_direct_crop_width_{-1};
  int last_direct_crop_height_{-1};
  uint8_t last_direct_subpixel_alpha_{0};
  bool last_direct_subpixel_vertical_{false};
#endif

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
  bool direct_active_{false};
};

}  // namespace esphome::lvgl
