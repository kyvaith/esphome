#include "ken_burns.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <numeric>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include "lvgl_esphome.h"

#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#endif

namespace esphome::lvgl {

static const char *const TAG = "lvgl.ken_burns";

void KenBurnsController::setup() {
  this->choose_target_();
  this->last_loop_ms_ = millis();
#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  if (this->use_direct_) {
#if CONFIG_FREERTOS_UNICORE
    constexpr BaseType_t worker_core = tskNO_AFFINITY;
#elif defined(CONFIG_ESP_MAIN_TASK_AFFINITY_CPU0) && CONFIG_ESP_MAIN_TASK_AFFINITY_CPU0
    constexpr BaseType_t worker_core = 1;
#elif defined(CONFIG_ESP_MAIN_TASK_AFFINITY_CPU1) && CONFIG_ESP_MAIN_TASK_AFFINITY_CPU1
    constexpr BaseType_t worker_core = 0;
#else
    constexpr BaseType_t worker_core = 1;
#endif
    constexpr uint32_t worker_stack_size = 8192;
    this->direct_worker_stack_ = static_cast<StackType_t *>(
        heap_caps_aligned_alloc(16, worker_stack_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (this->direct_worker_stack_ == nullptr) {
      this->direct_worker_stack_ = static_cast<StackType_t *>(
          heap_caps_aligned_alloc(16, worker_stack_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    if (this->direct_worker_stack_ != nullptr) {
      this->direct_worker_handle_ = xTaskCreateStaticPinnedToCore(
          &KenBurnsController::direct_worker_trampoline_, "lvgl_ken_burns", worker_stack_size, this, 1,
          this->direct_worker_stack_, &this->direct_worker_storage_, worker_core);
    }
    if (this->direct_worker_handle_ == nullptr) {
      if (this->direct_worker_stack_ != nullptr)
        heap_caps_free(this->direct_worker_stack_);
      this->direct_worker_stack_ = nullptr;
      this->direct_worker_handle_ = nullptr;
      ESP_LOGE(TAG, "Failed to create direct compositor task");
    } else {
      ESP_LOGI(TAG, "Direct compositor worker uses %s stack",
               esp_ptr_external_ram(this->direct_worker_stack_) ? "PSRAM" : "internal");
    }
  }
#endif
}

void KenBurnsController::loop() {
#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  if (this->use_direct_ && this->direct_worker_handle_ != nullptr) {
    if (this->direct_start_pending_.exchange(false)) {
      if (!this->start_direct_worker_()) {
        // Opening an app temporarily keeps the final snapshot frame pinned.
        // That hand-off is expected to last a few LVGL loops; retry instead of
        // permanently falling back to the regular image draw path.
        if (!this->paused_)
          this->direct_start_pending_.store(true);
      }
      return;
    }
    if (this->direct_worker_failed_.exchange(false)) {
      this->stop_direct_worker_();
      if (this->direct_active_) {
        this->update_transform_(this->phase_elapsed_ms_);
        if (auto *component = this->get_lvgl_component_()) {
          if (component->end_direct_image_animation()) {
            this->direct_active_ = false;
          } else {
            this->direct_worker_failed_.store(true);
            return;
          }
        }
      }
      ESP_LOGW(TAG, "Direct compositor stopped after a rejected frame");
    }
    return;
  }
#endif

  const uint32_t now = millis();
  const uint32_t delta = now - this->last_loop_ms_;
  if (delta < this->frame_interval_ms_)
    return;
  this->last_loop_ms_ = now;

  if (this->paused_ || this->obj_ == nullptr)
    return;

  lv_lock();
  const bool visible = lv_obj_is_valid(this->obj_) && lv_obj_is_visible(this->obj_);
  lv_unlock();
  if (!visible)
    return;

  // Do not jump forward after a blocked main loop or while the image was hidden.
  if (!this->phase_complete_) {
    this->phase_elapsed_ms_ = std::min(this->phase_duration_ms_,
                                       this->phase_elapsed_ms_ + std::min(delta, this->frame_interval_ms_ * 3U));
    this->phase_complete_ = this->phase_elapsed_ms_ >= this->phase_duration_ms_;
  }
  if (this->direct_active_) {
    // A rejected PPA frame must leave the last complete DSI frame visible.
    // Switching back to LVGL here causes a full redraw and mode ping-pong.
    this->update_direct_frame_(this->phase_elapsed_ms_);
  } else if (!this->update_direct_frame_(this->phase_elapsed_ms_)) {
    this->update_transform_(this->phase_elapsed_ms_);
  }
}

void KenBurnsController::restart() {
#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  this->transition_requested_.store(false);
  this->transition_active_.store(false);
  this->transition_source_.store(nullptr);
  this->direct_start_pending_.store(false);
  this->stop_direct_worker_();
  this->release_transition_buffers_();
#endif
  this->phase_elapsed_ms_ = 0;
  this->zooming_in_ = true;
  this->phase_complete_ = false;
  this->paused_ = false;
  this->choose_target_();
  this->last_loop_ms_ = millis();
#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  if (this->use_direct_ && this->direct_worker_handle_ != nullptr) {
    // The image decoder invokes restart() from its completion callback. Start
    // the PPA worker on the next component loop so that LVGL source promotion,
    // callback teardown and the first DMA2D transaction never overlap.
    this->direct_start_pending_.store(true);
    return;
  }
#endif
  this->direct_active_ = this->update_direct_frame_(0);
  if (!this->direct_active_)
    this->update_transform_(0);
}

bool KenBurnsController::pause() {
  this->paused_ = true;
  this->last_loop_ms_ = millis();
#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  this->transition_requested_.store(false);
  this->transition_source_.store(nullptr);
  this->direct_start_pending_.store(false);
  if (!this->stop_direct_worker_())
    return false;
  this->transition_active_.store(false);
#endif
  if (this->direct_active_) {
    // Put the LVGL object at the exact last direct-compositor position before
    // handing refresh ownership back. The visible DSI frame remains frozen
    // until LVGL has completed its replacement frame.
    this->update_transform_(this->phase_elapsed_ms_);
    if (this->get_lvgl_component_() == nullptr || !this->complete_snapshot_handoff())
      return false;
    lv_lock();
    if (this->obj_ != nullptr && lv_obj_is_valid(this->obj_)) {
      lv_obj_invalidate(lv_obj_get_parent(this->obj_));
      lv_refr_now(lv_obj_get_display(this->obj_));
    }
    lv_unlock();
  }
  return true;
}

bool KenBurnsController::pause_for_snapshot() {
  this->paused_ = true;
  this->last_loop_ms_ = millis();
#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  this->transition_requested_.store(false);
  this->transition_source_.store(nullptr);
  this->direct_start_pending_.store(false);
  if (!this->stop_direct_worker_())
    return false;
  this->transition_active_.store(false);
  if (this->direct_active_) {
    this->update_transform_(this->phase_elapsed_ms_);
    if (!this->complete_snapshot_handoff()) {
      ESP_LOGW(TAG, "Snapshot handoff deferred while the final DSI frame is pending");
      return false;
    }
  }
  this->release_transition_buffers_();
  // The last complete direct frame remains visible and can now be captured,
  // but LVGL owns the framebuffers again before the snapshot compositor runs.
  return true;
#else
  return false;
#endif
}

bool KenBurnsController::complete_snapshot_handoff(uint32_t timeout_ms) {
#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  if (!this->direct_active_)
    return true;
  auto *component = this->get_lvgl_component_();
  if (component == nullptr)
    return false;

  const uint32_t started_ms = millis();
  do {
    if (component->end_direct_image_animation()) {
      this->direct_active_ = false;
      return true;
    }
    vTaskDelay(1);
  } while (millis() - started_ms < timeout_ms);
  ESP_LOGW(TAG, "Direct image handoff timed out after %ums", static_cast<unsigned>(timeout_ms));
  return false;
#else
  (void) timeout_ms;
  return true;
#endif
}

void KenBurnsController::resume() {
#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  this->direct_start_pending_.store(false);
  this->stop_direct_worker_();
#endif
  this->paused_ = false;
  this->last_loop_ms_ = millis();
#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  if (this->use_direct_ && this->direct_worker_handle_ != nullptr) {
    this->direct_start_pending_.store(true);
    return;
  }
#endif
  this->direct_active_ = this->update_direct_frame_(this->phase_elapsed_ms_);
}

void KenBurnsController::reset_transform() {
  if (this->obj_ == nullptr)
    return;
  lv_lock();
  if (lv_obj_is_valid(this->obj_)) {
    lv_display_t *display = lv_obj_get_display(this->obj_);
    const bool invalidation_enabled = lv_display_is_invalidation_enabled(display);
    if (invalidation_enabled)
      lv_display_enable_invalidation(display, false);
    lv_image_set_pivot(this->obj_, lv_obj_get_width(this->obj_) / 2, lv_obj_get_height(this->obj_) / 2);
    lv_image_set_scale(this->obj_, LV_SCALE_NONE);
    lv_obj_set_pos(this->obj_, 0, 0);
    if (invalidation_enabled) {
      lv_display_enable_invalidation(display, true);
      lv_obj_invalidate(lv_obj_get_parent(this->obj_));
    }
    this->geometry_source_width_ = 0;
    this->geometry_source_height_ = 0;
    this->geometry_viewport_width_ = 0;
    this->geometry_viewport_height_ = 0;
  }
  lv_unlock();
}

bool KenBurnsController::transition_to(const lv_image_dsc_t *source, uint32_t duration_ms) {
#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  if (!this->use_direct_ || this->paused_ || !this->direct_active_ || this->direct_worker_handle_ == nullptr ||
      source == nullptr || source->data == nullptr || source->header.w < 2 || source->header.h < 2 ||
      this->transition_requested_.load() || this->transition_active_.load()) {
    return false;
  }

  this->transition_duration_ms_.store(std::clamp<uint32_t>(duration_ms, 200, 3000));
  this->transition_failed_.store(false, std::memory_order_release);
  this->transition_source_.store(source, std::memory_order_release);
  this->transition_requested_.store(true, std::memory_order_release);
  // A client may freeze the direct worker while downloading or decoding the
  // next image. Queue the transition before waking it so no obsolete Ken
  // Burns frame is rendered between the frozen frame and the crossfade.
  this->direct_worker_failed_.store(false, std::memory_order_release);
  this->direct_worker_run_.store(true, std::memory_order_release);
  xTaskNotifyGive(this->direct_worker_handle_);
  return true;
#else
  return false;
#endif
}

bool KenBurnsController::is_transition_pending_or_active() const {
#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  return this->transition_requested_.load(std::memory_order_acquire) ||
         this->transition_active_.load(std::memory_order_acquire);
#else
  return false;
#endif
}

bool KenBurnsController::transition_failed() const {
#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  return this->transition_failed_.load(std::memory_order_acquire);
#else
  return false;
#endif
}

bool KenBurnsController::freeze_direct() {
#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  if (!this->direct_active_ || !this->direct_worker_run_.load(std::memory_order_acquire))
    return false;
  this->stop_direct_worker_();
  // The incoming JPEG needs the largest contiguous PSRAM allocation in the
  // gallery pipeline. Transition bands are cheap to recreate after decode;
  // keeping them while the next source is downloaded needlessly fragments
  // the exact window in which the hardware decoder allocates its output.
  this->release_transition_buffers_();
  return true;
#else
  return false;
#endif
}

void KenBurnsController::resume_direct() {
#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  if (!this->direct_active_ || this->paused_ || this->direct_worker_handle_ == nullptr ||
      this->direct_worker_run_.load(std::memory_order_acquire)) {
    return;
  }
  this->direct_worker_failed_.store(false, std::memory_order_release);
  this->direct_worker_run_.store(true, std::memory_order_release);
  xTaskNotifyGive(this->direct_worker_handle_);
#endif
}

