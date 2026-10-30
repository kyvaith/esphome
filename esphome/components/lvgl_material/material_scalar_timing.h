#pragma once

#include <cstdint>

namespace esphome::lvgl_material {

// Round upward: a lower tick rate must never shorten the 34-ms cadence.
constexpr uint32_t scalar_render_delay_ticks(uint32_t tick_rate_hz) {
  return static_cast<uint32_t>((uint64_t{34} * tick_rate_hz + 999U) / 1000U);
}

}  // namespace esphome::lvgl_material
