#include "esp32_jpeg.h"

#ifdef USE_ESP32_JPEG

#include <algorithm>
#include <cstring>
#include <utility>

#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_memory_utils.h"
#include "sdkconfig.h"
#include "esphome/core/defines.h"
#include "esphome/core/log.h"

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

extern "C" bool esphome_esp32_jpeg_skip_output_cache_msync(void) {
  return g_esphome_esp32_jpeg_skip_output_cache_msync;
}
#endif

namespace esphome::esp32_jpeg {
namespace {

static const char *const TAG = "esp32_jpeg";

uint32_t align_up(uint32_t value, uint32_t alignment) { return (value + alignment - 1) / alignment * alignment; }

#if defined(SOC_JPEG_CODEC_SUPPORTED) && SOC_JPEG_CODEC_SUPPORTED
jpeg_decoder_handle_t preallocated_decoder = nullptr;
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

class Dma2dJpegBurstGuard {
 public:
  Dma2dJpegBurstGuard() {
    if constexpr (CONFIG_ESPHOME_JPEG_DMA2D_WRITE_PRIORITY != CONFIG_ESPHOME_DMA2D_WRITE_PRIORITY ||
                  CONFIG_ESPHOME_JPEG_DMA2D_READ_PRIORITY != CONFIG_ESPHOME_DMA2D_READ_PRIORITY) {
      axi_icm_ll_set_dma2d_qos_arbiter_prio(CONFIG_ESPHOME_JPEG_DMA2D_WRITE_PRIORITY,
                                            CONFIG_ESPHOME_JPEG_DMA2D_READ_PRIORITY);
    }
    if constexpr (CONFIG_ESPHOME_JPEG_DMA2D_AXI_BURSTINESS != CONFIG_ESPHOME_DMA2D_AXI_BURSTINESS) {
      axi_icm_ll_set_qos_burstiness(AXI_ICM_MASTER_DMA2D, CONFIG_ESPHOME_JPEG_DMA2D_AXI_BURSTINESS,
                                    AXI_ICM_ACCESS_READ);
      axi_icm_ll_set_qos_burstiness(AXI_ICM_MASTER_DMA2D, CONFIG_ESPHOME_JPEG_DMA2D_AXI_BURSTINESS,
                                    AXI_ICM_ACCESS_WRITE);
    }
    if constexpr (CONFIG_ESPHOME_JPEG_DMA2D_PEAK_LEVEL != CONFIG_ESPHOME_DMA2D_PEAK_LEVEL ||
                  CONFIG_ESPHOME_JPEG_DMA2D_TRANSACTION_LEVEL != CONFIG_ESPHOME_DMA2D_TRANSACTION_LEVEL) {
      axi_icm_ll_set_qos_peak_transaction_rate(AXI_ICM_MASTER_DMA2D, CONFIG_ESPHOME_JPEG_DMA2D_PEAK_LEVEL,
                                               CONFIG_ESPHOME_JPEG_DMA2D_TRANSACTION_LEVEL, AXI_ICM_ACCESS_READ);
      axi_icm_ll_set_qos_peak_transaction_rate(AXI_ICM_MASTER_DMA2D, CONFIG_ESPHOME_JPEG_DMA2D_PEAK_LEVEL,
                                               CONFIG_ESPHOME_JPEG_DMA2D_TRANSACTION_LEVEL, AXI_ICM_ACCESS_WRITE);
    }
  }

