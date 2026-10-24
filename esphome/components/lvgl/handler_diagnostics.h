#pragma once

#include <cstdint>

namespace esphome::lvgl {

// Wall times include task preemption. No render, flush or CPU metric is adjusted.
struct HandlerDiagnostics {
  uint64_t lock_us{0};
  uint64_t body_us{0};
  uint64_t no_render_body_us{0};
  uint32_t calls{0};
  uint32_t no_render_calls{0};
  uint32_t lock_max_us{0};
  uint32_t body_max_us{0};

  void note(uint32_t lock, uint32_t body, bool no_render) {
    this->calls++;
    this->lock_us += lock;
    this->body_us += body;
    if (lock > this->lock_max_us)
      this->lock_max_us = lock;
    if (body > this->body_max_us)
      this->body_max_us = body;
    if (no_render) {
      this->no_render_calls++;
      this->no_render_body_us += body;
    }
  }
};

}  // namespace esphome::lvgl
