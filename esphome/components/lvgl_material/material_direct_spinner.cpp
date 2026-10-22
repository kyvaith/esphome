#include "material_direct_spinner.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#ifdef USE_ESP32
#include "esp_heap_caps.h"
#endif

namespace esphome::lvgl_material {

static const char *const TAG = "lvgl_material.spinner";

void MaterialDirectSpinner::setup() {
  if (this->lvgl_component_ == nullptr || this->widget_ == nullptr) {
    ESP_LOGE(TAG, "Direct spinner configuration is incomplete");
    this->mark_failed();
  }
}

void MaterialDirectSpinner::loop() {
  if (this->present_in_flight_.load(std::memory_order_acquire) &&
      this->present_complete_.exchange(false, std::memory_order_acq_rel)) {
    this->present_in_flight_.store(false, std::memory_order_release);
    if (this->release_without_clear_pending_) {
      this->release_without_clear_pending_ = false;
      this->release_region_();
      return;
    }
    if (this->clear_in_flight_) {
      this->clear_in_flight_ = false;
      this->stopping_ = false;
      this->release_region_();
    }
  }

  if (this->stopping_) {
    if (!this->present_in_flight_.load(std::memory_order_acquire) && !this->submit_frame_(true)) {
      this->stopping_ = false;
      this->release_region_();
    }
    return;
  }

  // Build the polar lookup tables while the loader is still hidden. Doing
  // this on the first start made the native loading page visible for one
  // frame before the direct renderer could submit anything.
  if (!this->prepared_ && !this->active_ && this->lvgl_component_ != nullptr &&
      !this->lvgl_component_->is_paused()) {
    this->prepared_ = this->ensure_buffers_();
  }
  if (!this->active_ || this->present_in_flight_.load(std::memory_order_acquire))
    return;

  const uint32_t now = millis();
  if (static_cast<int32_t>(now - this->next_frame_ms_) < 0)
    return;
  this->next_frame_ms_ = now + this->frame_interval_;
  this->submit_frame_(false);
}

void MaterialDirectSpinner::on_shutdown() {
  this->active_ = false;
  this->stopping_ = false;
  this->release_region_();
  if (!this->present_in_flight_.load(std::memory_order_acquire))
    this->free_buffers_();
}

void MaterialDirectSpinner::dump_config() {
  ESP_LOGCONFIG(TAG, "Material Direct Spinner:");
  ESP_LOGCONFIG(TAG, "  Size: %dx%d", this->width_, this->height_);
  ESP_LOGCONFIG(TAG, "  Frame interval: %u ms", static_cast<unsigned>(this->frame_interval_));
  ESP_LOGCONFIG(TAG, "  Spin time: %u ms", static_cast<unsigned>(this->spin_time_));
}

bool MaterialDirectSpinner::start() {
  // Several application paths can announce the same loading generation. A
  // duplicate start must not reset the phase or republish the first frame:
  // doing so made the loader disappear and reappear while one request was
  // still pending.
  if (this->active_ && !this->stopping_) {
    this->stat_duplicate_starts_++;
    return true;
  }
  if (this->is_failed() || !this->ensure_buffers_())
    return false;
  this->prepared_ = true;
  this->stopping_ = false;
  this->clear_in_flight_ = false;
  this->release_without_clear_pending_ = false;
  this->first_frame_pending_ = true;
  this->active_ = true;
  this->stat_starts_++;
  this->stat_last_submit_ms_ = 0;
  this->stat_max_submit_gap_ms_ = 0;
  this->started_ms_ = millis();
  // Publish the first frame before returning from the action. If another
  // direct renderer still owns the region, loop() retries without delaying a
  // complete animation interval.
  const bool submitted = !this->present_in_flight_.load(std::memory_order_acquire) && this->submit_frame_(false);
  this->next_frame_ms_ = submitted ? this->started_ms_ + this->frame_interval_ : this->started_ms_;
  return true;
}

void MaterialDirectSpinner::stop(bool clear) {
  if (!this->active_ && !this->stopping_)
    return;
  this->stat_stops_++;
  this->active_ = false;
  if (!clear) {
    this->stopping_ = false;
    this->clear_in_flight_ = false;
    if (this->present_in_flight_.load(std::memory_order_acquire)) {
      this->release_without_clear_pending_ = true;
    } else {
      this->release_region_();
    }
    return;
  }
  this->release_without_clear_pending_ = false;
  this->stopping_ = true;
  if (!this->present_in_flight_.load(std::memory_order_acquire) && !this->submit_frame_(true)) {
    this->stopping_ = false;
    this->release_region_();
  }
}

void MaterialDirectSpinner::present_done_(void *arg) {
  auto *spinner = static_cast<MaterialDirectSpinner *>(arg);
  if (spinner != nullptr)
    spinner->present_complete_.store(true, std::memory_order_release);
}

bool MaterialDirectSpinner::ensure_buffers_() {
  lv_obj_update_layout(this->widget_);
  lv_area_t area{};
  lv_obj_get_coords(this->widget_, &area);
  const int width = lv_area_get_width(&area);
  const int height = lv_area_get_height(&area);
  if (width <= 0 || height <= 0 || sizeof(lv_color_t) != 3)
    return false;
  if (this->buffers_[0] != nullptr && width == this->width_ && height == this->height_) {
    this->x_ = area.x1;
    this->y_ = area.y1;
    return true;
  }

  if (this->present_in_flight_.load(std::memory_order_acquire))
    return false;
  this->free_buffers_();
  this->x_ = area.x1;
  this->y_ = area.y1;
  this->width_ = width;
  this->height_ = height;
  this->pixel_count_ = static_cast<size_t>(width) * height;
  const size_t frame_bytes = this->pixel_count_ * sizeof(lv_color_t);
  const size_t angle_bytes = this->pixel_count_ * sizeof(uint16_t);
#ifdef USE_ESP32
  constexpr uint32_t CAPS = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  this->buffers_[0] = static_cast<lv_color_t *>(heap_caps_aligned_alloc(64, frame_bytes, CAPS));
  this->buffers_[1] = static_cast<lv_color_t *>(heap_caps_aligned_alloc(64, frame_bytes, CAPS));
  this->angle_map_ = static_cast<uint16_t *>(heap_caps_aligned_alloc(64, angle_bytes, CAPS));
  this->coverage_map_ = static_cast<uint8_t *>(heap_caps_aligned_alloc(64, this->pixel_count_, CAPS));
#else
  this->buffers_[0] = static_cast<lv_color_t *>(std::malloc(frame_bytes));
  this->buffers_[1] = static_cast<lv_color_t *>(std::malloc(frame_bytes));
  this->angle_map_ = static_cast<uint16_t *>(std::malloc(angle_bytes));
  this->coverage_map_ = static_cast<uint8_t *>(std::malloc(this->pixel_count_));
#endif
  if (this->buffers_[0] == nullptr || this->buffers_[1] == nullptr || this->angle_map_ == nullptr ||
      this->coverage_map_ == nullptr) {
    this->free_buffers_();
    return false;
  }

  const float center_x = (width - 1) * 0.5f;
  const float center_y = (height - 1) * 0.5f;
  const float radius = std::max(1.0f, std::min(width, height) * 0.5f - this->thickness_ * 0.5f - 1.0f);
  const float half_width = this->thickness_ * 0.5f;
  constexpr float TWO_PI = 6.28318530718f;
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      const size_t index = static_cast<size_t>(y) * width + x;
      const float dx = x - center_x;
      const float dy = y - center_y;
      const float edge_distance = std::fabs(std::sqrt(dx * dx + dy * dy) - radius);
      const float coverage = std::clamp(half_width + 0.75f - edge_distance, 0.0f, 1.0f);
      this->coverage_map_[index] = static_cast<uint8_t>(coverage * 255.0f + 0.5f);
      float angle = std::atan2(dx, -dy);
      if (angle < 0.0f)
        angle += TWO_PI;
      this->angle_map_[index] = static_cast<uint16_t>(angle * (65536.0f / TWO_PI));
    }
  }
  return true;
}

