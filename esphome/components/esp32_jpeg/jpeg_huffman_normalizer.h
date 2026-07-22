#pragma once

#include "esphome/core/defines.h"

#ifdef USE_ESP32_JPEG

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "esp32_jpeg.h"

namespace esphome::esp32_jpeg {

// Re-encodes only JPEG Huffman symbols. Quantized DCT coefficients and their
// amplitude bits remain unchanged, so pixel reconstruction is still performed
// entirely by the ESP32-P4 JPEG peripheral.
esp_err_t normalize_huffman_for_hardware(const uint8_t *jpeg, size_t jpeg_size, JpegBuffer *output,
                                         bool *normalized);

}  // namespace esphome::esp32_jpeg

#endif  // USE_ESP32_JPEG
