#pragma once

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
#include "esp_timer.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <cstring>

// Access lv_lottie_t internals for safe re-initialisation on screen re-load.
// Needed to null out the dangling anim pointer and to clear the ThorVG canvas
// before re-pushing the paint.
#include <src/widgets/lottie/lv_lottie_private.h>

extern "C" uint32_t lvgl_esphome_get_perf_logging_enabled(void);
extern "C" bool lvgl_esphome_direct_blit_xrgb8888(const uint8_t *src, int src_stride, int x, int y, int width,
                                                    int height);
extern "C" bool lvgl_esphome_direct_blit_xrgb8888_coherent(const uint8_t *src, int src_stride, int x, int y,
                                                             int width, int height);
extern "C" bool lvgl_esphome_direct_regions_pause(bool paused, uint32_t timeout_ms);
extern "C" bool esphome_mipi_dsi_wait_fifo_margin(uint32_t min_depth, uint32_t timeout_us) __attribute__((weak));

namespace esphome {
namespace lvgl {

static constexpr size_t LOTTIE_TASK_STACK_SIZE = 64 * 1024;
static constexpr size_t LOTTIE_CACHE_ALIGN = 128;
static constexpr size_t LOTTIE_INTERNAL_BUFFER_MAX_BYTES = 256 * 1024;
static constexpr size_t LOTTIE_INTERNAL_BUFFER_HEADROOM_BYTES = 48 * 1024;
static constexpr size_t LOTTIE_DOUBLE_BUFFER_MAX_BYTES = 512 * 1024;
static constexpr size_t LOTTIE_FRAME_CACHE_MAX_BYTES = 12 * 1024 * 1024;
static constexpr uint32_t LOTTIE_FRAME_CACHE_TARGET_FPS = 60;
static const char *const LOTTIE_PERF_TAG = "lottie.perf";

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
    const void *data;           // PROGMEM (embedded) or nullptr
    size_t data_size;
    const char *file_path;      // string literal or nullptr
    bool loop;
    bool auto_start;
    uint32_t width;
    uint32_t height;
    bool flatten_to_opaque;
    lv_color_t opaque_background;

    // --- Animation params (captured on first load, reused on re-loads) ---
    lv_anim_exec_xcb_t exec_cb;
    void *anim_var;
    int32_t start_frame;
    int32_t end_frame;
    uint32_t duration_ms;
    bool data_loaded;           // true after first successful parse