size_t KenBurnsController::memory_usage_bytes() const {
#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  size_t bytes = this->direct_worker_stack_ == nullptr ? 0 : 8192;
  if (this->transition_old_frame_ != nullptr && this->transition_old_frame_owned_)
    bytes += this->transition_old_frame_size_;
  if (this->transition_new_frame_ != nullptr && this->transition_new_frame_owned_)
    bytes += this->transition_frame_size_;
  return bytes;
#else
  return 0;
#endif
}

void KenBurnsController::log_memory_usage(const char *phase) const {
#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  const size_t worker_bytes = this->direct_worker_stack_ == nullptr ? 0 : 8192;
  const size_t old_bytes = this->transition_old_frame_ != nullptr && this->transition_old_frame_owned_
                               ? this->transition_old_frame_size_
                               : 0;
  const size_t new_bytes = this->transition_new_frame_ != nullptr && this->transition_new_frame_owned_
                               ? this->transition_frame_size_
                               : 0;
  ESP_LOGW(TAG, "%s memory=%uK worker=%uK transition_old=%uK transition_new=%uK",
           phase == nullptr ? "runtime" : phase, (unsigned) (this->memory_usage_bytes() / 1024),
           (unsigned) (worker_bytes / 1024), (unsigned) (old_bytes / 1024), (unsigned) (new_bytes / 1024));
#else
  (void) phase;
#endif
}

