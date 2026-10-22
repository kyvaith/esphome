#include "esphome/components/lvgl/snapshot_cache_admission.h"

#include <array>
#include <cassert>
#include <cstdlib>
#include <iostream>

using namespace esphome::lvgl;

int main(int argc, char **argv) {
  assert(argc == 2);
  const int test = std::atoi(argv[1]);
  SnapshotPreviewReclaimState state{true, false, false, false, false, 1024};
  if (test < 7) {
    switch (test) {
      case 1:
        state.has_object = false;
        break;
      case 2:
        state.jpeg_bytes = 0;
        break;
      case 3:
        state.tile_page = true;
        break;
      case 4:
        state.has_raw = true;
        break;
      case 5:
        state.in_flight = true;
        break;
      case 6:
        state.protected_owner = true;
        break;
    }
    assert(snapshot_preview_can_reclaim(state) == (test == 0));
  } else {
    int reserve_calls = 0;
    int reclaim_calls = 0;
    int report_calls = 0;
    bool reported_retry = false;
    bool reported_restored = false;
    SnapshotPreviewReclaimStats reported;
    auto reserve = [&]() {
      reserve_calls++;
      // Model the first complete reserve having already rolled back to its
      // retained background before any optional compressed preview is freed.
      if (reserve_calls == 1) {
        assert(reclaim_calls == 0);
        return test == 7;
      }
      assert(reserve_calls == 2 && reclaim_calls == 1);
      return test == 9 || test == 11;
    };
    auto reclaim = [&]() {
      assert(reserve_calls == 1);
      reclaim_calls++;
      if (test == 8)
        return SnapshotPreviewReclaimStats{};
      if (test == 11) {
        // Only two closed compressed applications may be reclaimed. Home
        // backing, the current close background, a raw reader and an active
        // compression owner survive, even when reclaim would otherwise help.
        std::array<SnapshotPreviewReclaimState, 6> entries{{
            {true, false, false, false, false, 100},
            {true, true, false, false, false, 200},
            {true, false, false, false, true, 300},
            {true, false, true, false, false, 400},
            {true, false, false, true, false, 500},
            {true, false, false, false, false, 600},
        }};
        SnapshotPreviewReclaimStats stats;
        for (auto &entry : entries) {
          if (snapshot_preview_can_reclaim(entry)) {
            stats.bytes += entry.jpeg_bytes;
            stats.count++;
            entry.jpeg_bytes = 0;
          }
        }
        assert(entries[1].jpeg_bytes == 200 && entries[2].jpeg_bytes == 300);
        assert(entries[3].jpeg_bytes == 400 && entries[4].jpeg_bytes == 500);
        assert(stats.bytes == 700 && stats.count == 2);
        return stats;
      }
      return SnapshotPreviewReclaimStats{700, 2};
    };
    auto report = [&](const SnapshotPreviewReclaimStats &stats, bool retry, bool restored) {
      report_calls++;
      reported = stats;
      reported_retry = retry;
      reported_restored = restored;
    };
    const bool admitted = snapshot_reserve_with_preview_reclaim(reserve, reclaim, report);
    if (test == 7) {
      assert(admitted && reserve_calls == 1 && reclaim_calls == 0 && report_calls == 0);
    } else if (test == 8) {
      assert(!admitted && reserve_calls == 1 && reclaim_calls == 1 && report_calls == 1);
      assert(reported.bytes == 0 && !reported_retry && !reported_restored);
    } else {
      assert(reserve_calls == 2 && reclaim_calls == 1 && report_calls == 1);
      assert(reported.bytes == 700 && reported.count == 2 && reported_retry);
      assert(admitted == (test != 10) && reported_restored == admitted);
    }
  }
  std::cout << "cache admission case " << test << " PASS\n";
}
