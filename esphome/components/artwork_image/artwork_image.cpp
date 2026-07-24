#include "artwork_image.h"

#include <algorithm>
#include <cctype>
#include <cinttypes>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "esphome/core/version.h"

#ifdef USE_ESP32
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_timer.h"
#endif

#ifdef USE_ESP32_JPEG
#include "esphome/components/esp32_jpeg/esp32_jpeg.h"
#endif

#ifdef USE_ESP_IDF
#ifdef CONFIG_SOC_PPA_SUPPORTED
#include "driver/ppa.h"
#endif
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#endif

static const char *const TAG = "artwork_image";
static const char *const CONTENT_TYPE_HEADER_NAME = "content-type";
static constexpr uint32_t RETIRED_BUFFER_GRACE_MS = 2500;
static constexpr size_t MAX_RETIRED_BUFFERS = 2;
static constexpr size_t MAX_DOWNLOAD_BUFFER_SIZE = 2 * 1024 * 1024;
static constexpr size_t MAX_READ_CHUNK_SIZE = 4 * 1024;
static constexpr size_t MAX_SPARE_BUFFER_SIZE = 2 * 1024 * 1024;
static constexpr size_t DOWNLOAD_BUFFER_BASE_SIZE = 32 * 1024;
static constexpr int LOCAL_ARTWORK_HTTP_CONNECT_TIMEOUT_MS = 2500;
static constexpr int LOCAL_ARTWORK_HTTP_HEADER_TIMEOUT_MS = 2;
static constexpr int LOCAL_ARTWORK_HTTP_READ_TIMEOUT_MS = 5;
static constexpr int LOCAL_ARTWORK_HTTP_TX_BUFFER_SIZE = 512;
static constexpr uint32_t SLOW_ARTWORK_STAGE_MS = 30;
static constexpr uint32_t ARTWORK_READ_STRESS_PERIOD_MS = 250;
static constexpr uint32_t SENDSPIN_ARTWORK_PROCESS_DELAY_MS = 100;
#if defined(USE_ESP_IDF) && defined(USE_ARTWORK_IMAGE_JPEG_SUPPORT)
static constexpr uint32_t HTTP_JPEG_DECODE_TASK_STACK_SIZE = 8192;
static constexpr UBaseType_t HTTP_JPEG_DECODE_TASK_PRIORITY = 1;
#endif
#if defined(USE_SENDSPIN_ARTWORK) && defined(USE_ESP_IDF) && defined(USE_ARTWORK_IMAGE_JPEG_SUPPORT)
static constexpr uint32_t SENDSPIN_JPEG_DECODE_TASK_STACK_SIZE = 8192;
static constexpr UBaseType_t SENDSPIN_JPEG_DECODE_TASK_PRIORITY = 1;
#endif