    // --- Runtime state (freed on screen unload) ---
    uint8_t *pixel_buffer;      // buffer currently presented by LVGL
    uint8_t *work_buffer;       // ThorVG render target, swapped after a complete frame
    uint8_t *display_back_buffer;  // non-black opaque RGB frame prepared while LVGL presents pixel_buffer
    uint8_t *frame_cache;       // optional pre-rendered frames for tiny boot Lotties
    size_t frame_cache_stride;
    uint32_t frame_cache_count;
    bool pixel_buffer_internal;
    bool work_buffer_internal;
    bool display_back_buffer_internal;
    bool frame_cache_internal;
    bool prepared_frame_ready;
    volatile bool playback_completed;
    StackType_t *task_stack;    // PSRAM – 64 KB
    StaticTask_t *task_tcb;     // internal RAM
    TaskHandle_t task_handle;
    volatile bool stop_requested;
    volatile bool restart_requested;  // ✅ Flag to restart animation from frame 0
    TickType_t start_tick;       // legacy task clock
    int64_t start_time_us;       // monotonic animation clock (can be reset)
    bool user_wants_hidden;     // Save user's 'hidden' config from YAML
    bool runtime_hidden;        // Actual visibility at time of unload (captures script changes)
};

inline size_t lottie_align_up(size_t value, size_t align) {
    return (value + align - 1) & ~(align - 1);
}

inline void lottie_wait_display_fifo() {
#if CONFIG_ESPHOME_LOTTIE_DSI_BACKPRESSURE
    if (esphome_mipi_dsi_wait_fifo_margin != nullptr) {
        esphome_mipi_dsi_wait_fifo_margin(CONFIG_ESPHOME_LOTTIE_DSI_FIFO_MIN,
                                          CONFIG_ESPHOME_LOTTIE_DSI_WAIT_US);
    }
#endif
}

inline void lottie_sync_buffer(uint8_t *data, size_t len) {
    if (data == nullptr || len == 0) return;
    if (!esp_ptr_external_ram(data)) return;

    uintptr_t start = reinterpret_cast<uintptr_t>(data) & ~(LOTTIE_CACHE_ALIGN - 1);
    uintptr_t end = lottie_align_up(reinterpret_cast<uintptr_t>(data) + len, LOTTIE_CACHE_ALIGN);
    if (end <= start) return;
    if (!esp_ptr_external_ram(reinterpret_cast<const void *>(start)) ||
        !esp_ptr_external_ram(reinterpret_cast<const void *>(end - 1))) {
        return;
    }

    esp_cache_msync(reinterpret_cast<void *>(start), end - start,
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);
}

inline void lottie_sync_canvas_buffer(LottieContext *ctx) {
    if (ctx == nullptr || ctx->obj == nullptr) return;
    lv_draw_buf_t *draw_buf = lv_canvas_get_draw_buf(ctx->obj);
    if (draw_buf == nullptr || draw_buf->data == nullptr) return;
    size_t len = draw_buf->data_size;
    if (len == 0 && draw_buf->header.stride > 0 && draw_buf->header.h > 0) {
        len = static_cast<size_t>(draw_buf->header.stride) * draw_buf->header.h;
    }
    lottie_sync_buffer(static_cast<uint8_t *>(draw_buf->data), len);
}

inline size_t lottie_buffer_bytes(const LottieContext *ctx) {
    const uint32_t stride = lv_draw_buf_width_to_stride(ctx->width, LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED);
    return static_cast<size_t>(stride) * ctx->height;
}

inline bool lottie_uses_direct_xrgb(const LottieContext *ctx) {
#if defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
    return ctx != nullptr && ctx->flatten_to_opaque;
#else
    return false;
#endif
}

inline size_t lottie_display_buffer_bytes(const LottieContext *ctx) {
    lv_color_format_t cf = LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED;
    if (lottie_uses_direct_xrgb(ctx)) {
        cf = LV_COLOR_FORMAT_XRGB8888;
    } else if (ctx->flatten_to_opaque) {
        cf = LV_COLOR_FORMAT_RGB888;
    }
    return static_cast<size_t>(lv_draw_buf_width_to_stride(ctx->width, cf)) * ctx->height;
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

inline void lottie_fill_display_buffer(LottieContext *ctx, uint8_t *target, size_t bytes) {
    if (ctx == nullptr || target == nullptr || bytes == 0) return;

    if (lottie_uses_direct_xrgb(ctx)) {
        const uint32_t background =
            0xFF000000U |
            (static_cast<uint32_t>(ctx->opaque_background.red) << 16) |
            (static_cast<uint32_t>(ctx->opaque_background.green) << 8) |
            static_cast<uint32_t>(ctx->opaque_background.blue);
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

// ThorVG owns only the work buffer while rendering. LVGL keeps reading the
// previous complete frame and is locked only for the short source swap.
inline bool lottie_render_frame(LottieContext *ctx, int32_t frame, uint8_t *target) {
    if (ctx == nullptr || ctx->obj == nullptr || target == nullptr) return false;
    auto *lottie = reinterpret_cast<lv_lottie_t *>(ctx->obj);
    if (lottie->tvg_canvas == nullptr || lottie->tvg_anim == nullptr) return false;

    const uint32_t stride = lv_draw_buf_width_to_stride(ctx->width, LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED);
    const size_t bytes = static_cast<size_t>(stride) * ctx->height;
    if (lottie_uses_direct_xrgb(ctx)) {
        // Start ThorVG on the final opaque background. Its software rasterizer
        // blends the vector paint directly into this target, so publishing can
        // expose the same completed buffer as XRGB without a second full-frame
        // flatten pass. In memory the ARGB8888 target is BGRA on little-endian
        // ESP32, hence the packed 0xAARRGGBB value below.
        const uint32_t background =
            0xFF000000U |
            (static_cast<uint32_t>(ctx->opaque_background.red) << 16) |
            (static_cast<uint32_t>(ctx->opaque_background.green) << 8) |
            static_cast<uint32_t>(ctx->opaque_background.blue);
        std::fill_n(reinterpret_cast<uint32_t *>(target), bytes / sizeof(uint32_t), background);
    } else {
        memset(target, 0, bytes);
    }
    if (tvg_swcanvas_set_target(lottie->tvg_canvas, reinterpret_cast<uint32_t *>(target),
                                stride / 4, ctx->width, ctx->height,
                                TVG_COLORSPACE_ARGB8888) != TVG_RESULT_SUCCESS) {
        return false;
    }
    const Tvg_Result set_frame_result = tvg_animation_set_frame(lottie->tvg_anim, static_cast<float>(frame));
    const Tvg_Result update_result = set_frame_result == TVG_RESULT_SUCCESS
                                         ? tvg_canvas_update(lottie->tvg_canvas)
                                         : set_frame_result;
    const Tvg_Result draw_result = update_result == TVG_RESULT_SUCCESS
                                       ? tvg_canvas_draw(lottie->tvg_canvas)
                                       : update_result;
    const Tvg_Result sync_result = draw_result == TVG_RESULT_SUCCESS
                                       ? tvg_canvas_sync(lottie->tvg_canvas)
                                       : draw_result;
    if (sync_result != TVG_RESULT_SUCCESS) {
        ESP_LOGW(LOTTIE_PERF_TAG, "render frame failed frame=%d set=%d update=%d draw=%d sync=%d",
                 (int) frame, (int) set_frame_result, (int) update_result,
                 (int) draw_result, (int) sync_result);
        return false;
    }
    // The direct XRGB path is consumed exclusively by the PPA image unit,
    // which performs C2M cache synchronization immediately before DMA reads
    // the source. Syncing here as well traverses the complete frame twice.
    if (!lottie_uses_direct_xrgb(ctx)) {
        lottie_sync_buffer(target, bytes);
    }
    return true;
}

inline void lottie_publish_work_buffer(LottieContext *ctx) {
    if (ctx == nullptr || ctx->work_buffer == nullptr) return;
    uint8_t *ready = ctx->work_buffer;
    const bool ready_internal = ctx->work_buffer_internal;

    lv_lock();
    lv_canvas_set_buffer(ctx->obj, ready, ctx->width, ctx->height,
                         LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED);
    lv_draw_buf_t *draw_buf = lv_canvas_get_draw_buf(ctx->obj);
    if (draw_buf != nullptr) {
        lv_draw_buf_set_flag(draw_buf, LV_IMAGE_FLAGS_PREMULTIPLIED);
    }
    lv_obj_invalidate(ctx->obj);
    lv_unlock();

    ctx->work_buffer = ctx->pixel_buffer;
    ctx->work_buffer_internal = ctx->pixel_buffer_internal;
    ctx->pixel_buffer = ready;
    ctx->pixel_buffer_internal = ready_internal;
}

// ThorVG produces premultiplied BGRA. Flattening it once onto a known solid
// background turns the repeatedly rendered LVGL source into opaque RGB888.
// That lets PPA copy each frame directly instead of software-blending every
// pixel whenever LVGL refreshes the widget.
inline void lottie_flatten_opaque_frame(LottieContext *ctx, const uint8_t *src, uint8_t *dst) {
    const uint32_t src_stride =
        lv_draw_buf_width_to_stride(ctx->width, LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED);
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
            dst_px[0] = static_cast<uint8_t>(
                std::min<uint16_t>(255U, src_px[0] + (inv_alpha * bg_b + 127U) / 255U));
            dst_px[1] = static_cast<uint8_t>(
                std::min<uint16_t>(255U, src_px[1] + (inv_alpha * bg_g + 127U) / 255U));
            dst_px[2] = static_cast<uint8_t>(
                std::min<uint16_t>(255U, src_px[2] + (inv_alpha * bg_r + 127U) / 255U));
        }
    }
}

inline void lottie_attach_opaque_buffer(LottieContext *ctx) {
    const lv_color_format_t cf =
        lottie_uses_direct_xrgb(ctx) ? LV_COLOR_FORMAT_XRGB8888 : LV_COLOR_FORMAT_RGB888;
    lv_canvas_set_buffer(ctx->obj, ctx->pixel_buffer, ctx->width, ctx->height, cf);
    lv_draw_buf_t *draw_buf = lv_canvas_get_draw_buf(ctx->obj);
    if (draw_buf != nullptr) {
        lv_draw_buf_clear_flag(draw_buf, LV_IMAGE_FLAGS_PREMULTIPLIED);
    }
}

inline void lottie_publish_opaque_frame(LottieContext *ctx) {
    if (ctx == nullptr || ctx->pixel_buffer == nullptr || ctx->work_buffer == nullptr) {
        return;
    }

    // ThorVG's premultiplied RGB channels already contain the final pixels for
    // a black background. Expose the complete render buffer as opaque XRGB and
    // let the existing LVGL PPA SRM draw unit convert+copy it to RGB888 in one
    // hardware pass. This removes the intermediate RGB frame and a second
    // full-frame PSRAM traversal from every animation frame.
    if (lottie_uses_direct_xrgb(ctx)) {
        uint8_t *ready = ctx->work_buffer;
        const bool ready_internal = ctx->work_buffer_internal;

        lv_lock();
        lv_canvas_set_buffer(ctx->obj, ready, ctx->width, ctx->height, LV_COLOR_FORMAT_XRGB8888);
        lv_draw_buf_t *draw_buf = lv_canvas_get_draw_buf(ctx->obj);
        if (draw_buf != nullptr) {
            lv_draw_buf_clear_flag(draw_buf, LV_IMAGE_FLAGS_PREMULTIPLIED);
        }
        lv_obj_invalidate(ctx->obj);
        lv_unlock();

        ctx->work_buffer = ctx->pixel_buffer;
        ctx->work_buffer_internal = ctx->pixel_buffer_internal;
        ctx->pixel_buffer = ready;
        ctx->pixel_buffer_internal = ready_internal;
        return;
    }

    if (ctx->display_back_buffer == nullptr) return;

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

inline uint8_t *lottie_alloc_pixel_buffer(size_t alloc_bytes, bool *internal) {
    if (internal != nullptr) {
        *internal = false;
    }

    if (alloc_bytes <= LOTTIE_INTERNAL_BUFFER_MAX_BYTES) {
        size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (largest_internal >= alloc_bytes + LOTTIE_INTERNAL_BUFFER_HEADROOM_BYTES) {
            uint8_t *buf = (uint8_t *)heap_caps_aligned_alloc(
                LOTTIE_CACHE_ALIGN, alloc_bytes,
                MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
            if (buf != nullptr) {
                if (internal != nullptr) {
                    *internal = true;
                }
                return buf;
            }
        }
    }

    return (uint8_t *)heap_caps_aligned_alloc(
        LOTTIE_CACHE_ALIGN, alloc_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
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
        ESP_LOGI(LOTTIE_PERF_TAG, "frame cache skipped: need=%u max=%u frames=%u stride=%u",
                 (unsigned) cache_bytes, (unsigned) LOTTIE_FRAME_CACHE_MAX_BYTES,
                 (unsigned) frame_count, (unsigned) frame_stride);
        return false;
    }
    if (heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) < cache_bytes) {
        ESP_LOGI(LOTTIE_PERF_TAG, "frame cache skipped: need=%u largest_psram=%u frames=%u stride=%u",
                 (unsigned) cache_bytes,
                 (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
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
        memcpy(cache, ctx->pixel_buffer, display_bytes);
        lottie_sync_buffer(cache, display_bytes);
        lottie_wait_display_fifo();
    }

    for (uint32_t i = 1; i < frame_count; i++) {
        const int32_t frame = first_frame + static_cast<int32_t>(
            (static_cast<int64_t>(total_frames) * i + (frame_count - 1) / 2) / (frame_count - 1));
        uint8_t *target = cache + frame_stride * i;
        if (!lottie_render_frame(ctx, frame, target)) {
            heap_caps_free(cache);
            return false;
        }
        lottie_sync_buffer(target, display_bytes);
        lottie_wait_display_fifo();
        taskYIELD();
    }

    ctx->frame_cache = cache;
    ctx->frame_cache_stride = frame_stride;
    ctx->frame_cache_count = frame_count;
    ctx->frame_cache_internal = false;
    ESP_LOGI(LOTTIE_PERF_TAG, "prepared frame cache: frames=%u bytes=%u first=%d last=%d",
             (unsigned) frame_count, (unsigned) cache_bytes, (int) first_frame, (int) last_frame);
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
        presented_direct = lvgl_esphome_direct_blit_xrgb8888_coherent(
            frame, stride, coords.x1, coords.y1, ctx->width, ctx->height);
    }

    if (!presented_direct) {
        lv_lock();
        memcpy(ctx->pixel_buffer, frame, display_bytes);
        if (lv_obj_is_valid(ctx->obj)) {
            lv_obj_invalidate(ctx->obj);
        }
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
        if (window_us == 0) window_us = now_us;
        if (now_us - window_us >= 2000000) {
            ESP_LOGI(LOTTIE_PERF_TAG,
                     "cache2s: frames=%u direct=%u avg=%uus max=%uus gap_max=%uus@%u",
                     static_cast<unsigned>(frames), static_cast<unsigned>(direct_frames),
                     static_cast<unsigned>(total_us / std::max<uint32_t>(1U, frames)),
                     static_cast<unsigned>(max_us), static_cast<unsigned>(max_gap_us),
                     static_cast<unsigned>(max_gap_index));
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
    LottieContext *ctx = (LottieContext *)param;

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
            LV_LOG_INFO("Data loaded from embedded source (%d bytes)", (int)ctx->data_size);
        } else if (ctx->file_path != nullptr) {
            lv_lottie_set_src_file(ctx->obj, ctx->file_path);
            LV_LOG_INFO("Data loaded from file: %s", ctx->file_path);
        }

        // Capture animation parameters before deleting the LVGL animation
        lv_anim_t *anim = lv_lottie_get_anim(ctx->obj);
        if (anim != nullptr) {
            ctx->exec_cb     = anim->exec_cb;
            ctx->anim_var    = anim->var;
            ctx->start_frame = anim->start_value;
            ctx->end_frame   = anim->end_value;
            ctx->duration_ms = (uint32_t)lv_anim_get_time(anim);

            LV_LOG_INFO("Anim: frames %d..%d, duration %u ms",
                     (int)ctx->start_frame, (int)ctx->end_frame, (unsigned)ctx->duration_ms);

            // Delete the LVGL animation – we drive rendering ourselves
            // from this PSRAM task instead of the main task (small stack).
            lv_anim_delete(ctx->anim_var, ctx->exec_cb);

            // CRITICAL: null out the dangling pointer in lv_lottie_t.
            // Without this, anim_exec_cb (called by lv_lottie_set_buffer
            // on re-load) would dereference freed memory.
            lv_lottie_t *lottie = (lv_lottie_t *)ctx->obj;
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

        lv_lottie_t *lottie = (lv_lottie_t *)ctx->obj;
        tvg_canvas_clear(lottie->tvg_canvas, false);

        // Safe to call: widget is hidden (lv_obj_is_visible → false)
        // and lottie->anim is NULL (no dangling pointer access).
        lv_lottie_set_buffer(ctx->obj, ctx->width, ctx->height, ctx->pixel_buffer);
    }

    // Render the first frame before the object can become visible.  The pixel
    // buffer exists at this point, but without this draw LVGL can briefly flush
    // the blank buffer created in lottie_launch().
    if (ctx->data_loaded && ctx->exec_cb != nullptr && ctx->anim_var != nullptr &&
        ctx->end_frame > ctx->start_frame) {
        ctx->exec_cb(ctx->anim_var, ctx->start_frame);
        lottie_sync_canvas_buffer(ctx);
        lv_obj_invalidate(ctx->obj);
    }

    const bool reveal_after_prepare =
        !ctx->runtime_hidden && ctx->data_loaded && ctx->exec_cb != nullptr &&
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
    if (!ctx->data_loaded || ctx->exec_cb == nullptr ||
        ctx->duration_ms == 0 || ctx->end_frame <= ctx->start_frame) {
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
    uint32_t frame_delay_ms = ctx->duration_ms / (uint32_t)total_frames;
    if (frame_delay_ms < 16)  frame_delay_ms = 16;   // Cap at ~60 fps
    if (frame_delay_ms > 100) frame_delay_ms = 100;

    LV_LOG_INFO("Render loop: %u ms/frame, loop=%d",
             (unsigned)frame_delay_ms, (int)ctx->loop);

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

        uint32_t elapsed_ms = (uint32_t)((xTaskGetTickCount() - ctx->start_tick) * portTICK_PERIOD_MS);

        int32_t frame;
        if (ctx->loop) {
            uint32_t phase = elapsed_ms % ctx->duration_ms;
            frame = ctx->start_frame + (int32_t)((int64_t)total_frames * phase / ctx->duration_ms);
        } else {
            if (elapsed_ms >= ctx->duration_ms) {
                lv_lock();
                ctx->exec_cb(ctx->anim_var, lottie_last_renderable_frame(ctx));
                lottie_sync_canvas_buffer(ctx);
                lv_unlock();
                LV_LOG_INFO("Animation complete");
                break;
            }
            frame = ctx->start_frame + (int32_t)((int64_t)total_frames * elapsed_ms / ctx->duration_ms);
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
            uint32_t render_us = (uint32_t)(sync_start_us - render_start_us);
            uint32_t sync_us = (uint32_t)(sync_end_us - sync_start_us);
            perf_frames++;
            perf_render_us += render_us;
            perf_sync_us += sync_us;
            if (render_us > perf_render_max_us) perf_render_max_us = render_us;
            if (sync_us > perf_sync_max_us) perf_sync_max_us = sync_us;

            int64_t now_us = sync_end_us;
            if (now_us - perf_last_log_us >= 2000000 && perf_frames > 0) {
                LV_LOG_INFO("perf2s: frames=%u render_avg=%lluus render_max=%uus sync_avg=%lluus sync_max=%uus",
                         (unsigned)perf_frames,
                         (unsigned long long)(perf_render_us / perf_frames),
                         (unsigned)perf_render_max_us,
                         (unsigned long long)(perf_sync_us / perf_frames),
                         (unsigned)perf_sync_max_us);
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
// the global LVGL lock. A complete frame is then published with a short buffer
// swap, so unrelated widgets and input handling can continue on the other core.
inline void lottie_render_task(void *param) {
    auto *ctx = static_cast<LottieContext *>(param);
    vTaskDelay(pdMS_TO_TICKS(10));

    lv_lock();
    if (!ctx->data_loaded) {
        uint8_t *render_target = ctx->flatten_to_opaque ? ctx->work_buffer : ctx->pixel_buffer;
        lv_lottie_set_buffer(ctx->obj, ctx->width, ctx->height, render_target);
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
        uint8_t *render_target = ctx->flatten_to_opaque ? ctx->work_buffer : ctx->pixel_buffer;
        lv_lottie_set_buffer(ctx->obj, ctx->width, ctx->height, render_target);
        if (ctx->flatten_to_opaque) {
            lottie_attach_opaque_buffer(ctx);
        }
    }
    lv_unlock();

    if (!ctx->data_loaded || ctx->exec_cb == nullptr || ctx->anim_var == nullptr ||
        ctx->duration_ms == 0 || ctx->end_frame <= ctx->start_frame) {
        LV_LOG_WARN("No valid animation, task suspending");
        vTaskSuspend(nullptr);
    }

    // Render frame zero while hidden. LVGL's native callback deliberately
    // skips hidden objects, which previously left the boot canvas blank until
    // the first visible timer tick.
    uint8_t *first_target = ctx->work_buffer != nullptr ? ctx->work_buffer : ctx->pixel_buffer;
    const int32_t prepare_frame = lottie_first_renderable_frame(ctx);
    const bool first_frame_ok = lottie_render_frame(ctx, prepare_frame, first_target);
    if (first_frame_ok) {
        if (ctx->flatten_to_opaque) {
            lottie_publish_opaque_frame(ctx);
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

    if (ctx->prepared_frame_ready && !ctx->runtime_hidden && ctx->auto_start) {
        lv_lock();
        lv_obj_remove_flag(ctx->obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_invalidate(ctx->obj);
        lv_unlock();
    }

    const int32_t total_frames = ctx->end_frame - ctx->start_frame;
    int32_t last_frame = lottie_first_renderable_frame(ctx);
    int32_t last_cache_index = 0;
    bool completed = false;
    ctx->start_time_us = esp_timer_get_time();

    uint32_t perf_frames = 0;
    uint64_t perf_render_us = 0;
    uint64_t perf_present_us = 0;
    uint32_t perf_render_max_us = 0;
    uint32_t perf_present_max_us = 0;
    int64_t perf_last_log_us = ctx->start_time_us;

    while (!ctx->stop_requested) {
        if (ctx->restart_requested) {
            ctx->start_time_us = esp_timer_get_time();
            ctx->restart_requested = false;
            last_frame = -1;
            last_cache_index = -1;
            completed = false;
            ctx->playback_completed = false;
        }

        if (!ctx->auto_start || completed) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            if (ctx->stop_requested) break;
            continue;
        }

        if (ctx->runtime_hidden) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        const int64_t now_us = esp_timer_get_time();
        const uint32_t elapsed_ms = static_cast<uint32_t>((now_us - ctx->start_time_us) / 1000);
        uint32_t phase_ms = elapsed_ms;
        uint32_t cycle_base_ms = 0;
        if (ctx->loop) {
            phase_ms = elapsed_ms % ctx->duration_ms;
            cycle_base_ms = elapsed_ms - phase_ms;
        } else if (elapsed_ms >= ctx->duration_ms) {
            phase_ms = ctx->duration_ms;
            completed = true;
        }

        int32_t frame = lottie_last_renderable_frame(ctx);
        if (!completed) {
            frame = ctx->start_frame + static_cast<int32_t>(
                static_cast<int64_t>(total_frames) * phase_ms / ctx->duration_ms);
            if (frame <= ctx->start_frame) {
                frame = lottie_first_renderable_frame(ctx);
            }
        }

        if (ctx->frame_cache != nullptr && ctx->frame_cache_count > 1) {
            uint32_t cache_index = ctx->frame_cache_count - 1;
            if (!completed) {
                cache_index = std::min<uint32_t>(
                    ctx->frame_cache_count - 1,
                    static_cast<uint32_t>(
                        (static_cast<uint64_t>(phase_ms) * (ctx->frame_cache_count - 1)) /
                        ctx->duration_ms));
            }
            if (static_cast<int32_t>(cache_index) != last_cache_index) {
                lottie_publish_cached_frame(ctx, cache_index);
                last_cache_index = static_cast<int32_t>(cache_index);
            }
            last_frame = frame;
        } else if (frame != last_frame) {
            const bool perf_enabled = lvgl_esphome_get_perf_logging_enabled() != 0;
            const int64_t render_start_us = perf_enabled ? esp_timer_get_time() : 0;

            if (ctx->flatten_to_opaque) {
                if (lottie_render_frame(ctx, frame, ctx->work_buffer)) {
                    const int64_t present_start_us = perf_enabled ? esp_timer_get_time() : 0;
                    lottie_publish_opaque_frame(ctx);
                    const int64_t present_end_us = perf_enabled ? esp_timer_get_time() : 0;
                    if (perf_enabled) {
                        const uint32_t render_us = static_cast<uint32_t>(present_start_us - render_start_us);
                        const uint32_t present_us = static_cast<uint32_t>(present_end_us - present_start_us);
                        perf_frames++;
                        perf_render_us += render_us;
                        perf_present_us += present_us;
                        if (render_us > perf_render_max_us) perf_render_max_us = render_us;
                        if (present_us > perf_present_max_us) perf_present_max_us = present_us;
                    }
                }
            } else if (ctx->work_buffer != nullptr) {
                if (lottie_render_frame(ctx, frame, ctx->work_buffer)) {
                    const int64_t present_start_us = perf_enabled ? esp_timer_get_time() : 0;
                    lottie_publish_work_buffer(ctx);
                    const int64_t present_end_us = perf_enabled ? esp_timer_get_time() : 0;
                    if (perf_enabled) {
                        const uint32_t render_us = static_cast<uint32_t>(present_start_us - render_start_us);
                        const uint32_t present_us = static_cast<uint32_t>(present_end_us - present_start_us);
                        perf_frames++;
                        perf_render_us += render_us;
                        perf_present_us += present_us;
                        if (render_us > perf_render_max_us) perf_render_max_us = render_us;
                        if (present_us > perf_present_max_us) perf_present_max_us = present_us;
                    }
                }
            } else {
                lv_lock();
                ctx->exec_cb(ctx->anim_var, frame);
                lottie_sync_canvas_buffer(ctx);
                lv_unlock();
            }
            last_frame = frame;
        }

        // Publish completion only after the final frame has reached the
        // presentation buffer. Boot sequencing can then retain that frame
        // without guessing the animation duration in YAML.
        if (completed) ctx->playback_completed = true;

        const int64_t log_now_us = esp_timer_get_time();
        if (lvgl_esphome_get_perf_logging_enabled() != 0 &&
            log_now_us - perf_last_log_us >= 2000000 && perf_frames > 0) {
            ESP_LOGI(LOTTIE_PERF_TAG,
                     "perf2s: frames=%u render_avg=%lluus render_max=%uus prepare_publish_avg=%lluus "
                     "prepare_publish_max=%uus",
                     static_cast<unsigned>(perf_frames),
                     static_cast<unsigned long long>(perf_render_us / perf_frames),
                     static_cast<unsigned>(perf_render_max_us),
                     static_cast<unsigned long long>(perf_present_us / perf_frames),
                     static_cast<unsigned>(perf_present_max_us));
            perf_frames = 0;
            perf_render_us = 0;
            perf_present_us = 0;
            perf_render_max_us = 0;
            perf_present_max_us = 0;
            perf_last_log_us = log_now_us;
        }

        if (completed) continue;

        const uint32_t frame_index = static_cast<uint32_t>(frame - ctx->start_frame);
        uint32_t next_phase_ms = static_cast<uint32_t>(
            (static_cast<uint64_t>(frame_index + 1) * ctx->duration_ms + total_frames - 1) / total_frames);
        const uint32_t target_elapsed_ms = cycle_base_ms + next_phase_ms;
        const int64_t target_us = ctx->start_time_us + static_cast<int64_t>(target_elapsed_ms) * 1000;
        const int64_t remaining_us = target_us - esp_timer_get_time();
        if (remaining_us > 1000) {
            vTaskDelay(pdMS_TO_TICKS(static_cast<uint32_t>(remaining_us / 1000)));
        } else {
            // A late frame must still relinquish the CPU. taskYIELD() can run
            // this priority-2 task again immediately and starve lower-priority
            // work on the same core for the duration of an animation.
            vTaskDelay(1);
        }
    }

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
    if (ctx->task_stack)    { heap_caps_free(ctx->task_stack);    ctx->task_stack = nullptr; }
    if (ctx->task_tcb)      { heap_caps_free(ctx->task_tcb);      ctx->task_tcb = nullptr; }
    if (ctx->pixel_buffer)  { heap_caps_free(ctx->pixel_buffer);  ctx->pixel_buffer = nullptr; }
    if (ctx->work_buffer)   { heap_caps_free(ctx->work_buffer);   ctx->work_buffer = nullptr; }
    if (ctx->display_back_buffer) {
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

    LV_LOG_INFO("Lottie freed (%ux%u = %u KB %s buf + 64 KB stack)",
             (unsigned)ctx->width, (unsigned)ctx->height,
             (unsigned)(ctx->width * ctx->height * 4 / 1024),
             ctx->pixel_buffer_internal ? "SRAM" : "PSRAM");
    ctx->pixel_buffer_internal = false;
    ctx->work_buffer_internal = false;
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
        LV_LOG_ERROR("Pixel buffer alloc failed (%u bytes)", (unsigned)display_alloc_bytes);
        return false;
    }
    ctx->prepared_frame_ready = false;
    lottie_fill_display_buffer(ctx, ctx->pixel_buffer, display_bytes);

    ctx->display_back_buffer = nullptr;
    ctx->display_back_buffer_internal = false;
    if (ctx->flatten_to_opaque && !lottie_uses_direct_xrgb(ctx)) {
        ctx->display_back_buffer =
            lottie_alloc_pixel_buffer(display_alloc_bytes, &ctx->display_back_buffer_internal);
        if (ctx->display_back_buffer == nullptr) {
            LV_LOG_ERROR("RGB display back buffer alloc failed (%u bytes)",
                         (unsigned) display_alloc_bytes);
            lottie_free_resources(ctx);
            return false;
        }
        lottie_fill_display_buffer(ctx, ctx->display_back_buffer, display_bytes);
    }

    // Double buffering keeps incomplete ThorVG frames away from LVGL and lets
    // the renderer run without the global LVGL lock. It is intentionally
    // bounded so large application animations cannot consume excessive PSRAM.
    ctx->work_buffer = nullptr;
    ctx->work_buffer_internal = false;
    const size_t work_alloc_bytes = lottie_align_up(argb_bytes, LOTTIE_CACHE_ALIGN);
    if (ctx->flatten_to_opaque || work_alloc_bytes <= LOTTIE_DOUBLE_BUFFER_MAX_BYTES) {
        ctx->work_buffer = lottie_alloc_pixel_buffer(work_alloc_bytes, &ctx->work_buffer_internal);
        if (ctx->work_buffer != nullptr) {
            if (ctx->flatten_to_opaque) {
                memset(ctx->work_buffer, 0, work_alloc_bytes);
            } else {
                lottie_fill_display_buffer(ctx, ctx->work_buffer, work_alloc_bytes);
            }
        }
    }
    if (ctx->flatten_to_opaque && ctx->work_buffer == nullptr) {
        LV_LOG_ERROR("ARGB render buffer alloc failed (%u bytes)", (unsigned)work_alloc_bytes);
        lottie_free_resources(ctx);
        return false;
    }

    // Hide temporarily during async load (pixel buffer is blank)
    lv_obj_add_flag(ctx->obj, LV_OBJ_FLAG_HIDDEN);

    // Allocate task stack + TCB
    ctx->task_stack = (StackType_t *)heap_caps_malloc(
        LOTTIE_TASK_STACK_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ctx->task_tcb = (StaticTask_t *)heap_caps_malloc(
        sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!ctx->task_stack || !ctx->task_tcb) {
        LV_LOG_ERROR("Task alloc failed");
        lottie_free_resources(ctx);
        return false;
    }

    ctx->stop_requested = false;
    #if CONFIG_FREERTOS_UNICORE
    constexpr BaseType_t render_core = tskNO_AFFINITY;
    #elif defined(CONFIG_ESP_MAIN_TASK_AFFINITY_CPU0) && CONFIG_ESP_MAIN_TASK_AFFINITY_CPU0
    constexpr BaseType_t render_core = 1;
    #elif defined(CONFIG_ESP_MAIN_TASK_AFFINITY_CPU1) && CONFIG_ESP_MAIN_TASK_AFFINITY_CPU1
    constexpr BaseType_t render_core = 0;
    #else
    constexpr BaseType_t render_core = 1;
    #endif
    // Keep ThorVG on the core opposite ESPHome's main loop. This build pins
    // app_main to CPU0, so hard-coding CPU0 here made every vector frame
    // compete with LVGL scheduling and component callbacks.
    ctx->task_handle = xTaskCreateStaticPinnedToCore(
        lottie_render_task, "lottie_anim",
        LOTTIE_TASK_STACK_SIZE / sizeof(StackType_t),
        ctx, 2, ctx->task_stack, ctx->task_tcb, render_core);

    if (!ctx->task_handle) {
        lottie_free_resources(ctx);
        return false;
    }

    LV_LOG_INFO("Lottie launched (runtime_hidden=%d, %s: display=%u KB render=%u KB mode=%s + 64 KB stack, free PSRAM: %u KB, free SRAM: %u KB, largest SRAM: %u KB)",
             (int)ctx->runtime_hidden,
             ctx->pixel_buffer_internal ? "SRAM" : "PSRAM",
             (unsigned)(display_bytes / 1024),
             (unsigned)(ctx->work_buffer != nullptr ? argb_bytes / 1024 : 0),
             lottie_uses_direct_xrgb(ctx) ? "opaque XRGB8888/PPA" :
             (ctx->flatten_to_opaque ? "opaque RGB888" :
              (ctx->work_buffer != nullptr ? "double ARGB" : "single ARGB")),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024));
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
    LottieContext *ctx = (LottieContext *)lv_event_get_user_data(e);

    // Capture actual visibility BEFORE hiding – this preserves dynamic
    // show/hide from user scripts (e.g. weather widget selection).
    ctx->runtime_hidden = lv_obj_has_flag(ctx->obj, LV_OBJ_FLAG_HIDDEN);

    // Ask the render task to stop, but do not delete it from inside the LVGL
    // unload-start callback.  The task can be holding lv_lock() while rendering;
    // deleting it at that moment leaves LVGL locked and trips the watchdog.
    ctx->stop_requested = true;

    // Hide widget so LVGL won't try to draw the image during transition
    lv_obj_add_flag(ctx->obj, LV_OBJ_FLAG_HIDDEN);

    LV_LOG_INFO("Lottie task stopped, widget hidden (was_hidden=%d)", (int)ctx->runtime_hidden);
}

inline void lottie_screen_unloaded_cb(lv_event_t *e) {
    LottieContext *ctx = (LottieContext *)lv_event_get_user_data(e);

    // Now safe to free – screen is no longer visible
    if (!lottie_stop_task(ctx)) {
        return;
    }
    if (ctx->task_stack)    { heap_caps_free(ctx->task_stack);    ctx->task_stack = nullptr; }
    if (ctx->task_tcb)      { heap_caps_free(ctx->task_tcb);      ctx->task_tcb = nullptr; }
    if (ctx->pixel_buffer)  { heap_caps_free(ctx->pixel_buffer);  ctx->pixel_buffer = nullptr; }
    if (ctx->work_buffer)   { heap_caps_free(ctx->work_buffer);   ctx->work_buffer = nullptr; }
    if (ctx->display_back_buffer) {
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

    LV_LOG_INFO("Lottie FREED (%ux%u = %u KB %s buf + 64 KB stack) → free PSRAM: %u KB, free SRAM: %u KB",
             (unsigned)ctx->width, (unsigned)ctx->height,
             (unsigned)(ctx->width * ctx->height * 4 / 1024),
             ctx->pixel_buffer_internal ? "SRAM" : "PSRAM",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    ctx->pixel_buffer_internal = false;
    ctx->work_buffer_internal = false;
    ctx->display_back_buffer_internal = false;
    ctx->frame_cache_internal = false;
}

inline void lottie_screen_loaded_cb(lv_event_t *e) {
    LottieContext *ctx = (LottieContext *)lv_event_get_user_data(e);
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
  const bool was_hidden = ctx->runtime_hidden || lv_obj_has_flag(ctx->obj, LV_OBJ_FLAG_HIDDEN);
  ctx->runtime_hidden = false;
  ctx->auto_start = true;
  if (ctx->pixel_buffer == nullptr) {
    lottie_launch(ctx);
    return;
  }
  lv_obj_remove_flag(ctx->obj, LV_OBJ_FLAG_HIDDEN);
  if (restart && was_hidden) {
    lottie_restart(ctx);
  }
  if (ctx->task_handle != nullptr) {
    xTaskNotifyGive(ctx->task_handle);
  }
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

inline bool lottie_is_complete(const LottieContext *ctx) {
  return ctx == nullptr || ctx->playback_completed;
}

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
      const uint32_t background =
          0xFF000000U |
          (static_cast<uint32_t>(ctx->opaque_background.red) << 16) |
          (static_cast<uint32_t>(ctx->opaque_background.green) << 8) |
          static_cast<uint32_t>(ctx->opaque_background.blue);
      const size_t pixel_count = static_cast<size_t>(ctx->width) * ctx->height;
      // Completion artwork normally grows monotonically. Inspecting the last
      // eight immutable cache slots is enough to reject a blank/out-point
      // frame without scanning the complete 11 MB boot cache again.
      const uint32_t first_index = ctx->frame_cache_count > 8U ? ctx->frame_cache_count - 8U : 0U;
      for (uint32_t index = first_index; index < ctx->frame_cache_count; index++) {
        const auto *pixels = reinterpret_cast<const uint32_t *>(
            ctx->frame_cache + ctx->frame_cache_stride * static_cast<size_t>(index));
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
      ESP_LOGI(LOTTIE_PERF_TAG, "final frame retain: index=%u/%u coverage=%u final=%u",
               (unsigned) retained_index, (unsigned) (ctx->frame_cache_count - 1U),
               (unsigned) retained_coverage, (unsigned) final_coverage);
    }
    const uint8_t *final_frame =
        ctx->frame_cache + ctx->frame_cache_stride * static_cast<size_t>(retained_index);
    const size_t display_bytes = lottie_display_buffer_bytes(ctx);
    lv_display_t *display = nullptr;
    lv_lock();
    memcpy(ctx->pixel_buffer, final_frame, display_bytes);
    lottie_sync_buffer(ctx->pixel_buffer, display_bytes);
    if (ctx->obj != nullptr && lv_obj_is_valid(ctx->obj)) {
      if (ctx->flatten_to_opaque) {
        lottie_attach_opaque_buffer(ctx);
      } else {
        lv_canvas_set_buffer(ctx->obj, ctx->pixel_buffer, ctx->width, ctx->height,
                             LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED);
        lv_draw_buf_t *draw_buf = lv_canvas_get_draw_buf(ctx->obj);
        if (draw_buf != nullptr) {
          lv_draw_buf_set_flag(draw_buf, LV_IMAGE_FLAGS_PREMULTIPLIED);
        }
      }
      lv_obj_invalidate(ctx->obj);
      display = lv_obj_get_display(ctx->obj);
    }
    lv_unlock();

    // Cached frames are presented through the direct-region path. Commit the
    // canvas-owned final frame before releasing that cache so the wordmark
    // fade cannot redraw the stale pre-cache canvas over the completed trace.
    if (display != nullptr) {
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
  if (hide) {
    lv_obj_add_flag(ctx->obj, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_invalidate(ctx->obj);
}

inline void lottie_hide(LottieContext *ctx) {
  if (ctx == nullptr || ctx->obj == nullptr) {
    return;
  }
  ctx->runtime_hidden = true;
  lv_obj_add_flag(ctx->obj, LV_OBJ_FLAG_HIDDEN);
  lv_obj_invalidate(ctx->obj);
  lottie_free_resources(ctx);
}

// --------------------------------------------------------------------------
// Public API: initialise Lottie widget – allocate buffer, register screen
// events, and launch the load/render task.
// Call under lv_lock (from LVGL init code).
// --------------------------------------------------------------------------
inline bool lottie_init(lv_obj_t *obj, const void *data, size_t data_size,
                         const char *file_path, uint32_t width, uint32_t height,
                         bool loop, bool auto_start, bool user_wants_hidden,
                         bool flatten_to_opaque = false,
                         lv_color_t opaque_background = lv_color_hex(0x000000)) {
    LottieContext *ctx = (LottieContext *)heap_caps_malloc(
        sizeof(LottieContext), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!ctx) return false;
    memset(ctx, 0, sizeof(LottieContext));

    ctx->obj       = obj;
    ctx->data      = data;
    ctx->data_size = data_size;
    ctx->file_path = file_path;
    ctx->loop      = loop;
    ctx->auto_start = auto_start;
    ctx->width     = width;
    ctx->height    = height;
    ctx->flatten_to_opaque = flatten_to_opaque;
    ctx->opaque_background = opaque_background;
    ctx->user_wants_hidden = user_wants_hidden;  // Save user's 'hidden' config from YAML
    ctx->runtime_hidden = user_wants_hidden;    // Initially matches YAML config

    // Store context on the LVGL object so user scripts can retrieve it
    // via lv_obj_get_user_data() for lottie_restart() calls
    lv_obj_set_user_data(obj, ctx);

    // Register screen events for PSRAM lifecycle (two-phase unload).
    // IMPORTANT: We do NOT call lottie_launch() here.  Resources are only
    // allocated when the page actually becomes visible (SCREEN_LOADED event).
    // This prevents non-active pages from consuming PSRAM at startup.
    // With 19+ widgets across multiple pages, this saves megabytes of PSRAM.
    lv_obj_t *screen = lv_obj_get_screen(obj);
    lv_obj_add_event_cb(screen, lottie_screen_unload_start_cb,
                        LV_EVENT_SCREEN_UNLOAD_START, ctx);
    lv_obj_add_event_cb(screen, lottie_screen_unloaded_cb,
                        LV_EVENT_SCREEN_UNLOADED, ctx);
    lv_obj_add_event_cb(screen, lottie_screen_loaded_cb,
                        LV_EVENT_SCREEN_LOADED, ctx);

    LV_LOG_INFO("Lottie registered (%ux%u), waiting for page load to allocate",
             (unsigned)width, (unsigned)height);
    return true;
}

}  // namespace lvgl
}  // namespace esphome

#endif  // LV_USE_LOTTIE
#endif  // USE_ESP32
