#include "esp32_jpeg.h"

#ifdef USE_ESP32_JPEG

#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <cstring>
#include <utility>

#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_memory_utils.h"
#include "esp_private/esp_cache_private.h"
#include "sdkconfig.h"
#include "esphome/core/defines.h"
#include "esphome/core/log.h"
#include "jpeg_huffman_normalizer.h"

#if defined(SOC_JPEG_CODEC_SUPPORTED) && SOC_JPEG_CODEC_SUPPORTED
#include "driver/jpeg_decode.h"
#include "driver/jpeg_encode.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#include "hal/axi_icm_ll.h"
#endif
#endif

#if defined(SOC_JPEG_CODEC_SUPPORTED) && SOC_JPEG_CODEC_SUPPORTED
static volatile bool g_esphome_esp32_jpeg_skip_output_cache_msync = false;
static volatile uint32_t g_esphome_esp32_jpeg_last_status = 0;
#ifndef CONFIG_ESPHOME_JPEG_DMA2D_BURST_LENGTH
#define CONFIG_ESPHOME_JPEG_DMA2D_BURST_LENGTH 128
#endif
#ifdef CONFIG_ESPHOME_JPEG_DMA2D_DESC_BURST_DISABLE
static std::atomic<bool> g_esphome_esp32_jpeg_dma2d_desc_burst{false};
#else
static std::atomic<bool> g_esphome_esp32_jpeg_dma2d_desc_burst{true};
#endif
static std::atomic<uint16_t> g_esphome_esp32_jpeg_dma2d_burst_length{CONFIG_ESPHOME_JPEG_DMA2D_BURST_LENGTH};

#ifndef CONFIG_ESPHOME_JPEG_ENCODER_DMA2D_BURST_LENGTH
#define CONFIG_ESPHOME_JPEG_ENCODER_DMA2D_BURST_LENGTH 128
#endif
#ifndef CONFIG_ESPHOME_JPEG_ENCODER_DMA2D_BAND_HEIGHT
#define CONFIG_ESPHOME_JPEG_ENCODER_DMA2D_BAND_HEIGHT 16
#endif
#ifdef CONFIG_ESPHOME_JPEG_ENCODER_DMA2D_DESC_BURST_DISABLE
static std::atomic<bool> g_esphome_esp32_jpeg_encoder_dma2d_desc_burst{false};
#else
static std::atomic<bool> g_esphome_esp32_jpeg_encoder_dma2d_desc_burst{true};
#endif
static std::atomic<uint16_t> g_esphome_esp32_jpeg_encoder_dma2d_burst_length{
    CONFIG_ESPHOME_JPEG_ENCODER_DMA2D_BURST_LENGTH};

extern "C" bool esphome_esp32_jpeg_skip_output_cache_msync(void) {
  return g_esphome_esp32_jpeg_skip_output_cache_msync;
}

extern "C" void esphome_esp32_jpeg_report_status(uint32_t status) { g_esphome_esp32_jpeg_last_status = status; }

extern "C" int esphome_esp32_jpeg_dma2d_burst_length(void) {
  return g_esphome_esp32_jpeg_dma2d_burst_length.load(std::memory_order_relaxed);
}

extern "C" bool esphome_esp32_jpeg_dma2d_desc_burst_enabled(void) {
  return g_esphome_esp32_jpeg_dma2d_desc_burst.load(std::memory_order_relaxed);
}

extern "C" int esphome_esp32_jpeg_encoder_dma2d_burst_length(void) {
  return g_esphome_esp32_jpeg_encoder_dma2d_burst_length.load(std::memory_order_relaxed);
}

extern "C" bool esphome_esp32_jpeg_encoder_dma2d_desc_burst_enabled(void) {
  return g_esphome_esp32_jpeg_encoder_dma2d_desc_burst.load(std::memory_order_relaxed);
}

extern "C" int esphome_esp32_jpeg_encoder_dma2d_band_height(void) {
  return CONFIG_ESPHOME_JPEG_ENCODER_DMA2D_BAND_HEIGHT;
}
#endif

namespace esphome::esp32_jpeg {
namespace {

static const char *const TAG = "esp32_jpeg";

#ifndef CONFIG_ESPHOME_JPEG_BUFFER_DIAGNOSTICS
#define CONFIG_ESPHOME_JPEG_BUFFER_DIAGNOSTICS 0
#endif

void log_decode_buffer_probe_(const char *stage, uint8_t *buffer, size_t size) {
  if constexpr (!CONFIG_ESPHOME_JPEG_BUFFER_DIAGNOSTICS) {
    (void) stage;
    (void) buffer;
    (void) size;
    return;
  }
  if (buffer == nullptr || size == 0) {
    ESP_LOGW(TAG, "JPEG buffer probe %s: empty", stage);
    return;
  }

  esp_err_t sync_err = ESP_OK;
  if (esp_ptr_external_ram(buffer)) {
    constexpr uintptr_t alignment = 64;
    const uintptr_t start = reinterpret_cast<uintptr_t>(buffer);
    const uintptr_t aligned_start = start & ~(alignment - 1U);
    const uintptr_t aligned_end = (start + size + alignment - 1U) & ~(alignment - 1U);
    sync_err = esp_cache_msync(reinterpret_cast<void *>(aligned_start), aligned_end - aligned_start,
                               ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);
  }

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
  ESP_LOGW(TAG, "JPEG buffer probe %s: ptr=%p size=%zu step=%zu samples=%zu nonzero=%zu hash=%08" PRIX32 " sync=%s",
           stage, buffer, size, step, samples, nonzero, hash, esp_err_to_name(sync_err));
}

uint32_t align_up(uint32_t value, uint32_t alignment) { return (value + alignment - 1) / alignment * alignment; }

esp_err_t prepare_hardware_input_(const uint8_t *jpeg, size_t jpeg_size, JpegBuffer *normalized,
                                   const uint8_t **effective_jpeg, size_t *effective_size) {
  bool was_normalized = false;
  esp_err_t err = normalize_huffman_for_hardware(jpeg, jpeg_size, normalized, &was_normalized);
  if (err != ESP_OK)
    return err;
  if (was_normalized) {
    *effective_jpeg = normalized->data();
    *effective_size = normalized->size();
    ESP_LOGD(TAG, "Normalized JPEG Huffman stream for ESP32-P4 hardware decode: %zu -> %zu bytes", jpeg_size,
             normalized->size());
  } else {
    *effective_jpeg = jpeg;
    *effective_size = jpeg_size;
  }
  return ESP_OK;
}

esp_err_t align_hardware_decode_dimensions_(JpegBuffer *owned_input, const uint8_t **jpeg, size_t *jpeg_size) {
  if (owned_input == nullptr || jpeg == nullptr || *jpeg == nullptr || jpeg_size == nullptr || *jpeg_size < 4)
    return ESP_ERR_INVALID_ARG;

  const uint8_t *source = *jpeg;
  if (source[0] != 0xFF || source[1] != 0xD8)
    return ESP_ERR_INVALID_ARG;

  size_t position = 2;
  while (position + 4 <= *jpeg_size) {
    while (position < *jpeg_size && source[position] == 0xFF)
      position++;
    if (position >= *jpeg_size)
      break;

    const uint8_t marker = source[position++];
    if (marker == 0xD9 || marker == 0xDA)
      break;
    if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7))
      continue;
    if (position + 2 > *jpeg_size)
      return ESP_ERR_INVALID_ARG;

