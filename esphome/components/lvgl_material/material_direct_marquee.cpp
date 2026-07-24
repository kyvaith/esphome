#include "material_direct_marquee.h"

#include "esphome/core/log.h"

#include <algorithm>
#include <cstdlib>

#ifdef USE_ESP32
#include "esp_heap_caps.h"
#include "esp_timer.h"
#endif

namespace esphome::lvgl_material {

static const char *const TAG = "lvgl_material.marquee";

void MaterialDirectMarquee::setup() {
  if (this->lvgl_component_ == nullptr || this->label_ == nullptr || this->viewport_ == nullptr) {
    ESP_LOGE(TAG, "Direct marquee configuration is incomplete");
    this->mark_failed();
  }
}

void MaterialDirectMarquee::on_shutdown() {
  this->end(true);
  this->service();
}

void MaterialDirectMarquee::dump_config() {
  ESP_LOGCONFIG(TAG, "Material Direct Marquee:");
  ESP_LOGCONFIG(TAG, "  Label: %s", this->label_ == nullptr ? "missing" : "configured");
  ESP_LOGCONFIG(TAG, "  Viewport: %s", this->viewport_ == nullptr ? "missing" : "configured");
}

#ifdef USE_ESP32
void MaterialDirectMarquee::log_performance_(int64_t now_us) {
  if (lvgl_esphome_get_perf_logging_enabled() == 0)
    return;
  if (this->perf_window_start_us_ == 0)
    this->perf_window_start_us_ = now_us;
  if (now_us - this->perf_window_start_us_ < 2000000LL)
    return;

  const uint32_t compose_avg_us = this->perf_compose_count_ == 0
                                      ? 0
                                      : static_cast<uint32_t>(this->perf_compose_total_us_ / this->perf_compose_count_);
  const uint32_t dma_avg_us =
      this->perf_dma_count_ == 0 ? 0 : static_cast<uint32_t>(this->perf_dma_total_us_ / this->perf_dma_count_);
  ESP_LOGI(TAG, "perf2s: compose=%lu avg=%luus max=%luus async=%lu busy=%lu sync=%lu dma=%luus max=%luus",
           static_cast<unsigned long>(this->perf_compose_count_), static_cast<unsigned long>(compose_avg_us),
           static_cast<unsigned long>(this->perf_compose_max_us_), static_cast<unsigned long>(this->perf_async_count_),
           static_cast<unsigned long>(this->perf_busy_count_), static_cast<unsigned long>(this->perf_sync_count_),
           static_cast<unsigned long>(dma_avg_us), static_cast<unsigned long>(this->perf_dma_max_us_));
  this->perf_compose_count_ = 0;
  this->perf_compose_total_us_ = 0;
  this->perf_compose_max_us_ = 0;
  this->perf_async_count_ = 0;
  this->perf_busy_count_ = 0;
  this->perf_sync_count_ = 0;
  this->perf_dma_count_ = 0;
  this->perf_dma_total_us_ = 0;
  this->perf_dma_max_us_ = 0;
  this->perf_window_start_us_ = now_us;
}
#endif

void MaterialDirectMarquee::release_() {
  if (this->text_ != nullptr)
    lv_draw_buf_destroy(this->text_);
#ifdef USE_ESP32
  if (this->background_ != nullptr)
    heap_caps_free(this->background_);
#else
  std::free(this->background_);
#endif
  this->text_ = nullptr;
  this->background_ = nullptr;
  this->screen_x_ = 0;
  this->screen_y_ = 0;
  this->width_ = 0;
  this->height_ = 0;
  this->source_x_offset_ = 0;
  this->source_y_offset_ = 0;
  this->last_x_ = INT_MIN;
  this->pending_x_.store(INT_MIN, std::memory_order_release);
  this->active_.store(false, std::memory_order_release);
  this->cleanup_pending_ = false;
  this->present_complete_.store(false, std::memory_order_release);
  this->present_in_flight_.store(false, std::memory_order_release);
}

void MaterialDirectMarquee::service(bool allow_cleanup) {
  if (this->present_in_flight_.load(std::memory_order_acquire) &&
      this->present_complete_.exchange(false, std::memory_order_acq_rel)) {
#ifdef USE_ESP32
    const int64_t now_us = esp_timer_get_time();
    if (this->present_started_us_ != 0 && lvgl_esphome_get_perf_logging_enabled() != 0) {
      const uint32_t dma_us = static_cast<uint32_t>(now_us - this->present_started_us_);
      this->perf_dma_count_++;
      this->perf_dma_total_us_ += dma_us;
      if (dma_us > this->perf_dma_max_us_)
        this->perf_dma_max_us_ = dma_us;
    }
    this->present_started_us_ = 0;
#endif
    this->present_in_flight_.store(false, std::memory_order_release);
  }
#ifdef USE_ESP32
  this->log_performance_(esp_timer_get_time());
#endif
  const bool worker_idle =
#ifdef USE_ESP32
      !this->worker_busy_.load(std::memory_order_acquire);
#else
      true;
#endif
  if (allow_cleanup && this->cleanup_pending_ && !this->present_in_flight_.load(std::memory_order_acquire) &&
      worker_idle) {
    this->release_();
  }
}

void MaterialDirectMarquee::present_done_(void *arg) {
  auto *marquee = static_cast<MaterialDirectMarquee *>(arg);
  if (marquee == nullptr)
    return;
  marquee->present_complete_.store(true, std::memory_order_release);
#ifdef USE_ESP32
  if (marquee->worker_handle_ != nullptr)
    xTaskNotifyGive(marquee->worker_handle_);
#endif
}

void MaterialDirectMarquee::end(bool restore_native) {
  const bool was_active = this->active_.exchange(false, std::memory_order_acq_rel);
  if (was_active) {
    this->lvgl_component_->direct_blit_rgb888_release(this->screen_x_, this->screen_y_, this->width_, this->height_);
  }
  if (restore_native && this->label_ != nullptr) {
    lv_obj_set_x(this->label_, 0);
    lv_obj_clear_flag(this->label_, LV_OBJ_FLAG_HIDDEN);
  }
  this->cleanup_pending_ = true;
  this->pending_x_.store(INT_MIN, std::memory_order_release);
#ifdef USE_ESP32
  if (this->worker_handle_ != nullptr)
    xTaskNotifyGive(this->worker_handle_);
#endif
  this->service();
}

bool MaterialDirectMarquee::render_(int offset_x) {
  if (!this->active_.load(std::memory_order_acquire) || this->text_ == nullptr || this->background_ == nullptr ||
      this->present_in_flight_.load(std::memory_order_acquire)) {
    return false;
  }
  if (offset_x == this->last_x_)
    return true;

#ifdef USE_ESP32
  const bool perf_enabled = lvgl_esphome_get_perf_logging_enabled() != 0;
  const int64_t compose_started_us = perf_enabled ? esp_timer_get_time() : 0;
#endif
  const int source_x = this->source_x_offset_ - offset_x;
#ifdef USE_ESP32
  if (perf_enabled) {
    const uint32_t compose_us = static_cast<uint32_t>(esp_timer_get_time() - compose_started_us);
    this->perf_compose_count_++;
    this->perf_compose_total_us_ += compose_us;
    if (compose_us > this->perf_compose_max_us_)
      this->perf_compose_max_us_ = compose_us;
  }
#endif

  this->present_complete_.store(false, std::memory_order_release);
  this->present_in_flight_.store(true, std::memory_order_release);
#ifdef USE_ESP32
  this->present_started_us_ = perf_enabled ? esp_timer_get_time() : 0;
#endif
  const uint8_t async_result = this->lvgl_component_->direct_blend_argb8888_async(
      reinterpret_cast<const uint8_t *>(this->background_), this->width_ * static_cast<int>(sizeof(lv_color_t)),
      this->text_->data, this->text_->header.stride, this->text_->header.w, this->text_->header.h, source_x,
      this->source_y_offset_, this->screen_x_, this->screen_y_, this->width_, this->height_, present_done_, this);
  if (async_result == LVGL_DIRECT_BLIT_SUBMITTED) {
#ifdef USE_ESP32
    if (perf_enabled)
      this->perf_async_count_++;
#endif
    this->last_x_ = offset_x;
    return true;
  }
  this->present_in_flight_.store(false, std::memory_order_release);
  this->present_complete_.store(false, std::memory_order_release);
#ifdef USE_ESP32
  this->present_started_us_ = 0;
#endif
  if (async_result == LVGL_DIRECT_BLIT_BUSY) {
#ifdef USE_ESP32
    if (perf_enabled)
      this->perf_busy_count_++;
#endif
    int expected = INT_MIN;
    this->pending_x_.compare_exchange_strong(expected, offset_x, std::memory_order_acq_rel);
    return true;
  }
#ifdef USE_ESP32
  if (perf_enabled)
    this->perf_sync_count_++;
#endif
  return false;
}

#ifdef USE_ESP32
void MaterialDirectMarquee::worker_(void *arg) {
  auto *marquee = static_cast<MaterialDirectMarquee *>(arg);
  if (marquee == nullptr) {
    vTaskDelete(nullptr);
    return;
  }
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    marquee->service(false);
    while (marquee->active_.load(std::memory_order_acquire) &&
           !marquee->present_in_flight_.load(std::memory_order_acquire)) {
      const int offset_x = marquee->pending_x_.exchange(INT_MIN, std::memory_order_acq_rel);
      if (offset_x == INT_MIN || offset_x == marquee->last_x_)
        break;
      marquee->worker_busy_.store(true, std::memory_order_release);
      const bool rendered = marquee->render_(offset_x);
      marquee->worker_busy_.store(false, std::memory_order_release);
      if (!rendered && marquee->active_.load(std::memory_order_acquire)) {
        int expected = INT_MIN;
        marquee->pending_x_.compare_exchange_strong(expected, offset_x, std::memory_order_acq_rel);
      }
      if (marquee->present_in_flight_.load(std::memory_order_acquire) || !rendered)
        break;
    }
  }
}
#endif

