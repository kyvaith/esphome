#pragma once

#include <cstdint>

namespace esphome {

enum class BufferWriter : uint8_t {
  CPU,
  DMA,
};

}  // namespace esphome
