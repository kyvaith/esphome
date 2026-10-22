#pragma once

#include <algorithm>
#include <cstddef>

namespace esphome::esp32_jpeg {

struct EncoderOutputBufferPlan {
  size_t allocation_size{0};
  bool reuse_retained{false};
};

// The heap's largest normal allocation does not include the extra space needed
// to align an allocation. Keep alignment/pointer bookkeeping room, and let the
// SDK round the requested size up inside this aligned bound. An allocation may
// still fail if another task consumes the block after this observation.
constexpr EncoderOutputBufferPlan plan_encoder_output_buffer(size_t requested_size, size_t largest_block,
                                                             size_t alignment, size_t retained_capacity) {
  if (requested_size == 0)
    return {};
  if (retained_capacity >= requested_size)
    return {0, true};
  if (alignment == 0 || (alignment & (alignment - 1)) != 0)
    return {0, retained_capacity != 0};

  size_t available_size = 0;
  if (largest_block > alignment && largest_block - alignment > alignment) {
    available_size = (largest_block - alignment - alignment) & ~(alignment - 1);
  }
  const size_t allocation_size = std::min(requested_size, available_size);
  if (retained_capacity != 0 && retained_capacity >= allocation_size)
    return {0, true};
  return {allocation_size, false};
}

// DMA may use less than the SDK-rounded allocation, including when a retained
// scratch buffer came from a previous encode with a larger caller budget.
constexpr size_t encoder_output_limit(size_t requested_size, size_t allocated_capacity) {
  return std::min(requested_size, allocated_capacity);
}

}  // namespace esphome::esp32_jpeg
