#pragma once
#ifndef configTICK_RATE_HZ
#define configTICK_RATE_HZ 1000
#endif
#include "component_test_support.h"
#include "direct_region_generation.h"
#include "navigation_admission.h"
#include <array>
#include <cstdlib>
#include <memory>

inline constexpr int LV_PART_MAIN = 0, LV_OPA_COVER = 255, LV_COLOR_FORMAT_A8 = 1, LV_COLOR_FORMAT_RGB888 = 2,
                     LV_STRIDE_AUTO = 0, LV_RESULT_OK = 0, LV_IMAGE_HEADER_MAGIC = 0x19;
inline size_t test_allocations = 0, test_refreshes = 0, test_cache_drops = 0;
inline std::function<void()> test_notify_hook, test_delay_hook, test_refresh_hook;
inline uint32_t test_delay_total = 0;
inline bool test_missing_glyph = false;
inline uint32_t test_missing_glyph_point = 0, test_placeholder_glyph_point = 0;
inline constexpr int LV_FONT_GLYPH_FORMAT_A1 = 1, LV_FONT_GLYPH_FORMAT_A2 = 2, LV_FONT_GLYPH_FORMAT_A3 = 3,
                     LV_FONT_GLYPH_FORMAT_A4 = 4, LV_FONT_GLYPH_FORMAT_A8 = 8;
struct lv_color_t {
  uint8_t blue{}, green{}, red{};
};
inline lv_color_t lv_color_hex(uint32_t color) {
  return {static_cast<uint8_t>(color), static_cast<uint8_t>(color >> 8), static_cast<uint8_t>(color >> 16)};
}
struct lv_font_t {
  int line_height{4}, base_line{0};
};
inline lv_font_t test_font;
struct lv_image_header_t {
  uint32_t magic{}, cf{}, w{}, h{}, stride{};
};
struct lv_image_dsc_t {
  lv_image_header_t header;
  size_t data_size{};
  const uint8_t *data{};
};
struct lv_draw_buf_t {
  lv_image_header_t header;
  uint8_t *data{};
  size_t data_size{};
};
struct lv_font_glyph_dsc_t {
  uint16_t box_w{2}, box_h{2}, adv_w{3};
  int format{LV_FONT_GLYPH_FORMAT_A8};
  int16_t ofs_x{}, ofs_y{};
  bool is_placeholder{false};
};
inline int lv_area_get_width(const lv_area_t *area) { return area->x2 - area->x1 + 1; }
inline int lv_area_get_height(const lv_area_t *area) { return area->y2 - area->y1 + 1; }
inline int lv_obj_get_width(lv_obj_t *object) { return lv_area_get_width(&object->bounds); }
inline int lv_obj_get_height(lv_obj_t *object) { return lv_area_get_height(&object->bounds); }
inline void lv_obj_update_layout(lv_obj_t *) {}
inline void lv_obj_set_pos(lv_obj_t *object, int x, int y) {
  const int width = lv_obj_get_width(object), height = lv_obj_get_height(object);
  const int px = object->parent == nullptr ? 0 : object->parent->bounds.x1;
  const int py = object->parent == nullptr ? 0 : object->parent->bounds.y1;
  object->bounds = {px + x, py + y, px + x + width - 1, py + y + height - 1};
}
inline void lv_obj_set_size(lv_obj_t *object, int width, int height) {
  object->bounds.x2 = object->bounds.x1 + width - 1;
  object->bounds.y2 = object->bounds.y1 + height - 1;
}
inline lv_obj_t *lv_image_create(lv_obj_t *parent) {
  auto *p = new lv_obj_t;
  p->parent = parent;
  return p;
}
inline void lv_obj_delete(lv_obj_t *object) { delete object; }
inline void lv_image_set_src(lv_obj_t *, const lv_image_dsc_t *) {}
inline void lv_image_cache_drop(const void *) { test_cache_drops++; }
inline void lv_obj_move_foreground(lv_obj_t *) {}
inline void lv_obj_invalidate(lv_obj_t *) {}
inline void *lv_obj_get_display(lv_obj_t *) { return reinterpret_cast<void *>(1); }
inline int lv_display_get_color_format(void *) { return LV_COLOR_FORMAT_RGB888; }
inline void lv_refr_now(void *) {
  test_refreshes++;
  if (test_refresh_hook)
    test_refresh_hook();
}
inline lv_color_t lv_obj_get_style_bg_color(lv_obj_t *object, int) { return lv_color_hex(object->background); }
inline int lv_obj_get_style_bg_opa(lv_obj_t *, int) { return LV_OPA_COVER; }
inline lv_color_t lv_obj_get_style_text_color(lv_obj_t *object, int) { return lv_color_hex(object->text_color); }
inline const lv_font_t *lv_obj_get_style_text_font(lv_obj_t *object, int) {
  return object->font == nullptr ? &test_font : static_cast<const lv_font_t *>(object->font);
}
inline int lv_obj_get_style_text_letter_space(lv_obj_t *, int) { return 0; }
inline uint32_t lv_draw_buf_width_to_stride(int width, int format) {
  return (width * (format == LV_COLOR_FORMAT_A8 ? 1 : 3) + 3) & ~3;
}
inline int lv_draw_buf_init(lv_draw_buf_t *buf, int width, int height, int cf, int stride, void *data,
                            size_t capacity) {
  if (stride == 0)
    stride = lv_draw_buf_width_to_stride(width, cf);
  if (static_cast<size_t>(stride) * height > capacity)
    return 1;
  buf->header = {0, static_cast<uint32_t>(cf), static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                 static_cast<uint32_t>(stride)};
  buf->data = static_cast<uint8_t *>(data);
  buf->data_size = capacity;
  return LV_RESULT_OK;
}
inline bool lv_font_get_glyph_dsc(const lv_font_t *, lv_font_glyph_dsc_t *d, uint32_t point, uint32_t) {
  *d = {};
  d->is_placeholder = test_placeholder_glyph_point != 0 && point == test_placeholder_glyph_point;
  return !test_missing_glyph && (test_missing_glyph_point == 0 || point != test_missing_glyph_point);
}
inline const void *lv_font_get_glyph_bitmap(lv_font_glyph_dsc_t *glyph, lv_draw_buf_t *buf) {
  const size_t stride = lv_draw_buf_width_to_stride(glyph->box_w, LV_COLOR_FORMAT_A8);
  for (size_t y = 0; y < glyph->box_h; y++)
    std::memset(buf->data + y * stride, 255, glyph->box_w);
  return buf;
}
inline void lv_font_glyph_release_draw_data(lv_font_glyph_dsc_t *) {}

