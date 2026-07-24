#include "esphome/core/defines.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "dma2d_m2m_copy.h"
#include "lvgl_esphome.h"
#include "lvgl_navigation.h"

#include "core/lv_obj_class_private.h"
#include "core/lv_refr.h"
#include "display/lv_display_private.h"
#include "draw/lv_draw_buf_private.h"
#include "misc/lv_ll.h"

#include <atomic>
#include <array>
#include <cmath>
#include <cstring>

#ifdef USE_MIPI_DSI
#include "esphome/components/mipi_dsi/mipi_dsi.h"
#endif
#ifdef USE_ESP32_JPEG
#include "esphome/components/esp32_jpeg/esp32_jpeg.h"
#define USE_LVGL_SNAPSHOT_JPEG_CACHE 1
#endif
#ifdef USE_ESP32
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_timer.h"
#include "esp_private/esp_cache_private.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"
#endif

extern "C" bool esphome_mipi_dsi_wait_fifo_margin(uint32_t min_depth, uint32_t timeout_us) __attribute__((weak));
extern "C" void esphome_mipi_dsi_mark_stress(const char *label, uint32_t duration_ms) __attribute__((weak));

#ifdef USE_LVGL_PPA
#include "sdkconfig.h"
#include "driver/ppa.h"
#ifndef LV_PPA_BURST_LENGTH
#define LV_PPA_BURST_LENGTH (128)
#endif
#if LV_PPA_BURST_LENGTH == 128
#define LVGL_ESPHOME_PPA_DATA_BURST_LENGTH PPA_DATA_BURST_LENGTH_128
#elif LV_PPA_BURST_LENGTH == 64
#define LVGL_ESPHOME_PPA_DATA_BURST_LENGTH PPA_DATA_BURST_LENGTH_64
#elif LV_PPA_BURST_LENGTH == 32
#define LVGL_ESPHOME_PPA_DATA_BURST_LENGTH PPA_DATA_BURST_LENGTH_32
#elif LV_PPA_BURST_LENGTH == 16
#define LVGL_ESPHOME_PPA_DATA_BURST_LENGTH PPA_DATA_BURST_LENGTH_16
#elif LV_PPA_BURST_LENGTH == 8
#define LVGL_ESPHOME_PPA_DATA_BURST_LENGTH PPA_DATA_BURST_LENGTH_8
#else
#error "LV_PPA_BURST_LENGTH must be 8, 16, 32, 64 or 128"
#endif
#ifndef CONFIG_ESPHOME_LVGL_PPA_SRM_BURST_LENGTH
#define CONFIG_ESPHOME_LVGL_PPA_SRM_BURST_LENGTH LV_PPA_BURST_LENGTH
#endif
#if CONFIG_ESPHOME_LVGL_PPA_SRM_BURST_LENGTH == 128
#define LVGL_ESPHOME_PPA_SRM_DATA_BURST_LENGTH PPA_DATA_BURST_LENGTH_128
#elif CONFIG_ESPHOME_LVGL_PPA_SRM_BURST_LENGTH == 64
#define LVGL_ESPHOME_PPA_SRM_DATA_BURST_LENGTH PPA_DATA_BURST_LENGTH_64
#elif CONFIG_ESPHOME_LVGL_PPA_SRM_BURST_LENGTH == 32
#define LVGL_ESPHOME_PPA_SRM_DATA_BURST_LENGTH PPA_DATA_BURST_LENGTH_32
#elif CONFIG_ESPHOME_LVGL_PPA_SRM_BURST_LENGTH == 16
#define LVGL_ESPHOME_PPA_SRM_DATA_BURST_LENGTH PPA_DATA_BURST_LENGTH_16
#elif CONFIG_ESPHOME_LVGL_PPA_SRM_BURST_LENGTH == 8
#define LVGL_ESPHOME_PPA_SRM_DATA_BURST_LENGTH PPA_DATA_BURST_LENGTH_8
#else
#error "CONFIG_ESPHOME_LVGL_PPA_SRM_BURST_LENGTH must be 8, 16, 32, 64 or 128"
#endif
extern "C" {
void lv_draw_ppa_init(void);
uint32_t lv_draw_ppa_get_fill_task_count(void);
uint32_t lv_draw_ppa_get_img_task_count(void);
uint32_t lv_draw_ppa_get_img_eval_count(void);
uint32_t lv_draw_ppa_get_img_large_eval_count(void);
uint32_t lv_draw_ppa_get_img_accepted_eval_count(void);
uint32_t lv_draw_ppa_get_overlay_perf_count(void);
uint64_t lv_draw_ppa_get_overlay_perf_pre_us(void);
uint64_t lv_draw_ppa_get_overlay_perf_handler_us(void);
uint64_t lv_draw_ppa_get_overlay_perf_post_us(void);
uint32_t lv_draw_ppa_get_img_srm_task_count(void);
uint32_t lv_draw_ppa_get_img_srm_large_task_count(void);
uint32_t lv_draw_ppa_get_img_srm_unaligned_task_count(void);
uint64_t lv_draw_ppa_get_img_srm_unaligned_bytes(void);
uint64_t lv_draw_ppa_get_img_srm_copy_us(void);
uint32_t lv_draw_ppa_get_img_srm_copy_max_us(void);
uint64_t lv_draw_ppa_get_img_srm_sync_us(void);
uint32_t lv_draw_ppa_get_img_srm_sync_max_us(void);
uint64_t lv_draw_ppa_get_img_srm_sync_bytes(void);
uint64_t lv_draw_ppa_get_img_srm_ppa_us(void);
uint32_t lv_draw_ppa_get_img_srm_ppa_max_us(void);
uint64_t lv_draw_ppa_get_img_srm_wait_us(void);
uint32_t lv_draw_ppa_get_img_srm_wait_max_us(void);
uint32_t lv_draw_ppa_get_img_srm_band_max_us(void);
uint32_t lv_draw_ppa_get_img_overlay_count(void);
uint64_t lv_draw_ppa_get_img_overlay_src_sync_us(void);
uint64_t lv_draw_ppa_get_img_overlay_wait_us(void);
uint64_t lv_draw_ppa_get_img_overlay_dest_pre_us(void);
uint64_t lv_draw_ppa_get_img_overlay_ppa_us(void);
uint64_t lv_draw_ppa_get_img_overlay_dest_post_us(void);
void lv_draw_ppa_srm_qos_begin(uint32_t pixel_count);
void lv_draw_ppa_srm_qos_end(uint32_t pixel_count);
void lv_draw_ppa_direct_animation_qos_apply(void);
void lv_draw_ppa_direct_animation_qos_restore(void);
void lvgl_port_ppa_v9_init(lv_display_t *display);
}
#endif

#ifdef USE_LVGL_FPS_BENCHMARK
extern "C" {
void lvgl_fps_attach_v2(lv_display_t *display);
void lvgl_esphome_note_frame(void);
}
#endif

#if LV_USE_PROFILER && LV_USE_PROFILER_BUILTIN
#include "misc/lv_profiler_builtin.h"
#include "misc/lv_profiler_builtin_private.h"
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <utility>

namespace esphome::lvgl {
static const char *const TAG = "lvgl";

static uint32_t integer_sqrt_u32(uint32_t value) {
  uint32_t result = 0;
  uint32_t bit = 1U << 30;
  while (bit > value)
    bit >>= 2;
  while (bit != 0) {
    if (value >= result + bit) {
      value -= result + bit;
      result = (result >> 1) + bit;
    } else {
      result >>= 1;
    }
    bit >>= 2;
  }
  return result;
}

#ifndef CONFIG_ESPHOME_LVGL_SNAPSHOT_DSI_FIFO_MIN
#define CONFIG_ESPHOME_LVGL_SNAPSHOT_DSI_FIFO_MIN 1000
#endif
#ifndef CONFIG_ESPHOME_LVGL_SNAPSHOT_DSI_WAIT_US
#define CONFIG_ESPHOME_LVGL_SNAPSHOT_DSI_WAIT_US 6000
#endif
#ifndef CONFIG_ESPHOME_LVGL_SNAPSHOT_RAW_DSI_QUIET_MS
#define CONFIG_ESPHOME_LVGL_SNAPSHOT_RAW_DSI_QUIET_MS 35
#endif
#ifndef CONFIG_ESPHOME_LVGL_PPA_SRM_TRANSFORM_BAND_HEIGHT
#define CONFIG_ESPHOME_LVGL_PPA_SRM_TRANSFORM_BAND_HEIGHT 60
#endif
#ifndef CONFIG_ESPHOME_LVGL_PPA_SRM_TRANSFORM_BAND_GAP_US
#define CONFIG_ESPHOME_LVGL_PPA_SRM_TRANSFORM_BAND_GAP_US 0
#endif
#ifndef CONFIG_ESPHOME_LVGL_PPA_DIRECT_FULL_FRAME
#define CONFIG_ESPHOME_LVGL_PPA_DIRECT_FULL_FRAME 0
#endif
#ifndef CONFIG_ESPHOME_LVGL_PPA_SRM_ANIMATION_BURST_LENGTH
#define CONFIG_ESPHOME_LVGL_PPA_SRM_ANIMATION_BURST_LENGTH 64
#endif
#if CONFIG_ESPHOME_LVGL_PPA_SRM_ANIMATION_BURST_LENGTH == 128
#define LVGL_ESPHOME_PPA_SRM_ANIMATION_DATA_BURST_LENGTH PPA_DATA_BURST_LENGTH_128
#elif CONFIG_ESPHOME_LVGL_PPA_SRM_ANIMATION_BURST_LENGTH == 64
#define LVGL_ESPHOME_PPA_SRM_ANIMATION_DATA_BURST_LENGTH PPA_DATA_BURST_LENGTH_64
#elif CONFIG_ESPHOME_LVGL_PPA_SRM_ANIMATION_BURST_LENGTH == 32
#define LVGL_ESPHOME_PPA_SRM_ANIMATION_DATA_BURST_LENGTH PPA_DATA_BURST_LENGTH_32
#elif CONFIG_ESPHOME_LVGL_PPA_SRM_ANIMATION_BURST_LENGTH == 16
#define LVGL_ESPHOME_PPA_SRM_ANIMATION_DATA_BURST_LENGTH PPA_DATA_BURST_LENGTH_16
#elif CONFIG_ESPHOME_LVGL_PPA_SRM_ANIMATION_BURST_LENGTH == 8
#define LVGL_ESPHOME_PPA_SRM_ANIMATION_DATA_BURST_LENGTH PPA_DATA_BURST_LENGTH_8
#else
#error "CONFIG_ESPHOME_LVGL_PPA_SRM_ANIMATION_BURST_LENGTH must be 8, 16, 32, 64 or 128"
#endif

#ifndef CONFIG_ESPHOME_LVGL_PPA_DIRECT_ANIMATION_BURST_LENGTH
#define CONFIG_ESPHOME_LVGL_PPA_DIRECT_ANIMATION_BURST_LENGTH 128
#endif
#if CONFIG_ESPHOME_LVGL_PPA_DIRECT_ANIMATION_BURST_LENGTH == 128
#define LVGL_ESPHOME_PPA_DIRECT_ANIMATION_DATA_BURST_LENGTH PPA_DATA_BURST_LENGTH_128
#elif CONFIG_ESPHOME_LVGL_PPA_DIRECT_ANIMATION_BURST_LENGTH == 64
#define LVGL_ESPHOME_PPA_DIRECT_ANIMATION_DATA_BURST_LENGTH PPA_DATA_BURST_LENGTH_64
#elif CONFIG_ESPHOME_LVGL_PPA_DIRECT_ANIMATION_BURST_LENGTH == 32
#define LVGL_ESPHOME_PPA_DIRECT_ANIMATION_DATA_BURST_LENGTH PPA_DATA_BURST_LENGTH_32
#elif CONFIG_ESPHOME_LVGL_PPA_DIRECT_ANIMATION_BURST_LENGTH == 16
#define LVGL_ESPHOME_PPA_DIRECT_ANIMATION_DATA_BURST_LENGTH PPA_DATA_BURST_LENGTH_16
#elif CONFIG_ESPHOME_LVGL_PPA_DIRECT_ANIMATION_BURST_LENGTH == 8
#define LVGL_ESPHOME_PPA_DIRECT_ANIMATION_DATA_BURST_LENGTH PPA_DATA_BURST_LENGTH_8
#else
#error "CONFIG_ESPHOME_LVGL_PPA_DIRECT_ANIMATION_BURST_LENGTH must be 8, 16, 32, 64 or 128"
#endif

// Small persistent overlays (media wave, marquee, volume UI) share PSRAM with
// the continuously scanned DSI framebuffer. Give this path its own arbitration
// profile so full-screen snapshot and image transforms can remain at maximum
// throughput without letting a burst of regional updates drain the DSI FIFO.
#ifndef CONFIG_ESPHOME_LVGL_PPA_DIRECT_REGION_BURST_LENGTH
#define CONFIG_ESPHOME_LVGL_PPA_DIRECT_REGION_BURST_LENGTH CONFIG_ESPHOME_LVGL_PPA_DIRECT_ANIMATION_BURST_LENGTH
#endif
#if CONFIG_ESPHOME_LVGL_PPA_DIRECT_REGION_BURST_LENGTH == 128
#define LVGL_ESPHOME_PPA_DIRECT_REGION_DATA_BURST_LENGTH PPA_DATA_BURST_LENGTH_128
#elif CONFIG_ESPHOME_LVGL_PPA_DIRECT_REGION_BURST_LENGTH == 64
#define LVGL_ESPHOME_PPA_DIRECT_REGION_DATA_BURST_LENGTH PPA_DATA_BURST_LENGTH_64
#elif CONFIG_ESPHOME_LVGL_PPA_DIRECT_REGION_BURST_LENGTH == 32
#define LVGL_ESPHOME_PPA_DIRECT_REGION_DATA_BURST_LENGTH PPA_DATA_BURST_LENGTH_32
#elif CONFIG_ESPHOME_LVGL_PPA_DIRECT_REGION_BURST_LENGTH == 16
#define LVGL_ESPHOME_PPA_DIRECT_REGION_DATA_BURST_LENGTH PPA_DATA_BURST_LENGTH_16
#elif CONFIG_ESPHOME_LVGL_PPA_DIRECT_REGION_BURST_LENGTH == 8
#define LVGL_ESPHOME_PPA_DIRECT_REGION_DATA_BURST_LENGTH PPA_DATA_BURST_LENGTH_8
#else
#error "CONFIG_ESPHOME_LVGL_PPA_DIRECT_REGION_BURST_LENGTH must be 8, 16, 32, 64 or 128"
#endif

#ifndef CONFIG_ESPHOME_LVGL_PPA_DIRECT_REGION_DSI_FIFO_MIN
#define CONFIG_ESPHOME_LVGL_PPA_DIRECT_REGION_DSI_FIFO_MIN 896
#endif
#ifndef CONFIG_ESPHOME_LVGL_PPA_DIRECT_REGION_DSI_WAIT_US
#define CONFIG_ESPHOME_LVGL_PPA_DIRECT_REGION_DSI_WAIT_US 3000
#endif

static inline bool lvgl_esphome_wait_direct_region_dsi_fifo() {
#if defined(USE_MIPI_DSI)
  if (esphome_mipi_dsi_wait_fifo_margin != nullptr) {
    return esphome_mipi_dsi_wait_fifo_margin(CONFIG_ESPHOME_LVGL_PPA_DIRECT_REGION_DSI_FIFO_MIN,
                                             CONFIG_ESPHOME_LVGL_PPA_DIRECT_REGION_DSI_WAIT_US);
  }
#endif
  return true;
}

static inline void lvgl_esphome_wait_snapshot_dsi_fifo() {
#if defined(USE_MIPI_DSI)
  if (esphome_mipi_dsi_wait_fifo_margin != nullptr) {
    esphome_mipi_dsi_wait_fifo_margin(CONFIG_ESPHOME_LVGL_SNAPSHOT_DSI_FIFO_MIN,
                                      CONFIG_ESPHOME_LVGL_SNAPSHOT_DSI_WAIT_US);
  }
#endif
}

static inline void lvgl_esphome_snapshot_dsi_quiet(uint32_t quiet_ms) {
#if defined(USE_MIPI_DSI) && defined(USE_ESP32)
  if (quiet_ms == 0 || esphome_mipi_dsi_wait_fifo_margin == nullptr)
    return;
  const int64_t deadline = esp_timer_get_time() + static_cast<int64_t>(quiet_ms) * 1000;
  do {
    lvgl_esphome_wait_snapshot_dsi_fifo();
    vTaskDelay(1);
  } while (esp_timer_get_time() < deadline);
#else
  (void) quiet_ms;
#endif
}

extern "C" void lvgl_esphome_snapshot_dsi_quiet_ms(uint32_t quiet_ms) { lvgl_esphome_snapshot_dsi_quiet(quiet_ms); }

#ifdef USE_ESP32
static esp_err_t lvgl_cache_msync_external_result(const void *ptr, size_t len, int flags) {
  if (ptr == nullptr || len == 0 || !esp_ptr_external_ram(ptr))
    return ESP_OK;

  size_t cache_alignment = 0;
  if (esp_cache_get_alignment(MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA, &cache_alignment) != ESP_OK || cache_alignment == 0 ||
      (cache_alignment & (cache_alignment - 1U)) != 0) {
    cache_alignment = 64;
  }
  const uintptr_t cache_align = static_cast<uintptr_t>(cache_alignment);
  const uintptr_t ptr_addr = reinterpret_cast<uintptr_t>(ptr);
  const uintptr_t start = ptr_addr & ~(cache_align - 1U);
  const uintptr_t end = (ptr_addr + len + cache_align - 1U) & ~(cache_align - 1U);
  if (end <= start)
    return ESP_OK;
  if (!esp_ptr_external_ram(reinterpret_cast<const void *>(start)) ||
      !esp_ptr_external_ram(reinterpret_cast<const void *>(end - 1)))
    return ESP_OK;

  return esp_cache_msync(reinterpret_cast<void *>(start), end - start, flags);
}

static void lvgl_cache_msync_external(const void *ptr, size_t len, int flags) {
  lvgl_cache_msync_external_result(ptr, len, flags);
}

#endif

#ifdef USE_MIPI_DSI
static void lvgl_mipi_async_flush_ready(void *arg) { lv_display_flush_ready(static_cast<lv_display_t *>(arg)); }
#endif

// Published CPU% (real work, flush wait excluded) for the LVGL sysmon
// overlay. Updated each second by loop(). Read by __wrap_lv_timer_get_idle
// below to override lv_sysmon's broken FreeRTOS-mode CPU calculation.
static volatile uint32_t s_cpu_pct = 0;
static volatile uint32_t s_flush_ms = 0;
static volatile uint32_t s_direct_mode_active = 0;
static volatile uint32_t s_loop_max_ms = 0;
static volatile uint32_t s_flush_max_ms = 0;
static volatile uint32_t s_invalidated_kpx = 0;
static volatile uint32_t s_perf_logging_enabled = 0;
static volatile uint32_t s_swipe_logging_enabled = 0;
static volatile bool s_snapshot_swipe_active = false;
static volatile bool s_snapshot_direct_active = false;
static bool s_snapshot_app_open_frame_held = false;
static volatile int s_snapshot_page_indicator_page = 0;
static volatile int s_snapshot_page_indicator_count = 0;
static char s_snapshot_clock_text[8] = "9:30";
static const lv_font_t *s_snapshot_clock_font = nullptr;
static lv_draw_buf_t *s_snapshot_clock_glyph_buffer = nullptr;

static constexpr int SNAPSHOT_PAGE_INDICATOR_Y = 716;
static constexpr int SNAPSHOT_PAGE_INDICATOR_H = 10;
static constexpr int SNAPSHOT_CLOCK_Y = 10;
static constexpr int SNAPSHOT_CLOCK_H = 52;

static bool snapshot_draw_clock_rgb888(uint8_t *buffer, int width, int height) {
  static_assert(sizeof(lv_color_t) == 3, "Snapshot clock compositor requires RGB888");
  if (buffer == nullptr || width <= 0 || height <= 0 || s_snapshot_clock_font == nullptr)
    return false;
  if (SNAPSHOT_CLOCK_Y < 0 || SNAPSHOT_CLOCK_Y + SNAPSHOT_CLOCK_H > height)
    return false;

  char text[sizeof(s_snapshot_clock_text)];
  std::memcpy(text, s_snapshot_clock_text, sizeof(text));
  text[sizeof(text) - 1] = '\0';
  size_t len = 0;
  while (len < sizeof(text) && text[len] != '\0')
    len++;
  if (len == 0)
    return false;

  if (s_snapshot_clock_glyph_buffer == nullptr) {
    s_snapshot_clock_glyph_buffer = lv_draw_buf_create(64, 64, LV_COLOR_FORMAT_A8, LV_STRIDE_AUTO);
    if (s_snapshot_clock_glyph_buffer == nullptr)
      return false;
  }

  lv_font_glyph_dsc_t glyphs[8]{};
  int glyph_count = 0;
  int text_width = 0;
  for (size_t i = 0; i < len && glyph_count < 8; i++) {
    const uint32_t next = i + 1 < len ? static_cast<uint8_t>(text[i + 1]) : 0;
    if (!lv_font_get_glyph_dsc(s_snapshot_clock_font, &glyphs[glyph_count], static_cast<uint8_t>(text[i]), next))
      continue;
    text_width += glyphs[glyph_count].adv_w;
    glyph_count++;
  }
  if (glyph_count == 0 || text_width <= 0)
    return false;

  lv_color_t *pixels = reinterpret_cast<lv_color_t *>(buffer);
  int pen_x = (width - text_width) / 2;
  const int line_y = SNAPSHOT_CLOCK_Y + (SNAPSHOT_CLOCK_H - s_snapshot_clock_font->line_height) / 2;
  for (int i = 0; i < glyph_count; i++) {
    auto &glyph = glyphs[i];
    const auto *draw_buf =
        static_cast<const lv_draw_buf_t *>(lv_font_get_glyph_bitmap(&glyph, s_snapshot_clock_glyph_buffer));
    if (draw_buf != nullptr && glyph.box_w > 0 && glyph.box_h > 0) {
      const int glyph_x = pen_x + glyph.ofs_x;
      const int glyph_y =
          line_y + (s_snapshot_clock_font->line_height - s_snapshot_clock_font->base_line) - glyph.box_h - glyph.ofs_y;
      const int glyph_stride = lv_draw_buf_width_to_stride(glyph.box_w, LV_COLOR_FORMAT_A8);
      for (int gy = 0; gy < glyph.box_h; gy++) {
        const int y = glyph_y + gy;
        if (y < 0 || y >= height)
          continue;
        const uint8_t *alpha_row = draw_buf->data + static_cast<size_t>(gy) * glyph_stride;
        lv_color_t *dst_row = pixels + static_cast<size_t>(y) * width;
        for (int gx = 0; gx < glyph.box_w; gx++) {
          const int x = glyph_x + gx;
          if (x < 0 || x >= width)
            continue;
          const uint16_t alpha = alpha_row[gx];
          if (alpha == 0)
            continue;
          lv_color_t &dst = dst_row[x];
          const uint16_t inverse = 255U - alpha;
          dst.red = static_cast<uint8_t>((0xF5U * alpha + dst.red * inverse + 127U) / 255U);
          dst.green = static_cast<uint8_t>((0xEEU * alpha + dst.green * inverse + 127U) / 255U);
          dst.blue = static_cast<uint8_t>((0xFBU * alpha + dst.blue * inverse + 127U) / 255U);
        }
      }
    }
    pen_x += glyph.adv_w;
    lv_font_glyph_release_draw_data(&glyph);
  }
  return true;
}

static void snapshot_sync_clock_rgb888(uint8_t *buffer, int width, int height) {
#if defined(USE_ESP32)
  if (buffer == nullptr || width <= 0 || height <= 0)
    return;
  if (SNAPSHOT_CLOCK_Y < 0 || SNAPSHOT_CLOCK_Y + SNAPSHOT_CLOCK_H > height)
    return;
  const size_t row_bytes = (size_t) width * 3u;
  lvgl_cache_msync_external(buffer + (size_t) SNAPSHOT_CLOCK_Y * row_bytes, row_bytes * SNAPSHOT_CLOCK_H,
                            ESP_CACHE_MSYNC_FLAG_DIR_C2M);
#endif
}

static bool snapshot_draw_page_indicator_rgb888(uint8_t *buffer, int width, int height, int page, int count) {
  if (buffer == nullptr || width <= 0 || height <= 0 || count <= 0 || page <= 0 || page > count)
    return false;
  constexpr int dot_h = SNAPSHOT_PAGE_INDICATOR_H;
  constexpr int inactive_w = 10;
  constexpr int active_w = 30;
  constexpr int gap = 12;
  constexpr int y = SNAPSHOT_PAGE_INDICATOR_Y;
  if (y < 0 || y + dot_h > height)
    return false;

  const int total_w = active_w + (count - 1) * inactive_w + (count - 1) * gap;
  int x = (width - total_w) / 2;
  const size_t row_bytes = (size_t) width * 3u;

  auto draw_rounded_rect = [&](int x0, int w, uint8_t r, uint8_t g, uint8_t b) {
    constexpr int sample_grid = 4;
    constexpr int fixed_scale = sample_grid * 2;
    const int radius_fp = (dot_h * fixed_scale) / 2;
    const int center_y_fp = radius_fp;
    const int left_center_fp = radius_fp;
    const int right_center_fp = w * fixed_scale - radius_fp;
    const int radius_sq = radius_fp * radius_fp;
    for (int py = 0; py < dot_h; py++) {
      for (int px = 0; px < w; px++) {
        int covered = 0;
        for (int sy = 0; sy < sample_grid; sy++) {
          const int sample_y = py * fixed_scale + sy * 2 + 1;
          const int dy = sample_y - center_y_fp;
          for (int sx = 0; sx < sample_grid; sx++) {
            const int sample_x = px * fixed_scale + sx * 2 + 1;
            int dx = 0;
            if (sample_x < left_center_fp)
              dx = sample_x - left_center_fp;
            else if (sample_x > right_center_fp)
              dx = sample_x - right_center_fp;
            if (dx * dx + dy * dy <= radius_sq)
              covered++;
          }
        }
        if (covered == 0)
          continue;
        const int tx = x0 + px;
        if (tx < 0 || tx >= width)
          continue;
        uint8_t *dst = buffer + (size_t) (y + py) * row_bytes + (size_t) tx * 3u;
        const uint16_t alpha = static_cast<uint16_t>((covered * 255 + 8) / 16);
        const uint16_t inverse = 255U - alpha;
        dst[0] = static_cast<uint8_t>((r * alpha + dst[0] * inverse + 127U) / 255U);
        dst[1] = static_cast<uint8_t>((g * alpha + dst[1] * inverse + 127U) / 255U);
        dst[2] = static_cast<uint8_t>((b * alpha + dst[2] * inverse + 127U) / 255U);
      }
    }
  };

  for (int i = 1; i <= count; i++) {
    const bool active = i == page;
    const int w = active ? active_w : inactive_w;
    if (active) {
      draw_rounded_rect(x, w, 0xF2, 0xF2, 0xF2);
    } else {
      draw_rounded_rect(x, w, 0x5C, 0x5F, 0x5E);
    }
    x += w + gap;
  }
  return true;
}

static void snapshot_sync_page_indicator_rgb888(uint8_t *buffer, int width, int height) {
#if defined(USE_ESP32)
  if (buffer == nullptr || width <= 0 || height <= 0)
    return;
  if (SNAPSHOT_PAGE_INDICATOR_Y < 0 || SNAPSHOT_PAGE_INDICATOR_Y + SNAPSHOT_PAGE_INDICATOR_H > height)
    return;
  const size_t row_bytes = (size_t) width * 3u;
  lvgl_cache_msync_external(buffer + (size_t) SNAPSHOT_PAGE_INDICATOR_Y * row_bytes,
                            row_bytes * SNAPSHOT_PAGE_INDICATOR_H, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
#endif
}

struct SnapshotAppRenderBufferState {
  uint8_t *buffer{nullptr};
  const void *app_source{nullptr};
  const void *background_source{nullptr};
  int radius{0};
  int center_x{0};
  int center_y{0};
  bool initialized{false};
  bool opening{true};
};

static SnapshotAppRenderBufferState s_snapshot_app_render_buffers[3];
static bool s_snapshot_app_render_opening = true;
static const void *s_snapshot_app_dma_synced_app = nullptr;
static const void *s_snapshot_app_dma_synced_background = nullptr;

struct SnapshotAppFrameMetrics {
  uint32_t total_us{0};
  uint32_t prepare_us{0};
  uint32_t circle_us{0};
  uint32_t cache_sync_us{0};
  uint32_t ppa_us{0};
  uint32_t present_us{0};
};

static SnapshotAppFrameMetrics s_snapshot_app_last_frame_metrics;

#ifdef USE_LVGL_PPA
struct SnapshotAppLowResolutionState {
  struct TargetState {
    uint8_t *buffer{nullptr};
    int radius{0};
    int center_x{0};
    int center_y{0};
    bool initialized{false};
  };

  uint8_t *allocation{nullptr};
  uint8_t *app{nullptr};
  uint8_t *background{nullptr};
  uint8_t *composite{nullptr};
  size_t frame_bytes{0};
  size_t allocation_bytes{0};
  int width{0};
  int height{0};
  int radius{0};
  int center_x{0};
  int center_y{0};
  const void *app_source{nullptr};
  const void *background_source{nullptr};
  std::array<TargetState, 3> targets{};
  bool initialized{false};
};

static SnapshotAppLowResolutionState s_snapshot_app_low_res;

static void snapshot_app_low_res_reset() {
  s_snapshot_app_low_res.radius = 0;
  s_snapshot_app_low_res.center_x = 0;
  s_snapshot_app_low_res.center_y = 0;
  s_snapshot_app_low_res.app_source = nullptr;
  s_snapshot_app_low_res.background_source = nullptr;
  s_snapshot_app_low_res.targets = {};
  s_snapshot_app_low_res.initialized = false;
}
#endif

static void snapshot_app_render_buffers_reset(bool opening) {
  s_snapshot_app_render_opening = opening;
  s_snapshot_app_dma_synced_app = nullptr;
  s_snapshot_app_dma_synced_background = nullptr;
#ifdef USE_LVGL_PPA
  snapshot_app_low_res_reset();
#endif
  for (auto &state : s_snapshot_app_render_buffers) {
    state.buffer = nullptr;
    state.app_source = nullptr;
    state.background_source = nullptr;
    state.radius = opening ? 0 : 32767;
    state.center_x = 0;
    state.center_y = 0;
    state.initialized = false;
    state.opening = opening;
  }
}

bool snapshot_swipe_process_pending();
bool snapshot_scroll_process_pending();

namespace {
bool snapshot_swipe_direct_anim_tick();
bool snapshot_app_direct_anim_tick();
void snapshot_cache_process_pending_compression();
void snapshot_swipe_finish_now();
}  // namespace

#if LV_USE_PROFILER && LV_USE_PROFILER_BUILTIN
static volatile uint32_t s_profiler_enabled = 0;
static bool s_profiler_initialized = false;
static constexpr size_t PROFILER_MAX_STACK = 96;
static constexpr size_t PROFILER_MAX_AGG = 96;
static constexpr size_t PROFILER_NAME_LEN = 56;

struct ProfilerStackEntry {
  char name[PROFILER_NAME_LEN];
  uint64_t start_us;
};

struct ProfilerAggEntry {
  char name[PROFILER_NAME_LEN];
  uint32_t count;
  uint64_t total_us;
  uint32_t max_us;
};

static ProfilerStackEntry *s_profiler_stack = nullptr;
static size_t s_profiler_stack_depth = 0;
static ProfilerAggEntry *s_profiler_aggs = nullptr;
static size_t s_profiler_agg_count = 0;
static uint32_t s_profiler_stack_overflow = 0;
static uint32_t s_profiler_parse_drops = 0;
static uint64_t s_profiler_first_us = 0;
static uint64_t s_profiler_last_us = 0;

struct ProfilerManualStats {
  uint64_t started_us;
  uint64_t ended_us;
  uint32_t loop_calls;
  uint64_t loop_total_us;
  uint32_t loop_max_us;
  uint32_t loop_over_30ms;
  uint32_t flush_calls;
  uint64_t flush_total_us;
  uint32_t flush_max_us;
  uint64_t flush_px;
  uint32_t invalidated_areas;
  uint64_t invalidated_px;
  uint32_t ppa_fill_start;
  uint32_t ppa_img_start;
};

static ProfilerManualStats s_profiler_manual = {};

static uint64_t profiler_tick_us() {
#ifdef USE_ESP32
  return (uint64_t) esp_timer_get_time();
#else
  return (uint64_t) micros();
#endif
}

static int profiler_tid_get() { return 1; }

static int profiler_cpu_get() {
#ifdef USE_ESP32
  return xPortGetCoreID();
#else
  return 0;
#endif
}

static void profiler_copy_name(char *dst, const char *src) {
  size_t len = 0;
  while (src[len] != '\0' && src[len] != '\n' && src[len] != '\r' && src[len] != '\x1b' && len < PROFILER_NAME_LEN - 1)
    len++;
  memcpy(dst, src, len);
  dst[len] = '\0';
}

static void profiler_reset_summary() {
  memset(&s_profiler_manual, 0, sizeof(s_profiler_manual));
  s_profiler_manual.started_us = profiler_tick_us();
#ifdef USE_LVGL_PPA
  s_profiler_manual.ppa_fill_start = lv_draw_ppa_get_fill_task_count();
  s_profiler_manual.ppa_img_start = lv_draw_ppa_get_img_task_count();
#endif
  if (s_profiler_stack == nullptr || s_profiler_aggs == nullptr)
    return;
  s_profiler_stack_depth = 0;
  s_profiler_agg_count = 0;
  s_profiler_stack_overflow = 0;
  s_profiler_parse_drops = 0;
  s_profiler_first_us = 0;
  s_profiler_last_us = 0;
  memset(s_profiler_stack, 0, sizeof(ProfilerStackEntry) * PROFILER_MAX_STACK);
  memset(s_profiler_aggs, 0, sizeof(ProfilerAggEntry) * PROFILER_MAX_AGG);
}

static void profiler_note_loop(uint32_t duration_us) {
  if (!s_profiler_enabled)
    return;
  s_profiler_manual.loop_calls++;
  s_profiler_manual.loop_total_us += duration_us;
  if (duration_us > s_profiler_manual.loop_max_us)
    s_profiler_manual.loop_max_us = duration_us;
  if (duration_us > 30000)
    s_profiler_manual.loop_over_30ms++;
}

static void profiler_note_flush(uint32_t duration_us, uint32_t px) {
  if (!s_profiler_enabled)
    return;
  s_profiler_manual.flush_calls++;
  s_profiler_manual.flush_total_us += duration_us;
  if (duration_us > s_profiler_manual.flush_max_us)
    s_profiler_manual.flush_max_us = duration_us;
  s_profiler_manual.flush_px += px;
}

static void profiler_note_invalidated(uint32_t px) {
  if (!s_profiler_enabled)
    return;
  s_profiler_manual.invalidated_areas++;
  s_profiler_manual.invalidated_px += px;
}

static bool profiler_parse_line(const char *buf, char *phase, uint64_t *time_us, const char **name) {
  const char *stamp = strstr(buf, "] ");
  const char *mark = strstr(buf, "tracing_mark_write: ");
  if (stamp == nullptr || mark == nullptr)
    return false;
  stamp += 2;
  char *end = nullptr;
  uint64_t sec = strtoull(stamp, &end, 10);
  if (end == nullptr || *end != '.')
    return false;
  uint64_t nsec = strtoull(end + 1, &end, 10);
  *time_us = sec * 1000000ULL + nsec / 1000ULL;

  const char *payload = mark + strlen("tracing_mark_write: ");
  if ((payload[0] != 'B' && payload[0] != 'E') || payload[1] != '|' || payload[2] != '1' || payload[3] != '|')
    return false;
  *phase = payload[0];
  *name = payload + 4;
  return true;
}

static ProfilerAggEntry *profiler_get_agg(const char *name) {
  char copied[PROFILER_NAME_LEN];
  profiler_copy_name(copied, name);
  for (size_t i = 0; i < s_profiler_agg_count; i++) {
    if (strncmp(s_profiler_aggs[i].name, copied, PROFILER_NAME_LEN) == 0)
      return &s_profiler_aggs[i];
  }
  if (s_profiler_agg_count >= PROFILER_MAX_AGG)
    return nullptr;
  ProfilerAggEntry *entry = &s_profiler_aggs[s_profiler_agg_count++];
  strncpy(entry->name, copied, PROFILER_NAME_LEN - 1);
  entry->name[PROFILER_NAME_LEN - 1] = '\0';
  entry->count = 0;
  entry->total_us = 0;
  entry->max_us = 0;
  return entry;
}

static void profiler_add_duration(const char *name, uint32_t duration_us) {
  ProfilerAggEntry *entry = profiler_get_agg(name);
  if (entry == nullptr) {
    s_profiler_parse_drops++;
    return;
  }
  entry->count++;
  entry->total_us += duration_us;
  if (duration_us > entry->max_us)
    entry->max_us = duration_us;
}

static void profiler_process_line(const char *buf) {
  char phase = '\0';
  uint64_t time_us = 0;
  const char *name = nullptr;
  if (!profiler_parse_line(buf, &phase, &time_us, &name)) {
    if (buf[0] != '#')
      s_profiler_parse_drops++;
    return;
  }
  if (s_profiler_first_us == 0)
    s_profiler_first_us = time_us;
  s_profiler_last_us = time_us;

  if (phase == 'B') {
    if (s_profiler_stack_depth >= PROFILER_MAX_STACK) {
      s_profiler_stack_overflow++;
      return;
    }
    ProfilerStackEntry *entry = &s_profiler_stack[s_profiler_stack_depth++];
    profiler_copy_name(entry->name, name);
    entry->start_us = time_us;
    return;
  }

  for (size_t i = s_profiler_stack_depth; i > 0; i--) {
    ProfilerStackEntry *entry = &s_profiler_stack[i - 1];
    if (strncmp(entry->name, name, PROFILER_NAME_LEN) == 0) {
      uint64_t duration = time_us >= entry->start_us ? time_us - entry->start_us : 0;
      profiler_add_duration(name, duration > UINT32_MAX ? UINT32_MAX : (uint32_t) duration);
      s_profiler_stack_depth = i - 1;
      return;
    }
  }
  s_profiler_parse_drops++;
}

static void profiler_flush_cb(const char *buf) {
  if (buf == nullptr || s_profiler_stack == nullptr || s_profiler_aggs == nullptr)
    return;

  // LVGL flushes a complete systrace block, not one event per callback.
  // Parse every line so nested begin/end pairs can be aggregated reliably.
  constexpr size_t LINE_CAPACITY = 256;
  char line[LINE_CAPACITY];
  const char *cursor = buf;
  while (*cursor != '\0') {
    const char *line_end = cursor;
    while (*line_end != '\0' && *line_end != '\n' && *line_end != '\r')
      line_end++;
    const size_t line_len = static_cast<size_t>(line_end - cursor);
    if (line_len > 0) {
      if (line_len < LINE_CAPACITY) {
        memcpy(line, cursor, line_len);
        line[line_len] = '\0';
        profiler_process_line(line);
      } else {
        s_profiler_parse_drops++;
      }
    }
    cursor = line_end;
    while (*cursor == '\n' || *cursor == '\r')
      cursor++;
  }
}

static void profiler_print_summary() {
  if (s_profiler_aggs == nullptr)
    return;
  uint64_t window_us = s_profiler_last_us > s_profiler_first_us ? s_profiler_last_us - s_profiler_first_us : 0;
  ESP_LOGI("lvgl.prof", "SUMMARY window=%lluus unique=%u drops=%u stack_overflow=%u open=%u",
           (unsigned long long) window_us, (unsigned) s_profiler_agg_count, (unsigned) s_profiler_parse_drops,
           (unsigned) s_profiler_stack_overflow, (unsigned) s_profiler_stack_depth);

  bool selected[PROFILER_MAX_AGG] = {};
  const size_t limit = std::min<size_t>(12, s_profiler_agg_count);
  for (size_t rank = 0; rank < limit; rank++) {
    size_t best = PROFILER_MAX_AGG;
    for (size_t i = 0; i < s_profiler_agg_count; i++) {
      if (selected[i])
        continue;
      if (best == PROFILER_MAX_AGG || s_profiler_aggs[i].total_us > s_profiler_aggs[best].total_us)
        best = i;
    }
    if (best == PROFILER_MAX_AGG)
      break;
    selected[best] = true;
    const ProfilerAggEntry *entry = &s_profiler_aggs[best];
    uint32_t avg_us = entry->count == 0 ? 0 : (uint32_t) (entry->total_us / entry->count);
    ESP_LOGI("lvgl.prof", "COST rank=%u total=%lluus avg=%uus max=%uus count=%u name=%s", (unsigned) (rank + 1),
             (unsigned long long) entry->total_us, (unsigned) avg_us, (unsigned) entry->max_us, (unsigned) entry->count,
             entry->name);
  }
}

static void profiler_print_manual_summary() {
  s_profiler_manual.ended_us = profiler_tick_us();
  uint64_t window_us = s_profiler_manual.ended_us > s_profiler_manual.started_us
                           ? s_profiler_manual.ended_us - s_profiler_manual.started_us
                           : 0;
  uint32_t loop_avg_us = s_profiler_manual.loop_calls == 0
                             ? 0
                             : (uint32_t) (s_profiler_manual.loop_total_us / s_profiler_manual.loop_calls);
  uint32_t flush_avg_us = s_profiler_manual.flush_calls == 0
                              ? 0
                              : (uint32_t) (s_profiler_manual.flush_total_us / s_profiler_manual.flush_calls);
  uint32_t ppa_fill_delta = 0;
  uint32_t ppa_img_delta = 0;
#ifdef USE_LVGL_PPA
  uint32_t ppa_fill_now = lv_draw_ppa_get_fill_task_count();
  uint32_t ppa_img_now = lv_draw_ppa_get_img_task_count();
  ppa_fill_delta = ppa_fill_now - s_profiler_manual.ppa_fill_start;
  ppa_img_delta = ppa_img_now - s_profiler_manual.ppa_img_start;
#endif
  ESP_LOGI("lvgl.prof",
           "PROFILE_COST window=%lluus loop_calls=%u loop_total=%lluus loop_avg=%uus loop_max=%uus loop_over_30ms=%u",
           (unsigned long long) window_us, (unsigned) s_profiler_manual.loop_calls,
           (unsigned long long) s_profiler_manual.loop_total_us, (unsigned) loop_avg_us,
           (unsigned) s_profiler_manual.loop_max_us, (unsigned) s_profiler_manual.loop_over_30ms);
  ESP_LOGI("lvgl.prof", "PROFILE_COST flush_calls=%u flush_total=%lluus flush_avg=%uus flush_max=%uus flush_px=%llukpx",
           (unsigned) s_profiler_manual.flush_calls, (unsigned long long) s_profiler_manual.flush_total_us,
           (unsigned) flush_avg_us, (unsigned) s_profiler_manual.flush_max_us,
           (unsigned long long) (s_profiler_manual.flush_px / 1000ULL));
  ESP_LOGI("lvgl.prof", "PROFILE_COST invalidated=%u areas/%llukpx ppa_delta=%u/%u",
           (unsigned) s_profiler_manual.invalidated_areas,
           (unsigned long long) (s_profiler_manual.invalidated_px / 1000ULL), (unsigned) ppa_fill_delta,
           (unsigned) ppa_img_delta);
}

static void profiler_init_custom() {
  if (s_profiler_stack == nullptr) {
#ifdef USE_ESP32
    s_profiler_stack = static_cast<ProfilerStackEntry *>(
        heap_caps_calloc(PROFILER_MAX_STACK, sizeof(ProfilerStackEntry), MALLOC_CAP_SPIRAM));
#endif
    if (s_profiler_stack == nullptr)
      s_profiler_stack = static_cast<ProfilerStackEntry *>(calloc(PROFILER_MAX_STACK, sizeof(ProfilerStackEntry)));
  }
  if (s_profiler_aggs == nullptr) {
#ifdef USE_ESP32
    s_profiler_aggs = static_cast<ProfilerAggEntry *>(
        heap_caps_calloc(PROFILER_MAX_AGG, sizeof(ProfilerAggEntry), MALLOC_CAP_SPIRAM));
#endif
    if (s_profiler_aggs == nullptr)
      s_profiler_aggs = static_cast<ProfilerAggEntry *>(calloc(PROFILER_MAX_AGG, sizeof(ProfilerAggEntry)));
  }
  if (s_profiler_stack == nullptr || s_profiler_aggs == nullptr) {
    ESP_LOGW("lvgl.prof", "profiler summary buffers allocation failed");
    return;
  }
  lv_profiler_builtin_config_t config;
  lv_profiler_builtin_config_init(&config);
  config.buf_size = LV_PROFILER_BUILTIN_BUF_SIZE;
  config.tick_per_sec = 1000000;
  config.tick_get_cb = profiler_tick_us;
  config.flush_cb = profiler_flush_cb;
  config.tid_get_cb = profiler_tid_get;
  config.cpu_get_cb = profiler_cpu_get;
  lv_profiler_builtin_init(&config);
  lv_profiler_builtin_set_enable(false);
  s_profiler_enabled = 0;
  s_profiler_initialized = true;
}
#else
static volatile uint32_t s_profiler_enabled = 0;
static bool s_profiler_initialized = false;

struct ProfilerManualStats {
  uint64_t started_us;
  uint64_t ended_us;
  uint32_t loop_calls;
  uint64_t loop_total_us;
  uint32_t loop_max_us;
  uint32_t loop_over_30ms;
  uint32_t flush_calls;
  uint64_t flush_total_us;
  uint32_t flush_max_us;
  uint64_t flush_px;
  uint32_t invalidated_areas;
  uint64_t invalidated_px;
  uint32_t ppa_fill_start;
  uint32_t ppa_img_start;
};

static ProfilerManualStats s_profiler_manual = {};

static uint64_t profiler_tick_us() {
#ifdef USE_ESP32
  return (uint64_t) esp_timer_get_time();
#else
  return (uint64_t) micros();
#endif
}

static void profiler_reset_summary() {
  memset(&s_profiler_manual, 0, sizeof(s_profiler_manual));
  s_profiler_manual.started_us = profiler_tick_us();
#ifdef USE_LVGL_PPA
  s_profiler_manual.ppa_fill_start = lv_draw_ppa_get_fill_task_count();
  s_profiler_manual.ppa_img_start = lv_draw_ppa_get_img_task_count();
#endif
}

static void profiler_note_loop(uint32_t duration_us) {
  if (!s_profiler_enabled)
    return;
  s_profiler_manual.loop_calls++;
  s_profiler_manual.loop_total_us += duration_us;
  if (duration_us > s_profiler_manual.loop_max_us)
    s_profiler_manual.loop_max_us = duration_us;
  if (duration_us > 30000)
    s_profiler_manual.loop_over_30ms++;
}

static void profiler_note_flush(uint32_t duration_us, uint32_t px) {
  if (!s_profiler_enabled)
    return;
  s_profiler_manual.flush_calls++;
  s_profiler_manual.flush_total_us += duration_us;
  if (duration_us > s_profiler_manual.flush_max_us)
    s_profiler_manual.flush_max_us = duration_us;
  s_profiler_manual.flush_px += px;
}

static void profiler_note_invalidated(uint32_t px) {
  if (!s_profiler_enabled)
    return;
  s_profiler_manual.invalidated_areas++;
  s_profiler_manual.invalidated_px += px;
}

static void profiler_print_manual_summary() {
  s_profiler_manual.ended_us = profiler_tick_us();
  uint64_t window_us = s_profiler_manual.ended_us > s_profiler_manual.started_us
                           ? s_profiler_manual.ended_us - s_profiler_manual.started_us
                           : 0;
  uint32_t loop_avg_us = s_profiler_manual.loop_calls == 0
                             ? 0
                             : (uint32_t) (s_profiler_manual.loop_total_us / s_profiler_manual.loop_calls);
  uint32_t flush_avg_us = s_profiler_manual.flush_calls == 0
                              ? 0
                              : (uint32_t) (s_profiler_manual.flush_total_us / s_profiler_manual.flush_calls);
  uint32_t ppa_fill_delta = 0;
  uint32_t ppa_img_delta = 0;
#ifdef USE_LVGL_PPA
  uint32_t ppa_fill_now = lv_draw_ppa_get_fill_task_count();
  uint32_t ppa_img_now = lv_draw_ppa_get_img_task_count();
  ppa_fill_delta = ppa_fill_now - s_profiler_manual.ppa_fill_start;
  ppa_img_delta = ppa_img_now - s_profiler_manual.ppa_img_start;
#endif
  ESP_LOGI("lvgl.prof",
           "PROFILE_COST window=%lluus loop_calls=%u loop_total=%lluus loop_avg=%uus loop_max=%uus loop_over_30ms=%u",
           (unsigned long long) window_us, (unsigned) s_profiler_manual.loop_calls,
           (unsigned long long) s_profiler_manual.loop_total_us, (unsigned) loop_avg_us,
           (unsigned) s_profiler_manual.loop_max_us, (unsigned) s_profiler_manual.loop_over_30ms);
  ESP_LOGI("lvgl.prof", "PROFILE_COST flush_calls=%u flush_total=%lluus flush_avg=%uus flush_max=%uus flush_px=%llukpx",
           (unsigned) s_profiler_manual.flush_calls, (unsigned long long) s_profiler_manual.flush_total_us,
           (unsigned) flush_avg_us, (unsigned) s_profiler_manual.flush_max_us,
           (unsigned long long) (s_profiler_manual.flush_px / 1000ULL));
  ESP_LOGI("lvgl.prof", "PROFILE_COST invalidated=%u areas/%llukpx ppa_delta=%u/%u",
           (unsigned) s_profiler_manual.invalidated_areas,
           (unsigned long long) (s_profiler_manual.invalidated_px / 1000ULL), (unsigned) ppa_fill_delta,
           (unsigned) ppa_img_delta);
}

static void profiler_init_custom() {
  s_profiler_enabled = 0;
  s_profiler_initialized = true;
}
#endif

}  // namespace esphome::lvgl

extern "C" uint32_t lvgl_esphome_get_cpu_pct(void) {
  uint32_t cpu = esphome::lvgl::s_cpu_pct;
  return cpu > 100 ? 100 : cpu;
}

extern "C" uint32_t lvgl_esphome_get_flush_ms(void) { return esphome::lvgl::s_flush_ms; }

extern "C" uint32_t lvgl_esphome_get_direct_mode_active(void) { return esphome::lvgl::s_direct_mode_active; }

extern "C" uint32_t lvgl_esphome_get_loop_max_ms(void) { return esphome::lvgl::s_loop_max_ms; }

extern "C" uint32_t lvgl_esphome_get_flush_max_ms(void) { return esphome::lvgl::s_flush_max_ms; }

extern "C" uint32_t lvgl_esphome_get_invalidated_kpx(void) { return esphome::lvgl::s_invalidated_kpx; }

extern "C" uint32_t lvgl_esphome_get_perf_logging_enabled(void) { return esphome::lvgl::s_perf_logging_enabled; }

extern "C" uint32_t lvgl_esphome_get_swipe_logging_enabled(void) { return esphome::lvgl::s_swipe_logging_enabled; }

extern "C" void lvgl_esphome_set_perf_logging_enabled(bool enabled) {
  esphome::lvgl::s_perf_logging_enabled = enabled ? 1 : 0;
}

extern "C" void lvgl_esphome_set_swipe_logging_enabled(bool enabled) {
  esphome::lvgl::s_swipe_logging_enabled = enabled ? 1 : 0;
}

extern "C" uint32_t lvgl_esphome_get_profiler_enabled(void) { return esphome::lvgl::s_profiler_enabled; }

extern "C" void lvgl_esphome_set_profiler_enabled(bool enabled) {
  if (!esphome::lvgl::s_profiler_initialized)
    esphome::lvgl::profiler_init_custom();
  if (enabled)
    esphome::lvgl::profiler_reset_summary();
  esphome::lvgl::s_profiler_enabled = enabled ? 1 : 0;
#if LV_USE_PROFILER && LV_USE_PROFILER_BUILTIN
  // Enable LVGL's built-in trace only for short diagnostic windows. The
  // always-on path remains the lightweight manual sampler.
  lv_profiler_builtin_set_enable(enabled);
  ESP_LOGI("lvgl.prof", "profiler %s (builtin trace + manual sampler)", enabled ? "enabled" : "disabled");
#else
  ESP_LOGI("lvgl.prof", "profiler %s (manual sampler)", enabled ? "enabled" : "disabled");
#endif
}

extern "C" void lvgl_esphome_profiler_flush(void) {
  if (!esphome::lvgl::s_profiler_initialized)
    return;
#if LV_USE_PROFILER && LV_USE_PROFILER_BUILTIN
  lv_profiler_builtin_flush();
  lv_profiler_builtin_set_enable(false);
#endif
  esphome::lvgl::s_profiler_enabled = 0;
  esphome::lvgl::profiler_print_manual_summary();
#if LV_USE_PROFILER && LV_USE_PROFILER_BUILTIN
  if (esphome::lvgl::s_profiler_agg_count > 0)
    esphome::lvgl::profiler_print_summary();
#endif
  ESP_LOGI("lvgl.prof", "profiler flushed");
}

extern "C" void lvgl_esphome_profiler_mark(const char *name) {
  if (!esphome::lvgl::s_profiler_initialized || !esphome::lvgl::s_profiler_enabled || name == nullptr)
    return;
  ESP_LOGI("lvgl.prof", "PROFILE_MARK t=%lluus name=%s", (unsigned long long) esphome::lvgl::profiler_tick_us(), name);
}

// Linker wrap (PlatformIO LDFLAGs -Wl,--wrap=lv_timer_get_idle and
// -Wl,--wrap=lv_os_get_idle_percent).
// LVGL sysmon's perf widget reads CPU%% via lv_os_get_idle_percent()
// when LV_USE_OS=LV_OS_FREERTOS (and via lv_timer_get_idle() under
// LV_OS_NONE). Wrap both so the overlay reads our s_cpu_pct regardless
// of the OS mode. Returns 100 - cpu, the "idle %" sysmon expects.
extern "C" uint32_t __wrap_lv_timer_get_idle(void) {
  uint32_t cpu = esphome::lvgl::s_cpu_pct;
  if (cpu > 100)
    cpu = 100;
  return 100 - cpu;
}

extern "C" uint32_t __wrap_lv_os_get_idle_percent(void) {
  uint32_t cpu = esphome::lvgl::s_cpu_pct;
  if (cpu > 100)
    cpu = 100;
  return 100 - cpu;
}

namespace esphome::lvgl {

#ifdef USE_LVGL_PPA
/// Dedicated PPA SRM client for display framebuffer rotation (separate from LVGL draw unit).
static ppa_client_handle_t s_display_srm_client = nullptr;
/// Queued RGB888 copy client used by the circular app reveal compositor.
static ppa_client_handle_t s_snapshot_app_srm_client = nullptr;
/// Region updates originate outside the normal LVGL draw pass. Keep them on
/// their own client so a blocked display/snapshot transaction cannot consume
/// the sole transaction descriptor used by the media control presenter.
static ppa_client_handle_t s_region_srm_client = nullptr;
/// Small ARGB-over-RGB composites used by direct regional animations. This
/// keeps text alpha blending off the CPU without sharing the image-transition
/// client owned by the gallery worker.
static ppa_client_handle_t s_region_blend_client = nullptr;
/// Solid edge bands for elastic snapshot scrolling.
static ppa_client_handle_t s_snapshot_fill_client = nullptr;
// Title, media control and volume overlays can submit direct region updates
// from different FreeRTOS tasks. The PPA ownership markers below describe one
// transaction, so serialize the complete cache/PPA hand-off rather than
// allowing the clients to overwrite each other's ranges.
static StaticSemaphore_t s_region_srm_mutex_storage;
static SemaphoreHandle_t s_region_srm_mutex = nullptr;
// Hardware JPEG produces the immutable source directly in PSRAM and PPA
// overwrites every pixel of the idle DSI framebuffer. Repeating cache
// writeback/invalidation for every SRM band is both redundant and unsafe on
// ESP32-P4 while the other core is accessing a different PSRAM region.
static std::atomic<uintptr_t> s_direct_ppa_source_begin{0};
static std::atomic<uintptr_t> s_direct_ppa_source_end{0};
static std::atomic<uintptr_t> s_direct_ppa_source2_begin{0};
static std::atomic<uintptr_t> s_direct_ppa_source2_end{0};
static std::atomic<uintptr_t> s_direct_ppa_target_begin{0};
static std::atomic<uintptr_t> s_direct_ppa_target_end{0};

struct DirectPpaFrameCompletion {
  std::atomic<uint32_t> remaining{0};
  TaskHandle_t waiter{nullptr};
};

static bool IRAM_ATTR direct_ppa_frame_done(ppa_client_handle_t, ppa_event_data_t *, void *user_data) {
  auto *completion = static_cast<DirectPpaFrameCompletion *>(user_data);
  if (completion == nullptr)
    return false;
  if (completion->remaining.fetch_sub(1, std::memory_order_acq_rel) != 1)
    return false;
  BaseType_t high_priority_woken = pdFALSE;
  vTaskNotifyGiveFromISR(completion->waiter, &high_priority_woken);
  return high_priority_woken == pdTRUE;
}
/// Dedicated PPA SRM client for the partial framebuffer compositor.
static ppa_client_handle_t s_compositor_srm_client = nullptr;
static size_t s_compositor_srm_alignment = 0;

extern "C" bool esphome_artwork_image_buffer_written_by_dma(const void *ptr) __attribute__((weak));

extern "C" bool esphome_lvgl_ppa_skip_cache_msync(const void *buffer, size_t size, int flags) {
  if (buffer == nullptr || size == 0)
    return false;

  const uintptr_t begin = reinterpret_cast<uintptr_t>(buffer);
  uintptr_t end = 0;
  if (__builtin_add_overflow(begin, size, &end))
    return false;

  if ((flags & ESP_CACHE_MSYNC_FLAG_DIR_M2C) != 0) {
    const uintptr_t target_begin = s_direct_ppa_target_begin.load(std::memory_order_acquire);
    const uintptr_t target_end = s_direct_ppa_target_end.load(std::memory_order_acquire);
    return target_begin != 0 && begin >= target_begin && end <= target_end;
  }

  const uintptr_t source_begin = s_direct_ppa_source_begin.load(std::memory_order_acquire);
  const uintptr_t source_end = s_direct_ppa_source_end.load(std::memory_order_acquire);
  if (source_begin != 0 && begin >= source_begin && end <= source_end)
    return true;
  const uintptr_t source2_begin = s_direct_ppa_source2_begin.load(std::memory_order_acquire);
  const uintptr_t source2_end = s_direct_ppa_source2_end.load(std::memory_order_acquire);
  return source2_begin != 0 && begin >= source2_begin && end <= source2_end;
}

/**
 * Attempt to rotate a display framebuffer using the PPA SRM hardware.
 * Returns true if PPA rotation succeeded, false if software fallback is needed.
 *
 * Angle mapping (PPA uses CCW, ESPHome uses CW):
 *   90° CW  → PPA_SRM_ROTATION_ANGLE_270 (270° CCW)
 *   180°    → PPA_SRM_ROTATION_ANGLE_180
 *   270° CW → PPA_SRM_ROTATION_ANGLE_90  (90° CCW)
 */
static bool ppa_rotate_display_buf(const void *src, void *dst, int32_t w, int32_t h, display::DisplayRotation rot) {
  if (s_display_srm_client == nullptr || w < 2 || h < 2)
    return false;

  // ESP32-P4 PPA requires both buffer address and buffer_size to be aligned
  // to the data cache line size — 64 B by default, 128 B if
  // CONFIG_CACHE_L2_CACHE_LINE_128B=y. Use the larger value so the check
  // passes under both sdkconfigs.
  constexpr uintptr_t CACHE_LINE = 128;
  if ((reinterpret_cast<uintptr_t>(src) & (CACHE_LINE - 1)) != 0)
    return false;
  if ((reinterpret_cast<uintptr_t>(dst) & (CACHE_LINE - 1)) != 0)
    return false;

  ppa_srm_rotation_angle_t ppa_angle;
  int32_t out_w, out_h;
  switch (rot) {
    case display::DISPLAY_ROTATION_90_DEGREES:
      ppa_angle = PPA_SRM_ROTATION_ANGLE_270;
      out_w = h;
      out_h = w;
      break;
    case display::DISPLAY_ROTATION_180_DEGREES:
      ppa_angle = PPA_SRM_ROTATION_ANGLE_180;
      out_w = w;
      out_h = h;
      break;
    case display::DISPLAY_ROTATION_270_DEGREES:
      ppa_angle = PPA_SRM_ROTATION_ANGLE_90;
      out_w = h;
      out_h = w;
      break;
    default:
      return false;
  }

#if LV_COLOR_DEPTH == 32
  constexpr ppa_srm_color_mode_t PPA_CM = PPA_SRM_COLOR_MODE_RGB888;
  constexpr size_t BPP = 3;
#else
  constexpr ppa_srm_color_mode_t PPA_CM = PPA_SRM_COLOR_MODE_RGB565;
  constexpr size_t BPP = 2;
#endif

  size_t out_bytes = (size_t) out_w * out_h * BPP;
  size_t aligned_out_bytes = (out_bytes + CACHE_LINE - 1) & ~(CACHE_LINE - 1);

  ppa_srm_oper_config_t cfg = {};
  cfg.in.buffer = (void *) src;
  cfg.in.pic_w = w;
  cfg.in.pic_h = h;
  cfg.in.block_w = w;
  cfg.in.block_h = h;
  cfg.in.srm_cm = PPA_CM;
  cfg.out.buffer = dst;
  cfg.out.buffer_size = aligned_out_bytes;
  cfg.out.pic_w = out_w;
  cfg.out.pic_h = out_h;
  cfg.out.srm_cm = PPA_CM;
  cfg.rotation_angle = ppa_angle;
  cfg.scale_x = 1.0f;  // must be 1.0f, not 0.0f (default after zero-init)
  cfg.scale_y = 1.0f;
  cfg.alpha_update_mode = PPA_ALPHA_NO_CHANGE;
  cfg.mode = PPA_TRANS_MODE_BLOCKING;

  esp_err_t ret = ppa_do_scale_rotate_mirror(s_display_srm_client, &cfg);
  if (ret != ESP_OK) {
    static bool warned = false;
    if (!warned) {
      ESP_LOGW(TAG, "PPA display rotation unavailable (err=%d), using SW fallback", ret);
      warned = true;
    }
  }
  return ret == ESP_OK;
}
#endif  // USE_LVGL_PPA

static const size_t MIN_BUFFER_FRAC = 8;

static const char *const EVENT_NAMES[] = {
    "NONE",
    "PRESSED",
    "PRESSING",
    "PRESS_LOST",
    "SHORT_CLICKED",
    "LONG_PRESSED",
    "LONG_PRESSED_REPEAT",
    "CLICKED",
    "RELEASED",
    "SCROLL_BEGIN",
    "SCROLL_END",
    "SCROLL",
    "GESTURE",
    "KEY",
    "FOCUSED",
    "DEFOCUSED",
    "LEAVE",
    "HIT_TEST",
    "COVER_CHECK",
    "REFR_EXT_DRAW_SIZE",
    "DRAW_MAIN_BEGIN",
    "DRAW_MAIN",
    "DRAW_MAIN_END",
    "DRAW_POST_BEGIN",
    "DRAW_POST",
    "DRAW_POST_END",
    "DRAW_PART_BEGIN",
    "DRAW_PART_END",
    "VALUE_CHANGED",
    "INSERT",
    "REFRESH",
    "READY",
    "CANCEL",
    "DELETE",
    "CHILD_CHANGED",
    "CHILD_CREATED",
    "CHILD_DELETED",
    "SCREEN_UNLOAD_START",
    "SCREEN_LOAD_START",
    "SCREEN_LOADED",
    "SCREEN_UNLOADED",
    "SIZE_CHANGED",
    "STYLE_CHANGED",
    "LAYOUT_CHANGED",
    "GET_SELF_SIZE",
};

static const unsigned LOG_LEVEL_MAP[] = {
    ESPHOME_LOG_LEVEL_DEBUG, ESPHOME_LOG_LEVEL_INFO,  ESPHOME_LOG_LEVEL_WARN,
    ESPHOME_LOG_LEVEL_ERROR, ESPHOME_LOG_LEVEL_ERROR, ESPHOME_LOG_LEVEL_NONE,

};

std::string lv_event_code_name_for(lv_event_t *event) {
  auto event_code = lv_event_get_code(event);
  if (event_code < sizeof(EVENT_NAMES) / sizeof(EVENT_NAMES[0])) {
    return EVENT_NAMES[event_code];
  }
  char buffer[8];
  snprintf(buffer, sizeof(buffer), "%2u", static_cast<unsigned>(event_code));
  return buffer;
}

static void rounder_cb(lv_event_t *event) {
  auto *comp = static_cast<LvglComponent *>(lv_event_get_user_data(event));
  auto *area = static_cast<lv_area_t *>(lv_event_get_param(event));
  // cater for display driver chips with special requirements for bounds of partial
  // draw areas. Extend the draw area to satisfy:
  // * Coordinates must be a multiple of draw_rounding
  auto draw_rounding = comp->draw_rounding;
  // round down the start coordinates
  area->x1 = area->x1 / draw_rounding * draw_rounding;
  area->y1 = area->y1 / draw_rounding * draw_rounding;
  // round up the end coordinates
  area->x2 = (area->x2 + draw_rounding) / draw_rounding * draw_rounding - 1;
  area->y2 = (area->y2 + draw_rounding) / draw_rounding * draw_rounding - 1;
  comp->record_invalidated_area(area);
}

void LvglComponent::record_invalidated_area(const lv_area_t *area) {
  uint32_t px = (uint32_t) lv_area_get_width(area) * (uint32_t) lv_area_get_height(area);
  this->perf_invalidated_px_ += px;
  this->perf_invalidated_areas_++;
  profiler_note_invalidated(px);
}

void LvglComponent::render_end_cb(lv_event_t *event) {
  auto *comp = static_cast<LvglComponent *>(lv_event_get_user_data(event));
  comp->draw_end_();
}

void LvglComponent::render_start_cb(lv_event_t *event) {
  ESP_LOGVV(TAG, "Draw start");
  auto *comp = static_cast<LvglComponent *>(lv_event_get_user_data(event));
  comp->draw_start_();
}

lv_event_code_t lv_update_event;  // NOLINT
void LvglComponent::dump_config() {
  ESP_LOGCONFIG(TAG,
                "LVGL:\n"
                "  Display width/height: %d x %d\n"
                "  Buffer size: %zu%%\n"
                "  Rotation: %d\n"
                "  Draw rounding: %d",
                this->width_, this->height_, 100 / this->buffer_frac_, this->rotation, (int) this->draw_rounding);
#ifdef USE_LVGL_PPA
  ESP_LOGCONFIG(TAG, "  PPA SRM (display rotation): %s",
                s_display_srm_client != nullptr ? "registered (HW)" : "failed (SW fallback)");
#ifdef USE_LVGL_PPA_DRAW_UNIT
  ESP_LOGCONFIG(TAG, "  PPA draw unit:              registered (fill/image → HW)");
#else
  ESP_LOGCONFIG(TAG, "  PPA draw unit:              disabled");
#endif
#ifdef USE_LVGL_PPA_BLEND_HANDLER
  ESP_LOGCONFIG(TAG, "  PPA SW-blend handler (v9):  registered (fills/blends → HW)");
#else
  ESP_LOGCONFIG(TAG, "  PPA SW-blend handler (v9):  disabled");
#endif
#else
  ESP_LOGCONFIG(TAG, "  PPA acceleration: disabled (use_ppa: false)");
#endif
}

void LvglComponent::set_paused(bool paused, bool show_snow) {
  this->paused_ = paused;
  this->show_snow_ = show_snow;
  if (!paused && lv_screen_active() != nullptr) {
    lv_lock();
    lv_display_trigger_activity(this->disp_);  // resets the inactivity time
    lv_obj_invalidate(lv_screen_active());
    lv_unlock();
  }
  if (paused && this->pause_callback_ != nullptr)
    this->pause_callback_->trigger();
  if (!paused && this->resume_callback_ != nullptr)
    this->resume_callback_->trigger();
}

void LvglComponent::esphome_lvgl_init() {
  lv_init();
#ifdef USE_LVGL_PPA
#ifdef USE_LVGL_PPA_DRAW_UNIT
  // Two PPA paths active at once for max coverage:
  //
  //   1) lv_draw_ppa unit (full draw unit, lv_draw_ppa_init)
  //      → accelerates IMAGE draw tasks (canvas widget, lv_image)
  //      → critical for camera streaming through lv_canvas
  //
  //   2) lvgl_ppa_accel_v9 (SW-blend handler, lvgl_port_ppa_v9_init)
  //      → accelerates RGB565 fills/blends in the SW pipeline
  //      → catches what the draw unit rejects (radius != 0, opa < max,
  //        gradients, etc.)
  //
  // Espressif's esp_lvgl_adapter only uses (2), but that leaves canvas/
  // image draws going through the slow SW image renderer. With a 640x480
  // RGB565 camera canvas, this added ~50 ms of LVGL overhead per frame.
  // Enabling (1) brings image drawing back onto PPA hardware.
  lv_draw_ppa_init();
#endif

  // Register a dedicated PPA SRM client for display framebuffer rotation.
  // This is independent of the LVGL draw pipeline and stays enabled.
  if (s_display_srm_client == nullptr) {
    ppa_client_config_t srm_cfg = {};
    srm_cfg.oper_type = PPA_OPERATION_SRM;
    srm_cfg.max_pending_trans_num = 1;
    srm_cfg.data_burst_length = LVGL_ESPHOME_PPA_SRM_DATA_BURST_LENGTH;
    if (ppa_register_client(&srm_cfg, &s_display_srm_client) == ESP_OK) {
      ESP_LOGI(TAG, "PPA display rotation SRM client registered (srm_burst=%d)",
               (int) LVGL_ESPHOME_PPA_SRM_DATA_BURST_LENGTH);
    } else {
      ESP_LOGW(TAG, "PPA display rotation SRM client failed, SW rotation will be used");
      s_display_srm_client = nullptr;
    }
  }
  if (s_snapshot_app_srm_client == nullptr) {
    ppa_client_config_t app_cfg = {};
    app_cfg.oper_type = PPA_OPERATION_SRM;
    app_cfg.max_pending_trans_num = 12;
    app_cfg.data_burst_length = LVGL_ESPHOME_PPA_DIRECT_ANIMATION_DATA_BURST_LENGTH;
    if (ppa_register_client(&app_cfg, &s_snapshot_app_srm_client) == ESP_OK) {
      ppa_event_callbacks_t callbacks{};
      callbacks.on_trans_done = direct_ppa_frame_done;
      if (ppa_client_register_event_callbacks(s_snapshot_app_srm_client, &callbacks) != ESP_OK) {
        ppa_unregister_client(s_snapshot_app_srm_client);
        s_snapshot_app_srm_client = nullptr;
      }
    }
    if (s_snapshot_app_srm_client != nullptr) {
      ESP_LOGI(TAG, "PPA app reveal SRM client registered (srm_burst=%d)",
               (int) LVGL_ESPHOME_PPA_DIRECT_ANIMATION_DATA_BURST_LENGTH);
    } else {
      ESP_LOGW(TAG, "PPA app reveal SRM client failed; CPU compositor will be used");
    }
  }
  if (s_region_srm_client == nullptr) {
    ppa_client_config_t region_cfg = {};
    region_cfg.oper_type = PPA_OPERATION_SRM;
    region_cfg.max_pending_trans_num = 1;
    region_cfg.data_burst_length = LVGL_ESPHOME_PPA_DIRECT_REGION_DATA_BURST_LENGTH;
    if (ppa_register_client(&region_cfg, &s_region_srm_client) == ESP_OK) {
      ESP_LOGI(TAG, "PPA direct region SRM client registered (srm_burst=%d)",
               (int) LVGL_ESPHOME_PPA_DIRECT_REGION_DATA_BURST_LENGTH);
    } else {
      ESP_LOGW(TAG, "PPA direct region SRM client failed; regional updates will use LVGL invalidation");
      s_region_srm_client = nullptr;
    }
  }
  if (s_region_blend_client == nullptr) {
    ppa_client_config_t blend_cfg = {};
    blend_cfg.oper_type = PPA_OPERATION_BLEND;
    blend_cfg.max_pending_trans_num = 1;
    blend_cfg.data_burst_length = LVGL_ESPHOME_PPA_DIRECT_REGION_DATA_BURST_LENGTH;
    if (ppa_register_client(&blend_cfg, &s_region_blend_client) == ESP_OK) {
      ESP_LOGI(TAG, "PPA direct region blend client registered (burst=%d)",
               (int) LVGL_ESPHOME_PPA_DIRECT_REGION_DATA_BURST_LENGTH);
    } else {
      ESP_LOGW(TAG, "PPA direct region blend client failed; regional alpha composites will use software");
      s_region_blend_client = nullptr;
    }
  }
  if (s_snapshot_fill_client == nullptr) {
    ppa_client_config_t fill_cfg = {};
    fill_cfg.oper_type = PPA_OPERATION_FILL;
    fill_cfg.max_pending_trans_num = 1;
    fill_cfg.data_burst_length = LVGL_ESPHOME_PPA_DIRECT_REGION_DATA_BURST_LENGTH;
    if (ppa_register_client(&fill_cfg, &s_snapshot_fill_client) != ESP_OK) {
      ESP_LOGW(TAG, "PPA snapshot edge fill client failed; elastic list edges will use CPU fill");
      s_snapshot_fill_client = nullptr;
    }
  }
  if (s_region_srm_mutex == nullptr) {
    s_region_srm_mutex = xSemaphoreCreateMutexStatic(&s_region_srm_mutex_storage);
  }
#endif
  lv_tick_set_cb([] { return millis(); });
#if LV_USE_PROFILER && LV_USE_PROFILER_BUILTIN
  profiler_init_custom();
#endif
  lv_update_event = static_cast<lv_event_code_t>(lv_event_register_id());
}

void LvglComponent::add_event_cb(lv_obj_t *obj, event_callback_t callback, lv_event_code_t event) {
  lv_obj_add_event_cb(obj, callback, event, nullptr);
}

void LvglComponent::add_event_cb(lv_obj_t *obj, event_callback_t callback, lv_event_code_t event1,
                                 lv_event_code_t event2) {
  add_event_cb(obj, callback, event1);
  add_event_cb(obj, callback, event2);
}

void LvglComponent::add_event_cb(lv_obj_t *obj, event_callback_t callback, lv_event_code_t event1,
                                 lv_event_code_t event2, lv_event_code_t event3) {
  add_event_cb(obj, callback, event1);
  add_event_cb(obj, callback, event2);
  add_event_cb(obj, callback, event3);
}

void LvglComponent::add_page(LvPageType *page) {
  this->pages_.push_back(page);
  page->set_parent(this);
  lv_display_set_default(this->disp_);
  page->setup(this->pages_.size() - 1);
}

void LvglComponent::show_page(size_t index, lv_screen_load_anim_t anim, uint32_t time) {
  if (index >= this->pages_.size())
    return;
  this->current_page_ = index;
  if (anim == LV_SCREEN_LOAD_ANIM_NONE) {
    lv_screen_load(this->pages_[this->current_page_]->obj);
  } else {
    lv_screen_load_anim(this->pages_[this->current_page_]->obj, anim, time, 0, false);
  }
}

void LvglComponent::show_next_page(lv_screen_load_anim_t anim, uint32_t time) {
  if (this->pages_.empty() || (this->current_page_ == this->pages_.size() - 1 && !this->page_wrap_))
    return;
  size_t start = this->current_page_;
  do {
    this->current_page_ = (this->current_page_ + 1) % this->pages_.size();
    if (this->current_page_ == start)
      return;  // all pages have skip=true (guaranteed not to happen by YAML validation)
  } while (this->pages_[this->current_page_]->skip);  // skip empty pages()
  this->show_page(this->current_page_, anim, time);
}

void LvglComponent::show_prev_page(lv_screen_load_anim_t anim, uint32_t time) {
  if (this->pages_.empty() || (this->current_page_ == 0 && !this->page_wrap_))
    return;
  size_t start = this->current_page_;
  do {
    this->current_page_ = (this->current_page_ + this->pages_.size() - 1) % this->pages_.size();
    if (this->current_page_ == start)
      return;  // all pages have skip=true (guaranteed not to happen by YAML validation)
  } while (this->pages_[this->current_page_]->skip);  // skip empty pages()
  this->show_page(this->current_page_, anim, time);
}

size_t LvglComponent::get_current_page() const { return this->current_page_; }
bool LvPageType::is_showing() const { return this->parent_->get_current_page() == this->index; }

void LvglComponent::navigation_touch_begin(int32_t x, int32_t y) {
  if (this->navigation_ != nullptr)
    this->navigation_->touch_begin(x, y);
}

bool LvglComponent::navigation_touch_update(int32_t x, int32_t y) {
  return this->navigation_ != nullptr && this->navigation_->touch_update(x, y);
}

bool LvglComponent::navigation_touch_end() { return this->navigation_ != nullptr && this->navigation_->touch_end(); }

void LvglComponent::navigation_touch_cancel() {
  if (this->navigation_ != nullptr)
    this->navigation_->touch_cancel();
}

void LvglComponent::rotate_coordinates(int32_t &x, int32_t &y) const {
  switch (this->rotation) {
    default:
      break;
    case display::DISPLAY_ROTATION_180_DEGREES:
      x = this->width_ - x - 1;
      y = this->height_ - y - 1;
      break;
    case display::DISPLAY_ROTATION_270_DEGREES: {
      const auto original_x = x;
      x = this->height_ - y - 1;
      y = original_x;
      break;
    }
    case display::DISPLAY_ROTATION_90_DEGREES: {
      const auto original_y = y;
      y = this->width_ - x - 1;
      x = original_y;
      break;
    }
  }
}

void LvglComponent::draw_buffer_(const lv_area_t *area, lv_color_data *ptr) {
  auto width = lv_area_get_width(area);
  auto height = lv_area_get_height(area);
  auto height_rounded = (height + this->draw_rounding - 1) / this->draw_rounding * this->draw_rounding;
  auto x1 = area->x1;
  auto y1 = area->y1;
  auto *dst = reinterpret_cast<lv_color_data *>(this->rotate_buf_);
  const auto *src8 = reinterpret_cast<const uint8_t *>(ptr);
  const bool direct_full_buffer =
      this->direct_mode_active_ &&
      (src8 == this->draw_buf_ || (this->draw_buf2_ != nullptr && src8 == this->draw_buf2_));

#ifdef USE_LVGL_PPA
  // Try PPA hardware rotation first (zero CPU cost, ~10x faster than SW loops).
  // Falls back to software automatically if PPA rejects the operation.
  if (s_display_srm_client != nullptr && this->rotation != display::DISPLAY_ROTATION_0_DEGREES) {
    if (ppa_rotate_display_buf(ptr, this->rotate_buf_, width, height, this->rotation)) {
      // dst already points to rotate_buf_ (initialized above)
      // Coordinate update: identical geometry to the software path
      switch (this->rotation) {
        case display::DISPLAY_ROTATION_90_DEGREES:
          y1 = x1;
          x1 = this->height_ - area->y1 - height;
          height = width;
          width = height_rounded;
          break;
        case display::DISPLAY_ROTATION_180_DEGREES:
          x1 = this->width_ - x1 - width;
          y1 = this->height_ - y1 - height;
          break;
        case display::DISPLAY_ROTATION_270_DEGREES:
          x1 = y1;
          y1 = this->width_ - area->x1 - width;
          height = width;
          width = height_rounded;
          break;
        default:
          break;
      }
      for (auto *display : this->displays_) {
        display->draw_pixels_at(x1, y1, width, height, (const uint8_t *) dst, display::COLOR_ORDER_RGB, LV_BITNESS,
                                this->big_endian_);
      }
      return;
    }
    // PPA failed → fall through to software rotation below
  }
#endif  // USE_LVGL_PPA

  switch (this->rotation) {
    case display::DISPLAY_ROTATION_90_DEGREES:
#if LV_COLOR_DEPTH == 32
    {
      // RGB888: 3 bytes per pixel
      auto *dst8 = reinterpret_cast<uint8_t *>(this->rotate_buf_);
      auto *ptr8 = reinterpret_cast<const uint8_t *>(ptr);
      for (lv_coord_t x = height; x-- != 0;) {
        for (lv_coord_t y = 0; y != width; y++) {
          size_t out = (size_t(y) * height_rounded + x) * 3;
          dst8[out + 0] = *ptr8++;
          dst8[out + 1] = *ptr8++;
          dst8[out + 2] = *ptr8++;
        }
      }
    }
#else
      for (lv_coord_t x = height; x-- != 0;) {
        for (lv_coord_t y = 0; y != width; y++) {
          dst[y * height_rounded + x] = *ptr++;
        }
      }
#endif
      y1 = x1;
      x1 = this->height_ - area->y1 - height;
      height = width;
      width = height_rounded;
      break;

    case display::DISPLAY_ROTATION_180_DEGREES:
#if LV_COLOR_DEPTH == 32
    {
      // RGB888: 3 bytes per pixel
      auto *dst8 = reinterpret_cast<uint8_t *>(this->rotate_buf_);
      auto *ptr8 = reinterpret_cast<const uint8_t *>(ptr);
      for (lv_coord_t y = height; y-- != 0;) {
        for (lv_coord_t x = width; x-- != 0;) {
          size_t out = (size_t(y) * width + x) * 3;
          dst8[out + 0] = *ptr8++;
          dst8[out + 1] = *ptr8++;
          dst8[out + 2] = *ptr8++;
        }
      }
    }
#else
      for (lv_coord_t y = height; y-- != 0;) {
        for (lv_coord_t x = width; x-- != 0;) {
          dst[y * width + x] = *ptr++;
        }
      }
#endif
      x1 = this->width_ - x1 - width;
      y1 = this->height_ - y1 - height;
      break;

    case display::DISPLAY_ROTATION_270_DEGREES:
#if LV_COLOR_DEPTH == 32
    {
      // RGB888: 3 bytes per pixel
      auto *dst8 = reinterpret_cast<uint8_t *>(this->rotate_buf_);
      auto *ptr8 = reinterpret_cast<const uint8_t *>(ptr);
      for (lv_coord_t x = 0; x != height; x++) {
        for (lv_coord_t y = width; y-- != 0;) {
          size_t out = (size_t(y) * height_rounded + x) * 3;
          dst8[out + 0] = *ptr8++;
          dst8[out + 1] = *ptr8++;
          dst8[out + 2] = *ptr8++;
        }
      }
    }
#else
      for (lv_coord_t x = 0; x != height; x++) {
        for (lv_coord_t y = width; y-- != 0;) {
          dst[y * height_rounded + x] = *ptr++;
        }
      }
#endif
      x1 = y1;
      y1 = this->width_ - area->x1 - width;
      height = width;
      width = height_rounded;
      break;

    default:
      if (direct_full_buffer) {
        for (auto *display : this->displays_) {
          display->draw_pixels_at(x1, y1, width, height, src8, display::COLOR_ORDER_RGB, LV_BITNESS, this->big_endian_);
        }
        return;
      }
      dst = ptr;
      break;
  }
  for (auto *display : this->displays_) {
    display->draw_pixels_at(x1, y1, width, height, (const uint8_t *) dst, display::COLOR_ORDER_RGB, LV_BITNESS,
                            this->big_endian_);
  }
}

void LvglComponent::flush_cb_(lv_display_t *disp_drv, const lv_area_t *area, uint8_t *color_p) {
  if (!this->is_paused()) {
    uint64_t t0 = esp_timer_get_time();
#ifdef USE_MIPI_DSI
#ifdef USE_ESP32
    if (this->partial_compositor_flush_(disp_drv, area, color_p, t0)) {
      return;
    }
#endif
    if (!this->direct_mode_active_ && this->rotation == display::DISPLAY_ROTATION_0_DEGREES &&
        this->displays_.size() == 1) {
      auto *mipi_display = static_cast<mipi_dsi::MipiDsi *>(this->displays_[0]);
      const int width = lv_area_get_width(area);
      const int height = lv_area_get_height(area);
      if (mipi_display->draw_pixels_at_async(area->x1, area->y1, width, height, color_p, display::COLOR_ORDER_RGB,
                                             LV_BITNESS, this->big_endian_, 0, 0, 0, lvgl_mipi_async_flush_ready,
                                             disp_drv)) {
        const uint32_t flush_us = (uint32_t) (esp_timer_get_time() - t0);
        if (flush_us > this->perf_flush_max_us_)
          this->perf_flush_max_us_ = flush_us;
        const uint32_t flush_px = (uint32_t) width * (uint32_t) height;
        this->perf_flush_px_ += flush_px;
        profiler_note_flush(flush_us, flush_px);
        this->perf_flush_us_ += flush_us;
        ESP_LOGV(TAG, "async flush_cb, area=%d/%d, %d/%d scheduled in %lu us", area->x1, area->y1, width, height,
                 (unsigned long) flush_us);
        return;
      }
    }
#endif
    if (this->direct_mode_active_) {
      this->direct_last_flushed_buf_ = color_p;
      if (lv_display_flush_is_last(disp_drv)) {
        this->draw_buffer_(area, reinterpret_cast<lv_color_data *>(color_p));
        // Direct mode renders into the MIPI panel framebuffers.  Let the panel
        // finish one refresh before LVGL starts drawing into the next buffer;
        // otherwise fast animated widgets can occasionally race the scanout and
        // show short horizontal artifacts.
        this->wait_for_direct_frame_presented(20);
        if (!this->full_refresh_) {
          this->sync_direct_other_buffer_(area, color_p);
        }
#if LV_COLOR_DEPTH == 32 && defined(USE_ESP32) && defined(USE_LVGL_PPA)
        // The regional compositor rebases lazily from the frame that LVGL has
        // just presented. A generation counter avoids touching its mutex from
        // the time-critical flush callback and lets an in-flight stale frame
        // be discarded before it reaches DSI.
        this->direct_region_base_generation_.fetch_add(1, std::memory_order_release);
#endif
      } else {
        if (!this->sync_direct_other_buffer_(area, color_p)) {
          this->sync_direct_framebuffer_area_(area, color_p);
        }
      }
    } else {
      this->draw_buffer_(area, reinterpret_cast<lv_color_data *>(color_p));
    }
    uint64_t dt = esp_timer_get_time() - t0;
    uint32_t flush_us = (uint32_t) dt;
    if (flush_us > this->perf_flush_max_us_)
      this->perf_flush_max_us_ = flush_us;
    uint32_t flush_px = (uint32_t) lv_area_get_width(area) * (uint32_t) lv_area_get_height(area);
    this->perf_flush_px_ += flush_px;
    profiler_note_flush(flush_us, flush_px);
    // Track flush wait time so loop() can subtract it when computing
    // CPU%% — the synchronous DMA push isn't real CPU work.
    this->perf_flush_us_ += dt;
    ESP_LOGV(TAG, "flush_cb, area=%d/%d, %d/%d took %llu us", area->x1, area->y1, lv_area_get_width(area),
             lv_area_get_height(area), (unsigned long long) dt);
  }
  lv_display_flush_ready(disp_drv);
}

void LvglComponent::sync_direct_framebuffer_area_(const lv_area_t *area, uint8_t *color_p) {
#ifdef USE_ESP32
#if LV_COLOR_DEPTH == 32
  constexpr size_t BYTES_PER_PIXEL = 3;
#else
  constexpr size_t BYTES_PER_PIXEL = LV_COLOR_DEPTH / 8;
#endif
  const int32_t y1 = std::max<int32_t>(0, area->y1);
  const int32_t y2 = std::min<int32_t>(this->height_ - 1, area->y2);
  if (y2 < y1)
    return;
  if (this->draw_buf_ == nullptr)
    return;

  const size_t row_bytes = this->width_ * BYTES_PER_PIXEL;
  const size_t fb_bytes = this->width_ * this->height_ * BYTES_PER_PIXEL;
  uint8_t *framebuffer = nullptr;
  if (color_p >= this->draw_buf_ && color_p < this->draw_buf_ + fb_bytes) {
    framebuffer = this->draw_buf_;
  } else if (this->draw_buf2_ != nullptr && color_p >= this->draw_buf2_ && color_p < this->draw_buf2_ + fb_bytes) {
    framebuffer = this->draw_buf2_;
  } else {
    return;
  }

  uint8_t *sync_start = framebuffer + (size_t) y1 * row_bytes;
  const size_t sync_size = (size_t) (y2 - y1 + 1) * row_bytes;
  lvgl_cache_msync_external(sync_start, sync_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
#endif
}

bool LvglComponent::sync_direct_other_buffer_(const lv_area_t *area, uint8_t *color_p) {
#ifdef USE_ESP32
#if LV_COLOR_DEPTH == 32
  constexpr size_t BYTES_PER_PIXEL = 3;
#else
  constexpr size_t BYTES_PER_PIXEL = LV_COLOR_DEPTH / 8;
#endif
  const int32_t x1 = std::max<int32_t>(0, area->x1);
  const int32_t y1 = std::max<int32_t>(0, area->y1);
  const int32_t x2 = std::min<int32_t>(this->width_ - 1, area->x2);
  const int32_t y2 = std::min<int32_t>(this->height_ - 1, area->y2);
  if (x2 < x1 || y2 < y1)
    return false;
  if (this->draw_buf_ == nullptr || this->draw_buf2_ == nullptr)
    return false;

  auto sync_range = [](uint8_t *ptr, size_t len) { lvgl_cache_msync_external(ptr, len, ESP_CACHE_MSYNC_FLAG_DIR_C2M); };

  const size_t row_bytes = this->width_ * BYTES_PER_PIXEL;
  const size_t area_width_bytes = (x2 - x1 + 1) * BYTES_PER_PIXEL;
  const size_t fb_bytes = this->width_ * this->height_ * BYTES_PER_PIXEL;
  uint8_t *src = nullptr;
  uint8_t *dst = nullptr;
  if (color_p >= this->draw_buf_ && color_p < this->draw_buf_ + fb_bytes) {
    src = this->draw_buf_;
    dst = this->draw_buf2_;
  } else if (color_p >= this->draw_buf2_ && color_p < this->draw_buf2_ + fb_bytes) {
    src = this->draw_buf2_;
    dst = this->draw_buf_;
  } else {
    return false;
  }

  if (x1 == 0 && area_width_bytes == row_bytes) {
    uint8_t *dst_block = dst + y1 * row_bytes;
    const uint8_t *src_block = src + y1 * row_bytes;
    const size_t block_bytes = (size_t) (y2 - y1 + 1) * row_bytes;
    sync_range(const_cast<uint8_t *>(src_block), block_bytes);
    memcpy(dst_block, src_block, block_bytes);
    sync_range(dst_block, block_bytes);
  } else {
    for (int32_t y = y1; y <= y2; y++) {
      uint8_t *dst_line = dst + y * row_bytes + x1 * BYTES_PER_PIXEL;
      const uint8_t *src_line = src + y * row_bytes + x1 * BYTES_PER_PIXEL;
      sync_range(const_cast<uint8_t *>(src_line), area_width_bytes);
      memcpy(dst_line, src_line, area_width_bytes);
      sync_range(dst_line, area_width_bytes);
    }
  }
  return true;
#else
  return false;
#endif
}

uint8_t *LvglComponent::next_direct_render_buffer_() const {
  if (!this->direct_mode_active_ || this->draw_buf_ == nullptr)
    return this->draw_buf_;
  if (this->draw_buf2_ == nullptr)
    return this->draw_buf_;
  if (this->direct_last_flushed_buf_ == this->draw_buf_)
    return this->draw_buf2_;
  if (this->direct_last_flushed_buf_ == this->draw_buf2_)
    return this->draw_buf_;
  return this->draw_buf_;
}

void LvglComponent::present_direct_render_buffer_(uint8_t *buffer) {
  if (buffer == nullptr)
    return;
  for (auto *display : this->displays_) {
    display->draw_pixels_at(0, 0, this->width_, this->height_, buffer, display::COLOR_ORDER_RGB, LV_BITNESS,
                            this->big_endian_);
  }
  this->direct_last_flushed_buf_ = buffer;
}

uint8_t *LvglComponent::next_snapshot_render_buffer_(const uint8_t *exclude_a, const uint8_t *exclude_b,
                                                     uint32_t wait_ms) {
#ifdef USE_MIPI_DSI
  if (this->rotation == display::DISPLAY_ROTATION_0_DEGREES && this->displays_.size() == 1) {
    auto *mipi_display = static_cast<mipi_dsi::MipiDsi *>(this->displays_[0]);
#if LV_COLOR_DEPTH == 32
    constexpr size_t BYTES_PER_PIXEL = 3;
#else
    constexpr size_t BYTES_PER_PIXEL = LV_COLOR_DEPTH / 8;
#endif
    const size_t fb_bytes = (size_t) this->width_ * (size_t) this->height_ * BYTES_PER_PIXEL;
    if (mipi_display != nullptr && mipi_display->get_frame_buffer_size() >= fb_bytes) {
      if (this->direct_mode_active_) {
        // The DSI driver owns three framebuffers. Render into the one that is
        // neither scanned out nor queued for the next VSYNC so PPA work can
        // overlap the current frame instead of serializing on presentation.
        if (auto *target = mipi_display->get_direct_render_frame_buffer(exclude_a, exclude_b))
          return target;
        if (wait_ms != 0)
          return mipi_display->wait_for_direct_render_frame_buffer(exclude_a, exclude_b, wait_ms);
        return nullptr;
      }
      auto *fb0 = mipi_display->get_frame_buffer(0);
      auto *fb1 = mipi_display->get_frame_buffer(1);
      if (fb0 != nullptr && fb1 != nullptr) {
        if (this->snapshot_last_presented_buf_ == fb0)
          return fb1;
        if (this->snapshot_last_presented_buf_ == fb1)
          return fb0;
        // The DPI driver starts on fb0; fb1 is the least risky first manual target.
        return fb1;
      }
    }
  }
#endif
  return this->next_direct_render_buffer_();
}

bool LvglComponent::present_snapshot_render_buffer_(uint8_t *buffer, bool wait_for_active) {
  if (buffer == nullptr)
    return false;
  if (snapshot_draw_clock_rgb888(buffer, this->width_, this->height_))
    snapshot_sync_clock_rgb888(buffer, this->width_, this->height_);
#ifdef USE_MIPI_DSI
  if (this->rotation == display::DISPLAY_ROTATION_0_DEGREES && this->displays_.size() == 1) {
    auto *mipi_display = static_cast<mipi_dsi::MipiDsi *>(this->displays_[0]);
    const bool is_dsi_framebuffer =
        mipi_display != nullptr &&
        (buffer == mipi_display->get_frame_buffer(0) || buffer == mipi_display->get_frame_buffer(1) ||
         buffer == mipi_display->get_frame_buffer(2));
    if (this->direct_mode_active_ && is_dsi_framebuffer) {
      if (!mipi_display->queue_direct_frame_buffer(buffer, 50, wait_for_active))
        return false;
      this->direct_last_flushed_buf_ = buffer;
      this->snapshot_last_presented_buf_ = buffer;
      return true;
    }
    if (is_dsi_framebuffer &&
        (buffer == mipi_display->get_frame_buffer(0) || buffer == mipi_display->get_frame_buffer(1))) {
      if (!mipi_display->present_frame_buffer(buffer, 0, this->height_ - 1))
        return false;
      this->snapshot_last_presented_buf_ = buffer;
      return true;
    }
  }
#endif
  this->present_direct_render_buffer_(buffer);
  this->snapshot_last_presented_buf_ = buffer;
  return true;
}

bool LvglComponent::snapshot_present_current_frame() {
#if LV_COLOR_DEPTH == 32 && defined(USE_ESP32)
  if (this->width_ <= 0 || this->height_ <= 0 || this->direct_last_flushed_buf_ == nullptr)
    return false;
  constexpr size_t BYTES_PER_PIXEL = 3;
  const size_t fb_bytes = (size_t) this->width_ * (size_t) this->height_ * BYTES_PER_PIXEL;
  uint8_t *target = this->next_snapshot_render_buffer_(nullptr, nullptr, 20);
  if (target == nullptr)
    return false;
  if (target != this->direct_last_flushed_buf_) {
    memcpy(target, this->direct_last_flushed_buf_, fb_bytes);
    lvgl_cache_msync_external(target, fb_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  }
  if (!this->present_snapshot_render_buffer_(target))
    return false;
  this->wait_for_direct_frame_presented(20);
  return true;
#else
  return false;
#endif
}

bool LvglComponent::synchronize_direct_framebuffers() {
#if defined(USE_ESP32) && LV_COLOR_DEPTH == 32
  if (!this->direct_mode_active_ || this->direct_last_flushed_buf_ == nullptr || this->width_ <= 0 ||
      this->height_ <= 0) {
    return false;
  }

  lv_area_t full_area = {
      .x1 = 0,
      .y1 = 0,
      .x2 = static_cast<lv_coord_t>(this->width_ - 1),
      .y2 = static_cast<lv_coord_t>(this->height_ - 1),
  };
  this->wait_for_direct_frame_presented(20);
  return this->sync_direct_other_buffer_(&full_area, this->direct_last_flushed_buf_);
#else
  return false;
#endif
}

#ifdef USE_ESP32
bool LvglComponent::start_partial_compositor_() {
#ifdef USE_MIPI_DSI
  if (this->rotation != display::DISPLAY_ROTATION_0_DEGREES || this->displays_.size() != 1 ||
      this->draw_buf2_ == nullptr)
    return false;
  auto *mipi_display = static_cast<mipi_dsi::MipiDsi *>(this->displays_[0]);
  if (mipi_display == nullptr || mipi_display->get_frame_buffer(0) == nullptr ||
      mipi_display->get_frame_buffer(1) == nullptr) {
    return false;
  }
#ifdef USE_LVGL_PPA
  if (s_compositor_srm_alignment == 0) {
    if (esp_cache_get_alignment(MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA, &s_compositor_srm_alignment) != ESP_OK ||
        s_compositor_srm_alignment == 0) {
      s_compositor_srm_alignment = 64;
    }
  }
  if (s_compositor_srm_client == nullptr) {
    ppa_client_config_t srm_cfg = {};
    srm_cfg.oper_type = PPA_OPERATION_SRM;
    srm_cfg.max_pending_trans_num = 1;
    srm_cfg.data_burst_length = LVGL_ESPHOME_PPA_SRM_DATA_BURST_LENGTH;
    esp_err_t ret = ppa_register_client(&srm_cfg, &s_compositor_srm_client);
    if (ret == ESP_OK) {
      ESP_LOGI(TAG, "LVGL partial framebuffer compositor PPA SRM client registered (alignment=%u srm_burst=%d)",
               (unsigned) s_compositor_srm_alignment, (int) LVGL_ESPHOME_PPA_SRM_DATA_BURST_LENGTH);
    } else {
      s_compositor_srm_client = nullptr;
      ESP_LOGW(TAG, "LVGL partial framebuffer compositor PPA SRM client failed (%s); using CPU copy",
               esp_err_to_name(ret));
    }
  }
#endif
  this->partial_compositor_front_buffer_ = mipi_display->get_frame_buffer(0);
  this->partial_compositor_back_buffer_ = mipi_display->get_frame_buffer(1);
  this->partial_compositor_queue_ = xQueueCreate(4, sizeof(PartialCompositorJob));
  if (this->partial_compositor_queue_ == nullptr) {
    ESP_LOGW(TAG, "LVGL partial framebuffer compositor queue allocation failed");
    return false;
  }
#if CONFIG_FREERTOS_UNICORE
  constexpr BaseType_t compositor_core = tskNO_AFFINITY;
#else
  constexpr BaseType_t compositor_core = 0;
#endif
  TaskHandle_t task_handle = nullptr;
  const BaseType_t ok = xTaskCreatePinnedToCore(&LvglComponent::partial_compositor_task_trampoline_, "lvgl_fb_worker",
                                                6144, this, 8, &task_handle, compositor_core);
  if (ok != pdPASS) {
    ESP_LOGW(TAG, "LVGL partial framebuffer compositor task allocation failed");
    vQueueDelete(this->partial_compositor_queue_);
    this->partial_compositor_queue_ = nullptr;
    return false;
  }
  this->partial_compositor_task_handle_ = task_handle;
  this->partial_compositor_active_ = true;
  ESP_LOGI(TAG, "LVGL partial framebuffer compositor enabled on core %d", (int) compositor_core);
  return true;
#else
  return false;
#endif
}

bool LvglComponent::partial_compositor_flush_(lv_display_t *disp_drv, const lv_area_t *area, uint8_t *color_p,
                                              uint64_t t0) {
  if (!this->partial_compositor_active_ || this->partial_compositor_queue_ == nullptr)
    return false;
  PartialCompositorJob job{};
  job.disp = disp_drv;
  job.area = *area;
  job.color_p = color_p;
  job.last = lv_display_flush_is_last(disp_drv);
  job.t0 = t0;
#if LV_COLOR_DEPTH == 32
  constexpr size_t BYTES_PER_PIXEL = 3;
#else
  constexpr size_t BYTES_PER_PIXEL = LV_COLOR_DEPTH / 8;
#endif
  const size_t src_bytes = (size_t) lv_area_get_width(area) * (size_t) lv_area_get_height(area) * BYTES_PER_PIXEL;
  /* The worker task runs on another core. If LVGL rendered the partial buffer
   * in cached PSRAM, write it back before handing the pointer to the worker. */
  lvgl_cache_msync_external(color_p, src_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  __sync_synchronize();
  if (xQueueSend(this->partial_compositor_queue_, &job, pdMS_TO_TICKS(20)) != pdTRUE) {
    ESP_LOGW(TAG, "LVGL partial framebuffer compositor queue full; falling back to synchronous flush");
    return false;
  }

  const uint32_t schedule_us = (uint32_t) (esp_timer_get_time() - t0);
  if (schedule_us > this->perf_flush_max_us_)
    this->perf_flush_max_us_ = schedule_us;
  const uint32_t flush_px = (uint32_t) lv_area_get_width(area) * (uint32_t) lv_area_get_height(area);
  this->perf_flush_px_ += flush_px;
  this->perf_flush_us_ += schedule_us;
  profiler_note_flush(schedule_us, flush_px);
  return true;
}

void LvglComponent::partial_compositor_task_trampoline_(void *arg) {
  static_cast<LvglComponent *>(arg)->partial_compositor_task_();
}

void LvglComponent::partial_compositor_task_() {
  while (true) {
    PartialCompositorJob job{};
    if (xQueueReceive(this->partial_compositor_queue_, &job, portMAX_DELAY) != pdTRUE)
      continue;

    const uint64_t start_us = esp_timer_get_time();
    this->partial_compositor_copy_area_(this->partial_compositor_back_buffer_, job.area, job.color_p);
    this->partial_compositor_record_dirty_(job.area);

    bool frame_presented = false;
    bool sync_full_dirty = false;
    size_t sync_dirty_count = 0;
    lv_area_t sync_dirty_areas[PARTIAL_COMPOSITOR_MAX_DIRTY_AREAS]{};

    if (job.last) {
      int y_start = 0;
      int y_end = this->height_ - 1;
      if (!this->partial_compositor_full_dirty_ && this->partial_compositor_dirty_count_ > 0) {
        y_start = this->height_ - 1;
        y_end = 0;
        for (size_t i = 0; i < this->partial_compositor_dirty_count_; i++) {
          y_start = std::min<int>(y_start, this->partial_compositor_dirty_areas_[i].y1);
          y_end = std::max<int>(y_end, this->partial_compositor_dirty_areas_[i].y2);
        }
      }
      sync_full_dirty = this->partial_compositor_full_dirty_;
      sync_dirty_count = this->partial_compositor_dirty_count_;
      for (size_t i = 0; i < sync_dirty_count; i++)
        sync_dirty_areas[i] = this->partial_compositor_dirty_areas_[i];

#ifdef USE_MIPI_DSI
      auto *mipi_display = static_cast<mipi_dsi::MipiDsi *>(this->displays_[0]);
      // present_frame_buffer() hands an exposed full framebuffer back to the
      // DPI driver. Passing a non-zero y_start with a pointer to the beginning
      // of the full framebuffer can make the driver interpret the wrong row as
      // the top of the update area, which shows up as intermittent line noise.
      mipi_display->present_frame_buffer(this->partial_compositor_back_buffer_, 0, this->height_ - 1);
#endif
      std::swap(this->partial_compositor_front_buffer_, this->partial_compositor_back_buffer_);
      this->partial_compositor_dirty_count_ = 0;
      this->partial_compositor_full_dirty_ = false;
      frame_presented = true;
    }

    if (frame_presented) {
      if (sync_full_dirty) {
        lv_area_t full{};
        full.x1 = 0;
        full.y1 = 0;
        full.x2 = this->width_ - 1;
        full.y2 = this->height_ - 1;
        this->partial_compositor_copy_area_(this->partial_compositor_back_buffer_, full,
                                            this->partial_compositor_front_buffer_, true);
      } else {
        for (size_t i = 0; i < sync_dirty_count; i++) {
          this->partial_compositor_copy_area_(this->partial_compositor_back_buffer_, sync_dirty_areas[i],
                                              this->partial_compositor_front_buffer_, true);
        }
      }
    }

    const uint32_t ready_dt_us = (uint32_t) (esp_timer_get_time() - start_us);
    this->perf_compositor_ready_us_ += ready_dt_us;
    this->perf_compositor_jobs_++;
    if (ready_dt_us > this->perf_compositor_ready_max_us_)
      this->perf_compositor_ready_max_us_ = ready_dt_us;
    const uint32_t px = (uint32_t) lv_area_get_width(&job.area) * (uint32_t) lv_area_get_height(&job.area);
    this->perf_compositor_px_ += px;
    lv_display_flush_ready(job.disp);

    const uint32_t dt_us = (uint32_t) (esp_timer_get_time() - start_us);
    this->perf_compositor_us_ += dt_us;
    if (dt_us > this->perf_compositor_max_us_)
      this->perf_compositor_max_us_ = dt_us;
  }
}

void LvglComponent::partial_compositor_copy_area_(uint8_t *dst, const lv_area_t &area, const uint8_t *src,
                                                  bool src_is_framebuffer) {
  if (dst == nullptr || src == nullptr)
    return;
#if LV_COLOR_DEPTH == 32
  constexpr size_t BYTES_PER_PIXEL = 3;
#else
  constexpr size_t BYTES_PER_PIXEL = LV_COLOR_DEPTH / 8;
#endif
  const int32_t x1 = std::max<int32_t>(0, area.x1);
  const int32_t y1 = std::max<int32_t>(0, area.y1);
  const int32_t x2 = std::min<int32_t>(this->width_ - 1, area.x2);
  const int32_t y2 = std::min<int32_t>(this->height_ - 1, area.y2);
  if (x2 < x1 || y2 < y1)
    return;

  const size_t row_bytes = (size_t) this->width_ * BYTES_PER_PIXEL;
  const size_t framebuffer_bytes = row_bytes * (size_t) this->height_;
  const size_t copy_bytes = (size_t) (x2 - x1 + 1) * BYTES_PER_PIXEL;
  const size_t src_stride = src_is_framebuffer ? row_bytes : ((size_t) lv_area_get_width(&area) * BYTES_PER_PIXEL);

#ifdef USE_LVGL_PPA
  const int32_t copy_w = x2 - x1 + 1;
  const int32_t copy_h = y2 - y1 + 1;
  const int32_t src_pic_w = src_is_framebuffer ? this->width_ : lv_area_get_width(&area);
  const int32_t src_pic_h = src_is_framebuffer ? this->height_ : lv_area_get_height(&area);
  const int32_t src_off_x = src_is_framebuffer ? x1 : (x1 - area.x1);
  const int32_t src_off_y = src_is_framebuffer ? y1 : (y1 - area.y1);
  const bool src_geometry_ok = src_pic_w > 0 && src_pic_h > 0 && src_off_x >= 0 && src_off_y >= 0 &&
                               (src_off_x + copy_w) <= src_pic_w && (src_off_y + copy_h) <= src_pic_h;
  /* The compositor mostly copies short RGB888 spans from LVGL's partial draw
   * buffer into a full DSI framebuffer. PPA SRM is excellent for full-frame
   * snapshot motion, but the RGB888 partial-copy path has shown cache/stride
   * line artifacts on ESP32-P4 panels. Keep this hot correctness path on a
   * deterministic CPU row copy and leave PPA SRM for snapshot compositing. */
#if LV_COLOR_DEPTH != 32
  const size_t align = s_compositor_srm_alignment == 0 ? 64 : s_compositor_srm_alignment;
  const bool out_aligned = (align != 0) && ((reinterpret_cast<uintptr_t>(dst) & (align - 1)) == 0) &&
                           ((framebuffer_bytes & (align - 1)) == 0);
  const bool ppa_safe_memory = esp_ptr_external_ram(src) && esp_ptr_external_ram(dst);
  uint8_t *dst_sync_start = dst + (size_t) y1 * row_bytes;
  const size_t dst_sync_size = (size_t) (y2 - y1 + 1) * row_bytes;
  const uint8_t *src_sync_start =
      src_is_framebuffer ? src + (size_t) y1 * row_bytes : src + (size_t) (y1 - area.y1) * src_stride;
  const size_t src_sync_size = src_is_framebuffer ? dst_sync_size : (size_t) (y2 - y1 + 1) * src_stride;
  if (s_compositor_srm_client != nullptr && src_geometry_ok && out_aligned && ppa_safe_memory) {
#if LV_COLOR_DEPTH == 32
    constexpr ppa_srm_color_mode_t PPA_CM = PPA_SRM_COLOR_MODE_RGB888;
#else
    constexpr ppa_srm_color_mode_t PPA_CM = PPA_SRM_COLOR_MODE_RGB565;
#endif
    ppa_srm_oper_config_t cfg = {};
    cfg.in.buffer = const_cast<uint8_t *>(src);
    cfg.in.pic_w = src_pic_w;
    cfg.in.pic_h = src_pic_h;
    cfg.in.block_w = copy_w;
    cfg.in.block_h = copy_h;
    cfg.in.block_offset_x = src_off_x;
    cfg.in.block_offset_y = src_off_y;
    cfg.in.srm_cm = PPA_CM;
    cfg.out.buffer = dst;
    cfg.out.buffer_size = framebuffer_bytes;
    cfg.out.pic_w = this->width_;
    cfg.out.pic_h = this->height_;
    cfg.out.block_offset_x = x1;
    cfg.out.block_offset_y = y1;
    cfg.out.srm_cm = PPA_CM;
    cfg.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
    cfg.scale_x = 1.0f;
    cfg.scale_y = 1.0f;
    cfg.alpha_update_mode = PPA_ALPHA_NO_CHANGE;
    cfg.mode = PPA_TRANS_MODE_BLOCKING;

    /* Source and destination are PSRAM buffers shared by the CPU, PPA and DSI.
     * Write back before PPA so dirty CPU cache lines cannot overwrite adjacent
     * bytes later; invalidate after PPA so the CPU never sees stale output. */
    lvgl_cache_msync_external(src_sync_start, src_sync_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    lvgl_cache_msync_external(dst_sync_start, dst_sync_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    esp_err_t ret = ppa_do_scale_rotate_mirror(s_compositor_srm_client, &cfg);
    if (ret == ESP_OK) {
      lvgl_cache_msync_external(dst_sync_start, dst_sync_size, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
      return;
    }

    static uint32_t ppa_warn_count = 0;
    if (ppa_warn_count < 8) {
      ESP_LOGW(TAG,
               "LVGL partial compositor PPA copy failed (%s): area=%d,%d-%d,%d src_fb=%d src_pic=%dx%d src_off=%d,%d "
               "align=%u dst=%p fb=%u",
               esp_err_to_name(ret), (int) x1, (int) y1, (int) x2, (int) y2, src_is_framebuffer ? 1 : 0,
               (int) src_pic_w, (int) src_pic_h, (int) src_off_x, (int) src_off_y, (unsigned) align, dst,
               (unsigned) framebuffer_bytes);
      ppa_warn_count++;
    }
  }
#endif
#endif

  const uint8_t *src_row =
      src_is_framebuffer ? src + ((size_t) y1 * row_bytes) + ((size_t) x1 * BYTES_PER_PIXEL)
                         : src + ((size_t) (y1 - area.y1) * src_stride) + ((size_t) (x1 - area.x1) * BYTES_PER_PIXEL);
  uint8_t *dst_row = dst + ((size_t) y1 * row_bytes) + ((size_t) x1 * BYTES_PER_PIXEL);
  for (int32_t y = y1; y <= y2; y++) {
    /* The framebuffer lives in cached PSRAM. A partial CPU write can share
     * cache lines with pixels outside the copied rectangle. Pull only the
     * destination span from memory before modifying it, then write back only
     * that span; the source buffer is owned by LVGL/the presented framebuffer
     * and invalidating it here can race unrelated readers. */
    lvgl_cache_msync_external(dst_row, copy_bytes, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    memcpy(dst_row, src_row, copy_bytes);
    lvgl_cache_msync_external(dst_row, copy_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    dst_row += row_bytes;
    src_row += src_stride;
  }
}

void LvglComponent::partial_compositor_record_dirty_(const lv_area_t &area) {
  if (this->partial_compositor_full_dirty_)
    return;
  if (this->partial_compositor_dirty_count_ >= PARTIAL_COMPOSITOR_MAX_DIRTY_AREAS) {
    this->partial_compositor_full_dirty_ = true;
    return;
  }
  lv_area_t clipped{};
  clipped.x1 = std::max<int32_t>(0, area.x1);
  clipped.y1 = std::max<int32_t>(0, area.y1);
  clipped.x2 = std::min<int32_t>(this->width_ - 1, area.x2);
  clipped.y2 = std::min<int32_t>(this->height_ - 1, area.y2);
  if (clipped.x2 < clipped.x1 || clipped.y2 < clipped.y1)
    return;
  this->partial_compositor_dirty_areas_[this->partial_compositor_dirty_count_++] = clipped;
}

void LvglComponent::partial_compositor_sync_dirty_to_idle_() {
  if (this->partial_compositor_front_buffer_ == nullptr || this->partial_compositor_back_buffer_ == nullptr)
    return;
  if (this->partial_compositor_full_dirty_) {
    lv_area_t full{};
    full.x1 = 0;
    full.y1 = 0;
    full.x2 = this->width_ - 1;
    full.y2 = this->height_ - 1;
    this->partial_compositor_copy_area_(this->partial_compositor_back_buffer_, full,
                                        this->partial_compositor_front_buffer_, true);
    return;
  }
  for (size_t i = 0; i < this->partial_compositor_dirty_count_; i++) {
    this->partial_compositor_copy_area_(this->partial_compositor_back_buffer_, this->partial_compositor_dirty_areas_[i],
                                        this->partial_compositor_front_buffer_, true);
  }
}
#endif  // USE_ESP32

bool LvglComponent::wait_for_direct_frame_presented(uint32_t timeout_ms) {
#ifdef USE_MIPI_DSI
  if (this->displays_.empty())
    return false;
  auto *mipi_display = static_cast<mipi_dsi::MipiDsi *>(this->displays_[0]);
  if (mipi_display == nullptr || !mipi_display->wait_for_direct_frame_queue_idle(timeout_ms))
    return false;
  return mipi_display->wait_for_refresh_done(timeout_ms);
#else
  return false;
#endif
}

bool LvglComponent::direct_capture_rgb888(uint8_t *dst, int dst_stride, int x, int y, int width, int height) {
#if LV_COLOR_DEPTH == 32 && defined(USE_ESP32) && defined(USE_MIPI_DSI)
  if (!this->direct_mode_active_ || this->rotation != display::DISPLAY_ROTATION_0_DEGREES ||
      this->displays_.size() != 1 || dst == nullptr || s_snapshot_direct_active || s_snapshot_swipe_active ||
      width <= 0 || height <= 0 || dst_stride < width * 3 || x < 0 || y < 0 || x + width > this->width_ ||
      y + height > this->height_) {
    return false;
  }

  auto *mipi_display = static_cast<mipi_dsi::MipiDsi *>(this->displays_[0]);
  if (mipi_display == nullptr)
    return false;
  uint8_t *fb0 = mipi_display->get_frame_buffer(0);
  uint8_t *fb1 = mipi_display->get_frame_buffer(1);
  uint8_t *fb2 = mipi_display->get_frame_buffer(2);
  if (fb0 == nullptr || fb1 == nullptr || fb2 == nullptr)
    return false;

  const uint8_t *source = mipi_display->get_presented_frame_buffer();
  if (source != fb0 && source != fb1 && source != fb2)
    source = this->direct_last_flushed_buf_;
  if (source != fb0 && source != fb1 && source != fb2)
    return false;

  constexpr size_t bytes_per_pixel = 3;
  const size_t framebuffer_stride = (size_t) this->width_ * bytes_per_pixel;
  const size_t row_bytes = (size_t) width * bytes_per_pixel;
  const uint8_t *source_row = source + (size_t) y * framebuffer_stride + (size_t) x * bytes_per_pixel;
  uint8_t *target_row = dst;
  constexpr int ROWS_PER_BAND = 4;
  for (int row = 0; row < height;) {
    lvgl_esphome_wait_snapshot_dsi_fifo();
    const int rows = std::min(ROWS_PER_BAND, height - row);
    const size_t source_span = (size_t) (rows - 1) * framebuffer_stride + row_bytes;
    uint8_t *target_band = target_row;
    lvgl_cache_msync_external(source_row, source_span, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    for (int band_row = 0; band_row < rows; band_row++) {
      std::memcpy(target_row, source_row, row_bytes);
      source_row += framebuffer_stride;
      target_row += dst_stride;
    }
    lvgl_cache_msync_external(target_band, (size_t) dst_stride * (size_t) rows, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    row += rows;
    if (row < height) {
      taskYIELD();
    }
  }
  return true;
#else
  return false;
#endif
}

extern "C" bool lvgl_esphome_direct_capture_rgb888(uint8_t *dst, int dst_stride, int x, int y, int width, int height) {
  auto *disp = lv_display_get_default();
  auto *component = disp == nullptr ? nullptr : static_cast<LvglComponent *>(lv_display_get_user_data(disp));
  return component != nullptr && component->direct_capture_rgb888(dst, dst_stride, x, y, width, height);
}

extern "C" bool lvgl_esphome_compose_argb8888_over_rgb888(
    const uint8_t *background, int background_stride, const uint8_t *foreground, int foreground_stride,
    int foreground_width, int foreground_height, int foreground_x, int foreground_y, uint8_t *output,
    int output_stride, int width, int height) {
#if LV_COLOR_DEPTH == 32 && defined(USE_ESP32) && defined(USE_LVGL_PPA)
  if (background == nullptr || foreground == nullptr || output == nullptr || width <= 0 || height <= 0 ||
      foreground_width < width || foreground_height < height || foreground_x < 0 || foreground_y < 0 ||
      foreground_x + width > foreground_width || foreground_y + height > foreground_height ||
      background_stride != width * 3 || output_stride != width * 3 || foreground_stride != foreground_width * 4 ||
      s_region_blend_client == nullptr || s_region_srm_mutex == nullptr) {
    return false;
  }
  if (xSemaphoreTake(s_region_srm_mutex, pdMS_TO_TICKS(20)) != pdTRUE)
    return false;
  struct RegionBlendUnlock {
    ~RegionBlendUnlock() { xSemaphoreGive(s_region_srm_mutex); }
  } region_blend_unlock;

  const size_t background_bytes = static_cast<size_t>(background_stride) * height;
  const size_t foreground_span = static_cast<size_t>(height - 1) * foreground_stride +
                                 static_cast<size_t>(width) * 4U;
  const size_t output_bytes = static_cast<size_t>(output_stride) * height;
  const uint8_t *foreground_region = foreground + static_cast<size_t>(foreground_y) * foreground_stride +
                                     static_cast<size_t>(foreground_x) * 4U;
  lvgl_cache_msync_external(background, background_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  lvgl_cache_msync_external(foreground_region, foreground_span, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  lvgl_cache_msync_external(output, output_bytes, ESP_CACHE_MSYNC_FLAG_DIR_M2C);

  ppa_blend_oper_config_t config{};
  config.in_bg.buffer = background;
  config.in_bg.pic_w = width;
  config.in_bg.pic_h = height;
  config.in_bg.block_w = width;
  config.in_bg.block_h = height;
  config.in_bg.blend_cm = PPA_BLEND_COLOR_MODE_RGB888;
  config.in_fg.buffer = foreground;
  config.in_fg.pic_w = foreground_width;
  config.in_fg.pic_h = foreground_height;
  config.in_fg.block_w = width;
  config.in_fg.block_h = height;
  config.in_fg.block_offset_x = foreground_x;
  config.in_fg.block_offset_y = foreground_y;
  config.in_fg.blend_cm = PPA_BLEND_COLOR_MODE_ARGB8888;
  config.out.buffer = output;
  config.out.buffer_size = output_bytes;
  config.out.pic_w = width;
  config.out.pic_h = height;
  config.out.blend_cm = PPA_BLEND_COLOR_MODE_RGB888;
  config.bg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
  config.bg_alpha_fix_val = 0xFF;
  config.fg_alpha_update_mode = PPA_ALPHA_NO_CHANGE;
  config.mode = PPA_TRANS_MODE_BLOCKING;

  const esp_err_t result = ppa_do_blend(s_region_blend_client, &config);
  if (result != ESP_OK)
    return false;
  lvgl_cache_msync_external(output, output_bytes, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
  return true;
#else
  (void) background;
  (void) background_stride;
  (void) foreground;
  (void) foreground_stride;
  (void) foreground_width;
  (void) foreground_height;
  (void) foreground_x;
  (void) foreground_y;
  (void) output;
  (void) output_stride;
  (void) width;
  (void) height;
  return false;
#endif
}

extern "C" void lvgl_esphome_dsi_mark_stress(const char *label, uint32_t duration_ms) {
#if defined(USE_MIPI_DSI)
  if (esphome_mipi_dsi_mark_stress != nullptr) {
    esphome_mipi_dsi_mark_stress(label, duration_ms);
  }
#else
  (void) label;
  (void) duration_ms;
#endif
}

bool LvglComponent::direct_blit_rgb888(const uint8_t *src, int src_stride, int x, int y, int width, int height) {
  return this->direct_blit_(src, src_stride, x, y, width, height, false, false);
}

#if LV_COLOR_DEPTH == 32 && defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA)
LvglComponent::DirectRegionSlot *LvglComponent::direct_region_find_slot_(int x, int y, int width, int height,
                                                                          bool create) {
  DirectRegionSlot *free_slot = nullptr;
  for (auto &slot : this->direct_region_slots_) {
    if (slot.valid && slot.x == x && slot.y == y && slot.width == width && slot.height == height)
      return &slot;
    if (!slot.valid && free_slot == nullptr)
      free_slot = &slot;
  }
  return create ? free_slot : nullptr;
}

bool LvglComponent::direct_region_ppa_copy_(const uint8_t *source, int source_width, int source_height,
                                            int source_x, int source_y, int copy_width, int copy_height,
                                            uint8_t *target, size_t target_size, int target_width,
                                            int target_height, int target_x, int target_y, bool sync_source,
                                            bool invalidate_target) {
  if (source == nullptr || target == nullptr || source_width <= 0 || source_height <= 0 || copy_width <= 0 ||
      copy_height <= 0 || source_x < 0 || source_y < 0 || source_x + copy_width > source_width ||
      source_y + copy_height > source_height || target_width <= 0 || target_height <= 0 || target_x < 0 ||
      target_y < 0 || target_x + copy_width > target_width || target_y + copy_height > target_height ||
      s_region_srm_client == nullptr)
    return false;

  constexpr size_t BYTES_PER_PIXEL = 3;
  const size_t source_size = static_cast<size_t>(source_width) * source_height * BYTES_PER_PIXEL;
  if (target_size < static_cast<size_t>(target_width) * target_height * BYTES_PER_PIXEL)
    return false;
  if (sync_source &&
      lvgl_cache_msync_external_result(source, source_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M) != ESP_OK)
    return false;
  if (invalidate_target &&
      lvgl_cache_msync_external_result(target, target_size, ESP_CACHE_MSYNC_FLAG_DIR_M2C) != ESP_OK)
    return false;

  ppa_srm_oper_config_t cfg = {};
  cfg.in.buffer = const_cast<uint8_t *>(source);
  cfg.in.pic_w = source_width;
  cfg.in.pic_h = source_height;
  cfg.in.block_w = copy_width;
  cfg.in.block_h = copy_height;
  cfg.in.block_offset_x = source_x;
  cfg.in.block_offset_y = source_y;
  cfg.in.srm_cm = PPA_SRM_COLOR_MODE_RGB888;
  cfg.out.buffer = target;
  cfg.out.buffer_size = target_size;
  cfg.out.pic_w = target_width;
  cfg.out.pic_h = target_height;
  cfg.out.block_offset_x = target_x;
  cfg.out.block_offset_y = target_y;
  cfg.out.srm_cm = PPA_SRM_COLOR_MODE_RGB888;
  cfg.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
  cfg.scale_x = 1.0f;
  cfg.scale_y = 1.0f;
  cfg.alpha_update_mode = PPA_ALPHA_NO_CHANGE;
  cfg.mode = PPA_TRANS_MODE_BLOCKING;

  // Cache ownership was handled explicitly above. The patched PPA wrapper
  // must not repeat a full-stride sync for a small region transaction.
  const uintptr_t source_begin = reinterpret_cast<uintptr_t>(source);
  const uintptr_t target_begin = reinterpret_cast<uintptr_t>(target);
  s_direct_ppa_source_begin.store(source_begin, std::memory_order_release);
  s_direct_ppa_source_end.store(source_begin + source_size, std::memory_order_release);
  s_direct_ppa_target_begin.store(target_begin, std::memory_order_release);
  s_direct_ppa_target_end.store(target_begin + target_size, std::memory_order_release);
  lvgl_esphome_wait_direct_region_dsi_fifo();
  const esp_err_t result = ppa_do_scale_rotate_mirror(s_region_srm_client, &cfg);
  s_direct_ppa_source_begin.store(0, std::memory_order_release);
  s_direct_ppa_target_begin.store(0, std::memory_order_release);
  s_direct_ppa_source_end.store(0, std::memory_order_relaxed);
  s_direct_ppa_target_end.store(0, std::memory_order_relaxed);
  return result == ESP_OK;
}

bool LvglComponent::direct_region_ppa_blend_argb8888_(
    const uint8_t *background, int background_stride, const uint8_t *foreground, int foreground_stride,
    int foreground_width, int foreground_height, int foreground_x, int foreground_y, int blend_width,
    int blend_height, uint8_t *target, size_t target_size, int target_width, int target_height, int target_x,
    int target_y) {
  if (background == nullptr || foreground == nullptr || target == nullptr || blend_width <= 0 ||
      blend_height <= 0 || background_stride != blend_width * 3 || foreground_width <= 0 ||
      foreground_height <= 0 || foreground_stride != foreground_width * 4 || foreground_x < 0 ||
      foreground_y < 0 || foreground_x + blend_width > foreground_width ||
      foreground_y + blend_height > foreground_height || target_width <= 0 || target_height <= 0 ||
      target_x < 0 || target_y < 0 || target_x + blend_width > target_width ||
      target_y + blend_height > target_height || s_region_blend_client == nullptr)
    return false;

  const size_t background_size = static_cast<size_t>(background_stride) * blend_height;
  const size_t foreground_size = static_cast<size_t>(foreground_stride) * foreground_height;
  const size_t required_target_size = static_cast<size_t>(target_width) * target_height * 3U;
  if (target_size < required_target_size ||
      lvgl_cache_msync_external_result(background, background_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M) != ESP_OK ||
      lvgl_cache_msync_external_result(foreground, foreground_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M) != ESP_OK)
    return false;

  ppa_blend_oper_config_t cfg{};
  cfg.in_bg.buffer = background;
  cfg.in_bg.pic_w = blend_width;
  cfg.in_bg.pic_h = blend_height;
  cfg.in_bg.block_w = blend_width;
  cfg.in_bg.block_h = blend_height;
  cfg.in_bg.blend_cm = PPA_BLEND_COLOR_MODE_RGB888;
  cfg.in_fg.buffer = foreground;
  cfg.in_fg.pic_w = foreground_width;
  cfg.in_fg.pic_h = foreground_height;
  cfg.in_fg.block_w = blend_width;
  cfg.in_fg.block_h = blend_height;
  cfg.in_fg.block_offset_x = foreground_x;
  cfg.in_fg.block_offset_y = foreground_y;
  cfg.in_fg.blend_cm = PPA_BLEND_COLOR_MODE_ARGB8888;
  cfg.out.buffer = target;
  cfg.out.buffer_size = target_size;
  cfg.out.pic_w = target_width;
  cfg.out.pic_h = target_height;
  cfg.out.block_offset_x = target_x;
  cfg.out.block_offset_y = target_y;
  cfg.out.blend_cm = PPA_BLEND_COLOR_MODE_RGB888;
  cfg.bg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
  cfg.bg_alpha_fix_val = 0xFF;
  cfg.fg_alpha_update_mode = PPA_ALPHA_NO_CHANGE;
  cfg.mode = PPA_TRANS_MODE_BLOCKING;

  // Both immutable inputs have already been written back. Keep the output
  // cache maintenance in the PPA driver because this is a partial write into
  // a DSI framebuffer and pixels outside the block must be preserved.
  const uintptr_t background_begin = reinterpret_cast<uintptr_t>(background);
  const uintptr_t foreground_begin = reinterpret_cast<uintptr_t>(foreground);
  const uintptr_t target_begin = reinterpret_cast<uintptr_t>(target);
  s_direct_ppa_source_begin.store(background_begin, std::memory_order_release);
  s_direct_ppa_source_end.store(background_begin + background_size, std::memory_order_release);
  s_direct_ppa_source2_begin.store(foreground_begin, std::memory_order_release);
  s_direct_ppa_source2_end.store(foreground_begin + foreground_size, std::memory_order_release);
  s_direct_ppa_target_begin.store(target_begin, std::memory_order_release);
  s_direct_ppa_target_end.store(target_begin + target_size, std::memory_order_release);
  lvgl_esphome_wait_direct_region_dsi_fifo();
  const esp_err_t result = ppa_do_blend(s_region_blend_client, &cfg);
  s_direct_ppa_source_begin.store(0, std::memory_order_release);
  s_direct_ppa_source2_begin.store(0, std::memory_order_release);
  s_direct_ppa_target_begin.store(0, std::memory_order_release);
  s_direct_ppa_source_end.store(0, std::memory_order_relaxed);
  s_direct_ppa_source2_end.store(0, std::memory_order_relaxed);
  s_direct_ppa_target_end.store(0, std::memory_order_relaxed);
  return result == ESP_OK;
}

bool LvglComponent::start_direct_region_compositor_() {
  if (this->direct_region_queue_ != nullptr && this->direct_region_task_handle_ != nullptr)
    return true;
  this->direct_region_queue_ = xQueueCreate(DIRECT_REGION_BATCH_SIZE, sizeof(DirectRegionRequest));
  if (this->direct_region_queue_ == nullptr)
    return false;
#if CONFIG_FREERTOS_UNICORE
  constexpr BaseType_t task_core = tskNO_AFFINITY;
#else
  const BaseType_t task_core = xPortGetCoreID() == 0 ? 1 : 0;
#endif
  constexpr uint32_t task_stack_size = 6144;
  this->direct_region_task_stack_ = static_cast<StackType_t *>(
      heap_caps_aligned_alloc(16, task_stack_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (this->direct_region_task_stack_ == nullptr) {
    this->direct_region_task_stack_ = static_cast<StackType_t *>(
        heap_caps_aligned_alloc(16, task_stack_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (this->direct_region_task_stack_ != nullptr) {
    this->direct_region_task_handle_ = xTaskCreateStaticPinnedToCore(
        direct_region_task_trampoline_, "lvgl_region", task_stack_size, this, 1, this->direct_region_task_stack_,
        &this->direct_region_task_storage_, task_core);
  }
  if (this->direct_region_task_handle_ == nullptr) {
    if (this->direct_region_task_stack_ != nullptr)
      heap_caps_free(this->direct_region_task_stack_);
    this->direct_region_task_stack_ = nullptr;
    vQueueDelete(this->direct_region_queue_);
    this->direct_region_queue_ = nullptr;
    this->direct_region_task_handle_ = nullptr;
    return false;
  }
  ESP_LOGI(TAG, "Direct region compositor started on core %d (%s stack)", static_cast<int>(task_core),
           esp_ptr_external_ram(this->direct_region_task_stack_) ? "PSRAM" : "internal");
  return true;
}

void LvglComponent::direct_region_task_trampoline_(void *arg) {
  static_cast<LvglComponent *>(arg)->direct_region_task_();
}

bool LvglComponent::direct_region_prepare_target_(uint32_t generation, uint8_t **active_out,
                                                   uint8_t **target_out) {
  if (active_out == nullptr || target_out == nullptr || this->displays_.size() != 1)
    return false;
  auto *mipi_display = static_cast<mipi_dsi::MipiDsi *>(this->displays_[0]);
  if (mipi_display == nullptr)
    return false;

  uint8_t *active = mipi_display->get_presented_frame_buffer();
  if (active == nullptr)
    return false;

  auto buffer_index = [mipi_display](const uint8_t *buffer) -> int {
    for (size_t i = 0; i < 3; i++) {
      if (mipi_display->get_frame_buffer(i) == buffer)
        return static_cast<int>(i);
    }
    return -1;
  };

  // The queued framebuffer contains the newest complete overlay state. Use it
  // as the source when it belongs to the current LVGL base generation; the
  // active framebuffer remains the authoritative source immediately after a
  // normal LVGL refresh.
  uint8_t *source = active;
  if (auto *queued = mipi_display->get_queued_frame_buffer(); queued != nullptr) {
    const int queued_index = buffer_index(queued);
    if (queued_index >= 0 && this->direct_region_buffer_generation_[queued_index] == generation)
      source = queued;
  }

  // DSI owns the active and queued framebuffers. With three panel buffers the
  // compositor can prepare the remaining one without waiting for VSYNC.
  uint8_t *target = mipi_display->get_direct_render_frame_buffer();
  const int target_index = buffer_index(target);
  if (target == nullptr || target_index < 0 || target == source)
    return false;

  if (this->direct_region_buffer_generation_[target_index] != generation) {
    const size_t frame_size = mipi_display->get_frame_buffer_size();
    if (!this->direct_region_ppa_copy_(source, this->width_, this->height_, 0, 0, this->width_, this->height_,
                                       target, frame_size, this->width_, this->height_, 0, 0, false, true))
      return false;
    this->direct_region_buffer_generation_[target_index] = generation;
  }

  *active_out = source;
  *target_out = target;
  return true;
}

bool LvglComponent::direct_region_compose_to_(const DirectRegionRequest *requests, size_t request_count,
                                               uint8_t *active, uint8_t *target) {
  if (requests == nullptr || request_count == 0 || active == nullptr || target == nullptr ||
      this->displays_.size() != 1)
    return false;
  auto *mipi_display = static_cast<mipi_dsi::MipiDsi *>(this->displays_[0]);
  const size_t frame_size = mipi_display != nullptr ? mipi_display->get_frame_buffer_size() : 0;
  if (frame_size == 0)
    return false;

  // Carry live overlays that are not replaced by this batch, then draw every
  // requested region into the same target. Waveform, marquee, and other
  // overlays therefore share one DSI frame swap instead of serializing full
  // presentations for each region.
  for (const auto &slot : this->direct_region_slots_) {
    if (!slot.valid)
      continue;
    bool replaced = false;
    for (size_t i = 0; i < request_count; i++) {
      const auto &request = requests[i];
      if (request.operation == DirectRegionRequest::Operation::BARRIER)
        continue;
      if (slot.x == request.x && slot.y == request.y && slot.width == request.width &&
          slot.height == request.height) {
        replaced = true;
        break;
      }
    }
    if (replaced)
      continue;
    if (!this->direct_region_ppa_copy_(active, this->width_, this->height_, slot.x, slot.y, slot.width,
                                       slot.height, target, frame_size, this->width_, this->height_, slot.x, slot.y,
                                       false, false))
      return false;
  }
  for (size_t i = 0; i < request_count; i++) {
    const auto &request = requests[i];
    if (request.operation == DirectRegionRequest::Operation::BARRIER)
      continue;
    const bool composed = request.operation == DirectRegionRequest::Operation::BLEND_ARGB8888
                              ? this->direct_region_ppa_blend_argb8888_(
                                    request.background, request.background_stride, request.source,
                                    request.source_stride, request.source_width, request.source_height,
                                    request.source_x, request.source_y, request.width, request.height, target,
                                    frame_size, this->width_, this->height_, request.x, request.y)
                              : this->direct_region_ppa_copy_(
                                    request.source, request.source_width, request.source_height, request.source_x,
                                    request.source_y, request.width, request.height, target, frame_size,
                                    this->width_, this->height_, request.x, request.y, true, false);
    if (!composed)
      return false;
  }
  return true;
}

void LvglComponent::direct_region_task_() {
  // Pace all independent regions to the display refresh. Requests for the
  // same region are coalesced below, so a producer running ahead of VSYNC
  // cannot consume PPA bandwidth by drawing states that are never visible.
  constexpr int64_t frame_period_us = 16667;
  DirectRegionRequest requests[DIRECT_REGION_BATCH_SIZE]{};
  uint32_t perf_count = 0;
  uint32_t perf_request_count = 0;
  uint32_t perf_coalesced_count = 0;
  uint32_t perf_max_batch_count = 0;
  uint64_t perf_total_us = 0;
  uint32_t perf_max_us = 0;
  int64_t perf_window_start_us = 0;
  int64_t next_present_us = 0;
  DirectRegionRequest incoming{};
  while (xQueueReceive(this->direct_region_queue_, &incoming, portMAX_DELAY) == pdTRUE) {
    struct DeferredCompletion {
      LvglDirectBlitReadyCallback callback{};
      void *arg{};
    };
    DeferredCompletion deferred_completions[DIRECT_REGION_BATCH_SIZE * 2]{};
    size_t deferred_completion_count = 0;
    size_t request_count = 0;
    uint32_t raw_request_count = 0;
    uint32_t coalesced_request_count = 0;
    auto defer_completion = [&](LvglDirectBlitReadyCallback callback, void *arg) {
      if (callback == nullptr)
        return;
      if (deferred_completion_count < std::size(deferred_completions)) {
        deferred_completions[deferred_completion_count++] = {callback, arg};
      } else {
        // The queue can hold fewer entries than this array. Keep the fallback
        // for defensive handling if that relationship changes later.
        callback(arg);
      }
    };
    auto add_latest_request = [&](const DirectRegionRequest &request) {
      raw_request_count++;
      if (request.operation == DirectRegionRequest::Operation::BARRIER) {
        if (request_count < DIRECT_REGION_BATCH_SIZE) {
          requests[request_count++] = request;
        } else {
          defer_completion(request.ready_callback, request.ready_arg);
        }
        return;
      }
      for (size_t i = 0; i < request_count; i++) {
        auto &queued = requests[i];
        if (queued.operation == DirectRegionRequest::Operation::BARRIER)
          continue;
        if (queued.x != request.x || queued.y != request.y || queued.width != request.width ||
            queued.height != request.height)
          continue;
        // Do not acknowledge a superseded buffer while this batch is still
        // being collected. The callback wakes its producer, which can enqueue
        // another state immediately and create a self-feeding request storm.
        // The old source stays valid until composition completes, so release
        // it together with the sources that were actually presented.
        defer_completion(queued.ready_callback, queued.ready_arg);
        coalesced_request_count++;
        queued = request;
        return;
      }
      if (request_count < DIRECT_REGION_BATCH_SIZE) {
        requests[request_count++] = request;
      } else {
        defer_completion(request.ready_callback, request.ready_arg);
      }
    };
    add_latest_request(incoming);
    // Frame-pace independent animated regions so their newest states share one
    // DSI presentation instead of competing for PSRAM bandwidth.
    if (next_present_us != 0) {
      while (true) {
        const int64_t remaining_us = next_present_us - esp_timer_get_time();
        if (remaining_us <= 0)
          break;
        const TickType_t wait_ticks = pdMS_TO_TICKS(std::max<int64_t>(1, (remaining_us + 999) / 1000));
        if (xQueueReceive(this->direct_region_queue_, &incoming, wait_ticks) != pdTRUE)
          break;
        add_latest_request(incoming);
      }
    }
    while (xQueueReceive(this->direct_region_queue_, &incoming, 0) == pdTRUE) {
      add_latest_request(incoming);
    }
    const int64_t started_us = esp_timer_get_time();
    next_present_us = started_us + frame_period_us;
    bool presented = false;
    bool has_render_request = false;
    for (size_t i = 0; i < request_count; i++)
      has_render_request |= requests[i].operation != DirectRegionRequest::Operation::BARRIER;
    if (!has_render_request) {
      presented = true;
    } else if (s_region_srm_mutex != nullptr &&
               xSemaphoreTake(s_region_srm_mutex, pdMS_TO_TICKS(60)) == pdTRUE) {
      const uint32_t generation = this->direct_region_base_generation_.load(std::memory_order_acquire);
      uint8_t *active = nullptr;
      uint8_t *target = nullptr;
      if (this->direct_region_prepare_target_(generation, &active, &target) &&
          this->direct_region_compose_to_(requests, request_count, active, target) &&
          generation == this->direct_region_base_generation_.load(std::memory_order_acquire)) {
        auto *mipi_display = static_cast<mipi_dsi::MipiDsi *>(this->displays_[0]);
        presented = mipi_display->queue_direct_frame_buffer(target, 40);
        if (presented) {
          for (size_t i = 0; i < request_count; i++) {
            const auto &request = requests[i];
            if (request.operation == DirectRegionRequest::Operation::BARRIER)
              continue;
            if (auto *slot =
                    this->direct_region_find_slot_(request.x, request.y, request.width, request.height, true);
                slot != nullptr) {
              slot->x = request.x;
              slot->y = request.y;
              slot->width = request.width;
              slot->height = request.height;
              slot->valid = true;
            }
          }
        }
      } else {
        for (auto &buffer_generation : this->direct_region_buffer_generation_)
          buffer_generation = 0;
      }
      xSemaphoreGive(s_region_srm_mutex);
    }

    for (size_t i = 0; i < deferred_completion_count; i++) {
      if (deferred_completions[i].callback != nullptr)
        deferred_completions[i].callback(deferred_completions[i].arg);
    }
    for (size_t i = 0; i < request_count; i++) {
      if (requests[i].ready_callback != nullptr)
        requests[i].ready_callback(requests[i].ready_arg);
    }

    if (s_perf_logging_enabled) {
      const int64_t now_us = esp_timer_get_time();
      const uint32_t elapsed_us = static_cast<uint32_t>(now_us - started_us);
      perf_count++;
      perf_request_count += raw_request_count;
      perf_coalesced_count += coalesced_request_count;
      perf_max_batch_count = std::max(perf_max_batch_count, raw_request_count);
      perf_total_us += elapsed_us;
      perf_max_us = std::max(perf_max_us, elapsed_us);
      if (perf_window_start_us == 0)
        perf_window_start_us = now_us;
      if (now_us - perf_window_start_us >= 2000000LL) {
        const uint32_t submitted_copy =
            this->direct_region_copy_submitted_.exchange(0, std::memory_order_acq_rel);
        const uint32_t submitted_blend =
            this->direct_region_blend_submitted_.exchange(0, std::memory_order_acq_rel);
        const uint32_t submit_busy =
            this->direct_region_submit_busy_.exchange(0, std::memory_order_acq_rel);
        ESP_LOGI("lvgl.region",
                 "perf2s: frames=%u received=%u submitted=%u/%u busy=%u coalesced=%u batch_max=%u "
                 "avg=%uus max=%uus queued=%u last=%s",
                 perf_count, perf_request_count, submitted_copy, submitted_blend, submit_busy,
                 perf_coalesced_count, perf_max_batch_count,
                 static_cast<unsigned>(perf_total_us / std::max<uint32_t>(1, perf_count)), perf_max_us,
                 static_cast<unsigned>(uxQueueMessagesWaiting(this->direct_region_queue_)), YESNO(presented));
        perf_count = 0;
        perf_request_count = 0;
        perf_coalesced_count = 0;
        perf_max_batch_count = 0;
        perf_total_us = 0;
        perf_max_us = 0;
        perf_window_start_us = now_us;
      }
    }
  }
  this->direct_region_task_handle_ = nullptr;
  vTaskDelete(nullptr);
}
#endif

uint8_t LvglComponent::direct_blit_rgb888_async(const uint8_t *src, int src_stride, int x, int y, int width,
                                                 int height, LvglDirectBlitReadyCallback ready_callback,
                                                 void *ready_arg) {
#if LV_COLOR_DEPTH == 32 && defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA)
  if (!this->direct_mode_active_ || this->rotation != display::DISPLAY_ROTATION_0_DEGREES ||
      this->displays_.size() != 1 || src == nullptr || ready_callback == nullptr || s_snapshot_direct_active ||
      s_snapshot_swipe_active || this->direct_image_animation_active_ || width <= 0 || height <= 0 ||
      this->direct_region_paused_.load(std::memory_order_acquire) ||
      src_stride != width * 3 || x < 0 || y < 0 || x + width > this->width_ || y + height > this->height_) {
    return LVGL_DIRECT_BLIT_REJECTED;
  }

  auto *mipi_display = static_cast<mipi_dsi::MipiDsi *>(this->displays_[0]);
  if (mipi_display == nullptr || s_region_srm_client == nullptr || s_region_srm_mutex == nullptr ||
      this->direct_region_queue_ == nullptr)
    return LVGL_DIRECT_BLIT_REJECTED;
  DirectRegionRequest request{};
  request.operation = DirectRegionRequest::Operation::COPY_RGB888;
  request.source = src;
  request.source_stride = src_stride;
  request.source_width = width;
  request.source_height = height;
  request.x = x;
  request.y = y;
  request.width = width;
  request.height = height;
  request.ready_callback = ready_callback;
  request.ready_arg = ready_arg;
  if (xQueueSend(this->direct_region_queue_, &request, 0) == pdTRUE) {
    this->direct_region_copy_submitted_.fetch_add(1, std::memory_order_relaxed);
    return LVGL_DIRECT_BLIT_SUBMITTED;
  }
  this->direct_region_submit_busy_.fetch_add(1, std::memory_order_relaxed);
  return LVGL_DIRECT_BLIT_BUSY;
#else
  (void) src;
  (void) src_stride;
  (void) x;
  (void) y;
  (void) width;
  (void) height;
  (void) ready_callback;
  (void) ready_arg;
  return LVGL_DIRECT_BLIT_REJECTED;
#endif
}

uint8_t LvglComponent::direct_blend_argb8888_async(
    const uint8_t *background, int background_stride, const uint8_t *foreground, int foreground_stride,
    int foreground_width, int foreground_height, int foreground_x, int foreground_y, int x, int y, int width,
    int height, LvglDirectBlitReadyCallback ready_callback, void *ready_arg) {
#if LV_COLOR_DEPTH == 32 && defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA)
  if (!this->direct_mode_active_ || this->rotation != display::DISPLAY_ROTATION_0_DEGREES ||
      this->displays_.size() != 1 || background == nullptr || foreground == nullptr || ready_callback == nullptr ||
      s_snapshot_direct_active || s_snapshot_swipe_active || this->direct_image_animation_active_ || width <= 0 ||
      this->direct_region_paused_.load(std::memory_order_acquire) ||
      height <= 0 || background_stride != width * 3 || foreground_width <= 0 || foreground_height <= 0 ||
      foreground_stride != foreground_width * 4 || foreground_x < 0 || foreground_y < 0 ||
      foreground_x + width > foreground_width || foreground_y + height > foreground_height || x < 0 || y < 0 ||
      x + width > this->width_ || y + height > this->height_) {
    return LVGL_DIRECT_BLIT_REJECTED;
  }
  auto *mipi_display = static_cast<mipi_dsi::MipiDsi *>(this->displays_[0]);
  if (mipi_display == nullptr || s_region_blend_client == nullptr || s_region_srm_mutex == nullptr ||
      this->direct_region_queue_ == nullptr)
    return LVGL_DIRECT_BLIT_REJECTED;

  DirectRegionRequest request{};
  request.operation = DirectRegionRequest::Operation::BLEND_ARGB8888;
  request.source = foreground;
  request.source_stride = foreground_stride;
  request.source_width = foreground_width;
  request.source_height = foreground_height;
  request.source_x = foreground_x;
  request.source_y = foreground_y;
  request.background = background;
  request.background_stride = background_stride;
  request.x = x;
  request.y = y;
  request.width = width;
  request.height = height;
  request.ready_callback = ready_callback;
  request.ready_arg = ready_arg;
  if (xQueueSend(this->direct_region_queue_, &request, 0) == pdTRUE) {
    this->direct_region_blend_submitted_.fetch_add(1, std::memory_order_relaxed);
    return LVGL_DIRECT_BLIT_SUBMITTED;
  }
  this->direct_region_submit_busy_.fetch_add(1, std::memory_order_relaxed);
  return LVGL_DIRECT_BLIT_BUSY;
#else
  (void) background;
  (void) background_stride;
  (void) foreground;
  (void) foreground_stride;
  (void) foreground_width;
  (void) foreground_height;
  (void) foreground_x;
  (void) foreground_y;
  (void) x;
  (void) y;
  (void) width;
  (void) height;
  (void) ready_callback;
  (void) ready_arg;
  return LVGL_DIRECT_BLIT_REJECTED;
#endif
}

void LvglComponent::direct_blit_rgb888_release(int x, int y, int width, int height) {
#if LV_COLOR_DEPTH == 32 && defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA)
  if (s_region_srm_mutex == nullptr || xSemaphoreTake(s_region_srm_mutex, pdMS_TO_TICKS(25)) != pdTRUE)
    return;
  if (auto *slot = this->direct_region_find_slot_(x, y, width, height, false); slot != nullptr)
    slot->valid = false;
  bool any_valid = false;
  for (const auto &slot : this->direct_region_slots_)
    any_valid |= slot.valid;
  if (!any_valid) {
    for (auto &buffer_generation : this->direct_region_buffer_generation_)
      buffer_generation = 0;
  }
  xSemaphoreGive(s_region_srm_mutex);
#else
  (void) x;
  (void) y;
  (void) width;
  (void) height;
#endif
}

bool LvglComponent::direct_regions_pause(bool paused, uint32_t timeout_ms) {
#if LV_COLOR_DEPTH == 32 && defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA)
  if (!paused) {
    if (s_region_srm_mutex != nullptr && xSemaphoreTake(s_region_srm_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
      for (auto &buffer_generation : this->direct_region_buffer_generation_)
        buffer_generation = 0;
      xSemaphoreGive(s_region_srm_mutex);
    }
    this->direct_region_base_generation_.fetch_add(1, std::memory_order_release);
    this->direct_region_paused_.store(false, std::memory_order_release);
    return true;
  }

  this->direct_region_paused_.store(true, std::memory_order_release);
  if (this->direct_region_queue_ == nullptr || this->direct_region_task_handle_ == nullptr)
    return true;

  if (this->direct_region_barrier_ == nullptr)
    this->direct_region_barrier_ = xSemaphoreCreateBinaryStatic(&this->direct_region_barrier_storage_);
  if (this->direct_region_barrier_ == nullptr)
    return false;
  while (xSemaphoreTake(this->direct_region_barrier_, 0) == pdTRUE) {
  }
  DirectRegionRequest request{};
  request.operation = DirectRegionRequest::Operation::BARRIER;
  request.ready_callback = [](void *arg) {
    auto semaphore = static_cast<SemaphoreHandle_t>(arg);
    if (semaphore != nullptr)
      xSemaphoreGive(semaphore);
  };
  request.ready_arg = this->direct_region_barrier_;
  const TickType_t timeout_ticks = std::max<TickType_t>(1, pdMS_TO_TICKS(timeout_ms));
  if (xQueueSend(this->direct_region_queue_, &request, timeout_ticks) != pdTRUE)
    return false;
  return xSemaphoreTake(this->direct_region_barrier_, timeout_ticks) == pdTRUE;
#else
  (void) paused;
  (void) timeout_ms;
  return true;
#endif
}

bool LvglComponent::direct_blit_xrgb8888(const uint8_t *src, int src_stride, int x, int y, int width, int height) {
  return this->direct_blit_(src, src_stride, x, y, width, height, true, true);
}

bool LvglComponent::direct_blit_xrgb8888_coherent(const uint8_t *src, int src_stride, int x, int y, int width,
                                                   int height) {
  return this->direct_blit_(src, src_stride, x, y, width, height, true, true, true);
}

bool LvglComponent::direct_blit_(const uint8_t *src, int src_stride, int x, int y, int width, int height,
                                  bool xrgb8888, bool skip_dma2d, bool source_coherent) {
#if LV_COLOR_DEPTH == 32 && defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA)
  const size_t source_pixel_size = xrgb8888 ? 4U : 3U;
  if (!this->direct_mode_active_ || this->rotation != display::DISPLAY_ROTATION_0_DEGREES ||
      this->displays_.size() != 1 || src == nullptr || s_region_srm_client == nullptr || s_snapshot_direct_active ||
      s_snapshot_swipe_active || this->direct_image_animation_active_ || width <= 0 || height <= 0 ||
      src_stride != width * static_cast<int>(source_pixel_size) || x < 0 || y < 0 ||
      x + width > this->width_ || y + height > this->height_) {
    return false;
  }

  auto *mipi_display = static_cast<mipi_dsi::MipiDsi *>(this->displays_[0]);
  if (mipi_display == nullptr || s_region_srm_mutex == nullptr)
    return false;

  // Region frames are disposable: waiting here for a complete DSI refresh
  // made every 16 ms marquee update block for another 25-60 ms. A short FIFO
  // guard protects scanout while letting the next region replace an obsolete
  // one. The mutex also keeps the shared PPA cache ownership markers valid.
  if (xSemaphoreTake(s_region_srm_mutex, pdMS_TO_TICKS(20)) != pdTRUE)
    return false;
  struct RegionSrmUnlock {
    ~RegionSrmUnlock() { xSemaphoreGive(s_region_srm_mutex); }
  } region_srm_unlock;

  constexpr size_t BYTES_PER_PIXEL = 3;
  const size_t framebuffer_stride = (size_t) this->width_ * BYTES_PER_PIXEL;
  const size_t framebuffer_bytes = framebuffer_stride * (size_t) this->height_;
  const size_t source_bytes = (size_t) src_stride * (size_t) height;
  const int64_t started_us = esp_timer_get_time();

  if (!xrgb8888 && !skip_dma2d) {
    const auto dma2d_result = mipi_display->draw_pixels_at_dma2d_blocking(
        x, y, width, height, src, display::COLOR_ORDER_RGB, display::COLOR_BITNESS_888, 2);
    if (dma2d_result == mipi_dsi::Dma2dRegionResult::COMPLETE) {
      this->direct_region_base_generation_.fetch_add(1, std::memory_order_release);
      if (s_perf_logging_enabled) {
        const uint32_t elapsed_us = static_cast<uint32_t>(esp_timer_get_time() - started_us);
        if (elapsed_us > 30000U)
          ESP_LOGW("lvgl.region", "slow DMA2D region blit total=%uus area=%dx%d", (unsigned) elapsed_us, width,
                   height);
      }
      return true;
    }
    if (dma2d_result == mipi_dsi::Dma2dRegionResult::BUSY) {
      // Region animation frames are disposable. Keeping the last complete
      // frame is better than queueing behind a full-screen LVGL/artwork DMA
      // transaction and blocking every other UI animation for 50-500 ms.
      return true;
    }
    if (dma2d_result == mipi_dsi::Dma2dRegionResult::FAILED) {
      static bool dma2d_warned = false;
      if (!dma2d_warned) {
        ESP_LOGW("lvgl.region", "DMA2D region copy failed; retaining the previous frame");
        dma2d_warned = true;
      }
      return true;
    }
  }

  int64_t phase_started_us = esp_timer_get_time();
  const bool fifo_ready = lvgl_esphome_wait_direct_region_dsi_fifo();
  uint8_t *frame_buffer = mipi_display->get_presented_frame_buffer();
  const uint32_t fifo_wait_us = (uint32_t) (esp_timer_get_time() - phase_started_us);
  if (frame_buffer == nullptr)
    return false;

  phase_started_us = esp_timer_get_time();
  if (!source_coherent &&
      lvgl_cache_msync_external_result(src, source_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M) != ESP_OK)
    return false;
  const uint32_t source_sync_us = (uint32_t) (esp_timer_get_time() - phase_started_us);

  const size_t row_bytes = (size_t) width * BYTES_PER_PIXEL;
  uint8_t *target_region = frame_buffer + (size_t) y * framebuffer_stride + (size_t) x * BYTES_PER_PIXEL;
  const size_t target_region_span = (size_t) (height - 1) * framebuffer_stride + row_bytes;
  phase_started_us = esp_timer_get_time();
  if (lvgl_cache_msync_external_result(target_region, target_region_span,
                                       ESP_CACHE_MSYNC_FLAG_DIR_M2C) != ESP_OK) {
    return false;
  }
  const uint32_t target_sync_us = (uint32_t) (esp_timer_get_time() - phase_started_us);

  // Keep regional animation traffic on PPA. CPU writes into the framebuffer
  // currently scanned by DSI can stall for a full scan period under PSRAM
  // contention, even for a small control.
  phase_started_us = esp_timer_get_time();
  ppa_srm_oper_config_t cfg = {};
  cfg.in.buffer = const_cast<uint8_t *>(src);
  cfg.in.pic_w = width;
  cfg.in.pic_h = height;
  cfg.in.block_w = width;
  cfg.in.block_h = height;
  cfg.in.block_offset_x = 0;
  cfg.in.block_offset_y = 0;
  cfg.in.srm_cm = xrgb8888 ? PPA_SRM_COLOR_MODE_ARGB8888 : PPA_SRM_COLOR_MODE_RGB888;
  cfg.out.buffer = frame_buffer;
  cfg.out.buffer_size = framebuffer_bytes;
  cfg.out.pic_w = this->width_;
  cfg.out.pic_h = this->height_;
  cfg.out.block_offset_x = x;
  cfg.out.block_offset_y = y;
  cfg.out.srm_cm = PPA_SRM_COLOR_MODE_RGB888;
  cfg.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
  cfg.scale_x = 1.0f;
  cfg.scale_y = 1.0f;
  cfg.alpha_update_mode = PPA_ALPHA_NO_CHANGE;
  cfg.mode = PPA_TRANS_MODE_BLOCKING;

  // The source has been written back once and the active framebuffer region
  // invalidated once above. Suppress the driver's duplicate full-stride cache
  // maintenance for this exact transaction.
  const uintptr_t source_begin = reinterpret_cast<uintptr_t>(src);
  const uintptr_t target_begin = reinterpret_cast<uintptr_t>(frame_buffer);
  s_direct_ppa_source_begin.store(source_begin, std::memory_order_release);
  s_direct_ppa_source_end.store(source_begin + source_bytes, std::memory_order_release);
  s_direct_ppa_target_begin.store(target_begin, std::memory_order_release);
  s_direct_ppa_target_end.store(target_begin + framebuffer_bytes, std::memory_order_release);
  const esp_err_t result = ppa_do_scale_rotate_mirror(s_region_srm_client, &cfg);
  s_direct_ppa_source_begin.store(0, std::memory_order_release);
  s_direct_ppa_target_begin.store(0, std::memory_order_release);
  s_direct_ppa_source_end.store(0, std::memory_order_relaxed);
  s_direct_ppa_target_end.store(0, std::memory_order_relaxed);
  const uint32_t ppa_us = (uint32_t) (esp_timer_get_time() - phase_started_us);
  if (result != ESP_OK) {
    static bool warned = false;
    if (!warned) {
      ESP_LOGW("lvgl.region", "RGB888 PPA region blit unavailable (err=%d src=%p dst=%p)", result, src,
               frame_buffer);
      warned = true;
    }
    return false;
  }

  if (s_perf_logging_enabled) {
    static uint32_t count = 0;
    static uint64_t total_us = 0;
    static uint32_t max_us = 0;
    static int64_t window_started_us = 0;
    const int64_t now_us = esp_timer_get_time();
    const uint32_t elapsed_us = (uint32_t) (now_us - started_us);
    if (elapsed_us > 30000) {
      ESP_LOGW("lvgl.region", "slow blit total=%uus fifo=%uus/%s src_sync=%uus target_sync=%uus ppa=%uus",
               (unsigned) elapsed_us, (unsigned) fifo_wait_us, fifo_ready ? "ready" : "timeout",
               (unsigned) source_sync_us, (unsigned) target_sync_us, (unsigned) ppa_us);
    }
    count++;
    total_us += elapsed_us;
    max_us = std::max(max_us, elapsed_us);
    if (window_started_us == 0)
      window_started_us = now_us;
    if (now_us - window_started_us >= 2000000LL) {
      ESP_LOGI("lvgl.region", "perf2s: blits=%u avg=%uus max=%uus area=%dx%d buffers=1", (unsigned) count,
               (unsigned) (total_us / count), (unsigned) max_us, width, height);
      count = 0;
      total_us = 0;
      max_us = 0;
      window_started_us = now_us;
    }
  }
  this->direct_region_base_generation_.fetch_add(1, std::memory_order_release);
  return true;
#else
  return false;
#endif
}

extern "C" bool lvgl_esphome_direct_blit_rgb888(const uint8_t *src, int src_stride, int x, int y, int width,
                                                  int height) {
  auto *disp = lv_display_get_default();
  auto *component = disp == nullptr ? nullptr : static_cast<LvglComponent *>(lv_display_get_user_data(disp));
  return component != nullptr && component->direct_blit_rgb888(src, src_stride, x, y, width, height);
}

extern "C" uint8_t lvgl_esphome_direct_blit_rgb888_async(const uint8_t *src, int src_stride, int x, int y,
                                                            int width, int height,
                                                            LvglDirectBlitReadyCallback ready_callback,
                                                            void *ready_arg) {
  auto *disp = lv_display_get_default();
  auto *component = disp == nullptr ? nullptr : static_cast<LvglComponent *>(lv_display_get_user_data(disp));
  return component == nullptr
             ? LVGL_DIRECT_BLIT_REJECTED
             : component->direct_blit_rgb888_async(src, src_stride, x, y, width, height, ready_callback, ready_arg);
}

extern "C" uint8_t lvgl_esphome_direct_blend_argb8888_async(
    const uint8_t *background, int background_stride, const uint8_t *foreground, int foreground_stride,
    int foreground_width, int foreground_height, int foreground_x, int foreground_y, int x, int y, int width,
    int height, LvglDirectBlitReadyCallback ready_callback, void *ready_arg) {
  auto *disp = lv_display_get_default();
  auto *component = disp == nullptr ? nullptr : static_cast<LvglComponent *>(lv_display_get_user_data(disp));
  return component == nullptr
             ? LVGL_DIRECT_BLIT_REJECTED
             : component->direct_blend_argb8888_async(
                   background, background_stride, foreground, foreground_stride, foreground_width,
                   foreground_height, foreground_x, foreground_y, x, y, width, height, ready_callback, ready_arg);
}

extern "C" void lvgl_esphome_direct_blit_rgb888_release(int x, int y, int width, int height) {
  auto *disp = lv_display_get_default();
  auto *component = disp == nullptr ? nullptr : static_cast<LvglComponent *>(lv_display_get_user_data(disp));
  if (component != nullptr)
    component->direct_blit_rgb888_release(x, y, width, height);
}

extern "C" bool lvgl_esphome_direct_regions_pause(bool paused, uint32_t timeout_ms) {
  auto *disp = lv_display_get_default();
  auto *component = disp == nullptr ? nullptr : static_cast<LvglComponent *>(lv_display_get_user_data(disp));
  return component == nullptr || component->direct_regions_pause(paused, timeout_ms);
}

extern "C" void lvgl_esphome_synchronize_direct_framebuffer_area(int x, int y, int width, int height) {
  auto *disp = lv_display_get_default();
  auto *component = disp == nullptr ? nullptr : static_cast<LvglComponent *>(lv_display_get_user_data(disp));
  if (component != nullptr)
    component->synchronize_direct_framebuffer_area(x, y, width, height);
}

extern "C" bool lvgl_esphome_wait_for_direct_frame_presented(uint32_t timeout_ms) {
  auto *disp = lv_display_get_default();
  auto *component = disp == nullptr ? nullptr : static_cast<LvglComponent *>(lv_display_get_user_data(disp));
  return component != nullptr && component->wait_for_direct_frame_presented(timeout_ms);
}

extern "C" bool lvgl_esphome_direct_blit_xrgb8888(const uint8_t *src, int src_stride, int x, int y, int width,
                                                     int height) {
  auto *disp = lv_display_get_default();
  auto *component = disp == nullptr ? nullptr : static_cast<LvglComponent *>(lv_display_get_user_data(disp));
  return component != nullptr && component->direct_blit_xrgb8888(src, src_stride, x, y, width, height);
}

extern "C" bool lvgl_esphome_direct_blit_xrgb8888_coherent(const uint8_t *src, int src_stride, int x, int y,
                                                              int width, int height) {
  auto *disp = lv_display_get_default();
  auto *component = disp == nullptr ? nullptr : static_cast<LvglComponent *>(lv_display_get_user_data(disp));
  return component != nullptr && component->direct_blit_xrgb8888_coherent(src, src_stride, x, y, width, height);
}

void LvglComponent::synchronize_direct_framebuffer_area(int x, int y, int width, int height) {
#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && LV_COLOR_DEPTH == 32
  if (!this->direct_mode_active_ || this->disp_ == nullptr || this->direct_last_flushed_buf_ == nullptr ||
      this->disp_->buf_1 == nullptr || this->disp_->buf_2 == nullptr || width <= 0 || height <= 0)
    return;

  const int x1 = std::clamp(x, 0, static_cast<int>(this->width_));
  const int x2 = std::clamp(x + width, 0, static_cast<int>(this->width_));
  const int y1 = std::clamp(y, 0, static_cast<int>(this->height_));
  const int y2 = std::clamp(y + height, 0, static_cast<int>(this->height_));
  if (x2 <= x1 || y2 <= y1)
    return;

  constexpr size_t BYTES_PER_PIXEL = 3;
  const size_t row_bytes = static_cast<size_t>(this->width_) * BYTES_PER_PIXEL;
  const size_t copy_row_bytes = static_cast<size_t>(x2 - x1) * BYTES_PER_PIXEL;
  const size_t source_span = static_cast<size_t>(y2 - y1 - 1) * row_bytes + copy_row_bytes;
  const auto *source = this->direct_last_flushed_buf_ + static_cast<size_t>(y1) * row_bytes +
                       static_cast<size_t>(x1) * BYTES_PER_PIXEL;

  lv_draw_buf_t *buffers[] = {this->disp_->buf_1, this->disp_->buf_2};
  for (auto *buffer : buffers) {
    if (buffer == nullptr || buffer->data == nullptr)
      continue;
    auto *destination = static_cast<uint8_t *>(buffer->data) + static_cast<size_t>(y1) * row_bytes +
                        static_cast<size_t>(x1) * BYTES_PER_PIXEL;
    if (destination == source)
      continue;

    lvgl_cache_msync_external(destination, source_span, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
#if CONFIG_ESPHOME_LVGL_SNAPSHOT_DMA2D_M2M
    if (dma2d_m2m_copy_rgb888_2d(this->direct_last_flushed_buf_, this->width_, this->height_, x1, y1,
                                 static_cast<uint8_t *>(buffer->data), this->width_, this->height_, x1, y1,
                                 x2 - x1, y2 - y1)) {
      lvgl_cache_msync_external(destination, source_span, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
      continue;
    }
#endif
    lvgl_cache_msync_external(source, source_span, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    for (int row = y1; row < y2; row++) {
      memcpy(static_cast<uint8_t *>(buffer->data) + static_cast<size_t>(row) * row_bytes +
                 static_cast<size_t>(x1) * BYTES_PER_PIXEL,
             this->direct_last_flushed_buf_ + static_cast<size_t>(row) * row_bytes +
                 static_cast<size_t>(x1) * BYTES_PER_PIXEL,
             copy_row_bytes);
    }
    lvgl_cache_msync_external(destination, source_span, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  }
#else
  (void) x;
  (void) y;
  (void) width;
  (void) height;
#endif
}

void LvglComponent::synchronize_direct_framebuffer_rows(int y, int height) {
  this->synchronize_direct_framebuffer_area(0, y, this->width_, height);
}

void LvglComponent::realign_direct_buffer_after_manual_present(bool synchronize) {
  if (!this->direct_mode_active_ || this->disp_ == nullptr || this->direct_last_flushed_buf_ == nullptr)
    return;
  if (this->disp_->buf_1 == nullptr || this->disp_->buf_2 == nullptr)
    return;

  lv_draw_buf_t *next_lvgl_buf = nullptr;
  bool lvgl_buffers_seeded = false;
  if (this->disp_->buf_1->data == this->direct_last_flushed_buf_) {
    next_lvgl_buf = this->disp_->buf_2;
  } else if (this->disp_->buf_2->data == this->direct_last_flushed_buf_) {
    next_lvgl_buf = this->disp_->buf_1;
  }

#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && LV_COLOR_DEPTH == 32
  // Manual compositors can use the third DSI framebuffer, while LVGL owns
  // only framebuffers 0 and 1. Seed both LVGL buffers from that final frame
  // before returning control; otherwise the first ordinary refresh exposes
  // stale pixels from before the transition.
  if (next_lvgl_buf == nullptr && this->rotation == display::DISPLAY_ROTATION_0_DEGREES &&
      this->displays_.size() == 1) {
    auto *mipi_display = static_cast<mipi_dsi::MipiDsi *>(this->displays_[0]);
    uint8_t *third_framebuffer = mipi_display == nullptr ? nullptr : mipi_display->get_frame_buffer(2);
    if (this->direct_last_flushed_buf_ == third_framebuffer) {
      if (!synchronize) {
        // FULL mode redraws every pixel before the next flush. Keep DSI on the
        // completed third framebuffer and render directly into an idle LVGL
        // buffer; copying the 1.92 MB frame twice only saturates PSRAM.
        next_lvgl_buf =
            this->disp_->buf_act == this->disp_->buf_1 || this->disp_->buf_act == this->disp_->buf_2
                ? this->disp_->buf_act
                : this->disp_->buf_1;
      } else {
        constexpr size_t BYTES_PER_PIXEL = 3;
      const size_t frame_bytes = static_cast<size_t>(this->width_) * static_cast<size_t>(this->height_) *
                                 BYTES_PER_PIXEL;
      auto copy_final_frame = [&](lv_draw_buf_t *destination) -> bool {
        if (destination == nullptr || destination->data == nullptr || destination->data_size < frame_bytes)
          return false;
        auto *dst = static_cast<uint8_t *>(destination->data);
        // Clean any old CPU-owned destination lines before DMA takes
        // ownership. The source is already coherent physical memory because
        // it has just been queued to DSI by the manual compositor.
        lvgl_cache_msync_external(dst, frame_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
#if CONFIG_ESPHOME_LVGL_SNAPSHOT_DMA2D_M2M
        if (dma2d_m2m_copy_rgb888_2d(this->direct_last_flushed_buf_, this->width_, this->height_, 0, 0, dst,
                                     this->width_, this->height_, 0, 0, this->width_, this->height_)) {
          lvgl_cache_msync_external(dst, frame_bytes, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
          return true;
        }
#endif
        // DMA2D can be unavailable during early setup. In that rare case,
        // invalidate the DMA-produced source before the CPU reads it.
        lvgl_cache_msync_external(this->direct_last_flushed_buf_, frame_bytes, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
        memcpy(dst, this->direct_last_flushed_buf_, frame_bytes);
        lvgl_cache_msync_external(dst, frame_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
        return true;
      };

      const bool first_ready = copy_final_frame(this->disp_->buf_1);
      const bool second_ready = copy_final_frame(this->disp_->buf_2);
      if (first_ready && second_ready) {
        // Mirror the normal direct-mode invariant: buf_1 is the coherent
        // previously-presented frame and LVGL starts drawing in buf_2.
        this->direct_last_flushed_buf_ = static_cast<uint8_t *>(this->disp_->buf_1->data);
        next_lvgl_buf = this->disp_->buf_2;
        lvgl_buffers_seeded = true;
      } else {
        ESP_LOGW(TAG, "direct mode: failed to seed LVGL buffers from third DSI framebuffer");
      }
      }
    }
  }
#endif

  if (synchronize && next_lvgl_buf != nullptr && !lvgl_buffers_seeded) {
    lv_area_t full;
    full.x1 = 0;
    full.y1 = 0;
    full.x2 = static_cast<lv_coord_t>(this->width_ - 1);
    full.y2 = static_cast<lv_coord_t>(this->height_ - 1);
    this->sync_direct_other_buffer_(&full, this->direct_last_flushed_buf_);
  }

  if (next_lvgl_buf != nullptr && this->disp_->buf_act != next_lvgl_buf) {
    ESP_LOGD(TAG, "direct mode: realigning LVGL buf_act away from presented framebuffer");
    this->disp_->buf_act = next_lvgl_buf;
  }
}

#if LV_COLOR_DEPTH == 32 && defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA)
ppa_client_handle_t LvglComponent::register_direct_image_animation_client() {
#if LV_COLOR_DEPTH == 32 && defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA)
  ppa_client_config_t srm_cfg = {};
  srm_cfg.oper_type = PPA_OPERATION_SRM;
  srm_cfg.max_pending_trans_num = 12;
  srm_cfg.data_burst_length = LVGL_ESPHOME_PPA_DIRECT_ANIMATION_DATA_BURST_LENGTH;
  ppa_client_handle_t client = nullptr;
  if (ppa_register_client(&srm_cfg, &client) != ESP_OK)
    return nullptr;
  ppa_event_callbacks_t callbacks{};
  callbacks.on_trans_done = direct_ppa_frame_done;
  if (ppa_client_register_event_callbacks(client, &callbacks) != ESP_OK) {
    ppa_unregister_client(client);
    return nullptr;
  }
  ESP_LOGI(TAG, "PPA image animation SRM client registered by worker (srm_burst=%d)",
           (int) LVGL_ESPHOME_PPA_DIRECT_ANIMATION_DATA_BURST_LENGTH);
  return client;
#else
  return nullptr;
#endif
}

ppa_client_handle_t LvglComponent::register_direct_image_blend_client() {
  ppa_client_config_t blend_cfg{};
  blend_cfg.oper_type = PPA_OPERATION_BLEND;
  blend_cfg.max_pending_trans_num = 8;
  blend_cfg.data_burst_length = LVGL_ESPHOME_PPA_DIRECT_ANIMATION_DATA_BURST_LENGTH;
  ppa_client_handle_t client = nullptr;
  if (ppa_register_client(&blend_cfg, &client) != ESP_OK)
    return nullptr;
  ppa_event_callbacks_t callbacks{};
  callbacks.on_trans_done = direct_ppa_frame_done;
  if (ppa_client_register_event_callbacks(client, &callbacks) != ESP_OK) {
    ppa_unregister_client(client);
    return nullptr;
  }
  ESP_LOGI(TAG, "PPA image transition blend client registered (burst=%d)",
           (int) LVGL_ESPHOME_PPA_DIRECT_ANIMATION_DATA_BURST_LENGTH);
  return client;
}
#endif

bool LvglComponent::begin_direct_image_animation() {
#if LV_COLOR_DEPTH == 32 && defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA)
  if (!this->direct_mode_active_ || !this->full_refresh_ || this->disp_ == nullptr ||
      this->rotation != display::DISPLAY_ROTATION_0_DEGREES || this->displays_.size() != 1 ||
      this->draw_buf_ == nullptr || this->draw_buf2_ == nullptr || s_snapshot_direct_active ||
      s_snapshot_swipe_active || s_snapshot_app_open_frame_held) {
    static uint32_t last_deferred_log_ms = 0;
    if (millis() - last_deferred_log_ms >= 1000) {
      ESP_LOGD(TAG,
               "Direct image start deferred: direct=%d full=%d disp=%p rot=%d displays=%u bufs=%p/%p "
               "snapshot=%d swipe=%d app_hold=%d",
               this->direct_mode_active_, this->full_refresh_, this->disp_, static_cast<int>(this->rotation),
               static_cast<unsigned>(this->displays_.size()), this->draw_buf_, this->draw_buf2_,
               s_snapshot_direct_active, s_snapshot_swipe_active, s_snapshot_app_open_frame_held);
      last_deferred_log_ms = millis();
    }
    return false;
  }
  if (!this->direct_image_animation_active_) {
    lv_display_enable_invalidation(this->disp_, false);
    auto *mipi_display = static_cast<mipi_dsi::MipiDsi *>(this->displays_[0]);
    for (size_t index = 0; index < 3; index++) {
      uint8_t *framebuffer = mipi_display->get_frame_buffer(index);
      if (framebuffer == nullptr) {
        lv_display_enable_invalidation(this->disp_, true);
        return false;
      }
      /* LVGL may still own dirty cache lines from the last CPU-rendered frame.
       * Each framebuffer is handed to DMA lazily, immediately before PPA first
       * writes it. This avoids a three-framebuffer bandwidth spike here while
       * preventing old cache lines from being evicted over a newer PPA frame. */
      this->direct_image_framebuffer_dma_owned_[index] = false;
    }
    this->direct_image_synced_source_ = nullptr;
    this->direct_image_synced_source_size_ = 0;
    this->direct_image_animation_active_ = true;
    lv_draw_ppa_direct_animation_qos_apply();
  }
  return true;
#else
  return false;
#endif
}

bool LvglComponent::prepare_direct_framebuffer_dma_ownership_(uint8_t *framebuffer, size_t index) {
#if LV_COLOR_DEPTH == 32 && defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA)
  if (framebuffer == nullptr || index >= 3 || this->displays_.size() != 1)
    return false;
  if (this->direct_image_framebuffer_dma_owned_[index])
    return true;

  auto *mipi_display = static_cast<mipi_dsi::MipiDsi *>(this->displays_[0]);
  if (mipi_display == nullptr || framebuffer != mipi_display->get_frame_buffer(index))
    return false;

  const size_t framebuffer_size = mipi_display->get_frame_buffer_size();
  constexpr size_t CACHE_HANDOFF_CHUNK = 64U * 1024U;
  for (size_t offset = 0; offset < framebuffer_size; offset += CACHE_HANDOFF_CHUNK) {
    const size_t chunk = std::min(CACHE_HANDOFF_CHUNK, framebuffer_size - offset);
    lvgl_esphome_wait_snapshot_dsi_fifo();
    /* PPA replaces every byte of this idle framebuffer before it is queued to
     * DSI. Writing LVGL's obsolete cache contents back to PSRAM first doubles
     * the hand-off traffic and can starve scanout. M2C invalidation alone
     * discards those stale CPU cache lines, preventing a later eviction from
     * overwriting the freshly rendered DMA frame. */
    if (lvgl_cache_msync_external_result(framebuffer + offset, chunk, ESP_CACHE_MSYNC_FLAG_DIR_M2C) != ESP_OK) {
      ESP_LOGW(TAG, "Unable to hand direct framebuffer %u to DMA at offset %u", static_cast<unsigned>(index),
               static_cast<unsigned>(offset));
      return false;
    }
    taskYIELD();
  }

  this->direct_image_framebuffer_dma_owned_[index] = true;
  ESP_LOGD(TAG, "Direct framebuffer %u cache ownership handed to DMA", static_cast<unsigned>(index));
  return true;
#else
  (void) framebuffer;
  (void) index;
  return false;
#endif
}

#if LV_COLOR_DEPTH == 32 && defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA)
bool LvglComponent::direct_resolve_image_crop(const lv_image_dsc_t *source, int *source_x, int *source_y,
                                              int *source_width, int *source_height) const {
  if (source == nullptr || source_x == nullptr || source_y == nullptr || source_width == nullptr ||
      source_height == nullptr || *source_width < 2 || *source_height < 2 || this->width_ != this->height_)
    return false;

  const int desired_crop_size = std::min(*source_width, *source_height);
  int resolved_size = 0;
  for (int candidate = desired_crop_size; candidate >= 2; candidate--) {
    // Keep a complete PPA macroblock height whenever an exact scaler phase is
    // available. Besides avoiding the corrupt partial-macroblock tail seen on
    // ESP32-P4, this lets an 800x800 viewport use one SRM transaction instead
    // of rendering the final rows in a second pass. The source image itself is
    // still decoded at full resolution; only the square cover crop changes.
    if ((candidate & 15) != 0)
      continue;
    const uint32_t scale_q16 =
        (static_cast<uint32_t>(this->width_) * 16U + static_cast<uint32_t>(candidate) - 1U) /
        static_cast<uint32_t>(candidate);
    if (scale_q16 != 0 && scale_q16 < 256U * 16U &&
        scale_q16 * static_cast<uint32_t>(candidate) == static_cast<uint32_t>(this->width_) * 16U) {
      resolved_size = candidate;
      break;
    }
  }
  if (resolved_size < 2)
    return false;

  *source_x += (*source_width - resolved_size) / 2;
  *source_y += (*source_height - resolved_size) / 2;
  *source_width = resolved_size;
  *source_height = resolved_size;
  *source_x = std::clamp(*source_x, 0, static_cast<int>(source->header.w) - resolved_size);
  *source_y = std::clamp(*source_y, 0, static_cast<int>(source->header.h) - resolved_size);
  return *source_x >= 0 && *source_y >= 0;
}

bool LvglComponent::direct_present_image_crop(const lv_image_dsc_t *source, int source_x, int source_y,
                                              int source_width, int source_height, ppa_client_handle_t srm_client) {
  return this->direct_render_image_crop_(source, source_x, source_y, source_width, source_height, srm_client, nullptr,
                                         0, true, false);
}

bool LvglComponent::direct_present_rgb888_crop_dma2d(const lv_image_dsc_t *source, int source_x, int source_y,
                                                     int source_width, int source_height) {
  if (!this->direct_image_animation_active_ || source == nullptr || source->data == nullptr ||
      static_cast<lv_color_format_t>(source->header.cf) != LV_COLOR_FORMAT_RGB888 ||
      source_width != this->width_ || source_height != this->height_ || source_x < 0 || source_y < 0 ||
      source_x + source_width > static_cast<int>(source->header.w) ||
      source_y + source_height > static_cast<int>(source->header.h) || this->displays_.size() != 1) {
    return false;
  }

  constexpr size_t bytes_per_pixel = 3;
  const size_t source_stride = source->header.stride != 0
                                   ? source->header.stride
                                   : static_cast<size_t>(source->header.w) * bytes_per_pixel;
  if (source_stride % bytes_per_pixel != 0)
    return false;
  const int source_stride_pixels = static_cast<int>(source_stride / bytes_per_pixel);
  if (source_stride_pixels < static_cast<int>(source->header.w))
    return false;

  const size_t source_bytes = source_stride * static_cast<size_t>(source->header.h);
  if (this->direct_image_synced_source_ != source->data || this->direct_image_synced_source_size_ != source_bytes) {
    const bool source_written_by_dma = esphome_artwork_image_buffer_written_by_dma != nullptr &&
                                       esphome_artwork_image_buffer_written_by_dma(source->data);
    if (!source_written_by_dma &&
        lvgl_cache_msync_external_result(source->data, source_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M) != ESP_OK) {
      return false;
    }
    this->direct_image_synced_source_ = source->data;
    this->direct_image_synced_source_size_ = source_bytes;
  }

  auto *mipi_display = static_cast<mipi_dsi::MipiDsi *>(this->displays_[0]);
  if (mipi_display == nullptr)
    return false;
  uint8_t *target = mipi_display->get_direct_render_frame_buffer();
  if (target == nullptr)
    target = mipi_display->wait_for_direct_render_frame_buffer(nullptr, nullptr, 20);
  if (target == nullptr)
    return false;

  size_t target_index = 3;
  for (size_t index = 0; index < 3; index++) {
    if (target == mipi_display->get_frame_buffer(index)) {
      target_index = index;
      break;
    }
  }
  if (target_index >= 3 || !this->prepare_direct_framebuffer_dma_ownership_(target, target_index))
    return false;

  const int64_t started_us = esp_timer_get_time();
  if (esphome_mipi_dsi_wait_fifo_margin != nullptr) {
    esphome_mipi_dsi_wait_fifo_margin(CONFIG_ESPHOME_LVGL_PPA_SRM_TRANSFORM_DSI_FIFO_MIN,
                                      CONFIG_ESPHOME_LVGL_PPA_SRM_TRANSFORM_DSI_WAIT_US);
  }
  const uint32_t fifo_us = static_cast<uint32_t>(esp_timer_get_time() - started_us);
  const int64_t copy_started_us = esp_timer_get_time();
  const bool copied = dma2d_m2m_copy_rgb888_2d(
      source->data, source_stride_pixels, source->header.h, source_x, source_y, target, this->width_,
      this->height_, 0, 0, this->width_, this->height_);
  const uint32_t copy_us = static_cast<uint32_t>(esp_timer_get_time() - copy_started_us);
  if (!copied)
    return false;

  this->direct_image_framebuffer_dma_owned_[target_index] = true;
  const int64_t present_started_us = esp_timer_get_time();
  if (!mipi_display->queue_direct_frame_buffer(target, 50, false))
    return false;
  const uint32_t present_us = static_cast<uint32_t>(esp_timer_get_time() - present_started_us);
  this->direct_last_flushed_buf_ = target;

  if (s_perf_logging_enabled) {
    static uint32_t frames = 0;
    static uint64_t total_us = 0;
    static uint64_t total_fifo_us = 0;
    static uint64_t total_copy_us = 0;
    static uint64_t total_present_us = 0;
    static uint32_t max_us = 0;
    static int64_t window_started_us = 0;
    const int64_t now_us = esp_timer_get_time();
    const uint32_t elapsed_us = static_cast<uint32_t>(now_us - started_us);
    frames++;
    total_us += elapsed_us;
    total_fifo_us += fifo_us;
    total_copy_us += copy_us;
    total_present_us += present_us;
    max_us = std::max(max_us, elapsed_us);
    if (window_started_us == 0)
      window_started_us = now_us;
    if (now_us - window_started_us >= 2000000LL) {
      ESP_LOGI("lvgl.ken_burns.dma2d",
               "perf2s: frames=%u avg=%uus max=%uus fifo=%uus copy=%uus present=%uus crop=%dx%d",
               static_cast<unsigned>(frames),
               static_cast<unsigned>(total_us / std::max<uint32_t>(1, frames)),
               static_cast<unsigned>(max_us),
               static_cast<unsigned>(total_fifo_us / std::max<uint32_t>(1, frames)),
               static_cast<unsigned>(total_copy_us / std::max<uint32_t>(1, frames)),
               static_cast<unsigned>(total_present_us / std::max<uint32_t>(1, frames)), source_width,
               source_height);
      frames = 0;
      total_us = 0;
      total_fifo_us = 0;
      total_copy_us = 0;
      total_present_us = 0;
      max_us = 0;
      window_started_us = now_us;
    }
  }
  return true;
}

bool LvglComponent::direct_present_rgb888_crop_subpixel(
    const lv_image_dsc_t *source, int source_x, int source_y, int source_width, int source_height,
    uint8_t subpixel_alpha, bool subpixel_vertical, ppa_client_handle_t blend_client) {
  if (!this->direct_image_animation_active_ || source == nullptr || source->data == nullptr ||
      static_cast<lv_color_format_t>(source->header.cf) != LV_COLOR_FORMAT_RGB888 || blend_client == nullptr ||
      subpixel_alpha == 0 || source_width != this->width_ || source_height != this->height_ || source_x < 0 ||
      source_y < 0 || source_x + source_width + (subpixel_vertical ? 0 : 1) > static_cast<int>(source->header.w) ||
      source_y + source_height + (subpixel_vertical ? 1 : 0) > static_cast<int>(source->header.h) ||
      this->displays_.size() != 1) {
    return false;
  }

  constexpr size_t bytes_per_pixel = 3;
  const size_t source_stride = source->header.stride != 0
                                   ? source->header.stride
                                   : static_cast<size_t>(source->header.w) * bytes_per_pixel;
  if (source_stride % bytes_per_pixel != 0)
    return false;
  const int source_stride_pixels = static_cast<int>(source_stride / bytes_per_pixel);
  if (source_stride_pixels < static_cast<int>(source->header.w))
    return false;

  const size_t source_bytes = source_stride * static_cast<size_t>(source->header.h);
  if (this->direct_image_synced_source_ != source->data || this->direct_image_synced_source_size_ != source_bytes) {
    const bool source_written_by_dma = esphome_artwork_image_buffer_written_by_dma != nullptr &&
                                       esphome_artwork_image_buffer_written_by_dma(source->data);
    if (!source_written_by_dma &&
        lvgl_cache_msync_external_result(source->data, source_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M) != ESP_OK) {
      return false;
    }
    this->direct_image_synced_source_ = source->data;
    this->direct_image_synced_source_size_ = source_bytes;
  }

  auto *mipi_display = static_cast<mipi_dsi::MipiDsi *>(this->displays_[0]);
  if (mipi_display == nullptr)
    return false;
  uint8_t *target = mipi_display->get_direct_render_frame_buffer();
  if (target == nullptr)
    target = mipi_display->wait_for_direct_render_frame_buffer(nullptr, nullptr, 20);
  if (target == nullptr)
    return false;

  size_t target_index = 3;
  for (size_t index = 0; index < 3; index++) {
    if (target == mipi_display->get_frame_buffer(index)) {
      target_index = index;
      break;
    }
  }
  if (target_index >= 3 || !this->prepare_direct_framebuffer_dma_ownership_(target, target_index))
    return false;

  const size_t target_bytes =
      static_cast<size_t>(this->width_) * static_cast<size_t>(this->height_) * bytes_per_pixel;
  if (mipi_display->get_frame_buffer_size() < target_bytes)
    return false;

  ppa_blend_oper_config_t config{};
  config.in_bg.buffer = source->data;
  config.in_bg.pic_w = source_stride_pixels;
  config.in_bg.pic_h = source->header.h;
  config.in_bg.block_w = source_width;
  config.in_bg.block_h = source_height;
  config.in_bg.block_offset_x = source_x;
  config.in_bg.block_offset_y = source_y;
  config.in_bg.blend_cm = PPA_BLEND_COLOR_MODE_RGB888;
  config.in_fg.buffer = source->data;
  config.in_fg.pic_w = source_stride_pixels;
  config.in_fg.pic_h = source->header.h;
  config.in_fg.block_w = source_width;
  config.in_fg.block_h = source_height;
  config.in_fg.block_offset_x = source_x + (subpixel_vertical ? 0 : 1);
  config.in_fg.block_offset_y = source_y + (subpixel_vertical ? 1 : 0);
  config.in_fg.blend_cm = PPA_BLEND_COLOR_MODE_RGB888;
  config.out.buffer = target;
  config.out.buffer_size = target_bytes;
  config.out.pic_w = this->width_;
  config.out.pic_h = this->height_;
  config.out.block_offset_x = 0;
  config.out.block_offset_y = 0;
  config.out.blend_cm = PPA_BLEND_COLOR_MODE_RGB888;
  config.bg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
  config.bg_alpha_fix_val = 0xFF;
  config.fg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
  config.fg_alpha_fix_val = subpixel_alpha;
  config.mode = PPA_TRANS_MODE_NON_BLOCKING;

  DirectPpaFrameCompletion completion;
  completion.remaining.store(1, std::memory_order_release);
  completion.waiter = xTaskGetCurrentTaskHandle();
  config.user_data = &completion;
  ulTaskNotifyTake(pdTRUE, 0);

  const int64_t started_us = esp_timer_get_time();
  if (esphome_mipi_dsi_wait_fifo_margin != nullptr) {
    esphome_mipi_dsi_wait_fifo_margin(CONFIG_ESPHOME_LVGL_PPA_SRM_TRANSFORM_DSI_FIFO_MIN,
                                      CONFIG_ESPHOME_LVGL_PPA_SRM_TRANSFORM_DSI_WAIT_US);
  }
  const uint32_t fifo_us = static_cast<uint32_t>(esp_timer_get_time() - started_us);
  const uintptr_t source_begin = reinterpret_cast<uintptr_t>(source->data);
  const uintptr_t target_begin = reinterpret_cast<uintptr_t>(target);
  s_direct_ppa_source_begin.store(source_begin, std::memory_order_release);
  s_direct_ppa_source_end.store(source_begin + source_bytes, std::memory_order_release);
  s_direct_ppa_source2_begin.store(source_begin, std::memory_order_release);
  s_direct_ppa_source2_end.store(source_begin + source_bytes, std::memory_order_release);
  s_direct_ppa_target_begin.store(target_begin, std::memory_order_release);
  s_direct_ppa_target_end.store(target_begin + target_bytes, std::memory_order_release);

  const int64_t blend_started_us = esp_timer_get_time();
  const esp_err_t result = ppa_do_blend(blend_client, &config);
  if (result == ESP_OK) {
    while (completion.remaining.load(std::memory_order_acquire) != 0)
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  }
  const uint32_t blend_us = static_cast<uint32_t>(esp_timer_get_time() - blend_started_us);
  s_direct_ppa_source_begin.store(0, std::memory_order_release);
  s_direct_ppa_source2_begin.store(0, std::memory_order_release);
  s_direct_ppa_target_begin.store(0, std::memory_order_release);
  s_direct_ppa_source_end.store(0, std::memory_order_relaxed);
  s_direct_ppa_source2_end.store(0, std::memory_order_relaxed);
  s_direct_ppa_target_end.store(0, std::memory_order_relaxed);
  if (result != ESP_OK) {
    ESP_LOGW("lvgl.ken_burns.subpixel", "PPA interpolation failed: %s", esp_err_to_name(result));
    return false;
  }

  this->direct_image_framebuffer_dma_owned_[target_index] = true;
  const int64_t present_started_us = esp_timer_get_time();
  if (!mipi_display->queue_direct_frame_buffer(target, 50, false))
    return false;
  const uint32_t present_us = static_cast<uint32_t>(esp_timer_get_time() - present_started_us);
  this->direct_last_flushed_buf_ = target;

  if (s_perf_logging_enabled) {
    static uint32_t frames = 0;
    static uint64_t total_us = 0;
    static uint64_t total_fifo_us = 0;
    static uint64_t total_blend_us = 0;
    static uint64_t total_present_us = 0;
    static uint32_t max_us = 0;
    static int64_t window_started_us = 0;
    const int64_t now_us = esp_timer_get_time();
    const uint32_t elapsed_us = static_cast<uint32_t>(now_us - started_us);
    frames++;
    total_us += elapsed_us;
    total_fifo_us += fifo_us;
    total_blend_us += blend_us;
    total_present_us += present_us;
    max_us = std::max(max_us, elapsed_us);
    if (window_started_us == 0)
      window_started_us = now_us;
    if (now_us - window_started_us >= 2000000LL) {
      ESP_LOGI("lvgl.ken_burns.subpixel",
               "perf2s: frames=%u avg=%uus max=%uus fifo=%uus blend=%uus present=%uus axis=%c alpha=%u",
               static_cast<unsigned>(frames),
               static_cast<unsigned>(total_us / std::max<uint32_t>(1, frames)),
               static_cast<unsigned>(max_us),
               static_cast<unsigned>(total_fifo_us / std::max<uint32_t>(1, frames)),
               static_cast<unsigned>(total_blend_us / std::max<uint32_t>(1, frames)),
               static_cast<unsigned>(total_present_us / std::max<uint32_t>(1, frames)),
               subpixel_vertical ? 'y' : 'x', static_cast<unsigned>(subpixel_alpha));
      frames = 0;
      total_us = 0;
      total_fifo_us = 0;
      total_blend_us = 0;
      total_present_us = 0;
      max_us = 0;
      window_started_us = now_us;
    }
  }
  return true;
}

bool LvglComponent::direct_present_scaled_rgb888_crop_subpixel(
    const lv_image_dsc_t *source, int source_x, int source_y, int source_width, int source_height,
    uint8_t subpixel_alpha, bool subpixel_vertical, uint8_t *scratch, size_t scratch_size,
    ppa_client_handle_t blend_client, ppa_client_handle_t srm_client) {
  if (!this->direct_image_animation_active_ || source == nullptr || source->data == nullptr || scratch == nullptr ||
      static_cast<lv_color_format_t>(source->header.cf) != LV_COLOR_FORMAT_RGB888 || blend_client == nullptr ||
      srm_client == nullptr || subpixel_alpha == 0 || source_width < 2 || source_height < 2 || source_x < 0 ||
      source_y < 0 || source_x + source_width + (subpixel_vertical ? 0 : 1) > static_cast<int>(source->header.w) ||
      source_y + source_height + (subpixel_vertical ? 1 : 0) > static_cast<int>(source->header.h)) {
    return false;
  }

  constexpr size_t BYTES_PER_PIXEL = 3;
  const size_t source_stride = source->header.stride != 0
                                   ? source->header.stride
                                   : static_cast<size_t>(source->header.w) * BYTES_PER_PIXEL;
  if (source_stride % BYTES_PER_PIXEL != 0)
    return false;
  const int source_stride_pixels = static_cast<int>(source_stride / BYTES_PER_PIXEL);
  const size_t source_bytes = source_stride * static_cast<size_t>(source->header.h);
  const size_t required_scratch = static_cast<size_t>(source_width) * static_cast<size_t>(source_height) *
                                  BYTES_PER_PIXEL;
  if (source_stride_pixels < static_cast<int>(source->header.w) || scratch_size < required_scratch ||
      (reinterpret_cast<uintptr_t>(scratch) & 63U) != 0 || (required_scratch & 63U) != 0) {
    return false;
  }

  if (this->direct_image_synced_source_ != source->data || this->direct_image_synced_source_size_ != source_bytes) {
    const bool source_written_by_dma = esphome_artwork_image_buffer_written_by_dma != nullptr &&
                                       esphome_artwork_image_buffer_written_by_dma(source->data);
    if (!source_written_by_dma &&
        lvgl_cache_msync_external_result(source->data, source_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M) != ESP_OK) {
      return false;
    }
    this->direct_image_synced_source_ = source->data;
    this->direct_image_synced_source_size_ = source_bytes;
  }

  ppa_blend_oper_config_t blend{};
  blend.in_bg.buffer = source->data;
  blend.in_bg.pic_w = source_stride_pixels;
  blend.in_bg.pic_h = source->header.h;
  blend.in_bg.block_w = source_width;
  blend.in_bg.block_h = source_height;
  blend.in_bg.block_offset_x = source_x;
  blend.in_bg.block_offset_y = source_y;
  blend.in_bg.blend_cm = PPA_BLEND_COLOR_MODE_RGB888;
  blend.in_fg.buffer = source->data;
  blend.in_fg.pic_w = source_stride_pixels;
  blend.in_fg.pic_h = source->header.h;
  blend.in_fg.block_w = source_width;
  blend.in_fg.block_h = source_height;
  blend.in_fg.block_offset_x = source_x + (subpixel_vertical ? 0 : 1);
  blend.in_fg.block_offset_y = source_y + (subpixel_vertical ? 1 : 0);
  blend.in_fg.blend_cm = PPA_BLEND_COLOR_MODE_RGB888;
  blend.out.buffer = scratch;
  blend.out.buffer_size = required_scratch;
  blend.out.pic_w = source_width;
  blend.out.pic_h = source_height;
  blend.out.block_offset_x = 0;
  blend.out.block_offset_y = 0;
  blend.out.blend_cm = PPA_BLEND_COLOR_MODE_RGB888;
  blend.bg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
  blend.bg_alpha_fix_val = 0xFF;
  blend.fg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
  blend.fg_alpha_fix_val = subpixel_alpha;
  blend.mode = PPA_TRANS_MODE_NON_BLOCKING;

  DirectPpaFrameCompletion completion;
  completion.remaining.store(1, std::memory_order_release);
  completion.waiter = xTaskGetCurrentTaskHandle();
  blend.user_data = &completion;
  ulTaskNotifyTake(pdTRUE, 0);

  const int64_t started_us = esp_timer_get_time();
  if (esphome_mipi_dsi_wait_fifo_margin != nullptr) {
    esphome_mipi_dsi_wait_fifo_margin(CONFIG_ESPHOME_LVGL_PPA_SRM_TRANSFORM_DSI_FIFO_MIN,
                                      CONFIG_ESPHOME_LVGL_PPA_SRM_TRANSFORM_DSI_WAIT_US);
  }
  const uintptr_t source_begin = reinterpret_cast<uintptr_t>(source->data);
  const uintptr_t scratch_begin = reinterpret_cast<uintptr_t>(scratch);
  s_direct_ppa_source_begin.store(source_begin, std::memory_order_release);
  s_direct_ppa_source_end.store(source_begin + source_bytes, std::memory_order_release);
  s_direct_ppa_source2_begin.store(source_begin, std::memory_order_release);
  s_direct_ppa_source2_end.store(source_begin + source_bytes, std::memory_order_release);
  s_direct_ppa_target_begin.store(scratch_begin, std::memory_order_release);
  s_direct_ppa_target_end.store(scratch_begin + required_scratch, std::memory_order_release);

  const int64_t blend_started_us = esp_timer_get_time();
  const esp_err_t blend_result = ppa_do_blend(blend_client, &blend);
  if (blend_result == ESP_OK) {
    while (completion.remaining.load(std::memory_order_acquire) != 0)
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  }
  const uint32_t blend_us = static_cast<uint32_t>(esp_timer_get_time() - blend_started_us);
  s_direct_ppa_source_begin.store(0, std::memory_order_release);
  s_direct_ppa_source2_begin.store(0, std::memory_order_release);
  s_direct_ppa_target_begin.store(0, std::memory_order_release);
  s_direct_ppa_source_end.store(0, std::memory_order_relaxed);
  s_direct_ppa_source2_end.store(0, std::memory_order_relaxed);
  s_direct_ppa_target_end.store(0, std::memory_order_relaxed);
  if (blend_result != ESP_OK) {
    ESP_LOGW("lvgl.ken_burns.subpixel", "PPA scaled interpolation failed: %s", esp_err_to_name(blend_result));
    return false;
  }

  lv_image_dsc_t interpolated{};
  interpolated.header.cf = LV_COLOR_FORMAT_RGB888;
  interpolated.header.w = source_width;
  interpolated.header.h = source_height;
  interpolated.header.stride = static_cast<uint32_t>(source_width) * BYTES_PER_PIXEL;
  interpolated.data_size = required_scratch;
  interpolated.data = scratch;

  // The intermediate crop was produced by PPA and never touched by the CPU.
  // Mark it as synchronized only for the following SRM operation, then restore
  // the immutable decoded source as the cached owner for the next frame.
  this->direct_image_synced_source_ = scratch;
  this->direct_image_synced_source_size_ = required_scratch;
  const int64_t scale_started_us = esp_timer_get_time();
  const bool presented = this->direct_render_image_crop_(&interpolated, 0, 0, source_width, source_height,
                                                          srm_client, nullptr, 0, true, false);
  const uint32_t scale_us = static_cast<uint32_t>(esp_timer_get_time() - scale_started_us);
  this->direct_image_synced_source_ = source->data;
  this->direct_image_synced_source_size_ = source_bytes;

  if (presented && s_perf_logging_enabled) {
    static uint32_t frames = 0;
    static uint64_t total_us = 0;
    static uint64_t total_blend_us = 0;
    static uint64_t total_scale_us = 0;
    static uint32_t max_us = 0;
    static int64_t window_started_us = 0;
    const int64_t now_us = esp_timer_get_time();
    const uint32_t elapsed_us = static_cast<uint32_t>(now_us - started_us);
    frames++;
    total_us += elapsed_us;
    total_blend_us += blend_us;
    total_scale_us += scale_us;
    max_us = std::max(max_us, elapsed_us);
    if (window_started_us == 0)
      window_started_us = now_us;
    if (now_us - window_started_us >= 2000000LL) {
      ESP_LOGI("lvgl.ken_burns.scaled_subpixel",
               "perf2s: frames=%u avg=%uus max=%uus blend=%uus scale_present=%uus crop=%dx%d axis=%c alpha=%u",
               static_cast<unsigned>(frames),
               static_cast<unsigned>(total_us / std::max<uint32_t>(1, frames)),
               static_cast<unsigned>(max_us),
               static_cast<unsigned>(total_blend_us / std::max<uint32_t>(1, frames)),
               static_cast<unsigned>(total_scale_us / std::max<uint32_t>(1, frames)), source_width,
               source_height, subpixel_vertical ? 'y' : 'x', static_cast<unsigned>(subpixel_alpha));
      frames = 0;
      total_us = 0;
      total_blend_us = 0;
      total_scale_us = 0;
      max_us = 0;
      window_started_us = now_us;
    }
  }
  return presented;
}

bool LvglComponent::direct_render_image_crop_rgb888(const lv_image_dsc_t *source, int source_x, int source_y,
                                                    int source_width, int source_height, uint8_t *target,
                                                    size_t target_size, ppa_client_handle_t srm_client) {
  return this->direct_render_image_crop_(source, source_x, source_y, source_width, source_height, srm_client, target,
                                         target_size, false, false);
}

bool LvglComponent::direct_render_image_crop_rgb565(const lv_image_dsc_t *source, int source_x, int source_y,
                                                    int source_width, int source_height, uint8_t *target,
                                                    size_t target_size, ppa_client_handle_t srm_client) {
  return this->direct_render_image_crop_(source, source_x, source_y, source_width, source_height, srm_client, target,
                                         target_size, false, true);
}

bool LvglComponent::direct_render_image_crop_rgb888_bands(
    const lv_image_dsc_t *source, int source_x, int source_y, int source_width, int source_height,
    uint8_t *const *targets, const size_t *target_sizes, size_t target_count, size_t band_rows,
    ppa_client_handle_t srm_client) {
  if (!this->direct_image_animation_active_ || source == nullptr || source->data == nullptr || targets == nullptr ||
      target_sizes == nullptr || target_count == 0 || band_rows == 0 || srm_client == nullptr || this->width_ <= 0 ||
      this->height_ <= 0) {
    return false;
  }

  ppa_srm_color_mode_t source_mode;
  size_t source_bpp;
  switch (static_cast<lv_color_format_t>(source->header.cf)) {
    case LV_COLOR_FORMAT_RGB565:
      source_mode = PPA_SRM_COLOR_MODE_RGB565;
      source_bpp = 2;
      break;
    case LV_COLOR_FORMAT_RGB888:
      source_mode = PPA_SRM_COLOR_MODE_RGB888;
      source_bpp = 3;
      break;
    default:
      return false;
  }

  const size_t source_stride =
      source->header.stride != 0 ? source->header.stride : static_cast<size_t>(source->header.w) * source_bpp;
  if (source_stride % source_bpp != 0)
    return false;
  const int source_stride_pixels = static_cast<int>(source_stride / source_bpp);
  if (source_stride_pixels < static_cast<int>(source->header.w) ||
      !this->direct_resolve_image_crop(source, &source_x, &source_y, &source_width, &source_height)) {
    return false;
  }

  const uint32_t scale_q16 =
      (static_cast<uint32_t>(this->width_) * 16U + static_cast<uint32_t>(source_width) - 1U) /
      static_cast<uint32_t>(source_width);
  if (scale_q16 == 0 || static_cast<uint32_t>(source_width) * scale_q16 / 16U !=
                            static_cast<uint32_t>(this->width_) ||
      static_cast<uint32_t>(source_height) * scale_q16 / 16U != static_cast<uint32_t>(this->height_)) {
    return false;
  }

  const size_t expected_count =
      (static_cast<size_t>(this->height_) + band_rows - 1U) / band_rows;
  if (target_count != expected_count)
    return false;

  const size_t source_bytes = source_stride * static_cast<size_t>(source->header.h);
  if (this->direct_image_synced_source_ != source->data || this->direct_image_synced_source_size_ != source_bytes) {
    const bool source_written_by_dma = esphome_artwork_image_buffer_written_by_dma != nullptr &&
                                       esphome_artwork_image_buffer_written_by_dma(source->data);
    if (!source_written_by_dma &&
        lvgl_cache_msync_external_result(source->data, source_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M) != ESP_OK) {
      return false;
    }
    this->direct_image_synced_source_ = source->data;
    this->direct_image_synced_source_size_ = source_bytes;
  }

  const size_t row_bytes = static_cast<size_t>(this->width_) * 3U;
  const uintptr_t source_begin = reinterpret_cast<uintptr_t>(source->data);
  for (size_t band_index = 0, output_y = 0; band_index < target_count; band_index++) {
    const size_t output_h = std::min(band_rows, static_cast<size_t>(this->height_) - output_y);
    const size_t output_bytes = row_bytes * output_h;
    uint8_t *target = targets[band_index];
    if (target == nullptr || target_sizes[band_index] < output_bytes ||
        (reinterpret_cast<uintptr_t>(target) & 63U) != 0) {
      return false;
    }

    const uint32_t output_begin_q16 = static_cast<uint32_t>(output_y) * 16U;
    const uint32_t output_end_q16 = static_cast<uint32_t>(output_y + output_h) * 16U;
    if (output_begin_q16 % scale_q16 != 0 || output_end_q16 % scale_q16 != 0)
      return false;
    const uint32_t input_y = output_begin_q16 / scale_q16;
    const uint32_t input_end = output_end_q16 / scale_q16;
    if (input_end <= input_y || input_end > static_cast<uint32_t>(source_height))
      return false;

    ppa_srm_oper_config_t config{};
    config.in.buffer = const_cast<uint8_t *>(source->data);
    config.in.pic_w = source_stride_pixels;
    config.in.pic_h = source->header.h;
    config.in.block_w = source_width;
    config.in.block_h = input_end - input_y;
    config.in.block_offset_x = source_x;
    config.in.block_offset_y = source_y + input_y;
    config.in.srm_cm = source_mode;
    config.out.buffer = target;
    config.out.buffer_size = target_sizes[band_index];
    config.out.pic_w = this->width_;
    config.out.pic_h = output_h;
    config.out.block_offset_x = 0;
    config.out.block_offset_y = 0;
    config.out.srm_cm = PPA_SRM_COLOR_MODE_RGB888;
    config.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
    config.scale_x = static_cast<float>(scale_q16) / 16.0f;
    config.scale_y = static_cast<float>(scale_q16) / 16.0f;
    config.alpha_update_mode = PPA_ALPHA_NO_CHANGE;
    config.mode = PPA_TRANS_MODE_NON_BLOCKING;

    DirectPpaFrameCompletion completion;
    completion.remaining.store(1, std::memory_order_release);
    completion.waiter = xTaskGetCurrentTaskHandle();
    config.user_data = &completion;
    ulTaskNotifyTake(pdTRUE, 0);

    const uintptr_t target_begin = reinterpret_cast<uintptr_t>(target);
    s_direct_ppa_source_begin.store(source_begin, std::memory_order_release);
    s_direct_ppa_source_end.store(source_begin + source_bytes, std::memory_order_release);
    s_direct_ppa_target_begin.store(target_begin, std::memory_order_release);
    s_direct_ppa_target_end.store(target_begin + output_bytes, std::memory_order_release);
    if (esphome_mipi_dsi_wait_fifo_margin != nullptr) {
      esphome_mipi_dsi_wait_fifo_margin(CONFIG_ESPHOME_LVGL_PPA_SRM_TRANSFORM_DSI_FIFO_MIN,
                                        CONFIG_ESPHOME_LVGL_PPA_SRM_TRANSFORM_DSI_WAIT_US);
    }
    const esp_err_t result = ppa_do_scale_rotate_mirror(srm_client, &config);
    if (result == ESP_OK) {
      while (completion.remaining.load(std::memory_order_acquire) != 0)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
    s_direct_ppa_source_begin.store(0, std::memory_order_release);
    s_direct_ppa_target_begin.store(0, std::memory_order_release);
    s_direct_ppa_source_end.store(0, std::memory_order_relaxed);
    s_direct_ppa_target_end.store(0, std::memory_order_relaxed);
    if (result != ESP_OK) {
      ESP_LOGW(TAG, "PPA segmented image prepare failed at band %u: %s",
               static_cast<unsigned>(band_index), esp_err_to_name(result));
      return false;
    }
    output_y += output_h;
  }
  return true;
}

bool LvglComponent::direct_render_image_crop_(const lv_image_dsc_t *source, int source_x, int source_y,
                                              int source_width, int source_height, ppa_client_handle_t srm_client,
                                              uint8_t *target_override, size_t target_override_size, bool present,
                                              bool output_rgb565) {
#if LV_COLOR_DEPTH == 32 && defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA)
  if (!this->direct_image_animation_active_ || srm_client == nullptr || source == nullptr || source->data == nullptr ||
      source_width < 2 || source_height < 2 || source_x < 0 || source_y < 0 ||
      source_x + source_width > static_cast<int>(source->header.w) ||
      source_y + source_height > static_cast<int>(source->header.h)) {
    return false;
  }

  // The animation client queues all bands of a frame and signals the worker
  // only after the final DMA2D transaction. This removes one scheduler round
  // trip per band while keeping LVGL's draw-unit client independent.
  ppa_client_handle_t operation_client = srm_client;

  ppa_srm_color_mode_t source_mode;
  size_t source_bpp;
  switch (static_cast<lv_color_format_t>(source->header.cf)) {
    case LV_COLOR_FORMAT_RGB565:
      source_mode = PPA_SRM_COLOR_MODE_RGB565;
      source_bpp = 2;
      break;
    case LV_COLOR_FORMAT_RGB888:
      source_mode = PPA_SRM_COLOR_MODE_RGB888;
      source_bpp = 3;
      break;
    default:
      return false;
  }

  const size_t source_stride =
      source->header.stride != 0 ? source->header.stride : static_cast<size_t>(source->header.w) * source_bpp;
  if (source_stride % source_bpp != 0)
    return false;
  const int source_stride_pixels = static_cast<int>(source_stride / source_bpp);
  if (source_stride_pixels < static_cast<int>(source->header.w))
    return false;

  const size_t source_bytes = source_stride * static_cast<size_t>(source->header.h);
  if (this->direct_image_synced_source_ != source->data || this->direct_image_synced_source_size_ != source_bytes) {
    const bool source_written_by_dma = esphome_artwork_image_buffer_written_by_dma != nullptr &&
                                       esphome_artwork_image_buffer_written_by_dma(source->data);
    if (!source_written_by_dma &&
        lvgl_cache_msync_external_result(source->data, source_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M) != ESP_OK) {
      return false;
    }
    this->direct_image_synced_source_ = source->data;
    this->direct_image_synced_source_size_ = source_bytes;
  }

  auto *mipi_display = static_cast<mipi_dsi::MipiDsi *>(this->displays_[0]);
  if (mipi_display == nullptr)
    return false;

  if (present && output_rgb565)
    return false;
  const size_t output_bpp = output_rgb565 ? 2U : 3U;
  const ppa_srm_color_mode_t output_mode = output_rgb565 ? PPA_SRM_COLOR_MODE_RGB565 : PPA_SRM_COLOR_MODE_RGB888;
  const size_t output_stride = static_cast<size_t>(this->width_) * output_bpp;
  const size_t output_bytes = output_stride * static_cast<size_t>(this->height_);
  uint8_t *target = target_override != nullptr ? target_override : mipi_display->get_direct_render_frame_buffer();
  const size_t target_capacity =
      target_override != nullptr ? target_override_size : mipi_display->get_frame_buffer_size();
  if (target == nullptr || target_capacity < output_bytes || (reinterpret_cast<uintptr_t>(target) & 63U) != 0 ||
      (target_capacity & 63U) != 0)
    return false;

  size_t target_index = 0;
  if (present) {
    if (target == mipi_display->get_frame_buffer(0)) {
      target_index = 0;
    } else if (target == mipi_display->get_frame_buffer(1)) {
      target_index = 1;
    } else if (target == mipi_display->get_frame_buffer(2)) {
      target_index = 2;
    } else {
      return false;
    }
    if (!this->prepare_direct_framebuffer_dma_ownership_(target, target_index))
      return false;
  }

  /* ESP32-P4 PPA stores each scale factor with four fractional bits. Passing
   * the ideal floating-point scale makes the driver's bounds check and the
   * hardware disagree at band boundaries. Pick the largest centered crop
   * whose quantized scale produces the exact display size instead. Photos use
   * a square cover crop here, so one plan preserves their aspect ratio. */
  if (!this->direct_resolve_image_crop(source, &source_x, &source_y, &source_width, &source_height))
    return false;
  const uint32_t scale_q16 =
      (static_cast<uint32_t>(this->width_) * 16U + static_cast<uint32_t>(source_width) - 1U) /
      static_cast<uint32_t>(source_width);
  const float scale = static_cast<float>(scale_q16) / 16.0f;
  const int64_t started_us = esp_timer_get_time();

  ppa_srm_oper_config_t config{};
  config.in.buffer = const_cast<uint8_t *>(source->data);
  config.in.pic_w = source_stride_pixels;
  config.in.pic_h = source->header.h;
  config.in.block_w = source_width;
  config.in.block_h = source_height;
  config.in.block_offset_x = source_x;
  config.in.block_offset_y = source_y;
  config.in.srm_cm = source_mode;
  config.out.buffer = target;
  config.out.buffer_size = output_bytes;
  config.out.pic_w = this->width_;
  config.out.pic_h = this->height_;
  config.out.block_offset_x = 0;
  config.out.block_offset_y = 0;
  config.out.srm_cm = output_mode;
  config.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
  config.scale_x = scale;
  config.scale_y = scale;
  config.alpha_update_mode = PPA_ALPHA_NO_CHANGE;
  constexpr bool serialize_operations = false;
  config.mode = PPA_TRANS_MODE_NON_BLOCKING;

  /* A single 800x800 SRM transaction can monopolize the shared PSRAM/DMA
   * fabric long enough to trip the P4 watchdog. Split the frame into vertical
   * strips while retaining the same bounded transaction size.
   *
   * Every SRM transaction starts with scaler phase zero. Boundaries therefore
   * have to be chosen where output_x * 16 / scale_q16 is integral; otherwise a
   * one-pixel interpolation phase change appears as a moving seam.
   *
   * ESP32-P4 SRM also corrupts rows near the end of a transform when a partial
   * input macroblock shares one operation with complete 16-row macroblocks.
   * Submit that tail as a separate operation. The preceding height is a
   * multiple of 16, so both operations begin at an exact scaler phase and the
   * split does not introduce a visible horizontal seam. */
  struct TransformStrip {
    uint16_t output_x;
    uint16_t output_w;
    uint16_t input_x;
    uint16_t input_w;
  };
  constexpr size_t MAX_STRIPS = 12;
  std::array<TransformStrip, MAX_STRIPS> strips{};
  size_t planned_strip_count = 0;

  // The current ESP-IDF SRM driver performs its own macro-block split. A
  // single full-frame operation avoids resetting the scaler phase at strip
  // boundaries; retain bounded strips as a compatibility fallback.
  constexpr uint32_t DIRECT_TRANSFORM_MAX_STRIP_WIDTH = 220;
  const bool full_frame_operation = CONFIG_ESPHOME_LVGL_PPA_DIRECT_FULL_FRAME;
  const uint32_t maximum_output_strip = full_frame_operation
                                            ? static_cast<uint32_t>(this->width_)
                                            : DIRECT_TRANSFORM_MAX_STRIP_WIDTH;
  const uint32_t requested_output_strip = full_frame_operation
                                              ? static_cast<uint32_t>(this->width_)
                                              : std::clamp<uint32_t>(
                                                    std::min<uint32_t>(
                                                        CONFIG_ESPHOME_LVGL_PPA_SRM_TRANSFORM_BAND_HEIGHT,
                                                        maximum_output_strip),
                                                    1, static_cast<uint32_t>(this->width_));

  const uint32_t scale_phase_period = scale_q16 / std::gcd(scale_q16, 16U);
  const uint32_t boundary_alignment = scale_phase_period;
  const size_t minimum_strip_count =
      (static_cast<size_t>(this->width_) + requested_output_strip - 1U) / requested_output_strip;
  for (size_t count = std::max<size_t>(1, minimum_strip_count); count <= MAX_STRIPS; count++) {
    std::array<uint32_t, MAX_STRIPS + 1> output_boundaries{};
    output_boundaries[0] = 0;
    output_boundaries[count] = static_cast<uint32_t>(this->width_);
    uint32_t previous_output_x = 0;
    bool valid = true;
    for (size_t index = 1; index < count; index++) {
      const size_t remaining_strips = count - index;
      const uint32_t future_capacity = static_cast<uint32_t>(remaining_strips) * requested_output_strip;
      const uint32_t capacity_lower = future_capacity >= static_cast<uint32_t>(this->width_)
                                          ? 0U
                                          : static_cast<uint32_t>(this->width_) - future_capacity;
      const uint32_t lower = std::max<uint32_t>(previous_output_x + boundary_alignment, capacity_lower);
      const uint32_t upper =
          std::min<uint32_t>(previous_output_x + requested_output_strip,
                             static_cast<uint32_t>(this->width_) - static_cast<uint32_t>(remaining_strips));
      const uint32_t first = ((lower + boundary_alignment - 1U) / boundary_alignment) * boundary_alignment;
      const uint32_t last = (upper / boundary_alignment) * boundary_alignment;
      if (first > last) {
        valid = false;
        break;
      }

      const uint32_t ideal = static_cast<uint32_t>((static_cast<uint64_t>(this->width_) * index + count / 2U) / count);
      uint32_t boundary = ((ideal + boundary_alignment / 2U) / boundary_alignment) * boundary_alignment;
      boundary = std::clamp(boundary, first, last);
      if ((boundary * 16U) % scale_q16 != 0) {
        valid = false;
        break;
      }
      output_boundaries[index] = boundary;
      previous_output_x = boundary;
    }
    if (!valid)
      continue;

    previous_output_x = 0;
    uint32_t previous_input_x = 0;
    for (size_t index = 0; index < count; index++) {
      const uint32_t output_end = output_boundaries[index + 1U];
      const uint32_t input_end =
          index + 1U == count ? static_cast<uint32_t>(source_width) : output_end * 16U / scale_q16;
      const uint32_t output_w = output_end - previous_output_x;
      const uint32_t input_w = input_end - previous_input_x;
      if (output_w == 0 || output_w > requested_output_strip || input_w == 0 ||
          input_w * scale_q16 / 16U != output_w) {
        valid = false;
        break;
      }
      strips[index] = {static_cast<uint16_t>(previous_output_x), static_cast<uint16_t>(output_w),
                       static_cast<uint16_t>(previous_input_x), static_cast<uint16_t>(input_w)};
      previous_output_x = output_end;
      previous_input_x = input_end;
    }
    if (valid && previous_output_x == static_cast<uint32_t>(this->width_) &&
        previous_input_x == static_cast<uint32_t>(source_width)) {
      planned_strip_count = count;
      break;
    }
  }
  if (planned_strip_count == 0)
    return false;

  struct TransformBand {
    uint16_t output_y;
    uint16_t output_h;
    uint16_t input_y;
    uint16_t input_h;
  };
  std::array<TransformBand, 2> bands{};
  size_t planned_band_count = 1;
  const uint32_t complete_input_h = static_cast<uint32_t>(source_height) & ~15U;
  const uint32_t tail_input_h = static_cast<uint32_t>(source_height) - complete_input_h;
  if (tail_input_h != 0 && complete_input_h != 0) {
    const uint32_t complete_output_h = complete_input_h * scale_q16 / 16U;
    const uint32_t tail_output_h = static_cast<uint32_t>(this->height_) - complete_output_h;
    if (complete_output_h == 0 || tail_output_h == 0 ||
        tail_input_h * scale_q16 / 16U != tail_output_h) {
      return false;
    }
    bands[0] = {0, static_cast<uint16_t>(complete_output_h), 0, static_cast<uint16_t>(complete_input_h)};
    bands[1] = {static_cast<uint16_t>(complete_output_h), static_cast<uint16_t>(tail_output_h),
                static_cast<uint16_t>(complete_input_h), static_cast<uint16_t>(tail_input_h)};
    planned_band_count = 2;
  } else {
    bands[0] = {0, static_cast<uint16_t>(this->height_), 0, static_cast<uint16_t>(source_height)};
  }

  uint32_t output_x = 0;
  uint32_t operation_us = 0;
  uint32_t strip_count = 0;
  esp_err_t result = ESP_OK;

  const uintptr_t source_begin = reinterpret_cast<uintptr_t>(source->data);
  const uintptr_t source_end = source_begin + source_stride * static_cast<size_t>(source->header.h);
  const uintptr_t target_begin = reinterpret_cast<uintptr_t>(target);
  const uintptr_t target_end = target_begin + output_bytes;
  /* The source was written back once above and remains immutable throughout
   * the frame. The target is the idle DSI framebuffer and every output pixel
   * is replaced by these bands before it is presented. Let the patched PPA
   * driver skip its per-band cache operations for exactly those two owned
   * ranges. On ESP32-P4 a full external-memory esp_cache_msync() from the PPA
   * task can make the other core fault while it executes mapped audio/WiFi
   * code; repeating it for every band is also redundant. */
  s_direct_ppa_source_begin.store(source_begin, std::memory_order_release);
  s_direct_ppa_source_end.store(source_end, std::memory_order_release);
  s_direct_ppa_target_begin.store(target_begin, std::memory_order_release);
  s_direct_ppa_target_end.store(target_end, std::memory_order_release);

  DirectPpaFrameCompletion completion;
  const size_t planned_operation_count = planned_strip_count * planned_band_count;
  if (!serialize_operations) {
    completion.remaining.store(static_cast<uint32_t>(planned_operation_count), std::memory_order_release);
    completion.waiter = xTaskGetCurrentTaskHandle();
    ulTaskNotifyTake(pdTRUE, 0);
  }
  const int64_t fifo_wait_started_us = esp_timer_get_time();
  if (esphome_mipi_dsi_wait_fifo_margin != nullptr) {
    esphome_mipi_dsi_wait_fifo_margin(CONFIG_ESPHOME_LVGL_PPA_SRM_TRANSFORM_DSI_FIFO_MIN,
                                      CONFIG_ESPHOME_LVGL_PPA_SRM_TRANSFORM_DSI_WAIT_US);
  }
  const uint32_t fifo_wait_us = static_cast<uint32_t>(esp_timer_get_time() - fifo_wait_started_us);

  const int64_t ppa_frame_started_us = esp_timer_get_time();
  auto *active_before = mipi_display->get_presented_frame_buffer();
  size_t submitted_operations = 0;
  for (size_t strip_index = 0; strip_index < planned_strip_count; strip_index++) {
    if (strips[strip_index].output_x != output_x) {
      result = ESP_ERR_INVALID_STATE;
      break;
    }
    const uint32_t this_input_w = strips[strip_index].input_w;
    const uint32_t this_output_w = strips[strip_index].output_w;
    if (this_output_w == 0 || output_x + this_output_w > static_cast<uint32_t>(this->width_) ||
        this_input_w > static_cast<uint32_t>(source_width)) {
      result = ESP_ERR_INVALID_SIZE;
      break;
    }

    const uint32_t mapped_source_x = strips[strip_index].input_x;

    for (size_t band_index = 0; band_index < planned_band_count; band_index++) {
      const auto &band = bands[band_index];
      config.in.buffer = const_cast<uint8_t *>(source->data);
      config.in.block_w = this_input_w;
      config.in.pic_h = source->header.h;
      config.in.block_h = band.input_h;
      config.in.block_offset_x = source_x + mapped_source_x;
      config.in.block_offset_y = source_y + band.input_y;
      /* Keep the DMA2D/PPA picture anchored at the real framebuffer base and
       * select the destination band with block offsets. Although the IDF
       * argument validator accepts an aligned pointer into a framebuffer as a
       * standalone picture, ESP32-P4 SRM transactions using those interior
       * bases can overrun/corrupt unrelated DMA users. */
      config.out.buffer = target;
      config.out.buffer_size = output_bytes;
      config.out.pic_h = this->height_;
      config.out.block_offset_x = output_x;
      config.out.block_offset_y = band.output_y;
      config.user_data = serialize_operations ? nullptr : &completion;

      const int64_t operation_started_us = esp_timer_get_time();
      result = ppa_do_scale_rotate_mirror(operation_client, &config);
      const uint32_t this_operation_us = static_cast<uint32_t>(esp_timer_get_time() - operation_started_us);
      operation_us = std::max(operation_us, this_operation_us);
      if (result != ESP_OK) {
        if (!serialize_operations) {
          const uint32_t unsubmitted = static_cast<uint32_t>(planned_operation_count - submitted_operations);
          completion.remaining.fetch_sub(unsubmitted, std::memory_order_acq_rel);
        }
        break;
      }
      submitted_operations++;

#if CONFIG_ESPHOME_LVGL_PPA_SRM_TRANSFORM_BAND_GAP_US > 0
      esp_rom_delay_us(CONFIG_ESPHOME_LVGL_PPA_SRM_TRANSFORM_BAND_GAP_US);
#endif
    }
    if (result != ESP_OK)
      break;

    output_x += this_output_w;
    strip_count++;
  }

  const uint32_t queue_us = static_cast<uint32_t>(esp_timer_get_time() - ppa_frame_started_us);
  if (!serialize_operations) {
    while (completion.remaining.load(std::memory_order_acquire) != 0) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
  }
  const uint32_t ppa_frame_us = static_cast<uint32_t>(esp_timer_get_time() - ppa_frame_started_us);
  s_direct_ppa_source_begin.store(0, std::memory_order_release);
  s_direct_ppa_target_begin.store(0, std::memory_order_release);
  s_direct_ppa_source_end.store(0, std::memory_order_relaxed);
  s_direct_ppa_target_end.store(0, std::memory_order_relaxed);

  if (result != ESP_OK || output_x != static_cast<uint32_t>(this->width_)) {
    static uint32_t failure_count = 0;
    if (failure_count < 8) {
      ESP_LOGW(TAG, "Direct image PPA crop failed: %s crop=%d q16=%u strips=%u bands=%u columns=%u/%d",
               esp_err_to_name(result), source_width, static_cast<unsigned>(scale_q16),
               static_cast<unsigned>(strip_count), static_cast<unsigned>(planned_band_count),
               static_cast<unsigned>(output_x), this->width_);
      failure_count++;
    }
    return false;
  }

  uint32_t present_us = 0;
  if (present) {
    // PPA has now replaced the complete DMA-owned target frame.
    this->direct_image_framebuffer_dma_owned_[target_index] = true;
    const int64_t present_started_us = esp_timer_get_time();
    // Animation frames may replace a queued-but-not-yet-visible frame. Waiting
    // for scanout here costs up to one display period and needlessly lowers
    // the compositor frame rate; the driver's triple-buffer ownership still
    // prevents rendering into the active framebuffer.
    if (!mipi_display->queue_direct_frame_buffer(target, 50, false))
      return false;
    present_us = static_cast<uint32_t>(esp_timer_get_time() - present_started_us);
    this->direct_last_flushed_buf_ = target;
  }

  if (present && s_perf_logging_enabled) {
    static uint32_t frames = 0;
    static uint64_t total_us = 0;
    static uint32_t max_us = 0;
    static uint32_t max_band = 0;
    static uint64_t total_fifo_us = 0;
    static uint64_t total_queue_us = 0;
    static uint64_t total_ppa_frame_us = 0;
    static uint64_t total_present_us = 0;
    static uint32_t slow_frames = 0;
    static int64_t window_us = 0;
    const int64_t now_us = esp_timer_get_time();
    const uint32_t elapsed_us = static_cast<uint32_t>(now_us - started_us);
    frames++;
    total_us += elapsed_us;
    max_us = std::max(max_us, elapsed_us);
    max_band = std::max(max_band, operation_us);
    total_fifo_us += fifo_wait_us;
    total_queue_us += queue_us;
    total_ppa_frame_us += ppa_frame_us;
    total_present_us += present_us;
    if (ppa_frame_us >= 45000U)
      slow_frames++;
    if (window_us == 0)
      window_us = now_us;
    if (now_us - window_us >= 2000000) {
      ESP_LOGI("lvgl.ken_burns",
               "perf2s: frames=%u slow=%u avg=%uus max=%uus fifo=%uus queue=%uus ppa=%uus present=%uus "
               "max_submit=%uus ops=%u crop=%dx%d q16=%u",
               static_cast<unsigned>(frames), static_cast<unsigned>(slow_frames),
               static_cast<unsigned>(total_us / std::max<uint32_t>(1, frames)), static_cast<unsigned>(max_us),
               static_cast<unsigned>(total_fifo_us / std::max<uint32_t>(1, frames)),
               static_cast<unsigned>(total_queue_us / std::max<uint32_t>(1, frames)),
               static_cast<unsigned>(total_ppa_frame_us / std::max<uint32_t>(1, frames)),
               static_cast<unsigned>(total_present_us / std::max<uint32_t>(1, frames)), static_cast<unsigned>(max_band),
               static_cast<unsigned>(strip_count * planned_band_count), source_width, source_height,
               static_cast<unsigned>(scale_q16));
      frames = 0;
      total_us = 0;
      max_us = 0;
      max_band = 0;
      total_fifo_us = 0;
      total_queue_us = 0;
      total_ppa_frame_us = 0;
      total_present_us = 0;
      slow_frames = 0;
      window_us = now_us;
    }
  }
  return true;
#else
  return false;
#endif
}

bool LvglComponent::direct_present_rgb565_crossfade(const uint8_t *background, const uint8_t *foreground,
                                                    uint8_t opacity, ppa_client_handle_t blend_client) {
  return this->direct_present_crossfade_(background, foreground, opacity, blend_client, true, true);
}

bool LvglComponent::direct_present_rgb888_crossfade(const uint8_t *background, const uint8_t *foreground,
                                                    uint8_t opacity, ppa_client_handle_t blend_client) {
  return this->direct_present_crossfade_(background, foreground, opacity, blend_client, false, false);
}

bool LvglComponent::direct_present_rgb888_rgb565_crossfade(const uint8_t *background, const uint8_t *foreground,
                                                           uint8_t opacity, ppa_client_handle_t blend_client) {
  return this->direct_present_crossfade_(background, foreground, opacity, blend_client, false, true);
}

bool LvglComponent::direct_present_rgb888_crossfade_bands(
    const uint8_t *background, uint8_t *const *foreground_bands,
    const size_t *foreground_band_sizes, size_t band_count, size_t band_rows, uint8_t opacity,
    ppa_client_handle_t blend_client) {
  if (!this->direct_image_animation_active_ || background == nullptr || foreground_bands == nullptr ||
      foreground_band_sizes == nullptr || band_count == 0 || band_rows == 0 || blend_client == nullptr ||
      this->displays_.size() != 1 || this->width_ <= 0 || this->height_ <= 0) {
    return false;
  }

  const size_t expected_count =
      (static_cast<size_t>(this->height_) + band_rows - 1U) / band_rows;
  if (band_count != expected_count)
    return false;

  auto *mipi_display = static_cast<mipi_dsi::MipiDsi *>(this->displays_[0]);
  if (mipi_display == nullptr || !mipi_display->wait_for_direct_frame_queue_idle(25))
    return false;
  uint8_t *target = mipi_display->get_direct_render_frame_buffer(background);
  if (target == nullptr)
    return false;

  size_t target_index;
  if (target == mipi_display->get_frame_buffer(0)) {
    target_index = 0;
  } else if (target == mipi_display->get_frame_buffer(1)) {
    target_index = 1;
  } else if (target == mipi_display->get_frame_buffer(2)) {
    target_index = 2;
  } else {
    return false;
  }
  if (!this->prepare_direct_framebuffer_dma_ownership_(target, target_index))
    return false;

  const size_t row_bytes = static_cast<size_t>(this->width_) * 3U;
  const size_t frame_bytes = row_bytes * static_cast<size_t>(this->height_);
  if (mipi_display->get_frame_buffer_size() < frame_bytes)
    return false;

  const uintptr_t background_begin = reinterpret_cast<uintptr_t>(background);
  const uintptr_t target_begin = reinterpret_cast<uintptr_t>(target);
  if (esphome_mipi_dsi_wait_fifo_margin != nullptr) {
    esphome_mipi_dsi_wait_fifo_margin(CONFIG_ESPHOME_LVGL_PPA_SRM_TRANSFORM_DSI_FIFO_MIN,
                                      CONFIG_ESPHOME_LVGL_PPA_SRM_TRANSFORM_DSI_WAIT_US);
  }

  for (size_t band_index = 0, output_y = 0; band_index < band_count; band_index++) {
    const size_t output_h = std::min(band_rows, static_cast<size_t>(this->height_) - output_y);
    const size_t band_bytes = row_bytes * output_h;
    const uint8_t *foreground = foreground_bands[band_index];
    if (foreground == nullptr || foreground_band_sizes[band_index] < band_bytes ||
        (reinterpret_cast<uintptr_t>(foreground) & 63U) != 0) {
      return false;
    }

    ppa_blend_oper_config_t config{};
    config.in_bg.buffer = background;
    config.in_bg.pic_w = this->width_;
    config.in_bg.pic_h = this->height_;
    config.in_bg.block_w = this->width_;
    config.in_bg.block_h = output_h;
    config.in_bg.block_offset_x = 0;
    config.in_bg.block_offset_y = output_y;
    config.in_bg.blend_cm = PPA_BLEND_COLOR_MODE_RGB888;
    config.in_fg.buffer = const_cast<uint8_t *>(foreground);
    config.in_fg.pic_w = this->width_;
    config.in_fg.pic_h = output_h;
    config.in_fg.block_w = this->width_;
    config.in_fg.block_h = output_h;
    config.in_fg.block_offset_x = 0;
    config.in_fg.block_offset_y = 0;
    config.in_fg.blend_cm = PPA_BLEND_COLOR_MODE_RGB888;
    config.out.buffer = target;
    config.out.buffer_size = frame_bytes;
    config.out.pic_w = this->width_;
    config.out.pic_h = this->height_;
    config.out.block_offset_x = 0;
    config.out.block_offset_y = output_y;
    config.out.blend_cm = PPA_BLEND_COLOR_MODE_RGB888;
    config.bg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
    config.bg_alpha_fix_val = 0xFF;
    config.fg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
    config.fg_alpha_fix_val = opacity;
    config.mode = PPA_TRANS_MODE_NON_BLOCKING;

    DirectPpaFrameCompletion completion;
    completion.remaining.store(1, std::memory_order_release);
    completion.waiter = xTaskGetCurrentTaskHandle();
    config.user_data = &completion;
    ulTaskNotifyTake(pdTRUE, 0);

    const uintptr_t foreground_begin = reinterpret_cast<uintptr_t>(foreground);
    s_direct_ppa_source_begin.store(background_begin, std::memory_order_release);
    s_direct_ppa_source_end.store(background_begin + frame_bytes, std::memory_order_release);
    s_direct_ppa_source2_begin.store(foreground_begin, std::memory_order_release);
    s_direct_ppa_source2_end.store(foreground_begin + band_bytes, std::memory_order_release);
    s_direct_ppa_target_begin.store(target_begin, std::memory_order_release);
    s_direct_ppa_target_end.store(target_begin + frame_bytes, std::memory_order_release);
    const esp_err_t result = ppa_do_blend(blend_client, &config);
    if (result == ESP_OK) {
      while (completion.remaining.load(std::memory_order_acquire) != 0)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
    s_direct_ppa_source_begin.store(0, std::memory_order_release);
    s_direct_ppa_source2_begin.store(0, std::memory_order_release);
    s_direct_ppa_target_begin.store(0, std::memory_order_release);
    s_direct_ppa_source_end.store(0, std::memory_order_relaxed);
    s_direct_ppa_source2_end.store(0, std::memory_order_relaxed);
    s_direct_ppa_target_end.store(0, std::memory_order_relaxed);
    if (result != ESP_OK) {
      ESP_LOGW(TAG, "PPA segmented crossfade failed at band %u: %s",
               static_cast<unsigned>(band_index), esp_err_to_name(result));
      return false;
    }
    output_y += output_h;
  }

  this->direct_image_framebuffer_dma_owned_[target_index] = true;
  if (!mipi_display->queue_direct_frame_buffer(target, 50))
    return false;
  this->direct_last_flushed_buf_ = target;
  return true;
}

bool LvglComponent::direct_present_image_crop_rgb888_crossfade_banded(
    const lv_image_dsc_t *source, int source_x, int source_y, int source_width, int source_height,
    const uint8_t *background, uint8_t opacity, uint8_t *scratch, size_t scratch_size,
    ppa_client_handle_t srm_client, ppa_client_handle_t blend_client) {
  return this->direct_present_rgb888_crossfade_banded_(source, source_x, source_y, source_width, source_height,
                                                       background, opacity, scratch, scratch_size, srm_client,
                                                       blend_client, true);
}

bool LvglComponent::direct_present_rgb888_solid_crossfade_banded(const uint8_t *background, uint8_t opacity,
                                                                 const uint8_t *solid_band,
                                                                 size_t solid_band_size,
                                                                 ppa_client_handle_t blend_client) {
  return this->direct_present_rgb888_crossfade_banded_(nullptr, 0, 0, 0, 0, background, opacity,
                                                       const_cast<uint8_t *>(solid_band), solid_band_size, nullptr,
                                                       blend_client, false);
}

bool LvglComponent::direct_present_rgb888_crossfade_banded_(
    const lv_image_dsc_t *source, int source_x, int source_y, int source_width, int source_height,
    const uint8_t *background, uint8_t opacity, uint8_t *scratch, size_t scratch_size,
    ppa_client_handle_t srm_client, ppa_client_handle_t blend_client, bool render_source) {
  if (!this->direct_image_animation_active_ || background == nullptr || scratch == nullptr || blend_client == nullptr ||
      this->displays_.size() != 1 || this->width_ <= 0 || this->height_ <= 0 ||
      (reinterpret_cast<uintptr_t>(scratch) & 63U) != 0 || (scratch_size & 63U) != 0) {
    return false;
  }
  if (render_source && (source == nullptr || source->data == nullptr || srm_client == nullptr))
    return false;

  constexpr size_t PIXEL_SIZE = 3;
  const size_t row_bytes = static_cast<size_t>(this->width_) * PIXEL_SIZE;
  const size_t maximum_band_rows = scratch_size / row_bytes;
  if (maximum_band_rows == 0)
    return false;

  ppa_srm_color_mode_t source_mode = PPA_SRM_COLOR_MODE_RGB888;
  size_t source_bpp = 0;
  size_t source_stride = 0;
  int source_stride_pixels = 0;
  uint32_t scale_q16 = 16;
  uint32_t band_alignment = 1;
  if (render_source) {
    switch (static_cast<lv_color_format_t>(source->header.cf)) {
      case LV_COLOR_FORMAT_RGB565:
        source_mode = PPA_SRM_COLOR_MODE_RGB565;
        source_bpp = 2;
        break;
      case LV_COLOR_FORMAT_RGB888:
        source_mode = PPA_SRM_COLOR_MODE_RGB888;
        source_bpp = 3;
        break;
      default:
        return false;
    }
    source_stride =
        source->header.stride != 0 ? source->header.stride : static_cast<size_t>(source->header.w) * source_bpp;
    if (source_stride % source_bpp != 0)
      return false;
    source_stride_pixels = static_cast<int>(source_stride / source_bpp);
    if (source_stride_pixels < static_cast<int>(source->header.w) ||
        !this->direct_resolve_image_crop(source, &source_x, &source_y, &source_width, &source_height)) {
      return false;
    }
    scale_q16 = (static_cast<uint32_t>(this->width_) * 16U + static_cast<uint32_t>(source_width) - 1U) /
                static_cast<uint32_t>(source_width);
    if (scale_q16 == 0 || static_cast<uint32_t>(source_width) * scale_q16 / 16U !=
                              static_cast<uint32_t>(this->width_) ||
        static_cast<uint32_t>(source_height) * scale_q16 / 16U != static_cast<uint32_t>(this->height_)) {
      return false;
    }

    // Starting every SRM band on a complete input macroblock keeps both the
    // scaler phase and the P4 JPEG/PPA macroblock boundary exact. This avoids
    // the one-line seams produced when an arbitrary output row starts a new
    // SRM transaction.
    band_alignment = scale_q16;
    const size_t source_bytes = source_stride * static_cast<size_t>(source->header.h);
    if (this->direct_image_synced_source_ != source->data || this->direct_image_synced_source_size_ != source_bytes) {
      const bool source_written_by_dma = esphome_artwork_image_buffer_written_by_dma != nullptr &&
                                         esphome_artwork_image_buffer_written_by_dma(source->data);
      if (!source_written_by_dma &&
          lvgl_cache_msync_external_result(source->data, source_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M) != ESP_OK) {
        return false;
      }
      this->direct_image_synced_source_ = source->data;
      this->direct_image_synced_source_size_ = source_bytes;
    }
  }

  size_t band_rows = maximum_band_rows;
  if (render_source) {
    band_rows = (band_rows / band_alignment) * band_alignment;
    if (band_rows == 0 || static_cast<size_t>(this->height_) % band_alignment != 0)
      return false;
  }

  auto *mipi_display = static_cast<mipi_dsi::MipiDsi *>(this->displays_[0]);
  if (mipi_display == nullptr || !mipi_display->wait_for_direct_frame_queue_idle(25))
    return false;
  uint8_t *target = mipi_display->get_direct_render_frame_buffer(background, scratch);
  const size_t frame_bytes = row_bytes * static_cast<size_t>(this->height_);
  if (target == nullptr || mipi_display->get_frame_buffer_size() < frame_bytes)
    return false;

  size_t target_index;
  if (target == mipi_display->get_frame_buffer(0)) {
    target_index = 0;
  } else if (target == mipi_display->get_frame_buffer(1)) {
    target_index = 1;
  } else if (target == mipi_display->get_frame_buffer(2)) {
    target_index = 2;
  } else {
    return false;
  }
  if (!this->prepare_direct_framebuffer_dma_ownership_(target, target_index))
    return false;

  const auto clear_ownership_ranges = []() {
    s_direct_ppa_source_begin.store(0, std::memory_order_release);
    s_direct_ppa_source2_begin.store(0, std::memory_order_release);
    s_direct_ppa_target_begin.store(0, std::memory_order_release);
    s_direct_ppa_source_end.store(0, std::memory_order_relaxed);
    s_direct_ppa_source2_end.store(0, std::memory_order_relaxed);
    s_direct_ppa_target_end.store(0, std::memory_order_relaxed);
  };
  const auto wait_for_operation = [](std::atomic<uint32_t> &remaining) {
    while (remaining.load(std::memory_order_acquire) != 0)
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  };

  if (esphome_mipi_dsi_wait_fifo_margin != nullptr) {
    esphome_mipi_dsi_wait_fifo_margin(CONFIG_ESPHOME_LVGL_PPA_SRM_TRANSFORM_DSI_FIFO_MIN,
                                      CONFIG_ESPHOME_LVGL_PPA_SRM_TRANSFORM_DSI_WAIT_US);
  }

  esp_err_t result = ESP_OK;
  for (size_t output_y = 0; output_y < static_cast<size_t>(this->height_);) {
    const size_t output_h = std::min(band_rows, static_cast<size_t>(this->height_) - output_y);
    const size_t band_bytes = row_bytes * output_h;

    if (render_source) {
      const uint32_t input_y = static_cast<uint32_t>(output_y) * 16U / scale_q16;
      const uint32_t input_end = static_cast<uint32_t>(output_y + output_h) * 16U / scale_q16;
      if (input_end <= input_y || input_end > static_cast<uint32_t>(source_height)) {
        result = ESP_ERR_INVALID_SIZE;
        break;
      }

      ppa_srm_oper_config_t srm{};
      srm.in.buffer = const_cast<uint8_t *>(source->data);
      srm.in.pic_w = source_stride_pixels;
      srm.in.pic_h = source->header.h;
      srm.in.block_w = source_width;
      srm.in.block_h = input_end - input_y;
      srm.in.block_offset_x = source_x;
      srm.in.block_offset_y = source_y + input_y;
      srm.in.srm_cm = source_mode;
      srm.out.buffer = scratch;
      srm.out.buffer_size = scratch_size;
      srm.out.pic_w = this->width_;
      srm.out.pic_h = output_h;
      srm.out.block_offset_x = 0;
      srm.out.block_offset_y = 0;
      srm.out.srm_cm = PPA_SRM_COLOR_MODE_RGB888;
      srm.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
      srm.scale_x = static_cast<float>(scale_q16) / 16.0f;
      srm.scale_y = static_cast<float>(scale_q16) / 16.0f;
      srm.alpha_update_mode = PPA_ALPHA_NO_CHANGE;
      srm.mode = PPA_TRANS_MODE_NON_BLOCKING;

      DirectPpaFrameCompletion completion;
      completion.remaining.store(1, std::memory_order_release);
      completion.waiter = xTaskGetCurrentTaskHandle();
      srm.user_data = &completion;
      ulTaskNotifyTake(pdTRUE, 0);

      const uintptr_t source_begin = reinterpret_cast<uintptr_t>(source->data);
      const uintptr_t scratch_begin = reinterpret_cast<uintptr_t>(scratch);
      s_direct_ppa_source_begin.store(source_begin, std::memory_order_release);
      s_direct_ppa_source_end.store(source_begin + source_stride * static_cast<size_t>(source->header.h),
                                    std::memory_order_release);
      s_direct_ppa_target_begin.store(scratch_begin, std::memory_order_release);
      s_direct_ppa_target_end.store(scratch_begin + band_bytes, std::memory_order_release);
      result = ppa_do_scale_rotate_mirror(srm_client, &srm);
      if (result == ESP_OK)
        wait_for_operation(completion.remaining);
      clear_ownership_ranges();
      if (result != ESP_OK)
        break;
    }

    ppa_blend_oper_config_t blend{};
    blend.in_bg.buffer = background;
    blend.in_bg.pic_w = this->width_;
    blend.in_bg.pic_h = this->height_;
    blend.in_bg.block_w = this->width_;
    blend.in_bg.block_h = output_h;
    blend.in_bg.block_offset_x = 0;
    blend.in_bg.block_offset_y = output_y;
    blend.in_bg.blend_cm = PPA_BLEND_COLOR_MODE_RGB888;
    blend.in_fg.buffer = scratch;
    blend.in_fg.pic_w = this->width_;
    blend.in_fg.pic_h = output_h;
    blend.in_fg.block_w = this->width_;
    blend.in_fg.block_h = output_h;
    blend.in_fg.block_offset_x = 0;
    blend.in_fg.block_offset_y = 0;
    blend.in_fg.blend_cm = PPA_BLEND_COLOR_MODE_RGB888;
    blend.out.buffer = target;
    blend.out.buffer_size = frame_bytes;
    blend.out.pic_w = this->width_;
    blend.out.pic_h = this->height_;
    blend.out.block_offset_x = 0;
    blend.out.block_offset_y = output_y;
    blend.out.blend_cm = PPA_BLEND_COLOR_MODE_RGB888;
    blend.bg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
    blend.bg_alpha_fix_val = 0xFF;
    blend.fg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
    blend.fg_alpha_fix_val = opacity;
    blend.mode = PPA_TRANS_MODE_NON_BLOCKING;

    DirectPpaFrameCompletion completion;
    completion.remaining.store(1, std::memory_order_release);
    completion.waiter = xTaskGetCurrentTaskHandle();
    blend.user_data = &completion;
    ulTaskNotifyTake(pdTRUE, 0);

    const uintptr_t background_begin = reinterpret_cast<uintptr_t>(background);
    const uintptr_t scratch_begin = reinterpret_cast<uintptr_t>(scratch);
    const uintptr_t target_begin = reinterpret_cast<uintptr_t>(target);
    s_direct_ppa_source_begin.store(background_begin, std::memory_order_release);
    s_direct_ppa_source_end.store(background_begin + frame_bytes, std::memory_order_release);
    s_direct_ppa_source2_begin.store(scratch_begin, std::memory_order_release);
    s_direct_ppa_source2_end.store(scratch_begin + band_bytes, std::memory_order_release);
    s_direct_ppa_target_begin.store(target_begin, std::memory_order_release);
    s_direct_ppa_target_end.store(target_begin + frame_bytes, std::memory_order_release);
    result = ppa_do_blend(blend_client, &blend);
    if (result == ESP_OK)
      wait_for_operation(completion.remaining);
    clear_ownership_ranges();
    if (result != ESP_OK)
      break;

    output_y += output_h;
  }

  if (result != ESP_OK) {
    ESP_LOGW(TAG, "PPA banded image crossfade failed: %s", esp_err_to_name(result));
    return false;
  }

  this->direct_image_framebuffer_dma_owned_[target_index] = true;
  if (!mipi_display->queue_direct_frame_buffer(target, 50))
    return false;
  this->direct_last_flushed_buf_ = target;
  return true;
}

const uint8_t *LvglComponent::direct_get_stable_presented_frame(uint32_t timeout_ms) {
#if LV_COLOR_DEPTH == 32 && defined(USE_MIPI_DSI)
  if (!this->direct_mode_active_ || this->displays_.size() != 1)
    return nullptr;
  auto *mipi_display = static_cast<mipi_dsi::MipiDsi *>(this->displays_[0]);
  if (mipi_display == nullptr || !mipi_display->wait_for_direct_frame_queue_idle(timeout_ms))
    return nullptr;
  return mipi_display->get_presented_frame_buffer();
#else
  (void) timeout_ms;
  return nullptr;
#endif
}

bool LvglComponent::direct_present_crossfade_(const uint8_t *background, const uint8_t *foreground, uint8_t opacity,
                                              ppa_client_handle_t blend_client, bool background_rgb565,
                                              bool foreground_rgb565) {
  if (!this->direct_image_animation_active_ || background == nullptr || foreground == nullptr ||
      blend_client == nullptr || this->displays_.size() != 1 || this->width_ <= 0 || this->height_ <= 0)
    return false;

  auto *mipi_display = static_cast<mipi_dsi::MipiDsi *>(this->displays_[0]);
  // A full-frame blend pins its background framebuffer until the transition
  // ends. Wait for the previously queued output to become active before
  // selecting the next target; otherwise all three DSI buffers can be seen as
  // active, queued, or pinned for one scanout interval and the transition is
  // spuriously aborted.
  if (mipi_display == nullptr || !mipi_display->wait_for_direct_frame_queue_idle(25))
    return false;
  uint8_t *target =
      mipi_display->get_direct_render_frame_buffer(background, foreground);
  if (target == nullptr)
    return false;

  size_t target_index;
  if (target == mipi_display->get_frame_buffer(0)) {
    target_index = 0;
  } else if (target == mipi_display->get_frame_buffer(1)) {
    target_index = 1;
  } else if (target == mipi_display->get_frame_buffer(2)) {
    target_index = 2;
  } else {
    return false;
  }
  if (!this->prepare_direct_framebuffer_dma_ownership_(target, target_index))
    return false;

  constexpr size_t OUTPUT_PIXEL_SIZE = 3;
  const size_t background_pixel_size = background_rgb565 ? 2U : 3U;
  const size_t foreground_pixel_size = foreground_rgb565 ? 2U : 3U;
  const ppa_blend_color_mode_t background_color_mode =
      background_rgb565 ? PPA_BLEND_COLOR_MODE_RGB565 : PPA_BLEND_COLOR_MODE_RGB888;
  const ppa_blend_color_mode_t foreground_color_mode =
      foreground_rgb565 ? PPA_BLEND_COLOR_MODE_RGB565 : PPA_BLEND_COLOR_MODE_RGB888;
  const size_t background_frame_bytes =
      static_cast<size_t>(this->width_) * static_cast<size_t>(this->height_) * background_pixel_size;
  const size_t foreground_frame_bytes =
      static_cast<size_t>(this->width_) * static_cast<size_t>(this->height_) * foreground_pixel_size;
  const size_t output_frame_bytes =
      static_cast<size_t>(this->width_) * static_cast<size_t>(this->height_) * OUTPUT_PIXEL_SIZE;
  if (mipi_display->get_frame_buffer_size() < output_frame_bytes)
    return false;

  ppa_blend_oper_config_t config{};
  config.in_bg.buffer = background;
  config.in_bg.pic_w = this->width_;
  config.in_bg.pic_h = this->height_;
  config.in_bg.block_w = this->width_;
  config.in_bg.block_h = this->height_;
  config.in_bg.blend_cm = background_color_mode;
  config.in_fg.buffer = foreground;
  config.in_fg.pic_w = this->width_;
  config.in_fg.pic_h = this->height_;
  config.in_fg.block_w = this->width_;
  config.in_fg.block_h = this->height_;
  config.in_fg.blend_cm = foreground_color_mode;
  config.out.buffer = target;
  config.out.buffer_size = output_frame_bytes;
  config.out.pic_w = this->width_;
  config.out.pic_h = this->height_;
  config.out.blend_cm = PPA_BLEND_COLOR_MODE_RGB888;
  config.bg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
  config.bg_alpha_fix_val = 0xFF;
  config.fg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
  config.fg_alpha_fix_val = opacity;
  config.mode = PPA_TRANS_MODE_NON_BLOCKING;

  const uintptr_t background_begin = reinterpret_cast<uintptr_t>(background);
  const uintptr_t foreground_begin = reinterpret_cast<uintptr_t>(foreground);
  const uintptr_t target_begin = reinterpret_cast<uintptr_t>(target);
  s_direct_ppa_source_begin.store(background_begin, std::memory_order_release);
  s_direct_ppa_source_end.store(background_begin + background_frame_bytes, std::memory_order_release);
  s_direct_ppa_source2_begin.store(foreground_begin, std::memory_order_release);
  s_direct_ppa_source2_end.store(foreground_begin + foreground_frame_bytes, std::memory_order_release);
  s_direct_ppa_target_begin.store(target_begin, std::memory_order_release);
  s_direct_ppa_target_end.store(target_begin + output_frame_bytes, std::memory_order_release);

  DirectPpaFrameCompletion completion;
  completion.remaining.store(1, std::memory_order_release);
  completion.waiter = xTaskGetCurrentTaskHandle();
  config.user_data = &completion;
  ulTaskNotifyTake(pdTRUE, 0);

  const int64_t started_us = esp_timer_get_time();
  const int64_t queue_started_us = started_us;
  if (esphome_mipi_dsi_wait_fifo_margin != nullptr) {
    esphome_mipi_dsi_wait_fifo_margin(CONFIG_ESPHOME_LVGL_PPA_SRM_TRANSFORM_DSI_FIFO_MIN,
                                      CONFIG_ESPHOME_LVGL_PPA_SRM_TRANSFORM_DSI_WAIT_US);
  }
  const esp_err_t result = ppa_do_blend(blend_client, &config);
  const uint32_t queue_us = static_cast<uint32_t>(esp_timer_get_time() - queue_started_us);
  if (result == ESP_OK) {
    while (completion.remaining.load(std::memory_order_acquire) != 0)
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  }
  const uint32_t blend_us = static_cast<uint32_t>(esp_timer_get_time() - started_us);

  s_direct_ppa_source_begin.store(0, std::memory_order_release);
  s_direct_ppa_source2_begin.store(0, std::memory_order_release);
  s_direct_ppa_target_begin.store(0, std::memory_order_release);
  s_direct_ppa_source_end.store(0, std::memory_order_relaxed);
  s_direct_ppa_source2_end.store(0, std::memory_order_relaxed);
  s_direct_ppa_target_end.store(0, std::memory_order_relaxed);

  if (result != ESP_OK) {
    ESP_LOGW(TAG, "PPA %s/%s crossfade failed: %s", background_rgb565 ? "RGB565" : "RGB888",
             foreground_rgb565 ? "RGB565" : "RGB888", esp_err_to_name(result));
    return false;
  }

  this->direct_image_framebuffer_dma_owned_[target_index] = true;
  if (!mipi_display->queue_direct_frame_buffer(target, 50)) {
    ESP_LOGW(TAG, "Direct crossfade framebuffer queue timed out");
    return false;
  }
  this->direct_last_flushed_buf_ = target;

  if (s_perf_logging_enabled) {
    static uint32_t frames = 0;
    static uint64_t total_us = 0;
    static uint32_t max_us = 0;
    static int64_t window_us = 0;
    const int64_t now_us = esp_timer_get_time();
    frames++;
    total_us += blend_us;
    max_us = std::max(max_us, blend_us);
    if (window_us == 0)
      window_us = now_us;
    if (now_us - window_us >= 1000000) {
      ESP_LOGD("lvgl.ken_burns", "crossfade: frames=%u avg=%uus max=%uus queue=%uus opacity=%u",
               static_cast<unsigned>(frames), static_cast<unsigned>(total_us / std::max<uint32_t>(1, frames)),
               static_cast<unsigned>(max_us), static_cast<unsigned>(queue_us), static_cast<unsigned>(opacity));
      frames = 0;
      total_us = 0;
      max_us = 0;
      window_us = now_us;
    }
  }
  return true;
}

bool LvglComponent::direct_present_rgb565_software(const uint8_t *source, size_t source_size) {
  if (!this->direct_image_animation_active_ || source == nullptr || this->displays_.size() != 1 ||
      this->width_ <= 0 || this->height_ <= 0)
    return false;

  auto *mipi_display = static_cast<mipi_dsi::MipiDsi *>(this->displays_[0]);
  uint8_t *target = mipi_display != nullptr ? mipi_display->get_direct_render_frame_buffer() : nullptr;
  const size_t pixels = static_cast<size_t>(this->width_) * static_cast<size_t>(this->height_);
  const size_t source_bytes = pixels * 2U;
  const size_t target_bytes = pixels * 3U;
  if (mipi_display == nullptr || target == nullptr || source_size < source_bytes ||
      mipi_display->get_frame_buffer_size() < target_bytes)
    return false;

  size_t target_index;
  if (target == mipi_display->get_frame_buffer(0)) {
    target_index = 0;
  } else if (target == mipi_display->get_frame_buffer(1)) {
    target_index = 1;
  } else if (target == mipi_display->get_frame_buffer(2)) {
    target_index = 2;
  } else {
    return false;
  }
  if (!this->prepare_direct_framebuffer_dma_ownership_(target, target_index))
    return false;

  for (size_t index = 0; index < pixels; index++) {
    const uint16_t pixel = static_cast<uint16_t>(source[index * 2U]) |
                           (static_cast<uint16_t>(source[index * 2U + 1U]) << 8U);
    const uint8_t red = static_cast<uint8_t>(((pixel >> 11U) & 0x1FU) * 255U / 31U);
    const uint8_t green = static_cast<uint8_t>(((pixel >> 5U) & 0x3FU) * 255U / 63U);
    const uint8_t blue = static_cast<uint8_t>((pixel & 0x1FU) * 255U / 31U);
    uint8_t *out = target + index * 3U;
    if (this->big_endian_) {
      out[0] = red;
      out[1] = green;
      out[2] = blue;
    } else {
      out[0] = blue;
      out[1] = green;
      out[2] = red;
    }
  }
  if (lvgl_cache_msync_external_result(target, target_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M) != ESP_OK)
    return false;

  this->direct_image_framebuffer_dma_owned_[target_index] = true;
  if (!mipi_display->queue_direct_frame_buffer(target, 50))
    return false;
  this->direct_last_flushed_buf_ = target;
  return true;
}
#endif

bool LvglComponent::end_direct_image_animation() {
  if (!this->direct_image_animation_active_)
    return true;

  /* Do not return framebuffer ownership to LVGL until DSI has latched the
   * final queued direct frame. Marking the mode inactive first allowed LVGL,
   * the app snapshot compositor and scanout to use the same PSRAM buffer when
   * a frame-active callback arrived late. */
  if (!this->displays_.empty()) {
    auto *mipi_display = static_cast<mipi_dsi::MipiDsi *>(this->displays_[0]);
    bool queue_idle = mipi_display == nullptr;
    for (uint8_t attempt = 0; mipi_display != nullptr && attempt < 3 && !queue_idle; attempt++) {
      queue_idle = mipi_display->wait_for_direct_frame_queue_idle(100);
      if (!queue_idle)
        vTaskDelay(1);
    }
    if (!queue_idle) {
      ESP_LOGW(TAG, "Direct image handoff deferred: final DSI framebuffer is still queued");
      return false;
    }
  }
  this->direct_image_animation_active_ = false;
#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  lv_draw_ppa_direct_animation_qos_restore();
#endif
  this->direct_image_framebuffer_dma_owned_[0] = false;
  this->direct_image_framebuffer_dma_owned_[1] = false;
  this->direct_image_framebuffer_dma_owned_[2] = false;
  this->direct_image_synced_source_ = nullptr;
  this->direct_image_synced_source_size_ = 0;
  // FULL mode redraws every pixel into the idle framebuffer, so copying the
  // 1.92 MB presented frame first only wastes PSRAM bandwidth.
  this->realign_direct_buffer_after_manual_present(false);
  if (this->disp_ != nullptr)
    lv_display_enable_invalidation(this->disp_, true);
  return true;
}

bool LvglComponent::snapshot_swipe_direct_render(lv_draw_buf_t *current, lv_draw_buf_t *next, int current_x, int next_x,
                                                 int width) {
#if LV_COLOR_DEPTH == 32 && defined(USE_ESP32)
  uint64_t t0 = esp_timer_get_time();
  if (current == nullptr || next == nullptr)
    return false;
  if (width != this->width_ || this->width_ <= 0 || this->height_ <= 0)
    return false;
  if (current->data == nullptr || next->data == nullptr)
    return false;
  if (current->header.cf != LV_COLOR_FORMAT_RGB888 || next->header.cf != LV_COLOR_FORMAT_RGB888)
    return false;
  if (current->header.w < this->width_ || next->header.w < this->width_ || current->header.h < this->height_ ||
      next->header.h < this->height_)
    return false;

  constexpr size_t BYTES_PER_PIXEL = 3;
  const size_t row_bytes = (size_t) this->width_ * BYTES_PER_PIXEL;
  const size_t fb_bytes = (size_t) this->width_ * this->height_ * BYTES_PER_PIXEL;
  const int64_t target_started_us = esp_timer_get_time();
  uint8_t *target = this->next_snapshot_render_buffer_(nullptr, nullptr, 20);
  const uint32_t target_us = static_cast<uint32_t>(esp_timer_get_time() - target_started_us);
  if (target == nullptr)
    return false;

  auto sync_range = [](uint8_t *ptr, size_t len) { lvgl_cache_msync_external(ptr, len, ESP_CACHE_MSYNC_FLAG_DIR_C2M); };

  auto copy_visible = [&](uint8_t *dst, const lv_draw_buf_t *src, int image_x, uint32_t *elapsed_us) -> bool {
    const int64_t copy_started_us = esp_timer_get_time();
    const int32_t dst_x1 = std::max<int32_t>(0, image_x);
    const int32_t dst_x2 = std::min<int32_t>(this->width_, image_x + width);
    if (dst_x2 <= dst_x1) {
      *elapsed_us = static_cast<uint32_t>(esp_timer_get_time() - copy_started_us);
      return false;
    }
    const int32_t src_x = dst_x1 - image_x;
    const size_t copy_bytes = (size_t) (dst_x2 - dst_x1) * BYTES_PER_PIXEL;
#if CONFIG_ESPHOME_LVGL_SNAPSHOT_DMA2D_M2M
    if (dma2d_m2m_copy_rgb888_2d(src->data, src->header.w, src->header.h, src_x, 0, dst, this->width_,
                                 this->height_, dst_x1, 0, dst_x2 - dst_x1, this->height_)) {
      *elapsed_us = static_cast<uint32_t>(esp_timer_get_time() - copy_started_us);
      return false;
    }
#endif
#ifdef USE_LVGL_PPA
    if (s_display_srm_client != nullptr) {
      ppa_srm_oper_config_t cfg = {};
      cfg.in.buffer = src->data;
      cfg.in.pic_w = src->header.w;
      cfg.in.pic_h = src->header.h;
      cfg.in.block_w = dst_x2 - dst_x1;
      cfg.in.block_h = this->height_;
      cfg.in.block_offset_x = src_x;
      cfg.in.block_offset_y = 0;
      cfg.in.srm_cm = PPA_SRM_COLOR_MODE_RGB888;
      cfg.out.buffer = dst;
      cfg.out.buffer_size = fb_bytes;
      cfg.out.pic_w = this->width_;
      cfg.out.pic_h = this->height_;
      cfg.out.block_offset_x = dst_x1;
      cfg.out.block_offset_y = 0;
      cfg.out.srm_cm = PPA_SRM_COLOR_MODE_RGB888;
      cfg.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
      cfg.scale_x = 1.0f;
      cfg.scale_y = 1.0f;
      cfg.alpha_update_mode = PPA_ALPHA_NO_CHANGE;
      cfg.mode = PPA_TRANS_MODE_BLOCKING;
      esp_err_t ret = ppa_do_scale_rotate_mirror(s_display_srm_client, &cfg);
      if (ret == ESP_OK) {
        *elapsed_us = static_cast<uint32_t>(esp_timer_get_time() - copy_started_us);
        return false;
      }
      static bool warned = false;
      if (!warned) {
        ESP_LOGW(TAG, "snapshot direct: PPA copy failed (%d), using CPU fallback", ret);
        warned = true;
      }
    }
#endif
    const size_t src_stride = src->header.stride;
    const uint8_t *src_row = src->data + (size_t) src_x * BYTES_PER_PIXEL;
    uint8_t *dst_row = dst + (size_t) dst_x1 * BYTES_PER_PIXEL;
    for (int32_t y = 0; y < this->height_; y++) {
      memcpy(dst_row, src_row, copy_bytes);
      src_row += src_stride;
      dst_row += row_bytes;
    }
    *elapsed_us = static_cast<uint32_t>(esp_timer_get_time() - copy_started_us);
    return true;
  };

  uint32_t current_copy_us = 0;
  uint32_t next_copy_us = 0;
  uint32_t overlay_us = 0;
  auto render_to = [&](uint8_t *dst) {
    bool needs_full_sync = false;
    bool batch_copied = false;
#if CONFIG_ESPHOME_LVGL_SNAPSHOT_DMA2D_M2M
    Dma2dM2mCopySpan spans[2]{};
    size_t span_count = 0;
    auto append_span = [&](const lv_draw_buf_t *src, int image_x) {
      const int32_t dst_x1 = std::max<int32_t>(0, image_x);
      const int32_t dst_x2 = std::min<int32_t>(this->width_, image_x + width);
      if (dst_x2 <= dst_x1)
        return;
      const int32_t src_x = dst_x1 - image_x;
      spans[span_count++] = {
          static_cast<const uint8_t *>(src->data), static_cast<int>(src->header.stride / BYTES_PER_PIXEL),
          static_cast<int>(src->header.h), src_x, 0, dst_x1, 0, dst_x2 - dst_x1, this->height_};
    };
    append_span(current, current_x);
    append_span(next, next_x);
    const int64_t batch_started_us = esp_timer_get_time();
    if (span_count != 0)
      batch_copied = dma2d_m2m_copy_rgb888_spans(spans, span_count, dst, this->width_, this->height_);
    current_copy_us = static_cast<uint32_t>(esp_timer_get_time() - batch_started_us);
    next_copy_us = 0;
#endif
    if (!batch_copied) {
      needs_full_sync |= copy_visible(dst, current, current_x, &current_copy_us);
      needs_full_sync |= copy_visible(dst, next, next_x, &next_copy_us);
    }
    const int64_t overlay_started_us = esp_timer_get_time();
    const bool indicator_changed = snapshot_draw_page_indicator_rgb888(
        dst, this->width_, this->height_, s_snapshot_page_indicator_page, s_snapshot_page_indicator_count);
    if (needs_full_sync)
      sync_range(dst, fb_bytes);
    else if (indicator_changed)
      snapshot_sync_page_indicator_rgb888(dst, this->width_, this->height_);
    overlay_us = static_cast<uint32_t>(esp_timer_get_time() - overlay_started_us);
  };

  render_to(target);
  const int64_t present_started_us = esp_timer_get_time();
  if (!this->present_snapshot_render_buffer_(target, false))
    return false;
  const uint32_t present_us = static_cast<uint32_t>(esp_timer_get_time() - present_started_us);
#ifdef USE_LVGL_FPS_BENCHMARK
  lvgl_esphome_note_frame();
#endif
  uint32_t frame_us = (uint32_t) (esp_timer_get_time() - t0);
  static uint64_t last_log_us = 0;
  static uint32_t frames = 0;
  static uint64_t total_us = 0;
  static uint32_t max_us = 0;
  static uint64_t total_target_us = 0;
  static uint64_t total_current_copy_us = 0;
  static uint64_t total_next_copy_us = 0;
  static uint64_t total_overlay_us = 0;
  static uint64_t total_present_us = 0;
  uint64_t now_us = esp_timer_get_time();
  frames++;
  total_us += frame_us;
  total_target_us += target_us;
  total_current_copy_us += current_copy_us;
  total_next_copy_us += next_copy_us;
  total_overlay_us += overlay_us;
  total_present_us += present_us;
  if (frame_us > max_us)
    max_us = frame_us;
  if (last_log_us == 0)
    last_log_us = now_us;
  if (now_us - last_log_us >= 1000000ULL) {
    uint32_t fps = (uint32_t) ((uint64_t) frames * 1000000ULL / (now_us - last_log_us));
    if (s_swipe_logging_enabled) {
      ESP_LOGI(TAG, "snapshot direct: fps=%u avg=%lluus max=%uus", (unsigned) fps,
               (unsigned long long) (frames == 0 ? 0 : total_us / frames), (unsigned) max_us);
      ESP_LOGI(TAG,
               "snapshot direct stages: target=%lluus current=%lluus next=%lluus overlay=%lluus present=%lluus",
               (unsigned long long) (frames == 0 ? 0 : total_target_us / frames),
               (unsigned long long) (frames == 0 ? 0 : total_current_copy_us / frames),
               (unsigned long long) (frames == 0 ? 0 : total_next_copy_us / frames),
               (unsigned long long) (frames == 0 ? 0 : total_overlay_us / frames),
               (unsigned long long) (frames == 0 ? 0 : total_present_us / frames));
    }
    last_log_us = now_us;
    frames = 0;
    total_us = 0;
    max_us = 0;
    total_target_us = 0;
    total_current_copy_us = 0;
    total_next_copy_us = 0;
    total_overlay_us = 0;
    total_present_us = 0;
  }
  return true;
#else
  return false;
#endif
}

bool LvglComponent::snapshot_swipe_direct_render_edge(lv_draw_buf_t *current, int current_x, int width) {
#if LV_COLOR_DEPTH == 32 && defined(USE_ESP32)
  if (current == nullptr || current->data == nullptr)
    return false;
  if (width != this->width_ || this->width_ <= 0 || this->height_ <= 0)
    return false;
  if (current->header.cf != LV_COLOR_FORMAT_RGB888 || current->header.w < this->width_ ||
      current->header.h < this->height_)
    return false;

  constexpr size_t BYTES_PER_PIXEL = 3;
  const size_t row_bytes = (size_t) this->width_ * BYTES_PER_PIXEL;
  const size_t fb_bytes = (size_t) this->width_ * this->height_ * BYTES_PER_PIXEL;
  uint8_t *target = this->next_snapshot_render_buffer_(nullptr, nullptr, 20);
  if (target == nullptr)
    return false;

  const int edge_limit = (this->width_ + 1) / 2;
  current_x = std::clamp(current_x, -edge_limit, edge_limit);
  bool needs_full_sync = false;
  auto clear_visible = [&](int x1, int x2) {
    const int screen_w = (int) this->width_;
    x1 = std::clamp(x1, 0, screen_w);
    x2 = std::clamp(x2, 0, screen_w);
    if (x2 <= x1)
      return;
#ifdef USE_LVGL_PPA
    if (s_snapshot_fill_client != nullptr) {
      ppa_fill_oper_config_t cfg = {};
      cfg.out.buffer = target;
      cfg.out.buffer_size = fb_bytes;
      cfg.out.pic_w = this->width_;
      cfg.out.pic_h = this->height_;
      cfg.out.block_offset_x = x1;
      cfg.out.block_offset_y = 0;
      cfg.out.fill_cm = PPA_FILL_COLOR_MODE_RGB888;
      cfg.fill_block_w = x2 - x1;
      cfg.fill_block_h = this->height_;
      color_pixel_argb8888_data_t fill_color = {};
      fill_color.a = 0xFF;
      cfg.fill_argb_color = fill_color;
      cfg.mode = PPA_TRANS_MODE_BLOCKING;
      if (ppa_do_fill(s_snapshot_fill_client, &cfg) == ESP_OK)
        return;
    }
#endif
    const size_t clear_bytes = (size_t) (x2 - x1) * BYTES_PER_PIXEL;
    uint8_t *dst_row = target + (size_t) x1 * BYTES_PER_PIXEL;
    for (int y = 0; y < this->height_; y++) {
      memset(dst_row, 0, clear_bytes);
      dst_row += row_bytes;
    }
    needs_full_sync = true;
  };

  auto copy_visible = [&](const lv_draw_buf_t *src, int image_x) -> bool {
    const int32_t dst_x1 = std::max<int32_t>(0, image_x);
    const int32_t dst_x2 = std::min<int32_t>(this->width_, image_x + width);
    if (dst_x2 <= dst_x1)
      return false;
    const int32_t src_x = dst_x1 - image_x;
    const size_t copy_bytes = (size_t) (dst_x2 - dst_x1) * BYTES_PER_PIXEL;
#if CONFIG_ESPHOME_LVGL_SNAPSHOT_DMA2D_M2M
    if (dma2d_m2m_copy_rgb888_2d(src->data, src->header.stride / BYTES_PER_PIXEL, src->header.h, src_x, 0,
                                 target, this->width_, this->height_, dst_x1, 0, dst_x2 - dst_x1,
                                 this->height_)) {
      return false;
    }
#endif
#ifdef USE_LVGL_PPA
    if (s_display_srm_client != nullptr) {
      ppa_srm_oper_config_t cfg = {};
      cfg.in.buffer = src->data;
      cfg.in.pic_w = src->header.stride / BYTES_PER_PIXEL;
      cfg.in.pic_h = src->header.h;
      cfg.in.block_w = dst_x2 - dst_x1;
      cfg.in.block_h = this->height_;
      cfg.in.block_offset_x = src_x;
      cfg.in.block_offset_y = 0;
      cfg.in.srm_cm = PPA_SRM_COLOR_MODE_RGB888;
      cfg.out.buffer = target;
      cfg.out.buffer_size = fb_bytes;
      cfg.out.pic_w = this->width_;
      cfg.out.pic_h = this->height_;
      cfg.out.block_offset_x = dst_x1;
      cfg.out.block_offset_y = 0;
      cfg.out.srm_cm = PPA_SRM_COLOR_MODE_RGB888;
      cfg.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
      cfg.scale_x = 1.0f;
      cfg.scale_y = 1.0f;
      cfg.alpha_update_mode = PPA_ALPHA_NO_CHANGE;
      cfg.mode = PPA_TRANS_MODE_BLOCKING;
      esp_err_t ret = ppa_do_scale_rotate_mirror(s_display_srm_client, &cfg);
      if (ret == ESP_OK)
        return false;
      static bool warned = false;
      if (!warned) {
        ESP_LOGW(TAG, "snapshot edge: PPA copy failed (%d), using CPU fallback", ret);
        warned = true;
      }
    }
#endif
    const size_t src_stride = src->header.stride;
    const uint8_t *src_row = src->data + (size_t) src_x * BYTES_PER_PIXEL;
    uint8_t *dst_row = target + (size_t) dst_x1 * BYTES_PER_PIXEL;
    for (int32_t y = 0; y < this->height_; y++) {
      memcpy(dst_row, src_row, copy_bytes);
      src_row += src_stride;
      dst_row += row_bytes;
    }
    return true;
  };

  if (current_x > 0) {
    clear_visible(0, current_x);
  } else if (current_x < 0) {
    clear_visible(this->width_ + current_x, this->width_);
  }
  needs_full_sync |= copy_visible(current, current_x);
  const bool indicator_changed = snapshot_draw_page_indicator_rgb888(
      target, this->width_, this->height_, s_snapshot_page_indicator_page, s_snapshot_page_indicator_count);
  if (needs_full_sync)
    lvgl_cache_msync_external(target, fb_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  else if (indicator_changed)
    snapshot_sync_page_indicator_rgb888(target, this->width_, this->height_);
  // Match the panorama compositor: queue the completed frame and immediately
  // compose the next one into the third DSI buffer. Waiting for this buffer to
  // become active adds one complete refresh period to every bounce frame.
  if (!this->present_snapshot_render_buffer_(target, false))
    return false;
#ifdef USE_LVGL_FPS_BENCHMARK
  lvgl_esphome_note_frame();
#endif
  return true;
#else
  return false;
#endif
}

bool LvglComponent::snapshot_swipe_direct_render_panorama(const uint8_t *panorama, int current_x, int width, int scale,
                                                          int initial_next_x, int panorama_page_count,
                                                          int source_page_index) {
#if LV_COLOR_DEPTH == 32 && defined(USE_ESP32) && defined(USE_LVGL_PPA)
  uint64_t t0 = esp_timer_get_time();
  if (panorama == nullptr || s_display_srm_client == nullptr)
    return false;
  if (width != this->width_ || this->width_ <= 0 || this->height_ <= 0 || initial_next_x == 0 || scale <= 0 ||
      panorama_page_count < 2 || source_page_index < 0 || source_page_index + 1 >= panorama_page_count)
    return false;
  if ((this->width_ % scale) != 0 || (this->height_ % scale) != 0)
    return false;

  constexpr size_t OUT_BYTES_PER_PIXEL = 3;
  constexpr size_t IN_BYTES_PER_PIXEL = 3;
  const int source_width = width / scale;
  const int source_height = this->height_ / scale;
  const int panorama_width = source_width * panorama_page_count;
  int window_x = initial_next_x > 0 ? -current_x : width - current_x;
  window_x = std::clamp(window_x, 0, width);
  const int source_x = source_page_index * source_width + std::clamp(window_x / scale, 0, source_width);

  const size_t fb_bytes = (size_t) this->width_ * this->height_ * OUT_BYTES_PER_PIXEL;
  uint8_t *target = this->next_snapshot_render_buffer_(nullptr, nullptr, 20);
  if (target == nullptr)
    return false;

  bool copied = false;
#if CONFIG_ESPHOME_LVGL_SNAPSHOT_DMA2D_M2M
  if (scale == 1) {
    copied = dma2d_m2m_copy_rgb888_2d(panorama, panorama_width, source_height, source_x, 0, target, this->width_,
                                      this->height_, 0, 0, this->width_, this->height_);
  }
#endif
  if (!copied) {
    ppa_srm_oper_config_t cfg = {};
    cfg.in.buffer = const_cast<uint8_t *>(panorama);
    cfg.in.pic_w = panorama_width;
    cfg.in.pic_h = source_height;
    cfg.in.block_w = source_width;
    cfg.in.block_h = source_height;
    cfg.in.block_offset_x = source_x;
    cfg.in.block_offset_y = 0;
    cfg.in.srm_cm = PPA_SRM_COLOR_MODE_RGB888;
    cfg.out.buffer = target;
    cfg.out.buffer_size = fb_bytes;
    cfg.out.pic_w = this->width_;
    cfg.out.pic_h = this->height_;
    cfg.out.block_offset_x = 0;
    cfg.out.block_offset_y = 0;
    cfg.out.srm_cm = PPA_SRM_COLOR_MODE_RGB888;
    cfg.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
    cfg.scale_x = (float) scale;
    cfg.scale_y = (float) scale;
    cfg.alpha_update_mode = PPA_ALPHA_NO_CHANGE;
    cfg.mode = PPA_TRANS_MODE_BLOCKING;

    const esp_err_t ret = ppa_do_scale_rotate_mirror(s_display_srm_client, &cfg);
    if (ret != ESP_OK) {
      static bool warned = false;
      if (!warned) {
        ESP_LOGW(TAG, "snapshot panorama: DMA2D/PPA RGB888 copy failed (%d)", ret);
        warned = true;
      }
      return false;
    }
  }
  if (snapshot_draw_page_indicator_rgb888(target, this->width_, this->height_, s_snapshot_page_indicator_page,
                                          s_snapshot_page_indicator_count))
    snapshot_sync_page_indicator_rgb888(target, this->width_, this->height_);
  const uint64_t present_start = esp_timer_get_time();
  if (!this->present_snapshot_render_buffer_(target, false))
    return false;
  const uint32_t present_us = (uint32_t) (esp_timer_get_time() - present_start);

#ifdef USE_LVGL_FPS_BENCHMARK
  lvgl_esphome_note_frame();
#endif
  uint32_t frame_us = (uint32_t) (esp_timer_get_time() - t0);
  static uint64_t last_log_us = 0;
  static uint32_t frames = 0;
  static uint64_t total_us = 0;
  static uint32_t max_us = 0;
  static uint64_t total_present_us = 0;
  static uint32_t max_present_us = 0;
  uint64_t now_us = esp_timer_get_time();
  frames++;
  total_us += frame_us;
  total_present_us += present_us;
  if (frame_us > max_us)
    max_us = frame_us;
  if (present_us > max_present_us)
    max_present_us = present_us;
  if (last_log_us == 0)
    last_log_us = now_us;
  if (now_us - last_log_us >= 1000000ULL) {
    uint32_t fps = (uint32_t) ((uint64_t) frames * 1000000ULL / (now_us - last_log_us));
    if (s_swipe_logging_enabled) {
      ESP_LOGI(TAG, "snapshot panorama: fps=%u avg=%lluus max=%uus present=%lluus/%uus scale=%dx src=%uKB dst=%uKB", (unsigned) fps,
               (unsigned long long) (frames == 0 ? 0 : total_us / frames), (unsigned) max_us,
               (unsigned long long) (frames == 0 ? 0 : total_present_us / frames), (unsigned) max_present_us, scale,
               (unsigned) (((size_t) panorama_width * source_height * IN_BYTES_PER_PIXEL) / 1024),
               (unsigned) (fb_bytes / 1024));
    }
    last_log_us = now_us;
    frames = 0;
    total_us = 0;
    max_us = 0;
    total_present_us = 0;
    max_present_us = 0;
  }
  return true;
#else
  return false;
#endif
}

bool LvglComponent::snapshot_scroll_direct_render(lv_draw_buf_t *content, lv_draw_buf_t *content_tail,
                                                  int content_tail_y, int scroll_y, int viewport_w,
                                                  int viewport_h) {
#if LV_COLOR_DEPTH == 32 && defined(USE_ESP32)
  uint64_t t0 = esp_timer_get_time();
  if (content == nullptr || content->data == nullptr)
    return false;
  if (viewport_w != this->width_ || viewport_h != this->height_ || viewport_w <= 0 || viewport_h <= 0)
    return false;
  if (content->header.cf != LV_COLOR_FORMAT_RGB888 || content->header.w < viewport_w || content->header.h < viewport_h)
    if (content_tail == nullptr || content->header.cf != LV_COLOR_FORMAT_RGB888 ||
        content->header.w < viewport_w || content_tail->data == nullptr ||
        content_tail->header.cf != LV_COLOR_FORMAT_RGB888 || content_tail->header.w < viewport_w ||
        content_tail_y != content->header.h)
      return false;

  const int content_height = content_tail == nullptr ? content->header.h : content_tail_y + content_tail->header.h;
  const int max_scroll_y = std::max(0, content_height - viewport_h);
  scroll_y = std::clamp(scroll_y, -120, max_scroll_y + 120);

  constexpr size_t BYTES_PER_PIXEL = 3;
  const size_t row_bytes = (size_t) viewport_w * BYTES_PER_PIXEL;
  const size_t fb_bytes = (size_t) viewport_w * viewport_h * BYTES_PER_PIXEL;
  uint8_t *target = this->next_snapshot_render_buffer_(nullptr, nullptr, 20);
  if (target == nullptr)
    return false;

  bool needs_sync = false;
  auto fill_rows = [&](int target_y, int rows) -> bool {
    if (rows <= 0)
      return true;
#ifdef USE_LVGL_PPA
    if (s_snapshot_fill_client != nullptr) {
      ppa_fill_oper_config_t cfg = {};
      cfg.out.buffer = target;
      cfg.out.buffer_size = fb_bytes;
      cfg.out.pic_w = viewport_w;
      cfg.out.pic_h = viewport_h;
      cfg.out.block_offset_x = 0;
      cfg.out.block_offset_y = target_y;
      cfg.out.fill_cm = PPA_FILL_COLOR_MODE_RGB888;
      cfg.fill_block_w = viewport_w;
      cfg.fill_block_h = rows;
      color_pixel_argb8888_data_t fill_color = {};
      fill_color.a = 0xFF;
      cfg.fill_argb_color = fill_color;
      cfg.mode = PPA_TRANS_MODE_BLOCKING;
      if (ppa_do_fill(s_snapshot_fill_client, &cfg) == ESP_OK)
        return true;
    }
#endif
    for (int row = 0; row < rows; row++)
      memset(target + static_cast<size_t>(target_y + row) * row_bytes, 0, row_bytes);
    needs_sync = true;
    return true;
  };
  auto copy_rows = [&](lv_draw_buf_t *source, int source_y, int target_y, int rows) -> bool {
    if (source == nullptr || source->data == nullptr || rows <= 0)
      return rows == 0;
#if CONFIG_ESPHOME_LVGL_SNAPSHOT_DMA2D_M2M
    if (dma2d_m2m_copy_rgb888_2d(source->data, source->header.w, source->header.h, 0, source_y, target,
                                 viewport_w, viewport_h, 0, target_y, viewport_w, rows)) {
      return true;
    }
#endif
#ifdef USE_LVGL_PPA
    if (s_display_srm_client != nullptr) {
    ppa_srm_oper_config_t cfg = {};
      cfg.in.buffer = source->data;
      cfg.in.pic_w = source->header.w;
      cfg.in.pic_h = source->header.h;
    cfg.in.block_w = viewport_w;
      cfg.in.block_h = rows;
    cfg.in.block_offset_x = 0;
      cfg.in.block_offset_y = source_y;
    cfg.in.srm_cm = PPA_SRM_COLOR_MODE_RGB888;
    cfg.out.buffer = target;
    cfg.out.buffer_size = fb_bytes;
    cfg.out.pic_w = viewport_w;
    cfg.out.pic_h = viewport_h;
    cfg.out.block_offset_x = 0;
      cfg.out.block_offset_y = target_y;
    cfg.out.srm_cm = PPA_SRM_COLOR_MODE_RGB888;
    cfg.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
    cfg.scale_x = 1.0f;
    cfg.scale_y = 1.0f;
    cfg.alpha_update_mode = PPA_ALPHA_NO_CHANGE;
    cfg.mode = PPA_TRANS_MODE_BLOCKING;
    esp_err_t ret = ppa_do_scale_rotate_mirror(s_display_srm_client, &cfg);
    if (ret == ESP_OK) {
        return true;
      }
      static bool warned = false;
      if (!warned) {
        ESP_LOGW(TAG, "snapshot scroll: PPA RGB888 copy failed (%d), using CPU fallback", ret);
        warned = true;
      }
    }
#endif
    const uint8_t *src_row = source->data + (size_t) source_y * source->header.stride;
    uint8_t *dst_row = target + (size_t) target_y * row_bytes;
    for (int y = 0; y < rows; y++) {
      memcpy(dst_row, src_row, row_bytes);
      src_row += source->header.stride;
      dst_row += row_bytes;
    }
    needs_sync = true;
    return true;
  };

  auto copy_content_rows = [&](int source_y, int target_y, int rows) -> bool {
    if (rows <= 0)
      return true;
    if (content_tail == nullptr || source_y + rows <= content_tail_y)
      return copy_rows(content, source_y, target_y, rows);
    if (source_y >= content_tail_y)
      return copy_rows(content_tail, source_y - content_tail_y, target_y, rows);
    const int head_rows = content_tail_y - source_y;
#if CONFIG_ESPHOME_LVGL_SNAPSHOT_DMA2D_M2M
    const Dma2dM2mCopySpan spans[2] = {
        {content->data, static_cast<int>(content->header.w), static_cast<int>(content->header.h), 0, source_y, 0,
         target_y, viewport_w, head_rows},
        {content_tail->data, static_cast<int>(content_tail->header.w), static_cast<int>(content_tail->header.h), 0,
         0, 0, target_y + head_rows, viewport_w, rows - head_rows},
    };
    if (dma2d_m2m_copy_rgb888_spans(spans, 2, target, viewport_w, viewport_h))
      return true;
#endif
    return copy_rows(content, source_y, target_y, head_rows) &&
           copy_rows(content_tail, 0, target_y + head_rows, rows - head_rows);
  };

  bool copied = false;
  if (scroll_y < 0) {
    const int gap = std::min(viewport_h, -scroll_y);
    copied = fill_rows(0, gap) && copy_content_rows(0, gap, viewport_h - gap);
  } else if (scroll_y > max_scroll_y) {
    const int gap = std::min(viewport_h, scroll_y - max_scroll_y);
    copied = copy_content_rows(max_scroll_y + gap, 0, viewport_h - gap) &&
             fill_rows(viewport_h - gap, gap);
  } else {
    copied = copy_content_rows(scroll_y, 0, viewport_h);
  }
  if (!copied)
    return false;
  if (needs_sync) {
    lvgl_cache_msync_external(target, fb_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  }

  if (!this->present_snapshot_render_buffer_(target, false))
    return false;
#ifdef USE_LVGL_FPS_BENCHMARK
  lvgl_esphome_note_frame();
#endif
  uint32_t frame_us = (uint32_t) (esp_timer_get_time() - t0);
  static uint64_t last_log_us = 0;
  static uint32_t frames = 0;
  static uint64_t total_us = 0;
  static uint32_t max_us = 0;
  uint64_t now_us = esp_timer_get_time();
  frames++;
  total_us += frame_us;
  if (frame_us > max_us)
    max_us = frame_us;
  if (last_log_us == 0)
    last_log_us = now_us;
  if (now_us - last_log_us >= 1000000ULL) {
    uint32_t fps = (uint32_t) ((uint64_t) frames * 1000000ULL / (now_us - last_log_us));
    if (s_swipe_logging_enabled) {
      ESP_LOGI(TAG, "snapshot scroll: fps=%u avg=%lluus max=%uus y=%d h=%d", (unsigned) fps,
               (unsigned long long) (frames == 0 ? 0 : total_us / frames), (unsigned) max_us, scroll_y,
               content_height);
    }
    last_log_us = now_us;
    frames = 0;
    total_us = 0;
    max_us = 0;
  }
  return true;
#else
  return false;
#endif
}

bool LvglComponent::snapshot_app_direct_render(lv_draw_buf_t *background, lv_draw_buf_t *app, int center_x,
                                               int center_y, int width, int height) {
#if LV_COLOR_DEPTH == 32 && defined(USE_ESP32)
  const int64_t frame_started_us = esp_timer_get_time();
  uint32_t init_copy_us = 0;
  uint32_t prepare_us = 0;
  uint32_t circle_us = 0;
  uint32_t cache_sync_us = 0;
  uint32_t present_us = 0;
  if (app == nullptr || app->data == nullptr)
    return false;
  if (this->width_ <= 0 || this->height_ <= 0 || width < 0 || height < 0)
    return false;
  if (app->header.cf != LV_COLOR_FORMAT_RGB888 || app->header.w < this->width_ || app->header.h < this->height_)
    return false;
  if (background != nullptr && (background->data == nullptr || background->header.cf != LV_COLOR_FORMAT_RGB888 ||
                                background->header.w < this->width_ || background->header.h < this->height_)) {
    background = nullptr;
  }

  constexpr size_t BYTES_PER_PIXEL = 3;
  const size_t row_bytes = (size_t) this->width_ * BYTES_PER_PIXEL;
  const size_t fb_bytes = row_bytes * (size_t) this->height_;
  auto visible_source_span = [&](const lv_draw_buf_t *source) -> size_t {
    if (source == nullptr || source->header.h == 0)
      return 0;
    return (static_cast<size_t>(source->header.h) - 1U) * source->header.stride + row_bytes;
  };
  // A close transition may use the currently presented DSI framebuffer as its
  // immutable source. Keep that framebuffer (and a possible framebuffer-backed
  // background) out of the render-target rotation for the whole animation.
  const auto *app_source = static_cast<const uint8_t *>(app->data);
  const auto *background_source =
      background == nullptr ? nullptr : static_cast<const uint8_t *>(background->data);
  uint8_t *target = this->next_snapshot_render_buffer_(app_source, background_source, 20);
#ifdef USE_MIPI_DSI
  if (target == nullptr && this->direct_mode_active_ && this->rotation == display::DISPLAY_ROTATION_0_DEGREES &&
      this->displays_.size() == 1) {
    // Closing can reserve one framebuffer as its immutable source while the
    // other two are active and staged/queued. Wait on the real frame-boundary
    // ISR instead of counting scheduler yields or reusing a DSI-owned buffer.
    auto *mipi_display = static_cast<mipi_dsi::MipiDsi *>(this->displays_[0]);
    if (mipi_display != nullptr)
      target = mipi_display->wait_for_direct_render_frame_buffer(app_source, background_source, 50);
  }
#endif
  if (target == nullptr)
    return false;

  bool needs_sync = false;
  int dirty_y1 = this->height_;
  int dirty_y2 = -1;
  auto sync_full = [&]() { lvgl_cache_msync_external(target, fb_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M); };

  auto copy_full = [&](const lv_draw_buf_t *src) -> bool {
    if (src == nullptr)
      return false;
#if CONFIG_ESPHOME_LVGL_SNAPSHOT_DMA2D_M2M
    // This is an exact 1:1 framebuffer copy. DMA2D M2M avoids consuming the
    // LVGL core for ~35 ms every time the DSI target buffer alternates, while
    // preserving the full-resolution source used by the circular reveal.
    lvgl_cache_msync_external(src->data, visible_source_span(src), ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    lvgl_cache_msync_external(target, fb_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    if (dma2d_m2m_copy_rgb888_2d(src->data, src->header.stride / BYTES_PER_PIXEL, src->header.h, 0, 0, target, this->width_,
                                 this->height_, 0, 0, this->width_, this->height_)) {
      lvgl_cache_msync_external(target, fb_bytes, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
      return true;
    }
#endif
#ifdef USE_LVGL_PPA
    if (s_display_srm_client != nullptr) {
      ppa_srm_oper_config_t cfg = {};
      cfg.in.buffer = src->data;
      cfg.in.pic_w = src->header.stride / BYTES_PER_PIXEL;
      cfg.in.pic_h = src->header.h;
      cfg.in.block_w = this->width_;
      cfg.in.block_h = this->height_;
      cfg.in.block_offset_x = 0;
      cfg.in.block_offset_y = 0;
      cfg.in.srm_cm = PPA_SRM_COLOR_MODE_RGB888;
      cfg.out.buffer = target;
      cfg.out.buffer_size = fb_bytes;
      cfg.out.pic_w = this->width_;
      cfg.out.pic_h = this->height_;
      cfg.out.block_offset_x = 0;
      cfg.out.block_offset_y = 0;
      cfg.out.srm_cm = PPA_SRM_COLOR_MODE_RGB888;
      cfg.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
      cfg.scale_x = 1.0f;
      cfg.scale_y = 1.0f;
      cfg.alpha_update_mode = PPA_ALPHA_NO_CHANGE;
      cfg.mode = PPA_TRANS_MODE_BLOCKING;
      lvgl_cache_msync_external(src->data, visible_source_span(src), ESP_CACHE_MSYNC_FLAG_DIR_C2M);
      lvgl_cache_msync_external(target, fb_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
      if (ppa_do_scale_rotate_mirror(s_display_srm_client, &cfg) == ESP_OK) {
        lvgl_cache_msync_external(target, fb_bytes, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
        return true;
      }
    }
#endif
    const uint8_t *src_row = src->data;
    uint8_t *dst_row = target;
    for (int y = 0; y < this->height_; y++) {
      memcpy(dst_row, src_row, row_bytes);
      src_row += src->header.stride;
      dst_row += row_bytes;
    }
    needs_sync = true;
    return true;
  };

  SnapshotAppRenderBufferState *buffer_state = nullptr;
  for (auto &state : s_snapshot_app_render_buffers) {
    if (state.buffer == target) {
      buffer_state = &state;
      break;
    }
  }
  if (buffer_state == nullptr) {
    for (auto &state : s_snapshot_app_render_buffers) {
      if (state.buffer == nullptr) {
        buffer_state = &state;
        state.buffer = target;
        break;
      }
    }
  }
  if (buffer_state == nullptr) {
    buffer_state = &s_snapshot_app_render_buffers[0];
    buffer_state->buffer = target;
    buffer_state->initialized = false;
  }
  if (buffer_state->opening != s_snapshot_app_render_opening) {
    buffer_state->initialized = false;
    buffer_state->opening = s_snapshot_app_render_opening;
  }
  if (buffer_state->app_source != app->data || buffer_state->background_source !=
                                                     (background == nullptr ? nullptr : background->data)) {
    buffer_state->initialized = false;
  }

  const int diameter = std::clamp<int>(std::max(width, height), 1, std::max(this->width_, this->height_));
  const int radius = std::max(1, diameter / 2);

#if CONFIG_ESPHOME_LVGL_SNAPSHOT_DMA2D_M2M
  const bool final_close = !s_snapshot_app_render_opening && (width <= 8 || height <= 8);
  const int dma_radius = final_close ? 0 : radius;
  const bool dma_supported = background != nullptr && app->header.stride % BYTES_PER_PIXEL == 0 &&
                             background->header.stride % BYTES_PER_PIXEL == 0;
  if (dma_supported) {
    bool dma_ok = true;
    const bool target_has_frame = buffer_state->initialized;
    const int64_t prepare_started_us = esp_timer_get_time();
    if (!final_close && s_snapshot_app_dma_synced_app != app->data) {
      dma_ok = lvgl_cache_msync_external_result(app->data, (size_t) app->header.stride * app->header.h,
                                                ESP_CACHE_MSYNC_FLAG_DIR_C2M) == ESP_OK;
      if (dma_ok)
        s_snapshot_app_dma_synced_app = app->data;
    }
    if (dma_ok && s_snapshot_app_dma_synced_background != background->data) {
      dma_ok = lvgl_cache_msync_external_result(background->data, visible_source_span(background),
                                                ESP_CACHE_MSYNC_FLAG_DIR_C2M) == ESP_OK;
      if (dma_ok)
        s_snapshot_app_dma_synced_background = background->data;
    }
    if (dma_ok && !buffer_state->initialized) {
      dma_ok = lvgl_cache_msync_external_result(target, fb_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M) == ESP_OK;
    }
    prepare_us = static_cast<uint32_t>(esp_timer_get_time() - prepare_started_us);

#ifdef USE_MIPI_DSI
    if (dma_ok && this->displays_.size() == 1) {
      auto *mipi_display = static_cast<mipi_dsi::MipiDsi *>(this->displays_[0]);
      if (mipi_display != nullptr)
        lvgl_esphome_wait_snapshot_dsi_fifo();
    }
#endif
    const int64_t compose_started_us = esp_timer_get_time();
    if (dma_ok) {
      if (final_close) {
        // The last closing frame is exactly the Home background. Submit it as
        // one 2D DMA2D transfer instead of building 800 one-line circle spans.
        // Besides being cheaper, this guarantees that the outermost pixels are
        // replaced and cannot retain a thin ring from the closing app.
        dma_ok = dma2d_m2m_copy_rgb888_2d(
            background->data, background->header.stride / BYTES_PER_PIXEL, background->header.h, 0, 0, target,
            this->width_, this->height_, 0, 0, this->width_, this->height_);
      } else if (target_has_frame) {
        dma_ok = dma2d_m2m_update_rgb888_circle(
            background->data, background->header.stride / BYTES_PER_PIXEL, background->header.h, app->data,
            app->header.stride / BYTES_PER_PIXEL, app->header.h, target, this->width_, this->height_,
            buffer_state->center_x, buffer_state->center_y, buffer_state->radius, center_x, center_y, dma_radius);
      } else {
        dma_ok = dma2d_m2m_compose_rgb888_circle(
            background->data, background->header.stride / BYTES_PER_PIXEL, background->header.h, app->data,
            app->header.stride / BYTES_PER_PIXEL, app->header.h, target, this->width_, this->height_, center_x,
            center_y, dma_radius);
      }
    }
    circle_us = static_cast<uint32_t>(esp_timer_get_time() - compose_started_us);

    const int64_t sync_started_us = esp_timer_get_time();
    if (dma_ok)
      dma_ok = lvgl_cache_msync_external_result(target, fb_bytes, ESP_CACHE_MSYNC_FLAG_DIR_M2C) == ESP_OK;
    cache_sync_us = static_cast<uint32_t>(esp_timer_get_time() - sync_started_us);
    if (dma_ok) {
      buffer_state->initialized = true;
      buffer_state->app_source = app->data;
      buffer_state->background_source = background->data;
      buffer_state->radius = dma_radius;
      buffer_state->center_x = center_x;
      buffer_state->center_y = center_y;
      const int64_t present_started_us = esp_timer_get_time();
      // Keep one complete frame queued while DMA2D composes the next one into
      // the third DSI buffer. The final handoff waits for the queue explicitly.
      dma_ok = this->present_snapshot_render_buffer_(target, false);
      present_us = static_cast<uint32_t>(esp_timer_get_time() - present_started_us);
    }
    if (dma_ok) {
#ifdef USE_LVGL_FPS_BENCHMARK
      lvgl_esphome_note_frame();
#endif
      const uint32_t total_us = static_cast<uint32_t>(esp_timer_get_time() - frame_started_us);
      s_snapshot_app_last_frame_metrics = {
          .total_us = total_us,
          .prepare_us = prepare_us,
          .circle_us = circle_us,
          .cache_sync_us = cache_sync_us,
          .ppa_us = 0,
          .present_us = present_us,
      };
      if (s_perf_logging_enabled && total_us > 30000U) {
        ESP_LOGW("lvgl.app", "DMA2D frame total=%uus prepare=%uus compose=%uus sync=%uus present=%uus size=%d",
                 static_cast<unsigned>(total_us), static_cast<unsigned>(prepare_us),
                 static_cast<unsigned>(circle_us), static_cast<unsigned>(cache_sync_us),
                 static_cast<unsigned>(present_us), diameter);
      }
      return true;
    }

    static bool warned = false;
    if (!warned) {
      ESP_LOGW(TAG, "snapshot app: DMA2D circle compositor failed; using CPU fallback");
      warned = true;
    }
    buffer_state->initialized = false;
  }
#endif

#ifdef USE_LVGL_PPA
  /* Compose the circular reveal at half resolution, then let PPA expand the
   * complete frame to the display. The visible snapshots remain full
   * resolution; only the transient animation workspace is smaller. This
   * changes the hot path from RGB888 PSRAM-to-PSRAM CPU copies over an
   * 800x800 annulus to short 400x400 copies plus one hardware SRM operation. */
#ifndef CONFIG_ESPHOME_LVGL_APP_ANIMATION_HALF_RES
#define CONFIG_ESPHOME_LVGL_APP_ANIMATION_HALF_RES 0
#endif
  const bool low_res_supported = CONFIG_ESPHOME_LVGL_APP_ANIMATION_HALF_RES && background != nullptr &&
                                 s_snapshot_app_srm_client != nullptr &&
                                 this->displays_.size() == 1 && this->rotation == display::DISPLAY_ROTATION_0_DEGREES &&
                                 (this->width_ % 2) == 0 && (this->height_ % 2) == 0 &&
                                 app->header.stride % BYTES_PER_PIXEL == 0 &&
                                 background->header.stride % BYTES_PER_PIXEL == 0;
  if (low_res_supported) {
    constexpr int WORKSPACE_SCALE = 2;
    constexpr size_t WORKSPACE_ALIGNMENT = 128;
    const int workspace_width = this->width_ / WORKSPACE_SCALE;
    const int workspace_height = this->height_ / WORKSPACE_SCALE;
    const size_t workspace_row_bytes = static_cast<size_t>(workspace_width) * BYTES_PER_PIXEL;
    const size_t workspace_frame_bytes = workspace_row_bytes * static_cast<size_t>(workspace_height);
    const size_t workspace_allocation_bytes = workspace_frame_bytes * 3U;
    auto &low_res = s_snapshot_app_low_res;

    if (low_res.allocation == nullptr || low_res.width != workspace_width ||
        low_res.height != workspace_height || low_res.allocation_bytes != workspace_allocation_bytes) {
      if (low_res.allocation != nullptr)
        heap_caps_free(low_res.allocation);
      low_res = {};
      low_res.allocation = static_cast<uint8_t *>(heap_caps_aligned_alloc(
          WORKSPACE_ALIGNMENT, workspace_allocation_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
      if (low_res.allocation != nullptr) {
        low_res.app = low_res.allocation;
        low_res.background = low_res.app + workspace_frame_bytes;
        low_res.composite = low_res.background + workspace_frame_bytes;
        low_res.frame_bytes = workspace_frame_bytes;
        low_res.allocation_bytes = workspace_allocation_bytes;
        low_res.width = workspace_width;
        low_res.height = workspace_height;
      }
    }

    auto clear_direct_ranges = []() {
      s_direct_ppa_source_begin.store(0, std::memory_order_release);
      s_direct_ppa_source2_begin.store(0, std::memory_order_release);
      s_direct_ppa_target_begin.store(0, std::memory_order_release);
      s_direct_ppa_source_end.store(0, std::memory_order_relaxed);
      s_direct_ppa_source2_end.store(0, std::memory_order_relaxed);
      s_direct_ppa_target_end.store(0, std::memory_order_relaxed);
    };

    auto scale_snapshot_to_workspace = [&](const lv_draw_buf_t *source, uint8_t *destination) -> bool {
      const size_t source_bytes = static_cast<size_t>(source->header.stride) * source->header.h;
      if (lvgl_cache_msync_external_result(source->data, source_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M) != ESP_OK ||
          lvgl_cache_msync_external_result(destination, workspace_frame_bytes,
                                           ESP_CACHE_MSYNC_FLAG_DIR_M2C) != ESP_OK) {
        return false;
      }

      ppa_srm_oper_config_t config{};
      config.in.buffer = source->data;
      config.in.pic_w = source->header.stride / BYTES_PER_PIXEL;
      config.in.pic_h = source->header.h;
      config.in.block_w = this->width_;
      config.in.block_h = this->height_;
      config.in.srm_cm = PPA_SRM_COLOR_MODE_RGB888;
      config.out.buffer = destination;
      config.out.buffer_size = workspace_frame_bytes;
      config.out.pic_w = workspace_width;
      config.out.pic_h = workspace_height;
      config.out.srm_cm = PPA_SRM_COLOR_MODE_RGB888;
      config.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
      config.scale_x = 1.0f / WORKSPACE_SCALE;
      config.scale_y = 1.0f / WORKSPACE_SCALE;
      config.alpha_update_mode = PPA_ALPHA_NO_CHANGE;
      config.mode = PPA_TRANS_MODE_BLOCKING;

      const uintptr_t source_begin = reinterpret_cast<uintptr_t>(source->data);
      const uintptr_t target_begin = reinterpret_cast<uintptr_t>(destination);
      s_direct_ppa_source_begin.store(source_begin, std::memory_order_release);
      s_direct_ppa_source_end.store(source_begin + source_bytes, std::memory_order_release);
      s_direct_ppa_target_begin.store(target_begin, std::memory_order_release);
      s_direct_ppa_target_end.store(target_begin + workspace_frame_bytes, std::memory_order_release);
      const esp_err_t result = ppa_do_scale_rotate_mirror(s_snapshot_app_srm_client, &config);
      clear_direct_ranges();
      if (result != ESP_OK)
        return false;
      return lvgl_cache_msync_external_result(destination, workspace_frame_bytes,
                                              ESP_CACHE_MSYNC_FLAG_DIR_M2C) == ESP_OK;
    };

    bool low_res_ok = low_res.allocation != nullptr;
    bool low_res_initial_copy = false;
    const int64_t prepare_started_us = esp_timer_get_time();
    if (low_res_ok &&
        (!low_res.initialized || low_res.app_source != app->data || low_res.background_source != background->data)) {
      low_res_ok = scale_snapshot_to_workspace(app, low_res.app) &&
                   scale_snapshot_to_workspace(background, low_res.background);
      if (low_res_ok) {
        const uint8_t *initial_frame = s_snapshot_app_render_opening ? low_res.background : low_res.app;
        memcpy(low_res.composite, initial_frame, workspace_frame_bytes);
        low_res.radius = s_snapshot_app_render_opening ? 0 : std::min(workspace_width, workspace_height) / 2;
        low_res.center_x = workspace_width / 2;
        low_res.center_y = workspace_height / 2;
        low_res.app_source = app->data;
        low_res.background_source = background->data;
        low_res.initialized = true;
        low_res_initial_copy = true;
      }
    }
    if (low_res_initial_copy)
      prepare_us = static_cast<uint32_t>(esp_timer_get_time() - prepare_started_us);

    int low_res_dirty_y1 = workspace_height;
    int low_res_dirty_y2 = -1;
    auto low_res_circle_span = [&](int y, int circle_center_x, int circle_center_y, int circle_radius, int *x1,
                                   int *x2) -> bool {
      if (circle_radius <= 0)
        return false;
      const int dy = y - circle_center_y;
      const int radius_sq = circle_radius * circle_radius;
      if (dy * dy > radius_sq)
        return false;
      const int span = static_cast<int>(integer_sqrt_u32(static_cast<uint32_t>(radius_sq - dy * dy)));
      *x1 = std::clamp(circle_center_x - span, 0, workspace_width - 1);
      *x2 = std::clamp(circle_center_x + span, 0, workspace_width - 1);
      return *x2 >= *x1;
    };
    auto low_res_copy_span = [&](int y, int x1, int x2, const uint8_t *source) {
      x1 = std::clamp(x1, 0, workspace_width - 1);
      x2 = std::clamp(x2, 0, workspace_width - 1);
      if (x2 < x1)
        return;
      const size_t offset = static_cast<size_t>(y) * workspace_row_bytes +
                            static_cast<size_t>(x1) * BYTES_PER_PIXEL;
      memcpy(low_res.composite + offset, source + offset,
             static_cast<size_t>(x2 - x1 + 1) * BYTES_PER_PIXEL);
      low_res_dirty_y1 = std::min(low_res_dirty_y1, y);
      low_res_dirty_y2 = std::max(low_res_dirty_y2, y);
    };
    auto update_low_res_circle = [&](int new_center_x, int new_center_y, int new_radius) {
      for (int y = 0; y < workspace_height; y++) {
        int old_x1 = 0;
        int old_x2 = -1;
        int new_x1 = 0;
        int new_x2 = -1;
        const bool has_old = low_res_circle_span(y, low_res.center_x, low_res.center_y, low_res.radius, &old_x1,
                                                 &old_x2);
        const bool has_new =
            low_res_circle_span(y, new_center_x, new_center_y, new_radius, &new_x1, &new_x2);
        if (has_old) {
          if (!has_new) {
            low_res_copy_span(y, old_x1, old_x2, low_res.background);
          } else {
            low_res_copy_span(y, old_x1, std::min(old_x2, new_x1 - 1), low_res.background);
            low_res_copy_span(y, std::max(old_x1, new_x2 + 1), old_x2, low_res.background);
          }
        }
        if (has_new) {
          if (!has_old) {
            low_res_copy_span(y, new_x1, new_x2, low_res.app);
          } else {
            low_res_copy_span(y, new_x1, std::min(new_x2, old_x1 - 1), low_res.app);
            low_res_copy_span(y, std::max(new_x1, old_x2 + 1), new_x2, low_res.app);
          }
        }
      }
      low_res.radius = new_radius;
      low_res.center_x = new_center_x;
      low_res.center_y = new_center_y;
    };

    const bool final_close = !s_snapshot_app_render_opening && (width <= 8 || height <= 8);
    const int workspace_center_x = std::clamp(center_x / WORKSPACE_SCALE, 0, workspace_width - 1);
    const int workspace_center_y = std::clamp(center_y / WORKSPACE_SCALE, 0, workspace_height - 1);
    const int workspace_radius = final_close ? 0 : std::max(1, radius / WORKSPACE_SCALE);
    if (low_res_ok) {
      const int64_t circle_started_us = esp_timer_get_time();
      update_low_res_circle(workspace_center_x, workspace_center_y, workspace_radius);
      circle_us = static_cast<uint32_t>(esp_timer_get_time() - circle_started_us);

      const int64_t sync_started_us = esp_timer_get_time();
      if (low_res_initial_copy) {
        low_res_ok = lvgl_cache_msync_external_result(low_res.composite, workspace_frame_bytes,
                                                      ESP_CACHE_MSYNC_FLAG_DIR_C2M) == ESP_OK;
      } else if (low_res_dirty_y2 >= low_res_dirty_y1) {
        low_res_ok = lvgl_cache_msync_external_result(
                         low_res.composite + static_cast<size_t>(low_res_dirty_y1) * workspace_row_bytes,
                         static_cast<size_t>(low_res_dirty_y2 - low_res_dirty_y1 + 1) * workspace_row_bytes,
                         ESP_CACHE_MSYNC_FLAG_DIR_C2M) == ESP_OK;
      }
      cache_sync_us = static_cast<uint32_t>(esp_timer_get_time() - sync_started_us);
    }

    auto *mipi_display = static_cast<mipi_dsi::MipiDsi *>(this->displays_[0]);
    SnapshotAppLowResolutionState::TargetState *target_state = nullptr;
    int source_x = 0;
    int source_y = 0;
    int source_width = workspace_width;
    int source_height = workspace_height;
    if (low_res_ok && mipi_display != nullptr && mipi_display->get_frame_buffer_size() >= fb_bytes) {
      size_t free_target_slot = low_res.targets.size();
      for (size_t index = 0; index < low_res.targets.size(); index++) {
        if (low_res.targets[index].buffer == target) {
          target_state = &low_res.targets[index];
          break;
        }
        if (free_target_slot == low_res.targets.size() && low_res.targets[index].buffer == nullptr)
          free_target_slot = index;
      }
      if (target_state == nullptr) {
        if (free_target_slot == low_res.targets.size())
          free_target_slot = 0;
        target_state = &low_res.targets[free_target_slot];
        *target_state = {};
        target_state->buffer = target;
      }

      if (!target_state->initialized) {
        constexpr size_t CACHE_HANDOFF_CHUNK = 64U * 1024U;
        for (size_t offset = 0; offset < fb_bytes && low_res_ok; offset += CACHE_HANDOFF_CHUNK) {
          const size_t chunk = std::min(CACHE_HANDOFF_CHUNK, fb_bytes - offset);
          lvgl_esphome_wait_snapshot_dsi_fifo();
          low_res_ok = lvgl_cache_msync_external_result(target + offset, chunk,
                                                        ESP_CACHE_MSYNC_FLAG_DIR_M2C) == ESP_OK;
        }
      } else {
        auto include_circle = [&](int circle_center_x, int circle_center_y, int circle_radius) {
          if (circle_radius <= 0)
            return;
          const int x1 = std::clamp(circle_center_x - circle_radius - 1, 0, workspace_width - 1);
          const int y1 = std::clamp(circle_center_y - circle_radius - 1, 0, workspace_height - 1);
          const int x2 = std::clamp(circle_center_x + circle_radius + 1, 0, workspace_width - 1);
          const int y2 = std::clamp(circle_center_y + circle_radius + 1, 0, workspace_height - 1);
          if (source_width == 0 || source_height == 0) {
            source_x = x1;
            source_y = y1;
            source_width = x2 - x1 + 1;
            source_height = y2 - y1 + 1;
            return;
          }
          const int union_x1 = std::min(source_x, x1);
          const int union_y1 = std::min(source_y, y1);
          const int union_x2 = std::max(source_x + source_width - 1, x2);
          const int union_y2 = std::max(source_y + source_height - 1, y2);
          source_x = union_x1;
          source_y = union_y1;
          source_width = union_x2 - union_x1 + 1;
          source_height = union_y2 - union_y1 + 1;
        };

        source_width = 0;
        source_height = 0;
        include_circle(target_state->center_x, target_state->center_y, target_state->radius);
        include_circle(workspace_center_x, workspace_center_y, workspace_radius);

        if (source_width > 0 && source_height > 0) {
          const int output_y = source_y * WORKSPACE_SCALE;
          const int output_height = source_height * WORKSPACE_SCALE;
          low_res_ok = lvgl_cache_msync_external_result(
                           target + static_cast<size_t>(output_y) * row_bytes,
                           static_cast<size_t>(output_height) * row_bytes,
                           ESP_CACHE_MSYNC_FLAG_DIR_M2C) == ESP_OK;
        }
      }
    } else {
      low_res_ok = false;
    }

    if (low_res_ok) {
      uint32_t ppa_us = 0;
      if (source_width > 0 && source_height > 0) {
        ppa_srm_oper_config_t config{};
        config.in.buffer = low_res.composite;
        config.in.pic_w = workspace_width;
        config.in.pic_h = workspace_height;
        config.in.block_w = source_width;
        config.in.block_h = source_height;
        config.in.block_offset_x = source_x;
        config.in.block_offset_y = source_y;
        config.in.srm_cm = PPA_SRM_COLOR_MODE_RGB888;
        config.out.buffer = target;
        config.out.buffer_size = fb_bytes;
        config.out.pic_w = this->width_;
        config.out.pic_h = this->height_;
        config.out.block_offset_x = source_x * WORKSPACE_SCALE;
        config.out.block_offset_y = source_y * WORKSPACE_SCALE;
        config.out.srm_cm = PPA_SRM_COLOR_MODE_RGB888;
        config.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
        config.scale_x = static_cast<float>(WORKSPACE_SCALE);
        config.scale_y = static_cast<float>(WORKSPACE_SCALE);
        config.alpha_update_mode = PPA_ALPHA_NO_CHANGE;
        config.mode = PPA_TRANS_MODE_BLOCKING;

        const uintptr_t source_begin = reinterpret_cast<uintptr_t>(low_res.composite);
        const uintptr_t target_begin = reinterpret_cast<uintptr_t>(target);
        s_direct_ppa_source_begin.store(source_begin, std::memory_order_release);
        s_direct_ppa_source_end.store(source_begin + workspace_frame_bytes, std::memory_order_release);
        s_direct_ppa_target_begin.store(target_begin, std::memory_order_release);
        s_direct_ppa_target_end.store(target_begin + fb_bytes, std::memory_order_release);
        if (esphome_mipi_dsi_wait_fifo_margin != nullptr) {
          esphome_mipi_dsi_wait_fifo_margin(CONFIG_ESPHOME_LVGL_PPA_SRM_TRANSFORM_DSI_FIFO_MIN,
                                            CONFIG_ESPHOME_LVGL_PPA_SRM_TRANSFORM_DSI_WAIT_US);
        }
        lv_draw_ppa_direct_animation_qos_apply();
        const int64_t ppa_started_us = esp_timer_get_time();
        const esp_err_t result = ppa_do_scale_rotate_mirror(s_snapshot_app_srm_client, &config);
        ppa_us = static_cast<uint32_t>(esp_timer_get_time() - ppa_started_us);
        lv_draw_ppa_direct_animation_qos_restore();
        clear_direct_ranges();
        low_res_ok = result == ESP_OK;
      }

      if (low_res_ok && target_state != nullptr) {
        target_state->radius = workspace_radius;
        target_state->center_x = workspace_center_x;
        target_state->center_y = workspace_center_y;
        target_state->initialized = true;
      }

      if (low_res_ok) {
        const int64_t present_started_us = esp_timer_get_time();
        low_res_ok = this->present_snapshot_render_buffer_(target);
        present_us = static_cast<uint32_t>(esp_timer_get_time() - present_started_us);
      }
      if (low_res_ok) {
#ifdef USE_LVGL_FPS_BENCHMARK
        lvgl_esphome_note_frame();
#endif
        const uint32_t total_us = static_cast<uint32_t>(esp_timer_get_time() - frame_started_us);
        s_snapshot_app_last_frame_metrics = {
            .total_us = total_us,
            .prepare_us = prepare_us,
            .circle_us = circle_us,
            .cache_sync_us = cache_sync_us,
            .ppa_us = ppa_us,
            .present_us = present_us,
        };
        // Per-frame warnings contend with the animation worker and distort the
        // timings they are meant to diagnose. The worker publishes one
        // aggregate record after each transition; retain this only as a guard
        // for a genuinely stalled frame.
        if (s_perf_logging_enabled && total_us > 200000U) {
          ESP_LOGW("lvgl.app", "half-res frame total=%uus circle=%uus sync=%uus ppa=%uus present=%uus size=%d",
                   static_cast<unsigned>(total_us), static_cast<unsigned>(circle_us),
                   static_cast<unsigned>(cache_sync_us), static_cast<unsigned>(ppa_us),
                   static_cast<unsigned>(present_us), diameter);
        }
        return true;
      }
    }

    static bool warned = false;
    if (!warned) {
      ESP_LOGW(TAG, "snapshot app: half-resolution PPA compositor failed; using full-resolution CPU fallback");
      warned = true;
    }
    clear_direct_ranges();
    low_res.initialized = false;
  }
#endif

#if 0  // Full-frame SRM+blend traverses PSRAM twice per frame and is slower than the incremental compositor.
  const bool ppa_supported = background != nullptr && this->width_ == this->height_ &&
                             app->header.stride >= this->width_ * BYTES_PER_PIXEL &&
                             background->header.stride >= this->width_ * BYTES_PER_PIXEL &&
                             app->header.stride % BYTES_PER_PIXEL == 0 &&
                             background->header.stride % BYTES_PER_PIXEL == 0 &&
                             s_snapshot_app_srm_client != nullptr && s_snapshot_app_blend_client != nullptr &&
                             this->displays_.size() == 1;
  if (ppa_supported) {
    auto *mipi_display = app_mipi_display;
    const size_t argb_frame_bytes = (size_t) this->width_ * (size_t) this->height_ * 4U;
    const size_t allocation_bytes = argb_frame_bytes * 2U;

    auto ensure_argb_source = [&]() -> bool {
      if (s_snapshot_app_ppa.allocation != nullptr && s_snapshot_app_ppa.width == this->width_ &&
          s_snapshot_app_ppa.height == this->height_ && s_snapshot_app_ppa.source_rgb == app->data)
        return true;

      snapshot_app_ppa_release();
      constexpr size_t ALIGNMENT = 128;
      const uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT;
      auto *allocation = static_cast<uint8_t *>(heap_caps_aligned_alloc(ALIGNMENT, allocation_bytes, caps));
      if (allocation == nullptr) {
        ESP_LOGW(TAG, "snapshot app: unable to allocate %u-byte PPA workspace (largest PSRAM block=%u)",
                 static_cast<unsigned>(allocation_bytes),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
        return false;
      }

      s_snapshot_app_ppa.allocation = allocation;
      s_snapshot_app_ppa.source_argb = allocation;
      s_snapshot_app_ppa.scaled_argb = allocation + argb_frame_bytes;
      s_snapshot_app_ppa.frame_bytes = argb_frame_bytes;
      s_snapshot_app_ppa.allocation_bytes = allocation_bytes;
      s_snapshot_app_ppa.width = this->width_;
      s_snapshot_app_ppa.height = this->height_;
      s_snapshot_app_ppa.source_rgb = app->data;

      // PPA blend has no independent mask input. Build one ARGB source once
      // per animation: app RGB plus a two-pixel antialiased circular alpha
      // edge. Every following frame is then only one SRM and one blend.
      const int mask_diameter = std::min(this->width_, this->height_);
      const int outer_radius2 = mask_diameter + 1;
      const int inner_radius2 = std::max(1, outer_radius2 - 4);
      const int64_t outer_sq = (int64_t) outer_radius2 * outer_radius2;
      const int64_t inner_sq = (int64_t) inner_radius2 * inner_radius2;
      const int64_t edge_span = std::max<int64_t>(1, outer_sq - inner_sq);
      for (int y = 0; y < this->height_; y++) {
        const uint8_t *src = app->data + (size_t) y * app->header.stride;
        uint8_t *dst = s_snapshot_app_ppa.source_argb + (size_t) y * (size_t) this->width_ * 4U;
        const int dy2 = 2 * y - (this->height_ - 1);
        for (int x = 0; x < this->width_; x++) {
          const int dx2 = 2 * x - (this->width_ - 1);
          const int64_t distance_sq = (int64_t) dx2 * dx2 + (int64_t) dy2 * dy2;
          uint8_t alpha = 0;
          if (distance_sq <= inner_sq) {
            alpha = 0xFF;
          } else if (distance_sq < outer_sq) {
            alpha = static_cast<uint8_t>(((outer_sq - distance_sq) * 255 + edge_span / 2) / edge_span);
          }
          dst[0] = src[0];
          dst[1] = src[1];
          dst[2] = src[2];
          dst[3] = alpha;
          src += BYTES_PER_PIXEL;
          dst += 4;
        }
      }
      if (lvgl_cache_msync_external_result(s_snapshot_app_ppa.source_argb, argb_frame_bytes,
                                           ESP_CACHE_MSYNC_FLAG_DIR_C2M) != ESP_OK) {
        snapshot_app_ppa_release();
        return false;
      }
      return true;
    };

    auto clear_direct_ranges = []() {
      s_direct_ppa_source_begin.store(0, std::memory_order_release);
      s_direct_ppa_source2_begin.store(0, std::memory_order_release);
      s_direct_ppa_target_begin.store(0, std::memory_order_release);
      s_direct_ppa_source_end.store(0, std::memory_order_relaxed);
      s_direct_ppa_source2_end.store(0, std::memory_order_relaxed);
      s_direct_ppa_target_end.store(0, std::memory_order_relaxed);
    };

    auto copy_background_rect = [&](int x1, int y1, int x2, int y2) -> bool {
      x1 = std::clamp(x1, 0, this->width_ - 1);
      y1 = std::clamp(y1, 0, this->height_ - 1);
      x2 = std::clamp(x2, 0, this->width_ - 1);
      y2 = std::clamp(y2, 0, this->height_ - 1);
      if (x2 < x1 || y2 < y1)
        return true;

      ppa_srm_oper_config_t cfg{};
      cfg.in.buffer = background->data;
      cfg.in.pic_w = background->header.stride / BYTES_PER_PIXEL;
      cfg.in.pic_h = background->header.h;
      cfg.in.block_w = x2 - x1 + 1;
      cfg.in.block_h = y2 - y1 + 1;
      cfg.in.block_offset_x = x1;
      cfg.in.block_offset_y = y1;
      cfg.in.srm_cm = PPA_SRM_COLOR_MODE_RGB888;
      cfg.out.buffer = target;
      cfg.out.buffer_size = fb_bytes;
      cfg.out.pic_w = this->width_;
      cfg.out.pic_h = this->height_;
      cfg.out.block_offset_x = x1;
      cfg.out.block_offset_y = y1;
      cfg.out.srm_cm = PPA_SRM_COLOR_MODE_RGB888;
      cfg.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
      cfg.scale_x = 1.0f;
      cfg.scale_y = 1.0f;
      cfg.alpha_update_mode = PPA_ALPHA_NO_CHANGE;
      cfg.mode = PPA_TRANS_MODE_BLOCKING;

      const uintptr_t source_begin = reinterpret_cast<uintptr_t>(background->data);
      const uintptr_t target_begin = reinterpret_cast<uintptr_t>(target);
      s_direct_ppa_source_begin.store(source_begin, std::memory_order_release);
      s_direct_ppa_source_end.store(source_begin + (size_t) background->header.stride * this->height_,
                                    std::memory_order_release);
      s_direct_ppa_target_begin.store(target_begin, std::memory_order_release);
      s_direct_ppa_target_end.store(target_begin + fb_bytes, std::memory_order_release);
      const esp_err_t result = ppa_do_scale_rotate_mirror(s_snapshot_app_srm_client, &cfg);
      clear_direct_ranges();
      return result == ESP_OK;
    };

    auto scale_argb = [&](int scaled_size) -> bool {
      ppa_srm_oper_config_t cfg{};
      cfg.in.buffer = s_snapshot_app_ppa.source_argb;
      cfg.in.pic_w = this->width_;
      cfg.in.pic_h = this->height_;
      cfg.in.block_w = this->width_;
      cfg.in.block_h = this->height_;
      cfg.in.srm_cm = PPA_SRM_COLOR_MODE_ARGB8888;
      cfg.out.buffer = s_snapshot_app_ppa.scaled_argb;
      cfg.out.buffer_size = s_snapshot_app_ppa.frame_bytes;
      cfg.out.pic_w = scaled_size;
      cfg.out.pic_h = scaled_size;
      cfg.out.srm_cm = PPA_SRM_COLOR_MODE_ARGB8888;
      cfg.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
      cfg.scale_x = (float) scaled_size / (float) this->width_;
      cfg.scale_y = (float) scaled_size / (float) this->height_;
      cfg.alpha_update_mode = PPA_ALPHA_NO_CHANGE;
      cfg.mode = PPA_TRANS_MODE_BLOCKING;

      const uintptr_t source_begin = reinterpret_cast<uintptr_t>(s_snapshot_app_ppa.source_argb);
      const uintptr_t target_begin = reinterpret_cast<uintptr_t>(s_snapshot_app_ppa.scaled_argb);
      s_direct_ppa_source_begin.store(source_begin, std::memory_order_release);
      s_direct_ppa_source_end.store(source_begin + s_snapshot_app_ppa.frame_bytes, std::memory_order_release);
      s_direct_ppa_target_begin.store(target_begin, std::memory_order_release);
      s_direct_ppa_target_end.store(target_begin + s_snapshot_app_ppa.frame_bytes, std::memory_order_release);
      const esp_err_t result = ppa_do_scale_rotate_mirror(s_snapshot_app_srm_client, &cfg);
      clear_direct_ranges();
      return result == ESP_OK;
    };

    auto blend_argb = [&](int scaled_size, int dst_x, int dst_y) -> bool {
      ppa_blend_oper_config_t cfg{};
      cfg.in_bg.buffer = target;
      cfg.in_bg.pic_w = this->width_;
      cfg.in_bg.pic_h = this->height_;
      cfg.in_bg.block_w = scaled_size;
      cfg.in_bg.block_h = scaled_size;
      cfg.in_bg.block_offset_x = dst_x;
      cfg.in_bg.block_offset_y = dst_y;
      cfg.in_bg.blend_cm = PPA_BLEND_COLOR_MODE_RGB888;
      cfg.in_fg.buffer = s_snapshot_app_ppa.scaled_argb;
      cfg.in_fg.pic_w = scaled_size;
      cfg.in_fg.pic_h = scaled_size;
      cfg.in_fg.block_w = scaled_size;
      cfg.in_fg.block_h = scaled_size;
      cfg.in_fg.blend_cm = PPA_BLEND_COLOR_MODE_ARGB8888;
      cfg.out.buffer = target;
      cfg.out.buffer_size = fb_bytes;
      cfg.out.pic_w = this->width_;
      cfg.out.pic_h = this->height_;
      cfg.out.block_offset_x = dst_x;
      cfg.out.block_offset_y = dst_y;
      cfg.out.blend_cm = PPA_BLEND_COLOR_MODE_RGB888;
      cfg.bg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
      cfg.bg_alpha_fix_val = 0xFF;
      cfg.fg_alpha_update_mode = PPA_ALPHA_NO_CHANGE;
      cfg.mode = PPA_TRANS_MODE_BLOCKING;

      const uintptr_t source_begin = reinterpret_cast<uintptr_t>(s_snapshot_app_ppa.scaled_argb);
      const uintptr_t target_begin = reinterpret_cast<uintptr_t>(target);
      s_direct_ppa_source_begin.store(source_begin, std::memory_order_release);
      s_direct_ppa_source_end.store(source_begin + s_snapshot_app_ppa.frame_bytes, std::memory_order_release);
      s_direct_ppa_source2_begin.store(target_begin, std::memory_order_release);
      s_direct_ppa_source2_end.store(target_begin + fb_bytes, std::memory_order_release);
      s_direct_ppa_target_begin.store(target_begin, std::memory_order_release);
      s_direct_ppa_target_end.store(target_begin + fb_bytes, std::memory_order_release);
      const esp_err_t result = ppa_do_blend(s_snapshot_app_blend_client, &cfg);
      clear_direct_ranges();
      return result == ESP_OK;
    };

    bool ppa_ok = mipi_display != nullptr && ensure_argb_source();
    if (ppa_ok && s_snapshot_app_ppa.synced_background != background->data) {
      ppa_ok = lvgl_cache_msync_external_result(background->data,
                                                (size_t) background->header.stride * this->height_,
                                                ESP_CACHE_MSYNC_FLAG_DIR_C2M) == ESP_OK;
      if (ppa_ok)
        s_snapshot_app_ppa.synced_background = background->data;
    }

    uint32_t handoff_us = 0;
    uint32_t restore_us = 0;
    uint32_t scale_us = 0;
    uint32_t blend_us = 0;
    if (ppa_ok && !buffer_state->initialized) {
      const int64_t handoff_started_us = esp_timer_get_time();
      constexpr size_t CACHE_HANDOFF_CHUNK = 64U * 1024U;
      for (size_t offset = 0; offset < fb_bytes && ppa_ok; offset += CACHE_HANDOFF_CHUNK) {
        const size_t chunk = std::min(CACHE_HANDOFF_CHUNK, fb_bytes - offset);
        lvgl_esphome_wait_snapshot_dsi_fifo();
        ppa_ok = lvgl_cache_msync_external_result(target + offset, chunk,
                                                  ESP_CACHE_MSYNC_FLAG_DIR_M2C) == ESP_OK;
      }
      handoff_us = static_cast<uint32_t>(esp_timer_get_time() - handoff_started_us);
      if (ppa_ok)
        ppa_ok = copy_background_rect(0, 0, this->width_ - 1, this->height_ - 1);
      if (ppa_ok) {
        buffer_state->radius = 0;
        buffer_state->center_x = center_x;
        buffer_state->center_y = center_y;
        buffer_state->initialized = true;
      }
    }

    if (ppa_ok) {
      if (esphome_mipi_dsi_wait_fifo_margin != nullptr)
        esphome_mipi_dsi_wait_fifo_margin(896, 1500);
      lv_draw_ppa_direct_animation_qos_apply();

      const bool final_close = !s_snapshot_app_render_opening && (width <= 8 || height <= 8);
      const int previous_radius = buffer_state->radius;
      const int previous_center_x = buffer_state->center_x;
      const int previous_center_y = buffer_state->center_y;
      if (!s_snapshot_app_render_opening && previous_radius > 0) {
        const int64_t restore_started_us = esp_timer_get_time();
        ppa_ok = copy_background_rect(previous_center_x - previous_radius - 2,
                                      previous_center_y - previous_radius - 2,
                                      previous_center_x + previous_radius + 2,
                                      previous_center_y + previous_radius + 2);
        restore_us = static_cast<uint32_t>(esp_timer_get_time() - restore_started_us);
      }

      int scaled_size = 0;
      if (ppa_ok && !final_close) {
        // ESP32-P4 stores SRM scale factors with four fractional bits. Render
        // the exact hardware size instead of asking it to fill a larger,
        // partially unwritten rectangle. At 800 px this yields 50 px steps,
        // i.e. roughly 30 distinct full-quality frames over a 500 ms reveal.
        const uint32_t scale_q16 = std::clamp<uint32_t>(
            (static_cast<uint32_t>(diameter) * 16U + static_cast<uint32_t>(this->width_) / 2U) /
                static_cast<uint32_t>(this->width_),
            1U, 16U);
        scaled_size = static_cast<int>((static_cast<uint32_t>(this->width_) * scale_q16) / 16U);
        const int dst_x = std::clamp(center_x - scaled_size / 2, 0, this->width_ - scaled_size);
        const int dst_y = std::clamp(center_y - scaled_size / 2, 0, this->height_ - scaled_size);

        const int64_t scale_started_us = esp_timer_get_time();
        ppa_ok = scale_argb(scaled_size);
        scale_us = static_cast<uint32_t>(esp_timer_get_time() - scale_started_us);
        if (ppa_ok) {
          const int64_t blend_started_us = esp_timer_get_time();
          ppa_ok = blend_argb(scaled_size, dst_x, dst_y);
          blend_us = static_cast<uint32_t>(esp_timer_get_time() - blend_started_us);
        }
        if (ppa_ok) {
          buffer_state->radius = scaled_size / 2;
          buffer_state->center_x = dst_x + scaled_size / 2;
          buffer_state->center_y = dst_y + scaled_size / 2;
        }
      } else if (ppa_ok) {
        buffer_state->radius = 0;
        buffer_state->center_x = center_x;
        buffer_state->center_y = center_y;
      }

      lv_draw_ppa_direct_animation_qos_restore();
      clear_direct_ranges();

      if (ppa_ok) {
        const int64_t present_started_us = esp_timer_get_time();
        ppa_ok = mipi_display->queue_direct_frame_buffer(target, 50);
        present_us = static_cast<uint32_t>(esp_timer_get_time() - present_started_us);
        if (ppa_ok)
          this->direct_last_flushed_buf_ = target;
      }
    }

    if (ppa_ok) {
#ifdef USE_LVGL_FPS_BENCHMARK
      lvgl_esphome_note_frame();
#endif
      const uint32_t total_us = static_cast<uint32_t>(esp_timer_get_time() - frame_started_us);
      if (s_perf_logging_enabled && total_us > 30000U) {
        ESP_LOGW("lvgl.app",
                 "PPA frame total=%uus handoff=%uus restore=%uus scale=%uus blend=%uus present=%uus size=%d",
                 static_cast<unsigned>(total_us), static_cast<unsigned>(handoff_us),
                 static_cast<unsigned>(restore_us), static_cast<unsigned>(scale_us),
                 static_cast<unsigned>(blend_us), static_cast<unsigned>(present_us), diameter);
      }
      return true;
    }

    static bool warned = false;
    if (!warned) {
      ESP_LOGW(TAG, "snapshot app: PPA reveal failed; using CPU fallback");
      warned = true;
    }
    clear_direct_ranges();
    lv_draw_ppa_direct_animation_qos_restore();
    buffer_state->initialized = false;
  }
#endif

  if (!buffer_state->initialized) {
    const int64_t init_started_us = esp_timer_get_time();
    if (!copy_full(background)) {
      memset(target, 0, fb_bytes);
      needs_sync = true;
    }
    init_copy_us = (uint32_t) (esp_timer_get_time() - init_started_us);
    buffer_state->radius = 0;
    buffer_state->center_x = center_x;
    buffer_state->center_y = center_y;
    buffer_state->initialized = true;
  }

#if 0
  const bool use_argb_ppa = s_snapshot_app_argb_source != nullptr && s_snapshot_app_argb_scratch != nullptr &&
                            s_snapshot_app_argb_width == this->width_ &&
                            s_snapshot_app_argb_height == this->height_ && s_display_srm_client != nullptr &&
                            s_display_blend_client != nullptr && background != nullptr;
  if (use_argb_ppa) {
    uint32_t restore_us = 0;
    uint32_t scale_us = 0;
    uint32_t blend_us = 0;
    bool ppa_ok = true;
    const int previous_radius = buffer_state->radius;
    const int previous_center_y = buffer_state->center_y;

    auto restore_background_rect = [&](int old_center_x, int old_center_y, int old_radius) -> bool {
      if (old_radius <= 0)
        return true;
      const int x1 = std::clamp(old_center_x - old_radius - 2, 0, this->width_ - 1);
      const int y1 = std::clamp(old_center_y - old_radius - 2, 0, this->height_ - 1);
      const int x2 = std::clamp(old_center_x + old_radius + 2, 0, this->width_ - 1);
      const int y2 = std::clamp(old_center_y + old_radius + 2, 0, this->height_ - 1);
      const int rect_w = x2 - x1 + 1;
      const int rect_h = y2 - y1 + 1;
      if (rect_w <= 0 || rect_h <= 0)
        return true;

      ppa_srm_oper_config_t cfg{};
      cfg.in.buffer = background->data;
      cfg.in.pic_w = background->header.w;
      cfg.in.pic_h = background->header.h;
      cfg.in.block_w = rect_w;
      cfg.in.block_h = rect_h;
      cfg.in.block_offset_x = x1;
      cfg.in.block_offset_y = y1;
      cfg.in.srm_cm = PPA_SRM_COLOR_MODE_RGB888;
      cfg.out.buffer = target;
      cfg.out.buffer_size = fb_bytes;
      cfg.out.pic_w = this->width_;
      cfg.out.pic_h = this->height_;
      cfg.out.block_offset_x = x1;
      cfg.out.block_offset_y = y1;
      cfg.out.srm_cm = PPA_SRM_COLOR_MODE_RGB888;
      cfg.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
      cfg.scale_x = 1.0f;
      cfg.scale_y = 1.0f;
      cfg.alpha_update_mode = PPA_ALPHA_NO_CHANGE;
      cfg.mode = PPA_TRANS_MODE_BLOCKING;
      lvgl_cache_msync_external(background->data + (size_t) y1 * background->header.stride,
                                (size_t) rect_h * background->header.stride, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
      lvgl_cache_msync_external(target + (size_t) y1 * row_bytes, (size_t) rect_h * row_bytes,
                                ESP_CACHE_MSYNC_FLAG_DIR_C2M);
      return ppa_do_scale_rotate_mirror(s_display_srm_client, &cfg) == ESP_OK;
    };

    auto scale_argb = [&](int scaled_size) -> bool {
      ppa_srm_oper_config_t cfg{};
      cfg.in.buffer = s_snapshot_app_argb_source;
      cfg.in.pic_w = this->width_;
      cfg.in.pic_h = this->height_;
      cfg.in.block_w = this->width_;
      cfg.in.block_h = this->height_;
      cfg.in.srm_cm = PPA_SRM_COLOR_MODE_ARGB8888;
      cfg.out.buffer = s_snapshot_app_argb_scratch;
      cfg.out.buffer_size = s_snapshot_app_argb_capacity;
      cfg.out.pic_w = scaled_size;
      cfg.out.pic_h = scaled_size;
      cfg.out.srm_cm = PPA_SRM_COLOR_MODE_ARGB8888;
      cfg.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
      cfg.scale_x = (float) scaled_size / (float) this->width_;
      cfg.scale_y = (float) scaled_size / (float) this->height_;
      cfg.alpha_update_mode = PPA_ALPHA_NO_CHANGE;
      cfg.mode = PPA_TRANS_MODE_BLOCKING;
      return ppa_do_scale_rotate_mirror(s_display_srm_client, &cfg) == ESP_OK;
    };

    auto blend_argb = [&](int scaled_size, int dst_x, int dst_y) -> bool {
      ppa_blend_oper_config_t cfg{};
      cfg.in_bg.buffer = target;
      cfg.in_bg.pic_w = this->width_;
      cfg.in_bg.pic_h = this->height_;
      cfg.in_bg.block_w = scaled_size;
      cfg.in_bg.block_h = scaled_size;
      cfg.in_bg.block_offset_x = dst_x;
      cfg.in_bg.block_offset_y = dst_y;
      cfg.in_bg.blend_cm = PPA_BLEND_COLOR_MODE_RGB888;
      cfg.in_fg.buffer = s_snapshot_app_argb_scratch;
      cfg.in_fg.pic_w = scaled_size;
      cfg.in_fg.pic_h = scaled_size;
      cfg.in_fg.block_w = scaled_size;
      cfg.in_fg.block_h = scaled_size;
      cfg.in_fg.blend_cm = PPA_BLEND_COLOR_MODE_ARGB8888;
      cfg.out.buffer = target;
      cfg.out.buffer_size = fb_bytes;
      cfg.out.pic_w = this->width_;
      cfg.out.pic_h = this->height_;
      cfg.out.block_offset_x = dst_x;
      cfg.out.block_offset_y = dst_y;
      cfg.out.blend_cm = PPA_BLEND_COLOR_MODE_RGB888;
      cfg.bg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
      cfg.bg_alpha_fix_val = 0xFF;
      cfg.fg_alpha_update_mode = PPA_ALPHA_NO_CHANGE;
      cfg.mode = PPA_TRANS_MODE_BLOCKING;
      return ppa_do_blend(s_display_blend_client, &cfg) == ESP_OK;
    };

    if (esphome_mipi_dsi_wait_fifo_margin != nullptr)
      esphome_mipi_dsi_wait_fifo_margin(896, 1500);
    lv_draw_ppa_direct_animation_qos_apply();

    // Opening only grows the masked foreground, so old pixels remain valid.
    // Closing shrinks/moves it and first restores the previous bounding box.
    if (!s_snapshot_app_render_opening && buffer_state->radius > 0) {
      const int64_t started_us = esp_timer_get_time();
      ppa_ok = restore_background_rect(buffer_state->center_x, buffer_state->center_y, buffer_state->radius);
      restore_us = (uint32_t) (esp_timer_get_time() - started_us);
    }

    const bool final_close = !s_snapshot_app_render_opening && (width <= 8 || height <= 8);
    if (ppa_ok && !final_close) {
      const int scaled_size =
          std::clamp(diameter, 4, std::min<int>((int) this->width_, (int) this->height_));
      const int dst_x = std::clamp(center_x - scaled_size / 2, 0, this->width_ - scaled_size);
      const int dst_y = std::clamp(center_y - scaled_size / 2, 0, this->height_ - scaled_size);
      const int64_t scale_started_us = esp_timer_get_time();
      ppa_ok = scale_argb(scaled_size);
      scale_us = (uint32_t) (esp_timer_get_time() - scale_started_us);
      if (ppa_ok) {
        const int64_t blend_started_us = esp_timer_get_time();
        ppa_ok = blend_argb(scaled_size, dst_x, dst_y);
        blend_us = (uint32_t) (esp_timer_get_time() - blend_started_us);
      }
    }
    lv_draw_ppa_direct_animation_qos_restore();

    if (ppa_ok) {
      buffer_state->radius = final_close ? 0 : radius;
      buffer_state->center_x = center_x;
      buffer_state->center_y = center_y;
      const int sync_y1 = std::clamp(std::min(previous_center_y - previous_radius, center_y - radius) - 3, 0,
                                     this->height_ - 1);
      const int sync_y2 = std::clamp(std::max(previous_center_y + previous_radius, center_y + radius) + 3, 0,
                                     this->height_ - 1);
      const int64_t sync_started_us = esp_timer_get_time();
      lvgl_cache_msync_external(target + (size_t) sync_y1 * row_bytes,
                                (size_t) (sync_y2 - sync_y1 + 1) * row_bytes,
                                ESP_CACHE_MSYNC_FLAG_DIR_M2C);
      cache_sync_us = (uint32_t) (esp_timer_get_time() - sync_started_us);
      const int64_t present_started_us = esp_timer_get_time();
      if (!this->present_snapshot_render_buffer_(target))
        return false;
      present_us = (uint32_t) (esp_timer_get_time() - present_started_us);
#ifdef USE_LVGL_FPS_BENCHMARK
      lvgl_esphome_note_frame();
#endif
      const uint32_t total_us = (uint32_t) (esp_timer_get_time() - frame_started_us);
      if (s_perf_logging_enabled && total_us > 30000) {
        ESP_LOGW("lvgl.app", "PPA frame total=%uus init=%uus restore=%uus scale=%uus blend=%uus sync=%uus present=%uus size=%d",
                 (unsigned) total_us, (unsigned) init_copy_us, (unsigned) restore_us, (unsigned) scale_us,
                 (unsigned) blend_us, (unsigned) cache_sync_us, (unsigned) present_us, diameter);
      }
      return true;
    }

    static bool warned = false;
    if (!warned) {
      ESP_LOGW(TAG, "snapshot app: circular PPA frame failed; using RGB888 fallback");
      warned = true;
    }
    buffer_state->initialized = false;
    if (!copy_full(background)) {
      memset(target, 0, fb_bytes);
      needs_sync = true;
    }
    buffer_state->radius = 0;
    buffer_state->center_x = center_x;
    buffer_state->center_y = center_y;
    buffer_state->initialized = true;
  }
#endif

#if 0  // Queuing hundreds of narrow SRM copies costs far more than the CPU span compositor.
  if (s_snapshot_app_srm_client != nullptr && background != nullptr) {
    struct CopyRect {
      const lv_draw_buf_t *source;
      int x;
      int y;
      int width;
      int height;
    };
    struct ActiveRect {
      bool active{false};
      CopyRect rect{};
    };

    constexpr size_t MAX_PENDING_RECTS = 12;
    constexpr int EDGE_QUANTUM = 8;
    std::array<CopyRect, MAX_PENDING_RECTS> pending{};
    std::array<ActiveRect, 4> active{};
    size_t pending_count = 0;
    uint32_t operation_count = 0;
    uint32_t max_submit_us = 0;
    uint32_t ppa_us = 0;
    bool ppa_ok = true;

    const size_t app_bytes = (size_t) app->header.stride * (size_t) app->header.h;
    const size_t background_bytes = (size_t) background->header.stride * (size_t) background->header.h;
    const int app_stride_pixels = app->header.stride / BYTES_PER_PIXEL;
    const int background_stride_pixels = background->header.stride / BYTES_PER_PIXEL;
    if (app->header.stride % BYTES_PER_PIXEL != 0 || background->header.stride % BYTES_PER_PIXEL != 0 ||
        app_stride_pixels < this->width_ || background_stride_pixels < this->width_) {
      ppa_ok = false;
    }

    if (ppa_ok && s_snapshot_app_synced_app != app->data) {
      ppa_ok = lvgl_cache_msync_external_result(app->data, app_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M) == ESP_OK;
      if (ppa_ok)
        s_snapshot_app_synced_app = app->data;
    }
    if (ppa_ok && s_snapshot_app_synced_background != background->data) {
      ppa_ok = lvgl_cache_msync_external_result(background->data, background_bytes,
                                                ESP_CACHE_MSYNC_FLAG_DIR_C2M) == ESP_OK;
      if (ppa_ok)
        s_snapshot_app_synced_background = background->data;
    }

    auto submit_pending = [&]() -> bool {
      if (pending_count == 0)
        return true;

      DirectPpaFrameCompletion completion;
      completion.remaining.store(static_cast<uint32_t>(pending_count), std::memory_order_release);
      completion.waiter = xTaskGetCurrentTaskHandle();
      ulTaskNotifyTake(pdTRUE, 0);

      esp_err_t result = ESP_OK;
      size_t submitted = 0;
      const int64_t batch_started_us = esp_timer_get_time();
      for (size_t index = 0; index < pending_count; index++) {
        const auto &rect = pending[index];
        const int source_stride_pixels = rect.source == app ? app_stride_pixels : background_stride_pixels;
        ppa_srm_oper_config_t config{};
        config.in.buffer = rect.source->data;
        config.in.pic_w = source_stride_pixels;
        config.in.pic_h = rect.source->header.h;
        config.in.block_w = rect.width;
        config.in.block_h = rect.height;
        config.in.block_offset_x = rect.x;
        config.in.block_offset_y = rect.y;
        config.in.srm_cm = PPA_SRM_COLOR_MODE_RGB888;
        config.out.buffer = target;
        config.out.buffer_size = fb_bytes;
        config.out.pic_w = this->width_;
        config.out.pic_h = this->height_;
        config.out.block_offset_x = rect.x;
        config.out.block_offset_y = rect.y;
        config.out.srm_cm = PPA_SRM_COLOR_MODE_RGB888;
        config.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
        config.scale_x = 1.0f;
        config.scale_y = 1.0f;
        config.alpha_update_mode = PPA_ALPHA_NO_CHANGE;
        config.mode = PPA_TRANS_MODE_NON_BLOCKING;
        config.user_data = &completion;

        const int64_t submit_started_us = esp_timer_get_time();
        result = ppa_do_scale_rotate_mirror(s_snapshot_app_srm_client, &config);
        max_submit_us = std::max<uint32_t>(max_submit_us,
                                           static_cast<uint32_t>(esp_timer_get_time() - submit_started_us));
        if (result != ESP_OK) {
          completion.remaining.fetch_sub(static_cast<uint32_t>(pending_count - submitted),
                                         std::memory_order_acq_rel);
          break;
        }
        submitted++;
      }

      while (completion.remaining.load(std::memory_order_acquire) != 0)
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

      ppa_us += static_cast<uint32_t>(esp_timer_get_time() - batch_started_us);
      operation_count += static_cast<uint32_t>(submitted);
      pending_count = 0;
      return result == ESP_OK;
    };

    auto queue_rect = [&](const CopyRect &rect) -> bool {
      if (rect.width <= 0 || rect.height <= 0)
        return true;
      dirty_y1 = std::min(dirty_y1, rect.y);
      dirty_y2 = std::max(dirty_y2, rect.y + rect.height - 1);
      pending[pending_count++] = rect;
      return pending_count < pending.size() || submit_pending();
    };

    auto finish_active = [&](size_t slot) -> bool {
      if (!active[slot].active)
        return true;
      const bool result = queue_rect(active[slot].rect);
      active[slot].active = false;
      return result;
    };

    auto update_active = [&](size_t slot, const lv_draw_buf_t *source, int y, int x1, int x2) -> bool {
      if (x2 < x1)
        return finish_active(slot);
      if (active[slot].active && active[slot].rect.source == source && active[slot].rect.x == x1 &&
          active[slot].rect.width == x2 - x1 + 1 &&
          active[slot].rect.y + active[slot].rect.height == y) {
        active[slot].rect.height++;
        return true;
      }
      if (!finish_active(slot))
        return false;
      active[slot].active = true;
      active[slot].rect = {source, x1, y, x2 - x1 + 1, 1};
      return true;
    };

    auto quantized_circle_span = [&](int y, int circle_center_x, int circle_center_y, int circle_radius, int *x1,
                                     int *x2) -> bool {
      if (circle_radius <= 0)
        return false;
      const int dy = y - circle_center_y;
      const int radius_sq = circle_radius * circle_radius;
      if (dy * dy > radius_sq)
        return false;
      const int span = static_cast<int>(integer_sqrt_u32(static_cast<uint32_t>(radius_sq - dy * dy)));
      int left = std::clamp(circle_center_x - span, 0, this->width_ - 1);
      int right = std::clamp(circle_center_x + span, 0, this->width_ - 1);
      if (right - left + 1 >= EDGE_QUANTUM) {
        left = ((left + EDGE_QUANTUM - 1) / EDGE_QUANTUM) * EDGE_QUANTUM;
        right = ((right + 1) / EDGE_QUANTUM) * EDGE_QUANTUM - 1;
      }
      if (right < left)
        return false;
      *x1 = left;
      *x2 = right;
      return true;
    };

    const int requested_radius = !s_snapshot_app_render_opening && (width <= 8 || height <= 8) ? 0 : radius;
    const uintptr_t app_begin = reinterpret_cast<uintptr_t>(app->data);
    const uintptr_t background_begin = reinterpret_cast<uintptr_t>(background->data);
    const uintptr_t target_begin = reinterpret_cast<uintptr_t>(target);
    if (ppa_ok) {
      s_direct_ppa_source_begin.store(app_begin, std::memory_order_release);
      s_direct_ppa_source_end.store(app_begin + app_bytes, std::memory_order_release);
      s_direct_ppa_source2_begin.store(background_begin, std::memory_order_release);
      s_direct_ppa_source2_end.store(background_begin + background_bytes, std::memory_order_release);
      s_direct_ppa_target_begin.store(target_begin, std::memory_order_release);
      s_direct_ppa_target_end.store(target_begin + fb_bytes, std::memory_order_release);
      lv_draw_ppa_direct_animation_qos_apply();
      if (esphome_mipi_dsi_wait_fifo_margin != nullptr) {
        esphome_mipi_dsi_wait_fifo_margin(CONFIG_ESPHOME_LVGL_PPA_SRM_TRANSFORM_DSI_FIFO_MIN,
                                          CONFIG_ESPHOME_LVGL_PPA_SRM_TRANSFORM_DSI_WAIT_US);
      }

      for (int y = 0; y < this->height_ && ppa_ok; y++) {
        int old_x1 = 0;
        int old_x2 = -1;
        int new_x1 = 0;
        int new_x2 = -1;
        const bool has_old = quantized_circle_span(y, buffer_state->center_x, buffer_state->center_y,
                                                   buffer_state->radius, &old_x1, &old_x2);
        const bool has_new =
            quantized_circle_span(y, center_x, center_y, requested_radius, &new_x1, &new_x2);

        int bg_left_x1 = 0;
        int bg_left_x2 = -1;
        int bg_right_x1 = 0;
        int bg_right_x2 = -1;
        int app_left_x1 = 0;
        int app_left_x2 = -1;
        int app_right_x1 = 0;
        int app_right_x2 = -1;
        if (has_old) {
          if (!has_new) {
            bg_left_x1 = old_x1;
            bg_left_x2 = old_x2;
          } else {
            bg_left_x1 = old_x1;
            bg_left_x2 = std::min(old_x2, new_x1 - 1);
            bg_right_x1 = std::max(old_x1, new_x2 + 1);
            bg_right_x2 = old_x2;
          }
        }
        if (has_new) {
          if (!has_old) {
            app_left_x1 = new_x1;
            app_left_x2 = new_x2;
          } else {
            app_left_x1 = new_x1;
            app_left_x2 = std::min(new_x2, old_x1 - 1);
            app_right_x1 = std::max(new_x1, old_x2 + 1);
            app_right_x2 = new_x2;
          }
        }

        ppa_ok = update_active(0, background, y, bg_left_x1, bg_left_x2) &&
                 update_active(1, background, y, bg_right_x1, bg_right_x2) &&
                 update_active(2, app, y, app_left_x1, app_left_x2) &&
                 update_active(3, app, y, app_right_x1, app_right_x2);
      }
      for (size_t slot = 0; slot < active.size() && ppa_ok; slot++)
        ppa_ok = finish_active(slot);
      if (ppa_ok)
        ppa_ok = submit_pending();

      lv_draw_ppa_direct_animation_qos_restore();
      s_direct_ppa_source_begin.store(0, std::memory_order_release);
      s_direct_ppa_source2_begin.store(0, std::memory_order_release);
      s_direct_ppa_target_begin.store(0, std::memory_order_release);
      s_direct_ppa_source_end.store(0, std::memory_order_relaxed);
      s_direct_ppa_source2_end.store(0, std::memory_order_relaxed);
      s_direct_ppa_target_end.store(0, std::memory_order_relaxed);
    }

    if (ppa_ok) {
      buffer_state->radius = requested_radius;
      buffer_state->center_x = center_x;
      buffer_state->center_y = center_y;
      const int64_t sync_started_us = esp_timer_get_time();
      if (dirty_y2 >= dirty_y1) {
        lvgl_cache_msync_external(target + (size_t) dirty_y1 * row_bytes,
                                  (size_t) (dirty_y2 - dirty_y1 + 1) * row_bytes,
                                  ESP_CACHE_MSYNC_FLAG_DIR_M2C);
      }
      cache_sync_us = static_cast<uint32_t>(esp_timer_get_time() - sync_started_us);
      const int64_t present_started_us = esp_timer_get_time();
      if (!this->present_snapshot_render_buffer_(target))
        return false;
      present_us = static_cast<uint32_t>(esp_timer_get_time() - present_started_us);
#ifdef USE_LVGL_FPS_BENCHMARK
      lvgl_esphome_note_frame();
#endif
      const uint32_t total_us = static_cast<uint32_t>(esp_timer_get_time() - frame_started_us);
      if (s_perf_logging_enabled && total_us > 30000) {
        ESP_LOGW("lvgl.app", "RGB frame total=%uus init=%uus ppa=%uus sync=%uus present=%uus ops=%u max_submit=%uus size=%d",
                 (unsigned) total_us, (unsigned) init_copy_us, (unsigned) ppa_us, (unsigned) cache_sync_us,
                 (unsigned) present_us, (unsigned) operation_count, (unsigned) max_submit_us, diameter);
      }
      return true;
    }

    static bool warned = false;
    if (!warned) {
      ESP_LOGW(TAG, "snapshot app: queued RGB888 PPA copy failed; using CPU fallback");
      warned = true;
    }
  }
#endif

  auto copy_span = [&](int y, int x1, int x2, const lv_draw_buf_t *src_buf) {
    if (x2 < x1)
      return;
    x1 = std::clamp(x1, 0, this->width_ - 1);
    x2 = std::clamp(x2, 0, this->width_ - 1);
    if (x2 < x1)
      return;
    const size_t copy_bytes = (size_t) (x2 - x1 + 1) * BYTES_PER_PIXEL;
    uint8_t *dst_row = target + (size_t) y * row_bytes + (size_t) x1 * BYTES_PER_PIXEL;
    if (src_buf == nullptr || src_buf->data == nullptr) {
      memset(dst_row, 0, copy_bytes);
    } else {
      const uint8_t *src_row = static_cast<const uint8_t *>(src_buf->data) + (size_t) y * src_buf->header.stride +
                               (size_t) x1 * BYTES_PER_PIXEL;
      memcpy(dst_row, src_row, copy_bytes);
    }
    dirty_y1 = std::min(dirty_y1, y);
    dirty_y2 = std::max(dirty_y2, y);
  };

  auto sync_dirty_rows = [&]() {
    if (needs_sync) {
      sync_full();
      return;
    }
    if (dirty_y2 >= dirty_y1) {
      lvgl_cache_msync_external(target + (size_t) dirty_y1 * row_bytes,
                                (size_t) (dirty_y2 - dirty_y1 + 1) * row_bytes,
                                ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    }
  };

  auto circle_span = [&](int y, int circle_center_x, int circle_center_y, int circle_radius, int *x1,
                         int *x2) -> bool {
    if (circle_radius <= 0)
      return false;
    const int dy = y - circle_center_y;
    const int radius_sq = circle_radius * circle_radius;
    const int dy_sq = dy * dy;
    if (dy_sq > radius_sq)
      return false;
    const int span = static_cast<int>(integer_sqrt_u32(static_cast<uint32_t>(radius_sq - dy_sq)));
    *x1 = std::clamp(circle_center_x - span, 0, this->width_ - 1);
    *x2 = std::clamp(circle_center_x + span, 0, this->width_ - 1);
    return *x2 >= *x1;
  };

  // Update only the symmetric difference between the circle already stored in
  // this DSI buffer and the requested circle. The overlap already contains the
  // correct app pixels. This remains valid while the close animation moves its
  // centre and avoids repainting most of the 800x800 frame on every tick.
  auto update_circle = [&](int new_center_x, int new_center_y, int new_radius) {
    for (int y = 0; y < this->height_; y++) {
      int old_x1 = 0;
      int old_x2 = -1;
      int new_x1 = 0;
      int new_x2 = -1;
      const bool has_old = circle_span(y, buffer_state->center_x, buffer_state->center_y, buffer_state->radius,
                                       &old_x1, &old_x2);
      const bool has_new = circle_span(y, new_center_x, new_center_y, new_radius, &new_x1, &new_x2);

      if (has_old) {
        if (!has_new) {
          copy_span(y, old_x1, old_x2, background);
        } else {
          copy_span(y, old_x1, std::min(old_x2, new_x1 - 1), background);
          copy_span(y, std::max(old_x1, new_x2 + 1), old_x2, background);
        }
      }
      if (has_new) {
        if (!has_old) {
          copy_span(y, new_x1, new_x2, app);
        } else {
          copy_span(y, new_x1, std::min(new_x2, old_x1 - 1), app);
          copy_span(y, std::max(new_x1, old_x2 + 1), new_x2, app);
        }
      }
    }

    buffer_state->radius = new_radius;
    buffer_state->center_x = new_center_x;
    buffer_state->center_y = new_center_y;
  };

  if (!s_snapshot_app_render_opening && (width <= 8 || height <= 8)) {
    const int64_t circle_started_us = esp_timer_get_time();
    // The final close frame must be an exact background frame. Incremental
    // integer circle spans can leave a one-pixel annulus from the preceding
    // radius, which is especially visible around a round display edge.
    if (!copy_full(background)) {
      memset(target, 0, fb_bytes);
      needs_sync = true;
    }
    buffer_state->radius = 0;
    buffer_state->center_x = center_x;
    buffer_state->center_y = center_y;
    circle_us = (uint32_t) (esp_timer_get_time() - circle_started_us);
    const int64_t sync_started_us = esp_timer_get_time();
    sync_dirty_rows();
    cache_sync_us = (uint32_t) (esp_timer_get_time() - sync_started_us);
    const int64_t present_started_us = esp_timer_get_time();
    if (!this->present_snapshot_render_buffer_(target))
      return false;
    present_us = (uint32_t) (esp_timer_get_time() - present_started_us);
#ifdef USE_LVGL_FPS_BENCHMARK
    lvgl_esphome_note_frame();
#endif
    const uint32_t total_us = (uint32_t) (esp_timer_get_time() - frame_started_us);
    if (s_perf_logging_enabled && total_us > 30000) {
      ESP_LOGW("lvgl.app", "slow final frame total=%uus init=%uus circle=%uus sync=%uus present=%uus",
               (unsigned) total_us, (unsigned) init_copy_us, (unsigned) circle_us, (unsigned) cache_sync_us,
               (unsigned) present_us);
    }
    return true;
  }

  if (width == 0 || height == 0)
    return false;

  const int64_t circle_started_us = esp_timer_get_time();
  update_circle(center_x, center_y, radius);
  circle_us = (uint32_t) (esp_timer_get_time() - circle_started_us);
  const int64_t sync_started_us = esp_timer_get_time();
  sync_dirty_rows();
  cache_sync_us = (uint32_t) (esp_timer_get_time() - sync_started_us);
  const int64_t present_started_us = esp_timer_get_time();
  if (!this->present_snapshot_render_buffer_(target))
    return false;
  present_us = (uint32_t) (esp_timer_get_time() - present_started_us);
#ifdef USE_LVGL_FPS_BENCHMARK
  lvgl_esphome_note_frame();
#endif
  const uint32_t total_us = (uint32_t) (esp_timer_get_time() - frame_started_us);
  if (s_perf_logging_enabled && total_us > 30000) {
    ESP_LOGW("lvgl.app", "slow frame total=%uus init=%uus circle=%uus sync=%uus present=%uus size=%d",
             (unsigned) total_us, (unsigned) init_copy_us, (unsigned) circle_us, (unsigned) cache_sync_us,
             (unsigned) present_us, diameter);
  }
  return true;
#else
  return false;
#endif
}

IdleTrigger::IdleTrigger(LvglComponent *parent, TemplatableFn<uint32_t> timeout) : timeout_(timeout) {
  parent->add_on_idle_callback([this](uint32_t idle_time) {
    if (!this->is_idle_ && idle_time > this->timeout_.value()) {
      this->is_idle_ = true;
      this->trigger();
    } else if (this->is_idle_ && idle_time < this->timeout_.value()) {
      this->is_idle_ = false;
    }
  });
}

#ifdef USE_LVGL_TOUCHSCREEN
LVTouchListener::LVTouchListener(uint16_t long_press_time, uint16_t long_press_repeat_time, LvglComponent *parent) {
  this->set_parent(parent);
  this->drv_ = lv_indev_create();
  lv_indev_set_type(this->drv_, LV_INDEV_TYPE_POINTER);
  lv_indev_set_disp(this->drv_, parent->get_disp());
  lv_indev_set_long_press_time(this->drv_, long_press_time);
  lv_indev_set_gesture_min_distance(this->drv_, 45);
  lv_indev_set_gesture_min_velocity(this->drv_, 4);
  // long press repeat time TBD
  lv_indev_set_user_data(this->drv_, this);
  lv_indev_set_read_cb(this->drv_, [](lv_indev_t *d, lv_indev_data_t *data) {
    auto *l = static_cast<LVTouchListener *>(lv_indev_get_user_data(d));
    if (l->touch_pressed_) {
      data->point.x = l->touch_point_.x;
      data->point.y = l->touch_point_.y;
      data->state = LV_INDEV_STATE_PRESSED;
    } else {
      data->state = LV_INDEV_STATE_RELEASED;
    }
  });
}

void LVTouchListener::update(const touchscreen::TouchPoints_t &tpoints) {
  const bool pressed = !this->parent_->is_paused() && !tpoints.empty();
  if (!pressed) {
    this->touch_pressed_ = false;
    return;
  }

  this->touch_point_ = tpoints[0];
  int32_t x = this->touch_point_.x;
  int32_t y = this->touch_point_.y;
  this->parent_->rotate_coordinates(x, y);
  if (!this->raw_touch_active_) {
    this->raw_touch_active_ = true;
    this->navigation_touch_captured_ = false;
    this->parent_->navigation_touch_begin(x, y);
  }

  const bool captured = this->parent_->navigation_touch_update(x, y);
  if (captured && !this->navigation_touch_captured_) {
    // The press may already belong to a clickable child. Resetting the input
    // device at the capture boundary prevents its eventual release from being
    // interpreted as a click.
    lv_indev_reset(this->drv_, nullptr);
  }
  this->navigation_touch_captured_ |= captured;
  this->touch_pressed_ = !this->navigation_touch_captured_;
}

void LVTouchListener::release() {
  const bool navigation_handled = this->raw_touch_active_ && this->parent_->navigation_touch_end();
  if (navigation_handled || this->navigation_touch_captured_)
    lv_indev_reset(this->drv_, nullptr);
  this->touch_pressed_ = false;
  this->raw_touch_active_ = false;
  this->navigation_touch_captured_ = false;
  this->parent_->maybe_wakeup();
}
#endif  // USE_LVGL_TOUCHSCREEN

#ifdef USE_LVGL_METER

int16_t lv_get_needle_angle_for_value(lv_obj_t *obj, int32_t value) {
  auto *scale = lv_obj_get_parent(obj);
  auto min_value = lv_scale_get_range_min_value(scale);
  auto max_value = lv_scale_get_range_max_value(scale);
  value = clamp(value, min_value, max_value);
  return ((value - min_value) * lv_scale_get_angle_range(scale) / (max_value - min_value) +
          lv_scale_get_rotation((scale))) %
         360;
}

void IndicatorLine::set_obj(lv_obj_t *lv_obj) {
  LvCompound::set_obj(lv_obj);
  lv_line_set_points(lv_obj, this->points_, 2);
  lv_obj_add_event_cb(
      lv_obj_get_parent(obj),
      [](lv_event_t *e) {
        auto *indicator = static_cast<IndicatorLine *>(lv_event_get_user_data(e));
        indicator->update_length_();
        ESP_LOGD(TAG, "Updated length, value = %d", indicator->angle_);
      },
      LV_EVENT_SIZE_CHANGED, this);
}

void IndicatorLine::set_value(int value) {
  auto angle = lv_get_needle_angle_for_value(this->obj, value);
  if (angle != this->angle_) {
    this->angle_ = angle;
    this->update_length_();
  }
}

void IndicatorLine::update_length_() {
  auto cx = lv_obj_get_width(lv_obj_get_parent(this->obj)) / 2;
  auto cy = lv_obj_get_height(lv_obj_get_parent(this->obj)) / 2;
  auto radius = clamp_at_most(cx, cy);
  auto length = lv_obj_get_style_length(this->obj, LV_PART_MAIN);
  auto radial_offset = lv_obj_get_style_radial_offset(this->obj, LV_PART_MAIN);
  if (LV_COORD_IS_PCT(radial_offset)) {
    radial_offset = radius * LV_COORD_GET_PCT(radial_offset) / 100;
  }
  if (LV_COORD_IS_PCT(length)) {
    length = radius * LV_COORD_GET_PCT(length) / 100;
  } else if (length < 0) {
    length += radius;
  }
  auto x = lv_trigo_cos(this->angle_) / 32768.0f;
  auto y = lv_trigo_sin(this->angle_) / 32768.0f;
  // radius here also represents the offset of the scale center from top left
  this->points_[0].x = radius + radial_offset * x;
  this->points_[0].y = radius + radial_offset * y;
  this->points_[1].x = radius + x * (radial_offset + length);
  this->points_[1].y = radius + y * (radial_offset + length);
  lv_obj_refresh_self_size(this->obj);
  lv_obj_invalidate(this->obj);
}
#endif

#ifdef USE_LVGL_KEY_LISTENER
LVEncoderListener::LVEncoderListener(lv_indev_type_t type, uint16_t long_press_time, uint16_t long_press_repeat_time) {
  this->drv_ = lv_indev_create();
  lv_indev_set_type(this->drv_, type);
  lv_indev_set_long_press_time(this->drv_, long_press_time);
  lv_indev_set_long_press_repeat_time(this->drv_, long_press_repeat_time);
  lv_indev_set_user_data(this->drv_, this);
  lv_indev_set_read_cb(this->drv_, [](lv_indev_t *d, lv_indev_data_t *data) {
    auto *l = static_cast<LVEncoderListener *>(lv_indev_get_user_data(d));
    data->state = l->pressed_ ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->key = l->key_;
    // LVGL 9.5: Apply rotary sensitivity multiplier
    auto raw_diff = (int16_t) (l->count_ - l->last_count_);
    data->enc_diff = (int16_t) (raw_diff * l->sensitivity_);
    l->last_count_ = l->count_;
    data->continue_reading = false;
  });
}
#endif  // USE_LVGL_KEY_LISTENER

#if defined(USE_LVGL_DROPDOWN) || defined(LV_USE_ROLLER)
std::string LvSelectable::get_selected_text() {
  auto selected = this->get_selected_index();
  if (selected >= this->options_.size())
    return "";
  return this->options_[selected];
}

static std::string join_string(std::vector<std::string> options) {
  return std::accumulate(
      options.begin(), options.end(), std::string(),
      [](const std::string &a, const std::string &b) -> std::string { return a + (!a.empty() ? "\n" : "") + b; });
}

void LvSelectable::set_selected_text(const std::string &text, lv_anim_enable_t anim) {
  auto index = std::find(this->options_.begin(), this->options_.end(), text);
  if (index != this->options_.end()) {
    lv_lock();
    this->set_selected_index(index - this->options_.begin(), anim);
    lv_obj_send_event(this->obj, lv_update_event, nullptr);
    lv_unlock();
  }
}

void LvSelectable::set_options(std::vector<std::string> options) {
  lv_lock();
  auto index = this->get_selected_index();
  if (index >= options.size())
    index = options.size() - 1;
  this->options_ = std::move(options);
  this->set_option_string(join_string(this->options_).c_str());
  lv_obj_send_event(this->obj, LV_EVENT_REFRESH, nullptr);
  this->set_selected_index(index, LV_ANIM_OFF);
  lv_unlock();
}
#endif  // USE_LVGL_DROPDOWN || LV_USE_ROLLER

#ifdef USE_LVGL_BUTTONMATRIX
void LvButtonMatrixType::set_obj(lv_obj_t *lv_obj) {
  LvCompound::set_obj(lv_obj);
  lv_obj_add_event_cb(
      lv_obj,
      [](lv_event_t *event) {
        auto *self = static_cast<LvButtonMatrixType *>(lv_event_get_user_data(event));
        if (self->key_callback_.size() == 0)
          return;
        auto key_idx = lv_buttonmatrix_get_selected_button(self->obj);
        if (key_idx == LV_BUTTONMATRIX_BUTTON_NONE)
          return;
        if (self->key_map_.contains(key_idx)) {
          self->send_key_(self->key_map_[key_idx]);
          return;
        }
        const auto *str = lv_buttonmatrix_get_button_text(self->obj, key_idx);
        auto len = strlen(str);
        while (len--)
          self->send_key_(*str++);
      },
      LV_EVENT_PRESSED, this);
}
#endif  // USE_LVGL_BUTTONMATRIX

#ifdef USE_LVGL_KEYBOARD
static const char *const KB_SPECIAL_KEYS[] = {
    "abc", "ABC", "1#",
    // maybe add other special keys here
};

void LvKeyboardType::set_obj(lv_obj_t *lv_obj) {
  LvCompound::set_obj(lv_obj);
  lv_obj_add_event_cb(
      lv_obj,
      [](lv_event_t *event) {
        auto *self = static_cast<LvKeyboardType *>(lv_event_get_user_data(event));
        if (self->key_callback_.size() == 0)
          return;

        auto key_idx = lv_buttonmatrix_get_selected_button(self->obj);
        if (key_idx == LV_BUTTONMATRIX_BUTTON_NONE)
          return;
        const char *txt = lv_buttonmatrix_get_button_text(self->obj, key_idx);
        if (txt == nullptr)
          return;
        for (const auto *kb_special_key : KB_SPECIAL_KEYS) {
          if (strcmp(txt, kb_special_key) == 0)
            return;
        }
        while (*txt != 0)
          self->send_key_(*txt++);
      },
      LV_EVENT_PRESSED, this);
}
#endif  // USE_LVGL_KEYBOARD

void LvglComponent::draw_end_() {
  if (this->draw_end_callback_ != nullptr)
    this->draw_end_callback_->trigger();
  if (this->update_when_display_idle_) {
    for (auto *disp : this->displays_)
      disp->update();
  }
}

bool LvglComponent::is_paused() const {
  if (this->paused_)
    return true;
  if (this->update_when_display_idle_) {
    for (auto *disp : this->displays_) {
      if (!disp->is_idle())
        return true;
    }
  }
  return false;
}

void LvglComponent::write_random_() {
  int iterations = 6 - lv_display_get_inactive_time(this->disp_) / 60000;
  if (iterations <= 0)
    iterations = 1;
  int16_t width = lv_display_get_horizontal_resolution(this->disp_);
  int16_t height = lv_display_get_vertical_resolution(this->disp_);
  while (iterations-- != 0) {
    int32_t col = random_uint32() % width;
    col = col / this->draw_rounding * this->draw_rounding;
    int32_t row = random_uint32() % height;
    row = row / this->draw_rounding * this->draw_rounding;
    // size will be between 8 and 32, and a multiple of draw_rounding
    int32_t size = (random_uint32() % 25 + 8) / this->draw_rounding * this->draw_rounding;
    lv_area_t area{.x1 = col, .y1 = row, .x2 = col + size - 1, .y2 = row + size - 1};
    // clip to display bounds just in case
    if (area.x2 >= width)
      area.x2 = width - 1;
    if (area.y2 >= height)
      area.y2 = height - 1;

    size_t line_len = lv_area_get_width(&area) * lv_area_get_height(&area) / 2;
    for (size_t i = 0; i != line_len; i++) {
      reinterpret_cast<uint32_t *>(this->draw_buf_)[i] = random_uint32();
    }
    this->draw_buffer_(&area, reinterpret_cast<lv_color_data *>(this->draw_buf_));
  }
}

/**
 * @class LvglComponent
 * @brief Component for rendering LVGL.
 *
 * This component renders LVGL widgets on a display. Some initialisation must be done in the constructor
 * since LVGL needs to be initialised before any widgets can be created.
 *
 * @param displays a list of displays to render onto. All displays must have the same
 *                 resolution.
 * @param buffer_frac the fraction of the display resolution to use for the LVGL
 *                    draw buffer. A higher value will make animations smoother but
 *                    also increase memory usage.
 * @param full_refresh if true, the display will be fully refreshed on every frame.
 *                     If false, only changed areas will be updated.
 * @param draw_rounding the rounding to use when drawing. A value of 1 will draw
 *                      without any rounding, a value of 2 will round to the nearest
 *                      multiple of 2, and so on.
 * @param resume_on_input if true, this component will resume rendering when the user
 *                         presses a key or clicks on the screen.
 */
LvglComponent::LvglComponent(std::vector<display::Display *> displays, float buffer_frac, bool full_refresh,
                             bool direct_mode, int draw_rounding, bool resume_on_input, bool update_when_display_idle)
    : draw_rounding(draw_rounding),
      displays_(std::move(displays)),
      buffer_frac_(buffer_frac),
      full_refresh_(full_refresh),
      direct_mode_(direct_mode),
      resume_on_input_(resume_on_input),
      update_when_display_idle_(update_when_display_idle) {
  this->disp_ = lv_display_create(240, 240);
}

void LvglComponent::setup() {
  auto *display = this->displays_[0];
  auto rounding = this->draw_rounding;
  // cater for displays with dimensions that don't divide by the required rounding
  this->width_ = display->get_width();
  this->height_ = display->get_height();
  auto width = (display->get_width() + rounding - 1) / rounding * rounding;
  auto height = (display->get_height() + rounding - 1) / rounding * rounding;
  auto frac = this->buffer_frac_;
  this->rotation = display->get_rotation();
  if (frac == 0)
    frac = 1;
    // LV_COLOR_FORMAT_RGB888 uses 3 bytes/pixel even when LV_COLOR_DEPTH=32
#if LV_COLOR_DEPTH == 32
  constexpr size_t BYTES_PER_PIXEL = 3;  // RGB888
#else
  constexpr size_t BYTES_PER_PIXEL = LV_COLOR_DEPTH / 8;
#endif
  auto buf_bytes = width * height / frac * BYTES_PER_PIXEL;
  // Align buffer size to the data cache line (128 B if
  // CONFIG_CACHE_L2_CACHE_LINE_128B=y, else 64 B is enough). 128 satisfies
  // both — esp_cache_msync() + PPA require both address AND size to be
  // cache-line aligned. Without this, PPA operations fail on PSRAM buffers
  // ('out.buffer addr or out.buffer_size not aligned to cache line size').
  constexpr size_t BUF_SIZE_ALIGN = 128;
  buf_bytes = (buf_bytes + BUF_SIZE_ALIGN - 1) & ~(BUF_SIZE_ALIGN - 1);
  void *buffer = nullptr;

  // Helper lambda to allocate an aligned DMA-capable buffer.
  // When USE_LVGL_PPA is defined, we try internal DMA-capable SRAM first
  // (required for PPA on ESP32-P4), then fall back to PSRAM with cache sync.
  auto alloc_draw_buf = [](size_t sz) -> void * {
#if defined(USE_LVGL_PPA) && defined(USE_ESP32)
    // Round size up to 128-byte cache line so PPA buffer_size checks pass
    // on both 64 B and 128 B cache-line sdkconfigs.
    size_t aligned_sz = (sz + 127) & ~size_t{127};
    void *p = heap_caps_aligned_alloc(128, aligned_sz, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (p != nullptr)
      return p;
    // Internal DMA SRAM full → PSRAM (128-byte aligned for 128 B cache line)
    p = heap_caps_aligned_alloc(128, aligned_sz, MALLOC_CAP_SPIRAM);
    if (p != nullptr)
      return p;
#endif
    return lv_malloc_core(sz);
  };

#ifdef USE_MIPI_DSI
  if (this->direct_mode_) {
    auto *mipi_display = static_cast<mipi_dsi::MipiDsi *>(display);
    if (mipi_display != nullptr && this->rotation == display::DISPLAY_ROTATION_0_DEGREES &&
        mipi_display->get_frame_buffer(0) != nullptr && mipi_display->get_frame_buffer(1) != nullptr &&
        mipi_display->get_frame_buffer_size() >= buf_bytes) {
      buffer = mipi_display->get_frame_buffer();
      this->draw_buf2_ = mipi_display->get_frame_buffer(1);
      this->direct_mode_active_ = true;
      s_direct_mode_active = 1;
      ESP_LOGI(TAG, "LVGL direct mode enabled on MIPI framebuffer (%zu bytes)", buf_bytes);
    } else {
      ESP_LOGW(TAG, "LVGL direct mode requested but unavailable, falling back to partial flush");
    }
  }
#else
  if (this->direct_mode_) {
    ESP_LOGW(TAG, "LVGL direct mode requested but MIPI DSI is not enabled");
  }
#endif

  if (buffer == nullptr)
    buffer = alloc_draw_buf(buf_bytes);
  // if specific buffer size not set and can't get 100%, try for a smaller one
  if (buffer == nullptr && this->buffer_frac_ == 0) {
    frac = MIN_BUFFER_FRAC;
    buf_bytes /= MIN_BUFFER_FRAC;
    buffer = alloc_draw_buf(buf_bytes);
  }
  this->buffer_frac_ = frac;
  if (buffer == nullptr) {
    this->status_set_error(LOG_STR("Memory allocation failure"));
    this->mark_failed();
    return;
  }
  this->draw_buf_ = static_cast<uint8_t *>(buffer);
#ifdef USE_MIPI_DSI
  if (!this->direct_mode_active_ && this->rotation == display::DISPLAY_ROTATION_0_DEGREES &&
      this->displays_.size() == 1) {
    auto *mipi_display = static_cast<mipi_dsi::MipiDsi *>(display);
    if (mipi_display != nullptr && mipi_display->get_frame_buffer(0) != nullptr &&
        mipi_display->get_frame_buffer(1) != nullptr) {
      this->draw_buf2_ = static_cast<uint8_t *>(alloc_draw_buf(buf_bytes));
      if (this->draw_buf2_ == nullptr) {
        ESP_LOGW(TAG, "LVGL partial framebuffer compositor disabled: second draw buffer allocation failed");
      }
    }
  }
#endif
#ifdef USE_ESP32
  auto memory_region = [](const void *ptr) -> const char * {
    if (ptr == nullptr)
      return "none";
    if (esp_ptr_internal(ptr))
      return "internal";
    if (esp_ptr_external_ram(ptr))
      return "psram";
    return "other";
  };
#endif
#ifdef USE_ESP32
  ESP_LOGI(TAG, "LVGL draw buffer 1: %p %zu bytes in %s", this->draw_buf_, buf_bytes, memory_region(this->draw_buf_));
  if (this->draw_buf2_ != nullptr)
    ESP_LOGI(TAG, "LVGL draw buffer 2: %p %zu bytes in %s", this->draw_buf2_, buf_bytes,
             memory_region(this->draw_buf2_));
#endif
  lv_display_set_resolution(this->disp_, this->width_, this->height_);
#if LV_COLOR_DEPTH == 32
  // RGB888: 3 bytes per pixel, fully supported by PPA as destination
  lv_display_set_color_format(this->disp_, LV_COLOR_FORMAT_RGB888);
#else
  lv_display_set_color_format(this->disp_, LV_COLOR_FORMAT_RGB565);
#endif
  // CRITICAL: Set user_data BEFORE flush_cb, as flush_cb uses user_data
  lv_display_set_user_data(this->disp_, this);
  lv_display_set_flush_cb(this->disp_, static_flush_cb);
  lv_display_add_event_cb(this->disp_, rounder_cb, LV_EVENT_INVALIDATE_AREA, this);
  // Store buf_bytes - lv_display_set_buffers() is called at the END of setup()
  // to avoid triggering rendering before all callbacks and pages are configured.
  this->buf_bytes_ = buf_bytes;
  if (this->rotation != display::DISPLAY_ROTATION_0_DEGREES) {
    this->rotate_buf_ = static_cast<lv_color_t *>(alloc_draw_buf(buf_bytes));
    if (this->rotate_buf_ == nullptr) {
      this->status_set_error(LOG_STR("Memory allocation failure"));
      this->mark_failed();
      return;
    }
#ifdef USE_LVGL_PPA
    if (s_display_srm_client != nullptr) {
      ESP_LOGI(TAG, "Display rotation will use PPA SRM hardware acceleration");
    }
#endif
  }
  if (this->draw_start_callback_ != nullptr) {
    lv_display_add_event_cb(this->disp_, render_start_cb, LV_EVENT_RENDER_START, this);
  }
  if (this->draw_end_callback_ != nullptr || this->update_when_display_idle_) {
    lv_display_add_event_cb(this->disp_, render_end_cb, LV_EVENT_REFR_READY, this);
  }
#if LV_USE_LOG
  lv_log_register_print_cb([](lv_log_level_t level, const char *buf) {
    auto next = strchr(buf, ')');
    if (next != nullptr)
      buf = next + 1;
    while (isspace(*buf))
      buf++;
    if (level >= sizeof(LOG_LEVEL_MAP) / sizeof(LOG_LEVEL_MAP[0]))
      level = sizeof(LOG_LEVEL_MAP) / sizeof(LOG_LEVEL_MAP[0]) - 1;
    esp_log_printf_(LOG_LEVEL_MAP[level], TAG, 0, "%.*s", (int) strlen(buf) - 1, buf);
  });
#endif
  // Rotation will be handled by our drawing function, so reset the display rotation.
  for (auto *disp : this->displays_)
    disp->set_rotation(display::DISPLAY_ROTATION_0_DEGREES);
  this->show_page(0, LV_SCREEN_LOAD_ANIM_NONE, 0);
  lv_display_trigger_activity(this->disp_);

#ifdef USE_ESP32
  if (!this->direct_mode_active_ && !this->full_refresh_) {
    this->start_partial_compositor_();
  }
#endif

  // CRITICAL: Configure buffers at the VERY END of setup()
  // This avoids deadlock while ensuring buffers are ready before any callbacks execute
  lv_display_set_buffers(this->disp_, this->draw_buf_, this->draw_buf2_, this->buf_bytes_,
                         this->direct_mode_active_
                             ? LV_DISPLAY_RENDER_MODE_DIRECT
                             : (this->full_refresh_ ? LV_DISPLAY_RENDER_MODE_FULL : LV_DISPLAY_RENDER_MODE_PARTIAL));
  this->buffers_configured_ = true;

#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  if (this->direct_mode_active_ && !this->start_direct_region_compositor_()) {
    ESP_LOGW(TAG, "Direct region compositor unavailable; regional widgets will use LVGL invalidation");
  }
#endif

#if defined(USE_LVGL_PPA) && defined(USE_LVGL_PPA_BLEND_HANDLER)
  // Espressif esp-iot-solution PPA SW blend handler — accelerates all
  // RGB565 SW blend paths (text, gradients post-rasterize, partial blends).
  // Complements the higher-level PPA draw unit in lv_draw_ppa.c.
  lvgl_port_ppa_v9_init(this->disp_);
#elif defined(USE_LVGL_PPA)
  ESP_LOGI(TAG, "PPA SRM enabled; LVGL SW-blend PPA registration disabled");
#endif

#ifdef USE_LVGL_FPS_BENCHMARK
  // Espressif esp_lvgl_adapter FPS sampler — prints a P10/25/50/75/90
  // report after ~200 samples (or sustained low-FPS detection).
  if (s_perf_logging_enabled)
    ESP_LOGI(TAG, "FPS benchmark: calling attach() for disp=%p", this->disp_);
  lvgl_fps_attach_v2(this->disp_);
  if (s_perf_logging_enabled)
    ESP_LOGI(TAG, "FPS benchmark: attach() returned");
#else
  if (s_perf_logging_enabled)
    ESP_LOGI(TAG, "FPS benchmark: not compiled in (USE_LVGL_FPS_BENCHMARK undefined)");
#endif
}

void LvglComponent::update() {
  // update indicators
  if (this->is_paused()) {
    return;
  }
  this->idle_callbacks_.call(lv_display_get_inactive_time(this->disp_));
}

void LvglComponent::loop() {
  if (!this->buffers_configured_)
    return;  // setup() not complete or failed, skip rendering

  if (!this->loop_started_) {
    this->loop_started_ = true;
    ESP_LOGD(TAG, "LVGL loop started - system is now fully ready");
  }

  if (this->is_paused()) {
    if (this->paused_ && this->show_snow_)
      this->write_random_();
  } else {
    // A direct image animator owns the idle DSI framebuffer and presents it at
    // VSYNC. Running LVGL's refresh timer in parallel would redraw the same
    // full screen and erase the performance benefit of the direct path.
    if (this->direct_image_animation_active_)
      return;
    if (snapshot_swipe_process_pending() || snapshot_scroll_process_pending())
      return;
    if (s_snapshot_direct_active) {
      snapshot_swipe_direct_anim_tick();
      snapshot_app_direct_anim_tick();
      return;
    }
    snapshot_cache_process_pending_compression();
    // Time the LVGL handler. flush_cb_ separately accumulates the DSI
    // DMA wait into perf_flush_us_; subtract it so the reported CPU%%
    // counts only real render work (matches lvgl_camera_display's
    // approach: cpu_time / frame_interval).
    uint64_t t0 = esp_timer_get_time();
    lv_timer_handler();
    uint64_t t1 = esp_timer_get_time();
    uint64_t loop_dt = t1 - t0;
    profiler_note_loop(loop_dt > UINT32_MAX ? UINT32_MAX : (uint32_t) loop_dt);
    this->perf_busy_us_ += loop_dt;
    if (loop_dt > this->perf_loop_max_us_)
      this->perf_loop_max_us_ = (uint32_t) loop_dt;
    uint64_t now_us = t1;
    if (this->perf_window_start_us_ == 0)
      this->perf_window_start_us_ = now_us;
    uint64_t elapsed_us = now_us - this->perf_window_start_us_;
    if (elapsed_us >= 1000000) {
      uint64_t cpu_us = (this->perf_busy_us_ > this->perf_flush_us_) ? (this->perf_busy_us_ - this->perf_flush_us_) : 0;
      uint32_t cpu_pct = (uint32_t) ((cpu_us * 100ULL) / elapsed_us);
      if (cpu_pct > 100)
        cpu_pct = 100;
      s_cpu_pct = cpu_pct;  // publish to __wrap_lv_timer_get_idle / sysmon overlay
      s_flush_ms = (uint32_t) (this->perf_flush_us_ / 1000ULL);
      s_loop_max_ms = this->perf_loop_max_us_ / 1000U;
      s_flush_max_ms = this->perf_flush_max_us_ / 1000U;
      s_invalidated_kpx = (uint32_t) (this->perf_invalidated_px_ / 1000ULL);
#ifdef USE_ESP32
      uint32_t free_psram_kb = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024U;
      uint32_t free_internal_kb = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024U;
#else
      uint32_t free_psram_kb = 0;
      uint32_t free_internal_kb = 0;
#endif
      uint32_t ppa_fill_tasks = 0;
      uint32_t ppa_img_tasks = 0;
      uint32_t ppa_img_eval_tasks = 0;
      uint32_t ppa_img_large_eval_tasks = 0;
      uint32_t ppa_img_accepted_eval_tasks = 0;
      uint32_t ppa_overlay_perf_count = 0;
      uint64_t ppa_overlay_perf_pre_us = 0;
      uint64_t ppa_overlay_perf_handler_us = 0;
      uint64_t ppa_overlay_perf_post_us = 0;
      uint32_t ppa_img_overlay_count = 0;
      uint64_t ppa_img_overlay_src_sync_us = 0;
      uint64_t ppa_img_overlay_wait_us = 0;
      uint64_t ppa_img_overlay_dest_pre_us = 0;
      uint64_t ppa_img_overlay_ppa_us = 0;
      uint64_t ppa_img_overlay_dest_post_us = 0;
      uint32_t ppa_img_srm_tasks = 0;
      uint32_t ppa_img_srm_large_tasks = 0;
      uint32_t ppa_img_srm_unaligned_tasks = 0;
      uint64_t ppa_img_srm_unaligned_bytes = 0;
      uint64_t ppa_img_srm_copy_us = 0;
      uint32_t ppa_img_srm_copy_max_us = 0;
      uint64_t ppa_img_srm_sync_us = 0;
      uint32_t ppa_img_srm_sync_max_us = 0;
      uint64_t ppa_img_srm_sync_bytes = 0;
      uint64_t ppa_img_srm_ppa_us = 0;
      uint32_t ppa_img_srm_ppa_max_us = 0;
      uint32_t ppa_img_srm_band_max_us = 0;
      uint64_t ppa_img_srm_wait_us = 0;
      uint32_t ppa_img_srm_wait_max_us = 0;
#ifdef USE_ESP32
      const uint64_t compositor_us = this->perf_compositor_us_;
      const uint64_t compositor_ready_us = this->perf_compositor_ready_us_;
      const uint64_t compositor_px = this->perf_compositor_px_;
      const uint32_t compositor_jobs = this->perf_compositor_jobs_;
      const uint32_t compositor_max_ms = this->perf_compositor_max_us_ / 1000U;
      const uint32_t compositor_ready_max_ms = this->perf_compositor_ready_max_us_ / 1000U;
#else
      const uint64_t compositor_us = 0;
      const uint64_t compositor_ready_us = 0;
      const uint64_t compositor_px = 0;
      const uint32_t compositor_jobs = 0;
      const uint32_t compositor_max_ms = 0;
      const uint32_t compositor_ready_max_ms = 0;
#endif
#ifdef USE_LVGL_PPA
      ppa_fill_tasks = lv_draw_ppa_get_fill_task_count();
      ppa_img_tasks = lv_draw_ppa_get_img_task_count();
      ppa_img_eval_tasks = lv_draw_ppa_get_img_eval_count();
      ppa_img_large_eval_tasks = lv_draw_ppa_get_img_large_eval_count();
      ppa_img_accepted_eval_tasks = lv_draw_ppa_get_img_accepted_eval_count();
      ppa_overlay_perf_count = lv_draw_ppa_get_overlay_perf_count();
      ppa_overlay_perf_pre_us = lv_draw_ppa_get_overlay_perf_pre_us();
      ppa_overlay_perf_handler_us = lv_draw_ppa_get_overlay_perf_handler_us();
      ppa_overlay_perf_post_us = lv_draw_ppa_get_overlay_perf_post_us();
      ppa_img_overlay_count = lv_draw_ppa_get_img_overlay_count();
      ppa_img_overlay_src_sync_us = lv_draw_ppa_get_img_overlay_src_sync_us();
      ppa_img_overlay_wait_us = lv_draw_ppa_get_img_overlay_wait_us();
      ppa_img_overlay_dest_pre_us = lv_draw_ppa_get_img_overlay_dest_pre_us();
      ppa_img_overlay_ppa_us = lv_draw_ppa_get_img_overlay_ppa_us();
      ppa_img_overlay_dest_post_us = lv_draw_ppa_get_img_overlay_dest_post_us();
      ppa_img_srm_tasks = lv_draw_ppa_get_img_srm_task_count();
      ppa_img_srm_large_tasks = lv_draw_ppa_get_img_srm_large_task_count();
      ppa_img_srm_unaligned_tasks = lv_draw_ppa_get_img_srm_unaligned_task_count();
      ppa_img_srm_unaligned_bytes = lv_draw_ppa_get_img_srm_unaligned_bytes();
      ppa_img_srm_copy_us = lv_draw_ppa_get_img_srm_copy_us();
      ppa_img_srm_copy_max_us = lv_draw_ppa_get_img_srm_copy_max_us();
      ppa_img_srm_sync_us = lv_draw_ppa_get_img_srm_sync_us();
      ppa_img_srm_sync_max_us = lv_draw_ppa_get_img_srm_sync_max_us();
      ppa_img_srm_sync_bytes = lv_draw_ppa_get_img_srm_sync_bytes();
      ppa_img_srm_ppa_us = lv_draw_ppa_get_img_srm_ppa_us();
      ppa_img_srm_ppa_max_us = lv_draw_ppa_get_img_srm_ppa_max_us();
      ppa_img_srm_band_max_us = lv_draw_ppa_get_img_srm_band_max_us();
      ppa_img_srm_wait_us = lv_draw_ppa_get_img_srm_wait_us();
      ppa_img_srm_wait_max_us = lv_draw_ppa_get_img_srm_wait_max_us();
#endif
#ifdef USE_MIPI_DSI
      mipi_dsi::AsyncFlushPerfStats dsi_stats{};
      if (!this->displays_.empty()) {
        auto *mipi_display = static_cast<mipi_dsi::MipiDsi *>(this->displays_[0]);
        if (mipi_display != nullptr)
          mipi_display->consume_async_flush_perf(&dsi_stats);
      }
#else
      struct {
        uint32_t flushes{};
        uint32_t underruns{};
        uint32_t zero_copy_flushes{};
        uint32_t staged_flushes{};
        uint32_t done_flushes{};
        uint32_t unsafe_addr_flushes{};
        uint32_t unsafe_row_flushes{};
        uint32_t unsafe_size_flushes{};
        uint64_t staged_bytes{};
        uint64_t sync_us{};
        uint64_t copy_us{};
        uint64_t submit_us{};
        uint64_t done_us{};
        uint32_t sync_max_us{};
        uint32_t copy_max_us{};
        uint32_t submit_max_us{};
        uint32_t done_max_us{};
      } dsi_stats;
#endif
      if (dsi_stats.underruns > 0) {
        ESP_LOGW(TAG, "dsi underrun: count=%u free=%uK/%uK loop_max=%ums flush_max=%ums",
                 (unsigned) dsi_stats.underruns, (unsigned) free_psram_kb, (unsigned) free_internal_kb,
                 (unsigned) (this->perf_loop_max_us_ / 1000U), (unsigned) (this->perf_flush_max_us_ / 1000U));
      }
#ifdef USE_LVGL_PPA
      static uint32_t last_ppa_fill_tasks = 0;
      static uint32_t last_ppa_img_tasks = 0;
      static uint32_t last_ppa_img_eval_tasks = 0;
      static uint32_t last_ppa_img_large_eval_tasks = 0;
      static uint32_t last_ppa_img_accepted_eval_tasks = 0;
      static uint32_t last_ppa_overlay_perf_count = 0;
      static uint64_t last_ppa_overlay_perf_pre_us = 0;
      static uint64_t last_ppa_overlay_perf_handler_us = 0;
      static uint64_t last_ppa_overlay_perf_post_us = 0;
      static uint32_t last_ppa_img_overlay_count = 0;
      static uint64_t last_ppa_img_overlay_src_sync_us = 0;
      static uint64_t last_ppa_img_overlay_wait_us = 0;
      static uint64_t last_ppa_img_overlay_dest_pre_us = 0;
      static uint64_t last_ppa_img_overlay_ppa_us = 0;
      static uint64_t last_ppa_img_overlay_dest_post_us = 0;
      static uint32_t last_ppa_img_srm_tasks = 0;
      static uint32_t last_ppa_img_srm_large_tasks = 0;
      static uint32_t last_ppa_img_srm_unaligned_tasks = 0;
      static uint64_t last_ppa_img_srm_unaligned_bytes = 0;
      static uint64_t last_ppa_img_srm_copy_us = 0;
      static uint64_t last_ppa_img_srm_sync_us = 0;
      static uint64_t last_ppa_img_srm_sync_bytes = 0;
      static uint64_t last_ppa_img_srm_ppa_us = 0;
      static uint64_t last_ppa_img_srm_wait_us = 0;
      if (s_perf_logging_enabled && ppa_img_overlay_count != last_ppa_img_overlay_count) {
        const uint32_t count = ppa_img_overlay_count - last_ppa_img_overlay_count;
        const uint64_t src_sync_us = ppa_img_overlay_src_sync_us - last_ppa_img_overlay_src_sync_us;
        const uint64_t wait_us = ppa_img_overlay_wait_us - last_ppa_img_overlay_wait_us;
        const uint64_t dest_pre_us = ppa_img_overlay_dest_pre_us - last_ppa_img_overlay_dest_pre_us;
        const uint64_t ppa_us = ppa_img_overlay_ppa_us - last_ppa_img_overlay_ppa_us;
        const uint64_t dest_post_us = ppa_img_overlay_dest_post_us - last_ppa_img_overlay_dest_post_us;
        ESP_LOGW(TAG, "ppa overlay core: count=%u src=%lluus wait=%lluus dest_pre=%lluus ppa=%lluus dest_post=%lluus",
                 (unsigned) count, (unsigned long long) (src_sync_us / count), (unsigned long long) (wait_us / count),
                 (unsigned long long) (dest_pre_us / count), (unsigned long long) (ppa_us / count),
                 (unsigned long long) (dest_post_us / count));
        last_ppa_img_overlay_count = ppa_img_overlay_count;
        last_ppa_img_overlay_src_sync_us = ppa_img_overlay_src_sync_us;
        last_ppa_img_overlay_wait_us = ppa_img_overlay_wait_us;
        last_ppa_img_overlay_dest_pre_us = ppa_img_overlay_dest_pre_us;
        last_ppa_img_overlay_ppa_us = ppa_img_overlay_ppa_us;
        last_ppa_img_overlay_dest_post_us = ppa_img_overlay_dest_post_us;
      }
      if (s_perf_logging_enabled && ppa_overlay_perf_count != last_ppa_overlay_perf_count) {
        const uint32_t count = ppa_overlay_perf_count - last_ppa_overlay_perf_count;
        const uint64_t pre_us = ppa_overlay_perf_pre_us - last_ppa_overlay_perf_pre_us;
        const uint64_t handler_us = ppa_overlay_perf_handler_us - last_ppa_overlay_perf_handler_us;
        const uint64_t post_us = ppa_overlay_perf_post_us - last_ppa_overlay_perf_post_us;
        ESP_LOGW(TAG, "ppa overlay: count=%u avg_pre=%lluus avg_handler=%lluus avg_post=%lluus", (unsigned) count,
                 (unsigned long long) (pre_us / count), (unsigned long long) (handler_us / count),
                 (unsigned long long) (post_us / count));
        last_ppa_overlay_perf_count = ppa_overlay_perf_count;
        last_ppa_overlay_perf_pre_us = ppa_overlay_perf_pre_us;
        last_ppa_overlay_perf_handler_us = ppa_overlay_perf_handler_us;
        last_ppa_overlay_perf_post_us = ppa_overlay_perf_post_us;
      }
      if (s_perf_logging_enabled &&
          (ppa_fill_tasks != last_ppa_fill_tasks || ppa_img_tasks != last_ppa_img_tasks ||
           ppa_img_eval_tasks != last_ppa_img_eval_tasks || ppa_img_large_eval_tasks != last_ppa_img_large_eval_tasks ||
           ppa_img_accepted_eval_tasks != last_ppa_img_accepted_eval_tasks ||
           ppa_img_srm_tasks != last_ppa_img_srm_tasks || ppa_img_srm_large_tasks != last_ppa_img_srm_large_tasks ||
           ppa_img_srm_unaligned_tasks != last_ppa_img_srm_unaligned_tasks ||
           ppa_img_srm_copy_us != last_ppa_img_srm_copy_us || ppa_img_srm_sync_us != last_ppa_img_srm_sync_us ||
           ppa_img_srm_wait_us != last_ppa_img_srm_wait_us || ppa_img_srm_ppa_us != last_ppa_img_srm_ppa_us)) {
        ESP_LOGW(
            TAG,
            "ppa diag: fill=%u(+%u) img_dispatch=%u(+%u) img_eval=%u(+%u) large=%u(+%u) "
            "accepted=%u(+%u) srm=%u(+%u) srm_large=%u(+%u) srm_unalign=%u(+%u) "
            "srm_copy=%lluus(+%lluus) max=%uus bytes=%lluKB(+%lluKB) "
            "srm_sync=%lluus(+%lluus) max=%uus sync_kb=%llu(+%llu) "
            "srm_wait=%lluus(+%lluus) max=%uus "
            "srm_ppa=%lluus(+%lluus) max=%uus band_max=%uus",
            (unsigned) ppa_fill_tasks, (unsigned) (ppa_fill_tasks - last_ppa_fill_tasks), (unsigned) ppa_img_tasks,
            (unsigned) (ppa_img_tasks - last_ppa_img_tasks), (unsigned) ppa_img_eval_tasks,
            (unsigned) (ppa_img_eval_tasks - last_ppa_img_eval_tasks), (unsigned) ppa_img_large_eval_tasks,
            (unsigned) (ppa_img_large_eval_tasks - last_ppa_img_large_eval_tasks),
            (unsigned) ppa_img_accepted_eval_tasks,
            (unsigned) (ppa_img_accepted_eval_tasks - last_ppa_img_accepted_eval_tasks), (unsigned) ppa_img_srm_tasks,
            (unsigned) (ppa_img_srm_tasks - last_ppa_img_srm_tasks), (unsigned) ppa_img_srm_large_tasks,
            (unsigned) (ppa_img_srm_large_tasks - last_ppa_img_srm_large_tasks), (unsigned) ppa_img_srm_unaligned_tasks,
            (unsigned) (ppa_img_srm_unaligned_tasks - last_ppa_img_srm_unaligned_tasks),
            (unsigned long long) ppa_img_srm_copy_us,
            (unsigned long long) (ppa_img_srm_copy_us - last_ppa_img_srm_copy_us), (unsigned) ppa_img_srm_copy_max_us,
            (unsigned long long) (ppa_img_srm_unaligned_bytes / 1024ULL),
            (unsigned long long) ((ppa_img_srm_unaligned_bytes - last_ppa_img_srm_unaligned_bytes) / 1024ULL),
            (unsigned long long) ppa_img_srm_sync_us,
            (unsigned long long) (ppa_img_srm_sync_us - last_ppa_img_srm_sync_us), (unsigned) ppa_img_srm_sync_max_us,
            (unsigned long long) (ppa_img_srm_sync_bytes / 1024ULL),
            (unsigned long long) ((ppa_img_srm_sync_bytes - last_ppa_img_srm_sync_bytes) / 1024ULL),
            (unsigned long long) ppa_img_srm_wait_us,
            (unsigned long long) (ppa_img_srm_wait_us - last_ppa_img_srm_wait_us), (unsigned) ppa_img_srm_wait_max_us,
            (unsigned long long) ppa_img_srm_ppa_us,
            (unsigned long long) (ppa_img_srm_ppa_us - last_ppa_img_srm_ppa_us), (unsigned) ppa_img_srm_ppa_max_us,
            (unsigned) ppa_img_srm_band_max_us);
        last_ppa_fill_tasks = ppa_fill_tasks;
        last_ppa_img_tasks = ppa_img_tasks;
        last_ppa_img_eval_tasks = ppa_img_eval_tasks;
        last_ppa_img_large_eval_tasks = ppa_img_large_eval_tasks;
        last_ppa_img_accepted_eval_tasks = ppa_img_accepted_eval_tasks;
        last_ppa_img_srm_tasks = ppa_img_srm_tasks;
        last_ppa_img_srm_large_tasks = ppa_img_srm_large_tasks;
        last_ppa_img_srm_unaligned_tasks = ppa_img_srm_unaligned_tasks;
        last_ppa_img_srm_unaligned_bytes = ppa_img_srm_unaligned_bytes;
        last_ppa_img_srm_copy_us = ppa_img_srm_copy_us;
        last_ppa_img_srm_sync_us = ppa_img_srm_sync_us;
        last_ppa_img_srm_sync_bytes = ppa_img_srm_sync_bytes;
        last_ppa_img_srm_wait_us = ppa_img_srm_wait_us;
        last_ppa_img_srm_ppa_us = ppa_img_srm_ppa_us;
      }
#endif
      if (s_perf_logging_enabled) {
        ESP_LOGI(TAG,
                 "perf1s: cpu=%u%% loop=%lluus flush=%lluus dsi_under=%u dsi_sync=%lluus max=%ums dsi_copy=%lluus/%u "
                 "max=%ums dsi_submit=%lluus max=%ums dsi_done=%lluus/%u max=%ums zc=%u stage=%u/%u unsafe=%u/%u/%u "
                 "%lluKB comp=%lluus ready=%lluus/%u jobs max_comp=%ums max_ready=%ums max_loop=%ums max_flush=%ums "
                 "inv=%lu areas/%lu kpx flush_px=%llu kpx comp_px=%llu kpx free=%uK/%uK dir=%u ppa=%u/%u",
                 (unsigned) cpu_pct, (unsigned long long) cpu_us, (unsigned long long) this->perf_flush_us_,
                 (unsigned) dsi_stats.underruns, (unsigned long long) dsi_stats.sync_us,
                 (unsigned) (dsi_stats.sync_max_us / 1000U), (unsigned long long) dsi_stats.copy_us,
                 (unsigned) dsi_stats.staged_flushes, (unsigned) (dsi_stats.copy_max_us / 1000U),
                 (unsigned long long) dsi_stats.submit_us, (unsigned) (dsi_stats.submit_max_us / 1000U),
                 (unsigned long long) dsi_stats.done_us, (unsigned) dsi_stats.done_flushes,
                 (unsigned) (dsi_stats.done_max_us / 1000U), (unsigned) dsi_stats.zero_copy_flushes,
                 (unsigned) dsi_stats.staged_flushes, (unsigned) dsi_stats.flushes,
                 (unsigned) dsi_stats.unsafe_addr_flushes, (unsigned) dsi_stats.unsafe_row_flushes,
                 (unsigned) dsi_stats.unsafe_size_flushes, (unsigned long long) (dsi_stats.staged_bytes / 1024ULL),
                 (unsigned long long) compositor_us, (unsigned long long) compositor_ready_us,
                 (unsigned) compositor_jobs, (unsigned) compositor_max_ms, (unsigned) compositor_ready_max_ms,
                 (unsigned) (this->perf_loop_max_us_ / 1000U), (unsigned) (this->perf_flush_max_us_ / 1000U),
                 (unsigned long) this->perf_invalidated_areas_, (unsigned long) (this->perf_invalidated_px_ / 1000ULL),
                 (unsigned long long) (this->perf_flush_px_ / 1000ULL), (unsigned long long) (compositor_px / 1000ULL),
                 (unsigned) free_psram_kb, (unsigned) free_internal_kb, (unsigned) s_direct_mode_active,
                 (unsigned) ppa_fill_tasks, (unsigned) ppa_img_tasks);
      }
      // Verbose-only log: enable via 'logs: lvgl: VERBOSE' in YAML if you
      // need the breakdown. Default DEBUG/INFO levels stay silent.
      ESP_LOGV(TAG, "perf: CPU %u%% (render %llu us, flush %llu us / wall %llu us)", (unsigned) cpu_pct,
               (unsigned long long) cpu_us, (unsigned long long) this->perf_flush_us_, (unsigned long long) elapsed_us);
      this->perf_busy_us_ = 0;
      this->perf_flush_us_ = 0;
      this->perf_invalidated_px_ = 0;
      this->perf_invalidated_areas_ = 0;
      this->perf_flush_px_ = 0;
#ifdef USE_ESP32
      this->perf_compositor_us_ = 0;
      this->perf_compositor_ready_us_ = 0;
      this->perf_compositor_px_ = 0;
      this->perf_compositor_jobs_ = 0;
      this->perf_compositor_max_us_ = 0;
      this->perf_compositor_ready_max_us_ = 0;
#endif
      this->perf_loop_max_us_ = 0;
      this->perf_flush_max_us_ = 0;
      this->perf_window_start_us_ = now_us;
    }
  }
}

#ifdef USE_LVGL_ANIMIMG
void lv_animimg_stop(lv_obj_t *obj) {
  int32_t duration = lv_animimg_get_duration(obj);
  lv_animimg_set_duration(obj, 0);
  lv_animimg_start(obj);
  lv_animimg_set_duration(obj, duration);
}
#endif

namespace {
struct SnapshotSwipeState {
  lv_obj_t *current_root{nullptr};
  lv_obj_t *next_root{nullptr};
  lv_obj_t *layer{nullptr};
  lv_obj_t *current_img{nullptr};
  lv_obj_t *next_img{nullptr};
  lv_timer_t *direct_anim_timer{nullptr};
  lv_timer_t *cleanup_timer{nullptr};
  lv_draw_buf_t *current_buf{nullptr};
  lv_draw_buf_t *next_buf{nullptr};
  int anim_start_current_x{0};
  int anim_start_next_x{0};
  uint64_t anim_start_us{0};
  uint32_t anim_duration_ms{0};
  bool direct_anim_active{false};
  bool owns_current_buf{false};
  bool owns_next_buf{false};
  int finish_current_x{0};
  int finish_next_x{0};
  int current_x{0};
  int next_x{0};
  int width{0};
  bool commit{false};
  bool pending_update{false};
  int pending_current_x{0};
  int pending_next_x{0};
  bool pending_finish{false};
  int pending_finish_current_x{0};
  int pending_finish_next_x{0};
  uint32_t pending_finish_duration_ms{0};
  bool pending_finish_commit{false};
  bool direct_render{false};
  bool edge_bounce{false};
  bool worker_enabled{false};
  bool panorama_render{false};
  uint8_t *panorama_buf{nullptr};
  size_t panorama_size{0};
  int panorama_scale{1};
  int panorama_next_x{0};
  int panorama_page_count{2};
  int panorama_source_page_index{0};
  uint64_t perf_first_render_us{0};
  uint64_t perf_last_render_us{0};
  uint64_t perf_total_render_us{0};
  uint32_t perf_render_frames{0};
  uint32_t perf_max_render_us{0};
  uint32_t perf_failed_frames{0};
  LvglComponent *component{nullptr};
};

struct SnapshotScrollState {
  lv_obj_t *root{nullptr};
  lv_draw_buf_t *content_buf{nullptr};
  lv_draw_buf_t *content_tail_buf{nullptr};
  int content_tail_y{0};
  int viewport_w{0};
  int viewport_h{0};
  int content_h{0};
  int max_scroll_y{0};
  int current_scroll_y{0};
  int pending_scroll_y{0};
  bool direct_render{false};
  bool pending_update{false};
  bool inertia_active{false};
  uint64_t inertia_start_us{0};
  uint32_t inertia_duration_ms{0};
  int inertia_start_y{0};
  int inertia_target_y{0};
  int inertia_final_y{0};
  bool inertia_bounce_pending{false};
  bool worker_enabled{false};
  bool root_was_hidden{false};
  uint64_t perf_first_render_us{0};
  uint64_t perf_last_render_us{0};
  uint64_t perf_total_render_us{0};
  uint32_t perf_render_frames{0};
  uint32_t perf_max_render_us{0};
  uint32_t perf_failed_frames{0};
  LvglComponent *component{nullptr};
};

struct SnapshotAppState {
  lv_obj_t *app_root{nullptr};
  lv_obj_t *background_root{nullptr};
  lv_draw_buf_t *app_buf{nullptr};
  lv_draw_buf_t *background_buf{nullptr};
  bool owns_app_buf{false};
  bool owns_background_buf{false};
  bool active{false};
  bool opening{true};
  uint64_t anim_start_us{0};
  uint32_t anim_duration_ms{0};
  int start_size{1};
  int end_size{0};
  int start_center_x{0};
  int start_center_y{0};
  int end_center_x{0};
  int end_center_y{0};
  LvglComponent *component{nullptr};
  bool worker_enabled{false};
};

#ifdef USE_ESP32
constexpr uint32_t SNAPSHOT_SWIPE_WORKER_STACK_BYTES = 6144;
constexpr uint32_t SNAPSHOT_SWIPE_WORKER_STACK_WORDS =
    (SNAPSHOT_SWIPE_WORKER_STACK_BYTES + sizeof(StackType_t) - 1) / sizeof(StackType_t);

struct SnapshotSwipeWorkerState {
  TaskHandle_t task{nullptr};
  StaticTask_t task_storage{};
  StackType_t task_stack[SNAPSHOT_SWIPE_WORKER_STACK_WORDS]{};
  std::atomic<bool> active{false};
  std::atomic<bool> running{false};
  std::atomic<bool> cancel{false};
  std::atomic<bool> finish_requested{false};
  std::atomic<bool> finish_done{false};
  std::atomic<bool> failed{false};
  std::atomic<uint32_t> request_generation{0};
  std::atomic<uint32_t> processed_generation{0};
  std::atomic<int> requested_current_x{0};
  std::atomic<int> requested_next_x{0};
  std::atomic<int> rendered_current_x{0};
  std::atomic<int> rendered_next_x{0};
  std::atomic<int> finish_current_x{0};
  std::atomic<int> finish_next_x{0};
  std::atomic<uint32_t> finish_duration_ms{0};
};

SnapshotSwipeWorkerState snapshot_swipe_worker;

constexpr uint32_t SNAPSHOT_SCROLL_WORKER_STACK_BYTES = 4096;
constexpr uint32_t SNAPSHOT_SCROLL_WORKER_STACK_WORDS =
    (SNAPSHOT_SCROLL_WORKER_STACK_BYTES + sizeof(StackType_t) - 1) / sizeof(StackType_t);

struct SnapshotScrollWorkerState {
  TaskHandle_t task{nullptr};
  StaticTask_t task_storage{};
  StackType_t task_stack[SNAPSHOT_SCROLL_WORKER_STACK_WORDS]{};
  std::atomic<bool> active{false};
  std::atomic<bool> running{false};
  std::atomic<bool> cancel{false};
  std::atomic<bool> failed{false};
  std::atomic<uint32_t> request_generation{0};
  std::atomic<uint32_t> processed_generation{0};
  std::atomic<int> requested_scroll_y{0};
  std::atomic<int> rendered_scroll_y{0};
  std::atomic<bool> inertia_requested{false};
  std::atomic<bool> inertia_done{false};
  std::atomic<int> inertia_start_y{0};
  std::atomic<int> inertia_target_y{0};
  std::atomic<int> inertia_final_y{0};
  std::atomic<uint32_t> inertia_duration_ms{0};
  std::atomic<uint32_t> inertia_bounce_duration_ms{0};
};

SnapshotScrollWorkerState snapshot_scroll_worker;

constexpr uint32_t SNAPSHOT_APP_WORKER_STACK_BYTES = 6144;
constexpr uint32_t SNAPSHOT_APP_WORKER_STACK_WORDS =
    (SNAPSHOT_APP_WORKER_STACK_BYTES + sizeof(StackType_t) - 1) / sizeof(StackType_t);

struct SnapshotAppWorkerState {
  TaskHandle_t task{nullptr};
  StaticTask_t task_storage{};
  StackType_t task_stack[SNAPSHOT_APP_WORKER_STACK_WORDS]{};
  std::atomic<bool> pending{false};
  std::atomic<bool> running{false};
  std::atomic<bool> cancel{false};
  std::atomic<bool> done{false};
  std::atomic<bool> failed{false};
};

SnapshotAppWorkerState snapshot_app_worker;
#endif

struct SnapshotCacheEntry {
  lv_obj_t *obj{nullptr};
  lv_draw_buf_t *buf{nullptr};
#ifdef USE_LVGL_SNAPSHOT_JPEG_CACHE
  esp32_jpeg::JpegBuffer jpeg;
#endif
  uint32_t width{0};
  uint32_t height{0};
  uint32_t stride{0};
  lv_color_format_t cf{LV_COLOR_FORMAT_UNKNOWN};
  bool big_endian{false};
  bool decoded_from_jpeg{false};
  bool tile_page{false};
  uint32_t generation{0};
  bool compression_in_flight{false};
};

#ifdef USE_LVGL_SNAPSHOT_JPEG_CACHE
struct SnapshotCompressionWorkerState {
  TaskHandle_t task{nullptr};
  std::atomic<bool> busy{false};
  std::atomic<bool> result_ready{false};
  SnapshotCacheEntry *entry{nullptr};
  lv_obj_t *obj{nullptr};
  lv_draw_buf_t *buf{nullptr};
  uint32_t generation{0};
  bool big_endian{false};
  uint32_t width{0};
  uint32_t height{0};
  uint32_t stride{0};
  lv_color_format_t cf{LV_COLOR_FORMAT_UNKNOWN};
  size_t raw_size{0};
  esp_err_t result{ESP_FAIL};
  uint64_t elapsed_us{0};
  esp32_jpeg::JpegBuffer jpeg;
};

SnapshotCompressionWorkerState snapshot_compression_worker;
#endif

struct SnapshotPanoramaCacheEntry {
  lv_obj_t *left{nullptr};
  lv_obj_t *right{nullptr};
  lv_obj_t *pages[4]{nullptr, nullptr, nullptr, nullptr};
  int page_count{0};
  uint8_t *buf{nullptr};
  size_t size{0};
  int width{0};
  int height{0};
  int scale{1};
};

struct SnapshotPanoramaPageSource {
  const uint8_t *data{nullptr};
  size_t stride{0};
  int width{0};
  int height{0};
  int source_scale{1};
  SnapshotPanoramaCacheEntry *owner{nullptr};
#ifdef USE_LVGL_PPA
  const uint8_t *ppa_data{nullptr};
  int ppa_pic_w{0};
  int ppa_offset_x{0};
#endif
};

constexpr size_t SNAPSHOT_TILE_SLOT_COUNT = 3;
constexpr size_t SNAPSHOT_CACHE_ENTRY_COUNT = 24;

struct SnapshotTileWindowCache {
  lv_draw_buf_t *slots[SNAPSHOT_TILE_SLOT_COUNT]{nullptr, nullptr, nullptr};
  lv_obj_t *pages[SNAPSHOT_TILE_SLOT_COUNT]{nullptr, nullptr, nullptr};
  bool dirty[SNAPSHOT_TILE_SLOT_COUNT]{false, false, false};
  int width{0};
  int height{0};
};

SnapshotSwipeState snapshot_swipe_state;
SnapshotScrollState snapshot_scroll_state;
SnapshotAppState snapshot_app_state;
lv_obj_t *snapshot_app_prepared_close_obj = nullptr;
lv_draw_buf_t *snapshot_app_prepared_close_buf = nullptr;
bool snapshot_app_prepared_close_owns_buf = false;
lv_draw_buf_t *snapshot_app_work_buf = nullptr;
lv_obj_t *snapshot_app_work_obj = nullptr;
lv_draw_buf_t snapshot_app_panorama_background_view{};
lv_draw_buf_t snapshot_swipe_panorama_page_views[2]{};
lv_draw_buf_t snapshot_swipe_edge_panorama_view{};
lv_draw_buf_t snapshot_app_presented_close_view{};
SnapshotCacheEntry snapshot_cache[SNAPSHOT_CACHE_ENTRY_COUNT];
SnapshotPanoramaCacheEntry snapshot_panorama_cache[3];
SnapshotTileWindowCache snapshot_tile_window_cache;
lv_obj_t *snapshot_cache_pending_compress_obj = nullptr;
uint32_t snapshot_cache_pending_compress_at = 0;
lv_obj_t *snapshot_cache_latest_raw_app_obj = nullptr;

lv_draw_buf_t *snapshot_tile_window_find(lv_obj_t *obj) {
  if (obj == nullptr)
    return nullptr;
  for (size_t index = 0; index < SNAPSHOT_TILE_SLOT_COUNT; index++) {
    if (snapshot_tile_window_cache.pages[index] == obj)
      return snapshot_tile_window_cache.slots[index];
  }
  return nullptr;
}

size_t snapshot_tile_window_bytes() {
  size_t bytes = 0;
  for (auto *slot : snapshot_tile_window_cache.slots) {
    if (slot != nullptr)
      bytes += slot->data_size;
  }
  return bytes;
}

bool snapshot_tile_window_reserve(int width, int height) {
  if (width <= 0 || height <= 0)
    return false;
  if (snapshot_tile_window_cache.width == width && snapshot_tile_window_cache.height == height) {
    bool ready = true;
    for (auto *slot : snapshot_tile_window_cache.slots)
      ready = ready && slot != nullptr && slot->data != nullptr;
    if (ready)
      return true;
  }

  for (size_t index = 0; index < SNAPSHOT_TILE_SLOT_COUNT; index++) {
    if (snapshot_tile_window_cache.slots[index] != nullptr)
      lv_draw_buf_destroy(snapshot_tile_window_cache.slots[index]);
    snapshot_tile_window_cache.slots[index] = nullptr;
    snapshot_tile_window_cache.pages[index] = nullptr;
    snapshot_tile_window_cache.dirty[index] = false;
  }
  snapshot_tile_window_cache.width = 0;
  snapshot_tile_window_cache.height = 0;

  for (size_t index = 0; index < SNAPSHOT_TILE_SLOT_COUNT; index++) {
    auto *slot = lv_draw_buf_create(width, height, LV_COLOR_FORMAT_RGB888, width * 3);
    if (slot == nullptr || slot->data == nullptr) {
      if (slot != nullptr)
        lv_draw_buf_destroy(slot);
      for (size_t rollback = 0; rollback < index; rollback++) {
        lv_draw_buf_destroy(snapshot_tile_window_cache.slots[rollback]);
        snapshot_tile_window_cache.slots[rollback] = nullptr;
        snapshot_tile_window_cache.pages[rollback] = nullptr;
        snapshot_tile_window_cache.dirty[rollback] = false;
      }
      ESP_LOGW(TAG, "snapshot tiles: failed to reserve slot %u/%u", static_cast<unsigned>(index + 1),
               static_cast<unsigned>(SNAPSHOT_TILE_SLOT_COUNT));
      return false;
    }
    snapshot_tile_window_cache.slots[index] = slot;
    snapshot_tile_window_cache.pages[index] = nullptr;
    snapshot_tile_window_cache.dirty[index] = false;
  }
  snapshot_tile_window_cache.width = width;
  snapshot_tile_window_cache.height = height;
  ESP_LOGI(TAG, "snapshot tiles: reserved %u reusable RGB888 slots (%u KB)",
           static_cast<unsigned>(SNAPSHOT_TILE_SLOT_COUNT),
           static_cast<unsigned>(snapshot_tile_window_bytes() / 1024));
  return true;
}

extern "C" size_t lvgl_esphome_snapshot_memory_bytes(void) {
  size_t raw_cache_bytes = 0;
  size_t jpeg_cache_bytes = 0;
  size_t panorama_bytes = 0;
  const size_t tile_window_bytes = snapshot_tile_window_bytes();
  size_t app_work_bytes = snapshot_app_work_buf == nullptr ? 0 : snapshot_app_work_buf->data_size;
  size_t scroll_bytes = 0;
  size_t prepared_close_bytes = 0;

  for (const auto &entry : snapshot_cache) {
    if (entry.buf != nullptr)
      raw_cache_bytes += entry.buf->data_size;
#ifdef USE_LVGL_SNAPSHOT_JPEG_CACHE
    jpeg_cache_bytes += entry.jpeg.capacity();
#endif
  }
  for (const auto &entry : snapshot_panorama_cache)
    panorama_bytes += entry.buf == nullptr ? 0 : entry.size;

  if (snapshot_scroll_state.content_buf != nullptr)
    scroll_bytes += snapshot_scroll_state.content_buf->data_size;
  if (snapshot_scroll_state.content_tail_buf != nullptr &&
      snapshot_scroll_state.content_tail_buf != snapshot_scroll_state.content_buf)
    scroll_bytes += snapshot_scroll_state.content_tail_buf->data_size;
  if (snapshot_app_prepared_close_owns_buf && snapshot_app_prepared_close_buf != nullptr &&
      snapshot_app_prepared_close_buf != snapshot_app_work_buf)
    prepared_close_bytes = snapshot_app_prepared_close_buf->data_size;

#ifdef USE_LVGL_SNAPSHOT_JPEG_CACHE
  if (snapshot_compression_worker.result_ready.load(std::memory_order_acquire))
    jpeg_cache_bytes += snapshot_compression_worker.jpeg.capacity();
#endif
#ifdef USE_LVGL_PPA
  const size_t app_low_res_bytes = s_snapshot_app_low_res.allocation == nullptr
                                       ? 0
                                       : s_snapshot_app_low_res.allocation_bytes;
#else
  constexpr size_t app_low_res_bytes = 0;
#endif
  return raw_cache_bytes + jpeg_cache_bytes + panorama_bytes + tile_window_bytes + app_work_bytes + scroll_bytes +
         prepared_close_bytes + app_low_res_bytes;
}

extern "C" void lvgl_esphome_snapshot_log_memory(const char *phase) {
  size_t raw_cache_bytes = 0;
  size_t jpeg_cache_bytes = 0;
  size_t panorama_bytes = 0;
  const size_t tile_window_bytes = snapshot_tile_window_bytes();
  size_t scroll_bytes = 0;
  for (const auto &entry : snapshot_cache) {
    if (entry.buf != nullptr)
      raw_cache_bytes += entry.buf->data_size;
#ifdef USE_LVGL_SNAPSHOT_JPEG_CACHE
    jpeg_cache_bytes += entry.jpeg.capacity();
#endif
  }
  for (const auto &entry : snapshot_panorama_cache)
    panorama_bytes += entry.buf == nullptr ? 0 : entry.size;
  if (snapshot_scroll_state.content_buf != nullptr)
    scroll_bytes += snapshot_scroll_state.content_buf->data_size;
  if (snapshot_scroll_state.content_tail_buf != nullptr &&
      snapshot_scroll_state.content_tail_buf != snapshot_scroll_state.content_buf)
    scroll_bytes += snapshot_scroll_state.content_tail_buf->data_size;
  const size_t app_work_bytes = snapshot_app_work_buf == nullptr ? 0 : snapshot_app_work_buf->data_size;
  const size_t prepared_close_bytes =
      snapshot_app_prepared_close_owns_buf && snapshot_app_prepared_close_buf != nullptr &&
              snapshot_app_prepared_close_buf != snapshot_app_work_buf
          ? snapshot_app_prepared_close_buf->data_size
          : 0;
#ifdef USE_LVGL_PPA
  const size_t app_low_res_bytes = s_snapshot_app_low_res.allocation == nullptr
                                       ? 0
                                       : s_snapshot_app_low_res.allocation_bytes;
#else
  constexpr size_t app_low_res_bytes = 0;
#endif
  ESP_LOGW("memory.snapshot",
           "%s total=%uK panorama=%uK tiles=%uK raw=%uK jpeg=%uK app_work=%uK scroll=%uK close=%uK lowres=%uK",
           phase == nullptr ? "runtime" : phase, (unsigned) (lvgl_esphome_snapshot_memory_bytes() / 1024),
           (unsigned) (panorama_bytes / 1024), (unsigned) (tile_window_bytes / 1024),
           (unsigned) (raw_cache_bytes / 1024),
           (unsigned) (jpeg_cache_bytes / 1024), (unsigned) (app_work_bytes / 1024),
           (unsigned) (scroll_bytes / 1024), (unsigned) (prepared_close_bytes / 1024),
           (unsigned) (app_low_res_bytes / 1024));
}

constexpr lv_color_format_t SNAPSHOT_CF = LV_COLOR_FORMAT_RGB888;
constexpr int SNAPSHOT_PANORAMA_SCALE = 1;
constexpr bool SNAPSHOT_DIRECT_COMPOSITOR_ENABLED = true;
constexpr bool SNAPSHOT_JPEG_CACHE_ENABLED = true;
// Application transitions expose large text and high-contrast Material shapes.
// Keep their cached frame visually identical to the live LVGL page; reducing
// quality here is immediately visible as a soft opening/closing animation.
constexpr uint32_t SNAPSHOT_JPEG_QUALITY = 100;
bool snapshot_jpeg_bootstrap_complete = false;
constexpr int SNAPSHOT_APP_OPEN_START_SIZE = 1;
constexpr int SNAPSHOT_APP_OPEN_MIN_PRESENT_SIZE = 96;
constexpr uint32_t SNAPSHOT_APP_OPEN_FIRST_FRAME_ADVANCE_MS = 16;
uint32_t snapshot_diag_budget = 24;

#ifdef USE_ESP32
uint64_t snapshot_diag_now_us_() { return esp_timer_get_time(); }

void snapshot_log_heap_(const char *stage, const void *obj, bool force) {
  if (!force && snapshot_diag_budget == 0)
    return;
  if (!force)
    snapshot_diag_budget--;
  ESP_LOGW(TAG, "snapshot diag: %s obj=%p internal=%uK/%uK psram=%uK/%uK direct=%u active=%u", stage, obj,
           (unsigned) (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
           (unsigned) (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
           (unsigned) (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
           (unsigned) (heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024), (unsigned) s_direct_mode_active,
           (unsigned) s_snapshot_direct_active);
}
#else
uint64_t snapshot_diag_now_us_() { return 0; }

void snapshot_log_heap_(const char *stage, const void *obj, bool force) {
  if (!force && snapshot_diag_budget == 0)
    return;
  if (!force)
    snapshot_diag_budget--;
  ESP_LOGW(TAG, "snapshot diag: %s obj=%p", stage, obj);
}
#endif

SnapshotCacheEntry *snapshot_cache_find_entry(lv_obj_t *obj) {
  for (auto &entry : snapshot_cache) {
    if (entry.obj == obj)
      return &entry;
  }
  return nullptr;
}

void snapshot_cache_destroy_raw(SnapshotCacheEntry &entry) {
  if (entry.buf != nullptr) {
    lv_draw_buf_destroy(entry.buf);
    entry.buf = nullptr;
  }
  entry.decoded_from_jpeg = false;
}

void snapshot_cache_destroy_jpeg(SnapshotCacheEntry &entry) {
#ifdef USE_LVGL_SNAPSHOT_JPEG_CACHE
  entry.jpeg.release();
#endif
  entry.width = 0;
  entry.height = 0;
  entry.stride = 0;
  entry.cf = LV_COLOR_FORMAT_UNKNOWN;
}

bool snapshot_cache_obj_big_endian(lv_obj_t *obj) {
  if (obj == nullptr)
    return false;
  auto *disp = lv_obj_get_display(obj);
  auto *component = disp == nullptr ? nullptr : static_cast<LvglComponent *>(lv_display_get_user_data(disp));
  return component != nullptr && component->is_big_endian();
}

void snapshot_cache_free_entry(SnapshotCacheEntry &entry) {
  if (entry.compression_in_flight)
    return;
  snapshot_cache_destroy_raw(entry);
  snapshot_cache_destroy_jpeg(entry);
  entry.obj = nullptr;
  entry.big_endian = false;
  entry.tile_page = false;
  entry.generation++;
}

#ifdef USE_LVGL_SNAPSHOT_JPEG_CACHE
void snapshot_compression_worker_task(void *) {
  auto &worker = snapshot_compression_worker;
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    const esp32_jpeg::EncodeConfig encode_cfg = {
        .width = worker.width,
        .height = worker.height,
        .input_format = esp32_jpeg::PixelFormat::RGB888,
        .down_sampling = esp32_jpeg::DownSampling::YUV444,
        .quality = SNAPSHOT_JPEG_QUALITY,
        .pixel_reverse = worker.big_endian,
        .retain_output_buffer = false,
        // Snapshot compression runs while DSI continuously scans PSRAM.
        // Keep each JPEG DMA2D transaction short enough to leave regular
        // service windows for the display, just like the synchronous path.
        .dma2d_burst_length = 8,
        .dma2d_descriptor_burst = 0,
        .timeout_ms = 120,
    };
    esp32_jpeg::JpegBuffer output;
    const uint64_t started_us = snapshot_diag_now_us_();
    const esp_err_t err = esp32_jpeg::encode(encode_cfg, static_cast<const uint8_t *>(worker.buf->data),
                                             worker.raw_size, &output);
    worker.elapsed_us = snapshot_diag_now_us_() - started_us;
    worker.result = err;
    worker.jpeg = std::move(output);
    std::atomic_thread_fence(std::memory_order_release);
    worker.result_ready.store(true, std::memory_order_release);
  }
}

bool snapshot_compression_worker_setup() {
  auto &worker = snapshot_compression_worker;
  if (worker.task != nullptr)
    return true;
#if CONFIG_FREERTOS_UNICORE
  constexpr BaseType_t worker_core = tskNO_AFFINITY;
#elif defined(CONFIG_ESP_MAIN_TASK_AFFINITY_CPU0) && CONFIG_ESP_MAIN_TASK_AFFINITY_CPU0
  constexpr BaseType_t worker_core = 1;
#elif defined(CONFIG_ESP_MAIN_TASK_AFFINITY_CPU1) && CONFIG_ESP_MAIN_TASK_AFFINITY_CPU1
  constexpr BaseType_t worker_core = 0;
#else
  constexpr BaseType_t worker_core = 1;
#endif
  TaskHandle_t task = nullptr;
  const BaseType_t created = xTaskCreatePinnedToCore(snapshot_compression_worker_task, "lvgl_jpeg_cache", 6144, nullptr,
                                                     1, &task, worker_core);
  if (created != pdPASS) {
    ESP_LOGW(TAG, "snapshot jpeg: failed to create asynchronous compression worker");
    return false;
  }
  worker.task = task;
  ESP_LOGI(TAG, "Snapshot JPEG compression worker enabled on core %d", (int) worker_core);
  return true;
}
#endif

bool snapshot_cache_encode_jpeg(SnapshotCacheEntry &entry, lv_draw_buf_t *buf) {
#if defined(USE_LVGL_SNAPSHOT_JPEG_CACHE) && LV_COLOR_DEPTH == 32
  if (!SNAPSHOT_JPEG_CACHE_ENABLED || buf == nullptr || buf->data == nullptr)
    return false;
  if (buf->header.cf != LV_COLOR_FORMAT_RGB888)
    return false;
  const uint32_t width = buf->header.w;
  const uint32_t height = buf->header.h;
  const uint32_t stride = buf->header.stride;
  const size_t raw_size = buf->data_size != 0 ? buf->data_size : (size_t) stride * height;
  if (width == 0 || height == 0 || stride != width * 3 || raw_size < (size_t) stride * height)
    return false;
  if ((width % 16) != 0 || (height % 16) != 0)
    return false;

  esp32_jpeg::EncodeConfig encode_cfg = {
      .width = width,
      .height = height,
      .input_format = esp32_jpeg::PixelFormat::RGB888,
      .down_sampling = esp32_jpeg::DownSampling::YUV444,
      .quality = SNAPSHOT_JPEG_QUALITY,
      .pixel_reverse = entry.big_endian,
      .retain_output_buffer = !snapshot_jpeg_bootstrap_complete,
      .dma2d_burst_length = 8,
      .dma2d_descriptor_burst = 0,
      .timeout_ms = 120,
  };
  esp32_jpeg::JpegBuffer out;
  const uint64_t t0 = snapshot_diag_now_us_();
  const esp_err_t err = esp32_jpeg::encode(encode_cfg, static_cast<const uint8_t *>(buf->data), raw_size, &out);
  const uint64_t elapsed_us = snapshot_diag_now_us_() - t0;

  bool stored = false;
  if (err == ESP_OK && out.size() > 0 && out.size() < raw_size) {
    snapshot_cache_destroy_jpeg(entry);
    entry.jpeg = std::move(out);
    entry.width = width;
    entry.height = height;
    entry.stride = stride;
    entry.cf = static_cast<lv_color_format_t>(buf->header.cf);
    stored = true;
  }

  if (s_swipe_logging_enabled) {
    if (stored) {
      ESP_LOGI(TAG, "snapshot jpeg: encoded %ux%u %u KB -> %u KB q=%u in %lluus", (unsigned) width, (unsigned) height,
               (unsigned) (raw_size / 1024), (unsigned) (entry.jpeg.size() / 1024), (unsigned) SNAPSHOT_JPEG_QUALITY,
               (unsigned long long) elapsed_us);
    } else {
      ESP_LOGW(TAG, "snapshot jpeg: encode skipped/failed err=%d out=%u raw=%u in %lluus", (int) err,
               (unsigned) out.size(), (unsigned) raw_size, (unsigned long long) elapsed_us);
    }
  }
  return stored;
#else
  return false;
#endif
}

bool snapshot_cache_decode_jpeg_to_draw_buf(SnapshotCacheEntry &entry, lv_draw_buf_t *decoded) {
#if defined(USE_LVGL_SNAPSHOT_JPEG_CACHE) && LV_COLOR_DEPTH == 32
  if (decoded == nullptr || decoded->data == nullptr || entry.jpeg.empty() || entry.width == 0 || entry.height == 0)
    return false;

  esp32_jpeg::PictureInfo info = {};
  if (esp32_jpeg::get_info(entry.jpeg.data(), entry.jpeg.size(), &info) != ESP_OK || info.width != entry.width ||
      info.height != entry.height) {
    return false;
  }

  if (lv_draw_buf_reshape(decoded, entry.cf, entry.width, entry.height, entry.stride) == nullptr)
    return false;

  esp32_jpeg::DecodeConfig decode_cfg = {
      .output_format = esp32_jpeg::PixelFormat::RGB888,
      // ESP32-P4's JPEG RGB888 path uses the same byte layout convention as
      // LVGL RGB888: little-endian buffers are B,G,R and big-endian buffers are
      // R,G,B. Decode to the configured LVGL byte layout before the direct
      // compositor copies the snapshot into the display framebuffer.
      .rgb_order = entry.big_endian ? esp32_jpeg::RgbElementOrder::RGB : esp32_jpeg::RgbElementOrder::BGR,
      .color_conversion = esp32_jpeg::ColorConversionStandard::BT601,
      .direct_output = true,
      .dma2d_burst_length = 8,
      .dma2d_descriptor_burst = 0,
      .timeout_ms = 120,
  };
  size_t out_size = 0;
  const uint64_t t0 = snapshot_diag_now_us_();
  const esp_err_t err = esp32_jpeg::decode(decode_cfg, entry.jpeg.data(), entry.jpeg.size(),
                                           static_cast<uint8_t *>(decoded->data), decoded->data_size, &out_size);
  const uint64_t elapsed_us = snapshot_diag_now_us_() - t0;

  if (err != ESP_OK || out_size == 0) {
    if (s_swipe_logging_enabled) {
      ESP_LOGW(TAG, "snapshot jpeg: decode failed err=%d out=%u jpeg=%u in %lluus", (int) err, (unsigned) out_size,
               (unsigned) entry.jpeg.size(), (unsigned long long) elapsed_us);
    }
    return false;
  }

  if (s_swipe_logging_enabled) {
    ESP_LOGI(TAG, "snapshot jpeg: decoded %u KB -> %u KB order=%s in %lluus", (unsigned) (entry.jpeg.size() / 1024),
             (unsigned) (decoded->data_size / 1024), entry.big_endian ? "rgb" : "bgr", (unsigned long long) elapsed_us);
  }
  return true;
#else
  (void) entry;
  (void) decoded;
  return false;
#endif
}

lv_draw_buf_t *snapshot_cache_decode_jpeg(SnapshotCacheEntry &entry) {
#if defined(USE_LVGL_SNAPSHOT_JPEG_CACHE) && LV_COLOR_DEPTH == 32
  if (entry.buf != nullptr)
    return entry.buf;
  if (entry.jpeg.empty() || entry.width == 0 || entry.height == 0)
    return nullptr;

  auto *decoded = lv_draw_buf_create(entry.width, entry.height, entry.cf, entry.stride);
  if (decoded == nullptr || decoded->data == nullptr || !snapshot_cache_decode_jpeg_to_draw_buf(entry, decoded)) {
    if (decoded != nullptr)
      lv_draw_buf_destroy(decoded);
    return nullptr;
  }

  entry.buf = decoded;
  entry.decoded_from_jpeg = true;
  return entry.buf;
#else
  return nullptr;
#endif
}

lv_draw_buf_t *snapshot_cache_find(lv_obj_t *obj) {
  if (auto *tile = snapshot_tile_window_find(obj))
    return tile;
  auto *entry = snapshot_cache_find_entry(obj);
  if (entry == nullptr)
    return nullptr;
  if (entry->buf != nullptr)
    return entry->buf;
  // Home pages outside the three-slot working set stay JPEG-only. They are
  // decoded into a recycled tile slot by snapshot_cache_tile_window(); never
  // allocate a fourth display-sized raw buffer from this generic lookup.
  if (entry->tile_page)
    return nullptr;
  return snapshot_cache_decode_jpeg(*entry);
}

void snapshot_cache_release_decoded_if_compressed(lv_obj_t *obj) {
#ifdef USE_LVGL_SNAPSHOT_JPEG_CACHE
  auto *entry = snapshot_cache_find_entry(obj);
  if (entry == nullptr || entry->jpeg.empty() || entry->buf == nullptr)
    return;
  snapshot_cache_destroy_raw(*entry);
  if (snapshot_cache_latest_raw_app_obj == obj)
    snapshot_cache_latest_raw_app_obj = nullptr;
#else
  (void) obj;
#endif
}

void snapshot_panorama_free_entry(SnapshotPanoramaCacheEntry &entry) {
#ifdef USE_ESP32
  if (entry.buf != nullptr) {
    heap_caps_free(entry.buf);
    entry.buf = nullptr;
  }
#endif
  entry.left = nullptr;
  entry.right = nullptr;
  for (auto &page : entry.pages)
    page = nullptr;
  entry.page_count = 0;
  entry.size = 0;
  entry.width = 0;
  entry.height = 0;
  entry.scale = 1;
}

void snapshot_panorama_cache_invalidate(lv_obj_t *obj) {
  if (obj == nullptr)
    return;
  for (auto &entry : snapshot_panorama_cache) {
    bool contains = entry.left == obj || entry.right == obj;
    for (int i = 0; !contains && i < entry.page_count; i++)
      contains = entry.pages[i] == obj;
    if (contains)
      snapshot_panorama_free_entry(entry);
  }
}

void snapshot_cache_store_impl(lv_obj_t *obj, lv_draw_buf_t *buf, bool keep_raw_fallback) {
  SnapshotCacheEntry *slot = nullptr;
  for (auto &entry : snapshot_cache) {
    if (entry.obj == obj) {
      if (entry.compression_in_flight) {
        // The worker still reads the current raw snapshot. Keep that coherent
        // image and discard a rare rapid-close replacement instead of freeing
        // memory from under the JPEG peripheral.
        lv_draw_buf_destroy(buf);
        return;
      }
      snapshot_panorama_cache_invalidate(obj);
      snapshot_cache_destroy_raw(entry);
      entry.big_endian = snapshot_cache_obj_big_endian(obj);
      if (snapshot_cache_encode_jpeg(entry, buf)) {
        lv_draw_buf_destroy(buf);
      } else {
        snapshot_cache_destroy_jpeg(entry);
        if (keep_raw_fallback) {
          entry.buf = buf;
        } else {
          snapshot_cache_free_entry(entry);
          lv_draw_buf_destroy(buf);
        }
      }
      return;
    }
    if (slot == nullptr && entry.obj == nullptr && !entry.compression_in_flight)
      slot = &entry;
  }
  if (slot == nullptr) {
    for (auto &entry : snapshot_cache) {
      if (!entry.compression_in_flight && !entry.tile_page) {
        slot = &entry;
        break;
      }
    }
  }
  if (slot == nullptr) {
    lv_draw_buf_destroy(buf);
    return;
  }
  snapshot_panorama_cache_invalidate(slot->obj);
  snapshot_cache_free_entry(*slot);
  slot->obj = obj;
  slot->big_endian = snapshot_cache_obj_big_endian(obj);
  if (snapshot_cache_encode_jpeg(*slot, buf)) {
    lv_draw_buf_destroy(buf);
  } else {
    if (keep_raw_fallback) {
      slot->buf = buf;
    } else {
      snapshot_cache_free_entry(*slot);
      lv_draw_buf_destroy(buf);
    }
  }
}

void snapshot_cache_store(lv_obj_t *obj, lv_draw_buf_t *buf) { snapshot_cache_store_impl(obj, buf, true); }

void snapshot_cache_store_compressed_only(lv_obj_t *obj, lv_draw_buf_t *buf) {
  snapshot_cache_store_impl(obj, buf, false);
}

bool snapshot_cache_store_compressed_view(lv_obj_t *obj, lv_draw_buf_t *buf) {
  if (obj == nullptr || buf == nullptr || buf->data == nullptr)
    return false;

  SnapshotCacheEntry *slot = snapshot_cache_find_entry(obj);
  if (slot != nullptr && slot->compression_in_flight)
    return false;
  if (slot == nullptr) {
    for (auto &entry : snapshot_cache) {
      if (entry.obj == nullptr && !entry.compression_in_flight) {
        slot = &entry;
        break;
      }
    }
  }
  if (slot == nullptr) {
    for (auto &entry : snapshot_cache) {
      if (!entry.compression_in_flight && !entry.tile_page) {
        slot = &entry;
        break;
      }
    }
  }
  if (slot == nullptr)
    return false;

  snapshot_panorama_cache_invalidate(slot->obj);
  snapshot_cache_free_entry(*slot);
  slot->obj = obj;
  slot->big_endian = snapshot_cache_obj_big_endian(obj);
  if (snapshot_cache_encode_jpeg(*slot, buf))
    return true;

  snapshot_cache_free_entry(*slot);
  return false;
}

lv_draw_buf_t *snapshot_take_centered(lv_obj_t *obj);

void snapshot_cache_store_raw_only(lv_obj_t *obj, lv_draw_buf_t *buf) {
  if (obj == nullptr || buf == nullptr)
    return;
  SnapshotCacheEntry *slot = snapshot_cache_find_entry(obj);
  if (slot == nullptr) {
    for (auto &entry : snapshot_cache) {
      if (entry.obj == nullptr) {
        slot = &entry;
        break;
      }
    }
  }
  if (slot == nullptr)
    for (auto &entry : snapshot_cache) {
      if (!entry.compression_in_flight && !entry.tile_page) {
        slot = &entry;
        break;
      }
    }
  if (slot == nullptr) {
    lv_draw_buf_destroy(buf);
    return;
  }
  if (slot->compression_in_flight) {
    lv_draw_buf_destroy(buf);
    return;
  }

  snapshot_panorama_cache_invalidate(slot->obj);
  const auto incoming_cf = static_cast<lv_color_format_t>(buf->header.cf);
  const bool preserve_compressed_fallback =
#ifdef USE_LVGL_SNAPSHOT_JPEG_CACHE
      slot->obj == obj && !slot->jpeg.empty() && slot->width == buf->header.w && slot->height == buf->header.h &&
      slot->stride == buf->header.stride && slot->cf == incoming_cf;
#else
      false;
#endif
  snapshot_cache_destroy_raw(*slot);
  if (!preserve_compressed_fallback)
    snapshot_cache_destroy_jpeg(*slot);
  slot->obj = obj;
  slot->big_endian = snapshot_cache_obj_big_endian(obj);
  slot->buf = buf;
  slot->cf = incoming_cf;
  slot->width = buf->header.w;
  slot->height = buf->header.h;
  slot->stride = buf->header.stride;
  slot->generation++;
}

void snapshot_cache_schedule_compression(lv_obj_t *obj, uint32_t delay_ms) {
#ifdef USE_LVGL_SNAPSHOT_JPEG_CACHE
  snapshot_cache_pending_compress_obj = obj;
  snapshot_cache_pending_compress_at = millis() + delay_ms;
#else
  (void) obj;
  (void) delay_ms;
#endif
}

void snapshot_cache_process_pending_compression() {
#ifdef USE_LVGL_SNAPSHOT_JPEG_CACHE
  auto &worker = snapshot_compression_worker;
  if (worker.result_ready.load(std::memory_order_acquire)) {
    std::atomic_thread_fence(std::memory_order_acquire);
    auto *entry = worker.entry;
    const bool current = entry != nullptr && entry->obj == worker.obj && entry->buf == worker.buf &&
                         entry->generation == worker.generation && entry->compression_in_flight;
    const bool stored = current && worker.result == ESP_OK && !worker.jpeg.empty() &&
                        worker.jpeg.size() < worker.raw_size;
    if (current) {
      entry->compression_in_flight = false;
      if (stored) {
        snapshot_cache_destroy_jpeg(*entry);
        entry->jpeg = std::move(worker.jpeg);
        entry->width = worker.width;
        entry->height = worker.height;
        entry->stride = worker.stride;
        entry->cf = worker.cf;
        snapshot_cache_destroy_raw(*entry);
        if (snapshot_cache_latest_raw_app_obj == worker.obj)
          snapshot_cache_latest_raw_app_obj = nullptr;
      }
    }
    if (s_swipe_logging_enabled) {
      ESP_LOGI(TAG, "snapshot jpeg: async compression %s err=%d raw=%uKB jpeg=%uKB in %lluus",
               stored ? "stored" : "discarded", (int) worker.result, (unsigned) (worker.raw_size / 1024),
               (unsigned) (worker.jpeg.size() / 1024), (unsigned long long) worker.elapsed_us);
    }
    worker.jpeg.release();
    worker.entry = nullptr;
    worker.obj = nullptr;
    worker.buf = nullptr;
    worker.result_ready.store(false, std::memory_order_release);
    worker.busy.store(false, std::memory_order_release);
  }

  if (snapshot_cache_pending_compress_obj == nullptr ||
      static_cast<int32_t>(millis() - snapshot_cache_pending_compress_at) < 0) {
    return;
  }

  if (worker.busy.load(std::memory_order_acquire))
    return;

  lv_obj_t *obj = snapshot_cache_pending_compress_obj;
  auto *entry = snapshot_cache_find_entry(obj);
  if (entry == nullptr || entry->buf == nullptr) {
    snapshot_cache_pending_compress_obj = nullptr;
    return;
  }
  const uint32_t width = entry->buf->header.w;
  const uint32_t height = entry->buf->header.h;
  const uint32_t stride = entry->buf->header.stride;
  const size_t raw_size = entry->buf->data_size != 0 ? entry->buf->data_size : (size_t) stride * height;
  if (entry->buf->header.cf != LV_COLOR_FORMAT_RGB888 || width == 0 || height == 0 || stride != width * 3 ||
      raw_size < (size_t) stride * height || (width % 16) != 0 || (height % 16) != 0 ||
      !snapshot_compression_worker_setup()) {
    snapshot_cache_pending_compress_obj = nullptr;
    return;
  }

  worker.entry = entry;
  worker.obj = obj;
  worker.buf = entry->buf;
  worker.generation = entry->generation;
  worker.big_endian = entry->big_endian;
  worker.width = width;
  worker.height = height;
  worker.stride = stride;
  worker.cf = static_cast<lv_color_format_t>(entry->buf->header.cf);
  worker.raw_size = raw_size;
  worker.result = ESP_FAIL;
  worker.elapsed_us = 0;
  entry->compression_in_flight = true;
  snapshot_cache_pending_compress_obj = nullptr;
  worker.busy.store(true, std::memory_order_release);
  std::atomic_thread_fence(std::memory_order_release);
  xTaskNotifyGive(worker.task);
#endif
}

bool snapshot_cache_prepare_raw_page(lv_obj_t *obj) {
#if LV_USE_SNAPSHOT
  if (obj == nullptr)
    return false;
  auto *entry = snapshot_cache_find_entry(obj);
  if (entry != nullptr && entry->buf != nullptr)
    return true;

#ifdef USE_LVGL_SNAPSHOT_JPEG_CACHE
  // Boot already stores every home page as a compact JPEG. Decode that image
  // straight into its persistent raw page buffer instead of rendering the
  // complete hidden LVGL tree a second time. Apart from being much faster,
  // this avoids a long stream of CPU/PPA writes competing with DSI for PSRAM.
  if (entry != nullptr && !entry->jpeg.empty()) {
    lvgl_esphome_wait_snapshot_dsi_fifo();
    if (esphome_mipi_dsi_mark_stress != nullptr)
      esphome_mipi_dsi_mark_stress("snapshot-jpeg-decode", 220);
    const uint64_t t0 = snapshot_diag_now_us_();
    auto *decoded = snapshot_cache_decode_jpeg(*entry);
    const uint64_t decode_us = snapshot_diag_now_us_() - t0;
    lvgl_esphome_wait_snapshot_dsi_fifo();
    lvgl_esphome_snapshot_dsi_quiet(CONFIG_ESPHOME_LVGL_SNAPSHOT_RAW_DSI_QUIET_MS);
    if (decoded != nullptr) {
      if (decode_us > 50000 || snapshot_diag_budget > 0) {
        ESP_LOGW(TAG, "snapshot diag: raw page decoded from JPEG obj=%p took=%lluus size=%uKB", obj,
                 (unsigned long long) decode_us, (unsigned) (decoded->data_size / 1024));
      }
      return true;
    }
    ESP_LOGW(TAG, "snapshot jpeg: raw page decode failed; rendering LVGL fallback obj=%p", obj);
  }
#endif

  lvgl_esphome_wait_snapshot_dsi_fifo();
  if (esphome_mipi_dsi_mark_stress != nullptr) {
    esphome_mipi_dsi_mark_stress("snapshot-raw-page", 160);
  }
  const uint64_t t0 = snapshot_diag_now_us_();
  auto *buf = snapshot_take_centered(obj);
  if (buf == nullptr) {
    ESP_LOGW(TAG, "snapshot diag: raw page cache failed obj=%p", obj);
    snapshot_log_heap_("raw page cache failed", obj, true);
    return false;
  }
  const uint64_t take_us = snapshot_diag_now_us_() - t0;
  lvgl_esphome_wait_snapshot_dsi_fifo();
  snapshot_cache_store_raw_only(obj, buf);
  lvgl_esphome_wait_snapshot_dsi_fifo();
  lvgl_esphome_snapshot_dsi_quiet(CONFIG_ESPHOME_LVGL_SNAPSHOT_RAW_DSI_QUIET_MS);
  if (take_us > 50000 || snapshot_diag_budget > 0) {
    ESP_LOGW(TAG, "snapshot diag: raw page cache obj=%p took=%lluus size=%uKB cf=%u stride=%u", obj,
             (unsigned long long) take_us, (unsigned) (buf->data_size / 1024), (unsigned) buf->header.cf,
             (unsigned) buf->header.stride);
  }
  return true;
#else
  return false;
#endif
}

void snapshot_swipe_clear_panorama() {
  snapshot_swipe_state.panorama_buf = nullptr;
  snapshot_swipe_state.panorama_size = 0;
  snapshot_swipe_state.panorama_scale = 1;
  snapshot_swipe_state.panorama_next_x = 0;
  snapshot_swipe_state.panorama_page_count = 2;
  snapshot_swipe_state.panorama_source_page_index = 0;
  snapshot_swipe_state.panorama_render = false;
}

static inline void snapshot_swipe_copy_rgb888_scaled_row(const uint8_t *src, uint8_t *dst, int width, int scale) {
  if (scale == 1) {
    memcpy(dst, src, (size_t) width * 3);
    return;
  }
  for (int x = 0; x < width; x++) {
    const int src_x = x * scale;
    dst[x * 3 + 0] = src[src_x * 3 + 0];
    dst[x * 3 + 1] = src[src_x * 3 + 1];
    dst[x * 3 + 2] = src[src_x * 3 + 2];
  }
}

int snapshot_panorama_pair_index(const SnapshotPanoramaCacheEntry &entry, lv_obj_t *left, lv_obj_t *right) {
  if (entry.page_count >= 2) {
    for (int i = 0; i + 1 < entry.page_count; i++) {
      if (entry.pages[i] == left && entry.pages[i + 1] == right)
        return i;
    }
  }
  return entry.left == left && entry.right == right ? 0 : -1;
}

SnapshotPanoramaCacheEntry *snapshot_panorama_cache_find(lv_obj_t *left, lv_obj_t *right, int width, int scale,
                                                         int *source_page_index = nullptr) {
  for (auto &entry : snapshot_panorama_cache) {
    const int pair_index = snapshot_panorama_pair_index(entry, left, right);
    if (pair_index >= 0 && entry.width == width && entry.scale == scale && entry.buf != nullptr) {
      if (source_page_index != nullptr)
        *source_page_index = pair_index;
      return &entry;
    }
  }
  return nullptr;
}

SnapshotPanoramaCacheEntry *snapshot_panorama_cache_alloc_slot(SnapshotPanoramaCacheEntry *protect_a,
                                                               SnapshotPanoramaCacheEntry *protect_b) {
  for (auto &entry : snapshot_panorama_cache) {
    if (&entry != protect_a && &entry != protect_b && entry.buf == nullptr)
      return &entry;
  }
  for (auto &entry : snapshot_panorama_cache) {
    if (&entry != protect_a && &entry != protect_b)
      return &entry;
  }
  return nullptr;
}

bool snapshot_panorama_source_from_buffer(lv_draw_buf_t *buf, int source_scale, SnapshotPanoramaPageSource *source) {
  if (buf == nullptr || buf->data == nullptr || source == nullptr)
    return false;
  if (buf->header.cf != LV_COLOR_FORMAT_RGB888 || source_scale <= 0)
    return false;
  source->data = static_cast<const uint8_t *>(buf->data);
  source->stride = buf->header.stride;
  source->width = buf->header.w;
  source->height = buf->header.h;
  source->source_scale = source_scale;
  source->owner = nullptr;
#ifdef USE_LVGL_PPA
  source->ppa_data = source->data;
  source->ppa_pic_w = source->stride > 0 ? (int) (source->stride / 3) : source->width;
  source->ppa_offset_x = 0;
#endif
  return true;
}

bool snapshot_panorama_source_from_cache(lv_obj_t *obj, int width, int scale, SnapshotPanoramaPageSource *source) {
#if defined(USE_ESP32) && LV_COLOR_DEPTH == 32
  if (obj == nullptr || width <= 0 || scale <= 0 || source == nullptr)
    return false;
  if ((width % scale) != 0)
    return false;
  const int scaled_width = width / scale;
  const size_t page_bytes = (size_t) scaled_width * 3;
  for (auto &entry : snapshot_panorama_cache) {
    if (entry.buf == nullptr || entry.width != width || entry.scale != scale || entry.height <= 0)
      continue;
    int page_index = -1;
    for (int i = 0; i < entry.page_count; i++) {
      if (entry.pages[i] == obj) {
        page_index = i;
        break;
      }
    }
    if (page_index < 0) {
      if (entry.left == obj)
        page_index = 0;
      else if (entry.right == obj)
        page_index = 1;
    }
    if (page_index < 0)
      continue;
    const int page_count = std::max(2, entry.page_count);
    const size_t panorama_stride = page_bytes * page_count;
    const size_t offset = page_bytes * page_index;
    source->data = entry.buf + offset;
    source->stride = panorama_stride;
    source->width = scaled_width;
    source->height = entry.height;
    source->source_scale = 1;
    source->owner = &entry;
#ifdef USE_LVGL_PPA
    source->ppa_data = entry.buf;
    source->ppa_pic_w = scaled_width * page_count;
    source->ppa_offset_x = page_index * scaled_width;
#endif
    return true;
  }
#endif
  return false;
}

bool snapshot_panorama_source_view(const SnapshotPanoramaPageSource &source, int width, int height,
                                   lv_draw_buf_t *view) {
#if defined(USE_ESP32) && LV_COLOR_DEPTH == 32
  if (view == nullptr || source.data == nullptr || source.owner == nullptr || source.owner->buf == nullptr ||
      source.stride == 0 || source.width < width || source.height < height)
    return false;
  const uint32_t panorama_width = static_cast<uint32_t>(source.stride / 3U);
  if (lv_draw_buf_init(view, panorama_width, height, LV_COLOR_FORMAT_RGB888, static_cast<uint32_t>(source.stride),
                       source.owner->buf, static_cast<uint32_t>(source.owner->size)) != LV_RESULT_OK) {
    return false;
  }
  view->data = const_cast<uint8_t *>(source.data);
  view->unaligned_data = const_cast<uint8_t *>(source.data);
  view->header.w = static_cast<uint32_t>(width);
  view->header.h = static_cast<uint32_t>(height);
  view->data_size = static_cast<uint32_t>((static_cast<size_t>(height - 1) * source.stride) +
                                          static_cast<size_t>(width) * 3U);
  return true;
#else
  (void) source;
  (void) width;
  (void) height;
  (void) view;
  return false;
#endif
}

lv_draw_buf_t *snapshot_app_background_from_panorama(lv_obj_t *obj) {
#if defined(USE_ESP32) && LV_COLOR_DEPTH == 32
  if (obj == nullptr)
    return nullptr;
  // The current Home page already lives in the fixed three-slot working set.
  // Reuse it directly under app open/close animations; no panorama view or
  // display-sized background copy is needed.
  if (auto *tile = snapshot_tile_window_find(obj))
    return tile;
  auto *display = lv_obj_get_display(obj);
  if (display == nullptr)
    return nullptr;

  const int width = lv_display_get_horizontal_resolution(display);
  const int height = lv_display_get_vertical_resolution(display);
  SnapshotPanoramaPageSource source{};
  if (!snapshot_panorama_source_from_cache(obj, width, 1, &source) || source.owner == nullptr ||
      source.owner->buf == nullptr || source.width != width || source.height < height || source.stride == 0)
    return nullptr;

  // Initialise the LVGL metadata against the complete allocation first, then
  // turn it into a read-only page view. The page rows retain the panorama
  // stride, while data points directly at the selected page. App transitions
  // can therefore consume the already cached Home frame without allocating or
  // copying another display-sized RGB888 buffer.
  const uint32_t panorama_width = static_cast<uint32_t>(source.stride / 3U);
  if (lv_draw_buf_init(&snapshot_app_panorama_background_view, panorama_width, height, LV_COLOR_FORMAT_RGB888,
                       static_cast<uint32_t>(source.stride), source.owner->buf,
                       static_cast<uint32_t>(source.owner->size)) != LV_RESULT_OK) {
    return nullptr;
  }
  snapshot_app_panorama_background_view.data = const_cast<uint8_t *>(source.data);
  snapshot_app_panorama_background_view.unaligned_data = const_cast<uint8_t *>(source.data);
  snapshot_app_panorama_background_view.header.w = static_cast<uint32_t>(width);
  snapshot_app_panorama_background_view.header.h = static_cast<uint32_t>(height);
  snapshot_app_panorama_background_view.data_size =
      static_cast<uint32_t>((static_cast<size_t>(height - 1) * source.stride) + static_cast<size_t>(width) * 3U);
  return &snapshot_app_panorama_background_view;
#else
  (void) obj;
  return nullptr;
#endif
}

static inline void snapshot_panorama_copy_source_row(const SnapshotPanoramaPageSource &source, int y, uint8_t *dst,
                                                     int width) {
  const uint8_t *src_row = source.data + (size_t) (y * source.source_scale) * source.stride;
  snapshot_swipe_copy_rgb888_scaled_row(src_row, dst, width, source.source_scale);
}

bool snapshot_panorama_copy_source_ppa(const SnapshotPanoramaPageSource &source, uint8_t *dst, size_t dst_size,
                                       int dst_pic_w, int dst_pic_h, int dst_offset_x, int width, int height) {
#if defined(USE_ESP32) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  if (s_display_srm_client == nullptr || source.ppa_data == nullptr || dst == nullptr)
    return false;
  if (source.source_scale != 1 || source.ppa_pic_w <= 0 || width <= 0 || height <= 0)
    return false;
  // A full 800-row SRM operation can monopolize PSRAM long enough for the
  // continuously scanned DSI framebuffer FIFO to run dry. Keep the same PPA
  // path and output quality, but yield the memory bus between short bands.
  constexpr int PPA_BAND_ROWS = 32;
  for (int y = 0; y < height; y += PPA_BAND_ROWS) {
    const int band_height = std::min(PPA_BAND_ROWS, height - y);
    lvgl_esphome_wait_snapshot_dsi_fifo();

    ppa_srm_oper_config_t cfg = {};
    cfg.in.buffer = const_cast<uint8_t *>(source.ppa_data);
    cfg.in.pic_w = source.ppa_pic_w;
    cfg.in.pic_h = source.height;
    cfg.in.block_w = width;
    cfg.in.block_h = band_height;
    cfg.in.block_offset_x = source.ppa_offset_x;
    cfg.in.block_offset_y = y;
    cfg.in.srm_cm = PPA_SRM_COLOR_MODE_RGB888;
    cfg.out.buffer = dst;
    cfg.out.buffer_size = dst_size;
    cfg.out.pic_w = dst_pic_w;
    cfg.out.pic_h = dst_pic_h;
    cfg.out.block_offset_x = dst_offset_x;
    cfg.out.block_offset_y = y;
    cfg.out.srm_cm = PPA_SRM_COLOR_MODE_RGB888;
    cfg.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
    cfg.scale_x = 1.0f;
    cfg.scale_y = 1.0f;
    cfg.alpha_update_mode = PPA_ALPHA_NO_CHANGE;
    cfg.mode = PPA_TRANS_MODE_BLOCKING;
    if (ppa_do_scale_rotate_mirror(s_display_srm_client, &cfg) != ESP_OK)
      return false;
    taskYIELD();
  }
  return true;
#else
  return false;
#endif
}

void snapshot_panorama_cache_sync_banded(uint8_t *buffer, size_t row_bytes, int height, int flags) {
#ifdef USE_ESP32
  if (buffer == nullptr || row_bytes == 0 || height <= 0)
    return;
  constexpr int CACHE_SYNC_BAND_ROWS = 16;
  for (int y = 0; y < height; y += CACHE_SYNC_BAND_ROWS) {
    const int band_height = std::min(CACHE_SYNC_BAND_ROWS, height - y);
    lvgl_esphome_wait_snapshot_dsi_fifo();
    lvgl_cache_msync_external(buffer + static_cast<size_t>(y) * row_bytes,
                              static_cast<size_t>(band_height) * row_bytes, flags);
    taskYIELD();
  }
#else
  (void) buffer;
  (void) row_bytes;
  (void) height;
  (void) flags;
#endif
}

SnapshotPanoramaCacheEntry *snapshot_panorama_cache_prepare_from_sources(lv_obj_t *left_obj, lv_obj_t *right_obj,
                                                                         const SnapshotPanoramaPageSource &left,
                                                                         const SnapshotPanoramaPageSource &right,
                                                                         int width) {
#if defined(USE_ESP32) && LV_COLOR_DEPTH == 32
  constexpr int scale = SNAPSHOT_PANORAMA_SCALE;
  if (left_obj == nullptr || right_obj == nullptr || width <= 0)
    return nullptr;
  if (left.data == nullptr || right.data == nullptr)
    return nullptr;
  if ((width % scale) != 0 || left.source_scale <= 0 || right.source_scale <= 0)
    return nullptr;

  constexpr size_t CACHE_ALIGN = 128;
  constexpr size_t BYTES_PER_PIXEL = 3;
  const int scaled_width = width / scale;
  const int scaled_height = left.height / left.source_scale;
  const int right_scaled_height = right.height / right.source_scale;
  if (scaled_height <= 0 || right_scaled_height < scaled_height)
    return nullptr;
  if (left.width < scaled_width * left.source_scale || right.width < scaled_width * right.source_scale)
    return nullptr;
  if (left.height < scaled_height * left.source_scale || right.height < scaled_height * right.source_scale)
    return nullptr;
  const int panorama_width = scaled_width * 2;
  const size_t panorama_stride = (size_t) panorama_width * BYTES_PER_PIXEL;
  const size_t panorama_size = panorama_stride * scaled_height;
  const size_t aligned_size = (panorama_size + CACHE_ALIGN - 1) & ~(CACHE_ALIGN - 1);

  auto *slot = snapshot_panorama_cache_alloc_slot(left.owner, right.owner);
  if (slot == nullptr)
    return nullptr;
  snapshot_panorama_free_entry(*slot);

  auto *panorama =
      static_cast<uint8_t *>(heap_caps_aligned_alloc(CACHE_ALIGN, aligned_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (panorama == nullptr) {
    ESP_LOGW(TAG, "snapshot panorama: allocation failed (%u bytes)", (unsigned) aligned_size);
    snapshot_log_heap_("panorama alloc failed", left_obj, true);
    return nullptr;
  }

  const uint64_t t0 = snapshot_diag_now_us_();
  bool ppa_copied = false;
#if defined(USE_ESP32) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  if (left.source_scale == 1 && right.source_scale == 1) {
    const bool left_ok = snapshot_panorama_copy_source_ppa(left, panorama, aligned_size, panorama_width, scaled_height,
                                                           0, scaled_width, scaled_height);
    const bool right_ok =
        left_ok && snapshot_panorama_copy_source_ppa(right, panorama, aligned_size, panorama_width, scaled_height,
                                                     scaled_width, scaled_width, scaled_height);
    ppa_copied = left_ok && right_ok;
  }
#endif
  if (ppa_copied) {
    snapshot_panorama_cache_sync_banded(panorama, panorama_stride, scaled_height,
                                        ESP_CACHE_MSYNC_FLAG_DIR_M2C);
  } else {
    for (int y = 0; y < scaled_height; y++) {
      uint8_t *dst_row = panorama + (size_t) y * panorama_stride;
      snapshot_panorama_copy_source_row(left, y, dst_row, scaled_width);
      snapshot_panorama_copy_source_row(right, y, dst_row + (size_t) scaled_width * BYTES_PER_PIXEL, scaled_width);
    }
    snapshot_panorama_cache_sync_banded(panorama, panorama_stride, scaled_height,
                                        ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  }

  slot->left = left_obj;
  slot->right = right_obj;
  slot->pages[0] = left_obj;
  slot->pages[1] = right_obj;
  slot->page_count = 2;
  slot->buf = panorama;
  slot->size = aligned_size;
  slot->width = width;
  slot->height = scaled_height;
  slot->scale = scale;
  if (s_swipe_logging_enabled) {
    ESP_LOGI(TAG, "snapshot panorama: cached RGB888 %dx%d scale=%dx (%u KB) in %lluus via %s", panorama_width,
             scaled_height, scale, (unsigned) (aligned_size / 1024), (unsigned long long) (esp_timer_get_time() - t0),
             ppa_copied ? "ppa" : "cpu");
  }
  return slot;
#else
  return nullptr;
#endif
}

SnapshotPanoramaCacheEntry *snapshot_panorama_cache_prepare_from_buffers(lv_obj_t *left_obj, lv_obj_t *right_obj,
                                                                         lv_draw_buf_t *left, lv_draw_buf_t *right,
                                                                         int width) {
#if defined(USE_ESP32) && LV_COLOR_DEPTH == 32
  constexpr int scale = SNAPSHOT_PANORAMA_SCALE;
  SnapshotPanoramaPageSource left_source;
  SnapshotPanoramaPageSource right_source;
  if (!snapshot_panorama_source_from_buffer(left, scale, &left_source) ||
      !snapshot_panorama_source_from_buffer(right, scale, &right_source)) {
    return nullptr;
  }
  return snapshot_panorama_cache_prepare_from_sources(left_obj, right_obj, left_source, right_source, width);
#else
  return nullptr;
#endif
}

SnapshotPanoramaCacheEntry *snapshot_panorama_cache_prepare(lv_obj_t *left_obj, lv_obj_t *right_obj, int width) {
#if defined(USE_ESP32) && LV_COLOR_DEPTH == 32
  if (left_obj == nullptr || right_obj == nullptr || width <= 0)
    return nullptr;
  constexpr int scale = SNAPSHOT_PANORAMA_SCALE;
  if (auto *cached = snapshot_panorama_cache_find(left_obj, right_obj, width, scale))
    return cached;

  auto *left = snapshot_cache_find(left_obj);
  auto *right = snapshot_cache_find(right_obj);
  return snapshot_panorama_cache_prepare_from_buffers(left_obj, right_obj, left, right, width);
#else
  return nullptr;
#endif
}

SnapshotPanoramaCacheEntry *snapshot_panorama_cache_prepare_pages(lv_obj_t **pages, int page_count, int width) {
#if defined(USE_ESP32) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  if (pages == nullptr || page_count < 2 || page_count > 4 || width <= 0 || s_display_srm_client == nullptr)
    return nullptr;

  auto *display = lv_obj_get_display(pages[0]);
  constexpr size_t CACHE_ALIGN = 128;
  constexpr size_t BYTES_PER_PIXEL = 3;
  const int height = display == nullptr ? 0 : lv_display_get_vertical_resolution(display);
  if (height <= 0)
    return nullptr;
  const int panorama_width = width * page_count;
  const size_t panorama_size = static_cast<size_t>(panorama_width) * height * BYTES_PER_PIXEL;
  const size_t aligned_size = (panorama_size + CACHE_ALIGN - 1) & ~(CACHE_ALIGN - 1);

  for (auto &entry : snapshot_panorama_cache) {
    if (entry.buf != nullptr)
      snapshot_panorama_free_entry(entry);
  }
  auto &slot = snapshot_panorama_cache[0];
  auto *panorama = static_cast<uint8_t *>(
      heap_caps_aligned_alloc(CACHE_ALIGN, aligned_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (panorama == nullptr) {
    ESP_LOGW(TAG, "snapshot panorama: full allocation failed (%u bytes)", static_cast<unsigned>(aligned_size));
    snapshot_log_heap_("full panorama alloc failed", pages[0], true);
    return nullptr;
  }

  const uint64_t started_us = snapshot_diag_now_us_();
  bool copied = true;
  for (int page = 0; page < page_count; page++) {
    lv_draw_buf_t *source_buf = snapshot_cache_find(pages[page]);
    const bool owns_source = source_buf == nullptr;
    if (source_buf == nullptr)
      source_buf = snapshot_take_centered(pages[page]);
    SnapshotPanoramaPageSource source;
    if (source_buf == nullptr || !snapshot_panorama_source_from_buffer(source_buf, 1, &source) ||
        !snapshot_panorama_copy_source_ppa(source, panorama, aligned_size, panorama_width, height, page * width,
                                           width, height)) {
      copied = false;
    }
    if (owns_source && source_buf != nullptr)
      lv_draw_buf_destroy(source_buf);
    if (!copied)
      break;
  }
  if (!copied) {
    heap_caps_free(panorama);
    ESP_LOGW(TAG, "snapshot panorama: failed to build full %dx%d PPA surface", panorama_width, height);
    return nullptr;
  }

  snapshot_panorama_cache_sync_banded(panorama, static_cast<size_t>(panorama_width) * BYTES_PER_PIXEL, height,
                                      ESP_CACHE_MSYNC_FLAG_DIR_M2C);
  slot.left = pages[0];
  slot.right = pages[1];
  slot.page_count = page_count;
  for (int i = 0; i < page_count; i++)
    slot.pages[i] = pages[i];
  slot.buf = panorama;
  slot.size = aligned_size;
  slot.width = width;
  slot.height = height;
  slot.scale = 1;

  for (auto &entry : snapshot_cache) {
    for (int i = 0; i < page_count; i++) {
      if (entry.obj == pages[i]) {
        snapshot_cache_free_entry(entry);
        break;
      }
    }
  }
  ESP_LOGI(TAG, "snapshot panorama: cached persistent RGB888 %dx%d (%u KB) in %lluus", panorama_width, height,
           static_cast<unsigned>(aligned_size / 1024),
           static_cast<unsigned long long>(snapshot_diag_now_us_() - started_us));
  return &slot;
#else
  (void) pages;
  (void) page_count;
  (void) width;
  return nullptr;
#endif
}

int snapshot_swipe_ease_out(int start, int end, uint32_t elapsed_ms, uint32_t duration_ms);

bool snapshot_swipe_render_direct_frame(int current_x, int next_x) {
  auto &state = snapshot_swipe_state;
  if (state.component == nullptr)
    return false;
  const uint64_t start_us = esp_timer_get_time();
  bool rendered = false;
  if (state.edge_bounce) {
    rendered = state.component->snapshot_swipe_direct_render_edge(state.current_buf, current_x, state.width);
  } else if (state.panorama_render && state.panorama_buf != nullptr &&
             state.component->snapshot_swipe_direct_render_panorama(state.panorama_buf, current_x, state.width,
                                                                    state.panorama_scale, state.panorama_next_x,
                                                                    state.panorama_page_count,
                                                                    state.panorama_source_page_index)) {
    rendered = true;
  } else {
    rendered =
        state.component->snapshot_swipe_direct_render(state.current_buf, state.next_buf, current_x, next_x, state.width);
  }

  const uint64_t end_us = esp_timer_get_time();
  const uint32_t render_us = (uint32_t) (end_us - start_us);
  if (rendered) {
    if (state.perf_first_render_us == 0)
      state.perf_first_render_us = start_us;
    state.perf_last_render_us = end_us;
    state.perf_total_render_us += render_us;
    state.perf_render_frames++;
    state.perf_max_render_us = std::max(state.perf_max_render_us, render_us);
  } else {
    state.perf_failed_frames++;
  }
  return rendered;
}

bool snapshot_scroll_render_direct_frame(int scroll_y) {
  auto &state = snapshot_scroll_state;
  if (!state.direct_render || state.component == nullptr || state.content_buf == nullptr)
    return false;

  const uint64_t started_us = esp_timer_get_time();
  const bool rendered = state.component->snapshot_scroll_direct_render(
      state.content_buf, state.content_tail_buf, state.content_tail_y, scroll_y, state.viewport_w, state.viewport_h);
  const uint64_t finished_us = esp_timer_get_time();
  const uint32_t render_us = static_cast<uint32_t>(finished_us - started_us);
  if (state.perf_first_render_us == 0)
    state.perf_first_render_us = started_us;
  state.perf_last_render_us = finished_us;
  state.perf_total_render_us += render_us;
  state.perf_render_frames++;
  state.perf_max_render_us = std::max(state.perf_max_render_us, render_us);
  if (rendered) {
    state.current_scroll_y = scroll_y;
  } else {
    state.perf_failed_frames++;
  }
  return rendered;
}

#ifdef USE_ESP32
void snapshot_swipe_worker_task(void *) {
  auto &worker = snapshot_swipe_worker;
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (!worker.active.load(std::memory_order_acquire))
      continue;

    worker.running.store(true, std::memory_order_release);
    uint32_t handled_generation = worker.processed_generation.load(std::memory_order_acquire);
    while (worker.active.load(std::memory_order_acquire) && !worker.cancel.load(std::memory_order_acquire)) {
      if (worker.finish_requested.exchange(false, std::memory_order_acq_rel)) {
        const int start_current_x = worker.rendered_current_x.load(std::memory_order_acquire);
        const int start_next_x = worker.rendered_next_x.load(std::memory_order_acquire);
        const int target_current_x = worker.finish_current_x.load(std::memory_order_acquire);
        const int target_next_x = worker.finish_next_x.load(std::memory_order_acquire);
        const uint32_t duration_ms = std::max<uint32_t>(1, worker.finish_duration_ms.load(std::memory_order_acquire));
        const uint64_t animation_start_us = esp_timer_get_time();
        uint64_t next_frame_us = animation_start_us;
        bool success = true;

        while (!worker.cancel.load(std::memory_order_acquire)) {
          const uint64_t now_us = esp_timer_get_time();
          const uint32_t elapsed_ms = static_cast<uint32_t>((now_us - animation_start_us) / 1000ULL);
          const bool final_frame = elapsed_ms >= duration_ms;
          const int current_x = final_frame
                                    ? target_current_x
                                    : snapshot_swipe_ease_out(start_current_x, target_current_x, elapsed_ms, duration_ms);
          const int next_x = final_frame
                                 ? target_next_x
                                 : snapshot_swipe_ease_out(start_next_x, target_next_x, elapsed_ms, duration_ms);
          if (current_x != worker.rendered_current_x.load(std::memory_order_relaxed) ||
              next_x != worker.rendered_next_x.load(std::memory_order_relaxed) || final_frame) {
            success = snapshot_swipe_render_direct_frame(current_x, next_x);
            if (!success)
              break;
            worker.rendered_current_x.store(current_x, std::memory_order_release);
            worker.rendered_next_x.store(next_x, std::memory_order_release);
          }
          if (final_frame)
            break;

          next_frame_us += 16666ULL;
          const int64_t wait_us = static_cast<int64_t>(next_frame_us - esp_timer_get_time());
          if (wait_us > 1000)
            vTaskDelay(pdMS_TO_TICKS(static_cast<uint32_t>(wait_us / 1000)));
          else
            taskYIELD();
        }

        worker.failed.store(!success, std::memory_order_release);
        worker.active.store(false, std::memory_order_release);
        worker.finish_done.store(true, std::memory_order_release);
        break;
      }

      const uint32_t generation = worker.request_generation.load(std::memory_order_acquire);
      if (generation == handled_generation)
        break;
      const int current_x = worker.requested_current_x.load(std::memory_order_acquire);
      const int next_x = worker.requested_next_x.load(std::memory_order_acquire);
      const bool success = snapshot_swipe_render_direct_frame(current_x, next_x);
      if (success) {
        worker.rendered_current_x.store(current_x, std::memory_order_release);
        worker.rendered_next_x.store(next_x, std::memory_order_release);
      } else {
        worker.failed.store(true, std::memory_order_release);
      }
      handled_generation = generation;
      worker.processed_generation.store(generation, std::memory_order_release);
    }
    worker.running.store(false, std::memory_order_release);
  }
}

bool snapshot_swipe_worker_activate(int current_x, int next_x) {
  auto &worker = snapshot_swipe_worker;
  if (worker.task == nullptr) {
#if CONFIG_FREERTOS_UNICORE
    constexpr BaseType_t worker_core = tskNO_AFFINITY;
#else
    const BaseType_t worker_core = xPortGetCoreID() == 0 ? 1 : 0;
#endif
    worker.task = xTaskCreateStaticPinnedToCore(
        snapshot_swipe_worker_task, "lvgl_swipe", SNAPSHOT_SWIPE_WORKER_STACK_BYTES, nullptr, 1,
        worker.task_stack, &worker.task_storage, worker_core);
    if (worker.task == nullptr) {
      ESP_LOGW(TAG, "snapshot swipe: failed to create direct compositor worker");
      return false;
    }
    ESP_LOGI(TAG, "Snapshot swipe compositor worker enabled on core %d", static_cast<int>(worker_core));
  }

  worker.cancel.store(false, std::memory_order_release);
  worker.running.store(false, std::memory_order_release);
  worker.finish_requested.store(false, std::memory_order_release);
  worker.finish_done.store(false, std::memory_order_release);
  worker.failed.store(false, std::memory_order_release);
  worker.request_generation.store(0, std::memory_order_release);
  worker.processed_generation.store(0, std::memory_order_release);
  worker.requested_current_x.store(current_x, std::memory_order_release);
  worker.requested_next_x.store(next_x, std::memory_order_release);
  worker.rendered_current_x.store(current_x, std::memory_order_release);
  worker.rendered_next_x.store(next_x, std::memory_order_release);
  worker.active.store(true, std::memory_order_release);
  return true;
}

void snapshot_swipe_worker_queue_update(int current_x, int next_x) {
  auto &worker = snapshot_swipe_worker;
  if (!worker.active.load(std::memory_order_acquire) || worker.finish_requested.load(std::memory_order_acquire))
    return;
  worker.requested_current_x.store(current_x, std::memory_order_relaxed);
  worker.requested_next_x.store(next_x, std::memory_order_relaxed);
  worker.request_generation.fetch_add(1, std::memory_order_release);
  xTaskNotifyGive(worker.task);
}

void snapshot_swipe_worker_queue_finish(int current_x, int next_x, uint32_t duration_ms) {
  auto &worker = snapshot_swipe_worker;
  if (!worker.active.load(std::memory_order_acquire))
    return;
  worker.finish_current_x.store(current_x, std::memory_order_relaxed);
  worker.finish_next_x.store(next_x, std::memory_order_relaxed);
  worker.finish_duration_ms.store(duration_ms, std::memory_order_relaxed);
  worker.finish_requested.store(true, std::memory_order_release);
  xTaskNotifyGive(worker.task);
}

bool snapshot_swipe_worker_stop_and_wait(uint32_t timeout_ms = 1200) {
  auto &worker = snapshot_swipe_worker;
  if (worker.task == nullptr)
    return true;
  worker.cancel.store(true, std::memory_order_release);
  worker.active.store(false, std::memory_order_release);
  xTaskNotifyGive(worker.task);
  const uint32_t started_ms = millis();
  while (worker.running.load(std::memory_order_acquire)) {
    if (millis() - started_ms >= timeout_ms) {
      ESP_LOGE(TAG, "snapshot swipe: direct compositor worker did not stop within %ums",
               static_cast<unsigned>(timeout_ms));
      return false;
    }
    vTaskDelay(1);
  }
  worker.finish_requested.store(false, std::memory_order_release);
  worker.finish_done.store(false, std::memory_order_release);
  return true;
}

void snapshot_scroll_worker_task(void *) {
  auto &worker = snapshot_scroll_worker;
  auto interpolate = [](int start, int end, uint32_t elapsed_ms, uint32_t duration_ms) -> int {
    if (duration_ms == 0 || elapsed_ms >= duration_ms)
      return end;
    const uint32_t t = std::min<uint32_t>(1024U, (elapsed_ms * 1024U) / duration_ms);
    const uint32_t inv = 1024U - t;
    const uint32_t eased =
        1024U - static_cast<uint32_t>((static_cast<uint64_t>(inv) * inv * inv) >> 20U);
    return start + static_cast<int>((static_cast<int64_t>(end - start) * eased) / 1024);
  };
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (!worker.active.load(std::memory_order_acquire))
      continue;

    worker.running.store(true, std::memory_order_release);
    if (worker.inertia_requested.load(std::memory_order_acquire)) {
      auto render_phase = [&](int start_y, int end_y, uint32_t duration_ms) -> bool {
        const uint64_t start_us = esp_timer_get_time();
        uint64_t next_frame_us = start_us;
        while (worker.active.load(std::memory_order_acquire) &&
               !worker.cancel.load(std::memory_order_acquire)) {
          const uint64_t now_us = esp_timer_get_time();
          const uint32_t elapsed_ms = static_cast<uint32_t>((now_us - start_us) / 1000ULL);
          const int scroll_y = interpolate(start_y, end_y, elapsed_ms, duration_ms);
          if (!snapshot_scroll_render_direct_frame(scroll_y))
            return false;
          worker.rendered_scroll_y.store(scroll_y, std::memory_order_release);
          if (elapsed_ms >= duration_ms)
            return true;

          next_frame_us += 16667ULL;
          const int64_t wait_us = static_cast<int64_t>(next_frame_us - esp_timer_get_time());
          if (wait_us > 1000)
            vTaskDelay(pdMS_TO_TICKS(static_cast<uint32_t>(wait_us / 1000)));
          else
            taskYIELD();
        }
        return false;
      };

      const int start_y = worker.inertia_start_y.load(std::memory_order_relaxed);
      const int target_y = worker.inertia_target_y.load(std::memory_order_relaxed);
      const int final_y = worker.inertia_final_y.load(std::memory_order_relaxed);
      bool success = render_phase(start_y, target_y, worker.inertia_duration_ms.load(std::memory_order_relaxed));
      if (success && target_y != final_y) {
        success = render_phase(target_y, final_y,
                               worker.inertia_bounce_duration_ms.load(std::memory_order_relaxed));
      }
      if (!success && worker.active.load(std::memory_order_acquire) &&
          !worker.cancel.load(std::memory_order_acquire)) {
        worker.failed.store(true, std::memory_order_release);
      }
      worker.inertia_requested.store(false, std::memory_order_release);
      worker.inertia_done.store(success, std::memory_order_release);
    }

    uint32_t handled_generation = worker.processed_generation.load(std::memory_order_acquire);
    while (worker.active.load(std::memory_order_acquire) && !worker.cancel.load(std::memory_order_acquire)) {
      const uint32_t generation = worker.request_generation.load(std::memory_order_acquire);
      if (generation == handled_generation)
        break;

      const int scroll_y = worker.requested_scroll_y.load(std::memory_order_acquire);
      const bool success = snapshot_scroll_render_direct_frame(scroll_y);
      if (success) {
        worker.rendered_scroll_y.store(scroll_y, std::memory_order_release);
      } else {
        worker.failed.store(true, std::memory_order_release);
      }
      handled_generation = generation;
      worker.processed_generation.store(generation, std::memory_order_release);
      if (!success)
        break;
    }
    worker.running.store(false, std::memory_order_release);
  }
}

bool snapshot_scroll_worker_activate(int scroll_y) {
  auto &worker = snapshot_scroll_worker;
  if (worker.task == nullptr) {
#if CONFIG_FREERTOS_UNICORE
    constexpr BaseType_t worker_core = tskNO_AFFINITY;
#else
    const BaseType_t worker_core = xPortGetCoreID() == 0 ? 1 : 0;
#endif
    worker.task = xTaskCreateStaticPinnedToCore(
        snapshot_scroll_worker_task, "lvgl_scroll", SNAPSHOT_SCROLL_WORKER_STACK_BYTES, nullptr, 1,
        worker.task_stack, &worker.task_storage, worker_core);
    if (worker.task == nullptr) {
      ESP_LOGW(TAG, "snapshot scroll: failed to create direct compositor worker");
      return false;
    }
    ESP_LOGI(TAG, "Snapshot scroll compositor worker enabled on core %d", static_cast<int>(worker_core));
  }

  worker.cancel.store(false, std::memory_order_release);
  worker.running.store(false, std::memory_order_release);
  worker.failed.store(false, std::memory_order_release);
  worker.request_generation.store(0, std::memory_order_release);
  worker.processed_generation.store(0, std::memory_order_release);
  worker.requested_scroll_y.store(scroll_y, std::memory_order_release);
  worker.rendered_scroll_y.store(scroll_y, std::memory_order_release);
  worker.inertia_requested.store(false, std::memory_order_release);
  worker.inertia_done.store(false, std::memory_order_release);
  worker.active.store(true, std::memory_order_release);
  return true;
}

bool snapshot_scroll_worker_start_inertia(int start_y, int target_y, int final_y, uint32_t duration_ms,
                                          uint32_t bounce_duration_ms) {
  auto &worker = snapshot_scroll_worker;
  if (!worker.active.load(std::memory_order_acquire) || worker.failed.load(std::memory_order_acquire))
    return false;
  worker.inertia_start_y.store(start_y, std::memory_order_relaxed);
  worker.inertia_target_y.store(target_y, std::memory_order_relaxed);
  worker.inertia_final_y.store(final_y, std::memory_order_relaxed);
  worker.inertia_duration_ms.store(duration_ms, std::memory_order_relaxed);
  worker.inertia_bounce_duration_ms.store(bounce_duration_ms, std::memory_order_relaxed);
  worker.inertia_done.store(false, std::memory_order_release);
  worker.inertia_requested.store(true, std::memory_order_release);
  xTaskNotifyGive(worker.task);
  return true;
}

uint32_t snapshot_scroll_worker_queue_update(int scroll_y) {
  auto &worker = snapshot_scroll_worker;
  if (!worker.active.load(std::memory_order_acquire))
    return 0;
  worker.requested_scroll_y.store(scroll_y, std::memory_order_relaxed);
  const uint32_t generation = worker.request_generation.fetch_add(1, std::memory_order_acq_rel) + 1;
  xTaskNotifyGive(worker.task);
  return generation;
}

bool snapshot_scroll_worker_wait_for(uint32_t generation, uint32_t timeout_ms) {
  if (generation == 0)
    return false;
  auto &worker = snapshot_scroll_worker;
  const uint32_t started_ms = millis();
  while (worker.processed_generation.load(std::memory_order_acquire) < generation &&
         !worker.failed.load(std::memory_order_acquire)) {
    if (millis() - started_ms >= timeout_ms)
      return false;
    vTaskDelay(1);
  }
  return !worker.failed.load(std::memory_order_acquire) &&
         worker.processed_generation.load(std::memory_order_acquire) >= generation;
}

bool snapshot_scroll_worker_stop_and_wait(uint32_t timeout_ms = 1200) {
  auto &worker = snapshot_scroll_worker;
  if (worker.task == nullptr)
    return true;
  worker.cancel.store(true, std::memory_order_release);
  worker.active.store(false, std::memory_order_release);
  xTaskNotifyGive(worker.task);
  const uint32_t started_ms = millis();
  while (worker.running.load(std::memory_order_acquire)) {
    if (millis() - started_ms >= timeout_ms) {
      ESP_LOGE(TAG, "snapshot scroll: direct compositor worker did not stop within %ums",
               static_cast<unsigned>(timeout_ms));
      return false;
    }
    vTaskDelay(1);
  }
  worker.inertia_requested.store(false, std::memory_order_release);
  worker.inertia_done.store(false, std::memory_order_release);
  return true;
}
#endif

void snapshot_swipe_cleanup() {
#ifdef USE_ESP32
  if (!snapshot_swipe_worker_stop_and_wait())
    return;
#endif
  if (s_swipe_logging_enabled && snapshot_swipe_state.perf_render_frames > 0) {
    const uint64_t elapsed_us = snapshot_swipe_state.perf_last_render_us - snapshot_swipe_state.perf_first_render_us;
    const uint32_t fps =
        elapsed_us == 0 ? 0 : (uint32_t) ((uint64_t) snapshot_swipe_state.perf_render_frames * 1000000ULL / elapsed_us);
    const char *path = snapshot_swipe_state.edge_bounce
                           ? "edge"
                           : (snapshot_swipe_state.panorama_render ? "panorama" : "strips");
    ESP_LOGW(TAG, "snapshot swipe summary: path=%s frames=%u fps=%u avg=%lluus max=%uus failed=%u commit=%d",
             path, (unsigned) snapshot_swipe_state.perf_render_frames, (unsigned) fps,
             (unsigned long long) (snapshot_swipe_state.perf_total_render_us /
                                   snapshot_swipe_state.perf_render_frames),
             (unsigned) snapshot_swipe_state.perf_max_render_us, (unsigned) snapshot_swipe_state.perf_failed_frames,
             snapshot_swipe_state.commit);
  }
  s_snapshot_swipe_active = false;
  s_snapshot_direct_active = false;
  if (snapshot_swipe_state.direct_anim_timer != nullptr) {
    lv_timer_delete(snapshot_swipe_state.direct_anim_timer);
    snapshot_swipe_state.direct_anim_timer = nullptr;
  }
  if (snapshot_swipe_state.cleanup_timer != nullptr) {
    lv_timer_delete(snapshot_swipe_state.cleanup_timer);
    snapshot_swipe_state.cleanup_timer = nullptr;
  }
  if (snapshot_swipe_state.current_img != nullptr) {
    lv_obj_delete(snapshot_swipe_state.current_img);
    snapshot_swipe_state.current_img = nullptr;
  }
  if (snapshot_swipe_state.next_img != nullptr) {
    lv_obj_delete(snapshot_swipe_state.next_img);
    snapshot_swipe_state.next_img = nullptr;
  }
  if (snapshot_swipe_state.layer != nullptr) {
    lv_obj_delete(snapshot_swipe_state.layer);
    snapshot_swipe_state.layer = nullptr;
  }
  if (snapshot_swipe_state.current_buf != nullptr) {
    if (snapshot_swipe_state.owns_current_buf)
      lv_draw_buf_destroy(snapshot_swipe_state.current_buf);
    snapshot_swipe_state.current_buf = nullptr;
  }
  if (snapshot_swipe_state.next_buf != nullptr) {
    if (snapshot_swipe_state.owns_next_buf)
      lv_draw_buf_destroy(snapshot_swipe_state.next_buf);
    snapshot_swipe_state.next_buf = nullptr;
  }
  snapshot_swipe_clear_panorama();
  snapshot_swipe_state.current_root = nullptr;
  snapshot_swipe_state.next_root = nullptr;
  snapshot_swipe_state.owns_current_buf = false;
  snapshot_swipe_state.owns_next_buf = false;
  snapshot_swipe_state.anim_start_current_x = 0;
  snapshot_swipe_state.anim_start_next_x = 0;
  snapshot_swipe_state.anim_start_us = 0;
  snapshot_swipe_state.anim_duration_ms = 0;
  snapshot_swipe_state.direct_anim_active = false;
  snapshot_swipe_state.finish_current_x = 0;
  snapshot_swipe_state.finish_next_x = 0;
  snapshot_swipe_state.current_x = 0;
  snapshot_swipe_state.next_x = 0;
  snapshot_swipe_state.width = 0;
  snapshot_swipe_state.commit = false;
  snapshot_swipe_state.pending_update = false;
  snapshot_swipe_state.pending_current_x = 0;
  snapshot_swipe_state.pending_next_x = 0;
  snapshot_swipe_state.pending_finish = false;
  snapshot_swipe_state.pending_finish_current_x = 0;
  snapshot_swipe_state.pending_finish_next_x = 0;
  snapshot_swipe_state.pending_finish_duration_ms = 0;
  snapshot_swipe_state.pending_finish_commit = false;
  snapshot_swipe_state.direct_render = false;
  snapshot_swipe_state.edge_bounce = false;
  snapshot_swipe_state.worker_enabled = false;
  snapshot_swipe_state.perf_first_render_us = 0;
  snapshot_swipe_state.perf_last_render_us = 0;
  snapshot_swipe_state.perf_total_render_us = 0;
  snapshot_swipe_state.perf_render_frames = 0;
  snapshot_swipe_state.perf_max_render_us = 0;
  snapshot_swipe_state.perf_failed_frames = 0;
  snapshot_swipe_state.component = nullptr;
}

void snapshot_swipe_complete_before_next_gesture() {
  auto &state = snapshot_swipe_state;
  if (!s_snapshot_swipe_active)
    return;

  const bool finishing = state.direct_anim_active || state.pending_finish;
#ifdef USE_ESP32
  const bool worker_finishing = state.worker_enabled &&
                                snapshot_swipe_worker.finish_requested.load(std::memory_order_acquire);
  if (state.worker_enabled) {
    if (snapshot_swipe_worker_stop_and_wait(120)) {
      state.current_x = snapshot_swipe_worker.rendered_current_x.load(std::memory_order_acquire);
      state.next_x = snapshot_swipe_worker.rendered_next_x.load(std::memory_order_acquire);
      state.worker_enabled = false;
    } else {
      snapshot_swipe_cleanup();
      return;
    }
  }
#else
  constexpr bool worker_finishing = false;
#endif

  if (state.direct_render && (finishing || worker_finishing)) {
    state.pending_finish = false;
    state.direct_anim_active = false;
    snapshot_swipe_finish_now();
    return;
  }
  snapshot_swipe_cleanup();
}

void snapshot_swipe_align(lv_obj_t *obj, int x) {
  if (obj != nullptr)
    lv_obj_align(obj, LV_ALIGN_CENTER, x, 0);
}

void snapshot_swipe_anim_x(void *obj, int32_t x) { snapshot_swipe_align(static_cast<lv_obj_t *>(obj), x); }

void snapshot_swipe_apply_final_roots() {
  if (snapshot_swipe_state.current_root == nullptr)
    return;
  if (snapshot_swipe_state.edge_bounce || snapshot_swipe_state.next_root == nullptr) {
    lv_obj_align(snapshot_swipe_state.current_root, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(snapshot_swipe_state.current_root, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  if (snapshot_swipe_state.commit) {
    lv_obj_align(snapshot_swipe_state.current_root, LV_ALIGN_CENTER, snapshot_swipe_state.finish_current_x, 0);
    lv_obj_align(snapshot_swipe_state.next_root, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(snapshot_swipe_state.current_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(snapshot_swipe_state.next_root, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_align(snapshot_swipe_state.current_root, LV_ALIGN_CENTER, 0, 0);
    lv_obj_align(snapshot_swipe_state.next_root, LV_ALIGN_CENTER, snapshot_swipe_state.finish_next_x, 0);
    lv_obj_clear_flag(snapshot_swipe_state.current_root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(snapshot_swipe_state.next_root, LV_OBJ_FLAG_HIDDEN);
  }
}

int snapshot_swipe_ease_out(int start, int end, uint32_t elapsed_ms, uint32_t duration_ms) {
  if (duration_ms == 0 || elapsed_ms >= duration_ms)
    return end;
  uint32_t t = (elapsed_ms * 1024U) / duration_ms;
  if (t > 1024U)
    t = 1024U;
  uint32_t inv = 1024U - t;
  uint32_t eased = 1024U - (uint32_t) (((uint64_t) inv * inv * inv) / (1024ULL * 1024ULL));
  return start + (int) (((int64_t) (end - start) * eased) / 1024);
}

int snapshot_app_ease_out(int start, int end, uint32_t elapsed_ms, uint32_t duration_ms) {
  if (duration_ms == 0 || elapsed_ms >= duration_ms)
    return end;
  uint32_t t = std::min<uint32_t>(1024U, (elapsed_ms * 1024U) / duration_ms);
  const uint64_t inv = 1024U - t;
  // Quintic ease-out gives application cards an immediate launch followed by
  // a long, visible settle. The carousel keeps its gentler cubic curve.
  const uint64_t inv5 = inv * inv * inv * inv * inv;
  const uint32_t eased = 1024U - static_cast<uint32_t>(inv5 / (1024ULL * 1024ULL * 1024ULL * 1024ULL));
  return start + static_cast<int>((static_cast<int64_t>(end - start) * eased) / 1024);
}

#ifdef USE_ESP32
void snapshot_app_worker_task(void *) {
  auto &worker = snapshot_app_worker;
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (!worker.pending.exchange(false, std::memory_order_acq_rel))
      continue;
    if (worker.cancel.load(std::memory_order_acquire)) {
      worker.done.store(true, std::memory_order_release);
      continue;
    }

    worker.running.store(true, std::memory_order_release);
    auto &state = snapshot_app_state;
    auto *component = state.component;
    auto *background = state.background_buf;
    auto *app = state.app_buf;
    const bool opening = state.opening;
    const uint32_t duration_ms = state.anim_duration_ms;
    const int start_size = opening ? std::max(state.start_size, SNAPSHOT_APP_OPEN_MIN_PRESENT_SIZE) : state.start_size;
    const int end_size = state.end_size;
    const int start_center_x = state.start_center_x;
    const int start_center_y = state.start_center_y;
    const int end_center_x = state.end_center_x;
    const int end_center_y = state.end_center_y;

    uint32_t rendered_frames = 0;
    uint64_t render_total_us = 0;
    uint32_t render_max_us = 0;
    uint64_t prepare_total_us = 0;
    uint64_t circle_total_us = 0;
    uint64_t cache_sync_total_us = 0;
    uint64_t ppa_total_us = 0;
    uint64_t present_total_us = 0;
    auto render_frame = [&](int center_x, int center_y, int size) {
      const int64_t started_us = esp_timer_get_time();
      const bool rendered = component->snapshot_app_direct_render(background, app, center_x, center_y, size, size);
      const uint32_t elapsed_us = static_cast<uint32_t>(esp_timer_get_time() - started_us);
      rendered_frames++;
      render_total_us += elapsed_us;
      render_max_us = std::max(render_max_us, elapsed_us);
      if (rendered) {
        prepare_total_us += s_snapshot_app_last_frame_metrics.prepare_us;
        circle_total_us += s_snapshot_app_last_frame_metrics.circle_us;
        cache_sync_total_us += s_snapshot_app_last_frame_metrics.cache_sync_us;
        ppa_total_us += s_snapshot_app_last_frame_metrics.ppa_us;
        present_total_us += s_snapshot_app_last_frame_metrics.present_us;
      }
      return rendered;
    };

    bool success = component != nullptr && background != nullptr && app != nullptr;
    if (success) {
      success = render_frame(start_center_x, start_center_y, start_size);
    }

    // Initial preparation downsamples both immutable snapshots and establishes
    // cache ownership of the DSI targets. Start the visible animation clock
    // only after that one-time work, otherwise the second frame jumps ahead by
    // the entire preparation latency.
    const uint64_t animation_start_us = esp_timer_get_time();
    uint64_t next_frame_us = animation_start_us;
    int last_size = start_size;
    int last_center_x = start_center_x;
    int last_center_y = start_center_y;
    while (success && !worker.cancel.load(std::memory_order_acquire)) {
      next_frame_us += 16000ULL;
      const int64_t wait_us = static_cast<int64_t>(next_frame_us - esp_timer_get_time());
      if (wait_us > 1000)
        vTaskDelay(pdMS_TO_TICKS(static_cast<uint32_t>(wait_us / 1000)));
      else
        taskYIELD();

      const uint64_t now_us = esp_timer_get_time();
      const uint32_t elapsed_ms = static_cast<uint32_t>((now_us - animation_start_us) / 1000ULL);
      const bool final_frame = elapsed_ms >= duration_ms;
      const auto interpolate = snapshot_app_ease_out;
      const int size = final_frame ? end_size : interpolate(start_size, end_size, elapsed_ms, duration_ms);
      const int center_x =
          final_frame ? end_center_x : interpolate(start_center_x, end_center_x, elapsed_ms, duration_ms);
      const int center_y =
          final_frame ? end_center_y : interpolate(start_center_y, end_center_y, elapsed_ms, duration_ms);
      if (size != last_size || center_x != last_center_x || center_y != last_center_y) {
        success = render_frame(center_x, center_y, size);
        last_size = size;
        last_center_x = center_x;
        last_center_y = center_y;
      }
      if (final_frame)
        break;

      // A full PPA frame can take longer than 16 ms. Keep the animation tied
      // to wall time, but yield before the next frame so networking and audio
      // tasks on this core are never starved.
      if (esp_timer_get_time() >= next_frame_us)
        vTaskDelay(1);
    }

    worker.failed.store(!success, std::memory_order_release);
    if (s_perf_logging_enabled && rendered_frames != 0) {
      const uint32_t stack_free = static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr));
      ESP_LOGI("lvgl.app",
               "worker animation frames=%u avg=%uus max=%uus prep=%uus circle=%uus sync=%uus ppa=%uus "
               "present=%uus duration=%ums stack_free=%u result=%s",
               static_cast<unsigned>(rendered_frames),
               static_cast<unsigned>(render_total_us / rendered_frames), static_cast<unsigned>(render_max_us),
               static_cast<unsigned>(prepare_total_us / rendered_frames),
               static_cast<unsigned>(circle_total_us / rendered_frames),
               static_cast<unsigned>(cache_sync_total_us / rendered_frames),
               static_cast<unsigned>(ppa_total_us / rendered_frames),
               static_cast<unsigned>(present_total_us / rendered_frames),
               static_cast<unsigned>(duration_ms), static_cast<unsigned>(stack_free), success ? "ok" : "failed");
    }
    worker.running.store(false, std::memory_order_release);
    worker.done.store(true, std::memory_order_release);
  }
}

bool snapshot_app_worker_start() {
  auto &worker = snapshot_app_worker;
  if (worker.task == nullptr) {
#if CONFIG_FREERTOS_UNICORE
    constexpr BaseType_t worker_core = tskNO_AFFINITY;
#else
    const BaseType_t worker_core = xPortGetCoreID() == 0 ? 1 : 0;
#endif
    worker.task = xTaskCreateStaticPinnedToCore(
        snapshot_app_worker_task, "lvgl_app_anim", SNAPSHOT_APP_WORKER_STACK_BYTES, nullptr, 1,
        worker.task_stack, &worker.task_storage, worker_core);
    if (worker.task == nullptr) {
      worker.task = nullptr;
      ESP_LOGW(TAG, "snapshot app: failed to create direct animation worker");
      return false;
    }
    ESP_LOGI(TAG, "Snapshot app compositor worker enabled on core %d", static_cast<int>(worker_core));
  }
  worker.cancel.store(false, std::memory_order_release);
  worker.failed.store(false, std::memory_order_release);
  worker.done.store(false, std::memory_order_release);
  worker.pending.store(true, std::memory_order_release);
  xTaskNotifyGive(worker.task);
  return true;
}

bool snapshot_app_worker_cancel_and_wait(uint32_t timeout_ms = 1200) {
  auto &worker = snapshot_app_worker;
  if (worker.task == nullptr)
    return true;
  worker.cancel.store(true, std::memory_order_release);
  xTaskNotifyGive(worker.task);
  const uint32_t started_ms = millis();
  while (worker.pending.load(std::memory_order_acquire) || worker.running.load(std::memory_order_acquire)) {
    if (millis() - started_ms >= timeout_ms) {
      ESP_LOGE(TAG, "snapshot app: direct animation worker did not stop within %ums",
               static_cast<unsigned>(timeout_ms));
      return false;
    }
    vTaskDelay(1);
  }
  return true;
}
#endif

bool snapshot_app_cleanup() {
#ifdef USE_ESP32
  if (!snapshot_app_worker_cancel_and_wait())
    return false;
#endif
  if (snapshot_app_state.owns_app_buf && snapshot_app_state.app_buf != nullptr) {
    lv_draw_buf_destroy(snapshot_app_state.app_buf);
  }
  if (snapshot_app_state.owns_background_buf && snapshot_app_state.background_buf != nullptr) {
    lv_draw_buf_destroy(snapshot_app_state.background_buf);
  }
  snapshot_app_state = {};
  return true;
}

void snapshot_scroll_cleanup();

void snapshot_app_clear_prepared_close() {
  if (snapshot_app_prepared_close_owns_buf && snapshot_app_prepared_close_buf != nullptr) {
    lv_draw_buf_destroy(snapshot_app_prepared_close_buf);
  }
  snapshot_app_prepared_close_buf = nullptr;
  snapshot_app_prepared_close_obj = nullptr;
  snapshot_app_prepared_close_owns_buf = false;
}

bool snapshot_app_reserve_work_buffer(lv_obj_t *obj) {
  if (snapshot_app_work_buf != nullptr)
    return true;
  if (obj == nullptr)
    return false;

  auto *disp = lv_obj_get_display(obj);
  const int width = disp == nullptr ? 0 : lv_display_get_horizontal_resolution(disp);
  const int height = disp == nullptr ? 0 : lv_display_get_vertical_resolution(disp);
  if (width <= 0 || height <= 0)
    return false;

  snapshot_app_work_buf = lv_draw_buf_create(width, height, SNAPSHOT_CF, width * 3);
  if (snapshot_app_work_buf == nullptr || snapshot_app_work_buf->data == nullptr) {
    if (snapshot_app_work_buf != nullptr)
      lv_draw_buf_destroy(snapshot_app_work_buf);
    snapshot_app_work_buf = nullptr;
    ESP_LOGW(TAG, "snapshot app: failed to reserve persistent %dx%d RGB888 work buffer", width, height);
    return false;
  }
  snapshot_app_work_obj = nullptr;
  ESP_LOGI(TAG, "Reserved persistent app snapshot work buffer: %u KB",
           static_cast<unsigned>(snapshot_app_work_buf->data_size / 1024));
  return true;
}

bool snapshot_take_centered_to(lv_obj_t *obj, lv_draw_buf_t *buf) {
  if (obj == nullptr || buf == nullptr)
    return false;
  const bool was_hidden = lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
  const lv_coord_t old_x = lv_obj_get_x(obj);
  const lv_coord_t old_y = lv_obj_get_y(obj);

  lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
  lv_obj_align(obj, LV_ALIGN_CENTER, 0, 0);
  auto *parent = lv_obj_get_parent(obj);
  lv_obj_update_layout(parent == nullptr ? obj : parent);
  const bool taken = lv_snapshot_take_to_draw_buf(obj, SNAPSHOT_CF, buf) == LV_RESULT_OK;
  lv_obj_set_pos(obj, old_x, old_y);
  lv_obj_update_layout(parent == nullptr ? obj : parent);
  if (was_hidden)
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  return taken;
}

lv_draw_buf_t *snapshot_app_take_fresh(lv_obj_t *obj) {
  if (obj == nullptr)
    return nullptr;
  const bool was_hidden = lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
  lv_obj_update_layout(lv_obj_get_parent(obj) == nullptr ? obj : lv_obj_get_parent(obj));
  auto *buf = lv_snapshot_take(obj, SNAPSHOT_CF);
  if (was_hidden)
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  return buf;
}

lv_draw_buf_t *snapshot_take_centered(lv_obj_t *obj) {
  if (obj == nullptr)
    return nullptr;
  const bool was_hidden = lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
  const lv_coord_t old_x = lv_obj_get_x(obj);
  const lv_coord_t old_y = lv_obj_get_y(obj);

  lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
  lv_obj_align(obj, LV_ALIGN_CENTER, 0, 0);
  lv_obj_update_layout(lv_obj_get_parent(obj) == nullptr ? obj : lv_obj_get_parent(obj));

  auto *buf = lv_snapshot_take(obj, SNAPSHOT_CF);
  lv_obj_set_pos(obj, old_x, old_y);
  lv_obj_update_layout(lv_obj_get_parent(obj) == nullptr ? obj : lv_obj_get_parent(obj));
  if (was_hidden)
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  return buf;
}

lv_draw_buf_t *snapshot_app_cached_or_take(lv_obj_t *obj, bool force_fresh, bool *owns, bool use_work_buffer) {
  if (owns != nullptr)
    *owns = false;
  if (obj == nullptr)
    return nullptr;

  // A persistent raw application cache is already a display-sized animation
  // source. Prefer it before reserving another 1.92 MB work surface. At JPEG
  // quality 100 the four compressed app frames plus this surface consumed
  // essentially the same PSRAM as four raw frames, while adding decode delay
  // and visible softness to every opening transition.
  if (!force_fresh) {
    auto *entry = snapshot_cache_find_entry(obj);
    if (entry != nullptr && entry->buf != nullptr)
      return entry->buf;
  }

  if (use_work_buffer && snapshot_app_reserve_work_buffer(obj)) {
    if (!force_fresh && snapshot_app_work_obj == obj)
      return snapshot_app_work_buf;
    if (!force_fresh) {
      auto *entry = snapshot_cache_find_entry(obj);
#ifdef USE_LVGL_SNAPSHOT_JPEG_CACHE
      if (entry != nullptr && !entry->jpeg.empty() &&
          snapshot_cache_decode_jpeg_to_draw_buf(*entry, snapshot_app_work_buf)) {
        snapshot_app_work_obj = obj;
        return snapshot_app_work_buf;
      }
#endif
    }
    if (snapshot_take_centered_to(obj, snapshot_app_work_buf)) {
      snapshot_app_work_obj = obj;
      return snapshot_app_work_buf;
    }
    snapshot_app_work_obj = nullptr;
    return nullptr;
  }

  if (!force_fresh && !use_work_buffer) {
    if (auto *panorama_view = snapshot_app_background_from_panorama(obj))
      return panorama_view;
  }

  if (!force_fresh) {
    if (auto *cached = snapshot_cache_find(obj))
      return cached;
  }
  auto *buf = snapshot_app_take_fresh(obj);
  if (buf != nullptr && owns != nullptr)
    *owns = true;
  return buf;
}

bool snapshot_app_begin(lv_obj_t *app, lv_obj_t *background, int width, int end_center_x, int end_center_y,
                        uint32_t duration_ms, bool opening) {
#if LV_USE_SNAPSHOT
  s_snapshot_app_open_frame_held = false;
  snapshot_swipe_cleanup();
  snapshot_scroll_cleanup();
  if (!snapshot_app_cleanup())
    return false;
  if (app == nullptr || width <= 0)
    return false;

  auto *disp = lv_obj_get_display(app);
  auto *component = disp == nullptr ? nullptr : static_cast<LvglComponent *>(lv_display_get_user_data(disp));
  if (component == nullptr || !SNAPSHOT_DIRECT_COMPOSITOR_ENABLED)
    return false;

  const bool previous_direct_active = s_snapshot_direct_active;

  bool owns_app = false;
  lv_draw_buf_t *app_buf = nullptr;
  if (!opening && snapshot_app_prepared_close_obj == app && snapshot_app_prepared_close_buf != nullptr) {
    app_buf = snapshot_app_prepared_close_buf;
    snapshot_app_prepared_close_obj = nullptr;
    snapshot_app_prepared_close_buf = nullptr;
    owns_app = snapshot_app_prepared_close_owns_buf;
    snapshot_app_prepared_close_owns_buf = false;
  } else {
    if (opening)
      snapshot_app_clear_prepared_close();
    app_buf = snapshot_app_cached_or_take(app, !opening, &owns_app, true);
  }
  bool owns_background = false;
  auto *background_buf = snapshot_app_cached_or_take(background, false, &owns_background, false);
  if (app_buf == nullptr) {
    ESP_LOGW(TAG, "snapshot app: failed to capture app=%p", app);
    if (owns_background && background_buf != nullptr)
      lv_draw_buf_destroy(background_buf);
    s_snapshot_direct_active = previous_direct_active;
    return false;
  }

  snapshot_app_state.app_root = app;
  snapshot_app_state.background_root = background;
  snapshot_app_state.app_buf = app_buf;
  snapshot_app_state.background_buf = background_buf;
  snapshot_app_state.owns_app_buf = owns_app;
  snapshot_app_state.owns_background_buf = owns_background;
  snapshot_app_state.active = true;
  snapshot_app_state.opening = opening;
  const uint64_t now_us = esp_timer_get_time();
  snapshot_app_state.anim_start_us =
      opening ? now_us - (uint64_t) SNAPSHOT_APP_OPEN_FIRST_FRAME_ADVANCE_MS * 1000ULL : now_us;
  snapshot_app_state.anim_duration_ms = duration_ms == 0 ? 1 : duration_ms;
  snapshot_app_state.start_size = opening ? std::min(width, SNAPSHOT_APP_OPEN_START_SIZE) : width;
  snapshot_app_state.end_size = opening ? width : 1;
  snapshot_app_state.start_center_x = width / 2;
  snapshot_app_state.start_center_y = width / 2;
  snapshot_app_state.end_center_x = end_center_x;
  snapshot_app_state.end_center_y = end_center_y;
  snapshot_app_state.component = component;
  snapshot_app_render_buffers_reset(opening);
  s_snapshot_direct_active = true;
  #ifdef USE_ESP32
  snapshot_app_state.worker_enabled = snapshot_app_worker_start();
  #endif
  if (opening && !snapshot_app_state.worker_enabled)
    snapshot_app_direct_anim_tick();
  return true;
#else
  return false;
#endif
}

bool snapshot_app_direct_anim_tick() {
  auto &state = snapshot_app_state;
  if (!state.active || state.component == nullptr)
    return false;

  bool animation_complete = false;
#ifdef USE_ESP32
  if (state.worker_enabled) {
    if (!snapshot_app_worker.done.load(std::memory_order_acquire))
      return true;
    if (snapshot_app_worker.failed.load(std::memory_order_acquire)) {
      ESP_LOGW(TAG, "snapshot app: worker render failed; presenting final frame synchronously");
      state.component->snapshot_app_direct_render(state.background_buf, state.app_buf, state.end_center_x,
                                                  state.end_center_y, state.end_size, state.end_size);
    }
    animation_complete = true;
  }
#endif
  if (!state.worker_enabled) {
    const uint64_t now_us = esp_timer_get_time();
    const uint32_t elapsed_ms = (uint32_t) ((now_us - state.anim_start_us) / 1000ULL);
    const uint32_t duration_ms = state.anim_duration_ms;
    const auto interpolate = snapshot_app_ease_out;
    const int size = interpolate(state.start_size, state.end_size, elapsed_ms, duration_ms);
    const int center_x = interpolate(state.start_center_x, state.end_center_x, elapsed_ms, duration_ms);
    const int center_y = interpolate(state.start_center_y, state.end_center_y, elapsed_ms, duration_ms);

    if (state.opening && size < SNAPSHOT_APP_OPEN_MIN_PRESENT_SIZE && elapsed_ms < duration_ms)
      return true;

    state.component->snapshot_app_direct_render(state.background_buf, state.app_buf, center_x, center_y, size, size);
    animation_complete = elapsed_ms >= duration_ms;
  }
  if (animation_complete) {
    state.component->wait_for_direct_frame_presented(50);
    state.component->realign_direct_buffer_after_manual_present(false);
    const bool opening = state.opening;
    if (opening && state.app_root != nullptr) {
      snapshot_cache_release_decoded_if_compressed(state.app_root);
    }
    if (!opening && state.app_buf == snapshot_app_work_buf && state.app_root != nullptr) {
      snapshot_app_work_obj = state.app_root;
    } else if (!opening && state.app_buf == &snapshot_app_presented_close_view && state.app_root != nullptr) {
      // Refresh the compact opening cache only after the last visible close
      // frame. The source remains pinned while DSI scans the completed Home
      // frame, so this does not require another full-screen allocation.
      const bool stored = snapshot_cache_store_compressed_view(state.app_root, state.app_buf);
      if (s_swipe_logging_enabled) {
        ESP_LOGI(TAG, "snapshot app: presented-frame cache refresh %s", stored ? "ok" : "failed");
      }
    } else if (!opening && state.owns_app_buf && state.app_root != nullptr && state.app_buf != nullptr) {
      // Keep at most one exact post-close app frame. If the asynchronous JPEG
      // encoder cannot start later because internal DMA memory is fragmented,
      // this fresh frame remains available for the next opening instead of
      // falling back to the stale boot-time application snapshot.
      if (snapshot_cache_latest_raw_app_obj != nullptr && snapshot_cache_latest_raw_app_obj != state.app_root) {
        auto *previous = snapshot_cache_find_entry(snapshot_cache_latest_raw_app_obj);
        if (previous != nullptr && !previous->compression_in_flight)
          snapshot_cache_destroy_raw(*previous);
      }
      snapshot_cache_store_raw_only(state.app_root, state.app_buf);
      snapshot_cache_latest_raw_app_obj = state.app_root;
      // Keep exactly one fresh raw app frame. Compressing it 250 ms after the
      // close animation still happens while the display is visible and creates
      // a sustained read+write burst over the same PSRAM bus used by DSI. The
      // next app close replaces this allocation, so memory remains bounded at
      // one 1.92 MB frame without risking a runtime blue flash.
      state.app_buf = nullptr;
      state.owns_app_buf = false;
    }
    if (!snapshot_app_cleanup())
      return true;
    // Keep the final direct frame on DSI until YAML has loaded the destination
    // LVGL page. This is required in both directions: releasing an opening
    // frame exposes the old Home page, while releasing a closing frame exposes
    // the old app and leaves stale edge pixels before Home is laid out.
    s_snapshot_app_open_frame_held = true;
    s_snapshot_direct_active = true;
  }
  return true;
}

bool snapshot_swipe_direct_anim_tick() {
  auto &state = snapshot_swipe_state;
  if (!state.direct_anim_active)
    return false;
#ifdef USE_ESP32
  if (state.worker_enabled)
    return true;
#endif
  if (!state.direct_render || state.component == nullptr) {
    state.direct_anim_active = false;
    return false;
  }

  const uint64_t now_us = esp_timer_get_time();
  const uint32_t elapsed_ms = (uint32_t) ((now_us - state.anim_start_us) / 1000ULL);
  const uint32_t duration_ms = state.anim_duration_ms;
  const int frame_current_x =
      snapshot_swipe_ease_out(state.anim_start_current_x, state.finish_current_x, elapsed_ms, duration_ms);
  const int frame_next_x =
      snapshot_swipe_ease_out(state.anim_start_next_x, state.finish_next_x, elapsed_ms, duration_ms);

  if (snapshot_swipe_render_direct_frame(frame_current_x, frame_next_x)) {
    state.current_x = frame_current_x;
    state.next_x = frame_next_x;
  }

  if (elapsed_ms >= duration_ms) {
    state.direct_anim_active = false;
    snapshot_swipe_finish_now();
  }
  return true;
}

void snapshot_swipe_direct_animate_to(int current_x, int next_x, uint32_t duration_ms) {
  if (!snapshot_swipe_state.direct_render || snapshot_swipe_state.component == nullptr)
    return;
  if (snapshot_swipe_state.direct_anim_timer != nullptr) {
    lv_timer_delete(snapshot_swipe_state.direct_anim_timer);
    snapshot_swipe_state.direct_anim_timer = nullptr;
  }
  snapshot_swipe_state.finish_current_x = current_x;
  snapshot_swipe_state.finish_next_x = next_x;
  snapshot_swipe_state.anim_start_current_x = snapshot_swipe_state.current_x;
  snapshot_swipe_state.anim_start_next_x = snapshot_swipe_state.next_x;
  snapshot_swipe_state.anim_start_us = esp_timer_get_time() - 16666ULL;
  snapshot_swipe_state.anim_duration_ms = duration_ms;
  snapshot_swipe_state.direct_anim_active = true;
  snapshot_swipe_direct_anim_tick();
}

void snapshot_swipe_anim_completed_cb(lv_anim_t *anim) {
  if (snapshot_swipe_state.cleanup_timer == nullptr) {
    snapshot_swipe_apply_final_roots();
    s_snapshot_swipe_active = false;
    lv_obj_invalidate(lv_screen_active());
    snapshot_swipe_state.cleanup_timer = lv_timer_create(
        [](lv_timer_t *timer) {
          snapshot_swipe_state.cleanup_timer = nullptr;
          snapshot_swipe_cleanup();
          lv_obj_invalidate(lv_screen_active());
          lv_timer_delete(timer);
        },
        48, nullptr);
  }
}

void snapshot_swipe_discard_pending_refresh(lv_display_t *disp) {
  if (disp == nullptr)
    return;
  lv_memzero(disp->inv_areas, sizeof(disp->inv_areas));
  lv_memzero(disp->inv_area_joined, sizeof(disp->inv_area_joined));
  disp->inv_p = 0;
  lv_ll_clear(&disp->sync_areas);
}

void snapshot_swipe_finish_now() {
  const bool direct_render = snapshot_swipe_state.direct_render && snapshot_swipe_state.component != nullptr;
  if (direct_render) {
    // Prime the inactive framebuffer with the exact final snapshot frame before
    // LVGL resumes. Otherwise the first real-page refresh can briefly present
    // an older buffer and flash between the snapshot compositor and LVGL.
    if (snapshot_swipe_state.current_x != snapshot_swipe_state.finish_current_x ||
        snapshot_swipe_state.next_x != snapshot_swipe_state.finish_next_x) {
      if (snapshot_swipe_render_direct_frame(snapshot_swipe_state.finish_current_x,
                                             snapshot_swipe_state.finish_next_x)) {
        snapshot_swipe_state.current_x = snapshot_swipe_state.finish_current_x;
        snapshot_swipe_state.next_x = snapshot_swipe_state.finish_next_x;
      }
    }
    snapshot_swipe_state.component->wait_for_direct_frame_presented(50);
    if (snapshot_swipe_state.commit) {
      // The compositor draws the page indicator after copying the panorama.
      // Mirror only that final 10-row strip into LVGL's two buffers before
      // handoff. This prevents correct -> previous -> correct indicator jumps
      // without copying two complete 1.92 MB frames.
      snapshot_swipe_state.component->synchronize_direct_framebuffer_rows(
          SNAPSHOT_PAGE_INDICATOR_Y, SNAPSHOT_PAGE_INDICATOR_H);
    }
    // FULL mode redraws the whole next LVGL buffer. Keep the compositor's
    // final third framebuffer on DSI until that redraw is ready instead of
    // copying 1.92 MB into each LVGL buffer between chained gestures.
    snapshot_swipe_state.component->realign_direct_buffer_after_manual_present(false);
  }
  snapshot_swipe_apply_final_roots();
  if (direct_render) {
    auto *component = snapshot_swipe_state.component;
    const bool commit = snapshot_swipe_state.commit;
    if (commit) {
      snapshot_swipe_discard_pending_refresh(component->get_disp());
    } else {
      s_snapshot_swipe_active = false;
      s_snapshot_direct_active = false;
      lv_obj_invalidate(lv_screen_active());
      lv_refr_now(component->get_disp());
      component->wait_for_direct_frame_presented(50);
    }
    if (snapshot_swipe_state.edge_bounce && snapshot_swipe_state.current_root != nullptr &&
        !snapshot_swipe_state.owns_current_buf) {
      snapshot_cache_release_decoded_if_compressed(snapshot_swipe_state.current_root);
      snapshot_swipe_state.current_buf = nullptr;
    }
    snapshot_swipe_cleanup();
    return;
  }
  snapshot_swipe_cleanup();
  lv_obj_invalidate(lv_screen_active());
}

void snapshot_scroll_cleanup() {
#ifdef USE_ESP32
  snapshot_scroll_worker_stop_and_wait();
#endif
  s_snapshot_direct_active = false;
  if (snapshot_scroll_state.root != nullptr) {
    if (snapshot_scroll_state.root_was_hidden) {
      lv_obj_add_flag(snapshot_scroll_state.root, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_clear_flag(snapshot_scroll_state.root, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (snapshot_scroll_state.content_buf != nullptr) {
    lv_draw_buf_destroy(snapshot_scroll_state.content_buf);
  }
  if (snapshot_scroll_state.content_tail_buf != nullptr) {
    lv_draw_buf_destroy(snapshot_scroll_state.content_tail_buf);
  }
  snapshot_scroll_state = {};
}

int snapshot_scroll_clamp_y(int scroll_y) {
  return std::clamp(scroll_y, 0, std::max(0, snapshot_scroll_state.max_scroll_y));
}

int snapshot_scroll_resist_y(int scroll_y) {
  constexpr int MAX_OVERSCROLL = 120;
  const int max_y = std::max(0, snapshot_scroll_state.max_scroll_y);
  if (scroll_y < 0)
    return -std::min(MAX_OVERSCROLL, (-scroll_y + 2) / 3);
  if (scroll_y > max_y)
    return max_y + std::min(MAX_OVERSCROLL, (scroll_y - max_y + 2) / 3);
  return scroll_y;
}

bool snapshot_scroll_capture(lv_obj_t *obj, int viewport_w, int viewport_h, bool render_now) {
#if LV_USE_SNAPSHOT
  if (obj == nullptr || viewport_w <= 0 || viewport_h <= 0)
    return false;

  auto *disp = lv_obj_get_display(obj);
  auto *component = disp == nullptr ? nullptr : static_cast<LvglComponent *>(lv_display_get_user_data(disp));
  if (component == nullptr || !SNAPSHOT_DIRECT_COMPOSITOR_ENABLED)
    return false;

  auto *parent = lv_obj_get_parent(obj);
  const int old_scroll_y = lv_obj_get_scroll_y(obj);
  const int max_scroll_y = std::max<int>(0, lv_obj_get_scroll_top(obj) + lv_obj_get_scroll_bottom(obj));
  const int content_h = std::max(viewport_h, viewport_h + max_scroll_y);
  const int old_h = lv_obj_get_height(obj);
  const bool was_hidden = lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);

  lv_obj_stop_scroll_anim(obj);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);

  // Prefer one contiguous list surface. It needs only one DMA2D/PPA crop per
  // frame and reaches the same throughput as the Home panorama. Fall back to
  // two segments only when PSRAM fragmentation prevents the larger block.
  int content_tail_y = 0;
  int head_h = content_h;
  int tail_h = 0;
  auto take_segment = [&](int segment_y, int segment_h) -> lv_draw_buf_t * {
    lv_obj_set_height(obj, segment_h);
    lv_obj_update_layout(parent == nullptr ? obj : parent);
    lv_obj_scroll_to_y(obj, segment_y, LV_ANIM_OFF);
    lv_obj_update_layout(parent == nullptr ? obj : parent);
    return lv_snapshot_take(obj, SNAPSHOT_CF);
  };

  lv_draw_buf_t *content_buf = take_segment(0, head_h);
  lv_draw_buf_t *content_tail_buf = nullptr;
  if (content_buf == nullptr && content_h > viewport_h) {
    content_tail_y = (content_h + 1) / 2;
    head_h = content_tail_y;
    tail_h = content_h - content_tail_y;
    content_buf = take_segment(0, head_h);
    content_tail_buf = content_buf != nullptr ? take_segment(content_tail_y, tail_h) : nullptr;
  }

  lv_obj_set_height(obj, old_h);
  lv_obj_scroll_to_y(obj, old_scroll_y, LV_ANIM_OFF);
  lv_obj_update_layout(parent == nullptr ? obj : parent);
  if (was_hidden) {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }

  if (content_buf == nullptr || (tail_h > 0 && content_tail_buf == nullptr)) {
    ESP_LOGW(TAG, "snapshot scroll: failed for obj=%p content_h=%d", obj, content_h);
    if (content_buf != nullptr)
      lv_draw_buf_destroy(content_buf);
    if (content_tail_buf != nullptr)
      lv_draw_buf_destroy(content_tail_buf);
    return false;
  }
  if (content_buf->header.cf != LV_COLOR_FORMAT_RGB888 || content_buf->header.w < viewport_w ||
      content_buf->header.h != head_h ||
      (content_tail_buf != nullptr &&
       (content_tail_buf->header.cf != LV_COLOR_FORMAT_RGB888 || content_tail_buf->header.w < viewport_w ||
        content_tail_buf->header.h != tail_h))) {
    ESP_LOGW(TAG, "snapshot scroll: invalid segmented buffers head=%dx%d tail=%dx%d", (int) content_buf->header.w,
             (int) content_buf->header.h, content_tail_buf == nullptr ? 0 : (int) content_tail_buf->header.w,
             content_tail_buf == nullptr ? 0 : (int) content_tail_buf->header.h);
    lv_draw_buf_destroy(content_buf);
    if (content_tail_buf != nullptr)
      lv_draw_buf_destroy(content_tail_buf);
    return false;
  }
#if defined(USE_ESP32)
  {
    const size_t content_size = content_buf->data_size != 0
                                    ? content_buf->data_size
                                    : (size_t) content_buf->header.stride * content_buf->header.h;
    lvgl_cache_msync_external(content_buf->data, content_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    if (content_tail_buf != nullptr) {
      const size_t tail_size = content_tail_buf->data_size != 0
                                   ? content_tail_buf->data_size
                                   : (size_t) content_tail_buf->header.stride * content_tail_buf->header.h;
      lvgl_cache_msync_external(content_tail_buf->data, tail_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    }
  }
#endif

  snapshot_scroll_state.root = obj;
  snapshot_scroll_state.content_buf = content_buf;
  snapshot_scroll_state.content_tail_buf = content_tail_buf;
  snapshot_scroll_state.content_tail_y = content_tail_y;
  snapshot_scroll_state.viewport_w = viewport_w;
  snapshot_scroll_state.viewport_h = viewport_h;
  snapshot_scroll_state.content_h = content_h;
  snapshot_scroll_state.max_scroll_y = std::min(max_scroll_y, std::max(0, content_h - viewport_h));
  snapshot_scroll_state.current_scroll_y = snapshot_scroll_clamp_y(old_scroll_y);
  snapshot_scroll_state.root_was_hidden = was_hidden;
  snapshot_scroll_state.component = component;

  if (!render_now)
    return true;

  if (!component->snapshot_scroll_direct_render(content_buf, content_tail_buf, content_tail_y,
                                                snapshot_scroll_state.current_scroll_y, viewport_w, viewport_h)) {
    snapshot_scroll_cleanup();
    return false;
  }
  snapshot_scroll_state.direct_render = true;
  s_snapshot_direct_active = true;
  lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
#ifdef USE_ESP32
  snapshot_scroll_state.worker_enabled = snapshot_scroll_worker_activate(snapshot_scroll_state.current_scroll_y);
#endif
  return true;
#else
  return false;
#endif
}
}  // namespace

extern "C" bool lvgl_esphome_snapshot_cache_page(lv_obj_t *obj) {
#if LV_USE_SNAPSHOT
  if (obj == nullptr)
    return false;
  snapshot_log_heap_("cache_page begin", obj, false);
  const uint64_t t0 = snapshot_diag_now_us_();
  const bool was_hidden = lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
  auto *buf = snapshot_take_centered(obj);
  if (buf == nullptr) {
    ESP_LOGW(TAG, "snapshot cache: failed for obj=%p", obj);
    snapshot_log_heap_("cache_page failed", obj, true);
    return false;
  }
  const uint64_t take_us = snapshot_diag_now_us_() - t0;
  if (take_us > 50000 || snapshot_diag_budget > 0) {
    ESP_LOGW(TAG, "snapshot diag: cache_page took=%lluus obj=%p size=%uKB cf=%u stride=%u hidden=%u",
             (unsigned long long) take_us, obj, (unsigned) (buf->data_size / 1024), (unsigned) buf->header.cf,
             (unsigned) buf->header.stride, (unsigned) was_hidden);
  }
  snapshot_cache_store(obj, buf);
  snapshot_log_heap_("cache_page stored", obj, false);
  return true;
#else
  return false;
#endif
}

extern "C" bool lvgl_esphome_snapshot_cache_compressed_page(lv_obj_t *obj) {
#if LV_USE_SNAPSHOT
  if (obj == nullptr)
    return false;
  snapshot_log_heap_("cache_compressed_page begin", obj, false);
  if (esphome_mipi_dsi_mark_stress != nullptr)
    esphome_mipi_dsi_mark_stress("snapshot-render", 900);
  const uint64_t t0 = snapshot_diag_now_us_();
  const bool was_hidden = lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
  bool reusable_work_buf = false;
  lv_draw_buf_t *buf = nullptr;
  if (snapshot_app_reserve_work_buffer(obj) && snapshot_take_centered_to(obj, snapshot_app_work_buf)) {
    buf = snapshot_app_work_buf;
    snapshot_app_work_obj = obj;
    reusable_work_buf = true;
  } else {
    buf = snapshot_take_centered(obj);
  }
  if (buf == nullptr) {
    ESP_LOGW(TAG, "snapshot compressed cache: failed for obj=%p", obj);
    snapshot_log_heap_("cache_compressed_page failed", obj, true);
    return false;
  }
  const uint64_t take_us = snapshot_diag_now_us_() - t0;
  if (take_us > 50000 || snapshot_diag_budget > 0) {
    ESP_LOGW(TAG, "snapshot diag: cache_compressed_page took=%lluus obj=%p size=%uKB cf=%u stride=%u hidden=%u",
             (unsigned long long) take_us, obj, (unsigned) (buf->data_size / 1024), (unsigned) buf->header.cf,
             (unsigned) buf->header.stride, (unsigned) was_hidden);
  }
  lvgl_esphome_wait_snapshot_dsi_fifo();
  if (esphome_mipi_dsi_mark_stress != nullptr) {
    esphome_mipi_dsi_mark_stress("snapshot-compress", 1200);
  }
  if (reusable_work_buf) {
    snapshot_cache_store_compressed_view(obj, buf);
  } else {
    snapshot_cache_store_compressed_only(obj, buf);
  }
  lvgl_esphome_wait_snapshot_dsi_fifo();
  snapshot_log_heap_("cache_compressed_page stored", obj, false);
  return snapshot_cache_find_entry(obj) != nullptr;
#else
  return false;
#endif
}

extern "C" bool lvgl_esphome_snapshot_app_reserve_work_buffer(lv_obj_t *obj) {
#if LV_USE_SNAPSHOT
  return snapshot_app_reserve_work_buffer(obj);
#else
  return false;
#endif
}

extern "C" bool lvgl_esphome_snapshot_app_release_work_buffer(void) {
#if LV_USE_SNAPSHOT
  if (snapshot_app_state.active || s_snapshot_app_open_frame_held ||
      snapshot_app_prepared_close_buf == snapshot_app_work_buf) {
    return false;
  }
  if (snapshot_app_work_buf != nullptr) {
    lv_draw_buf_destroy(snapshot_app_work_buf);
    snapshot_app_work_buf = nullptr;
    snapshot_app_work_obj = nullptr;
  }
  return true;
#else
  return false;
#endif
}

extern "C" bool lvgl_esphome_snapshot_cache_raw_page(lv_obj_t *obj) {
#if LV_USE_SNAPSHOT
  return snapshot_cache_prepare_raw_page(obj);
#else
  return false;
#endif
}

extern "C" bool lvgl_esphome_snapshot_cache_current_frame_raw_page(lv_obj_t *obj) {
#if LV_USE_SNAPSHOT && LV_COLOR_DEPTH == 32
  if (obj == nullptr)
    return false;
  auto *entry = snapshot_cache_find_entry(obj);
  if (entry != nullptr && entry->buf != nullptr)
    return true;

  auto *disp = lv_obj_get_display(obj);
  auto *component = disp == nullptr ? nullptr : static_cast<LvglComponent *>(lv_display_get_user_data(disp));
  if (component == nullptr)
    return snapshot_cache_prepare_raw_page(obj);

  lv_refr_now(disp);
  component->wait_for_direct_frame_presented(40);
  lvgl_esphome_snapshot_dsi_quiet(20);

  const int WIDTH = lv_display_get_horizontal_resolution(disp);
  const int HEIGHT = lv_display_get_vertical_resolution(disp);
  const int STRIDE = WIDTH * 3;
  if (WIDTH <= 0 || HEIGHT <= 0)
    return snapshot_cache_prepare_raw_page(obj);
  lv_draw_buf_t *buf = lv_draw_buf_create(WIDTH, HEIGHT, LV_COLOR_FORMAT_RGB888, STRIDE);
  if (buf == nullptr || buf->data == nullptr) {
    if (buf != nullptr)
      lv_draw_buf_destroy(buf);
    return snapshot_cache_prepare_raw_page(obj);
  }

  lvgl_esphome_wait_snapshot_dsi_fifo();
  if (esphome_mipi_dsi_mark_stress != nullptr) {
    esphome_mipi_dsi_mark_stress("snapshot-frame-copy", 220);
  }
  const uint64_t t0 = snapshot_diag_now_us_();
  const bool captured =
      component->direct_capture_rgb888(static_cast<uint8_t *>(buf->data), STRIDE, 0, 0, WIDTH, HEIGHT);
  const uint64_t take_us = snapshot_diag_now_us_() - t0;
  lvgl_esphome_wait_snapshot_dsi_fifo();
  if (!captured) {
    lv_draw_buf_destroy(buf);
    return snapshot_cache_prepare_raw_page(obj);
  }

  snapshot_cache_store_raw_only(obj, buf);
  lvgl_esphome_wait_snapshot_dsi_fifo();
  lvgl_esphome_snapshot_dsi_quiet(CONFIG_ESPHOME_LVGL_SNAPSHOT_RAW_DSI_QUIET_MS);
  if (take_us > 50000 || snapshot_diag_budget > 0) {
    ESP_LOGW(TAG, "snapshot diag: frame copy raw page obj=%p took=%lluus size=%uKB cf=%u stride=%u", obj,
             (unsigned long long) take_us, (unsigned) (buf->data_size / 1024), (unsigned) buf->header.cf,
             (unsigned) buf->header.stride);
  }
  return true;
#else
  return snapshot_cache_prepare_raw_page(obj);
#endif
}

extern "C" bool lvgl_esphome_snapshot_cache_current_frame_compressed_page(lv_obj_t *obj) {
#if LV_USE_SNAPSHOT && LV_COLOR_DEPTH == 32 && defined(USE_LVGL_SNAPSHOT_JPEG_CACHE)
  if (obj == nullptr)
    return false;

  auto *disp = lv_obj_get_display(obj);
  auto *component = disp == nullptr ? nullptr : static_cast<LvglComponent *>(lv_display_get_user_data(disp));
  if (component == nullptr)
    return lvgl_esphome_snapshot_cache_compressed_page(obj);

  lv_refr_now(disp);
  component->wait_for_direct_frame_presented(40);
  const uint8_t *source = component->direct_get_stable_presented_frame(50);
  const int width = lv_display_get_horizontal_resolution(disp);
  const int height = lv_display_get_vertical_resolution(disp);
  const int stride = width * 3;
  if (source == nullptr || width <= 0 || height <= 0)
    return lvgl_esphome_snapshot_cache_compressed_page(obj);

  lv_draw_buf_t view{};
  if (lv_draw_buf_init(&view, width, height, LV_COLOR_FORMAT_RGB888, stride, const_cast<uint8_t *>(source),
                       static_cast<uint32_t>(stride) * static_cast<uint32_t>(height)) != LV_RESULT_OK) {
    return lvgl_esphome_snapshot_cache_compressed_page(obj);
  }

  lvgl_esphome_wait_snapshot_dsi_fifo();
  if (esphome_mipi_dsi_mark_stress != nullptr)
    esphome_mipi_dsi_mark_stress("snapshot-frame-compress", 1200);
  const uint64_t started_us = snapshot_diag_now_us_();
  const bool stored = snapshot_cache_store_compressed_view(obj, &view);
  const uint64_t elapsed_us = snapshot_diag_now_us_() - started_us;
  lvgl_esphome_wait_snapshot_dsi_fifo();
  if (elapsed_us > 50000 || snapshot_diag_budget > 0) {
    ESP_LOGW(TAG, "snapshot diag: direct frame compressed obj=%p stored=%u took=%lluus", obj,
             static_cast<unsigned>(stored), static_cast<unsigned long long>(elapsed_us));
  }
  return stored;
#else
  return lvgl_esphome_snapshot_cache_compressed_page(obj);
#endif
}

extern "C" bool lvgl_esphome_snapshot_cache_pair(lv_obj_t *left, lv_obj_t *right, int width) {
#if LV_USE_SNAPSHOT
  if (left == nullptr || right == nullptr)
    return false;
  constexpr int scale = SNAPSHOT_PANORAMA_SCALE;
  if (snapshot_panorama_cache_find(left, right, width, scale) != nullptr)
    return true;

  snapshot_log_heap_("cache_pair begin", left, false);
  const uint64_t t0 = snapshot_diag_now_us_();
  SnapshotPanoramaPageSource left_source;
  SnapshotPanoramaPageSource right_source;
  bool left_from_cache = snapshot_panorama_source_from_cache(left, width, scale, &left_source);
  bool right_from_cache = snapshot_panorama_source_from_cache(right, width, scale, &right_source);
  bool left_from_page_cache = false;
  bool right_from_page_cache = false;

  lv_draw_buf_t *left_buf = nullptr;
  lv_draw_buf_t *right_buf = nullptr;
  bool left_owns_buf = false;
  bool right_owns_buf = false;
  uint64_t left_snapshot_us = 0;
  uint64_t right_snapshot_us = 0;

  if (!left_from_cache) {
    left_buf = snapshot_cache_find(left);
    if (left_buf != nullptr && snapshot_panorama_source_from_buffer(left_buf, scale, &left_source)) {
      left_from_page_cache = true;
    } else {
      const uint64_t left_t0 = snapshot_diag_now_us_();
      left_buf = snapshot_take_centered(left);
      left_snapshot_us = snapshot_diag_now_us_() - left_t0;
      left_owns_buf = true;
    }
    if (left_buf == nullptr || !snapshot_panorama_source_from_buffer(left_buf, scale, &left_source)) {
      if (left_owns_buf && left_buf != nullptr)
        lv_draw_buf_destroy(left_buf);
      if (left_from_page_cache)
        snapshot_cache_release_decoded_if_compressed(left);
      ESP_LOGW(TAG, "snapshot diag: cache_pair failed left=%p right=%p width=%d stage=left", left, right, width);
      snapshot_log_heap_("cache_pair left failed", left, true);
      return false;
    }
  }

  if (!right_from_cache) {
    right_buf = snapshot_cache_find(right);
    if (right_buf != nullptr && snapshot_panorama_source_from_buffer(right_buf, scale, &right_source)) {
      right_from_page_cache = true;
    } else {
      const uint64_t right_t0 = snapshot_diag_now_us_();
      right_buf = snapshot_take_centered(right);
      right_snapshot_us = snapshot_diag_now_us_() - right_t0;
      right_owns_buf = true;
    }
    if (right_buf == nullptr || !snapshot_panorama_source_from_buffer(right_buf, scale, &right_source)) {
      if (right_owns_buf && right_buf != nullptr)
        lv_draw_buf_destroy(right_buf);
      if (right_from_page_cache)
        snapshot_cache_release_decoded_if_compressed(right);
      if (left_owns_buf && left_buf != nullptr)
        lv_draw_buf_destroy(left_buf);
      if (left_from_page_cache)
        snapshot_cache_release_decoded_if_compressed(left);
      ESP_LOGW(TAG, "snapshot diag: cache_pair failed left=%p right=%p width=%d stage=right", left, right, width);
      snapshot_log_heap_("cache_pair right failed", right, true);
      return false;
    }
  }

  const uint64_t panorama_t0 = snapshot_diag_now_us_();
  const bool prepared =
      snapshot_panorama_cache_prepare_from_sources(left, right, left_source, right_source, width) != nullptr;
  const uint64_t panorama_us = snapshot_diag_now_us_() - panorama_t0;
  if (left_owns_buf && left_buf != nullptr)
    lv_draw_buf_destroy(left_buf);
  if (right_owns_buf && right_buf != nullptr)
    lv_draw_buf_destroy(right_buf);
  if (left_from_page_cache)
    snapshot_cache_release_decoded_if_compressed(left);
  if (right_from_page_cache)
    snapshot_cache_release_decoded_if_compressed(right);
  if (!prepared) {
    snapshot_log_heap_("cache_pair panorama failed", left, true);
  }
  const uint64_t elapsed_us = snapshot_diag_now_us_() - t0;
  if (!prepared || elapsed_us > 50000 || snapshot_diag_budget > 0) {
    ESP_LOGW(TAG,
             "snapshot diag: cache_pair left=%p right=%p width=%d prepared=%u took=%lluus panorama=%lluus "
             "left_snapshot=%lluus right_snapshot=%lluus left_src=%s right_src=%s",
             left, right, width, (unsigned) prepared, (unsigned long long) elapsed_us, (unsigned long long) panorama_us,
             (unsigned long long) left_snapshot_us, (unsigned long long) right_snapshot_us,
             left_from_cache ? "panorama" : (left_from_page_cache ? "page_cache" : "snapshot"),
             right_from_cache ? "panorama" : (right_from_page_cache ? "page_cache" : "snapshot"));
  }
  return prepared;
#else
  return false;
#endif
}

SnapshotCacheEntry *snapshot_tile_cache_entry(lv_obj_t *page, bool create) {
  auto *entry = snapshot_cache_find_entry(page);
  if (entry != nullptr) {
    if (entry->compression_in_flight)
      return nullptr;
    entry->tile_page = true;
    return entry;
  }
  if (!create)
    return nullptr;

  for (auto &candidate : snapshot_cache) {
    if (candidate.obj == nullptr && !candidate.compression_in_flight) {
      snapshot_cache_free_entry(candidate);
      candidate.obj = page;
      candidate.big_endian = snapshot_cache_obj_big_endian(page);
      candidate.tile_page = true;
      return &candidate;
    }
  }
  ESP_LOGW(TAG, "snapshot tiles: compressed page metadata capacity exhausted");
  return nullptr;
}

bool snapshot_tile_cache_valid(const SnapshotCacheEntry *entry, int width, int height) {
#ifdef USE_LVGL_SNAPSHOT_JPEG_CACHE
  return entry != nullptr && entry->tile_page && !entry->jpeg.empty() && entry->width == static_cast<uint32_t>(width) &&
         entry->height == static_cast<uint32_t>(height) && entry->stride == static_cast<uint32_t>(width * 3) &&
         entry->cf == LV_COLOR_FORMAT_RGB888;
#else
  (void) entry;
  (void) width;
  (void) height;
  return false;
#endif
}

bool snapshot_tile_cache_encode(lv_obj_t *page, lv_draw_buf_t *source) {
  auto *entry = snapshot_tile_cache_entry(page, true);
  if (entry == nullptr || source == nullptr || source->data == nullptr)
    return false;
  entry->big_endian = snapshot_cache_obj_big_endian(page);
  entry->tile_page = true;
  if (!snapshot_cache_encode_jpeg(*entry, source))
    return false;
  entry->generation++;
  return true;
}

int snapshot_tile_window_slot_index(lv_obj_t *page) {
  for (size_t index = 0; index < SNAPSHOT_TILE_SLOT_COUNT; index++) {
    if (snapshot_tile_window_cache.pages[index] == page)
      return static_cast<int>(index);
  }
  return -1;
}

bool snapshot_tile_window_flush_slot(size_t slot_index) {
  if (slot_index >= SNAPSHOT_TILE_SLOT_COUNT || !snapshot_tile_window_cache.dirty[slot_index])
    return true;
  auto *page = snapshot_tile_window_cache.pages[slot_index];
  auto *slot = snapshot_tile_window_cache.slots[slot_index];
  if (page == nullptr || slot == nullptr || !snapshot_tile_cache_encode(page, slot))
    return false;
  snapshot_tile_window_cache.dirty[slot_index] = false;
  return true;
}

bool snapshot_tile_window_load_page(lv_obj_t *page, size_t slot_index, int width, int height) {
  if (page == nullptr || slot_index >= SNAPSHOT_TILE_SLOT_COUNT)
    return false;
  auto *slot = snapshot_tile_window_cache.slots[slot_index];
  if (slot == nullptr)
    return false;

  auto *entry = snapshot_tile_cache_entry(page, false);
  bool ready = false;
  if (snapshot_tile_cache_valid(entry, width, height)) {
    lvgl_esphome_wait_snapshot_dsi_fifo();
    ready = snapshot_cache_decode_jpeg_to_draw_buf(*entry, slot);
    lvgl_esphome_wait_snapshot_dsi_fifo();
  } else {
    ready = snapshot_take_centered_to(page, slot) && snapshot_tile_cache_encode(page, slot);
  }
  if (!ready)
    return false;

  snapshot_tile_window_cache.pages[slot_index] = page;
  snapshot_tile_window_cache.dirty[slot_index] = false;
  return true;
}

extern "C" bool lvgl_esphome_snapshot_cache_tile_window(lv_obj_t **pages, int page_count, int current_page,
                                                        int width) {
#if LV_USE_SNAPSHOT && defined(USE_LVGL_SNAPSHOT_JPEG_CACHE)
  if (pages == nullptr || page_count <= 0 || current_page < 1 || current_page > page_count || width <= 0 ||
      s_snapshot_direct_active || s_snapshot_swipe_active || snapshot_app_state.active ||
      snapshot_scroll_state.direct_render) {
    return false;
  }
  for (int index = 0; index < page_count; index++) {
    if (pages[index] == nullptr)
      return false;
  }

  auto *display = lv_obj_get_display(pages[0]);
  const int height = display == nullptr ? 0 : lv_display_get_vertical_resolution(display);
  if (height <= 0)
    return false;

  // The old full panorama grew by one 1.92 MB RGB888 frame per page. Keep a
  // fixed previous/current/next working set instead; every other page remains
  // a compact JPEG and is decoded directly into a recycled slot.
  for (auto &panorama : snapshot_panorama_cache)
    snapshot_panorama_free_entry(panorama);
  if (!snapshot_tile_window_reserve(width, height))
    return false;

  const int window_count = std::min<int>(page_count, SNAPSHOT_TILE_SLOT_COUNT);
  const int max_start = page_count - window_count;
  const int window_start = std::clamp(current_page - 2, 0, max_start);
  lv_obj_t *desired_pages[SNAPSHOT_TILE_SLOT_COUNT]{nullptr, nullptr, nullptr};
  int desired_slots[SNAPSHOT_TILE_SLOT_COUNT]{-1, -1, -1};
  bool slot_used[SNAPSHOT_TILE_SLOT_COUNT]{false, false, false};

  for (int index = 0; index < window_count; index++) {
    desired_pages[index] = pages[window_start + index];
    desired_slots[index] = snapshot_tile_window_slot_index(desired_pages[index]);
    if (desired_slots[index] >= 0)
      slot_used[desired_slots[index]] = true;
  }

  for (int index = 0; index < window_count; index++) {
    if (desired_slots[index] >= 0)
      continue;
    for (size_t slot_index = 0; slot_index < SNAPSHOT_TILE_SLOT_COUNT; slot_index++) {
      if (slot_used[slot_index])
        continue;
      if (!snapshot_tile_window_flush_slot(slot_index))
        return false;
      snapshot_tile_window_cache.pages[slot_index] = nullptr;
      snapshot_tile_window_cache.dirty[slot_index] = false;
      desired_slots[index] = static_cast<int>(slot_index);
      slot_used[slot_index] = true;
      break;
    }
    if (desired_slots[index] < 0)
      return false;
  }

  for (int index = 0; index < window_count; index++) {
    const size_t slot_index = static_cast<size_t>(desired_slots[index]);
    if (snapshot_tile_window_cache.pages[slot_index] == desired_pages[index])
      continue;
    if (!snapshot_tile_window_load_page(desired_pages[index], slot_index, width, height))
      return false;
  }

  // Build the compressed backing store for pages outside the current window
  // once. A single existing application work buffer is reused as scratch, so
  // adding a page does not allocate another raw full-screen surface.
  for (int page_index = 0; page_index < page_count; page_index++) {
    auto *entry = snapshot_tile_cache_entry(pages[page_index], false);
    if (snapshot_tile_cache_valid(entry, width, height))
      continue;
    const int raw_slot = snapshot_tile_window_slot_index(pages[page_index]);
    lv_draw_buf_t *scratch = raw_slot >= 0 ? snapshot_tile_window_cache.slots[raw_slot] : nullptr;
    if (scratch == nullptr) {
      if (!snapshot_app_reserve_work_buffer(pages[page_index]))
        return false;
      scratch = snapshot_app_work_buf;
      if (!snapshot_take_centered_to(pages[page_index], scratch))
        return false;
    }
    if (!snapshot_tile_cache_encode(pages[page_index], scratch))
      return false;
  }

  for (size_t slot_index = 0; slot_index < SNAPSHOT_TILE_SLOT_COUNT; slot_index++) {
    if (!slot_used[slot_index]) {
      if (!snapshot_tile_window_flush_slot(slot_index))
        return false;
      snapshot_tile_window_cache.pages[slot_index] = nullptr;
      snapshot_tile_window_cache.dirty[slot_index] = false;
    }
  }

  if (!snapshot_jpeg_bootstrap_complete) {
    // Boot uses one reusable full-screen encoder output allocation for all app
    // and tile snapshots. Once every page has JPEG backing, keep only the
    // small hardware engine and release that 1.92 MB scratch allocation.
    snapshot_jpeg_bootstrap_complete = true;
    esp32_jpeg::release_preallocated_encoder_output();
  }

  ESP_LOGI(TAG, "snapshot tiles: window %d-%d/%d raw=%u KB compressed-total=%u KB", window_start + 1,
           window_start + window_count, page_count, static_cast<unsigned>(snapshot_tile_window_bytes() / 1024),
           static_cast<unsigned>(lvgl_esphome_snapshot_memory_bytes() / 1024));
  return true;
#else
  (void) pages;
  (void) page_count;
  (void) current_page;
  (void) width;
  return false;
#endif
}

extern "C" bool lvgl_esphome_snapshot_refresh_tile_page(lv_obj_t *page, int width) {
#if defined(USE_ESP32) && LV_COLOR_DEPTH == 32 && LV_USE_SNAPSHOT
  if (page == nullptr || width <= 0 || s_snapshot_direct_active || s_snapshot_swipe_active ||
      snapshot_app_state.active || snapshot_scroll_state.direct_render)
    return false;

  const int slot_index = snapshot_tile_window_slot_index(page);
  if (slot_index < 0)
    return false;
  auto *slot = snapshot_tile_window_cache.slots[slot_index];
  if (slot == nullptr || slot->header.w != static_cast<uint32_t>(width) || !snapshot_take_centered_to(page, slot))
    return false;

  // Keep the live raw page immediately current. Its JPEG is refreshed lazily
  // only if this slot is about to be recycled, coalescing repeated slider or
  // toggle updates into one hardware encode.
  snapshot_tile_window_cache.dirty[slot_index] = true;
  if (s_swipe_logging_enabled) {
    ESP_LOGI(TAG, "snapshot tiles: refreshed raw slot=%d; JPEG marked dirty", slot_index);
  }
  return true;
#else
  (void) page;
  (void) width;
  return false;
#endif
}

extern "C" bool lvgl_esphome_snapshot_is_active(void) {
  return s_snapshot_swipe_active || snapshot_app_state.active ||
         (s_snapshot_direct_active && !s_snapshot_app_open_frame_held);
}

extern "C" bool lvgl_esphome_snapshot_app_open(lv_obj_t *app, lv_obj_t *background, int width, uint32_t duration_ms) {
  return snapshot_app_begin(app, background, width, width / 2, width / 2, duration_ms, true);
}

extern "C" void lvgl_esphome_snapshot_app_release_open_hold(void) {
#if LV_USE_SNAPSHOT
  if (!s_snapshot_app_open_frame_held)
    return;
  s_snapshot_app_open_frame_held = false;
  s_snapshot_direct_active = false;
  lv_obj_invalidate(lv_screen_active());
#endif
}

extern "C" bool lvgl_esphome_snapshot_app_close(lv_obj_t *app, lv_obj_t *background, int width, int target_center_x,
                                                int target_center_y, uint32_t duration_ms) {
  return snapshot_app_begin(app, background, width, target_center_x, target_center_y, duration_ms, false);
}

extern "C" bool lvgl_esphome_snapshot_app_prepare_close(lv_obj_t *app) {
#if LV_USE_SNAPSHOT
  snapshot_app_clear_prepared_close();
  if (app == nullptr)
    return false;

  auto *disp = lv_obj_get_display(app);
  auto *component = disp == nullptr ? nullptr : static_cast<LvglComponent *>(lv_display_get_user_data(disp));
  lv_draw_buf_t *buf = nullptr;
  // When the app has a persistent raw cache, refresh that allocation in
  // place. The freshly captured frame immediately becomes both the closing
  // animation source and the next opening source, with no second full-screen
  // allocation, JPEG encode, or JPEG decode.
  auto *entry = snapshot_cache_find_entry(app);
  if (component != nullptr && entry != nullptr && entry->buf != nullptr) {
    const int width = lv_display_get_horizontal_resolution(disp);
    const int height = lv_display_get_vertical_resolution(disp);
    const int stride = width * 3;
    if (width > 0 && height > 0 && entry->buf->header.cf == LV_COLOR_FORMAT_RGB888 &&
        entry->buf->header.w == width && entry->buf->header.h == height && entry->buf->header.stride == stride &&
        component->direct_capture_rgb888(static_cast<uint8_t *>(entry->buf->data), stride, 0, 0, width, height)) {
      entry->decoded_from_jpeg = false;
      entry->generation++;
      buf = entry->buf;
    }
  }
  // The currently presented DSI framebuffer already is the exact full-colour
  // application image. Use it as a read-only close-animation source instead
  // of allocating and filling another 1.92 MB RGB888 surface. The compositor
  // excludes this framebuffer from its target rotation until completion.
  if (buf == nullptr && component != nullptr) {
    const int width = lv_display_get_horizontal_resolution(disp);
    const int height = lv_display_get_vertical_resolution(disp);
    const int stride = width * 3;
    const uint8_t *presented = component->direct_get_stable_presented_frame(50);
    snapshot_app_presented_close_view = {};
    if (presented != nullptr && width > 0 && height > 0 &&
        lv_draw_buf_init(&snapshot_app_presented_close_view, width, height, LV_COLOR_FORMAT_RGB888, stride,
                         const_cast<uint8_t *>(presented), static_cast<uint32_t>(stride) * height) == LV_RESULT_OK) {
      buf = &snapshot_app_presented_close_view;
    }
  }
  if (buf == nullptr && component != nullptr && snapshot_app_reserve_work_buffer(app)) {
    const int width = lv_display_get_horizontal_resolution(disp);
    const int height = lv_display_get_vertical_resolution(disp);
    const int stride = width * 3;
    if (width > 0 && height > 0 &&
        lv_draw_buf_reshape(snapshot_app_work_buf, LV_COLOR_FORMAT_RGB888, width, height, stride) != nullptr &&
        component->direct_capture_rgb888(static_cast<uint8_t *>(snapshot_app_work_buf->data), stride, 0, 0, width,
                                         height)) {
      buf = snapshot_app_work_buf;
    }
  }
  if (buf == nullptr && snapshot_app_reserve_work_buffer(app) &&
      snapshot_take_centered_to(app, snapshot_app_work_buf)) {
    buf = snapshot_app_work_buf;
  }
  if (buf == nullptr)
    return false;
  if (buf == snapshot_app_work_buf)
    snapshot_app_work_obj = app;
  snapshot_app_prepared_close_obj = app;
  snapshot_app_prepared_close_buf = buf;
  snapshot_app_prepared_close_owns_buf = false;
  return true;
#else
  return false;
#endif
}

extern "C" void lvgl_esphome_snapshot_app_clear_prepared_close(void) {
#if LV_USE_SNAPSHOT
  snapshot_app_clear_prepared_close();
#endif
}

extern "C" bool lvgl_esphome_snapshot_swipe_begin(lv_obj_t *current, lv_obj_t *next, int width, int next_x) {
#if LV_USE_SNAPSHOT
  snapshot_swipe_complete_before_next_gesture();
  s_snapshot_swipe_active = true;
  if (current == nullptr || next == nullptr)
    return false;

  auto *parent = lv_obj_get_parent(current);
  if (parent == nullptr)
    return false;
  snapshot_swipe_state.current_root = current;
  snapshot_swipe_state.next_root = next;

  lv_obj_clear_flag(current, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(next, LV_OBJ_FLAG_HIDDEN);
  lv_obj_align(current, LV_ALIGN_CENTER, 0, 0);
  lv_obj_align(next, LV_ALIGN_CENTER, next_x, 0);
  lv_obj_update_layout(parent);

  auto *disp = lv_obj_get_display(current);
  auto *component = disp == nullptr ? nullptr : static_cast<LvglComponent *>(lv_display_get_user_data(disp));
  if (component != nullptr && SNAPSHOT_DIRECT_COMPOSITOR_ENABLED) {
    constexpr int scale = SNAPSHOT_PANORAMA_SCALE;
    SnapshotPanoramaPageSource current_source{};
    SnapshotPanoramaPageSource next_source{};
    if (scale == 1 && snapshot_panorama_source_from_cache(current, width, scale, &current_source) &&
        snapshot_panorama_source_from_cache(next, width, scale, &next_source) &&
        snapshot_panorama_source_view(current_source, width, component->get_height(),
                                      &snapshot_swipe_panorama_page_views[0]) &&
        snapshot_panorama_source_view(next_source, width, component->get_height(),
                                      &snapshot_swipe_panorama_page_views[1])) {
      snapshot_swipe_state.current_buf = &snapshot_swipe_panorama_page_views[0];
      snapshot_swipe_state.next_buf = &snapshot_swipe_panorama_page_views[1];
      snapshot_swipe_state.width = width;
      snapshot_swipe_state.current_x = 0;
      snapshot_swipe_state.next_x = next_x;
      snapshot_swipe_state.component = component;
      snapshot_swipe_state.direct_render = true;
      s_snapshot_direct_active = true;
#ifdef USE_ESP32
      snapshot_swipe_state.worker_enabled = snapshot_swipe_worker_activate(0, next_x);
#endif
      lv_obj_add_flag(current, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(next, LV_OBJ_FLAG_HIDDEN);
      snapshot_swipe_discard_pending_refresh(component->get_disp());
      if (s_swipe_logging_enabled) {
        ESP_LOGI(TAG, "snapshot swipe: direct DMA2D slot compositor active, next_x=%d", next_x);
      }
      return true;
    }
    lv_obj_t *left_obj = next_x > 0 ? current : next;
    lv_obj_t *right_obj = next_x > 0 ? next : current;
    int source_page_index = 0;
    if (auto *panorama = snapshot_panorama_cache_find(left_obj, right_obj, width, scale, &source_page_index)) {
      snapshot_swipe_state.width = width;
      snapshot_swipe_state.current_x = 0;
      snapshot_swipe_state.next_x = next_x;
      snapshot_swipe_state.component = component;
      snapshot_swipe_state.panorama_buf = panorama->buf;
      snapshot_swipe_state.panorama_size = panorama->size;
      snapshot_swipe_state.panorama_scale = panorama->scale;
      snapshot_swipe_state.panorama_next_x = next_x;
      snapshot_swipe_state.panorama_page_count = std::max(2, panorama->page_count);
      snapshot_swipe_state.panorama_source_page_index = source_page_index;
      snapshot_swipe_state.panorama_render = true;
      snapshot_swipe_state.direct_render = true;
      s_snapshot_direct_active = true;
#ifdef USE_ESP32
      snapshot_swipe_state.worker_enabled = snapshot_swipe_worker_activate(0, next_x);
#endif
      lv_obj_add_flag(current, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(next, LV_OBJ_FLAG_HIDDEN);
      // A chained gesture can begin while LVGL still has invalidations queued
      // from committing the previous page. Both roots are now hidden and the
      // direct compositor owns scanout, so drawing those stale areas would only
      // contend with the next swipe for PSRAM bandwidth.
      snapshot_swipe_discard_pending_refresh(component->get_disp());
      if (s_swipe_logging_enabled) {
        ESP_LOGI(TAG, "snapshot swipe: direct framebuffer compositor active (RGB888 panorama), next_x=%d", next_x);
      }
      return true;
    }
  }

  snapshot_swipe_state.current_buf = snapshot_cache_find(current);
  snapshot_swipe_state.next_buf = snapshot_cache_find(next);
  if (snapshot_swipe_state.current_buf == nullptr) {
    snapshot_swipe_state.current_buf = lv_snapshot_take(current, SNAPSHOT_CF);
    snapshot_swipe_state.owns_current_buf = true;
  }
  if (snapshot_swipe_state.next_buf == nullptr) {
    snapshot_swipe_state.next_buf = lv_snapshot_take(next, SNAPSHOT_CF);
    snapshot_swipe_state.owns_next_buf = true;
  }
  if (snapshot_swipe_state.current_buf == nullptr || snapshot_swipe_state.next_buf == nullptr) {
    ESP_LOGW(TAG, "snapshot swipe: failed to create draw buffers");
    snapshot_swipe_cleanup();
    return false;
  }

  snapshot_swipe_state.width = width;
  snapshot_swipe_state.current_x = 0;
  snapshot_swipe_state.next_x = next_x;
  if (component != nullptr && SNAPSHOT_DIRECT_COMPOSITOR_ENABLED) {
    snapshot_swipe_state.component = component;
    constexpr int scale = SNAPSHOT_PANORAMA_SCALE;
    lv_obj_t *left_obj = next_x > 0 ? current : next;
    lv_obj_t *right_obj = next_x > 0 ? next : current;
    int source_page_index = 0;
    if (auto *panorama = snapshot_panorama_cache_find(left_obj, right_obj, width, scale, &source_page_index)) {
      snapshot_swipe_state.panorama_buf = panorama->buf;
      snapshot_swipe_state.panorama_size = panorama->size;
      snapshot_swipe_state.panorama_scale = panorama->scale;
      snapshot_swipe_state.panorama_next_x = next_x;
      snapshot_swipe_state.panorama_page_count = std::max(2, panorama->page_count);
      snapshot_swipe_state.panorama_source_page_index = source_page_index;
      snapshot_swipe_state.panorama_render = true;
      snapshot_cache_release_decoded_if_compressed(current);
      snapshot_cache_release_decoded_if_compressed(next);
      snapshot_swipe_state.current_buf = nullptr;
      snapshot_swipe_state.next_buf = nullptr;
    }
    snapshot_swipe_state.direct_render = true;
    s_snapshot_direct_active = true;
#ifdef USE_ESP32
    snapshot_swipe_state.worker_enabled = snapshot_swipe_worker_activate(0, next_x);
#endif
    lv_obj_add_flag(current, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(next, LV_OBJ_FLAG_HIDDEN);
    snapshot_swipe_discard_pending_refresh(component->get_disp());
    if (s_swipe_logging_enabled) {
      ESP_LOGI(TAG, "snapshot swipe: direct framebuffer compositor active (%s), next_x=%d",
               snapshot_swipe_state.panorama_render ? "RGB888 panorama" : "RGB888 strips", next_x);
    }
    return true;
  }

  snapshot_swipe_state.layer = lv_obj_create(parent);
  if (snapshot_swipe_state.layer == nullptr) {
    ESP_LOGW(TAG, "snapshot swipe: failed to create layer widget");
    snapshot_swipe_cleanup();
    return false;
  }
  lv_obj_remove_style_all(snapshot_swipe_state.layer);
  lv_obj_set_size(snapshot_swipe_state.layer, width * 3, width);
  lv_obj_align(snapshot_swipe_state.layer, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(snapshot_swipe_state.layer, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(snapshot_swipe_state.layer, LV_OPA_COVER, 0);
  lv_obj_clear_flag(snapshot_swipe_state.layer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(snapshot_swipe_state.layer, LV_OBJ_FLAG_CLICKABLE);

  snapshot_swipe_state.current_img = lv_image_create(snapshot_swipe_state.layer);
  snapshot_swipe_state.next_img = lv_image_create(snapshot_swipe_state.layer);
  if (snapshot_swipe_state.current_img == nullptr || snapshot_swipe_state.next_img == nullptr) {
    ESP_LOGW(TAG, "snapshot swipe: failed to create image widgets");
    snapshot_swipe_cleanup();
    return false;
  }

  lv_image_set_src(snapshot_swipe_state.current_img, snapshot_swipe_state.current_buf);
  lv_image_set_src(snapshot_swipe_state.next_img, snapshot_swipe_state.next_buf);
  lv_obj_set_size(snapshot_swipe_state.current_img, width, width);
  lv_obj_set_size(snapshot_swipe_state.next_img, width, width);
  lv_obj_add_flag(snapshot_swipe_state.current_img, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(snapshot_swipe_state.next_img, LV_OBJ_FLAG_CLICKABLE);
  snapshot_swipe_align(snapshot_swipe_state.current_img, 0);
  snapshot_swipe_align(snapshot_swipe_state.next_img, next_x);
  snapshot_swipe_align(snapshot_swipe_state.layer, 0);
  lv_obj_move_foreground(snapshot_swipe_state.layer);

  lv_obj_add_flag(current, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(next, LV_OBJ_FLAG_HIDDEN);
  ESP_LOGD(TAG, "snapshot swipe: active, next_x=%d", next_x);
  return true;
#else
  return false;
#endif
}

extern "C" bool lvgl_esphome_snapshot_swipe_edge_begin(lv_obj_t *current, int width) {
#if LV_USE_SNAPSHOT
  snapshot_swipe_complete_before_next_gesture();
  s_snapshot_swipe_active = true;
  if (current == nullptr || width <= 0)
    return false;

  auto *parent = lv_obj_get_parent(current);
  if (parent == nullptr)
    return false;
  auto *disp = lv_obj_get_display(current);
  auto *component = disp == nullptr ? nullptr : static_cast<LvglComponent *>(lv_display_get_user_data(disp));
  if (component == nullptr || !SNAPSHOT_DIRECT_COMPOSITOR_ENABLED)
    return false;

  lv_obj_clear_flag(current, LV_OBJ_FLAG_HIDDEN);
  lv_obj_align(current, LV_ALIGN_CENTER, 0, 0);
  lv_obj_update_layout(parent);

  SnapshotPanoramaPageSource panorama_source{};
  if (snapshot_panorama_source_from_cache(current, width, 1, &panorama_source) &&
      panorama_source.owner != nullptr && panorama_source.owner->buf != nullptr && panorama_source.width == width &&
      panorama_source.height >= component->get_height() && panorama_source.stride != 0) {
    const int height = component->get_height();
    const uint32_t panorama_width = static_cast<uint32_t>(panorama_source.stride / 3U);
    if (lv_draw_buf_init(&snapshot_swipe_edge_panorama_view, panorama_width, height, LV_COLOR_FORMAT_RGB888,
                         static_cast<uint32_t>(panorama_source.stride), panorama_source.owner->buf,
                         static_cast<uint32_t>(panorama_source.owner->size)) == LV_RESULT_OK) {
      snapshot_swipe_edge_panorama_view.data = const_cast<uint8_t *>(panorama_source.data);
      snapshot_swipe_edge_panorama_view.unaligned_data = const_cast<uint8_t *>(panorama_source.data);
      snapshot_swipe_edge_panorama_view.header.w = static_cast<uint32_t>(width);
      snapshot_swipe_edge_panorama_view.header.h = static_cast<uint32_t>(height);
      snapshot_swipe_edge_panorama_view.data_size = static_cast<uint32_t>(
          (static_cast<size_t>(height - 1) * panorama_source.stride) + static_cast<size_t>(width) * 3U);
      snapshot_swipe_state.current_buf = &snapshot_swipe_edge_panorama_view;
    }
  }
  if (snapshot_swipe_state.current_buf == nullptr)
    snapshot_swipe_state.current_buf = snapshot_cache_find(current);
  if (snapshot_swipe_state.current_buf == nullptr) {
    snapshot_swipe_state.current_buf = lv_snapshot_take(current, SNAPSHOT_CF);
    snapshot_swipe_state.owns_current_buf = true;
  }
  if (snapshot_swipe_state.current_buf == nullptr) {
    ESP_LOGW(TAG, "snapshot edge: failed to create draw buffer");
    snapshot_swipe_cleanup();
    return false;
  }

  snapshot_swipe_state.current_root = current;
  snapshot_swipe_state.width = width;
  snapshot_swipe_state.current_x = 0;
  snapshot_swipe_state.next_x = 0;
  snapshot_swipe_state.component = component;
  snapshot_swipe_state.direct_render = true;
  snapshot_swipe_state.edge_bounce = true;
  s_snapshot_direct_active = true;
#ifdef USE_ESP32
  snapshot_swipe_state.worker_enabled = snapshot_swipe_worker_activate(0, 0);
#endif
  lv_obj_add_flag(current, LV_OBJ_FLAG_HIDDEN);
  return true;
#else
  return false;
#endif
}

extern "C" void lvgl_esphome_snapshot_swipe_set_page_indicator(int page, int page_count) {
  s_snapshot_page_indicator_page = page;
  s_snapshot_page_indicator_count = page_count;
}

extern "C" void lvgl_esphome_snapshot_set_clock_text(const char *text) {
  if (text == nullptr || text[0] == '\0')
    return;
  std::strncpy(s_snapshot_clock_text, text, sizeof(s_snapshot_clock_text) - 1);
  s_snapshot_clock_text[sizeof(s_snapshot_clock_text) - 1] = '\0';
}

extern "C" void lvgl_esphome_snapshot_set_clock_font(const lv_font_t *font) { s_snapshot_clock_font = font; }

extern "C" void lvgl_esphome_snapshot_swipe_update(int current_x, int next_x) {
  if (snapshot_swipe_state.direct_render && snapshot_swipe_state.component != nullptr) {
    if (snapshot_swipe_render_direct_frame(current_x, next_x)) {
      snapshot_swipe_state.current_x = current_x;
      snapshot_swipe_state.next_x = next_x;
    }
    return;
  }
  if (snapshot_swipe_state.layer == nullptr)
    return;
  snapshot_swipe_align(snapshot_swipe_state.layer, current_x);
}

extern "C" void lvgl_esphome_snapshot_swipe_request_update(int current_x, int next_x) {
  auto &state = snapshot_swipe_state;
  if (!s_snapshot_swipe_active || state.pending_finish || state.direct_anim_active)
    return;
#ifdef USE_ESP32
  if (state.direct_render && state.worker_enabled) {
    snapshot_swipe_worker_queue_update(current_x, next_x);
    return;
  }
#endif
  state.pending_current_x = current_x;
  state.pending_next_x = next_x;
  state.pending_update = true;
}

extern "C" void lvgl_esphome_snapshot_swipe_finish(int current_x, int next_x, uint32_t duration_ms, bool commit) {
  if (snapshot_swipe_state.direct_render) {
    snapshot_swipe_state.finish_current_x = current_x;
    snapshot_swipe_state.finish_next_x = next_x;
    snapshot_swipe_state.commit = commit;
    if (duration_ms == 0) {
      snapshot_swipe_finish_now();
      return;
    }
    snapshot_swipe_direct_animate_to(current_x, next_x, duration_ms);
    return;
  }
  if (snapshot_swipe_state.current_img == nullptr || snapshot_swipe_state.next_img == nullptr) {
    snapshot_swipe_cleanup();
    return;
  }
  snapshot_swipe_state.finish_current_x = current_x;
  snapshot_swipe_state.finish_next_x = next_x;
  snapshot_swipe_state.commit = commit;
  if (duration_ms == 0) {
    snapshot_swipe_align(snapshot_swipe_state.layer, current_x);
    snapshot_swipe_finish_now();
    return;
  }

  lv_anim_t anim;
  lv_anim_init(&anim);
  lv_anim_set_exec_cb(&anim, snapshot_swipe_anim_x);
  lv_anim_set_duration(&anim, duration_ms);
  lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);

  lv_anim_set_var(&anim, snapshot_swipe_state.layer);
  lv_anim_set_values(&anim, lv_obj_get_x(snapshot_swipe_state.layer), current_x);
  lv_anim_set_completed_cb(&anim, snapshot_swipe_anim_completed_cb);
  lv_anim_start(&anim);
}

extern "C" void lvgl_esphome_snapshot_swipe_request_finish(int current_x, int next_x, uint32_t duration_ms,
                                                           bool commit) {
  auto &state = snapshot_swipe_state;
  if (!s_snapshot_swipe_active)
    return;
  state.pending_update = false;
  state.pending_finish_current_x = current_x;
  state.pending_finish_next_x = next_x;
  state.pending_finish_duration_ms = duration_ms;
  state.pending_finish_commit = commit;
  state.finish_current_x = current_x;
  state.finish_next_x = next_x;
  state.commit = commit;
#ifdef USE_ESP32
  if (state.direct_render && state.worker_enabled) {
    state.direct_anim_active = true;
    snapshot_swipe_worker_queue_finish(current_x, next_x, duration_ms);
    return;
  }
#endif
  state.pending_finish = true;
}

bool snapshot_swipe_process_pending() {
  auto &state = snapshot_swipe_state;
#ifdef USE_ESP32
  if (state.worker_enabled && snapshot_swipe_worker.finish_done.exchange(false, std::memory_order_acq_rel)) {
    state.current_x = snapshot_swipe_worker.rendered_current_x.load(std::memory_order_acquire);
    state.next_x = snapshot_swipe_worker.rendered_next_x.load(std::memory_order_acquire);
    state.direct_anim_active = false;
    snapshot_swipe_finish_now();
    return true;
  }
#endif
  if (state.pending_finish) {
    const int current_x = state.pending_finish_current_x;
    const int next_x = state.pending_finish_next_x;
    const uint32_t duration_ms = state.pending_finish_duration_ms;
    const bool commit = state.pending_finish_commit;
    state.pending_finish = false;
    lvgl_esphome_snapshot_swipe_finish(current_x, next_x, duration_ms, commit);
    return true;
  }
  if (state.pending_update) {
    const int current_x = state.pending_current_x;
    const int next_x = state.pending_next_x;
    state.pending_update = false;
    lvgl_esphome_snapshot_swipe_update(current_x, next_x);
    return true;
  }
  return false;
}

bool snapshot_scroll_process_pending() {
  auto &state = snapshot_scroll_state;
#ifdef USE_ESP32
  if (state.inertia_active && state.worker_enabled) {
    if (snapshot_scroll_worker.inertia_done.load(std::memory_order_acquire)) {
      const int final_y = state.inertia_final_y;
      snapshot_scroll_worker_stop_and_wait();
      state.worker_enabled = false;
      state.inertia_active = false;
      state.inertia_bounce_pending = false;
      state.current_scroll_y = final_y;
      lvgl_esphome_snapshot_scroll_finish_retain(final_y);
      return true;
    }
    if (snapshot_scroll_worker.inertia_requested.load(std::memory_order_acquire))
      return true;
    if (snapshot_scroll_worker.failed.load(std::memory_order_acquire)) {
      state.inertia_start_y = snapshot_scroll_worker.rendered_scroll_y.load(std::memory_order_acquire);
      state.inertia_start_us = static_cast<uint64_t>(millis()) * 1000ULL;
      snapshot_scroll_worker_stop_and_wait();
      state.worker_enabled = false;
    }
  }
#endif
  if (state.inertia_active && state.direct_render && state.component != nullptr && state.content_buf != nullptr) {
    const uint64_t now_us = static_cast<uint64_t>(millis()) * 1000ULL;
    const uint32_t elapsed_ms = static_cast<uint32_t>((now_us - state.inertia_start_us) / 1000ULL);
    if (elapsed_ms >= state.inertia_duration_ms) {
      if (state.inertia_bounce_pending) {
        state.inertia_start_y = state.inertia_target_y;
        state.inertia_target_y = state.inertia_final_y;
        state.inertia_duration_ms = 320;
        state.inertia_start_us = now_us;
        state.inertia_bounce_pending = false;
        state.current_scroll_y = state.inertia_start_y;
        return true;
      }
      state.inertia_active = false;
      lvgl_esphome_snapshot_scroll_finish_retain(state.inertia_final_y);
      return true;
    }

    const int scroll_y = snapshot_swipe_ease_out(state.inertia_start_y, state.inertia_target_y, elapsed_ms,
                                                  state.inertia_duration_ms);
    state.current_scroll_y = scroll_y;
#ifdef USE_ESP32
    if (state.worker_enabled) {
      snapshot_scroll_worker_queue_update(scroll_y);
      return true;
    }
#endif
    state.pending_scroll_y = scroll_y;
    state.pending_update = true;
  }
#ifdef USE_ESP32
  if (state.worker_enabled) {
    if (!snapshot_scroll_worker.failed.load(std::memory_order_acquire))
      return state.inertia_active;
    snapshot_scroll_worker_stop_and_wait();
    state.worker_enabled = false;
    state.pending_scroll_y = snapshot_scroll_worker.requested_scroll_y.load(std::memory_order_acquire);
    state.pending_update = true;
  }
#endif
  if (!state.pending_update || !state.direct_render || state.component == nullptr || state.content_buf == nullptr)
    return false;

  const int scroll_y = state.pending_scroll_y;
  state.pending_update = false;
  snapshot_scroll_render_direct_frame(scroll_y);
  return true;
}

void snapshot_scroll_log_summary() {
  const auto &state = snapshot_scroll_state;
  if (!s_swipe_logging_enabled || state.perf_render_frames == 0)
    return;
  const uint64_t elapsed_us = state.perf_last_render_us - state.perf_first_render_us;
  const uint32_t fps = elapsed_us == 0
                           ? 0
                           : static_cast<uint32_t>(
                                 static_cast<uint64_t>(state.perf_render_frames) * 1000000ULL / elapsed_us);
  ESP_LOGW(TAG, "snapshot scroll summary: frames=%u fps=%u avg=%lluus max=%uus failed=%u segments=%u",
           static_cast<unsigned>(state.perf_render_frames), static_cast<unsigned>(fps),
           static_cast<unsigned long long>(state.perf_total_render_us / state.perf_render_frames),
           static_cast<unsigned>(state.perf_max_render_us), static_cast<unsigned>(state.perf_failed_frames),
           state.content_tail_buf == nullptr ? 1U : 2U);
}

extern "C" void lvgl_esphome_snapshot_swipe_end(void) {
  snapshot_swipe_cleanup();
  lv_obj_invalidate(lv_screen_active());
}

extern "C" bool lvgl_esphome_snapshot_scroll_begin(lv_obj_t *obj, int viewport_w, int viewport_h) {
#if LV_USE_SNAPSHOT
  if (snapshot_scroll_state.root == obj && snapshot_scroll_state.content_buf != nullptr &&
      snapshot_scroll_state.viewport_w == viewport_w && snapshot_scroll_state.viewport_h == viewport_h &&
      !snapshot_scroll_state.direct_render) {
    snapshot_swipe_cleanup();
    snapshot_scroll_state.current_scroll_y = snapshot_scroll_clamp_y(lv_obj_get_scroll_y(obj));
    if (!snapshot_scroll_state.component->snapshot_scroll_direct_render(
            snapshot_scroll_state.content_buf, snapshot_scroll_state.content_tail_buf,
            snapshot_scroll_state.content_tail_y, snapshot_scroll_state.current_scroll_y, viewport_w, viewport_h)) {
      snapshot_scroll_cleanup();
      return false;
    }
    snapshot_scroll_state.direct_render = true;
    s_snapshot_direct_active = true;
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
#ifdef USE_ESP32
    snapshot_scroll_state.worker_enabled = snapshot_scroll_worker_activate(snapshot_scroll_state.current_scroll_y);
#endif
    return true;
  }

  const bool preserve_open_hold = s_snapshot_app_open_frame_held;
  snapshot_scroll_cleanup();
  snapshot_swipe_cleanup();
  if (preserve_open_hold)
    s_snapshot_direct_active = true;
  if (!snapshot_scroll_capture(obj, viewport_w, viewport_h, true))
    return false;
  if (s_swipe_logging_enabled) {
    ESP_LOGI(TAG, "snapshot scroll: begin y=%d max=%d content_h=%d", snapshot_scroll_state.current_scroll_y,
             snapshot_scroll_state.max_scroll_y, snapshot_scroll_state.content_h);
  }
  return true;
#else
  return false;
#endif
}

extern "C" bool lvgl_esphome_snapshot_scroll_prepare(lv_obj_t *obj, int viewport_w, int viewport_h) {
#if LV_USE_SNAPSHOT
  if (snapshot_scroll_state.root == obj && snapshot_scroll_state.content_buf != nullptr &&
      snapshot_scroll_state.viewport_w == viewport_w && snapshot_scroll_state.viewport_h == viewport_h) {
    return true;
  }
  const bool preserve_open_hold = s_snapshot_app_open_frame_held;
  snapshot_scroll_cleanup();
  snapshot_swipe_cleanup();
  if (preserve_open_hold)
    s_snapshot_direct_active = true;
  return snapshot_scroll_capture(obj, viewport_w, viewport_h, false);
#else
  return false;
#endif
}

extern "C" bool lvgl_esphome_snapshot_scroll_refresh(lv_obj_t *obj, int viewport_w, int viewport_h) {
#if LV_USE_SNAPSHOT
  auto &state = snapshot_scroll_state;
  if (obj == nullptr || state.root != obj || state.content_buf == nullptr || state.direct_render ||
      state.inertia_active || s_snapshot_swipe_active || snapshot_app_state.active)
    return false;

#ifdef USE_ESP32
  snapshot_scroll_worker_stop_and_wait();
#endif
  if (state.viewport_w != viewport_w || state.viewport_h != viewport_h ||
      state.content_buf->header.cf != SNAPSHOT_CF || state.content_buf->header.w < viewport_w)
    return false;

  auto *parent = lv_obj_get_parent(obj);
  const int old_scroll_y = lv_obj_get_scroll_y(obj);
  const int old_h = lv_obj_get_height(obj);
  const bool was_hidden = lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
  lv_obj_stop_scroll_anim(obj);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);

  auto refresh_segment = [&](int segment_y, lv_draw_buf_t *buffer) -> bool {
    if (buffer == nullptr)
      return true;
    lv_obj_set_height(obj, buffer->header.h);
    lv_obj_update_layout(parent == nullptr ? obj : parent);
    lv_obj_scroll_to_y(obj, segment_y, LV_ANIM_OFF);
    lv_obj_update_layout(parent == nullptr ? obj : parent);
    return lv_snapshot_take_to_draw_buf(obj, SNAPSHOT_CF, buffer) == LV_RESULT_OK;
  };

  const bool refreshed_head = refresh_segment(0, state.content_buf);
  const bool refreshed_tail =
      refreshed_head && refresh_segment(state.content_tail_y, state.content_tail_buf);

  lv_obj_set_height(obj, old_h);
  lv_obj_scroll_to_y(obj, old_scroll_y, LV_ANIM_OFF);
  lv_obj_update_layout(parent == nullptr ? obj : parent);
  if (was_hidden)
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);

  if (!refreshed_head || !refreshed_tail)
    return false;
#if defined(USE_ESP32)
  const size_t head_size = state.content_buf->data_size != 0
                               ? state.content_buf->data_size
                               : static_cast<size_t>(state.content_buf->header.stride) * state.content_buf->header.h;
  lvgl_cache_msync_external(state.content_buf->data, head_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  if (state.content_tail_buf != nullptr) {
    const size_t tail_size =
        state.content_tail_buf->data_size != 0
            ? state.content_tail_buf->data_size
            : static_cast<size_t>(state.content_tail_buf->header.stride) * state.content_tail_buf->header.h;
    lvgl_cache_msync_external(state.content_tail_buf->data, tail_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  }
#endif
  state.current_scroll_y = snapshot_scroll_clamp_y(old_scroll_y);
  if (s_swipe_logging_enabled) {
    ESP_LOGI(TAG, "snapshot scroll: refreshed y=%d max=%d segments=%u", state.current_scroll_y,
             state.max_scroll_y, state.content_tail_buf == nullptr ? 1U : 2U);
  }
  return true;
#else
  (void) obj;
  (void) viewport_w;
  (void) viewport_h;
  return false;
#endif
}

extern "C" void lvgl_esphome_snapshot_scroll_update(int scroll_y) {
  if (!snapshot_scroll_state.direct_render || snapshot_scroll_state.component == nullptr ||
      snapshot_scroll_state.content_buf == nullptr)
    return;
  const int clamped_y = snapshot_scroll_resist_y(scroll_y);
#ifdef USE_ESP32
  if (snapshot_scroll_state.worker_enabled) {
    snapshot_scroll_worker_queue_update(clamped_y);
    return;
  }
#endif
  snapshot_scroll_state.pending_scroll_y = clamped_y;
  snapshot_scroll_state.pending_update = true;
}

extern "C" void lvgl_esphome_snapshot_scroll_finish(int scroll_y) {
  if (snapshot_scroll_state.root == nullptr) {
    snapshot_scroll_cleanup();
    return;
  }
  const int clamped_y = snapshot_scroll_clamp_y(scroll_y);
  snapshot_scroll_state.pending_update = false;
  bool rendered_by_worker = false;
#ifdef USE_ESP32
  if (snapshot_scroll_state.worker_enabled) {
    const uint32_t generation = snapshot_scroll_worker_queue_update(clamped_y);
    rendered_by_worker = snapshot_scroll_worker_wait_for(generation, 160);
    snapshot_scroll_worker_stop_and_wait();
    snapshot_scroll_state.worker_enabled = false;
  }
#endif
  if (snapshot_scroll_state.direct_render && snapshot_scroll_state.component != nullptr &&
      snapshot_scroll_state.content_buf != nullptr && !rendered_by_worker) {
    snapshot_scroll_render_direct_frame(clamped_y);
  }
  if (snapshot_scroll_state.direct_render && snapshot_scroll_state.component != nullptr &&
      snapshot_scroll_state.content_buf != nullptr) {
    snapshot_scroll_state.component->wait_for_direct_frame_presented(50);
    snapshot_scroll_state.component->realign_direct_buffer_after_manual_present(false);
  }

  lv_obj_t *root = snapshot_scroll_state.root;
  lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN);
  lv_obj_scroll_to_y(root, clamped_y, LV_ANIM_OFF);
  snapshot_scroll_log_summary();
  snapshot_scroll_cleanup();
  lv_obj_invalidate(lv_screen_active());
  if (s_swipe_logging_enabled) {
    ESP_LOGI(TAG, "snapshot scroll: finish y=%d", clamped_y);
  }
}

extern "C" void lvgl_esphome_snapshot_scroll_finish_retain(int scroll_y) {
  if (snapshot_scroll_state.root == nullptr) {
    snapshot_scroll_cleanup();
    return;
  }
  const int clamped_y = snapshot_scroll_clamp_y(scroll_y);
  snapshot_scroll_state.pending_update = false;
  bool rendered_by_worker = false;
#ifdef USE_ESP32
  if (snapshot_scroll_state.worker_enabled) {
    const uint32_t generation = snapshot_scroll_worker_queue_update(clamped_y);
    rendered_by_worker = snapshot_scroll_worker_wait_for(generation, 160);
    snapshot_scroll_worker_stop_and_wait();
    snapshot_scroll_state.worker_enabled = false;
  }
#endif
  if (snapshot_scroll_state.direct_render && snapshot_scroll_state.component != nullptr &&
      snapshot_scroll_state.content_buf != nullptr && !rendered_by_worker) {
    snapshot_scroll_render_direct_frame(clamped_y);
  }
  if (snapshot_scroll_state.direct_render && snapshot_scroll_state.component != nullptr &&
      snapshot_scroll_state.content_buf != nullptr) {
    snapshot_scroll_state.component->wait_for_direct_frame_presented(50);
    snapshot_scroll_state.component->realign_direct_buffer_after_manual_present(false);
  }

  lv_obj_t *root = snapshot_scroll_state.root;
  lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN);
  lv_obj_scroll_to_y(root, clamped_y, LV_ANIM_OFF);
  snapshot_scroll_state.current_scroll_y = clamped_y;
  snapshot_scroll_state.direct_render = false;
  s_snapshot_direct_active = false;
  snapshot_scroll_log_summary();
  lv_obj_invalidate(root);
  if (s_swipe_logging_enabled) {
    ESP_LOGI(TAG, "snapshot scroll: finish retain y=%d", clamped_y);
  }
}

extern "C" void lvgl_esphome_snapshot_scroll_finish_inertial(int scroll_y, int velocity_px_s) {
  if (snapshot_scroll_state.root == nullptr || !snapshot_scroll_state.direct_render) {
    lvgl_esphome_snapshot_scroll_finish_retain(scroll_y);
    return;
  }

  auto &state = snapshot_scroll_state;
  const int max_y = std::max(0, state.max_scroll_y);
  const int start_y = snapshot_scroll_resist_y(scroll_y);
  const int clamped_velocity = std::clamp(velocity_px_s, -4200, 4200);
  // Keep enough momentum for a watch-style list flick. The compositor runs
  // independently from LVGL, so the longer coast adds no live-tree redraws.
  const int travel = std::clamp((clamped_velocity * 560) / 1000, -1200, 1200);
  const int raw_target_y = start_y + travel;
  const int final_y = std::clamp(raw_target_y, 0, max_y);
  const bool bounce = start_y < 0 || start_y > max_y || raw_target_y < 0 || raw_target_y > max_y;
  const int target_y = bounce ? std::clamp(raw_target_y, -140, max_y + 140) : final_y;
  const int distance = std::abs(target_y - start_y);
  if (!bounce && (distance < 8 || std::abs(clamped_velocity) < 80)) {
    lvgl_esphome_snapshot_scroll_finish_retain(final_y);
    return;
  }

  state.pending_update = false;
  state.current_scroll_y = start_y;
  state.inertia_start_y = start_y;
  state.inertia_target_y = target_y;
  state.inertia_final_y = final_y;
  state.inertia_bounce_pending = bounce && target_y != final_y;
  state.inertia_duration_ms =
      std::clamp<uint32_t>(320U + (static_cast<uint32_t>(distance) * 11U) / 20U, 320U, 900U);
  state.inertia_start_us = static_cast<uint64_t>(millis()) * 1000ULL;
  state.inertia_active = true;
#ifdef USE_ESP32
  if (state.worker_enabled &&
      snapshot_scroll_worker_start_inertia(start_y, target_y, final_y, state.inertia_duration_ms, 320U)) {
    return;
  }
#endif
}

extern "C" void lvgl_esphome_snapshot_scroll_end(void) {
  snapshot_scroll_cleanup();
  lv_obj_invalidate(lv_screen_active());
}

void LvglComponent::static_flush_cb(lv_display_t *disp_drv, const lv_area_t *area, uint8_t *color_p) {
  reinterpret_cast<LvglComponent *>(lv_display_get_user_data(disp_drv))->flush_cb_(disp_drv, area, color_p);
}

#if LV_USE_SCALE
void lv_scale_draw_event_cb(lv_event_t *e, int32_t range_start, int32_t range_end, lv_color_t color_start,
                            lv_color_t color_end, int width, bool local) {
  auto *scale = static_cast<lv_obj_t *>(lv_event_get_target(e));
  lv_draw_task_t *task = lv_event_get_draw_task(e);

  if (lv_draw_task_get_type(task) == LV_DRAW_TASK_TYPE_LINE) {
    auto *line_dsc = static_cast<lv_draw_line_dsc_t *>(lv_draw_task_get_draw_dsc(task));
    int32_t tick_value = line_dsc->base.id2;
    if (tick_value >= range_start && tick_value <= range_end) {
      int ratio;
      if (local) {
        int32_t range = range_end - range_start;
        ratio = range == 0 ? 0 : ((tick_value - range_start) * 255) / range;
      } else {
        auto tick_count = lv_scale_get_total_tick_count(scale);
        ratio = tick_count <= 1 ? 0 : (line_dsc->base.id1 * 255) / (tick_count - 1);
      }
      line_dsc->color = lv_color_mix(color_end, color_start, ratio);
      line_dsc->width += width;
    }
  }
}

void lv_scale_tick_offset_event_cb(lv_event_t *e, uint16_t offset, uint16_t stride) {
  auto *scale = static_cast<lv_obj_t *>(lv_event_get_target(e));
  lv_draw_task_t *task = lv_event_get_draw_task(e);
  auto type = lv_draw_task_get_type(task);

  if (type == LV_DRAW_TASK_TYPE_LINE) {
    auto *line_dsc = static_cast<lv_draw_line_dsc_t *>(lv_draw_task_get_draw_dsc(task));
    auto tick_idx = line_dsc->base.id1;

    bool is_major = (tick_idx >= offset) && ((tick_idx - offset) % stride == 0);

    if (!is_major) {
      line_dsc->color = lv_obj_get_style_line_color(scale, LV_PART_ITEMS);
      line_dsc->width = lv_obj_get_style_line_width(scale, LV_PART_ITEMS);

      int32_t minor_len = lv_obj_get_style_length(scale, LV_PART_ITEMS);
      int32_t major_len = lv_obj_get_style_length(scale, LV_PART_INDICATOR);
      if (major_len > 0 && minor_len > 0 && minor_len != major_len) {
        auto dx = line_dsc->p1.x - line_dsc->p2.x;
        auto dy = line_dsc->p1.y - line_dsc->p2.y;
        line_dsc->p1.x = line_dsc->p2.x + dx * minor_len / major_len;
        line_dsc->p1.y = line_dsc->p2.y + dy * minor_len / major_len;
      }
    }
  } else if (type == LV_DRAW_TASK_TYPE_LABEL) {
    auto *label_dsc = static_cast<lv_draw_label_dsc_t *>(lv_draw_task_get_draw_dsc(task));
    auto tick_idx = label_dsc->base.id1;

    bool is_major = (tick_idx >= offset) && ((tick_idx - offset) % stride == 0);

    if (!is_major) {
      label_dsc->opa = LV_OPA_TRANSP;
    }
  }
}
#endif  // LV_USE_SCALE

#ifdef USE_LVGL_GRADIENT
/**
 *
 * @param dsc The gradient descriptor containing the color stops
 * @param pos The current position to calculate the color for
 * @return The color for the given position
 */

lv_color_t lv_grad_calculate_color(const lv_grad_dsc_t *dsc, int32_t pos) {
  if (dsc->stops_count == 0)
    return lv_color_black();
  if (dsc->stops_count == 1 || pos <= dsc->stops[0].frac)
    return dsc->stops[0].color;
  if (pos >= dsc->stops[dsc->stops_count - 1].frac)
    return dsc->stops[dsc->stops_count - 1].color;
  int i = 1;
  while (i < dsc->stops_count && dsc->stops[i].frac < pos)
    i++;
  auto *stop1 = &dsc->stops[i - 1];
  auto *stop2 = &dsc->stops[i];
  int32_t range = stop2->frac - stop1->frac;
  int32_t offset = pos - stop1->frac;
  return lv_color_mix(stop2->color, stop1->color, range == 0 ? 0 : (offset * 255) / range);
}
#endif  // USE_LVGL_GRADIENT

lv_point_t LvglComponent::get_touch_relative_to_obj(lv_obj_t *obj) {
  auto *indev = lv_indev_get_act();
  if (indev == nullptr) {
    return {INT32_MAX, INT32_MAX};
  }
  lv_point_t point;
  lv_indev_get_point(indev, &point);
  lv_area_t coords;
  lv_obj_get_coords(obj, &coords);
  point.x -= coords.x1;
  point.y -= coords.y1;
  return point;
}

static void lv_container_constructor(const lv_obj_class_t *class_p, lv_obj_t *obj) {
  LV_TRACE_OBJ_CREATE("begin");
  LV_UNUSED(class_p);
}

// Container class. Name is based on LVGL naming convention but upper case to keep ESPHome clang-tidy happy
const lv_obj_class_t LV_CONTAINER_CLASS = {
    .base_class = &lv_obj_class,
    .constructor_cb = lv_container_constructor,
    .name = "lv_container",
};

lv_obj_t *lv_container_create(lv_obj_t *parent) {
  lv_obj_t *obj = lv_obj_class_create_obj(&LV_CONTAINER_CLASS, parent);
  lv_obj_class_init_obj(obj);
  return obj;
}

}  // namespace esphome::lvgl

lv_result_t lv_mem_test_core() { return LV_RESULT_OK; }

void lv_mem_init() {}

void lv_mem_deinit() {}

#if defined(USE_HOST) || defined(USE_RP2040) || defined(USE_ESP8266)
// Memory alignment for draw buffers on non-ESP32 platforms.
// We use 64-byte alignment for optimal performance even though LV_DRAW_BUF_ALIGN
// is set to 4 (to avoid warnings from LVGL's internal stack/static buffers).
// Standard malloc() only guarantees 8-16 byte alignment, so we implement
// our own aligned allocation.
static constexpr size_t LVGL_ALIGNMENT = 64;

// Store original pointer before aligned address for proper freeing
void *lv_malloc_core(size_t size) {
  if (size == 0)
    return nullptr;

  // Allocate extra space for alignment and to store original pointer
  size_t total_size = size + LVGL_ALIGNMENT + sizeof(void *);
  void *raw = malloc(total_size);  // NOLINT
  if (raw == nullptr) {
    ESP_LOGE(esphome::lvgl::TAG, "Failed to allocate %zu bytes", size);
    return nullptr;
  }

  // Calculate aligned pointer (leaving space for original pointer storage)
  uintptr_t raw_addr = reinterpret_cast<uintptr_t>(raw);
  uintptr_t aligned_addr = (raw_addr + sizeof(void *) + LVGL_ALIGNMENT - 1) & ~(LVGL_ALIGNMENT - 1);
  void *aligned = reinterpret_cast<void *>(aligned_addr);

  // Store original pointer just before aligned address
  reinterpret_cast<void **>(aligned)[-1] = raw;

  return aligned;
}

void lv_free_core(void *ptr) {
  if (ptr == nullptr)
    return;
  // Retrieve and free the original pointer
  void *raw = reinterpret_cast<void **>(ptr)[-1];
  free(raw);  // NOLINT
}

void *lv_realloc_core(void *ptr, size_t size) {
  if (ptr == nullptr)
    return lv_malloc_core(size);
  if (size == 0) {
    lv_free_core(ptr);
    return nullptr;
  }

  // Allocate new aligned buffer and copy data
  void *new_ptr = lv_malloc_core(size);
  if (new_ptr == nullptr)
    return nullptr;

    // We don't know the old size exactly, so copy min(new_size, old_usable_size).
    // On most platforms, malloc_usable_size() returns the actual allocated size.
    // Fall back to new size if unavailable (safe: reads at most what was allocated).
#if defined(__GLIBC__) || defined(__ANDROID__)
  size_t old_size = malloc_usable_size(reinterpret_cast<void **>(ptr)[-1]);
  // Subtract alignment overhead to get usable size from aligned pointer
  size_t overhead = reinterpret_cast<uintptr_t>(ptr) - reinterpret_cast<uintptr_t>(reinterpret_cast<void **>(ptr)[-1]);
  old_size = (old_size > overhead) ? old_size - overhead : 0;
#else
  size_t old_size = size;  // conservative fallback: may read less than available
#endif
  memcpy(new_ptr, ptr, (size < old_size) ? size : old_size);
  lv_free_core(ptr);

  return new_ptr;
}

void lv_mem_monitor_core(lv_mem_monitor_t *mon_p) { memset(mon_p, 0, sizeof(lv_mem_monitor_t)); }

#endif
#ifdef USE_ESP32
static unsigned cap_bits = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;  // NOLINT

void lv_mem_monitor_core(lv_mem_monitor_t *mon_p) {
  multi_heap_info_t heap_info;
  heap_caps_get_info(&heap_info, cap_bits);
  mon_p->total_size = heap_info.total_allocated_bytes + heap_info.total_free_bytes;
  mon_p->free_size = heap_info.total_free_bytes;
  mon_p->max_used = heap_info.total_allocated_bytes;
  mon_p->free_biggest_size = heap_info.largest_free_block;
  mon_p->used_cnt = heap_info.allocated_blocks;
  mon_p->free_cnt = heap_info.free_blocks;
  mon_p->used_pct = heap_info.allocated_blocks * 100 / (heap_info.allocated_blocks + heap_info.free_blocks);
  mon_p->frag_pct = 0;
}

void *lv_malloc_core(size_t size) {
  // Use 64-byte alignment for optimal ESP32 PSRAM/cache performance.
  // Note: LV_DRAW_BUF_ALIGN is set to 4 to avoid LVGL warnings from
  // internal stack/static buffers, but heap allocations use 64-byte alignment.
  constexpr size_t LVGL_ALIGNMENT = 64;
  constexpr size_t LVGL_INTERNAL_MAX_ALLOCATION = 4 * 1024;
  constexpr size_t LVGL_INTERNAL_HEADROOM = 96 * 1024;
  const size_t aligned_size = (size + LVGL_ALIGNMENT - 1) & ~(LVGL_ALIGNMENT - 1);

  // Keep small, frequently accessed LVGL objects in internal SRAM. Large
  // image and draw buffers remain in PSRAM, while the headroom protects audio,
  // networking, and interrupt-time allocations from SRAM starvation.
  const size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const bool prefer_internal =
      aligned_size <= LVGL_INTERNAL_MAX_ALLOCATION && largest_internal >= aligned_size + LVGL_INTERNAL_HEADROOM;
  void *ptr = nullptr;
  if (prefer_internal) {
    ptr = heap_caps_aligned_alloc(LVGL_ALIGNMENT, aligned_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  if (ptr == nullptr) {
    ptr = heap_caps_aligned_alloc(LVGL_ALIGNMENT, aligned_size, cap_bits);
  }
  if (ptr == nullptr) {
    ptr = heap_caps_aligned_alloc(LVGL_ALIGNMENT, aligned_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }

  if (ptr == nullptr) {
    ESP_LOGE(esphome::lvgl::TAG, "Failed to allocate %zu bytes (%zu aligned)", size, aligned_size);
    return nullptr;
  }

  // Log very large buffers only when snapshot diagnostics are explicitly enabled.
  if (esphome::lvgl::s_swipe_logging_enabled && size > 1000000) {
    ESP_LOGI(esphome::lvgl::TAG, "Large buffer allocated: %zu bytes at %p", size, ptr);
  }

  return ptr;
}

void lv_free_core(void *ptr) {
  ESP_LOGV(esphome::lvgl::TAG, "free %p", ptr);
  if (ptr == nullptr)
    return;
  heap_caps_free(ptr);
}

void *lv_realloc_core(void *ptr, size_t size) {
  ESP_LOGV(esphome::lvgl::TAG, "realloc %p: %zu", ptr, size);

  if (ptr == nullptr)
    return lv_malloc_core(size);
  if (size == 0) {
    lv_free_core(ptr);
    return nullptr;
  }

  // CRITICAL: heap_caps_realloc does NOT preserve 64-byte alignment!
  // We must allocate a new aligned buffer and copy the data
  void *new_ptr = lv_malloc_core(size);
  if (new_ptr == nullptr)
    return nullptr;

  // Copy data to new buffer using heap_caps_get_allocated_size for safe bounds
  size_t old_size = heap_caps_get_allocated_size(ptr);
  memcpy(new_ptr, ptr, (size < old_size) ? size : old_size);
  lv_free_core(ptr);

  return new_ptr;
}
#endif
