#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

#define USE_LVGL_PPA
#define USE_MIPI_DSI
#define LV_COLOR_DEPTH 32

using TickType_t = uint32_t;
using StackType_t = uint32_t;
using TaskHandle_t = void *;
using lv_color_t = uint32_t;
using lv_anim_exec_xcb_t = void (*)(void *, int32_t);
struct StaticTask_t {};
enum lv_color_format_t {
  LV_COLOR_FORMAT_ARGB8888,
  LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED,
  LV_COLOR_FORMAT_XRGB8888,
  LV_COLOR_FORMAT_RGB888
};
struct lv_area_t {
  int32_t x1{}, y1{}, x2{}, y2{};
};
struct lv_draw_buf_t {};
struct lv_display_t {};
struct lv_obj_t {
  bool hidden{};
  uint8_t *canvas{};
  lv_color_format_t format{};
  lv_area_t coordinates{};
  lv_draw_buf_t draw_buf;
};
constexpr int LV_OBJ_FLAG_HIDDEN = 1;
constexpr int LV_IMAGE_FLAGS_PREMULTIPLIED = 2;
constexpr uint32_t MALLOC_CAP_INTERNAL = 1, MALLOC_CAP_DMA = 2, MALLOC_CAP_SPIRAM = 4, MALLOC_CAP_8BIT = 8;
#include "lottie_types.inc"

struct Allocation {
  void *base;
  size_t size;
};
std::unordered_map<void *, Allocation> allocations;
unsigned allocation_calls = 0, fail_allocation = 0, free_calls = 0;
size_t allocation_budget = SIZE_MAX;
std::vector<size_t> requested_sizes;
unsigned render_calls = 0, copy_calls = 0, mailbox_calls = 0, wait_calls = 0, release_calls = 0, notify_calls = 0;
bool wait_success = true, release_success = true, stop_success = true;
uint32_t last_wait_timeout = 0;
uint8_t *last_render_target = nullptr, *last_copy_source = nullptr, *mailbox_source = nullptr;
LottiePublicationKind mailbox_kind{};
uint8_t *queued_source = nullptr;
void (*queued_callback)(void *) = nullptr;
void *queued_callback_arg = nullptr;
uint8_t queue_result = LOTTIE_DIRECT_BLIT_SUBMITTED;
lv_obj_t screen;
lv_display_t display;

template<typename... Args> void ignore_log(Args...) {}
#define ESP_LOGE(...) ignore_log(__VA_ARGS__)
#define ESP_LOGW(...) ignore_log(__VA_ARGS__)
#define ESP_LOGI(...) ignore_log(__VA_ARGS__)
#define LV_LOG_ERROR(...) ignore_log(__VA_ARGS__)
#define LV_LOG_INFO(...) ignore_log(__VA_ARGS__)
#define LV_LOG_WARN(...) ignore_log(__VA_ARGS__)

size_t heap_caps_get_largest_free_block(uint32_t caps) { return caps & MALLOC_CAP_INTERNAL ? 0 : allocation_budget; }
size_t heap_caps_get_free_size(uint32_t caps) { return heap_caps_get_largest_free_block(caps); }
void *heap_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps) {
  assert(alignment == LOTTIE_CACHE_ALIGN && alignment == 128);
  assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  allocation_calls++;
  requested_sizes.push_back(size);
  if (allocation_calls == fail_allocation || size > allocation_budget)
    return nullptr;
  void *base = std::malloc(size + alignment);
  assert(base != nullptr);
  auto address = (reinterpret_cast<uintptr_t>(base) + alignment - 1) & ~(alignment - 1);
  auto *aligned = reinterpret_cast<void *>(address);
  assert(allocations.emplace(aligned, Allocation{base, size}).second);
  allocation_budget -= size;
  return aligned;
}
void heap_caps_free(void *pointer) {
  const auto found = allocations.find(pointer);
  assert(found != allocations.end());  // Catch alias/double free.
  allocation_budget += found->second.size;
  std::free(found->second.base);
  allocations.erase(found);
  free_calls++;
}
uint32_t lv_draw_buf_width_to_stride(uint32_t width, lv_color_format_t format) {
  return width * (format == LV_COLOR_FORMAT_RGB888 ? 3 : 4);
}
size_t lottie_align_up(size_t bytes, size_t alignment) { return (bytes + alignment - 1) & ~(alignment - 1); }
#include "lottie_helpers.inc"

