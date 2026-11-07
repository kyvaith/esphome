#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace esphome::lvgl_material {

// The caller owns the captured bytes and zero-initialized packed low bits.
// Preserve the exact original without a second full-size background allocation.
inline void cached_dim_half(uint8_t *pixels, size_t bytes, uint8_t *low_bits, size_t offset) {
  size_t index = 0;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  for (; (offset & 7U) == 0 && bytes - index >= 8; index += 8, offset += 8) {
    uint32_t first, second;
    std::memcpy(&first, pixels + index, 4);
    std::memcpy(&second, pixels + index + 4, 4);
    low_bits[offset >> 3] = static_cast<uint8_t>((((first & 0x01010101U) * 0x01020408U) >> 24) |
                                                 ((((second & 0x01010101U) * 0x01020408U) >> 24) << 4));
    first = (first >> 1) & 0x7F7F7F7FU;
    second = (second >> 1) & 0x7F7F7F7FU;
    std::memcpy(pixels + index, &first, 4);
    std::memcpy(pixels + index + 4, &second, 4);
  }
#endif
  for (; index < bytes; index++, offset++) {
    low_bits[offset >> 3] |= (pixels[index] & 1U) << (offset & 7U);
    pixels[index] >>= 1;
  }
}

inline void cached_dim_restore(uint8_t *pixels, size_t bytes, const uint8_t *low_bits, size_t offset) {
  size_t index = 0;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  for (; (offset & 7U) == 0 && bytes - index >= 8; index += 8, offset += 8) {
    uint32_t first, second;
    std::memcpy(&first, pixels + index, 4);
    std::memcpy(&second, pixels + index + 4, 4);
    const uint32_t bits = low_bits[offset >> 3];
    first = (first << 1) | (((bits & 0xFU) * 0x00204081U) & 0x01010101U);
    second = (second << 1) | (((bits >> 4) * 0x00204081U) & 0x01010101U);
    std::memcpy(pixels + index, &first, 4);
    std::memcpy(pixels + index + 4, &second, 4);
  }
#endif
  for (; index < bytes; index++, offset++)
    pixels[index] = (pixels[index] << 1) | ((low_bits[offset >> 3] >> (offset & 7U)) & 1U);
}

}  // namespace esphome::lvgl_material