bool MaterialDirectSpinner::submit_frame_(bool clear) {
  if (this->buffers_[0] == nullptr || this->present_in_flight_.load(std::memory_order_acquire))
    return false;
  this->buffer_index_ ^= 1U;
  auto *buffer = this->buffers_[this->buffer_index_];
  if (clear) {
    std::fill_n(buffer, this->pixel_count_, this->background_color_);
  } else {
    this->render_frame_(buffer, millis());
  }

  this->present_complete_.store(false, std::memory_order_release);
  this->present_in_flight_.store(true, std::memory_order_release);
  this->clear_in_flight_ = clear;
  const bool stable_frame = clear || this->first_frame_pending_;
  const uint8_t result = this->lvgl_component_->direct_blit_rgb888_async(
      reinterpret_cast<const uint8_t *>(buffer), this->width_ * static_cast<int>(sizeof(lv_color_t)), this->x_,
      this->y_, this->width_, this->height_, present_done_, this, stable_frame, !clear);
  if (result == LVGL_DIRECT_BLIT_SUBMITTED) {
    const uint32_t now = millis();
    if (this->stat_last_submit_ms_ != 0)
      this->stat_max_submit_gap_ms_ = std::max(this->stat_max_submit_gap_ms_, now - this->stat_last_submit_ms_);
    this->stat_last_submit_ms_ = now;
    this->stat_submitted_++;
    this->first_frame_pending_ = false;
    return true;
  }
  this->stat_rejected_++;
  this->present_in_flight_.store(false, std::memory_order_release);
  this->present_complete_.store(false, std::memory_order_release);
  this->clear_in_flight_ = false;
  return false;
}

