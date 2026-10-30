#pragma once

// Host-only ESPHome/RTOS/display-lease facade. Every lv_* type and API below
// comes from the unchanged, separately compiled LVGL dependency.
#include <lvgl.h>
#include "direct_region_generation.h"
#include "navigation_admission.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#ifndef configTICK_RATE_HZ
#define configTICK_RATE_HZ 1000
#endif

namespace real_lvgl_test {
struct TraceEvent {
  size_t ordinal;
  uint32_t tick;
  std::string tag;
  uint32_t base, a, b;
};
inline std::vector<TraceEvent> trace;
inline std::function<void(const TraceEvent &)> trace_hook;
inline void record(const char *tag, uint32_t base = 0, uint32_t a = 0, uint32_t b = 0) {
  TraceEvent event{trace.size(), lv_tick_get(), tag, base, a, b};
  trace.push_back(event);
  if (trace_hook)
    trace_hook(event);
}
inline void advance(uint32_t milliseconds, bool refresh = true) {
  for (uint32_t remaining = milliseconds; remaining != 0;) {
    const uint32_t step = std::min(remaining, uint32_t{10});
    lv_tick_inc(step);
    lv_timer_handler();
    if (refresh)
      lv_refr_now(nullptr);
    remaining -= step;
  }
}
}  // namespace real_lvgl_test

inline size_t test_allocations = 0;
inline uint32_t test_delay_total = 0;
inline std::function<void()> test_notify_hook, test_delay_hook;

using StackType_t = uint8_t;
struct StaticTask_t {};
using TaskHandle_t = void *;
inline constexpr int pdTRUE = 1;
inline constexpr uint32_t portMAX_DELAY = UINT32_MAX;
inline constexpr int MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2;
inline uint32_t pdMS_TO_TICKS(uint32_t duration) { return duration; }
inline std::vector<std::pair<void *, void *>> test_aligned_allocations;
inline void *heap_caps_malloc(size_t bytes, int) {
  ++test_allocations;
  return std::malloc(bytes);
}
inline void *heap_caps_aligned_alloc(size_t alignment, size_t bytes, int) {
  ++test_allocations;
  // C++ host only. Allocation is freed using the paired host shim below.
  void *raw = std::malloc(bytes + alignment - 1 + sizeof(void *));
  if (raw == nullptr)
    return nullptr;
  const uintptr_t address = (reinterpret_cast<uintptr_t>(raw) + sizeof(void *) + alignment - 1) & ~(alignment - 1);
  auto *aligned = reinterpret_cast<void *>(address);
  reinterpret_cast<void **>(aligned)[-1] = raw;
  test_aligned_allocations.emplace_back(aligned, raw);
  return aligned;
}
inline void heap_caps_free(void *pointer) {
  auto found = std::find_if(test_aligned_allocations.begin(), test_aligned_allocations.end(),
                            [pointer](const auto &entry) { return entry.first == pointer; });
  if (found != test_aligned_allocations.end()) {
    std::free(found->second);
    test_aligned_allocations.erase(found);
  } else {
    std::free(pointer);
  }
}
inline TaskHandle_t xTaskCreateStaticPinnedToCore(void (*)(void *), const char *, uint32_t, void *, int, StackType_t *,
                                                 StaticTask_t *, int) {
  return reinterpret_cast<void *>(1);
}
inline void xTaskNotifyGive(TaskHandle_t) {}
inline uint32_t ulTaskNotifyTake(int, uint32_t) {
  if (test_notify_hook)
    test_notify_hook();
  return 1;
}
inline void vTaskDelay(uint32_t duration) {
  lv_tick_inc(duration);
  test_delay_total += duration;
  if (test_delay_hook)
    test_delay_hook();
}
inline void vTaskDelete(TaskHandle_t) {}

using LvglDirectBlitReadyCallback = void (*)(void *);
inline constexpr uint8_t LVGL_DIRECT_BLIT_REJECTED = 0, LVGL_DIRECT_BLIT_BUSY = 1, LVGL_DIRECT_BLIT_SUBMITTED = 2;

