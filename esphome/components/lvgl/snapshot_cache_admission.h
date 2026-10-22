#pragma once

#include <cstddef>

namespace esphome::lvgl {

struct SnapshotPreviewReclaimState {
  bool has_object{false};
  bool tile_page{false};
  bool has_raw{false};
  bool in_flight{false};
  bool protected_owner{false};
  size_t jpeg_bytes{0};
};

struct SnapshotPreviewReclaimStats {
  size_t bytes{0};
  size_t count{0};
};

constexpr bool snapshot_preview_can_reclaim(const SnapshotPreviewReclaimState &state) {
  return state.has_object && state.jpeg_bytes != 0 && !state.tile_page && !state.has_raw && !state.in_flight &&
         !state.protected_owner;
}

// The first reserve must finish its rollback before reclaim can run. Optional
// idle previews yield to the complete raw window; a failed retry stays failed.
template<typename Reserve, typename Reclaim, typename Report>
bool snapshot_reserve_with_preview_reclaim(Reserve &&reserve, Reclaim &&reclaim, Report &&report) {
  if (reserve())
    return true;
  const SnapshotPreviewReclaimStats reclaimed = reclaim();
  const bool retry = reclaimed.bytes != 0;
  const bool restored = retry && reserve();
  report(reclaimed, retry, restored);
  return restored;
}

}  // namespace esphome::lvgl
