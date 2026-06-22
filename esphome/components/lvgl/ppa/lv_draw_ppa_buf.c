/**
 * @file lv_draw_ppa_buf.c
 * Fixed PPA buffer cache handling for LVGL 9.4 on ESP32-P4
 * Backported from https://github.com/lvgl/lvgl/pull/9162
 * Adapted for C++ compilation (ESPHome build system)
 *
 * NOTE: We do NOT set the global invalidate_cache_cb handler because
 * it would affect ALL draw operations (software renderer included).
 * Cache sync is done per-operation in PPA dispatch, and only for
 * buffers in external (PSRAM) memory which is cached.
 */

#include "sdkconfig.h"
#ifdef CONFIG_SOC_PPA_SUPPORTED

#include "lv_draw_ppa_private.h"
#include "lv_draw_ppa.h"
#include "esp_memory_utils.h"

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
void lv_draw_buf_ppa_init_handlers(void)
{
    /* Intentionally empty - see file header comment */
}

static inline size_t lv_draw_ppa_cache_sync_size(lv_draw_buf_t * buf)
{
    size_t data_size = (size_t)buf->data_size;
    uint32_t px_size = lv_color_format_get_size((lv_color_format_t)buf->header.cf);
    if(px_size == 0 || buf->header.w == 0 || buf->header.h == 0) {
        return data_size;
    }

    size_t stride = buf->header.stride ? (size_t)buf->header.stride : ((size_t)buf->header.w * px_size);
    size_t stride_size = stride * (size_t)buf->header.h;
    if(stride_size > data_size) data_size = stride_size;

    return data_size;
}

static inline void lv_draw_ppa_cache_sync_buf(lv_draw_buf_t * buf, int flags)
{
    if(buf == NULL || buf->data == NULL || buf->data_size == 0) return;

    /* Only sync if buffer is in external (PSRAM) memory, which is cached.
     * Internal SRAM is not cached on ESP32-P4, so esp_cache_msync would
     * either be a no-op or could crash on non-cacheable regions. */
    if(!esp_ptr_external_ram(buf->data)) return;

    size_t data_size = lv_draw_ppa_cache_sync_size(buf);
    lv_draw_ppa_cache_msync(buf->data, data_size, flags);
}

static inline void lv_draw_ppa_cache_sync_area(lv_draw_buf_t * buf, const lv_area_t * buf_area,
                                               const lv_area_t * area, int flags)
{
    if(buf == NULL || buf_area == NULL || area == NULL || buf->data == NULL || buf->data_size == 0) return;
    if(!esp_ptr_external_ram(buf->data)) return;

    uint32_t px_size = lv_color_format_get_size((lv_color_format_t)buf->header.cf);
    if(px_size == 0 || buf->header.w == 0 || buf->header.h == 0) return;

    lv_area_t clipped;
    if(!lv_area_intersect(&clipped, area, buf_area)) return;

    int32_t width = lv_area_get_width(&clipped);
    int32_t height = lv_area_get_height(&clipped);
    if(width <= 0 || height <= 0) return;

    int32_t off_x = clipped.x1 - buf_area->x1;
    int32_t off_y = clipped.y1 - buf_area->y1;
    if(off_x < 0 || off_y < 0) return;

    size_t stride = buf->header.stride ? (size_t)buf->header.stride : ((size_t)buf->header.w * px_size);
    if(stride < ((size_t)buf->header.w * px_size) || (stride % px_size) != 0) return;

    size_t data_size = lv_draw_ppa_cache_sync_size(buf);
    uint8_t * data = (uint8_t *)buf->data;
    size_t row_bytes = (size_t)width * px_size;
    size_t x_offset = (size_t)off_x * px_size;

    for(int32_t y = 0; y < height; y++) {
        size_t offset = ((size_t)off_y + (size_t)y) * stride + x_offset;
        if(offset >= data_size) break;
        size_t bytes = row_bytes;
        if(offset + bytes > data_size) bytes = data_size - offset;
        lv_draw_ppa_cache_msync(data + offset, bytes, flags);
    }
}

void lv_draw_ppa_cache_sync_to_memory(lv_draw_buf_t * buf)
{
    lv_draw_ppa_cache_sync_buf(buf, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
}

void lv_draw_ppa_cache_sync_from_memory(lv_draw_buf_t * buf)
{
    lv_draw_ppa_cache_sync_buf(buf, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
}

void lv_draw_ppa_cache_sync_area_to_memory(lv_draw_buf_t * buf, const lv_area_t * buf_area,
                                           const lv_area_t * area)
{
    lv_draw_ppa_cache_sync_area(buf, buf_area, area, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
}

void lv_draw_ppa_cache_sync_area_from_memory(lv_draw_buf_t * buf, const lv_area_t * buf_area,
                                             const lv_area_t * area)
{
    lv_draw_ppa_cache_sync_area(buf, buf_area, area, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
}

#endif /* CONFIG_SOC_PPA_SUPPORTED */