namespace esphome {
inline uint32_t millis() { return lv_tick_get(); }
inline void delay(uint32_t duration) {
  lv_tick_inc(duration);
  test_delay_total += duration;
  if (test_delay_hook)
    test_delay_hook();
}
struct TestApp {
  uint32_t get_loop_component_start_time() const { return lv_tick_get(); }
  uint32_t get_loop_component_start_time_us() const { return lv_tick_get() * 1000; }
};
inline TestApp App;
namespace setup_priority {
inline constexpr float PROCESSOR = 400;
}
class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
  virtual void on_shutdown() {}
  virtual void dump_config() {}
  virtual float get_setup_priority() const { return 0; }
  bool is_failed() const { return this->failed_; }
  void mark_failed(const char * = nullptr) { this->failed_ = true; }
  void status_set_warning() { this->warning_ = true; }
 protected:
  bool failed_{false}, warning_{false};
};
class StringRef {
 public:
  StringRef() = default;
  explicit StringRef(const char *value) : value_(value) {}
  bool empty() const { return this->value_[0] == 0; }
  const char *c_str() const { return this->value_; }
 protected:
  const char *value_{""};
};
template<typename T> class LazyCallbackManager;
template<typename... Ts> class LazyCallbackManager<void(Ts...)> {
 public:
  template<typename F> void add(F &&callback) { this->callbacks_.emplace_back(std::forward<F>(callback)); }
  void call(Ts... value) {
    for (auto &callback : this->callbacks_)
      callback(value...);
  }
  bool empty() const { return this->callbacks_.empty(); }
 protected:
  std::vector<std::function<void(Ts...)>> callbacks_;
};
template<typename... Ts> class Action {
 public:
  virtual ~Action() = default;
  virtual void play(const Ts &...) = 0;
};
template<typename T> struct TestValue {
  T stored{};
  T value(...) const { return this->stored; }
};
namespace esp32 {
inline int background_task_core(int) { return 0; }
}
namespace lvgl {
class LvglComponent {
 public:
  bool paused{false}, pause_start_ok{true}, pause_wait_ok{true}, handoff_ok{true}, consume_handoff_ok{true},
      capture_ok{true}, release_ok{true};
  bool immediate_retire{true}, queue_carry{true};
  uint32_t generation{7}, pause_depth{0};
  uint8_t submit_result{LVGL_DIRECT_BLIT_SUBMITTED};
  size_t submissions{0}, captures{0}, releases{0}, handoffs{0}, native_flushes{0};
  size_t current_page{0};
  const uint8_t *borrowed{nullptr};
  size_t borrowed_bytes{0};
  DirectRegionGenerationResult *pending_result{nullptr};
  LvglDirectBlitReadyCallback callback{nullptr};
  void *argument{nullptr};
  std::function<void()> on_submit, on_native_flush, on_retire;
  lv_display_t *native_display{nullptr};
  std::vector<uint8_t> presented_pixels;
  bool is_paused() const { return this->paused; }
  int get_width() const { return this->native_display == nullptr ? 800 : lv_display_get_horizontal_resolution(this->native_display); }
  int get_height() const { return this->native_display == nullptr ? 800 : lv_display_get_vertical_resolution(this->native_display); }
  size_t get_current_page() const { return this->current_page; }
  void show_page(size_t page, int, int) { this->current_page = page; }
  uint32_t get_direct_region_base_generation() const { return this->generation; }
  uint32_t get_direct_region_pause_depth() const { return this->pause_depth; }
  void attach_native_display(lv_display_t *display) {
    this->native_display = display;
    this->presented_pixels.resize(static_cast<size_t>(this->get_width()) * this->get_height() * 3);
  }
  void native_flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
    // Target direct RGB888 LVGL flushes establish a new native base. The host
    // transport completes synchronously; it does not emulate DSI scanout.
    const bool last = lv_display_flush_is_last(display);
    if (last) {
      ++this->native_flushes;
      ++this->generation;
    }
    const int width = this->get_width(), height = this->get_height();
    const int rows = lv_area_get_height(area), columns = lv_area_get_width(area);
    const bool full_source = lv_display_get_render_mode(display) != LV_DISPLAY_RENDER_MODE_PARTIAL;
    for (int row = 0; row < rows; ++row) {
      const int y = area->y1 + row;
      if (y < 0 || y >= height || area->x1 < 0 || area->x2 >= width)
        continue;
      const size_t source = full_source ? (static_cast<size_t>(y) * width + area->x1) * 3 : static_cast<size_t>(row) * columns * 3;
      const size_t target = (static_cast<size_t>(y) * width + area->x1) * 3;
      std::memcpy(this->presented_pixels.data() + target, pixels + source, static_cast<size_t>(columns) * 3);
    }
    real_lvgl_test::record(last ? "native_flush_last" : "native_flush_partial", this->generation,
                           static_cast<uint32_t>(this->native_flushes), static_cast<uint32_t>(lv_anim_count_running()));
    if (last && this->on_native_flush)
      this->on_native_flush();
    lv_display_flush_ready(display);
  }
  bool direct_regions_pause_start() {
    ++this->pause_depth;
    real_lvgl_test::record("pause_start", this->generation, this->pause_depth);
    return this->pause_start_ok;
  }
  bool direct_regions_pause(bool pause, uint32_t) {
    if (pause)
      return this->direct_regions_pause_start();
    if (this->pause_depth)
      --this->pause_depth;
    ++this->generation;
    real_lvgl_test::record("pause_release", this->generation, this->pause_depth);
    return true;
  }
  bool direct_regions_pause_wait(uint32_t) { return this->pause_wait_ok; }
  bool direct_handoff_to_lvgl(uint32_t, bool consume = false) {
    ++this->handoffs;
    if (consume) {
      const bool exclusive = this->pause_depth == 1;
      if (this->pause_depth)
        --this->pause_depth;
      ++this->generation;
      real_lvgl_test::record("handoff_consume", this->generation, this->pause_depth);
      return exclusive && this->handoff_ok && this->consume_handoff_ok;
    }
    real_lvgl_test::record("handoff", this->generation, this->pause_depth);
    return this->handoff_ok;
  }
  bool direct_capture_rgb888(uint8_t *target, int stride, int x, int y, int width, int height) {
    ++this->captures;
    if (!this->capture_ok || x < 0 || y < 0 || width <= 0 || height <= 0 || x + width > this->get_width() ||
        y + height > this->get_height() || stride < width * 3)
      return false;
    for (int row = 0; row < height; ++row)
      std::memcpy(target + static_cast<size_t>(row) * stride,
                  this->presented_pixels.data() + (static_cast<size_t>(y + row) * this->get_width() + x) * 3,
                  static_cast<size_t>(width) * 3);
    real_lvgl_test::record("capture", this->generation, static_cast<uint32_t>(this->captures));
    return true;
  }
  bool direct_blit_rgb888_try_release(int, int, int, int) {
    ++this->releases;
    if (this->release_ok)
      ++this->generation;
    real_lvgl_test::record("region_release", this->generation, static_cast<uint32_t>(this->releases));
    return this->release_ok;
  }
  void retire() {
    assert(this->callback != nullptr && this->pending_result != nullptr);
    auto done = this->callback;
    auto *arg = this->argument;
    this->pending_result->retire(this->queue_carry, this->generation);
    this->callback = nullptr;
    this->argument = nullptr;
    real_lvgl_test::record("worker_retire", this->generation, this->queue_carry);
    if (this->on_retire)
      this->on_retire();
    done(arg);
  }
  uint8_t direct_blit_rgb888_generation_async(const uint8_t *source, int stride, int, int, int, int height,
                                              uint32_t expected, DirectRegionGenerationResult *result,
                                              LvglDirectBlitReadyCallback done, void *arg, bool, bool = true) {
    ++this->submissions;
    real_lvgl_test::record("worker_submit", this->generation, expected, this->pause_depth);
    if (expected != this->generation || this->pause_depth != 0)
      return LVGL_DIRECT_BLIT_REJECTED;
    if (this->submit_result != LVGL_DIRECT_BLIT_SUBMITTED)
      return this->submit_result;
    assert(this->callback == nullptr);
    this->borrowed = source;
    this->borrowed_bytes = static_cast<size_t>(stride) * height;
    this->pending_result = result;
    result->reset();
    this->callback = done;
    this->argument = arg;
    if (this->on_submit)
      this->on_submit();
    if (this->immediate_retire)
      this->retire();
    return LVGL_DIRECT_BLIT_SUBMITTED;
  }
};
class LvPageType {
 public:
  size_t index{0};
  lv_obj_t *obj{nullptr};
};
class LvglApplication {
 public:
  std::function<void()> prepare_close, closed, close_cancelled;
  template<typename F> void add_on_prepare_close_callback(F &&callback) { this->prepare_close = std::forward<F>(callback); }
  template<typename F> void add_on_closed_callback(F &&callback) { this->closed = std::forward<F>(callback); }
  template<typename F> void add_on_close_cancelled_callback(F &&callback) { this->close_cancelled = std::forward<F>(callback); }
  void call_on_prepare_close_callbacks() { if (this->prepare_close) this->prepare_close(); }
  void call_on_closed_callbacks() { if (this->closed) this->closed(); }
  void call_on_close_cancelled_callbacks() { if (this->close_cancelled) this->close_cancelled(); }
};
class LvglNavigation {
 public:
  bool transition{false};
  LvglApplication *active{nullptr};
  NavigationAdmissionGuard *guard{nullptr};
  bool add_admission_guard(NavigationAdmissionGuard *value) { this->guard = value; return true; }
  bool is_application_open(const LvglApplication *app) const { return this->active == app; }
  bool is_transition_active() const { return this->transition; }
};
}  // namespace lvgl
namespace api {
class APIServer {
 public:
  uint32_t call_id{0};
  bool success{false};
  std::string payload, error;
  void send_action_response(uint32_t id, bool accepted, StringRef message) {
    this->call_id = id;
    this->success = accepted;
    this->error = message.c_str();
    this->payload.clear();
  }
  void send_action_response(uint32_t id, bool accepted, StringRef message, const uint8_t *data, size_t size) {
    this->send_action_response(id, accepted, message);
    this->payload.assign(reinterpret_cast<const char *>(data), size);
  }
};
}  // namespace api
}  // namespace esphome