LvglComponent *KenBurnsController::get_lvgl_component_() const {
  if (this->obj_ == nullptr || !lv_obj_is_valid(this->obj_))
    return nullptr;
  auto *display = lv_obj_get_display(this->obj_);
  return display == nullptr ? nullptr : static_cast<LvglComponent *>(lv_display_get_user_data(display));
}

const lv_image_dsc_t *KenBurnsController::get_source_descriptor_() const {
  if (this->obj_ == nullptr || !lv_obj_is_valid(this->obj_))
    return nullptr;
  const void *source = lv_image_get_src(this->obj_);
  if (source == nullptr || lv_image_src_get_type(source) != LV_IMAGE_SRC_VARIABLE)
    return nullptr;
  return static_cast<const lv_image_dsc_t *>(source);
}

bool KenBurnsController::update_direct_frame_(uint32_t elapsed_ms) {
  if (!this->use_direct_ || this->obj_ == nullptr || this->phase_duration_ms_ == 0)
    return false;

  lv_lock();
  const bool valid = lv_obj_is_valid(this->obj_) && lv_obj_is_visible(this->obj_);
  const lv_image_dsc_t *source = valid ? this->get_source_descriptor_() : nullptr;
  LvglComponent *component = valid ? this->get_lvgl_component_() : nullptr;
  lv_unlock();
  if (source == nullptr || component == nullptr || source->header.w < 2 || source->header.h < 2)
    return false;
  if (!component->begin_direct_image_animation())
    return false;

  const bool was_direct_active = this->direct_active_;
  const bool rendered = this->render_direct_frame_(source, component, elapsed_ms);
  if (rendered) {
    this->direct_active_ = true;
  } else if (!was_direct_active) {
    component->end_direct_image_animation();
  }
  return rendered;
}

bool KenBurnsController::calculate_direct_crop_(const lv_image_dsc_t *source, uint32_t elapsed_ms, int *crop_x,
                                                 int *crop_y, int *crop_width, int *crop_height) const {
  if (source == nullptr || source->data == nullptr || source->header.w < 2 ||
      source->header.h < 2 || this->phase_duration_ms_ == 0 || crop_x == nullptr || crop_y == nullptr ||
      crop_width == nullptr || crop_height == nullptr)
    return false;

  const float linear = std::clamp(static_cast<float>(elapsed_ms) / static_cast<float>(this->phase_duration_ms_),
                                  0.0f, 1.0f);
  // The direct PPA path works with integer source coordinates. Smoothstep
  // spends too much of every phase below a one-pixel delta, which looks like
  // repeated pauses followed by a jump. Constant velocity keeps useful source
  // pixels arriving at a steady cadence for the single pass over each photo.
  const float progress = linear;
  const float directed = this->zooming_in_ ? progress : 1.0f - progress;
  // PPA exposes only four fractional scale bits. Continuously changing the
  // crop size therefore produces large 427 -> 413 -> 400 pixel jumps on an
  // 800px viewport. Keep one scale for the whole phase and animate only the
  // source origin; this remains a single hardware SRM operation per frame.
  const float fixed_zoom =
      (static_cast<float>(this->zoom_start_) + static_cast<float>(this->zoom_end_)) * 0.5f;
  const float configured_zoom =
      fixed_zoom / static_cast<float>(LV_SCALE_NONE);

  const float source_aspect = static_cast<float>(source->header.w) / static_cast<float>(source->header.h);
  constexpr float VIEWPORT_ASPECT = 1.0f;
  float base_crop_width;
  float base_crop_height;
  if (source_aspect > VIEWPORT_ASPECT) {
    base_crop_height = static_cast<float>(source->header.h);
    base_crop_width = base_crop_height * VIEWPORT_ASPECT;
  } else {
    base_crop_width = static_cast<float>(source->header.w);
    base_crop_height = base_crop_width / VIEWPORT_ASPECT;
  }

  *crop_width = std::clamp(static_cast<int>(std::lround(base_crop_width / configured_zoom)), 2,
                           static_cast<int>(source->header.w));
  *crop_height = std::clamp(static_cast<int>(std::lround(base_crop_height / configured_zoom)), 2,
                            static_cast<int>(source->header.h));
  const float movable_x = static_cast<float>(source->header.w - *crop_width);
  const float movable_y = static_cast<float>(source->header.h - *crop_height);
  /* A cover crop of a landscape photo has very little useful vertical travel;
   * applying an independent random Y target there turns each quantized PPA
   * scale step into a visible up/down jump. Keep motion on the image's long
   * axis. Portrait photos use the corresponding vertical path. */
  float effective_target_x = this->target_x_;
  float effective_target_y = this->target_y_;
  if (source->header.w > source->header.h) {
    if (std::abs(effective_target_x) < 0.65f * this->pan_limit_)
      effective_target_x = effective_target_x < 0.0f ? -0.65f * this->pan_limit_ : 0.65f * this->pan_limit_;
    effective_target_y = 0.0f;
  } else if (source->header.h > source->header.w) {
    if (std::abs(effective_target_y) < 0.65f * this->pan_limit_)
      effective_target_y = effective_target_y < 0.0f ? -0.65f * this->pan_limit_ : 0.65f * this->pan_limit_;
    effective_target_x = 0.0f;
  }
  const float pan_progress = directed * 2.0f - 1.0f;
  const float center_x = static_cast<float>(source->header.w) * 0.5f +
                         effective_target_x * movable_x * 0.5f * pan_progress;
  const float center_y = static_cast<float>(source->header.h) * 0.5f +
                         effective_target_y * movable_y * 0.5f * pan_progress;
  *crop_x = std::clamp(static_cast<int>(std::lround(center_x - *crop_width * 0.5f)), 0,
                       static_cast<int>(source->header.w) - *crop_width);
  *crop_y = std::clamp(static_cast<int>(std::lround(center_y - *crop_height * 0.5f)), 0,
                       static_cast<int>(source->header.h) - *crop_height);
  return true;
}

