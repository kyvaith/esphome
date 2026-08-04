#pragma once

#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/core/component.h"

#include <atomic>
#include <climits>
#include <cstddef>
#include <cstdint>

#ifdef USE_ESP32
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace esphome::lvgl_material {

class MaterialDirectMarquee : public Component {
 public:
  explicit MaterialDirectMarquee(lvgl::LvglComponent *component) : lvgl_component_(component) {}

  void set_label(lv_obj_t *label) { this->label_ = label; }
  void set_viewport(lv_obj_t *viewport) { this->viewport_ = viewport; }

  void setup() override;
  void on_shutdown() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR - 5.0f; }

  bool begin();
  bool update(int offset_x);
  void end(bool restore_native = true);
  void service(bool allow_cleanup = true);
  bool is_active() const { return this->active_.load(std::memory_order_acquire); }

 protected:
  static void present_done_(void *arg);
#ifdef USE_ESP32
  static void worker_(void *arg);
#endif

  bool render_(int offset_x);
  bool ensure_worker_();
  void release_();
#ifdef USE_ESP32
  void log_performance_(int64_t now_us);
#endif

  lvgl::LvglComponent *lvgl_component_{nullptr};
  lv_obj_t *label_{nullptr};
  lv_obj_t *viewport_{nullptr};
  lv_draw_buf_t *text_{nullptr};
  lv_color_t *background_{nullptr};
  int screen_x_{0};
  int screen_y_{0};
  int width_{0};
  int height_{0};
  int source_x_offset_{0};
  int source_y_offset_{0};
  int last_x_{INT_MIN};
  std::atomic<int> pending_x_{INT_MIN};
  std::atomic<bool> active_{false};
  bool cleanup_pending_{false};
  bool handoff_pending_{false};
  std::atomic<bool> present_in_flight_{false};
  std::atomic<bool> present_complete_{false};
#ifdef USE_ESP32
  TaskHandle_t worker_handle_{nullptr};
  StackType_t *worker_stack_{nullptr};
  StaticTask_t worker_storage_{};
  std::atomic<bool> worker_busy_{false};
  uint32_t perf_compose_count_{0};
  uint64_t perf_compose_total_us_{0};
  uint32_t perf_compose_max_us_{0};
  uint32_t perf_async_count_{0};
  uint32_t perf_busy_count_{0};
  uint32_t perf_sync_count_{0};
  uint32_t perf_dma_count_{0};
  uint64_t perf_dma_total_us_{0};
  uint32_t perf_dma_max_us_{0};
  int64_t perf_window_start_us_{0};
  int64_t present_started_us_{0};
  int64_t handoff_deadline_us_{0};
#endif
};

}  // namespace esphome::lvgl_material
