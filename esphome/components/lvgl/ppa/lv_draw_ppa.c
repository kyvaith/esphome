/**
 * @file lv_draw_ppa.c
 * Fixed PPA draw unit for LVGL 9.4 on ESP32-P4
 * Backported from https://github.com/lvgl/lvgl/pull/9162
 * Adapted for C++ compilation (ESPHome build system)
 */

#include "sdkconfig.h"
#ifdef CONFIG_SOC_PPA_SUPPORTED

#include "lv_draw_ppa_private.h"
#include "lv_draw_ppa.h"
#include "src/draw/lv_draw_image.h"
#include "esp_timer.h"

/*********************
 *      DEFINES
 *********************/
#define PPA_BUF_ALIGN     (16)  /* PPA needs at least 16-byte aligned buffers (128-bit burst) */

static const char * TAG = "ppa_draw";
static uint32_t s_ppa_fill_tasks = 0;
static uint32_t s_ppa_img_tasks = 0;
static uint32_t s_ppa_img_eval_logs = 0;
static uint32_t s_ppa_img_eval_tasks = 0;
static uint32_t s_ppa_img_large_eval_tasks = 0;
static uint32_t s_ppa_img_accepted_eval_tasks = 0;
static uint32_t s_ppa_img_reject_logs = 0;
static uint32_t s_ppa_overlay_perf_count = 0;
static uint64_t s_ppa_overlay_perf_pre_us = 0;
static uint64_t s_ppa_overlay_perf_handler_us = 0;
static uint64_t s_ppa_overlay_perf_post_us = 0;

extern uint32_t lvgl_esphome_get_perf_logging_enabled(void);
bool esphome_artwork_image_buffer_written_by_dma(const void * ptr) __attribute__((weak));

static inline bool ppa_buf_usable(lv_draw_buf_t * buf);

static inline uint32_t ppa_area_px(const lv_area_t * area)
{
    int32_t w = lv_area_get_width(area);
    int32_t h = lv_area_get_height(area);
    if(w <= 0 || h <= 0) return 0;
    return (uint32_t)w * (uint32_t)h;
}

static inline bool ppa_image_scale_is_identity(const lv_draw_image_dsc_t * dsc)
{
    /* LVGL can represent "natural size" as either LV_SCALE_NONE or the fixed
     * point value 256. Only use SRM when there is a real scale transform. */
    const int32_t sx = dsc->scale_x;
    const int32_t sy = dsc->scale_y;
    return (sx == LV_SCALE_NONE || sx == 256) && (sy == LV_SCALE_NONE || sy == 256);
}

static inline bool ppa_image_source_is_artwork_dma_buffer(const lv_draw_image_dsc_t * dsc)
{
    if(esphome_artwork_image_buffer_written_by_dma == NULL || dsc == NULL || dsc->src == NULL) {
        return false;
    }
    if(lv_image_src_get_type(dsc->src) != LV_IMAGE_SRC_VARIABLE) {
        return false;
    }
    const lv_image_dsc_t * img = (const lv_image_dsc_t *)dsc->src;
    return img->data != NULL && esphome_artwork_image_buffer_written_by_dma(img->data);
}

static inline bool ppa_task_large_visible_band(const lv_draw_task_t * t)
{
    lv_area_t visible_area;
    if(!lv_area_intersect(&visible_area, &t->area, &t->clip_area)) return false;
    if(t->target_layer != NULL) {
        if(!lv_area_intersect(&visible_area, &visible_area, &t->target_layer->buf_area)) return false;
    }

    /* LVGL partial rendering splits full-screen images into wide horizontal
     * bands. Those are exactly the expensive image draws PPA SRM should handle.
     * Small clipped redraws over cached RGB888 PSRAM (for example a clock tick
     * over album artwork) are the cases that produced delayed horizontal line
     * artifacts, so keep them on the software renderer. */
    return lv_area_get_width(&visible_area) >= 320 &&
           ppa_area_px(&visible_area) >= (128U * 128U);
}

