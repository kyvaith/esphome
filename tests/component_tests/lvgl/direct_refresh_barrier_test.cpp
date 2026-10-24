#include "esphome/components/lvgl/direct_refresh_barrier.h"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <vector>

#define LV_COLOR_DEPTH 32
#define USE_ESP32
#define USE_MIPI_DSI
#define USE_LVGL_PPA

constexpr int pdTRUE = 1;
constexpr int pdFALSE = 0;
constexpr uint32_t portMAX_DELAY = UINT32_MAX;
constexpr uint32_t pdMS_TO_TICKS(uint32_t value) { return value; }

struct Mutex {
  bool locked{};
  bool fail_take{};
  unsigned takes{}, gives{}, blocked{};
};
Mutex *s_region_srm_mutex{};
int xSemaphoreTake(Mutex *mutex, uint32_t) {
  mutex->takes++;
  if (mutex->locked || mutex->fail_take) {
    mutex->blocked++;
    return pdFALSE;
  }
  mutex->locked = true;
  return pdTRUE;
}
void xSemaphoreGive(Mutex *mutex) {
  assert(mutex->locked);
  mutex->locked = false;
  mutex->gives++;
}
uint32_t micros() {
  static uint32_t value{};
  return ++value;
}
struct Area {
  int32_t x1{}, y1{}, x2{}, y2{};
};
struct DrawBuffer {
  uint8_t *data{};
};
using lv_draw_buf_t = DrawBuffer;
struct Display {
  int32_t width{320}, height{480};
  Area inv_areas[8]{};
  unsigned inv_p{};
  DrawBuffer *buf_act{}, *buf_1{}, *buf_2{};
};
int32_t lv_display_get_horizontal_resolution(Display *display) { return display->width; }
int32_t lv_display_get_vertical_resolution(Display *display) { return display->height; }
struct lv_event_t {
  void *user_data{};
};
void *lv_event_get_user_data(lv_event_t *event) { return event->user_data; }

namespace esphome::mipi_dsi {
struct MipiDsi {
  uint8_t *active{}, *reserved{};
  bool queue_idle{true}, reservation_allowed{true};
  unsigned reserve_calls{}, release_calls{};
  bool wait_for_direct_frame_queue_idle(unsigned) const { return queue_idle; }
  uint8_t *get_presented_frame_buffer() const { return active; }
  bool reserve_direct_render_frame_buffer(uint8_t *target) {
    reserve_calls++;
    assert(target != active);
    if (!reservation_allowed)
      return false;
    assert(reserved == nullptr);
    reserved = target;
    return true;
  }
  void release_direct_render_frame_buffer(uint8_t *target) {
    assert(reserved == target);
    reserved = nullptr;
    release_calls++;
  }
};
}  // namespace esphome::mipi_dsi

namespace esphome::lvgl {
struct LvglComponent {
  Display *disp_{};
  std::vector<mipi_dsi::MipiDsi *> displays_;
  bool direct_region_full_refresh_barrier_{};
  bool direct_region_full_refresh_barrier_active_{};
  bool direct_region_refresh_locked_{};
  uint8_t *direct_region_refresh_reserved_buffer_{};
  uint8_t *direct_last_flushed_buf_{};
  std::atomic<uint32_t> direct_region_full_refresh_barriers_{};
  std::atomic<uint32_t> direct_region_concurrent_refreshes_{};
  std::atomic<uint32_t> direct_region_refresh_fallbacks_{};
  std::atomic<uint32_t> direct_region_refresh_relock_us_{};
  std::atomic<uint32_t> direct_region_refresh_relock_max_us_{};
  void direct_region_refresh_begin_();
  void direct_region_refresh_allow_compositor_();
  void direct_region_refresh_relock_();
  void direct_region_refresh_end_();
  static void direct_region_refresh_end_cb(lv_event_t *event);
};
#include "actual_direct_refresh_methods.inc"
}  // namespace esphome::lvgl

struct Fixture {
  Mutex mutex;
  uint8_t pixels[3]{};
  DrawBuffer first{&pixels[0]}, second{&pixels[1]};
  Display display;
  esphome::mipi_dsi::MipiDsi mipi;
  esphome::lvgl::LvglComponent component;
  explicit Fixture(bool enabled = true, bool full = true) {
    s_region_srm_mutex = &mutex;
    display.buf_act = display.buf_1 = &first;
    display.buf_2 = &second;
    display.inv_p = 1;
    display.inv_areas[0] = full ? Area{0, 0, 319, 479} : Area{20, 30, 80, 90};
    mipi.active = first.data;
    component.disp_ = &display;
    component.displays_.push_back(&mipi);
    component.direct_region_full_refresh_barrier_ = enabled;
  }
  void end() {
    component.direct_region_refresh_end_();
    assert(!mutex.locked && !mipi.reserved && !component.direct_region_refresh_reserved_buffer_);
    assert(!component.direct_region_full_refresh_barrier_active_);
  }
};

