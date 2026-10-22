#pragma once

#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace esphome::lvgl_material {

class MaterialDirectSpinner : public Component {
 public:
  explicit MaterialDirectSpinner(lvgl::LvglComponent *component) : lvgl_component_(component) {}

  void set_widget(lv_obj_t *widget) { this->widget_ = widget; }
  void set_colors(lv_color_t background, lv_color_t track, lv_color_t indicator) {
    this->background_color_ = background;
    this->track_color_ = track;
    this->indicator_color_ = indicator;
  }
  void set_geometry(uint16_t thickness, uint16_t arc_length) {
    this->thickness_ = thickness;
    this->arc_length_ = arc_length;
  }
  void set_timing(uint32_t frame_interval, uint32_t spin_time) {
    this->frame_interval_ = frame_interval;
    this->spin_time_ = spin_time;
  }

  void setup() override;
  void loop() override;
  void on_shutdown() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR - 5.0f; }

  bool start();
  void stop(bool clear = true);
  bool is_active() const { return this->active_; }
  void log_stats(const char *phase) const;

 protected:
  static void present_done_(void *arg);
  bool ensure_buffers_();
  bool submit_frame_(bool clear);
  void render_frame_(lv_color_t *buffer, uint32_t now_ms);
  void release_region_();
  void free_buffers_();

  lvgl::LvglComponent *lvgl_component_{nullptr};
  lv_obj_t *widget_{nullptr};
  lv_color_t *buffers_[2]{};
  uint16_t *angle_map_{nullptr};
  uint8_t *coverage_map_{nullptr};
  size_t pixel_count_{0};
  int x_{0};
  int y_{0};
  int width_{0};
  int height_{0};
  lv_color_t background_color_{lv_color_black()};
  lv_color_t track_color_{lv_color_hex(0x34283E)};
  lv_color_t indicator_color_{lv_color_hex(0xE8DEF8)};
  uint32_t frame_interval_{33};
  uint32_t spin_time_{900};
  uint32_t started_ms_{0};
  uint32_t next_frame_ms_{0};
  uint16_t thickness_{7};
  uint16_t arc_length_{80};
  uint8_t buffer_index_{0};
  bool active_{false};
  bool stopping_{false};
  bool clear_in_flight_{false};
  bool release_without_clear_pending_{false};
  bool first_frame_pending_{true};
  bool prepared_{false};
  std::atomic<bool> present_in_flight_{false};
  std::atomic<bool> present_complete_{false};
  uint32_t stat_starts_{0};
  uint32_t stat_duplicate_starts_{0};
  uint32_t stat_stops_{0};
  uint32_t stat_submitted_{0};
  uint32_t stat_rejected_{0};
  uint32_t stat_last_submit_ms_{0};
  uint32_t stat_max_submit_gap_ms_{0};
};

template<typename... Ts> class MaterialDirectSpinnerStartAction : public Action<Ts...> {
 public:
  explicit MaterialDirectSpinnerStartAction(MaterialDirectSpinner *parent) : parent_(parent) {}

  void play(const Ts &...x) override { this->parent_->start(); }

 protected:
  MaterialDirectSpinner *parent_;
};

template<typename... Ts> class MaterialDirectSpinnerStopAction : public Action<Ts...> {
 public:
  MaterialDirectSpinnerStopAction(MaterialDirectSpinner *parent, bool clear) : parent_(parent), clear_(clear) {}

  void play(const Ts &...x) override { this->parent_->stop(this->clear_); }

 protected:
  MaterialDirectSpinner *parent_;
  bool clear_;
};

}  // namespace esphome::lvgl_material
