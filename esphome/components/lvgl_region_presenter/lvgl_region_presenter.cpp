#include "lvgl_region_presenter.h"

#include <algorithm>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#ifdef USE_ESP32_VARIANT_ESP32P4
#include "esp_cache.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_timer.h"
#endif

namespace esphome::lvgl_region_presenter {

static const char *const TAG = "lvgl_region_presenter";

void LvglRegionPresenter::setup() {
#ifdef USE_ESP32_VARIANT_ESP32P4
  ppa_client_config_t config{};
  config.max_pending_trans_num = 1;
  config.data_burst_length = PPA_DATA_BURST_LENGTH_128;

  config.oper_type = PPA_OPERATION_SRM;
  esp_err_t error = ppa_register_client(&config, &this->srm_client_);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "Unable to register PPA SRM client: %s", esp_err_to_name(error));
    this->mark_failed();
    return;
  }

  config.oper_type = PPA_OPERATION_BLEND;
  error = ppa_register_client(&config, &this->blend_client_);
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "Unable to register PPA blend client: %s", esp_err_to_name(error));
    ppa_unregister_client(this->srm_client_);
    this->srm_client_ = nullptr;
    this->mark_failed();
    return;
  }

  this->queue_ =
      xQueueCreateStatic(QUEUE_LENGTH, sizeof(RegionRequest), this->queue_data_.data(), &this->queue_storage_);
  this->barrier_ = xSemaphoreCreateBinaryStatic(&this->barrier_storage_);
  this->state_lock_ = xSemaphoreCreateMutexStatic(&this->state_lock_storage_);
  if (this->queue_ == nullptr || this->barrier_ == nullptr || this->state_lock_ == nullptr || !this->start_task_()) {
    ESP_LOGE(TAG, "Unable to allocate the regional presentation worker");
    this->mark_failed();
  }
#else
  ESP_LOGE(TAG, "Regional presentation is unavailable on this platform");
  this->mark_failed();
#endif
}

void LvglRegionPresenter::on_shutdown() {
  this->end(500);
#ifdef USE_ESP32_VARIANT_ESP32P4
  if (this->queue_ != nullptr && this->task_handle_ != nullptr) {
    RegionRequest request{};
    request.operation = Operation::STOP;
    this->enqueue_(request, pdMS_TO_TICKS(20));
    const uint32_t started = millis();
    while (this->task_handle_ != nullptr && millis() - started < 100)
      delay(1);
    if (this->task_handle_ != nullptr) {
      vTaskDelete(this->task_handle_);
      this->task_handle_ = nullptr;
    }
  }
  if (this->blend_client_ != nullptr) {
    ppa_unregister_client(this->blend_client_);
    this->blend_client_ = nullptr;
  }
  if (this->srm_client_ != nullptr) {
    ppa_unregister_client(this->srm_client_);
    this->srm_client_ = nullptr;
  }
  if (this->task_stack_ != nullptr) {
    heap_caps_free(this->task_stack_);
    this->task_stack_ = nullptr;
  }
#endif
}

void LvglRegionPresenter::dump_config() {
  ESP_LOGCONFIG(TAG, "LVGL Region Presenter:");
  ESP_LOGCONFIG(TAG, "  Frame interval: %ums", static_cast<unsigned>(this->frame_interval_ms_));
  ESP_LOGCONFIG(TAG, "  Backend: %s", this->is_failed() ? "unavailable" : "PPA");
}

