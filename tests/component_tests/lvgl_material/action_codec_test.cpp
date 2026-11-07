#include "material_entity_control_codec.h"
#include "action_codec_cases.h"
#include <cassert>
#include <iostream>

using namespace esphome::lvgl_material;

static_assert(static_cast<unsigned>(ControlResult::CONTROL_RESULT_PENDING) == 0);
static_assert(static_cast<unsigned>(ControlResult::CONTROL_RESULT_HA_ACCEPTED) == 1);
static_assert(static_cast<unsigned>(ControlResult::CONTROL_RESULT_STATE_MATCHES) == 2);
static_assert(static_cast<unsigned>(ControlResult::CONTROL_RESULT_ALREADY_MATCHED) == 3);
static_assert(static_cast<unsigned>(ControlResult::CONTROL_RESULT_NOT_CONFIRMED) == 4);
static_assert(static_cast<unsigned>(ControlResult::CONTROL_RESULT_REJECTED) == 5);
static_assert(static_cast<unsigned>(ControlResult::CONTROL_RESULT_SERVICE_COMPLETED) == 6);
#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS
static_assert(static_cast<unsigned>(ControlResult::CONTROL_RESULT_SERVICE_DISPATCHED) == 7);
#endif

int main() {
  MaterialEntityControlCodec codec;
  size_t peak = 0;
  for (const auto &test : cases) {
    ControlMessage message;
    const bool decoded = codec.parse_entity(test.wire, message);
#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS
    const bool expected = test.accepted;
#else
    const bool expected = test.accepted && test.common;
#endif
    if (decoded != expected) {
      std::cerr << "decode mismatch " << test.name << " got=" << decoded << " expected=" << expected << '\n';
      return 1;
    }
    if (decoded) {
      char output[CONTROL_MAX_PAYLOAD_BYTES + 1];
      size_t length = 0;
      assert(codec.serialize_entity(message, output, sizeof(output), length));
      ControlMessage again;
      assert(codec.parse_entity({output, length}, again));
      assert(message.kind == again.kind && message.identity == again.identity && message.state == again.state);
      assert(message.operation == again.operation &&
             message.expected_state_generation == again.expected_state_generation);
      std::cout << test.name << '\t' << output << '\n';
    }
    peak = std::max(peak, codec.high_water());
  }
  std::cerr << "PASS cases=" << std::size(cases) << " arena_peak=" << peak << '\n';
}
