#include "material_scalar_raster.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace esphome::lvgl_material {

int scalar_character_index(char character) {
  if (character >= '0' && character <= '9')
    return character - '0';
  if (character == '%')
    return 10;
  return character == '-' ? 11 : -1;
}

static bool inside(const ScalarRect &rect, const ScalarRect &region) {
  return rect.width > 0 && rect.height > 0 && rect.x >= 0 && rect.y >= 0 &&
         static_cast<int>(rect.x) + rect.width <= region.width &&
         static_cast<int>(rect.y) + rect.height <= region.height;
}

bool scalar_style_valid(const ScalarRasterStyle &s) {
  return s.region.width > 0 && s.region.height > 0 && inside(s.track, s.region) && inside(s.bubble, s.region) &&
         inside(s.minimum, s.region) && inside(s.maximum, s.region) && s.track.width > s.track.height &&
         s.fonts[0].line_height > 0 && s.fonts[0].line_height <= s.bubble.height && s.fonts[1].line_height > 0 &&
         s.fonts[1].line_height <= std::min(s.minimum.height, s.maximum.height);
}

static void blend(uint8_t *pixel, ScalarColor color, unsigned alpha) {
  pixel[0] = static_cast<uint8_t>((pixel[0] * (255 - alpha) + color.blue * alpha + 127) / 255);
  pixel[1] = static_cast<uint8_t>((pixel[1] * (255 - alpha) + color.green * alpha + 127) / 255);
  pixel[2] = static_cast<uint8_t>((pixel[2] * (255 - alpha) + color.red * alpha + 127) / 255);
}

static void rounded(uint8_t *target, const ScalarRasterStyle &s, ScalarRect rect, float radius, ScalarColor color) {
  radius = std::min(radius, 0.5f * std::min(rect.width, rect.height));
  for (int y = std::max(0, static_cast<int>(rect.y)); y < std::min<int>(s.region.height, rect.y + rect.height); y++) {
    for (int x = std::max(0, static_cast<int>(rect.x)); x < std::min<int>(s.region.width, rect.x + rect.width); x++) {
      const float dx = std::max(std::abs(x + 0.5f - (rect.x + rect.width * 0.5f)) - rect.width * 0.5f + radius, 0.0f);
      const float dy = std::max(std::abs(y + 0.5f - (rect.y + rect.height * 0.5f)) - rect.height * 0.5f + radius, 0.0f);
      const float coverage = std::clamp(radius + 0.5f - std::sqrt(dx * dx + dy * dy), 0.0f, 1.0f);
      blend(target + (static_cast<size_t>(y) * s.region.width + x) * 3, color,
            static_cast<unsigned>(coverage * 255.0f));
    }
  }
}

static bool text(uint8_t *target, const ScalarRasterStyle &s, const uint8_t *masks, size_t mask_bytes,
                 const ScalarFont &font, const char *text, ScalarRect box, ScalarColor color, int alignment) {
  int width = 0;
  for (size_t i = 0; text[i] != '\0'; i++) {
    const int glyph = scalar_character_index(text[i]);
    const int next = text[i + 1] == '\0' ? ScalarFont::CHARACTER_COUNT : scalar_character_index(text[i + 1]);
    if (glyph < 0 || next < 0)
      return false;
    width += font.advances[glyph][next] + (text[i + 1] == '\0' ? 0 : font.letter_space);
  }
  if (width < 0 || width > box.width)
    return false;
  int pen = box.x + (alignment == 0 ? 0 : alignment == 2 ? box.width - width : (box.width - width) / 2);
  const int line_y = box.y + (box.height - font.line_height) / 2;
  for (size_t i = 0; text[i] != '\0'; i++) {
    const int index = scalar_character_index(text[i]);
    const auto &g = font.glyphs[index];
    if (g.offset > mask_bytes || static_cast<size_t>(g.width) * g.height > mask_bytes - g.offset)
      return false;
    for (int gy = 0; gy < g.height; gy++) {
      for (int gx = 0; gx < g.width; gx++) {
        const int x = pen + g.x + gx, y = line_y + g.y + gy;
        if (x >= box.x && x < box.x + box.width && y >= box.y && y < box.y + box.height && x >= 0 && y >= 0 &&
            x < s.region.width && y < s.region.height)
          blend(target + (static_cast<size_t>(y) * s.region.width + x) * 3, color,
                masks[g.offset + static_cast<size_t>(gy) * g.width + gx]);
      }
    }
    const int next = text[i + 1] == '\0' ? ScalarFont::CHARACTER_COUNT : scalar_character_index(text[i + 1]);
    pen += font.advances[index][next] + font.letter_space;
  }
  return true;
}

bool scalar_raster_rgb888(uint8_t *target, size_t capacity, const ScalarRasterStyle &s, const uint8_t *masks,
                          size_t mask_bytes, bool known, uint8_t pct) {
  if (target == nullptr || masks == nullptr || pct > 100 || !scalar_style_valid(s) ||
      static_cast<size_t>(s.region.width) * s.region.height > capacity / 3)
    return false;
  for (size_t i = 0; i < static_cast<size_t>(s.region.width) * s.region.height; i++) {
    target[i * 3] = s.background.blue;
    target[i * 3 + 1] = s.background.green;
    target[i * 3 + 2] = s.background.red;
  }
  rounded(target, s, s.track, s.track.height * 0.5f, s.inactive);
  const int center = s.track.x + (known ? (static_cast<int>(s.track.width - 1) * pct + 50) / 100 : s.track.width / 2);
  ScalarRect bubble = s.bubble;
  bubble.x = static_cast<int16_t>(std::clamp(center - bubble.width / 2, 0, s.region.width - bubble.width));
  if (known) {
    ScalarRect active = s.track;
    active.width = static_cast<int16_t>(std::max(1, center - active.x + 1));
    rounded(target, s, active, active.height * 0.5f, s.active);
    // The unknown state has no fabricated 50% indicator/thumb.
    ScalarRect thumb{static_cast<int16_t>(center - s.track.height / 2), s.track.y, s.track.height, s.track.height};
    rounded(target, s, thumb, thumb.width * 0.5f, s.active);
  }
  rounded(target, s, bubble, bubble.height * 0.5f, s.bubble_color);
  char candidate[5] = "--";
  if (known)
    std::snprintf(candidate, sizeof(candidate), "%u%%", static_cast<unsigned>(pct));
  return text(target, s, masks, mask_bytes, s.fonts[0], candidate, bubble, s.text, 1) &&
         text(target, s, masks, mask_bytes, s.fonts[1], "0%", s.minimum, s.caption, 0) &&
         text(target, s, masks, mask_bytes, s.fonts[1], "100%", s.maximum, s.caption, 2);
}

}  // namespace esphome::lvgl_material
