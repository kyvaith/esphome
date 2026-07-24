#pragma once

#include <cstdint>

namespace esphome::lvgl {

enum class GestureAxis : uint8_t {
  NONE,
  HORIZONTAL,
  VERTICAL,
};

struct GestureSample {
  int32_t start_x{};
  int32_t start_y{};
  int32_t x{};
  int32_t y{};
  int32_t delta_x{};
  int32_t delta_y{};
  GestureAxis axis{GestureAxis::NONE};
  bool captured{};
  bool just_captured{};
};

// Classifies one touch stream without imposing navigation behavior.
class GestureRouter {
 public:
  void set_start_distance(uint16_t start_distance) { this->start_distance_ = start_distance; }
  void set_axis_bias(uint16_t axis_bias) { this->axis_bias_ = axis_bias; }

  void begin(int32_t x, int32_t y, GestureAxis allowed_axis);
  void begin(int32_t x, int32_t y, GestureAxis allowed_axis, uint16_t start_distance, uint16_t axis_bias);
  const GestureSample &update(int32_t x, int32_t y);
  GestureSample finish();
  void cancel();

  const GestureSample &sample() const { return this->sample_; }
  bool is_tracking() const { return this->tracking_; }

 protected:
  uint16_t start_distance_{10};
  uint16_t axis_bias_{6};
  uint16_t active_start_distance_{10};
  uint16_t active_axis_bias_{6};
  GestureAxis allowed_axis_{GestureAxis::NONE};
  GestureSample sample_{};
  bool tracking_{};
  bool rejected_{};
};

}  // namespace esphome::lvgl
