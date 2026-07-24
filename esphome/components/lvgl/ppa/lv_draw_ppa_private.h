/**
 * @file lv_draw_ppa_private.h
 * Custom PPA private header for ESP32-P4
 * Based on https://github.com/lvgl/lvgl/pull/9162 (included in LVGL 9.5+)
 * Adapted for C++ compilation (ESPHome build system)
 */

#pragma once
// namespace esphome::lvgl -- ESPHome lint marker; this header is shared with C sources.

#ifndef LV_DRAW_PPA_PRIVATE_FIXED_H
#define LV_DRAW_PPA_PRIVATE_FIXED_H

/*********************
*      INCLUDES
*********************/
#include "lvgl.h"
#include "src/lv_conf_internal.h"
#include "src/draw/lv_draw_private.h"
#include "src/draw/lv_draw_buf_private.h"
#include "src/display/lv_display_private.h"
#include "src/misc/lv_area_private.h"

/* The ppa driver depends heavily on the esp-idf headers */
#include "sdkconfig.h"

#ifndef CONFIG_SOC_PPA_SUPPORTED
#error "This SoC does not support PPA"
#endif

#include "driver/ppa.h"
#include "esp_heap_caps.h"
#include "esp_err.h"
#include "hal/color_hal.h"
#include "esp_cache.h"
#include "esp_private/esp_cache_private.h"
#include "esp_log.h"
#include "esp_memory_utils.h"

/*********************
*      DEFINES
*********************/

#ifdef CONFIG_CACHE_L2_CACHE_LINE_SIZE
#define PPA_CACHE_LINE_SIZE  ((uint32_t)CONFIG_CACHE_L2_CACHE_LINE_SIZE)
#else
#define PPA_CACHE_LINE_SIZE  64U
#endif

#ifndef LV_PPA_BURST_LENGTH
#define LV_PPA_BURST_LENGTH (128)
#endif

#if LV_PPA_BURST_LENGTH == 128
#define LV_DRAW_PPA_DATA_BURST_LENGTH PPA_DATA_BURST_LENGTH_128
#elif LV_PPA_BURST_LENGTH == 64
#define LV_DRAW_PPA_DATA_BURST_LENGTH PPA_DATA_BURST_LENGTH_64
#elif LV_PPA_BURST_LENGTH == 32
#define LV_DRAW_PPA_DATA_BURST_LENGTH PPA_DATA_BURST_LENGTH_32
#elif LV_PPA_BURST_LENGTH == 16
#define LV_DRAW_PPA_DATA_BURST_LENGTH PPA_DATA_BURST_LENGTH_16
#elif LV_PPA_BURST_LENGTH == 8
#define LV_DRAW_PPA_DATA_BURST_LENGTH PPA_DATA_BURST_LENGTH_8
#else
#error "LV_PPA_BURST_LENGTH must be 8, 16, 32, 64 or 128"
#endif

#ifndef CONFIG_ESPHOME_LVGL_PPA_SRM_BURST_LENGTH
#define CONFIG_ESPHOME_LVGL_PPA_SRM_BURST_LENGTH LV_PPA_BURST_LENGTH
#endif

#if CONFIG_ESPHOME_LVGL_PPA_SRM_BURST_LENGTH == 128
#define LV_DRAW_PPA_SRM_DATA_BURST_LENGTH PPA_DATA_BURST_LENGTH_128
#elif CONFIG_ESPHOME_LVGL_PPA_SRM_BURST_LENGTH == 64
#define LV_DRAW_PPA_SRM_DATA_BURST_LENGTH PPA_DATA_BURST_LENGTH_64
#elif CONFIG_ESPHOME_LVGL_PPA_SRM_BURST_LENGTH == 32
#define LV_DRAW_PPA_SRM_DATA_BURST_LENGTH PPA_DATA_BURST_LENGTH_32
#elif CONFIG_ESPHOME_LVGL_PPA_SRM_BURST_LENGTH == 16
#define LV_DRAW_PPA_SRM_DATA_BURST_LENGTH PPA_DATA_BURST_LENGTH_16
#elif CONFIG_ESPHOME_LVGL_PPA_SRM_BURST_LENGTH == 8
#define LV_DRAW_PPA_SRM_DATA_BURST_LENGTH PPA_DATA_BURST_LENGTH_8
#else
#error "CONFIG_ESPHOME_LVGL_PPA_SRM_BURST_LENGTH must be 8, 16, 32, 64 or 128"
#endif