void MaterialDirectSpinner::log_stats(const char *phase) const {
  ESP_LOGW(TAG,
           "%s active=%s stopping=%s prepared=%s in_flight=%s starts=%u duplicate=%u stops=%u submitted=%u "
           "rejected=%u max_gap=%ums",
           phase == nullptr ? "spinner" : phase, YESNO(this->active_), YESNO(this->stopping_), YESNO(this->prepared_),
           YESNO(this->present_in_flight_.load(std::memory_order_relaxed)), static_cast<unsigned>(this->stat_starts_),
           static_cast<unsigned>(this->stat_duplicate_starts_), static_cast<unsigned>(this->stat_stops_),
           static_cast<unsigned>(this->stat_submitted_), static_cast<unsigned>(this->stat_rejected_),
           static_cast<unsigned>(this->stat_max_submit_gap_ms_));
}

void MaterialDirectSpinner::render_frame_(lv_color_t *buffer, uint32_t now_ms) {
  std::fill_n(buffer, this->pixel_count_, this->background_color_);
  const uint32_t spin_time = std::max<uint32_t>(1, this->spin_time_);
  const uint16_t phase = static_cast<uint16_t>(((now_ms - this->started_ms_) % spin_time) * 65536ULL / spin_time);
  const uint16_t arc_span = static_cast<uint16_t>(std::min<uint32_t>(359, this->arc_length_) * 65536ULL / 360ULL);
  for (size_t index = 0; index < this->pixel_count_; index++) {
    const uint8_t coverage = this->coverage_map_[index];
    if (coverage == 0)
      continue;
    const uint16_t delta = static_cast<uint16_t>(this->angle_map_[index] - phase);
    const lv_color_t color = delta <= arc_span ? this->indicator_color_ : this->track_color_;
    const uint16_t inverse = 255U - coverage;
    auto &pixel = buffer[index];
    pixel.red = static_cast<uint8_t>((color.red * coverage + pixel.red * inverse + 127U) / 255U);
    pixel.green = static_cast<uint8_t>((color.green * coverage + pixel.green * inverse + 127U) / 255U);
    pixel.blue = static_cast<uint8_t>((color.blue * coverage + pixel.blue * inverse + 127U) / 255U);
  }
}

void MaterialDirectSpinner::release_region_() {
  if (this->lvgl_component_ != nullptr && this->width_ > 0 && this->height_ > 0)
    this->lvgl_component_->direct_blit_rgb888_release(this->x_, this->y_, this->width_, this->height_);
}

void MaterialDirectSpinner::free_buffers_() {
#ifdef USE_ESP32
  heap_caps_free(this->buffers_[0]);
  heap_caps_free(this->buffers_[1]);
  heap_caps_free(this->angle_map_);
  heap_caps_free(this->coverage_map_);
#else
  std::free(this->buffers_[0]);
  std::free(this->buffers_[1]);
  std::free(this->angle_map_);
  std::free(this->coverage_map_);
#endif
  this->buffers_[0] = nullptr;
  this->buffers_[1] = nullptr;
  this->angle_map_ = nullptr;
  this->coverage_map_ = nullptr;
  this->pixel_count_ = 0;
  this->width_ = 0;
  this->height_ = 0;
  this->prepared_ = false;
}

}  // namespace esphome::lvgl_material
