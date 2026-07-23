#pragma once

#include <cstdint>

#include <lvgl.h>

namespace esphome::lvgl {

class LvglDiagnostics {
 public:
  void attach(lv_display_t *display, bool direct_mode);
  void update(uint32_t now_us);
  void note_handler(uint32_t duration_us);
  void note_flush(uint32_t duration_us);
  void note_presented_frame();

  void set_logging_enabled(bool enabled) { this->logging_enabled_ = enabled; }
  bool is_logging_enabled() const { return this->logging_enabled_; }

  uint32_t get_fps() const { return this->fps_; }
  uint32_t get_cpu_percent() const { return this->cpu_percent_; }
  uint32_t get_flush_ms() const { return this->flush_ms_; }
  uint32_t get_flush_max_ms() const { return this->flush_max_ms_; }
  uint32_t get_handler_max_ms() const { return this->handler_max_ms_; }
  uint32_t get_invalidated_kpx() const { return this->invalidated_kpx_; }
  bool is_direct_mode() const { return this->direct_mode_; }

 protected:
  static void refresh_ready_callback_(lv_event_t *event);
  static void invalidate_callback_(lv_event_t *event);
  void publish_window_(uint32_t elapsed_us);

  bool attached_{false};
  bool direct_mode_{false};
  bool logging_enabled_{false};
  uint32_t window_started_us_{0};
  uint32_t frame_count_{0};
  uint64_t handler_total_us_{0};
  uint64_t flush_total_us_{0};
  uint32_t handler_max_us_{0};
  uint32_t flush_max_us_{0};
  uint64_t invalidated_pixels_{0};

  uint32_t fps_{0};
  uint32_t cpu_percent_{0};
  uint32_t flush_ms_{0};
  uint32_t flush_max_ms_{0};
  uint32_t handler_max_ms_{0};
  uint32_t invalidated_kpx_{0};
};

LvglDiagnostics *get_default_diagnostics();

}  // namespace esphome::lvgl

extern "C" {
uint32_t lvgl_esphome_get_fps();
uint32_t lvgl_esphome_get_cpu_pct();
uint32_t lvgl_esphome_get_flush_ms();
uint32_t lvgl_esphome_get_flush_max_ms();
uint32_t lvgl_esphome_get_loop_max_ms();
uint32_t lvgl_esphome_get_invalidated_kpx();
uint32_t lvgl_esphome_get_direct_mode_active();
uint32_t lvgl_esphome_get_perf_logging_enabled();
void lvgl_esphome_set_perf_logging_enabled(bool enabled);
}