/**********************
*      TYPEDEFS
**********************/

/**
 * Round a byte size up to LV_DRAW_BUF_ALIGN (cache-line on ESP32-P4).
 * ESP-IDF PPA hardware and esp_cache_msync() both require sizes aligned to
 * the cache line — mirrors what esp_lvgl_port (common/ppa/lcd_ppa.c) does
 * via ALIGN_UP(size, CONFIG_CACHE_L2_CACHE_LINE_SIZE).
 */
static inline uint32_t lv_draw_ppa_cache_align(void)
{
    size_t alignment = 0;
    esp_err_t err = esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &alignment);
    if (err != ESP_OK || alignment == 0 || (alignment & (alignment - 1U)) != 0) {
        alignment = 64;
    }
    return (uint32_t) alignment;
}

static inline uint32_t lv_draw_ppa_align_size(uint32_t size)
{
    uint32_t alignment = lv_draw_ppa_cache_align();
    return (size + alignment - 1U) & ~(alignment - 1U);
}

/**
 * Return a cache-aligned PPA output size without exceeding the draw buffer.
 *
 * LVGL can expose partial draw buffers whose geometry describes a larger
 * logical surface than data_size.  PPA treats buffer_size as accessible
 * memory, so deriving it only from stride * height can allow DMA past the
 * allocation.  data_size is the authoritative bound.
 */
static inline bool lv_draw_ppa_get_output_buffer_size(const lv_draw_buf_t * buf,
                                                      size_t required_size,
                                                      uint32_t * output_size)
{
    if(buf == NULL || output_size == NULL || required_size == 0 ||
       required_size > (size_t)buf->data_size || required_size > UINT32_MAX) {
        return false;
    }

    uint32_t aligned_size = lv_draw_ppa_align_size((uint32_t)required_size);
    if(aligned_size < required_size || aligned_size > buf->data_size) {
        return false;
    }

    *output_size = aligned_size;
    return true;
}

static inline bool lv_draw_ppa_buf_cache_aligned(const void * p)
{
    return ((uintptr_t)p % lv_draw_ppa_cache_align()) == 0;
}

static inline void lv_draw_ppa_cache_msync(const void * p, uint32_t size, int flags)
{
    if(p == NULL || size == 0 || !esp_ptr_external_ram(p)) {
        return;
    }

    uint32_t alignment = lv_draw_ppa_cache_align();
    uintptr_t start = (uintptr_t)p;
    uintptr_t aligned_start = start & ~((uintptr_t)alignment - 1U);
    uintptr_t aligned_end = (start + size + alignment - 1U) & ~((uintptr_t)alignment - 1U);
    if(aligned_end <= aligned_start) {
        return;
    }
    if(!esp_ptr_external_ram((const void *)aligned_start) ||
       !esp_ptr_external_ram((const void *)(aligned_end - 1U))) {
        return;
    }

    int sync_flags = flags | ESP_CACHE_MSYNC_FLAG_TYPE_DATA;
    if((sync_flags & (ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE)) != 0) {
        esp_err_t err = esp_cache_msync((void *)aligned_start, aligned_end - aligned_start,
                                        ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);
        if(err != ESP_OK) {
            return;
        }
    }

    esp_cache_msync((void *)aligned_start, aligned_end - aligned_start,
                    sync_flags);
}