int main() {
  using esphome::lvgl::direct_region_has_full_display_invalidation;
  const Area full{0, 0, 319, 479}, clipped{-10, -10, 400, 500}, short_row{0, 0, 318, 479};
  assert(direct_region_has_full_display_invalidation(&full, 1, 320, 480));
  assert(direct_region_has_full_display_invalidation(&clipped, 1, 320, 480));
  assert(!direct_region_has_full_display_invalidation(&short_row, 1, 320, 480));
  assert(!direct_region_has_full_display_invalidation(static_cast<const Area *>(nullptr), 1, 320, 480));
  assert(!direct_region_has_full_display_invalidation(&full, 0, 320, 480));
  assert(!direct_region_has_full_display_invalidation(&full, 1, 0, 480));
  const Area gaps[]{{0, 0, 40, 40}, {280, 440, 319, 479}};
  assert(!direct_region_has_full_display_invalidation(gaps, 2, 320, 480));
  const Area very_large{0, 0, INT32_MAX - 1, INT32_MAX - 1};
  assert(direct_region_has_full_display_invalidation(&very_large, 1, INT32_MAX, INT32_MAX));
  for (const bool enabled : {false, true}) {
    for (const bool full_refresh : {false, true}) {
      Fixture f(enabled, full_refresh);
      auto &c = f.component;
      c.direct_region_refresh_begin_();
      assert(f.mutex.locked && f.mipi.reserved == f.second.data && f.display.buf_act == &f.second);
      assert(c.direct_region_concurrent_refreshes_ == 1);
      c.direct_region_refresh_allow_compositor_();
      const bool barrier = enabled && full_refresh;
      assert(f.mutex.locked == barrier);
      assert(c.direct_region_full_refresh_barrier_active_ == barrier);
      assert(c.direct_region_full_refresh_barriers_ == unsigned(barrier));
      const unsigned taken = f.mutex.takes;
      c.direct_region_refresh_relock_();
      assert(f.mutex.takes == taken + unsigned(!barrier));
      f.end();
      assert(f.mipi.release_calls == 1);
      f.end();  // Cleanup is idempotent; never release another owner's mutex.
      assert(f.mipi.release_calls == 1);
    }
  }
  {
    Fixture f(true, false);
    auto &c = f.component;
    c.direct_region_refresh_begin_();
    f.display.inv_areas[0] = full;  // Layout added this after REFR_START.
    c.direct_region_refresh_allow_compositor_();
    c.direct_region_refresh_allow_compositor_();
    assert(f.mutex.locked && c.direct_region_full_refresh_barriers_ == 1);
    // The independent regional worker stays blocked while native drawing owns
    // the barrier, then makes progress after the existing end callback.
    assert(xSemaphoreTake(&f.mutex, 0) == pdFALSE);
    lv_event_t deleted_display{&c};
    c.direct_region_refresh_end_cb(&deleted_display);
    assert(!f.mutex.locked && f.mipi.release_calls == 1);
    assert(xSemaphoreTake(&f.mutex, 0) == pdTRUE);
    xSemaphoreGive(&f.mutex);
    f.end();
    f.display.inv_areas[0] = gaps[0];
    c.direct_region_refresh_begin_();
    c.direct_region_refresh_allow_compositor_();
    assert(!f.mutex.locked);  // A prior full-frame barrier must not stick.
    f.end();
  }
  for (unsigned failure = 0; failure < 5; failure++) {
    Fixture f;
    if (failure == 0)
      f.mipi.queue_idle = false;
    if (failure == 1)
      f.mipi.reservation_allowed = false;
    if (failure == 2)
      f.mipi.active = nullptr;
    if (failure == 3)
      f.component.displays_.clear();
    if (failure == 4)
      f.mutex.fail_take = true;
    f.component.direct_region_refresh_begin_();
    f.component.direct_region_refresh_allow_compositor_();
    assert(f.mipi.reserved == nullptr);
    if (failure < 4)
      assert(f.mutex.locked);
    f.end();
  }
  {
    Fixture f;
    f.component.disp_ = nullptr;
    f.component.direct_region_refresh_begin_();
    assert(f.mutex.takes == 0);
    f.end();
    s_region_srm_mutex = nullptr;
    f.component.disp_ = &f.display;
    f.component.direct_region_refresh_begin_();
    assert(f.mutex.takes == 0);
  }
  return 0;
}
