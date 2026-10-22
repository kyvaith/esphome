/**
 * @file lvgl_ppa_accel_v9.h
 * LVGL v9 PPA hardware blend handler (Espressif esp-iot-solution port)
 *
 * Registers a custom SW blend handler that routes RGB565 blend/fill
 * operations to ESP32-P4 PPA. Complements the higher-level draw unit
 * in lv_draw_ppa.c by accelerating all SW blend code paths (text,
 * gradients-after-rasterize, rect edges, etc.).
 */

#pragma once
// namespace esphome::lvgl -- ESPHome lint marker; this header exposes C linkage.

#ifndef LVGL_PPA_ACCEL_V9_H
#define LVGL_PPA_ACCEL_V9_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

void lvgl_port_ppa_v9_init(lv_display_t *display);

// Fill a complete external XRGB/ARGB8888 surface through the shared PPA fill
// client. The caller keeps a CPU fallback for early LVGL startup.
bool lvgl_port_ppa_v9_fill_argb8888(void *buffer, size_t buffer_size, uint32_t width, uint32_t height,
                                    uint32_t argb_color);

#ifdef __cplusplus
}
#endif

#endif /* LVGL_PPA_ACCEL_V9_H */
