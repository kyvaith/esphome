#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome::lvgl {

// Inspect actual pending areas before LVGL joins/synchronizes them. A bounding
// box or sum of disjoint areas is not proof that the entire display is dirty.
template<typename Area>
bool direct_region_has_full_display_invalidation(const Area *areas, size_t count, int32_t width, int32_t height) {
  if (areas == nullptr || count == 0 || width <= 0 || height <= 0)
    return false;
  for (size_t index = 0; index < count; index++) {
    const auto &area = areas[index];
    if (area.x1 <= 0 && area.y1 <= 0 && area.x2 >= width - 1 && area.y2 >= height - 1)
      return true;
  }
  return false;
}

}  // namespace esphome::lvgl