#if defined(USE_ESP_IDF) && defined(USE_ARTWORK_IMAGE_JPEG_SUPPORT)
static StackType_t *allocate_artwork_task_stack(size_t size) {
#ifdef USE_ESP32
  auto *stack = static_cast<StackType_t *>(
      heap_caps_aligned_alloc(16, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (stack != nullptr)
    return stack;
  return static_cast<StackType_t *>(
      heap_caps_aligned_alloc(16, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
#else
  return static_cast<StackType_t *>(malloc(size));
#endif
}

static StaticTask_t *allocate_artwork_task_storage() {
#ifdef USE_ESP32
  return static_cast<StaticTask_t *>(
      heap_caps_calloc(1, sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
#else
  return static_cast<StaticTask_t *>(calloc(1, sizeof(StaticTask_t)));
#endif
}

static void free_artwork_task_memory(void *memory) {
  free(memory);
}

static const char *artwork_task_stack_location(const void *stack) {
#ifdef USE_ESP32
  return esp_ptr_external_ram(stack) ? "PSRAM" : "internal";
#else
  return "heap";
#endif
}
#endif

#ifndef CONFIG_ESPHOME_ARTWORK_TRACE_VERBOSE
#define CONFIG_ESPHOME_ARTWORK_TRACE_VERBOSE 0
#endif

#ifndef CONFIG_ESPHOME_ARTWORK_BUFFER_DIAGNOSTICS
#define CONFIG_ESPHOME_ARTWORK_BUFFER_DIAGNOSTICS 0
#endif

#ifndef CONFIG_ESPHOME_JPEG_DMA2D_OUTPUT_MSYNC_CHUNK
#define CONFIG_ESPHOME_JPEG_DMA2D_OUTPUT_MSYNC_CHUNK 0
#endif

#ifndef CONFIG_ESPHOME_ARTWORK_CACHE_SYNC_CHUNK
#define CONFIG_ESPHOME_ARTWORK_CACHE_SYNC_CHUNK CONFIG_ESPHOME_JPEG_DMA2D_OUTPUT_MSYNC_CHUNK
#endif

#ifndef CONFIG_ESPHOME_ARTWORK_CACHE_SYNC_FIFO_MIN
#define CONFIG_ESPHOME_ARTWORK_CACHE_SYNC_FIFO_MIN 896
#endif

#ifndef CONFIG_ESPHOME_ARTWORK_CACHE_SYNC_WAIT_US
#define CONFIG_ESPHOME_ARTWORK_CACHE_SYNC_WAIT_US 3000
#endif

#ifndef CONFIG_ESPHOME_ARTWORK_POST_DECODE_DSI_QUIET_MS
#define CONFIG_ESPHOME_ARTWORK_POST_DECODE_DSI_QUIET_MS 0
#endif

#ifndef CONFIG_ESPHOME_ARTWORK_LOCAL_HTTP_DSI_QUIET_MS
#define CONFIG_ESPHOME_ARTWORK_LOCAL_HTTP_DSI_QUIET_MS 0
#endif

#ifndef CONFIG_ESPHOME_ARTWORK_LOCAL_HTTP_READ_CHUNK
#define CONFIG_ESPHOME_ARTWORK_LOCAL_HTTP_READ_CHUNK 4096
#endif

#ifndef CONFIG_ESPHOME_ARTWORK_LOCAL_HTTP_RX_BUFFER_SIZE
#define CONFIG_ESPHOME_ARTWORK_LOCAL_HTTP_RX_BUFFER_SIZE 4096
#endif

#ifndef CONFIG_ESPHOME_ARTWORK_LOCAL_HTTP_STAGING_SIZE
#define CONFIG_ESPHOME_ARTWORK_LOCAL_HTTP_STAGING_SIZE 1024
#endif

#ifndef CONFIG_ESPHOME_ARTWORK_LOCAL_HTTP_COPY_FIFO_GUARD
#define CONFIG_ESPHOME_ARTWORK_LOCAL_HTTP_COPY_FIFO_GUARD 0
#endif

#ifndef CONFIG_ESPHOME_ARTWORK_LOCAL_HTTP_COPY_CHUNK
#define CONFIG_ESPHOME_ARTWORK_LOCAL_HTTP_COPY_CHUNK 1024
#endif

#ifndef CONFIG_ESPHOME_ARTWORK_LOCAL_HTTP_COPY_DELAY_TICKS
#define CONFIG_ESPHOME_ARTWORK_LOCAL_HTTP_COPY_DELAY_TICKS 0
#endif

#ifndef CONFIG_ESPHOME_ARTWORK_PPA_SRM_BURST_LENGTH
#define CONFIG_ESPHOME_ARTWORK_PPA_SRM_BURST_LENGTH 16
#endif

#ifndef CONFIG_ESPHOME_ARTWORK_PPA_SRM_BAND_HEIGHT
#define CONFIG_ESPHOME_ARTWORK_PPA_SRM_BAND_HEIGHT 8
#endif

#if defined(USE_ESP_IDF) && defined(CONFIG_SOC_PPA_SUPPORTED)
#if CONFIG_ESPHOME_ARTWORK_PPA_SRM_BURST_LENGTH == 128
#define ARTWORK_PPA_SRM_BURST_LENGTH PPA_DATA_BURST_LENGTH_128
#elif CONFIG_ESPHOME_ARTWORK_PPA_SRM_BURST_LENGTH == 64
#define ARTWORK_PPA_SRM_BURST_LENGTH PPA_DATA_BURST_LENGTH_64
#elif CONFIG_ESPHOME_ARTWORK_PPA_SRM_BURST_LENGTH == 32
#define ARTWORK_PPA_SRM_BURST_LENGTH PPA_DATA_BURST_LENGTH_32
#elif CONFIG_ESPHOME_ARTWORK_PPA_SRM_BURST_LENGTH == 16
#define ARTWORK_PPA_SRM_BURST_LENGTH PPA_DATA_BURST_LENGTH_16
#elif CONFIG_ESPHOME_ARTWORK_PPA_SRM_BURST_LENGTH == 8
#define ARTWORK_PPA_SRM_BURST_LENGTH PPA_DATA_BURST_LENGTH_8
#else
#error "CONFIG_ESPHOME_ARTWORK_PPA_SRM_BURST_LENGTH must be 8, 16, 32, 64 or 128"
#endif
#endif

extern "C" void esphome_mipi_dsi_mark_stress(const char *label, uint32_t duration_ms) __attribute__((weak));
extern "C" bool esphome_mipi_dsi_wait_fifo_margin(uint32_t min_depth, uint32_t timeout_us) __attribute__((weak));

#include "image_decoder.h"

#ifdef USE_ARTWORK_IMAGE_BMP_SUPPORT
#include "bmp_image.h"
#endif
#ifdef USE_ARTWORK_IMAGE_JPEG_SUPPORT
#include "jpeg_image.h"
#endif
#ifdef USE_ARTWORK_IMAGE_PNG_SUPPORT
#include "png_image.h"
#endif

namespace esphome {
namespace artwork_image {

using image::ImageType;

namespace {

struct DmaWrittenBuffer {
  const uint8_t *ptr{nullptr};
  size_t size{0};
};

DmaWrittenBuffer dma_written_buffers[4];

#if defined(USE_ESP_IDF) && defined(CONFIG_SOC_PPA_SUPPORTED) && defined(USE_ESP32_JPEG)
ppa_client_handle_t artwork_ppa_srm_client{nullptr};
ppa_client_handle_t artwork_ppa_blend_client{nullptr};
StaticSemaphore_t artwork_ppa_blend_mutex_storage{};
SemaphoreHandle_t artwork_ppa_blend_mutex{nullptr};
uint8_t *artwork_ppa_alpha_mask{nullptr};
size_t artwork_ppa_alpha_mask_size{0};

bool ensure_artwork_ppa_srm_client() {
  if (artwork_ppa_srm_client != nullptr) {
    return true;
  }
  ppa_client_config_t cfg = {};
  cfg.oper_type = PPA_OPERATION_SRM;
  cfg.max_pending_trans_num = 1;
  cfg.data_burst_length = ARTWORK_PPA_SRM_BURST_LENGTH;
  esp_err_t ret = ppa_register_client(&cfg, &artwork_ppa_srm_client);
  if (ret != ESP_OK) {
    artwork_ppa_srm_client = nullptr;
    ESP_LOGW(TAG, "Artwork PPA SRM client registration failed: %s", esp_err_to_name(ret));
    return false;
  }
  ESP_LOGI(TAG, "Artwork PPA SRM client registered (burst=%d)", static_cast<int>(ARTWORK_PPA_SRM_BURST_LENGTH));
  return true;
}

bool ensure_artwork_ppa_blend_resources(int width, int height) {
  if (width <= 0 || height <= 0) {
    return false;
  }
  if (artwork_ppa_blend_mutex == nullptr) {
    artwork_ppa_blend_mutex = xSemaphoreCreateMutexStatic(&artwork_ppa_blend_mutex_storage);
    if (artwork_ppa_blend_mutex == nullptr) {
      ESP_LOGW(TAG, "Artwork PPA blend mutex creation failed");
      return false;
    }
  }
  if (artwork_ppa_blend_client == nullptr) {
    ppa_client_config_t cfg = {};
    cfg.oper_type = PPA_OPERATION_BLEND;
    cfg.max_pending_trans_num = 1;
    cfg.data_burst_length = ARTWORK_PPA_SRM_BURST_LENGTH;
    esp_err_t ret = ppa_register_client(&cfg, &artwork_ppa_blend_client);
    if (ret != ESP_OK) {
      artwork_ppa_blend_client = nullptr;
      ESP_LOGW(TAG, "Artwork PPA blend client registration failed: %s", esp_err_to_name(ret));
      return false;
    }
    ESP_LOGI(TAG, "Artwork PPA blend client registered (burst=%d)",
             static_cast<int>(ARTWORK_PPA_SRM_BURST_LENGTH));
  }

  const size_t required = (static_cast<size_t>(width) * height + 63U) & ~size_t{63U};
  if (artwork_ppa_alpha_mask != nullptr && artwork_ppa_alpha_mask_size >= required) {
    return true;
  }
  if (artwork_ppa_alpha_mask != nullptr) {
    heap_caps_free(artwork_ppa_alpha_mask);
    artwork_ppa_alpha_mask = nullptr;
    artwork_ppa_alpha_mask_size = 0;
  }
  artwork_ppa_alpha_mask = static_cast<uint8_t *>(
      heap_caps_aligned_alloc(64, required, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (artwork_ppa_alpha_mask == nullptr) {
    ESP_LOGW(TAG, "Artwork PPA alpha mask allocation failed: %zu bytes", required);
    return false;
  }

  // PPA replaces every A8 sample with fg_alpha_fix_val. Initialize the
  // backing memory once so its first DMA read starts from a cache-clean range.
  memset(artwork_ppa_alpha_mask, 0xFF, required);
  esp_cache_msync(artwork_ppa_alpha_mask, required, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  artwork_ppa_alpha_mask_size = required;
  ESP_LOGI(TAG, "Artwork PPA alpha mask ready: %dx%d (%zu bytes)", width, height, required);
  return true;
}
#endif

void mark_display_stress(const char *label, uint32_t duration_ms = 1500) {
  if (esphome_mipi_dsi_mark_stress != nullptr) {
    esphome_mipi_dsi_mark_stress(label, duration_ms);
  }
}

void wait_for_display_quiet(const char *stage, uint32_t quiet_ms) {
#if defined(USE_ESP_IDF)
  if (quiet_ms == 0 || esphome_mipi_dsi_wait_fifo_margin == nullptr) {
    return;
  }
  const uint64_t start_us = esp_timer_get_time();
  const uint64_t deadline_us = start_us + static_cast<uint64_t>(quiet_ms) * 1000ULL;
  uint32_t waits = 0;
  while (esp_timer_get_time() < deadline_us) {
    esphome_mipi_dsi_wait_fifo_margin(CONFIG_ESPHOME_ARTWORK_CACHE_SYNC_FIFO_MIN,
                                      CONFIG_ESPHOME_ARTWORK_CACHE_SYNC_WAIT_US);
    waits++;
    vTaskDelay(1);
  }
  const uint64_t elapsed_us = esp_timer_get_time() - start_us;
  ESP_LOGW(TAG, "Artwork display quiet barrier %s took %lluus waits=%u target=%ums fifo_min=%u", stage,
           (unsigned long long) elapsed_us, waits, quiet_ms,
           static_cast<unsigned>(CONFIG_ESPHOME_ARTWORK_CACHE_SYNC_FIFO_MIN));
#else
  (void) stage;
  (void) quiet_ms;
#endif
}

bool wait_for_display_fifo_margin(const char *stage) {
#if defined(USE_ESP_IDF)
  if (esphome_mipi_dsi_wait_fifo_margin == nullptr) {
    return true;
  }
  const uint64_t start_us = esp_timer_get_time();
  const bool ready = esphome_mipi_dsi_wait_fifo_margin(CONFIG_ESPHOME_ARTWORK_CACHE_SYNC_FIFO_MIN,
                                                       CONFIG_ESPHOME_ARTWORK_CACHE_SYNC_WAIT_US);
  const uint64_t elapsed_us = esp_timer_get_time() - start_us;
  if (!ready || elapsed_us > 1000) {
    ESP_LOGW(TAG, "Artwork display fifo guard %s %s in %lluus fifo_min=%u", stage, ready ? "ready" : "timeout",
             (unsigned long long) elapsed_us, static_cast<unsigned>(CONFIG_ESPHOME_ARTWORK_CACHE_SYNC_FIFO_MIN));
  }
  return ready;
#else
  (void) stage;
  return true;
#endif
}

void assign_with_display_backpressure(std::vector<uint8_t> &target, const uint8_t *data, size_t length,
                                      const char *stage) {
  target.clear();
  if (data == nullptr || length == 0) {
    return;
  }
#if defined(USE_ESP_IDF)
  const int64_t copy_start_us = esp_timer_get_time();
#endif
  wait_for_display_fifo_margin(stage);
  target.resize(length);
  constexpr size_t COPY_CHUNK = 4096;
  size_t offset = 0;
  while (offset < length) {
    const size_t chunk = std::min(COPY_CHUNK, length - offset);
    memcpy(target.data() + offset, data + offset, chunk);
    offset += chunk;
    if (offset < length) {
      wait_for_display_fifo_margin(stage);
      // SsArt runs above ESPHome's main loop. taskYIELD() only yields to tasks
      // at the same priority, so a large image could starve UI and control
      // processing until the complete copy finished.
      vTaskDelay(1);
    }
  }
  wait_for_display_fifo_margin(stage);
#if defined(USE_ESP_IDF)
  const int64_t copy_elapsed_us = esp_timer_get_time() - copy_start_us;
  if (copy_elapsed_us > 100000) {
    ESP_LOGW(TAG, "Artwork compressed copy %s took %lldms for %zu bytes", stage, copy_elapsed_us / 1000, length);
  }
#endif
}

void register_dma_written_buffer(const void *ptr, size_t size, bool written_by_dma) {
  if (ptr == nullptr || size == 0) {
    return;
  }

  const auto *data = static_cast<const uint8_t *>(ptr);
  for (auto &entry : dma_written_buffers) {
    if (entry.ptr == data) {
      if (written_by_dma) {
        entry.size = size;
      } else {
        entry = {};
      }
      return;
    }
  }

  if (!written_by_dma) {
    return;
  }

  for (auto &entry : dma_written_buffers) {
    if (entry.ptr == nullptr) {
      entry.ptr = data;
      entry.size = size;
      return;
    }
  }

  dma_written_buffers[0] = DmaWrittenBuffer{data, size};
}

}  // namespace

extern "C" bool esphome_artwork_image_buffer_written_by_dma(const void *ptr) {
  if (ptr == nullptr) {
    return false;
  }
  const auto *data = static_cast<const uint8_t *>(ptr);
  for (const auto &entry : dma_written_buffers) {
    if (entry.ptr != nullptr && data >= entry.ptr && data < entry.ptr + entry.size) {
      return true;
    }
  }
  return false;
}

static void log_slow_artwork_stage(const char *stage, uint32_t start_ms) {
  const uint32_t elapsed = millis() - start_ms;
  if (elapsed > 2000) {
    ESP_LOGW(TAG, "Artwork slow stage: %s took %" PRIu32 "ms", stage, elapsed);
  } else if (elapsed > SLOW_ARTWORK_STAGE_MS) {
    ESP_LOGD(TAG, "Artwork stage: %s took %" PRIu32 "ms", stage, elapsed);
  }
}

static bool should_keep_spare_buffer(size_t size, bool jpeg_allocator) {
  return jpeg_allocator && size <= MAX_SPARE_BUFFER_SIZE;
}

static uint64_t artwork_trace_now_us() {
#ifdef USE_ESP32
  return esp_timer_get_time();
#else
  return static_cast<uint64_t>(millis()) * 1000ULL;
#endif
}

static const char *image_format_to_string(ImageFormat format) {
  switch (format) {
    case ImageFormat::AUTO:
      return "auto";
    case ImageFormat::JPEG:
      return "jpeg";
    case ImageFormat::PNG:
      return "png";
    case ImageFormat::BMP:
      return "bmp";
    case ImageFormat::HEIC:
      return "heic";
    default:
      return "unknown";
  }
}

#if defined(USE_SENDSPIN_ARTWORK) && defined(USE_ESP_IDF) && defined(USE_ARTWORK_IMAGE_JPEG_SUPPORT)
static const char *sendspin_format_to_string(sendspin::SendspinImageFormat format) {
  switch (format) {
    case sendspin::SendspinImageFormat::JPEG:
      return "jpeg";
    case sendspin::SendspinImageFormat::PNG:
      return "png";
    case sendspin::SendspinImageFormat::BMP:
      return "bmp";
    default:
      return "unknown";
  }
}
#endif

void ArtworkImage::log_memory_summary_(const char *stage) const {
#ifdef USE_ESP32
  ESP_LOGW(TAG, "Artwork memory %s: image=%dx%d buffer=%uKB retired=%zu psram=%uK/%uK internal=%uK/%uK", stage,
           this->buffer_width_, this->buffer_height_, (unsigned) (this->get_buffer_size_() / 1024),
           this->retired_buffers_.size(), (unsigned) (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
           (unsigned) (heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024),
           (unsigned) (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
           (unsigned) (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
#else
  ESP_LOGW(TAG, "Artwork memory %s: image=%dx%d buffer=%uKB retired=%zu", stage, this->buffer_width_,
           this->buffer_height_, (unsigned) (this->get_buffer_size_() / 1024), this->retired_buffers_.size());
#endif
}

void ArtworkImage::begin_trace_(const char *stage, size_t bytes) {
  this->trace_id_ = ++this->trace_next_id_;
  this->trace_start_us_ = artwork_trace_now_us();
  this->trace_event_(stage, bytes);
}

void ArtworkImage::trace_event_(const char *stage, size_t bytes) const {
  if constexpr (!CONFIG_ESPHOME_ARTWORK_TRACE_VERBOSE) {
    (void) stage;
    (void) bytes;
    return;
  }
  if (this->trace_id_ == 0) {
    return;
  }
  const uint64_t now_us = artwork_trace_now_us();
  const uint64_t elapsed_us = this->trace_start_us_ == 0 ? 0 : now_us - this->trace_start_us_;
#ifdef USE_ESP32
  ESP_LOGW(TAG,
           "artwork trace #%u +%lluus %s bytes=%zu active=%p image=%dx%d decode=%p %dx%d retired=%zu psram=%uK/%uK "
           "internal=%uK/%uK",
           this->trace_id_, (unsigned long long) elapsed_us, stage, bytes, this->buffer_, this->buffer_width_,
           this->buffer_height_, this->decode_buffer_, this->decode_buffer_width_, this->decode_buffer_height_,
           this->retired_buffers_.size(), (unsigned) (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
           (unsigned) (heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024),
           (unsigned) (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
           (unsigned) (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
#else
  ESP_LOGW(TAG, "artwork trace #%u +%lluus %s bytes=%zu active=%p image=%dx%d decode=%p %dx%d retired=%zu",
           this->trace_id_, (unsigned long long) elapsed_us, stage, bytes, this->buffer_, this->buffer_width_,
           this->buffer_height_, this->decode_buffer_, this->decode_buffer_width_, this->decode_buffer_height_,
           this->retired_buffers_.size());
#endif
}

static void sync_artwork_buffer_for_dma(const void *ptr, size_t size, bool written_by_dma,
                                        bool force_dma_output_sync = false) {
#if defined(USE_ESP32) && defined(USE_ESP_IDF)
  if (ptr == nullptr || size == 0 || !esp_ptr_external_ram(ptr)) {
    return;
  }
#ifdef CONFIG_ESPHOME_ARTWORK_SKIP_DMA_OUTPUT_MSYNC
  if (written_by_dma && !force_dma_output_sync) {
    return;
  }
#endif
  constexpr size_t alignment = 64;
  const uintptr_t start = reinterpret_cast<uintptr_t>(ptr);
  const uintptr_t aligned_start = start & ~(static_cast<uintptr_t>(alignment) - 1U);
  const uintptr_t aligned_end = (start + size + alignment - 1U) & ~(static_cast<uintptr_t>(alignment) - 1U);
  if (aligned_end <= aligned_start || !esp_ptr_external_ram(reinterpret_cast<const void *>(aligned_start)) ||
      !esp_ptr_external_ram(reinterpret_cast<const void *>(aligned_end - 1U))) {
    return;
  }
  const uint64_t start_us = esp_timer_get_time();
  const uint32_t direction = written_by_dma ? ESP_CACHE_MSYNC_FLAG_DIR_M2C : ESP_CACHE_MSYNC_FLAG_DIR_C2M;
  const size_t aligned_size = aligned_end - aligned_start;
  size_t chunk_size = CONFIG_ESPHOME_ARTWORK_CACHE_SYNC_CHUNK;
  chunk_size = (chunk_size + alignment - 1U) & ~(alignment - 1U);
  uint64_t wait_total_us = 0;
  uint32_t wait_count = 0;
  auto wait_for_display_fifo = [&]() {
    if (esphome_mipi_dsi_wait_fifo_margin == nullptr || CONFIG_ESPHOME_ARTWORK_CACHE_SYNC_FIFO_MIN == 0) {
      return;
    }
    const uint64_t wait_start = esp_timer_get_time();
    esphome_mipi_dsi_wait_fifo_margin(CONFIG_ESPHOME_ARTWORK_CACHE_SYNC_FIFO_MIN,
                                      CONFIG_ESPHOME_ARTWORK_CACHE_SYNC_WAIT_US);
    wait_total_us += esp_timer_get_time() - wait_start;
    wait_count++;
  };
  if (chunk_size == 0 || chunk_size >= aligned_size) {
    wait_for_display_fifo();
    esp_cache_msync(reinterpret_cast<void *>(aligned_start), aligned_size, direction | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);
  } else {
    for (uintptr_t cursor = aligned_start; cursor < aligned_end;) {
      const size_t len = std::min(chunk_size, static_cast<size_t>(aligned_end - cursor));
      wait_for_display_fifo();
      esp_cache_msync(reinterpret_cast<void *>(cursor), len, direction | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);
      cursor += len;
#ifdef CONFIG_ESPHOME_JPEG_DMA2D_OUTPUT_MSYNC_YIELD
      if (cursor < aligned_end && esphome_mipi_dsi_wait_fifo_margin == nullptr) {
        vTaskDelay(1);
      }
#endif
    }
  }
  const uint64_t elapsed_us = esp_timer_get_time() - start_us;
  if (elapsed_us > 250000) {
    ESP_LOGW(TAG, "Artwork cache sync %s took %lluus size=%zu aligned=%zu wait=%lluus/%u chunk=%zu fifo_min=%u",
             written_by_dma ? "M2C" : "C2M", (unsigned long long) elapsed_us, size, aligned_end - aligned_start,
             (unsigned long long) wait_total_us, wait_count, chunk_size,
             static_cast<unsigned>(CONFIG_ESPHOME_ARTWORK_CACHE_SYNC_FIFO_MIN));
  } else if (elapsed_us > 30000) {
    ESP_LOGD(TAG, "Artwork cache sync %s took %lluus size=%zu aligned=%zu wait=%lluus/%u chunk=%zu fifo_min=%u",
             written_by_dma ? "M2C" : "C2M", (unsigned long long) elapsed_us, size, aligned_end - aligned_start,
             (unsigned long long) wait_total_us, wait_count, chunk_size,
             static_cast<unsigned>(CONFIG_ESPHOME_ARTWORK_CACHE_SYNC_FIFO_MIN));
  }
#else
  (void) ptr;
  (void) size;
  (void) written_by_dma;
#endif
}

static void log_artwork_buffer_probe(const char *stage, uint8_t *buffer, size_t size, bool written_by_dma) {
  if constexpr (!CONFIG_ESPHOME_ARTWORK_BUFFER_DIAGNOSTICS) {
    (void) stage;
    (void) buffer;
    (void) size;
    (void) written_by_dma;
    return;
  }
  if (buffer == nullptr || size == 0) {
    ESP_LOGW(TAG, "Artwork buffer probe %s: empty", stage);
    return;
  }

  sync_artwork_buffer_for_dma(buffer, size, written_by_dma, true);
  constexpr size_t max_samples = 8192;
  const size_t step = std::max<size_t>(1, size / max_samples);
  uint32_t hash = 2166136261U;
  size_t samples = 0;
  size_t nonzero = 0;
  for (size_t offset = 0; offset < size; offset += step) {
    const uint8_t value = buffer[offset];
    hash = (hash ^ value) * 16777619U;
    nonzero += value != 0;
    samples++;
  }
  ESP_LOGW(TAG, "Artwork buffer probe %s: ptr=%p size=%zu step=%zu samples=%zu nonzero=%zu hash=%08" PRIX32, stage,
           buffer, size, step, samples, nonzero, hash);
}

#ifdef USE_ESP_IDF
class LocalHttpContainer : public http_request::HttpContainer {
 public:
  enum class HeaderResult {
    PENDING,
    READY,
    ERROR,
  };

  explicit LocalHttpContainer(esp_http_client_handle_t client) : client_(client) {
    this->read_staging_ = static_cast<uint8_t *>(
        heap_caps_malloc(CONFIG_ESPHOME_ARTWORK_LOCAL_HTTP_STAGING_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (this->read_staging_ == nullptr) {
      ESP_LOGW(TAG, "Unable to allocate %u-byte internal local HTTP staging buffer; using direct reads",
               static_cast<unsigned>(CONFIG_ESPHOME_ARTWORK_LOCAL_HTTP_STAGING_SIZE));
    }
  }

  ~LocalHttpContainer() override {
    this->close_transport_();
    if (this->client_ != nullptr) {
      esp_http_client_cleanup(this->client_);
      this->client_ = nullptr;
    }
    if (this->read_staging_ != nullptr) {
      heap_caps_free(this->read_staging_);
      this->read_staging_ = nullptr;
    }
  }

  bool prepare(const std::string &url, bool secure, const std::vector<http_request::Header> &headers) {
    if (this->client_ == nullptr) {
      return false;
    }

    this->end();
    this->content_length = 0;
    this->status_code = -1;
    this->duration_ms = 0;
    this->bytes_read_ = 0;
    this->set_secure(secure);
    this->set_chunked(false);
    this->response_headers_.clear();
    this->headers_ready_ = false;
    this->staged_size_ = 0;

    this->pending_url_ = url;
    this->pending_headers_ = headers;
    return true;
  }

  void add_response_header(const std::string &name, const std::string &value) {
    this->response_headers_.push_back({name, value});
  }

  bool headers_ready() const { return this->headers_ready_; }

  esp_err_t open() {
    if (this->client_ == nullptr || this->pending_url_.empty())
      return ESP_ERR_INVALID_STATE;

    // esp_http_client_open() cannot begin a new streaming request while the
    // previous one is still in HTTP_STATE_RES_COMPLETE. Close only the socket,
    // keep the allocated client, and perform that potentially blocking reset
    // on the artwork worker core rather than ESPHome's main loop.
    this->close_transport_();
    esp_err_t result = esp_http_client_set_url(this->client_, this->pending_url_.c_str());
    if (result != ESP_OK)
      return result;
    for (const auto &header : this->pending_headers_) {
      result = esp_http_client_set_header(this->client_, header.name.c_str(), header.value.c_str());
      if (result != ESP_OK)
        return result;
    }

    // fetch_headers_step() and read_staged() deliberately reduce the client
    // timeout after connecting. Restore the connect timeout for every reused
    // request; otherwise the second and later TCP handshakes inherit the 5 ms
    // read timeout and intermittently fail with ESP_ERR_HTTP_CONNECT.
    for (uint8_t attempt = 0; attempt < 2; attempt++) {
      esp_http_client_set_timeout_ms(this->client_, LOCAL_ARTWORK_HTTP_CONNECT_TIMEOUT_MS);
      result = esp_http_client_open(this->client_, 0);
      if (result == ESP_OK) {
        this->transport_open_ = true;
        return ESP_OK;
      }

      const int socket_errno = esp_http_client_get_errno(this->client_);
      ESP_LOGW(TAG, "Local artwork connect attempt %u failed: %s errno=%d", attempt + 1,
               esp_err_to_name(result), socket_errno);
      // A failed connect can leave the transport partly initialized even
      // though transport_open_ was never set. Close it unconditionally before
      // retrying so the reusable handle returns to HTTP_STATE_INIT.
      esp_http_client_close(this->client_);
      this->transport_open_ = false;
      this->close_pending_ = false;
      if (attempt == 0)
        vTaskDelay(pdMS_TO_TICKS(25));
    }
    return result;
  }

  HeaderResult fetch_headers_step() {
    if (this->headers_ready_) {
      return HeaderResult::READY;
    }
    if (this->client_ == nullptr) {
      return HeaderResult::ERROR;
    }

    esp_http_client_set_timeout_ms(this->client_, LOCAL_ARTWORK_HTTP_HEADER_TIMEOUT_MS);
    int64_t content_length = esp_http_client_fetch_headers(this->client_);
    if (content_length == -ESP_ERR_HTTP_EAGAIN) {
      return HeaderResult::PENDING;
    }
    if (content_length < 0) {
      ESP_LOGE(TAG, "Local artwork header fetch failed: %lld", static_cast<long long>(content_length));
      return HeaderResult::ERROR;
    }

    this->content_length = content_length > 0 ? static_cast<size_t>(content_length) : 0;
    this->set_chunked(esp_http_client_is_chunked_response(this->client_));
    this->status_code = esp_http_client_get_status_code(this->client_);
    this->headers_ready_ = true;
    esp_http_client_set_timeout_ms(this->client_, LOCAL_ARTWORK_HTTP_READ_TIMEOUT_MS);
    return HeaderResult::READY;
  }

  int read_staged(size_t max_len) {
    if (this->client_ == nullptr || this->read_staging_ == nullptr) {
      return -ESP_ERR_INVALID_STATE;
    }
    if (max_len > CONFIG_ESPHOME_ARTWORK_LOCAL_HTTP_READ_CHUNK) {
      max_len = CONFIG_ESPHOME_ARTWORK_LOCAL_HTTP_READ_CHUNK;
    }
    max_len = std::min<size_t>(max_len, CONFIG_ESPHOME_ARTWORK_LOCAL_HTTP_STAGING_SIZE);
    this->staged_size_ = 0;
    const uint32_t start = millis();
    int read = esp_http_client_read(this->client_, reinterpret_cast<char *>(this->read_staging_), max_len);
    log_slow_artwork_stage("local-read", start);
    if (read == -ESP_ERR_HTTP_EAGAIN) {
      return 0;
    }
    if (read > 0) {
      this->staged_size_ = static_cast<size_t>(read);
      this->bytes_read_ += read;
    }
    return read;
  }

  bool copy_staged_to(uint8_t *buf, size_t size) {
    if (buf == nullptr || this->read_staging_ == nullptr || size > this->staged_size_) {
      return false;
    }
#if CONFIG_ESPHOME_ARTWORK_LOCAL_HTTP_COPY_FIFO_GUARD && defined(USE_ESP32)
    if (esp_ptr_external_ram(buf) && CONFIG_ESPHOME_ARTWORK_LOCAL_HTTP_COPY_CHUNK > 0) {
      size_t offset = 0;
      while (offset < size) {
        wait_for_display_fifo_margin("artwork-http-copy");
        const size_t chunk = std::min<size_t>(CONFIG_ESPHOME_ARTWORK_LOCAL_HTTP_COPY_CHUNK, size - offset);
        memcpy(buf + offset, this->read_staging_ + offset, chunk);
        offset += chunk;
#if CONFIG_ESPHOME_ARTWORK_LOCAL_HTTP_COPY_DELAY_TICKS > 0
        if (offset < size) {
          vTaskDelay(CONFIG_ESPHOME_ARTWORK_LOCAL_HTTP_COPY_DELAY_TICKS);
        }
#endif
      }
    } else
#endif
    {
      memcpy(buf, this->read_staging_, size);
    }
    this->staged_size_ = 0;
    return true;
  }

  int read(uint8_t *buf, size_t max_len) override {
    const int read = this->read_staged(max_len);
    if (read > 0 && !this->copy_staged_to(buf, static_cast<size_t>(read))) {
      return -ESP_ERR_INVALID_STATE;
    }
    return read;
  }

  bool is_read_complete() const override {
    if (HttpContainer::is_read_complete()) {
      return true;
    }
    return this->is_chunked_ && esp_http_client_is_complete_data_received(this->client_);
  }

  void end() override {
    // Defer socket shutdown until open() runs on the worker. Calling close()
    // here used to block the ESPHome loop for seconds on some Immich responses.
    this->close_pending_ = this->transport_open_;
    this->staged_size_ = 0;
  }

 protected:
  void close_transport_() {
    if (this->client_ != nullptr && (this->transport_open_ || this->close_pending_))
      esp_http_client_close(this->client_);
    this->transport_open_ = false;
    this->close_pending_ = false;
  }

  esp_http_client_handle_t client_{nullptr};
  uint8_t *read_staging_{nullptr};
  size_t staged_size_{0};
  bool headers_ready_{false};
  bool transport_open_{false};
  bool close_pending_{false};
  std::string pending_url_;
  std::vector<http_request::Header> pending_headers_;
};

static esp_err_t insecure_local_http_event_handler(esp_http_client_event_t *evt) {
  auto *container = static_cast<LocalHttpContainer *>(evt->user_data);
  if (container == nullptr || evt->event_id != HTTP_EVENT_ON_HEADER) {
    return ESP_OK;
  }
  const std::string header_name = str_lower_case(evt->header_key);
  if (header_name == CONTENT_TYPE_HEADER_NAME) {
    container->add_response_header(header_name, evt->header_value);
  }
  return ESP_OK;
}
#endif

inline bool is_color_on(const Color &color) {
  // This produces the most accurate monochrome conversion, but is slightly slower.
  //  return (0.2125 * color.r + 0.7154 * color.g + 0.0721 * color.b) > 127;

  // Approximation using fast integer computations; produces acceptable results
  // Equivalent to 0.25 * R + 0.5 * G + 0.25 * B
  return ((color.r >> 2) + (color.g >> 1) + (color.b >> 2)) & 0x80;
}

ArtworkImage::ArtworkImage(const std::string &url, int width, int height, ImageFormat format, ImageType type,
                           image::Transparency transparency, uint32_t download_buffer_size, bool is_big_endian,
                           bool allow_insecure_local_urls)
    : Image(nullptr, 0, 0, type, transparency),
      buffer_(nullptr),
      download_buffer_(std::min<size_t>(download_buffer_size, DOWNLOAD_BUFFER_BASE_SIZE),
                       RAMAllocator<uint8_t>::PREFER_INTERNAL),
      download_buffer_initial_size_(download_buffer_size),
      format_(format),
      fixed_width_(width),
      fixed_height_(height),
      is_big_endian_(is_big_endian),
      allow_insecure_local_urls_(allow_insecure_local_urls),
      buffer_width_(0),
      buffer_height_(0),
      start_time_(0) {
  this->set_url(url);
}

size_t ArtworkImage::memory_usage_bytes() const {
  size_t decoded_bytes = this->buffer_ == nullptr ? 0 : this->buffer_capacity_;
  if (this->decode_buffer_ != nullptr && this->decode_buffer_ != this->buffer_)
    decoded_bytes += this->decode_buffer_capacity_;
  if (this->spare_buffer_ != nullptr && this->spare_buffer_ != this->buffer_ &&
      this->spare_buffer_ != this->decode_buffer_)
    decoded_bytes += this->spare_buffer_size_;
  if (this->darkened_buffer_ != nullptr && this->darkened_buffer_ != this->buffer_ &&
      this->darkened_buffer_ != this->decode_buffer_ && this->darkened_buffer_ != this->spare_buffer_)
    decoded_bytes += this->get_buffer_size_();
  for (const auto &entry : this->retired_buffers_)
    decoded_bytes += entry.size;

  size_t encoded_bytes = this->download_buffer_.size();
#ifdef USE_SENDSPIN_ARTWORK
  encoded_bytes += this->pending_sendspin_data_.capacity();
#if defined(USE_ESP_IDF) && defined(USE_ARTWORK_IMAGE_JPEG_SUPPORT)
  encoded_bytes += this->sendspin_decode_data_.capacity();
#endif
#endif

  size_t task_bytes = 0;
#if defined(USE_ESP_IDF) && defined(USE_ARTWORK_IMAGE_JPEG_SUPPORT)
  if (this->http_jpeg_decode_task_stack_ != nullptr)
    task_bytes += HTTP_JPEG_DECODE_TASK_STACK_SIZE;
#ifdef USE_SENDSPIN_ARTWORK
  if (this->sendspin_decode_task_stack_ != nullptr)
    task_bytes += SENDSPIN_JPEG_DECODE_TASK_STACK_SIZE;
#endif
#endif
  return decoded_bytes + encoded_bytes + task_bytes;
}

void ArtworkImage::log_memory_usage(const char *phase) const {
  size_t active_bytes = this->buffer_ == nullptr ? 0 : this->buffer_capacity_;
  size_t staging_bytes =
      this->decode_buffer_ != nullptr && this->decode_buffer_ != this->buffer_ ? this->decode_buffer_capacity_ : 0;
  size_t spare_retired_bytes = 0;
  if (this->spare_buffer_ != nullptr && this->spare_buffer_ != this->buffer_ &&
      this->spare_buffer_ != this->decode_buffer_)
    spare_retired_bytes += this->spare_buffer_size_;
  for (const auto &entry : this->retired_buffers_)
    spare_retired_bytes += entry.size;

  size_t encoded_bytes = this->download_buffer_.size();
#ifdef USE_SENDSPIN_ARTWORK
  encoded_bytes += this->pending_sendspin_data_.capacity();
#if defined(USE_ESP_IDF) && defined(USE_ARTWORK_IMAGE_JPEG_SUPPORT)
  encoded_bytes += this->sendspin_decode_data_.capacity();
#endif
#endif
  size_t task_bytes = 0;
#if defined(USE_ESP_IDF) && defined(USE_ARTWORK_IMAGE_JPEG_SUPPORT)
  if (this->http_jpeg_decode_task_stack_ != nullptr)
    task_bytes += HTTP_JPEG_DECODE_TASK_STACK_SIZE;
#ifdef USE_SENDSPIN_ARTWORK
  if (this->sendspin_decode_task_stack_ != nullptr)
    task_bytes += SENDSPIN_JPEG_DECODE_TASK_STACK_SIZE;
#endif
#endif
  ESP_LOGW("memory.artwork", "%s total=%uK active=%uK staging=%uK spare_retired=%uK encoded=%uK tasks=%uK",
           phase == nullptr ? "runtime" : phase, (unsigned) (this->memory_usage_bytes() / 1024),
           (unsigned) (active_bytes / 1024), (unsigned) (staging_bytes / 1024),
           (unsigned) (spare_retired_bytes / 1024), (unsigned) (encoded_bytes / 1024),
           (unsigned) (task_bytes / 1024));
}

void ArtworkImage::setup() {
#if defined(USE_ESP_IDF) && defined(CONFIG_SOC_PPA_SUPPORTED) && defined(USE_ESP32_JPEG)
  if (this->scrim_opacity_ > 0 && this->type_ == image::IMAGE_TYPE_RGB565) {
    ensure_artwork_ppa_blend_resources(this->fixed_width_, this->fixed_height_);
  }
#endif
#if defined(USE_ESP_IDF) && defined(USE_ARTWORK_IMAGE_JPEG_SUPPORT)
  bool needs_http_jpeg_worker = this->hardware_jpeg_;
#ifdef USE_SENDSPIN_ARTWORK
  // SendSpin has its own zero-copy decoder worker. Avoid reserving a second
  // 8 KiB task stack for artwork instances that never use the HTTP path.
  needs_http_jpeg_worker = needs_http_jpeg_worker && this->sendspin_hub_ == nullptr;
#endif
  if (needs_http_jpeg_worker) {
    this->start_http_jpeg_decode_worker_();
  }
#endif
#ifdef USE_SENDSPIN_ARTWORK
  if (this->sendspin_hub_ == nullptr) {
    return;
  }
#if defined(USE_ESP_IDF) && defined(USE_ARTWORK_IMAGE_JPEG_SUPPORT)
  this->start_sendspin_decode_worker_();
#endif
  this->sendspin_hub_->add_image_decode_callback(
      [this](uint8_t slot, const uint8_t *data, size_t length, sendspin::SendspinImageFormat format) {
        if (slot != this->sendspin_slot_) {
          return;
        }

        // Copying a complete image can wait for DSI FIFO headroom between
        // PSRAM chunks. Never hold sendspin_pending_lock_ during that work:
        // image display/clear notifications are dispatched from the main
        // SendSpin loop and need the same lock. Holding it here stalled the
        // whole ESPHome loop until the copy completed.
        std::vector<uint8_t> copied_data;
        assign_with_display_backpressure(copied_data, data, length, "sendspin-copy");

        bool paused = false;
        {
          LockGuard guard(this->sendspin_pending_lock_);
          paused = this->sendspin_paused_;
          if (paused) {
            this->begin_trace_("sendspin-image-paused", length);
            // Keep only the latest compressed frame while the player page is
            // closed. Decoding stays paused, but opening the player can now
            // present the first artwork received with the initial stream.
            if constexpr (CONFIG_ESPHOME_ARTWORK_TRACE_VERBOSE) {
              ESP_LOGW(TAG, "sendspin image retained while paused slot=%u format=%s length=%zu", slot,
                       sendspin_format_to_string(format), length);
            }
          } else {
            this->begin_trace_("sendspin-image", length);
            mark_display_stress("artwork-rx", 1200);
            if constexpr (CONFIG_ESPHOME_ARTWORK_TRACE_VERBOSE) {
              ESP_LOGW(TAG, "artwork trace #%u sendspin image slot=%u format=%s length=%zu", this->trace_id_, slot,
                       sendspin_format_to_string(format), length);
            }
          }
          // swap() keeps the critical section constant-time. The previous
          // allocation is released after unlocking when copied_data dies.
          this->pending_sendspin_data_.swap(copied_data);
          this->pending_sendspin_format_ = format;
          this->pending_sendspin_image_ = true;
          this->pending_sendspin_clear_ = false;
          this->sendspin_decode_failed_.store(false, std::memory_order_release);
        }
        if (!paused) {
          this->queue_sendspin_process_();
        }
      });
  this->sendspin_hub_->add_image_clear_callback([this](uint8_t slot) {
    if (slot != this->sendspin_slot_) {
      return;
    }
    bool paused = false;
    {
      LockGuard guard(this->sendspin_pending_lock_);
      if (this->sendspin_paused_) {
        this->pending_sendspin_data_.clear();
        this->pending_sendspin_image_ = false;
        this->pending_sendspin_display_ = false;
        this->pending_sendspin_clear_ = true;
        if constexpr (CONFIG_ESPHOME_ARTWORK_TRACE_VERBOSE) {
          ESP_LOGW(TAG, "sendspin clear queued while paused slot=%u", slot);
        }
        return;
      }
      this->begin_trace_("sendspin-clear");
      if constexpr (CONFIG_ESPHOME_ARTWORK_TRACE_VERBOSE) {
        ESP_LOGW(TAG, "artwork trace #%u sendspin clear slot=%u", this->trace_id_, slot);
      }
      this->pending_sendspin_data_.clear();
      this->pending_sendspin_image_ = false;
      this->pending_sendspin_display_ = false;
      this->pending_sendspin_clear_ = true;
      paused = this->sendspin_paused_;
    }
    if (!paused) {
      this->queue_sendspin_process_();
    }
  });
  this->sendspin_hub_->add_image_display_callback([this](uint8_t slot) {
    if (slot != this->sendspin_slot_) {
      return;
    }
    bool paused = false;
    {
      LockGuard guard(this->sendspin_pending_lock_);
      if (this->sendspin_paused_) {
        this->pending_sendspin_display_ = true;
        if constexpr (CONFIG_ESPHOME_ARTWORK_TRACE_VERBOSE) {
          ESP_LOGW(TAG, "sendspin display queued while paused slot=%u", slot);
        }
        return;
      }
      this->trace_event_("sendspin-display");
      mark_display_stress("artwork-display", 1500);
      if constexpr (CONFIG_ESPHOME_ARTWORK_TRACE_VERBOSE) {
        ESP_LOGW(TAG, "artwork trace #%u sendspin display slot=%u", this->trace_id_, slot);
      }
      this->pending_sendspin_display_ = true;
      paused = this->sendspin_paused_;
    }
    if (!paused) {
      this->queue_sendspin_process_();
    }
  });
#endif
}

bool ArtworkImage::reserve_download_buffer() {
  if (!this->hardware_jpeg_) {
    return true;
  }
  const size_t reserve_size = std::min<size_t>(this->download_buffer_initial_size_, MAX_DOWNLOAD_BUFFER_SIZE);
  if (reserve_size <= this->download_buffer_.size()) {
    return true;
  }
  if (this->download_buffer_.resize(reserve_size) < reserve_size) {
    ESP_LOGW(TAG, "Unable to reserve %zu-byte hardware JPEG input buffer; retaining %zu bytes", reserve_size,
             this->download_buffer_.size());
    return false;
  }
  ESP_LOGI(TAG, "Reserved %zu-byte reusable hardware JPEG input buffer", reserve_size);
  return true;
}

bool ArtworkImage::reserve_local_http_client(const std::string &url) {
#ifdef USE_ESP_IDF
  if (this->local_http_cache_ != nullptr) {
    return true;
  }

  const std::string reserve_url = url.empty() ? "http://127.0.0.1/" : url;
  const bool secure = reserve_url.rfind("https://", 0) == 0;
  esp_http_client_config_t config = {};
  config.url = reserve_url.c_str();
  config.method = HTTP_METHOD_GET;
  config.timeout_ms = std::min<int>(this->parent_->get_timeout(), LOCAL_ARTWORK_HTTP_CONNECT_TIMEOUT_MS);
  config.disable_auto_redirect = false;
  config.max_redirection_count = 3;
  config.auth_type = HTTP_AUTH_TYPE_BASIC;
  config.event_handler = insecure_local_http_event_handler;
  config.buffer_size = CONFIG_ESPHOME_ARTWORK_LOCAL_HTTP_RX_BUFFER_SIZE;
  config.buffer_size_tx = LOCAL_ARTWORK_HTTP_TX_BUFFER_SIZE;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    ESP_LOGW(TAG, "Unable to reserve local artwork HTTP client");
    return false;
  }

  this->local_http_cache_ = std::make_shared<LocalHttpContainer>(client);
  this->local_http_cache_->set_parent(this->parent_);
  this->local_http_cache_->set_secure(secure);
  esp_http_client_set_user_data(client, static_cast<void *>(this->local_http_cache_.get()));
  ESP_LOGI(TAG, "Reserved reusable local artwork HTTP client");
  return true;
#else
  (void) url;
  return true;
#endif
}

#if defined(USE_ESP_IDF) && defined(USE_ARTWORK_IMAGE_JPEG_SUPPORT)
bool ArtworkImage::start_http_jpeg_decode_worker_() {
  if (this->http_jpeg_decode_task_ != nullptr) {
    return true;
  }

#if CONFIG_FREERTOS_UNICORE
  constexpr BaseType_t decode_core = tskNO_AFFINITY;
#elif defined(CONFIG_ESP_MAIN_TASK_AFFINITY_CPU0) && CONFIG_ESP_MAIN_TASK_AFFINITY_CPU0
  constexpr BaseType_t decode_core = 1;
#elif defined(CONFIG_ESP_MAIN_TASK_AFFINITY_CPU1) && CONFIG_ESP_MAIN_TASK_AFFINITY_CPU1
  constexpr BaseType_t decode_core = 0;
#else
  constexpr BaseType_t decode_core = 1;
#endif
  this->http_jpeg_decode_task_stack_ = allocate_artwork_task_stack(HTTP_JPEG_DECODE_TASK_STACK_SIZE);
  this->http_jpeg_decode_task_storage_ = allocate_artwork_task_storage();
  if (this->http_jpeg_decode_task_stack_ == nullptr || this->http_jpeg_decode_task_storage_ == nullptr) {
    free_artwork_task_memory(this->http_jpeg_decode_task_stack_);
    free_artwork_task_memory(this->http_jpeg_decode_task_storage_);
    this->http_jpeg_decode_task_stack_ = nullptr;
    this->http_jpeg_decode_task_storage_ = nullptr;
    ESP_LOGW(TAG, "Could not allocate the background HTTP JPEG decoder stack; using the main loop");
    return false;
  }
  this->http_jpeg_decode_task_ = xTaskCreateStaticPinnedToCore(
      ArtworkImage::http_jpeg_decode_worker_task_, "artwork_http_jpeg", HTTP_JPEG_DECODE_TASK_STACK_SIZE, this,
      HTTP_JPEG_DECODE_TASK_PRIORITY, this->http_jpeg_decode_task_stack_, this->http_jpeg_decode_task_storage_,
      decode_core);
  if (this->http_jpeg_decode_task_ == nullptr) {
    free_artwork_task_memory(this->http_jpeg_decode_task_stack_);
    free_artwork_task_memory(this->http_jpeg_decode_task_storage_);
    this->http_jpeg_decode_task_stack_ = nullptr;
    this->http_jpeg_decode_task_storage_ = nullptr;
    this->http_jpeg_decode_task_ = nullptr;
    ESP_LOGW(TAG, "Could not create the background HTTP JPEG decoder task; using the main loop");
    return false;
  }
  ESP_LOGI(TAG, "HTTP JPEG decoder runs on core %d at priority %u (%s stack)", decode_core,
           static_cast<unsigned>(HTTP_JPEG_DECODE_TASK_PRIORITY),
           artwork_task_stack_location(this->http_jpeg_decode_task_stack_));
  return true;
}

bool ArtworkImage::queue_http_jpeg_decode_() {
  if (this->http_jpeg_decode_task_ == nullptr || this->active_format_ != ImageFormat::JPEG ||
      !this->hardware_jpeg_ || this->decoder_ == nullptr || this->decoder_->has_unknown_download_size() ||
      this->download_buffer_.unread() < this->decoder_->get_download_size()) {
    return false;
  }
  if (this->http_jpeg_decode_done_.load(std::memory_order_acquire) ||
      this->http_jpeg_decode_busy_.exchange(true, std::memory_order_acq_rel)) {
    return true;
  }

  this->http_jpeg_decode_input_size_ = this->download_buffer_.unread();
  this->http_jpeg_decode_result_.store(0, std::memory_order_release);
  this->http_jpeg_decode_done_.store(false, std::memory_order_release);
  this->trace_event_("http-jpeg-worker-queued", this->http_jpeg_decode_input_size_);
  xTaskNotifyGive(this->http_jpeg_decode_task_);
  return true;
}

bool ArtworkImage::queue_local_http_open_() {
  if (this->http_jpeg_decode_task_ == nullptr || this->local_downloader_ == nullptr ||
      this->local_http_open_busy_.exchange(true, std::memory_order_acq_rel)) {
    return false;
  }
  this->local_http_open_result_.store(ESP_FAIL, std::memory_order_release);
  this->local_http_open_done_.store(false, std::memory_order_release);
  this->trace_event_("local-open-worker-queued");
  xTaskNotifyGive(this->http_jpeg_decode_task_);
  return true;
}

bool ArtworkImage::process_local_http_open_result_() {
  if (!this->local_http_open_done_.exchange(false, std::memory_order_acq_rel)) return false;
  const esp_err_t result = static_cast<esp_err_t>(this->local_http_open_result_.load(std::memory_order_acquire));
  this->trace_event_("local-open-worker-result", result == ESP_OK ? 1 : 0);
  if (result == ESP_OK) return false;

  ESP_LOGE(TAG, "Local artwork request failed: %s", esp_err_to_name(result));
  this->fail_download_();
  return true;
}

bool ArtworkImage::queue_local_http_headers_() {
  if (this->http_jpeg_decode_task_ == nullptr || this->local_downloader_ == nullptr ||
      this->local_http_headers_done_.load(std::memory_order_acquire) ||
      this->local_http_headers_busy_.exchange(true, std::memory_order_acq_rel)) {
    return false;
  }
  this->local_http_headers_result_.store(static_cast<int>(LocalHttpContainer::HeaderResult::PENDING),
                                         std::memory_order_release);
  this->trace_event_("local-headers-worker-queued");
  xTaskNotifyGive(this->http_jpeg_decode_task_);
  return true;
}

bool ArtworkImage::process_local_http_headers_result_(int &result) {
  if (!this->local_http_headers_done_.exchange(false, std::memory_order_acq_rel)) {
    return false;
  }
  result = this->local_http_headers_result_.load(std::memory_order_acquire);
  this->trace_event_("local-headers-worker-result", static_cast<size_t>(std::max(result, 0)));
  return true;
}

bool ArtworkImage::queue_local_http_read_(size_t size) {
  if (this->http_jpeg_decode_task_ == nullptr || this->local_downloader_ == nullptr || size == 0 ||
      this->local_http_read_done_.load(std::memory_order_acquire) ||
      this->local_http_read_busy_.exchange(true, std::memory_order_acq_rel)) {
    return false;
  }
  this->local_http_read_size_ = size;
  this->local_http_read_result_.store(0, std::memory_order_release);
  this->trace_event_("local-read-worker-queued", size);
  xTaskNotifyGive(this->http_jpeg_decode_task_);
  return true;
}

bool ArtworkImage::process_local_http_read_result_(int &result) {
  if (!this->local_http_read_done_.exchange(false, std::memory_order_acq_rel)) {
    return false;
  }
  result = this->local_http_read_result_.load(std::memory_order_acquire);
  this->trace_event_("local-read-worker-result", static_cast<size_t>(std::max(result, 0)));
  if (result > 0 &&
      (this->local_downloader_ == nullptr ||
       !this->local_downloader_->copy_staged_to(this->download_buffer_.append(), static_cast<size_t>(result)))) {
    ESP_LOGE(TAG, "Could not transfer the completed local artwork read into the download buffer");
    result = -ESP_ERR_INVALID_STATE;
  }
  return true;
}

void ArtworkImage::http_jpeg_decode_worker_task_(void *arg) {
  auto *image = static_cast<ArtworkImage *>(arg);
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (image->local_http_open_busy_.load(std::memory_order_acquire)) {
      image->trace_event_("local-open-worker-start");
      mark_display_stress("artwork-local-open", 2000);
      const esp_err_t result = image->local_downloader_ == nullptr ? ESP_ERR_INVALID_STATE
                                                                   : image->local_downloader_->open();
      image->local_http_open_result_.store(result, std::memory_order_release);
      image->local_http_open_done_.store(true, std::memory_order_release);
      image->local_http_open_busy_.store(false, std::memory_order_release);
      continue;
    }
    if (image->local_http_headers_busy_.load(std::memory_order_acquire)) {
      image->trace_event_("local-headers-worker-start");
      mark_display_stress("artwork-local-headers", 2000);
      const auto result = image->local_downloader_ == nullptr ? LocalHttpContainer::HeaderResult::ERROR
                                                               : image->local_downloader_->fetch_headers_step();
      image->local_http_headers_result_.store(static_cast<int>(result), std::memory_order_release);
      image->local_http_headers_done_.store(true, std::memory_order_release);
      image->local_http_headers_busy_.store(false, std::memory_order_release);
      continue;
    }
    if (image->local_http_read_busy_.load(std::memory_order_acquire)) {
      const size_t read_size = image->local_http_read_size_;
      image->trace_event_("local-read-worker-start", read_size);
      mark_display_stress("artwork-local-read", 2000);
      const int result = image->local_downloader_ == nullptr ? -ESP_ERR_INVALID_STATE
                                                              : image->local_downloader_->read_staged(read_size);
      image->local_http_read_result_.store(result, std::memory_order_release);
      image->local_http_read_done_.store(true, std::memory_order_release);
      image->local_http_read_busy_.store(false, std::memory_order_release);
      continue;
    }
    const size_t input_size = image->http_jpeg_decode_input_size_;
    image->trace_event_("http-jpeg-worker-start", input_size);
    mark_display_stress("http-jpeg-decode", 2000);
    int result = -1;
    if (image->decoder_ != nullptr && input_size != 0) {
      result = image->decoder_->decode(image->download_buffer_.data(), input_size);
    }
    image->trace_event_("http-jpeg-worker-end", static_cast<size_t>(std::max(result, 0)));
    image->http_jpeg_decode_result_.store(result, std::memory_order_release);
    image->http_jpeg_decode_done_.store(true, std::memory_order_release);
    image->http_jpeg_decode_busy_.store(false, std::memory_order_release);
  }
}

bool ArtworkImage::process_http_jpeg_decode_result_() {
  if (!this->http_jpeg_decode_done_.exchange(false, std::memory_order_acq_rel)) {
    return false;
  }

  const int fed = this->http_jpeg_decode_result_.load(std::memory_order_acquire);
  const size_t input_size = this->http_jpeg_decode_input_size_;
  this->http_jpeg_decode_input_size_ = 0;

  if (this->release_after_http_decode_) {
    this->finish_deferred_release_();
    return true;
  }
  if (fed < 0) {
    ESP_LOGE(TAG, "Error when decoding HTTP JPEG in the background task");
    this->fail_download_();
    return true;
  }
  if (static_cast<size_t>(fed) > input_size || static_cast<size_t>(fed) > this->download_buffer_.unread()) {
    ESP_LOGE(TAG, "Background JPEG decoder consumed %d bytes from a %zu-byte input", fed, input_size);
    this->fail_download_();
    return true;
  }

  this->download_buffer_.read(static_cast<size_t>(fed));
  if (this->decoder_ != nullptr && this->decoder_->is_finished()) {
    this->finish_download_();
    return true;
  }
  if (this->downloader_ != nullptr && this->downloader_->is_read_complete()) {
    ESP_LOGE(TAG, "HTTP JPEG transfer finished before the background decoder completed");
    this->fail_download_();
  }
  return true;
}

void ArtworkImage::finish_deferred_release_() {
  const bool immediate = this->release_after_http_decode_immediate_;
  this->release_after_http_decode_ = false;
  this->release_after_http_decode_immediate_ = false;
  this->release(immediate);
}
#endif

#ifdef USE_SENDSPIN_ARTWORK
#if defined(USE_ESP_IDF) && defined(USE_ARTWORK_IMAGE_JPEG_SUPPORT)
bool ArtworkImage::start_sendspin_decode_worker_() {
  if (this->sendspin_decode_task_ != nullptr) {
    return true;
  }

#if CONFIG_FREERTOS_UNICORE
  constexpr BaseType_t decode_core = tskNO_AFFINITY;
#elif defined(CONFIG_ESP_MAIN_TASK_AFFINITY_CPU0) && CONFIG_ESP_MAIN_TASK_AFFINITY_CPU0
  constexpr BaseType_t decode_core = 1;
#elif defined(CONFIG_ESP_MAIN_TASK_AFFINITY_CPU1) && CONFIG_ESP_MAIN_TASK_AFFINITY_CPU1
  constexpr BaseType_t decode_core = 0;
#else
  constexpr BaseType_t decode_core = 1;
#endif
  this->sendspin_decode_task_stack_ = allocate_artwork_task_stack(SENDSPIN_JPEG_DECODE_TASK_STACK_SIZE);
  this->sendspin_decode_task_storage_ = allocate_artwork_task_storage();
  if (this->sendspin_decode_task_stack_ == nullptr || this->sendspin_decode_task_storage_ == nullptr) {
    free_artwork_task_memory(this->sendspin_decode_task_stack_);
    free_artwork_task_memory(this->sendspin_decode_task_storage_);
    this->sendspin_decode_task_stack_ = nullptr;
    this->sendspin_decode_task_storage_ = nullptr;
    ESP_LOGW(TAG, "Could not allocate the background SendSpin JPEG decoder stack; using the main loop");
    return false;
  }
  this->sendspin_decode_task_ = xTaskCreateStaticPinnedToCore(
      ArtworkImage::sendspin_decode_worker_task_, "artwork_jpeg", SENDSPIN_JPEG_DECODE_TASK_STACK_SIZE, this,
      SENDSPIN_JPEG_DECODE_TASK_PRIORITY, this->sendspin_decode_task_stack_, this->sendspin_decode_task_storage_,
      decode_core);
  if (this->sendspin_decode_task_ == nullptr) {
    free_artwork_task_memory(this->sendspin_decode_task_stack_);
    free_artwork_task_memory(this->sendspin_decode_task_storage_);
    this->sendspin_decode_task_stack_ = nullptr;
    this->sendspin_decode_task_storage_ = nullptr;
    this->sendspin_decode_task_ = nullptr;
    ESP_LOGW(TAG, "Could not create the background SendSpin JPEG decoder task; using the main loop");
    return false;
  }
  ESP_LOGI(TAG, "SendSpin JPEG decoder runs on core %d at priority %u (%s stack)", decode_core,
           static_cast<unsigned>(SENDSPIN_JPEG_DECODE_TASK_PRIORITY),
           artwork_task_stack_location(this->sendspin_decode_task_stack_));
  return true;
}

bool ArtworkImage::queue_sendspin_jpeg_decode_(std::vector<uint8_t> &&data, bool display) {
  if (this->sendspin_decode_task_ == nullptr || data.empty() ||
      this->sendspin_decode_busy_.exchange(true, std::memory_order_acq_rel)) {
    return false;
  }

  this->sendspin_decode_data_ = std::move(data);
  this->sendspin_decode_ready_.store(false, std::memory_order_release);
  this->sendspin_decode_failed_.store(false, std::memory_order_release);
  this->sendspin_decode_owns_callbacks_.store(this->begin_decode_callbacks(), std::memory_order_release);
  if (display) {
    LockGuard guard(this->sendspin_pending_lock_);
    this->pending_sendspin_display_ = true;
  }
  xTaskNotifyGive(this->sendspin_decode_task_);
  return true;
}

void ArtworkImage::sendspin_decode_worker_task_(void *arg) {
  auto *image = static_cast<ArtworkImage *>(arg);
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    std::vector<uint8_t> data = std::move(image->sendspin_decode_data_);
    image->trace_event_("sendspin-jpeg-worker-start", data.size());
    mark_display_stress("artwork-decode", 2000);
    if (!image->decode_encoded_image_(ImageFormat::JPEG, data.data(), data.size(), false)) {
      image->sendspin_decode_failed_.store(true, std::memory_order_release);
    }
    mark_display_stress("artwork-decode-done", 1500);
    image->trace_event_("sendspin-jpeg-worker-end", data.size());
    image->sendspin_decode_busy_.store(false, std::memory_order_release);
    image->queue_sendspin_process_();
  }
}
#endif

void ArtworkImage::set_sendspin_paused(bool paused) {
  bool resume = false;
  {
    LockGuard guard(this->sendspin_pending_lock_);
    if (this->sendspin_paused_ == paused) {
      return;
    }
    this->sendspin_paused_ = paused;
    resume = !paused;
  }
  if (resume) {
    this->trace_event_("sendspin-resume");
    this->queue_sendspin_process_();
  }
}

void ArtworkImage::queue_sendspin_process_() {
  if (this->sendspin_process_queued_.exchange(true, std::memory_order_acq_rel)) {
    return;
  }
  this->defer([this]() {
    this->trace_event_("sendspin-process-defer");
    this->sendspin_process_queued_.store(false, std::memory_order_release);
    this->cancel_timeout("sendspin_artwork_process");
    this->set_timeout("sendspin_artwork_process", SENDSPIN_ARTWORK_PROCESS_DELAY_MS,
                      [this]() { this->process_pending_sendspin_(); });
  });
}

void ArtworkImage::process_pending_sendspin_() {
  std::vector<uint8_t> data;
  sendspin::SendspinImageFormat format{sendspin::SendspinImageFormat::JPEG};
  bool has_image = false;
  bool display = false;
  bool clear = false;
  bool requeue = false;

  {
    LockGuard guard(this->sendspin_pending_lock_);
    if (this->sendspin_paused_) {
      this->trace_event_("sendspin-process-paused");
      return;
    }
#if defined(USE_ESP_IDF) && defined(USE_ARTWORK_IMAGE_JPEG_SUPPORT)
    if (this->sendspin_decode_busy_.load(std::memory_order_acquire)) {
      this->trace_event_("sendspin-process-decode-busy");
      return;
    }
    const bool decoded_result_pending = this->sendspin_decode_ready_.load(std::memory_order_acquire) ||
                                        this->sendspin_decode_failed_.load(std::memory_order_acquire);
    if (decoded_result_pending) {
      if (!this->pending_sendspin_display_ && !this->pending_sendspin_clear_) {
        this->trace_event_("sendspin-process-wait-display");
        return;
      }
      display = this->pending_sendspin_display_;
      clear = this->pending_sendspin_clear_;
      this->pending_sendspin_display_ = false;
      this->pending_sendspin_clear_ = false;
    } else
#endif
        if (this->pending_sendspin_image_ && this->sendspin_finish_queued_.load(std::memory_order_acquire)) {
      this->trace_event_("sendspin-process-defer-finish");
      requeue = true;
    } else {
      data = std::move(this->pending_sendspin_data_);
      format = this->pending_sendspin_format_;
      has_image = this->pending_sendspin_image_;
      display = this->pending_sendspin_display_;
      clear = this->pending_sendspin_clear_;
      this->pending_sendspin_image_ = false;
      this->pending_sendspin_display_ = false;
      this->pending_sendspin_clear_ = false;
    }
  }
  if (requeue) {
    this->queue_sendspin_process_();
    return;
  }
  this->trace_event_("sendspin-process-start", data.size());
  mark_display_stress("artwork-process", 1500);

  if (clear) {
    this->trace_event_("sendspin-process-clear");
    this->sendspin_decode_ready_.store(false, std::memory_order_release);
    this->sendspin_decode_failed_.store(false, std::memory_order_release);
    if (this->sendspin_decode_owns_callbacks_.exchange(false, std::memory_order_acq_rel)) {
      this->complete_decode_callbacks(false);
    }
    this->discard_decode_buffer_();
    // A SendSpin stream boundary is not an image decode failure. Retain a
    // valid hardware-JPEG surface so the player keeps its last artwork and
    // the next 800x800 frame can be decoded in place. Releasing it here forced
    // a new 1.28 MB staging allocation on every station/track transition,
    // rapidly fragmenting PSRAM and starving other full-screen snapshots.
    if (this->buffer_ == nullptr || !this->buffer_uses_jpeg_allocator_) {
      this->release();
      this->download_error_callback_.call();
    }
    return;
  }

  if (has_image && !data.empty()) {
    ImageFormat image_format = ImageFormat::AUTO;
    switch (format) {
      case sendspin::SendspinImageFormat::JPEG:
        image_format = ImageFormat::JPEG;
        break;
      case sendspin::SendspinImageFormat::PNG:
        image_format = ImageFormat::PNG;
        break;
      case sendspin::SendspinImageFormat::BMP:
        image_format = ImageFormat::BMP;
        break;
    }
    if constexpr (CONFIG_ESPHOME_ARTWORK_TRACE_VERBOSE) {
      ESP_LOGW(TAG, "artwork trace #%u decode request format=%s size=%zu display=%s", this->trace_id_,
               image_format_to_string(image_format), data.size(), YESNO(display));
    }
    this->sendspin_decode_failed_.store(false, std::memory_order_release);
#if defined(USE_ESP_IDF) && defined(USE_ARTWORK_IMAGE_JPEG_SUPPORT)
    if (image_format == ImageFormat::JPEG && this->sendspin_decode_task_ != nullptr) {
      if (this->queue_sendspin_jpeg_decode_(std::move(data), display)) {
        this->trace_event_("sendspin-jpeg-worker-queued");
        return;
      }
      ESP_LOGW(TAG, "Background SendSpin JPEG decoder was busy; decoding in the main loop");
    }
#endif
    mark_display_stress("artwork-decode", 2000);
    if (!this->decode_encoded_image_(image_format, data.data(), data.size(), false)) {
      this->sendspin_decode_failed_.store(true, std::memory_order_release);
    }
    mark_display_stress("artwork-decode-done", 1500);
  }

  if (!display) {
    this->trace_event_("sendspin-process-wait-display");
    return;
  }
  if (display && !has_image && !this->sendspin_decode_failed_.load(std::memory_order_acquire) &&
      !this->sendspin_decode_ready_.load(std::memory_order_acquire)) {
    LockGuard guard(this->sendspin_pending_lock_);
    if (!this->sendspin_paused_) {
      this->pending_sendspin_display_ = true;
    }
    this->trace_event_("sendspin-display-before-decode-ready");
    return;
  }
  if (this->sendspin_decode_failed_.load(std::memory_order_acquire) ||
      this->sendspin_decode_ready_.load(std::memory_order_acquire)) {
    this->trace_event_("sendspin-queue-finish");
    this->queue_sendspin_finish_();
  }
}

void ArtworkImage::queue_sendspin_finish_() {
  if (this->sendspin_finish_queued_.exchange(true, std::memory_order_acq_rel)) {
    return;
  }
  this->defer([this]() {
    this->trace_event_("sendspin-finish-defer");
    this->sendspin_finish_queued_.store(false, std::memory_order_release);
    if (this->sendspin_decode_failed_.exchange(false, std::memory_order_acq_rel)) {
      this->trace_event_("sendspin-finish-error");
      if (this->sendspin_decode_owns_callbacks_.exchange(false, std::memory_order_acq_rel)) {
        this->complete_decode_callbacks(false);
      }
      this->download_error_callback_.call();
      return;
    }
    if (this->sendspin_decode_ready_.exchange(false, std::memory_order_acq_rel)) {
      this->trace_event_("sendspin-finish-download");
      this->finish_download_();
      if (this->sendspin_decode_owns_callbacks_.exchange(false, std::memory_order_acq_rel)) {
        this->complete_decode_callbacks(true);
      }
    }
  });
}
#endif

void ArtworkImage::draw(int x, int y, display::Display *display, Color color_on, Color color_off) {
  if (this->data_start_) {
    Image::draw(x, y, display, color_on, color_off);
  } else if (this->placeholder_) {
    this->placeholder_->draw(x, y, display, color_on, color_off);
  }
}

void ArtworkImage::apply_rgb_darken_once(uint8_t percent) {
  if (this->buffer_ == nullptr || percent == 0 || percent >= 100) {
    return;
  }
  if (this->darkened_buffer_ == this->buffer_ && this->darkened_percent_ == percent) {
    return;
  }

  const uint32_t start = millis();
  const size_t size = this->get_buffer_size_();
  if (this->type_ == image::IMAGE_TYPE_RGB) {
    if (percent == 50) {
      for (size_t i = 0; i < size; i++) {
        this->buffer_[i] >>= 1;
      }
    } else {
      const uint16_t keep = 100 - percent;
      for (size_t i = 0; i < size; i++) {
        this->buffer_[i] = static_cast<uint8_t>((static_cast<uint16_t>(this->buffer_[i]) * keep) / 100);
      }
    }
  } else if (this->type_ == image::IMAGE_TYPE_RGB565 && this->transparency_ != image::TRANSPARENCY_ALPHA_CHANNEL) {
    const uint16_t keep = 100 - percent;
    for (size_t i = 0; i + 1 < size; i += 2) {
      const uint16_t raw = this->is_big_endian_ ? (static_cast<uint16_t>(this->buffer_[i]) << 8) | this->buffer_[i + 1]
                                                : (static_cast<uint16_t>(this->buffer_[i + 1]) << 8) | this->buffer_[i];
      uint16_t r = ((raw >> 11) & 0x1F) * keep / 100;
      uint16_t g = ((raw >> 5) & 0x3F) * keep / 100;
      uint16_t b = (raw & 0x1F) * keep / 100;
      const uint16_t dark = static_cast<uint16_t>((r << 11) | (g << 5) | b);
      if (this->is_big_endian_) {
        this->buffer_[i] = static_cast<uint8_t>(dark >> 8);
        this->buffer_[i + 1] = static_cast<uint8_t>(dark & 0xFF);
      } else {
        this->buffer_[i] = static_cast<uint8_t>(dark & 0xFF);
        this->buffer_[i + 1] = static_cast<uint8_t>(dark >> 8);
      }
    }
  } else {
    return;
  }
  this->darkened_buffer_ = this->buffer_;
  this->darkened_percent_ = percent;
  sync_artwork_buffer_for_dma(this->buffer_, size, false);
  log_slow_artwork_stage("darken-buffer", start);
}

bool ArtworkImage::apply_decode_buffer_scrim_() {
  if (this->scrim_opacity_ == 0 || this->decode_buffer_scrim_applied_) {
    return true;
  }
  if (this->decode_buffer_ == nullptr || this->decode_buffer_width_ <= 0 || this->decode_buffer_height_ <= 0) {
    ESP_LOGE(TAG, "Cannot apply artwork scrim without a decoded image buffer");
    return false;
  }
  if (this->type_ != image::IMAGE_TYPE_RGB565 && this->type_ != image::IMAGE_TYPE_RGB) {
    ESP_LOGE(TAG, "Artwork scrim requires an RGB565 or RGB output buffer");
    return false;
  }

  const uint32_t start = millis();
  const uint8_t alpha = static_cast<uint8_t>((static_cast<uint16_t>(this->scrim_opacity_) * 255U + 50U) / 100U);
  const uint8_t keep = 255U - alpha;
  const uint8_t scrim_red = static_cast<uint8_t>((this->scrim_color_ >> 16) & 0xFFU);
  const uint8_t scrim_green = static_cast<uint8_t>((this->scrim_color_ >> 8) & 0xFFU);
  const uint8_t scrim_blue = static_cast<uint8_t>(this->scrim_color_ & 0xFFU);
  const int bytes_per_pixel = this->get_bpp() / 8;

#if defined(USE_ESP_IDF) && defined(CONFIG_SOC_PPA_SUPPORTED) && defined(USE_ESP32_JPEG)
  const size_t buffer_size = this->get_decode_buffer_size_();
  if (this->type_ == image::IMAGE_TYPE_RGB565 &&
      this->transparency_ != image::TRANSPARENCY_ALPHA_CHANNEL &&
      (reinterpret_cast<uintptr_t>(this->decode_buffer_) & 63U) == 0 && (buffer_size & 63U) == 0 &&
      ensure_artwork_ppa_blend_resources(this->decode_buffer_width_, this->decode_buffer_height_)) {
    // JPEG writes this buffer through DMA. Invalidate stale CPU cache lines
    // before the in-place blend; no full-frame CPU pass or C2M writeback is
    // needed after PPA has produced the final RGB565 pixels.
    if (this->decode_buffer_written_by_dma_) {
      esp_cache_msync(this->decode_buffer_, buffer_size, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    }

    ppa_blend_oper_config_t cfg = {};
    cfg.in_bg.buffer = this->decode_buffer_;
    cfg.in_bg.pic_w = this->decode_buffer_width_;
    cfg.in_bg.pic_h = this->decode_buffer_height_;
    cfg.in_bg.block_w = this->decode_buffer_width_;
    cfg.in_bg.block_h = this->decode_buffer_height_;
    cfg.in_bg.blend_cm = PPA_BLEND_COLOR_MODE_RGB565;
    cfg.in_fg.buffer = artwork_ppa_alpha_mask;
    cfg.in_fg.pic_w = this->decode_buffer_width_;
    cfg.in_fg.pic_h = this->decode_buffer_height_;
    cfg.in_fg.block_w = this->decode_buffer_width_;
    cfg.in_fg.block_h = this->decode_buffer_height_;
    cfg.in_fg.blend_cm = PPA_BLEND_COLOR_MODE_A8;
    cfg.out.buffer = this->decode_buffer_;
    cfg.out.buffer_size = buffer_size;
    cfg.out.pic_w = this->decode_buffer_width_;
    cfg.out.pic_h = this->decode_buffer_height_;
    cfg.out.blend_cm = PPA_BLEND_COLOR_MODE_RGB565;
    cfg.bg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
    cfg.bg_alpha_fix_val = 0xFF;
    cfg.fg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
    cfg.fg_alpha_fix_val = alpha;
    cfg.fg_fix_rgb_val.b = scrim_blue;
    cfg.fg_fix_rgb_val.g = scrim_green;
    cfg.fg_fix_rgb_val.r = scrim_red;
    cfg.mode = PPA_TRANS_MODE_BLOCKING;

    mark_display_stress("artwork-scrim-ppa", 1000);
    wait_for_display_fifo_margin("artwork-scrim-ppa");
    esp_err_t result = ESP_ERR_TIMEOUT;
    // Multiple artwork sources can finish concurrently. A PPA client with one
    // transaction descriptor otherwise returns ESP_FAIL immediately, which used
    // to trigger a multi-second full-frame CPU blend on the ESPHome loop task.
    if (xSemaphoreTake(artwork_ppa_blend_mutex, pdMS_TO_TICKS(250)) == pdTRUE) {
      result = ppa_do_blend(artwork_ppa_blend_client, &cfg);
      xSemaphoreGive(artwork_ppa_blend_mutex);
    }
    if (result == ESP_OK) {
      this->decode_buffer_written_by_dma_ = true;
      this->decode_buffer_scrim_applied_ = true;
      register_dma_written_buffer(this->decode_buffer_, buffer_size, true);
      log_slow_artwork_stage("scrim-buffer-ppa", start);
      return true;
    }
    ESP_LOGW(TAG, "Artwork PPA scrim failed: %s; using CPU fallback", esp_err_to_name(result));
  }
#endif

  // JPEG/PPA writes the staging buffer through DMA. Make those pixels visible
  // to the CPU before the in-place pass; promotion performs the final C2M sync.
  // This must bypass the normal output-sync skip: a PPA target may still have
  // cached background pixels written by the CPU before DMA replaced them.
  if (this->decode_buffer_written_by_dma_) {
    sync_artwork_buffer_for_dma(this->decode_buffer_, this->get_decode_buffer_size_(), true, true);
  }

  mark_display_stress("artwork-scrim", 1500);
  for (int y = 0; y < this->decode_buffer_height_; y++) {
    if ((y & 7) == 0) {
      wait_for_display_fifo_margin("artwork-scrim");
#ifdef USE_ESP_IDF
      taskYIELD();
#endif
    }
    uint8_t *row = this->decode_buffer_ + static_cast<size_t>(y) * this->decode_buffer_width_ * bytes_per_pixel;
    if (this->type_ == image::IMAGE_TYPE_RGB565) {
      const uint8_t scrim_r5 = scrim_red >> 3;
      const uint8_t scrim_g6 = scrim_green >> 2;
      const uint8_t scrim_b5 = scrim_blue >> 3;
      for (int x = 0; x < this->decode_buffer_width_; x++) {
        uint8_t *pixel = row + static_cast<size_t>(x) * bytes_per_pixel;
        const uint16_t raw = this->is_big_endian_ ? (static_cast<uint16_t>(pixel[0]) << 8) | pixel[1]
                                                  : (static_cast<uint16_t>(pixel[1]) << 8) | pixel[0];
        const uint16_t red = ((((raw >> 11) & 0x1FU) * keep) + (scrim_r5 * alpha) + 127U) / 255U;
        const uint16_t green = ((((raw >> 5) & 0x3FU) * keep) + (scrim_g6 * alpha) + 127U) / 255U;
        const uint16_t blue = (((raw & 0x1FU) * keep) + (scrim_b5 * alpha) + 127U) / 255U;
        const uint16_t blended = static_cast<uint16_t>((red << 11) | (green << 5) | blue);
        if (this->is_big_endian_) {
          pixel[0] = static_cast<uint8_t>(blended >> 8);
          pixel[1] = static_cast<uint8_t>(blended & 0xFFU);
        } else {
          pixel[0] = static_cast<uint8_t>(blended & 0xFFU);
          pixel[1] = static_cast<uint8_t>(blended >> 8);
        }
      }
    } else {
      for (int x = 0; x < this->decode_buffer_width_; x++) {
        uint8_t *pixel = row + static_cast<size_t>(x) * bytes_per_pixel;
        pixel[0] = static_cast<uint8_t>((pixel[0] * keep + scrim_blue * alpha + 127U) / 255U);
        pixel[1] = static_cast<uint8_t>((pixel[1] * keep + scrim_green * alpha + 127U) / 255U);
        pixel[2] = static_cast<uint8_t>((pixel[2] * keep + scrim_red * alpha + 127U) / 255U);
      }
    }
  }

  this->decode_buffer_written_by_dma_ = false;
  this->decode_buffer_scrim_applied_ = true;
  register_dma_written_buffer(this->decode_buffer_, this->get_decode_buffer_size_(), false);
  log_slow_artwork_stage("scrim-buffer", start);
  return true;
}

#ifdef USE_LVGL
void ArtworkImage::prepare_lvgl_dsc_() {
  this->lvgl_dsc_slot_ = this->lvgl_dsc_slot_ == 0 ? 1 : 0;
  auto *dsc = &this->lvgl_dsc_slots_[this->lvgl_dsc_slot_];
  memset(dsc, 0, sizeof(*dsc));
  // ESP32-P4 JPEG output is MCU-aligned, so the allocation can contain
  // trailing padding rows/columns (for example 800x450 in an 800x456
  // buffer). Preserve the physical stride while exposing only actual image
  // pixels to LVGL. Otherwise transformed images scale uninitialized padding
  // into visible horizontal/vertical artifacts.
  const int logical_width =
      this->buffer_offset_x_ == 0 && this->buffer_content_width_ > 0 ? this->buffer_content_width_ : this->width_;
  const int logical_height =
      this->buffer_offset_y_ == 0 && this->buffer_content_height_ > 0 ? this->buffer_content_height_ : this->height_;
  dsc->data = this->data_start_;
  dsc->header.reserved_2 = 0;
  dsc->header.stride = this->get_width_stride();
  dsc->header.w = logical_width;
  dsc->header.h = logical_height;
  dsc->data_size = this->get_width_stride() * logical_height;
  switch (this->get_type()) {
    case image::IMAGE_TYPE_BINARY:
      dsc->header.cf = LV_COLOR_FORMAT_A1;
      break;
    case image::IMAGE_TYPE_GRAYSCALE:
      dsc->header.cf = LV_COLOR_FORMAT_A8;
      break;
    case image::IMAGE_TYPE_RGB:
      dsc->header.cf =
          this->transparency_ == image::TRANSPARENCY_ALPHA_CHANNEL ? LV_COLOR_FORMAT_ARGB8888 : LV_COLOR_FORMAT_RGB888;
      break;
    case image::IMAGE_TYPE_RGB565:
      dsc->header.cf =
          this->transparency_ == image::TRANSPARENCY_ALPHA_CHANNEL ? LV_COLOR_FORMAT_RGB565A8 : LV_COLOR_FORMAT_RGB565;
      break;
  }
}

lv_image_dsc_t *ArtworkImage::get_lv_image_dsc() {
  const int logical_width =
      this->buffer_offset_x_ == 0 && this->buffer_content_width_ > 0 ? this->buffer_content_width_ : this->width_;
  const int logical_height =
      this->buffer_offset_y_ == 0 && this->buffer_content_height_ > 0 ? this->buffer_content_height_ : this->height_;
  auto *dsc = &this->lvgl_dsc_slots_[this->lvgl_dsc_slot_];
  if (dsc->data != this->data_start_ || dsc->header.w != logical_width || dsc->header.h != logical_height ||
      dsc->header.stride != this->get_width_stride()) {
    this->prepare_lvgl_dsc_();
    dsc = &this->lvgl_dsc_slots_[this->lvgl_dsc_slot_];
  }
  return dsc;
}
#endif

void ArtworkImage::release(bool immediate) {
  this->update_pending_ = false;
  this->pending_url_.clear();
#if defined(USE_ESP_IDF) && defined(USE_ARTWORK_IMAGE_JPEG_SUPPORT)
  if (this->http_jpeg_decode_busy_.load(std::memory_order_acquire) ||
      this->local_http_open_busy_.load(std::memory_order_acquire) ||
      this->local_http_headers_busy_.load(std::memory_order_acquire) ||
      this->local_http_read_busy_.load(std::memory_order_acquire)) {
    this->release_after_http_decode_ = true;
    this->release_after_http_decode_immediate_ = this->release_after_http_decode_immediate_ || immediate;
    return;
  }
#endif
  this->end_connection_();
  this->retire_active_buffer_();
  this->cleanup_retired_buffers_(immediate);
  if (immediate) {
    this->release_spare_buffer_();
  }
  if (!this->retired_buffers_.empty()) {
    this->enable_loop();
  }
}

bool ArtworkImage::begin_decode_callbacks() {
  if (this->decode_callbacks_active_.exchange(true, std::memory_order_acq_rel)) {
    return false;
  }
  this->decode_start_callback_.call();
  return true;
}

void ArtworkImage::complete_decode_callbacks(bool successful) {
  if (!this->decode_callbacks_active_.exchange(false, std::memory_order_acq_rel)) {
    return;
  }
  this->decode_finished_callback_.call(successful);
}

uint8_t *ArtworkImage::try_reuse_active_buffer_for_decode(int width, int height, int content_width,
                                                           int content_height) {
  if (this->decode_buffer_ != nullptr || this->buffer_ == nullptr || !this->buffer_uses_jpeg_allocator_) {
    return nullptr;
  }
  const size_t requested_size = width > 0 && height > 0 ? this->get_buffer_size_(width, height) : 0;
  const bool exact_reuse = content_width == width && content_height == height && width == this->buffer_width_ &&
                           height == this->buffer_height_ && requested_size == this->get_buffer_size_();
  const bool capacity_reuse = this->reuse_active_buffer_capacity_ && content_width > 0 && content_height > 0 &&
                              content_width <= width && content_height <= height && requested_size > 0 &&
                              requested_size <= this->buffer_capacity_;
  if (!exact_reuse && !capacity_reuse) {
    return nullptr;
  }

  this->decode_buffer_ = this->buffer_;
  this->decode_buffer_capacity_ = this->buffer_capacity_;
  this->decode_buffer_reuses_active_ = true;
  this->decode_buffer_uses_jpeg_allocator_ = this->buffer_uses_jpeg_allocator_;
  this->decode_buffer_width_ = width;
  this->decode_buffer_height_ = height;
  this->decode_content_width_ = content_width;
  this->decode_content_height_ = content_height;
  this->decode_offset_x_ = 0;
  this->decode_offset_y_ = 0;
  this->decode_buffer_written_by_dma_ = false;
  this->decode_buffer_darkened_percent_ = 0;
  this->decode_buffer_scrim_applied_ = false;
  ESP_LOGD(TAG, "Reusing active artwork buffer for %dx%d hardware decode", width, height);
  return this->decode_buffer_;
}

void ArtworkImage::cancel_reused_active_buffer_decode() {
  if (!this->decode_buffer_reuses_active_) {
    return;
  }
  this->decode_buffer_ = nullptr;
  this->decode_buffer_capacity_ = 0;
  this->decode_buffer_reuses_active_ = false;
  this->decode_buffer_uses_jpeg_allocator_ = false;
  this->decode_buffer_width_ = 0;
  this->decode_buffer_height_ = 0;
  this->decode_content_width_ = 0;
  this->decode_content_height_ = 0;
  this->decode_offset_x_ = 0;
  this->decode_offset_y_ = 0;
  this->decode_buffer_written_by_dma_ = false;
  this->decode_buffer_darkened_percent_ = 0;
  this->decode_buffer_scrim_applied_ = false;
}

uint8_t *ArtworkImage::try_get_staging_buffer_for_decode(int width, int height, int content_width, int content_height) {
  if (this->decode_buffer_ != nullptr) {
    return nullptr;
  }
  if (width <= 0 || height <= 0 || content_width != width || content_height != height) {
    return nullptr;
  }
  const size_t size = this->get_buffer_size_(width, height);
  if (this->spare_buffer_ != nullptr && this->spare_buffer_size_ >= size) {
    if (this->spare_buffer_uses_jpeg_allocator_) {
      this->decode_buffer_ = this->spare_buffer_;
      this->decode_buffer_capacity_ = this->spare_buffer_size_;
      this->decode_buffer_uses_jpeg_allocator_ = true;
      this->spare_buffer_ = nullptr;
      this->spare_buffer_size_ = 0;
      this->spare_buffer_uses_jpeg_allocator_ = false;
    } else {
      this->release_spare_buffer_();
    }
  }
  if (this->decode_buffer_ == nullptr) {
#ifdef USE_ESP32_JPEG
    size_t capacity = 0;
    this->decode_buffer_ = esp32_jpeg::allocate_decode_output(size, &capacity);
    this->decode_buffer_uses_jpeg_allocator_ = this->decode_buffer_ != nullptr;
    if (this->decode_buffer_ == nullptr || capacity < size) {
      if (this->decode_buffer_ != nullptr) {
        esp32_jpeg::release_decode_output(this->decode_buffer_);
        this->decode_buffer_ = nullptr;
        this->decode_buffer_uses_jpeg_allocator_ = false;
      }
      ESP_LOGW(TAG,
               "Hardware JPEG staging allocation failed: requested=%zu capacity=%zu. Biggest block in heap: %zu "
               "Bytes",
               size, capacity, this->allocator_.get_max_free_block_size());
      return nullptr;
    }
    this->decode_buffer_capacity_ = capacity;
#else
    this->decode_buffer_ = this->allocator_.allocate(size);
    this->decode_buffer_uses_jpeg_allocator_ = false;
    if (this->decode_buffer_ == nullptr) {
      ESP_LOGW(TAG, "Hardware JPEG staging allocation failed: %zu bytes. Biggest block in heap: %zu Bytes", size,
               this->allocator_.get_max_free_block_size());
      return nullptr;
    }
    this->decode_buffer_capacity_ = size;
#endif
  } else {
    ESP_LOGD(TAG, "Reusing JPEG-compatible artwork staging buffer");
  }
  this->decode_buffer_reuses_active_ = false;
  this->decode_buffer_width_ = width;
  this->decode_buffer_height_ = height;
  this->decode_content_width_ = content_width;
  this->decode_content_height_ = content_height;
  this->decode_offset_x_ = 0;
  this->decode_offset_y_ = 0;
  this->decode_buffer_written_by_dma_ = false;
  this->decode_buffer_darkened_percent_ = 0;
  this->decode_buffer_scrim_applied_ = false;
  ESP_LOGW(TAG, "Using artwork staging buffer for %dx%d hardware decode (%zu bytes)", width, height, size);
  return this->decode_buffer_;
}

void ArtworkImage::cancel_staging_buffer_decode() {
  if (!this->decode_buffer_) {
    return;
  }
  const size_t size = this->decode_buffer_capacity_;
  if (!this->decode_buffer_reuses_active_ && this->spare_buffer_ == nullptr &&
      should_keep_spare_buffer(size, this->decode_buffer_uses_jpeg_allocator_)) {
    this->spare_buffer_ = this->decode_buffer_;
    this->spare_buffer_size_ = size;
    this->spare_buffer_uses_jpeg_allocator_ = this->decode_buffer_uses_jpeg_allocator_;
  } else if (!this->decode_buffer_reuses_active_) {
    this->release_buffer_(this->decode_buffer_, size, this->decode_buffer_uses_jpeg_allocator_);
  }
  this->decode_buffer_ = nullptr;
  this->decode_buffer_capacity_ = 0;
  this->decode_buffer_reuses_active_ = false;
  this->decode_buffer_uses_jpeg_allocator_ = false;
  this->decode_buffer_width_ = 0;
  this->decode_buffer_height_ = 0;
  this->decode_content_width_ = 0;
  this->decode_content_height_ = 0;
  this->decode_offset_x_ = 0;
  this->decode_offset_y_ = 0;
  this->decode_buffer_written_by_dma_ = false;
  this->decode_buffer_darkened_percent_ = 0;
  this->decode_buffer_scrim_applied_ = false;
}

bool ArtworkImage::reserve_decode_buffer_capacity(size_t size) {
  if (size == 0 || (this->buffer_ != nullptr && this->buffer_capacity_ >= size) ||
      (this->spare_buffer_ != nullptr && this->spare_buffer_size_ >= size)) {
    return true;
  }
  if (this->decode_buffer_ != nullptr || this->spare_buffer_ != nullptr) {
    return false;
  }
#ifdef USE_ESP32_JPEG
  size_t capacity = 0;
  uint8_t *buffer = esp32_jpeg::allocate_decode_output(size, &capacity);
  if (buffer == nullptr || capacity < size) {
    if (buffer != nullptr)
      esp32_jpeg::release_decode_output(buffer);
    ESP_LOGW(TAG, "Unable to reserve JPEG output buffer: requested=%zu capacity=%zu", size, capacity);
    return false;
  }
  this->spare_buffer_ = buffer;
  this->spare_buffer_size_ = capacity;
  this->spare_buffer_uses_jpeg_allocator_ = true;
#else
  uint8_t *buffer = this->allocator_.allocate(size);
  if (buffer == nullptr)
    return false;
  this->spare_buffer_ = buffer;
  this->spare_buffer_size_ = size;
  this->spare_buffer_uses_jpeg_allocator_ = false;
#endif
  ESP_LOGI(TAG, "Reserved reusable decoded-image buffer: %zu bytes", this->spare_buffer_size_);
  return true;
}

size_t ArtworkImage::resize_(int width_in, int height_in) {
  int width = this->fixed_width_;
  int height = this->fixed_height_;
  int content_width = width;
  int content_height = height;
  int offset_x = 0;
  int offset_y = 0;
  if (this->is_auto_resize_()) {
    width = width_in;
    height = height_in;
    content_width = width;
    content_height = height;
  } else if (width_in > 0 && height_in > 0) {
    if (width_in != height_in) {
      double scale = std::min(static_cast<double>(this->fixed_width_) / width_in,
                              static_cast<double>(this->fixed_height_) / height_in);
      content_width = std::max(1, (static_cast<int>(width_in * scale) + 3) & ~3);
      content_height = std::max(1, (static_cast<int>(height_in * scale) + 3) & ~3);
      if (content_width > this->fixed_width_)
        content_width = this->fixed_width_;
      if (content_height > this->fixed_height_)
        content_height = this->fixed_height_;
      offset_x = (this->fixed_width_ - content_width) / 2;
      offset_y = (this->fixed_height_ - content_height) / 2;
    }
  }
  size_t new_size = this->get_buffer_size_(width, height);
  const bool needs_clear = content_width != width || content_height != height || offset_x != 0 || offset_y != 0;
  if (this->decode_buffer_) {
    if (new_size <= this->decode_buffer_capacity_) {
      this->decode_buffer_width_ = width;
      this->decode_buffer_height_ = height;
      this->decode_content_width_ = content_width;
      this->decode_content_height_ = content_height;
      this->decode_offset_x_ = offset_x;
      this->decode_offset_y_ = offset_y;
      this->decode_buffer_written_by_dma_ = false;
      this->decode_buffer_darkened_percent_ = 0;
      this->decode_buffer_scrim_applied_ = false;
      if (needs_clear) {
        memset(this->decode_buffer_, 0, new_size);
      }
      ESP_LOGI(TAG, "Artwork fit: source=%dx%d target=%dx%d content=%dx%d offset=%d,%d", width_in, height_in, width,
               height, content_width, content_height, offset_x, offset_y);
      return new_size;
    }
    this->release_buffer_(this->decode_buffer_, this->decode_buffer_capacity_,
                          this->decode_buffer_uses_jpeg_allocator_);
    this->decode_buffer_ = nullptr;
    this->decode_buffer_capacity_ = 0;
    this->decode_buffer_uses_jpeg_allocator_ = false;
    this->decode_buffer_width_ = 0;
    this->decode_buffer_height_ = 0;
    this->decode_content_width_ = 0;
    this->decode_content_height_ = 0;
    this->decode_offset_x_ = 0;
    this->decode_offset_y_ = 0;
    this->decode_buffer_written_by_dma_ = false;
    this->decode_buffer_darkened_percent_ = 0;
    this->decode_buffer_scrim_applied_ = false;
  }
  ESP_LOGD(TAG, "Allocating decode buffer of %zu bytes", new_size);
  this->decode_buffer_ = this->allocator_.allocate(new_size);
  this->decode_buffer_capacity_ = new_size;
  this->decode_buffer_uses_jpeg_allocator_ = false;
  if (this->decode_buffer_ == nullptr) {
    this->decode_buffer_capacity_ = 0;
    ESP_LOGE(TAG, "allocation of %zu bytes failed. Biggest block in heap: %zu Bytes", new_size,
             this->allocator_.get_max_free_block_size());
    this->end_connection_();
    return 0;
  }
  this->decode_buffer_width_ = width;
  this->decode_buffer_height_ = height;
  this->decode_content_width_ = content_width;
  this->decode_content_height_ = content_height;
  this->decode_offset_x_ = offset_x;
  this->decode_offset_y_ = offset_y;
  this->decode_buffer_written_by_dma_ = false;
  this->decode_buffer_darkened_percent_ = 0;
  this->decode_buffer_scrim_applied_ = false;
  if (needs_clear) {
    memset(this->decode_buffer_, 0, new_size);
  }
  ESP_LOGI(TAG, "Artwork fit: source=%dx%d target=%dx%d content=%dx%d offset=%d,%d", width_in, height_in, width, height,
           content_width, content_height, offset_x, offset_y);
  return new_size;
}

bool ArtworkImage::fit_rgb565_decode_buffer_with_ppa_(uint8_t *buffer, int buffer_width, int buffer_height,
                                                      int content_width, int content_height,
                                                      bool buffer_uses_jpeg_allocator) {
#if defined(USE_ESP_IDF) && defined(CONFIG_SOC_PPA_SUPPORTED) && defined(USE_ESP32_JPEG)
  if (!buffer_uses_jpeg_allocator || buffer == nullptr || this->get_bpp() != 16 || this->fixed_width_ <= 0 ||
      this->fixed_height_ <= 0 || buffer_width <= 0 || buffer_height <= 0 || content_width <= 0 ||
      content_height <= 0 || content_width > buffer_width || content_height > buffer_height) {
    return false;
  }
  if (buffer_width == this->fixed_width_ && buffer_height == this->fixed_height_ &&
      content_width == this->fixed_width_ && content_height == this->fixed_height_) {
    return false;
  }
  if (!ensure_artwork_ppa_srm_client()) {
    return false;
  }

  log_artwork_buffer_probe("ppa-source", buffer, static_cast<size_t>(buffer_width) * buffer_height * 2u, true);

  const float scale = std::min(static_cast<float>(this->fixed_width_) / static_cast<float>(content_width),
                               static_cast<float>(this->fixed_height_) / static_cast<float>(content_height));
  int scaled_width = std::max(1, static_cast<int>(static_cast<float>(content_width) * scale));
  int scaled_height = std::max(1, static_cast<int>(static_cast<float>(content_height) * scale));
  if (scaled_width > this->fixed_width_) {
    scaled_width = this->fixed_width_;
  }
  if (scaled_height > this->fixed_height_) {
    scaled_height = this->fixed_height_;
  }
  const int offset_x = (this->fixed_width_ - scaled_width) / 2;
  const int offset_y = (this->fixed_height_ - scaled_height) / 2;
  const size_t target_size = static_cast<size_t>(this->fixed_width_) * this->fixed_height_ * 2u;

  size_t capacity = 0;
  uint8_t *target = esp32_jpeg::allocate_decode_output(target_size, &capacity);
  if (target == nullptr || capacity < target_size) {
    if (target != nullptr) {
      esp32_jpeg::release_decode_output(target);
    }
    ESP_LOGW(TAG, "Artwork PPA fit allocation failed: requested=%zu capacity=%zu", target_size, capacity);
    return false;
  }

  const bool needs_background =
      offset_x != 0 || offset_y != 0 || scaled_width != this->fixed_width_ || scaled_height != this->fixed_height_;
  if (needs_background) {
    memset(target, 0, target_size);
    sync_artwork_buffer_for_dma(target, target_size, false);
  }

  ppa_srm_oper_config_t cfg = {};
  cfg.in.buffer = buffer;
  cfg.in.pic_w = buffer_width;
  cfg.in.pic_h = buffer_height;
  cfg.in.block_w = content_width;
  cfg.in.block_offset_x = 0;
  cfg.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
  cfg.out.buffer = target;
  cfg.out.buffer_size = target_size;
  cfg.out.pic_w = this->fixed_width_;
  cfg.out.pic_h = this->fixed_height_;
  cfg.out.block_offset_x = offset_x;
  cfg.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
  cfg.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
  cfg.scale_x = static_cast<float>(scaled_width) / static_cast<float>(content_width);
  cfg.scale_y = static_cast<float>(scaled_height) / static_cast<float>(content_height);
  cfg.mirror_x = false;
  cfg.mirror_y = false;
  cfg.rgb_swap = false;
  cfg.byte_swap = false;
  cfg.alpha_update_mode = PPA_ALPHA_NO_CHANGE;
  cfg.mode = PPA_TRANS_MODE_BLOCKING;

  const uint32_t band_height = CONFIG_ESPHOME_ARTWORK_PPA_SRM_BAND_HEIGHT == 0
                                   ? static_cast<uint32_t>(scaled_height)
                                   : CONFIG_ESPHOME_ARTWORK_PPA_SRM_BAND_HEIGHT;
  const uint64_t start_us = esp_timer_get_time();
  uint32_t max_band_us = 0;
  uint32_t wait_count = 0;
  esp_err_t ret = ESP_OK;
  mark_display_stress("artwork-ppa-fit", 2000);

  for (uint32_t y = 0; y < static_cast<uint32_t>(scaled_height); y += band_height) {
    uint32_t this_band_h = static_cast<uint32_t>(scaled_height) - y;
    if (band_height > 0 && this_band_h > band_height) {
      this_band_h = band_height;
    }
    const uint32_t src_y = static_cast<uint32_t>(static_cast<float>(y) / cfg.scale_y);
    uint32_t src_h = static_cast<uint32_t>(ceilf(static_cast<float>(this_band_h) / cfg.scale_y));
    if (src_y >= static_cast<uint32_t>(content_height)) {
      break;
    }
    if (src_y + src_h > static_cast<uint32_t>(content_height)) {
      src_h = static_cast<uint32_t>(content_height) - src_y;
    }
    if (src_h == 0) {
      continue;
    }

    cfg.in.block_h = src_h;
    cfg.in.block_offset_y = src_y;
    cfg.out.block_offset_y = offset_y + y;

    if (esphome_mipi_dsi_wait_fifo_margin != nullptr) {
      esphome_mipi_dsi_wait_fifo_margin(CONFIG_ESPHOME_ARTWORK_CACHE_SYNC_FIFO_MIN,
                                        CONFIG_ESPHOME_ARTWORK_CACHE_SYNC_WAIT_US);
      wait_count++;
    }
    const uint64_t band_start_us = esp_timer_get_time();
    ret = ppa_do_scale_rotate_mirror(artwork_ppa_srm_client, &cfg);
    const uint32_t band_us = static_cast<uint32_t>(esp_timer_get_time() - band_start_us);
    if (band_us > max_band_us) {
      max_band_us = band_us;
    }
    if (ret != ESP_OK) {
      break;
    }
    if (esphome_mipi_dsi_wait_fifo_margin != nullptr) {
      esphome_mipi_dsi_wait_fifo_margin(CONFIG_ESPHOME_ARTWORK_CACHE_SYNC_FIFO_MIN,
                                        CONFIG_ESPHOME_ARTWORK_CACHE_SYNC_WAIT_US);
      wait_count++;
    }
    taskYIELD();
  }

  const uint64_t elapsed_us = esp_timer_get_time() - start_us;
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Artwork PPA fit failed: %s source=%dx%d target=%dx%d", esp_err_to_name(ret), content_width,
             content_height, this->fixed_width_, this->fixed_height_);
    esp32_jpeg::release_decode_output(target);
    return false;
  }

  log_artwork_buffer_probe("ppa-target", target, target_size, true);

  this->release_buffer_(buffer, static_cast<size_t>(buffer_width) * buffer_height * 2u, buffer_uses_jpeg_allocator);
  this->discard_decode_buffer_();
  this->decode_buffer_ = target;
  this->decode_buffer_capacity_ = target_size;
  this->decode_buffer_uses_jpeg_allocator_ = true;
  this->decode_buffer_reuses_active_ = false;
  this->decode_buffer_width_ = this->fixed_width_;
  this->decode_buffer_height_ = this->fixed_height_;
  this->decode_content_width_ = scaled_width;
  this->decode_content_height_ = scaled_height;
  this->decode_offset_x_ = offset_x;
  this->decode_offset_y_ = offset_y;
  this->decode_buffer_written_by_dma_ = true;
  this->decode_buffer_darkened_percent_ = 0;
  this->decode_buffer_scrim_applied_ = false;
  register_dma_written_buffer(target, target_size, true);
  ESP_LOGW(TAG,
           "Artwork PPA fit: source=%dx%d buffer=%dx%d target=%dx%d content=%dx%d offset=%d,%d took=%lluus "
           "max_band=%uus waits=%u",
           content_width, content_height, buffer_width, buffer_height, this->fixed_width_, this->fixed_height_,
           scaled_width, scaled_height, offset_x, offset_y, (unsigned long long) elapsed_us, max_band_us, wait_count);
  return true;
#else
  (void) buffer;
  (void) buffer_width;
  (void) buffer_height;
  (void) content_width;
  (void) content_height;
  (void) buffer_uses_jpeg_allocator;
  return false;
#endif
}

void ArtworkImage::request_update_url(const std::string &url) {
  if (!this->validate_url_(url)) {
    return;
  }
  if (this->is_busy_()) {
    this->queue_pending_update_(url);
    return;
  }
  this->url_ = url;
  this->update();
}

void ArtworkImage::update() {
  if (this->is_busy_()) {
    this->queue_pending_update_(this->url_);
    return;
  }
  ESP_LOGI(TAG, "Updating image %s", this->url_.c_str());
  this->log_state_("request-start");

  std::vector<http_request::Header> headers = {};

  http_request::Header accept_header;
  accept_header.name = "Accept";
  std::string accept_mime_type;
  switch (this->format_) {
    case ImageFormat::AUTO:
      accept_mime_type = "image/jpeg, image/png";
      break;
#ifdef USE_ARTWORK_IMAGE_BMP_SUPPORT
    case ImageFormat::BMP:
      accept_mime_type = "image/bmp";
      break;
#endif  // USE_ARTWORK_IMAGE_BMP_SUPPORT
#ifdef USE_ARTWORK_IMAGE_JPEG_SUPPORT
    case ImageFormat::JPEG:
      accept_mime_type = "image/jpeg";
      break;
#endif  // USE_ARTWORK_IMAGE_JPEG_SUPPORT
#ifdef USE_ARTWORK_IMAGE_PNG_SUPPORT
    case ImageFormat::PNG:
      accept_mime_type = "image/png";
      break;
#endif  // USE_ARTWORK_IMAGE_PNG_SUPPORT
    default:
      accept_mime_type = "image/*";
  }
  accept_header.value = accept_mime_type + ",*/*;q=0.8";

  headers.push_back(accept_header);

  for (auto &header : this->request_headers_) {
    headers.push_back(http_request::Header{header.first, header.second.value()});
  }

  if (this->should_use_local_idf_url_(this->url_)) {
    mark_display_stress("artwork-local-request", 1500);
    this->downloader_ = this->get_local_idf_(this->url_, headers);
#ifdef USE_ESP_IDF
    this->local_downloader_ = static_cast<LocalHttpContainer *>(this->downloader_.get());
    if (this->local_downloader_ != nullptr) {
      if (!this->queue_local_http_open_()) {
        mark_display_stress("artwork-local-open", 2000);
        const esp_err_t result = this->local_downloader_->open();
        if (result != ESP_OK) {
          ESP_LOGE(TAG, "Local artwork request failed: %s", esp_err_to_name(result));
          this->end_connection_();
          this->download_error_callback_.call();
          this->start_pending_update_();
          return;
        }
      }
    }
#endif
  } else {
    mark_display_stress("artwork-parent-request", 1500);
    this->downloader_ = this->parent_->get(this->url_, headers, {CONTENT_TYPE_HEADER_NAME});
#ifdef USE_ESP_IDF
    this->local_downloader_ = nullptr;
#endif
  }

  if (this->downloader_ == nullptr) {
    ESP_LOGE(TAG, "Download failed.");
    this->end_connection_();
    this->download_error_callback_.call();
    this->start_pending_update_();
    return;
  }

#ifdef USE_ESP_IDF
  if (this->local_downloader_ != nullptr && !this->local_downloader_->headers_ready()) {
    this->log_state_("response-pending");
    this->local_headers_ready_pending_start_ = false;
    this->local_headers_ready_ms_ = 0;
    this->start_time_ = ::time(nullptr);
    this->last_data_millis_ = millis();
    this->last_download_read_stress_ms_ = 0;
    this->enable_loop();
    return;
  }
#endif

  this->start_response_download_();
}

bool ArtworkImage::start_response_download_() {
  int http_code = this->downloader_->status_code;
  this->log_state_("response-ready");
  if (http_code == HTTP_CODE_NOT_MODIFIED) {
    // Image hasn't changed on server. Skip download.
    ESP_LOGI(TAG, "Server returned HTTP 304 (Not Modified). Download skipped.");
    this->end_connection_();
    this->download_finished_callback_.call(true);
    this->start_pending_update_();
    return false;
  }
  if (http_code != HTTP_CODE_OK) {
    ESP_LOGE(TAG, "HTTP result: %d", http_code);
    this->end_connection_();
    this->download_error_callback_.call();
    this->start_pending_update_();
    return false;
  }

  ImageFormat resolved = this->detect_format_();
#ifdef USE_ESP_IDF
  if (this->local_downloader_ != nullptr && resolved != ImageFormat::AUTO) {
    // Local ESP-IDF HTTP responses can arrive immediately after header parsing.
    // Defer decoder creation until magic bytes are in the download buffer so
    // allocations do not run in the same tight window as the HTTP start response.
    resolved = ImageFormat::AUTO;
  }
#endif
  ESP_LOGD(TAG, "Starting download");
  size_t total_size = this->get_sane_content_length_();

  // The hardware JPEG decoder needs the complete encoded image. Reserve its
  // known payload once, before receiving any bytes, instead of repeatedly
  // reallocating and copying an ever-growing buffer through PSRAM.
  if (this->hardware_jpeg_ && total_size > this->download_buffer_.size() &&
      total_size <= MAX_DOWNLOAD_BUFFER_SIZE && this->download_buffer_.unread() == 0) {
    // Grow geometrically in 64 KiB steps and retain the high-water mark. This
    // avoids a second realloc when consecutive images differ only slightly.
    constexpr size_t GROWTH_GRANULARITY = 64 * 1024;
    const size_t target_size = std::min<size_t>(
        MAX_DOWNLOAD_BUFFER_SIZE, ((total_size + GROWTH_GRANULARITY - 1) / GROWTH_GRANULARITY) * GROWTH_GRANULARITY);
    ESP_LOGD(TAG, "Growing reusable hardware JPEG download buffer to %zu bytes", target_size);
    if (this->download_buffer_.resize(target_size) < total_size) {
      this->end_connection_();
      this->download_error_callback_.call();
      this->start_pending_update_();
      return false;
    }
  }

  if (resolved == ImageFormat::AUTO) {
    ESP_LOGD(TAG, "Deferring auto image format detection until magic bytes are available");
    this->log_state_("format-detect-wait");
    this->start_time_ = ::time(nullptr);
    this->last_data_millis_ = millis();
    this->last_download_read_stress_ms_ = 0;
    this->enable_loop();
    return true;
  }

  if (!this->create_decoder_(resolved, total_size)) {
    this->end_connection_();
    this->download_error_callback_.call();
    this->start_pending_update_();
    return false;
  }
  this->log_state_("decoder-ready");
  ESP_LOGI(TAG, "Downloading image (Size: %zu)", total_size);
  this->start_time_ = ::time(nullptr);
  this->last_data_millis_ = millis();
  this->last_download_read_stress_ms_ = 0;
  this->enable_loop();
  return true;
}

bool ArtworkImage::should_use_local_idf_url_(const std::string &url) const {
  bool is_http = url.rfind("http://", 0) == 0;
  bool is_https = url.rfind("https://", 0) == 0;
  if (!is_http && !(is_https && this->allow_insecure_local_urls_)) {
    return false;
  }

  size_t host_start = is_https ? 8 : 7;
  size_t host_end = url.find_first_of("/?#", host_start);
  std::string authority =
      url.substr(host_start, host_end == std::string::npos ? std::string::npos : host_end - host_start);
  size_t at = authority.rfind('@');
  if (at != std::string::npos) {
    authority = authority.substr(at + 1);
  }

  std::string host;
  if (!authority.empty() && authority.front() == '[') {
    size_t end = authority.find(']');
    host = end == std::string::npos ? authority : authority.substr(1, end - 1);
  } else {
    size_t colon = authority.find(':');
    host = colon == std::string::npos ? authority : authority.substr(0, colon);
  }

  std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c) { return std::tolower(c); });
  return this->is_private_or_local_host_(host);
}

bool ArtworkImage::is_private_or_local_host_(const std::string &host) const {
  if (host == "localhost" || host == "homeassistant.local" ||
      (host.size() > 6 && host.compare(host.size() - 6, 6, ".local") == 0)) {
    return true;
  }
  if (host.rfind("fe80:", 0) == 0 || host == "::1") {
    return true;
  }

  int parts[4] = {-1, -1, -1, -1};
  const char *cursor = host.c_str();
  char *end = nullptr;
  for (int i = 0; i < 4; i++) {
    long value = std::strtol(cursor, &end, 10);
    if (end == cursor || value < 0 || value > 255) {
      return false;
    }
    parts[i] = static_cast<int>(value);
    if (i < 3) {
      if (*end != '.')
        return false;
      cursor = end + 1;
    } else if (*end != '\0') {
      return false;
    }
  }

  return parts[0] == 10 || parts[0] == 127 || (parts[0] == 192 && parts[1] == 168) ||
         (parts[0] == 172 && parts[1] >= 16 && parts[1] <= 31) || (parts[0] == 169 && parts[1] == 254);
}

std::shared_ptr<http_request::HttpContainer> ArtworkImage::get_local_idf_(
    const std::string &url, const std::vector<http_request::Header> &headers) {
#ifdef USE_ESP_IDF
  bool secure = url.rfind("https://", 0) == 0;
  if (secure) {
    ESP_LOGW(TAG, "Using insecure TLS for local artwork URL: %s", url.c_str());
  } else {
    ESP_LOGD(TAG, "Using guarded local artwork request: %s", url.c_str());
  }
  if (this->local_http_cache_ == nullptr && !this->reserve_local_http_client(url)) {
    ESP_LOGE(TAG, "Local artwork request failed; client could not be initialized");
    return nullptr;
  }

  const uint32_t stage_start = millis();
  if (!this->local_http_cache_->prepare(url, secure, headers)) {
    ESP_LOGE(TAG, "Local artwork request failed; reusable client could not be prepared");
    return nullptr;
  }
  log_slow_artwork_stage("local-prepare", stage_start);
  return this->local_http_cache_;
#else
  return this->parent_->get(url, headers, {CONTENT_TYPE_HEADER_NAME});
#endif
}

size_t ArtworkImage::get_sane_content_length_() const {
  if (!this->downloader_) {
    return 0;
  }
  size_t content_length = this->downloader_->content_length;
  if (content_length > MAX_DOWNLOAD_BUFFER_SIZE) {
    ESP_LOGW(TAG, "Ignoring invalid artwork content length: %zu", content_length);
    return 0;
  }
  return content_length;
}

void ArtworkImage::loop() {
  this->cleanup_retired_buffers_(false);
#ifdef USE_SENDSPIN_ARTWORK
  // The SendSpin worker owns decoder_, decode_buffer_ and their hand-off while
  // a complete JPEG is being decoded. Treat that transaction like the HTTP
  // worker below: the component loop must not inspect or finish the same
  // decoder concurrently. That race could call finish_download_() halfway
  // through hardware decode, briefly expose a stale/empty image and leave the
  // worker without the buffer required for the scrim pass.
  if (this->sendspin_decode_busy_.load(std::memory_order_acquire)) {
    return;
  }
#endif
#if defined(USE_ESP_IDF) && defined(USE_ARTWORK_IMAGE_JPEG_SUPPORT)
  if (this->process_local_http_open_result_()) {
    return;
  }
  if (this->local_http_open_busy_.load(std::memory_order_acquire)) {
    return;
  }
  if (this->local_http_headers_busy_.load(std::memory_order_acquire) ||
      this->local_http_read_busy_.load(std::memory_order_acquire)) {
    return;
  }
  if (this->process_http_jpeg_decode_result_()) {
    return;
  }
  if (this->http_jpeg_decode_busy_.load(std::memory_order_acquire)) {
    return;
  }
  if (this->release_after_http_decode_) {
    this->local_http_headers_done_.store(false, std::memory_order_release);
    this->local_http_read_done_.store(false, std::memory_order_release);
    this->finish_deferred_release_();
    return;
  }
#endif
  if (!this->decoder_ && !this->downloader_) {
    if (this->retired_buffers_.empty()) {
      this->disable_loop();
    }
    return;
  }

#ifdef USE_ESP_IDF
  if (this->local_headers_ready_pending_start_) {
    // Header parsing and response setup both touch the network/PSRAM path.
    // Keep them in separate display frames instead of creating one bandwidth
    // spike that can starve the continuously scanned DSI framebuffer.
    if (millis() - this->local_headers_ready_ms_ < 20) {
      return;
    }
    this->local_headers_ready_pending_start_ = false;
    this->local_headers_ready_ms_ = 0;
    const uint32_t stage_start = millis();
    mark_display_stress("artwork-local-start-response", 600);
    const bool started = this->start_response_download_();
    if (!started) {
      return;
    }
    log_slow_artwork_stage("local-start-response", stage_start);
    return;
  }

  if (this->local_downloader_ != nullptr && !this->local_downloader_->headers_ready()) {
    if (this->http_jpeg_decode_task_ != nullptr) {
      int worker_result = 0;
      if (!this->process_local_http_headers_result_(worker_result)) {
        this->queue_local_http_headers_();
        return;
      }
      const auto result = static_cast<LocalHttpContainer::HeaderResult>(worker_result);
      if (result == LocalHttpContainer::HeaderResult::PENDING) {
        App.feed_wdt();
        if (millis() - this->last_data_millis_ > DOWNLOAD_STALL_TIMEOUT_MS) {
          ESP_LOGE(TAG, "Download stalled waiting for local artwork headers");
          this->fail_download_();
        }
        return;
      }
      if (result == LocalHttpContainer::HeaderResult::ERROR) {
        App.feed_wdt();
        this->fail_download_();
        return;
      }
      this->local_headers_ready_pending_start_ = true;
      this->local_headers_ready_ms_ = millis();
      App.feed_wdt();
      return;
    }
    const uint32_t stage_start = millis();
    mark_display_stress("artwork-local-headers-step", 1000);
    auto result = this->local_downloader_->fetch_headers_step();
    if (result == LocalHttpContainer::HeaderResult::PENDING) {
      log_slow_artwork_stage("local-fetch-headers-step", stage_start);
      App.feed_wdt();
      if (millis() - this->last_data_millis_ > DOWNLOAD_STALL_TIMEOUT_MS) {
        ESP_LOGE(TAG, "Download stalled waiting for local artwork headers");
        this->fail_download_();
      }
      return;
    }
    if (result == LocalHttpContainer::HeaderResult::ERROR) {
      log_slow_artwork_stage("local-fetch-headers-step", stage_start);
      App.feed_wdt();
      this->fail_download_();
      return;
    }
    this->local_headers_ready_pending_start_ = true;
    this->local_headers_ready_ms_ = millis();
    mark_display_stress("artwork-local-headers-ready", 1000);
    log_slow_artwork_stage("local-fetch-headers-ready", stage_start);
    App.feed_wdt();
    return;
  }
#endif

  // Deferred decoder creation for AUTO format: read data for magic-byte detection
  if (!this->decoder_ && this->downloader_) {
    if (!this->ensure_download_buffer_capacity_()) {
      this->fail_download_();
      return;
    }

    size_t available = std::min(this->download_buffer_.free_capacity(),
                                std::min(this->download_buffer_initial_size_, MAX_READ_CHUNK_SIZE));
    if (this->local_downloader_ != nullptr) {
      available = std::min<size_t>(available, CONFIG_ESPHOME_ARTWORK_LOCAL_HTTP_READ_CHUNK);
    }
    if (millis() - this->last_download_read_stress_ms_ >= ARTWORK_READ_STRESS_PERIOD_MS) {
      mark_display_stress("artwork-detect-read", 500);
      this->last_download_read_stress_ms_ = millis();
    }
    int len = 0;
#if defined(USE_ESP_IDF) && defined(USE_ARTWORK_IMAGE_JPEG_SUPPORT)
    if (this->local_downloader_ != nullptr && this->http_jpeg_decode_task_ != nullptr) {
      if (!this->process_local_http_read_result_(len)) {
        this->queue_local_http_read_(available);
        return;
      }
    } else
#endif
    {
      const uint32_t read_start = millis();
      len = this->downloader_->read(this->download_buffer_.append(), available);
      log_slow_artwork_stage("detect-read", read_start);
    }
    bool transfer_complete = false;
    if (len > 0) {
      this->download_buffer_.write(len);
      this->last_data_millis_ = millis();
    } else if (len < 0) {
      ESP_LOGE(TAG, "Download failed while detecting image format: %d", len);
      this->fail_download_();
      return;
    } else if (this->downloader_->is_read_complete()) {
      transfer_complete = true;
      if (this->download_buffer_.unread() < 12) {
        ESP_LOGE(TAG, "Download finished before enough data was received to detect image format");
        this->fail_download_();
        return;
      }
    }

    if (this->download_buffer_.unread() < 12) {
      if (millis() - this->last_data_millis_ > DOWNLOAD_STALL_TIMEOUT_MS) {
        ESP_LOGE(TAG, "Download stalled waiting for format detection bytes");
        this->end_connection_();
        this->download_error_callback_.call();
        this->start_pending_update_();
      }
      return;
    }

    ImageFormat resolved = this->detect_format_();
    if (resolved == ImageFormat::AUTO) {
      ESP_LOGE(TAG, "Could not determine image format from headers or file content");
      this->end_connection_();
      this->download_error_callback_.call();
      this->start_pending_update_();
      return;
    }

    size_t total_size = this->get_sane_content_length_();
    if (total_size == 0 && transfer_complete) {
      total_size = this->downloader_->get_bytes_read();
    }
    if (!this->create_decoder_(resolved, total_size)) {
      this->end_connection_();
      this->download_error_callback_.call();
      this->start_pending_update_();
      return;
    }
    this->log_state_("decoder-ready");
    ESP_LOGI(TAG, "Downloading image (Size: %zu)", total_size);

    // Feed already-buffered data to the newly created decoder
    if (!this->decode_buffered_data_()) {
      this->fail_download_();
      return;
    }
    if (this->decoder_->is_finished()) {
      this->finish_download_();
    }
    return;
  }

  if (this->decoder_->is_finished()) {
    this->finish_download_();
    return;
  }
  if (this->downloader_ == nullptr) {
    ESP_LOGE(TAG, "Downloader not instantiated; cannot download");
    return;
  }

  if (!this->ensure_download_buffer_capacity_()) {
    this->fail_download_();
    return;
  }

  size_t available = std::min(this->download_buffer_.free_capacity(),
                              std::min(this->download_buffer_initial_size_, MAX_READ_CHUNK_SIZE));
  if (this->local_downloader_ != nullptr) {
    available = std::min<size_t>(available, CONFIG_ESPHOME_ARTWORK_LOCAL_HTTP_READ_CHUNK);
  }
  if (millis() - this->last_download_read_stress_ms_ >= ARTWORK_READ_STRESS_PERIOD_MS) {
    mark_display_stress("artwork-http-read", 500);
    this->last_download_read_stress_ms_ = millis();
  }
  int len = 0;
#if defined(USE_ESP_IDF) && defined(USE_ARTWORK_IMAGE_JPEG_SUPPORT)
  if (this->local_downloader_ != nullptr && this->http_jpeg_decode_task_ != nullptr) {
    if (!this->process_local_http_read_result_(len)) {
      this->queue_local_http_read_(available);
      return;
    }
  } else
#endif
  {
    const uint32_t read_start = millis();
    len = this->downloader_->read(this->download_buffer_.append(), available);
    log_slow_artwork_stage("download-read", read_start);
  }
  if (len > 0) {
    this->download_buffer_.write(len);
    this->last_data_millis_ = millis();
    if (!this->decode_buffered_data_()) {
      this->fail_download_();
      return;
    }
    if (this->decoder_->is_finished()) {
      this->finish_download_();
    }
    return;
  }

  if (len < 0) {
    ESP_LOGE(TAG, "Download failed while reading image data: %d", len);
    this->fail_download_();
    return;
  }

  if (this->downloader_->is_read_complete()) {
    if (this->decoder_->has_unknown_download_size()) {
      this->decoder_->set_download_size(this->downloader_->get_bytes_read());
      ESP_LOGD(TAG, "HTTP transfer complete; inferred image size: %zu bytes", this->downloader_->get_bytes_read());
    }
    if (!this->decode_buffered_data_()) {
      this->fail_download_();
      return;
    }
    if (this->decoder_->is_finished()) {
      this->finish_download_();
      return;
    }
    ESP_LOGE(TAG, "HTTP transfer finished before image decoder completed");
    this->fail_download_();
    return;
  }

  if (millis() - this->last_data_millis_ > DOWNLOAD_STALL_TIMEOUT_MS) {
    ESP_LOGE(TAG, "Download stalled: no data received for %" PRIu32 "ms (buffered %zu bytes)",
             DOWNLOAD_STALL_TIMEOUT_MS, this->download_buffer_.unread());
    this->fail_download_();
    return;
  }
}

void ArtworkImage::map_chroma_key(Color &color) {
  if (this->transparency_ == image::TRANSPARENCY_CHROMA_KEY) {
    if (color.g == 1 && color.r == 0 && color.b == 0) {
      color.g = 0;
    }
    if (color.w < 0x80) {
      color.r = 0;
      color.g = this->type_ == ImageType::IMAGE_TYPE_RGB565 ? 4 : 1;
      color.b = 0;
    }
  }
}

void ArtworkImage::draw_pixel_(int x, int y, Color color) {
  if (!this->decode_buffer_) {
    ESP_LOGE(TAG, "Decode buffer not allocated!");
    return;
  }
  if (x < 0 || y < 0 || x >= this->decode_buffer_width_ || y >= this->decode_buffer_height_) {
    ESP_LOGE(TAG, "Tried to paint a pixel (%d,%d) outside the image!", x, y);
    return;
  }
  uint32_t pos = this->get_position_(x, y);
  switch (this->type_) {
    case ImageType::IMAGE_TYPE_BINARY: {
      const uint32_t width_8 = ((this->decode_buffer_width_ + 7u) / 8u) * 8u;
      pos = x + y * width_8;
      auto bitno = 0x80 >> (pos % 8u);
      pos /= 8u;
      auto on = is_color_on(color);
      if (this->has_transparency() && color.w < 0x80)
        on = false;
      if (on) {
        this->decode_buffer_[pos] |= bitno;
      } else {
        this->decode_buffer_[pos] &= ~bitno;
      }
      break;
    }
    case ImageType::IMAGE_TYPE_GRAYSCALE: {
      auto gray = static_cast<uint8_t>(0.2125 * color.r + 0.7154 * color.g + 0.0721 * color.b);
      if (this->transparency_ == image::TRANSPARENCY_CHROMA_KEY) {
        if (gray == 1) {
          gray = 0;
        }
        if (color.w < 0x80) {
          gray = 1;
        }
      } else if (this->transparency_ == image::TRANSPARENCY_ALPHA_CHANNEL) {
        if (color.w != 0xFF)
          gray = color.w;
      }
      this->decode_buffer_[pos] = gray;
      break;
    }
    case ImageType::IMAGE_TYPE_RGB565: {
      this->map_chroma_key(color);
      uint16_t col565 = display::ColorUtil::color_to_565(color);
      if (this->is_big_endian_) {
        this->decode_buffer_[pos + 0] = static_cast<uint8_t>((col565 >> 8) & 0xFF);
        this->decode_buffer_[pos + 1] = static_cast<uint8_t>(col565 & 0xFF);
      } else {
        this->decode_buffer_[pos + 0] = static_cast<uint8_t>(col565 & 0xFF);
        this->decode_buffer_[pos + 1] = static_cast<uint8_t>((col565 >> 8) & 0xFF);
      }
      if (this->transparency_ == image::TRANSPARENCY_ALPHA_CHANNEL) {
        this->decode_buffer_[pos + 2] = color.w;
      }
      break;
    }
    case ImageType::IMAGE_TYPE_RGB: {
      this->map_chroma_key(color);
      this->decode_buffer_[pos + 0] = color.b;
      this->decode_buffer_[pos + 1] = color.g;
      this->decode_buffer_[pos + 2] = color.r;
      if (this->transparency_ == image::TRANSPARENCY_ALPHA_CHANNEL) {
        this->decode_buffer_[pos + 3] = color.w;
      }
      break;
    }
  }
}

ImageFormat ArtworkImage::detect_format_() {
  if (this->format_ != ImageFormat::AUTO) {
    return this->format_;
  }

  // Prefer magic bytes because Home Assistant proxy headers can be stale for
  // identical media_player_proxy paths whose cache parameter points at new art.
  if (this->download_buffer_.unread() >= 4) {
    const uint8_t *data = this->download_buffer_.data();
    if (data[0] == 0xFF && data[1] == 0xD8) {
      if (this->detect_progressive_jpeg_()) {
        ESP_LOGW(TAG, "Detected progressive JPEG from magic bytes: %s", this->url_.c_str());
      } else {
        ESP_LOGD(TAG, "Detected JPEG from magic bytes; decoder will report baseline/progressive from the header");
      }
      return ImageFormat::JPEG;
    }
    if (data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47) {
      ESP_LOGD(TAG, "Detected PNG from magic bytes");
      return ImageFormat::PNG;
    }
    if (this->detect_heic_()) {
      ESP_LOGW(TAG, "Detected HEIC/HEIF from file signature");
      return ImageFormat::HEIC;
    }
    if (data[0] == 0x42 && data[1] == 0x4D) {
      ESP_LOGD(TAG, "Detected BMP from magic bytes");
      return ImageFormat::BMP;
    }
  }

  // Fallback: Content-Type header
  if (this->downloader_) {
    std::string ct = str_lower_case(this->downloader_->get_response_header(CONTENT_TYPE_HEADER_NAME));
    if (ct.find("image/jpeg") != std::string::npos || ct.find("image/jpg") != std::string::npos) {
      ESP_LOGD(TAG, "Detected JPEG from Content-Type: %s", ct.c_str());
      return ImageFormat::JPEG;
    }
    if (ct.find("image/png") != std::string::npos) {
      ESP_LOGD(TAG, "Detected PNG from Content-Type: %s", ct.c_str());
      return ImageFormat::PNG;
    }
    if (ct.find("image/heic") != std::string::npos || ct.find("image/heif") != std::string::npos) {
      ESP_LOGW(TAG, "Detected HEIC/HEIF from Content-Type: %s", ct.c_str());
      return ImageFormat::HEIC;
    }
    if (ct.find("image/bmp") != std::string::npos) {
      ESP_LOGD(TAG, "Detected BMP from Content-Type: %s", ct.c_str());
      return ImageFormat::BMP;
    }
  }

  return ImageFormat::AUTO;
}

bool ArtworkImage::detect_progressive_jpeg_() {
  size_t len = this->download_buffer_.unread();
  const uint8_t *data = this->download_buffer_.data();
  if (len < 4 || data[0] != 0xFF || data[1] != 0xD8) {
    return false;
  }

  size_t pos = 2;
  while (pos + 3 < len) {
    while (pos < len && data[pos] != 0xFF)
      pos++;
    while (pos < len && data[pos] == 0xFF)
      pos++;
    if (pos >= len)
      break;

    uint8_t marker = data[pos++];
    if (marker == 0xDA || marker == 0xD9) {
      break;
    }
    if (marker >= 0xD0 && marker <= 0xD7) {
      continue;
    }
    if (pos + 1 >= len)
      break;
    uint16_t segment_len = (static_cast<uint16_t>(data[pos]) << 8) | data[pos + 1];
    if (segment_len < 2)
      break;

    if (marker == 0xC2) {
      return true;
    }
    if (marker == 0xC0) {
      return false;
    }
    pos += segment_len;
  }
  return false;
}

bool ArtworkImage::detect_heic_() {
  size_t len = this->download_buffer_.unread();
  const uint8_t *data = this->download_buffer_.data();
  if (len < 12) {
    return false;
  }
  if (data[4] != 'f' || data[5] != 't' || data[6] != 'y' || data[7] != 'p') {
    return false;
  }

  for (size_t pos = 8; pos + 3 < len && pos < 64; pos += 4) {
    if ((data[pos] == 'h' && data[pos + 1] == 'e' && data[pos + 2] == 'i' &&
         (data[pos + 3] == 'c' || data[pos + 3] == 'x')) ||
        (data[pos] == 'h' && data[pos + 1] == 'e' && data[pos + 2] == 'v' &&
         (data[pos + 3] == 'c' || data[pos + 3] == 'x')) ||
        (data[pos] == 'm' && data[pos + 1] == 'i' && data[pos + 2] == 'f' && data[pos + 3] == '1') ||
        (data[pos] == 'm' && data[pos + 1] == 's' && data[pos + 2] == 'f' && data[pos + 3] == '1')) {
      return true;
    }
  }
  return false;
}

bool ArtworkImage::create_decoder_(ImageFormat format, size_t total_size) {
  this->active_format_ = ImageFormat::AUTO;
  if (format == ImageFormat::HEIC) {
    ESP_LOGE(TAG, "HEIC/HEIF artwork detected, but no native HEIC decoder is bundled for this firmware");
    return false;
  }
#ifdef USE_ARTWORK_IMAGE_BMP_SUPPORT
  if (format == ImageFormat::BMP) {
    ESP_LOGD(TAG, "Allocating BMP decoder");
    this->decoder_ = make_unique<BmpDecoder>(this);
  }
#endif
#ifdef USE_ARTWORK_IMAGE_JPEG_SUPPORT
  if (format == ImageFormat::JPEG) {
    ESP_LOGD(TAG, "Allocating JPEG decoder");
    this->decoder_ = esphome::make_unique<JpegDecoder>(this);
  }
#endif
#ifdef USE_ARTWORK_IMAGE_PNG_SUPPORT
  if (format == ImageFormat::PNG) {
    ESP_LOGD(TAG, "Allocating PNG decoder");
    this->decoder_ = make_unique<PngDecoder>(this);
  }
#endif
  if (!this->decoder_) {
    ESP_LOGE(TAG, "Could not instantiate decoder. Image format unsupported: %d", format);
    return false;
  }
  if (this->decoder_->prepare(total_size) < 0) {
    this->decoder_.reset();
    return false;
  }
  this->active_format_ = format;
  return true;
}

void ArtworkImage::discard_decode_buffer_() {
  if (this->decode_buffer_) {
    if (!this->decode_buffer_reuses_active_) {
      const size_t size = this->decode_buffer_capacity_;
      if (this->spare_buffer_ == nullptr && should_keep_spare_buffer(size, this->decode_buffer_uses_jpeg_allocator_)) {
        this->spare_buffer_ = this->decode_buffer_;
        this->spare_buffer_size_ = size;
        this->spare_buffer_uses_jpeg_allocator_ = this->decode_buffer_uses_jpeg_allocator_;
      } else {
        this->release_buffer_(this->decode_buffer_, size, this->decode_buffer_uses_jpeg_allocator_);
      }
    }
    this->decode_buffer_ = nullptr;
  }
  this->decode_buffer_capacity_ = 0;
  this->decode_buffer_reuses_active_ = false;
  this->decode_buffer_uses_jpeg_allocator_ = false;
  this->decode_buffer_width_ = 0;
  this->decode_buffer_height_ = 0;
  this->decode_content_width_ = 0;
  this->decode_content_height_ = 0;
  this->decode_offset_x_ = 0;
  this->decode_offset_y_ = 0;
  this->decode_buffer_written_by_dma_ = false;
  this->decode_buffer_darkened_percent_ = 0;
  this->decode_buffer_scrim_applied_ = false;
}

void ArtworkImage::release_spare_buffer_() {
  if (this->spare_buffer_ != nullptr) {
    this->release_buffer_(this->spare_buffer_, this->spare_buffer_size_, this->spare_buffer_uses_jpeg_allocator_);
    this->spare_buffer_ = nullptr;
    this->spare_buffer_size_ = 0;
    this->spare_buffer_uses_jpeg_allocator_ = false;
  }
}

void ArtworkImage::release_buffer_(uint8_t *buffer, size_t size, bool jpeg_allocator) {
  if (buffer == nullptr) {
    return;
  }
  register_dma_written_buffer(buffer, size, false);
#ifdef USE_ESP32_JPEG
  if (jpeg_allocator) {
    esp32_jpeg::release_decode_output(buffer);
    return;
  }
#else
  (void) jpeg_allocator;
#endif
  this->allocator_.deallocate(buffer, size);
}

bool ArtworkImage::promote_decode_buffer_() {
  if (!this->decode_buffer_) {
    ESP_LOGE(TAG, "Decode finished without a decoded image buffer");
    return false;
  }
  if (this->decode_buffer_width_ <= 0 || this->decode_buffer_height_ <= 0) {
    ESP_LOGE(TAG, "Decode finished with invalid dimensions: %dx%d", this->decode_buffer_width_,
             this->decode_buffer_height_);
    return false;
  }

  const bool reused_active_buffer = this->decode_buffer_reuses_active_;
  const bool written_by_dma = this->decode_buffer_written_by_dma_;
  const bool jpeg_allocator = this->decode_buffer_uses_jpeg_allocator_;
  const size_t decode_capacity = this->decode_buffer_capacity_;
  const uint8_t decode_buffer_darkened_percent = this->decode_buffer_darkened_percent_;
  mark_display_stress("artwork-promote", 1500);
  if (!reused_active_buffer) {
    this->retire_active_buffer_();
  }
  this->buffer_ = this->decode_buffer_;
  this->buffer_capacity_ = decode_capacity;
  this->buffer_uses_jpeg_allocator_ = jpeg_allocator;
  this->buffer_width_ = this->decode_buffer_width_;
  this->buffer_height_ = this->decode_buffer_height_;
  this->buffer_content_width_ = this->decode_content_width_;
  this->buffer_content_height_ = this->decode_content_height_;
  this->buffer_offset_x_ = this->decode_offset_x_;
  this->buffer_offset_y_ = this->decode_offset_y_;
  this->darkened_buffer_ = nullptr;
  this->darkened_percent_ = 0;
  if constexpr (CONFIG_ESPHOME_ARTWORK_TRACE_VERBOSE) {
    ESP_LOGW(TAG, "Artwork buffer ready: image=%dx%d content=%dx%d offset=%d,%d", this->buffer_width_,
             this->buffer_height_, this->buffer_content_width_, this->buffer_content_height_, this->buffer_offset_x_,
             this->buffer_offset_y_);
  }
  this->decode_buffer_ = nullptr;
  this->decode_buffer_capacity_ = 0;
  this->decode_buffer_reuses_active_ = false;
  this->decode_buffer_uses_jpeg_allocator_ = false;
  this->decode_buffer_width_ = 0;
  this->decode_buffer_height_ = 0;
  this->decode_content_width_ = 0;
  this->decode_content_height_ = 0;
  this->decode_offset_x_ = 0;
  this->decode_offset_y_ = 0;
  this->decode_buffer_written_by_dma_ = false;
  this->decode_buffer_darkened_percent_ = 0;
  this->decode_buffer_scrim_applied_ = false;

  this->data_start_ = this->buffer_;
  this->width_ = this->buffer_width_;
  this->height_ = this->buffer_height_;
  const uint32_t sync_start = millis();
  sync_artwork_buffer_for_dma(this->buffer_, this->get_buffer_size_(), written_by_dma);
  log_slow_artwork_stage(written_by_dma ? "finish-dma-cache-sync" : "finish-cache-sync", sync_start);
  if (decode_buffer_darkened_percent == this->darken_percent_ && this->darken_percent_ > 0) {
    this->darkened_buffer_ = this->buffer_;
    this->darkened_percent_ = this->darken_percent_;
  } else {
    this->apply_rgb_darken_once(this->darken_percent_);
  }
  register_dma_written_buffer(this->buffer_, this->get_buffer_size_(),
                              written_by_dma && this->darkened_buffer_ != this->buffer_);
#ifdef USE_LVGL
  this->prepare_lvgl_dsc_();
#endif
  return true;
}

void ArtworkImage::retire_active_buffer_() {
  if (!this->buffer_) {
    return;
  }
  auto *retired = this->buffer_;
  this->retired_buffers_.push_back(
      RetiredBuffer{retired, this->buffer_capacity_, millis(), this->buffer_uses_jpeg_allocator_});
  this->buffer_ = nullptr;
  this->buffer_capacity_ = 0;
  this->buffer_uses_jpeg_allocator_ = false;
  this->data_start_ = nullptr;
  this->buffer_width_ = 0;
  this->buffer_height_ = 0;
  this->buffer_content_width_ = 0;
  this->buffer_content_height_ = 0;
  this->buffer_offset_x_ = 0;
  this->buffer_offset_y_ = 0;
  if (this->darkened_buffer_ == retired) {
    this->darkened_buffer_ = nullptr;
    this->darkened_percent_ = 0;
  }
  this->width_ = 0;
  this->height_ = 0;
  this->enable_loop();
}

void ArtworkImage::cleanup_retired_buffers_(bool force) {
  uint32_t now = millis();
  auto it = this->retired_buffers_.begin();
  while (it != this->retired_buffers_.end()) {
    if (force || now - it->retired_at >= RETIRED_BUFFER_GRACE_MS ||
        this->retired_buffers_.size() > MAX_RETIRED_BUFFERS) {
      if (!force && this->spare_buffer_ == nullptr && should_keep_spare_buffer(it->size, it->jpeg_allocator)) {
        this->spare_buffer_ = it->data;
        this->spare_buffer_size_ = it->size;
        this->spare_buffer_uses_jpeg_allocator_ = it->jpeg_allocator;
      } else {
        this->release_buffer_(it->data, it->size, it->jpeg_allocator);
      }
      it = this->retired_buffers_.erase(it);
    } else {
      ++it;
    }
  }
  if (force) {
    this->release_spare_buffer_();
  }
}

bool ArtworkImage::ensure_download_buffer_capacity_() {
  if (this->download_buffer_.free_capacity() > 0) {
    return true;
  }

  size_t current_size = this->download_buffer_.size();
  size_t target_size = current_size == 0 ? this->download_buffer_initial_size_ : current_size * 2;
  if (target_size > MAX_DOWNLOAD_BUFFER_SIZE) {
    target_size = MAX_DOWNLOAD_BUFFER_SIZE;
  }
  if (target_size <= current_size) {
    ESP_LOGE(TAG, "Artwork download exceeded %zu bytes", MAX_DOWNLOAD_BUFFER_SIZE);
    return false;
  }

  ESP_LOGD(TAG, "Growing download buffer from %zu to %zu bytes", current_size, target_size);
  mark_display_stress("artwork-download-resize", 1000);
  return this->download_buffer_.resize(target_size) == target_size;
}

bool ArtworkImage::decode_encoded_image_(ImageFormat format, const uint8_t *data, size_t length,
                                         bool finish_on_decode) {
  this->trace_event_("decode-encoded-start", length);
  if (data == nullptr || length == 0) {
    ESP_LOGE(TAG, "Sendspin artwork image is empty");
    return false;
  }
  if (length > MAX_DOWNLOAD_BUFFER_SIZE) {
    ESP_LOGE(TAG, "Sendspin artwork image too large: %zu bytes (max %zu)", length, MAX_DOWNLOAD_BUFFER_SIZE);
    return false;
  }

  const uint32_t start = millis();
  this->end_connection_();
  this->trace_event_("decode-after-end-connection", length);

#ifdef USE_ARTWORK_IMAGE_JPEG_SUPPORT
  if (format == ImageFormat::JPEG) {
    // SendSpin already delivers a complete encoded image. Avoid copying it
    // into DownloadBuffer before the JPEG decoder copies it into its DMA input
    // buffer. This keeps artwork changes from doing one extra PSRAM pass.
    this->download_buffer_.reset();
    this->decoder_ = esphome::make_unique<JpegDecoder>(this);
    this->decoder_->set_download_size(length);
    const int fed = this->decoder_->decode(const_cast<uint8_t *>(data), length);
    log_slow_artwork_stage("sendspin-jpeg-direct-decode", start);
    this->trace_event_("decode-direct-jpeg-end", static_cast<size_t>(std::max(fed, 0)));
    if (fed < 0) {
      ESP_LOGE(TAG, "Error when decoding JPEG artwork.");
      this->end_connection_();
      return false;
    }
    if (static_cast<size_t>(fed) > length || !this->decoder_->is_finished()) {
      ESP_LOGE(TAG, "JPEG artwork decoder did not finish after %zu bytes", length);
      this->end_connection_();
      return false;
    }
    if (!this->apply_decode_buffer_scrim_()) {
      this->end_connection_();
      return false;
    }
    this->start_time_ = ::time(nullptr);
    if (finish_on_decode) {
      this->finish_download_();
    } else {
#ifdef USE_SENDSPIN_ARTWORK
      this->decoder_.reset();
      this->sendspin_decode_ready_.store(true, std::memory_order_release);
      this->trace_event_("decode-ready-deferred", length);
#else
      this->finish_download_();
#endif
    }
    this->trace_event_("decode-encoded-end", length);
    return true;
  }
#endif

  this->download_buffer_.reset();
  if (this->download_buffer_.resize(length) < length) {
    ESP_LOGE(TAG, "Sendspin artwork buffer resize failed: %zu bytes", length);
    return false;
  }
  memcpy(this->download_buffer_.append(), data, length);
  this->download_buffer_.write(length);
  this->trace_event_("decode-buffer-filled", length);

  if (!this->create_decoder_(format, length)) {
    this->end_connection_();
    return false;
  }
  if (!this->decode_buffered_data_()) {
    this->end_connection_();
    return false;
  }
  if (!this->decoder_->is_finished()) {
    ESP_LOGE(TAG, "Sendspin artwork decoder did not finish after %zu bytes", length);
    this->end_connection_();
    return false;
  }
  this->trace_event_("decode-complete", length);
  if (this->decode_buffer_written_by_dma_) {
    wait_for_display_quiet("post-decode", CONFIG_ESPHOME_ARTWORK_POST_DECODE_DSI_QUIET_MS);
  }
  if (!this->apply_decode_buffer_scrim_()) {
    this->end_connection_();
    return false;
  }

  this->start_time_ = ::time(nullptr);
  if (finish_on_decode) {
    this->finish_download_();
  } else {
#ifdef USE_SENDSPIN_ARTWORK
    this->decoder_.reset();
    this->download_buffer_.reset();
    this->sendspin_decode_ready_.store(true, std::memory_order_release);
    this->trace_event_("decode-ready-deferred", length);
#else
    this->finish_download_();
#endif
  }
  log_slow_artwork_stage("sendspin-decode", start);
  this->trace_event_("decode-encoded-end", length);
  return true;
}

bool ArtworkImage::decode_buffered_data_() {
  if (!this->decoder_ || this->download_buffer_.unread() == 0) {
    return true;
  }

#if defined(USE_ESP_IDF) && defined(USE_ARTWORK_IMAGE_JPEG_SUPPORT)
  if (this->active_format_ == ImageFormat::JPEG && this->hardware_jpeg_ &&
      this->http_jpeg_decode_task_ != nullptr) {
    if (this->decoder_->has_unknown_download_size() ||
        this->download_buffer_.unread() < this->decoder_->get_download_size()) {
      return true;
    }
    return this->queue_http_jpeg_decode_();
  }
#endif

  size_t unread = this->download_buffer_.unread();
  const uint32_t start = millis();
  this->trace_event_("decode-buffered-start", unread);
  auto fed = this->decoder_->decode(this->download_buffer_.data(), unread);
  log_slow_artwork_stage("decode-buffered", start);
  this->trace_event_("decode-buffered-end", static_cast<size_t>(std::max(fed, 0)));
  if (fed < 0) {
    ESP_LOGE(TAG, "Error when decoding image.");
    return false;
  }
  if (static_cast<size_t>(fed) > unread) {
    ESP_LOGE(TAG, "Decoder consumed %d bytes, but only %zu were buffered", fed, unread);
    return false;
  }
  this->download_buffer_.read(fed);
  return true;
}

void ArtworkImage::finish_download_() {
  this->trace_event_("finish-start");
  mark_display_stress("artwork-finish", 2000);
  if (!this->apply_decode_buffer_scrim_()) {
    this->fail_download_();
    return;
  }
  uint32_t stage_start = millis();
  if (!this->promote_decode_buffer_()) {
    this->fail_download_();
    return;
  }
  log_slow_artwork_stage("finish-promote", stage_start);
  this->trace_event_("finish-promote");
  if constexpr (CONFIG_ESPHOME_ARTWORK_TRACE_VERBOSE) {
    this->log_state_("download-complete");
  }
  ESP_LOGD(TAG, "Image fully downloaded, read %zu bytes, width/height = %d/%d",
           this->downloader_ ? this->downloader_->get_bytes_read() : 0, this->width_, this->height_);
  ESP_LOGD(TAG, "Total time: %" PRIu32 "s", (uint32_t) (::time(nullptr) - this->start_time_));
  App.feed_wdt();
  stage_start = millis();
#ifdef USE_LVGL
#if ESPHOME_VERSION_CODE >= VERSION_CODE(2026, 4, 0)
  this->get_lv_image_dsc();
#else
  this->get_lv_img_dsc();
#endif
#endif
  log_slow_artwork_stage("finish-lvgl-descriptor", stage_start);
  this->trace_event_("finish-lvgl-descriptor");
  if constexpr (CONFIG_ESPHOME_ARTWORK_TRACE_VERBOSE) {
    this->log_state_("lvgl-descriptor-ready");
  }
  App.feed_wdt();
  stage_start = millis();
  this->end_connection_();
  log_slow_artwork_stage("finish-end-connection", stage_start);
  this->trace_event_("finish-end-connection");
  this->defer([this]() {
    uint32_t stage_start = millis();
    this->trace_event_("finish-callback-start");
    mark_display_stress("artwork-lvgl-callback", 2000);
    this->download_finished_callback_.call(false);
    log_slow_artwork_stage("finish-callback", stage_start);
    this->trace_event_("finish-callback-end");
    App.feed_wdt();
    if constexpr (CONFIG_ESPHOME_ARTWORK_TRACE_VERBOSE) {
      this->log_state_("download-callback-finished");
    }
    stage_start = millis();
    this->start_pending_update_();
    log_slow_artwork_stage("finish-start-pending", stage_start);
#ifdef USE_SENDSPIN_ARTWORK
    bool sendspin_pending = false;
    {
      LockGuard guard(this->sendspin_pending_lock_);
      sendspin_pending =
          this->pending_sendspin_image_ || this->pending_sendspin_display_ || this->pending_sendspin_clear_;
    }
    if (sendspin_pending) {
      this->trace_event_("finish-queue-pending-sendspin");
      this->queue_sendspin_process_();
    }
#endif
  });
}

void ArtworkImage::fail_download_() {
  this->end_connection_();
  this->defer([this]() {
    this->download_error_callback_.call();
    this->start_pending_update_();
#ifdef USE_SENDSPIN_ARTWORK
    bool sendspin_pending = false;
    {
      LockGuard guard(this->sendspin_pending_lock_);
      sendspin_pending =
          this->pending_sendspin_image_ || this->pending_sendspin_display_ || this->pending_sendspin_clear_;
    }
    if (sendspin_pending) {
      this->trace_event_("fail-queue-pending-sendspin");
      this->queue_sendspin_process_();
    }
#endif
  });
}

void ArtworkImage::queue_pending_update_(const std::string &url) {
  if (!this->validate_url_(url)) {
    return;
  }
  bool replaced = this->update_pending_ && this->pending_url_ != url;
  this->pending_url_ = url;
  this->update_pending_ = true;
  ESP_LOGW(TAG, "Artwork update %s while busy; latest URL will run after current work finishes",
           replaced ? "re-queued" : "queued");
  this->log_state_("update-queued");
}

void ArtworkImage::start_pending_update_() {
  if (!this->update_pending_ || this->is_busy_()) {
    return;
  }
  std::string url = this->pending_url_;
  this->pending_url_.clear();
  this->update_pending_ = false;
  ESP_LOGI(TAG, "Starting queued artwork update");
  this->url_ = url;
  this->update();
}

void ArtworkImage::log_state_(const char *stage) {
  size_t heap_free = 0;
  size_t heap_largest = this->allocator_.get_max_free_block_size();
#ifdef USE_ESP32
  heap_free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  heap_largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
#endif
  size_t bytes_read = this->downloader_ ? this->downloader_->get_bytes_read() : 0;
  size_t content_length = this->downloader_ ? this->downloader_->content_length : 0;
  ESP_LOGD(TAG,
           "State %-24s url_len=%zu http=%zu/%zu dl_buf=%zu/%zu image=%dx%d content=%dx%d@%d,%d decode=%dx%d "
           "content=%dx%d@%d,%d retired=%zu heap_free=%zu heap_largest=%zu pending=%s",
           stage, this->url_.size(), bytes_read, content_length, this->download_buffer_.unread(),
           this->download_buffer_.size(), this->buffer_width_, this->buffer_height_, this->buffer_content_width_,
           this->buffer_content_height_, this->buffer_offset_x_, this->buffer_offset_y_, this->decode_buffer_width_,
           this->decode_buffer_height_, this->decode_content_width_, this->decode_content_height_,
           this->decode_offset_x_, this->decode_offset_y_, this->retired_buffers_.size(), heap_free, heap_largest,
           this->update_pending_ ? "yes" : "no");
}

void ArtworkImage::end_connection_() {
  if (this->downloader_) {
    this->downloader_->end();
    this->downloader_ = nullptr;
  }
#ifdef USE_ESP_IDF
  this->local_downloader_ = nullptr;
  this->local_headers_ready_pending_start_ = false;
  this->local_headers_ready_ms_ = 0;
#if defined(USE_ARTWORK_IMAGE_JPEG_SUPPORT)
  this->local_http_open_done_.store(false, std::memory_order_release);
  this->local_http_headers_done_.store(false, std::memory_order_release);
  this->local_http_read_done_.store(false, std::memory_order_release);
  this->local_http_read_size_ = 0;
#endif
#endif
  this->decoder_.reset();
  this->active_format_ = ImageFormat::AUTO;
  this->discard_decode_buffer_();
  this->download_buffer_.reset();
  if (!this->hardware_jpeg_) {
    this->download_buffer_.shrink(std::min<size_t>(this->download_buffer_initial_size_, DOWNLOAD_BUFFER_BASE_SIZE));
  }
}

bool ArtworkImage::validate_url_(const std::string &url) {
  if ((url.length() < 8) || !url.starts_with("http") || (url.find("://") == std::string::npos)) {
    ESP_LOGE(TAG, "URL is invalid and/or must be prefixed with 'http://' or 'https://'");
    return false;
  }
  return true;
}

}  // namespace artwork_image
}  // namespace esphome
