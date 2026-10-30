#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace esphome::lvgl_material {

struct ScalarRect {
  int16_t x{}, y{}, width{}, height{};
};
struct ScalarColor {
  uint8_t red{}, green{}, blue{};
};
struct ScalarGlyph {
  uint32_t offset{};
  uint16_t width{}, height{};
  int16_t x{}, y{};
};
struct ScalarFont {
  static constexpr size_t CHARACTER_COUNT = 12;
  std::array<ScalarGlyph, CHARACTER_COUNT> glyphs{};
  std::array<std::array<int16_t, CHARACTER_COUNT + 1>, CHARACTER_COUNT> advances{};
  int16_t line_height{}, letter_space{};
};
struct ScalarRasterStyle {
  ScalarRect region, track, bubble, minimum, maximum;
  ScalarColor background, inactive, active, bubble_color, text, caption;
  std::array<ScalarFont, 2> fonts{};
};

// Only immutable metadata and cached A8 masks are read by the worker.
// Geometry is local to the configured region; no display/product constants.
bool scalar_style_valid(const ScalarRasterStyle &style);
bool scalar_raster_rgb888(uint8_t *target, size_t capacity, const ScalarRasterStyle &style, const uint8_t *glyph_masks,
                          size_t mask_bytes, bool known, uint8_t percentage);
int scalar_character_index(char character);

}  // namespace esphome::lvgl_material
