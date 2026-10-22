#include "esp32_jpeg_decoder.h"

#ifdef USE_RUNTIME_IMAGE_ESP32_JPEG

#include "runtime_image.h"
#include "esphome/components/esp32_jpeg/esp32_jpeg.h"
#include "esphome/core/log.h"

#include <cstring>

namespace esphome::runtime_image {

static const char *const TAG = "runtime_image.esp32_jpeg";

namespace {

uint32_t align_up(uint32_t value, uint32_t alignment) { return (value + alignment - 1U) / alignment * alignment; }

void get_mcu_alignment(esp32_jpeg::DownSampling down_sampling, uint32_t *horizontal, uint32_t *vertical) {
  switch (down_sampling) {
    case esp32_jpeg::DownSampling::YUV420:
      *horizontal = 16;
      *vertical = 16;
      return;
    case esp32_jpeg::DownSampling::YUV422:
      *horizontal = 16;
      *vertical = 8;
      return;
    case esp32_jpeg::DownSampling::YUV444:
    case esp32_jpeg::DownSampling::GRAY:
      *horizontal = 8;
      *vertical = 8;
      return;
  }
}

}  // namespace

bool Esp32JpegDecoder::has_end_marker_(const uint8_t *buffer, size_t size) {
  if (buffer == nullptr || size < 2) {
    return false;
  }
  for (size_t index = size - 1; index > 0; index--) {
    if (buffer[index - 1] == 0xFF && buffer[index] == 0xD9) {
      return true;
    }
  }
  return false;
}

int Esp32JpegDecoder::decode(uint8_t *buffer, size_t size) {
  if (this->expected_size_ > 0 && size < this->expected_size_) {
    return 0;
  }
  if (this->expected_size_ == 0 && !this->has_end_marker_(buffer, size)) {
    return 0;
  }

  esp32_jpeg::PictureInfo info{};
  esp_err_t error = esp32_jpeg::get_info(buffer, size, &info);
  if (error != ESP_OK || info.width == 0 || info.height == 0) {
    ESP_LOGE(TAG, "Failed to read JPEG header: %s", esp_err_to_name(error));
    return DECODE_ERROR_INVALID_TYPE;
  }
  if (!this->image_->accepts_decoded_dimensions(info.width, info.height)) {
    ESP_LOGE(TAG, "Hardware JPEG cannot resize %ux%u into the configured target", static_cast<unsigned>(info.width),
             static_cast<unsigned>(info.height));
    return DECODE_ERROR_UNSUPPORTED_FORMAT;
  }

  esp32_jpeg::PixelFormat output_format;
  esp32_jpeg::RgbElementOrder rgb_order;
  switch (this->image_->get_type()) {
    case image::IMAGE_TYPE_RGB565:
      output_format = esp32_jpeg::PixelFormat::RGB565;
      rgb_order = this->image_->is_big_endian() ? esp32_jpeg::RgbElementOrder::RGB : esp32_jpeg::RgbElementOrder::BGR;
      break;
    case image::IMAGE_TYPE_RGB:
      output_format = esp32_jpeg::PixelFormat::RGB888;
      rgb_order = esp32_jpeg::RgbElementOrder::BGR;
      break;
    default:
      ESP_LOGE(TAG, "Hardware JPEG only supports RGB565 and RGB runtime images");
      return DECODE_ERROR_UNSUPPORTED_FORMAT;
  }

  uint32_t horizontal_alignment = 8;
  uint32_t vertical_alignment = 8;
  get_mcu_alignment(info.down_sampling, &horizontal_alignment, &vertical_alignment);
  const uint32_t aligned_width = align_up(info.width, horizontal_alignment);
  const uint32_t aligned_height = align_up(info.height, vertical_alignment);
  const size_t bytes_per_pixel = esp32_jpeg::bytes_per_pixel(output_format);
  const size_t output_size = static_cast<size_t>(aligned_width) * static_cast<size_t>(aligned_height) * bytes_per_pixel;

  esp32_jpeg::DecodeConfig config{
      .output_format = output_format,
      .rgb_order = rgb_order,
      .color_conversion = esp32_jpeg::ColorConversionStandard::BT601,
      .timeout_ms = 1000,
  };
  uint8_t *output = nullptr;
  size_t written = 0;
  error = esp32_jpeg::decode_allocated(config, buffer, size, output_size, &output, &written);
  if (error != ESP_OK || output == nullptr) {
    ESP_LOGE(TAG, "Hardware JPEG decode failed: %s", esp_err_to_name(error));
    return error == ESP_ERR_NO_MEM ? DECODE_ERROR_OUT_OF_MEMORY : DECODE_ERROR_INTERNAL_DECODER_ERROR;
  }

  const size_t required_written =
      static_cast<size_t>(aligned_width) * static_cast<size_t>(info.height) * bytes_per_pixel;
  if (written < required_written) {
    ESP_LOGE(TAG, "Hardware JPEG returned a short frame: %zu < %zu", written, required_written);
    esp32_jpeg::release_decode_output(output);
    return DECODE_ERROR_INTERNAL_DECODER_ERROR;
  }

  if (aligned_width != info.width) {
    const size_t source_stride = static_cast<size_t>(aligned_width) * bytes_per_pixel;
    const size_t target_stride = static_cast<size_t>(info.width) * bytes_per_pixel;
    for (uint32_t row = 1; row < info.height; row++) {
      std::memmove(output + static_cast<size_t>(row) * target_stride, output + static_cast<size_t>(row) * source_stride,
                   target_stride);
    }
  }

  const BufferWriter writer = aligned_width == info.width ? BufferWriter::DMA : BufferWriter::CPU;
  if (!this->image_->adopt_decode_buffer(output, info.width, info.height, writer)) {
    esp32_jpeg::release_decode_output(output);
    return DECODE_ERROR_OUT_OF_MEMORY;
  }

  this->decoded_bytes_ = size;
  ESP_LOGD(TAG, "Decoded %ux%u JPEG in hardware (%zu bytes)", static_cast<unsigned>(info.width),
           static_cast<unsigned>(info.height), size);
  return size;
}

}  // namespace esphome::runtime_image

#endif  // USE_RUNTIME_IMAGE_ESP32_JPEG
