/**
 * @file lv_draw_ppa.h
 * Custom PPA draw unit header for ESP32-P4
 * Based on https://github.com/lvgl/lvgl/pull/9162 (included in LVGL 9.5+)
 */

#pragma once
// namespace esphome::lvgl -- ESPHome lint marker; this header exposes C linkage.

#ifndef LV_DRAW_PPA_FIXED_H
#define LV_DRAW_PPA_FIXED_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include "lvgl.h"
#include "src/draw/lv_draw_private.h"
#include "src/display/lv_display_private.h"
#include "src/misc/lv_area_private.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 * GLOBAL PROTOTYPES
 **********************/

void lv_draw_ppa_init(void);
void lv_draw_ppa_deinit(void);
void lv_draw_buf_ppa_init_handlers(void);
uint32_t lv_draw_ppa_get_fill_task_count(void);
uint32_t lv_draw_ppa_get_img_task_count(void);
uint32_t lv_draw_ppa_get_img_eval_count(void);
uint32_t lv_draw_ppa_get_img_large_eval_count(void);
uint32_t lv_draw_ppa_get_img_accepted_eval_count(void);
uint32_t lv_draw_ppa_get_img_srm_task_count(void);
uint32_t lv_draw_ppa_get_img_srm_large_task_count(void);
uint32_t lv_draw_ppa_get_img_srm_unaligned_task_count(void);
uint64_t lv_draw_ppa_get_img_srm_unaligned_bytes(void);
uint64_t lv_draw_ppa_get_img_srm_copy_us(void);
uint32_t lv_draw_ppa_get_img_srm_copy_max_us(void);
uint64_t lv_draw_ppa_get_img_srm_ppa_us(void);
uint32_t lv_draw_ppa_get_img_srm_ppa_max_us(void);

void lv_draw_ppa_fill(lv_draw_task_t * t, const lv_draw_fill_dsc_t * dsc,
                      const lv_area_t * coords);

void lv_draw_ppa_img(lv_draw_task_t * t, const lv_draw_image_dsc_t * dsc,
                     const lv_area_t * coords);

#ifdef LV_USE_PPA_IMG
void lv_draw_ppa_img_rotate(lv_draw_task_t * t, const lv_draw_image_dsc_t * dsc,
                            const lv_area_t * coords);
void lv_draw_ppa_img_srm(lv_draw_task_t * t, const lv_draw_image_dsc_t * dsc,
                         const lv_area_t * coords);
#endif

void lv_draw_ppa_cache_sync_to_memory(lv_draw_buf_t * buf);
void lv_draw_ppa_cache_sync_from_memory(lv_draw_buf_t * buf);
void lv_draw_ppa_cache_sync_area_to_memory(lv_draw_buf_t * buf, const lv_area_t * buf_area,
                                           const lv_area_t * area);
void lv_draw_ppa_cache_sync_area_from_memory(lv_draw_buf_t * buf, const lv_area_t * buf_area,
                                             const lv_area_t * area);

/**********************
 *      MACROS
 **********************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /* LV_DRAW_PPA_FIXED_H */