bool KenBurnsController::render_direct_frame_(const lv_image_dsc_t *source, LvglComponent *component,
                                               uint32_t elapsed_ms) {
  if (source == nullptr || component == nullptr || source->data == nullptr || source->header.w < 2 ||
      source->header.h < 2 || this->phase_duration_ms_ == 0)
    return false;

#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  this->direct_component_ = component;
  int crop_x = 0;
  int crop_y = 0;
  int crop_width = 0;
  int crop_height = 0;
  if (!this->calculate_direct_crop_(source, elapsed_ms, &crop_x, &crop_y, &crop_width, &crop_height))
    return false;

  // Source coordinates are integer pixels. At a calm pan speed many timer
  // ticks resolve to the exact same crop; submitting those frames used to
  // make PPA read and rewrite the complete 800x800 image for no visible
  // change. Keep the already scanned framebuffer until the crop really moves.
  if (this->last_direct_source_data_ == source->data && this->last_direct_crop_x_ == crop_x &&
      this->last_direct_crop_y_ == crop_y && this->last_direct_crop_width_ == crop_width &&
      this->last_direct_crop_height_ == crop_height) {
    return true;
  }
  const bool presented = component->direct_present_image_crop(source, crop_x, crop_y, crop_width, crop_height,
                                                                this->direct_srm_client_);
  if (presented) {
    this->last_direct_source_data_ = source->data;
    this->last_direct_crop_x_ = crop_x;
    this->last_direct_crop_y_ = crop_y;
    this->last_direct_crop_width_ = crop_width;
    this->last_direct_crop_height_ = crop_height;
  }
  return presented;
#else
  return false;
#endif
}

void KenBurnsController::reset_direct_frame_cache_() {
#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  this->last_direct_source_data_ = nullptr;
  this->last_direct_crop_x_ = -1;
  this->last_direct_crop_y_ = -1;
  this->last_direct_crop_width_ = -1;
  this->last_direct_crop_height_ = -1;
#endif
}

#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
bool KenBurnsController::start_direct_worker_() {
  if (!this->use_direct_ || this->paused_ || this->direct_worker_handle_ == nullptr || this->obj_ == nullptr)
    return false;

  lv_image_dsc_t source{};
  LvglComponent *component = nullptr;
  lv_lock();
  const bool valid = lv_obj_is_valid(this->obj_) && lv_obj_is_visible(this->obj_);
  const lv_image_dsc_t *descriptor = valid ? this->get_source_descriptor_() : nullptr;
  component = valid ? this->get_lvgl_component_() : nullptr;
  if (descriptor != nullptr)
    source = *descriptor;
  lv_unlock();

  if (component == nullptr || source.data == nullptr || source.header.w < 2 || source.header.h < 2)
    return false;
  if (this->direct_srm_client_ == nullptr)
    return false;
  if (!component->begin_direct_image_animation())
    return false;

  this->direct_source_ = source;
  this->direct_component_ = component;
  this->reset_direct_frame_cache_();
  this->direct_worker_failed_.store(false);
  this->direct_worker_run_.store(true);
  this->direct_active_ = true;
  xTaskNotifyGive(this->direct_worker_handle_);
  return true;
}

bool KenBurnsController::stop_direct_worker_(uint32_t timeout_ms) {
  if (this->direct_worker_handle_ == nullptr)
    return true;
  this->direct_worker_run_.store(false);
  xTaskNotifyGive(this->direct_worker_handle_);
  const uint32_t started_ms = millis();
  while (this->direct_worker_active_.load()) {
    if (millis() - started_ms >= timeout_ms) {
      ESP_LOGW(TAG, "Direct compositor worker did not stop within %ums", static_cast<unsigned>(timeout_ms));
      return false;
    }
    vTaskDelay(1);
  }
  this->reset_direct_frame_cache_();
  return true;
}