#ifdef USE_ESP32_VARIANT_ESP32P4
bool LvglRegionPresenter::start_task_() {
  this->task_stack_ =
      static_cast<StackType_t *>(heap_caps_aligned_alloc(16, TASK_STACK_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (this->task_stack_ == nullptr)
    return false;

#if CONFIG_FREERTOS_UNICORE
  constexpr BaseType_t task_core = tskNO_AFFINITY;
#else
  const BaseType_t task_core = xPortGetCoreID() == 0 ? 1 : 0;
#endif
  this->task_handle_ =
      xTaskCreateStaticPinnedToCore(task_trampoline_, "lvgl_region", TASK_STACK_BYTES / sizeof(StackType_t), this, 1,
                                    this->task_stack_, &this->task_storage_, task_core);
  return this->task_handle_ != nullptr;
}

void LvglRegionPresenter::task_trampoline_(void *arg) { static_cast<LvglRegionPresenter *>(arg)->task_(); }
#endif

bool LvglRegionPresenter::begin(uint32_t timeout_ms) {
#ifdef USE_ESP32_VARIANT_ESP32P4
  if (this->session_active_.load(std::memory_order_acquire))
    return true;
  if (this->is_failed() || this->lvgl_component_ == nullptr || this->task_handle_ == nullptr) {
    return false;
  }
  if (xSemaphoreTake(this->state_lock_, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
    return false;

  bool success = false;
  if (this->lvgl_component_->begin_frame_buffer_presentation(timeout_ms)) {
    display::FrameBufferView active{};
    if (this->lvgl_component_->get_active_presentation_frame(&active, BufferReader::DMA) &&
        active.index < MAX_FRAME_BUFFERS && active.bitness == display::COLOR_BITNESS_888 &&
        active.stride >= active.width * 3U) {
      this->reset_state_();
      this->buffer_generations_[active.index] = this->base_generation_;
      this->session_active_.store(true, std::memory_order_release);
      this->accepting_requests_.store(true, std::memory_order_release);
      success = true;
    } else {
      this->lvgl_component_->end_frame_buffer_presentation(timeout_ms);
    }
  }
  xSemaphoreGive(this->state_lock_);
  return success;
#else
  (void) timeout_ms;
  return false;
#endif
}

bool LvglRegionPresenter::end(uint32_t timeout_ms) {
#ifdef USE_ESP32_VARIANT_ESP32P4
  if (!this->session_active_.load(std::memory_order_acquire))
    return true;
  this->accepting_requests_.store(false, std::memory_order_release);
  if (!this->flush(timeout_ms)) {
    this->accepting_requests_.store(true, std::memory_order_release);
    return false;
  }
  if (xSemaphoreTake(this->state_lock_, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
    this->accepting_requests_.store(true, std::memory_order_release);
    return false;
  }
  const bool success =
      this->lvgl_component_ != nullptr && this->lvgl_component_->end_frame_buffer_presentation(timeout_ms);
  if (success) {
    this->session_active_.store(false, std::memory_order_release);
    this->reset_state_();
  } else {
    this->accepting_requests_.store(true, std::memory_order_release);
  }
  xSemaphoreGive(this->state_lock_);
  return success;
#else
  (void) timeout_ms;
  return true;
#endif
}

RegionSubmitResult LvglRegionPresenter::submit_rgb888(const uint8_t *source, size_t source_stride, int source_width,
                                                      int source_height, int source_x, int source_y,
                                                      display::ColorOrder source_order, BufferWriter source_writer,
                                                      int x, int y, int width, int height,
                                                      RegionReadyCallback ready_callback, void *ready_arg) {
#ifdef USE_ESP32_VARIANT_ESP32P4
  if (!this->accepting_requests_.load(std::memory_order_acquire) || source == nullptr || ready_callback == nullptr ||
      source_width <= 0 || source_height <= 0 || source_stride < static_cast<size_t>(source_width) * 3U ||
      source_x < 0 || source_y < 0 || width <= 0 || height <= 0 || source_x + width > source_width ||
      source_y + height > source_height || x < 0 || y < 0 || source_order == display::COLOR_ORDER_GRB) {
    return RegionSubmitResult::REJECTED;
  }
  RegionRequest request{};
  request.operation = Operation::COPY_RGB888;
  request.source = source;
  request.source_stride = source_stride;
  request.source_width = source_width;
  request.source_height = source_height;
  request.source_x = source_x;
  request.source_y = source_y;
  request.source_order = source_order;
  request.source_writer = source_writer;
  request.x = x;
  request.y = y;
  request.width = width;
  request.height = height;
  request.ready_callback = ready_callback;
  request.ready_arg = ready_arg;
  return this->enqueue_(request) ? RegionSubmitResult::SUBMITTED : RegionSubmitResult::BUSY;
#else
  (void) source;
  (void) source_stride;
  (void) source_width;
  (void) source_height;
  (void) source_x;
  (void) source_y;
  (void) source_order;
  (void) source_writer;
  (void) x;
  (void) y;
  (void) width;
  (void) height;
  (void) ready_callback;
  (void) ready_arg;
  return RegionSubmitResult::REJECTED;
#endif
}

RegionSubmitResult LvglRegionPresenter::submit_argb8888(
    const uint8_t *background, size_t background_stride, display::ColorOrder background_order,
    BufferWriter background_writer, const uint8_t *foreground, size_t foreground_stride, int foreground_width,
    int foreground_height, int foreground_x, int foreground_y, BufferWriter foreground_writer, int x, int y, int width,
    int height, RegionReadyCallback ready_callback, void *ready_arg) {
#ifdef USE_ESP32_VARIANT_ESP32P4
  if (!this->accepting_requests_.load(std::memory_order_acquire) || background == nullptr || foreground == nullptr ||
      ready_callback == nullptr || width <= 0 || height <= 0 || background_stride < static_cast<size_t>(width) * 3U ||
      foreground_width <= 0 || foreground_height <= 0 ||
      foreground_stride < static_cast<size_t>(foreground_width) * 4U || foreground_x < 0 || foreground_y < 0 ||
      foreground_x + width > foreground_width || foreground_y + height > foreground_height || x < 0 || y < 0 ||
      background_order == display::COLOR_ORDER_GRB) {
    return RegionSubmitResult::REJECTED;
  }
  RegionRequest request{};
  request.operation = Operation::BLEND_ARGB8888;
  request.source = foreground;
  request.source_stride = foreground_stride;
  request.source_width = foreground_width;
  request.source_height = foreground_height;
  request.source_x = foreground_x;
  request.source_y = foreground_y;
  request.source_writer = foreground_writer;
  request.background = background;
  request.background_stride = background_stride;
  request.background_order = background_order;
  request.background_writer = background_writer;
  request.x = x;
  request.y = y;
  request.width = width;
  request.height = height;
  request.ready_callback = ready_callback;
  request.ready_arg = ready_arg;
  return this->enqueue_(request) ? RegionSubmitResult::SUBMITTED : RegionSubmitResult::BUSY;
#else
  (void) background;
  (void) background_stride;
  (void) background_order;
  (void) background_writer;
  (void) foreground;
  (void) foreground_stride;
  (void) foreground_width;
  (void) foreground_height;
  (void) foreground_x;
  (void) foreground_y;
  (void) foreground_writer;
  (void) x;
  (void) y;
  (void) width;
  (void) height;
  (void) ready_callback;
  (void) ready_arg;
  return RegionSubmitResult::REJECTED;
#endif
}

bool LvglRegionPresenter::flush(uint32_t timeout_ms) {
#ifdef USE_ESP32_VARIANT_ESP32P4
  if (this->queue_ == nullptr || this->task_handle_ == nullptr)
    return false;
  while (xSemaphoreTake(this->barrier_, 0) == pdTRUE) {
  }
  RegionRequest request{};
  request.operation = Operation::BARRIER;
  request.ready_callback = [](void *arg) {
    auto semaphore = static_cast<SemaphoreHandle_t>(arg);
    if (semaphore != nullptr)
      xSemaphoreGive(semaphore);
  };
  request.ready_arg = this->barrier_;
  const TickType_t timeout_ticks = std::max<TickType_t>(1, pdMS_TO_TICKS(timeout_ms));
  return this->enqueue_(request, timeout_ticks) && xSemaphoreTake(this->barrier_, timeout_ticks) == pdTRUE;
#else
  (void) timeout_ms;
  return true;
#endif
}

#ifdef USE_ESP32_VARIANT_ESP32P4
bool LvglRegionPresenter::enqueue_(const RegionRequest &request, TickType_t timeout_ticks) {
  return this->queue_ != nullptr && xQueueSend(this->queue_, &request, timeout_ticks) == pdTRUE;
}

void LvglRegionPresenter::complete_(const RegionRequest &request) const {
  if (request.ready_callback != nullptr)
    request.ready_callback(request.ready_arg);
}

bool LvglRegionPresenter::request_matches_slot_(const RegionRequest &request, const RegionSlot &slot) const {
  return slot.valid && request.x == slot.x && request.y == slot.y && request.width == slot.width &&
         request.height == slot.height;
}

LvglRegionPresenter::RegionSlot *LvglRegionPresenter::find_slot_(int x, int y, int width, int height, bool create) {
  RegionSlot *free_slot = nullptr;
  for (auto &slot : this->slots_) {
    if (slot.valid && slot.x == x && slot.y == y && slot.width == width && slot.height == height)
      return &slot;
    if (!slot.valid && free_slot == nullptr)
      free_slot = &slot;
  }
  return create ? free_slot : nullptr;
}

void LvglRegionPresenter::reset_state_() {
  this->base_generation_++;
  if (this->base_generation_ == 0)
    this->base_generation_++;
  this->slots_.fill({});
  this->buffer_generations_.fill(0);
}

bool LvglRegionPresenter::sync_source_(const uint8_t *source, size_t size, BufferWriter writer) const {
  if (source == nullptr || size == 0 || writer == BufferWriter::DMA || !esp_ptr_external_ram(source))
    return source != nullptr && size != 0;
  const esp_err_t error = esp_cache_msync(const_cast<uint8_t *>(source), size,
                                          ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
  if (error != ESP_OK)
    ESP_LOGW(TAG, "PPA source ownership transfer failed: %s", esp_err_to_name(error));
  return error == ESP_OK;
}

bool LvglRegionPresenter::ppa_copy_(const uint8_t *source, size_t source_stride, int source_width, int source_height,
                                    int source_x, int source_y, display::ColorOrder source_order,
                                    BufferWriter source_writer, int width, int height,
                                    display::FrameBufferLease *target, int target_x, int target_y, bool sync_source) {
  if (source == nullptr || target == nullptr || !*target || this->srm_client_ == nullptr || source_width <= 0 ||
      source_height <= 0 || source_stride < static_cast<size_t>(source_width) * 3U || source_x < 0 || source_y < 0 ||
      width <= 0 || height <= 0 || source_x + width > source_width || source_y + height > source_height ||
      target_x < 0 || target_y < 0 || target_x + width > static_cast<int>(target->width) ||
      target_y + height > static_cast<int>(target->height) || target->bitness != display::COLOR_BITNESS_888 ||
      target->stride % 3U != 0 || source_order == display::COLOR_ORDER_GRB ||
      target->color_order == display::COLOR_ORDER_GRB) {
    return false;
  }
  if (sync_source && !this->sync_source_(source, source_stride * static_cast<size_t>(source_height), source_writer)) {
    return false;
  }

  ppa_srm_oper_config_t config{};
  config.in.buffer = source;
  config.in.pic_w = source_stride / 3U;
  config.in.pic_h = source_height;
  config.in.block_w = width;
  config.in.block_h = height;
  config.in.block_offset_x = source_x;
  config.in.block_offset_y = source_y;
  config.in.srm_cm = PPA_SRM_COLOR_MODE_RGB888;
  config.out.buffer = target->data;
  config.out.buffer_size = target->size;
  config.out.pic_w = target->stride / 3U;
  config.out.pic_h = target->height;
  config.out.block_offset_x = target_x;
  config.out.block_offset_y = target_y;
  config.out.srm_cm = PPA_SRM_COLOR_MODE_RGB888;
  config.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
  config.scale_x = 1.0f;
  config.scale_y = 1.0f;
  config.rgb_swap = source_order != target->color_order;
  config.alpha_update_mode = PPA_ALPHA_NO_CHANGE;
  config.mode = PPA_TRANS_MODE_BLOCKING;
  return ppa_do_scale_rotate_mirror(this->srm_client_, &config) == ESP_OK;
}

bool LvglRegionPresenter::ppa_blend_(const RegionRequest &request, display::FrameBufferLease *target) {
  if (target == nullptr || !*target || this->blend_client_ == nullptr ||
      !this->sync_source_(request.background, request.background_stride * static_cast<size_t>(request.height),
                          request.background_writer) ||
      !this->sync_source_(request.source, request.source_stride * static_cast<size_t>(request.source_height),
                          request.source_writer)) {
    return false;
  }

  ppa_blend_oper_config_t config{};
  config.in_bg.buffer = request.background;
  config.in_bg.pic_w = request.background_stride / 3U;
  config.in_bg.pic_h = request.height;
  config.in_bg.block_w = request.width;
  config.in_bg.block_h = request.height;
  config.in_bg.blend_cm = PPA_BLEND_COLOR_MODE_RGB888;
  config.in_fg.buffer = request.source;
  config.in_fg.pic_w = request.source_stride / 4U;
  config.in_fg.pic_h = request.source_height;
  config.in_fg.block_w = request.width;
  config.in_fg.block_h = request.height;
  config.in_fg.block_offset_x = request.source_x;
  config.in_fg.block_offset_y = request.source_y;
  config.in_fg.blend_cm = PPA_BLEND_COLOR_MODE_ARGB8888;
  config.out.buffer = target->data;
  config.out.buffer_size = target->size;
  config.out.pic_w = target->stride / 3U;
  config.out.pic_h = target->height;
  config.out.block_offset_x = request.x;
  config.out.block_offset_y = request.y;
  config.out.blend_cm = PPA_BLEND_COLOR_MODE_RGB888;
  config.bg_rgb_swap = request.background_order != target->color_order;
  config.fg_rgb_swap = target->color_order == display::COLOR_ORDER_RGB;
  config.bg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
  config.bg_alpha_fix_val = 0xFF;
  config.fg_alpha_update_mode = PPA_ALPHA_NO_CHANGE;
  config.mode = PPA_TRANS_MODE_BLOCKING;
  return ppa_do_blend(this->blend_client_, &config) == ESP_OK;
}

bool LvglRegionPresenter::prepare_target_(const display::FrameBufferView &active, display::FrameBufferLease *target) {
  if (this->lvgl_component_ == nullptr ||
      !this->lvgl_component_->acquire_presentation_frame(target, BufferWriter::DMA, 50) ||
      target->index >= MAX_FRAME_BUFFERS || target->width != active.width || target->height != active.height ||
      target->stride != active.stride || target->bitness != active.bitness ||
      target->color_order != active.color_order) {
    if (target != nullptr && *target)
      this->lvgl_component_->release_presentation_frame(target);
    return false;
  }

  if (this->buffer_generations_[target->index] == this->base_generation_)
    return true;
  if (!this->ppa_copy_(active.data, active.stride, active.stride / 3U, active.height, 0, 0, active.color_order,
                       active.writer, active.width, active.height, target, 0, 0, false)) {
    this->lvgl_component_->release_presentation_frame(target);
    return false;
  }
  this->buffer_generations_[target->index] = this->base_generation_;
  return true;
}

bool LvglRegionPresenter::compose_batch_(const RegionRequest *requests, size_t request_count,
                                         const display::FrameBufferView &active, display::FrameBufferLease *target) {
  for (const auto &slot : this->slots_) {
    if (!slot.valid)
      continue;
    bool replaced = false;
    for (size_t i = 0; i < request_count; i++) {
      if (requests[i].operation != Operation::BARRIER && requests[i].operation != Operation::STOP &&
          this->request_matches_slot_(requests[i], slot)) {
        replaced = true;
        break;
      }
    }
    if (!replaced &&
        !this->ppa_copy_(active.data, active.stride, active.stride / 3U, active.height, slot.x, slot.y,
                         active.color_order, active.writer, slot.width, slot.height, target, slot.x, slot.y, false)) {
      return false;
    }
  }

  for (size_t i = 0; i < request_count; i++) {
    const auto &request = requests[i];
    if (request.operation == Operation::COPY_RGB888) {
      if (!this->ppa_copy_(request.source, request.source_stride, request.source_width, request.source_height,
                           request.source_x, request.source_y, request.source_order, request.source_writer,
                           request.width, request.height, target, request.x, request.y, true)) {
        return false;
      }
    } else if (request.operation == Operation::BLEND_ARGB8888) {
      if (!this->ppa_blend_(request, target))
        return false;
    }
  }
  return true;
}

bool LvglRegionPresenter::process_batch_(RegionRequest *requests, size_t request_count) {
  if (!this->session_active_.load(std::memory_order_acquire) || this->lvgl_component_ == nullptr)
    return false;

  bool has_render_request = false;
  for (size_t i = 0; i < request_count; i++) {
    has_render_request |=
        requests[i].operation == Operation::COPY_RGB888 || requests[i].operation == Operation::BLEND_ARGB8888;
  }
  if (!has_render_request)
    return true;

  display::FrameBufferView active{};
  if (!this->lvgl_component_->get_active_presentation_frame(&active, BufferReader::DMA))
    return false;
  display::FrameBufferLease target{};
  if (!this->prepare_target_(active, &target))
    return false;
  if (!this->compose_batch_(requests, request_count, active, &target)) {
    this->lvgl_component_->release_presentation_frame(&target);
    return false;
  }
  if (!this->lvgl_component_->present_presentation_frame(&target, 50)) {
    if (target)
      this->lvgl_component_->release_presentation_frame(&target);
    return false;
  }

  for (size_t i = 0; i < request_count; i++) {
    const auto &request = requests[i];
    if (request.operation == Operation::COPY_RGB888 || request.operation == Operation::BLEND_ARGB8888) {
      if (auto *slot = this->find_slot_(request.x, request.y, request.width, request.height, true); slot != nullptr) {
        *slot = {
            .x = request.x,
            .y = request.y,
            .width = request.width,
            .height = request.height,
            .valid = true,
        };
      }
    }
  }
  return true;
}

void LvglRegionPresenter::task_() {
  RegionRequest requests[QUEUE_LENGTH]{};
  RegionRequest incoming{};
  int64_t next_present_us = 0;
  bool stop_requested = false;
  while (xQueueReceive(this->queue_, &incoming, portMAX_DELAY) == pdTRUE) {
    if (incoming.operation == Operation::STOP)
      break;

    std::array<RegionRequest, QUEUE_LENGTH * 2> deferred{};
    size_t deferred_count = 0;
    size_t request_count = 0;
    auto defer_completion = [&](const RegionRequest &request) {
      if (request.ready_callback != nullptr && deferred_count < deferred.size())
        deferred[deferred_count++] = request;
    };
    auto add_latest = [&](const RegionRequest &request) {
      if (request.operation == Operation::BARRIER) {
        if (request_count < QUEUE_LENGTH)
          requests[request_count++] = request;
        else
          defer_completion(request);
        return;
      }
      for (size_t i = 0; i < request_count; i++) {
        if (requests[i].operation == Operation::BARRIER || requests[i].x != request.x || requests[i].y != request.y ||
            requests[i].width != request.width || requests[i].height != request.height) {
          continue;
        }
        defer_completion(requests[i]);
        requests[i] = request;
        return;
      }
      if (request_count < QUEUE_LENGTH)
        requests[request_count++] = request;
      else
        defer_completion(request);
    };

    add_latest(incoming);
    if (next_present_us != 0) {
      while (true) {
        const int64_t remaining_us = next_present_us - esp_timer_get_time();
        if (remaining_us <= 0)
          break;
        const TickType_t wait_ticks = pdMS_TO_TICKS(std::max<int64_t>(1, (remaining_us + 999) / 1000));
        if (xQueueReceive(this->queue_, &incoming, wait_ticks) != pdTRUE)
          break;
        if (incoming.operation == Operation::STOP) {
          stop_requested = true;
          break;
        }
        add_latest(incoming);
      }
    }
    while (xQueueReceive(this->queue_, &incoming, 0) == pdTRUE) {
      if (incoming.operation == Operation::STOP) {
        stop_requested = true;
        break;
      }
      add_latest(incoming);
    }

    next_present_us = esp_timer_get_time() + static_cast<int64_t>(this->frame_interval_ms_) * 1000;
    if (!this->process_batch_(requests, request_count))
      ESP_LOGW(TAG, "Unable to present regional update batch");
    for (size_t i = 0; i < deferred_count; i++)
      this->complete_(deferred[i]);
    for (size_t i = 0; i < request_count; i++)
      this->complete_(requests[i]);
    if (stop_requested)
      break;
  }
  this->task_handle_ = nullptr;
  vTaskDelete(nullptr);
}
#endif

}  // namespace esphome::lvgl_region_presenter
