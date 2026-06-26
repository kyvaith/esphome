/**
 * @file lv_draw_ppa_img.c
 * Fixed PPA image blending for LVGL 9.4 on ESP32-P4
 * Backported from https://github.com/lvgl/lvgl/pull/9162
 * Adapted for C++ compilation (ESPHome build system)
 */

#include "sdkconfig.h"
#ifdef CONFIG_SOC_PPA_SUPPORTED

#include "lv_draw_ppa_private.h"
#include "lv_draw_ppa.h"
#include "src/draw/lv_draw_image_private.h"
#include "src/draw/lv_image_decoder_private.h"
#include "src/draw/lv_image_decoder.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

#ifndef CONFIG_ESPHOME_LVGL_PPA_SRM_BAND_HEIGHT
#define CONFIG_ESPHOME_LVGL_PPA_SRM_BAND_HEIGHT 32
#endif

static void lv_draw_img_ppa_core(lv_draw_task_t * t, const lv_draw_image_dsc_t * draw_dsc,
                                 const lv_image_decoder_dsc_t * decoder_dsc, lv_draw_image_sup_t * sup,
                                 const lv_area_t * img_coords, const lv_area_t * clipped_img_area);

static uint32_t s_ppa_img_srm_tasks;
static uint32_t s_ppa_img_srm_large_tasks;
static uint32_t s_ppa_img_srm_unaligned_tasks;
static uint64_t s_ppa_img_srm_unaligned_bytes;
static uint64_t s_ppa_img_srm_copy_us;
static uint32_t s_ppa_img_srm_copy_max_us;
static uint64_t s_ppa_img_srm_sync_us;
static uint32_t s_ppa_img_srm_sync_max_us;
static uint64_t s_ppa_img_srm_sync_bytes;
static uint64_t s_ppa_img_srm_ppa_us;
static uint32_t s_ppa_img_srm_ppa_max_us;

uint32_t lv_draw_ppa_get_img_srm_task_count(void)
{
    return s_ppa_img_srm_tasks;
}

uint32_t lv_draw_ppa_get_img_srm_large_task_count(void)
{
    return s_ppa_img_srm_large_tasks;
}

uint32_t lv_draw_ppa_get_img_srm_unaligned_task_count(void)
{
    return s_ppa_img_srm_unaligned_tasks;
}

uint64_t lv_draw_ppa_get_img_srm_unaligned_bytes(void)
{
    return s_ppa_img_srm_unaligned_bytes;
}

uint64_t lv_draw_ppa_get_img_srm_copy_us(void)
{
    return s_ppa_img_srm_copy_us;
}

uint32_t lv_draw_ppa_get_img_srm_copy_max_us(void)
{
    return s_ppa_img_srm_copy_max_us;
}

uint64_t lv_draw_ppa_get_img_srm_sync_us(void)
{
    return s_ppa_img_srm_sync_us;
}

uint32_t lv_draw_ppa_get_img_srm_sync_max_us(void)
{
    return s_ppa_img_srm_sync_max_us;
}

uint64_t lv_draw_ppa_get_img_srm_sync_bytes(void)
{
    return s_ppa_img_srm_sync_bytes;
}

uint64_t lv_draw_ppa_get_img_srm_ppa_us(void)
{
    return s_ppa_img_srm_ppa_us;
}

uint32_t lv_draw_ppa_get_img_srm_ppa_max_us(void)
{
    return s_ppa_img_srm_ppa_max_us;
}


void lv_draw_ppa_img(lv_draw_task_t * t, const lv_draw_image_dsc_t * dsc,
                     const lv_area_t * coords)
{
    if(dsc->opa <= (lv_opa_t)LV_OPA_MIN)
        return;
    lv_draw_image_normal_helper(t, dsc, coords, lv_draw_img_ppa_core, NULL);
}

