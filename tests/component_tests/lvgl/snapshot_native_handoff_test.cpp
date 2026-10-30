// Execute the production completion functions against deterministic display owners.
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static std::vector<std::string> trace;
static unsigned pause_depth{2};  // A caller already owns another nested pause.
static bool source_borrowed;
static bool handoff_result{true};
static bool buffers_ready{true};
static bool render_result{true};
static int presented_base{42};
// A pixel in a static control, outside the next clock invalidation.
static std::array<int, 3> static_pixels{7, 9, 42};
static void expect_coherent() {
  for (int pixel : static_pixels)
    assert(pixel == presented_base);
}

struct LvglComponent {
  int display;
  bool direct_handoff_to_lvgl(uint32_t timeout) {
    assert(timeout == 100);
    assert(pause_depth == 2);
    assert(!source_borrowed);
    trace.emplace_back("handoff");
    if (!handoff_result)
      return false;
    static_pixels.fill(presented_base);
    return true;
  }
  // The old implementation aligned the pointer but left static pixels stale.
  bool realign_direct_buffer_after_manual_present(bool synchronize) {
    assert(!synchronize);
    trace.emplace_back("align-only");
    return true;
  }
  bool wait_for_direct_frame_presented(uint32_t timeout) {
    assert(timeout == 50);
    trace.emplace_back("wait");
    return true;
  }
  void synchronize_direct_framebuffer_rows(int y, int height) {
    assert(y == 5 && height == 1);
    trace.emplace_back("indicator");
  }
  int *get_disp() { return &display; }
} component;

static bool s_snapshot_swipe_active{true};
static bool s_snapshot_direct_active{true};
static bool s_swipe_logging_enabled{false};
static std::atomic<int> s_snapshot_page_indicator_y{5};
static std::atomic<int> s_snapshot_page_indicator_height{1};
static uint64_t snapshot_diag_now_us_() { return 0; }
static const char *const TAG = "test";
template<typename... Args> static void test_log(Args &&...) {}
#define ESP_LOGI(...) test_log(__VA_ARGS__)
#define YESNO(x) ((x) ? "YES" : "NO")

static struct {
  bool direct_render{true};
  LvglComponent *component{&::component};
  int current_x{}, next_x{}, finish_current_x{}, finish_next_x{};
  bool commit{true};
  bool edge_bounce{};
  void *current_root{&::component};
  bool owns_current_buf{};
  void *current_buf{&::component};
} snapshot_swipe_state;

static bool snapshot_swipe_render_direct_frame(int, int) {
  assert(pause_depth == 2);
  trace.emplace_back("render-final");
  if (render_result)
    presented_base = 84;
  return render_result;
}
static void snapshot_swipe_apply_final_roots() { trace.emplace_back("roots"); }
static void snapshot_swipe_discard_pending_refresh(int *) {
  expect_coherent();
  assert(pause_depth == 2);
  trace.emplace_back("discard");
}
static int screen;
static int *lv_screen_active() { return &screen; }
static void lv_obj_invalidate(int *) { trace.emplace_back("invalidate"); }
static void lv_refr_now(int *) {
  assert(!s_snapshot_swipe_active && !s_snapshot_direct_active);
  assert(pause_depth == 2);
  trace.emplace_back("native-refresh");
  static_pixels.fill(presented_base);
}
static void snapshot_cache_release_decoded_if_compressed(void *) { trace.emplace_back("cache-release"); }
static void snapshot_swipe_cleanup() {
  assert(pause_depth == 2);
  pause_depth--;
  trace.emplace_back("cleanup");
}

#include "lvgl_esphome.cpp.inc"

