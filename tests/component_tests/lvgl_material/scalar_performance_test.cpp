#include "material_scalar_performance.h"

#include <cassert>
#include <cstdint>
#include <limits>

using namespace esphome::lvgl_material;

class Probe : public ScalarPerformanceAccumulator {
 public:
  void begin_write() { this->begin_(); }
  void end_write() { this->end_(); }
};

static void accepted_sample(Probe &metrics, bool stable, uint32_t origin, uint32_t observed) {
  metrics.note_logical_origin();
  metrics.note_worker_observed_origin();
  metrics.note_submission(true, false);
  metrics.note_result(true, true, false, stable, origin, observed);
}

static void exact_threshold_and_separate_phases() {
  Probe metrics;
  accepted_sample(metrics, false, 1000, 101000);
  accepted_sample(metrics, false, 1000, 101001);
  accepted_sample(metrics, true, 1000, 2000);
  ScalarPerformanceSnapshot snapshot;
  assert(metrics.snapshot(snapshot));
  assert(snapshot.logical_origins == 3 && snapshot.worker_observed_origins == 3);
  assert(snapshot.submitted_sources == 3 && snapshot.submission_attempts == 3);
  assert(snapshot.retired_sources == 3 && snapshot.accepted_retirements == 3);
  assert(snapshot.drag_phase.count() == 2 && snapshot.stable.count() == 1);
  assert(snapshot.drag_phase.at_most_100ms() == 1 && snapshot.stable.at_most_100ms() == 1);
  assert(snapshot.drag_phase.bins[100] == 1 && snapshot.drag_phase.bins[101] == 1);
  assert(snapshot.drag_phase.p95_upper_ms() == 101 && snapshot.stable.p95_upper_ms() == 1);
  assert(snapshot.drag_phase.max_us == 100001);
  assert(snapshot.superseded_accepted_retirements == 0);
}

static void rejected_and_dedup_never_become_zero_latency_samples() {
  Probe metrics;
  metrics.note_logical_origin();
  metrics.note_worker_observed_origin();
  metrics.note_submission(false, true);
  metrics.note_submission(false, true);
  metrics.note_submission(false, false);
  metrics.note_result(false, false, false, true, 10, 10);
  metrics.note_logical_origin();
  metrics.note_worker_observed_origin();
  metrics.note_result(false, true, true, false, 10, 10);
  metrics.note_logical_origin();
  metrics.note_worker_observed_origin();
  metrics.note_submission(true, false);
  metrics.note_result(true, false, false, true, 10, 10);
  ScalarPerformanceSnapshot snapshot;
  assert(metrics.snapshot(snapshot));
  assert(snapshot.busy_attempts == 2 && snapshot.submission_attempts == 4);
  assert(snapshot.submitted_sources == 1 && snapshot.retired_sources == 1);
  assert(snapshot.value_dedup_results == 1 && snapshot.rejected_results == 1);
  assert(snapshot.unconfirmed_retirements == 1 && snapshot.accepted_retirements == 0);
  assert(snapshot.drag_phase.count() == 0 && snapshot.stable.count() == 0);
  assert(snapshot.drag_phase.p95_upper_ms() == UINT32_MAX);
}

static void quantile_overflow_and_wrap_are_explicit() {
  Probe metrics;
  for (unsigned i = 0; i < 95; i++)
    accepted_sample(metrics, false, 0, 100000);
  for (unsigned i = 0; i < 5; i++)
    accepted_sample(metrics, false, 0, 100001);
  accepted_sample(metrics, true, 0, 128000);
  accepted_sample(metrics, true, UINT32_MAX - 9, 5);  // 15us modular age.
  accepted_sample(metrics, true, 5, 4);               // Implausible age, excluded explicitly.
  ScalarPerformanceSnapshot snapshot;
  assert(metrics.snapshot(snapshot));
  assert(snapshot.drag_phase.count() == 100 && snapshot.drag_phase.p95_upper_ms() == 100);
  assert(snapshot.drag_phase.at_most_100ms() == 95);
  assert(snapshot.stable.count() == 2 && snapshot.stable.bins[1] == 1);
  assert(snapshot.stable.bins[128] == 1 && snapshot.stable.p95_upper_ms() == UINT32_MAX);
  assert(snapshot.stable.max_us == 128000 && snapshot.invalid_age_samples == 1);
  assert(snapshot.accepted_retirements == 103);
}

static void superseded_acceptance_is_reported_without_claiming_current_view() {
  Probe metrics;
  metrics.note_logical_origin();
  metrics.note_worker_observed_origin();
  metrics.note_submission(true, false);
  metrics.note_result(true, true, false, false, 100, 100100, true);
  ScalarPerformanceSnapshot snapshot;
  assert(metrics.snapshot(snapshot));
  assert(snapshot.accepted_retirements == 1 && snapshot.superseded_accepted_retirements == 1);
  assert(snapshot.drag_phase.count() == 1 && snapshot.drag_phase.at_most_100ms() == 1);
}

static void coherent_bounded_snapshot_refuses_active_writer() {
  Probe metrics;
  ScalarPerformanceSnapshot snapshot;
  metrics.begin_write();
  assert(!metrics.snapshot(snapshot));
  metrics.end_write();
  assert(metrics.snapshot(snapshot) && (snapshot.sequence & 1U) == 0);
  accepted_sample(metrics, false, 0, 0);
  assert(metrics.snapshot(snapshot));
  assert(snapshot.drag_phase.bins[0] == 1 && snapshot.drag_phase.p95_upper_ms() == 0);
}

int main() {
  exact_threshold_and_separate_phases();
  rejected_and_dedup_never_become_zero_latency_samples();
  quantile_overflow_and_wrap_are_explicit();
  coherent_bounded_snapshot_refuses_active_writer();
  superseded_acceptance_is_reported_without_claiming_current_view();
}
