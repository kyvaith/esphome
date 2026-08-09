#include "esphome/components/lvgl_material/material_wavy_progress.h"
#include "esphome/core/log.h"

#include <math.h>
#include <atomic>
#include <stdint.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

extern "C" uint32_t lvgl_esphome_get_perf_logging_enabled(void);
extern "C" bool lvgl_esphome_direct_blit_rgb888(const uint8_t *src, int src_stride, int x, int y, int width,
                                                int height);
extern "C" uint8_t lvgl_esphome_direct_blit_rgb888_async(const uint8_t *src, int src_stride, int x, int y, int width,
                                                         int height, void (*ready_callback)(void *), void *ready_arg);
extern "C" uint8_t lvgl_esphome_direct_blend_rgb565_argb8888_async(const uint8_t *background, int background_stride,
                                                                   const uint8_t *foreground, int foreground_stride,
                                                                   int foreground_width, int foreground_height,
                                                                   int foreground_x, int foreground_y, int x, int y,
                                                                   int width, int height,
                                                                   void (*ready_callback)(void *), void *ready_arg);
extern "C" uint8_t lvgl_esphome_direct_blend_rgb565_argb8888_stable_async(
    const uint8_t *background, int background_stride, const uint8_t *foreground, int foreground_stride,
    int foreground_width, int foreground_height, int foreground_x, int foreground_y, int x, int y, int width,
    int height, void (*ready_callback)(void *), void *ready_arg);
extern "C" void lvgl_esphome_direct_blit_rgb888_release(int x, int y, int width, int height);
extern "C" bool lvgl_esphome_direct_regions_pause(bool paused, uint32_t timeout_ms);
#endif