    const size_t segment_start = position - 2;
    const uint16_t segment_size = (static_cast<uint16_t>(source[position]) << 8) | source[position + 1];
    if (segment_size < 2 || position + segment_size > *jpeg_size)
      return ESP_ERR_INVALID_ARG;

    if (marker == 0xC0 || marker == 0xC1) {
      if (segment_size < 11)
        return ESP_ERR_INVALID_ARG;

      const size_t height_offset = segment_start + 5;
      const size_t width_offset = segment_start + 7;
      const size_t component_count_offset = segment_start + 9;
      if (component_count_offset >= *jpeg_size)
        return ESP_ERR_INVALID_ARG;
      const uint8_t component_count = source[component_count_offset];
      if (component_count == 0 || component_count > 3 || segment_size < 8 + component_count * 3)
        return ESP_ERR_INVALID_ARG;

      uint8_t max_horizontal_sampling = 1;
      uint8_t max_vertical_sampling = 1;
      for (uint8_t index = 0; index < component_count; index++) {
        const size_t sampling_offset = component_count_offset + 2 + index * 3;
        if (sampling_offset >= *jpeg_size)
          return ESP_ERR_INVALID_ARG;
        const uint8_t sampling = source[sampling_offset];
        max_horizontal_sampling = std::max<uint8_t>(max_horizontal_sampling, sampling >> 4);
        max_vertical_sampling = std::max<uint8_t>(max_vertical_sampling, sampling & 0x0F);
      }

      const uint16_t original_height =
          (static_cast<uint16_t>(source[height_offset]) << 8) | source[height_offset + 1];
      const uint16_t original_width =
          (static_cast<uint16_t>(source[width_offset]) << 8) | source[width_offset + 1];
      const uint32_t horizontal_mcu = static_cast<uint32_t>(max_horizontal_sampling) * 8U;
      const uint32_t vertical_mcu = static_cast<uint32_t>(max_vertical_sampling) * 8U;
      const uint32_t aligned_width = align_up(original_width, horizontal_mcu);
      const uint32_t aligned_height = align_up(original_height, vertical_mcu);
      if (aligned_width == original_width && aligned_height == original_height)
        return ESP_OK;
      if (aligned_width > UINT16_MAX || aligned_height > UINT16_MAX)
        return ESP_ERR_INVALID_SIZE;

      uint8_t *mutable_jpeg = nullptr;
      if (owned_input->data() == source) {
        mutable_jpeg = owned_input->data();
      } else {
        mutable_jpeg = static_cast<uint8_t *>(heap_caps_malloc(*jpeg_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (mutable_jpeg == nullptr)
          mutable_jpeg = static_cast<uint8_t *>(heap_caps_malloc(*jpeg_size, MALLOC_CAP_8BIT));
        if (mutable_jpeg == nullptr)
          return ESP_ERR_NO_MEM;
        std::memcpy(mutable_jpeg, source, *jpeg_size);
        owned_input->reset(mutable_jpeg, *jpeg_size, *jpeg_size);
      }

      mutable_jpeg[height_offset] = static_cast<uint8_t>(aligned_height >> 8);
      mutable_jpeg[height_offset + 1] = static_cast<uint8_t>(aligned_height);
      mutable_jpeg[width_offset] = static_cast<uint8_t>(aligned_width >> 8);
      mutable_jpeg[width_offset + 1] = static_cast<uint8_t>(aligned_width);
      *jpeg = mutable_jpeg;
      ESP_LOGI(TAG, "Padded JPEG SOF for ESP32-P4 hardware decode: %ux%u -> %" PRIu32 "x%" PRIu32,
               original_width, original_height, aligned_width, aligned_height);
      return ESP_OK;
    }

    position += segment_size;
  }

  return ESP_OK;
}

esp_err_t prepare_hardware_decode_input_(const uint8_t *jpeg, size_t jpeg_size, JpegBuffer *normalized,
                                         const uint8_t **effective_jpeg, size_t *effective_size) {
  esp_err_t err = prepare_hardware_input_(jpeg, jpeg_size, normalized, effective_jpeg, effective_size);
  if (err != ESP_OK)
    return err;

  // ESP-IDF 5.x rejects JPEG dimensions whose pixel count is not divisible by
  // eight. The entropy stream already contains complete MCUs at the image
  // edges, so expose those padded dimensions to the hardware and crop back to
  // the original frame when the decoded buffer is adopted.
  return align_hardware_decode_dimensions_(normalized, effective_jpeg, effective_size);
}

#if defined(SOC_JPEG_CODEC_SUPPORTED) && SOC_JPEG_CODEC_SUPPORTED
jpeg_decoder_handle_t preallocated_decoder = nullptr;
jpeg_encoder_handle_t preallocated_encoder = nullptr;
uint8_t *preallocated_encoder_output = nullptr;
size_t preallocated_encoder_output_capacity = 0;
StaticSemaphore_t jpeg_codec_mutex_buffer;
SemaphoreHandle_t jpeg_codec_mutex = nullptr;
constexpr size_t MIN_ENCODER_INTERNAL_DMA_LARGEST = 56 * 1024;
constexpr size_t MIN_DECODER_INTERNAL_DMA_LARGEST = 56 * 1024;
constexpr size_t DECODER_INTERNAL_INPUT_ALIGNMENT = 64;
#ifndef CONFIG_ESPHOME_JPEG_DECODER_INTERNAL_INPUT_MAX_BYTES
constexpr size_t DECODER_INTERNAL_INPUT_MAX_BYTES = 64 * 1024;
#else
constexpr size_t DECODER_INTERNAL_INPUT_MAX_BYTES = CONFIG_ESPHOME_JPEG_DECODER_INTERNAL_INPUT_MAX_BYTES;
#endif
#ifndef CONFIG_ESPHOME_JPEG_DECODER_DIRECT_PSRAM_INPUT
#define CONFIG_ESPHOME_JPEG_DECODER_DIRECT_PSRAM_INPUT 0
#endif
bool encoder_dma_guard_logged = false;
bool decoder_dma_guard_logged = false;
bool decoder_internal_input_logged = false;
bool decoder_input_fallback_logged = false;
bool decoder_direct_psram_input_logged = false;

#if defined(CONFIG_IDF_TARGET_ESP32P4)
#ifndef CONFIG_ESPHOME_JPEG_DMA2D_AXI_BURSTINESS
#define CONFIG_ESPHOME_JPEG_DMA2D_AXI_BURSTINESS 1
#endif
#ifndef CONFIG_ESPHOME_DMA2D_AXI_BURSTINESS
#define CONFIG_ESPHOME_DMA2D_AXI_BURSTINESS 8
#endif
#ifndef CONFIG_ESPHOME_JPEG_DMA2D_PEAK_LEVEL
#define CONFIG_ESPHOME_JPEG_DMA2D_PEAK_LEVEL 2
#endif
#ifndef CONFIG_ESPHOME_JPEG_DMA2D_TRANSACTION_LEVEL
#define CONFIG_ESPHOME_JPEG_DMA2D_TRANSACTION_LEVEL 4
#endif
#ifndef CONFIG_ESPHOME_DMA2D_PEAK_LEVEL
#define CONFIG_ESPHOME_DMA2D_PEAK_LEVEL 0
#endif
#ifndef CONFIG_ESPHOME_DMA2D_TRANSACTION_LEVEL
#define CONFIG_ESPHOME_DMA2D_TRANSACTION_LEVEL 1
#endif
#ifndef CONFIG_ESPHOME_JPEG_DMA2D_WRITE_PRIORITY
#define CONFIG_ESPHOME_JPEG_DMA2D_WRITE_PRIORITY 0
#endif
#ifndef CONFIG_ESPHOME_JPEG_DMA2D_READ_PRIORITY
#define CONFIG_ESPHOME_JPEG_DMA2D_READ_PRIORITY 0
#endif
#ifndef CONFIG_ESPHOME_DMA2D_WRITE_PRIORITY
#define CONFIG_ESPHOME_DMA2D_WRITE_PRIORITY 1
#endif
#ifndef CONFIG_ESPHOME_DMA2D_READ_PRIORITY
#define CONFIG_ESPHOME_DMA2D_READ_PRIORITY 1
#endif

static std::atomic<uint16_t> jpeg_dma2d_axi_burstiness{CONFIG_ESPHOME_JPEG_DMA2D_AXI_BURSTINESS};
static std::atomic<uint8_t> jpeg_dma2d_peak_level{CONFIG_ESPHOME_JPEG_DMA2D_PEAK_LEVEL};
static std::atomic<uint8_t> jpeg_dma2d_transaction_level{CONFIG_ESPHOME_JPEG_DMA2D_TRANSACTION_LEVEL};
static std::atomic<uint8_t> jpeg_dma2d_write_priority{CONFIG_ESPHOME_JPEG_DMA2D_WRITE_PRIORITY};
static std::atomic<uint8_t> jpeg_dma2d_read_priority{CONFIG_ESPHOME_JPEG_DMA2D_READ_PRIORITY};

class Dma2dJpegBurstGuard {
 public:
  Dma2dJpegBurstGuard() {
    const auto burstiness = jpeg_dma2d_axi_burstiness.load(std::memory_order_relaxed);
    const auto peak_level = jpeg_dma2d_peak_level.load(std::memory_order_relaxed);
    const auto transaction_level = jpeg_dma2d_transaction_level.load(std::memory_order_relaxed);
    const auto write_priority = jpeg_dma2d_write_priority.load(std::memory_order_relaxed);
    const auto read_priority = jpeg_dma2d_read_priority.load(std::memory_order_relaxed);

    axi_icm_ll_set_dma2d_qos_arbiter_prio(write_priority, read_priority);
    axi_icm_ll_set_qos_burstiness(AXI_ICM_MASTER_DMA2D, burstiness, AXI_ICM_ACCESS_READ);
    axi_icm_ll_set_qos_burstiness(AXI_ICM_MASTER_DMA2D, burstiness, AXI_ICM_ACCESS_WRITE);
    axi_icm_ll_set_qos_peak_transaction_rate(AXI_ICM_MASTER_DMA2D, peak_level, transaction_level,
                                             AXI_ICM_ACCESS_READ);
    axi_icm_ll_set_qos_peak_transaction_rate(AXI_ICM_MASTER_DMA2D, peak_level, transaction_level,
                                             AXI_ICM_ACCESS_WRITE);
  }

