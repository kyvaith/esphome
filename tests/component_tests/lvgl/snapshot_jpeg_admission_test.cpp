#include "esphome/components/lvgl/snapshot_cache_admission.h"

#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>

using namespace esphome::lvgl;

#define USE_ESP32
#ifndef TEST_WITHOUT_JPEG_CACHE
#define USE_LVGL_SNAPSHOT_JPEG_CACHE
#endif
#define LV_COLOR_DEPTH 32
#define YESNO(value) ((value) ? "YES" : "NO")
template<typename... Ts> void fake_log(const Ts &...) {}
#define ESP_LOGI(...) fake_log(__VA_ARGS__)
#define ESP_LOGW(...) fake_log(__VA_ARGS__)
static constexpr const char *TAG = "test";
static constexpr bool SNAPSHOT_JPEG_CACHE_ENABLED = true;
static constexpr uint32_t SNAPSHOT_JPEG_QUALITY = 100;
static constexpr size_t SNAPSHOT_TILE_SLOT_COUNT = 3;
using esp_err_t = int;
static constexpr int ESP_OK = 0;
static constexpr int ESP_ERR_NO_MEM = 257;
static constexpr int ESP_ERR_INVALID_STATE = 259;
static constexpr int ESP_ERR_TIMEOUT = 263;
enum lv_color_format_t { LV_COLOR_FORMAT_UNKNOWN, LV_COLOR_FORMAT_RGB888, LV_COLOR_FORMAT_RGB565 };
namespace esp32_jpeg {
enum class PixelFormat { RGB888 };
enum class DownSampling { YUV444 };
struct EncodeConfig {
  uint32_t width;
  uint32_t height;
  PixelFormat input_format;
  DownSampling down_sampling;
  uint32_t quality;
  bool pixel_reverse;
  bool retain_output_buffer;
  int dma2d_burst_length;
  int dma2d_descriptor_burst;
  int timeout_ms;
};
struct JpegBuffer {
  size_t bytes{0};
  size_t size() const { return this->bytes; }
  size_t capacity() const { return this->bytes; }
  JpegBuffer &operator=(JpegBuffer &&other) {
    this->bytes = std::exchange(other.bytes, 0);
    return *this;
  }
};
}  // namespace esp32_jpeg
struct lv_display_t;
struct lv_obj_t {
  lv_display_t *display{};
};
struct lv_draw_buf_t {
  struct Header {
    uint32_t w{16};
    uint32_t h{16};
    uint32_t stride{48};
    lv_color_format_t cf{LV_COLOR_FORMAT_RGB888};
  } header;
  void *data{};
  size_t data_size{768};
};
struct SnapshotCacheEntry {
  lv_obj_t *obj{};
  lv_draw_buf_t *buf{};
  bool compression_in_flight{};
  bool tile_page{};
  bool big_endian{};
  bool jpeg_stale{true};
  unsigned generation{5};
  esp32_jpeg::JpegBuffer jpeg;
  uint32_t width{16}, height{16}, stride{48};
  lv_color_format_t cf{LV_COLOR_FORMAT_RGB888};
};
static std::array<SnapshotCacheEntry, 10> snapshot_cache;
static bool *required_consumed_budget;
static unsigned navigation_queries;
struct LvglNavigation {
  void *active{};
  void *get_active_application() {
    if (required_consumed_budget != nullptr)
      assert(*required_consumed_budget);
    navigation_queries++;
    return this->active;
  }
};
struct LvglComponent {
  LvglNavigation *navigation{};
  LvglNavigation *get_navigation() const { return this->navigation; }
};
struct lv_display_t {
  LvglComponent *component{};
};
static lv_display_t *lv_obj_get_display(lv_obj_t *obj) { return obj->display; }
static void *lv_display_get_user_data(lv_display_t *display) { return display->component; }
static bool snapshot_jpeg_bootstrap_complete = true;
static bool s_swipe_logging_enabled;
static bool s_snapshot_direct_active, s_snapshot_swipe_active, s_snapshot_app_open_frame_held;
static lv_obj_t *snapshot_app_prepared_close_obj, *snapshot_app_work_obj, *snapshot_cache_latest_raw_app_obj;
static struct {
  bool active{}, worker_start_pending{};
  lv_obj_t *app_root{}, *background_root{};
} snapshot_app_state;
static struct {
  bool direct_render{};
  lv_obj_t *root{};
} snapshot_scroll_state;
static struct {
  lv_obj_t *current_root{}, *next_root{};
} snapshot_swipe_state;
struct Worker {
  std::atomic<bool> active{false}, running{false}, pending{false}, busy{false}, result_ready{false};
};
static Worker snapshot_app_worker, snapshot_swipe_worker, snapshot_scroll_worker, snapshot_tile_prefetch_worker;
static struct {
  SnapshotCacheEntry *entry{};
} snapshot_compression_worker;
static struct {
  std::array<lv_obj_t *, SNAPSHOT_TILE_SLOT_COUNT> pages{}, restore_pages{};
} snapshot_tile_window_cache;
static void snapshot_cache_destroy_jpeg(SnapshotCacheEntry &entry) {
  entry.jpeg.bytes = 0;
  entry.width = entry.height = entry.stride = 0;
  entry.cf = LV_COLOR_FORMAT_UNKNOWN;
  entry.jpeg_stale = false;
}
static bool snapshot_cache_obj_big_endian(lv_obj_t *) { return false; }
static SnapshotCacheEntry *snapshot_tile_cache_entry(lv_obj_t *page, bool) {
  for (auto &entry : snapshot_cache) {
    if (entry.obj == page)
      return &entry;
  }
  return nullptr;
}
static std::vector<esp_err_t> results;
static size_t encode_calls;
static const uint8_t *expected_input;
static uint64_t snapshot_diag_now_us_() { return 1; }
static esp_err_t snapshot_encode_jpeg(const esp32_jpeg::EncodeConfig &, const uint8_t *input, size_t,
                                      esp32_jpeg::JpegBuffer *output) {
  assert(input == expected_input);
  assert(encode_calls < results.size());
  const auto result = results[encode_calls++];
  if (result == ESP_OK)
    output->bytes = 128;
  return result;
}