static inline bool ppa_image_task_is_large_opaque_copy(const lv_draw_task_t * t, const lv_draw_image_dsc_t * dsc)
{
    if(!ppa_image_scale_is_identity(dsc)) return false;
    if(dsc->rotation != 0 || dsc->skew_x != 0 || dsc->skew_y != 0) return false;
    if(dsc->opa < (lv_opa_t)LV_OPA_MAX || dsc->blend_mode != LV_BLEND_MODE_NORMAL) return false;
    if(!ppa_task_large_visible_band(t)) return false;
    if(!ppa_src_cf_supported((lv_color_format_t)dsc->header.cf)) return false;

    lv_draw_buf_t * dest = t->target_layer != NULL ? t->target_layer->draw_buf : NULL;
    if(!ppa_buf_usable(dest)) return false;
    return ppa_dest_cf_supported((lv_color_format_t)dest->header.cf);
}

static inline bool ppa_image_task_is_direct_overlay(const lv_draw_task_t * t, const lv_draw_image_dsc_t * dsc)
{
    if(!ppa_image_scale_is_identity(dsc)) return false;
    if(dsc->rotation != 0 || dsc->skew_x != 0 || dsc->skew_y != 0) return false;
    if(dsc->opa < (lv_opa_t)LV_OPA_MAX || dsc->blend_mode != LV_BLEND_MODE_NORMAL) return false;
    const lv_color_format_t src_cf = (lv_color_format_t)dsc->header.cf;
    if(src_cf != LV_COLOR_FORMAT_ARGB8888 && src_cf != LV_COLOR_FORMAT_RGB888 &&
       src_cf != LV_COLOR_FORMAT_XRGB8888) {
        return false;
    }
    if(dsc->clip_radius != 0 || dsc->recolor_opa > (lv_opa_t)LV_OPA_MIN || dsc->tile) return false;
    if(dsc->colorkey != NULL || dsc->bitmap_mask_src != NULL) return false;

    lv_area_t visible_area;
    if(!lv_area_intersect(&visible_area, &t->area, &t->clip_area)) return false;
    if(t->target_layer != NULL) {
        if(!lv_area_intersect(&visible_area, &visible_area, &t->target_layer->buf_area)) return false;
    }

    /* Small icons are cheaper to leave in the software renderer. Dynamic
     * overlays such as meters and animated controls quickly amortize the PPA
     * setup cost. RGB888/XRGB8888 sources are opaque copies; ARGB8888 sources
     * are blended over the current destination. */
    if(ppa_area_px(&visible_area) < (64U * 64U)) return false;

    lv_draw_buf_t * dest = t->target_layer != NULL ? t->target_layer->draw_buf : NULL;
    if(!ppa_buf_usable(dest)) return false;
    return ppa_dest_cf_supported((lv_color_format_t)dest->header.cf);
}

static inline bool ppa_image_task_is_alpha_overlay(const lv_draw_task_t * t, const lv_draw_image_dsc_t * dsc)
{
    return ppa_image_task_is_direct_overlay(t, dsc) &&
           (lv_color_format_t)dsc->header.cf == LV_COLOR_FORMAT_ARGB8888;
}