static void lv_draw_img_ppa_core(lv_draw_task_t * t, const lv_draw_image_dsc_t * draw_dsc,
                                 const lv_image_decoder_dsc_t * decoder_dsc, lv_draw_image_sup_t * sup,
                                 const lv_area_t * img_coords, const lv_area_t * clipped_img_area)
{
    LV_UNUSED(sup);

    lv_layer_t * layer = t->target_layer;
    lv_draw_buf_t * draw_buf = layer->draw_buf;
    const lv_draw_buf_t * decoded = decoder_dsc->decoded;
    lv_draw_ppa_unit_t * u = (lv_draw_ppa_unit_t *)t->draw_unit;

    lv_area_t rel_clip_area;
    lv_area_copy(&rel_clip_area, clipped_img_area);
    lv_area_move(&rel_clip_area, -img_coords->x1, -img_coords->y1);

    lv_area_t rel_img_coords;
    lv_area_copy(&rel_img_coords, img_coords);
    lv_area_move(&rel_img_coords, -img_coords->x1, -img_coords->y1);

    lv_area_t src_area;
    if(!lv_area_intersect(&src_area, &rel_clip_area, &rel_img_coords))
        return;

    lv_area_t dest_area;
    lv_area_copy(&dest_area, clipped_img_area);
    lv_area_move(&dest_area, -t->target_layer->buf_area.x1, -t->target_layer->buf_area.y1);

    if(decoded == NULL || decoded->data == NULL)
        return;

    const uint8_t * src_buf = decoded->data;
    lv_color_format_t src_cf = (lv_color_format_t)decoded->header.cf;
    lv_color_format_t dest_cf = (lv_color_format_t)draw_buf->header.cf;
    uint8_t * dest_buf = draw_buf->data;
    uint32_t block_w = (uint32_t)lv_area_get_width(&src_area);
    uint32_t block_h = (uint32_t)lv_area_get_height(&src_area);
    lv_draw_ppa_cache_msync(decoded->data, decoded->data_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);

    uint32_t src_px_size = lv_color_format_get_size(src_cf);
    uint32_t dest_px_size = lv_color_format_get_size(dest_cf);
    uint32_t src_stride = decoded->header.stride ? decoded->header.stride : (decoded->header.w * src_px_size);
    uint32_t dest_stride = draw_buf->header.stride ? draw_buf->header.stride : (draw_buf->header.w * dest_px_size);
    if(src_px_size == 0 || dest_px_size == 0 ||
       (src_stride % src_px_size) != 0 ||
       (dest_stride % dest_px_size) != 0) {
        LV_LOG_WARN("PPA image skipped: invalid stride src=%u/%u dest=%u/%u",
                    (unsigned)src_stride, (unsigned)src_px_size,
                    (unsigned)dest_stride, (unsigned)dest_px_size);
        return;
    }
    uint32_t src_stride_px = src_stride / src_px_size;
    uint32_t dest_stride_px = dest_stride / dest_px_size;
    uint32_t dest_buffer_size = lv_draw_ppa_align_size((size_t)dest_stride * draw_buf->header.h);

    /* Use field-by-field assignment for C++ compatibility
     * (C++ designated initializers must be in declaration order) */
    ppa_blend_oper_config_t cfg;
    lv_memzero(&cfg, sizeof(cfg));

    /* Background input (source image) */
    cfg.in_bg.buffer         = (void *)src_buf;
    cfg.in_bg.pic_w          = src_stride_px;
    cfg.in_bg.pic_h          = decoded->header.h;
    cfg.in_bg.block_w        = block_w;
    cfg.in_bg.block_h        = block_h;
    cfg.in_bg.block_offset_x = (uint32_t)src_area.x1;
    cfg.in_bg.block_offset_y = (uint32_t)src_area.y1;
    cfg.in_bg.blend_cm       = lv_color_format_to_ppa_blend(src_cf);

    cfg.bg_rgb_swap          = false;
    cfg.bg_byte_swap         = false;
    cfg.bg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
    cfg.bg_alpha_fix_val     = 0xFF;
    cfg.bg_ck_en             = false;

    /* Foreground input */
    cfg.in_fg.buffer         = (void *)dest_buf;
    cfg.in_fg.pic_w          = dest_stride_px;
    cfg.in_fg.pic_h          = draw_buf->header.h;
    cfg.in_fg.block_w        = block_w;
    cfg.in_fg.block_h        = block_h;
    cfg.in_fg.block_offset_x = (uint32_t)dest_area.x1;
    cfg.in_fg.block_offset_y = (uint32_t)dest_area.y1;
    cfg.in_fg.blend_cm       = PPA_BLEND_COLOR_MODE_A8;

    cfg.fg_rgb_swap          = false;
    cfg.fg_byte_swap         = false;
    cfg.fg_alpha_update_mode = PPA_ALPHA_FIX_VALUE;
    cfg.fg_alpha_fix_val     = 0;
    cfg.fg_ck_en             = false;

    /* Output */
    cfg.out.buffer           = dest_buf;
    /* PPA hardware rejects unaligned out.buffer_size (issue #9868). */
    cfg.out.buffer_size      = dest_buffer_size;
    cfg.out.pic_w            = dest_stride_px;
    cfg.out.pic_h            = draw_buf->header.h;
    cfg.out.block_offset_x   = (uint32_t)dest_area.x1;
    cfg.out.block_offset_y   = (uint32_t)dest_area.y1;
    cfg.out.blend_cm         = lv_color_format_to_ppa_blend(dest_cf);

    cfg.mode                 = PPA_TRANS_MODE_BLOCKING;
    cfg.user_data            = u;

    const uint32_t pixel_count = block_w * block_h;
    const int64_t start_us = pixel_count >= 100000U ? esp_timer_get_time() : 0;
    esp_err_t ret = ppa_do_blend(u->blend_client, &cfg);
    if(start_us != 0) {
        ESP_LOGW("lvgl.ppa_img", "blend %ux%u src_cf=%d dst_cf=%d ret=%d took=%lldus",
                 (unsigned)block_w, (unsigned)block_h, (int)src_cf, (int)dest_cf, (int)ret,
                 (long long)(esp_timer_get_time() - start_us));
    }
    if(ret != ESP_OK) {
        LV_LOG_ERROR("PPA blend failed: %d", ret);
    }
}