  ~Dma2dJpegBurstGuard() {
    axi_icm_ll_set_dma2d_qos_arbiter_prio(CONFIG_ESPHOME_DMA2D_WRITE_PRIORITY, CONFIG_ESPHOME_DMA2D_READ_PRIORITY);
    axi_icm_ll_set_qos_burstiness(AXI_ICM_MASTER_DMA2D, CONFIG_ESPHOME_DMA2D_AXI_BURSTINESS, AXI_ICM_ACCESS_READ);
    axi_icm_ll_set_qos_burstiness(AXI_ICM_MASTER_DMA2D, CONFIG_ESPHOME_DMA2D_AXI_BURSTINESS, AXI_ICM_ACCESS_WRITE);
    axi_icm_ll_set_qos_peak_transaction_rate(AXI_ICM_MASTER_DMA2D, CONFIG_ESPHOME_DMA2D_PEAK_LEVEL,
                                             CONFIG_ESPHOME_DMA2D_TRANSACTION_LEVEL, AXI_ICM_ACCESS_READ);
    axi_icm_ll_set_qos_peak_transaction_rate(AXI_ICM_MASTER_DMA2D, CONFIG_ESPHOME_DMA2D_PEAK_LEVEL,
                                             CONFIG_ESPHOME_DMA2D_TRANSACTION_LEVEL, AXI_ICM_ACCESS_WRITE);
  }
};

class Dma2dJpegTransferAbilityGuard {
 public:
  Dma2dJpegTransferAbilityGuard(bool encoder, uint16_t burst_length, int8_t descriptor_burst)
      : encoder_(encoder), active_(burst_length != 0 || descriptor_burst >= 0) {
    if (!this->active_)
      return;

    auto &burst = encoder ? g_esphome_esp32_jpeg_encoder_dma2d_burst_length
                          : g_esphome_esp32_jpeg_dma2d_burst_length;
    auto &descriptor = encoder ? g_esphome_esp32_jpeg_encoder_dma2d_desc_burst
                               : g_esphome_esp32_jpeg_dma2d_desc_burst;
    this->previous_burst_length_ = burst.load(std::memory_order_relaxed);
    this->previous_descriptor_burst_ = descriptor.load(std::memory_order_relaxed);
    if (burst_length != 0)
      burst.store(burst_length, std::memory_order_relaxed);
    if (descriptor_burst >= 0)
      descriptor.store(descriptor_burst != 0, std::memory_order_relaxed);
  }