static void ppa_log_image_eval(lv_draw_unit_t * draw_unit, const lv_draw_task_t * t,
                               const lv_draw_image_dsc_t * dsc, bool large_visible)
{
    lv_draw_buf_t * eval_dest = t->target_layer != NULL ? t->target_layer->draw_buf : NULL;
    lv_color_format_t dest_cf = eval_dest != NULL ? (lv_color_format_t)eval_dest->header.cf : LV_COLOR_FORMAT_UNKNOWN;
    lv_area_t visible_area;
    bool has_visible = lv_area_intersect(&visible_area, &t->area, &t->clip_area);
    if(has_visible && t->target_layer != NULL) {
        has_visible = lv_area_intersect(&visible_area, &visible_area, &t->target_layer->buf_area);
    }
    ESP_LOGW(TAG,
             "ppa image eval %s area=%dx%d clip=%dx%d visible=%dx%d img_area=%dx%d src_cf=%d opa=%u "
             "blend=%d scale=%d/%d rot=%d dest_cf=%d dest=%ux%u stride=%u usable=%d score=%d unit=%d",
             large_visible ? "large" : "seen",
             (int)lv_area_get_width(&t->area), (int)lv_area_get_height(&t->area),
             (int)lv_area_get_width(&t->clip_area), (int)lv_area_get_height(&t->clip_area),
             has_visible ? (int)lv_area_get_width(&visible_area) : 0,
             has_visible ? (int)lv_area_get_height(&visible_area) : 0,
             (int)lv_area_get_width(&dsc->image_area), (int)lv_area_get_height(&dsc->image_area),
             (int)dsc->header.cf, (unsigned)dsc->opa, (int)dsc->blend_mode,
             (int)dsc->scale_x, (int)dsc->scale_y, (int)dsc->rotation,
             (int)dest_cf, eval_dest != NULL ? (unsigned)eval_dest->header.w : 0,
             eval_dest != NULL ? (unsigned)eval_dest->header.h : 0,
             eval_dest != NULL ? (unsigned)eval_dest->header.stride : 0,
             (int)ppa_buf_usable(eval_dest), (int)t->preference_score,
             (int)draw_unit->idx);
}

static void ppa_log_image_reject(lv_draw_unit_t * draw_unit, const lv_draw_task_t * t,
                                 const lv_draw_image_dsc_t * dsc, const char * reason)
{
    if(!lvgl_esphome_get_perf_logging_enabled() || s_ppa_img_reject_logs >= 320) return;
    if(dsc->scale_x == LV_SCALE_NONE && dsc->scale_y == LV_SCALE_NONE && dsc->rotation == 0) return;

    lv_draw_buf_t * eval_dest = t->target_layer != NULL ? t->target_layer->draw_buf : NULL;
    lv_color_format_t dest_cf = eval_dest != NULL ? (lv_color_format_t)eval_dest->header.cf : LV_COLOR_FORMAT_UNKNOWN;
    lv_area_t visible_area;
    bool has_visible = lv_area_intersect(&visible_area, &t->area, &t->clip_area);
    if(has_visible && t->target_layer != NULL) {
        has_visible = lv_area_intersect(&visible_area, &visible_area, &t->target_layer->buf_area);
    }
    ESP_LOGW(TAG,
             "ppa image reject reason=%s area=%dx%d clip=%dx%d visible=%dx%d img_area=%dx%d "
             "src_cf=%d opa=%u blend=%d scale=%d/%d rot=%d dest_cf=%d dest=%ux%u stride=%u usable=%d "
             "score=%d unit=%d",
             reason,
             (int)lv_area_get_width(&t->area), (int)lv_area_get_height(&t->area),
             (int)lv_area_get_width(&t->clip_area), (int)lv_area_get_height(&t->clip_area),
             has_visible ? (int)lv_area_get_width(&visible_area) : 0,
             has_visible ? (int)lv_area_get_height(&visible_area) : 0,
             (int)lv_area_get_width(&dsc->image_area), (int)lv_area_get_height(&dsc->image_area),
             (int)dsc->header.cf, (unsigned)dsc->opa, (int)dsc->blend_mode,
             (int)dsc->scale_x, (int)dsc->scale_y, (int)dsc->rotation,
             (int)dest_cf, eval_dest != NULL ? (unsigned)eval_dest->header.w : 0,
             eval_dest != NULL ? (unsigned)eval_dest->header.h : 0,
             eval_dest != NULL ? (unsigned)eval_dest->header.stride : 0,
             (int)ppa_buf_usable(eval_dest), (int)t->preference_score,
             (int)draw_unit->idx);
    s_ppa_img_reject_logs++;
}