#ifdef LV_USE_PPA_IMG

void lv_draw_ppa_img_srm(lv_draw_task_t * t, const lv_draw_image_dsc_t * dsc,
                          const lv_area_t * coords)
{
    if(dsc->opa <= (lv_opa_t)LV_OPA_MIN) return;

    lv_draw_ppa_unit_t * u   = (lv_draw_ppa_unit_t *)t->draw_unit;
    lv_layer_t * layer        = t->target_layer;
    lv_draw_buf_t * dest_buf  = layer->draw_buf;

    /* coords is the draw task area. On partial redraws it can be much larger
     * than the dirty region, so clip it before mapping destination pixels back
     * into the source image. The source origin must stay anchored to
     * dsc->image_area; otherwise a small redraw such as a 1 Hz clock update can
     * copy the top rows of the image into the clipped area. */
    lv_area_t clipped_area;
    if(!lv_area_intersect(&clipped_area, coords, &t->clip_area)) return;

    lv_area_t visible_area;
    if(!lv_area_intersect(&visible_area, &clipped_area, &layer->buf_area)) return;

    lv_image_decoder_dsc_t decoder_dsc;
    lv_image_decoder_args_t dec_args;
    lv_memzero(&dec_args, sizeof(dec_args));
    dec_args.flush_cache = true;

    lv_result_t res = lv_image_decoder_open(&decoder_dsc, dsc->src, &dec_args);
    if(res != LV_RESULT_OK) return;

    const lv_draw_buf_t * decoded = decoder_dsc.decoded;
    if(!decoded || !decoded->data) {
        lv_image_decoder_close(&decoder_dsc);
        return;
    }

    const uint8_t * src_buf = (const uint8_t *)decoded->data;
    lv_color_format_t src_cf  = (lv_color_format_t)decoded->header.cf;
    lv_color_format_t dest_cf = (lv_color_format_t)dest_buf->header.cf;
    if(!ppa_src_cf_supported(src_cf) || !ppa_dest_cf_supported(dest_cf)) {
        lv_image_decoder_close(&decoder_dsc);
        return;
    }

    float sx = (dsc->scale_x != LV_SCALE_NONE) ? ((float)dsc->scale_x / 256.0f) : 1.0f;
    float sy = (dsc->scale_y != LV_SCALE_NONE) ? ((float)dsc->scale_y / 256.0f) : 1.0f;

    uint32_t src_w = decoded->header.w;
    uint32_t src_h = decoded->header.h;

    const lv_area_t * image_area = &dsc->image_area;
    if(lv_area_get_width(image_area) <= 0 || lv_area_get_height(image_area) <= 0) {
        image_area = coords;
    }

    /* Virtual image origin: pivot stays fixed on screen as scale changes.
     * image_area->x1/y1 is the full image top-left, independent of clipping. */
    float virt_x = (float)image_area->x1 + (float)dsc->pivot.x * (1.0f - sx);
    float virt_y = (float)image_area->y1 + (float)dsc->pivot.y * (1.0f - sy);

    /* Visible clip dimensions and buffer-local destination (always non-negative) */
    int32_t clip_w = lv_area_get_width(&visible_area);
    int32_t clip_h = lv_area_get_height(&visible_area);

    lv_area_t dest_area;
    lv_area_copy(&dest_area, &visible_area);
    lv_area_move(&dest_area, -layer->buf_area.x1, -layer->buf_area.y1);

    /* Map visible tile top-left back into source image space */
    int32_t src_bx = (int32_t)(((float)visible_area.x1 - virt_x) / sx);
    int32_t src_by = (int32_t)(((float)visible_area.y1 - virt_y) / sy);

    /* ceilf gives the ideal source block; floorf clamp keeps PPA happy.
     * The PPA may render 1 pixel short — we fix that after the call. */
    uint32_t src_bw = (uint32_t)ceilf((float)clip_w / sx);
    uint32_t src_bh = (uint32_t)ceilf((float)clip_h / sy);

    uint32_t avail_w = (uint32_t)(dest_buf->header.w - dest_area.x1);
    uint32_t avail_h = (uint32_t)(dest_buf->header.h - dest_area.y1);
    uint32_t max_src_bw = (uint32_t)floorf((float)avail_w / sx);
    uint32_t max_src_bh = (uint32_t)floorf((float)avail_h / sy);
    bool gap_right  = (src_bw > max_src_bw);
    bool gap_bottom = (src_bh > max_src_bh);
    if(src_bw > max_src_bw) src_bw = max_src_bw;
    if(src_bh > max_src_bh) src_bh = max_src_bh;

    if(src_bx < 0 || src_by < 0 ||
       (uint32_t)src_bx >= src_w || (uint32_t)src_by >= src_h) {
        lv_image_decoder_close(&decoder_dsc);
        return;
    }
    if((uint32_t)src_bx + src_bw > src_w) src_bw = src_w - (uint32_t)src_bx;
    if((uint32_t)src_by + src_bh > src_h) src_bh = src_h - (uint32_t)src_by;
    if(src_bw == 0 || src_bh == 0) {
        lv_image_decoder_close(&decoder_dsc);
        return;
    }

    uint32_t out_bpp = (dest_cf == LV_COLOR_FORMAT_RGB565) ? 2u :
                       (dest_cf == LV_COLOR_FORMAT_RGB888)  ? 3u : 4u;
    uint32_t src_bpp = lv_color_format_get_size(src_cf);
    uint32_t src_stride = decoded->header.stride ? decoded->header.stride : (src_w * src_bpp);
    if(src_bpp == 0 || (src_stride % src_bpp) != 0) {
        LV_LOG_WARN("PPA SRM scale skipped: invalid src stride=%u px=%u",
                    (unsigned)src_stride, (unsigned)src_bpp);
        lv_image_decoder_close(&decoder_dsc);
        return;
    }
    uint32_t src_stride_px = src_stride / src_bpp;
    uint32_t dest_stride = dest_buf->header.stride ? dest_buf->header.stride : (dest_buf->header.w * out_bpp);
    if(out_bpp == 0 || (dest_stride % out_bpp) != 0) {
        LV_LOG_WARN("PPA SRM scale skipped: invalid dest stride=%u px=%u",
                    (unsigned)dest_stride, (unsigned)out_bpp);
        lv_image_decoder_close(&decoder_dsc);
        return;
    }
    uint32_t dest_stride_px = dest_stride / out_bpp;
    uint32_t raw_bytes    = dest_stride * dest_buf->header.h;
    uint32_t aligned_size = lv_draw_ppa_align_size(raw_bytes);
    uint32_t pixel_count = (uint32_t)clip_w * (uint32_t)clip_h;

    /* PPA only reads the source rows covered by the clipped draw task.
     * Syncing the full decoded image on every redraw can move megabytes over
     * PSRAM for a tiny dirty area and starve MIPI DSI scanout. */
    const uint8_t * src_sync = src_buf + (size_t)src_by * src_stride;
    uint32_t src_sync_size = src_stride * src_bh;
    int64_t sync_start_us = pixel_count >= 100000U ? esp_timer_get_time() : 0;
    lv_draw_ppa_cache_msync(src_sync, src_sync_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    if(sync_start_us != 0) {
        uint32_t elapsed = (uint32_t)(esp_timer_get_time() - sync_start_us);
        s_ppa_img_srm_sync_us += elapsed;
        s_ppa_img_srm_sync_bytes += src_sync_size;
        if(elapsed > s_ppa_img_srm_sync_max_us) {
            s_ppa_img_srm_sync_max_us = elapsed;
        }
        if(elapsed > 5000U) {
            ESP_LOGW("lvgl.ppa_img", "srm source sync %ux%u bytes=%u took=%uus",
                     (unsigned)src_bw, (unsigned)src_bh, (unsigned)src_sync_size, (unsigned)elapsed);
        }
    }

    ppa_srm_oper_config_t cfg;
    lv_memzero(&cfg, sizeof(cfg));

    cfg.in.buffer         = (void *)decoded->data;
    cfg.in.pic_w          = src_stride_px;
    cfg.in.pic_h          = src_h;
    cfg.in.block_w        = src_bw;
    cfg.in.block_h        = src_bh;
    cfg.in.block_offset_x = (uint32_t)src_bx;
    cfg.in.block_offset_y = (uint32_t)src_by;
    cfg.in.srm_cm         = lv_color_format_to_ppa_srm(src_cf);

    uint8_t * aligned_out = NULL;
    uint8_t * out_ptr     = dest_buf->data;

    if(esp_ptr_external_ram(dest_buf->data) &&
       !lv_draw_ppa_buf_cache_aligned(dest_buf->data)) {
        if((uint32_t)clip_w * (uint32_t)clip_h >= 100000U) {
            ESP_LOGW("lvgl.ppa_img", "srm unaligned output: copying %u bytes before PPA", (unsigned)raw_bytes);
        }
        aligned_out = (uint8_t *)heap_caps_aligned_alloc(
            PPA_CACHE_LINE_SIZE, aligned_size,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if(!aligned_out) {
            LV_LOG_ERROR("PPA SRM: aligned alloc failed (%u B)", (unsigned)aligned_size);
            lv_image_decoder_close(&decoder_dsc);
            return;
        }
        s_ppa_img_srm_unaligned_tasks++;
        s_ppa_img_srm_unaligned_bytes += raw_bytes;
        int64_t copy_start_us = esp_timer_get_time();
        memcpy(aligned_out, dest_buf->data, raw_bytes);
        uint32_t copy_elapsed_us = (uint32_t)(esp_timer_get_time() - copy_start_us);
        s_ppa_img_srm_copy_us += copy_elapsed_us;
        if(copy_elapsed_us > s_ppa_img_srm_copy_max_us) {
            s_ppa_img_srm_copy_max_us = copy_elapsed_us;
        }
        lv_draw_ppa_cache_msync(aligned_out, aligned_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
        out_ptr = aligned_out;
    }

    cfg.out.buffer         = out_ptr;
    cfg.out.buffer_size    = aligned_size;
    cfg.out.pic_w          = dest_stride_px;
    cfg.out.pic_h          = dest_buf->header.h;
    cfg.out.block_offset_x = (uint32_t)dest_area.x1;
    cfg.out.block_offset_y = (uint32_t)dest_area.y1;
    cfg.out.srm_cm         = lv_color_format_to_ppa_srm(dest_cf);

    cfg.rotation_angle    = PPA_SRM_ROTATION_ANGLE_0;
    cfg.scale_x           = sx;
    cfg.scale_y           = sy;
    cfg.mirror_x          = false;
    cfg.mirror_y          = false;
    cfg.rgb_swap          = false;
    cfg.byte_swap         = false;
    cfg.alpha_update_mode = PPA_ALPHA_NO_CHANGE;
    cfg.mode              = PPA_TRANS_MODE_BLOCKING;
    cfg.user_data         = u;

    s_ppa_img_srm_tasks++;
    if(pixel_count >= 100000U) {
        s_ppa_img_srm_large_tasks++;
    }
    const bool plain_1x =
        dsc->rotation == 0 && dsc->scale_x == LV_SCALE_NONE && dsc->scale_y == LV_SCALE_NONE &&
        dsc->skew_x == 0 && dsc->skew_y == 0 && sx == 1.0f && sy == 1.0f &&
        !gap_right && !gap_bottom;
    const uint32_t band_height = CONFIG_ESPHOME_LVGL_PPA_SRM_BAND_HEIGHT;
    const bool use_bands =
        plain_1x && aligned_out == NULL && pixel_count >= 100000U &&
        band_height > 0 && (uint32_t)clip_h > band_height;

    /* SRM writes through DMA while LVGL's draw buffer is cacheable PSRAM.
     * Synchronize whole touched rows, not just the visible rectangle: RGB888
     * spans are not cache-line aligned, so a row-level contract preserves
     * neighbouring software-rendered pixels and prevents delayed horizontal
     * artifacts when later redraws hit the same cache lines.
     *
     * Large 1:1 artwork copies are split into bands. A single full-screen SRM
     * transfer can monopolize PSRAM long enough to starve MIPI DSI scanout;
     * banding leaves short gaps between PPA jobs while keeping the CPU-free SRM
     * path for the expensive RGB565/RGB888 copy.
     */
    uint8_t * sync_start = out_ptr + (size_t)dest_area.y1 * dest_stride;
    uint32_t sync_size = dest_stride * (uint32_t)clip_h;

    esp_err_t ret = ESP_OK;
    uint32_t elapsed = 0;
    uint32_t max_band_elapsed = 0;
    const int64_t start_us = pixel_count >= 100000U ? esp_timer_get_time() : 0;
    if(use_bands) {
        for(uint32_t y = 0; y < (uint32_t)clip_h; y += band_height) {
            uint32_t this_band_h = (uint32_t)clip_h - y;
            if(this_band_h > band_height) this_band_h = band_height;

            cfg.in.block_h = this_band_h;
            cfg.in.block_offset_y = (uint32_t)src_by + y;
            cfg.out.block_offset_y = (uint32_t)dest_area.y1 + y;

            uint8_t * band_sync_start = out_ptr + (size_t)(dest_area.y1 + y) * dest_stride;
            uint32_t band_sync_size = dest_stride * this_band_h;
            lv_draw_ppa_cache_msync(band_sync_start, band_sync_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);

            const int64_t band_start_us = esp_timer_get_time();
            ret = ppa_do_scale_rotate_mirror(u->srm_client, &cfg);
            uint32_t band_elapsed = (uint32_t)(esp_timer_get_time() - band_start_us);
            if(band_elapsed > max_band_elapsed) max_band_elapsed = band_elapsed;
            if(ret == ESP_OK) {
                lv_draw_ppa_cache_msync_after_dma_write(band_sync_start, band_sync_size);
            } else {
                break;
            }
            taskYIELD();
        }
    } else {
        lv_draw_ppa_cache_msync(sync_start, sync_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
        ret = ppa_do_scale_rotate_mirror(u->srm_client, &cfg);
        if(ret == ESP_OK) {
            lv_draw_ppa_cache_msync_after_dma_write(sync_start, sync_size);
        }
    }
    if(start_us != 0) {
        elapsed = (uint32_t)(esp_timer_get_time() - start_us);
        s_ppa_img_srm_ppa_us += elapsed;
        if(elapsed > s_ppa_img_srm_ppa_max_us) {
            s_ppa_img_srm_ppa_max_us = elapsed;
        }
        ESP_LOGW("lvgl.ppa_img", "srm %dx%d src=%ux%u scale=%.2f/%.2f band=%u max_band=%uus ret=%d took=%uus",
                 (int)clip_w, (int)clip_h, (unsigned)src_bw, (unsigned)src_bh,
                 (double)sx, (double)sy, use_bands ? (unsigned)band_height : 0U,
                 (unsigned)max_band_elapsed, (int)ret, (unsigned)elapsed);
    }
    if(ret != ESP_OK) {
        LV_LOG_ERROR("PPA SRM scale failed: %d (src %ux%u scale %.2f/%.2f)",
                     (int)ret, src_w, src_h, (double)sx, (double)sy);
    }

    /* PPA floorf rounding leaves a 1-pixel gap at right/bottom edges.
     * Fill it by duplicating the last rendered column/row. Invalidate CPU
     * cache first: PPA wrote via DMA, so CPU cache can be stale. */
    if(ret == ESP_OK && (gap_right || gap_bottom)) {
        lv_draw_ppa_cache_msync_after_dma_write(out_ptr, aligned_size);

        uint8_t *base = out_ptr;
        uint32_t stride = dest_stride;

        if(gap_right && clip_w >= 2) {
            uint32_t col = dest_area.x1 + (uint32_t)clip_w - 1;
            uint32_t col_prev = col - 1;
            for(int32_t y = 0; y < clip_h; y++) {
                uint32_t row_off = (dest_area.y1 + (uint32_t)y) * stride;
                lv_memcpy(base + row_off + col * out_bpp,
                          base + row_off + col_prev * out_bpp, out_bpp);
            }
        }
        if(gap_bottom && clip_h >= 2) {
            uint32_t row = dest_area.y1 + (uint32_t)clip_h - 1;
            uint32_t row_prev = row - 1;
            lv_memcpy(base + row * stride + dest_area.x1 * out_bpp,
                      base + row_prev * stride + dest_area.x1 * out_bpp,
                      (uint32_t)clip_w * out_bpp);
        }

        lv_draw_ppa_cache_msync(out_ptr, aligned_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    }

    if(aligned_out) {
        if(ret == ESP_OK) {
            lv_draw_ppa_cache_msync_after_dma_write(aligned_out, aligned_size);
            int64_t copy_start_us = esp_timer_get_time();
            memcpy(dest_buf->data, aligned_out, raw_bytes);
            uint32_t copy_elapsed_us = (uint32_t)(esp_timer_get_time() - copy_start_us);
            s_ppa_img_srm_copy_us += copy_elapsed_us;
            if(copy_elapsed_us > s_ppa_img_srm_copy_max_us) {
                s_ppa_img_srm_copy_max_us = copy_elapsed_us;
            }
            lv_draw_ppa_cache_msync(dest_buf->data, raw_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
        }
        heap_caps_free(aligned_out);
    }

    lv_image_decoder_close(&decoder_dsc);
}

/**
 * PPA SRM hardware-accelerated image rotation (0/90/180/270 degrees)
 * Uses the ESP32-P4 PPA Scale-Rotate-Mirror engine for zero-CPU-cost rotation.
 */
void lv_draw_ppa_img_rotate(lv_draw_task_t * t, const lv_draw_image_dsc_t * dsc,
                            const lv_area_t * coords)
{
    if(dsc->opa <= (lv_opa_t)LV_OPA_MIN)
        return;

    lv_draw_ppa_unit_t * u = (lv_draw_ppa_unit_t *)t->draw_unit;
    lv_layer_t * layer = t->target_layer;
    lv_draw_buf_t * dest_buf = layer->draw_buf;

    /* Decode the source image */
    lv_image_decoder_dsc_t decoder_dsc;
    lv_image_decoder_args_t dec_args;
    lv_memzero(&dec_args, sizeof(dec_args));
    dec_args.stride_align = false;
    dec_args.premultiply = false;
    dec_args.no_cache = false;
    dec_args.use_indexed = false;
    dec_args.flush_cache = true;  /* Ensure cache coherency for PPA DMA */

    lv_result_t res = lv_image_decoder_open(&decoder_dsc, dsc->src, &dec_args);
    if(res != LV_RESULT_OK) {
        LV_LOG_WARN("PPA SRM: failed to decode image");
        return;
    }

    const lv_draw_buf_t * decoded = decoder_dsc.decoded;
    if(!decoded || !decoded->data) {
        lv_image_decoder_close(&decoder_dsc);
        return;
    }

    lv_color_format_t src_cf = (lv_color_format_t)decoded->header.cf;
    lv_color_format_t dest_cf = (lv_color_format_t)dest_buf->header.cf;

    /* Verify PPA format support for both source and destination */
    if(!ppa_src_cf_supported(src_cf) || !ppa_dest_cf_supported(dest_cf)) {
        LV_LOG_WARN("PPA SRM: unsupported color format src=%d dest=%d", src_cf, dest_cf);
        lv_image_decoder_close(&decoder_dsc);
        return;
    }

    /* Map LVGL rotation (clockwise, 0.1 deg units) to PPA rotation (counter-clockwise) */
    int32_t angle = dsc->rotation % 3600;
    if(angle < 0) angle += 3600;

    ppa_srm_rotation_angle_t ppa_rot;
    switch(angle) {
        case 0:    ppa_rot = PPA_SRM_ROTATION_ANGLE_0;   break;
        case 900:  ppa_rot = PPA_SRM_ROTATION_ANGLE_270; break;  /* 90° CW = 270° CCW */
        case 1800: ppa_rot = PPA_SRM_ROTATION_ANGLE_180; break;
        case 2700: ppa_rot = PPA_SRM_ROTATION_ANGLE_90;  break;  /* 270° CW = 90° CCW */
        default:
            lv_image_decoder_close(&decoder_dsc);
            return;
    }

    uint32_t src_w = decoded->header.w;
    uint32_t src_h = decoded->header.h;

    /* Compute destination area relative to layer buffer origin */
    lv_area_t dest_area;
    lv_area_copy(&dest_area, &t->area);
    lv_area_move(&dest_area, -layer->buf_area.x1, -layer->buf_area.y1);

    /* Flush decoded source buffer for PPA DMA access. Align size to cache
     * line; _UNALIGNED flag is only a safety net for the address. */
    lv_draw_ppa_cache_msync(decoded->data, decoded->data_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);

    /* Configure PPA SRM operation */
    ppa_srm_oper_config_t cfg;
    lv_memzero(&cfg, sizeof(cfg));

    /* Input: full source image block */
    uint32_t src_bpp_r = lv_color_format_get_size(src_cf);
    uint32_t src_stride_r = decoded->header.stride ? decoded->header.stride : (src_w * src_bpp_r);
    if(src_bpp_r == 0 || (src_stride_r % src_bpp_r) != 0) {
        LV_LOG_WARN("PPA SRM rotate skipped: invalid src stride=%u px=%u",
                    (unsigned)src_stride_r, (unsigned)src_bpp_r);
        lv_image_decoder_close(&decoder_dsc);
        return;
    }
    uint32_t src_stride_px_r = src_stride_r / src_bpp_r;
    cfg.in.buffer         = (void *)decoded->data;
    cfg.in.pic_w          = src_stride_px_r;
    cfg.in.pic_h          = src_h;
    cfg.in.block_w        = src_w;
    cfg.in.block_h        = src_h;
    cfg.in.block_offset_x = 0;
    cfg.in.block_offset_y = 0;
    cfg.in.srm_cm         = lv_color_format_to_ppa_srm(src_cf);

    uint32_t out_bpp_r = (dest_cf == LV_COLOR_FORMAT_RGB565) ? 2u :
                         (dest_cf == LV_COLOR_FORMAT_RGB888)  ? 3u : 4u;
    uint32_t dest_stride_r = dest_buf->header.stride ? dest_buf->header.stride : (dest_buf->header.w * out_bpp_r);
    if(out_bpp_r == 0 || (dest_stride_r % out_bpp_r) != 0) {
        LV_LOG_WARN("PPA SRM rotate skipped: invalid dest stride=%u px=%u",
                    (unsigned)dest_stride_r, (unsigned)out_bpp_r);
        lv_image_decoder_close(&decoder_dsc);
        return;
    }
    uint32_t dest_stride_px_r = dest_stride_r / out_bpp_r;
    uint32_t raw_bytes_r    = dest_stride_r * dest_buf->header.h;
    uint32_t aligned_size_r = lv_draw_ppa_align_size(raw_bytes_r);

    uint8_t * aligned_out_r = NULL;
    uint8_t * out_ptr_r     = dest_buf->data;

    if(esp_ptr_external_ram(dest_buf->data) &&
       !lv_draw_ppa_buf_cache_aligned(dest_buf->data)) {
        aligned_out_r = (uint8_t *)heap_caps_aligned_alloc(
            PPA_CACHE_LINE_SIZE, aligned_size_r,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if(!aligned_out_r) {
            LV_LOG_ERROR("PPA SRM rotate: aligned alloc failed");
            lv_image_decoder_close(&decoder_dsc);
            return;
        }
        memcpy(aligned_out_r, dest_buf->data, raw_bytes_r);
        lv_draw_ppa_cache_msync(aligned_out_r, aligned_size_r, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
        out_ptr_r = aligned_out_r;
    }

    cfg.out.buffer         = out_ptr_r;
    cfg.out.buffer_size    = aligned_size_r;
    cfg.out.pic_w          = dest_stride_px_r;
    cfg.out.pic_h          = dest_buf->header.h;
    cfg.out.block_offset_x = (uint32_t)dest_area.x1;
    cfg.out.block_offset_y = (uint32_t)dest_area.y1;
    cfg.out.srm_cm         = lv_color_format_to_ppa_srm(dest_cf);

    cfg.rotation_angle     = ppa_rot;
    cfg.scale_x            = (dsc->scale_x != LV_SCALE_NONE) ? ((float)dsc->scale_x / 256.0f) : 1.0f;
    cfg.scale_y            = (dsc->scale_y != LV_SCALE_NONE) ? ((float)dsc->scale_y / 256.0f) : 1.0f;
    cfg.mirror_x           = false;
    cfg.mirror_y           = false;
    cfg.rgb_swap           = false;
    cfg.byte_swap          = false;
    cfg.alpha_update_mode  = PPA_ALPHA_NO_CHANGE;
    cfg.mode               = PPA_TRANS_MODE_BLOCKING;
    cfg.user_data          = u;

    esp_err_t ret = ppa_do_scale_rotate_mirror(u->srm_client, &cfg);
    if(ret != ESP_OK) {
        LV_LOG_ERROR("PPA SRM rotation failed: %d  (src %ux%u, angle %d)", (int)ret, src_w, src_h, angle);
    }

    if(aligned_out_r) {
        if(ret == ESP_OK) {
            lv_draw_ppa_cache_msync_after_dma_write(aligned_out_r, aligned_size_r);
            memcpy(dest_buf->data, aligned_out_r, raw_bytes_r);
            lv_draw_ppa_cache_msync(dest_buf->data, raw_bytes_r, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
        }
        heap_caps_free(aligned_out_r);
    }

    lv_image_decoder_close(&decoder_dsc);
}

#endif /* LV_USE_PPA_IMG */

#endif /* CONFIG_SOC_PPA_SUPPORTED */
