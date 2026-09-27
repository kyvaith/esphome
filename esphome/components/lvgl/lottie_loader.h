#pragma once

#include "esphome/core/defines.h"

#ifdef USE_ESP32

// Only compile when Lottie widget is actually used (LV_USE_LOTTIE=1 in lv_conf.h).
// This header is pulled in via esphome.h for all builds, so we must guard it.
#include <lvgl.h>
#if LV_USE_LOTTIE

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_cache.h"
#include "esp_memory_utils.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "esphome/components/esp32/task_utils.h"
#include "esphome/core/main_task.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <cmath>
#include <cstring>

// Access lv_lottie_t internals for safe re-initialisation on screen re-load.
// Needed to null out the dangling anim pointer and to clear the ThorVG canvas
// before re-pushing the paint.
#include <src/widgets/lottie/lv_lottie_private.h>
#include "ppa/lvgl_ppa_accel_v9.h"

extern "C" uint32_t lvgl_esphome_get_perf_logging_enabled(void);
extern "C" bool lvgl_esphome_direct_blit_xrgb8888(const uint8_t *src, int src_stride, int x, int y, int width,
                                                  int height);
extern "C" bool lvgl_esphome_direct_blit_xrgb8888_coherent(const uint8_t *src, int src_stride, int x, int y, int width,
                                                           int height);
extern "C" uint8_t lvgl_esphome_direct_blit_xrgb8888_async(const uint8_t *src, int src_stride, int x, int y, int width,
                                                           int height, void (*ready_callback)(void *), void *ready_arg);
extern "C" uint8_t lvgl_esphome_direct_blend_argb8888_async(
    const uint8_t *background, int background_stride, const uint8_t *foreground, int foreground_stride,
    int foreground_width, int foreground_height, int foreground_x, int foreground_y, int x, int y, int width,
    int height, void (*ready_callback)(void *), void *ready_arg);
extern "C" uint8_t lvgl_esphome_direct_scale_blend_argb8888_async(
    const uint8_t *foreground, int foreground_stride, int foreground_width, int foreground_height, int foreground_x,
    int foreground_y, int foreground_crop_width, int foreground_crop_height, int integer_scale, int x, int y,
    uint8_t *scale_target, size_t scale_target_size, int scale_target_width, int scale_target_height,
    const uint8_t *background, int background_stride, void (*ready_callback)(void *), void *ready_arg);
extern "C" bool lvgl_esphome_render_background_rgb888(lv_obj_t *exclude, uint8_t *dst, int dst_stride,
                                                        int x, int y, int width, int height);
extern "C" void lvgl_esphome_direct_blit_rgb888_release(int x, int y, int width, int height);
extern "C" bool lvgl_esphome_direct_handoff_to_lvgl(uint32_t timeout_ms);
extern "C" bool lvgl_esphome_copy_argb8888(const uint8_t *src, uint8_t *dst, int width, int height);
extern "C" bool lvgl_esphome_scale_argb8888(const uint8_t *src, int src_width, int src_height, uint8_t *dst,
                                             int dst_width, int dst_height);
extern "C" bool lvgl_esphome_scale_argb8888_dma_chain(const uint8_t *src, int src_width, int src_height,
                                                        uint8_t *dst, int dst_width, int dst_height);
extern "C" void lvgl_esphome_release_dma_image_buffer(const void *buffer);
extern "C" bool lvgl_esphome_copy_xrgb8888_to_rgb888(const uint8_t *src, uint8_t *dst, int width, int height);
extern "C" bool lvgl_esphome_direct_regions_pause(bool paused, uint32_t timeout_ms);
extern "C" bool lvgl_esphome_snapshot_is_active(void);
extern "C" bool esphome_mipi_dsi_wait_fifo_margin(uint32_t min_depth, uint32_t timeout_us) __attribute__((weak));

namespace esphome {
namespace lvgl {

static constexpr size_t LOTTIE_TASK_STACK_SIZE = 64 * 1024;
static constexpr size_t LOTTIE_CACHE_ALIGN = 128;
static constexpr size_t LOTTIE_INTERNAL_BUFFER_MAX_BYTES = 256 * 1024;
static constexpr size_t LOTTIE_INTERNAL_BUFFER_HEADROOM_BYTES = 48 * 1024;
// A 484x484 weather canvas is about 0.9 MiB in ARGB8888. Allowing its second
// buffer keeps ThorVG off the LVGL lock and prevents a partially rasterized
// frame from being exposed. Other large scenes remain bounded by this limit.
static constexpr size_t LOTTIE_DOUBLE_BUFFER_MAX_BYTES = 1024 * 1024;
static constexpr size_t LOTTIE_FRAME_CACHE_MAX_BYTES = 12 * 1024 * 1024;
static constexpr uint32_t LOTTIE_FRAME_CACHE_TARGET_FPS = 60;
// Prepare the next compact weather frame before its VSYNC slot. The PPA
// compositor paces presentation, while this lead overlaps CPU rasterization
// with the previous frame's scale/blend transaction.
static constexpr int64_t LOTTIE_DIRECT_PIPELINE_LEAD_US = 6000;
static const char *const LOTTIE_PERF_TAG = "lottie.perf";
static constexpr uint8_t LOTTIE_DIRECT_BLIT_REJECTED = 0;
static constexpr uint8_t LOTTIE_DIRECT_BLIT_BUSY = 1;
static constexpr uint8_t LOTTIE_DIRECT_BLIT_SUBMITTED = 2;

struct LottieContext;
inline void lottie_direct_frame_ready(void *arg);
inline bool lottie_publish_direct_scaled_frame(LottieContext *ctx);

#ifndef CONFIG_ESPHOME_LOTTIE_DSI_BACKPRESSURE
#define CONFIG_ESPHOME_LOTTIE_DSI_BACKPRESSURE 1
#endif
#ifndef CONFIG_ESPHOME_LOTTIE_DSI_FIFO_MIN
#define CONFIG_ESPHOME_LOTTIE_DSI_FIFO_MIN 1000
#endif
#ifndef CONFIG_ESPHOME_LOTTIE_DSI_WAIT_US
#define CONFIG_ESPHOME_LOTTIE_DSI_WAIT_US 6000
#endif

// Persistent context for each Lottie widget – tracks all PSRAM allocations,
// the render task, and cached animation parameters for safe re-load.
struct LottieContext {
  // --- Config (set once, never freed) ---
  lv_obj_t *obj;
  const void *data;  // PROGMEM (embedded) or nullptr
  size_t data_size;
  const char *file_path;  // string literal or nullptr
  bool loop;
  uint32_t play_count;  // 0 means unlimited when loop is enabled
  volatile bool auto_start;
  bool retain_on_unload;
  uint32_t width;
  uint32_t height;
  uint32_t render_width;
  uint32_t render_height;
  bool fast_radial;
  lv_color_t fast_radial_fill_color;
  lv_color_t fast_radial_outline_color;
  // The radial renderer only changes this compact region. Keeping it in the
  // context lets the publisher invalidate the sun instead of the full canvas.
  int16_t fast_radial_dirty_x1;
  int16_t fast_radial_dirty_y1;
  int16_t fast_radial_dirty_x2;
  int16_t fast_radial_dirty_y2;
  // Each fast-radial source is ping-ponged while PPA reads the other one.
  // Remember the last geometry written to each physical buffer so the next
  // frame can erase only the old/new ray bounds instead of a large square.
  uint8_t *fast_radial_buffer_ptr[2];
  int32_t fast_radial_last_frame[2];
  bool flatten_to_opaque;
  lv_color_t opaque_background;

  // --- Animation params (captured on first load, reused on re-loads) ---
  lv_anim_exec_xcb_t exec_cb;
  void *anim_var;
  int32_t start_frame;
  int32_t end_frame;
  uint32_t duration_ms;
  bool data_loaded;  // true after first successful parse