bool KenBurnsController::ensure_transition_buffers_(const lv_image_dsc_t *incoming_source) {
  int width = 0;
  int height = 0;
  lv_lock();
  if (this->obj_ != nullptr && lv_obj_is_valid(this->obj_)) {
    lv_display_t *display = lv_obj_get_display(this->obj_);
    if (display != nullptr) {
      width = lv_display_get_horizontal_resolution(display);
      height = lv_display_get_vertical_resolution(display);
    }
  }
  lv_unlock();
  if (width <= 0 || height <= 0)
    return false;

  constexpr size_t ALIGNMENT = 64;
  constexpr size_t BYTES_PER_PIXEL = 3;
  constexpr size_t BLACK_BAND_ROWS = 64;
  const size_t required_size = static_cast<size_t>(width) * height * BYTES_PER_PIXEL;
  const size_t black_band_size = static_cast<size_t>(width) * BLACK_BAND_ROWS * 3U;
  const bool black_band_ready = !this->fade_through_black_ ||
                                (this->transition_old_frame_ != nullptr &&
                                 this->transition_old_frame_size_ == black_band_size);
  if (this->transition_new_frame_ != nullptr && this->transition_frame_size_ == required_size && black_band_ready) {
    return true;
  }

  this->release_transition_buffers_();
  const uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT;

  // DSI keeps the final old image in a pinned framebuffer for the whole
  // transition, so its decoded RGB565 source is no longer read. Reuse that
  // larger allocation as the RGB888 workspace instead of allocating another
  // 1.92 MB block while scanout is active. Apart from avoiding fragmentation,
  // this removes the PSRAM allocation burst that can starve the DSI FIFO.
  const lv_image_dsc_t &old_source = this->direct_source_;
  const bool reusable_old_source = old_source.data != nullptr && old_source.data != incoming_source->data &&
                                   old_source.data_size >= required_size &&
                                   esp_ptr_external_ram(old_source.data) &&
                                   (reinterpret_cast<uintptr_t>(old_source.data) % ALIGNMENT) == 0;
  if (reusable_old_source) {
    this->transition_new_frame_ = const_cast<uint8_t *>(old_source.data);
    this->transition_new_frame_owned_ = false;
    ESP_LOGD(TAG, "Reusing %u-byte old image source as RGB888 transition workspace",
             static_cast<unsigned>(old_source.data_size));
  } else {
    this->transition_new_frame_ =
        static_cast<uint8_t *>(heap_caps_aligned_alloc(ALIGNMENT, required_size, caps));
    this->transition_new_frame_owned_ = this->transition_new_frame_ != nullptr;
  }
  if (this->transition_new_frame_ == nullptr) {
    ESP_LOGW(TAG, "Unable to allocate RGB888 transition frame (%u bytes, largest PSRAM block=%u)",
             static_cast<unsigned>(required_size),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
    this->release_transition_buffers_();
    return false;
  }
  this->transition_frame_size_ = required_size;
  if (this->transition_new_frame_owned_) {
    ESP_LOGI(TAG, "RGB888 transition frame allocated: total=%u largest_free=%u",
             static_cast<unsigned>(required_size),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
  }

  if (this->fade_through_black_) {
    const size_t reusable_tail_size = old_source.data_size > required_size ? old_source.data_size - required_size : 0;
    if (!this->transition_new_frame_owned_ && reusable_tail_size >= black_band_size) {
      this->transition_old_frame_ = this->transition_new_frame_ + required_size;
      this->transition_old_frame_owned_ = false;
    } else {
      this->transition_old_frame_ =
          static_cast<uint8_t *>(heap_caps_aligned_alloc(ALIGNMENT, black_band_size, caps));
      this->transition_old_frame_owned_ = this->transition_old_frame_ != nullptr;
    }
    if (this->transition_old_frame_ == nullptr) {
      ESP_LOGW(TAG, "Unable to allocate %u-byte fade-to-black band", static_cast<unsigned>(black_band_size));
      this->release_transition_buffers_();
      return false;
    }
    memset(this->transition_old_frame_, 0, black_band_size);
    esp_cache_msync(this->transition_old_frame_, black_band_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    this->transition_old_frame_size_ = black_band_size;
  }
  return true;
}

void KenBurnsController::release_transition_buffers_() {
  if (this->transition_old_frame_ != nullptr && this->transition_old_frame_owned_)
    heap_caps_free(this->transition_old_frame_);
  if (this->transition_new_frame_ != nullptr && this->transition_new_frame_owned_)
    heap_caps_free(this->transition_new_frame_);
  this->transition_old_frame_ = nullptr;
  this->transition_new_frame_ = nullptr;
  this->transition_old_frame_size_ = 0;
  this->transition_frame_size_ = 0;
  this->transition_old_frame_owned_ = false;
  this->transition_new_frame_owned_ = false;
}

bool KenBurnsController::perform_direct_transition_(const lv_image_dsc_t *source) {
  if (source == nullptr || source->data == nullptr || this->direct_component_ == nullptr ||
      this->direct_srm_client_ == nullptr || this->direct_blend_client_ == nullptr) {
    return false;
  }

  const bool saved_zooming_in = this->zooming_in_;
  const bool saved_pan_forward = this->pan_forward_;
  const bool saved_phase_complete = this->phase_complete_;
  const float saved_target_x = this->target_x_;
  const float saved_target_y = this->target_y_;
  const auto restore_previous_motion = [&]() {
    this->zooming_in_ = saved_zooming_in;
    this->pan_forward_ = saved_pan_forward;
    this->phase_complete_ = saved_phase_complete;
    this->target_x_ = saved_target_x;
    this->target_y_ = saved_target_y;
  };
  this->zooming_in_ = true;
  this->select_pan_direction_(!saved_pan_forward);
  this->phase_complete_ = false;
  const uint32_t duration_ms = this->transition_duration_ms_.load(std::memory_order_acquire);
  // Prepare the incoming photo at the actual start of its pan. Advancing it
  // by the transition duration made the first post-transition frame jump.
  const uint32_t incoming_phase_ms = 0;
  int new_x = 0;
  int new_y = 0;
  int new_w = 0;
  int new_h = 0;
  bool new_crop_valid =
      this->calculate_direct_crop_(source, incoming_phase_ms, &new_x, &new_y, &new_w, &new_h);
  if (new_crop_valid) {
    new_crop_valid =
        this->direct_component_->direct_resolve_image_crop(source, &new_x, &new_y, &new_w, &new_h);
  }
  if (!new_crop_valid) {
    restore_previous_motion();
    this->release_transition_buffers_();
    return false;
  }
  if (!this->ensure_transition_buffers_(source)) {
    restore_previous_motion();
    return false;
  }

  lvgl_esphome_dsi_mark_stress("gallery-transition-prepare", 1200);
  const int64_t prepare_started_us = esp_timer_get_time();
  // The final old image is already being scanned out by DSI. Keep that
  // framebuffer pinned as the immutable transition background and prepare
  // the incoming image once. Every animation frame then needs one blend only.
  const int64_t stable_wait_started_us = esp_timer_get_time();
  const uint8_t *visible_old_frame = this->direct_component_->direct_get_stable_presented_frame(80);
  const uint32_t stable_wait_us = static_cast<uint32_t>(esp_timer_get_time() - stable_wait_started_us);
  const int64_t render_started_us = esp_timer_get_time();
  const bool prepared = visible_old_frame != nullptr &&
                        this->direct_component_->direct_render_image_crop_rgb888(
                            source, new_x, new_y, new_w, new_h, this->transition_new_frame_,
                            this->transition_frame_size_, this->direct_srm_client_);
  const uint32_t render_us = static_cast<uint32_t>(esp_timer_get_time() - render_started_us);
  if (!prepared) {
    ESP_LOGW(TAG, "Unable to prepare direct transition frames");
    restore_previous_motion();
    this->release_transition_buffers_();
    return false;
  }
  const uint32_t prepare_us = static_cast<uint32_t>(esp_timer_get_time() - prepare_started_us);

  const uint32_t started_ms = millis();
  const int64_t blend_started_us = esp_timer_get_time();
  uint32_t frames = 0;
  uint64_t blend_total_us = 0;
  uint32_t blend_max_us = 0;
  const uint8_t *fade_black_frame = nullptr;
  lvgl_esphome_dsi_mark_stress("gallery-transition-blend", duration_ms + 300);
  while (this->direct_worker_run_.load(std::memory_order_acquire)) {
    const uint32_t frame_started_ms = millis();
    const uint32_t elapsed_ms = frame_started_ms - started_ms;
    const float linear = std::min(1.0f, static_cast<float>(elapsed_ms) / static_cast<float>(duration_ms));
    float transition_progress = linear;
    const uint8_t *background = visible_old_frame;
    bool fade_to_black = false;
    if (this->fade_through_black_) {
      if (linear < 0.5f) {
        transition_progress = linear * 2.0f;
        fade_to_black = true;
      } else {
        if (fade_black_frame == nullptr) {
          if (!this->direct_component_->direct_present_rgb888_solid_crossfade_banded(
                  visible_old_frame, 255, this->transition_old_frame_, this->transition_old_frame_size_,
                  this->direct_blend_client_)) {
            ESP_LOGW(TAG, "Unable to complete fade to black");
            restore_previous_motion();
            this->release_transition_buffers_();
            return false;
          }
          fade_black_frame = this->direct_component_->direct_get_stable_presented_frame(80);
          if (fade_black_frame == nullptr) {
            ESP_LOGW(TAG, "Unable to pin the black transition frame");
            restore_previous_motion();
            this->release_transition_buffers_();
            return false;
          }
        }
        transition_progress = (linear - 0.5f) * 2.0f;
        background = fade_black_frame;
      }
    }
    const float eased = transition_progress * transition_progress * (3.0f - 2.0f * transition_progress);
    const uint8_t opacity = static_cast<uint8_t>(std::lround(eased * 255.0f));
    const int64_t frame_blend_started_us = esp_timer_get_time();
    const bool presented = fade_to_black
                               ? this->direct_component_->direct_present_rgb888_solid_crossfade_banded(
                                     background, opacity, this->transition_old_frame_,
                                     this->transition_old_frame_size_,
                                     this->direct_blend_client_)
                               : this->direct_component_->direct_present_rgb888_crossfade(
                                     background, this->transition_new_frame_, opacity, this->direct_blend_client_);
    if (!presented) {
      ESP_LOGW(TAG, "Direct transition frame rejected at opacity %u", static_cast<unsigned>(opacity));
      restore_previous_motion();
      this->release_transition_buffers_();
      return false;
    }
    const uint32_t frame_blend_us = static_cast<uint32_t>(esp_timer_get_time() - frame_blend_started_us);
    blend_total_us += frame_blend_us;
    blend_max_us = std::max(blend_max_us, frame_blend_us);
    frames++;
    if (linear >= 1.0f)
      break;

    const uint32_t frame_elapsed_ms = millis() - frame_started_ms;
    const uint32_t wait_ms = frame_elapsed_ms < this->frame_interval_ms_
                                 ? this->frame_interval_ms_ - frame_elapsed_ms
                                 : 1;
    // PPA completion uses this task's notification slot. If the final IRQ
    // arrives just before the completion counter is inspected, its give can
    // remain pending and make a notification-based frame delay return
    // immediately. A real delay guarantees that idle and system tasks run
    // between full-screen PPA frames.
    vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(wait_ms)));
  }
  if (!this->direct_worker_run_.load(std::memory_order_acquire)) {
    restore_previous_motion();
    this->release_transition_buffers_();
    return false;
  }

  this->direct_source_ = *source;
  this->reset_direct_frame_cache_();
  // The incoming frame used for the final blend already represents this phase.
  // Resume from the same crop instead of snapping back to the beginning of the
  // pan on the first post-transition frame.
  this->phase_elapsed_ms_ = incoming_phase_ms;
  // The final crossfade frame already contains this exact incoming crop.
  // Remember it so the first regular tick does not perform another 1.92 MB
  // full-screen render immediately after the transition.
  this->last_direct_source_data_ = source->data;
  this->calculate_direct_crop_(source, incoming_phase_ms, &this->last_direct_crop_x_,
                               &this->last_direct_crop_y_, &this->last_direct_crop_width_,
                               &this->last_direct_crop_height_);
  this->zooming_in_ = true;
  this->phase_complete_ = false;
  this->last_loop_ms_ = millis();

  lv_lock();
  if (this->obj_ != nullptr && lv_obj_is_valid(this->obj_)) {
    lv_display_t *display = lv_obj_get_display(this->obj_);
    const bool invalidation_enabled = display != nullptr && lv_display_is_invalidation_enabled(display);
    if (invalidation_enabled)
      lv_display_enable_invalidation(display, false);
    lv_image_set_src(this->obj_, source);
    if (invalidation_enabled)
      lv_display_enable_invalidation(display, true);
  }
  lv_unlock();

  const uint32_t blend_duration_us = static_cast<uint32_t>(esp_timer_get_time() - blend_started_us);
  ESP_LOGI(TAG,
           "transition: prepare=%uus wait=%uus render=%uus frames=%u blend_avg=%uus blend_max=%uus wall=%ums "
           "blend_wall=%uus fade=%s",
           static_cast<unsigned>(prepare_us), static_cast<unsigned>(stable_wait_us),
           static_cast<unsigned>(render_us), static_cast<unsigned>(frames),
           static_cast<unsigned>(blend_total_us / std::max<uint32_t>(1, frames)),
           static_cast<unsigned>(blend_max_us), static_cast<unsigned>(millis() - started_ms),
           static_cast<unsigned>(blend_duration_us), YESNO(this->fade_through_black_));
  // The full-resolution source slots are larger than this workspace. Keeping
  // the workspace between transitions can split PSRAM into blocks that are
  // individually too small for the next JPEG output even when total free
  // memory is sufficient. The last blended frame is already owned by the DSI
  // framebuffer, so release the temporary frame before the caller retires the
  // previous source slot; the adjacent free regions can then coalesce.
  this->release_transition_buffers_();
  return true;
}

void KenBurnsController::direct_worker_trampoline_(void *arg) {
  static_cast<KenBurnsController *>(arg)->direct_worker_();
}

void KenBurnsController::direct_worker_() {
  this->direct_srm_client_ = LvglComponent::register_direct_image_animation_client();
  this->direct_blend_client_ = LvglComponent::register_direct_image_blend_client();
  if (this->direct_srm_client_ == nullptr || this->direct_blend_client_ == nullptr) {
    ESP_LOGE(TAG, "Failed to register task-owned PPA clients (srm=%p blend=%p)", this->direct_srm_client_,
             this->direct_blend_client_);
  } else {
    ESP_LOGI(TAG, "Task-owned PPA SRM/blend clients ready (stack watermark=%u)",
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  }
  bool stack_watermark_logged = false;
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (!this->direct_worker_run_.load())
      continue;

    this->direct_worker_active_.store(true);
    uint32_t previous_ms = millis();
    uint8_t consecutive_frame_failures = 0;
    while (this->direct_worker_run_.load()) {
      if (this->transition_requested_.exchange(false, std::memory_order_acq_rel)) {
        const lv_image_dsc_t *transition_source =
            this->transition_source_.load(std::memory_order_acquire);
        this->transition_active_.store(true, std::memory_order_release);
        const bool transitioned = this->perform_direct_transition_(transition_source);
        this->transition_active_.store(false, std::memory_order_release);
        this->transition_source_.store(nullptr, std::memory_order_release);
        this->transition_failed_.store(!transitioned, std::memory_order_release);
        if (!transitioned)
          ESP_LOGW(TAG, "Direct image transition failed; retaining the current image");
        previous_ms = millis();
        consecutive_frame_failures = 0;
        if (!this->direct_worker_run_.load())
          break;
        continue;
      }

      const uint32_t frame_started_ms = millis();
      const uint32_t delta = frame_started_ms - previous_ms;
      previous_ms = frame_started_ms;
      if (!this->phase_complete_) {
        this->phase_elapsed_ms_ = std::min(
            this->phase_duration_ms_, this->phase_elapsed_ms_ + std::min(delta, this->frame_interval_ms_ * 3U));
        this->phase_complete_ = this->phase_elapsed_ms_ >= this->phase_duration_ms_;
      }

      if (!this->render_direct_frame_(&this->direct_source_, this->direct_component_, this->phase_elapsed_ms_)) {
        consecutive_frame_failures++;
        if (consecutive_frame_failures >= 5) {
          this->direct_worker_run_.store(false);
          this->direct_worker_failed_.store(true);
          break;
        }
        ESP_LOGW(TAG, "Direct compositor frame rejected (%u/5); retaining the previous frame",
                 static_cast<unsigned>(consecutive_frame_failures));
        vTaskDelay(1);
        continue;
      }
      consecutive_frame_failures = 0;
      if (!stack_watermark_logged) {
        ESP_LOGD(TAG, "First direct frame complete (stack watermark=%u)",
                 static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
        stack_watermark_logged = true;
      }

      if (this->phase_complete_) {
        // Keep the final frame queued and stop consuming PSRAM bandwidth until
        // a new source, resume request or explicit transition wakes us.
        this->direct_worker_active_.store(false);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (!this->direct_worker_run_.load())
          break;
        this->direct_worker_active_.store(true);
        previous_ms = millis();
        continue;
      }

      const uint32_t frame_elapsed_ms = millis() - frame_started_ms;
      // Never spin continuously when a frame exceeds its budget. The ESPHome
      // loop task shares this core and must get a scheduling point between
      // frames for networking, component lifecycle and the watchdog.
      const uint32_t wait_ms = frame_elapsed_ms < this->frame_interval_ms_
                                   ? this->frame_interval_ms_ - frame_elapsed_ms
                                   : 1;
      vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(wait_ms)));
    }
    this->direct_worker_active_.store(false);
  }
}
#endif