namespace esphome::lvgl_material {
namespace wavy_progress_internal {

inline bool media_wavy_arc_present_ready(bool worker_context = false);

namespace {

// ESP-IDF's task stack size is expressed in bytes. Keep the backing array in
// StackType_t units while giving the raster worker enough headroom for LVGL,
// PPA and cache-sync calls.
constexpr uint32_t WAVE_RENDER_STACK_BYTES = 8192;
constexpr UBaseType_t WAVE_RENDER_TASK_PRIORITY = 1;
constexpr UBaseType_t WAVE_CONTROL_TASK_PRIORITY = 3;

// A 244-pixel RGB888 row is 4-byte aligned, allowing the DSI DMA2D path to
// consume the worker buffer directly instead of allocating/copying staging.
constexpr int WAVE_SIZE = 244;
constexpr int WAVE_PIXELS = WAVE_SIZE * WAVE_SIZE;
constexpr int WAVE_BYTES = WAVE_PIXELS * static_cast<int>(sizeof(lv_color32_t));
constexpr int WAVE_BACKDROP_BYTES = WAVE_PIXELS * static_cast<int>(sizeof(lv_color16_t));
constexpr size_t WAVE_BACKDROP_BUFFER_COUNT = 3;
constexpr int WAVE_CACHE_ALIGN = 128;
constexpr int WAVE_CENTER = (WAVE_SIZE - 1) / 2;
constexpr int WAVE_ACTIVE_RADIUS = 121;
constexpr int WAVE_ACTIVE_MIN = WAVE_CENTER - WAVE_ACTIVE_RADIUS;
constexpr int WAVE_ACTIVE_SIZE = WAVE_ACTIVE_RADIUS * 2 + 1;
constexpr uint32_t POLAR_RADIUS_MASK = 0x0FFFU;
constexpr float DEG_TO_RAD = 0.017453292519943295769f;
#ifdef ESP_PLATFORM
constexpr size_t WAVE_TRACE_CAPACITY = 160;
constexpr size_t WAVE_PRESENT_SLOT_COUNT = 2;
#endif

struct WavePolarPixel {
  uint16_t radius_q4;
  uint16_t angle_q4;
};

struct WavePixelSpan {
  uint16_t pixel_index;
  uint16_t count;
};

#ifdef ESP_PLATFORM
struct WavyArcState;

struct WavePresentSlot {
  WavyArcState *owner{nullptr};
  std::atomic<bool> in_flight{false};
  std::atomic<bool> complete{false};
  lv_color32_t *pixels{nullptr};
  lv_color16_t *backdrop_pixels{nullptr};
  uint32_t generation{0};
  uint32_t sequence{0};
  uint32_t interaction_sequence{0};
  bool stable{false};
  int64_t started_us{0};
};

struct WaveTraceEvent {
  uint32_t timestamp_us;
  uint32_t generation;
  uint32_t ready_generation;
  int16_t phase;
  int16_t loader_phase;
  uint8_t event;
  uint8_t buffer_index;
  uint8_t flags;
  uint8_t extra;
};
#endif

struct WavyArcState {
  lv_obj_t *arc{nullptr};
  bool registered{false};
  bool buffers_ready{false};
  bool dirty{true};
  int value_basis_points{0};
  bool playing{false};
  bool pending{false};
  bool pressed{false};
  int phase_deg{0};
  int loader_phase_deg{0};
  int last_rendered_value{-1};
  int last_rendered_phase{-1};
  int last_rendered_loader_phase{-1};
  bool last_rendered_pending{false};
  bool last_rendered_playing{false};
  bool last_rendered_pressed{false};
  std::atomic<bool> direct_present_enabled{false};
  lv_color32_t *pixels{nullptr};
  lv_color32_t *worker_pixels{nullptr};
  lv_color32_t *spare_pixels{nullptr};
  lv_color32_t *icon_buffer_pixels[3]{};
  bool icon_buffer_valid[3]{};
  bool icon_buffer_playing[3]{};
  bool icon_buffer_pressed[3]{};
  bool blob_buffer_valid[3]{};
  bool blob_buffer_pressed[3]{};
  lv_color16_t *backdrop_buffers[WAVE_BACKDROP_BUFFER_COUNT]{};
  lv_color16_t *backdrop_pixels{nullptr};
  WavePolarPixel *dynamic_pixels{nullptr};
  WavePixelSpan *dynamic_spans{nullptr};
  uint32_t dynamic_pixel_count{0};
  uint32_t dynamic_span_count{0};
  int16_t sin_q8[360];
  int16_t blob_boundary_by_angle[360];
  int16_t ring_boundary_by_angle[360];
  uint8_t cap_mask_by_angle[360];
  lv_image_dsc_t image{};
  bool background_pending{false};
  uint32_t background_request_generation{0};
  uint32_t background_ready_generation{0};
#ifdef ESP_PLATFORM
  SemaphoreHandle_t render_mutex{nullptr};
  StaticSemaphore_t render_mutex_storage{};
  TaskHandle_t render_task{nullptr};
  StaticTask_t render_task_storage{};
  StackType_t *render_task_stack{nullptr};
  std::atomic<int> queued_phase_delta{0};
  std::atomic<int> queued_value_basis_points{-1};
  std::atomic<int> queued_playback_state{-1};
  std::atomic<int> queued_playing{-1};
  std::atomic<int> queued_pending{-1};
  std::atomic<int> queued_pressed{-1};
  std::atomic<uint32_t> next_interaction_sequence{0};
  std::atomic<uint32_t> queued_interaction_sequence{0};
  std::atomic<uint32_t> presented_interaction_sequence{0};
  uint32_t render_generation{0};
  uint32_t ready_generation{0};
  uint32_t interaction_stable_generation{0xFFFFFFFFU};
  uint32_t interaction_stable_sequence{0};
  bool frame_ready{false};
  uint32_t stale_ready_drop_count{0};
  int direct_x{0};
  int direct_y{0};
  uint32_t perf_render_count{0};
  uint64_t perf_render_total_us{0};
  uint32_t perf_render_max_us{0};
  uint32_t perf_present_count{0};
  uint64_t perf_present_total_us{0};
  uint32_t perf_present_max_us{0};
  uint32_t perf_dma_count{0};
  uint64_t perf_dma_total_us{0};
  uint32_t perf_dma_max_us{0};
  int64_t perf_window_start_us{0};
  WavePresentSlot present_slots[WAVE_PRESENT_SLOT_COUNT]{};
  uint32_t next_present_sequence{1};
  std::atomic<bool> trace_enabled{false};
  std::atomic<uint32_t> trace_position{0};
  WaveTraceEvent trace_events[WAVE_TRACE_CAPACITY]{};
#endif
};

static WavyArcState media_wavy_arc;

#ifdef ESP_PLATFORM
static size_t present_in_flight_count(const WavyArcState *state) {
  if (state == nullptr)
    return 0;
  size_t count = 0;
  for (const auto &slot : state->present_slots)
    count += slot.in_flight.load(std::memory_order_acquire) ? 1U : 0U;
  return count;
}

static bool present_has_stable_frame(const WavyArcState *state) {
  if (state == nullptr)
    return false;
  for (const auto &slot : state->present_slots) {
    if (slot.in_flight.load(std::memory_order_acquire) && slot.stable)
      return true;
  }
  return false;
}

static uint8_t trace_buffer_index(const WavyArcState *state, const lv_color32_t *buffer) {
  if (state == nullptr || buffer == nullptr)
    return 9;
  for (uint8_t index = 0; index < 3; index++) {
    if (state->icon_buffer_pixels[index] == buffer)
      return index;
  }
  return 9;
}

static void record_wave_trace(WavyArcState *state, char event, const lv_color32_t *buffer = nullptr,
                              uint8_t extra = 0) {
  if (state == nullptr || !state->trace_enabled.load(std::memory_order_relaxed))
    return;
  const uint32_t position = state->trace_position.fetch_add(1, std::memory_order_relaxed);
  if (position >= WAVE_TRACE_CAPACITY)
    return;
  auto &entry = state->trace_events[position];
  entry.generation = state->render_generation;
  entry.ready_generation = state->ready_generation;
  entry.phase = static_cast<int16_t>(state->phase_deg);
  entry.loader_phase = static_cast<int16_t>(state->loader_phase_deg);
  entry.event = static_cast<uint8_t>(event);
  entry.buffer_index = trace_buffer_index(state, buffer);
  entry.flags = (state->pressed ? 0x01 : 0x00) | (state->playing ? 0x02 : 0x00) |
                (state->pending ? 0x04 : 0x00) |
                (present_in_flight_count(state) != 0 ? 0x08 : 0x00) | (state->frame_ready ? 0x10 : 0x00) |
                (present_has_stable_frame(state) ? 0x20 : 0x00);
  entry.extra = extra;
  std::atomic_thread_fence(std::memory_order_release);
  entry.timestamp_us = static_cast<uint32_t>(esp_timer_get_time());
}
#endif

static int align_up(int value, int alignment) { return (value + alignment - 1) & ~(alignment - 1); }

static uint8_t edge_coverage(int32_t distance_q4) {
  if (distance_q4 >= 16) {
    return 255;
  }
  if (distance_q4 <= -16) {
    return 0;
  }
  return static_cast<uint8_t>(((distance_q4 + 16) * 255) / 32);
}

static uint8_t circle_coverage_squared(int32_t distance_squared_q8, int32_t radius_q4) {
  const int32_t inner_radius_q4 = radius_q4 - 16;
  const int32_t outer_radius_q4 = radius_q4 + 16;
  const int32_t inner_squared_q8 = inner_radius_q4 * inner_radius_q4;
  const int32_t outer_squared_q8 = outer_radius_q4 * outer_radius_q4;
  if (distance_squared_q8 <= inner_squared_q8)
    return 255;
  if (distance_squared_q8 >= outer_squared_q8)
    return 0;
  return static_cast<uint8_t>(
      ((outer_squared_q8 - distance_squared_q8) * 255 + (outer_squared_q8 - inner_squared_q8) / 2) /
      (outer_squared_q8 - inner_squared_q8));
}

static int iabs_int(int value) { return value < 0 ? -value : value; }

static int angle_delta_cw_q4(int start_q4, int angle_q4) {
  constexpr int full_circle_q4 = 360 * 16;
  int delta = angle_q4 - start_q4;
  if (delta < 0)
    delta += full_circle_q4;
  if (delta >= full_circle_q4)
    delta -= full_circle_q4;
  return delta;
}

static bool angle_in_segment_q4(int start_q4, int sweep_q4, int angle_q4) {
  constexpr int full_circle_q4 = 360 * 16;
  if (sweep_q4 <= 0)
    return false;
  if (sweep_q4 >= full_circle_q4)
    return true;
  return angle_delta_cw_q4(start_q4, angle_q4) <= sweep_q4;
}

static int angle_distance_q4(int a_q4, int b_q4) {
  constexpr int full_circle_q4 = 360 * 16;
  constexpr int half_circle_q4 = 180 * 16;
  int distance = iabs_int(a_q4 - b_q4);
  return distance > half_circle_q4 ? full_circle_q4 - distance : distance;
}

static lv_color_t make_color(uint8_t red, uint8_t green, uint8_t blue) {
  lv_color_t color;
  color.red = red;
  color.green = green;
  color.blue = blue;
  return color;
}

static lv_color_t blend_rgb(const lv_color_t &background, uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha) {
  if (alpha == 0)
    return background;
  if (alpha == 255)
    return make_color(red, green, blue);
  const uint16_t inverse = 255U - alpha;
  return make_color(static_cast<uint8_t>((red * alpha + background.red * inverse + 127U) / 255U),
                    static_cast<uint8_t>((green * alpha + background.green * inverse + 127U) / 255U),
                    static_cast<uint8_t>((blue * alpha + background.blue * inverse + 127U) / 255U));
}

static lv_color32_t make_overlay(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha) {
  lv_color32_t color{};
  color.red = red;
  color.green = green;
  color.blue = blue;
  color.alpha = alpha;
  return color;
}

static lv_color32_t blend_overlay(const lv_color32_t &background, uint8_t red, uint8_t green, uint8_t blue,
                                  uint8_t alpha) {
  if (alpha == 0)
    return background;
  if (alpha == 255 || background.alpha == 0)
    return make_overlay(red, green, blue, alpha);
  const uint16_t inverse = 255U - alpha;
  const uint16_t out_alpha = alpha + (static_cast<uint16_t>(background.alpha) * inverse + 127U) / 255U;
  if (out_alpha == 0)
    return make_overlay(0, 0, 0, 0);
  const uint32_t background_weight = static_cast<uint32_t>(background.alpha) * inverse;
  return make_overlay(
      static_cast<uint8_t>((static_cast<uint32_t>(red) * alpha * 255U +
                            static_cast<uint32_t>(background.red) * background_weight + out_alpha * 127U) /
                           (out_alpha * 255U)),
      static_cast<uint8_t>((static_cast<uint32_t>(green) * alpha * 255U +
                            static_cast<uint32_t>(background.green) * background_weight + out_alpha * 127U) /
                           (out_alpha * 255U)),
      static_cast<uint8_t>((static_cast<uint32_t>(blue) * alpha * 255U +
                            static_cast<uint32_t>(background.blue) * background_weight + out_alpha * 127U) /
                           (out_alpha * 255U)),
      static_cast<uint8_t>(out_alpha));
}

static void init_image_descriptor(WavyArcState *state) {
  memset(&state->image, 0, sizeof(state->image));
  state->image.header.magic = LV_IMAGE_HEADER_MAGIC;
  state->image.header.cf = LV_COLOR_FORMAT_ARGB8888;
  state->image.header.w = WAVE_SIZE;
  state->image.header.h = WAVE_SIZE;
  state->image.header.stride = WAVE_SIZE * sizeof(lv_color32_t);
  state->image.data_size = WAVE_BYTES;
  state->image.data = reinterpret_cast<const uint8_t *>(state->pixels);
}

static bool ensure_buffers(WavyArcState *state) {
  if (state == nullptr) {
    return false;
  }
  if (state->buffers_ready) {
    return true;
  }

#ifdef ESP_PLATFORM
  const int aligned_bytes = align_up(WAVE_BYTES, WAVE_CACHE_ALIGN);
  state->pixels = static_cast<lv_color32_t *>(
      heap_caps_aligned_alloc(WAVE_CACHE_ALIGN, aligned_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  state->worker_pixels = static_cast<lv_color32_t *>(
      heap_caps_aligned_alloc(WAVE_CACHE_ALIGN, aligned_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  state->spare_pixels = static_cast<lv_color32_t *>(
      heap_caps_aligned_alloc(WAVE_CACHE_ALIGN, aligned_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  for (auto &buffer : state->backdrop_buffers) {
    buffer = static_cast<lv_color16_t *>(heap_caps_aligned_alloc(
        WAVE_CACHE_ALIGN, align_up(WAVE_BACKDROP_BYTES, WAVE_CACHE_ALIGN), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
#else
  state->pixels = static_cast<lv_color32_t *>(malloc(WAVE_BYTES));
  state->worker_pixels = static_cast<lv_color32_t *>(malloc(WAVE_BYTES));
  state->spare_pixels = static_cast<lv_color32_t *>(malloc(WAVE_BYTES));
  for (auto &buffer : state->backdrop_buffers)
    buffer = static_cast<lv_color16_t *>(malloc(WAVE_BACKDROP_BYTES));
#endif

  bool backdrop_buffers_ready = true;
  for (auto *buffer : state->backdrop_buffers)
    backdrop_buffers_ready &= buffer != nullptr;
  if (state->pixels == nullptr || state->worker_pixels == nullptr || state->spare_pixels == nullptr ||
      !backdrop_buffers_ready) {
    return false;
  }

  memset(state->pixels, 0, WAVE_BYTES);
  memset(state->worker_pixels, 0, WAVE_BYTES);
  memset(state->spare_pixels, 0, WAVE_BYTES);
  for (auto *buffer : state->backdrop_buffers)
    memset(buffer, 0, WAVE_BACKDROP_BYTES);
  state->backdrop_pixels = state->backdrop_buffers[0];
  state->icon_buffer_pixels[0] = state->pixels;
  state->icon_buffer_pixels[1] = state->worker_pixels;
  state->icon_buffer_pixels[2] = state->spare_pixels;

  for (int i = 0; i < 360; i++) {
    state->sin_q8[i] = static_cast<int16_t>(sinf(static_cast<float>(i) * DEG_TO_RAD) * 256.0f);
  }
  constexpr int static_blob_radius = 78;
  constexpr int blob_candidate_radius = 90;
  constexpr int ring_candidate_min_radius = 93;
  constexpr int ring_candidate_max_radius = 121;
  constexpr int static_blob_radius_sq = static_blob_radius * static_blob_radius;
  constexpr int blob_candidate_radius_sq = blob_candidate_radius * blob_candidate_radius;
  constexpr int ring_candidate_min_radius_sq = ring_candidate_min_radius * ring_candidate_min_radius;
  constexpr int ring_candidate_max_radius_sq = ring_candidate_max_radius * ring_candidate_max_radius;

  uint32_t dynamic_count = 0;
  uint32_t span_count = 0;
  for (int y = 0; y < WAVE_ACTIVE_SIZE; y++) {
    const int pixel_y = y + WAVE_ACTIVE_MIN;
    const int dy = pixel_y - WAVE_CENTER;
    bool in_span = false;
    for (int x = 0; x < WAVE_ACTIVE_SIZE; x++) {
      const int pixel_x = x + WAVE_ACTIVE_MIN;
      const int dx = pixel_x - WAVE_CENTER;
      const int radius_sq = dx * dx + dy * dy;
      if (radius_sq <= static_blob_radius_sq) {
        state->pixels[pixel_y * WAVE_SIZE + pixel_x] = make_overlay(0xE4, 0xC2, 0xFF, 255);
        in_span = false;
      } else if (radius_sq <= blob_candidate_radius_sq ||
                 (radius_sq >= ring_candidate_min_radius_sq && radius_sq <= ring_candidate_max_radius_sq)) {
        dynamic_count++;
        if (!in_span) {
          span_count++;
          in_span = true;
        }
      } else {
        in_span = false;
      }
    }
  }

#ifdef ESP_PLATFORM
  state->dynamic_pixels = static_cast<WavePolarPixel *>(
      heap_caps_malloc(dynamic_count * sizeof(WavePolarPixel), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  state->dynamic_spans = static_cast<WavePixelSpan *>(
      heap_caps_malloc(span_count * sizeof(WavePixelSpan), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
  state->dynamic_pixels = static_cast<WavePolarPixel *>(malloc(dynamic_count * sizeof(WavePolarPixel)));
  state->dynamic_spans = static_cast<WavePixelSpan *>(malloc(span_count * sizeof(WavePixelSpan)));
#endif
  if (state->dynamic_pixels == nullptr || state->dynamic_spans == nullptr) {
    return false;
  }

  uint32_t dynamic_index = 0;
  uint32_t span_index = 0;
  for (int y = 0; y < WAVE_ACTIVE_SIZE; y++) {
    const int pixel_y = y + WAVE_ACTIVE_MIN;
    int span_start = 0;
    uint16_t span_length = 0;
    for (int x = 0; x <= WAVE_ACTIVE_SIZE; x++) {
      const int pixel_x = x + WAVE_ACTIVE_MIN;
      const int dx_int = pixel_x - WAVE_CENTER;
      const int dy_int = pixel_y - WAVE_CENTER;
      const int radius_sq = dx_int * dx_int + dy_int * dy_int;
      const bool dynamic = x < WAVE_ACTIVE_SIZE && radius_sq > static_blob_radius_sq &&
                           (radius_sq <= blob_candidate_radius_sq ||
                            (radius_sq >= ring_candidate_min_radius_sq && radius_sq <= ring_candidate_max_radius_sq));
      if (!dynamic) {
        if (span_length > 0) {
          WavePixelSpan &span = state->dynamic_spans[span_index++];
          span.pixel_index = static_cast<uint16_t>(pixel_y * WAVE_SIZE + span_start);
          span.count = span_length;
          span_length = 0;
        }
        continue;
      }

      if (span_length == 0) {
        span_start = pixel_x;
      }
      span_length++;

      const float dx = static_cast<float>(dx_int);
      const float dy = static_cast<float>(dy_int);
      const float radius = sqrtf(dx * dx + dy * dy);
      float angle = atan2f(dy, dx) / DEG_TO_RAD;
      if (angle < 0.0f) {
        angle += 360.0f;
      }
      const uint32_t radius_q4 = static_cast<uint32_t>(radius * 16.0f + 0.5f);
      const uint32_t angle_q4 = static_cast<uint32_t>(static_cast<int>(angle * 16.0f + 0.5f) % (360 * 16));
      WavePolarPixel &entry = state->dynamic_pixels[dynamic_index++];
      entry.radius_q4 = static_cast<uint16_t>(radius_q4 & POLAR_RADIUS_MASK);
      entry.angle_q4 = static_cast<uint16_t>(angle_q4);
    }
  }
  state->dynamic_pixel_count = dynamic_index;
  state->dynamic_span_count = span_index;

  init_image_descriptor(state);
  state->buffers_ready = true;
  state->dirty = true;
  return true;
}

static uint8_t scaled_alpha(uint8_t coverage, uint8_t opacity) {
  return static_cast<uint8_t>((static_cast<int>(coverage) * static_cast<int>(opacity) + 127) / 255);
}

static uint8_t cap_coverage(int radius_q4, int angle_q4, int cap_angle_q4, int cap_radius_q4, int half_width_q4) {
  const int distance_angle_q4 = angle_distance_q4(angle_q4, cap_angle_q4);
  if (distance_angle_q4 > 5 * 16)
    return 0;
  const int radial_q4 = radius_q4 - cap_radius_q4;
  const int arc_q4 = (cap_radius_q4 * distance_angle_q4 * 314) / 288000;
  return circle_coverage_squared(radial_q4 * radial_q4 + arc_q4 * arc_q4, half_width_q4);
}

static lv_color_t read_source_pixel(const lv_image_dsc_t *source, int x, int y) {
  if (source == nullptr || source->data == nullptr || source->header.w == 0 || source->header.h == 0) {
    return make_color(0, 0, 0);
  }
  if (x < 0)
    x = 0;
  if (y < 0)
    y = 0;
  if (x >= source->header.w)
    x = source->header.w - 1;
  if (y >= source->header.h)
    y = source->header.h - 1;
  const uint8_t *row = source->data + static_cast<size_t>(y) * source->header.stride;
  switch (source->header.cf) {
    case LV_COLOR_FORMAT_RGB565: {
      lv_color16_t color16;
      memcpy(&color16, row + static_cast<size_t>(x) * sizeof(color16), sizeof(color16));
      return lv_color16_to_color(color16);
    }
    case LV_COLOR_FORMAT_RGB888: {
      lv_color_t color;
      memcpy(&color, row + static_cast<size_t>(x) * sizeof(color), sizeof(color));
      return color;
    }
    case LV_COLOR_FORMAT_ARGB8888:
    case LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED:
    case LV_COLOR_FORMAT_XRGB8888: {
      lv_color32_t color32;
      memcpy(&color32, row + static_cast<size_t>(x) * sizeof(color32), sizeof(color32));
      return make_color(color32.red, color32.green, color32.blue);
    }
    default:
      return make_color(0, 0, 0);
  }
}

static bool capture_backdrop(WavyArcState *state, lv_color16_t *target, const lv_image_dsc_t *source,
                             bool apply_scrim) {
  if (!ensure_buffers(state) || target == nullptr)
    return false;

  int display_width = source != nullptr && source->header.w > 0 ? source->header.w : WAVE_SIZE;
  int display_height = source != nullptr && source->header.h > 0 ? source->header.h : WAVE_SIZE;
  if (state->arc != nullptr) {
    lv_display_t *display = lv_obj_get_display(state->arc);
    if (display != nullptr) {
      const int horizontal_resolution = lv_display_get_horizontal_resolution(display);
      const int vertical_resolution = lv_display_get_vertical_resolution(display);
      if (horizontal_resolution > 0)
        display_width = horizontal_resolution;
      if (vertical_resolution > 0)
        display_height = vertical_resolution;
    }
  }

  int origin_x = (display_width - WAVE_SIZE) / 2;
  int origin_y = (display_height - WAVE_SIZE) / 2;
  if (state->arc != nullptr) {
    lv_area_t area{};
    lv_obj_get_coords(state->arc, &area);
    if (lv_area_get_width(&area) > 0 && lv_area_get_height(&area) > 0) {
      origin_x = area.x1;
      origin_y = area.y1;
    }
  }

  constexpr uint8_t scrim_red = 0x21;
  constexpr uint8_t scrim_green = 0x0F;
  constexpr uint8_t scrim_blue = 0x48;
  constexpr uint8_t scrim_alpha = 153;
  const bool direct_rgb565_crop =
      source != nullptr && source->data != nullptr && source->header.cf == LV_COLOR_FORMAT_RGB565 &&
      source->header.w == display_width && source->header.h == display_height &&
      source->header.stride >= display_width * static_cast<int>(sizeof(uint16_t)) && origin_x >= 0 && origin_y >= 0 &&
      origin_x + WAVE_SIZE <= display_width && origin_y + WAVE_SIZE <= display_height;
  for (int y = 0; y < WAVE_SIZE; y++) {
    const int display_y = origin_y + y;
    const int source_y = source != nullptr && source->header.h > 0
                             ? (display_y * static_cast<int>(source->header.h)) / display_height
                             : 0;
    const uint8_t *direct_row = direct_rgb565_crop
                                    ? source->data + static_cast<size_t>(display_y) * source->header.stride +
                                          static_cast<size_t>(origin_x) * sizeof(uint16_t)
                                    : nullptr;
    if (direct_row != nullptr && !apply_scrim) {
      memcpy(target + static_cast<size_t>(y) * WAVE_SIZE, direct_row, WAVE_SIZE * sizeof(lv_color16_t));
      continue;
    }
    for (int x = 0; x < WAVE_SIZE; x++) {
      const int display_x = origin_x + x;
      const int source_x = source != nullptr && source->header.w > 0
                               ? (display_x * static_cast<int>(source->header.w)) / display_width
                               : 0;
      lv_color_t color;
      if (direct_row != nullptr) {
        lv_color16_t color16;
        memcpy(&color16, direct_row + static_cast<size_t>(x) * sizeof(color16), sizeof(color16));
        color = lv_color16_to_color(color16);
      } else {
        color = read_source_pixel(source, source_x, source_y);
      }
      if (apply_scrim) {
        color = blend_rgb(color, scrim_red, scrim_green, scrim_blue, scrim_alpha);
      }
      const int index = y * WAVE_SIZE + x;
      const uint16_t color16 = lv_color_to_u16(color);
      memcpy(&target[index], &color16, sizeof(color16));
    }
  }

  return true;
}

static lv_color16_t *find_free_backdrop(WavyArcState *state) {
  if (state == nullptr)
    return nullptr;
  for (auto *buffer : state->backdrop_buffers) {
    if (buffer == nullptr || buffer == state->backdrop_pixels)
      continue;
#ifdef ESP_PLATFORM
    bool presented = false;
    for (const auto &slot : state->present_slots) {
      presented |= slot.in_flight.load(std::memory_order_acquire) && slot.backdrop_pixels == buffer;
    }
    if (presented)
      continue;
#endif
    return buffer;
  }
  return nullptr;
}

static void reset_control_icon_area(WavyArcState *state) {
  // The rotating blob edge is already rebuilt from dynamic_pixels. Only the
  // previous play/pause glyph needs clearing, and this rectangle is safely
  // inside the blob's minimum radius for every phase.
  constexpr int x1 = WAVE_CENTER - 33;
  constexpr int x2 = WAVE_CENTER + 33;
  constexpr int y1 = WAVE_CENTER - 32;
  constexpr int y2 = WAVE_CENTER + 32;
  const lv_color32_t fill = state->pressed ? make_overlay(0xC4, 0xA7, 0xDB, 255) : make_overlay(0xE4, 0xC2, 0xFF, 255);
  for (int y = y1; y <= y2; y++) {
    lv_color32_t *row = state->pixels + y * WAVE_SIZE;
    for (int x = x1; x <= x2; x++) {
      row[x] = fill;
    }
  }
}

static void update_static_blob_if_needed(WavyArcState *state) {
  constexpr int static_blob_radius = 78;
  constexpr int static_blob_radius_sq = static_blob_radius * static_blob_radius;
  for (size_t i = 0; i < 3; i++) {
    if (state->icon_buffer_pixels[i] != state->pixels)
      continue;
    if (state->blob_buffer_valid[i] && state->blob_buffer_pressed[i] == state->pressed)
      return;
    const lv_color32_t fill =
        state->pressed ? make_overlay(0xC4, 0xA7, 0xDB, 255) : make_overlay(0xE4, 0xC2, 0xFF, 255);
    for (int y = WAVE_CENTER - static_blob_radius; y <= WAVE_CENTER + static_blob_radius; y++) {
      const int dy = y - WAVE_CENTER;
      lv_color32_t *row = state->pixels + y * WAVE_SIZE;
      for (int x = WAVE_CENTER - static_blob_radius; x <= WAVE_CENTER + static_blob_radius; x++) {
        const int dx = x - WAVE_CENTER;
        if (dx * dx + dy * dy <= static_blob_radius_sq)
          row[x] = fill;
      }
    }
    state->blob_buffer_pressed[i] = state->pressed;
    state->blob_buffer_valid[i] = true;
    state->icon_buffer_valid[i] = false;
    return;
  }
}

static void draw_capsule(WavyArcState *state, int center_x, int center_y, int half_length, int radius, uint8_t red,
                         uint8_t green, uint8_t blue) {
  const int x1 = center_x - radius - 1;
  const int x2 = center_x + radius + 1;
  const int y1 = center_y - half_length - radius - 1;
  const int y2 = center_y + half_length + radius + 1;
  for (int y = y1; y <= y2; y++) {
    const int nearest_y =
        y < center_y - half_length ? center_y - half_length : (y > center_y + half_length ? center_y + half_length : y);
    const int dy_q4 = (y - nearest_y) * 16;
    for (int x = x1; x <= x2; x++) {
      const int dx_q4 = (x - center_x) * 16;
      const uint8_t coverage = circle_coverage_squared(dx_q4 * dx_q4 + dy_q4 * dy_q4, radius * 16);
      if (coverage == 0)
        continue;
      const int index = y * WAVE_SIZE + x;
      state->pixels[index] = blend_overlay(state->pixels[index], red, green, blue, coverage);
    }
  }
}

static int triangle_edge(int ax, int ay, int bx, int by, int px, int py) {
  return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static void draw_play_icon(WavyArcState *state) {
  // Four sub-pixel samples keep the diagonal edges smooth without involving
  // LVGL's much heavier label/glyph redraw path on every animation frame.
  constexpr int x1 = (WAVE_CENTER - 16) * 4;
  constexpr int y1 = (WAVE_CENTER - 28) * 4;
  constexpr int x2 = (WAVE_CENTER - 16) * 4;
  constexpr int y2 = (WAVE_CENTER + 28) * 4;
  constexpr int x3 = (WAVE_CENTER + 30) * 4;
  constexpr int y3 = WAVE_CENTER * 4;
  constexpr int sample_offsets[2] = {1, 3};
  for (int y = WAVE_CENTER - 30; y <= WAVE_CENTER + 30; y++) {
    for (int x = WAVE_CENTER - 18; x <= WAVE_CENTER + 32; x++) {
      uint8_t inside = 0;
      for (int sy : sample_offsets) {
        for (int sx : sample_offsets) {
          const int px = x * 4 + sx;
          const int py = y * 4 + sy;
          const int e1 = triangle_edge(x1, y1, x2, y2, px, py);
          const int e2 = triangle_edge(x2, y2, x3, y3, px, py);
          const int e3 = triangle_edge(x3, y3, x1, y1, px, py);
          if ((e1 <= 0 && e2 <= 0 && e3 <= 0) || (e1 >= 0 && e2 >= 0 && e3 >= 0))
            inside++;
        }
      }
      if (inside == 0)
        continue;
      const int index = y * WAVE_SIZE + x;
      state->pixels[index] = blend_overlay(state->pixels[index], 0x40, 0x00, 0x60, inside * 255U / 4U);
    }
  }
}

static void draw_control_icon(WavyArcState *state) {
  if (state->playing) {
    draw_capsule(state, WAVE_CENTER - 14, WAVE_CENTER, 20, 8, 0x40, 0x00, 0x60);
    draw_capsule(state, WAVE_CENTER + 14, WAVE_CENTER, 20, 8, 0x40, 0x00, 0x60);
  } else {
    draw_play_icon(state);
  }
}

static void update_control_icon_if_needed(WavyArcState *state) {
  for (size_t i = 0; i < 3; i++) {
    if (state->icon_buffer_pixels[i] != state->pixels)
      continue;
    if (state->icon_buffer_valid[i] && state->icon_buffer_playing[i] == state->playing &&
        state->icon_buffer_pressed[i] == state->pressed)
      return;
    reset_control_icon_area(state);
    draw_control_icon(state);
    state->icon_buffer_playing[i] = state->playing;
    state->icon_buffer_pressed[i] = state->pressed;
    state->icon_buffer_valid[i] = true;
    return;
  }
}

static void render_bitmap(WavyArcState *state) {
  if (!ensure_buffers(state)) {
    return;
  }

  const int phase = state->phase_deg;
  const int loader_phase = state->loader_phase_deg;
  const int stored_progress =
      state->value_basis_points < 0 ? 0 : (state->value_basis_points > 10000 ? 10000 : state->value_basis_points);
  const bool pending = state->pending;
  // While transport confirmation is pending, retain the real progress in the
  // state but replace it visually with one inactive ring and the moving loader.
  // Overlaying the loader on a nearly full progress segment made it invisible.
  const int progress = pending ? 0 : stored_progress;
  if (!state->dirty && state->last_rendered_phase == phase && state->last_rendered_loader_phase == loader_phase &&
      state->last_rendered_value == stored_progress && state->last_rendered_pending == pending &&
      state->last_rendered_playing == state->playing && state->last_rendered_pressed == state->pressed) {
    return;
  }

#ifdef ESP_PLATFORM
  const bool perf_enabled = lvgl_esphome_get_perf_logging_enabled() != 0;
  const int64_t render_start_us = perf_enabled ? esp_timer_get_time() : 0;
#endif

  constexpr int waves = 10;
  constexpr int blob_amplitude_q4 = 4 * 16;
  constexpr int ring_amplitude_q4 = 6 * 16;
  constexpr int blob_radius_q4 = 84 * 16;
  constexpr int ring_radius_q4 = 107 * 16;
  constexpr int ring_half_width_q4 = 6 * 16;
  constexpr int full_circle_q4 = 360 * 16;
  constexpr int track_start_q4 = 270 * 16;
  constexpr int split_gap_q4 = 12 * 16;
  constexpr int min_segment_sweep_q4 = 10 * 16;
  constexpr int loading_sweep_q4 = 18 * 16;

  const bool finite_track = progress > 0 && progress < 10000;
  const int finite_available_sweep_q4 = full_circle_q4 - (2 * split_gap_q4);
  const int finite_variable_sweep_q4 = finite_available_sweep_q4 - (2 * min_segment_sweep_q4);
  const int progress_sweep_q4 = finite_track ? (min_segment_sweep_q4 + (finite_variable_sweep_q4 * progress) / 10000)
                                             : (full_circle_q4 * progress) / 10000;
  const int played_start_q4 = track_start_q4;
  const int played_end_q4 = (played_start_q4 + progress_sweep_q4) % full_circle_q4;
  const int remaining_start_q4 = finite_track ? (played_end_q4 + split_gap_q4) % full_circle_q4 : played_end_q4;
  const int remaining_end_q4 =
      finite_track ? (track_start_q4 - split_gap_q4 + full_circle_q4) % full_circle_q4 : track_start_q4;
  const int played_sweep_q4 = progress_sweep_q4;
  const int remaining_sweep_q4 =
      finite_track ? (finite_available_sweep_q4 - played_sweep_q4) : (full_circle_q4 - progress_sweep_q4);
  const int loading_start_q4 = (track_start_q4 + (loader_phase * 16)) % full_circle_q4;
  const int loading_end_q4 = (loading_start_q4 + loading_sweep_q4) % full_circle_q4;
  const bool full_progress = progress >= 10000;
  const bool no_progress = progress <= 0;
  // The inactive part always uses the same light translucent Material color.
  // Pending playback adds a brighter moving segment without changing the base.
  // Wave radii depend only on the integer angle. Segment ownership uses the
  // stored 1/16-degree pixel angle below so the progress endpoint can advance
  // smoothly without allocating a second high-resolution lookup table.
  for (int angle = 0; angle < 360; angle++) {
    const int wave_index = (angle * waves + phase) % 360;
    const int wave_q8 = state->sin_q8[wave_index < 0 ? wave_index + 360 : wave_index];
    state->blob_boundary_by_angle[angle] = blob_radius_q4 + (blob_amplitude_q4 * wave_q8) / 256;
    state->ring_boundary_by_angle[angle] = ring_radius_q4 + (ring_amplitude_q4 * wave_q8) / 256;

    const int angle_q4 = angle * 16;
    uint8_t cap_mask = 0;
    if (finite_track) {
      // Rounded progress endpoints occupy only a few angular bins. Building
      // this tiny map once avoids four circular-distance calculations for
      // every one of the ~19k ring pixels in each animation frame.
      if (angle_distance_q4(angle_q4, played_start_q4) <= 6 * 16)
        cap_mask |= 0x01;
      if (angle_distance_q4(angle_q4, played_end_q4) <= 6 * 16)
        cap_mask |= 0x02;
      if (angle_distance_q4(angle_q4, remaining_start_q4) <= 6 * 16)
        cap_mask |= 0x04;
      if (angle_distance_q4(angle_q4, remaining_end_q4) <= 6 * 16)
        cap_mask |= 0x08;
    }
    if (pending) {
      if (angle_distance_q4(angle_q4, loading_start_q4) <= 6 * 16)
        cap_mask |= 0x10;
      if (angle_distance_q4(angle_q4, loading_end_q4) <= 6 * 16)
        cap_mask |= 0x20;
    }
    state->cap_mask_by_angle[angle] = cap_mask;
  }

  update_static_blob_if_needed(state);

  // Pixels outside these two radial bands are transparent for every phase and
  // were cleared once when the buffer was allocated. Avoid touching them.
  constexpr int blob_possible_max_q4 = (84 + 4 + 1) * 16;
  constexpr int ring_possible_min_q4 = (107 - 6 - 6 - 1) * 16;
  constexpr int ring_possible_max_q4 = (107 + 6 + 6 + 1) * 16;
  uint32_t dynamic_index = 0;
  for (uint32_t span_index = 0; span_index < state->dynamic_span_count; span_index++) {
    const WavePixelSpan &span = state->dynamic_spans[span_index];
    uint32_t pixel_index = span.pixel_index;
    const uint32_t span_end = dynamic_index + span.count;
    for (; dynamic_index < span_end; dynamic_index++, pixel_index++) {
      const WavePolarPixel &entry = state->dynamic_pixels[dynamic_index];
      const int radius = static_cast<int>(entry.radius_q4);
      const int angle_q4 = static_cast<int>(entry.angle_q4);
      const int angle = ((angle_q4 + 8) / 16) % 360;

      if (radius <= blob_possible_max_q4) {
        const uint8_t fill_alpha = edge_coverage(state->blob_boundary_by_angle[angle] - radius);
        state->pixels[pixel_index] =
            state->pressed ? make_overlay(0xC4, 0xA7, 0xDB, fill_alpha) : make_overlay(0xE4, 0xC2, 0xFF, fill_alpha);
        continue;
      }

      if (radius <= ring_possible_min_q4 || radius >= ring_possible_max_q4) {
        state->pixels[pixel_index] = make_overlay(0, 0, 0, 0);
        continue;
      }

      const uint8_t ring_alpha =
          edge_coverage(ring_half_width_q4 - iabs_int(radius - state->ring_boundary_by_angle[angle]));
      bool in_played = full_progress;
      bool in_remaining = no_progress;
      if (finite_track) {
        int track_delta_q4 = angle_q4 - track_start_q4;
        if (track_delta_q4 < 0)
          track_delta_q4 += full_circle_q4;
        in_played = track_delta_q4 <= played_sweep_q4;
        in_remaining =
            track_delta_q4 >= played_sweep_q4 + split_gap_q4 && track_delta_q4 <= full_circle_q4 - split_gap_q4;
      }
      const bool in_loading = pending && angle_in_segment_q4(loading_start_q4, loading_sweep_q4, angle_q4);
      uint8_t loading_coverage = in_loading ? ring_alpha : 0;
      uint8_t played_coverage = in_played ? ring_alpha : 0;
      uint8_t remaining_coverage = in_remaining ? ring_alpha : 0;
      const uint8_t cap_mask = state->cap_mask_by_angle[angle];
      if (cap_mask != 0) {
        constexpr int cap_half_width_q4 = ring_half_width_q4;
        if ((cap_mask & 0x01) != 0) {
          const uint8_t coverage =
              cap_coverage(radius, angle_q4, played_start_q4,
                           state->ring_boundary_by_angle[((played_start_q4 + 8) / 16) % 360], cap_half_width_q4);
          if (coverage > played_coverage)
            played_coverage = coverage;
        }
        if ((cap_mask & 0x02) != 0) {
          const uint8_t coverage =
              cap_coverage(radius, angle_q4, played_end_q4,
                           state->ring_boundary_by_angle[((played_end_q4 + 8) / 16) % 360], cap_half_width_q4);
          if (coverage > played_coverage)
            played_coverage = coverage;
        }
        if ((cap_mask & 0x04) != 0) {
          const uint8_t coverage =
              cap_coverage(radius, angle_q4, remaining_start_q4,
                           state->ring_boundary_by_angle[((remaining_start_q4 + 8) / 16) % 360], cap_half_width_q4);
          if (coverage > remaining_coverage)
            remaining_coverage = coverage;
        }
        if ((cap_mask & 0x08) != 0) {
          const uint8_t coverage =
              cap_coverage(radius, angle_q4, remaining_end_q4,
                           state->ring_boundary_by_angle[((remaining_end_q4 + 8) / 16) % 360], cap_half_width_q4);
          if (coverage > remaining_coverage)
            remaining_coverage = coverage;
        }
        if ((cap_mask & 0x10) != 0) {
          const uint8_t coverage =
              cap_coverage(radius, angle_q4, loading_start_q4,
                           state->ring_boundary_by_angle[((loading_start_q4 + 8) / 16) % 360], cap_half_width_q4);
          if (coverage > loading_coverage)
            loading_coverage = coverage;
        }
        if ((cap_mask & 0x20) != 0) {
          const uint8_t coverage =
              cap_coverage(radius, angle_q4, loading_end_q4,
                           state->ring_boundary_by_angle[((loading_end_q4 + 8) / 16) % 360], cap_half_width_q4);
          if (coverage > loading_coverage)
            loading_coverage = coverage;
        }
      }

      if (loading_coverage > 0) {
        state->pixels[pixel_index] = make_overlay(0xF8, 0xEC, 0xFF, scaled_alpha(loading_coverage, 232));
      } else if (played_coverage >= remaining_coverage && played_coverage > 0) {
        state->pixels[pixel_index] = make_overlay(0xF1, 0xDC, 0xFF, scaled_alpha(played_coverage, 224));
      } else if (remaining_coverage > 0) {
        state->pixels[pixel_index] = make_overlay(0xF1, 0xDC, 0xFF, scaled_alpha(remaining_coverage, 152));
      } else {
        state->pixels[pixel_index] = make_overlay(0, 0, 0, 0);
      }
    }
  }

  // The glyph is static while the wavy surface rotates. Track it per physical
  // triple buffer so steady animation frames do not clear and rasterize the
  // same 67x65 area again.
  update_control_icon_if_needed(state);

  state->last_rendered_phase = phase;
  state->last_rendered_loader_phase = loader_phase;
  state->last_rendered_value = stored_progress;
  state->last_rendered_pending = pending;
  state->last_rendered_playing = state->playing;
  state->last_rendered_pressed = state->pressed;
  state->dirty = false;

#ifdef ESP_PLATFORM
  if (perf_enabled) {
    const int64_t now_us = esp_timer_get_time();
    const uint32_t render_us = static_cast<uint32_t>(now_us - render_start_us);
    state->perf_render_count++;
    state->perf_render_total_us += render_us;
    if (render_us > state->perf_render_max_us)
      state->perf_render_max_us = render_us;
    if (state->perf_window_start_us == 0)
      state->perf_window_start_us = now_us;
    if (now_us - state->perf_window_start_us >= 2000000LL) {
      const uint32_t avg_us = state->perf_render_count == 0
                                  ? 0
                                  : static_cast<uint32_t>(state->perf_render_total_us / state->perf_render_count);
      const uint32_t present_avg_us =
          state->perf_present_count == 0
              ? 0
              : static_cast<uint32_t>(state->perf_present_total_us / state->perf_present_count);
      const uint32_t dma_avg_us =
          state->perf_dma_count == 0 ? 0 : static_cast<uint32_t>(state->perf_dma_total_us / state->perf_dma_count);
      ESP_LOGI("media.wave",
               "perf2s: renders=%lu render=%luus max=%luus submit=%luus max=%luus dma=%luus max=%luus pixels=%d",
               static_cast<unsigned long>(state->perf_render_count), static_cast<unsigned long>(avg_us),
               static_cast<unsigned long>(state->perf_render_max_us), static_cast<unsigned long>(present_avg_us),
               static_cast<unsigned long>(state->perf_present_max_us), static_cast<unsigned long>(dma_avg_us),
               static_cast<unsigned long>(state->perf_dma_max_us), WAVE_PIXELS);
      state->perf_render_count = 0;
      state->perf_render_total_us = 0;
      state->perf_render_max_us = 0;
      state->perf_present_count = 0;
      state->perf_present_total_us = 0;
      state->perf_present_max_us = 0;
      state->perf_dma_count = 0;
      state->perf_dma_total_us = 0;
      state->perf_dma_max_us = 0;
      state->perf_window_start_us = now_us;
    }
  }
#endif
}

static void invalidate_bitmap(WavyArcState *state) {
  if (state == nullptr || state->arc == nullptr)
    return;
  render_bitmap(state);
  lv_obj_invalidate(state->arc);
}

#ifdef ESP_PLATFORM
static void notify_render_worker(WavyArcState *state) {
  if (state != nullptr && state->render_task != nullptr) {
    xTaskNotifyGive(state->render_task);
  }
}

static void direct_present_ready(void *arg) {
  auto *slot = static_cast<WavePresentSlot *>(arg);
  if (slot != nullptr && slot->owner != nullptr) {
    record_wave_trace(slot->owner, 'C', slot->pixels, static_cast<uint8_t>(slot->sequence & 0xFFU));
    if (slot->interaction_sequence != 0) {
      slot->owner->presented_interaction_sequence.store(slot->interaction_sequence, std::memory_order_release);
    }
    slot->complete.store(true, std::memory_order_release);
    // Retire the finished source and publish an already-rendered control
    // frame immediately. Waiting for the 16-ms ESPHome interval here can cost
    // several display periods while LVGL or network work occupies the loop.
    notify_render_worker(slot->owner);
  }
}

static void render_worker_task(void *arg) {
  auto *state = static_cast<WavyArcState *>(arg);
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    // A completed frame already provides natural back-pressure. Draining any
    // notifications queued while the worker was busy coalesces state changes
    // without yielding a full FreeRTOS tick before every frame.
    ulTaskNotifyTake(pdTRUE, 0);

    // Direct-region submission is queue based and thread-safe. Servicing it on
    // the raster worker removes main-loop latency from touch feedback while
    // the render mutex still serializes it with the regular UI-loop service.
    media_wavy_arc_present_ready(true);

    if (xSemaphoreTake(state->render_mutex, portMAX_DELAY) != pdTRUE)
      continue;
    bool control_changed = false;
    bool pressed_update_received = false;
    bool pressed_changed = false;
    const int queued_value = state->queued_value_basis_points.exchange(-1, std::memory_order_acquire);
    if (queued_value >= 0 && state->value_basis_points != queued_value) {
      state->value_basis_points = queued_value;
      control_changed = true;
    }
    const int queued_playback_state = state->queued_playback_state.exchange(-1, std::memory_order_acquire);
    if (queued_playback_state >= 0) {
      // Playing and pending describe one visual state. Apply them under the
      // same lock and generation so no intermediate play/pause or progress
      // frame can reach the DSI queue.
      state->queued_playing.store(-1, std::memory_order_relaxed);
      state->queued_pending.store(-1, std::memory_order_relaxed);
      const bool playing = (queued_playback_state & 0x01) != 0;
      const bool pending = (queued_playback_state & 0x02) != 0;
      if (state->playing != playing || state->pending != pending) {
        state->playing = playing;
        state->pending = pending;
        control_changed = true;
      }
      record_wave_trace(state, 'U', nullptr, static_cast<uint8_t>(queued_playback_state));
    } else {
      const int queued_playing = state->queued_playing.exchange(-1, std::memory_order_acquire);
      if (queued_playing >= 0 && state->playing != (queued_playing != 0)) {
        state->playing = queued_playing != 0;
        control_changed = true;
      }
      const int queued_pending = state->queued_pending.exchange(-1, std::memory_order_acquire);
      if (queued_pending >= 0 && state->pending != (queued_pending != 0)) {
        state->pending = queued_pending != 0;
        control_changed = true;
      }
    }
    const int queued_pressed = state->queued_pressed.exchange(-1, std::memory_order_acquire);
    const uint32_t queued_interaction_sequence =
        queued_pressed >= 0 ? state->queued_interaction_sequence.exchange(0, std::memory_order_acquire) : 0;
    pressed_update_received = queued_pressed >= 0;
    if (queued_pressed >= 0 && state->pressed != (queued_pressed != 0)) {
      state->pressed = queued_pressed != 0;
      pressed_changed = true;
      control_changed = true;
    }
    if (pressed_update_received)
      record_wave_trace(state, 'P', nullptr, static_cast<uint8_t>(queued_pressed));
    if (control_changed) {
      state->dirty = true;
      state->render_generation++;
      if (pressed_update_received) {
        // Both touch-down and touch-up are stable visual boundaries. Keeping
        // the release overlay cached prevents an older pressed overlay still
        // present in another DSI framebuffer from resurfacing while transport
        // confirmation temporarily stops phase updates.
        state->interaction_stable_generation = state->render_generation;
        state->interaction_stable_sequence = queued_interaction_sequence;
      }
    } else if (pressed_update_received && !pressed_changed && queued_interaction_sequence != 0) {
      // A duplicate boundary already matches the stable visible state. Do not
      // force another raster frame, but do release its bounded caller wait.
      state->presented_interaction_sequence.store(queued_interaction_sequence, std::memory_order_release);
    }
    // A completed buffer is immutable while it is current. If a newer control
    // state arrived before presentation, the buffer is still worker-owned and
    // can be discarded instead of briefly presenting the obsolete geometry.
    if (state->frame_ready) {
      if (state->ready_generation == state->render_generation) {
        xSemaphoreGive(state->render_mutex);
        if (pressed_update_received)
          vTaskPrioritySet(state->render_task, WAVE_RENDER_TASK_PRIORITY);
        continue;
      }
      state->frame_ready = false;
      state->dirty = true;
      state->stale_ready_drop_count++;
      record_wave_trace(state, 'D', state->worker_pixels);
    }
    const int phase_delta = state->queued_phase_delta.exchange(0, std::memory_order_relaxed);
    // Freeze the geometry on touch-down, not only after LVGL emits CLICKED on
    // release. Also discard the phase accumulated in the same worker cycle as
    // either press transition; otherwise releasing the button can apply one
    // final stale step immediately before the transport request is queued.
    const bool base_phase_active = state->playing && !state->pending && !state->pressed && !pressed_update_received;
    const bool loader_phase_active = state->pending;
    const bool phase_requested = phase_delta != 0 && (base_phase_active || loader_phase_active);
    if (!state->direct_present_enabled || (!state->dirty && !phase_requested)) {
      xSemaphoreGive(state->render_mutex);
      if (pressed_update_received)
        vTaskPrioritySet(state->render_task, WAVE_RENDER_TASK_PRIORITY);
      continue;
    }

    if (phase_requested) {
      // A transport request freezes the base wave immediately. Only the
      // loader segment advances while confirmation is pending; otherwise the
      // last visible wave phase can appear to rock back and forth as pending
      // and confirmed frames alternate in the presentation queue.
      if (base_phase_active) {
        state->phase_deg = (state->phase_deg + phase_delta) % 360;
        if (state->phase_deg < 0)
          state->phase_deg += 360;
      }
      if (loader_phase_active) {
        state->loader_phase_deg = (state->loader_phase_deg + phase_delta * 5) % 360;
        if (state->loader_phase_deg < 0)
          state->loader_phase_deg += 360;
      }
      state->dirty = true;
    }

    if (state->worker_pixels == nullptr) {
      // All three buffers are currently the native front plus two queued
      // regional frames. Keep only the newest logical state; a completion
      // callback returns a raster target and wakes this worker again.
      state->dirty = true;
      xSemaphoreGive(state->render_mutex);
      if (pressed_update_received)
        vTaskPrioritySet(state->render_task, WAVE_RENDER_TASK_PRIORITY);
      continue;
    }

    const uint32_t generation = state->render_generation;
    lv_color32_t *front = state->pixels;
    lv_color32_t *rendered = state->worker_pixels;
    state->pixels = rendered;
    render_bitmap(state);
    state->pixels = front;
    xSemaphoreGive(state->render_mutex);
    if (pressed_update_received)
      vTaskPrioritySet(state->render_task, WAVE_RENDER_TASK_PRIORITY);

    if (xSemaphoreTake(state->render_mutex, portMAX_DELAY) != pdTRUE)
      continue;
    const bool ready_to_present = generation == state->render_generation;
    if (ready_to_present) {
      state->ready_generation = generation;
      state->frame_ready = true;
      record_wave_trace(state, 'R', state->worker_pixels);
    } else {
      state->dirty = true;
    }
    xSemaphoreGive(state->render_mutex);
    if (ready_to_present && state->direct_present_enabled.load(std::memory_order_relaxed))
      media_wavy_arc_present_ready(true);
  }
}

static bool ensure_render_worker(WavyArcState *state) {
  if (state->render_mutex == nullptr) {
    state->render_mutex = xSemaphoreCreateMutexStatic(&state->render_mutex_storage);
    if (state->render_mutex == nullptr)
      return false;
  }
  if (state->render_task != nullptr)
    return true;
  if (state->render_task_stack == nullptr) {
    state->render_task_stack = static_cast<StackType_t *>(
        heap_caps_aligned_alloc(16, WAVE_RENDER_STACK_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (state->render_task_stack == nullptr) {
      state->render_task_stack = static_cast<StackType_t *>(
          heap_caps_aligned_alloc(16, WAVE_RENDER_STACK_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    if (state->render_task_stack == nullptr)
      return false;
  }
#if CONFIG_FREERTOS_UNICORE
  constexpr BaseType_t render_core = tskNO_AFFINITY;
#else
  // The regional PPA compositor is deliberately created on the other core.
  // Keep CPU rasterization on the caller/loop core so the 15 ms raster and
  // the PPA submission can overlap instead of serializing on one CPU.
  const BaseType_t render_core = xPortGetCoreID();
#endif
  // Audio and AFE tasks use much higher priorities, so they can still pre-empt
  // this best-effort worker without the UI renderer stalling audio delivery.
  TaskHandle_t task = xTaskCreateStaticPinnedToCore(render_worker_task, "media_wave", WAVE_RENDER_STACK_BYTES, state,
                                                    WAVE_RENDER_TASK_PRIORITY, state->render_task_stack,
                                                    &state->render_task_storage, render_core);
  if (task == nullptr) {
    heap_caps_free(state->render_task_stack);
    state->render_task_stack = nullptr;
    return false;
  }
  state->render_task = task;
  ESP_LOGI("media.wave", "Render worker uses %s stack",
           esp_ptr_external_ram(state->render_task_stack) ? "PSRAM" : "internal");
  return true;
}

static bool lock_render_state(WavyArcState *state) {
  static int64_t busy_since_us = 0;
  static int64_t last_warning_us = 0;
  if (state->render_mutex == nullptr || xSemaphoreTake(state->render_mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
    busy_since_us = 0;
    return true;
  }

  // This function is called from ESPHome's loop task. A visual frame is
  // disposable, whereas blocking the loop behind the raster worker trips the
  // task watchdog and restarts the whole device. Report a sustained stall and
  // let the next 16 ms service tick coalesce/retry the update.
  const int64_t now_us = esp_timer_get_time();
  if (busy_since_us == 0)
    busy_since_us = now_us;
  if (now_us - busy_since_us >= 250000LL && now_us - last_warning_us >= 1000000LL) {
    last_warning_us = now_us;
    ESP_LOGW("media.wave", "render-state lock busy; dropping/coalescing UI frame (worker_state=%d)",
             state->render_task == nullptr ? -1 : static_cast<int>(eTaskGetState(state->render_task)));
  }
  return false;
}

static void unlock_render_state(WavyArcState *state) {
  if (state->render_mutex != nullptr)
    xSemaphoreGive(state->render_mutex);
}
#endif

}  // namespace

inline void media_wavy_arc_init(lv_obj_t *image_obj) {
  if (image_obj == nullptr) {
    return;
  }

  auto &state = media_wavy_arc;
  const bool needs_setup = !state.registered || state.arc != image_obj;
  const bool had_buffers = state.buffers_ready;
  if (needs_setup) {
    state.arc = image_obj;
    state.registered = true;
    state.dirty = true;
    lv_obj_remove_flag(image_obj, LV_OBJ_FLAG_CLICKABLE);
  }

  if (!ensure_buffers(&state) || (!needs_setup && had_buffers)) {
    return;
  }
  lv_image_set_src(image_obj, &state.image);
  lv_obj_set_size(image_obj, WAVE_SIZE, WAVE_SIZE);
  render_bitmap(&state);
  // The YAML placeholder is hidden so it can never flash as a square while
  // the app snapshot is being opened. Reveal only the initialized ARGB image.
  lv_obj_clear_flag(image_obj, LV_OBJ_FLAG_HIDDEN);
  lv_obj_invalidate(image_obj);
}

inline void media_wavy_arc_set_background(const lv_image_dsc_t *source, bool apply_scrim) {
  auto &state = media_wavy_arc;
#ifdef ESP_PLATFORM
  // Artwork replacement is not a disposable animation update. Dropping it
  // after the normal 2 ms try-lock leaves the previous cover permanently
  // embedded below the wave. The raster worker never waits for the LVGL task,
  // so taking this mutex to completion is a bounded ownership handoff.
  if (state.render_mutex != nullptr && xSemaphoreTake(state.render_mutex, portMAX_DELAY) != pdTRUE)
    return;
#endif
  const uint32_t background_generation = ++state.background_request_generation;
  auto *next_backdrop = find_free_backdrop(&state);
  const bool captured = capture_backdrop(&state, next_backdrop, source, apply_scrim);
  state.background_pending = false;
  if (captured) {
    // A queued PPA operation owns its source pointer until its callback. The
    // three RGB565 crops let a new cover become active immediately without
    // touching either that in-flight source or the current active crop.
    state.backdrop_pixels = next_backdrop;
    state.background_ready_generation = background_generation;
    state.dirty = true;
  }
#ifdef ESP_PLATFORM
  if (captured)
    state.render_generation++;
  const bool use_worker = state.render_task != nullptr;
  unlock_render_state(&state);
  if (captured && use_worker && state.direct_present_enabled.load(std::memory_order_relaxed))
    notify_render_worker(&state);
  else if (captured && !use_worker && state.arc != nullptr)
    invalidate_bitmap(&state);
#else
  if (captured && state.arc != nullptr)
    invalidate_bitmap(&state);
#endif
  if (!captured)
    ESP_LOGW("media.wave", "unable to capture backdrop generation %u", static_cast<unsigned>(background_generation));
}

inline void media_wavy_arc_log_state(const char *reason) {
#ifdef ESP_PLATFORM
  auto &state = media_wavy_arc;
  ESP_LOGW("media.wave",
           "%s registered=%s buffers=%s worker=%p direct=%s dirty=%s ready=%s bg_pending=%s "
           "generation=%u/%u phase=%d loader_phase=%d value=%d playing=%s pending=%s pressed=%s in_flight=%s "
           "stale_drops=%u queued=%d/%d/%d/%d",
           reason == nullptr ? "state" : reason, YESNO(state.registered), YESNO(state.buffers_ready), state.render_task,
           YESNO(state.direct_present_enabled.load(std::memory_order_relaxed)), YESNO(state.dirty),
           YESNO(state.frame_ready), YESNO(state.background_pending), static_cast<unsigned>(state.ready_generation),
           static_cast<unsigned>(state.render_generation), state.phase_deg, state.loader_phase_deg,
           state.value_basis_points, YESNO(state.playing), YESNO(state.pending), YESNO(state.pressed),
           YESNO(present_in_flight_count(&state) != 0),
           static_cast<unsigned>(state.stale_ready_drop_count),
           state.queued_value_basis_points.load(std::memory_order_relaxed),
           state.queued_playing.load(std::memory_order_relaxed), state.queued_pending.load(std::memory_order_relaxed),
           state.queued_pressed.load(std::memory_order_relaxed));
#else
  (void) reason;
#endif
}

inline void media_wavy_arc_reset_trace() {
#ifdef ESP_PLATFORM
  auto &state = media_wavy_arc;
  state.trace_enabled.store(false, std::memory_order_release);
  state.trace_position.store(0, std::memory_order_relaxed);
  memset(state.trace_events, 0, sizeof(state.trace_events));
  state.trace_enabled.store(true, std::memory_order_release);
#endif
}

inline void media_wavy_arc_log_trace(const char *reason) {
#ifdef ESP_PLATFORM
  auto &state = media_wavy_arc;
  state.trace_enabled.store(false, std::memory_order_release);
  const uint32_t count = std::min<uint32_t>(state.trace_position.load(std::memory_order_acquire), WAVE_TRACE_CAPACITY);
  const uint32_t started_us = count == 0 ? 0 : state.trace_events[0].timestamp_us;
  ESP_LOGW("media.wave.trace", "%s events=%u", reason == nullptr ? "trace" : reason,
           static_cast<unsigned>(count));
  for (uint32_t index = 0; index < count; index++) {
    const auto &entry = state.trace_events[index];
    if (entry.timestamp_us == 0)
      continue;
    ESP_LOGW("media.wave.trace", "%03u +%06uus %c b=%u gen=%u/%u phase=%d/%d flags=%02x extra=%u",
             static_cast<unsigned>(index), static_cast<unsigned>(entry.timestamp_us - started_us),
             static_cast<char>(entry.event), static_cast<unsigned>(entry.buffer_index),
             static_cast<unsigned>(entry.ready_generation), static_cast<unsigned>(entry.generation), entry.phase,
             entry.loader_phase, static_cast<unsigned>(entry.flags), static_cast<unsigned>(entry.extra));
  }
#else
  (void) reason;
#endif
}

inline void media_wavy_arc_set_value_basis_points(int progress) {
  auto &state = media_wavy_arc;
  if (progress < 0) {
    progress = 0;
  } else if (progress > 10000) {
    progress = 10000;
  }
#ifdef ESP_PLATFORM
  if (state.render_task != nullptr) {
    state.queued_value_basis_points.store(progress, std::memory_order_release);
    if (state.direct_present_enabled.load(std::memory_order_relaxed))
      notify_render_worker(&state);
    return;
  }
  if (!lock_render_state(&state))
    return;
#endif
  if (state.value_basis_points == progress) {
#ifdef ESP_PLATFORM
    unlock_render_state(&state);
#endif
    return;
  }
  state.value_basis_points = progress;
  state.dirty = true;
#ifdef ESP_PLATFORM
  state.render_generation++;
  const bool use_worker = state.render_task != nullptr;
  unlock_render_state(&state);
  if (use_worker) {
    if (state.direct_present_enabled.load(std::memory_order_relaxed))
      notify_render_worker(&state);
  } else if (state.arc != nullptr && state.direct_present_enabled.load(std::memory_order_relaxed)) {
    invalidate_bitmap(&state);
  }
#else
  if (state.arc != nullptr)
    invalidate_bitmap(&state);
#endif
}

inline int media_wavy_arc_get_value_basis_points() {
  auto &state = media_wavy_arc;
#ifdef ESP_PLATFORM
  if (!lock_render_state(&state))
    return -1;
#endif
  const int value = state.last_rendered_value >= 0 ? state.last_rendered_value : state.value_basis_points;
#ifdef ESP_PLATFORM
  unlock_render_state(&state);
#endif
  return value;
}

inline void media_wavy_arc_set_value_permille(int progress) { media_wavy_arc_set_value_basis_points(progress * 10); }

inline void media_wavy_arc_set_value(int progress) { media_wavy_arc_set_value_basis_points(progress * 100); }

inline void media_wavy_arc_set_playing(bool playing) {
  auto &state = media_wavy_arc;
#ifdef ESP_PLATFORM
  if (state.render_task != nullptr) {
    state.queued_playing.store(playing ? 1 : 0, std::memory_order_release);
    if (state.direct_present_enabled.load(std::memory_order_relaxed))
      notify_render_worker(&state);
    return;
  }
  if (!lock_render_state(&state))
    return;
#endif
  if (state.playing == playing) {
#ifdef ESP_PLATFORM
    unlock_render_state(&state);
#endif
    return;
  }
  state.playing = playing;
  state.dirty = true;
#ifdef ESP_PLATFORM
  state.render_generation++;
  const bool use_worker = state.render_task != nullptr;
  unlock_render_state(&state);
  if (use_worker) {
    if (state.direct_present_enabled.load(std::memory_order_relaxed))
      notify_render_worker(&state);
  } else if (state.arc != nullptr && state.direct_present_enabled.load(std::memory_order_relaxed)) {
    invalidate_bitmap(&state);
  }
#else
  if (state.arc != nullptr)
    invalidate_bitmap(&state);
#endif
}

inline void media_wavy_arc_set_playback_state(bool playing, bool pending) {
  auto &state = media_wavy_arc;
#ifdef ESP_PLATFORM
  const int packed = (playing ? 0x01 : 0x00) | (pending ? 0x02 : 0x00);
  record_wave_trace(&state, 'T', nullptr, static_cast<uint8_t>(packed));
  if (state.render_task != nullptr) {
    state.queued_playback_state.store(packed, std::memory_order_release);
    if (state.direct_present_enabled.load(std::memory_order_relaxed))
      notify_render_worker(&state);
    return;
  }
  if (!lock_render_state(&state))
    return;
#endif
  if (state.playing == playing && state.pending == pending) {
#ifdef ESP_PLATFORM
    unlock_render_state(&state);
#endif
    return;
  }
  state.playing = playing;
  state.pending = pending;
  state.dirty = true;
#ifdef ESP_PLATFORM
  state.render_generation++;
  const bool use_worker = state.render_task != nullptr;
  unlock_render_state(&state);
  if (use_worker) {
    if (state.direct_present_enabled.load(std::memory_order_relaxed))
      notify_render_worker(&state);
  } else if (state.arc != nullptr && state.direct_present_enabled.load(std::memory_order_relaxed)) {
    invalidate_bitmap(&state);
  }
#else
  if (state.arc != nullptr)
    invalidate_bitmap(&state);
#endif
}

inline void media_wavy_arc_set_direct_present(bool enabled) {
  auto &state = media_wavy_arc;
  if (state.direct_present_enabled.load(std::memory_order_relaxed) == enabled)
    return;
#ifdef ESP_PLATFORM
  // Create the worker only when the live player becomes visible. Keeping task
  // creation out of LVGL/page initialization avoids competing with boot-time
  // snapshot generation and uses only statically allocated FreeRTOS storage.
  if (enabled)
    ensure_render_worker(&state);
  lv_area_t area{};
  if (enabled && state.arc != nullptr)
    lv_obj_get_coords(state.arc, &area);
  bool locked = false;
  if (!enabled && state.render_mutex != nullptr) {
    // Disabling is a synchronization boundary used before an artwork buffer
    // is overwritten. Unlike a disposable animation update, it must not be
    // dropped merely because the raster worker owns the mutex for a frame.
    locked = xSemaphoreTake(state.render_mutex, pdMS_TO_TICKS(150)) == pdTRUE;
  } else {
    locked = lock_render_state(&state);
  }
  if (!locked) {
    if (!enabled)
      ESP_LOGW("media.wave", "timed out while quiescing renderer");
    return;
  }
#endif
  if (state.direct_present_enabled.load(std::memory_order_relaxed) == enabled) {
#ifdef ESP_PLATFORM
    unlock_render_state(&state);
#endif
    return;
  }
  state.direct_present_enabled = enabled;
  if (enabled && state.arc != nullptr)
    state.dirty = true;
#ifdef ESP_PLATFORM
  if (!enabled) {
    state.frame_ready = false;
    state.queued_phase_delta.store(0, std::memory_order_relaxed);
    // The pending backdrop is an owned RGB565 crop, not a borrowed artwork
    // pointer. Preserve it across page/overlay hand-offs so disabling direct
    // presentation cannot make an old cover look ready again.
  }
  if (enabled) {
    state.direct_x = area.x1;
    state.direct_y = area.y1;
  }
  // Disabling invalidates any frame already owned by the compositor. Enabling
  // must preserve a frame prepared while hidden; incrementing its generation
  // here made that new frame look stale and exposed the previous artwork first.
  if (!enabled)
    state.render_generation++;
  const bool use_worker = state.render_task != nullptr;
  const int released_x = state.direct_x;
  const int released_y = state.direct_y;
  unlock_render_state(&state);
  if (!enabled)
    lvgl_esphome_direct_blit_rgb888_release(released_x, released_y, WAVE_SIZE, WAVE_SIZE);
  if (enabled && use_worker)
    notify_render_worker(&state);
  else if (enabled && state.arc != nullptr)
    invalidate_bitmap(&state);
#else
  if (enabled && state.arc != nullptr)
    invalidate_bitmap(&state);
#endif
}

inline void media_wavy_arc_set_pressed(bool pressed) {
  auto &state = media_wavy_arc;
#ifdef ESP_PLATFORM
  record_wave_trace(&state, 'Q', nullptr, pressed ? 1 : 0);
  if (state.render_task != nullptr) {
    const uint32_t interaction_sequence = state.next_interaction_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    state.queued_interaction_sequence.store(interaction_sequence, std::memory_order_relaxed);
    state.queued_pressed.store(pressed ? 1 : 0, std::memory_order_release);
    if (state.direct_present_enabled.load(std::memory_order_relaxed)) {
      // A press is latency-sensitive while phase updates are disposable. Boost
      // only this raster frame so the previous bright state cannot linger
      // before the Material state layer darkens.
      vTaskPrioritySet(state.render_task, WAVE_CONTROL_TASK_PRIORITY);
      notify_render_worker(&state);
      // LVGL marks the transparent hit target dirty before it emits the input
      // event. Keep that native refresh behind the stable direct-region frame;
      // otherwise another member of the DSI triple-buffer pool can briefly
      // resurrect the previous pressed state.
      const TickType_t started = xTaskGetTickCount();
      // A stable interaction frame is acknowledged only after the DSI VSYNC
      // handoff and synchronization of the idle framebuffer pool. On the
      // 800x800 panel that boundary can legitimately take a little over 70 ms.
      // Do not let LVGL continue into CLICKED (and the transport command) on
      // the old 60-ms timeout, because playback-state rendering can then race
      // the still-pending release frame and make the state layer flash.
      const TickType_t timeout = pdMS_TO_TICKS(120);
      while (state.presented_interaction_sequence.load(std::memory_order_acquire) < interaction_sequence &&
             xTaskGetTickCount() - started < timeout) {
        vTaskDelay(1);
      }
      if (state.presented_interaction_sequence.load(std::memory_order_acquire) < interaction_sequence) {
        ESP_LOGW("media.wave", "interaction frame %u did not reach VSYNC before timeout",
                 static_cast<unsigned>(interaction_sequence));
      }
    }
    return;
  }
  if (!lock_render_state(&state))
    return;
#endif
  if (state.pressed == pressed) {
#ifdef ESP_PLATFORM
    unlock_render_state(&state);
#endif
    return;
  }
  state.pressed = pressed;
  state.dirty = true;
#ifdef ESP_PLATFORM
  state.render_generation++;
  const bool use_worker = state.render_task != nullptr;
  unlock_render_state(&state);
  if (use_worker) {
    if (state.direct_present_enabled.load(std::memory_order_relaxed))
      notify_render_worker(&state);
  } else if (state.arc != nullptr && state.direct_present_enabled.load(std::memory_order_relaxed)) {
    invalidate_bitmap(&state);
  }
#else
  if (state.arc != nullptr)
    invalidate_bitmap(&state);
#endif
}

inline bool media_wavy_arc_background_ready() {
#ifdef ESP_PLATFORM
  auto &state = media_wavy_arc;
  if (state.render_task == nullptr)
    return !state.background_pending && state.background_ready_generation == state.background_request_generation;
  if (state.render_mutex == nullptr || xSemaphoreTake(state.render_mutex, 0) != pdTRUE)
    return false;
  // Holding render_mutex guarantees that an in-progress backdrop rebuild has
  // completed. The rendered frame may already have been handed to the direct
  // presenter, so frame_ready is not a valid backdrop-readiness condition.
  const bool ready =
      !state.background_pending && state.background_ready_generation == state.background_request_generation;
  unlock_render_state(&state);
  return ready;
#else
  return true;
#endif
}

inline bool media_wavy_arc_present_ready(bool worker_context) {
#ifdef ESP_PLATFORM
  auto &state = media_wavy_arc;
  if (state.render_task == nullptr)
    return false;

  auto control_update_pending = [&state]() {
    return state.queued_value_basis_points.load(std::memory_order_acquire) >= 0 ||
           state.queued_playback_state.load(std::memory_order_acquire) >= 0 ||
           state.queued_playing.load(std::memory_order_acquire) >= 0 ||
           state.queued_pending.load(std::memory_order_acquire) >= 0 ||
           state.queued_pressed.load(std::memory_order_acquire) >= 0;
  };

  if (!lock_render_state(&state))
    return present_in_flight_count(&state) != 0;

  auto buffer_is_presented = [&state](const lv_color32_t *buffer) {
    if (buffer == nullptr)
      return false;
    for (const auto &slot : state.present_slots) {
      if (slot.in_flight.load(std::memory_order_acquire) && slot.pixels == buffer)
        return true;
    }
    return false;
  };
  auto release_render_buffer = [&state, &buffer_is_presented](lv_color32_t *buffer) {
    if (buffer == nullptr || buffer == state.pixels || buffer_is_presented(buffer))
      return;
    if (state.worker_pixels == nullptr) {
      state.worker_pixels = buffer;
    } else if (state.worker_pixels != buffer && state.spare_pixels == nullptr) {
      state.spare_pixels = buffer;
    }
  };

  bool retired_any = false;
  bool adopted_current = false;
  while (true) {
    WavePresentSlot *completed = nullptr;
    for (auto &slot : state.present_slots) {
      if (!slot.in_flight.load(std::memory_order_acquire) ||
          !slot.complete.load(std::memory_order_acquire))
        continue;
      if (completed == nullptr || slot.sequence < completed->sequence)
        completed = &slot;
    }
    if (completed == nullptr)
      break;

    if (completed->started_us != 0 && lvgl_esphome_get_perf_logging_enabled() != 0) {
      const uint32_t dma_us = static_cast<uint32_t>(esp_timer_get_time() - completed->started_us);
      state.perf_dma_count++;
      state.perf_dma_total_us += dma_us;
      if (dma_us > state.perf_dma_max_us)
        state.perf_dma_max_us = dma_us;
    }

    bool newer_submission = false;
    for (const auto &slot : state.present_slots) {
      newer_submission |= slot.in_flight.load(std::memory_order_acquire) &&
                          slot.sequence > completed->sequence;
    }
    lv_color32_t *completed_pixels = completed->pixels;
    const uint32_t completed_generation = completed->generation;
    const uint32_t completed_sequence = completed->sequence;
    completed->pixels = nullptr;
    completed->backdrop_pixels = nullptr;
    completed->generation = 0;
    completed->sequence = 0;
    completed->interaction_sequence = 0;
    completed->stable = false;
    completed->started_us = 0;
    completed->complete.store(false, std::memory_order_release);
    completed->in_flight.store(false, std::memory_order_release);
    completed->owner = nullptr;

    if (completed_pixels != nullptr && !newer_submission &&
        completed_generation == state.render_generation && state.direct_present_enabled &&
        !control_update_pending()) {
      lv_color32_t *old_front = state.pixels;
      state.pixels = completed_pixels;
      state.image.data = reinterpret_cast<const uint8_t *>(state.pixels);
      release_render_buffer(old_front);
      adopted_current = true;
    } else {
      release_render_buffer(completed_pixels);
    }
    record_wave_trace(&state, 'F', completed_pixels, static_cast<uint8_t>(completed_sequence & 0xFFU));
    retired_any = true;
  }

  bool current_frame_owned = adopted_current ||
                             (state.frame_ready && state.ready_generation == state.render_generation);
  for (const auto &slot : state.present_slots) {
    current_frame_owned |= slot.in_flight.load(std::memory_order_acquire) &&
                           slot.generation == state.render_generation;
  }
  if (retired_any && !current_frame_owned && !control_update_pending())
    state.dirty = true;

  bool request_another = state.dirty || state.background_pending ||
                         state.queued_phase_delta.load(std::memory_order_relaxed) != 0 ||
                         state.queued_value_basis_points.load(std::memory_order_relaxed) >= 0 ||
                         state.queued_playback_state.load(std::memory_order_relaxed) >= 0 ||
                         state.queued_playing.load(std::memory_order_relaxed) >= 0 ||
                         state.queued_pending.load(std::memory_order_relaxed) >= 0 ||
                         state.queued_pressed.load(std::memory_order_relaxed) >= 0;

  // Disabling direct presentation is also used as the artwork handoff
  // boundary.  A PPA request submitted immediately before that boundary can
  // finish while presentation is disabled.  Its completion still has to be
  // retired above so the rendered buffer returns to the spare pool and the
  // worker can rebuild the backdrop.  Returning before servicing the
  // completion left present_in_flight asserted forever, timed out the
  // handoff, and later exposed the stale wave/background frame.
  if (!state.direct_present_enabled.load(std::memory_order_relaxed)) {
    const bool wake_worker = request_another && state.worker_pixels != nullptr;
    unlock_render_state(&state);
    if (wake_worker)
      notify_render_worker(&state);
    return false;
  }

  lv_color32_t *rendered = nullptr;
  lv_color16_t *present_backdrop = nullptr;
  uint32_t ready_generation = 0;
  bool present_stable = false;
  if (state.frame_ready) {
    if (state.ready_generation == state.render_generation && !control_update_pending()) {
      rendered = state.worker_pixels;
      present_backdrop = state.backdrop_pixels;
      ready_generation = state.ready_generation;
      // Touch feedback freezes the wave geometry just like a stopped player.
      // Cache that exact overlay so a simultaneous native LVGL refresh cannot
      // rebase the region from an older rotating phase while the finger is
      // still down.
      present_stable = state.pressed || ready_generation == state.interaction_stable_generation ||
                       (!state.playing && !state.pending);
    } else {
      // set_playing()/set_pressed() enqueue without blocking the LVGL task.
      // Do not submit a frame completed just before that state change; once it
      // reaches the DSI queue, a generation check can no longer hide it.
      state.frame_ready = false;
      state.dirty = true;
      state.stale_ready_drop_count++;
      request_another = true;
    }
  }

  WavePresentSlot *present_slot = nullptr;
  if (rendered != nullptr) {
    for (auto &slot : state.present_slots) {
      if (!slot.in_flight.load(std::memory_order_acquire)) {
        present_slot = &slot;
        break;
      }
    }
  }
  if (rendered == nullptr || present_slot == nullptr) {
    const bool active = present_in_flight_count(&state) != 0;
    const bool wake_worker = request_another && state.worker_pixels != nullptr;
    unlock_render_state(&state);
    if (wake_worker)
      notify_render_worker(&state);
    return active;
  }

  lv_color32_t *replacement = state.spare_pixels;
  state.worker_pixels = replacement;
  state.spare_pixels = nullptr;
  state.frame_ready = false;
  present_slot->owner = &state;
  present_slot->pixels = rendered;
  present_slot->backdrop_pixels = present_backdrop;
  present_slot->generation = ready_generation;
  present_slot->sequence = state.next_present_sequence++;
  present_slot->interaction_sequence =
      ready_generation == state.interaction_stable_generation ? state.interaction_stable_sequence : 0;
  present_slot->stable = present_stable;
  present_slot->started_us = 0;
  present_slot->complete.store(false, std::memory_order_release);
  present_slot->in_flight.store(true, std::memory_order_release);
  record_wave_trace(&state, 'S', rendered, static_cast<uint8_t>(present_slot->sequence & 0xFFU));
  const bool perf_enabled = lvgl_esphome_get_perf_logging_enabled() != 0;
  const int64_t present_start_us = perf_enabled ? esp_timer_get_time() : 0;
  const uint8_t async_result =
      present_stable
          ? lvgl_esphome_direct_blend_rgb565_argb8888_stable_async(
                reinterpret_cast<const uint8_t *>(present_backdrop), WAVE_SIZE * static_cast<int>(sizeof(lv_color16_t)),
                reinterpret_cast<const uint8_t *>(rendered), WAVE_SIZE * static_cast<int>(sizeof(lv_color32_t)),
                WAVE_SIZE, WAVE_SIZE, 0, 0, state.direct_x, state.direct_y, WAVE_SIZE, WAVE_SIZE, direct_present_ready,
                present_slot)
          : lvgl_esphome_direct_blend_rgb565_argb8888_async(
                reinterpret_cast<const uint8_t *>(present_backdrop), WAVE_SIZE * static_cast<int>(sizeof(lv_color16_t)),
                reinterpret_cast<const uint8_t *>(rendered), WAVE_SIZE * static_cast<int>(sizeof(lv_color32_t)),
                WAVE_SIZE, WAVE_SIZE, 0, 0, state.direct_x, state.direct_y, WAVE_SIZE, WAVE_SIZE, direct_present_ready,
                present_slot);
  record_wave_trace(&state, 'A', rendered, async_result);
  if (perf_enabled) {
    const uint32_t present_us = static_cast<uint32_t>(esp_timer_get_time() - present_start_us);
    state.perf_present_count++;
    state.perf_present_total_us += present_us;
    if (present_us > state.perf_present_max_us)
      state.perf_present_max_us = present_us;
  }

  if (async_result == 2) {
    present_slot->started_us = esp_timer_get_time();
    const bool wake_worker = state.worker_pixels != nullptr;
    unlock_render_state(&state);
    if (wake_worker)
      notify_render_worker(&state);
    return true;
  }

  present_slot->pixels = nullptr;
  present_slot->backdrop_pixels = nullptr;
  present_slot->generation = 0;
  present_slot->sequence = 0;
  present_slot->interaction_sequence = 0;
  present_slot->stable = false;
  present_slot->started_us = 0;
  present_slot->complete.store(false, std::memory_order_release);
  present_slot->in_flight.store(false, std::memory_order_release);
  present_slot->owner = nullptr;
  state.worker_pixels = rendered;
  state.spare_pixels = replacement;
  state.frame_ready = true;
  if (async_result == 1) {
    unlock_render_state(&state);
    return true;
  }

  // A rejected direct request can fall back to native LVGL only from the UI
  // task. Keep the completed frame ready for that path instead of touching an
  // LVGL object from the raster worker.
  if (worker_context) {
    unlock_render_state(&state);
    return false;
  }

  lv_color32_t *old_front = state.pixels;
  state.pixels = rendered;
  state.image.data = reinterpret_cast<const uint8_t *>(state.pixels);
  state.worker_pixels = old_front;
  state.spare_pixels = replacement;
  state.frame_ready = false;
  const bool wake_worker = request_another && state.worker_pixels != nullptr;
  unlock_render_state(&state);

  if (state.arc != nullptr)
    lv_obj_invalidate(state.arc);
  if (wake_worker)
    notify_render_worker(&state);
  return false;
#else
  return false;
#endif
}

inline void media_wavy_arc_set_pending(bool pending) {
  auto &state = media_wavy_arc;
#ifdef ESP_PLATFORM
  if (state.render_task != nullptr) {
    state.queued_pending.store(pending ? 1 : 0, std::memory_order_release);
    if (state.direct_present_enabled.load(std::memory_order_relaxed))
      notify_render_worker(&state);
    return;
  }
  if (!lock_render_state(&state))
    return;
#endif
  if (state.pending == pending) {
#ifdef ESP_PLATFORM
    unlock_render_state(&state);
#endif
    return;
  }
  state.pending = pending;
  state.dirty = true;
#ifdef ESP_PLATFORM
  state.render_generation++;
  const bool use_worker = state.render_task != nullptr;
  unlock_render_state(&state);
  if (use_worker) {
    if (state.direct_present_enabled.load(std::memory_order_relaxed))
      notify_render_worker(&state);
  } else if (state.arc != nullptr && state.direct_present_enabled.load(std::memory_order_relaxed)) {
    invalidate_bitmap(&state);
  }
#else
  if (state.arc != nullptr)
    invalidate_bitmap(&state);
#endif
}

inline void media_wavy_arc_tick(float degrees) {
  auto &state = media_wavy_arc;
  if (state.arc == nullptr) {
    return;
  }
  int step = static_cast<int>(degrees >= 0.0f ? degrees + 0.5f : degrees - 0.5f);
  if (step == 0) {
    step = degrees >= 0.0f ? 1 : -1;
  }
#ifdef ESP_PLATFORM
  if (state.render_task != nullptr) {
    if (!state.direct_present_enabled.load(std::memory_order_relaxed))
      return;
    state.queued_phase_delta.fetch_add(step, std::memory_order_relaxed);
    notify_render_worker(&state);
    return;
  }
#endif
  if ((!state.playing || state.pressed) && !state.pending)
    return;
  // Match the worker path: pending transport feedback animates only the
  // loader, never the base wave geometry.
  if (state.playing && !state.pending && !state.pressed) {
    state.phase_deg += step;
    while (state.phase_deg >= 360) {
      state.phase_deg -= 360;
    }
    while (state.phase_deg < 0) {
      state.phase_deg += 360;
    }
  }
  if (state.pending) {
    state.loader_phase_deg += step * 5;
    while (state.loader_phase_deg >= 360) {
      state.loader_phase_deg -= 360;
    }
    while (state.loader_phase_deg < 0) {
      state.loader_phase_deg += 360;
    }
  }
  state.dirty = true;
  invalidate_bitmap(&state);
}

}  // namespace wavy_progress_internal

static const char *const TAG = "lvgl_material.wavy_progress";

void MaterialWavyProgress::setup() {
  if (this->widget_ == nullptr) {
    this->mark_failed();
    ESP_LOGE(TAG, "No LVGL widget configured");
  }
}

void MaterialWavyProgress::on_shutdown() { wavy_progress_internal::media_wavy_arc_set_direct_present(false); }

void MaterialWavyProgress::dump_config() {
  ESP_LOGCONFIG(TAG, "Material wavy progress:");
  ESP_LOGCONFIG(TAG, "  Widget: %s", this->widget_ == nullptr ? "missing" : "configured");
}

void MaterialWavyProgress::initialize() { wavy_progress_internal::media_wavy_arc_init(this->widget_); }

void MaterialWavyProgress::set_background(const lv_image_dsc_t *source, bool apply_scrim) {
  wavy_progress_internal::media_wavy_arc_set_background(source, apply_scrim);
}

void MaterialWavyProgress::log_state(const char *reason) { wavy_progress_internal::media_wavy_arc_log_state(reason); }

void MaterialWavyProgress::reset_trace() { wavy_progress_internal::media_wavy_arc_reset_trace(); }

void MaterialWavyProgress::log_trace(const char *reason) { wavy_progress_internal::media_wavy_arc_log_trace(reason); }

int MaterialWavyProgress::get_value_basis_points() {
  return wavy_progress_internal::media_wavy_arc_get_value_basis_points();
}

void MaterialWavyProgress::set_value_basis_points(int progress) {
  wavy_progress_internal::media_wavy_arc_set_value_basis_points(progress);
}

void MaterialWavyProgress::set_value_permille(int progress) {
  wavy_progress_internal::media_wavy_arc_set_value_permille(progress);
}

void MaterialWavyProgress::set_value(int progress) { wavy_progress_internal::media_wavy_arc_set_value(progress); }

void MaterialWavyProgress::set_playing(bool playing) { wavy_progress_internal::media_wavy_arc_set_playing(playing); }

void MaterialWavyProgress::set_playback_state(bool playing, bool pending) {
  wavy_progress_internal::media_wavy_arc_set_playback_state(playing, pending);
}

void MaterialWavyProgress::set_pressed(bool pressed) { wavy_progress_internal::media_wavy_arc_set_pressed(pressed); }

void MaterialWavyProgress::set_direct_present(bool enabled) {
  wavy_progress_internal::media_wavy_arc_set_direct_present(enabled);
}

bool MaterialWavyProgress::is_background_ready() { return wavy_progress_internal::media_wavy_arc_background_ready(); }

bool MaterialWavyProgress::service_present() { return wavy_progress_internal::media_wavy_arc_present_ready(); }

void MaterialWavyProgress::set_pending(bool pending) { wavy_progress_internal::media_wavy_arc_set_pending(pending); }

void MaterialWavyProgress::tick(float degrees) { wavy_progress_internal::media_wavy_arc_tick(degrees); }

}  // namespace esphome::lvgl_material
