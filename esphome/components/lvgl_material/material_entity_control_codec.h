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
                                         ArduinoJson::Allocator *allocator, bool allow_empty_choice = false,
                                         bool allow_cover_panel = false
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
                                         ,
                                         ControlTextRx *text_rx = nullptr
#endif
);
bool control_serialize_entity_json_allocated(const ControlMessage &message, char *output, size_t capacity,
                                             size_t &length, ArduinoJson::Allocator *allocator,
                                             bool allow_empty_choice = false, bool allow_cover_panel = false
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
                                             ,
                                             ControlTextValueView text_value = {}
#endif
);
#endif

#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
bool control_decode_panel(ArduinoJson::JsonObjectConst object, ControlMessage &message);
void control_encode_panel(ArduinoJson::JsonObject object, const ControlMessage &message);
bool control_parse_panel_bind_allocated(std::string_view payload, ControlText<32> &channel, uint8_t &types,
                                        ArduinoJson::Allocator *allocator);
#endif

#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
bool control_decode_detail(ArduinoJson::JsonObjectConst object, ControlMessage &message);
void control_encode_detail(ArduinoJson::JsonObject object, const ControlMessage &message);
bool control_parse_detail_bind_allocated(std::string_view payload, ControlText<32> &channel, uint8_t &profiles,
                                         ArduinoJson::Allocator *allocator);
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
bool control_parse_detail_range_bind_allocated(std::string_view payload, ControlText<32> &channel,
                                               ArduinoJson::Allocator *allocator);
#endif
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
bool control_parse_detail_color_bind_allocated(std::string_view payload, ControlText<32> &channel,
                                               ArduinoJson::Allocator *allocator);
#endif
#endif

#ifdef USE_LVGL_MATERIAL_EXTRA_CONTROLS
bool control_decode_extra(ArduinoJson::JsonObjectConst object, ControlMessage &message);
void control_encode_extra(ArduinoJson::JsonObject object, const ControlMessage &message);
bool control_parse_extra_features_allocated(std::string_view payload, ControlText<32> &channel, uint16_t &profiles,
                                            uint16_t configured, bool bind, ArduinoJson::Allocator *allocator);
#endif

#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
bool control_decode_media(ArduinoJson::JsonObjectConst object, ControlMessage &message);
void control_encode_media(ArduinoJson::JsonObject object, const ControlMessage &message);
#endif

#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS
bool control_decode_action(ArduinoJson::JsonObjectConst object, ControlMessage &message);
void control_encode_action(ArduinoJson::JsonObject object, const ControlMessage &message);
#endif

#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
bool control_decode_datetime(ArduinoJson::JsonObjectConst object, ControlMessage &message);
void control_encode_datetime(ArduinoJson::JsonObject object, const ControlMessage &message);
#endif

#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
bool control_decode_text(ArduinoJson::JsonObjectConst object, ControlMessage &message, ControlTextValueView &value);
void control_encode_text(ArduinoJson::JsonObject object, const ControlMessage &message, ControlTextValueView value);
#endif

#ifdef USE_LVGL_MATERIAL_INFO_AGENDA
bool control_decode_agenda(ArduinoJson::JsonObjectConst object, ControlMessage &message);
void control_encode_agenda(ArduinoJson::JsonObject object, const ControlMessage &message);
bool control_parse_info_agenda_bind_allocated(std::string_view payload, ControlText<32> &channel,
                                              ArduinoJson::Allocator *allocator);
#endif

// One owner-task transaction at a time. Exhaustion refuses the message; there
// is no fallback to the global heap and no retained DOM after a call returns.
#ifdef USE_LVGL_MATERIAL_INFO_STATISTICS
bool control_decode_statistics(ArduinoJson::JsonObjectConst object, ControlMessage &message);
void control_encode_statistics(ArduinoJson::JsonObject object, const ControlMessage &message);
bool control_parse_info_statistics_bind_allocated(std::string_view payload, ControlText<32> &channel,
                                                 ArduinoJson::Allocator *allocator);
#endif

#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
bool control_parse_action_parameters_bind_allocated(std::string_view payload, ControlText<32> &channel,
                                                    ArduinoJson::Allocator *allocator);
#endif

class MaterialEntityControlCodec : protected ArduinoJson::Allocator {
 public:
  bool parse(std::string_view payload, ControlMessage &output);
  bool serialize(const ControlMessage &message, char *output, size_t capacity, size_t &length);
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  bool parse_entity(std::string_view payload, ControlMessage &output, bool allow_empty_choice = false,
                    bool allow_cover_panel = false
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
                    ,
                    ControlTextRx *text_rx = nullptr
#endif
  );
  bool serialize_entity(const ControlMessage &message, char *output, size_t capacity, size_t &length,
                        bool allow_empty_choice = false, bool allow_cover_panel = false
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
                        ,
                        ControlTextValueView text_value = {}
#endif
  );
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
  bool parse_panel_bind(std::string_view payload, ControlText<32> &channel, uint8_t &types);
#endif
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
  bool parse_detail_bind(std::string_view payload, ControlText<32> &channel, uint8_t &profiles);
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
  bool parse_detail_range_bind(std::string_view payload, ControlText<32> &channel);
#endif
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
  bool parse_detail_color_bind(std::string_view payload, ControlText<32> &channel);
#endif
#endif
#ifdef USE_LVGL_MATERIAL_EXTRA_CONTROLS
  bool parse_extra_features(std::string_view payload, ControlText<32> &channel, uint16_t &profiles, uint16_t configured,
                            bool bind);
#endif
#ifdef USE_LVGL_MATERIAL_INFO_AGENDA
  bool parse_info_agenda_bind(std::string_view payload, ControlText<32> &channel);
#endif
#ifdef USE_LVGL_MATERIAL_INFO_STATISTICS
  bool parse_info_statistics_bind(std::string_view payload, ControlText<32> &channel);
#endif
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
  bool parse_action_parameters_bind(std::string_view payload, ControlText<32> &channel);
#endif
  size_t high_water() const { return this->high_water_; }

 protected:
  void *allocate(size_t size) override;
  void deallocate(void *pointer) override;
  void *reallocate(void *pointer, size_t size) override;
  struct alignas(std::max_align_t) Block {
    size_t size;
  };
#if defined(USE_LVGL_MATERIAL_FAN_CONTROLS) || defined(USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS) || \
    defined(USE_LVGL_MATERIAL_ROBOT_CONTROLS) || defined(USE_LVGL_MATERIAL_PANEL_CONTROLS)
  // Complete device catalogs need additional bounded JSON metadata, not pixels.
  alignas(std::max_align_t) std::array<uint8_t, 5120> arena_{};
#else
  alignas(std::max_align_t) std::array<uint8_t, 4096> arena_{};
#endif
  size_t used_{0};
  size_t high_water_{0};
  bool busy_{false};
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  bool generic_transaction_{false};
#endif
};

}  // namespace esphome::lvgl_material
