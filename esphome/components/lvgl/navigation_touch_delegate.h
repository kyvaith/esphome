#pragma once

#include <cstdint>

namespace esphome::lvgl {

// Optional modal/edge recognizer. Reservation defers LVGL press; only a false
// release permits the normal tap replay. Never feed one contact to two owners.
class NavigationTouchDelegate {
 public:
  virtual bool touch_begin(int32_t x, int32_t y) = 0;
  virtual bool touch_update(int32_t x, int32_t y) = 0;
  virtual bool touch_end() = 0;
  virtual void touch_cancel() = 0;
};

}  // namespace esphome::lvgl
