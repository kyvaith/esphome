#pragma once

#include "esphome/core/defines.h"

#ifdef USE_ESP32_JPEG

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "esphome/core/component.h"
#include "soc/soc_caps.h"

namespace esphome::esp32_jpeg {

enum class PixelFormat {
  RGB565,
  RGB888,
  GRAY,
};

enum class DownSampling {
  YUV444,
  YUV422,
  YUV420,
  GRAY,
};

enum class RgbElementOrder {
  RGB,
  BGR,
};

enum class ColorConversionStandard {
  BT601,
  BT709,
};

struct PictureInfo {
  uint32_t width{0};
  uint32_t height{0};
  DownSampling down_sampling{DownSampling::YUV420};
};

class JpegBuffer {
 public:
  JpegBuffer() = default;
  ~JpegBuffer();
  JpegBuffer(const JpegBuffer &) = delete;
  JpegBuffer &operator=(const JpegBuffer &) = delete;
  JpegBuffer(JpegBuffer &&other) noexcept;
  JpegBuffer &operator=(JpegBuffer &&other) noexcept;

  uint8_t *data() { return this->data_; }
  const uint8_t *data() const { return this->data_; }
  size_t size() const { return this->size_; }
  size_t capacity() const { return this->capacity_; }
  bool empty() const { return this->data_ == nullptr || this->size_ == 0; }

  void reset(uint8_t *data, size_t size, size_t capacity);
  void release();

 protected:
  uint8_t *data_{nullptr};
  size_t size_{0};
  size_t capacity_{0};
};

struct EncodeConfig {
  uint32_t width{0};
  uint32_t height{0};
  PixelFormat input_format{PixelFormat::RGB888};
  DownSampling down_sampling{DownSampling::YUV420};
  uint8_t quality{80};
  bool pixel_reverse{false};
  // Zero/-1 retain the component defaults. Per-operation overrides keep
  // fullscreen snapshot traffic from starving a continuously scanned display.
  uint16_t dma2d_burst_length{0};
  int8_t dma2d_descriptor_burst{-1};
  int timeout_ms{40};
};

struct DecodeConfig {
  PixelFormat output_format{PixelFormat::RGB888};
  RgbElementOrder rgb_order{RgbElementOrder::BGR};
  ColorConversionStandard color_conversion{ColorConversionStandard::BT601};
  bool direct_output{false};
  bool skip_output_cache_sync{false};
  uint16_t dma2d_burst_length{0};
  int8_t dma2d_descriptor_burst{-1};
  int timeout_ms{40};
};

class Esp32JpegComponent : public Component {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::BUS; }

  void set_decoder_timeout(int timeout_ms) { this->decoder_timeout_ms_ = timeout_ms; }

 protected:
  int decoder_timeout_ms_{180};
};

size_t bytes_per_pixel(PixelFormat format);
size_t raw_image_size(uint32_t width, uint32_t height, PixelFormat format);
size_t decoded_output_size(const PictureInfo &info, PixelFormat format);

uint8_t *allocate_decode_output(size_t requested_size, size_t *capacity = nullptr);
void release_decode_output(uint8_t *buffer);
esp_err_t get_info(const uint8_t *jpeg, size_t jpeg_size, PictureInfo *info);
esp_err_t encode(const EncodeConfig &config, const uint8_t *input, size_t input_size, JpegBuffer *output);
esp_err_t decode(const DecodeConfig &config, const uint8_t *jpeg, size_t jpeg_size, uint8_t *output, size_t output_size,
                 size_t *written = nullptr);
esp_err_t decode_allocated(const DecodeConfig &config, const uint8_t *jpeg, size_t jpeg_size, size_t output_size,
                           uint8_t **output, size_t *written = nullptr);
esp_err_t preallocate_decoder(int timeout_ms);
void release_preallocated_encoder();
void set_decoder_dma2d_burst_length(uint16_t burst_length);
uint16_t get_decoder_dma2d_burst_length();
void set_decoder_dma2d_descriptor_burst(bool enabled);
bool get_decoder_dma2d_descriptor_burst();
void set_decoder_dma2d_qos(uint16_t burstiness, uint8_t peak_level, uint8_t transaction_level,
                            uint8_t write_priority, uint8_t read_priority);

}  // namespace esphome::esp32_jpeg

#endif  // USE_ESP32_JPEG