static inline void lv_draw_ppa_cache_msync_after_dma_write(const void * p, uint32_t size)
{
    if(p == NULL || size == 0 || !esp_ptr_external_ram(p)) {
        return;
    }

    uint32_t alignment = lv_draw_ppa_cache_align();
    uintptr_t start = (uintptr_t)p;
    uintptr_t aligned_start = start & ~((uintptr_t)alignment - 1U);
    uintptr_t aligned_end = (start + size + alignment - 1U) & ~((uintptr_t)alignment - 1U);
    if(aligned_end <= aligned_start) {
        return;
    }
    if(!esp_ptr_external_ram((const void *)aligned_start) ||
       !esp_ptr_external_ram((const void *)(aligned_end - 1U))) {
        return;
    }

    /* PPA/DMA wrote memory directly.  Do not write back CPU cache here:
     * doing so after the transfer can restore stale cache lines over the
     * fresh DMA result.  The draw unit already performs C2M before handing
     * a destination window to PPA, so a plain M2C invalidation is the safe
     * post-DMA operation.
     */
    esp_cache_msync((void *)aligned_start, aligned_end - aligned_start,
                    ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);
}

typedef struct lv_draw_ppa_unit {
    lv_draw_unit_t base_unit;
    lv_draw_task_t * task_act;
    ppa_client_handle_t srm_client;
    ppa_client_handle_t fill_client;
    ppa_client_handle_t blend_client;
    uint8_t * buf;
} lv_draw_ppa_unit_t;

/**********************
*   STATIC FUNCTIONS
**********************/

static inline bool ppa_src_cf_supported(lv_color_format_t cf)
{
    switch(cf) {
        case LV_COLOR_FORMAT_RGB565:
        case LV_COLOR_FORMAT_RGB888:
        case LV_COLOR_FORMAT_ARGB8888:
        case LV_COLOR_FORMAT_XRGB8888:
            return true;
        default:
            return false;
    }
}

static inline bool ppa_dest_cf_supported(lv_color_format_t cf)
{
    switch(cf) {
        case LV_COLOR_FORMAT_RGB565:
        case LV_COLOR_FORMAT_RGB888:
        case LV_COLOR_FORMAT_ARGB8888:
            return true;
        default:
            return false;
    }
}

static inline ppa_fill_color_mode_t lv_color_format_to_ppa_fill(lv_color_format_t lv_fmt)
{
    switch(lv_fmt) {
        case LV_COLOR_FORMAT_RGB565:
            return PPA_FILL_COLOR_MODE_RGB565;
        case LV_COLOR_FORMAT_RGB888:
            return PPA_FILL_COLOR_MODE_RGB888;
        case LV_COLOR_FORMAT_ARGB8888:
            return PPA_FILL_COLOR_MODE_ARGB8888;
        default:
            return PPA_FILL_COLOR_MODE_RGB565;
    }
}

static inline ppa_blend_color_mode_t lv_color_format_to_ppa_blend(lv_color_format_t lv_fmt)
{
    switch(lv_fmt) {
        case LV_COLOR_FORMAT_RGB565:
            return PPA_BLEND_COLOR_MODE_RGB565;
        case LV_COLOR_FORMAT_RGB888:
            return PPA_BLEND_COLOR_MODE_RGB888;
        case LV_COLOR_FORMAT_ARGB8888:
            return PPA_BLEND_COLOR_MODE_ARGB8888;
        default:
            return PPA_BLEND_COLOR_MODE_RGB565;
    }
}

static inline ppa_srm_color_mode_t lv_color_format_to_ppa_srm(lv_color_format_t lv_fmt)
{
    switch(lv_fmt) {
        case LV_COLOR_FORMAT_RGB565:
            return PPA_SRM_COLOR_MODE_RGB565;
        case LV_COLOR_FORMAT_RGB888:
            return PPA_SRM_COLOR_MODE_RGB888;
        case LV_COLOR_FORMAT_XRGB8888:
            return PPA_SRM_COLOR_MODE_ARGB8888;
        default:
            return PPA_SRM_COLOR_MODE_RGB565;
    }
}

#endif /* LV_DRAW_PPA_PRIVATE_FIXED_H */
