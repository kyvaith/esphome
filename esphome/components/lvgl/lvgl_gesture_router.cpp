#include "lvgl_gesture_router.h"

#include <algorithm>
#include <cstdlib>

namespace esphome::lvgl {

void GestureRouter::begin(int32_t x, int32_t y, GestureAxis allowed_axis) {
  this->allowed_axis_ = allowed_axis;
  this->sample_ = {
      .start_x = x,
      .start_y = y,
      .x = x,
      .y = y,
  };
  this->tracking_ = allowed_axis != GestureAxis::NONE;
  this->rejected_ = false;
}

const GestureSample &GestureRouter::update(int32_t x, int32_t y) {
  this->sample_.x = x;
  this->sample_.y = y;
  this->sample_.delta_x = x - this->sample_.start_x;
  this->sample_.delta_y = y - this->sample_.start_y;
  this->sample_.just_captured = false;

  if (!this->tracking_ || this->sample_.captured || this->rejected_)
    return this->sample_;

  const int32_t abs_x = std::abs(this->sample_.delta_x);
  const int32_t abs_y = std::abs(this->sample_.delta_y);
  if (std::max(abs_x, abs_y) < this->start_distance_)
    return this->sample_;

  const bool horizontal = abs_x > abs_y + this->axis_bias_;
  const bool vertical = abs_y > abs_x + this->axis_bias_;
  const bool allowed = (this->allowed_axis_ == GestureAxis::HORIZONTAL && horizontal) ||
                       (this->allowed_axis_ == GestureAxis::VERTICAL && vertical);
  if (allowed) {
    this->sample_.axis = this->allowed_axis_;
    this->sample_.captured = true;
    this->sample_.just_captured = true;
  } else if ((horizontal || vertical) && !allowed) {
    // Once the user has clearly selected the other axis, leave the gesture to
    // LVGL for the rest of this touch. This avoids stealing vertical list
    // scrolling from a horizontal home-page router.
    this->rejected_ = true;
  }
  return this->sample_;
}

GestureSample GestureRouter::finish() {
  const GestureSample result = this->sample_;
  this->cancel();
  return result;
}

void GestureRouter::cancel() {
  this->allowed_axis_ = GestureAxis::NONE;
  this->sample_ = {};
  this->tracking_ = false;
  this->rejected_ = false;
}

}  // namespace esphome::lvgl
