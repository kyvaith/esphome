#pragma once
#include "real_lvgl_test_support.h"
#include <cmath>
inline void assert_readable_disabled_control(esphome::lvgl::LvglComponent &display, lv_obj_t *button, lv_obj_t *text) {
  if (!lv_obj_is_visible(button) || !lv_obj_has_state(button, LV_STATE_DISABLED))
    return;
  lv_refr_now(lv_obj_get_display(button));
  lv_area_t bounds, caption;
  lv_obj_get_coords(button, &bounds);
  lv_obj_get_coords(text, &caption);
  const auto luminance = [&](int x, int y) {
    assert(x >= 0 && x < display.get_width() && y >= 0 && y < display.get_height());
    const size_t offset = (static_cast<size_t>(y) * display.get_width() + x) * 3;
    double sum = 0;
    for (size_t channel = 0; channel < 3; channel++) {
      const double value = display.presented_pixels[offset + channel] / 255.0;
      const double linear = value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
      // RGB888 is stored BGR by this LVGL software renderer.
      sum += linear * (channel == 0 ? 0.0722 : channel == 1 ? 0.7152 : 0.2126);
    }
    return sum;
  };
  const double background = luminance((bounds.x1 + bounds.x2) / 2, bounds.y2 - 10);
  double foreground = background;
  for (int y = std::max(caption.y1, bounds.y1 + 2); y <= std::min(caption.y2, bounds.y2 - 2); y++)
    for (int x = std::max(caption.x1, bounds.x1 + 2); x <= std::min(caption.x2, bounds.x2 - 2); x++)
      foreground = std::max(foreground, luminance(x, y));
  assert(background < 0.08);
  assert((foreground + 0.05) / (background + 0.05) >= 4.5);
}
