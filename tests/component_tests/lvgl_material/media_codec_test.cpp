#include "material_entity_control_codec.h"
#include "media_codec_cases.h"
#include <cassert>
#include <iostream>
using namespace esphome::lvgl_material;
int main() {
  MaterialEntityControlCodec codec;
  size_t peak = 0;
  for (const auto &test : cases) {
    ControlMessage message;
    const bool parsed = codec.parse_entity(test.wire, message);
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
    if (parsed != test.accepted) {
      std::cerr << test.name << " parse mismatch\n";
      return 1;
    }
    if (parsed) {
      char wire[2049];
      size_t length;
      assert(codec.serialize_entity(message, wire, sizeof(wire), length));
      ControlMessage again;
      assert(codec.parse_entity({wire, length}, again));
      std::cout << test.name << '\t' << wire << '\n';
    }
#else
    assert(!parsed);
#endif
    peak = std::max(peak, codec.high_water());
  }
  std::cerr << "PASS cases=" << std::size(cases) << " arena_peak=" << peak << '\n';
}