// The three complete function bodies and the stack-context declaration below
// are extracted from firmware source by pytest. Only external owners/codec are faked.
#include "snapshot_jpeg_admission_under_test.inc"

int main(int argc, char **argv) {
  assert(argc == 2);
  const int test = std::atoi(argv[1]);
  LvglNavigation navigation;
  LvglComponent component{&navigation};
  lv_display_t display{&component};
  std::array<lv_obj_t, 10> objects;
  std::array<uint8_t, 768> pixels{};
  lv_draw_buf_t input;
  input.data = pixels.data();
  expected_input = pixels.data();
  for (size_t index = 0; index < objects.size(); index++) {
    objects[index].display = &display;
    snapshot_cache[index].obj = &objects[index];
    snapshot_cache[index].jpeg.bytes = index < 3 ? 123 : 200;
    snapshot_cache[index].tile_page = index < 3;
  }
  lv_obj_t *home_pages[]{&objects[0], &objects[1], &objects[2]};
  SnapshotTileEncodeAdmission admission{home_pages, 3};
#ifdef TEST_WITHOUT_JPEG_CACHE
  bool out_of_memory = true;
  assert(!snapshot_cache_encode_jpeg(snapshot_cache[0], &input, &out_of_memory));
  assert(!out_of_memory);
  assert(!snapshot_tile_cache_encode(home_pages[0], &input, &admission));
  assert(!admission.reclaim_attempted && encode_calls == 0 && navigation_queries == 0);
  assert(snapshot_cache[0].jpeg.bytes == 123 && snapshot_cache[0].generation == 5);
  std::cout << "snapshot JPEG admission feature disabled PASS\n";
  return 0;
#endif
  required_consumed_budget = &admission.reclaim_attempted;
  snapshot_tile_window_cache.pages = {home_pages[0], home_pages[1], home_pages[2]};
  results = {ESP_ERR_NO_MEM, ESP_OK};
  if (test == 0)
    results = {ESP_OK};
  else if (test == 1)
    results = {ESP_ERR_INVALID_STATE};
  else if (test == 2)
    results = {ESP_ERR_TIMEOUT};
  else if (test == 4) {
    for (size_t index = 3; index < snapshot_cache.size(); index++)
      snapshot_cache[index].jpeg.bytes = 0;
  } else if (test == 6)
    results = {ESP_ERR_NO_MEM, ESP_ERR_NO_MEM, ESP_ERR_NO_MEM};
  else if (test == 7)
    navigation.active = &objects[3];
  else if (test == 8)
    display.component = nullptr;
  else if (test == 9) {
    snapshot_cache[3].buf = &input;
    snapshot_cache[4].compression_in_flight = true;
    snapshot_compression_worker.entry = &snapshot_cache[5];
    snapshot_app_prepared_close_obj = &objects[6];
    snapshot_tile_window_cache.restore_pages[0] = &objects[7];
    snapshot_app_work_obj = &objects[8];
  } else if (test == 10)
    input.header.cf = LV_COLOR_FORMAT_RGB565;
  else if (test == 11)
    snapshot_tile_prefetch_worker.busy = true;
  else if (test == 12)
    snapshot_tile_prefetch_worker.result_ready = true;
  else if (test == 13)
    snapshot_app_state.worker_start_pending = true;
  else if (test == 14)
    snapshot_app_worker.running = true;
  else if (test == 15)
    s_snapshot_app_open_frame_held = true;
  else if (test == 16)
    results = {ESP_ERR_NO_MEM, ESP_OK, ESP_ERR_NO_MEM};
  else if (test == 17)
    snapshot_scroll_state.direct_render = true;

  const bool success = snapshot_tile_cache_encode(home_pages[0], &input, test == 3 ? nullptr : &admission);
  const bool expected_success = test == 0 || test == 5 || test == 9 || test == 16;
  assert(success == expected_success);
  assert(snapshot_cache[0].generation == (success ? 6U : 5U));
  assert(snapshot_cache[0].jpeg.bytes == (success ? 128U : 123U));
  assert(snapshot_cache[0].jpeg_stale == !success);
  assert(snapshot_cache[1].jpeg.bytes == 123 && snapshot_cache[2].jpeg.bytes == 123);
  const bool expected_reclaim = test != 0 && test != 1 && test != 2 && test != 3 && test != 10;
  assert(admission.reclaim_attempted == expected_reclaim);
  assert(encode_calls == (test == 10 ? 0U : ((test == 5 || test == 6 || test == 9 || test == 16) ? 2U : 1U)));
  if (test == 0 || test == 1 || test == 2 || test == 3 || test == 7 || test == 8 || test == 10 || test >= 11) {
    if (test != 16)
      assert(snapshot_cache[3].jpeg.bytes == 200);
  }
  if (test == 9) {
    for (size_t index = 3; index < 9; index++)
      assert(snapshot_cache[index].jpeg.bytes == 200 && snapshot_cache[index].generation == 5);
    assert(snapshot_cache[9].jpeg.bytes == 0 && snapshot_cache[9].generation == 6);
  }
  if (test == 6 || test == 16) {
    // Even a later page (or repeated request) cannot acquire a second reclaim
    // budget, including when a new optional preview has appeared meanwhile.
    snapshot_cache[3].jpeg.bytes = 201;
    const auto queries = navigation_queries;
    const auto *page = test == 16 ? home_pages[1] : home_pages[0];
    assert(!snapshot_tile_cache_encode(const_cast<lv_obj_t *>(page), &input, &admission));
    assert(encode_calls == 3 && navigation_queries == queries);
    assert(snapshot_cache[3].jpeg.bytes == 201);
    assert(snapshot_cache[1].jpeg.bytes == 123 && snapshot_cache[1].generation == 5);
  }
  std::cout << "snapshot JPEG admission case " << test << " PASS\n";
}
