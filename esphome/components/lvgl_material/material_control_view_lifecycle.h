#pragma once

#ifdef USE_LVGL_NAVIGATION_ADMISSION_GUARD
#include "esphome/components/lvgl/lvgl_navigation.h"
#include <cstdint>

namespace esphome::lvgl_material {

class MaterialEntityControlComponent;

// The presenter alone owns pixels, sources and framebuffer/pause leases.
// The view lifecycle owns only the captured admission generations.
class ControlViewBackend {
 public:
  virtual ~ControlViewBackend() = default;
  virtual bool suspend_control_view_backend(uint32_t timeout_ms) = 0;
  virtual bool resume_control_view_backend(uint32_t captured_generation) = 0;
  virtual bool hide_control_view_alias() = 0;
  virtual bool control_view_region_visible() const = 0;
  // Native backends without a concurrent regional renderer need no fence.
  virtual bool has_native_power_fence() const { return false; }
  virtual bool begin_native_power_input(uint32_t) { return true; }
  virtual bool finish_native_power_input(uint32_t) { return true; }
};

class ControlViewLifecycle {
 public:
  explicit ControlViewLifecycle(MaterialEntityControlComponent *control) : control_(control) {}
  bool configure(ControlViewBackend *backend, lvgl::LvglNavigation *navigation, lvgl::LvglApplication *application);
  bool is_backend(const ControlViewBackend *backend) const { return this->backend_ == backend; }
  void attach_application_callbacks();
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  // The owner has already suspended/drained the previous backend and hidden its
  // alias. This switches presentation only; it never copies pixels or tokens.
  bool handoff_backend(ControlViewBackend *previous, ControlViewBackend *next, bool retain_generation = false);
#endif
  bool suspend_for_snapshot(uint32_t timeout_ms = 250);
  bool resume(uint32_t captured_generation);
  bool prepare_navigation(lvgl::NavigationAdmissionKind kind, lvgl::LvglApplication *application);
  bool suspend_for_direct_overlay();
  void resume_after_direct_overlay();
  bool begin_native_power_input(uint32_t captured_generation);
  void finish_native_power_input(uint32_t captured_generation);
  bool native_power_input_active() const { return this->power_generation_ != 0; }
  void cancel_native_power_input() {
    this->power_generation_ = 0;
    this->power_end_pending_ = false;
  }
  // Called at the original presenter's loop position; no new scheduled work.
  void loop();
  uint32_t generation() const { return this->generation_; }
  uint32_t overlay_generation() const { return this->overlay_generation_; }
  void set_generation(uint32_t generation) { this->generation_ = generation; }
  void cancel_close_recovery() { this->cancelled_close_generation_ = 0; }

 protected:
  MaterialEntityControlComponent *control_;
  ControlViewBackend *backend_{nullptr};
  lvgl::LvglNavigation *navigation_{nullptr};
  lvgl::LvglApplication *application_{nullptr};
  uint32_t generation_{0};
  uint32_t overlay_generation_{0};
  uint32_t closing_generation_{0};
  uint32_t cancelled_close_generation_{0};
  uint32_t power_generation_{0};
  bool power_end_pending_{false};
};

}  // namespace esphome::lvgl_material
#endif
