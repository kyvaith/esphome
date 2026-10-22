#include "esphome/components/esp32_jpeg/encoder_output_buffer.h"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <limits>

using esphome::esp32_jpeg::encoder_output_limit;
using esphome::esp32_jpeg::plan_encoder_output_buffer;

struct BufferCase {
  size_t requested;
  size_t largest;
  size_t alignment;
  size_t retained;
  size_t allocation;
  bool reuse;
};

static constexpr BufferCase CASES[] = {
    // The failures observed with aggregate free PSRAM still above 2 MiB.
    {1048576, 1048576, 64, 0, 1048448, false},
    {1048576, 440 * 1024, 64, 0, 440 * 1024 - 128, false},
    {524288, 440 * 1024, 64, 0, 440 * 1024 - 128, false},
    // Healthy memory keeps the original raw-size/default and bounded budgets.
    {1920000, 4 * 1024 * 1024, 64, 0, 1920000, false},
    {524288, 2 * 1024 * 1024, 64, 0, 524288, false},
    {1025, 8192, 64, 0, 1025, false},
    // Borrowing works without any new free block, including a smaller caller.
    {524288, 0, 64, 1048576, 0, true},
    {1048576, 200 * 1024, 64, 262144, 0, true},
    {1048576, 440 * 1024, 64, 262144, 440 * 1024 - 128, false},
    {1048576, 128, 64, 65536, 0, true},
    // No capacity is invented for empty/tiny heaps or invalid alignments.
    {1048576, 0, 64, 0, 0, false},
    {1048576, 129, 64, 0, 0, false},
    {1048576, 8192, 0, 0, 0, false},
    {1048576, 8192, 3, 512, 0, true},
    {0, 8192, 64, 512, 0, false},
    // Arithmetic remains bounded at the target/host size_t boundaries.
    {std::numeric_limits<size_t>::max(), 1024, 64, 0, 896, false},
    {std::numeric_limits<size_t>::max(), std::numeric_limits<size_t>::max(),
     size_t{1} << (std::numeric_limits<size_t>::digits - 1), 0, 0, false},
    {std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::max(), 64, 0,
     (size_t{std::numeric_limits<uint32_t>::max()} - 128) & ~size_t{63}, false},
};

int main(int argc, char **argv) {
  assert(argc == 2);
  const auto index = static_cast<size_t>(std::strtoul(argv[1], nullptr, 10));
  if (index < std::size(CASES)) {
    const auto &test = CASES[index];
    const auto plan = plan_encoder_output_buffer(test.requested, test.largest, test.alignment, test.retained);
    assert(plan.allocation_size == test.allocation);
    assert(plan.reuse_retained == test.reuse);
    return 0;
  }
  if (index == std::size(CASES)) {
    // The physical SDK allocation may be rounded up or borrowed from a larger
    // encode, but DMA and successful publication must honor the caller budget.
    assert(encoder_output_limit(524288, 1048576) == 524288);
    assert(encoder_output_limit(1048576, 450432) == 450432);
    assert(encoder_output_limit(1025, 1088) == 1025);
    assert(encoder_output_limit(0, 8192) == 0);
    return 0;
  }
  assert(index == std::size(CASES) + 1);
  for (size_t alignment : {size_t{16}, size_t{64}, size_t{128}}) {
    for (size_t largest = 0; largest < 8192; largest += 17) {
      for (size_t requested : {size_t{1}, size_t{1025}, size_t{1048576}}) {
        for (size_t retained : {size_t{0}, size_t{64}, size_t{4096}}) {
          const auto plan = plan_encoder_output_buffer(requested, largest, alignment, retained);
          assert(plan.allocation_size <= requested);
          if (plan.reuse_retained)
            assert(retained != 0);
          if (plan.allocation_size != 0) {
            const size_t rounded = (plan.allocation_size + alignment - 1) & ~(alignment - 1);
            assert(rounded <= largest - 2 * alignment);
          }
          if (retained >= requested)
            assert(plan.reuse_retained);
        }
      }
    }
  }
}
