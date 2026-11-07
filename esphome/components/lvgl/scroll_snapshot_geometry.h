#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace esphome::lvgl {

// The existing maximum content byte budget bounds both pixel storage and the
// number of row-aligned pieces, derived from the configured stride and height.
constexpr size_t SCROLL_SNAPSHOT_SEGMENT_TARGET_BYTES = 256 * 1024;

// Coalescing input and integer easing can request the same pixels repeatedly.
// Always submit the endpoint so completion retains the normal present contract.
inline bool scroll_snapshot_should_render(int presented_y, int requested_y, bool final_frame) {
  return final_frame || presented_y != requested_y;
}

inline size_t scroll_snapshot_rows_per_segment(size_t stride) {
  return stride == 0 ? 0 : std::max<size_t>(1, SCROLL_SNAPSHOT_SEGMENT_TARGET_BYTES / stride);
}

inline size_t scroll_snapshot_segment_count(size_t height, size_t stride) {
  const size_t rows = scroll_snapshot_rows_per_segment(stride);
  return rows == 0 ? 0 : height / rows + (height % rows != 0);
}

template<typename Segments, typename Destroy>
void scroll_snapshot_release_segments(Segments &segments, Destroy &&destroy) {
  for (const auto &segment : segments) {
    if (segment.owned && segment.buffer != nullptr)
      destroy(segment.buffer);
  }
  segments.clear();
}

// Publish only a complete partition. A failure at any piece returns every new
// allocation, while borrowed external storage is never passed to destroy().
template<typename Segments, typename Take, typename Destroy>
bool scroll_snapshot_capture_partition(Segments &segments, int height, size_t stride, Take &&take, Destroy &&destroy,
                                       size_t target_bytes = SCROLL_SNAPSHOT_SEGMENT_TARGET_BYTES) {
  if (height <= 0 || stride == 0 || !segments.empty())
    return false;
  const size_t minimum_rows = scroll_snapshot_rows_per_segment(stride);
  size_t rows_per_segment = std::max(minimum_rows, target_bytes / stride);
  segments.reserve(scroll_snapshot_segment_count(static_cast<size_t>(height), stride));
  for (int y = 0; y < height;) {
    int rows = static_cast<int>(std::min<size_t>(height - y, rows_per_segment));
    auto *buffer = take(y, rows);
    // A larger opt-in target reduces hardware submissions per viewport. Keep
    // successful pieces, and bound failed attempts by halving down to the
    // existing target. The old default still makes exactly one attempt/piece.
    while (buffer == nullptr && static_cast<size_t>(rows) > minimum_rows) {
      rows_per_segment = std::max(minimum_rows, static_cast<size_t>(rows) / 2);
      rows = static_cast<int>(std::min<size_t>(height - y, rows_per_segment));
      buffer = take(y, rows);
    }
    if (buffer == nullptr) {
      scroll_snapshot_release_segments(segments, destroy);
      return false;
    }
    segments.push_back({buffer, y, rows, true});
    y += rows;
  }
  return true;
}

// A viewport may span any number of pieces. Never read a neighboring piece via
// an out-of-bounds row or present a frame after an incomplete hardware copy.
template<typename Segments, typename Copy>
bool scroll_snapshot_visit_spans(const Segments &segments, int source_y, int rows, Copy &&copy) {
  if (source_y < 0 || rows < 0)
    return false;
  int copied = 0;
  for (size_t index = 0; index < segments.size() && copied < rows; index++) {
    const auto &segment = segments[index];
    if (source_y < segment.y)
      return false;
    const int local_y = source_y - segment.y;
    if (local_y >= segment.height)
      continue;
    const int count = std::min(rows - copied, segment.height - local_y);
    if (!copy(index, local_y, copied, count))
      return false;
    source_y += count;
    copied += count;
  }
  return copied == rows;
}

}  // namespace esphome::lvgl