bool KenBurnsController::update_transform_(uint32_t elapsed_ms) {
  if (this->obj_ == nullptr || this->phase_duration_ms_ == 0)
    return false;

  lv_lock();
  if (!lv_obj_is_valid(this->obj_)) {
    lv_unlock();
    return false;
  }

  auto *parent = lv_obj_get_parent(this->obj_);
  const int32_t viewport_width = parent != nullptr ? lv_obj_get_content_width(parent) : lv_obj_get_width(this->obj_);
  const int32_t viewport_height = parent != nullptr ? lv_obj_get_content_height(parent) : lv_obj_get_height(this->obj_);
  const int32_t source_width = lv_image_get_src_width(this->obj_);
  const int32_t source_height = lv_image_get_src_height(this->obj_);
  if (viewport_width <= 0 || viewport_height <= 0 || source_width <= 0 || source_height <= 0) {
    lv_unlock();
    return false;
  }

  const float linear = std::clamp(static_cast<float>(elapsed_ms) / static_cast<float>(this->phase_duration_ms_),
                                  0.0f, 1.0f);
  const float progress = linear * linear * (3.0f - 2.0f * linear);
  const float directed = this->zooming_in_ ? progress : 1.0f - progress;

  const uint32_t cover_scale = std::max(
      (static_cast<uint32_t>(viewport_width) * LV_SCALE_NONE + source_width - 1) / source_width,
      (static_cast<uint32_t>(viewport_height) * LV_SCALE_NONE + source_height - 1) / source_height);
  const uint32_t start_scale = cover_scale * this->zoom_start_ / LV_SCALE_NONE;
  const uint32_t end_scale = cover_scale * this->zoom_end_ / LV_SCALE_NONE;
  const uint32_t scale = static_cast<uint32_t>(std::lround(
      static_cast<float>(start_scale) + static_cast<float>(end_scale - start_scale) * directed));

  const int32_t scaled_width = static_cast<int32_t>((static_cast<int64_t>(source_width) * scale) / LV_SCALE_NONE);
  const int32_t scaled_height = static_cast<int32_t>((static_cast<int64_t>(source_height) * scale) / LV_SCALE_NONE);
  const float pan_progress = directed;
  const float offset_x =
      this->target_x_ * static_cast<float>(std::max<int32_t>(0, scaled_width - viewport_width)) * 0.5f * pan_progress;
  const float offset_y =
      this->target_y_ * static_cast<float>(std::max<int32_t>(0, scaled_height - viewport_height)) * 0.5f * pan_progress;
  const int32_t x = (viewport_width - scaled_width) / 2 + static_cast<int32_t>(std::lround(offset_x));
  const int32_t y = (viewport_height - scaled_height) / 2 + static_cast<int32_t>(std::lround(offset_y));

  const int32_t base_x = (viewport_width - source_width) / 2;
  const int32_t base_y = (viewport_height - source_height) / 2;
  const bool geometry_changed = source_width != this->geometry_source_width_ ||
                                source_height != this->geometry_source_height_ ||
                                viewport_width != this->geometry_viewport_width_ ||
                                viewport_height != this->geometry_viewport_height_;

  int32_t pivot_x = source_width / 2;
  int32_t pivot_y = source_height / 2;
  const int32_t scale_delta = static_cast<int32_t>(scale) - LV_SCALE_NONE;
  if (scale_delta != 0) {
    pivot_x = static_cast<int32_t>((static_cast<int64_t>(base_x - x) * LV_SCALE_NONE) / scale_delta);
    pivot_y = static_cast<int32_t>((static_cast<int64_t>(base_y - y) * LV_SCALE_NONE) / scale_delta);
  }

  lv_display_t *display = lv_obj_get_display(this->obj_);
  const bool invalidation_enabled = lv_display_is_invalidation_enabled(display);
  if (invalidation_enabled)
    lv_display_enable_invalidation(display, false);

  if (geometry_changed) {
    lv_obj_set_pos(this->obj_, base_x, base_y);
    this->geometry_source_width_ = source_width;
    this->geometry_source_height_ = source_height;
    this->geometry_viewport_width_ = viewport_width;
    this->geometry_viewport_height_ = viewport_height;
  }
  lv_image_set_pivot(this->obj_, pivot_x, pivot_y);
  lv_image_set_scale(this->obj_, scale);

  if (invalidation_enabled) {
    lv_display_enable_invalidation(display, true);
    lv_obj_invalidate(parent != nullptr ? parent : this->obj_);
  }
  lv_unlock();
  return true;
}

void KenBurnsController::choose_target_() {
  this->select_pan_direction_(this->pan_forward_);
}

void KenBurnsController::select_pan_direction_(bool forward) {
  this->pan_forward_ = forward;
  const float direction = forward ? 1.0f : -1.0f;
  this->target_x_ = direction * this->pan_limit_;
  this->target_y_ = direction * this->pan_limit_;
}

}  // namespace esphome::lvgl