  ~Dma2dJpegTransferAbilityGuard() {
    if (!this->active_)
      return;
    auto &burst = this->encoder_ ? g_esphome_esp32_jpeg_encoder_dma2d_burst_length
                                 : g_esphome_esp32_jpeg_dma2d_burst_length;
    auto &descriptor = this->encoder_ ? g_esphome_esp32_jpeg_encoder_dma2d_desc_burst
                                      : g_esphome_esp32_jpeg_dma2d_desc_burst;
    burst.store(this->previous_burst_length_, std::memory_order_relaxed);
    descriptor.store(this->previous_descriptor_burst_, std::memory_order_relaxed);
  }

 protected:
  bool encoder_{false};
  bool active_{false};
  uint16_t previous_burst_length_{128};
  bool previous_descriptor_burst_{true};
};

class Dma2dJpegOutputCacheSyncGuard {
 public:
  explicit Dma2dJpegOutputCacheSyncGuard(bool skip)
      : previous_(g_esphome_esp32_jpeg_skip_output_cache_msync), active_(skip) {
    if (this->active_)
      g_esphome_esp32_jpeg_skip_output_cache_msync = true;
  }

  ~Dma2dJpegOutputCacheSyncGuard() {
    if (this->active_)
      g_esphome_esp32_jpeg_skip_output_cache_msync = this->previous_;
  }

 protected:
  bool previous_{false};
  bool active_{false};
};
#else
class Dma2dJpegBurstGuard {
 public:
  Dma2dJpegBurstGuard() = default;
};

class Dma2dJpegTransferAbilityGuard {
 public:
  Dma2dJpegTransferAbilityGuard(bool encoder, uint16_t burst_length, int8_t descriptor_burst) {}
};

class Dma2dJpegOutputCacheSyncGuard {
 public:
  explicit Dma2dJpegOutputCacheSyncGuard(bool skip) {}
};
#endif

void ensure_jpeg_codec_mutex_() {
  if (jpeg_codec_mutex == nullptr)
    jpeg_codec_mutex = xSemaphoreCreateMutexStatic(&jpeg_codec_mutex_buffer);
}

class JpegCodecLock {
 public:
  explicit JpegCodecLock(int timeout_ms) {
    ensure_jpeg_codec_mutex_();
    if (jpeg_codec_mutex == nullptr)
      return;
    const TickType_t timeout = timeout_ms <= 0 ? pdMS_TO_TICKS(1000) : pdMS_TO_TICKS(timeout_ms);
    this->locked_ = xSemaphoreTake(jpeg_codec_mutex, timeout) == pdTRUE;
  }

  ~JpegCodecLock() {
    if (this->locked_)
      xSemaphoreGive(jpeg_codec_mutex);
  }

  bool locked() const { return this->locked_; }