/* Check if a draw buffer is suitable for PPA (non-NULL, aligned, has data) */
static inline bool ppa_buf_usable(lv_draw_buf_t * buf)
{
    if(buf == NULL || buf->data == NULL || buf->data_size == 0) return false;
    if(((uintptr_t)buf->data) % PPA_BUF_ALIGN != 0) return false;
    uint32_t px_size = lv_color_format_get_size((lv_color_format_t)buf->header.cf);
    if(px_size == 0 || buf->header.w == 0 || buf->header.h == 0) return false;

    uint32_t stride = buf->header.stride ? buf->header.stride : (buf->header.w * px_size);
    if(stride < (buf->header.w * px_size)) return false;
    if((stride % px_size) != 0) return false;

    size_t required_size = (size_t)stride * buf->header.h;
    if(required_size > buf->data_size && !esp_ptr_external_ram(buf->data)) return false;
    return true;
}

/**********************
 *  STATIC PROTOTYPES
 **********************/
static int32_t ppa_evaluate(lv_draw_unit_t * draw_unit, lv_draw_task_t * task);
static int32_t ppa_dispatch(lv_draw_unit_t * draw_unit, lv_layer_t * layer);
static int32_t ppa_delete(lv_draw_unit_t * draw_unit);

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

void lv_draw_ppa_init(void)
{
    lv_draw_ppa_unit_t * draw_ppa_unit = (lv_draw_ppa_unit_t *)lv_draw_create_unit(sizeof(lv_draw_ppa_unit_t));
    draw_ppa_unit->base_unit.evaluate_cb = ppa_evaluate;
    draw_ppa_unit->base_unit.dispatch_cb = ppa_dispatch;
    draw_ppa_unit->base_unit.delete_cb = ppa_delete;

    ESP_LOGW(TAG, "PPA draw unit registered, idx=%d burst=%d srm_burst=%d", (int)draw_ppa_unit->base_unit.idx,
             (int)LV_DRAW_PPA_DATA_BURST_LENGTH, (int)LV_DRAW_PPA_SRM_DATA_BURST_LENGTH);

    /* Register PPA clients */
    esp_err_t res;
    ppa_client_config_t cfg;
    lv_memzero(&cfg, sizeof(cfg));

    /* Register SRM client */
    cfg.oper_type = PPA_OPERATION_SRM;
    cfg.max_pending_trans_num = 1;
    cfg.data_burst_length = LV_DRAW_PPA_SRM_DATA_BURST_LENGTH;

    res = ppa_register_client(&cfg, &draw_ppa_unit->srm_client);
    if(res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register SRM client: %d", res);
    }

    /* Register Fill client */
    lv_memzero(&cfg, sizeof(cfg));
    cfg.oper_type = PPA_OPERATION_FILL;
    cfg.max_pending_trans_num = 1;
    cfg.data_burst_length = LV_DRAW_PPA_DATA_BURST_LENGTH;

    res = ppa_register_client(&cfg, &draw_ppa_unit->fill_client);
    if(res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register Fill client: %d", res);
    }

    /* Register Blend client */
    lv_memzero(&cfg, sizeof(cfg));
    cfg.oper_type = PPA_OPERATION_BLEND;
    cfg.max_pending_trans_num = 1;
    cfg.data_burst_length = LV_DRAW_PPA_DATA_BURST_LENGTH;

    res = ppa_register_client(&cfg, &draw_ppa_unit->blend_client);
    if(res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register Blend client: %d", res);
    }
}

void lv_draw_ppa_deinit(void)
{
}

uint32_t lv_draw_ppa_get_fill_task_count(void)
{
    return s_ppa_fill_tasks;
}

uint32_t lv_draw_ppa_get_img_task_count(void)
{
    return s_ppa_img_tasks;
}

uint32_t lv_draw_ppa_get_img_eval_count(void)
{
    return s_ppa_img_eval_tasks;
}

uint32_t lv_draw_ppa_get_img_large_eval_count(void)
{
    return s_ppa_img_large_eval_tasks;
}

uint32_t lv_draw_ppa_get_img_accepted_eval_count(void)
{
    return s_ppa_img_accepted_eval_tasks;
}

uint32_t lv_draw_ppa_get_overlay_perf_count(void)
{
    return s_ppa_overlay_perf_count;
}

