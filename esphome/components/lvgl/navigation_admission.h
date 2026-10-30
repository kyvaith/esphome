#pragma once

#include <cstdint>

namespace esphome::lvgl {

class LvglApplication;

enum class NavigationAdmissionKind : uint8_t { OPEN, CLOSE, HOME, CAPTURE, TAKEOVER };

// Opt-in synchronous preflight. Returning false leaves navigation ownership
// unchanged. The guard retains every resource that has not safely drained.
class NavigationAdmissionGuard {
 public:
  virtual bool prepare_navigation(NavigationAdmissionKind kind, LvglApplication *application) = 0;
};

}  // namespace esphome::lvgl