 protected:
  bool locked_{false};
};

void log_decoder_allocation_failure_(esp_err_t err) {
  ESP_LOGW(TAG,
           "JPEG decoder engine allocation failed err=%d internal_free=%zu internal_largest=%zu dma_free=%zu "
           "dma_largest=%zu",
           (int) err, heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
           heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
           heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
}

bool has_encoder_dma_budget_() {
  const size_t dma_largest = heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (dma_largest >= MIN_ENCODER_INTERNAL_DMA_LARGEST)
    return true;

  if (!encoder_dma_guard_logged) {
    encoder_dma_guard_logged = true;
    ESP_LOGW(TAG,
             "Skipping JPEG encoder: internal DMA heap too fragmented internal_free=%zu internal_largest=%zu "
             "dma_free=%zu dma_largest=%zu min_largest=%zu",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL), dma_largest,
             MIN_ENCODER_INTERNAL_DMA_LARGEST);
  }
  return false;
}

bool has_decoder_dma_budget_() {
  const size_t dma_largest = heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (dma_largest >= MIN_DECODER_INTERNAL_DMA_LARGEST)
    return true;

  if (!decoder_dma_guard_logged) {
    decoder_dma_guard_logged = true;
    ESP_LOGW(TAG,
             "Skipping JPEG decoder: internal DMA heap too fragmented internal_free=%zu internal_largest=%zu "
             "dma_free=%zu dma_largest=%zu min_largest=%zu",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL), dma_largest,
             MIN_DECODER_INTERNAL_DMA_LARGEST);
  }
  return false;
}

uint8_t *allocate_decoder_input_(const uint8_t *jpeg, size_t jpeg_size, size_t *capacity, bool *owned) {
  if (owned != nullptr)
    *owned = true;

#if CONFIG_ESPHOME_JPEG_DECODER_DIRECT_PSRAM_INPUT
  if (esp_ptr_external_ram(jpeg)) {
    if (capacity != nullptr)
      *capacity = jpeg_size;
    if (owned != nullptr)
      *owned = false;
    if (!decoder_direct_psram_input_logged) {
      decoder_direct_psram_input_logged = true;
      ESP_LOGI(TAG, "JPEG decoder uses direct PSRAM input when possible");
    }
    return const_cast<uint8_t *>(jpeg);
  }
#endif

  if (jpeg_size <= DECODER_INTERNAL_INPUT_MAX_BYTES) {
    const size_t aligned_size = align_up(jpeg_size, DECODER_INTERNAL_INPUT_ALIGNMENT);
    uint8_t *input_data = static_cast<uint8_t *>(heap_caps_aligned_calloc(
        DECODER_INTERNAL_INPUT_ALIGNMENT, 1, aligned_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (input_data != nullptr) {
      std::memcpy(input_data, jpeg, jpeg_size);
      if (capacity != nullptr)
        *capacity = aligned_size;
      if (!decoder_internal_input_logged) {
        decoder_internal_input_logged = true;
        ESP_LOGI(TAG, "JPEG decoder input staged in internal DMA RAM up to %zu bytes",
                 DECODER_INTERNAL_INPUT_MAX_BYTES);
      }
      return input_data;
    }
  }

  if (!decoder_input_fallback_logged) {
    decoder_input_fallback_logged = true;
    ESP_LOGW(TAG, "JPEG decoder input uses PSRAM fallback jpeg_size=%zu internal_dma_largest=%zu internal_free=%zu",
             jpeg_size, heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  }

  jpeg_decode_memory_alloc_cfg_t input_mem_cfg = {
      .buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER,
  };
  size_t input_capacity = 0;
  uint8_t *input_data = static_cast<uint8_t *>(jpeg_alloc_decoder_mem(jpeg_size, &input_mem_cfg, &input_capacity));
  if (capacity != nullptr)
    *capacity = input_capacity;
  if (input_data != nullptr && input_capacity >= jpeg_size)
    std::memcpy(input_data, jpeg, jpeg_size);
  return input_data;
}

void release_decoder_input_(uint8_t *input_data, bool owned) {
  if (owned && input_data != nullptr)
    heap_caps_free(input_data);
}

esp_err_t preallocate_encoder_locked_(int timeout_ms) {
  if (preallocated_encoder != nullptr)
    return ESP_OK;

  if (!has_encoder_dma_budget_())
    return ESP_ERR_NO_MEM;

  jpeg_encode_engine_cfg_t engine_cfg = {
      .intr_priority = 0,
      .timeout_ms = timeout_ms,
  };
  esp_err_t err = jpeg_new_encoder_engine(&engine_cfg, &preallocated_encoder);
  if (err != ESP_OK)
    return err;

  ESP_LOGCONFIG(TAG, "Preallocated JPEG encoder engine (timeout=%dms)", timeout_ms);
  return ESP_OK;
}

esp_err_t preallocate_decoder_locked_(int timeout_ms) {
  if (preallocated_decoder != nullptr)
    return ESP_OK;

  if (!has_decoder_dma_budget_())
    return ESP_ERR_NO_MEM;

  jpeg_decode_engine_cfg_t engine_cfg = {
      .intr_priority = 0,
      .timeout_ms = timeout_ms,
  };
  esp_err_t err = jpeg_new_decoder_engine(&engine_cfg, &preallocated_decoder);
  if (err != ESP_OK) {
    log_decoder_allocation_failure_(err);
    return err;
  }

  ESP_LOGCONFIG(TAG, "Preallocated JPEG decoder engine (timeout=%dms)", timeout_ms);
  return ESP_OK;
}

jpeg_enc_input_format_t to_encode_format(PixelFormat format) {
  switch (format) {
    case PixelFormat::RGB565:
      return JPEG_ENCODE_IN_FORMAT_RGB565;
    case PixelFormat::RGB888:
      return JPEG_ENCODE_IN_FORMAT_RGB888;
    case PixelFormat::GRAY:
      return JPEG_ENCODE_IN_FORMAT_GRAY;
  }
  return JPEG_ENCODE_IN_FORMAT_RGB888;
}

jpeg_dec_output_format_t to_decode_format(PixelFormat format) {
  switch (format) {
    case PixelFormat::RGB565:
      return JPEG_DECODE_OUT_FORMAT_RGB565;
    case PixelFormat::RGB888:
      return JPEG_DECODE_OUT_FORMAT_RGB888;
    case PixelFormat::GRAY:
      return JPEG_DECODE_OUT_FORMAT_GRAY;
  }
  return JPEG_DECODE_OUT_FORMAT_RGB888;
}

jpeg_down_sampling_type_t to_down_sampling(DownSampling down_sampling) {
  switch (down_sampling) {
    case DownSampling::YUV444:
      return JPEG_DOWN_SAMPLING_YUV444;
    case DownSampling::YUV422:
      return JPEG_DOWN_SAMPLING_YUV422;
    case DownSampling::YUV420:
      return JPEG_DOWN_SAMPLING_YUV420;
    case DownSampling::GRAY:
      return JPEG_DOWN_SAMPLING_GRAY;
  }
  return JPEG_DOWN_SAMPLING_YUV420;
}

DownSampling from_down_sampling(jpeg_down_sampling_type_t down_sampling) {
  switch (down_sampling) {
    case JPEG_DOWN_SAMPLING_YUV444:
      return DownSampling::YUV444;
    case JPEG_DOWN_SAMPLING_YUV422:
      return DownSampling::YUV422;
    case JPEG_DOWN_SAMPLING_YUV420:
      return DownSampling::YUV420;
    case JPEG_DOWN_SAMPLING_GRAY:
      return DownSampling::GRAY;
  }
  return DownSampling::YUV420;
}

jpeg_dec_rgb_element_order_t to_rgb_order(RgbElementOrder order) {
  return order == RgbElementOrder::RGB ? JPEG_DEC_RGB_ELEMENT_ORDER_RGB : JPEG_DEC_RGB_ELEMENT_ORDER_BGR;
}

jpeg_yuv_rgb_conv_std_t to_color_standard(ColorConversionStandard standard) {
  return standard == ColorConversionStandard::BT709 ? JPEG_YUV_RGB_CONV_STD_BT709 : JPEG_YUV_RGB_CONV_STD_BT601;
}
#endif

}  // namespace

void Esp32JpegComponent::setup() {
  // Preallocate both engines while the internal DMA heap is still contiguous.
  // Products that only encode during boot may release the encoder afterwards;
  // the decoder remains resident so runtime artwork never has to recreate it.
  esp_err_t err = preallocate_decoder(this->decoder_timeout_ms_);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "JPEG decoder preallocation failed: %s", esp_err_to_name(err));
  }
  JpegCodecLock lock(this->decoder_timeout_ms_);
  if (!lock.locked()) {
    ESP_LOGW(TAG, "JPEG encoder preallocation lock timed out");
    return;
  }
  err = preallocate_encoder_locked_(this->decoder_timeout_ms_);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "JPEG encoder preallocation failed: %s", esp_err_to_name(err));
  }
}

void Esp32JpegComponent::dump_config() { ESP_LOGCONFIG(TAG, "ESP32 JPEG hardware accelerator"); }

JpegBuffer::~JpegBuffer() { this->release(); }

JpegBuffer::JpegBuffer(JpegBuffer &&other) noexcept {
  this->data_ = std::exchange(other.data_, nullptr);
  this->size_ = std::exchange(other.size_, 0);
  this->capacity_ = std::exchange(other.capacity_, 0);
}

JpegBuffer &JpegBuffer::operator=(JpegBuffer &&other) noexcept {
  if (this != &other) {
    this->release();
    this->data_ = std::exchange(other.data_, nullptr);
    this->size_ = std::exchange(other.size_, 0);
    this->capacity_ = std::exchange(other.capacity_, 0);
  }
  return *this;
}

void JpegBuffer::reset(uint8_t *data, size_t size, size_t capacity) {
  this->release();
  this->data_ = data;
  this->size_ = size;
  this->capacity_ = capacity;
}

void JpegBuffer::release() {
  if (this->data_ != nullptr) {
    heap_caps_free(this->data_);
    this->data_ = nullptr;
  }
  this->size_ = 0;
  this->capacity_ = 0;
}

size_t bytes_per_pixel(PixelFormat format) {
  switch (format) {
    case PixelFormat::RGB565:
      return 2;
    case PixelFormat::RGB888:
      return 3;
    case PixelFormat::GRAY:
      return 1;
  }
  return 0;
}

size_t raw_image_size(uint32_t width, uint32_t height, PixelFormat format) {
  return static_cast<size_t>(width) * height * bytes_per_pixel(format);
}

size_t decoded_output_size(const PictureInfo &info, PixelFormat format) {
  return raw_image_size(align_up(info.width, 16), align_up(info.height, 16), format);
}