  ~Dma2dJpegBurstGuard() {
    if constexpr (CONFIG_ESPHOME_JPEG_DMA2D_WRITE_PRIORITY != CONFIG_ESPHOME_DMA2D_WRITE_PRIORITY ||
                  CONFIG_ESPHOME_JPEG_DMA2D_READ_PRIORITY != CONFIG_ESPHOME_DMA2D_READ_PRIORITY) {
      axi_icm_ll_set_dma2d_qos_arbiter_prio(CONFIG_ESPHOME_DMA2D_WRITE_PRIORITY, CONFIG_ESPHOME_DMA2D_READ_PRIORITY);
    }
    if constexpr (CONFIG_ESPHOME_JPEG_DMA2D_AXI_BURSTINESS != CONFIG_ESPHOME_DMA2D_AXI_BURSTINESS) {
      axi_icm_ll_set_qos_burstiness(AXI_ICM_MASTER_DMA2D, CONFIG_ESPHOME_DMA2D_AXI_BURSTINESS,
                                    AXI_ICM_ACCESS_READ);
      axi_icm_ll_set_qos_burstiness(AXI_ICM_MASTER_DMA2D, CONFIG_ESPHOME_DMA2D_AXI_BURSTINESS,
                                    AXI_ICM_ACCESS_WRITE);
    }
    if constexpr (CONFIG_ESPHOME_JPEG_DMA2D_PEAK_LEVEL != CONFIG_ESPHOME_DMA2D_PEAK_LEVEL ||
                  CONFIG_ESPHOME_JPEG_DMA2D_TRANSACTION_LEVEL != CONFIG_ESPHOME_DMA2D_TRANSACTION_LEVEL) {
      axi_icm_ll_set_qos_peak_transaction_rate(AXI_ICM_MASTER_DMA2D, CONFIG_ESPHOME_DMA2D_PEAK_LEVEL,
                                               CONFIG_ESPHOME_DMA2D_TRANSACTION_LEVEL, AXI_ICM_ACCESS_READ);
      axi_icm_ll_set_qos_peak_transaction_rate(AXI_ICM_MASTER_DMA2D, CONFIG_ESPHOME_DMA2D_PEAK_LEVEL,
                                               CONFIG_ESPHOME_DMA2D_TRANSACTION_LEVEL, AXI_ICM_ACCESS_WRITE);
    }
  }
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
  ESP_LOGW(TAG, "JPEG decoder engine allocation failed err=%d internal_free=%zu internal_largest=%zu dma_free=%zu "
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
    ESP_LOGW(TAG,
             "JPEG decoder input uses PSRAM fallback jpeg_size=%zu internal_dma_largest=%zu internal_free=%zu",
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

void release_preallocated_decoder_() {
  if (preallocated_decoder != nullptr) {
    jpeg_del_decoder_engine(preallocated_decoder);
    preallocated_decoder = nullptr;
  }
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

  ESP_LOGCONFIG(TAG, "Preallocated JPEG decoder engine");
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
  // Allocate the JPEG decoder while the internal DMA heap is still contiguous.
  // Artwork decode happens during playback, when the heap is usually too
  // fragmented to create the hardware decoder on demand without falling back
  // to a much slower software decode.
  esp_err_t err = preallocate_decoder(this->decoder_timeout_ms_);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "JPEG decoder preallocation failed: %s", esp_err_to_name(err));
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

void release_decode_output(uint8_t *buffer) {
  heap_caps_free(buffer);
}

esp_err_t get_info(const uint8_t *jpeg, size_t jpeg_size, PictureInfo *info) {
#if defined(SOC_JPEG_CODEC_SUPPORTED) && SOC_JPEG_CODEC_SUPPORTED
  if (jpeg == nullptr || jpeg_size == 0 || info == nullptr)
    return ESP_ERR_INVALID_ARG;

  JpegCodecLock lock(1000);
  if (!lock.locked())
    return ESP_ERR_TIMEOUT;

  jpeg_decode_picture_info_t picture_info = {};
  esp_err_t err = jpeg_decoder_get_info(jpeg, jpeg_size, &picture_info);
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

  if (!has_encoder_dma_budget_())
    return ESP_ERR_NO_MEM;

  // The ESP32-P4 JPEG block is shared by the decoder and encoder. Keeping a
  // decoder engine preallocated is useful for artwork, but snapshot caching
  // occasionally needs the encoder; release the idle decoder before creating
  // the encoder to avoid the IDF driver tearing down a half-created handle.
  const bool restore_decoder_after_encode = preallocated_decoder != nullptr;
  auto restore_decoder = [&]() {
    if (!restore_decoder_after_encode || preallocated_decoder != nullptr)
      return;
    esp_err_t prealloc_err = preallocate_decoder_locked_(config.timeout_ms);
    if (prealloc_err != ESP_OK) {
      ESP_LOGW(TAG, "Failed to restore preallocated JPEG decoder after encode: %s",
               esp_err_to_name(prealloc_err));
    }
  };
  release_preallocated_decoder_();

  jpeg_encoder_handle_t encoder = nullptr;
  jpeg_encode_engine_cfg_t engine_cfg = {
      .intr_priority = 0,
      .timeout_ms = config.timeout_ms,
  };
  esp_err_t err = jpeg_new_encoder_engine(&engine_cfg, &encoder);
  if (err != ESP_OK) {
    restore_decoder();
    return err;
  }

  size_t input_capacity = 0;
  jpeg_encode_memory_alloc_cfg_t input_mem_cfg = {
      .buffer_direction = JPEG_ENC_ALLOC_INPUT_BUFFER,
  };
  uint8_t *input_data =
      static_cast<uint8_t *>(jpeg_alloc_encoder_mem(expected_input_size, &input_mem_cfg, &input_capacity));
  if (input_data == nullptr || input_capacity < expected_input_size) {
    if (input_data != nullptr)
      heap_caps_free(input_data);
    jpeg_del_encoder_engine(encoder);
    restore_decoder();
    return ESP_ERR_NO_MEM;
  }
  std::memcpy(input_data, input, expected_input_size);

  size_t output_capacity = 0;
  jpeg_encode_memory_alloc_cfg_t output_mem_cfg = {
      .buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER,
  };
  uint8_t *output_data =
      static_cast<uint8_t *>(jpeg_alloc_encoder_mem(expected_input_size, &output_mem_cfg, &output_capacity));
  if (output_data == nullptr) {
    heap_caps_free(input_data);
    jpeg_del_encoder_engine(encoder);
    restore_decoder();
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
  err = jpeg_encoder_process(encoder, &encode_cfg, input_data, expected_input_size, output_data, output_capacity,
                             &encoded_size);
  heap_caps_free(input_data);
  jpeg_del_encoder_engine(encoder);
  restore_decoder();
  if (err != ESP_OK || encoded_size == 0) {
    heap_caps_free(output_data);
    return err == ESP_OK ? ESP_FAIL : err;
  }

  uint8_t *stored_data =
      static_cast<uint8_t *>(heap_caps_malloc(encoded_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (stored_data == nullptr)
    stored_data = static_cast<uint8_t *>(heap_caps_malloc(encoded_size, MALLOC_CAP_8BIT));
  if (stored_data == nullptr) {
    heap_caps_free(output_data);
    return ESP_ERR_NO_MEM;
  }
  std::memcpy(stored_data, output_data, encoded_size);
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
    esp_err_t err = jpeg_new_decoder_engine(&engine_cfg, &decoder);
    if (err != ESP_OK) {
      log_decoder_allocation_failure_(err);
      return err;
    }
    owns_decoder = true;
  }

  size_t input_capacity = 0;
  bool input_owned = true;
  uint8_t *input_data = allocate_decoder_input_(jpeg, jpeg_size, &input_capacity, &input_owned);
  if (input_data == nullptr || input_capacity < jpeg_size) {
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
  esp_err_t err = ESP_OK;
  {
    Dma2dJpegBurstGuard burst_guard;
    Dma2dJpegOutputCacheSyncGuard cache_sync_guard(config.skip_output_cache_sync && !decoded_owned);
    err = jpeg_decoder_process(decoder, &decode_cfg, input_data, jpeg_size, decoded_data, decoded_capacity,
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
    esp_err_t err = jpeg_new_decoder_engine(&engine_cfg, &decoder);
    if (err != ESP_OK) {
      log_decoder_allocation_failure_(err);
      return err;
    }
    owns_decoder = true;
  }

  size_t input_capacity = 0;
  bool input_owned = true;
  uint8_t *input_data = allocate_decoder_input_(jpeg, jpeg_size, &input_capacity, &input_owned);
  if (input_data == nullptr || input_capacity < jpeg_size) {
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

  jpeg_decode_picture_info_t debug_info = {};
  esp_err_t info_err = jpeg_decoder_get_info(input_data, jpeg_size, &debug_info);
  ESP_LOGW(TAG,
           "JPEG decode_allocated trace: info_err=%d size=%zux%zu sample=%d out_format=%d rgb_order=%d "
           "conv=%d jpeg=%zu output_capacity=%zu",
           (int) info_err, info_err == ESP_OK ? (size_t) debug_info.width : 0,
           info_err == ESP_OK ? (size_t) debug_info.height : 0,
           info_err == ESP_OK ? (int) debug_info.sample_method : -1, (int) decode_cfg.output_format,
           (int) decode_cfg.rgb_order, (int) decode_cfg.conv_std, jpeg_size, output_capacity);

  uint32_t decoded_size = 0;
  esp_err_t err = ESP_OK;
  {
    Dma2dJpegBurstGuard burst_guard;
    err = jpeg_decoder_process(decoder, &decode_cfg, input_data, jpeg_size, decoded_data, output_capacity,
                               &decoded_size);
  }
  ESP_LOGW(TAG, "JPEG decode_allocated trace: process err=%d decoded_size=%u", (int) err, (unsigned) decoded_size);
  if (err == ESP_ERR_INVALID_STATE && decoded_size > 0 && decoded_size <= output_capacity) {
    ESP_LOGW(TAG, "JPEG decode_allocated trace: accepting decoded output despite ESP_ERR_INVALID_STATE");
    err = ESP_OK;
  }
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

}  // namespace esphome::esp32_jpeg

#endif  // USE_ESP32_JPEG