using StackType_t = uint8_t;
struct StaticTask_t {};
using TaskHandle_t = void *;
inline constexpr int pdTRUE = 1, portMAX_DELAY = -1, MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2;
inline int pdMS_TO_TICKS(int value) { return value; }
inline void *heap_caps_malloc(size_t bytes, int) {
  test_allocations++;
  return std::malloc(bytes);
}
inline void *heap_caps_aligned_alloc(size_t, size_t bytes, int caps) { return heap_caps_malloc(bytes, caps); }
inline void heap_caps_free(void *pointer) { std::free(pointer); }
inline TaskHandle_t xTaskCreateStaticPinnedToCore(void (*)(void *), const char *, uint32_t, void *, int, StackType_t *,
                                                  StaticTask_t *, int) {
  return reinterpret_cast<void *>(1);
}
inline void xTaskNotifyGive(TaskHandle_t) {}
inline uint32_t ulTaskNotifyTake(int, int) {
  if (test_notify_hook)
    test_notify_hook();
  return 1;
}
inline void vTaskDelay(int duration) {
  test_now += duration;
  test_delay_total += duration;
  if (test_delay_hook)
    test_delay_hook();
}
inline void vTaskDelete(TaskHandle_t) {}
using LvglDirectBlitReadyCallback = void (*)(void *);
inline constexpr uint8_t LVGL_DIRECT_BLIT_REJECTED = 0, LVGL_DIRECT_BLIT_BUSY = 1, LVGL_DIRECT_BLIT_SUBMITTED = 2;

