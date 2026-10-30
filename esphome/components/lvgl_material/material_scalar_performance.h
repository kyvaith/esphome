#pragma once

#ifdef USE_LVGL_MATERIAL_SCALAR_PERFORMANCE_METRICS

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace esphome::lvgl_material {

// Cold, caller-owned snapshot. Bin n is ceil(age_us / 1000), 0..127;
// bin128 is overflow. These are accepted source-retirement observations,
// never physical-panel or VSYNC acknowledgements.
struct ScalarLatencySnapshot {
  static constexpr size_t BIN_COUNT = 129;
  std::array<uint32_t, BIN_COUNT> bins{};
  uint32_t max_us{};

  uint64_t count() const {
    uint64_t total = 0;
    for (const auto value : this->bins)
      total += value;
    return total;
  }
  uint64_t at_most_100ms() const {
    uint64_t total = 0;
    for (size_t i = 0; i <= 100; i++)
      total += this->bins[i];
    return total;
  }
  // Sentinel means no samples or the quantile falls in the overflow bin.
  uint32_t p95_upper_ms() const {
    const uint64_t total = this->count();
    if (total == 0)
      return UINT32_MAX;
    const uint64_t rank = (total * 95 + 99) / 100;
    uint64_t cumulative = 0;
    for (size_t i = 0; i < this->bins.size(); i++) {
      cumulative += this->bins[i];
      if (cumulative >= rank)
        return i == 128 ? UINT32_MAX : static_cast<uint32_t>(i);
    }
    return UINT32_MAX;
  }
};

struct ScalarPerformanceSnapshot {
  uint32_t sequence{};
  uint32_t logical_origins{};
  uint32_t worker_observed_origins{};
  uint32_t submission_attempts{};
  uint32_t busy_attempts{};
  uint32_t submitted_sources{};
  uint32_t retired_sources{};
  uint32_t accepted_retirements{};
  uint32_t unconfirmed_retirements{};
  uint32_t superseded_accepted_retirements{};
  uint32_t value_dedup_results{};
  uint32_t rejected_results{};
  uint32_t invalid_age_samples{};
  ScalarLatencySnapshot drag_phase;
  ScalarLatencySnapshot stable;
};

// One writer is the existing scalar worker. The logical-origin counter is
// owner-task written and sampled by that same task while no callback can run.
// Atomic cells make the bounded seqlock read race-free even on a retry.
class ScalarPerformanceAccumulator {
 public:
  void note_logical_origin() { this->logical_origins_.fetch_add(1, std::memory_order_relaxed); }

  void note_worker_observed_origin() {
    this->begin_();
    this->worker_observed_origins_.fetch_add(1, std::memory_order_relaxed);
    this->end_();
  }

  void note_submission(bool submitted, bool busy) {
    this->begin_();
    this->submission_attempts_.fetch_add(1, std::memory_order_relaxed);
    if (submitted)
      this->submitted_sources_.fetch_add(1, std::memory_order_relaxed);
    if (busy)
      this->busy_attempts_.fetch_add(1, std::memory_order_relaxed);
    this->end_();
  }

  void note_result(bool retired, bool accepted, bool value_dedup, bool stable, uint32_t origin_us, uint32_t observed_us,
                   bool superseded = false) {
    this->begin_();
    if (retired) {
      this->retired_sources_.fetch_add(1, std::memory_order_relaxed);
      if (accepted) {
        this->accepted_retirements_.fetch_add(1, std::memory_order_relaxed);
        if (superseded)
          this->superseded_accepted_retirements_.fetch_add(1, std::memory_order_relaxed);
        const uint32_t age = observed_us - origin_us;
        if (age < 0x80000000U)
          this->note_latency_(stable, age);
        else
          this->invalid_age_samples_.fetch_add(1, std::memory_order_relaxed);
      } else {
        this->unconfirmed_retirements_.fetch_add(1, std::memory_order_relaxed);
      }
    } else if (accepted && value_dedup) {
      this->value_dedup_results_.fetch_add(1, std::memory_order_relaxed);
    } else {
      this->rejected_results_.fetch_add(1, std::memory_order_relaxed);
    }
    this->end_();
  }

  // Owner task only. The component additionally rejects in-flight/pending work.
  // Never reset counters while the worker can write; take cumulative deltas.
  bool snapshot(ScalarPerformanceSnapshot &out) const {
    for (unsigned attempt = 0; attempt < 3; attempt++) {
      const uint32_t sequence = this->sequence_.load(std::memory_order_acquire);
      if ((sequence & 1U) != 0)
        continue;
      out.sequence = sequence;
      out.logical_origins = this->logical_origins_.load(std::memory_order_relaxed);
      out.worker_observed_origins = this->worker_observed_origins_.load(std::memory_order_relaxed);
      out.submission_attempts = this->submission_attempts_.load(std::memory_order_relaxed);
      out.busy_attempts = this->busy_attempts_.load(std::memory_order_relaxed);
      out.submitted_sources = this->submitted_sources_.load(std::memory_order_relaxed);
      out.retired_sources = this->retired_sources_.load(std::memory_order_relaxed);
      out.accepted_retirements = this->accepted_retirements_.load(std::memory_order_relaxed);
      out.unconfirmed_retirements = this->unconfirmed_retirements_.load(std::memory_order_relaxed);
      out.superseded_accepted_retirements = this->superseded_accepted_retirements_.load(std::memory_order_relaxed);
      out.value_dedup_results = this->value_dedup_results_.load(std::memory_order_relaxed);
      out.rejected_results = this->rejected_results_.load(std::memory_order_relaxed);
      out.invalid_age_samples = this->invalid_age_samples_.load(std::memory_order_relaxed);
      for (size_t i = 0; i < ScalarLatencySnapshot::BIN_COUNT; i++) {
        out.drag_phase.bins[i] = this->drag_bins_[i].load(std::memory_order_relaxed);
        out.stable.bins[i] = this->stable_bins_[i].load(std::memory_order_relaxed);
      }
      out.drag_phase.max_us = this->drag_max_us_.load(std::memory_order_relaxed);
      out.stable.max_us = this->stable_max_us_.load(std::memory_order_relaxed);
      std::atomic_thread_fence(std::memory_order_acquire);
      if (sequence == this->sequence_.load(std::memory_order_acquire))
        return true;
    }
    return false;
  }

 protected:
  void begin_() { this->sequence_.fetch_add(1, std::memory_order_acq_rel); }
  void end_() { this->sequence_.fetch_add(1, std::memory_order_release); }
  void note_latency_(bool stable, uint32_t age) {
    const uint32_t rounded_ms = age / 1000U + (age % 1000U != 0);
    const size_t bin = rounded_ms < 128 ? rounded_ms : 128;
    auto &bins = stable ? this->stable_bins_ : this->drag_bins_;
    auto &maximum = stable ? this->stable_max_us_ : this->drag_max_us_;
    bins[bin].fetch_add(1, std::memory_order_relaxed);
    if (age > maximum.load(std::memory_order_relaxed))
      maximum.store(age, std::memory_order_relaxed);
  }

  std::atomic<uint32_t> sequence_{0}, logical_origins_{0}, worker_observed_origins_{0};
  std::atomic<uint32_t> submission_attempts_{0}, busy_attempts_{0}, submitted_sources_{0}, retired_sources_{0};
  std::atomic<uint32_t> accepted_retirements_{0}, unconfirmed_retirements_{0}, value_dedup_results_{0};
  std::atomic<uint32_t> rejected_results_{0}, invalid_age_samples_{0}, superseded_accepted_retirements_{0};
  std::array<std::atomic<uint32_t>, ScalarLatencySnapshot::BIN_COUNT> drag_bins_{}, stable_bins_{};
  std::atomic<uint32_t> drag_max_us_{0}, stable_max_us_{0};
};

}  // namespace esphome::lvgl_material
#endif
