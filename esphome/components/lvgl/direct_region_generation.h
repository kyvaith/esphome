#pragma once

#include <atomic>
#include <cstdint>

namespace esphome::lvgl {

// Software queue/carry acceptance, never a physical panel acknowledgement.
enum class DirectRegionGenerationState : uint8_t { PENDING, RETIRED_UNCONFIRMED, QUEUED_WITH_CARRY };

// Caller storage must outlive the source-retirement callback. A rejected/busy
// submission transfers no source ownership and produces no callback.
struct DirectRegionGenerationResult {
  std::atomic<DirectRegionGenerationState> state{DirectRegionGenerationState::PENDING};
  uint32_t accepted_generation{};

  void reset() {
    this->accepted_generation = 0;
    this->state.store(DirectRegionGenerationState::PENDING, std::memory_order_release);
  }
  void retire(bool queue_with_carry, uint32_t generation) {
    this->accepted_generation = queue_with_carry ? generation : 0;
    this->state.store(queue_with_carry ? DirectRegionGenerationState::QUEUED_WITH_CARRY
                                       : DirectRegionGenerationState::RETIRED_UNCONFIRMED,
                      std::memory_order_release);
  }
  bool accepted(uint32_t generation) const {
    return this->state.load(std::memory_order_acquire) == DirectRegionGenerationState::QUEUED_WITH_CARRY &&
           generation != 0 && this->accepted_generation == generation;
  }
};

inline bool direct_region_generation_matches(uint32_t expected, uint32_t current, bool paused) {
  return expected == 0 || (!paused && expected == current);
}

}  // namespace esphome::lvgl
