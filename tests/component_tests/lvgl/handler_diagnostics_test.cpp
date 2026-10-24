#include "esphome/components/lvgl/handler_diagnostics.h"

#include <cassert>
#include <cstdint>

int main() {
  esphome::lvgl::HandlerDiagnostics stats;
  assert(stats.calls == 0);
  stats.note(20, 50, true);
  stats.note(40, 200, false);
  stats.note(10, 70, true);
  assert(stats.calls == 3 && stats.no_render_calls == 2);
  assert(stats.lock_us == 70 && stats.body_us == 320 && stats.no_render_body_us == 120);
  assert(stats.lock_max_us == 40 && stats.body_max_us == 200);
  stats = {};
  stats.note(UINT32_MAX, UINT32_MAX, true);
  stats.note(UINT32_MAX, UINT32_MAX, true);
  assert(stats.calls == 2 && stats.no_render_calls == 2);
  assert(stats.lock_us == uint64_t{UINT32_MAX} * 2 && stats.body_us == uint64_t{UINT32_MAX} * 2);
  assert(stats.no_render_body_us == uint64_t{UINT32_MAX} * 2);
  assert(stats.lock_max_us == UINT32_MAX && stats.body_max_us == UINT32_MAX);
  static_assert(sizeof(stats) <= 48);
}
