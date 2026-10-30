#pragma once
#include "scalar_test_support.h"

// New public LVGL API used by the native presenter; the existing facade's font
// pointer models its effect. No production ownership decision is implemented here.
inline void lv_obj_set_style_text_font(lv_obj_t *object, const lv_font_t *font, int) { object->font = font; }

// Native input scheduling is exercised against public LVGL in the separate
// real-input regression. This pre-existing scalar facade has no input reader.
inline void lv_indev_wait_release(lv_indev_t *) {}
inline void lv_indev_reset(lv_indev_t *, lv_obj_t *) {}