uint64_t lv_draw_ppa_get_overlay_perf_pre_us(void)
{
    return s_ppa_overlay_perf_pre_us;
}

uint64_t lv_draw_ppa_get_overlay_perf_handler_us(void)
{
    return s_ppa_overlay_perf_handler_us;
}

uint64_t lv_draw_ppa_get_overlay_perf_post_us(void)
{
    return s_ppa_overlay_perf_post_us;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
static int32_t ppa_evaluate(lv_draw_unit_t * draw_unit, lv_draw_task_t * t)
{
    switch(t->type) {
        case LV_DRAW_TASK_TYPE_FILL: {
            const lv_draw_fill_dsc_t * dsc = (const lv_draw_fill_dsc_t *)t->draw_dsc;
            if(dsc->radius != 0) return 0;
            if(dsc->grad.dir != LV_GRAD_DIR_NONE) return 0;
            if(dsc->opa < (lv_opa_t)LV_OPA_MAX) return 0;

            lv_draw_buf_t * draw_buf = t->target_layer->draw_buf;
            if(!ppa_buf_usable(draw_buf)) return 0;
            lv_color_format_t dest_cf = (lv_color_format_t)draw_buf->header.cf;
            if(!ppa_dest_cf_supported(dest_cf)) return 0;
            /* ESP32-P4 PPA fill corrupts RGB888 LVGL buffers with short horizontal
             * artifacts. Keep PPA fill for other formats, and let LVGL's software
             * renderer handle RGB888 fills while image/SRM acceleration stays on. */
            if(dest_cf == LV_COLOR_FORMAT_RGB888) return 0;

            if(t->preference_score > 40) {
                t->preference_score = 40;
                t->preferred_draw_unit_id = draw_unit->idx;
            }
            return 1;
        }

        case LV_DRAW_TASK_TYPE_IMAGE: {
            const lv_draw_image_dsc_t * dsc = (const lv_draw_image_dsc_t *)t->draw_dsc;
            const bool large_visible = ppa_task_large_visible_band(t);
            if(ppa_image_source_is_artwork_dma_buffer(dsc) && !large_visible) return 0;
            s_ppa_img_eval_tasks++;
            if(large_visible) s_ppa_img_large_eval_tasks++;
            if(s_ppa_img_eval_logs < 40 ||
               dsc->scale_x != LV_SCALE_NONE || dsc->scale_y != LV_SCALE_NONE ||
               large_visible) {
                ppa_log_image_eval(draw_unit, t, dsc, large_visible);
                s_ppa_img_eval_logs++;
            }

#ifdef LV_USE_PPA_IMG
            /* PPA SRM handles 90-degree increment rotation in hardware */
            if(dsc->rotation != 0) {
                int32_t angle = dsc->rotation % 3600;
                if(angle < 0) angle += 3600;
                /* Only accept exact 90° multiples */
                if(angle != 0 && angle != 900 && angle != 1800 && angle != 2700) return 0;
                if(dsc->skew_x != 0 || dsc->skew_y != 0) return 0;
                if(dsc->opa < (lv_opa_t)LV_OPA_MAX) return 0;
                if(dsc->blend_mode != LV_BLEND_MODE_NORMAL) return 0;
                if(!ppa_src_cf_supported((lv_color_format_t)dsc->header.cf)) return 0;

                lv_draw_buf_t * dest_buf = t->target_layer->draw_buf;
                if(!ppa_buf_usable(dest_buf)) return 0;
                if(!ppa_dest_cf_supported((lv_color_format_t)dest_buf->header.cf)) return 0;

                /* SRM rotation gets higher priority than software */
                if(t->preference_score > 30) {
                    t->preference_score = 30;
                    t->preferred_draw_unit_id = draw_unit->idx;
                }
                s_ppa_img_accepted_eval_tasks++;
                return 1;
            }
#else
            if(dsc->rotation != 0) return 0;
#endif
            if(dsc->skew_x != 0 || dsc->skew_y != 0) return 0;

#ifdef LV_USE_PPA_IMG
            /* PPA SRM handles scale+translate (Ken Burns) with rotation=0 */
            if(!ppa_image_scale_is_identity(dsc)) {
                if(dsc->opa < (lv_opa_t)LV_OPA_MAX) {
                    ppa_log_image_reject(draw_unit, t, dsc, "scale-opa");
                    return 0;
                }
                if(dsc->blend_mode != LV_BLEND_MODE_NORMAL) {
                    ppa_log_image_reject(draw_unit, t, dsc, "scale-blend");
                    return 0;
                }
                if(!ppa_task_large_visible_band(t)) {
                    ppa_log_image_reject(draw_unit, t, dsc, "scale-small-visible");
                    return 0;
                }
                if(!ppa_src_cf_supported((lv_color_format_t)dsc->header.cf)) {
                    ppa_log_image_reject(draw_unit, t, dsc, "scale-src-cf");
                    return 0;
                }
                lv_draw_buf_t * scale_dest = t->target_layer->draw_buf;
                if(!ppa_buf_usable(scale_dest)) {
                    ppa_log_image_reject(draw_unit, t, dsc, "scale-dest-unusable");
                    return 0;
                }
                if(!ppa_dest_cf_supported((lv_color_format_t)scale_dest->header.cf)) {
                    ppa_log_image_reject(draw_unit, t, dsc, "scale-dest-cf");
                    return 0;
                }
                if(t->preference_score > 50) {
                    t->preference_score = 50;
                    t->preferred_draw_unit_id = draw_unit->idx;
                }
                s_ppa_img_accepted_eval_tasks++;
                return 1;
            }

#else
            if(!ppa_image_scale_is_identity(dsc)) return 0;
#endif
#ifdef LV_USE_PPA_IMG
            if(ppa_image_task_is_direct_overlay(t, dsc)) {
                if(t->preference_score > 45) {
                    t->preference_score = 45;
                    t->preferred_draw_unit_id = draw_unit->idx;
                }
                s_ppa_img_accepted_eval_tasks++;
                return 1;
            }
            if(!ppa_image_task_is_large_opaque_copy(t, dsc)) return 0;
            if(t->preference_score > 55) {
                t->preference_score = 55;
                t->preferred_draw_unit_id = draw_unit->idx;
            }
            s_ppa_img_accepted_eval_tasks++;
            return 1;
#endif
            return 0;
        }

        default:
            break;
    }

    return 0;
}

static int32_t ppa_dispatch(lv_draw_unit_t * draw_unit, lv_layer_t * layer)
{
    lv_draw_ppa_unit_t * u = (lv_draw_ppa_unit_t *)draw_unit;

    /* Already processing a task */
    if(u->task_act) {
        return LV_DRAW_UNIT_IDLE;
    }

    /* Allocate layer buffer once for all tasks in this batch */
    if(lv_draw_layer_alloc_buf(layer) == NULL) {
        return LV_DRAW_UNIT_IDLE;
    }

    lv_layer_t * target = NULL;
    lv_draw_buf_t * buf = NULL;
    int32_t task_count = 0;

    /* Process all available PPA tasks in one dispatch call */
    lv_draw_task_t * t = lv_draw_get_available_task(layer, NULL, draw_unit->idx);
    while(t && t->preferred_draw_unit_id == draw_unit->idx) {

        t->state = LV_DRAW_TASK_STATE_IN_PROGRESS;
        t->draw_unit = draw_unit;
        u->task_act = t;

        target = t->target_layer;
        buf = (target) ? target->draw_buf : NULL;

        if(buf != NULL && buf->data != NULL) {
            const lv_draw_image_dsc_t * perf_img_dsc =
                t->type == LV_DRAW_TASK_TYPE_IMAGE ? (const lv_draw_image_dsc_t *)t->draw_dsc : NULL;
            const bool overlay_perf = perf_img_dsc != NULL &&
                                      (lv_color_format_t)perf_img_dsc->header.cf == LV_COLOR_FORMAT_ARGB8888 &&
                                      lvgl_esphome_get_perf_logging_enabled();
            const bool direct_overlay =
                perf_img_dsc != NULL && ppa_image_task_is_direct_overlay(t, perf_img_dsc);
            const int64_t pre_start_us = overlay_perf ? esp_timer_get_time() : 0;
            lv_area_t sync_area;
            bool has_sync_area = lv_area_intersect(&sync_area, &t->area, &t->clip_area);
            /* Direct image copies/blends synchronize the complete source and
             * destination row window in lv_draw_img_ppa_core(). Repeating the
             * generic row-by-row sync here adds several milliseconds per frame. */
            if(has_sync_area && !direct_overlay) {
                lv_draw_ppa_cache_sync_area_to_memory(buf, &target->buf_area, &sync_area);
            }
            const int64_t handler_start_us = overlay_perf ? esp_timer_get_time() : 0;

            switch(t->type) {
                case LV_DRAW_TASK_TYPE_FILL:
                    s_ppa_fill_tasks++;
                    lv_draw_ppa_fill(t, (lv_draw_fill_dsc_t *)t->draw_dsc, &t->area);
                    break;
                case LV_DRAW_TASK_TYPE_IMAGE: {
                    s_ppa_img_tasks++;
                    lv_draw_image_dsc_t * img_dsc = (lv_draw_image_dsc_t *)t->draw_dsc;
#ifdef LV_USE_PPA_IMG
                    if(img_dsc->rotation != 0) {
                        lv_draw_ppa_img_rotate(t, img_dsc, &t->area);
                    } else if(!ppa_image_scale_is_identity(img_dsc) ||
                              img_dsc->skew_x != 0 || img_dsc->skew_y != 0) {
                        lv_draw_ppa_img_srm(t, img_dsc, &t->area);
                    } else if(ppa_image_task_is_alpha_overlay(t, img_dsc)) {
                        lv_draw_ppa_img(t, img_dsc, &t->area);
                    } else if(ppa_image_task_is_direct_overlay(t, img_dsc)) {
                        lv_draw_ppa_img_srm(t, img_dsc, &t->area);
                    } else if(ppa_image_task_is_large_opaque_copy(t, img_dsc)) {
                        /* A large 1:1 opaque artwork image is already decoded in
                         * the final size. Copy it with the blend client so SRM is
                         * reserved for real transforms. Keep non-artwork copies on
                         * the proven SRM path; boot snapshots were more stable
                         * there than on PPA blend. */
                        if(ppa_image_source_is_artwork_dma_buffer(img_dsc)) {
                            lv_draw_ppa_img(t, img_dsc, &t->area);
                        } else {
                            lv_draw_ppa_img_srm(t, img_dsc, &t->area);
                        }
                    } else
#endif
                    {
                        lv_draw_ppa_img(t, img_dsc, &t->area);
                    }
                    break;
                }
                default:
                    break;
            }
            const int64_t post_start_us = overlay_perf ? esp_timer_get_time() : 0;

            if(has_sync_area && !direct_overlay) {
                lv_draw_ppa_cache_sync_area_from_memory(buf, &target->buf_area, &sync_area);
            }
            if(overlay_perf) {
                const int64_t now_us = esp_timer_get_time();
                s_ppa_overlay_perf_count++;
                s_ppa_overlay_perf_pre_us += (uint64_t)(handler_start_us - pre_start_us);
                s_ppa_overlay_perf_handler_us += (uint64_t)(post_start_us - handler_start_us);
                s_ppa_overlay_perf_post_us += (uint64_t)(now_us - post_start_us);
            }
        }

        t->state = LV_DRAW_TASK_STATE_FINISHED;
        u->task_act = NULL;
        task_count++;

        /* Get next available task */
        t = lv_draw_get_available_task(layer, NULL, draw_unit->idx);
    }

    if(task_count > 0) {
        lv_draw_dispatch_request();
        return 1;
    }

    return LV_DRAW_UNIT_IDLE;
}

static int32_t ppa_delete(lv_draw_unit_t * draw_unit)
{
    LV_UNUSED(draw_unit);
    return 0;
}

#endif /* CONFIG_SOC_PPA_SUPPORTED */
