#pragma once

#include <cstdint>
#include <lvgl.h>

namespace esphome::lvgl {

// Optional widget-owned identity captured before a deferred Home/scroll press.
// The custom event has no pressed style, rendering, allocation or click effect.
struct DeferredPressContext {
  uint64_t generation{};
  bool capturing{true};
  bool valid{true};
};

inline lv_event_code_t deferred_press_context_event() {
  static const auto event = static_cast<lv_event_code_t>(lv_event_register_id());
  return event;
}

}  // namespace esphome::lvgl