#define TEMPLATABLE_VALUE(type, name) \
 protected: esphome::TestValue<type> name##_; \
 public: void set_##name(type value) { this->name##_.stored = value; }
#define LOG_STR(x) x
template<typename... Args> inline void real_lvgl_test_log(const Args &...) {}
#define ESP_LOGCONFIG(...) real_lvgl_test_log(__VA_ARGS__)
#define ESP_LOGI(...) real_lvgl_test_log(__VA_ARGS__)
#define ESP_LOGE(...) real_lvgl_test_log(__VA_ARGS__)
#define ESP_LOGW(...) real_lvgl_test_log(__VA_ARGS__)

namespace real_lvgl_test {
class HostDisplay {
 public:
  HostDisplay(esphome::lvgl::LvglComponent &facade, int width = 800, int height = 800,
              lv_display_render_mode_t mode = LV_DISPLAY_RENDER_MODE_DIRECT)
      : facade_(facade), pixels_(static_cast<size_t>(width) * height * 3),
        second_pixels_(mode == LV_DISPLAY_RENDER_MODE_DIRECT ? pixels_.size() : 0) {
    this->display_ = lv_display_create(width, height);
    assert(this->display_ != nullptr);
    lv_display_set_color_format(this->display_, LV_COLOR_FORMAT_RGB888);
    lv_display_set_buffers(this->display_, this->pixels_.data(),
                           this->second_pixels_.empty() ? nullptr : this->second_pixels_.data(), this->pixels_.size(), mode);
    lv_display_set_user_data(this->display_, &this->facade_);
    lv_display_set_flush_cb(this->display_, [](lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
      static_cast<esphome::lvgl::LvglComponent *>(lv_display_get_user_data(display))->native_flush(display, area, pixels);
    });
    this->facade_.attach_native_display(this->display_);
  }
  ~HostDisplay() { lv_display_delete(this->display_); }
  HostDisplay(const HostDisplay &) = delete;
  HostDisplay &operator=(const HostDisplay &) = delete;
  lv_display_t *get() const { return this->display_; }
 private:
  esphome::lvgl::LvglComponent &facade_;
  lv_display_t *display_{nullptr};
  std::vector<uint8_t> pixels_, second_pixels_;
};

class Pointer {
 public:
  explicit Pointer(lv_display_t *display) {
    this->indev_ = lv_indev_create();
    assert(this->indev_ != nullptr);
    lv_indev_set_type(this->indev_, LV_INDEV_TYPE_POINTER);
    lv_indev_set_mode(this->indev_, LV_INDEV_MODE_EVENT);
    lv_indev_set_display(this->indev_, display);
    lv_indev_set_user_data(this->indev_, this);
    lv_indev_set_read_cb(this->indev_, [](lv_indev_t *indev, lv_indev_data_t *data) {
      auto *self = static_cast<Pointer *>(lv_indev_get_user_data(indev));
      data->point = self->point_;
      data->state = self->state_;
    });
  }
  ~Pointer() { lv_indev_delete(this->indev_); }
  Pointer(const Pointer &) = delete;
  Pointer &operator=(const Pointer &) = delete;
  lv_indev_t *get() const { return this->indev_; }
  void press(int x, int y) {
    this->point_ = {x, y};
    this->state_ = LV_INDEV_STATE_PRESSED;
    record("pointer_press", 0, static_cast<uint32_t>(x), static_cast<uint32_t>(y));
    lv_indev_read(this->indev_);
  }
  void move(int x, int y) {
    this->point_ = {x, y};
    record("pointer_move", 0, static_cast<uint32_t>(x), static_cast<uint32_t>(y));
    lv_indev_read(this->indev_);
  }
  void release() {
    this->state_ = LV_INDEV_STATE_RELEASED;
    record("pointer_release", 0, static_cast<uint32_t>(this->point_.x), static_cast<uint32_t>(this->point_.y));
    lv_indev_read(this->indev_);
  }
 private:
  lv_indev_t *indev_{nullptr};
  lv_point_t point_{0, 0};
  lv_indev_state_t state_{LV_INDEV_STATE_RELEASED};
};

// Product geometry and styling copied into a real native object tree. Host font
// glyphs are used; this is not Roboto/P4 pixel or scanout validation.
struct NativeWidgets {
  lv_obj_t *root, *hit, *title, *value, *unknown, *on, *off, *state, *status;
  lv_obj_t *region, *track, *bubble, *scalar_value, *minimum, *maximum, *on_label, *off_label;
  explicit NativeWidgets(lv_obj_t *parent) {
    this->root = lv_obj_create(parent);
    lv_obj_remove_style_all(this->root);
    lv_obj_set_pos(this->root, 0, 0);
    lv_obj_set_size(this->root, 800, 800);
    lv_obj_set_scrollable(this->root, false);
    lv_obj_set_style_bg_color(this->root, lv_color_hex(0x102030), 0);
    lv_obj_set_style_bg_opa(this->root, LV_OPA_COVER, 0);
    this->title = label(this->root, "Light", 120, 300, 560, 35);
    this->state = label(this->root, "On", 200, 340, 400, 44);
    this->status = label(this->root, "ready", 120, 380, 560, 30);
    this->value = label(this->root, "0%", 150, 204, 500, 132);
    lv_obj_set_hidden(this->value, true);
    this->unknown = label(this->root, "--", 150, 238, 500, 72);
    this->region = plain(this->root, 118, 410, 564, 112, 0x000000);
    this->hit = this->region;
    lv_obj_set_clickable(this->region, true);
    this->track = plain(this->region, 0, 54, 564, 36, 0x071018);
    lv_obj_set_style_radius(this->track, 18, 0);
    this->bubble = plain(this->region, 0, 0, 78, 48, 0xD3E3FD);
    lv_obj_set_style_radius(this->bubble, 24, 0);
    this->scalar_value = lv_label_create(this->bubble);
    lv_label_set_text(this->scalar_value, "--");
    lv_obj_set_style_text_font(this->scalar_value, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(this->scalar_value, lv_color_hex(0x071018), 0);
    lv_obj_set_clickable(this->scalar_value, false);
    lv_obj_center(this->scalar_value);
    this->minimum = label(this->region, "0%", 0, 92, 44, 20);
    this->maximum = label(this->region, "100%", 514, 92, 50, 20);
    lv_obj_set_style_text_color(this->minimum, lv_color_hex(0xC4C7C5), 0);
    lv_obj_set_style_text_color(this->maximum, lv_color_hex(0xC4C7C5), 0);
    lv_obj_set_style_text_align(this->maximum, LV_TEXT_ALIGN_RIGHT, 0);
    this->on = power_button(this->root, false, this->on_label);
    this->off = power_button(this->root, true, this->off_label);
    lv_obj_update_layout(this->root);
  }
  static lv_obj_t *plain(lv_obj_t *parent, int x, int y, int width, int height, uint32_t color) {
    auto *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    lv_obj_set_scrollable(obj, false);
    lv_obj_set_clickable(obj, false);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    return obj;
  }
  static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, int width, int height) {
    auto *obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    lv_obj_set_style_text_font(obj, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_clickable(obj, false);
    return obj;
  }
  static lv_obj_t *power_button(lv_obj_t *parent, bool is_off, lv_obj_t *&label_out) {
    auto *button = lv_button_create(parent);
    lv_obj_set_pos(button, is_off ? 418 : 174, 560);
    lv_obj_set_size(button, 208, 80);
    lv_obj_set_style_bg_color(button, lv_color_hex(is_off ? 0x071018 : 0xD3E3FD), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_radius(button, 40, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_bg_opa(button, 204, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, 89, LV_STATE_DISABLED);
    lv_obj_set_style_text_opa(button, 114, LV_STATE_DISABLED);
    lv_obj_set_scrollable(button, false);
    label_out = lv_label_create(button);
    lv_label_set_text(label_out, is_off ? "Off" : "On");
    lv_obj_set_style_text_font(label_out, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(label_out, lv_color_hex(is_off ? 0xF5F7FA : 0x071018), 0);
    lv_obj_set_clickable(label_out, false);
    lv_obj_center(label_out);
    // Exact early product policy retained for the candidate19/20 baseline.
    lv_obj_remove_theme(button, static_cast<lv_style_selector_t>(LV_PART_MAIN) | LV_STATE_DEFAULT);
    lv_obj_remove_theme(button, static_cast<lv_style_selector_t>(LV_PART_MAIN) | LV_STATE_PRESSED);
    return button;
  }
};
}  // namespace real_lvgl_test