static bool held{true};
static bool work_held{true};
static void lvgl_esphome_snapshot_app_release_open_hold() {
  expect_coherent();
  assert(held && pause_depth == 2);
  held = false;
  pause_depth--;
  trace.emplace_back("hold-release");
}
static void lvgl_esphome_snapshot_app_release_work_buffer() {
  assert(work_held && !source_borrowed);
  work_held = false;
  trace.emplace_back("work-release");
}
static bool lvgl_esphome_snapshot_tile_buffers_ready() { return buffers_ready; }
struct Navigation {
  void prepare_application_transition(void *, bool, bool) {
    expect_coherent();
    assert(!source_borrowed && held);
    trace.emplace_back("prepare-native");
  }
  void activate_home_view(int index) {
    assert(index == 1);
    trace.emplace_back("activate-home");
  }
  void complete_application_transition(void *, bool, bool) {
    assert(!held && !work_held);
    trace.emplace_back("complete-native");
  }
} navigation;
struct LvglDirectSnapshotCompositor {
  enum class DirectApplicationPhase { NONE, OPENING, CLOSING };
  void *application_{&screen};
  int application_home_index_{1};
  DirectApplicationPhase direct_application_phase_{DirectApplicationPhase::CLOSING};
  LvglComponent *parent_{&component};
  Navigation *navigation_{&navigation};
  bool home_prepared_{};
  unsigned released_sources{};
  void release_external_application_transition_buffer_() {
    if (source_borrowed) {
      source_borrowed = false;
      released_sources++;
      trace.emplace_back("source-release");
    }
  }
  void reset_direct_application_() {
    direct_application_phase_ = DirectApplicationPhase::NONE;
    application_ = nullptr;
    trace.emplace_back("reset");
  }
  void prepare_home(int index) {
    assert(index == 1);
    trace.emplace_back("prepare-home");
  }
  void complete_direct_application_();
};

#include "lvgl_direct_snapshot_compositor.cpp.inc"

static bool saw(const char *event) { return std::find(trace.begin(), trace.end(), event) != trace.end(); }
static size_t position(const char *event) {
  auto found = std::find(trace.begin(), trace.end(), event);
  assert(found != trace.end());
  return found - trace.begin();
}

int main(int argc, char **argv) {
  assert(argc == 2);
  const int test_case = std::atoi(argv[1]);
  if (test_case < 5) {
    if (test_case == 1)
      snapshot_swipe_state.finish_current_x = 1;
    if (test_case == 2)
      handoff_result = false;
    if (test_case == 3)
      snapshot_swipe_state.direct_render = false;
    if (test_case == 4)
      snapshot_swipe_state.edge_bounce = true;
    snapshot_swipe_finish_now();
    assert(pause_depth == 1);
    if (test_case == 3) {
      assert(!saw("handoff") && saw("invalidate"));
    } else {
      expect_coherent();
      assert(saw("handoff"));
      assert(position("handoff") < position("roots"));
      if (test_case == 2) {
        assert(!saw("discard") && saw("native-refresh"));
        assert(position("native-refresh") < position("cleanup"));
      } else {
        assert(saw("discard") && !saw("native-refresh"));
      }
      if (test_case == 1)
        assert(presented_base == 84 && saw("render-final"));
      if (test_case == 4) {
        assert(position("cache-release") < position("cleanup"));
        assert(snapshot_swipe_state.current_buf == nullptr);
      }
    }
  } else {
    LvglDirectSnapshotCompositor compositor;
    source_borrowed = true;
    if (test_case == 5)
      compositor.direct_application_phase_ = LvglDirectSnapshotCompositor::DirectApplicationPhase::OPENING;
    if (test_case == 7) {
      handoff_result = false;
      compositor.complete_direct_application_();
      assert(compositor.released_sources == 1);
      assert(held && work_held && pause_depth == 2);
      assert(compositor.direct_application_phase_ == LvglDirectSnapshotCompositor::DirectApplicationPhase::CLOSING);
      assert(!saw("prepare-native") && !saw("complete-native") && !saw("reset"));
      assert(static_pixels[0] == 7);
      handoff_result = true;
      trace.clear();
      buffers_ready = false;
    }
    compositor.complete_direct_application_();
    expect_coherent();
    assert(compositor.released_sources == 1);
    assert(!held && !work_held && pause_depth == 1);
    assert(compositor.direct_application_phase_ == LvglDirectSnapshotCompositor::DirectApplicationPhase::NONE);
    assert(position("handoff") < position("prepare-native"));
    assert(position("prepare-native") < position("hold-release"));
    assert(position("work-release") < position("complete-native"));
    if (test_case == 5) {
      assert(!saw("activate-home") && !compositor.home_prepared_);
    } else {
      assert(saw("activate-home"));
      assert(test_case == 7 ? saw("prepare-home") : compositor.home_prepared_);
    }
  }
  std::printf("case %d PASS\n", test_case);
}
