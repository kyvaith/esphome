#include "esphome/components/lvgl/scroll_snapshot_geometry.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

using namespace esphome::lvgl;

struct Buffer {
  size_t bytes;
};
struct Segment {
  Buffer *buffer;
  int y;
  int height;
  bool owned;
};

int main() {
  constexpr size_t stride = 1600;
  constexpr int height = 1436;
  constexpr int viewport = 800;
  int allocated = 0;
  int destroyed = 0;
  auto destroy = [&](Buffer *buffer) {
    destroyed++;
    delete buffer;
  };
  auto take = [&](int, int rows) {
    allocated++;
    return new Buffer{stride * rows};
  };
  std::vector<Segment> segments;
  assert(scroll_snapshot_rows_per_segment(stride) == 163);
  assert(scroll_snapshot_segment_count(height, stride) == 9);
  assert(scroll_snapshot_capture_partition(segments, height, stride, take, destroy));
  assert(allocated == 9 && destroyed == 0);
  size_t bytes = 0;
  int next_row = 0;
  for (const auto &segment : segments) {
    assert(segment.y == next_row);
    assert(segment.buffer->bytes <= SCROLL_SNAPSHOT_SEGMENT_TARGET_BYTES);
    next_row += segment.height;
    bytes += segment.buffer->bytes;
  }
  assert(next_row == height && bytes == 2297600 && segments.back().height == 132);

  // Every legal scroll offset, including each exact boundary and neighbors,
  // crosses five or six pieces without a missing, duplicated or excess row.
  for (int y = 0; y <= height - viewport; y++) {
    int expected_row = y;
    int spans = 0;
    assert(scroll_snapshot_visit_spans(segments, y, viewport, [&](size_t index, int local, int copied, int rows) {
      const auto &segment = segments[index];
      assert(rows > 0 && local >= 0 && local + rows <= segment.height);
      assert(segment.y + local == expected_row);
      assert(copied == expected_row - y);
      expected_row += rows;
      spans++;
      return true;
    }));
    assert(expected_row == y + viewport && spans >= 5 && spans <= 6);
  }
  // Overscroll keeps the existing 120 black rows plus 680 copied rows.
  for (int source_y : {0, height - viewport + 120}) {
    int copied = 0;
    assert(scroll_snapshot_visit_spans(segments, source_y, viewport - 120, [&](size_t, int, int, int rows) {
      copied += rows;
      return true;
    }));
    assert(copied + 120 == viewport);
  }
  int copies = 0;
  assert(!scroll_snapshot_visit_spans(segments, -1, viewport, [](auto...) { return true; }));
  assert(!scroll_snapshot_visit_spans(segments, height - viewport + 1, viewport, [](auto...) { return true; }));
  assert(!scroll_snapshot_visit_spans(segments, 0, viewport, [&](auto...) {
    copies++;
    return false;
  }));
  assert(copies == 1);
  scroll_snapshot_release_segments(segments, destroy);
  assert(segments.empty() && destroyed == allocated);

  // Fail at each possible piece: every previously allocated buffer is retired
  // exactly once and no incomplete partition remains available to a presenter.
  for (int fail_at = 0; fail_at < 9; fail_at++) {
    allocated = destroyed = 0;
    int calls = 0;
    auto failing_take = [&](int, int rows) -> Buffer * {
      if (calls++ == fail_at)
        return nullptr;
      allocated++;
      return new Buffer{stride * rows};
    };
    assert(!scroll_snapshot_capture_partition(segments, height, stride, failing_take, destroy));
    assert(segments.empty() && allocated == fail_at && destroyed == fail_at);
  }
  // A borrowed external fast-path surface is returned, never freed by LVGL.
  Buffer external{stride * height};
  segments.push_back({&external, 0, height, false});
  destroyed = 0;
  scroll_snapshot_release_segments(segments, destroy);
  assert(segments.empty() && destroyed == 0);

  assert(scroll_snapshot_segment_count(0, stride) == 0);
  assert(scroll_snapshot_segment_count(height, 0) == 0);
  assert(!scroll_snapshot_capture_partition(segments, 0, stride, take, destroy));
  assert(!scroll_snapshot_capture_partition(segments, height, 0, take, destroy));
  const size_t maximum = std::numeric_limits<size_t>::max();
  assert(scroll_snapshot_segment_count(maximum, stride) == maximum / 163 + (maximum % 163 != 0));
  assert(scroll_snapshot_rows_per_segment(SCROLL_SNAPSHOT_SEGMENT_TARGET_BYTES + 1) == 1);

  std::cout << "scroll snapshot planner, all 637 viewports, overscroll, rollback and borrowed ownership PASS\n";
}
