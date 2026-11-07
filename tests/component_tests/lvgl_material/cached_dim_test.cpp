#include "esphome/components/lvgl_material/material_cached_dim.h"
#include <cassert>
#include <vector>

int main() {
  using namespace esphome::lvgl_material;
  for (unsigned pattern = 0; pattern < 256; pattern++) {
    for (size_t length : {0U, 1U, 3U, 7U, 8U, 9U, 15U, 16U, 17U, 63U}) {
      for (size_t offset : {0U, 1U, 7U, 8U, 9U}) {
        std::vector<uint8_t> pixels(length + 2, 0xA5), bits((offset + length + 7) / 8 + 1);
        auto expected_bits = bits;
        for (size_t byte = 0; byte < length; byte++) {
          pixels[byte + 1] = ((pattern * 31 + byte * 43) & 254) | ((pattern >> (byte & 7)) & 1U);
          expected_bits[(offset + byte) >> 3] |= (pixels[byte + 1] & 1U) << ((offset + byte) & 7U);
        }
        const auto original = pixels;
        auto expected = pixels;
        for (size_t byte = 1; byte <= length; byte++)
          expected[byte] >>= 1;
        const size_t split = length / 3;
        cached_dim_half(pixels.data() + 1, split, bits.data(), offset);
        cached_dim_half(pixels.data() + 1 + split, length - split, bits.data(), offset + split);
        assert(pixels == expected && bits == expected_bits);
        cached_dim_restore(pixels.data() + 1, split, bits.data(), offset);
        cached_dim_restore(pixels.data() + 1 + split, length - split, bits.data(), offset + split);
        assert(pixels == original);
      }
    }
  }
}
