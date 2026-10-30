#pragma once

#include "esphome/core/defines.h"

#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER

#include <array>
#include <cstdint>
#include <string_view>

namespace esphome::lvgl_material {

static constexpr int64_t NUMBER_SCALE = 1000000;
static constexpr int64_t NUMBER_MAX_MICRO = 1000000000000000000LL;
static constexpr uint32_t NUMBER_MAX_INDEX = 1000000;

// Canonical decimal arithmetic only. No floating point or retained JSON strings.
// The extreme endpoint difference (2e18) is within signed 64-bit arithmetic.
struct MaterialNumberDomain {
  int64_t minimum{0};
  int64_t maximum{0};
  int64_t step{0};
  std::array<char, 33> unit{};
  uint32_t max_index{0};

  bool valid() const;
  bool value_at(uint32_t index, int64_t &value) const;
  bool operator==(const MaterialNumberDomain &) const = default;
};

bool number_parse_decimal(std::string_view text, int64_t &value);
bool number_format_decimal(int64_t value, char *output, size_t capacity);
bool number_plain_utf8(std::string_view value);

}  // namespace esphome::lvgl_material

#endif
