#pragma once

#include "material_entity_control.h"
#include <ArduinoJson.h>
#include <array>
#include <cstddef>

namespace esphome::lvgl_material {

bool control_parse_json_allocated(std::string_view payload, ControlMessage &output, ArduinoJson::Allocator *allocator);
bool control_serialize_json_allocated(const ControlMessage &message, char *output, size_t capacity, size_t &length,
                                      ArduinoJson::Allocator *allocator);

#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
bool control_parse_entity_json_allocated(std::string_view payload, ControlMessage &output,
                                         ArduinoJson::Allocator *allocator);
bool control_serialize_entity_json_allocated(const ControlMessage &message, char *output, size_t capacity,
                                             size_t &length, ArduinoJson::Allocator *allocator);
#endif

// One owner-task transaction at a time. Exhaustion refuses the message; there
// is no fallback to the global heap and no retained DOM after a call returns.
class MaterialEntityControlCodec : protected ArduinoJson::Allocator {
 public:
  bool parse(std::string_view payload, ControlMessage &output);
  bool serialize(const ControlMessage &message, char *output, size_t capacity, size_t &length);
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  bool parse_entity(std::string_view payload, ControlMessage &output);
  bool serialize_entity(const ControlMessage &message, char *output, size_t capacity, size_t &length);
#endif
  size_t high_water() const { return this->high_water_; }

 protected:
  void *allocate(size_t size) override;
  void deallocate(void *pointer) override;
  void *reallocate(void *pointer, size_t size) override;
  struct alignas(std::max_align_t) Block {
    size_t size;
  };
  alignas(std::max_align_t) std::array<uint8_t, 4096> arena_{};
  size_t used_{0};
  size_t high_water_{0};
  bool busy_{false};
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  bool generic_transaction_{false};
#endif
};

}  // namespace esphome::lvgl_material