namespace esphome {
inline void delay(uint32_t duration) {
  test_now += duration;
  if (test_delay_hook)
    test_delay_hook();
}
namespace esp32 {
inline int background_task_core(int) { return 0; }
}  // namespace esp32
namespace lvgl {
class LvglComponent {
 public:
  bool paused{false}, pause_start_ok{true}, pause_wait_ok{true}, handoff_ok{true}, consume_handoff_ok{true},
      capture_ok{true}, release_ok{true};
  bool immediate_retire{true}, queue_carry{true};
  uint32_t generation{7}, pause_depth{0};
  uint8_t submit_result{LVGL_DIRECT_BLIT_SUBMITTED};
  size_t submissions{0}, captures{0}, releases{0}, handoffs{0};
  size_t current_page{0};
  const uint8_t *borrowed{nullptr};
  size_t borrowed_bytes{};
  DirectRegionGenerationResult *pending_result{nullptr};
  LvglDirectBlitReadyCallback callback{nullptr};
  void *argument{nullptr};
  std::function<void()> on_submit;
  bool is_paused() const { return this->paused; }
  int get_width() const { return 800; }
  int get_height() const { return 800; }
  size_t get_current_page() const { return current_page; }
  void show_page(size_t page, int, int) { current_page = page; }
  uint32_t get_direct_region_base_generation() const { return generation; }
  uint32_t get_direct_region_pause_depth() const { return pause_depth; }
  bool direct_regions_pause_start() {
    pause_depth++;
    return pause_start_ok;
  }
  bool direct_regions_pause(bool pause, uint32_t) {
    if (pause)
      return direct_regions_pause_start();
    if (pause_depth)
      pause_depth--;
    generation++;
    return true;
  }
  bool direct_regions_pause_wait(uint32_t) { return pause_wait_ok; }
  bool direct_handoff_to_lvgl(uint32_t, bool consume = false) {
    handoffs++;
    if (consume) {
      const bool exclusive = pause_depth == 1;
      if (pause_depth)
        pause_depth--;
      generation++;
      return exclusive && handoff_ok && consume_handoff_ok;
    }
    return handoff_ok;
  }
  bool direct_capture_rgb888(uint8_t *target, int stride, int, int, int, int height) {
    captures++;
    if (!capture_ok)
      return false;
    std::memset(target, 0xA5, static_cast<size_t>(stride) * height);
    return true;
  }
  bool direct_blit_rgb888_try_release(int, int, int, int) {
    releases++;
    if (release_ok)
      generation++;
    return release_ok;
  }
  void retire() {
    assert(callback);
    auto done = callback;
    auto arg = argument;
    pending_result->retire(queue_carry, generation);
    callback = nullptr;
    done(arg);
  }
  uint8_t direct_blit_rgb888_generation_async(const uint8_t *source, int stride, int, int, int, int height,
                                              uint32_t expected, DirectRegionGenerationResult *result,
                                              LvglDirectBlitReadyCallback done, void *arg, bool, bool = true) {
    submissions++;
    if (expected != generation || pause_depth != 0)
      return LVGL_DIRECT_BLIT_REJECTED;
    if (submit_result != LVGL_DIRECT_BLIT_SUBMITTED)
      return submit_result;
    assert(callback == nullptr);
    borrowed = source;
    borrowed_bytes = static_cast<size_t>(stride) * height;
    pending_result = result;
    result->reset();
    callback = done;
    argument = arg;
    if (on_submit)
      on_submit();
    if (immediate_retire)
      retire();
    return LVGL_DIRECT_BLIT_SUBMITTED;
  }
};
class LvPageType {
 public:
  size_t index{};
  lv_obj_t *obj{};
};
#ifndef TEST_REAL_NAV
class LvglApplication {
 public:
  std::function<void()> prepare_close, closed, close_cancelled;
  template<typename F> void add_on_prepare_close_callback(F &&callback) { prepare_close = std::forward<F>(callback); }
  template<typename F> void add_on_closed_callback(F &&callback) { closed = std::forward<F>(callback); }
  template<typename F> void add_on_close_cancelled_callback(F &&callback) {
    close_cancelled = std::forward<F>(callback);
  }
  void call_on_prepare_close_callbacks() {
    if (prepare_close)
      prepare_close();
  }
  void call_on_closed_callbacks() {
    if (closed)
      closed();
  }
  void call_on_close_cancelled_callbacks() {
    if (close_cancelled)
      close_cancelled();
  }
};
class LvglNavigation {
 public:
  bool transition{false};
  LvglApplication *active{nullptr};
  NavigationAdmissionGuard *guard{nullptr};
  bool add_admission_guard(NavigationAdmissionGuard *value) {
    guard = value;
    return true;
  }
  bool is_application_open(const LvglApplication *app) const { return active == app; }
  bool is_transition_active() const { return transition; }
};
#endif
}  // namespace lvgl
}  // namespace esphome
template<typename... Args> inline void scalar_test_log(const Args &...) {}
#undef ESP_LOGCONFIG
#define ESP_LOGCONFIG(...) scalar_test_log(__VA_ARGS__)
#define ESP_LOGI(...) scalar_test_log(__VA_ARGS__)
#define ESP_LOGE(...) scalar_test_log(__VA_ARGS__)
#define ESP_LOGW(...) scalar_test_log(__VA_ARGS__)