  // --- Runtime state (freed on screen unload) ---
  uint8_t *pixel_buffer;         // buffer currently presented by LVGL
  uint8_t *work_buffer;          // stable ThorVG render target
  uint8_t *work_buffer_alt;      // spare fast-radial target while PPA reads work_buffer
  uint8_t *direct_background;    // immutable RGB888 scene without this animation
  size_t direct_background_size;
  uint8_t *display_back_buffer;  // complete display frame prepared while LVGL presents pixel_buffer
  uint8_t *thorvg_target;        // target bound to ThorVG for the loaded animation
  uint8_t *frame_cache;          // optional pre-rendered frames for tiny boot Lotties
  size_t frame_cache_stride;
  uint32_t frame_cache_count;
  bool pixel_buffer_internal;
  bool work_buffer_internal;
  bool work_buffer_alt_internal;
  bool display_back_buffer_internal;
  bool frame_cache_internal;
  bool prepared_frame_ready;
  // Snapshot capture is requested by the ESPHome loop task but executed by
  // this 64 KiB worker. Rendering the retained direct frame on loopTask can
  // overflow its small stack and reset the device during boot handoff.
  volatile bool snapshot_commit_requested;
  volatile bool snapshot_commit_completed;
  volatile bool snapshot_commit_success;
  volatile bool playback_completed;
  StackType_t *task_stack;  // PSRAM – 64 KB
  StaticTask_t *task_tcb;   // internal RAM
  TaskHandle_t task_handle;
  volatile bool stop_requested;
  volatile bool restart_requested;  // ✅ Flag to restart animation from frame 0
  volatile bool reveal_after_restart;
  volatile uint32_t restart_phase_ms;
  TickType_t start_tick;            // legacy task clock
  int64_t start_time_us;            // monotonic animation clock (can be reset)
  bool user_wants_hidden;           // Save user's 'hidden' config from YAML
  volatile bool runtime_hidden;     // Actual visibility at time of unload (captures script changes)
  bool hidden_before_unload;
  bool direct_frame_in_flight;
  // The compact source most recently handed to the asynchronous regional
  // compositor.  With fast-scaled weather the producer pointer is rotated
  // while PPA is busy, so work_buffer is not necessarily the final frame.
  uint8_t *direct_published_source;
  bool direct_region_active;
  int32_t direct_region_x;
  int32_t direct_region_y;
  int32_t direct_region_width;
  int32_t direct_region_height;
  int32_t retained_frame;
  uint32_t retained_coverage;
  uint32_t direct_publish_diag_last_ms;
  uint32_t direct_publish_diag_rejects;
  uint32_t direct_publish_diag_submits;
  uint32_t direct_publish_diag_callbacks;
  LottieContext *next_context;
  bool native_publish_pending;
  bool native_publish_success;
  bool native_publish_scaled;
  uint8_t *native_publish_buffer;
  uint64_t raster_stages_us[6];
  uint32_t raster_stage_samples;
};

inline LottieContext *lottie_contexts = nullptr;

inline bool lottie_direct_frame_in_flight(const LottieContext *ctx) {
  return ctx != nullptr && __atomic_load_n(&ctx->direct_frame_in_flight, __ATOMIC_ACQUIRE);
}

inline void lottie_set_direct_frame_in_flight(LottieContext *ctx, bool in_flight) {
  if (ctx != nullptr)
    __atomic_store_n(&ctx->direct_frame_in_flight, in_flight, __ATOMIC_RELEASE);
}

inline void lottie_direct_frame_ready(void *arg) {
  auto *ctx = static_cast<LottieContext *>(arg);
  if (ctx == nullptr)
    return;
  __atomic_add_fetch(&ctx->direct_publish_diag_callbacks, 1U, __ATOMIC_RELAXED);
  lottie_set_direct_frame_in_flight(ctx, false);
  TaskHandle_t task = ctx->task_handle;
  if (task != nullptr)
    xTaskNotifyGive(task);
}

inline size_t lottie_align_up(size_t value, size_t align) { return (value + align - 1) & ~(align - 1); }

inline void lottie_wait_display_fifo() {
#if CONFIG_ESPHOME_LOTTIE_DSI_BACKPRESSURE
  if (esphome_mipi_dsi_wait_fifo_margin != nullptr) {
    esphome_mipi_dsi_wait_fifo_margin(CONFIG_ESPHOME_LOTTIE_DSI_FIFO_MIN, CONFIG_ESPHOME_LOTTIE_DSI_WAIT_US);
  }
#endif
}

inline void lottie_sync_buffer(uint8_t *data, size_t len) {
  if (data == nullptr || len == 0)
    return;
  if (!esp_ptr_external_ram(data))
    return;

  uintptr_t start = reinterpret_cast<uintptr_t>(data) & ~(LOTTIE_CACHE_ALIGN - 1);
  uintptr_t end = lottie_align_up(reinterpret_cast<uintptr_t>(data) + len, LOTTIE_CACHE_ALIGN);
  if (end <= start)
    return;
  if (!esp_ptr_external_ram(reinterpret_cast<const void *>(start)) ||
      !esp_ptr_external_ram(reinterpret_cast<const void *>(end - 1))) {
    return;
  }

  esp_cache_msync(reinterpret_cast<void *>(start), end - start,
                  ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);
}

inline void lottie_sync_canvas_buffer(LottieContext *ctx) {
  if (ctx == nullptr || ctx->obj == nullptr)
    return;
  lv_draw_buf_t *draw_buf = lv_canvas_get_draw_buf(ctx->obj);
  if (draw_buf == nullptr || draw_buf->data == nullptr)
    return;
  size_t len = draw_buf->data_size;
  if (len == 0 && draw_buf->header.stride > 0 && draw_buf->header.h > 0) {
    len = static_cast<size_t>(draw_buf->header.stride) * draw_buf->header.h;
  }
  lottie_sync_buffer(static_cast<uint8_t *>(draw_buf->data), len);
}

// FreeRTOS ticks are millisecond-granular while the weather timeline is
// scheduled at display cadence (16.667 ms). Sleep for complete ticks, then
// close the sub-tick remainder with a short ROM delay. Do not call
// vTaskDelay(0): after the first tick that used to yield repeatedly for the
// remaining few milliseconds, handing the frame deadline to unrelated tasks
// and capping the animation near 52 FPS.
inline void lottie_wait_until_us(int64_t target_us) {
  while (true) {
    const int64_t remaining_us = target_us - esp_timer_get_time();
    if (remaining_us <= 0)
      return;
    if (remaining_us > 2000) {
      const uint32_t sleep_ms = static_cast<uint32_t>((remaining_us - 1000) / 1000);
      const TickType_t sleep_ticks = pdMS_TO_TICKS(sleep_ms);
      if (sleep_ticks > 0) {
        vTaskDelay(sleep_ticks);
        continue;
      }
    }
    esp_rom_delay_us(static_cast<uint32_t>(std::min<int64_t>(remaining_us, 1000)));
  }
}

inline lv_color_format_t lottie_argb_color_format(const LottieContext *ctx) {
  // ThorVG renders premultiplied ARGB. The compact fast-radial renderer is
  // ours, so emit straight ARGB instead: ESP32-P4 PPA can then alpha-blend the
  // canvas in hardware instead of sending every weather frame to LVGL's
  // software blender.
  return ctx != nullptr && ctx->fast_radial ? LV_COLOR_FORMAT_ARGB8888
                                            : LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED;
}

inline size_t lottie_buffer_bytes(const LottieContext *ctx) {
  const uint32_t stride = lv_draw_buf_width_to_stride(ctx->render_width, lottie_argb_color_format(ctx));
  return static_cast<size_t>(stride) * ctx->render_height;
}

inline bool lottie_uses_scaled_render(const LottieContext *ctx) {
  return ctx != nullptr && (ctx->render_width != ctx->width || ctx->render_height != ctx->height);
}

inline bool lottie_uses_direct_xrgb(const LottieContext *ctx) {
#if defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  return ctx != nullptr && ctx->flatten_to_opaque;
#else
  return false;
#endif
}

inline bool lottie_uses_direct_scaled_argb(const LottieContext *ctx) {
#if defined(USE_LVGL_PPA) && defined(USE_MIPI_DSI) && LV_COLOR_DEPTH == 32
  // The regional PPA path is useful even at 1:1. In that case it avoids
  // sending the weather canvas through LVGL's main invalidation/flush path;
  // only the changed radial rectangle is blended into the active frame.
  if (ctx == nullptr || !ctx->fast_radial || ctx->render_width == 0 || ctx->render_height == 0 ||
      ctx->width % ctx->render_width != 0 || ctx->height % ctx->render_height != 0) {
    return false;
  }
  return ctx->width / ctx->render_width == ctx->height / ctx->render_height;
#else
  return false;
#endif
}

// Opaque weather frames are already in the panel's XRGB format. The general
// ThorVG path uses the serialized DSI-safe presenter; only the compact
// fast-radial scene is allowed to overlap rasterization and regional PPA.
inline bool lottie_uses_direct_opaque_pipeline(const LottieContext *ctx) {
#if defined(USE_LVGL_PPA) && defined(USE_MIPI_DSI) && LV_COLOR_DEPTH == 32
  // Weather assets are flattened to the panel's opaque XRGB format. For the
  // small weather canvases, use the same two-buffer regional PPA path as the
  // radial renderer. Larger/general Lottie scenes stay on the serialized
  // LVGL path because a full-screen PSRAM transfer can starve DSI.
  return ctx != nullptr && ctx->flatten_to_opaque && ctx->width <= 420 && ctx->height <= 420;
#else
  (void) ctx;
  return false;
#endif
}

inline size_t lottie_display_buffer_bytes(const LottieContext *ctx) {
  lv_color_format_t cf = lottie_argb_color_format(ctx);
  if (lottie_uses_direct_xrgb(ctx)) {
    cf = LV_COLOR_FORMAT_XRGB8888;
  } else if (ctx->flatten_to_opaque) {
    cf = LV_COLOR_FORMAT_RGB888;
  }
  return static_cast<size_t>(lv_draw_buf_width_to_stride(ctx->width, cf)) * ctx->height;
}

inline bool lottie_renders_to_display_back_buffer(const LottieContext *ctx) {
  // Opaque animations keep LVGL's canvas source immutable while ThorVG renders
  // into a private XRGB buffer. The regional PPA compositor consumes that
  // buffer directly. This applies to finite weather scenes as well as loops;
  // finite playback promotes its retained final frame before releasing the
  // regional lease.
  // Once the opaque ping-pong targets are available, the regional PPA path
  // owns those targets directly. Keeping the legacy full-size back buffer in
  // the decision here would silently route every frame through the old copy.
  return ctx != nullptr && lottie_uses_direct_xrgb(ctx) && ctx->display_back_buffer != nullptr;
}

// Make a prepared direct-XRGB canvas logically visible without asking LVGL to
// draw its retained source first. The render task immediately follows with a
// complete direct-PPA frame, so invalidating here would briefly expose the
// opaque canvas rectangle before that frame reaches DSI.
inline void lottie_reveal_prepared_frame(LottieContext *ctx) {
  if (ctx == nullptr || ctx->obj == nullptr)
    return;

  lv_lock();
  if (lv_obj_is_valid(ctx->obj) && !ctx->runtime_hidden) {
    const bool direct_reveal = (ctx->flatten_to_opaque && lottie_uses_direct_xrgb(ctx)) ||
                               lottie_uses_direct_scaled_argb(ctx);
    lv_display_t *display = lv_obj_get_display(ctx->obj);
    const bool invalidation_enabled =
        direct_reveal && display != nullptr && lv_display_is_invalidation_enabled(display);
    if (invalidation_enabled)
      lv_display_enable_invalidation(display, false);
    lv_obj_remove_flag(ctx->obj, LV_OBJ_FLAG_HIDDEN);
    if (invalidation_enabled)
      lv_display_enable_invalidation(display, true);
    if (!direct_reveal)
      lv_obj_invalidate(ctx->obj);
  }
  lv_unlock();
}

inline uint8_t *lottie_render_target(LottieContext *ctx) {
  if (lottie_renders_to_display_back_buffer(ctx))
    return ctx->display_back_buffer;
  if (lottie_uses_direct_opaque_pipeline(ctx))
    return ctx->work_buffer;
  return ctx != nullptr && ctx->work_buffer != nullptr ? ctx->work_buffer
                                                       : (ctx != nullptr ? ctx->pixel_buffer : nullptr);
}

inline uint8_t *lottie_initial_render_target(LottieContext *ctx) {
  if (lottie_renders_to_display_back_buffer(ctx))
    return ctx->display_back_buffer;
  return lottie_render_target(ctx);
}

inline size_t lottie_memory_usage_bytes(const LottieContext *ctx) {
  if (ctx == nullptr)
    return 0;
  size_t bytes = 0;
  if (ctx->pixel_buffer != nullptr)
    bytes += lottie_display_buffer_bytes(ctx);
  if (ctx->work_buffer != nullptr && ctx->work_buffer != ctx->pixel_buffer)
    bytes += lottie_buffer_bytes(ctx);
  if (ctx->work_buffer_alt != nullptr && ctx->work_buffer_alt != ctx->pixel_buffer &&
      ctx->work_buffer_alt != ctx->work_buffer)
    bytes += lottie_buffer_bytes(ctx);
  if (ctx->display_back_buffer != nullptr && ctx->display_back_buffer != ctx->pixel_buffer &&
      ctx->display_back_buffer != ctx->work_buffer) {
    bytes += lottie_display_buffer_bytes(ctx);
  }
  if (ctx->frame_cache != nullptr)
    bytes += ctx->frame_cache_stride * ctx->frame_cache_count;
  bytes += ctx->direct_background_size;
  if (ctx->task_stack != nullptr)
    bytes += LOTTIE_TASK_STACK_SIZE;
  if (ctx->task_tcb != nullptr)
    bytes += sizeof(StaticTask_t);
  return bytes;
}

inline void lottie_log_memory_usage(const LottieContext *ctx, const char *phase) {
  if (ctx == nullptr) {
    ESP_LOGW("memory.lottie", "%s unavailable", phase == nullptr ? "runtime" : phase);
    return;
  }
  const size_t pixel_bytes = ctx->pixel_buffer == nullptr ? 0 : lottie_display_buffer_bytes(ctx);
  const size_t work_bytes =
      ctx->work_buffer == nullptr || ctx->work_buffer == ctx->pixel_buffer ? 0 : lottie_buffer_bytes(ctx);
  const size_t work_alt_bytes = ctx->work_buffer_alt == nullptr || ctx->work_buffer_alt == ctx->pixel_buffer ||
                                        ctx->work_buffer_alt == ctx->work_buffer
                                    ? 0
                                    : lottie_buffer_bytes(ctx);
  const size_t back_bytes = ctx->display_back_buffer == nullptr || ctx->display_back_buffer == ctx->pixel_buffer ||
                                    ctx->display_back_buffer == ctx->work_buffer
                                ? 0
                                : lottie_display_buffer_bytes(ctx);
  const size_t cache_bytes = ctx->frame_cache == nullptr ? 0 : ctx->frame_cache_stride * ctx->frame_cache_count;
  const size_t task_bytes =
      (ctx->task_stack == nullptr ? 0 : LOTTIE_TASK_STACK_SIZE) + (ctx->task_tcb == nullptr ? 0 : sizeof(StaticTask_t));
  ESP_LOGW("memory.lottie",
           "%s total=%uK pixel=%uK work=%uK work_alt=%uK back=%uK frame_cache=%uK tasks=%uK frames=%u",
            phase == nullptr ? "runtime" : phase, (unsigned) (lottie_memory_usage_bytes(ctx) / 1024),
            (unsigned) (pixel_bytes / 1024), (unsigned) (work_bytes / 1024), (unsigned) (work_alt_bytes / 1024),
            (unsigned) (back_bytes / 1024), (unsigned) (cache_bytes / 1024), (unsigned) (task_bytes / 1024),
            (unsigned) ctx->frame_cache_count);
}

inline int32_t lottie_first_renderable_frame(const LottieContext *ctx) {
  if (ctx == nullptr || ctx->end_frame <= ctx->start_frame) {
    return ctx != nullptr ? ctx->start_frame : 0;
  }
  // ThorVG rejects frame 0 for at least some trim-path animations. Starting
  // from the first renderable frame also avoids a blank prepared canvas.
  return ctx->start_frame + 1;
}

inline int32_t lottie_last_renderable_frame(const LottieContext *ctx) {
  if (ctx == nullptr || ctx->end_frame <= ctx->start_frame) {
    return ctx != nullptr ? ctx->start_frame : 0;
  }
  // Lottie's out-point is exclusive. Rendering it can publish the blank
  // frame immediately following the visible animation.
  return std::max(ctx->start_frame, ctx->end_frame - 1);
}

// Re-anchor a retained frame instead of advancing the animation clock by the
// whole time spent off-screen. The latter makes the producer try to catch up
// after a carousel return, causing a visible speed burst and sometimes a blank
// direct-region handoff.
inline void lottie_reanchor_retained_frame(LottieContext *ctx) {
  if (ctx == nullptr || ctx->duration_ms == 0 || ctx->end_frame <= ctx->start_frame)
    return;
  const int32_t first = lottie_first_renderable_frame(ctx);
  const int32_t last = lottie_last_renderable_frame(ctx);
  const int32_t frame = std::clamp(ctx->retained_frame, first, last);
  const int32_t span = std::max<int32_t>(1, last - first);
  const uint32_t phase_ms = static_cast<uint32_t>(
      (static_cast<uint64_t>(frame - first) * ctx->duration_ms) / static_cast<uint32_t>(span));
  ctx->start_time_us = esp_timer_get_time() - static_cast<int64_t>(phase_ms) * 1000;
}

inline void lottie_fill_display_buffer(LottieContext *ctx, uint8_t *target, size_t bytes) {
  if (ctx == nullptr || target == nullptr || bytes == 0)
    return;

  lvgl_esphome_release_dma_image_buffer(target);

  if (lottie_uses_direct_xrgb(ctx)) {
    const uint32_t background = 0xFF000000U | (static_cast<uint32_t>(ctx->opaque_background.red) << 16) |
                                (static_cast<uint32_t>(ctx->opaque_background.green) << 8) |
                                static_cast<uint32_t>(ctx->opaque_background.blue);
    if (!lvgl_port_ppa_v9_fill_argb8888(target, bytes, ctx->render_width, ctx->render_height, background))
      std::fill_n(reinterpret_cast<uint32_t *>(target), bytes / sizeof(uint32_t), background);
    return;
  }

  if (ctx->flatten_to_opaque) {
    const uint32_t stride = lv_draw_buf_width_to_stride(ctx->width, LV_COLOR_FORMAT_RGB888);
    for (uint32_t y = 0; y < ctx->height; y++) {
      uint8_t *row = target + static_cast<size_t>(y) * stride;
      for (uint32_t x = 0; x < ctx->width; x++, row += 3) {
        row[0] = ctx->opaque_background.blue;
        row[1] = ctx->opaque_background.green;
        row[2] = ctx->opaque_background.red;
      }
    }
    return;
  }

  memset(target, 0, bytes);
}

// The sunny weather asset is a fixed eight-ray radial scene. ThorVG is the
// right general renderer for arbitrary Lottie documents, but rasterizing this
// particular 484px transparent scene on every frame costs more than the
// actual animation. Keep the Lottie lifecycle and timing, while drawing the
// small set of primitives directly into the same ARGB presentation buffer.
// Most pixels in a capsule are either comfortably inside or outside its
// edge. The previous implementation evaluated four square roots for every
// sample of every pixel. Classify those pixels using squared distances and
// pay for a single sqrt only on the one-pixel antialiasing band.
static constexpr int32_t LOTTIE_FAST_RADIAL_COORD_SHIFT = 8;
static constexpr int32_t LOTTIE_FAST_RADIAL_COORD_ONE = 1 << LOTTIE_FAST_RADIAL_COORD_SHIFT;
static constexpr int32_t LOTTIE_FAST_RADIAL_VECTOR_SHIFT = 15;
static constexpr int32_t LOTTIE_FAST_RADIAL_VECTOR_ONE = 1 << LOTTIE_FAST_RADIAL_VECTOR_SHIFT;

struct LottieFastRadialCoverage {
  // The 242px source canvas keeps every squared distance below 2^32. Keep
  // both the comparisons and the antialias interpolation 32-bit. A Q20
  // reciprocal is sufficient for a one-byte coverage value and avoids the
  // 64-bit multiply that used to run on every edge pixel.
  uint32_t full_squared;
  uint32_t zero_squared;
  uint32_t slope_q20;
};

struct LottieFastRadialPalette {
  uint32_t fill_over_outline[256];
  uint32_t outline_color;
  uint32_t fill_color;
  bool valid;
};

// The radial weather scene is the only Lottie path with a deliberately tight
// per-pixel software loop. Keep the higher-level firmware at ESPHome's normal
// -O2 setting, but let GCC spend its extra instruction-selection budget on
// these three hot functions. This is local to the optional fast path and does
// not change LVGL, PPA, or the rest of the application.
#if defined(__GNUC__)
#define LOTTIE_FAST_RADIAL_HOT __attribute__((hot, optimize("O3")))
#else
#define LOTTIE_FAST_RADIAL_HOT
#endif

inline int32_t lottie_fast_radial_q8(float value) {
  return static_cast<int32_t>(value * static_cast<float>(LOTTIE_FAST_RADIAL_COORD_ONE) + 0.5f);
}

inline LottieFastRadialCoverage lottie_fast_radial_make_coverage(int32_t radius_q8) {
  // Preserve the former one-pixel antialias ramp, but interpolate in squared
  // distance. Around the edge the difference from sqrt(distance) is below a
  // subpixel while removing sqrt and division from every sampled pixel.
  const int32_t full_radius = std::max<int32_t>(0, radius_q8 - LOTTIE_FAST_RADIAL_COORD_ONE / 4);
  const int32_t zero_radius = radius_q8 + 3 * LOTTIE_FAST_RADIAL_COORD_ONE / 4;
  const uint32_t full_squared = static_cast<uint32_t>(full_radius * full_radius);
  const uint32_t zero_squared = static_cast<uint32_t>(zero_radius * zero_radius);
  const uint32_t span = std::max<uint32_t>(1, zero_squared - full_squared);
  // For this scene, remaining * slope_q20 is bounded by 255 << 20 and fits
  // in uint32_t. That is materially cheaper on the ESP32-P4 than the former
  // 64-bit interpolation.
  const uint32_t slope_q20 = static_cast<uint32_t>(((255U << 20) + span / 2U) / span);
  return {full_squared, zero_squared, slope_q20};
}

inline uint8_t lottie_fast_radial_coverage_from_squared(uint32_t distance_squared,
                                                         const LottieFastRadialCoverage &coverage) {
  if (distance_squared <= coverage.full_squared)
    return 255;
  if (distance_squared >= coverage.zero_squared)
    return 0;
  const uint32_t remaining = coverage.zero_squared - distance_squared;
  return static_cast<uint8_t>(std::min<uint32_t>(
      255U, (remaining * coverage.slope_q20 + (1U << 19)) >> 20));
}

inline uint32_t lottie_fast_radial_opaque_color(const lv_color_t &color) {
  return 0xFF000000U | (static_cast<uint32_t>(color.red) << 16) | (static_cast<uint32_t>(color.green) << 8) |
         static_cast<uint32_t>(color.blue);
}

inline uint32_t lottie_fast_divide_by_255(uint32_t value) {
  const uint32_t rounded = value + 128U;
  return (rounded + (rounded >> 8U)) >> 8U;
}

inline uint32_t lottie_fast_radial_blend_over_opaque(uint32_t destination, uint32_t source, uint8_t alpha) {
  const uint32_t inverse = 255U - alpha;
  const uint32_t red = lottie_fast_divide_by_255(((source >> 16) & 0xFFU) * alpha +
                                                  ((destination >> 16) & 0xFFU) * inverse);
  const uint32_t green = lottie_fast_divide_by_255(((source >> 8) & 0xFFU) * alpha +
                                                    ((destination >> 8) & 0xFFU) * inverse);
  const uint32_t blue = lottie_fast_divide_by_255((source & 0xFFU) * alpha + (destination & 0xFFU) * inverse);
  return 0xFF000000U | (red << 16) | (green << 8) | blue;
}

inline void lottie_fast_radial_prepare_palette(LottieFastRadialPalette *palette, uint32_t outline_color,
                                                 uint32_t fill_color) {
  if (palette == nullptr)
    return;
  if (palette->valid && palette->outline_color == outline_color && palette->fill_color == fill_color)
    return;
  palette->outline_color = outline_color;
  palette->fill_color = fill_color;
  palette->fill_over_outline[0] = outline_color;
  palette->fill_over_outline[255] = fill_color;
  for (uint32_t alpha = 1; alpha < 255; alpha++) {
    palette->fill_over_outline[alpha] =
        lottie_fast_radial_blend_over_opaque(outline_color, fill_color, static_cast<uint8_t>(alpha));
  }
  palette->valid = true;
}

struct LottieFastRadialVector {
  float x;
  float y;
};

inline void lottie_fast_radial_make_vectors(float phase, LottieFastRadialVector *vectors) {
  if (vectors == nullptr)
    return;
  const float phase_cos = std::cos(phase);
  const float phase_sin = std::sin(phase);
  constexpr float DIAGONAL = 0.7071067811865475f;
  vectors[0] = {DIAGONAL * (phase_sin - phase_cos), -DIAGONAL * (phase_sin + phase_cos)};
  vectors[1] = {phase_sin, -phase_cos};
  vectors[2] = {DIAGONAL * (phase_cos + phase_sin), DIAGONAL * (phase_sin - phase_cos)};
  vectors[3] = {phase_cos, phase_sin};
  vectors[4] = {DIAGONAL * (phase_cos - phase_sin), DIAGONAL * (phase_sin + phase_cos)};
  vectors[5] = {-phase_sin, phase_cos};
  vectors[6] = {-DIAGONAL * (phase_cos + phase_sin), DIAGONAL * (phase_cos - phase_sin)};
  vectors[7] = {-phase_cos, -phase_sin};
}

// Clear only the rectangle that can contain one capsule. The source buffer is
// ping-ponged, so clearing the previous and current bounds removes stale
// pixels even when a buffer was last used two timeline frames ago.
inline void lottie_fast_radial_clear_capsule_bounds(uint32_t *pixels, uint32_t stride_pixels, uint32_t width,
                                                     uint32_t height, float center_x, float center_y, float ux,
                                                     float uy, float half_length, float outer_radius) {
  if (pixels == nullptr || width == 0 || height == 0)
    return;
  const float extent_x = std::fabs(ux) * half_length + outer_radius + 1.5f;
  const float extent_y = std::fabs(uy) * half_length + outer_radius + 1.5f;
  const int x_start = std::max(0, static_cast<int>(std::floor(center_x - extent_x)));
  const int y_start = std::max(0, static_cast<int>(std::floor(center_y - extent_y)));
  const int x_end = std::min(static_cast<int>(width) - 1, static_cast<int>(std::ceil(center_x + extent_x)));
  const int y_end = std::min(static_cast<int>(height) - 1, static_cast<int>(std::ceil(center_y + extent_y)));
  if (x_start > x_end || y_start > y_end)
    return;
  const size_t row_bytes = static_cast<size_t>(x_end - x_start + 1) * sizeof(uint32_t);
  for (int y = y_start; y <= y_end; y++)
    std::memset(pixels + static_cast<size_t>(y) * stride_pixels + x_start, 0, row_bytes);
}

inline void lottie_fast_radial_store_sample(uint32_t *pixel, const LottieFastRadialPalette &palette,
                                             uint8_t outer_coverage, uint8_t inner_coverage) {
  if (outer_coverage == 0)
    return;

  if (outer_coverage == 255) {
    *pixel = palette.fill_over_outline[inner_coverage];
    return;
  }

  // The outline is wider than the antialias ramp, so a partial outer edge
  // never overlaps the inner edge. Its destination is the cleared canvas.
  *pixel = (static_cast<uint32_t>(outer_coverage) << 24) | (palette.outline_color & 0x00FFFFFFU);
}

// For a linearly moving point, x*x + y*y is a quadratic sequence. Seed its
// first value and first difference once per scanline segment, then advance it
// with additions only. This removes two multiplications from every sampled
// pixel while retaining the fixed-point distance used by the coverage ramp.
inline void lottie_fast_radial_init_squared_step(int32_t x, int32_t y, int32_t step_x, int32_t step_y,
                                                  int32_t *squared, int32_t *delta, int32_t *delta_delta) {
  const int32_t step_squared = step_x * step_x + step_y * step_y;
  *squared = x * x + y * y;
  *delta = 2 * (x * step_x + y * step_y) + step_squared;
  *delta_delta = 2 * step_squared;
}

inline int8_t lottie_fast_radial_capsule_region(int32_t along_q8, int32_t half_length_q8) {
  if (along_q8 < -half_length_q8)
    return -1;
  if (along_q8 > half_length_q8)
    return 1;
  return 0;
}

inline LOTTIE_FAST_RADIAL_HOT void lottie_fast_radial_draw_capsule(uint32_t *pixels, uint32_t stride_pixels,
                                             uint32_t width, uint32_t height,
                                             float center_x, float center_y, float ux, float uy, float half_length,
                                             float outer_radius,
                                             const LottieFastRadialCoverage &inner_coverage,
                                             const LottieFastRadialCoverage &outer_coverage,
                                             const LottieFastRadialPalette &palette) {
  const float extent_x = std::fabs(ux) * half_length + outer_radius + 1.5f;
  const float extent_y = std::fabs(uy) * half_length + outer_radius + 1.5f;
  const int x_start = std::max(0, static_cast<int>(std::floor(center_x - extent_x)));
  const int y_start = std::max(0, static_cast<int>(std::floor(center_y - extent_y)));
  const int x_end = std::min(static_cast<int>(width) - 1, static_cast<int>(std::ceil(center_x + extent_x)));
  const int y_end = std::min(static_cast<int>(height) - 1, static_cast<int>(std::ceil(center_y + extent_y)));
  const int32_t center_x_q8 = lottie_fast_radial_q8(center_x);
  const int32_t center_y_q8 = lottie_fast_radial_q8(center_y);
  const int32_t ux_q15 = static_cast<int32_t>(ux * static_cast<float>(LOTTIE_FAST_RADIAL_VECTOR_ONE));
  const int32_t uy_q15 = static_cast<int32_t>(uy * static_cast<float>(LOTTIE_FAST_RADIAL_VECTOR_ONE));
  const int32_t half_length_q8 = lottie_fast_radial_q8(half_length);
  const int32_t along_step_q8 =
      (LOTTIE_FAST_RADIAL_COORD_ONE * ux_q15) >> LOTTIE_FAST_RADIAL_VECTOR_SHIFT;
  const int32_t across_step_q8 =
      (-LOTTIE_FAST_RADIAL_COORD_ONE * uy_q15) >> LOTTIE_FAST_RADIAL_VECTOR_SHIFT;
  for (int y = y_start; y <= y_end; y++) {
    uint32_t *row = pixels + static_cast<size_t>(y) * stride_pixels;
    const int32_t local_y_q8 = (y << LOTTIE_FAST_RADIAL_COORD_SHIFT) +
                               LOTTIE_FAST_RADIAL_COORD_ONE / 2 - center_y_q8;
    const int32_t local_x_q8 = (x_start << LOTTIE_FAST_RADIAL_COORD_SHIFT) +
                               LOTTIE_FAST_RADIAL_COORD_ONE / 2 - center_x_q8;
    int32_t along_q8 = (local_x_q8 * ux_q15 + local_y_q8 * uy_q15) >> LOTTIE_FAST_RADIAL_VECTOR_SHIFT;
    int32_t across_q8 = (-local_x_q8 * uy_q15 + local_y_q8 * ux_q15) >> LOTTIE_FAST_RADIAL_VECTOR_SHIFT;
    int8_t region = lottie_fast_radial_capsule_region(along_q8, half_length_q8);
    int32_t distance_y_q8 = region < 0 ? along_q8 + half_length_q8 :
                            (region > 0 ? along_q8 - half_length_q8 : 0);
    int32_t squared_q8;
    int32_t delta_q8;
    int32_t delta_delta_q8;
    lottie_fast_radial_init_squared_step(across_q8, distance_y_q8, across_step_q8,
                                         region == 0 ? 0 : along_step_q8, &squared_q8, &delta_q8,
                                         &delta_delta_q8);
    for (int x = x_start; x <= x_end; x++) {
      const int8_t next_region = lottie_fast_radial_capsule_region(along_q8, half_length_q8);
      if (next_region != region) {
        region = next_region;
        distance_y_q8 = region < 0 ? along_q8 + half_length_q8 :
                        (region > 0 ? along_q8 - half_length_q8 : 0);
        lottie_fast_radial_init_squared_step(across_q8, distance_y_q8, across_step_q8,
                                             region == 0 ? 0 : along_step_q8, &squared_q8, &delta_q8,
                                             &delta_delta_q8);
      }
      const uint32_t distance_squared = static_cast<uint32_t>(squared_q8);
      const uint8_t outer = lottie_fast_radial_coverage_from_squared(distance_squared, outer_coverage);
      if (outer != 0) {
        const uint8_t inner = lottie_fast_radial_coverage_from_squared(distance_squared, inner_coverage);
        lottie_fast_radial_store_sample(&row[x], palette, outer, inner);
      }
      squared_q8 += delta_q8;
      delta_q8 += delta_delta_q8;
      along_q8 += along_step_q8;
      across_q8 += across_step_q8;
    }
  }
}

inline LOTTIE_FAST_RADIAL_HOT void lottie_fast_radial_draw_circle(uint32_t *pixels, uint32_t stride_pixels,
                                            uint32_t width, uint32_t height,
                                            float center_x, float center_y, float outer_radius,
                                            const LottieFastRadialCoverage &inner_coverage,
                                            const LottieFastRadialCoverage &outer_coverage,
                                            const LottieFastRadialPalette &palette) {
  const int x_start = std::max(0, static_cast<int>(std::floor(center_x - outer_radius - 1.5f)));
  const int y_start = std::max(0, static_cast<int>(std::floor(center_y - outer_radius - 1.5f)));
  const int x_end = std::min(static_cast<int>(width) - 1, static_cast<int>(std::ceil(center_x + outer_radius + 1.5f)));
  const int y_end = std::min(static_cast<int>(height) - 1, static_cast<int>(std::ceil(center_y + outer_radius + 1.5f)));
  const int32_t center_x_q8 = lottie_fast_radial_q8(center_x);
  const int32_t center_y_q8 = lottie_fast_radial_q8(center_y);
  for (int y = y_start; y <= y_end; y++) {
    uint32_t *row = pixels + static_cast<size_t>(y) * stride_pixels;
    const int32_t local_y_q8 = (y << LOTTIE_FAST_RADIAL_COORD_SHIFT) +
                               LOTTIE_FAST_RADIAL_COORD_ONE / 2 - center_y_q8;
    int32_t local_x_q8 = (x_start << LOTTIE_FAST_RADIAL_COORD_SHIFT) +
                         LOTTIE_FAST_RADIAL_COORD_ONE / 2 - center_x_q8;
    int32_t distance_squared;
    int32_t distance_delta;
    int32_t distance_delta_delta;
    lottie_fast_radial_init_squared_step(local_x_q8, local_y_q8, LOTTIE_FAST_RADIAL_COORD_ONE, 0,
                                         &distance_squared, &distance_delta, &distance_delta_delta);
    for (int x = x_start; x <= x_end; x++) {
      const uint8_t outer = lottie_fast_radial_coverage_from_squared(static_cast<uint32_t>(distance_squared),
                                                                      outer_coverage);
      if (outer != 0) {
        const uint8_t inner = lottie_fast_radial_coverage_from_squared(static_cast<uint32_t>(distance_squared),
                                                                         inner_coverage);
        lottie_fast_radial_store_sample(&row[x], palette, outer, inner);
      }
      distance_squared += distance_delta;
      distance_delta += distance_delta_delta;
      local_x_q8 += LOTTIE_FAST_RADIAL_COORD_ONE;
    }
  }
}

inline LOTTIE_FAST_RADIAL_HOT bool lottie_render_fast_radial_frame(LottieContext *ctx, int32_t frame, uint8_t *target) {
  if (ctx == nullptr || target == nullptr || ctx->render_width == 0 || ctx->render_height == 0)
    return false;
  const uint32_t stride = lv_draw_buf_width_to_stride(ctx->render_width, lottie_argb_color_format(ctx));
  auto *pixels = reinterpret_cast<uint32_t *>(target);
  const uint32_t stride_pixels = stride / sizeof(uint32_t);
  const float scale = static_cast<float>(std::min(ctx->render_width, ctx->render_height)) / 128.0f;
  const float center_x = (static_cast<float>(ctx->render_width) - 1.0f) * 0.5f;
  const float center_y = (static_cast<float>(ctx->render_height) - 1.0f) * 0.5f;
  const int32_t frame_span = std::max<int32_t>(1, ctx->end_frame - ctx->start_frame);
  const float progress = std::clamp(static_cast<float>(frame - ctx->start_frame) / static_cast<float>(frame_span), 0.0f, 1.0f);
  const float phase = progress * 6.2831853071795864769f;
  // The compact source is presented at 2x. These proportions match the
  // measured 800px product composition while retaining the 197px raster.
  const float ray_center_radius = 51.0f * scale;
  const float ray_half_length = 9.5f * scale;
  // The source SVG uses 6 logical pixels for the ray width and a 1.28px
  // stroke at 128x128. Keep the fill radius and the stroke half-width in
  // those source units; treating the stroke as a whole capsule radius makes
  // a black rosette that is much larger than the design.
  const float ray_inner_radius = 3.0f * scale;
  // Keep an emphatic contour after the compact frame is enlarged by PPA. The
  // extra half source pixel makes the outline read like the design at the
  // 2x presentation scale without creating a second outer rosette.
  const float stroke_half_width = 3.5f * scale;
  const float ray_outer_radius = ray_inner_radius + stroke_half_width;
  // All rays share one rotation phase. Computing sin/cos for every ray made
  // the software rasterizer spend more time on trigonometry than geometry.
  // Rotate the eight fixed 45-degree basis vectors from one sin/cos pair.
  LottieFastRadialVector ray_vectors[8];
  lottie_fast_radial_make_vectors(phase, ray_vectors);

  // Keep the source and destination region dimensions stable while the ray
  // rotates. The regional compositor can then reuse its PPA scratch and
  // framebuffer lease; changing the rectangle every frame causes an expensive
  // rebase even when the visible pixel count is slightly smaller.
  const int clear_extent = static_cast<int>(std::ceil(ray_center_radius + ray_half_length + ray_outer_radius + 2.0f));
  const int clear_x_start = std::max(0, static_cast<int>(std::floor(center_x - clear_extent)));
  const int clear_y_start = std::max(0, static_cast<int>(std::floor(center_y - clear_extent)));
  const int clear_x_end = std::min(static_cast<int>(ctx->render_width) - 1,
                                   static_cast<int>(std::ceil(center_x + clear_extent)));
  const int clear_y_end = std::min(static_cast<int>(ctx->render_height) - 1,
                                   static_cast<int>(std::ceil(center_y + clear_extent)));
  int buffer_index = -1;
  for (int index = 0; index < 2; index++) {
    if (ctx->fast_radial_buffer_ptr[index] == target) {
      buffer_index = index;
      break;
    }
  }
  const int32_t previous_frame = buffer_index >= 0 ? ctx->fast_radial_last_frame[buffer_index] : -1;
  const bool buffer_initialized = previous_frame >= ctx->start_frame;
  if (!buffer_initialized) {
    // A newly allocated/restarted source is blank, but keep the explicit
    // clear for the case where the allocator recycled an older canvas.
    const size_t clear_row_bytes = static_cast<size_t>(clear_x_end - clear_x_start + 1) * sizeof(uint32_t);
    for (int y = clear_y_start; y <= clear_y_end; y++)
      std::memset(pixels + static_cast<size_t>(y) * stride_pixels + clear_x_start, 0, clear_row_bytes);
  } else {
    const float previous_progress = std::clamp(
        static_cast<float>(previous_frame - ctx->start_frame) / static_cast<float>(frame_span), 0.0f, 1.0f);
    const float previous_phase = previous_progress * 6.2831853071795864769f;
    LottieFastRadialVector previous_vectors[8];
    lottie_fast_radial_make_vectors(previous_phase, previous_vectors);
    for (const auto &ray : previous_vectors) {
      lottie_fast_radial_clear_capsule_bounds(pixels, stride_pixels, ctx->render_width, ctx->render_height,
                                               center_x + ray.x * ray_center_radius,
                                               center_y + ray.y * ray_center_radius, ray.x, ray.y, ray_half_length,
                                               ray_outer_radius);
    }
    for (const auto &ray : ray_vectors) {
      lottie_fast_radial_clear_capsule_bounds(pixels, stride_pixels, ctx->render_width, ctx->render_height,
                                               center_x + ray.x * ray_center_radius,
                                               center_y + ray.y * ray_center_radius, ray.x, ray.y, ray_half_length,
                                               ray_outer_radius);
    }
  }
  ctx->fast_radial_dirty_x1 = static_cast<int16_t>(clear_x_start);
  ctx->fast_radial_dirty_y1 = static_cast<int16_t>(clear_y_start);
  ctx->fast_radial_dirty_x2 = static_cast<int16_t>(clear_x_end);
  ctx->fast_radial_dirty_y2 = static_cast<int16_t>(clear_y_end);
  const uint32_t outline_color = lottie_fast_radial_opaque_color(ctx->fast_radial_outline_color);
  const uint32_t fill_color = lottie_fast_radial_opaque_color(ctx->fast_radial_fill_color);
  // The palette is invariant while the weather widget is alive. Keeping it
  // outside the frame stack avoids rebuilding 256 alpha blends on every
  // tick; this path has a single fast-radial weather instance by design.
  static LottieFastRadialPalette palette{};
  lottie_fast_radial_prepare_palette(&palette, outline_color, fill_color);
  const auto ray_inner_coverage = lottie_fast_radial_make_coverage(lottie_fast_radial_q8(ray_inner_radius));
  const auto ray_outer_coverage = lottie_fast_radial_make_coverage(lottie_fast_radial_q8(ray_outer_radius));
  for (const auto &ray : ray_vectors) {
    const float ux = ray.x;
    const float uy = ray.y;
    lottie_fast_radial_draw_capsule(
        pixels, stride_pixels, ctx->render_width, ctx->render_height, center_x + ux * ray_center_radius,
        center_y + uy * ray_center_radius, ux, uy, ray_half_length, ray_outer_radius, ray_inner_coverage,
        ray_outer_coverage, palette);
  }
  if (!buffer_initialized) {
    const float core_inner_radius = 26.0f * scale;
    const float core_outer_radius = core_inner_radius + stroke_half_width;
    const auto core_inner_coverage = lottie_fast_radial_make_coverage(lottie_fast_radial_q8(core_inner_radius));
    const auto core_outer_coverage = lottie_fast_radial_make_coverage(lottie_fast_radial_q8(core_outer_radius));
    lottie_fast_radial_draw_circle(pixels, stride_pixels, ctx->render_width, ctx->render_height, center_x, center_y,
                                   core_outer_radius, core_inner_coverage, core_outer_coverage, palette);
  }
  if (buffer_index >= 0)
    ctx->fast_radial_last_frame[buffer_index] = frame;
  // PPA is the next owner in both presentation paths. The scaler synchronizes
  // this CPU-written source before reading it, and the LVGL PPA image unit does
  // the same for an unscaled canvas. An eager writeback here duplicated every
  // PSRAM cache transfer and consumed most of the frame budget.
#if !defined(USE_LVGL_PPA)
  const size_t sync_offset = static_cast<size_t>(clear_y_start) * stride;
  const size_t sync_bytes = static_cast<size_t>(clear_y_end - clear_y_start + 1) * stride;
  lottie_sync_buffer(target + sync_offset, sync_bytes);
#endif
  return true;
}

// ThorVG owns only the work buffer while rendering. LVGL keeps reading the
// previous complete frame and is locked only for the short source swap.
inline bool lottie_render_frame(LottieContext *ctx, int32_t frame, uint8_t *target) {
  if (ctx == nullptr || ctx->obj == nullptr || target == nullptr)
    return false;
  if (ctx->fast_radial)
    return lottie_render_fast_radial_frame(ctx, frame, target);
  auto *lottie = reinterpret_cast<lv_lottie_t *>(ctx->obj);
  if (lottie->tvg_canvas == nullptr || lottie->tvg_anim == nullptr)
    return false;

  const uint32_t stride = lv_draw_buf_width_to_stride(ctx->render_width, lottie_argb_color_format(ctx));
  const size_t bytes = static_cast<size_t>(stride) * ctx->render_height;
  const bool profile = lvgl_esphome_get_perf_logging_enabled() != 0;
  const int64_t t0 = profile ? esp_timer_get_time() : 0;
  // Transparent scenes alternate the rendered and displayed ARGB buffers.
  // Wait until DSI releases the previous source before reusing it; the third
  // full-frame copy used by the old path consumed nearly another megabyte of
  // PSRAM and caused allocator starvation on the Home page.
  if (ctx->work_buffer == target && ctx->display_back_buffer == nullptr &&
      !lottie_uses_direct_opaque_pipeline(ctx))
    lottie_wait_display_fifo();
  if (lottie_uses_direct_xrgb(ctx)) {
    // Start ThorVG on the final opaque background. Its software rasterizer
    // blends the vector paint directly into this target, so publishing can
    // expose the same completed buffer as XRGB without a second full-frame
    // flatten pass. In memory the ARGB8888 target is BGRA on little-endian
    // ESP32, hence the packed 0xAARRGGBB value below.
    const uint32_t background = 0xFF000000U | (static_cast<uint32_t>(ctx->opaque_background.red) << 16) |
                                (static_cast<uint32_t>(ctx->opaque_background.green) << 8) |
                                static_cast<uint32_t>(ctx->opaque_background.blue);
    if (!lvgl_port_ppa_v9_fill_argb8888(target, bytes, ctx->render_width, ctx->render_height, background))
      std::fill_n(reinterpret_cast<uint32_t *>(target), bytes / sizeof(uint32_t), background);
  } else {
    memset(target, 0, bytes);
  }
  const int64_t t1 = profile ? esp_timer_get_time() : 0;
  if (ctx->thorvg_target != target) {
    if (tvg_swcanvas_set_target(lottie->tvg_canvas, reinterpret_cast<uint32_t *>(target), stride / 4,
                                ctx->render_width, ctx->render_height, TVG_COLORSPACE_ARGB8888) !=
        TVG_RESULT_SUCCESS) {
      return false;
    }
    ctx->thorvg_target = target;
  }
  const Tvg_Result set_frame_result = tvg_animation_set_frame(lottie->tvg_anim, static_cast<float>(frame));
  const int64_t t2 = profile ? esp_timer_get_time() : 0;
  const bool frame_ready =
      set_frame_result == TVG_RESULT_SUCCESS || set_frame_result == TVG_RESULT_INSUFFICIENT_CONDITION;
  // A Lottie canvas contains one animation picture. Updating that root paint
  // avoids ThorVG walking the canvas paint list again on every frame while
  // preserving the same transform/animation state as a full canvas update.
  // Fall back to the full update only if the root update cannot be prepared.
  Tvg_Result update_result = frame_ready ? tvg_canvas_update_paint(lottie->tvg_canvas, lottie->tvg_paint)
                                         : set_frame_result;
  if (frame_ready && update_result != TVG_RESULT_SUCCESS && update_result != TVG_RESULT_INSUFFICIENT_CONDITION)
    update_result = tvg_canvas_update(lottie->tvg_canvas);
  const int64_t t3 = profile ? esp_timer_get_time() : 0;
  const bool canvas_ready = update_result == TVG_RESULT_SUCCESS || update_result == TVG_RESULT_INSUFFICIENT_CONDITION;
  const Tvg_Result draw_result = canvas_ready ? tvg_canvas_draw(lottie->tvg_canvas) : update_result;
  const int64_t t4 = profile ? esp_timer_get_time() : 0;
  // ThorVG requires a canvas sync after every submitted draw. This is part of
  // the C API contract even for a software canvas and even when draw returns
  // SUCCESS; without it the target can be presented before the raster work is
  // complete, producing stale/static frames and corrupting later surfaces.
  const bool draw_submitted = draw_result == TVG_RESULT_SUCCESS || draw_result == TVG_RESULT_INSUFFICIENT_CONDITION;
  const Tvg_Result sync_result = draw_submitted ? tvg_canvas_sync(lottie->tvg_canvas) : draw_result;
  const int64_t t5 = profile ? esp_timer_get_time() : 0;
  if (sync_result != TVG_RESULT_SUCCESS && sync_result != TVG_RESULT_INSUFFICIENT_CONDITION) {
    ESP_LOGW(LOTTIE_PERF_TAG, "render frame failed frame=%d set=%d update=%d draw=%d sync=%d", (int) frame,
             (int) set_frame_result, (int) update_result, (int) draw_result, (int) sync_result);
    return false;
  }
  // The direct XRGB path is consumed by the regional PPA image unit. The
  // source is rendered by ThorVG through the CPU cache, so the compositor
  // must perform C2M before its DMA read. Keep that ownership boundary in the
  // PPA operation itself; doing an eager sync here would duplicate it.
  if (!lottie_uses_direct_xrgb(ctx) && !lottie_uses_scaled_render(ctx)) {
    lottie_sync_buffer(target, bytes);
  }
  if (profile) {
    const int64_t times[] = {t0, t1, t2, t3, t4, t5, esp_timer_get_time()};
    for (size_t i = 0; i < 6; i++)
      ctx->raster_stages_us[i] += times[i + 1] - times[i];
    ctx->raster_stage_samples++;
  }
  return true;
}

// Track a representative visible frame without retaining another full image.
// Several weather animations fade to an empty out-point, so their numeric last
// frame is not the frame users expect to remain after finite playback. Sampling
// at most 32 x 32 pixels keeps this negligible beside ThorVG rendering.
inline void lottie_track_retained_frame(LottieContext *ctx, int32_t frame, const uint8_t *target) {
  if (ctx == nullptr || target == nullptr || !lottie_uses_direct_xrgb(ctx) || ctx->play_count == 0)
    return;

  const uint8_t bg_r = ctx->opaque_background.red;
  const uint8_t bg_g = ctx->opaque_background.green;
  const uint8_t bg_b = ctx->opaque_background.blue;
  const uint32_t stride = lv_draw_buf_width_to_stride(ctx->width, LV_COLOR_FORMAT_XRGB8888) / sizeof(uint32_t);
  const uint32_t step_x = std::max<uint32_t>(1U, (ctx->width + 31U) / 32U);
  const uint32_t step_y = std::max<uint32_t>(1U, (ctx->height + 31U) / 32U);
  const auto *pixels = reinterpret_cast<const uint32_t *>(target);
  uint32_t coverage = 0;
  for (uint32_t y = 0; y < ctx->height; y += step_y) {
    const uint32_t *row = pixels + static_cast<size_t>(y) * stride;
    for (uint32_t x = 0; x < ctx->width; x += step_x) {
      const uint32_t pixel = row[x];
      const uint8_t alpha = static_cast<uint8_t>(pixel >> 24);
      const uint8_t red = static_cast<uint8_t>(pixel >> 16);
      const uint8_t green = static_cast<uint8_t>(pixel >> 8);
      const uint8_t blue = static_cast<uint8_t>(pixel);
      const uint16_t delta = static_cast<uint16_t>(std::abs(static_cast<int>(red) - bg_r)) +
                             static_cast<uint16_t>(std::abs(static_cast<int>(green) - bg_g)) +
                             static_cast<uint16_t>(std::abs(static_cast<int>(blue) - bg_b));
      coverage += alpha >= 8U && delta >= 12U;
    }
  }
  if (coverage >= ctx->retained_coverage) {
    ctx->retained_coverage = coverage;
    ctx->retained_frame = frame;
  }
}

inline bool lottie_copy_argb_frame(LottieContext *ctx, const uint8_t *src, uint8_t *dst) {
  if (ctx == nullptr || src == nullptr || dst == nullptr)
    return false;

  if (lottie_uses_scaled_render(ctx)) {
    return lvgl_esphome_scale_argb8888(src, ctx->render_width, ctx->render_height, dst, ctx->width, ctx->height);
  }

  const size_t bytes = lottie_display_buffer_bytes(ctx);
  if (lvgl_esphome_copy_argb8888(src, dst, ctx->width, ctx->height)) {
    return true;
  }
  memcpy(dst, src, bytes);
  lottie_sync_buffer(dst, bytes);
  return true;
}

inline void lottie_invalidate_published_frame(LottieContext *ctx) {
  if (ctx == nullptr || ctx->obj == nullptr)
    return;
  if (!ctx->fast_radial || ctx->fast_radial_dirty_x2 < ctx->fast_radial_dirty_x1 ||
      ctx->fast_radial_dirty_y2 < ctx->fast_radial_dirty_y1) {
    lv_obj_invalidate(ctx->obj);
    return;
  }

  lv_area_t object_area;
  lv_obj_get_coords(ctx->obj, &object_area);
  lv_area_t dirty_area = {
      static_cast<lv_coord_t>(object_area.x1 + ctx->fast_radial_dirty_x1),
      static_cast<lv_coord_t>(object_area.y1 + ctx->fast_radial_dirty_y1),
      static_cast<lv_coord_t>(object_area.x1 + ctx->fast_radial_dirty_x2),
      static_cast<lv_coord_t>(object_area.y1 + ctx->fast_radial_dirty_y2),
  };
  lv_obj_invalidate_area(ctx->obj, &dirty_area);
}

// LVGL locking inside the worker alone cannot serialize with ESPHome YAML
// actions, which run on the main loop. Only that loop may mutate the native
// canvas. The bounded mailbox holds one frame; rasterization needs no copies.
inline void lottie_process_native_publications() {
  for (auto *ctx = lottie_contexts; ctx != nullptr; ctx = ctx->next_context) {
    if (!__atomic_load_n(&ctx->native_publish_pending, __ATOMIC_ACQUIRE))
      continue;
    ctx->native_publish_success = false;
    auto *ready = ctx->native_publish_buffer;
    if (!ctx->stop_requested && ready != nullptr && ctx->obj != nullptr && lv_obj_is_valid(ctx->obj)) {
      lv_lock();
      lv_canvas_set_buffer(ctx->obj, ready, ctx->width, ctx->height, lottie_argb_color_format(ctx));
      auto *draw_buf = lv_canvas_get_draw_buf(ctx->obj);
      if (draw_buf != nullptr) {
        if (ctx->fast_radial)
          lv_draw_buf_clear_flag(draw_buf, LV_IMAGE_FLAGS_PREMULTIPLIED);
        else
          lv_draw_buf_set_flag(draw_buf, LV_IMAGE_FLAGS_PREMULTIPLIED);
      }
      lottie_invalidate_published_frame(ctx);
      lv_unlock();
      if (ctx->native_publish_scaled) {
        const bool internal = ctx->display_back_buffer_internal;
        ctx->display_back_buffer = ctx->pixel_buffer;
        ctx->display_back_buffer_internal = ctx->pixel_buffer_internal;
        ctx->pixel_buffer_internal = internal;
      } else {
        const bool internal = ctx->work_buffer_internal;
        ctx->work_buffer = ctx->pixel_buffer;
        ctx->work_buffer_internal = ctx->pixel_buffer_internal;
        ctx->pixel_buffer_internal = internal;
      }
      ctx->pixel_buffer = ready;
      ctx->native_publish_success = true;
    }
    __atomic_store_n(&ctx->native_publish_pending, false, __ATOMIC_RELEASE);
    if (ctx->task_handle != nullptr)
      xTaskNotifyGive(ctx->task_handle);
  }
}

inline bool lottie_request_native_publication(LottieContext *ctx, uint8_t *buffer, bool scaled) {
  ctx->native_publish_buffer = buffer;
  ctx->native_publish_scaled = scaled;
  ctx->native_publish_success = false;
  __atomic_store_n(&ctx->native_publish_pending, true, __ATOMIC_RELEASE);
  // The renderer is a worker task and the native canvas swap is owned by the
  // ESPHome loop. Wake that loop immediately instead of waiting for its next
  // scheduled tick; this keeps the first boot/weather frame from stalling the
  // animation for tens of milliseconds.
  ::esphome_main_task_notify();
  while (__atomic_load_n(&ctx->native_publish_pending, __ATOMIC_ACQUIRE) && !ctx->stop_requested)
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
  if (ctx->stop_requested) {
    __atomic_store_n(&ctx->native_publish_pending, false, __ATOMIC_RELEASE);
    return false;
  }
  return ctx->native_publish_success;
}

// The opaque direct publisher appears before the throttled diagnostic helper.
inline void lottie_log_direct_publish_failure(LottieContext *ctx, const char *reason, uint8_t result = 0);

inline bool lottie_publish_direct_opaque_frame(LottieContext *ctx) {
  if (ctx == nullptr)
    return false;
  const char *reject_reason = nullptr;
  if (ctx->work_buffer == nullptr || ctx->work_buffer_alt == nullptr)
    reject_reason = "no-ping-pong-buffer";
  else if (lottie_direct_frame_in_flight(ctx))
    reject_reason = "frame-in-flight";
  else if (ctx->runtime_hidden)
    reject_reason = "runtime-hidden";
  if (reject_reason != nullptr) {
    lottie_log_direct_publish_failure(ctx, reject_reason);
    return false;
  }

  lv_area_t coords{};
  bool presented = false;
  bool hidden_but_ready_to_reveal = false;
  lv_lock();
  if (ctx->obj != nullptr && lv_obj_is_valid(ctx->obj)) {
    const bool on_active_screen = lv_obj_get_screen(ctx->obj) == lv_screen_active();
    const bool hidden = lv_obj_has_flag(ctx->obj, LV_OBJ_FLAG_HIDDEN);
    // lottie_show() deliberately keeps the canvas hidden while its first
    // direct frame is prepared. Treating that transitional state as absent
    // created a deadlock: no direct frame could be published, so the object
    // could never be revealed. runtime_hidden is the authoritative lifecycle
    // guard; a hidden object with it cleared is the intentional reveal path.
    hidden_but_ready_to_reveal = hidden && !ctx->runtime_hidden && ctx->auto_start;
    presented = on_active_screen && (!hidden || hidden_but_ready_to_reveal);
    lv_obj_get_coords(ctx->obj, &coords);
  }
  lv_unlock();
  if (!presented || coords.x1 < 0 || coords.y1 < 0 ||
      lv_area_get_width(&coords) != static_cast<int32_t>(ctx->width) ||
      lv_area_get_height(&coords) != static_cast<int32_t>(ctx->height)) {
    lottie_log_direct_publish_failure(ctx, "object-not-presented");
    return false;
  }

  auto *submitted_source = ctx->work_buffer;
  const bool submitted_internal = ctx->work_buffer_internal;
  const int stride = static_cast<int>(lv_draw_buf_width_to_stride(ctx->render_width, LV_COLOR_FORMAT_XRGB8888));
  lottie_wait_display_fifo();
  lottie_set_direct_frame_in_flight(ctx, true);
  const uint8_t result = lvgl_esphome_direct_blit_xrgb8888_async(
      submitted_source, stride, coords.x1, coords.y1, ctx->width, ctx->height, lottie_direct_frame_ready, ctx);
  if (result != LOTTIE_DIRECT_BLIT_SUBMITTED) {
    lottie_log_direct_publish_failure(ctx, result == LOTTIE_DIRECT_BLIT_BUSY ? "queue-busy" : "api-rejected", result);
    lottie_set_direct_frame_in_flight(ctx, false);
    return false;
  }

  ctx->direct_published_source = submitted_source;
  ctx->direct_region_active = true;
  ctx->direct_region_x = coords.x1;
  ctx->direct_region_y = coords.y1;
  ctx->direct_region_width = ctx->width;
  ctx->direct_region_height = ctx->height;
  ctx->work_buffer = ctx->work_buffer_alt;
  ctx->work_buffer_internal = ctx->work_buffer_alt_internal;
  ctx->work_buffer_alt = submitted_source;
  ctx->work_buffer_alt_internal = submitted_internal;
  if (hidden_but_ready_to_reveal) {
    lv_lock();
    if (ctx->obj != nullptr && lv_obj_is_valid(ctx->obj) && !ctx->runtime_hidden && ctx->auto_start)
      lv_obj_clear_flag(ctx->obj, LV_OBJ_FLAG_HIDDEN);
    lv_unlock();
  }
  return true;
}

inline bool lottie_publish_work_buffer(LottieContext *ctx) {
  if (ctx == nullptr || ctx->work_buffer == nullptr || ctx->pixel_buffer == nullptr)
    return false;

  if (lottie_uses_direct_opaque_pipeline(ctx) && !ctx->runtime_hidden)
    return lottie_publish_direct_opaque_frame(ctx);

  if (lottie_uses_direct_scaled_argb(ctx) && !ctx->runtime_hidden) {
    // The fast weather scene is already a compact straight-ARGB surface.
    // Scale only its stable dirty rectangle into a small SRAM band and blend
    // that band straight into the next DSI framebuffer. A failed/busy submit
    // is reported to the render task, which retries the latest timeline frame;
    // falling back to a native 484x484 LVGL draw would reintroduce the PSRAM
    // burst this path removes.
    return lottie_publish_direct_scaled_frame(ctx);
  }

  if (lottie_uses_scaled_render(ctx)) {
    uint8_t *source = ctx->work_buffer;
    uint8_t *destination = ctx->display_back_buffer;
    if (source == nullptr || destination == nullptr)
      return false;

    // The destination is the buffer that was previously presented by LVGL.
    // Wait for the active DSI scanout before reusing it, then let PPA scale
    // without holding LVGL's global lock. The old code held that lock across
    // a full-frame 256->484 scale, which serialized touch, layout and every
    // unrelated LVGL animation behind the weather frame.
    lottie_wait_display_fifo();
    if (!lvgl_esphome_scale_argb8888_dma_chain(source, ctx->render_width, ctx->render_height, destination, ctx->width,
                                               ctx->height)) {
      return false;
    }

    if (ctx->stop_requested || ctx->work_buffer != source || ctx->display_back_buffer != destination)
      return false;
    return lottie_request_native_publication(ctx, destination, true);
  }

  // Publish the rendered buffer directly and turn the old displayed buffer
  // into the next render target. lottie_render_frame() waits for DSI before
  // that target is reused, so this remains a real tear-free double buffer.
  return lottie_request_native_publication(ctx, ctx->work_buffer, false);
}

// ThorVG produces premultiplied BGRA. Flattening it once onto a known solid
// background turns the repeatedly rendered LVGL source into opaque RGB888.
// That lets PPA copy each frame directly instead of software-blending every
// pixel whenever LVGL refreshes the widget.
inline void lottie_flatten_opaque_frame(LottieContext *ctx, const uint8_t *src, uint8_t *dst) {
  const uint32_t src_stride = lv_draw_buf_width_to_stride(ctx->width, LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED);
  const uint32_t dst_stride = lv_draw_buf_width_to_stride(ctx->width, LV_COLOR_FORMAT_RGB888);
  const uint8_t bg_b = ctx->opaque_background.blue;
  const uint8_t bg_g = ctx->opaque_background.green;
  const uint8_t bg_r = ctx->opaque_background.red;
  const bool black_background = (bg_b | bg_g | bg_r) == 0;

  for (uint32_t y = 0; y < ctx->height; y++) {
    const uint8_t *src_px = src + static_cast<size_t>(y) * src_stride;
    uint8_t *dst_px = dst + static_cast<size_t>(y) * dst_stride;
    if (black_background) {
      for (uint32_t x = 0; x < ctx->width; x++, src_px += 4, dst_px += 3) {
        dst_px[0] = src_px[0];
        dst_px[1] = src_px[1];
        dst_px[2] = src_px[2];
      }
      continue;
    }

    for (uint32_t x = 0; x < ctx->width; x++, src_px += 4, dst_px += 3) {
      const uint16_t inv_alpha = 255U - src_px[3];
      dst_px[0] = static_cast<uint8_t>(std::min<uint16_t>(255U, src_px[0] + (inv_alpha * bg_b + 127U) / 255U));
      dst_px[1] = static_cast<uint8_t>(std::min<uint16_t>(255U, src_px[1] + (inv_alpha * bg_g + 127U) / 255U));
      dst_px[2] = static_cast<uint8_t>(std::min<uint16_t>(255U, src_px[2] + (inv_alpha * bg_r + 127U) / 255U));
    }
  }
}

inline void lottie_attach_opaque_buffer(LottieContext *ctx) {
  const lv_color_format_t cf = lottie_uses_direct_xrgb(ctx) ? LV_COLOR_FORMAT_XRGB8888 : LV_COLOR_FORMAT_RGB888;
  lv_canvas_set_buffer(ctx->obj, ctx->pixel_buffer, ctx->width, ctx->height, cf);
  lv_draw_buf_t *draw_buf = lv_canvas_get_draw_buf(ctx->obj);
  if (draw_buf != nullptr) {
    lv_draw_buf_clear_flag(draw_buf, LV_IMAGE_FLAGS_PREMULTIPLIED);
  }
}

// Contract an opaque XRGB8888 frame to LVGL's native RGB888 byte layout in
// place. The destination advances more slowly than the source, so a forward
// pass cannot overwrite unread pixels. The allocation remains four bytes per
// pixel and can be reused as a ThorVG render target on the next playback.
inline bool lottie_copy_xrgb8888_to_rgb888(LottieContext *ctx, const uint8_t *source, uint8_t *target) {
  if (ctx == nullptr || source == nullptr || target == nullptr || source == target)
    return false;
  if (lvgl_esphome_copy_xrgb8888_to_rgb888(source, target, ctx->width, ctx->height))
    return true;

  const uint32_t source_stride = lv_draw_buf_width_to_stride(ctx->width, LV_COLOR_FORMAT_XRGB8888);
  const uint32_t target_stride = lv_draw_buf_width_to_stride(ctx->width, LV_COLOR_FORMAT_RGB888);
  for (uint32_t y = 0; y < ctx->height; y++) {
    const auto *source_pixel =
        reinterpret_cast<const uint32_t *>(source + static_cast<size_t>(y) * source_stride);
    uint8_t *target_pixel = target + static_cast<size_t>(y) * target_stride;
    for (uint32_t x = 0; x < ctx->width; x++) {
      const uint32_t pixel = source_pixel[x];
      *target_pixel++ = static_cast<uint8_t>(pixel);
      *target_pixel++ = static_cast<uint8_t>(pixel >> 8);
      *target_pixel++ = static_cast<uint8_t>(pixel >> 16);
    }
  }
  lottie_sync_buffer(target, static_cast<size_t>(target_stride) * ctx->height);
  return true;
}

inline bool lottie_wait_direct_frame_buffer(LottieContext *ctx, uint32_t timeout_ms) {
  if (ctx == nullptr || (!lottie_renders_to_display_back_buffer(ctx) && !lottie_uses_direct_scaled_argb(ctx) &&
                         !lottie_uses_direct_opaque_pipeline(ctx)))
    return true;

  const int64_t deadline_us = timeout_ms == 0 ? 0 : esp_timer_get_time() + static_cast<int64_t>(timeout_ms) * 1000;
  while (lottie_direct_frame_in_flight(ctx)) {
    TickType_t wait_ticks = pdMS_TO_TICKS(20);
    if (deadline_us != 0) {
      const int64_t remaining_us = deadline_us - esp_timer_get_time();
      if (remaining_us <= 0)
        return false;
      wait_ticks = pdMS_TO_TICKS(static_cast<uint32_t>(std::max<int64_t>(1, (remaining_us + 999) / 1000)));
      wait_ticks = std::min<TickType_t>(wait_ticks, pdMS_TO_TICKS(20));
    }
    ulTaskNotifyTake(pdTRUE, std::max<TickType_t>(1, wait_ticks));
  }
  return true;
}

inline void lottie_release_direct_region(LottieContext *ctx) {
  if (ctx == nullptr)
    return;
  if (ctx->direct_region_active)
    lvgl_esphome_direct_blit_rgb888_release(ctx->direct_region_x, ctx->direct_region_y, ctx->direct_region_width,
                                          ctx->direct_region_height);
  ctx->direct_region_active = false;
  if (ctx->direct_background != nullptr) {
    heap_caps_free(ctx->direct_background);
    ctx->direct_background = nullptr;
    ctx->direct_background_size = 0;
  }
}

inline void lottie_log_direct_publish_failure(LottieContext *ctx, const char *reason, uint8_t result) {
  if (ctx == nullptr)
    return;
  ctx->direct_publish_diag_rejects++;
  const uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
  if (ctx->direct_publish_diag_last_ms != 0 && now_ms - ctx->direct_publish_diag_last_ms < 1000U)
    return;
  ctx->direct_publish_diag_last_ms = now_ms;
  ESP_LOGW(LOTTIE_PERF_TAG,
           "direct publish rejected ctx=%p reason=%s result=%u rejects=%u work=%p runtime_hidden=%u auto=%u "
           "dirty=%d,%d..%d,%d display=%ux%u render=%ux%u",
           ctx, reason == nullptr ? "unknown" : reason, static_cast<unsigned>(result),
           static_cast<unsigned>(ctx->direct_publish_diag_rejects), ctx->work_buffer,
           static_cast<unsigned>(ctx->runtime_hidden), static_cast<unsigned>(ctx->auto_start),
           static_cast<int>(ctx->fast_radial_dirty_x1), static_cast<int>(ctx->fast_radial_dirty_y1),
           static_cast<int>(ctx->fast_radial_dirty_x2), static_cast<int>(ctx->fast_radial_dirty_y2),
           static_cast<unsigned>(ctx->width), static_cast<unsigned>(ctx->height),
           static_cast<unsigned>(ctx->render_width), static_cast<unsigned>(ctx->render_height));
}

inline bool lottie_publish_direct_scaled_frame(LottieContext *ctx) {
  if (ctx == nullptr) {
    return false;
  }
  const char *reject_reason = nullptr;
  if (ctx->work_buffer == nullptr)
    reject_reason = "no-work-buffer";
  else if (lottie_direct_frame_in_flight(ctx))
    reject_reason = "frame-in-flight";
  else if (ctx->runtime_hidden)
    reject_reason = "runtime-hidden";
  else if (!lottie_uses_direct_scaled_argb(ctx))
    reject_reason = "direct-scaled-disabled";
  else if (ctx->fast_radial_dirty_x2 < ctx->fast_radial_dirty_x1 ||
           ctx->fast_radial_dirty_y2 < ctx->fast_radial_dirty_y1)
    reject_reason = "empty-dirty-region";
  if (reject_reason != nullptr) {
    lottie_log_direct_publish_failure(ctx, reject_reason);
    return false;
  }

  lv_area_t coords{};
  bool presented = false;
  lv_lock();
  if (ctx->obj != nullptr && lv_obj_is_valid(ctx->obj)) {
    lv_obj_get_coords(ctx->obj, &coords);
    presented = lv_obj_get_screen(ctx->obj) == lv_screen_active();
  }
  lv_unlock();
  if (!presented) {
    lottie_log_direct_publish_failure(ctx, "object-not-presented");
    return false;
  }

  const int integer_scale = static_cast<int>(ctx->width / ctx->render_width);
  int source_x = ctx->fast_radial_dirty_x1;
  int source_y = ctx->fast_radial_dirty_y1;
  int source_width = ctx->fast_radial_dirty_x2 - ctx->fast_radial_dirty_x1 + 1;
  int source_height = ctx->fast_radial_dirty_y2 - ctx->fast_radial_dirty_y1 + 1;
  int x = coords.x1 + source_x * integer_scale;
  int y = coords.y1 + source_y * integer_scale;
  int width = source_width * integer_scale;
  int height = source_height * integer_scale;
  lv_display_t *display = lv_display_get_default();
  const int display_width = display == nullptr ? 0 : lv_display_get_horizontal_resolution(display);
  const int display_height = display == nullptr ? 0 : lv_display_get_vertical_resolution(display);
  if (display == nullptr || display_width <= 0 || display_height <= 0) {
    lottie_log_direct_publish_failure(ctx, "no-display", 0);
    return false;
  }

  // The weather artwork is intentionally allowed to bleed past the circular
  // face.  Convert the visible DSI intersection back to source pixels before
  // submitting PPA; rejecting the whole dirty rectangle made the one-shot
  // renderer stall forever as soon as a ray touched an edge.
  const int original_x = x;
  const int original_y = y;
  const int original_width = width;
  const int original_height = height;
  const int visible_x1 = std::max(0, x);
  const int visible_y1 = std::max(0, y);
  const int visible_x2 = std::min(display_width, x + width);
  const int visible_y2 = std::min(display_height, y + height);
  if (visible_x1 >= visible_x2 || visible_y1 >= visible_y2) {
    lottie_log_direct_publish_failure(ctx, "region-out-of-bounds");
    return false;
  }

  const int left_source_clip = std::max(0, (visible_x1 - x + integer_scale - 1) / integer_scale);
  const int top_source_clip = std::max(0, (visible_y1 - y + integer_scale - 1) / integer_scale);
  const int right_source_clip = std::max(0, (x + width - visible_x2 + integer_scale - 1) / integer_scale);
  const int bottom_source_clip = std::max(0, (y + height - visible_y2 + integer_scale - 1) / integer_scale);
  source_x += left_source_clip;
  source_y += top_source_clip;
  source_width -= left_source_clip + right_source_clip;
  source_height -= top_source_clip + bottom_source_clip;
  if (source_width <= 0 || source_height <= 0) {
    lottie_log_direct_publish_failure(ctx, "empty-visible-region");
    return false;
  }
  x = coords.x1 + source_x * integer_scale;
  y = coords.y1 + source_y * integer_scale;
  width = source_width * integer_scale;
  height = source_height * integer_scale;
  if (x < 0 || y < 0 || x + width > display_width || y + height > display_height) {
    lottie_log_direct_publish_failure(ctx, "region-clip-rounding", 0);
    return false;
  }
  if (x != original_x || y != original_y || width != original_width || height != original_height) {
    static bool clip_logged = false;
    if (!clip_logged) {
      ESP_LOGI(LOTTIE_PERF_TAG,
               "direct weather region clipped ctx=%p object=%d,%d dirty=%d,%d %dx%d visible=%d,%d %dx%d display=%dx%d",
               ctx, (int) coords.x1, (int) coords.y1, original_x, original_y, original_width, original_height, x, y,
               width, height, display_width, display_height);
      clip_logged = true;
    }
  }

  if (ctx->direct_region_active &&
      (ctx->direct_region_x != x || ctx->direct_region_y != y || ctx->direct_region_width != width ||
       ctx->direct_region_height != height)) {
    lottie_release_direct_region(ctx);
  }

  uint8_t *submitted_source = ctx->work_buffer;
  const bool submitted_source_internal = ctx->work_buffer_internal;
  const int stride = static_cast<int>(lv_draw_buf_width_to_stride(ctx->render_width, LV_COLOR_FORMAT_ARGB8888));
  if (ctx->direct_background == nullptr) {
    const int64_t capture_started_us = esp_timer_get_time();
    const size_t bytes = lottie_align_up(static_cast<size_t>(width) * height * 3U, LOTTIE_CACHE_ALIGN);
    auto *background = static_cast<uint8_t *>(heap_caps_aligned_alloc(
        LOTTIE_CACHE_ALIGN, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (background == nullptr) {
      lottie_log_direct_publish_failure(ctx, "background-allocation");
      return false;
    }
    // Fast-radial weather is rendered on the black home surface. Do not call
    // LVGL's draw dispatcher from the animation task: even with lv_lock(),
    // that would run a temporary layer concurrently with the dispatcher
    // lifecycle owned by the LVGL task and can corrupt its sync primitive.
    // Preparing the known-opaque backdrop directly also removes a full
    // off-screen LVGL render from the first weather frame.
    memset(background, 0, bytes);
    lottie_sync_buffer(background, bytes);
    ctx->direct_background = background;
    ctx->direct_background_size = bytes;
    ctx->start_time_us += esp_timer_get_time() - capture_started_us;
  }
  lottie_set_direct_frame_in_flight(ctx, true);
  uint8_t result = LOTTIE_DIRECT_BLIT_REJECTED;
  if (integer_scale == 1 && ctx->width == ctx->render_width && ctx->height == ctx->render_height) {
    // At 1:1 there is no reason to invoke the scaler. The blend-only request
    // also lets the compositor sync and read just the dirty source rectangle.
    result = lvgl_esphome_direct_blend_argb8888_async(
        ctx->direct_background, width * 3, submitted_source, stride, static_cast<int>(ctx->render_width),
        static_cast<int>(ctx->render_height), source_x, source_y, x, y, width, height, lottie_direct_frame_ready, ctx);
  } else {
    result = lvgl_esphome_direct_scale_blend_argb8888_async(
        submitted_source, stride, ctx->render_width, ctx->render_height, source_x, source_y, source_width, source_height,
        integer_scale, x, y, ctx->pixel_buffer, lottie_align_up(lottie_display_buffer_bytes(ctx), LOTTIE_CACHE_ALIGN),
        static_cast<int>(ctx->width), static_cast<int>(ctx->height), ctx->direct_background, width * 3,
        lottie_direct_frame_ready, ctx);
  }
  if (result != LOTTIE_DIRECT_BLIT_SUBMITTED) {
    lottie_log_direct_publish_failure(ctx, result == LOTTIE_DIRECT_BLIT_BUSY ? "queue-busy" : "api-rejected", result);
    lottie_set_direct_frame_in_flight(ctx, false);
    return false;
  }

  __atomic_add_fetch(&ctx->direct_publish_diag_submits, 1U, __ATOMIC_RELAXED);
  ctx->direct_published_source = submitted_source;

  ctx->direct_region_active = true;
  ctx->direct_region_x = x;
  ctx->direct_region_y = y;
  ctx->direct_region_width = width;
  ctx->direct_region_height = height;

  // The compositor owns submitted_source until lottie_direct_frame_ready().
  // Rotate the producer pointer immediately so the renderer can prepare the
  // next frame while that PPA transaction is still in flight. With no spare
  // buffer the old serialized wait path remains unchanged.
  if (ctx->work_buffer_alt != nullptr) {
    ctx->work_buffer = ctx->work_buffer_alt;
    ctx->work_buffer_internal = ctx->work_buffer_alt_internal;
    ctx->work_buffer_alt = submitted_source;
    ctx->work_buffer_alt_internal = submitted_source_internal;
  }
  return true;
}

// Fast radial weather is presented as a compact ARGB source scaled directly
// into DSI.  Once a finite animation ends, however, the regional lease is
// released and ordinary LVGL redraws (snapshots, page transitions, touch
// invalidation) read the canvas.  The canvas used to remain bound to the
// compact dimensions, which exposed a half-size/stale frame.  Materialize
// the final compact frame into the already allocated full-size canvas with
// PPA, then attach that canvas exactly once.  No additional image buffer is
// allocated and the live animation path remains regional.
inline bool lottie_commit_direct_scaled_frame_to_canvas(LottieContext *ctx) {
  if (ctx == nullptr || !lottie_uses_direct_scaled_argb(ctx) || ctx->pixel_buffer == nullptr ||
      !lottie_wait_direct_frame_buffer(ctx, 150)) {
    return false;
  }

  uint8_t *source = ctx->direct_published_source;
  if (source == nullptr)
    source = ctx->work_buffer_alt != nullptr ? ctx->work_buffer_alt : ctx->work_buffer;
  if (source == nullptr || source == ctx->pixel_buffer) {
    ESP_LOGW(LOTTIE_PERF_TAG, "finite scaled frame retain: no stable source");
    return false;
  }

  if (!lvgl_esphome_scale_argb8888(source, static_cast<int>(ctx->render_width),
                                   static_cast<int>(ctx->render_height), ctx->pixel_buffer,
                                   static_cast<int>(ctx->width), static_cast<int>(ctx->height))) {
    ESP_LOGW(LOTTIE_PERF_TAG, "finite scaled frame retain: PPA materialization failed");
    return false;
  }

  lv_display_t *display = nullptr;
  lv_lock();
  if (ctx->obj != nullptr && lv_obj_is_valid(ctx->obj)) {
    lv_canvas_set_buffer(ctx->obj, ctx->pixel_buffer, ctx->width, ctx->height, lottie_argb_color_format(ctx));
    lv_draw_buf_t *draw_buf = lv_canvas_get_draw_buf(ctx->obj);
    if (draw_buf != nullptr)
      lv_draw_buf_clear_flag(draw_buf, LV_IMAGE_FLAGS_PREMULTIPLIED);
    lv_obj_invalidate(ctx->obj);
    display = lv_obj_get_display(ctx->obj);
  }
  lv_unlock();

  lottie_release_direct_region(ctx);
  ctx->direct_published_source = nullptr;
  if (display != nullptr && !lvgl_esphome_snapshot_is_active()) {
    if (!lvgl_esphome_direct_handoff_to_lvgl(100))
      ESP_LOGW(LOTTIE_PERF_TAG, "finite scaled frame handoff timed out");
    lv_lock();
    lv_refr_now(display);
    lv_unlock();
  }
  ESP_LOGI(LOTTIE_PERF_TAG, "finite scaled frame retained: source=%p canvas=%p size=%ux%u", source,
           ctx->pixel_buffer, static_cast<unsigned>(ctx->width), static_cast<unsigned>(ctx->height));
  return true;
}

inline bool lottie_publish_direct_frame(LottieContext *ctx) {
  if (ctx == nullptr || ctx->display_back_buffer == nullptr || lottie_direct_frame_in_flight(ctx))
    return false;

  lv_area_t coords{};
  bool visible = false;
  lv_lock();
  if (ctx->obj != nullptr && lv_obj_is_valid(ctx->obj)) {
    visible = lv_obj_is_visible(ctx->obj);
    lv_obj_get_coords(ctx->obj, &coords);
  }
  lv_unlock();

  const bool exact_region = visible && lv_area_get_width(&coords) == static_cast<int32_t>(ctx->width) &&
                            lv_area_get_height(&coords) == static_cast<int32_t>(ctx->height);
  if (!exact_region || coords.x1 < 0 || coords.y1 < 0) {
    lottie_release_direct_region(ctx);
    return false;
  }

  if (ctx->direct_region_active && (ctx->direct_region_x != coords.x1 || ctx->direct_region_y != coords.y1)) {
    lottie_release_direct_region(ctx);
  }

  const int stride = static_cast<int>(lv_draw_buf_width_to_stride(ctx->width, LV_COLOR_FORMAT_XRGB8888));
  lottie_set_direct_frame_in_flight(ctx, true);
  const uint8_t result = lvgl_esphome_direct_blit_xrgb8888_async(
      ctx->display_back_buffer, stride, coords.x1, coords.y1, ctx->width, ctx->height, lottie_direct_frame_ready, ctx);
  if (result != LOTTIE_DIRECT_BLIT_SUBMITTED) {
    lottie_set_direct_frame_in_flight(ctx, false);
    return false;
  }

  ctx->direct_region_active = true;
  ctx->direct_region_x = coords.x1;
  ctx->direct_region_y = coords.y1;
  ctx->direct_region_width = ctx->width;
  ctx->direct_region_height = ctx->height;
  return true;
}

inline void lottie_publish_opaque_frame(LottieContext *ctx) {
  if (ctx == nullptr || ctx->pixel_buffer == nullptr) {
    return;
  }

  if (lottie_renders_to_display_back_buffer(ctx)) {
    lottie_publish_direct_frame(ctx);
    return;
  }

  // ThorVG's premultiplied RGB channels already contain the final pixels for
  // a black background. Expose the complete render buffer as opaque XRGB and
  // let the existing LVGL PPA SRM draw unit convert+copy it to RGB888 in one
  // hardware pass. This removes the intermediate RGB frame and a second
  // full-frame PSRAM traversal from every animation frame.
  if (lottie_uses_direct_xrgb(ctx)) {
    if (ctx->display_back_buffer == nullptr) {
      return;
    }
    if (ctx->work_buffer != nullptr && !lottie_copy_argb_frame(ctx, ctx->work_buffer, ctx->display_back_buffer)) {
      return;
    }
    uint8_t *ready = ctx->display_back_buffer;
    const bool ready_internal = ctx->display_back_buffer_internal;

    lv_lock();
    lv_canvas_set_buffer(ctx->obj, ready, ctx->width, ctx->height, LV_COLOR_FORMAT_XRGB8888);
    lv_draw_buf_t *draw_buf = lv_canvas_get_draw_buf(ctx->obj);
    if (draw_buf != nullptr) {
      lv_draw_buf_clear_flag(draw_buf, LV_IMAGE_FLAGS_PREMULTIPLIED);
    }
    lv_obj_invalidate(ctx->obj);
    lv_unlock();

    ctx->display_back_buffer = ctx->pixel_buffer;
    ctx->display_back_buffer_internal = ctx->pixel_buffer_internal;
    ctx->pixel_buffer = ready;
    ctx->pixel_buffer_internal = ready_internal;
    return;
  }

  if (ctx->work_buffer == nullptr || ctx->display_back_buffer == nullptr)
    return;

  // Conversion and cache maintenance touch the whole frame. Keep both off
  // the LVGL lock so input, timers, and unrelated widgets can continue on
  // the UI core while core 0 prepares the next opaque frame.
  const size_t display_bytes = lottie_display_buffer_bytes(ctx);
  const size_t display_alloc_bytes = lottie_align_up(display_bytes, LOTTIE_CACHE_ALIGN);
  lottie_flatten_opaque_frame(ctx, ctx->work_buffer, ctx->display_back_buffer);
  lottie_sync_buffer(ctx->display_back_buffer, display_bytes);

  uint8_t *ready = ctx->display_back_buffer;
  const bool ready_internal = ctx->display_back_buffer_internal;

  // Acquiring the LVGL lock also guarantees that the previous source is no
  // longer being consumed by an active draw. Publishing is then just a
  // pointer swap; LVGL never observes a partially converted frame.
  lv_lock();
  lv_canvas_set_buffer(ctx->obj, ready, ctx->width, ctx->height, LV_COLOR_FORMAT_RGB888);
  lv_draw_buf_t *draw_buf = lv_canvas_get_draw_buf(ctx->obj);
  if (draw_buf != nullptr) {
    lv_draw_buf_clear_flag(draw_buf, LV_IMAGE_FLAGS_PREMULTIPLIED);
  }
  lv_obj_invalidate(ctx->obj);
  lv_unlock();

  ctx->display_back_buffer = ctx->pixel_buffer;
  ctx->display_back_buffer_internal = ctx->pixel_buffer_internal;
  ctx->pixel_buffer = ready;
  ctx->pixel_buffer_internal = ready_internal;
}

// A looping opaque Lottie normally presents frames through the direct PPA
// region while LVGL retains an immutable canvas source. Finite playback must
// hand the last direct frame back to that canvas before the region is released;
// otherwise snapshots and later LVGL refreshes see the initial frame instead.
inline bool lottie_commit_direct_frame_to_canvas(LottieContext *ctx) {
  const bool direct_pipeline = lottie_uses_direct_opaque_pipeline(ctx);
  if (ctx == nullptr || (!lottie_renders_to_display_back_buffer(ctx) && !direct_pipeline) ||
      ctx->pixel_buffer == nullptr || (!direct_pipeline && ctx->display_back_buffer == nullptr) ||
      !lottie_wait_direct_frame_buffer(ctx, 150)) {
    return false;
  }

  // Render the retained frame into the existing direct source, then promote
  // that complete buffer to the LVGL canvas. Re-attaching the old canvas
  // pointer allowed LVGL/PPA to reuse its stale pre-animation source state.
  // Pointer promotion is atomic under the LVGL lock and needs no full-frame
  // copy or additional allocation.
  const int32_t retained_frame = ctx->retained_frame >= lottie_first_renderable_frame(ctx)
                                     ? ctx->retained_frame
                                     : lottie_last_renderable_frame(ctx);
  uint8_t *render_target = direct_pipeline
                               ? (ctx->direct_published_source != nullptr ? ctx->direct_published_source
                                                                           : ctx->work_buffer)
                               : ctx->display_back_buffer;
  if (render_target == nullptr)
    return false;
  if (!lottie_render_frame(ctx, retained_frame, render_target) ||
      !lottie_copy_xrgb8888_to_rgb888(ctx, render_target, ctx->pixel_buffer))
    return false;
  // lottie_render_frame() intentionally skips cache maintenance for opaque
  // direct-XRGB playback because the PPA presentation path owns that sync.
  // This buffer is about to become an ordinary LVGL canvas source instead,
  // so publish the CPU-written final frame before LVGL/PPA can read it.
  ESP_LOGW(LOTTIE_PERF_TAG, "finite frame retain: frame=%d coverage=%u final=%d target=%p canvas=%p",
           (int) retained_frame, (unsigned) ctx->retained_coverage, (int) lottie_last_renderable_frame(ctx),
           render_target, ctx->pixel_buffer);

  lv_display_t *display = nullptr;
  lv_lock();
  if (ctx->obj != nullptr && lv_obj_is_valid(ctx->obj)) {
    lv_canvas_set_buffer(ctx->obj, ctx->pixel_buffer, ctx->width, ctx->height, LV_COLOR_FORMAT_RGB888);
    lv_draw_buf_t *draw_buf = lv_canvas_get_draw_buf(ctx->obj);
    if (draw_buf != nullptr)
      lv_draw_buf_clear_flag(draw_buf, LV_IMAGE_FLAGS_PREMULTIPLIED);
    lv_obj_invalidate(ctx->obj);
    display = lv_obj_get_display(ctx->obj);
  }
  lv_unlock();

  lottie_release_direct_region(ctx);
  // A full-screen snapshot transition already owns presentation and the DSI
  // framebuffer set. Keep the retained Lottie canvas current, but do not let
  // finite-playback cleanup perform a second handoff or refresh while the
  // transition worker is composing a frame with DMA2D.
  if (display != nullptr && !lvgl_esphome_snapshot_is_active()) {
    if (!lvgl_esphome_direct_handoff_to_lvgl(100))
      ESP_LOGW(LOTTIE_PERF_TAG, "final direct frame handoff timed out");
    lv_lock();
    lv_refr_now(display);
    lv_unlock();
  }
  return true;
}

inline uint8_t *lottie_alloc_pixel_buffer(size_t alloc_bytes, bool *internal) {
  if (internal != nullptr) {
    *internal = false;
  }

  if (alloc_bytes <= LOTTIE_INTERNAL_BUFFER_MAX_BYTES) {
    size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (largest_internal >= alloc_bytes + LOTTIE_INTERNAL_BUFFER_HEADROOM_BYTES) {
      uint8_t *buf = (uint8_t *) heap_caps_aligned_alloc(LOTTIE_CACHE_ALIGN, alloc_bytes,
                                                         MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
      if (buf != nullptr) {
        if (internal != nullptr) {
          *internal = true;
        }
        return buf;
      }
    }
  }

  return (uint8_t *) heap_caps_aligned_alloc(LOTTIE_CACHE_ALIGN, alloc_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

inline bool lottie_prepare_frame_cache(LottieContext *ctx) {
  if (ctx == nullptr || ctx->loop || !ctx->flatten_to_opaque || !lottie_uses_direct_xrgb(ctx) ||
      ctx->duration_ms == 0 || ctx->end_frame <= ctx->start_frame || ctx->work_buffer == nullptr) {
    return false;
  }

  const int32_t first_frame = lottie_first_renderable_frame(ctx);
  const int32_t last_frame = lottie_last_renderable_frame(ctx);
  if (last_frame < first_frame) {
    return false;
  }
  const int32_t total_frames = last_frame - first_frame;
  const uint32_t target_frames =
      std::max<uint32_t>(2, (ctx->duration_ms * LOTTIE_FRAME_CACHE_TARGET_FPS + 999U) / 1000U + 1U);
  const uint32_t frame_count = std::min<uint32_t>(static_cast<uint32_t>(total_frames) + 1U, target_frames);
  const size_t frame_stride = lottie_align_up(lottie_display_buffer_bytes(ctx), LOTTIE_CACHE_ALIGN);
  const size_t cache_bytes = frame_stride * frame_count;

  if (cache_bytes > LOTTIE_FRAME_CACHE_MAX_BYTES) {
    ESP_LOGI(LOTTIE_PERF_TAG, "frame cache skipped: need=%u max=%u frames=%u stride=%u", (unsigned) cache_bytes,
             (unsigned) LOTTIE_FRAME_CACHE_MAX_BYTES, (unsigned) frame_count, (unsigned) frame_stride);
    return false;
  }
  if (heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) < cache_bytes) {
    ESP_LOGI(LOTTIE_PERF_TAG, "frame cache skipped: need=%u largest_psram=%u frames=%u stride=%u",
             (unsigned) cache_bytes, (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
             (unsigned) frame_count, (unsigned) frame_stride);
    return false;
  }

  uint8_t *cache = static_cast<uint8_t *>(
      heap_caps_aligned_alloc(LOTTIE_CACHE_ALIGN, cache_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (cache == nullptr) {
    return false;
  }

  const size_t display_bytes = lottie_display_buffer_bytes(ctx);
  if (ctx->pixel_buffer != nullptr) {
    lottie_wait_display_fifo();
    // The native canvas is packed RGB888, while every cached frame is XRGB8888.
    // Seed slot zero from the prepared render target, just like all later slots.
    if (!lottie_copy_argb_frame(ctx, lottie_initial_render_target(ctx), cache)) {
      heap_caps_free(cache);
      return false;
    }
    lottie_wait_display_fifo();
  }

  for (uint32_t i = 1; i < frame_count; i++) {
    const int32_t frame =
        first_frame +
        static_cast<int32_t>((static_cast<int64_t>(total_frames) * i + (frame_count - 1) / 2) / (frame_count - 1));
    uint8_t *target = cache + frame_stride * i;
    if (!lottie_render_frame(ctx, frame, ctx->work_buffer) || !lottie_copy_argb_frame(ctx, ctx->work_buffer, target)) {
      heap_caps_free(cache);
      return false;
    }
    lottie_wait_display_fifo();
    taskYIELD();
  }

  ctx->frame_cache = cache;
  ctx->frame_cache_stride = frame_stride;
  ctx->frame_cache_count = frame_count;
  ctx->frame_cache_internal = false;
  ESP_LOGI(LOTTIE_PERF_TAG, "prepared frame cache: frames=%u bytes=%u first=%d last=%d", (unsigned) frame_count,
           (unsigned) cache_bytes, (int) first_frame, (int) last_frame);
  return true;
}

inline void lottie_publish_cached_frame(LottieContext *ctx, uint32_t index) {
  if (ctx == nullptr || ctx->frame_cache == nullptr || ctx->pixel_buffer == nullptr ||
      index >= ctx->frame_cache_count) {
    return;
  }
  uint8_t *frame = ctx->frame_cache + ctx->frame_cache_stride * index;
  const size_t display_bytes = lottie_display_buffer_bytes(ctx);

  // Cached frames are immutable and were written back when the cache was
  // prepared. Let PPA read the selected cache slot directly. The previous
  // path copied every frame into pixel_buffer and synchronized it again,
  // doubling PSRAM traffic during the visible boot animation.
  const int64_t started_us = esp_timer_get_time();
  lv_area_t coords{};
  bool visible = false;
  lv_lock();
  if (lv_obj_is_valid(ctx->obj)) {
    visible = lv_obj_is_visible(ctx->obj);
    lv_obj_get_coords(ctx->obj, &coords);
  }
  lv_unlock();

  bool presented_direct = false;
  if (visible && lv_area_get_width(&coords) == static_cast<int32_t>(ctx->width) &&
      lv_area_get_height(&coords) == static_cast<int32_t>(ctx->height)) {
    const int stride = static_cast<int>(lv_draw_buf_width_to_stride(ctx->width, LV_COLOR_FORMAT_XRGB8888));
    const uint8_t result = lvgl_esphome_direct_blit_xrgb8888_async(frame, stride, coords.x1, coords.y1, ctx->width,
                                                                   ctx->height, lottie_direct_frame_ready, nullptr);
    // A busy queue intentionally drops this immutable animation frame;
    // falling back to a native LVGL write would reintroduce concurrent
    // framebuffer ownership and costs more than waiting for the next one.
    presented_direct = result != LOTTIE_DIRECT_BLIT_REJECTED;
  }

  if (!presented_direct) {
    lv_lock();
    memcpy(ctx->pixel_buffer, frame, display_bytes);
    // The cache is XRGB8888. Reattach the canvas descriptor before invalidating
    // it; otherwise a transient direct-compositor rejection leaves LVGL
    // interpreting the four-byte frame as its previous packed format and the
    // first visible fallback frame becomes a gray rectangle.
    if (lv_obj_is_valid(ctx->obj)) {
      lottie_attach_opaque_buffer(ctx);
    }
    // The direct queue can be temporarily unavailable while boot snapshot or
    // display hand-off work owns the compositor. The fallback then becomes a
    // normal PPA/LVGL source, so publish the CPU-written cache lines before
    // invalidating the canvas. Without this sync the first visible frame could
    // be the stale allocation contents, seen as a dark rectangular flash.
    lottie_sync_buffer(ctx->pixel_buffer, display_bytes);
    if (lv_obj_is_valid(ctx->obj)) lv_obj_invalidate(ctx->obj);
    lv_unlock();
  }

  if (lvgl_esphome_get_perf_logging_enabled() != 0) {
    static uint32_t frames = 0;
    static uint32_t direct_frames = 0;
    static uint64_t total_us = 0;
    static uint32_t max_us = 0;
    static int64_t last_frame_us = 0;
    static uint32_t max_gap_us = 0;
    static uint32_t max_gap_index = 0;
    static int64_t window_us = 0;
    const int64_t now_us = esp_timer_get_time();
    const uint32_t elapsed_us = static_cast<uint32_t>(now_us - started_us);
    const uint32_t gap_us = last_frame_us == 0 ? 0 : static_cast<uint32_t>(now_us - last_frame_us);
    last_frame_us = now_us;
    if (gap_us > max_gap_us) {
      max_gap_us = gap_us;
      max_gap_index = index;
    }
    frames++;
    direct_frames += presented_direct ? 1U : 0U;
    total_us += elapsed_us;
    max_us = std::max(max_us, elapsed_us);
    if (window_us == 0)
      window_us = now_us;
    if (now_us - window_us >= 2000000) {
      ESP_LOGI(LOTTIE_PERF_TAG, "cache2s: frames=%u direct=%u avg=%uus max=%uus gap_max=%uus@%u",
               static_cast<unsigned>(frames), static_cast<unsigned>(direct_frames),
               static_cast<unsigned>(total_us / std::max<uint32_t>(1U, frames)), static_cast<unsigned>(max_us),
               static_cast<unsigned>(max_gap_us), static_cast<unsigned>(max_gap_index));
      frames = 0;
      direct_frames = 0;
      total_us = 0;
      max_us = 0;
      max_gap_us = 0;
      max_gap_index = 0;
      window_us = now_us;
    }
  }
}

// --------------------------------------------------------------------------
// Render task – runs on 64 KB PSRAM stack.
//
// First load:  set buffer → parse data → capture anim params → render loop
// Re-load:     clear canvas → set buffer (no re-parse) → render loop
//
// lv_lottie_set_buffer() MUST be called from this task (not from an LVGL
// event callback) because it internally triggers a ThorVG render that
// needs the large stack.
// --------------------------------------------------------------------------
inline void lottie_load_task(void *param) {
  LottieContext *ctx = (LottieContext *) param;

  // Wait for LVGL to settle after the page/overlay event.  The widget is
  // already hidden while loading, so a long first-load delay only makes the
  // animation appear as a visible pop after the overlay is on screen.
  vTaskDelay(pdMS_TO_TICKS(ctx->data_loaded ? 100 : 50));

  lv_lock();

  if (!ctx->data_loaded) {
    // ===== FIRST LOAD =====
    LV_LOG_INFO("First load: parsing lottie data...");

    // Set pixel buffer – this calls anim_exec_cb internally but since
    // no data is loaded yet ThorVG has nothing to render (safe).
    lv_lottie_set_buffer(ctx->obj, ctx->width, ctx->height, ctx->pixel_buffer);

    // Parse lottie data (heavy ThorVG work – needs 64 KB stack)
    if (ctx->data != nullptr) {
      lv_lottie_set_src_data(ctx->obj, ctx->data, ctx->data_size);
      LV_LOG_INFO("Data loaded from embedded source (%d bytes)", (int) ctx->data_size);
    } else if (ctx->file_path != nullptr) {
      lv_lottie_set_src_file(ctx->obj, ctx->file_path);
      LV_LOG_INFO("Data loaded from file: %s", ctx->file_path);
    }

    // Capture animation parameters before deleting the LVGL animation
    lv_anim_t *anim = lv_lottie_get_anim(ctx->obj);
    if (anim != nullptr) {
      ctx->exec_cb = anim->exec_cb;
      ctx->anim_var = anim->var;
      ctx->start_frame = anim->start_value;
      ctx->end_frame = anim->end_value;
      ctx->duration_ms = (uint32_t) lv_anim_get_time(anim);

      LV_LOG_INFO("Anim: frames %d..%d, duration %u ms", (int) ctx->start_frame, (int) ctx->end_frame,
                  (unsigned) ctx->duration_ms);

      // Delete the LVGL animation – we drive rendering ourselves
      // from this PSRAM task instead of the main task (small stack).
      lv_anim_delete(ctx->anim_var, ctx->exec_cb);

      // CRITICAL: null out the dangling pointer in lv_lottie_t.
      // Without this, anim_exec_cb (called by lv_lottie_set_buffer
      // on re-load) would dereference freed memory.
      lv_lottie_t *lottie = (lv_lottie_t *) ctx->obj;
      lottie->anim = NULL;

      ctx->data_loaded = true;
      LV_LOG_INFO("LVGL anim removed – rendering from PSRAM task");
    } else {
      LV_LOG_ERROR("Animation INVALID – parsing may have failed!");
    }
  } else {
    // ===== RE-LOAD (screen came back) =====
    // Data is already parsed in the lv_lottie widget.  We just need
    // to point ThorVG + LVGL canvas at the new pixel buffer.
    //
    // tvg_canvas_clear removes the paint from the canvas without
    // deleting it (false), so lv_lottie_set_buffer can push it again
    // without a double-push.
    LV_LOG_INFO("Re-load: updating buffer (no re-parse)");

    lv_lottie_t *lottie = (lv_lottie_t *) ctx->obj;
    tvg_canvas_clear(lottie->tvg_canvas, false);

    // Safe to call: widget is hidden (lv_obj_is_visible → false)
    // and lottie->anim is NULL (no dangling pointer access).
    lv_lottie_set_buffer(ctx->obj, ctx->width, ctx->height, ctx->pixel_buffer);
  }

  // Render the first frame before the object can become visible.  The pixel
  // buffer exists at this point, but without this draw LVGL can briefly flush
  // the blank buffer created in lottie_launch().
  if (ctx->data_loaded && ctx->exec_cb != nullptr && ctx->anim_var != nullptr && ctx->end_frame > ctx->start_frame) {
    ctx->exec_cb(ctx->anim_var, ctx->start_frame);
    lottie_sync_canvas_buffer(ctx);
    lv_obj_invalidate(ctx->obj);
  }

  const bool reveal_after_prepare = !ctx->runtime_hidden && ctx->data_loaded && ctx->exec_cb != nullptr &&
                                    ctx->anim_var != nullptr && ctx->end_frame > ctx->start_frame;

  lv_unlock();

  // Keep the object hidden for one LVGL tick after the heavy ThorVG parse and
  // first-frame render.  This prevents the first visible flush from racing the
  // buffer setup path, while preserving normal restart/update behaviour.
  if (reveal_after_prepare) {
    vTaskDelay(pdMS_TO_TICKS(32));
    lv_lock();
    if (!ctx->runtime_hidden) {
      lv_obj_remove_flag(ctx->obj, LV_OBJ_FLAG_HIDDEN);
      lv_obj_invalidate(ctx->obj);
    }
    lv_unlock();
  }

  // Validate animation parameters
  if (!ctx->data_loaded || ctx->exec_cb == nullptr || ctx->duration_ms == 0 || ctx->end_frame <= ctx->start_frame) {
    LV_LOG_WARN("No valid animation, task suspending");
    vTaskSuspend(NULL);
    return;
  }
  if (!ctx->auto_start) {
    LV_LOG_INFO("auto_start=false, task suspending");
    vTaskSuspend(NULL);
    return;
  }

  // --- Frame render loop (64 KB PSRAM stack) ---
  int32_t total_frames = ctx->end_frame - ctx->start_frame;
  uint32_t frame_delay_ms = ctx->duration_ms / (uint32_t) total_frames;
  if (frame_delay_ms < 16)
    frame_delay_ms = 16;  // Cap at ~60 fps
  if (frame_delay_ms > 100)
    frame_delay_ms = 100;

  LV_LOG_INFO("Render loop: %u ms/frame, loop=%d", (unsigned) frame_delay_ms, (int) ctx->loop);

  ctx->start_tick = xTaskGetTickCount();  // ✅ Store in context for restart capability
  uint32_t perf_frames = 0;
  uint64_t perf_render_us = 0;
  uint64_t perf_sync_us = 0;
  uint32_t perf_render_max_us = 0;
  uint32_t perf_sync_max_us = 0;
  int64_t perf_last_log_us = esp_timer_get_time();

  while (!ctx->stop_requested) {
    if (ctx->runtime_hidden) {
      if (ctx->restart_requested) {
        ctx->start_tick = xTaskGetTickCount();
        ctx->restart_requested = false;
        lv_lock();
        ctx->exec_cb(ctx->anim_var, ctx->start_frame);
        lottie_sync_canvas_buffer(ctx);
        lv_obj_invalidate(ctx->obj);
        lv_unlock();
      }
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    // ✅ Check if restart requested
    if (ctx->restart_requested) {
      ctx->start_tick = xTaskGetTickCount();
      ctx->restart_requested = false;
      LV_LOG_INFO("Animation restarted from frame 0");
    }

    uint32_t elapsed_ms = (uint32_t) ((xTaskGetTickCount() - ctx->start_tick) * portTICK_PERIOD_MS);

    int32_t frame;
    if (ctx->loop) {
      uint32_t phase = elapsed_ms % ctx->duration_ms;
      frame = ctx->start_frame + (int32_t) ((int64_t) total_frames * phase / ctx->duration_ms);
    } else {
      if (elapsed_ms >= ctx->duration_ms) {
        lv_lock();
        ctx->exec_cb(ctx->anim_var, lottie_last_renderable_frame(ctx));
        lottie_sync_canvas_buffer(ctx);
        lv_unlock();
        LV_LOG_INFO("Animation complete");
        break;
      }
      frame = ctx->start_frame + (int32_t) ((int64_t) total_frames * elapsed_ms / ctx->duration_ms);
    }

    lv_lock();
    bool perf_log_enabled = lvgl_esphome_get_perf_logging_enabled() != 0;
    int64_t render_start_us = perf_log_enabled ? esp_timer_get_time() : 0;
    ctx->exec_cb(ctx->anim_var, frame);
    int64_t sync_start_us = perf_log_enabled ? esp_timer_get_time() : 0;
    lottie_sync_canvas_buffer(ctx);
    int64_t sync_end_us = perf_log_enabled ? esp_timer_get_time() : 0;
    lv_unlock();

    if (perf_log_enabled) {
      uint32_t render_us = (uint32_t) (sync_start_us - render_start_us);
      uint32_t sync_us = (uint32_t) (sync_end_us - sync_start_us);
      perf_frames++;
      perf_render_us += render_us;
      perf_sync_us += sync_us;
      if (render_us > perf_render_max_us)
        perf_render_max_us = render_us;
      if (sync_us > perf_sync_max_us)
        perf_sync_max_us = sync_us;

      int64_t now_us = sync_end_us;
      if (now_us - perf_last_log_us >= 2000000 && perf_frames > 0) {
        LV_LOG_INFO("perf2s: frames=%u render_avg=%lluus render_max=%uus sync_avg=%lluus sync_max=%uus",
                    (unsigned) perf_frames, (unsigned long long) (perf_render_us / perf_frames),
                    (unsigned) perf_render_max_us, (unsigned long long) (perf_sync_us / perf_frames),
                    (unsigned) perf_sync_max_us);
        perf_frames = 0;
        perf_render_us = 0;
        perf_sync_us = 0;
        perf_render_max_us = 0;
        perf_sync_max_us = 0;
        perf_last_log_us = now_us;
      }
    } else if (perf_frames != 0) {
      perf_frames = 0;
      perf_render_us = 0;
      perf_sync_us = 0;
      perf_render_max_us = 0;
      perf_sync_max_us = 0;
      perf_last_log_us = esp_timer_get_time();
    }

    vTaskDelay(pdMS_TO_TICKS(frame_delay_ms));
  }

  if (ctx->stop_requested) {
    LV_LOG_INFO("Stop requested – task suspending");
  }

  // Suspend (NOT delete) – cleanup callback will delete us safely
  vTaskSuspend(NULL);
}

// Optimized renderer: ThorVG works on a private back buffer without holding
// the global LVGL lock. Looping opaque frames are handed to the regional PPA
// compositor while the LVGL canvas keeps one stable source for its lifetime.
inline void lottie_render_task(void *param) {
  auto *ctx = static_cast<LottieContext *>(param);
  vTaskDelay(pdMS_TO_TICKS(10));

  lv_lock();
  if (!ctx->data_loaded) {
    auto *lottie = reinterpret_cast<lv_lottie_t *>(ctx->obj);
    if (lottie->tvg_anim == nullptr) {
      lottie->tvg_anim = tvg_animation_new();
      lottie->tvg_paint = tvg_animation_get_picture(lottie->tvg_anim);
      lottie->tvg_canvas = tvg_swcanvas_create();
      if (lottie->tvg_anim == nullptr || lottie->tvg_paint == nullptr || lottie->tvg_canvas == nullptr) {
        tvg_animation_del(lottie->tvg_anim);
        tvg_canvas_destroy(lottie->tvg_canvas);
        lottie->tvg_anim = nullptr;
        lottie->tvg_paint = nullptr;
        lottie->tvg_canvas = nullptr;
        lv_unlock();
        LV_LOG_ERROR("Lottie scene allocation failed");
        vTaskSuspend(nullptr);
        return;
      }
      // Recreate only the native timing descriptor; the worker takes over
      // playback immediately after loading, as on the original first load.
      lv_anim_t animation;
      lv_anim_init(&animation);
      lv_anim_set_exec_cb(&animation, ctx->exec_cb);
      lv_anim_set_var(&animation, ctx->obj);
      lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
      lottie->anim = lv_anim_start(&animation);
      if (lottie->anim == nullptr) {
        lv_unlock();
        tvg_animation_del(lottie->tvg_anim);
        tvg_canvas_destroy(lottie->tvg_canvas);
        lottie->tvg_anim = nullptr;
        lottie->tvg_paint = nullptr;
        lottie->tvg_canvas = nullptr;
        LV_LOG_ERROR("Lottie animation allocation failed");
        vTaskSuspend(nullptr);
        return;
      }
    }
    uint8_t *render_target = lottie_initial_render_target(ctx);
    lv_lottie_set_buffer(ctx->obj, ctx->render_width, ctx->render_height, render_target);
    ctx->thorvg_target = render_target;
    if (ctx->data != nullptr) {
      lv_lottie_set_src_data(ctx->obj, ctx->data, ctx->data_size);
    } else if (ctx->file_path != nullptr) {
      lv_lottie_set_src_file(ctx->obj, ctx->file_path);
    }

    lv_anim_t *anim = lv_lottie_get_anim(ctx->obj);
    if (anim != nullptr) {
      ctx->exec_cb = anim->exec_cb;
      ctx->anim_var = anim->var;
      ctx->start_frame = anim->start_value;
      ctx->end_frame = anim->end_value;
      ctx->duration_ms = static_cast<uint32_t>(lv_anim_get_time(anim));
      lv_anim_delete(ctx->anim_var, ctx->exec_cb);
      reinterpret_cast<lv_lottie_t *>(ctx->obj)->anim = nullptr;
      ctx->data_loaded = true;
    }
    if (ctx->flatten_to_opaque) {
      lottie_attach_opaque_buffer(ctx);
    }
  } else {
    auto *lottie = reinterpret_cast<lv_lottie_t *>(ctx->obj);
    tvg_canvas_clear(lottie->tvg_canvas, false);
    ctx->thorvg_target = nullptr;
    uint8_t *render_target = lottie_initial_render_target(ctx);
    lv_lottie_set_buffer(ctx->obj, ctx->render_width, ctx->render_height, render_target);
    ctx->thorvg_target = render_target;
    if (ctx->flatten_to_opaque) {
      lottie_attach_opaque_buffer(ctx);
    }
  }
  lv_unlock();

  ESP_LOGI(LOTTIE_PERF_TAG,
           "task start ctx=%p fast_radial=%u scaled=%u opaque=%u render=%ux%u display=%ux%u frames=%d..%d duration=%ums "
           "loop=%u play_count=%u ping_pong=%u core=%d",
           ctx, (unsigned) ctx->fast_radial, (unsigned) lottie_uses_scaled_render(ctx),
           (unsigned) ctx->flatten_to_opaque, (unsigned) ctx->render_width, (unsigned) ctx->render_height,
           (unsigned) ctx->width, (unsigned) ctx->height, (int) ctx->start_frame, (int) ctx->end_frame,
           (unsigned) ctx->duration_ms, (unsigned) ctx->loop, (unsigned) ctx->play_count,
           (unsigned) (ctx->work_buffer_alt != nullptr), (int) xPortGetCoreID());

  if (!ctx->data_loaded || ctx->exec_cb == nullptr || ctx->anim_var == nullptr || ctx->duration_ms == 0 ||
      ctx->end_frame <= ctx->start_frame) {
    LV_LOG_WARN("No valid animation, task suspending");
    vTaskSuspend(nullptr);
  }

  // Render frame zero while hidden. LVGL's native callback deliberately
  // skips hidden objects, which previously left the boot canvas blank until
  // the first visible timer tick.
  uint8_t *first_target = lottie_initial_render_target(ctx);
  const int32_t prepare_frame = lottie_first_renderable_frame(ctx);
  const bool first_frame_ok = lottie_render_frame(ctx, prepare_frame, first_target);
  if (first_frame_ok) {
    // Direct-XRGB playback normally delegates cache maintenance to the direct
    // PPA presenter. The prepared frame is first exposed through the ordinary
    // LVGL canvas, however, so publish those CPU writes before revealing it.
    if (lottie_uses_direct_xrgb(ctx))
      lottie_sync_buffer(first_target, lottie_buffer_bytes(ctx));
    if (lottie_uses_direct_opaque_pipeline(ctx)) {
      // The first frame is prepared while the widget is hidden, so there is
      // no direct-region lease to submit yet. Keep a complete native canvas
      // copy as the bridge until the object is revealed.
      if (!lottie_copy_xrgb8888_to_rgb888(ctx, first_target, ctx->pixel_buffer)) {
        LV_LOG_WARN("Failed to convert initial opaque Lottie frame");
        vTaskSuspend(nullptr);
      }
      lv_lock();
      lv_canvas_set_buffer(ctx->obj, ctx->pixel_buffer, ctx->width, ctx->height, LV_COLOR_FORMAT_RGB888);
      lv_draw_buf_clear_flag(lv_canvas_get_draw_buf(ctx->obj), LV_IMAGE_FLAGS_PREMULTIPLIED);
      lv_obj_invalidate(ctx->obj);
      lv_unlock();
    } else if (ctx->flatten_to_opaque) {
      if (lottie_renders_to_display_back_buffer(ctx)) {
        if (!lottie_copy_xrgb8888_to_rgb888(ctx, first_target, ctx->pixel_buffer)) {
          LV_LOG_WARN("Failed to convert initial opaque Lottie frame");
          vTaskSuspend(nullptr);
        }
        lv_lock();
        // The retained canvas is packed RGB888, not the XRGB render target.
        // Its descriptor must keep the converted stride for boot snapshots.
        lv_canvas_set_buffer(ctx->obj, ctx->pixel_buffer, ctx->width, ctx->height, LV_COLOR_FORMAT_RGB888);
        lv_draw_buf_clear_flag(lv_canvas_get_draw_buf(ctx->obj), LV_IMAGE_FLAGS_PREMULTIPLIED);
        lv_obj_invalidate(ctx->obj);
        lv_unlock();
      } else {
        lottie_publish_opaque_frame(ctx);
      }
    } else if (ctx->work_buffer != nullptr) {
      lottie_publish_work_buffer(ctx);
    } else {
      lv_lock();
      lv_obj_invalidate(ctx->obj);
      lv_unlock();
    }
    lottie_prepare_frame_cache(ctx);
    ctx->prepared_frame_ready = true;
  } else {
    LV_LOG_WARN("Failed to render initial Lottie frame");
  }

  if (ctx->prepared_frame_ready && !ctx->runtime_hidden && ctx->auto_start && !ctx->restart_requested) {
    lottie_reveal_prepared_frame(ctx);
    ctx->reveal_after_restart = false;
  }

  const int32_t total_frames = ctx->end_frame - ctx->start_frame;
  int32_t last_frame = lottie_first_renderable_frame(ctx);
  int32_t last_cache_index = 0;
  int32_t pending_frame = -1;
  bool pending_frame_ready = false;
  int64_t pending_render_start_us = 0;
  bool completed = false;
  bool completed_frame_published = false;
  bool final_frame_committed = false;
  uint32_t phase_offset_ms = 0;
  ctx->start_time_us = esp_timer_get_time();

  uint32_t perf_frames = 0;
  uint64_t perf_render_us = 0;
  uint64_t perf_present_us = 0;
  uint32_t perf_raster_frames = 0;
  uint64_t perf_raster_us = 0;
  uint32_t perf_render_max_us = 0;
  uint32_t perf_present_max_us = 0;
  uint32_t perf_raster_max_us = 0;
  uint32_t perf_publish_missed = 0;
  int64_t previous_publish_us = 0;
  uint64_t perf_raster_start_after_publish_us = 0;
  uint32_t perf_raster_start_samples = 0;
  uint32_t perf_raster_started_while_ppa = 0;
  int64_t perf_last_log_us = ctx->start_time_us;
  int64_t diag_last_log_us = ctx->start_time_us;

  while (!ctx->stop_requested) {
    if (ctx->restart_requested) {
      ctx->start_time_us = esp_timer_get_time();
      perf_frames = perf_raster_frames = perf_publish_missed = 0;
      perf_render_us = perf_present_us = perf_raster_us = 0;
      perf_render_max_us = perf_present_max_us = perf_raster_max_us = 0;
      perf_raster_start_after_publish_us = 0;
      perf_raster_start_samples = perf_raster_started_while_ppa = 0;
      perf_last_log_us = ctx->start_time_us;
      std::fill_n(ctx->raster_stages_us, 6, 0);
      ctx->raster_stage_samples = 0;
      phase_offset_ms = __atomic_exchange_n(&ctx->restart_phase_ms, 0U, __ATOMIC_ACQ_REL);
      ctx->restart_requested = false;
      last_frame = -1;
      last_cache_index = -1;
      pending_frame = -1;
      pending_frame_ready = false;
      pending_render_start_us = 0;
      previous_publish_us = 0;
      completed = false;
      completed_frame_published = false;
      final_frame_committed = false;
      ctx->playback_completed = false;
      ctx->retained_frame = -1;
      ctx->retained_coverage = 0;
      ctx->direct_published_source = nullptr;
    }

    if (__atomic_exchange_n(&ctx->snapshot_commit_requested, false, __ATOMIC_ACQ_REL)) {
      const bool committed = lottie_commit_direct_frame_to_canvas(ctx);
      __atomic_store_n(&ctx->snapshot_commit_success, committed, __ATOMIC_RELEASE);
      __atomic_store_n(&ctx->snapshot_commit_completed, true, __ATOMIC_RELEASE);
    }

    if (ctx->runtime_hidden && ctx->direct_region_active && lottie_wait_direct_frame_buffer(ctx, 100))
      lottie_release_direct_region(ctx);

    if (!ctx->auto_start || (completed && (!lottie_uses_direct_scaled_argb(ctx) || completed_frame_published))) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      if (ctx->stop_requested)
        break;
      continue;
    }

    if (ctx->runtime_hidden) {
      pending_frame = -1;
      pending_frame_ready = false;
      if (lottie_renders_to_display_back_buffer(ctx) && lottie_wait_direct_frame_buffer(ctx, 100))
        lottie_release_direct_region(ctx);
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    // Snapshot/navigation owns the physical DSI buffers for the duration of
    // a transition. Do not enter the publish path while that ownership is
    // active: the LVGL lock can legitimately be held by the snapshot worker
    // for a full frame, and retrying here would burn the UI core without
    // producing a visible frame.
    const bool direct_async_pipeline =
        lottie_uses_direct_scaled_argb(ctx) || lottie_uses_direct_opaque_pipeline(ctx);
    if (direct_async_pipeline && lvgl_esphome_snapshot_is_active()) {
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(16));
      continue;
    }

    // lottie_show() publishes restart_requested before making the widget
    // visible. Catch a request that arrived after the loop's first check so
    // an old clock can never emit catch-up frames on page re-entry.
    if (ctx->restart_requested)
      continue;

    const bool direct_prepare_pipeline = direct_async_pipeline && ctx->work_buffer_alt != nullptr;
    const int64_t now_us = esp_timer_get_time();
    const int64_t timeline_sample_us = now_us + (direct_prepare_pipeline ? LOTTIE_DIRECT_PIPELINE_LEAD_US : 0);
    const uint32_t elapsed_ms = static_cast<uint32_t>(
        std::max<int64_t>(0, timeline_sample_us - ctx->start_time_us) / 1000);
    uint32_t phase_ms = elapsed_ms;
    uint64_t timeline_ms = static_cast<uint64_t>(phase_offset_ms) + elapsed_ms;
    uint64_t cycle_base_ms = 0;
    if (ctx->loop) {
      const uint64_t finite_duration_ms = static_cast<uint64_t>(ctx->duration_ms) * ctx->play_count;
      if (ctx->play_count != 0 && static_cast<uint64_t>(elapsed_ms) >= finite_duration_ms) {
        phase_ms = phase_offset_ms % ctx->duration_ms;
        timeline_ms = static_cast<uint64_t>(phase_offset_ms) + finite_duration_ms;
        cycle_base_ms = timeline_ms - phase_ms;
        completed = true;
      } else {
        phase_ms = static_cast<uint32_t>(timeline_ms % ctx->duration_ms);
        cycle_base_ms = timeline_ms - phase_ms;
      }
    } else if (elapsed_ms >= ctx->duration_ms) {
      phase_ms = ctx->duration_ms;
      completed = true;
    }

    int32_t frame = lottie_last_renderable_frame(ctx);
    if (!completed || (ctx->loop && phase_offset_ms != 0)) {
      frame = ctx->start_frame + static_cast<int32_t>(static_cast<int64_t>(total_frames) * phase_ms / ctx->duration_ms);
      if (frame <= ctx->start_frame) {
        frame = lottie_first_renderable_frame(ctx);
      }
      if (completed && ctx->retained_frame >= lottie_first_renderable_frame(ctx)) {
        frame = ctx->retained_frame;
      }
    }
    if (completed && lottie_uses_direct_scaled_argb(ctx) && frame == last_frame)
      completed_frame_published = true;

    if (ctx->frame_cache != nullptr && ctx->frame_cache_count > 1) {
      uint32_t cache_index = ctx->frame_cache_count - 1;
      if (!completed) {
        cache_index = std::min<uint32_t>(
            ctx->frame_cache_count - 1,
            static_cast<uint32_t>((static_cast<uint64_t>(phase_ms) * (ctx->frame_cache_count - 1)) / ctx->duration_ms));
      }
      if (static_cast<int32_t>(cache_index) != last_cache_index) {
        lottie_publish_cached_frame(ctx, cache_index);
        last_cache_index = static_cast<int32_t>(cache_index);
        if (ctx->reveal_after_restart && ctx->prepared_frame_ready) {
          lottie_reveal_prepared_frame(ctx);
          ctx->reveal_after_restart = false;
        }
      }
      last_frame = frame;
    } else if (frame != last_frame) {
      const bool perf_enabled = lvgl_esphome_get_perf_logging_enabled() != 0;
      const int64_t render_start_us = perf_enabled ? esp_timer_get_time() : 0;

      if (ctx->flatten_to_opaque && !lottie_uses_direct_opaque_pipeline(ctx)) {
        if (lottie_renders_to_display_back_buffer(ctx) && !lottie_wait_direct_frame_buffer(ctx, 100)) {
          vTaskDelay(1);
          continue;
        }
        uint8_t *render_target = lottie_render_target(ctx);
        const int64_t raster_start_us = perf_enabled ? esp_timer_get_time() : 0;
        const bool rendered = lottie_render_frame(ctx, frame, render_target);
        const int64_t raster_end_us = perf_enabled ? esp_timer_get_time() : 0;
        if (rendered) {
          if (!completed)
            lottie_track_retained_frame(ctx, frame, render_target);
          const int64_t present_start_us = perf_enabled ? esp_timer_get_time() : 0;
          lottie_publish_opaque_frame(ctx);
          if (ctx->reveal_after_restart && ctx->prepared_frame_ready) {
            lottie_reveal_prepared_frame(ctx);
            ctx->reveal_after_restart = false;
          }
           const int64_t present_end_us = perf_enabled ? esp_timer_get_time() : 0;
          if (perf_enabled) {
            const uint32_t render_us = static_cast<uint32_t>(present_start_us - render_start_us);
            const uint32_t present_us = static_cast<uint32_t>(present_end_us - present_start_us);
            perf_frames++;
            perf_render_us += render_us;
            perf_present_us += present_us;
            const uint32_t raster_us = static_cast<uint32_t>(raster_end_us - raster_start_us);
            perf_raster_frames++;
            perf_raster_us += raster_us;
            if (raster_us > perf_raster_max_us)
              perf_raster_max_us = raster_us;
            if (render_us > perf_render_max_us)
              perf_render_max_us = render_us;
            if (present_us > perf_present_max_us)
              perf_present_max_us = present_us;
          }
        }
      } else if (ctx->work_buffer != nullptr) {
        const bool direct_scaled = lottie_uses_direct_scaled_argb(ctx);
        const bool direct_opaque = lottie_uses_direct_opaque_pipeline(ctx);
        const bool direct_async = direct_scaled || direct_opaque;
        // A 424 px regional composition can legitimately take just over one
        // display period while PPA drains its bands. Waiting only 20 ms made
        // the producer time out while the compositor was still healthy,
        // creating artificial missed frames and a second competing retry.
        // Compact scaled sources can overlap rasterization and PPA. A native
        // opaque weather surface cannot: both passes stream large PSRAM
        // buffers and starve DSI when run together. Keep its buffers separate,
        // but wait for the consumer before starting the next raster pass.
        const bool direct_ping_pong = direct_scaled && ctx->work_buffer_alt != nullptr;
        if (direct_async && !direct_ping_pong && !lottie_wait_direct_frame_buffer(ctx, 60)) {
          if (perf_enabled) {
            perf_publish_missed++;
          }
          lottie_log_direct_publish_failure(ctx, "callback-timeout");
          // A direct request is still owned by the compositor. Sleep until
          // its callback or the next display slot instead of rasterizing the
          // same timeline frame thousands of times per second.
          ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(16));
          continue;
        }

        // Keep one completed render pending while the regional compositor is
        // busy. With the fast weather path the spare source is the actual
        // ping-pong surface: do not discard that finished frame just because
        // the timeline advanced while PPA was blending it. Re-rendering here
        // was the main source of the large publish_missed count.
         if (!pending_frame_ready) {
          pending_render_start_us = perf_enabled ? esp_timer_get_time() : render_start_us;
          const int64_t raster_start_us = perf_enabled ? esp_timer_get_time() : 0;
            if (perf_enabled && direct_async && previous_publish_us != 0) {
            perf_raster_start_after_publish_us += static_cast<uint64_t>(raster_start_us - previous_publish_us);
            perf_raster_start_samples++;
            if (lottie_direct_frame_in_flight(ctx))
              perf_raster_started_while_ppa++;
          }
          const bool rendered = lottie_render_frame(ctx, frame, ctx->work_buffer);
          const int64_t raster_end_us = perf_enabled ? esp_timer_get_time() : 0;
          if (rendered) {
            if (perf_enabled) {
              const uint32_t raster_us = static_cast<uint32_t>(raster_end_us - raster_start_us);
              perf_raster_frames++;
              perf_raster_us += raster_us;
              if (raster_us > perf_raster_max_us)
                perf_raster_max_us = raster_us;
            }
          } else {
            if (direct_async)
              ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(16));
            else
              vTaskDelay(1);
            continue;
          }
          pending_frame = frame;
          pending_frame_ready = true;
        }

        // The callback releases the submitted source buffer. Wait before
        // handing the pending ping-pong buffer to the compositor; otherwise
        // the producer submits a request that can only be rejected as
        // frame-in-flight.
        if (direct_async && lottie_direct_frame_in_flight(ctx)) {
          ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20));
          continue;
        }

        const int64_t present_start_us = perf_enabled ? esp_timer_get_time() : 0;
        const bool published = lottie_publish_work_buffer(ctx);
        const int64_t present_end_us = perf_enabled ? esp_timer_get_time() : 0;
        if (!published) {
          if (perf_enabled)
            perf_publish_missed++;
          ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(16));
          continue;
        }
        if (ctx->reveal_after_restart && ctx->prepared_frame_ready) {
          lottie_reveal_prepared_frame(ctx);
          ctx->reveal_after_restart = false;
        }
        if (perf_enabled) {
          const uint32_t render_us = static_cast<uint32_t>(present_start_us - pending_render_start_us);
          const uint32_t present_us = static_cast<uint32_t>(present_end_us - present_start_us);
          perf_frames++;
          perf_render_us += render_us;
          perf_present_us += present_us;
          if (render_us > perf_render_max_us)
            perf_render_max_us = render_us;
          if (present_us > perf_present_max_us)
            perf_present_max_us = present_us;
          previous_publish_us = present_end_us;
        }
        last_frame = pending_frame;
        pending_frame = -1;
        pending_frame_ready = false;
        if (completed)
          completed_frame_published = true;
      } else {
        lv_lock();
        ctx->exec_cb(ctx->anim_var, frame);
        lottie_sync_canvas_buffer(ctx);
        // Large transparent scenes intentionally use one ARGB canvas when a
        // second full-size buffer would fragment PSRAM.  The canvas contents
        // are still valid, but LVGL will not flush them unless the object is
        // invalidated explicitly.  Without this, the animation appears to
        // jump straight to the last frame on the next unrelated redraw.
        lv_obj_invalidate(ctx->obj);
        lv_unlock();
        if (ctx->reveal_after_restart && ctx->prepared_frame_ready) {
          lottie_reveal_prepared_frame(ctx);
          ctx->reveal_after_restart = false;
        }
       }
      last_frame = frame;
    }

    // Publish completion only after the final frame has reached the
    // presentation buffer. Boot sequencing can then retain that frame
    // without guessing the animation duration in YAML.
    if (completed && !final_frame_committed &&
        (lottie_renders_to_display_back_buffer(ctx) || lottie_uses_direct_scaled_argb(ctx) ||
         lottie_uses_direct_opaque_pipeline(ctx))) {
      final_frame_committed = lottie_renders_to_display_back_buffer(ctx)
                                  ? lottie_commit_direct_frame_to_canvas(ctx)
                                  : lottie_uses_direct_scaled_argb(ctx)
                                        ? lottie_commit_direct_scaled_frame_to_canvas(ctx)
                                        : lottie_commit_direct_frame_to_canvas(ctx);
      if (!final_frame_committed) {
        vTaskDelay(1);
        continue;
      }
    }
    if (completed)
      ctx->playback_completed = true;

    const int64_t log_now_us = esp_timer_get_time();
    if (ctx->fast_radial && log_now_us - diag_last_log_us >= 1000000) {
      ESP_LOGI(LOTTIE_PERF_TAG,
               "radial state ctx=%p frame=%d elapsed=%ums last=%d completed=%u pending=%u in_flight=%u "
               "submits=%u callbacks=%u rejects=%u",
               ctx, (int) frame, (unsigned) elapsed_ms, (int) last_frame, (unsigned) completed,
               (unsigned) pending_frame_ready, (unsigned) lottie_direct_frame_in_flight(ctx),
               (unsigned) __atomic_load_n(&ctx->direct_publish_diag_submits, __ATOMIC_RELAXED),
               (unsigned) __atomic_load_n(&ctx->direct_publish_diag_callbacks, __ATOMIC_RELAXED),
               (unsigned) ctx->direct_publish_diag_rejects);
      diag_last_log_us = log_now_us;
    }
    if (lvgl_esphome_get_perf_logging_enabled() != 0 && log_now_us - perf_last_log_us >= 2000000 &&
        (perf_frames > 0 || perf_publish_missed > 0)) {
      ESP_LOGI(LOTTIE_PERF_TAG,
               "perf2s: ctx=%p elapsed_ms=%u fast_radial=%u scaled=%u frames=%u publish_missed=%u render_avg=%lluus render_max=%uus "
               "raster_avg=%lluus raster_max=%uus prepare_publish_avg=%lluus prepare_publish_max=%uus "
               "raster_start_after_publish_avg=%lluus while_ppa=%u/%u",
               ctx, static_cast<unsigned>((log_now_us - perf_last_log_us) / 1000),
               (unsigned) ctx->fast_radial, (unsigned) lottie_uses_scaled_render(ctx),
               static_cast<unsigned>(perf_frames), static_cast<unsigned>(perf_publish_missed),
               static_cast<unsigned long long>(perf_frames == 0 ? 0 : perf_render_us / perf_frames),
               static_cast<unsigned>(perf_render_max_us),
               static_cast<unsigned long long>(perf_raster_frames == 0 ? 0 : perf_raster_us / perf_raster_frames),
               static_cast<unsigned>(perf_raster_max_us),
               static_cast<unsigned long long>(perf_frames == 0 ? 0 : perf_present_us / perf_frames),
               static_cast<unsigned>(perf_present_max_us),
               static_cast<unsigned long long>(perf_raster_start_samples == 0
                                                   ? 0
                                                   : perf_raster_start_after_publish_us / perf_raster_start_samples),
               static_cast<unsigned>(perf_raster_started_while_ppa),
               static_cast<unsigned>(perf_raster_start_samples));
      if (ctx->raster_stage_samples != 0) {
        const uint32_t count = ctx->raster_stage_samples;
        ESP_LOGI(LOTTIE_PERF_TAG, "raster stages: ctx=%p clear=%uus frame=%uus update=%uus draw=%uus sync=%uus cache=%uus",
                 ctx, (unsigned) (ctx->raster_stages_us[0] / count), (unsigned) (ctx->raster_stages_us[1] / count),
                 (unsigned) (ctx->raster_stages_us[2] / count), (unsigned) (ctx->raster_stages_us[3] / count),
                 (unsigned) (ctx->raster_stages_us[4] / count), (unsigned) (ctx->raster_stages_us[5] / count));
        std::fill_n(ctx->raster_stages_us, 6, 0);
        ctx->raster_stage_samples = 0;
      }
      perf_frames = 0;
      perf_publish_missed = 0;
       perf_render_us = 0;
       perf_present_us = 0;
       perf_raster_frames = 0;
       perf_raster_us = 0;
       perf_render_max_us = 0;
       perf_raster_max_us = 0;
       perf_present_max_us = 0;
       perf_raster_start_after_publish_us = 0;
       perf_raster_start_samples = 0;
       perf_raster_started_while_ppa = 0;
      perf_last_log_us = log_now_us;
    }

    if (completed)
      continue;

    const uint32_t frame_index = static_cast<uint32_t>(frame - ctx->start_frame);
    uint32_t next_phase_ms = static_cast<uint32_t>(
        (static_cast<uint64_t>(frame_index + 1) * ctx->duration_ms + total_frames - 1) / total_frames);
    uint64_t target_timeline_ms = cycle_base_ms + next_phase_ms;
    if (target_timeline_ms <= timeline_ms)
      target_timeline_ms += ctx->duration_ms;
    const uint64_t target_elapsed_ms = target_timeline_ms - phase_offset_ms;
    const int64_t target_us = ctx->start_time_us + static_cast<int64_t>(target_elapsed_ms) * 1000 -
                              (direct_prepare_pipeline ? LOTTIE_DIRECT_PIPELINE_LEAD_US : 0);
    // A complex frame can take longer than one display period. In that case
    // target_us is already in the past and lottie_wait_until_us() returns
    // without yielding. Keep the animation on schedule, but give the loop
    // task and the display/audio workers one tick before rendering again.
    if (target_us <= esp_timer_get_time())
      vTaskDelay(pdMS_TO_TICKS(1));
    else
      lottie_wait_until_us(target_us);
  }

  // The compositor callback is the ownership boundary for the private render
  // buffer. Never suspend and let cleanup free it while PPA can still read it.
  lottie_wait_direct_frame_buffer(ctx, 0);
  lottie_release_direct_region(ctx);
  vTaskSuspend(nullptr);
}

// --------------------------------------------------------------------------
// Free all PSRAM/internal-RAM resources for one Lottie widget.
// --------------------------------------------------------------------------
inline bool lottie_stop_task(LottieContext *ctx) {
  if (ctx == nullptr || ctx->task_handle == nullptr) {
    return true;
  }
  ctx->stop_requested = true;
  TaskHandle_t task = ctx->task_handle;
  xTaskNotifyGive(task);
  if (task == xTaskGetCurrentTaskHandle()) {
    return false;
  }
  for (uint32_t waited_ms = 0; waited_ms < 250; waited_ms += 5) {
    eTaskState state = eTaskGetState(task);
    if (state == eSuspended || state == eDeleted) {
      bool running = false;
      for (BaseType_t core = 0; core < configNUMBER_OF_CORES; core++) {
        if (xTaskGetCurrentTaskHandleForCore(core) == task) {
          running = true;
          break;
        }
      }
      if (running) {
        vTaskDelay(pdMS_TO_TICKS(5));
        continue;
      }
      vTaskDelete(task);
      ctx->task_handle = nullptr;
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  LV_LOG_WARN("Lottie task did not stop in time; keeping resources allocated");
  return false;
}

inline void lottie_free_resources(LottieContext *ctx) {
  if (!lottie_stop_task(ctx)) {
    return;
  }
  lottie_release_direct_region(ctx);
  if (ctx->task_stack) {
    heap_caps_free(ctx->task_stack);
    ctx->task_stack = nullptr;
  }
  if (ctx->task_tcb) {
    heap_caps_free(ctx->task_tcb);
    ctx->task_tcb = nullptr;
  }
  if (ctx->pixel_buffer) {
    lvgl_esphome_release_dma_image_buffer(ctx->pixel_buffer);
    heap_caps_free(ctx->pixel_buffer);
    ctx->pixel_buffer = nullptr;
  }
  if (ctx->work_buffer) {
    heap_caps_free(ctx->work_buffer);
    ctx->work_buffer = nullptr;
  }
  if (ctx->work_buffer_alt) {
    heap_caps_free(ctx->work_buffer_alt);
    ctx->work_buffer_alt = nullptr;
  }
  if (ctx->display_back_buffer) {
    lvgl_esphome_release_dma_image_buffer(ctx->display_back_buffer);
    heap_caps_free(ctx->display_back_buffer);
    ctx->display_back_buffer = nullptr;
  }
  if (ctx->frame_cache) {
    heap_caps_free(ctx->frame_cache);
    ctx->frame_cache = nullptr;
  }
  ctx->frame_cache_stride = 0;
  ctx->frame_cache_count = 0;
  ctx->stop_requested = false;
  ctx->thorvg_target = nullptr;
  lottie_set_direct_frame_in_flight(ctx, false);
  ctx->direct_published_source = nullptr;
  ctx->direct_region_active = false;
  ctx->fast_radial_buffer_ptr[0] = nullptr;
  ctx->fast_radial_buffer_ptr[1] = nullptr;
  ctx->fast_radial_last_frame[0] = -1;
  ctx->fast_radial_last_frame[1] = -1;

  LV_LOG_INFO("Lottie freed (%ux%u = %u KB %s buf + 64 KB stack)", (unsigned) ctx->width, (unsigned) ctx->height,
              (unsigned) (ctx->width * ctx->height * 4 / 1024), ctx->pixel_buffer_internal ? "SRAM" : "PSRAM");
  ctx->pixel_buffer_internal = false;
  ctx->work_buffer_internal = false;
  ctx->work_buffer_alt_internal = false;
  ctx->display_back_buffer_internal = false;
  ctx->frame_cache_internal = false;
}

// --------------------------------------------------------------------------
// (Re-)allocate pixel buffer and launch the render task.
// lv_lottie_set_buffer is NOT called here – it is called inside the task
// because it triggers ThorVG rendering which needs the 64 KB stack.
// --------------------------------------------------------------------------
inline bool lottie_launch(LottieContext *ctx) {
  // NOTE: Do NOT re-capture runtime_hidden here!
  // On re-loads the widget is already hidden (by lottie_screen_unload_start_cb),
  // so reading LV_OBJ_FLAG_HIDDEN would always return true, losing the real
  // visibility state that was correctly saved in the unload callback.
  // runtime_hidden is set by:
  //   - lottie_init()                      → first load (from YAML config)
  //   - lottie_screen_unload_start_cb()    → re-loads  (actual state before hide)

  // Prefer internal SRAM for small ARGB canvas buffers. Lottie updates the
  // same canvas repeatedly, and keeping that source out of PSRAM avoids a
  // fragile cache/PPA/display-read path on ESP32-P4. Larger animations still
  // fall back to PSRAM to keep the UI bootable.
  const size_t argb_bytes = lottie_buffer_bytes(ctx);
  const size_t display_bytes = lottie_display_buffer_bytes(ctx);
  const size_t display_alloc_bytes = lottie_align_up(display_bytes, LOTTIE_CACHE_ALIGN);
  ctx->pixel_buffer_internal = false;
  ctx->pixel_buffer = lottie_alloc_pixel_buffer(display_alloc_bytes, &ctx->pixel_buffer_internal);
  if (!ctx->pixel_buffer) {
    ESP_LOGE("lvgl.lottie", "Pixel buffer alloc failed: %u bytes free=%uK largest=%uK",
             (unsigned) display_alloc_bytes, (unsigned) (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
             (unsigned) (heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) / 1024));
    LV_LOG_ERROR("Pixel buffer alloc failed (%u bytes)", (unsigned) display_alloc_bytes);
    return false;
  }
  ctx->prepared_frame_ready = false;
  ctx->retained_frame = -1;
  ctx->retained_coverage = 0;
  lottie_fill_display_buffer(ctx, ctx->pixel_buffer, display_bytes);

  ctx->display_back_buffer = nullptr;
  ctx->display_back_buffer_internal = false;

  // Double buffering keeps incomplete ThorVG frames away from LVGL and lets
  // the renderer run without the global LVGL lock. It is intentionally
  // bounded so large application animations cannot consume excessive PSRAM.
  ctx->work_buffer = nullptr;
  ctx->work_buffer_internal = false;
  ctx->work_buffer_alt = nullptr;
  ctx->work_buffer_alt_internal = false;
  ctx->fast_radial_buffer_ptr[0] = nullptr;
  ctx->fast_radial_buffer_ptr[1] = nullptr;
  ctx->fast_radial_last_frame[0] = -1;
  ctx->fast_radial_last_frame[1] = -1;
  const size_t work_alloc_bytes = lottie_align_up(argb_bytes, LOTTIE_CACHE_ALIGN);
  // Direct XRGB presentation is safe for every opaque scene. Keeping the
  // finite weather path on the same two-buffer contract avoids allocating an
  // unnecessary ARGB work surface and removes a full-frame copy per frame.
  // Keep an opaque XRGB canvas as the stable LVGL source, but rasterize live
  // frames into two private targets so direct PPA presentation can overlap
  // ThorVG with the previous frame's DSI transfer.
  const bool render_direct_to_back = false;
  if (!render_direct_to_back && (ctx->flatten_to_opaque || work_alloc_bytes <= LOTTIE_DOUBLE_BUFFER_MAX_BYTES)) {
    ctx->work_buffer = lottie_alloc_pixel_buffer(work_alloc_bytes, &ctx->work_buffer_internal);
    if (ctx->work_buffer != nullptr) {
      memset(ctx->work_buffer, 0, work_alloc_bytes);
    }
  }
  if (ctx->flatten_to_opaque && ctx->work_buffer == nullptr && !render_direct_to_back) {
    ESP_LOGE("lvgl.lottie", "ARGB render buffer alloc failed: %u bytes free=%uK largest=%uK",
             (unsigned) work_alloc_bytes, (unsigned) (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
             (unsigned) (heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) / 1024));
    LV_LOG_ERROR("ARGB render buffer alloc failed (%u bytes)", (unsigned) work_alloc_bytes);
    lottie_free_resources(ctx);
    return false;
  }

  // The fast weather path submits the compact source to PPA asynchronously.
  // Use the otherwise unnecessary second full-size scaled canvas as a second
  // compact source instead: CPU rasterization can prepare frame N+1 while
  // PPA is consuming frame N, without increasing the live PSRAM footprint.
  if ((lottie_uses_direct_scaled_argb(ctx) || lottie_uses_direct_opaque_pipeline(ctx)) &&
      ctx->work_buffer != nullptr) {
    ctx->work_buffer_alt = lottie_alloc_pixel_buffer(work_alloc_bytes, &ctx->work_buffer_alt_internal);
    if (ctx->work_buffer_alt != nullptr) {
      memset(ctx->work_buffer_alt, 0, work_alloc_bytes);
    } else {
      ESP_LOGW("lvgl.lottie", "Fast radial ping-pong buffer unavailable: need=%u bytes free=%uK largest=%uK",
               (unsigned) work_alloc_bytes, (unsigned) (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
               (unsigned) (heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) / 1024));
    }
  }
  ctx->fast_radial_buffer_ptr[0] = ctx->work_buffer;
  ctx->fast_radial_buffer_ptr[1] = ctx->work_buffer_alt;

  // Opaque/direct scenes need a third presentation buffer because PPA reads
  // an immutable XRGB source asynchronously. Transparent ARGB scenes use the
  // pixel and work buffers as a normal double buffer instead.
  if (render_direct_to_back || (ctx->flatten_to_opaque && ctx->work_buffer != nullptr) ||
      (lottie_uses_scaled_render(ctx) && !lottie_uses_direct_scaled_argb(ctx))) {
    ctx->display_back_buffer = lottie_alloc_pixel_buffer(display_alloc_bytes, &ctx->display_back_buffer_internal);
    if (ctx->display_back_buffer == nullptr) {
      ESP_LOGE("lvgl.lottie", "Display back buffer alloc failed: %u bytes free=%uK largest=%uK",
               (unsigned) display_alloc_bytes, (unsigned) (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
               (unsigned) (heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) / 1024));
      LV_LOG_ERROR("Display back buffer alloc failed (%u bytes)", (unsigned) display_alloc_bytes);
      lottie_free_resources(ctx);
      return false;
    }
    lottie_fill_display_buffer(ctx, ctx->display_back_buffer, display_bytes);
  }
  ctx->thorvg_target = nullptr;
  lottie_set_direct_frame_in_flight(ctx, false);
  ctx->direct_published_source = nullptr;
  ctx->direct_region_active = false;
  ctx->direct_region_x = 0;
  ctx->direct_region_y = 0;
  ctx->direct_region_width = 0;
  ctx->direct_region_height = 0;

  // Hide temporarily during async load (pixel buffer is blank)
  lv_obj_add_flag(ctx->obj, LV_OBJ_FLAG_HIDDEN);

  // Allocate task stack + TCB
  ctx->task_stack = (StackType_t *) heap_caps_malloc(LOTTIE_TASK_STACK_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  ctx->task_tcb = (StaticTask_t *) heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!ctx->task_stack || !ctx->task_tcb) {
    LV_LOG_ERROR("Task alloc failed");
    lottie_free_resources(ctx);
    return false;
  }

  ctx->stop_requested = false;
#if CONFIG_FREERTOS_UNICORE
  constexpr BaseType_t render_core = tskNO_AFFINITY;
  constexpr BaseType_t render_priority = 2;
#else
  // The regional PPA compositor is pinned to the background core. Keep the
  // CPU-bound weather rasterizer beside ESPHome's loop task so the PPA work
  // can progress independently instead of serializing both halves on core 0.
  const BaseType_t loop_core = esp32::loop_task_core();
  const BaseType_t render_core = loop_core == 0 || loop_core == 1 ? loop_core : xPortGetCoreID();
  constexpr BaseType_t render_priority = 2;
#endif
  ctx->task_handle =
      xTaskCreateStaticPinnedToCore(lottie_render_task, "lottie_anim", LOTTIE_TASK_STACK_SIZE / sizeof(StackType_t),
                                    ctx, render_priority, ctx->task_stack, ctx->task_tcb, render_core);

  if (!ctx->task_handle) {
    lottie_free_resources(ctx);
    return false;
  }

  LV_LOG_INFO(
      "Lottie launched (runtime_hidden=%d, %s: display=%u KB render=%u KB mode=%s + 64 KB stack, core=%d priority=%d, "
      "loop_core=%d free PSRAM: %u KB, free SRAM: %u KB, largest SRAM: %u KB)",
      (int) ctx->runtime_hidden, ctx->pixel_buffer_internal ? "SRAM" : "PSRAM", (unsigned) (display_bytes / 1024),
      (unsigned) (ctx->work_buffer != nullptr ? argb_bytes / 1024 : 0),
      lottie_uses_direct_xrgb(ctx)
          ? "opaque XRGB8888/PPA"
          : (ctx->flatten_to_opaque ? "opaque RGB888" : (ctx->work_buffer != nullptr ? "double ARGB swap" : "single ARGB")),
      (int) render_core, (int) render_priority, (int) esp32::loop_task_core(),
      (unsigned) (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
      (unsigned) (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
      (unsigned) (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024));
  return true;
}

// --------------------------------------------------------------------------
// Screen event callbacks – two-phase unload to avoid drawing freed buffer
// during screen transition animation.
//
//   SCREEN_UNLOAD_START  → stop task + hide widget (LVGL still draws screen)
//   SCREEN_UNLOADED      → free PSRAM (screen no longer visible)
//   SCREEN_LOADED        → re-allocate and re-launch
// --------------------------------------------------------------------------
inline void lottie_screen_unload_start_cb(lv_event_t *e) {
  LottieContext *ctx = (LottieContext *) lv_event_get_user_data(e);

  // Capture actual visibility BEFORE hiding – this preserves dynamic
  // show/hide from user scripts (e.g. weather widget selection).
  ctx->hidden_before_unload = lv_obj_has_flag(ctx->obj, LV_OBJ_FLAG_HIDDEN);

  if (ctx->retain_on_unload) {
    // Snapshot/navigation screens are unloaded briefly and often. Keep the
    // prepared renderer alive and let its worker sleep instead of racing a
    // free/relaunch cycle against the screen transition.
    ctx->runtime_hidden = true;
    lv_obj_add_flag(ctx->obj, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  ctx->runtime_hidden = ctx->hidden_before_unload;

  // Ask the render task to stop, but do not delete it from inside the LVGL
  // unload-start callback.  The task can be holding lv_lock() while rendering;
  // deleting it at that moment leaves LVGL locked and trips the watchdog.
  ctx->stop_requested = true;

  // Hide widget so LVGL won't try to draw the image during transition
  lv_obj_add_flag(ctx->obj, LV_OBJ_FLAG_HIDDEN);

  LV_LOG_INFO("Lottie task stopped, widget hidden (was_hidden=%d)", (int) ctx->runtime_hidden);
}

inline void lottie_screen_unloaded_cb(lv_event_t *e) {
  LottieContext *ctx = (LottieContext *) lv_event_get_user_data(e);

  if (ctx->retain_on_unload) {
    return;
  }

  // Now safe to free – screen is no longer visible
  if (!lottie_stop_task(ctx)) {
    return;
  }
  if (ctx->task_stack) {
    heap_caps_free(ctx->task_stack);
    ctx->task_stack = nullptr;
  }
  if (ctx->task_tcb) {
    heap_caps_free(ctx->task_tcb);
    ctx->task_tcb = nullptr;
  }
  if (ctx->pixel_buffer) {
    lvgl_esphome_release_dma_image_buffer(ctx->pixel_buffer);
    heap_caps_free(ctx->pixel_buffer);
    ctx->pixel_buffer = nullptr;
  }
  if (ctx->work_buffer) {
    heap_caps_free(ctx->work_buffer);
    ctx->work_buffer = nullptr;
  }
  if (ctx->work_buffer_alt) {
    heap_caps_free(ctx->work_buffer_alt);
    ctx->work_buffer_alt = nullptr;
  }
  if (ctx->display_back_buffer) {
    lvgl_esphome_release_dma_image_buffer(ctx->display_back_buffer);
    heap_caps_free(ctx->display_back_buffer);
    ctx->display_back_buffer = nullptr;
  }
  if (ctx->frame_cache) {
    heap_caps_free(ctx->frame_cache);
    ctx->frame_cache = nullptr;
  }
  ctx->frame_cache_stride = 0;
  ctx->frame_cache_count = 0;
  ctx->stop_requested = false;
  ctx->thorvg_target = nullptr;
  lottie_set_direct_frame_in_flight(ctx, false);
  ctx->direct_region_active = false;
  ctx->fast_radial_buffer_ptr[0] = nullptr;
  ctx->fast_radial_buffer_ptr[1] = nullptr;
  ctx->fast_radial_last_frame[0] = -1;
  ctx->fast_radial_last_frame[1] = -1;

  LV_LOG_INFO("Lottie FREED (%ux%u = %u KB %s buf + 64 KB stack) → free PSRAM: %u KB, free SRAM: %u KB",
              (unsigned) ctx->width, (unsigned) ctx->height, (unsigned) (ctx->width * ctx->height * 4 / 1024),
              ctx->pixel_buffer_internal ? "SRAM" : "PSRAM",
              (unsigned) (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
              (unsigned) (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
  ctx->pixel_buffer_internal = false;
  ctx->work_buffer_internal = false;
  ctx->work_buffer_alt_internal = false;
  ctx->display_back_buffer_internal = false;
  ctx->frame_cache_internal = false;
}

inline void lottie_screen_loaded_cb(lv_event_t *e) {
  LottieContext *ctx = (LottieContext *) lv_event_get_user_data(e);
  if (ctx->retain_on_unload && ctx->pixel_buffer != nullptr) {
    ctx->runtime_hidden = ctx->hidden_before_unload;
    if (!ctx->runtime_hidden) {
      lv_obj_remove_flag(ctx->obj, LV_OBJ_FLAG_HIDDEN);
      lv_obj_invalidate(ctx->obj);
    }
    if (ctx->task_handle != nullptr) {
      xTaskNotifyGive(ctx->task_handle);
    }
    return;
  }
  if (ctx->pixel_buffer == nullptr && (!ctx->runtime_hidden || ctx->auto_start)) {
    lottie_launch(ctx);
  }
}

// --------------------------------------------------------------------------
// Public API: Restart animation from frame 0 (preserves loop/hidden state)
// Safe to call at any time – sets a flag checked by the render loop.
// --------------------------------------------------------------------------
inline void lottie_restart(LottieContext *ctx) {
  if (ctx && ctx->task_handle) {
    uint32_t phase_ms = 0;
    const int32_t total_frames = ctx->end_frame - ctx->start_frame;
    if (ctx->loop && ctx->play_count != 0 && ctx->duration_ms != 0 && total_frames > 0 &&
        ctx->retained_frame >= lottie_first_renderable_frame(ctx)) {
      const int32_t frame_offset =
          std::clamp<int32_t>(ctx->retained_frame - ctx->start_frame, int32_t{0}, total_frames - int32_t{1});
      phase_ms = static_cast<uint32_t>(static_cast<uint64_t>(frame_offset) * ctx->duration_ms / total_frames);
    }
    __atomic_store_n(&ctx->restart_phase_ms, phase_ms, __ATOMIC_RELEASE);
    ctx->playback_completed = false;
    ctx->restart_requested = true;
    xTaskNotifyGive(ctx->task_handle);
    LV_LOG_INFO("Restart requested (will reset on next frame)");
  }
}

inline void lottie_show(LottieContext *ctx, bool restart) {
  if (ctx == nullptr || ctx->obj == nullptr) {
    return;
  }
  if (restart) {
    // A visible retained frame is a valid bridge into the restarted epoch.
    // Hiding and immediately revealing the canvas invalidated its parent and
    // exposed the widget's opaque black rectangle before the first direct PPA
    // frame reached DSI. Only an object that was intentionally prepared while
    // hidden needs the deferred first reveal.
    ctx->reveal_after_restart = ctx->prepared_frame_ready && lv_obj_has_flag(ctx->obj, LV_OBJ_FLAG_HIDDEN);
  }
  // Publish the new epoch before waking a paused producer. Otherwise it can
  // observe visibility with the previous page's clock and emit a catch-up frame.
  if (restart)
    lottie_restart(ctx);
  ctx->runtime_hidden = false;
  ctx->auto_start = true;
  if (ctx->pixel_buffer == nullptr) {
    lottie_launch(ctx);
    return;
  }
  if (!ctx->prepared_frame_ready) {
    // lottie_launch() allocates and clears the canvas before its render task
    // has produced the first frame. A second show request in that window must
    // not expose the cleared rectangle. The render task reveals the object as
    // soon as prepared_frame_ready becomes true.
    ctx->reveal_after_restart = true;
    if (ctx->task_handle != nullptr)
      xTaskNotifyGive(ctx->task_handle);
    return;
  }
  if (!ctx->reveal_after_restart)
    lottie_reveal_prepared_frame(ctx);
  if (ctx->task_handle != nullptr) {
    xTaskNotifyGive(ctx->task_handle);
  }
  if ((!ctx->flatten_to_opaque || !lottie_uses_direct_xrgb(ctx)) && !lottie_uses_direct_scaled_argb(ctx))
    lv_obj_invalidate(ctx->obj);
}

// Allocate, parse, and render frame zero while the widget remains hidden.
// A later lottie_show() only reveals the prepared buffer and starts the clock.
inline void lottie_prepare(LottieContext *ctx) {
  if (ctx == nullptr || ctx->obj == nullptr) {
    return;
  }
  ctx->runtime_hidden = true;
  ctx->auto_start = false;
  ctx->playback_completed = false;
  lv_obj_add_flag(ctx->obj, LV_OBJ_FLAG_HIDDEN);
  if (ctx->pixel_buffer == nullptr) {
    lottie_launch(ctx);
  }
}

inline bool lottie_is_ready(const LottieContext *ctx) {
  if (ctx == nullptr || ctx->obj == nullptr || ctx->pixel_buffer == nullptr || !ctx->prepared_frame_ready) {
    return false;
  }
  return true;
}

inline bool lottie_is_complete(const LottieContext *ctx) { return ctx == nullptr || ctx->playback_completed; }

// Release only the optional pre-rendered frame cache after a one-shot
// animation has completed. The final pixel buffer remains attached to the
// widget, so the completed frame stays visible while the reclaimed PSRAM can
// be used to warm the rest of the UI.
inline bool lottie_release_completed_frame_cache(LottieContext *ctx, bool preserve_peak_coverage = false) {
  if (ctx == nullptr || !ctx->playback_completed || ctx->frame_cache == nullptr) {
    return false;
  }

  // Cached frames are submitted to the asynchronous direct-region worker.
  // Drain that queue before taking ownership of the final slot; otherwise a
  // queued PPA read can outlive frame_cache and the following normal LVGL
  // refresh redraws a blank/stale canvas over the completed trace.
  if (!lvgl_esphome_direct_regions_pause(true, 200)) {
    ESP_LOGW(LOTTIE_PERF_TAG, "final frame handoff: direct-region barrier timed out");
    return false;
  }

  // Cached playback presents immutable frames directly to DSI, so the
  // canvas-owned pixel buffer can still contain an earlier frame. Preserve
  // the final visible frame there before releasing the cache; otherwise the
  // next normal LVGL refresh redraws the stale canvas and the completed trace
  // disappears during the boot-to-home handoff.
  if (ctx->pixel_buffer != nullptr && ctx->frame_cache_count != 0 && ctx->frame_cache_stride != 0) {
    uint32_t retained_index = ctx->frame_cache_count - 1U;
    uint32_t retained_coverage = 0;
    uint32_t final_coverage = 0;
    if (preserve_peak_coverage && lottie_uses_direct_xrgb(ctx)) {
      const uint32_t background = 0xFF000000U | (static_cast<uint32_t>(ctx->opaque_background.red) << 16) |
                                  (static_cast<uint32_t>(ctx->opaque_background.green) << 8) |
                                  static_cast<uint32_t>(ctx->opaque_background.blue);
      const size_t pixel_count = static_cast<size_t>(ctx->width) * ctx->height;
      // Completion artwork normally grows monotonically. Inspecting the last
      // eight immutable cache slots is enough to reject a blank/out-point
      // frame without scanning the complete 11 MB boot cache again.
      const uint32_t first_index = ctx->frame_cache_count > 8U ? ctx->frame_cache_count - 8U : 0U;
      for (uint32_t index = first_index; index < ctx->frame_cache_count; index++) {
        const auto *pixels =
            reinterpret_cast<const uint32_t *>(ctx->frame_cache + ctx->frame_cache_stride * static_cast<size_t>(index));
        uint32_t coverage = 0;
        for (size_t pixel = 0; pixel < pixel_count; pixel++)
          coverage += pixels[pixel] != background;
        if (index == ctx->frame_cache_count - 1U)
          final_coverage = coverage;
        if (coverage >= retained_coverage) {
          retained_coverage = coverage;
          retained_index = index;
        }
      }
      ESP_LOGI(LOTTIE_PERF_TAG, "final frame retain: index=%u/%u coverage=%u final=%u", (unsigned) retained_index,
               (unsigned) (ctx->frame_cache_count - 1U), (unsigned) retained_coverage, (unsigned) final_coverage);
    }
    const uint8_t *final_frame = ctx->frame_cache + ctx->frame_cache_stride * static_cast<size_t>(retained_index);
    const size_t display_bytes = lottie_display_buffer_bytes(ctx);
    lv_display_t *display = nullptr;
    lv_area_t direct_coords{};
    bool release_direct_region = false;
    lv_lock();
    lvgl_esphome_release_dma_image_buffer(ctx->pixel_buffer);
    memcpy(ctx->pixel_buffer, final_frame, display_bytes);
    lottie_sync_buffer(ctx->pixel_buffer, display_bytes);
    if (ctx->obj != nullptr && lv_obj_is_valid(ctx->obj)) {
      if (ctx->flatten_to_opaque) {
        lottie_attach_opaque_buffer(ctx);
      } else {
        lv_canvas_set_buffer(ctx->obj, ctx->pixel_buffer, ctx->width, ctx->height, lottie_argb_color_format(ctx));
        lv_draw_buf_t *draw_buf = lv_canvas_get_draw_buf(ctx->obj);
        if (draw_buf != nullptr) {
          if (ctx->fast_radial)
            lv_draw_buf_clear_flag(draw_buf, LV_IMAGE_FLAGS_PREMULTIPLIED);
          else
            lv_draw_buf_set_flag(draw_buf, LV_IMAGE_FLAGS_PREMULTIPLIED);
        }
      }
      lv_obj_invalidate(ctx->obj);
      display = lv_obj_get_display(ctx->obj);
      lv_obj_get_coords(ctx->obj, &direct_coords);
      release_direct_region = lv_area_get_width(&direct_coords) == static_cast<int32_t>(ctx->width) &&
                              lv_area_get_height(&direct_coords) == static_cast<int32_t>(ctx->height);
    }
    lv_unlock();

    // Cached frames are presented through the direct-region path. Commit the
    // canvas-owned final frame before releasing that cache so the wordmark
    // fade cannot redraw the stale pre-cache canvas over the completed trace.
    if (release_direct_region) {
      lvgl_esphome_direct_blit_rgb888_release(direct_coords.x1, direct_coords.y1, ctx->width, ctx->height);
    }
    if (display != nullptr && !lvgl_esphome_snapshot_is_active()) {
      if (!lvgl_esphome_direct_handoff_to_lvgl(100)) {
        ESP_LOGW(LOTTIE_PERF_TAG, "final frame handoff: unable to seed LVGL direct buffers");
      }
      lv_lock();
      lv_refr_now(display);
      lv_unlock();
    }
  }

  heap_caps_free(ctx->frame_cache);
  ctx->frame_cache = nullptr;
  ctx->frame_cache_stride = 0;
  ctx->frame_cache_count = 0;
  ctx->frame_cache_internal = false;
  lvgl_esphome_direct_regions_pause(false, 0);
  return true;
}

inline void lottie_pause(LottieContext *ctx, bool hide = true) {
  if (ctx == nullptr || ctx->obj == nullptr) {
    return;
  }
  ctx->runtime_hidden = true;
  ctx->auto_start = false;
  ctx->reveal_after_restart = false;
  if (hide) {
    lv_obj_add_flag(ctx->obj, LV_OBJ_FLAG_HIDDEN);
  }
  if (ctx->task_handle != nullptr) {
    xTaskNotifyGive(ctx->task_handle);
  }
  lv_obj_invalidate(ctx->obj);
}

// Stop a direct Lottie producer before a framebuffer compositor takes over.
// The render task observes runtime_hidden, drains its own in-flight frame and
// releases the regional lease. Avoiding lv_obj_invalidate() and a synchronous
// wait here keeps the touchscreen/navigation callback non-blocking.
inline void lottie_pause_for_navigation(LottieContext *ctx) {
  if (ctx == nullptr || ctx->obj == nullptr)
    return;
  ctx->runtime_hidden = true;
  ctx->auto_start = false;
  ctx->reveal_after_restart = false;
  if (ctx->task_handle != nullptr)
    xTaskNotifyGive(ctx->task_handle);
}

inline void lottie_hide(LottieContext *ctx) {
  if (ctx == nullptr || ctx->obj == nullptr) {
    return;
  }
  ctx->runtime_hidden = true;
  ctx->auto_start = false;
  ctx->reveal_after_restart = false;
  lv_obj_add_flag(ctx->obj, LV_OBJ_FLAG_HIDDEN);
  lv_obj_invalidate(ctx->obj);
  lottie_free_resources(ctx);
}

// Hidden mutually exclusive scenes must not retain every parsed JSON tree,
// RLE span cache and compositor surface. Call only after the worker stopped.
inline void lottie_release_scene(LottieContext *ctx) {
  if (ctx == nullptr || ctx->obj == nullptr || ctx->task_handle != nullptr)
    return;
  auto *lottie = reinterpret_cast<lv_lottie_t *>(ctx->obj);
  lv_lock();
  if (lottie->tvg_anim != nullptr)
    tvg_animation_del(lottie->tvg_anim);
  if (lottie->tvg_canvas != nullptr)
    tvg_canvas_destroy(lottie->tvg_canvas);
  lottie->tvg_anim = nullptr;
  lottie->tvg_paint = nullptr;
  lottie->tvg_canvas = nullptr;
  ctx->data_loaded = false;
  ctx->prepared_frame_ready = false;
  ctx->thorvg_target = nullptr;
  lv_unlock();
}

inline void lottie_unload(LottieContext *ctx) {
  lottie_hide(ctx);
  lottie_release_scene(ctx);
}

// Release a navigation-owned animation without enqueueing a full LVGL
// invalidation. The application transition already owns the display handoff;
// invalidating the hidden weather canvas here can compete with its first
// frame. The next Home presentation calls lottie_prepare() and recreates the
// resources on demand.
inline void lottie_release_resources_for_navigation(LottieContext *ctx) {
  if (ctx == nullptr || ctx->obj == nullptr)
    return;
  ctx->runtime_hidden = true;
  ctx->auto_start = false;
  ctx->reveal_after_restart = false;
  lv_obj_add_flag(ctx->obj, LV_OBJ_FLAG_HIDDEN);
  if (ctx->task_handle != nullptr)
    xTaskNotifyGive(ctx->task_handle);
  lottie_free_resources(ctx);
}

// --------------------------------------------------------------------------
// Public API: initialise Lottie widget – allocate buffer, register screen
// events, and launch the load/render task.
// Call under lv_lock (from LVGL init code).
// --------------------------------------------------------------------------
inline bool lottie_init(lv_obj_t *obj, const void *data, size_t data_size, const char *file_path, uint32_t width,
                        uint32_t height, bool loop, bool auto_start, bool user_wants_hidden,
                        bool retain_on_unload = false, bool flatten_to_opaque = false,
                        lv_color_t opaque_background = lv_color_hex(0x000000), uint32_t play_count = 0,
                        uint32_t render_width = 0, uint32_t render_height = 0, bool fast_radial = false,
                        lv_color_t fast_radial_fill_color = lv_color_hex(0x46B1E1),
                        lv_color_t fast_radial_outline_color = lv_color_hex(0x0D0D0D)) {
  LottieContext *ctx = (LottieContext *) heap_caps_malloc(sizeof(LottieContext), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!ctx)
    return false;
  memset(ctx, 0, sizeof(LottieContext));
  ctx->next_context = lottie_contexts;
  lottie_contexts = ctx;

  ctx->obj = obj;
  ctx->data = data;
  ctx->data_size = data_size;
  ctx->file_path = file_path;
  ctx->loop = loop;
  ctx->play_count = loop ? play_count : 1U;
  ctx->auto_start = auto_start;
  ctx->retain_on_unload = retain_on_unload;
  ctx->width = width;
  ctx->height = height;
  ctx->render_width = render_width == 0 ? width : render_width;
  ctx->render_height = render_height == 0 ? height : render_height;
  ctx->fast_radial = fast_radial;
  ctx->fast_radial_fill_color = fast_radial_fill_color;
  ctx->fast_radial_outline_color = fast_radial_outline_color;
  if (flatten_to_opaque && lottie_uses_scaled_render(ctx)) {
    LV_LOG_WARN("Opaque Lottie render scaling is not supported; using display dimensions");
    ctx->render_width = width;
    ctx->render_height = height;
  }
  ctx->flatten_to_opaque = flatten_to_opaque;
  ctx->opaque_background = opaque_background;
  ctx->user_wants_hidden = user_wants_hidden;  // Save user's 'hidden' config from YAML
  ctx->runtime_hidden = user_wants_hidden;     // Initially matches YAML config

  // Store context on the LVGL object so user scripts can retrieve it
  // via lv_obj_get_user_data() for lottie_restart() calls
  lv_obj_set_user_data(obj, ctx);

  // Register screen events for PSRAM lifecycle (two-phase unload).
  // IMPORTANT: We do NOT call lottie_launch() here.  Resources are only
  // allocated when the page actually becomes visible (SCREEN_LOADED event).
  // This prevents non-active pages from consuming PSRAM at startup.
  // With 19+ widgets across multiple pages, this saves megabytes of PSRAM.
  lv_obj_t *screen = lv_obj_get_screen(obj);
  lv_obj_add_event_cb(screen, lottie_screen_unload_start_cb, LV_EVENT_SCREEN_UNLOAD_START, ctx);
  lv_obj_add_event_cb(screen, lottie_screen_unloaded_cb, LV_EVENT_SCREEN_UNLOADED, ctx);
  lv_obj_add_event_cb(screen, lottie_screen_loaded_cb, LV_EVENT_SCREEN_LOADED, ctx);

  LV_LOG_INFO("Lottie registered (display=%ux%u render=%ux%u), waiting for page load to allocate", (unsigned) width,
              (unsigned) height, (unsigned) ctx->render_width, (unsigned) ctx->render_height);
  return true;
}

}  // namespace lvgl
}  // namespace esphome

#endif  // LV_USE_LOTTIE
#endif  // USE_ESP32
