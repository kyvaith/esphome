#include "lvgl_gesture_router.h"

#include <algorithm>
#include <cstdlib>

namespace esphome::lvgl {

void GestureRouter::begin(int32_t x, int32_t y, GestureAxis allowed_axis, uint32_t now) {
  this->begin(x, y, allowed_axis, this->start_distance_, this->axis_bias_, now);
}

void GestureRouter::begin(int32_t x, int32_t y, GestureAxis allowed_axis, uint16_t start_distance, uint16_t axis_bias,
                          uint32_t now) {
  this->allowed_axis_ = allowed_axis;
  this->active_start_distance_ = start_distance;
  this->active_axis_bias_ = axis_bias;
  this->sample_ = {
      .start_x = x,
      .start_y = y,
      .x = x,
      .y = y,
  };
  this->last_x_ = x;
  this->last_y_ = y;
  this->start_ms_ = now;
  this->last_update_ms_ = now;
  this->tracking_ = allowed_axis != GestureAxis::NONE;
  this->rejected_ = false;
}

const GestureSample &GestureRouter::update(int32_t x, int32_t y, uint32_t now) {
  const uint32_t elapsed = now - this->last_update_ms_;
  if (this->tracking_ && elapsed > 0 && elapsed <= 120) {
    const int32_t instantaneous_x = ((x - this->last_x_) * 1000) / static_cast<int32_t>(elapsed);
    const int32_t instantaneous_y = ((y - this->last_y_) * 1000) / static_cast<int32_t>(elapsed);
    if (this->sample_.velocity_x == 0 && this->sample_.velocity_y == 0) {
      this->sample_.velocity_x = instantaneous_x;
      this->sample_.velocity_y = instantaneous_y;
    } else {
      // Favor the latest sample so a short watch-style flick is not diluted
      // by the stationary touch-down point.
      this->sample_.velocity_x = (this->sample_.velocity_x + instantaneous_x * 2) / 3;
      this->sample_.velocity_y = (this->sample_.velocity_y + instantaneous_y * 2) / 3;
    }
  } else if (elapsed > 120) {
    this->sample_.velocity_x = 0;
    this->sample_.velocity_y = 0;
  }
  this->last_x_ = x;
  this->last_y_ = y;
  this->last_update_ms_ = now;
  this->sample_.x = x;
  this->sample_.y = y;
  this->sample_.delta_x = x - this->sample_.start_x;
  this->sample_.delta_y = y - this->sample_.start_y;
  this->sample_.just_captured = false;

  if (!this->tracking_ || this->sample_.captured || this->rejected_)
    return this->sample_;

  const int32_t abs_x = std::abs(this->sample_.delta_x);
  const int32_t abs_y = std::abs(this->sample_.delta_y);
  if (std::max(abs_x, abs_y) < this->active_start_distance_)
    return this->sample_;

  const bool horizontal = abs_x > abs_y + this->active_axis_bias_;
  const bool vertical = abs_y > abs_x + this->active_axis_bias_;
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

GestureSample GestureRouter::finish(uint32_t now) {
  const uint32_t gesture_elapsed = now - this->start_ms_;
  if (gesture_elapsed > 0 && gesture_elapsed <= 500) {
    const int32_t average_x = (this->sample_.delta_x * 1000) / static_cast<int32_t>(gesture_elapsed);
    const int32_t average_y = (this->sample_.delta_y * 1000) / static_cast<int32_t>(gesture_elapsed);
    if ((this->sample_.velocity_x == 0 || (average_x < 0) == (this->sample_.velocity_x < 0)) &&
        std::abs(average_x) > std::abs(this->sample_.velocity_x)) {
      this->sample_.velocity_x = average_x;
    }
    if ((this->sample_.velocity_y == 0 || (average_y < 0) == (this->sample_.velocity_y < 0)) &&
        std::abs(average_y) > std::abs(this->sample_.velocity_y)) {
      this->sample_.velocity_y = average_y;
    }
  }
  if (now - this->last_update_ms_ > 120) {
    this->sample_.velocity_x = 0;
    this->sample_.velocity_y = 0;
  }
  const GestureSample result = this->sample_;
  this->cancel();
  return result;
}

void GestureRouter::capture(GestureAxis axis) {
  if (!this->tracking_)
    return;
  this->sample_.axis = axis;
  this->sample_.captured = true;
  this->sample_.just_captured = true;
  this->rejected_ = false;
}

void GestureRouter::cancel() {
  this->allowed_axis_ = GestureAxis::NONE;
  this->sample_ = {};
  this->last_x_ = 0;
  this->last_y_ = 0;
  this->start_ms_ = 0;
  this->last_update_ms_ = 0;
  this->tracking_ = false;
  this->rejected_ = false;
}

}  // namespace esphome::lvgl