void lv_lock() {}
void lv_unlock() {}
bool lv_obj_is_valid(lv_obj_t *object) { return object != nullptr; }
bool lv_obj_is_visible(lv_obj_t *object) { return !object->hidden; }
lv_obj_t *lv_obj_get_screen(lv_obj_t *) { return &screen; }
lv_obj_t *lv_screen_active() { return &screen; }
bool lv_obj_has_flag(lv_obj_t *object, int) { return object->hidden; }
lv_obj_t *lv_obj_get_parent(lv_obj_t *) { return nullptr; }
void lv_obj_get_coords(lv_obj_t *object, lv_area_t *coordinates) { *coordinates = object->coordinates; }
lv_display_t *lv_obj_get_display(lv_obj_t *) { return &display; }
int32_t lv_area_get_width(const lv_area_t *area) { return area->x2 - area->x1 + 1; }
int32_t lv_area_get_height(const lv_area_t *area) { return area->y2 - area->y1 + 1; }
int lv_display_get_horizontal_resolution(lv_display_t *) { return 800; }
int lv_display_get_vertical_resolution(lv_display_t *) { return 800; }
void lv_obj_clear_flag(lv_obj_t *object, int) { object->hidden = false; }
void lv_obj_invalidate(lv_obj_t *) {}
void lv_canvas_set_buffer(lv_obj_t *object, uint8_t *data, uint32_t, uint32_t, lv_color_format_t format) {
  object->canvas = data;
  object->format = format;
}
lv_draw_buf_t *lv_canvas_get_draw_buf(lv_obj_t *object) { return &object->draw_buf; }
void lv_draw_buf_clear_flag(lv_draw_buf_t *, int) {}
void lvgl_esphome_release_dma_image_buffer(const void *) {}
void lottie_sync_buffer(uint8_t *, size_t) {}
void lottie_wait_display_fifo() {}
bool lottie_direct_frame_in_flight(const LottieContext *context) { return context->direct_frame_in_flight; }
void lottie_set_direct_frame_in_flight(LottieContext *context, bool value) { context->direct_frame_in_flight = value; }
bool lottie_wait_direct_frame_buffer(LottieContext *context, uint32_t timeout) {
  wait_calls++;
  last_wait_timeout = timeout;
  return wait_success && !context->direct_frame_in_flight;
}
bool lottie_release_direct_region(LottieContext *context) {
  release_calls++;
  if (!release_success || context->direct_frame_in_flight)
    return false;
  context->direct_region_active = false;
  return true;
}
bool lottie_stop_task(LottieContext *) { return stop_success; }
void lottie_log_direct_publish_failure(LottieContext *, const char *, uint8_t = 0) {}
void lottie_direct_frame_ready(void *argument) {
  auto *context = static_cast<LottieContext *>(argument);
  assert(context->direct_frame_in_flight);
  context->direct_frame_in_flight = false;
  queued_source = nullptr;
}
uint8_t lvgl_esphome_direct_blit_xrgb8888_region_async(const uint8_t *source, int stride, int width, int height, int,
                                                       int, int, int, int, int, void (*callback)(void *),
                                                       void *argument) {
  assert(stride == width * 4 && height > 0);
  assert(reinterpret_cast<uintptr_t>(source) % LOTTIE_CACHE_ALIGN == 0);
  if (queue_result == LOTTIE_DIRECT_BLIT_SUBMITTED) {
    assert(queued_source == nullptr);
    queued_source = const_cast<uint8_t *>(source);
    queued_callback = callback;
    queued_callback_arg = argument;
  }
  return queue_result;
}
int32_t lottie_first_renderable_frame(const LottieContext *context) { return context->start_frame + 1; }
int32_t lottie_last_renderable_frame(const LottieContext *context) { return context->end_frame; }
bool lottie_render_frame(LottieContext *context, int32_t frame, uint8_t *target) {
  assert(target != nullptr && target != context->pixel_buffer);
  assert(!(target == queued_source && context->direct_frame_in_flight));
  assert(allocations.at(target).size >= lottie_buffer_bytes(context));
  std::memset(target, frame & 0xFF, lottie_buffer_bytes(context));
  last_render_target = target;
  render_calls++;
  return true;
}
bool lottie_copy_xrgb8888_to_rgb888(LottieContext *context, const uint8_t *source, uint8_t *target) {
  assert(source != target && target == context->pixel_buffer);
  assert(allocations.at(const_cast<uint8_t *>(source)).size >= context->width * context->height * 4);
  assert(allocations.at(target).size >= context->width * context->height * 3);
  std::memcpy(target, source, context->width * context->height * 3);
  last_copy_source = const_cast<uint8_t *>(source);
  copy_calls++;
  return true;
}
bool lottie_request_native_publication(LottieContext *context, uint8_t *source, bool scaled,
                                       LottiePublicationKind kind = LottiePublicationKind::SWAP) {
  // This fake is the external main-loop mailbox boundary, not the function
  // under test. Record immutable source/type before emulating its completion.
  assert(!scaled && !context->direct_frame_in_flight);
  assert(kind == LottiePublicationKind::RETAIN || kind == LottiePublicationKind::RETAIN_HANDOFF);
  mailbox_source = source;
  mailbox_kind = kind;
  mailbox_calls++;
  if (kind == LottiePublicationKind::RETAIN_HANDOFF && !lottie_release_direct_region(context))
    return false;
  assert(lottie_copy_xrgb8888_to_rgb888(context, source, context->pixel_buffer));
  lv_canvas_set_buffer(context->obj, context->pixel_buffer, context->width, context->height, LV_COLOR_FORMAT_RGB888);
  return true;
}
void lottie_fill_display_buffer(LottieContext *, uint8_t *target, size_t bytes) { std::memset(target, 0, bytes); }
void lottie_publish_opaque_frame(LottieContext *) { assert(false); }
bool lottie_publish_work_buffer(LottieContext *) {
  assert(false);
  return false;
}
void lottie_prepare_frame_cache(LottieContext *) {}
void vTaskSuspend(void *) { assert(false); }
constexpr int pdTRUE = 1;
uint32_t pdMS_TO_TICKS(uint32_t milliseconds) { return milliseconds; }
void ulTaskNotifyTake(int, uint32_t) { notify_calls++; }