uint8_t *allocate_decode_output(size_t requested_size, size_t *capacity) {
#if defined(SOC_JPEG_CODEC_SUPPORTED) && SOC_JPEG_CODEC_SUPPORTED
  jpeg_decode_memory_alloc_cfg_t output_mem_cfg = {
      .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
  };
  size_t allocated_capacity = 0;
  uint8_t *buffer =
      static_cast<uint8_t *>(jpeg_alloc_decoder_mem(requested_size, &output_mem_cfg, &allocated_capacity));
  if (capacity != nullptr)
    *capacity = allocated_capacity;
  return buffer;
#else
  if (capacity != nullptr)
    *capacity = requested_size;
  return static_cast<uint8_t *>(heap_caps_malloc(requested_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#endif
}

void release_decode_output(uint8_t *buffer) { heap_caps_free(buffer); }

esp_err_t get_info(const uint8_t *jpeg, size_t jpeg_size, PictureInfo *info) {
#if defined(SOC_JPEG_CODEC_SUPPORTED) && SOC_JPEG_CODEC_SUPPORTED
  if (jpeg == nullptr || jpeg_size == 0 || info == nullptr)
    return ESP_ERR_INVALID_ARG;

  JpegBuffer normalized;
  const uint8_t *effective_jpeg = nullptr;
  size_t effective_size = 0;
  esp_err_t err = prepare_hardware_input_(jpeg, jpeg_size, &normalized, &effective_jpeg, &effective_size);
  if (err != ESP_OK)
    return err;

  JpegCodecLock lock(1000);
  if (!lock.locked())
    return ESP_ERR_TIMEOUT;

  jpeg_decode_picture_info_t picture_info = {};
  err = jpeg_decoder_get_info(effective_jpeg, effective_size, &picture_info);
  if (err != ESP_OK)
    return err;

  info->width = picture_info.width;
  info->height = picture_info.height;
  info->down_sampling = from_down_sampling(picture_info.sample_method);
  return ESP_OK;
#else
  return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t encode(const EncodeConfig &config, const uint8_t *input, size_t input_size, JpegBuffer *output) {
#if defined(SOC_JPEG_CODEC_SUPPORTED) && SOC_JPEG_CODEC_SUPPORTED
  if (input == nullptr || output == nullptr || config.width == 0 || config.height == 0)
    return ESP_ERR_INVALID_ARG;

  const size_t expected_input_size = raw_image_size(config.width, config.height, config.input_format);
  if (input_size < expected_input_size)
    return ESP_ERR_INVALID_SIZE;

  JpegCodecLock lock(config.timeout_ms);
  if (!lock.locked())
    return ESP_ERR_TIMEOUT;

  jpeg_encoder_handle_t encoder = preallocated_encoder;
  bool owns_encoder = false;
  esp_err_t err = ESP_OK;
  if (encoder == nullptr) {
    if (!has_encoder_dma_budget_())
      return ESP_ERR_NO_MEM;
    jpeg_encode_engine_cfg_t engine_cfg = {
        .intr_priority = 0,
        .timeout_ms = config.timeout_ms,
    };
    err = jpeg_new_encoder_engine(&engine_cfg, &encoder);
    if (err != ESP_OK)
      return err;
    owns_encoder = true;
  }

  size_t input_capacity = 0;
  uint8_t *input_data = nullptr;
  bool owns_input = false;
  size_t cache_alignment = 64;
  esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &cache_alignment);
  if (esp_ptr_external_ram(input) && (reinterpret_cast<uintptr_t>(input) % cache_alignment) == 0 &&
      (expected_input_size % cache_alignment) == 0) {
    input_data = const_cast<uint8_t *>(input);
    input_capacity = expected_input_size;
  } else {
    jpeg_encode_memory_alloc_cfg_t input_mem_cfg = {
        .buffer_direction = JPEG_ENC_ALLOC_INPUT_BUFFER,
    };
    input_data =
        static_cast<uint8_t *>(jpeg_alloc_encoder_mem(expected_input_size, &input_mem_cfg, &input_capacity));
    owns_input = true;
    if (input_data != nullptr && input_capacity >= expected_input_size)
      std::memcpy(input_data, input, expected_input_size);
  }
  if (input_data == nullptr || input_capacity < expected_input_size) {
    if (owns_input && input_data != nullptr)
      heap_caps_free(input_data);
    if (owns_encoder)
      jpeg_del_encoder_engine(encoder);
    return ESP_ERR_NO_MEM;
  }

  size_t output_capacity = 0;
  uint8_t *output_data = nullptr;
  const bool retain_output = !owns_encoder && encoder == preallocated_encoder;
  if (retain_output && preallocated_encoder_output_capacity >= expected_input_size) {
    output_data = preallocated_encoder_output;
    output_capacity = preallocated_encoder_output_capacity;
  } else {
    jpeg_encode_memory_alloc_cfg_t output_mem_cfg = {
        .buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER,
    };
    output_data =
        static_cast<uint8_t *>(jpeg_alloc_encoder_mem(expected_input_size, &output_mem_cfg, &output_capacity));
    if (output_data != nullptr && retain_output) {
      if (preallocated_encoder_output != nullptr)
        heap_caps_free(preallocated_encoder_output);
      preallocated_encoder_output = output_data;
      preallocated_encoder_output_capacity = output_capacity;
    }
  }
  if (output_data == nullptr) {
    if (owns_input)
      heap_caps_free(input_data);
    if (owns_encoder)
      jpeg_del_encoder_engine(encoder);
    return ESP_ERR_NO_MEM;
  }

  uint8_t quality = std::min<uint8_t>(std::max<uint8_t>(config.quality, 1), 100);
  jpeg_encode_cfg_t encode_cfg = {
    .height = config.height,
    .width = config.width,
    .src_type = to_encode_format(config.input_format),
    .sub_sample = to_down_sampling(config.down_sampling),
    .image_quality = quality,
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    .pixel_reverse = config.pixel_reverse,
#endif
  };
  uint32_t encoded_size = 0;
  {
    Dma2dJpegBurstGuard qos_guard;
    Dma2dJpegTransferAbilityGuard transfer_guard(true, config.dma2d_burst_length,
                                                  config.dma2d_descriptor_burst);
    err = jpeg_encoder_process(encoder, &encode_cfg, input_data, expected_input_size, output_data, output_capacity,
                               &encoded_size);
  }
  if (owns_input)
    heap_caps_free(input_data);
  if (owns_encoder)
    jpeg_del_encoder_engine(encoder);
  if (err != ESP_OK || encoded_size == 0) {
    if (!retain_output)
      heap_caps_free(output_data);
    return err == ESP_OK ? ESP_FAIL : err;
  }

  uint8_t *stored_data = static_cast<uint8_t *>(heap_caps_malloc(encoded_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (stored_data == nullptr)
    stored_data = static_cast<uint8_t *>(heap_caps_malloc(encoded_size, MALLOC_CAP_8BIT));
  if (stored_data == nullptr) {
    if (!retain_output)
      heap_caps_free(output_data);
    return ESP_ERR_NO_MEM;
  }
  std::memcpy(stored_data, output_data, encoded_size);
  if (!retain_output)
    heap_caps_free(output_data);
  output->reset(stored_data, encoded_size, encoded_size);
  return ESP_OK;
#else
  return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t decode(const DecodeConfig &config, const uint8_t *jpeg, size_t jpeg_size, uint8_t *output, size_t output_size,
                 size_t *written) {
#if defined(SOC_JPEG_CODEC_SUPPORTED) && SOC_JPEG_CODEC_SUPPORTED
  if (jpeg == nullptr || jpeg_size == 0 || output == nullptr || output_size == 0)
    return ESP_ERR_INVALID_ARG;

  if (written != nullptr)
    *written = 0;

  JpegBuffer normalized;
  const uint8_t *effective_jpeg = nullptr;
  size_t effective_size = 0;
  esp_err_t err = prepare_hardware_decode_input_(jpeg, jpeg_size, &normalized, &effective_jpeg, &effective_size);
  if (err != ESP_OK)
    return err;

  JpegCodecLock lock(config.timeout_ms);
  if (!lock.locked())
    return ESP_ERR_TIMEOUT;

  jpeg_decoder_handle_t decoder = preallocated_decoder;
  bool owns_decoder = false;
  if (decoder == nullptr) {
    if (!has_decoder_dma_budget_())
      return ESP_ERR_NO_MEM;
    jpeg_decode_engine_cfg_t engine_cfg = {
        .intr_priority = 0,
        .timeout_ms = config.timeout_ms,
    };
    err = jpeg_new_decoder_engine(&engine_cfg, &decoder);
    if (err != ESP_OK) {
      log_decoder_allocation_failure_(err);
      return err;
    }
    owns_decoder = true;
  }

  size_t input_capacity = 0;
  bool input_owned = true;
  uint8_t *input_data = allocate_decoder_input_(effective_jpeg, effective_size, &input_capacity, &input_owned);
  if (input_data == nullptr || input_capacity < effective_size) {
    release_decoder_input_(input_data, input_owned);
    if (owns_decoder)
      jpeg_del_decoder_engine(decoder);
    return ESP_ERR_NO_MEM;
  }

  jpeg_decode_cfg_t decode_cfg = {
      .output_format = to_decode_format(config.output_format),
      .rgb_order = to_rgb_order(config.rgb_order),
      .conv_std = to_color_standard(config.color_conversion),
  };

  uint8_t *decoded_data = output;
  size_t decoded_capacity = output_size;
  bool decoded_owned = false;
  if (!config.direct_output) {
    jpeg_decode_memory_alloc_cfg_t output_mem_cfg = {
        .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
    };
    decoded_data = static_cast<uint8_t *>(jpeg_alloc_decoder_mem(output_size, &output_mem_cfg, &decoded_capacity));
    if (decoded_data == nullptr) {
      release_decoder_input_(input_data, input_owned);
      if (owns_decoder)
        jpeg_del_decoder_engine(decoder);
      return ESP_ERR_NO_MEM;
    }
    decoded_owned = true;
  }

  uint32_t decoded_size = 0;
  err = ESP_OK;
  {
    Dma2dJpegBurstGuard burst_guard;
    Dma2dJpegTransferAbilityGuard transfer_guard(false, config.dma2d_burst_length,
                                                  config.dma2d_descriptor_burst);
    Dma2dJpegOutputCacheSyncGuard cache_sync_guard(config.skip_output_cache_sync && !decoded_owned);
    err = jpeg_decoder_process(decoder, &decode_cfg, input_data, effective_size, decoded_data, decoded_capacity,
                               &decoded_size);
  }
  release_decoder_input_(input_data, input_owned);
  if (owns_decoder)
    jpeg_del_decoder_engine(decoder);
  if (err != ESP_OK) {
    if (decoded_owned)
      heap_caps_free(decoded_data);
    return err;
  }
  if (decoded_size > output_size) {
    if (decoded_owned)
      heap_caps_free(decoded_data);
    return ESP_ERR_INVALID_SIZE;
  }
  if (decoded_owned) {
    std::memcpy(output, decoded_data, decoded_size);
    heap_caps_free(decoded_data);
  }

  if (written != nullptr)
    *written = decoded_size;
  return ESP_OK;
#else
  return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t decode_allocated(const DecodeConfig &config, const uint8_t *jpeg, size_t jpeg_size, size_t output_size,
                           uint8_t **output, size_t *written) {
#if defined(SOC_JPEG_CODEC_SUPPORTED) && SOC_JPEG_CODEC_SUPPORTED
  if (jpeg == nullptr || jpeg_size == 0 || output == nullptr || output_size == 0)
    return ESP_ERR_INVALID_ARG;

  *output = nullptr;
  if (written != nullptr)
    *written = 0;

  JpegBuffer normalized;
  const uint8_t *effective_jpeg = nullptr;
  size_t effective_size = 0;
  esp_err_t err = prepare_hardware_decode_input_(jpeg, jpeg_size, &normalized, &effective_jpeg, &effective_size);
  if (err != ESP_OK)
    return err;

  JpegCodecLock lock(config.timeout_ms);
  if (!lock.locked())
    return ESP_ERR_TIMEOUT;

  jpeg_decoder_handle_t decoder = preallocated_decoder;
  bool owns_decoder = false;
  if (decoder == nullptr) {
    if (!has_decoder_dma_budget_())
      return ESP_ERR_NO_MEM;
    jpeg_decode_engine_cfg_t engine_cfg = {
        .intr_priority = 0,
        .timeout_ms = config.timeout_ms,
    };
    err = jpeg_new_decoder_engine(&engine_cfg, &decoder);
    if (err != ESP_OK) {
      log_decoder_allocation_failure_(err);
      return err;
    }
    owns_decoder = true;
  }

  size_t input_capacity = 0;
  bool input_owned = true;
  uint8_t *input_data = allocate_decoder_input_(effective_jpeg, effective_size, &input_capacity, &input_owned);
  if (input_data == nullptr || input_capacity < effective_size) {
    release_decoder_input_(input_data, input_owned);
    if (owns_decoder)
      jpeg_del_decoder_engine(decoder);
    return ESP_ERR_NO_MEM;
  }

  size_t output_capacity = 0;
  jpeg_decode_memory_alloc_cfg_t output_mem_cfg = {
      .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
  };
  uint8_t *decoded_data =
      static_cast<uint8_t *>(jpeg_alloc_decoder_mem(output_size, &output_mem_cfg, &output_capacity));
  if (decoded_data == nullptr || output_capacity < output_size) {
    if (decoded_data != nullptr)
      heap_caps_free(decoded_data);
    release_decoder_input_(input_data, input_owned);
    if (owns_decoder)
      jpeg_del_decoder_engine(decoder);
    return ESP_ERR_NO_MEM;
  }

  jpeg_decode_cfg_t decode_cfg = {
      .output_format = to_decode_format(config.output_format),
      .rgb_order = to_rgb_order(config.rgb_order),
      .conv_std = to_color_standard(config.color_conversion),
  };

  uint32_t decoded_size = 0;
  g_esphome_esp32_jpeg_last_status = 0;
  err = ESP_OK;
  {
    Dma2dJpegBurstGuard burst_guard;
    Dma2dJpegTransferAbilityGuard transfer_guard(false, config.dma2d_burst_length,
                                                  config.dma2d_descriptor_burst);
    err =
        jpeg_decoder_process(decoder, &decode_cfg, input_data, effective_size, decoded_data, output_capacity,
                             &decoded_size);
  }
  log_decode_buffer_probe_("allocated-output", decoded_data,
                           std::min(static_cast<size_t>(decoded_size), output_capacity));
  ESP_LOGD(TAG, "JPEG decode_allocated: process err=%d decoded_size=%u raw_status=0x%08" PRIX32, (int) err,
           (unsigned) decoded_size, static_cast<uint32_t>(g_esphome_esp32_jpeg_last_status));
  release_decoder_input_(input_data, input_owned);
  if (owns_decoder)
    jpeg_del_decoder_engine(decoder);
  if (err != ESP_OK || decoded_size == 0 || decoded_size > output_size) {
    heap_caps_free(decoded_data);
    return err == ESP_OK ? ESP_FAIL : err;
  }

  *output = decoded_data;
  if (written != nullptr)
    *written = decoded_size;
  return ESP_OK;
#else
  return ESP_ERR_NOT_SUPPORTED;
#endif
}

esp_err_t preallocate_decoder(int timeout_ms) {
#if defined(SOC_JPEG_CODEC_SUPPORTED) && SOC_JPEG_CODEC_SUPPORTED
  JpegCodecLock lock(timeout_ms);
  if (!lock.locked())
    return ESP_ERR_TIMEOUT;

  return preallocate_decoder_locked_(timeout_ms);
#else
  return ESP_ERR_NOT_SUPPORTED;
#endif
}

void release_preallocated_encoder() {
#if defined(SOC_JPEG_CODEC_SUPPORTED) && SOC_JPEG_CODEC_SUPPORTED
  JpegCodecLock lock(100);
  if (!lock.locked())
    return;

  if (preallocated_encoder != nullptr) {
    jpeg_del_encoder_engine(preallocated_encoder);
    preallocated_encoder = nullptr;
  }
  if (preallocated_encoder_output != nullptr) {
    heap_caps_free(preallocated_encoder_output);
    preallocated_encoder_output = nullptr;
    preallocated_encoder_output_capacity = 0;
  }
  ESP_LOGI(TAG, "Released preallocated JPEG encoder and output buffer; decoder remains resident");
#endif
}

void set_decoder_dma2d_burst_length(uint16_t burst_length) {
#if defined(SOC_JPEG_CODEC_SUPPORTED) && SOC_JPEG_CODEC_SUPPORTED
  switch (burst_length) {
    case 1:
    case 8:
      burst_length = 8;
      break;
    case 16:
    case 32:
    case 64:
    case 128:
      break;
    default:
      ESP_LOGW(TAG, "Unsupported JPEG DMA2D burst length %u; keeping %u", burst_length,
               get_decoder_dma2d_burst_length());
      return;
  }
  g_esphome_esp32_jpeg_dma2d_burst_length.store(burst_length, std::memory_order_relaxed);
  ESP_LOGI(TAG, "JPEG DMA2D burst length set to %u", burst_length);
#else
  (void) burst_length;
#endif
}

uint16_t get_decoder_dma2d_burst_length() {
#if defined(SOC_JPEG_CODEC_SUPPORTED) && SOC_JPEG_CODEC_SUPPORTED
  return g_esphome_esp32_jpeg_dma2d_burst_length.load(std::memory_order_relaxed);
#else
  return 0;
#endif
}

void set_decoder_dma2d_descriptor_burst(bool enabled) {
#if defined(SOC_JPEG_CODEC_SUPPORTED) && SOC_JPEG_CODEC_SUPPORTED
  g_esphome_esp32_jpeg_dma2d_desc_burst.store(enabled, std::memory_order_relaxed);
  ESP_LOGI(TAG, "JPEG DMA2D descriptor burst %s", YESNO(enabled));
#else
  (void) enabled;
#endif
}

bool get_decoder_dma2d_descriptor_burst() {
#if defined(SOC_JPEG_CODEC_SUPPORTED) && SOC_JPEG_CODEC_SUPPORTED
  return g_esphome_esp32_jpeg_dma2d_desc_burst.load(std::memory_order_relaxed);
#else
  return false;
#endif
}

void set_decoder_dma2d_qos(uint16_t burstiness, uint8_t peak_level, uint8_t transaction_level,
                           uint8_t write_priority, uint8_t read_priority) {
#if defined(CONFIG_IDF_TARGET_ESP32P4)
  if (burstiness < 1 || burstiness > 256 || peak_level >= transaction_level || transaction_level > 11 ||
      write_priority > 15 || read_priority > 15) {
    ESP_LOGW(TAG, "Invalid JPEG DMA2D QoS profile: burstiness=%u peak=%u transaction=%u write_prio=%u read_prio=%u",
             burstiness, peak_level, transaction_level, write_priority, read_priority);
    return;
  }
  jpeg_dma2d_axi_burstiness.store(burstiness, std::memory_order_relaxed);
  jpeg_dma2d_peak_level.store(peak_level, std::memory_order_relaxed);
  jpeg_dma2d_transaction_level.store(transaction_level, std::memory_order_relaxed);
  jpeg_dma2d_write_priority.store(write_priority, std::memory_order_relaxed);
  jpeg_dma2d_read_priority.store(read_priority, std::memory_order_relaxed);
  ESP_LOGI(TAG, "JPEG DMA2D QoS set: burstiness=%u peak=%u transaction=%u write_prio=%u read_prio=%u", burstiness,
           peak_level, transaction_level, write_priority, read_priority);
#else
  (void) burstiness;
  (void) peak_level;
  (void) transaction_level;
  (void) write_priority;
  (void) read_priority;
#endif
}

}  // namespace esphome::esp32_jpeg

#endif  // USE_ESP32_JPEG
