#pragma once

#include "esphome/components/lvgl/lvgl_navigation.h"
#include "esphome/components/network_camera/network_camera.h"
#include "esphome/core/component.h"

#include <array>
#include <string>

namespace esphome::camera_preview {

class CameraPreview : public Component {
 public:
  CameraPreview(network_camera::NetworkCamera *camera, lvgl::LvglApplication *application)
      : camera_(camera), application_(application) {}

  void setup() override;
  bool apply_catalog(const std::string &payload);
  const char *open(const std::string &source_key, const std::string &event_id, int duration_seconds);
  bool active() const { return this->active_; }

 protected:
  void check_preview_();
  void finish_();

  network_camera::NetworkCamera *camera_;
  lvgl::LvglApplication *application_;
  std::array<std::array<char, 32>, 4> recent_events_{};
  uint32_t started_ms_{};
  uint32_t duration_ms_{};
  size_t next_event_{};
  bool active_{};
};

}  // namespace esphome::camera_preview
