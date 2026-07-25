#include "dma2d_m2m_copy.h"

#include "esphome/core/defines.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cstring>

#if defined(USE_ESP32) && defined(CONFIG_IDF_TARGET_ESP32P4)
#include "esp_attr.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_private/dma2d.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "hal/dma2d_types.h"
#include "soc/dma2d_channel.h"
#endif

namespace esphome::lvgl {

#if defined(USE_ESP32) && defined(CONFIG_IDF_TARGET_ESP32P4)
namespace {

static const char *const TAG = "lvgl.dma2d_m2m";
static constexpr size_t DMA2D_MAX_BATCH_SPANS = 192;
using Dma2dBurstLength = decltype(dma2d_transfer_ability_t{}.data_burst_length);
static constexpr Dma2dBurstLength DMA2D_DATA_BURST_LENGTH = static_cast<Dma2dBurstLength>(128);

struct alignas(64) Dma2dM2mContext {
  alignas(64) dma2d_descriptor_t tx_descriptors[DMA2D_MAX_BATCH_SPANS]{};
  alignas(64) dma2d_descriptor_t rx_descriptors[DMA2D_MAX_BATCH_SPANS]{};
  dma2d_pool_handle_t pool{nullptr};
  dma2d_trans_config_t transaction_config{};
  dma2d_transfer_ability_t transfer_ability{};
  dma2d_trans_t *transaction{nullptr};
  StaticSemaphore_t lock_storage{};
  StaticSemaphore_t done_storage{};
  SemaphoreHandle_t lock{nullptr};
  SemaphoreHandle_t done{nullptr};
  bool initialized{false};
  bool failed{false};
};

Dma2dM2mContext context;

void init_descriptor(dma2d_descriptor_t *descriptor, void *buffer, int picture_width, int picture_height,
                     int block_x, int block_y, int block_width, int block_height) {
  *descriptor = {};
  descriptor->owner = DMA2D_DESCRIPTOR_BUFFER_OWNER_DMA;
  descriptor->dma2d_en = 1;
  descriptor->hb_length = block_width;
  descriptor->vb_size = block_height;
  descriptor->pbyte = DMA2D_DESCRIPTOR_PBYTE_3B0_PER_PIXEL;
  descriptor->ha_length = picture_width;
  descriptor->va_size = picture_height;
  descriptor->mode = DMA2D_DESCRIPTOR_BLOCK_RW_MODE_SINGLE;
  descriptor->x = block_x;
  descriptor->y = block_y;
  descriptor->buffer = buffer;
  descriptor->next = nullptr;
}

bool IRAM_ATTR transaction_done(dma2d_channel_handle_t, dma2d_event_data_t *, void *user_data) {
  auto *ctx = static_cast<Dma2dM2mContext *>(user_data);
  BaseType_t task_woken = pdFALSE;
  xSemaphoreGiveFromISR(ctx->done, &task_woken);
  return task_woken == pdTRUE;
}

bool IRAM_ATTR transaction_picked(uint32_t channel_count, const dma2d_trans_channel_info_t *channels,
                                  void *user_data) {
  auto *ctx = static_cast<Dma2dM2mContext *>(user_data);
  if (ctx == nullptr || channels == nullptr || channel_count != 2)
    return false;

  dma2d_channel_handle_t tx_channel = nullptr;
  dma2d_channel_handle_t rx_channel = nullptr;
  for (uint32_t index = 0; index < channel_count; index++) {
    if (channels[index].dir == DMA2D_CHANNEL_DIRECTION_TX)
      tx_channel = channels[index].chan;
    else
      rx_channel = channels[index].chan;
  }
  if (tx_channel == nullptr || rx_channel == nullptr)
    return false;

  dma2d_trigger_t trigger{
      .periph = DMA2D_TRIG_PERIPH_M2M,
      .periph_sel_id = SOC_DMA2D_TRIG_PERIPH_M2M_TX,
  };
  dma2d_connect(tx_channel, &trigger);
  trigger.periph_sel_id = SOC_DMA2D_TRIG_PERIPH_M2M_RX;
  dma2d_connect(rx_channel, &trigger);
  dma2d_set_transfer_ability(tx_channel, &ctx->transfer_ability);
  dma2d_set_transfer_ability(rx_channel, &ctx->transfer_ability);

  dma2d_rx_event_callbacks_t callbacks{
      .on_recv_eof = transaction_done,
  };
  dma2d_register_rx_event_callbacks(rx_channel, &callbacks, ctx);
  dma2d_set_desc_addr(tx_channel, reinterpret_cast<intptr_t>(&ctx->tx_descriptors[0]));
  dma2d_set_desc_addr(rx_channel, reinterpret_cast<intptr_t>(&ctx->rx_descriptors[0]));
  dma2d_start(tx_channel);
  dma2d_start(rx_channel);
  return false;
}

bool initialize_context() {
  if (context.initialized)
    return true;
  if (context.failed)
    return false;

  context.lock = xSemaphoreCreateMutexStatic(&context.lock_storage);
  context.done = xSemaphoreCreateBinaryStatic(&context.done_storage);
  context.transaction = static_cast<dma2d_trans_t *>(
      heap_caps_aligned_calloc(8, 1, SIZEOF_DMA2D_TRANS_T, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  dma2d_pool_config_t pool_config{
      .pool_id = 0,
  };
  if (context.lock == nullptr || context.done == nullptr || context.transaction == nullptr ||
      dma2d_acquire_pool(&pool_config, &context.pool) != ESP_OK) {
    context.failed = true;
    return false;
  }

  context.transfer_ability.data_burst_length = DMA2D_DATA_BURST_LENGTH;
  context.transfer_ability.desc_burst_en = true;
  context.transfer_ability.mb_size = DMA2D_MACRO_BLOCK_SIZE_NONE;
  context.transaction_config.tx_channel_num = 1;
  context.transaction_config.rx_channel_num = 1;
  context.transaction_config.channel_flags = DMA2D_CHANNEL_FUNCTION_FLAG_SIBLING;
  context.transaction_config.on_job_picked = transaction_picked;
  context.transaction_config.user_config = &context;
  context.initialized = true;
  return true;
}

bool run_transaction_locked(size_t descriptor_count, TickType_t timeout) {
  if (descriptor_count == 0 || descriptor_count > DMA2D_MAX_BATCH_SPANS)
    return false;

  for (size_t index = 0; index < descriptor_count; index++) {
    const bool last = index + 1 == descriptor_count;
    context.tx_descriptors[index].suc_eof = last ? 1 : 0;
    context.tx_descriptors[index].next = last ? nullptr : &context.tx_descriptors[index + 1];
    context.rx_descriptors[index].suc_eof = 0;
    context.rx_descriptors[index].next = last ? nullptr : &context.rx_descriptors[index + 1];
  }

  while (xSemaphoreTake(context.done, 0) == pdTRUE) {
  }
  const size_t descriptor_bytes =
      (descriptor_count * sizeof(dma2d_descriptor_t) + 63U) & ~static_cast<size_t>(63U);
  esp_cache_msync(context.tx_descriptors, descriptor_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  esp_cache_msync(context.rx_descriptors, descriptor_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  std::memset(context.transaction, 0, SIZEOF_DMA2D_TRANS_T);

  const esp_err_t result = dma2d_enqueue(context.pool, &context.transaction_config, context.transaction);
  if (result != ESP_OK) {
    ESP_LOGW(TAG, "DMA2D enqueue failed: %s", esp_err_to_name(result));
    return false;
  }
  if (xSemaphoreTake(context.done, timeout) == pdTRUE)
    return true;

  // PPA, JPEG and the app-transition compositor share the ESP32-P4 DMA2D
  // channel pool. A transition can therefore time out once while the final
  // regional PPA frame is still retiring. End only that transaction and leave
  // the reusable M2M context healthy so the following animation frame can use
  // hardware again instead of permanently dropping to the CPU path.
  bool need_yield = false;
  const esp_err_t force_result = dma2d_force_end(context.transaction, &need_yield);
  ESP_LOGW(TAG, "DMA2D transaction timed out (%u descriptors), force_end=%s", static_cast<unsigned>(descriptor_count),
           esp_err_to_name(force_result));
  while (xSemaphoreTake(context.done, 0) == pdTRUE) {
  }
  return false;
}

uint32_t integer_sqrt(uint32_t value) {
  uint32_t result = 0;
  uint32_t bit = 1UL << 30;
  while (bit > value)
    bit >>= 2;
  while (bit != 0) {
    if (value >= result + bit) {
      value -= result + bit;
      result = (result >> 1) + bit;
    } else {
      result >>= 1;
    }
    bit >>= 2;
  }
  return result;
}

}  // namespace
#endif

bool dma2d_m2m_copy_rgb888_spans(const Dma2dM2mCopySpan *spans, size_t span_count, uint8_t *target,
                                 int target_width, int target_height) {
#if defined(USE_ESP32) && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (spans == nullptr || target == nullptr || span_count == 0 || span_count > DMA2D_MAX_BATCH_SPANS ||
      target_width <= 0 || target_height <= 0 || !initialize_context()) {
    return false;
  }
  for (size_t index = 0; index < span_count; index++) {
    const auto &span = spans[index];
    if (span.source == nullptr || span.source_width <= 0 || span.source_height <= 0 || span.source_x < 0 ||
        span.source_y < 0 || span.target_x < 0 || span.target_y < 0 || span.width <= 0 || span.height <= 0 ||
        span.source_x + span.width > span.source_width || span.source_y + span.height > span.source_height ||
        span.target_x + span.width > target_width || span.target_y + span.height > target_height) {
      return false;
    }
  }
  if (xSemaphoreTake(context.lock, pdMS_TO_TICKS(20)) != pdTRUE)
    return false;

  for (size_t index = 0; index < span_count; index++) {
    const auto &span = spans[index];
    init_descriptor(&context.tx_descriptors[index], const_cast<uint8_t *>(span.source), span.source_width,
                    span.source_height, span.source_x, span.source_y, span.width, span.height);
    init_descriptor(&context.rx_descriptors[index], target, target_width, target_height, span.target_x,
                    span.target_y, span.width, span.height);
  }
  const bool complete = run_transaction_locked(span_count, pdMS_TO_TICKS(100));
  xSemaphoreGive(context.lock);
  return complete;
#else
  return false;
#endif
}

bool dma2d_m2m_copy_rgb888_2d(const uint8_t *source, int source_width, int source_height, int source_x,
                              int source_y, uint8_t *target, int target_width, int target_height, int target_x,
                              int target_y, int block_width, int block_height) {
#if defined(USE_ESP32) && defined(CONFIG_IDF_TARGET_ESP32P4)
  const Dma2dM2mCopySpan span{source, source_width, source_height, source_x, source_y,
                              target_x, target_y, block_width, block_height};
  return dma2d_m2m_copy_rgb888_spans(&span, 1, target, target_width, target_height);
#else
  return false;
#endif
}

bool dma2d_m2m_compose_rgb888_circle(const uint8_t *background, int background_stride_pixels,
                                     int background_height, const uint8_t *foreground,
                                     int foreground_stride_pixels, int foreground_height, uint8_t *target,
                                     int target_width, int target_height, int center_x, int center_y, int radius) {
#if defined(USE_ESP32) && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (background == nullptr || foreground == nullptr || target == nullptr || background_stride_pixels < target_width ||
      foreground_stride_pixels < target_width || background_height < target_height ||
      foreground_height < target_height || target_width <= 0 || target_height <= 0 || radius < 0 ||
      !initialize_context()) {
    return false;
  }
  if (xSemaphoreTake(context.lock, pdMS_TO_TICKS(20)) != pdTRUE)
    return false;

  // App transitions use the same proven 128-byte M2M profile as the smooth
  // home-page compositor. DSI admission is handled before the transaction;
  // halving this burst only stretched every full-resolution transition frame
  // beyond the 16.7 ms display budget.
  context.transfer_ability.data_burst_length = DMA2D_DATA_BURST_LENGTH;
  size_t descriptor_count = 0;
  bool complete = true;

  auto flush_batch = [&]() {
    if (descriptor_count == 0 || !complete)
      return;
    complete = run_transaction_locked(descriptor_count, pdMS_TO_TICKS(100));
    descriptor_count = 0;
  };

  auto add_span = [&](const uint8_t *source, int source_stride_pixels, int y, int x, int width) {
    if (!complete || width <= 0)
      return;
    if (descriptor_count == DMA2D_MAX_BATCH_SPANS)
      flush_batch();
    if (!complete)
      return;
    init_descriptor(&context.tx_descriptors[descriptor_count], const_cast<uint8_t *>(source), source_stride_pixels,
                    target_height, x, y, width, 1);
    init_descriptor(&context.rx_descriptors[descriptor_count], target, target_width, target_height, x, y, width, 1);
    descriptor_count++;
  };

  const int radius_sq = radius * radius;
  for (int y = 0; y < target_height && complete; y++) {
    int foreground_x1 = 0;
    int foreground_x2 = -1;
    if (radius > 0) {
      const int dy = y - center_y;
      if (dy * dy <= radius_sq) {
        const int span = static_cast<int>(integer_sqrt(static_cast<uint32_t>(radius_sq - dy * dy)));
        foreground_x1 = std::clamp(center_x - span, 0, target_width - 1);
        foreground_x2 = std::clamp(center_x + span, 0, target_width - 1);
      }
    }

    if (foreground_x2 < foreground_x1) {
      add_span(background, background_stride_pixels, y, 0, target_width);
      continue;
    }
    add_span(background, background_stride_pixels, y, 0, foreground_x1);
    add_span(foreground, foreground_stride_pixels, y, foreground_x1, foreground_x2 - foreground_x1 + 1);
    add_span(background, background_stride_pixels, y, foreground_x2 + 1, target_width - foreground_x2 - 1);
  }
  flush_batch();
  context.transfer_ability.data_burst_length = DMA2D_DATA_BURST_LENGTH;
  xSemaphoreGive(context.lock);
  return complete;
#else
  return false;
#endif
}

bool dma2d_m2m_update_rgb888_circle(const uint8_t *background, int background_stride_pixels,
                                    int background_height, const uint8_t *foreground,
                                    int foreground_stride_pixels, int foreground_height, uint8_t *target,
                                    int target_width, int target_height, int old_center_x, int old_center_y,
                                    int old_radius, int new_center_x, int new_center_y, int new_radius) {
#if defined(USE_ESP32) && defined(CONFIG_IDF_TARGET_ESP32P4)
  if (background == nullptr || foreground == nullptr || target == nullptr || background_stride_pixels < target_width ||
      foreground_stride_pixels < target_width || background_height < target_height ||
      foreground_height < target_height || target_width <= 0 || target_height <= 0 || old_radius < 0 ||
      new_radius < 0 || !initialize_context()) {
    return false;
  }
  if (xSemaphoreTake(context.lock, pdMS_TO_TICKS(20)) != pdTRUE)
    return false;

  context.transfer_ability.data_burst_length = DMA2D_DATA_BURST_LENGTH;
  size_t descriptor_count = 0;
  bool complete = true;

  auto flush_batch = [&]() {
    if (descriptor_count == 0 || !complete)
      return;
    complete = run_transaction_locked(descriptor_count, pdMS_TO_TICKS(100));
    descriptor_count = 0;
  };
  auto add_span = [&](const uint8_t *source, int source_stride_pixels, int y, int x, int width) {
    if (!complete || width <= 0)
      return;
    if (descriptor_count == DMA2D_MAX_BATCH_SPANS)
      flush_batch();
    if (!complete)
      return;
    init_descriptor(&context.tx_descriptors[descriptor_count], const_cast<uint8_t *>(source), source_stride_pixels,
                    target_height, x, y, width, 1);
    init_descriptor(&context.rx_descriptors[descriptor_count], target, target_width, target_height, x, y, width, 1);
    descriptor_count++;
  };
  auto circle_span = [&](int y, int center_x, int center_y, int radius, int *x1, int *x2) {
    *x1 = 0;
    *x2 = -1;
    if (radius <= 0)
      return false;
    const int dy = y - center_y;
    if (dy < -radius || dy > radius)
      return false;
    const int span = static_cast<int>(integer_sqrt(static_cast<uint32_t>(radius * radius - dy * dy)));
    *x1 = std::clamp(center_x - span, 0, target_width - 1);
    *x2 = std::clamp(center_x + span, 0, target_width - 1);
    return *x2 >= *x1;
  };
  auto add_interval_difference = [&](const uint8_t *source, int source_stride_pixels, int y, bool has_a, int a1,
                                     int a2, bool has_b, int b1, int b2) {
    if (!has_a)
      return;
    if (!has_b || a2 < b1 || a1 > b2) {
      add_span(source, source_stride_pixels, y, a1, a2 - a1 + 1);
      return;
    }
    if (a1 < b1)
      add_span(source, source_stride_pixels, y, a1, std::min(a2, b1 - 1) - a1 + 1);
    if (a2 > b2) {
      const int start = std::max(a1, b2 + 1);
      add_span(source, source_stride_pixels, y, start, a2 - start + 1);
    }
  };

  for (int y = 0; y < target_height && complete; y++) {
    int old_x1 = 0;
    int old_x2 = -1;
    int new_x1 = 0;
    int new_x2 = -1;
    const bool has_old = circle_span(y, old_center_x, old_center_y, old_radius, &old_x1, &old_x2);
    const bool has_new = circle_span(y, new_center_x, new_center_y, new_radius, &new_x1, &new_x2);

    // Pixels leaving the old circle become background; pixels entering the
    // new circle become foreground. The overlap and the outside stay intact.
    add_interval_difference(background, background_stride_pixels, y, has_old, old_x1, old_x2, has_new, new_x1,
                            new_x2);
    add_interval_difference(foreground, foreground_stride_pixels, y, has_new, new_x1, new_x2, has_old, old_x1,
                            old_x2);
  }
  flush_batch();
  context.transfer_ability.data_burst_length = DMA2D_DATA_BURST_LENGTH;
  xSemaphoreGive(context.lock);
  return complete;
#else
  return false;
#endif
}

}  // namespace esphome::lvgl