bool MaterialDirectMarquee::ensure_worker_() {
#ifdef USE_ESP32
  if (this->worker_handle_ != nullptr)
    return true;
#if CONFIG_FREERTOS_UNICORE
  constexpr BaseType_t WORKER_CORE = tskNO_AFFINITY;
#elif defined(CONFIG_ESP_MAIN_TASK_AFFINITY_CPU0) && CONFIG_ESP_MAIN_TASK_AFFINITY_CPU0
  constexpr BaseType_t WORKER_CORE = 1;
#elif defined(CONFIG_ESP_MAIN_TASK_AFFINITY_CPU1) && CONFIG_ESP_MAIN_TASK_AFFINITY_CPU1
  constexpr BaseType_t WORKER_CORE = 0;
#else
  constexpr BaseType_t WORKER_CORE = 1;
#endif
  constexpr uint32_t WORKER_STACK_SIZE = 6144;
  this->worker_stack_ =
      static_cast<StackType_t *>(heap_caps_aligned_alloc(16, WORKER_STACK_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (this->worker_stack_ == nullptr) {
    this->worker_stack_ =
        static_cast<StackType_t *>(heap_caps_aligned_alloc(16, WORKER_STACK_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (this->worker_stack_ == nullptr)
    return false;
  this->worker_handle_ = xTaskCreateStaticPinnedToCore(worker_, "material_marquee", WORKER_STACK_SIZE, this, 1,
                                                       this->worker_stack_, &this->worker_storage_, WORKER_CORE);
  if (this->worker_handle_ == nullptr) {
    heap_caps_free(this->worker_stack_);
    this->worker_stack_ = nullptr;
    return false;
  }
#endif
  return true;
}

bool MaterialDirectMarquee::update(int offset_x) {
  this->service();
  if (!this->active_.load(std::memory_order_acquire) || this->text_ == nullptr || this->background_ == nullptr)
    return false;
  if (offset_x == this->last_x_ && !this->present_in_flight_.load(std::memory_order_acquire))
    return true;
  this->pending_x_.store(offset_x, std::memory_order_release);
#ifdef USE_ESP32
  if (this->worker_handle_ != nullptr) {
    xTaskNotifyGive(this->worker_handle_);
    return true;
  }
#endif
  if (this->present_in_flight_.load(std::memory_order_acquire))
    return true;
  const int pending_x = this->pending_x_.exchange(INT_MIN, std::memory_order_acq_rel);
  return pending_x == INT_MIN || this->render_(pending_x);
}

bool MaterialDirectMarquee::begin() {
  this->service();
  this->end(true);
  this->service();
  if (this->cleanup_pending_ || this->is_failed() || this->label_ == nullptr || this->viewport_ == nullptr)
    return false;

  lv_obj_update_layout(this->viewport_);
  lv_area_t viewport_area{};
  lv_obj_get_coords(this->viewport_, &viewport_area);
  const int width = lv_area_get_width(&viewport_area);
  const int height = lv_area_get_height(&viewport_area);
  if (width <= 0 || height <= 0 || lv_obj_get_width(this->label_) <= width)
    return false;
  if (sizeof(lv_color_t) != 3) {
    ESP_LOGE(TAG, "Direct marquee requires an RGB888 LVGL color type");
    return false;
  }

  this->screen_x_ = viewport_area.x1;
  this->screen_y_ = viewport_area.y1;
  this->width_ = width;
  this->height_ = height;
  this->text_ = lv_snapshot_take(this->label_, LV_COLOR_FORMAT_ARGB8888);
  const size_t pixel_count = static_cast<size_t>(width) * height;
#ifdef USE_ESP32
  this->background_ = static_cast<lv_color_t *>(
      heap_caps_malloc(pixel_count * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
  this->background_ = static_cast<lv_color_t *>(std::malloc(pixel_count * sizeof(lv_color_t)));
#endif
  if (this->text_ == nullptr || this->text_->header.cf != LV_COLOR_FORMAT_ARGB8888 || this->background_ == nullptr) {
    this->end(true);
    return false;
  }

  const int label_width = static_cast<int>(lv_obj_get_width(this->label_));
  const int label_height = static_cast<int>(lv_obj_get_height(this->label_));
  this->source_x_offset_ = std::max(0, (static_cast<int>(this->text_->header.w) - label_width) / 2);
  this->source_y_offset_ = std::max(0, (static_cast<int>(this->text_->header.h) - label_height) / 2);

  lv_display_t *display = lv_obj_get_display(this->label_);
  this->lvgl_component_->direct_regions_pause(true, 120);
  lv_obj_add_flag(this->label_, LV_OBJ_FLAG_HIDDEN);
  if (display != nullptr)
    lv_refr_now(display);
  if (!this->lvgl_component_->direct_capture_rgb888(reinterpret_cast<uint8_t *>(this->background_),
                                                    width * static_cast<int>(sizeof(lv_color_t)), this->screen_x_,
                                                    this->screen_y_, width, height)) {
    this->lvgl_component_->direct_regions_pause(false, 0);
    this->end(true);
    return false;
  }
  this->lvgl_component_->direct_regions_pause(false, 0);

  this->active_.store(true, std::memory_order_release);
  this->last_x_ = INT_MIN;
  if (!this->render_(0)) {
    this->end(true);
    return false;
  }
  this->lvgl_component_->wait_for_direct_frame_presented(40);
  this->ensure_worker_();
  return true;
}

}  // namespace esphome::lvgl_material