#include "lottie_cleanup.inc"
bool allocate_surfaces(LottieContext *ctx) {
#include "lottie_allocation.inc"
  return true;
}
void prepare_initial(LottieContext *ctx) {
#include "lottie_initial_prepare.inc"
}
#include "lottie_publication.inc"
#include "lottie_retention.inc"
bool process_hidden_prefix(LottieContext *ctx) {
  for (unsigned attempt = 0; attempt < 1; attempt++) {
#include "lottie_hidden_drain.inc"
    return true;  // Native worker may now reach its existing sleep/render gates.
  }
  return false;  // Actual production continue postponed all later work.
}
bool retained_recolor(LottieContext *ctx, bool color_applied, bool completed) {
  for (unsigned attempt = 0; attempt < 1; attempt++) {
#include "lottie_retained_recolor.inc"
    return true;
  }
  return false;
}

int main(int argc, char **argv) {
  assert(argc == 2);
  const unsigned test = std::strtoul(argv[1], nullptr, 10);
  assert(test < 16);
  lv_obj_t object;
  LottieContext context{};
  context.obj = &object;
  context.width = context.height = context.render_width = context.render_height = 384;
  context.flatten_to_opaque = true;
  context.auto_start = true;
  context.start_frame = 0;
  context.end_frame = 60;
  context.retained_frame = 17;
  if (test == 1 || test == 9)
    context.width = context.height = context.render_width = context.render_height = 512;
  if (test == 2 || test == 3) {
    context.flatten_to_opaque = false;
    context.width = context.height = 256;
    context.render_width = context.render_height = test == 3 ? 128 : 256;
  }
  if (test == 4 || test == 5)
    context.render_width = context.render_height = test == 4 ? 192 : 768;
  object.coordinates = {0, 0, static_cast<int32_t>(context.width - 1), static_cast<int32_t>(context.height - 1)};
  if (test == 0)
    allocation_budget = 3 * 589824;  // A fourth legacy allocation would fail.
  if (test >= 6 && test <= 8)
    fail_allocation = test - 5;
  if (test == 9)
    fail_allocation = 3;
  const bool allocated = allocate_surfaces(&context);
  if (test == 6 || test == 7 || test == 9) {
    assert(!allocated && allocations.empty());
    assert(free_calls == (test == 6 ? 0 : test == 7 ? 1 : 2));
  } else {
    assert(allocated);
    if (test == 0 || test >= 10) {
      assert(allocation_calls == 3 && allocations.size() == 3);
      assert(context.pixel_buffer && context.work_buffer && context.work_buffer_alt && !context.display_back_buffer);
      assert(context.pixel_buffer != context.work_buffer && context.pixel_buffer != context.work_buffer_alt &&
             context.work_buffer != context.work_buffer_alt);
      assert(!lottie_renders_to_display_back_buffer(&context));
      assert(lottie_initial_render_target(&context) == context.work_buffer);
    } else if (test == 1 || test == 3) {
      assert(allocation_calls == 3 && context.display_back_buffer && !context.work_buffer_alt);
      if (test == 1)
        assert(lottie_initial_render_target(&context) == context.display_back_buffer);
    } else if (test == 2) {
      assert(allocation_calls == 2 && !context.display_back_buffer && !context.work_buffer_alt);
    } else if (test == 4 || test == 5) {
      assert(allocation_calls == 4 && context.display_back_buffer && context.work_buffer_alt);
      assert(allocations.at(context.display_back_buffer).size == 589824);
      assert(lottie_initial_render_target(&context) == context.display_back_buffer);
    } else if (test == 8) {
      assert(allocation_calls == 3 && allocations.size() == 2 && !context.work_buffer_alt);
      assert(!lottie_publish_direct_opaque_frame(&context));  // Preserve existing alt semantics.
    }
    if (test == 0) {
      prepare_initial(&context);
      assert(context.prepared_frame_ready && object.canvas == context.pixel_buffer);
      assert(object.format == LV_COLOR_FORMAT_RGB888 && last_render_target == context.work_buffer);
      assert(last_copy_source == context.work_buffer && copy_calls == 1);
      const uint8_t canvas_before = context.pixel_buffer[0];
      uint8_t *first = context.work_buffer, *second = context.work_buffer_alt;
      assert(lottie_render_frame(&context, 2, first));
      assert(lottie_publish_direct_opaque_frame(&context));
      assert(queued_source == first && context.work_buffer == second && context.work_buffer_alt == first);
      assert(context.pixel_buffer[0] == canvas_before);
      assert(!lottie_publish_direct_opaque_frame(&context));
      assert(context.work_buffer == second && queued_source == first);
      queued_callback(queued_callback_arg);
      assert(lottie_render_frame(&context, 3, second));
      assert(lottie_publish_direct_opaque_frame(&context));
      assert(queued_source == second && context.work_buffer == first && context.work_buffer_alt == second);
      context.retained_frame = 3;
      assert(!lottie_commit_direct_frame_to_canvas(&context) && mailbox_calls == 0);
      queued_callback(queued_callback_arg);
      assert(lottie_commit_direct_frame_to_canvas(&context));
      assert(last_wait_timeout == 150 && mailbox_source == second &&
             mailbox_kind == LottiePublicationKind::RETAIN_HANDOFF);
      assert(object.canvas == context.pixel_buffer && context.pixel_buffer[0] == 3);
      assert(!context.direct_region_active);
      assert(retained_recolor(&context, true, true));
      assert(mailbox_kind == LottiePublicationKind::RETAIN && mailbox_source == context.work_buffer);
      assert(mailbox_calls == 2 && object.canvas == context.pixel_buffer);
      context.direct_frame_in_flight = true;
      const unsigned before_free = free_calls;
      assert(!lottie_free_resources(&context));
      assert(free_calls == before_free && allocations.size() == 3);
      context.direct_frame_in_flight = false;
    } else if (test >= 10 && test <= 13) {
      if (test == 10) {
        assert(lottie_render_frame(&context, 2, context.work_buffer));
        assert(lottie_publish_direct_opaque_frame(&context));
        assert(context.direct_frame_in_flight && queued_source != nullptr);
      }
      const unsigned renders_before_pause = render_calls;
      context.runtime_hidden = true;
      context.auto_start = test != 13;
      context.direct_region_active = test != 13;
      context.direct_frame_in_flight = test == 10;
      wait_success = true;
      release_success = test != 11;
      const bool continued = process_hidden_prefix(&context);
      assert(continued == (test == 12 || test == 13));
      assert(wait_calls == (test == 13 ? 0 : 1));
      assert(release_calls == (test == 10 || test == 13 ? 0 : 1));
      assert(notify_calls == (test == 10 || test == 11 ? 1 : 0));
      assert(render_calls == renders_before_pause && mailbox_calls == 0 && free_calls == 0);
      if (test == 10) {
        queued_callback(queued_callback_arg);
        assert(!context.direct_frame_in_flight && queued_source == nullptr);
        assert(process_hidden_prefix(&context));
        assert(wait_calls == 2 && release_calls == 1 && !context.direct_region_active);
        assert(render_calls == renders_before_pause && mailbox_calls == 0 && free_calls == 0);
      }
      if (test == 12)
        assert(!context.direct_region_active);
      wait_success = release_success = true;
      context.direct_frame_in_flight = false;
    } else if (test == 14 || test == 15) {
      context.prepared_frame_ready = true;
      wait_success = test != 14;
      assert(lottie_commit_direct_frame_to_canvas(&context) == (test == 15));
      assert(last_wait_timeout == 150 && mailbox_calls == (test == 15 ? 1 : 0));
      if (test == 15)
        assert(mailbox_source == context.work_buffer && mailbox_kind == LottiePublicationKind::RETAIN_HANDOFF);
      wait_success = true;
    }
    release_success = stop_success = true;
    assert(lottie_free_resources(&context));
    assert(allocations.empty());
    assert(context.pixel_buffer == nullptr && context.work_buffer == nullptr && context.work_buffer_alt == nullptr &&
           context.display_back_buffer == nullptr && !context.resource_cleanup_pending);
  }
  assert(allocations.empty());
  std::printf("case %u PASS\n", test);
}
