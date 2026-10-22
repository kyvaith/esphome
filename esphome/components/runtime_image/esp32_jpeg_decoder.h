#pragma once

#include "esphome/core/defines.h"

#ifdef USE_RUNTIME_IMAGE_ESP32_JPEG

#include "image_decoder.h"

namespace esphome::runtime_image {

class Esp32JpegDecoder : public ImageDecoder {
 public:
  explicit Esp32JpegDecoder(RuntimeImage *image) : ImageDecoder(image, JPEG) {}

  int decode(uint8_t *buffer, size_t size) override;
  bool is_finished() const override { return this->decoded_bytes_ > 0; }
  bool knows_end() const override { return true; }

 protected:
  static bool has_end_marker_(const uint8_t *buffer, size_t size);
};

}  // namespace esphome::runtime_image

#endif  // USE_RUNTIME_IMAGE_ESP32_JPEG
