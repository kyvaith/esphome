#include "material_entity_control_codec.h"

#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER

#include <ArduinoJson.h>
#include <cstring>
#include <initializer_list>

namespace esphome::lvgl_material {
namespace {

using ArduinoJson::JsonObjectConst;
using ArduinoJson::JsonVariantConst;
static constexpr const char *KINDS[] = {
    "capability", "bind",         "open", "cancel_open", "grant", "state", "command", "result", "close",
#if defined(USE_LVGL_MATERIAL_DETAIL_CONTROLS) || defined(USE_LVGL_MATERIAL_MEDIA_CONTROLS)
    "facet",      "facet_result",
#endif
#ifdef USE_LVGL_MATERIAL_INFO_AGENDA
    "page",       "page_result",
#endif
};
#ifdef USE_LVGL_MATERIAL_THERMOSTAT
static constexpr const char *THERMOSTAT_ACTIVITIES[] = {"unknown", "off", "idle",       "heating",   "cooling",
                                                        "drying",  "fan", "preheating", "defrosting"};
#endif
static constexpr const char *OPERATIONS[] = {
    "brightness",   "turn_on",      "turn_off",  "step",
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
    "choose",
#endif
#ifdef USE_LVGL_MATERIAL_COVER_CONTROLS
    "open",         "stop",         "close",
#ifdef USE_LVGL_MATERIAL_COVER_PANEL
    "set_position", "open_tilt",    "stop_tilt", "close_tilt",  "set_tilt_position",
#endif
#endif
#ifdef USE_LVGL_MATERIAL_THERMOSTAT
    "set_target",
#endif
#ifdef USE_LVGL_MATERIAL_FAN_CONTROLS
    "power",        "speed",        "preset",    "oscillation", "direction",
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS
    "power",        "humidity",     "mode",
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
    "start",        "pause",        "stop",      "dock",        "locate",
    "spot",         "speed",        "area",
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
    "start",        "resume",       "pause",     "cancel",      "set_duration",
    "power",        "activity",
#endif
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
    "facet_number", "facet_choice",
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
    "facet_color",
#endif
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
    "facet_range",
#endif
#endif
#ifdef USE_LVGL_MATERIAL_EXTRA_CONTROLS
    "lock",
#endif
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
    "play",         "pause",        "previous",  "next",        "stop",
    "turn_on",      "turn_off",     "volume",    "volume_up",   "volume_down",
    "mute",         "shuffle",      "choice",
#endif
#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS
    "press",        "activate",     "run",       "stop",        "enable",
    "disable",      "trigger",
#endif
#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
    "set_datetime",
#endif
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
    "set_value",
#endif
};
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
static constexpr const char *ROBOT_TYPES[] = {"vacuum", "lawn_mower"};
static constexpr const char *ROBOT_ACTIVITIES[] = {"unknown", "cleaning",  "docked", "idle",
                                                   "paused",  "returning", "error",  "mowing"};
static constexpr const char *ROBOT_OPERATIONS[] = {"start", "pause", "stop", "dock", "locate", "spot", "speed", "area"};
#endif
#ifdef USE_LVGL_MATERIAL_COVER_CONTROLS
static constexpr const char *COVER_STATES[] = {"unknown", "open", "opening", "closed", "closing"};
#endif
#ifdef USE_LVGL_MATERIAL_COVER_PANEL
static constexpr const char *COVER_CLASSES[] = {"unknown", "awning", "blind", "curtain", "damper", "door",
                                                "garage",  "gate",   "shade", "shutter", "window"};
#endif
static constexpr const char *RESULTS[] = {
    "pending",
    "ha_accepted",
    "state_matches",
    "already_matched",
    "not_confirmed",
    "rejected",
#if defined(USE_LVGL_MATERIAL_ROBOT_CONTROLS) || defined(USE_LVGL_MATERIAL_PANEL_CONTROLS)
    "service_completed",
#endif
#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS
    "service_dispatched",
#endif
};
static constexpr const char *REASONS[] = {"none",
                                          "service_error",
                                          "service_timeout",
                                          "state_timeout",
                                          "selection_changed",
                                          "connection_lost",
                                          "entity_unavailable",
                                          "unsupported",
                                          "expired",
                                          "sequence_conflict",
                                          "stale_sequence",
                                          "busy",
                                          "unauthorized",
                                          "user",
                                          "navigation",
                                          "sleep",
                                          "voice",
                                          "capabilities_changed",
                                          "unload",
                                          "stale_state"};

template<size_t N, typename T> bool enum_value(JsonVariantConst value, const char *const (&names)[N], T &output) {
  if (!value.is<const char *>())
    return false;
  const auto string = value.as<ArduinoJson::JsonString>();
  const std::string_view text(string.c_str(), string.size());
  for (size_t i = 0; i < N; i++) {
    if (text == names[i]) {
      output = static_cast<T>(i);
      return true;
    }
  }
  return false;
}

bool fields(JsonObjectConst object, std::initializer_list<const char *> keys, bool has_extension = false) {
  if (object.isNull() || object.size() != keys.size() + has_extension)
    return false;
  for (const char *key : keys) {
    bool found = false;
    for (auto pair : object)
      found |= std::string_view(pair.key().c_str(), pair.key().size()) == key;
    if (!found)
      return false;
  }
  return true;
}

bool uint_value(JsonVariantConst value, uint64_t high, uint64_t &output) {
  if (value.is<bool>() || !value.is<uint64_t>())
    return false;
  output = value.as<uint64_t>();
  return output <= high;
}

bool sequence(JsonVariantConst value, uint32_t &output) {
  uint64_t number;
  if (!uint_value(value, CONTROL_MAX_SEQUENCE, number) || number == 0)
    return false;
  output = static_cast<uint32_t>(number);
  return true;
}

// Reject invalid UTF-8, NUL/C0/DEL, overlong forms and surrogate encodings.
bool plain_utf8(std::string_view value) {
  for (size_t i = 0; i < value.size();) {
    const auto first = static_cast<uint8_t>(value[i++]);
    if (first < 0x20 || first == 0x7f)
      return false;
    if (first < 0x80)
      continue;
    uint32_t point;
    size_t tail;
    if (first >= 0xc2 && first <= 0xdf) {
      point = first & 0x1f;
      tail = 1;
    } else if (first >= 0xe0 && first <= 0xef) {
      point = first & 0x0f;
      tail = 2;
    } else if (first >= 0xf0 && first <= 0xf4) {
      point = first & 7;
      tail = 3;
    } else
      return false;
    if (i + tail > value.size())
      return false;
    for (size_t n = 0; n < tail; n++) {
      const auto next = static_cast<uint8_t>(value[i++]);
      if ((next & 0xc0) != 0x80)
        return false;
      point = (point << 6) | (next & 0x3f);
    }
    if ((tail == 2 && point < 0x800) || (tail == 3 && point < 0x10000) || point > 0x10ffff ||
        (point >= 0xd800 && point <= 0xdfff))
      return false;
  }
  return true;
}

template<size_t N> bool string_value(JsonVariantConst value, ControlText<N> &output) {
  if (!value.is<ArduinoJson::JsonString>())
    return false;
  const auto text = value.as<ArduinoJson::JsonString>();
  const std::string_view view(text.c_str(), text.size());
  return plain_utf8(view) && output.assign(view);
}

bool close_reason(ControlReason reason) {
  switch (reason) {
    case ControlReason::CONTROL_REASON_USER:
    case ControlReason::CONTROL_REASON_NAVIGATION:
    case ControlReason::CONTROL_REASON_SLEEP:
    case ControlReason::CONTROL_REASON_VOICE:
    case ControlReason::CONTROL_REASON_SELECTION_CHANGED:
    case ControlReason::CONTROL_REASON_CONNECTION_LOST:
    case ControlReason::CONTROL_REASON_ENTITY_UNAVAILABLE:
    case ControlReason::CONTROL_REASON_CAPABILITIES_CHANGED:
    case ControlReason::CONTROL_REASON_EXPIRED:
    case ControlReason::CONTROL_REASON_UNLOAD:
      return true;
    default:
      return false;
  }
}

bool decode(JsonObjectConst object, ControlMessage &message, [[maybe_unused]] bool allow_empty_choice,
            [[maybe_unused]] bool allow_cover_panel
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
            ,
            ControlTextValueView *text_value = nullptr
#endif
) {
  uint64_t number;
  if (!uint_value(object["v"], 1, number) || number != 1 || !object["type"].is<const char *>() ||
      object["type"].as<ArduinoJson::JsonString>() != "entity_control_v1" ||
      !enum_value(object["kind"], KINDS, message.kind))
    return false;
  if (message.kind == ControlKind::CONTROL_KIND_CAPABILITY) {
    if (!fields(object, {"v", "type", "kind", "enabled", "max_payload_bytes", "control_kinds"}) ||
        !object["enabled"].is<bool>() || !uint_value(object["max_payload_bytes"], CONTROL_MAX_PAYLOAD_BYTES, number) ||
        number != CONTROL_MAX_PAYLOAD_BYTES)
      return false;
    const auto kinds = object["control_kinds"].as<ArduinoJson::JsonArrayConst>();
    if (kinds.size() < 2 || kinds.size() > 8 || kinds[0].as<ArduinoJson::JsonString>() != "light" ||
        kinds[1].as<ArduinoJson::JsonString>() != "number")
      return false;
    size_t cursor = 2;
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
    if (kinds[cursor].as<ArduinoJson::JsonString>() == "choice") {
      message.choice_enabled = true;
      cursor++;
    }
#endif
#ifdef USE_LVGL_MATERIAL_COVER_CONTROLS
    if (kinds[cursor].as<ArduinoJson::JsonString>() == "cover") {
      message.cover_enabled = true;
      cursor++;
    }
#endif
#ifdef USE_LVGL_MATERIAL_FAN_CONTROLS
    if (kinds[cursor].as<ArduinoJson::JsonString>() == "fan") {
      message.fan_enabled = true;
      cursor++;
    }
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS
    if (kinds[cursor].as<ArduinoJson::JsonString>() == "humidifier") {
      message.humidifier_enabled = true;
      cursor++;
    }
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
    if (kinds[cursor].as<ArduinoJson::JsonString>() == "robot") {
      message.robot_enabled = true;
      cursor++;
    }
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
    if (kinds[cursor].as<ArduinoJson::JsonString>() == "panel") {
      message.panel_enabled = true;
      cursor++;
    }
#endif
    if (cursor != kinds.size())
      return false;
    message.enabled = object["enabled"];
    return true;
  }
  if (!string_value(object["channel"], message.identity.channel) || !control_valid_hex(message.identity.channel))
    return false;
  if (message.kind == ControlKind::CONTROL_KIND_BIND)
    return fields(object, {"v", "type", "kind", "channel"});
  if (!sequence(object["open_seq"], message.identity.open_seq) ||
      !string_value(object["card_id"], message.identity.context.card_id) ||
      !uint_value(object["revision"], CONTROL_MAX_REVISION, message.identity.context.revision) ||
      !uint_value(object["slot"], 255, number))
    return false;
  message.identity.context.slot = static_cast<uint8_t>(number);
  const auto control = object["control_kind"].as<ArduinoJson::JsonString>();
  if (control == "light")
    message.identity.context.control_kind = ControlEntityKind::CONTROL_ENTITY_LIGHT;
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
  else if (control == "robot")
    message.identity.context.control_kind = ControlEntityKind::CONTROL_ENTITY_ROBOT;
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
  else if (control == "panel")
    message.identity.context.control_kind = ControlEntityKind::CONTROL_ENTITY_PANEL;
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS
  else if (control == "humidifier")
    message.identity.context.control_kind = ControlEntityKind::CONTROL_ENTITY_HUMIDIFIER;
#endif
#ifdef USE_LVGL_MATERIAL_FAN_CONTROLS
  else if (control == "fan")
    message.identity.context.control_kind = ControlEntityKind::CONTROL_ENTITY_FAN;
#endif
#ifdef USE_LVGL_MATERIAL_COVER_CONTROLS
  else if (control == "cover")
    message.identity.context.control_kind = ControlEntityKind::CONTROL_ENTITY_COVER;
#endif
  else if (control == "number")
    message.identity.context.control_kind = ControlEntityKind::CONTROL_ENTITY_NUMBER;
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
  else if (control == "choice")
    message.identity.context.control_kind = ControlEntityKind::CONTROL_ENTITY_CHOICE;
#endif
  else
    return false;
  if (!control_valid_context(message.identity.context))
    return false;
  bool detail_open = false;
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
  for (auto pair : object)
    if (std::string_view(pair.key().c_str(), pair.key().size()) == "detail_version")
      detail_open = true;
  if (detail_open &&
      (message.identity.context.control_kind != ControlEntityKind::CONTROL_ENTITY_LIGHT ||
       (message.kind != ControlKind::CONTROL_KIND_OPEN && message.kind != ControlKind::CONTROL_KIND_CANCEL_OPEN) ||
       !uint_value(object["detail_version"], 1, number) || number != 1))
    return false;
  message.detail_open = detail_open;
#endif
  if (message.kind == ControlKind::CONTROL_KIND_OPEN)
    return fields(object, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind"},
                  detail_open);
  if (message.kind == ControlKind::CONTROL_KIND_CANCEL_OPEN)
    return fields(object,
                  {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind", "reason"},
                  detail_open) &&
           enum_value(object["reason"], REASONS, message.reason) && close_reason(message.reason);
  if (!string_value(object["token"], message.identity.token) || !control_valid_hex(message.identity.token))
    return false;
  if (message.kind == ControlKind::CONTROL_KIND_CLOSE)
    return fields(object, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind",
                           "token", "reason"}) &&
           enum_value(object["reason"], REASONS, message.reason) && close_reason(message.reason);
#ifdef USE_LVGL_MATERIAL_INFO_AGENDA
  if (message.kind == ControlKind::CONTROL_KIND_PAGE || message.kind == ControlKind::CONTROL_KIND_PAGE_RESULT ||
      object["panel_caps"]["info_type"].as<ArduinoJson::JsonString>() == "agenda")
    return control_decode_agenda(object, message);
#endif
#ifdef USE_LVGL_MATERIAL_INFO_STATISTICS
  if (object["panel_caps"]["info_type"].as<ArduinoJson::JsonString>() == "statistics")
    return control_decode_statistics(object, message);
#endif
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
  if (!object["text_version"].isUnbound() || object["panel_type"].as<ArduinoJson::JsonString>() == "text")
    return text_value != nullptr && control_decode_text(object, message, *text_value);
#endif
#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
  if (!object["datetime_version"].isUnbound() || object["panel_type"].as<ArduinoJson::JsonString>() == "datetime")
    return control_decode_datetime(object, message);
#endif
#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS
  // Independent version marker prevents shared operation names from entering another panel decoder.
  if (!object["action_version"].isUnbound() || object["panel_type"].as<ArduinoJson::JsonString>() == "action")
    return control_decode_action(object, message);
#endif
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
  // Media owns its independent facet discriminator before the Light/Climate
  // Detail decoder. A shared name such as "main" cannot enter that decoder.
  if (!object["media_version"].isUnbound() || object["panel_type"].as<ArduinoJson::JsonString>() == "media")
    return control_decode_media(object, message);
#endif
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
  if (message.kind == ControlKind::CONTROL_KIND_FACET || message.kind == ControlKind::CONTROL_KIND_FACET_RESULT ||
      object["light_detail"].is<JsonObjectConst>() || object["panel_type"].as<ArduinoJson::JsonString>() == "climate" ||
      (message.kind == ControlKind::CONTROL_KIND_COMMAND &&
       (object["op"].as<ArduinoJson::JsonString>() == "facet_number" ||
        object["op"].as<ArduinoJson::JsonString>() == "facet_choice" ||
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
        object["op"].as<ArduinoJson::JsonString>() == "facet_range" ||
#endif
        (message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_LIGHT &&
         !object["view_seq"].isNull()))))
    return control_decode_detail(object, message);
#endif
#ifdef USE_LVGL_MATERIAL_EXTRA_CONTROLS
  if (object["panel_type"].as<ArduinoJson::JsonString>() == "info" ||
      object["panel_type"].as<ArduinoJson::JsonString>() == "lock" ||
      (message.kind == ControlKind::CONTROL_KIND_COMMAND && object["op"].as<ArduinoJson::JsonString>() == "lock"))
    return control_decode_extra(object, message);
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
  if (message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_PANEL &&
      (message.kind == ControlKind::CONTROL_KIND_GRANT || message.kind == ControlKind::CONTROL_KIND_STATE ||
       message.kind == ControlKind::CONTROL_KIND_COMMAND))
    return control_decode_panel(object, message);
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
  if ((message.kind == ControlKind::CONTROL_KIND_GRANT || message.kind == ControlKind::CONTROL_KIND_STATE) &&
      message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_ROBOT) {
    auto &robot = message.state.emplace_family<ControlSnapshot::Robot>();
    if (!fields(object, {"v",
                         "type",
                         "kind",
                         "channel",
                         "open_seq",
                         "slot",
                         "card_id",
                         "revision",
                         "control_kind",
                         "token",
                         "state_seq",
                         "ttl_ms",
                         "title",
                         "robot_type",
                         "activity",
                         "available",
                         "speed_index",
                         "battery_percent",
                         "robot_caps",
                         "eligible_actions",
                         "area_enabled",
                         "state_generation"}) ||
        !sequence(object["state_seq"], message.state.state_seq) ||
        !uint_value(object["ttl_ms"], CONTROL_LEASE_MS, number) || number != CONTROL_LEASE_MS ||
        !string_value(object["title"], message.state.title) ||
        !sequence(object["state_generation"], robot.state_generation) ||
        !enum_value(object["robot_type"], ROBOT_TYPES, robot.type) ||
        !enum_value(object["activity"], ROBOT_ACTIVITIES, robot.activity) || !object["available"].is<bool>() ||
        !object["area_enabled"].is<bool>() || !uint_value(object["eligible_actions"], 63, number))
      return false;
    robot.available = object["available"];
    robot.area_enabled = object["area_enabled"];
    robot.eligible_actions = static_cast<uint8_t>(number);
    if (!object["speed_index"].isNull()) {
      if (!uint_value(object["speed_index"], 7, number))
        return false;
      robot.speed_index = static_cast<uint8_t>(number);
    }
    if (!object["battery_percent"].isNull()) {
      if (!uint_value(object["battery_percent"], 100, number))
        return false;
      robot.battery_percent = static_cast<uint8_t>(number);
    }
    const auto caps = object["robot_caps"].as<JsonObjectConst>();
    if (!fields(caps, {"actions", "speeds", "areas"}) || !uint_value(caps["actions"], 63, number) ||
        !caps["speeds"].is<ArduinoJson::JsonArrayConst>() || !caps["areas"].is<ArduinoJson::JsonArrayConst>())
      return false;
    robot.caps.actions = static_cast<uint8_t>(number);
    for (bool area : {false, true}) {
      const auto data = caps[area ? "areas" : "speeds"].as<ArduinoJson::JsonArrayConst>();
      auto &options = area ? robot.caps.areas : robot.caps.speeds;
      if (data.size() > options.size())
        return false;
      (area ? robot.caps.area_count : robot.caps.speed_count) = static_cast<uint8_t>(data.size());
      for (size_t index = 0; index < data.size(); index++)
        if (!string_value(data[index], options[index]))
          return false;
    }
    return control_valid_robot_snapshot(message.state);
  }
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS
  if ((message.kind == ControlKind::CONTROL_KIND_GRANT || message.kind == ControlKind::CONTROL_KIND_STATE) &&
      message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_HUMIDIFIER) {
    auto &humidity = message.state.emplace_family<ControlSnapshot::Humidifier>();
    if (!fields(object, {"v",
                         "type",
                         "kind",
                         "channel",
                         "open_seq",
                         "slot",
                         "card_id",
                         "revision",
                         "control_kind",
                         "token",
                         "state_seq",
                         "ttl_ms",
                         "title",
                         "humidifier_state",
                         "available",
                         "device_class",
                         "humidity_domain",
                         "humidity_index",
                         "humidity_text",
                         "current_humidity",
                         "mode_index",
                         "humidifier_caps",
                         "state_generation"}) ||
        !sequence(object["state_seq"], message.state.state_seq) ||
        !uint_value(object["ttl_ms"], CONTROL_LEASE_MS, number) || number != CONTROL_LEASE_MS ||
        !string_value(object["title"], message.state.title) || !object["available"].is<bool>() ||
        !sequence(object["state_generation"], humidity.state_generation) ||
        !string_value(object["humidity_text"], humidity.target_text) ||
        !string_value(object["current_humidity"], humidity.current))
      return false;
    const auto power = object["humidifier_state"].as<ArduinoJson::JsonString>();
    if (power == "on")
      humidity.power = 1;
    else if (power == "off")
      humidity.power = 0;
    else if (power != "unknown")
      return false;
    humidity.available = object["available"];
    const auto device_class = object["device_class"].as<ArduinoJson::JsonString>();
    if (device_class == "humidifier")
      humidity.device_class = 0;
    else if (device_class == "dehumidifier")
      humidity.device_class = 1;
    else if (device_class != "unknown")
      return false;
    if (!object["humidity_domain"].isNull()) {
      const auto domain = object["humidity_domain"].as<JsonObjectConst>();
      ControlText<32> minimum, maximum, step, unit;
      if (!fields(domain, {"schema", "min", "max", "step", "unit", "max_index"}) ||
          domain["schema"].as<ArduinoJson::JsonString>() != "numeric_steps_v1" ||
          !string_value(domain["min"], minimum) || !string_value(domain["max"], maximum) ||
          !string_value(domain["step"], step) || !string_value(domain["unit"], unit) ||
          !number_parse_decimal(minimum.view(), humidity.domain.minimum) ||
          !number_parse_decimal(maximum.view(), humidity.domain.maximum) ||
          !number_parse_decimal(step.view(), humidity.domain.step) || !uint_value(domain["max_index"], 100, number))
        return false;
      humidity.domain.max_index = static_cast<uint32_t>(number);
      humidity.domain.unit = unit.bytes;
      if (!humidity.domain.valid())
        return false;
    }
    if (!object["humidity_index"].isNull()) {
      if (!uint_value(object["humidity_index"], 100, number))
        return false;
      humidity.index = static_cast<uint8_t>(number);
    }
    if (!object["mode_index"].isNull()) {
      if (!uint_value(object["mode_index"], humidity.caps.modes.size() - 1, number))
        return false;
      humidity.mode_index = static_cast<uint8_t>(number);
    }
    const auto caps = object["humidifier_caps"].as<JsonObjectConst>();
    if (!fields(caps, {"turn_on", "turn_off", "modes"}) || !caps["turn_on"].is<bool>() ||
        !caps["turn_off"].is<bool>() || !caps["modes"].is<ArduinoJson::JsonArrayConst>())
      return false;
    humidity.caps.turn_on = caps["turn_on"];
    humidity.caps.turn_off = caps["turn_off"];
    const auto modes = caps["modes"].as<ArduinoJson::JsonArrayConst>();
    if (modes.size() > humidity.caps.modes.size())
      return false;
    humidity.caps.mode_count = modes.size();
    for (size_t index = 0; index < modes.size(); index++)
      if (!string_value(modes[index], humidity.caps.modes[index]))
        return false;
    return control_valid_humidifier_snapshot(message.state);
  }
#endif
#ifdef USE_LVGL_MATERIAL_FAN_CONTROLS
  if ((message.kind == ControlKind::CONTROL_KIND_GRANT || message.kind == ControlKind::CONTROL_KIND_STATE) &&
      message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_FAN) {
    auto &fan = message.state.emplace_family<ControlSnapshot::Fan>();
    if (!fields(object, {"v",           "type",      "kind",         "channel",         "open_seq",     "slot",
                         "card_id",     "revision",  "control_kind", "token",           "state_seq",    "ttl_ms",
                         "title",       "fan_state", "available",    "percentage",      "preset_index", "preset_known",
                         "oscillating", "direction", "fan_caps",     "state_generation"}) ||
        !sequence(object["state_seq"], message.state.state_seq) ||
        !uint_value(object["ttl_ms"], CONTROL_LEASE_MS, number) || number != CONTROL_LEASE_MS ||
        !string_value(object["title"], message.state.title) || !object["available"].is<bool>() ||
        !object["preset_known"].is<bool>() || !sequence(object["state_generation"], fan.state_generation))
      return false;
    const auto state = object["fan_state"].as<ArduinoJson::JsonString>();
    if (state == "on")
      fan.power = 1;
    else if (state == "off")
      fan.power = 0;
    else if (state != "unknown")
      return false;
    fan.available = object["available"];
    fan.preset_known = object["preset_known"];
    if (!object["percentage"].isNull()) {
      if (!uint_value(object["percentage"], 100, number))
        return false;
      fan.percentage = static_cast<uint8_t>(number);
    }
    if (!object["preset_index"].isNull()) {
      if (!uint_value(object["preset_index"], fan.caps.presets.size() - 1, number))
        return false;
      fan.preset_index = static_cast<uint8_t>(number);
    }
    if (!object["oscillating"].isNull()) {
      if (!object["oscillating"].is<bool>())
        return false;
      fan.oscillating = object["oscillating"].as<bool>();
    }
    if (!object["direction"].isNull()) {
      const auto direction = object["direction"].as<ArduinoJson::JsonString>();
      if (direction == "forward")
        fan.direction = 0;
      else if (direction == "reverse")
        fan.direction = 1;
      else
        return false;
    }
    const auto caps = object["fan_caps"].as<JsonObjectConst>();
    if (!fields(caps, {"turn_on", "turn_off", "oscillate", "direction", "speeds", "presets"}) ||
        !caps["turn_on"].is<bool>() || !caps["turn_off"].is<bool>() || !caps["oscillate"].is<bool>() ||
        !caps["direction"].is<bool>() || !caps["speeds"].is<ArduinoJson::JsonArrayConst>() ||
        !caps["presets"].is<ArduinoJson::JsonArrayConst>())
      return false;
    fan.caps.turn_on = caps["turn_on"];
    fan.caps.turn_off = caps["turn_off"];
    fan.caps.oscillate = caps["oscillate"];
    fan.caps.direction = caps["direction"];
    const auto speeds = caps["speeds"].as<ArduinoJson::JsonArrayConst>();
    const auto presets = caps["presets"].as<ArduinoJson::JsonArrayConst>();
    if (speeds.size() > fan.caps.speeds.size() || presets.size() > fan.caps.presets.size())
      return false;
    fan.caps.speed_count = static_cast<uint8_t>(speeds.size());
    fan.caps.preset_count = static_cast<uint8_t>(presets.size());
    for (size_t index = 0; index < speeds.size(); index++) {
      if (!uint_value(speeds[index], 100, number))
        return false;
      fan.caps.speeds[index] = static_cast<uint8_t>(number);
    }
    for (size_t index = 0; index < presets.size(); index++)
      if (!string_value(presets[index], fan.caps.presets[index]))
        return false;
    return control_valid_fan_snapshot(message.state);
  }
#endif
#ifdef USE_LVGL_MATERIAL_COVER_CONTROLS
  if ((message.kind == ControlKind::CONTROL_KIND_GRANT || message.kind == ControlKind::CONTROL_KIND_STATE) &&
      message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_COVER) {
    auto &cover = message.state.emplace_family<ControlCoverSnapshot>();
    if (!fields(
            object,
            {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind", "token",
             "state_seq", "ttl_ms", "title", "cover_state", "position", "available", "cover_caps", "state_generation"},
            allow_cover_panel) ||
        !sequence(object["state_seq"], message.state.state_seq) ||
        !uint_value(object["ttl_ms"], CONTROL_LEASE_MS, number) || number != CONTROL_LEASE_MS ||
        !string_value(object["title"], message.state.title) || !object["available"].is<bool>() ||
        !sequence(object["state_generation"], cover.state_generation) ||
        !enum_value(object["cover_state"], COVER_STATES, cover.state))
      return false;
    const auto caps = object["cover_caps"].as<JsonObjectConst>();
    if (!fields(caps, {"open", "stop", "close"}) || !caps["open"].is<bool>() || !caps["stop"].is<bool>() ||
        !caps["close"].is<bool>())
      return false;
    cover.caps = {caps["open"], caps["stop"], caps["close"]};
    cover.available = object["available"];
    cover.position_known = !object["position"].isNull();
    if (cover.position_known) {
      if (!uint_value(object["position"], 100, number))
        return false;
      cover.position = static_cast<uint8_t>(number);
    }
#ifdef USE_LVGL_MATERIAL_COVER_PANEL
    if (allow_cover_panel) {
      auto &panel = cover.panel;
      const auto data = object["cover_panel"].as<JsonObjectConst>();
      if (!fields(data, {"version", "device_class", "tilt_position", "caps"}) ||
          !uint_value(data["version"], 1, number) || number != 1 ||
          !enum_value(data["device_class"], COVER_CLASSES, panel.device_class))
        return false;
      const auto capabilities = data["caps"].as<JsonObjectConst>();
      if (!fields(capabilities, {"set_position", "open_tilt", "close_tilt", "stop_tilt", "set_tilt_position"}) ||
          !capabilities["set_position"].is<bool>() || !capabilities["open_tilt"].is<bool>() ||
          !capabilities["close_tilt"].is<bool>() || !capabilities["stop_tilt"].is<bool>() ||
          !capabilities["set_tilt_position"].is<bool>())
        return false;
      panel.caps = {capabilities["set_position"], capabilities["open_tilt"], capabilities["close_tilt"],
                    capabilities["stop_tilt"], capabilities["set_tilt_position"]};
      panel.present = true;
      panel.tilt_known = !data["tilt_position"].isNull();
      if (panel.tilt_known) {
        if (!uint_value(data["tilt_position"], 100, number))
          return false;
        panel.tilt_position = static_cast<uint8_t>(number);
      }
    }
#endif
    return control_valid_cover_snapshot(message.state, allow_cover_panel);
  }
#endif
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
  if ((message.kind == ControlKind::CONTROL_KIND_GRANT || message.kind == ControlKind::CONTROL_KIND_STATE) &&
      message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_CHOICE) {
    auto &state = message.state.emplace_family<ControlSnapshot::Choice>();
    if (!fields(object,
                {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind", "token",
                 "state_seq", "ttl_ms", "title", "options", "index", "available", "state_generation"}) ||
        !sequence(object["state_seq"], message.state.state_seq) ||
        !uint_value(object["ttl_ms"], CONTROL_LEASE_MS, number) || number != CONTROL_LEASE_MS ||
        !string_value(object["title"], message.state.title) || !object["available"].is<bool>() ||
        !sequence(object["state_generation"], state.state_generation))
      return false;
    const auto options = object["options"].as<ArduinoJson::JsonArrayConst>();
    if (options.size() == 0 || options.size() > state.options.size())
      return false;
    state.count = static_cast<uint8_t>(options.size());
    for (size_t index = 0; index < options.size(); index++) {
      if (!string_value(options[index], state.options[index]))
        return false;
    }
    state.available = object["available"];
    const bool known = !object["index"].isNull();
    if (known != state.available && !(allow_empty_choice && state.available && !known))
      return false;
    if (state.available && !known)
      state.index = ControlSnapshot::Choice::EMPTY_INDEX;
    if (known) {
      if (!uint_value(object["index"], state.count - 1, number))
        return false;
      state.index = static_cast<uint8_t>(number);
    }
    return control_valid_choice_snapshot(message.state, allow_empty_choice);
  }
#endif
  if ((message.kind == ControlKind::CONTROL_KIND_GRANT || message.kind == ControlKind::CONTROL_KIND_STATE) &&
      message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_NUMBER) {
    auto &state = message.state.emplace_family<ControlNumberSnapshot>();
    bool thermostat = false;
#ifdef USE_LVGL_MATERIAL_THERMOSTAT
    thermostat = !object["thermostat"].isUnbound();
#endif
    if (!fields(object,
                {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind", "token",
                 "state_seq", "ttl_ms", "title", "domain", "index", "available", "value_text", "state_generation"},
                thermostat) ||
        !sequence(object["state_seq"], message.state.state_seq) ||
        !uint_value(object["ttl_ms"], CONTROL_LEASE_MS, number) || number != CONTROL_LEASE_MS ||
        !string_value(object["title"], message.state.title) || !object["available"].is<bool>() ||
        !sequence(object["state_generation"], state.state_generation) ||
        !string_value(object["value_text"], state.value_text))
      return false;
#ifdef USE_LVGL_MATERIAL_THERMOSTAT
    if (thermostat) {
      auto &view = state.thermostat;
      const auto data = object["thermostat"].as<JsonObjectConst>();
      view.dial = data["dial"].is<bool>() && data["dial"].as<bool>();
      ControlText<3> unit;
      if (!fields(data, {"unit", "target", "current", "min", "max", "level", "activity"}, view.dial) ||
          !string_value(data["unit"], unit) || (unit.view() != "Â°C" && unit.view() != "Â°F") ||
          !string_value(data["target"], view.target) || !string_value(data["current"], view.current) ||
          !string_value(data["min"], view.minimum) || !string_value(data["max"], view.maximum) ||
          !enum_value(data["activity"], THERMOSTAT_ACTIVITIES, view.activity))
        return false;
      for (const auto *text : {&view.target, &view.current, &view.minimum, &view.maximum}) {
        int64_t temperature;
        char canonical[13];
        if (!text->view().empty() &&
            (!number_parse_decimal(text->view(), temperature) || temperature < -100 * NUMBER_SCALE ||
             temperature > 300 * NUMBER_SCALE || !number_format_decimal(temperature, canonical, sizeof(canonical)) ||
             text->view() != canonical))
          return false;
      }
      if (!data["level"].isNull()) {
        if (!uint_value(data["level"], 100, number))
          return false;
        view.level = static_cast<uint8_t>(number);
      }
      view.present = true;
      view.fahrenheit = unit.view() == "Â°F";
      if (object["domain"].isNull())
        return !object["available"].as<bool>() && object["index"].isNull() &&
               control_valid_number_snapshot(message.state);
    }
#endif
    const auto domain = object["domain"].as<JsonObjectConst>();
    ControlText<64> minimum, maximum, step;
    ControlText<32> unit;
    if (!fields(domain, {"schema", "min", "max", "step", "unit", "max_index"}) ||
        domain["schema"].as<ArduinoJson::JsonString>() != "numeric_steps_v1" || !string_value(domain["min"], minimum) ||
        !string_value(domain["max"], maximum) || !string_value(domain["step"], step) ||
        !string_value(domain["unit"], unit) || !number_parse_decimal(minimum.view(), state.domain.minimum) ||
        !number_parse_decimal(maximum.view(), state.domain.maximum) ||
        !number_parse_decimal(step.view(), state.domain.step) ||
        !uint_value(domain["max_index"], NUMBER_MAX_INDEX, number))
      return false;
    state.domain.max_index = static_cast<uint32_t>(number);
    state.domain.unit = unit.bytes;
    if (!state.domain.valid())
      return false;
    state.available = object["available"];
    const bool known = !object["index"].isNull();
    if (known != state.available)
      return false;
    if (known) {
      if (!uint_value(object["index"], state.domain.max_index, number))
        return false;
      state.index = static_cast<uint32_t>(number);
    }
    return control_valid_number_snapshot(message.state);
  }
  if (message.kind == ControlKind::CONTROL_KIND_GRANT || message.kind == ControlKind::CONTROL_KIND_STATE) {
    if (!fields(object, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind",
                         "token", "state_seq", "ttl_ms", "title", "on", "brightness_pct", "caps"}) ||
        !sequence(object["state_seq"], message.state.state_seq) ||
        !uint_value(object["ttl_ms"], CONTROL_LEASE_MS, number) || number != CONTROL_LEASE_MS ||
        !string_value(object["title"], message.state.title) || !object["on"].is<bool>())
      return false;
    message.state.on = object["on"];
    const auto caps = object["caps"].as<JsonObjectConst>();
    if (!fields(caps, {"brightness", "turn_on", "turn_off"}) || !caps["brightness"].is<bool>() ||
        !caps["turn_on"].is<bool>() || !caps["turn_off"].is<bool>())
      return false;
    message.state.caps = {caps["brightness"], caps["turn_on"], caps["turn_off"]};
    message.state.brightness_known = !object["brightness_pct"].isNull();
    if (message.state.brightness_known) {
      if (!uint_value(object["brightness_pct"], 100, number))
        return false;
      message.state.brightness_pct = static_cast<uint8_t>(number);
    }
    return control_valid_snapshot(message.state);
  }
  if (!sequence(object["command_seq"], message.command_seq))
    return false;
  if (message.kind == ControlKind::CONTROL_KIND_COMMAND) {
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
    if (message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_ROBOT) {
      uint8_t operation;
      if (!enum_value(object["op"], ROBOT_OPERATIONS, operation))
        return false;
      const bool target = operation >= 6;
      if (!fields(object,
                  {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind", "token",
                   "command_seq", "op", "expected_state_generation"},
                  target) ||
          !sequence(object["expected_state_generation"], message.expected_state_generation))
        return false;
      message.operation = static_cast<ControlOperation>(
          static_cast<uint8_t>(ControlOperation::CONTROL_OPERATION_ROBOT_START) + operation);
      if (target) {
        if (!uint_value(object["target_index"], 7, number))
          return false;
        message.robot_target_index = static_cast<uint8_t>(number);
      }
      return true;
    }
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS
    if (message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_HUMIDIFIER) {
      if (!fields(object, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind",
                           "token", "command_seq", "op", "target_index", "expected_state_generation"}) ||
          !sequence(object["expected_state_generation"], message.expected_state_generation))
        return false;
      const auto operation = object["op"].as<ArduinoJson::JsonString>();
      uint8_t maximum;
      if (operation == "power") {
        message.operation = ControlOperation::CONTROL_OPERATION_HUMIDIFIER_POWER;
        maximum = 1;
      } else if (operation == "humidity") {
        message.operation = ControlOperation::CONTROL_OPERATION_HUMIDIFIER_TARGET;
        maximum = 100;
      } else if (operation == "mode") {
        message.operation = ControlOperation::CONTROL_OPERATION_HUMIDIFIER_MODE;
        maximum = ControlSnapshot::Humidifier::MAX_OPTIONS - 1;
      } else {
        return false;
      }
      if (!uint_value(object["target_index"], maximum, number))
        return false;
      message.humidifier_target_index = static_cast<uint8_t>(number);
      return true;
    }
#endif
#ifdef USE_LVGL_MATERIAL_FAN_CONTROLS
    if (message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_FAN) {
      if (!fields(object, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind",
                           "token", "command_seq", "op", "target_index", "expected_state_generation"}) ||
          !enum_value(object["op"], OPERATIONS, message.operation) ||
          message.operation < ControlOperation::CONTROL_OPERATION_FAN_POWER ||
          message.operation > ControlOperation::CONTROL_OPERATION_FAN_DIRECTION ||
          !uint_value(object["target_index"], ControlSnapshot::Fan::MAX_OPTIONS - 1, number) ||
          !sequence(object["expected_state_generation"], message.expected_state_generation))
        return false;
      message.fan_target_index = static_cast<uint8_t>(number);
      return (message.operation == ControlOperation::CONTROL_OPERATION_FAN_SPEED ||
              message.operation == ControlOperation::CONTROL_OPERATION_FAN_PRESET || number <= 1);
    }
#endif
#ifdef USE_LVGL_MATERIAL_COVER_CONTROLS
    if (message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_COVER) {
      if (!enum_value(object["op"], OPERATIONS, message.operation))
        return false;
      bool target = false;
      auto last = ControlOperation::CONTROL_OPERATION_COVER_CLOSE;
#ifdef USE_LVGL_MATERIAL_COVER_PANEL
      if (allow_cover_panel) {
        last = ControlOperation::CONTROL_OPERATION_COVER_SET_TILT_POSITION;
        target = message.operation == ControlOperation::CONTROL_OPERATION_COVER_SET_POSITION ||
                 message.operation == ControlOperation::CONTROL_OPERATION_COVER_SET_TILT_POSITION;
        if (target) {
          if (!uint_value(object["target_index"], 100, number))
            return false;
          message.cover_target_index = static_cast<uint8_t>(number);
        }
      }
#endif
      return fields(object,
                    {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind", "token",
                     "command_seq", "op", "expected_state_generation"},
                    target) &&
             message.operation >= ControlOperation::CONTROL_OPERATION_COVER_OPEN && message.operation <= last &&
             sequence(object["expected_state_generation"], message.expected_state_generation);
    }
#endif
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
    if (message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_CHOICE) {
      if (!fields(object,
                  {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind", "token",
                   "command_seq", "op", "target_index", "expected_index", "expected_state_generation"}) ||
          object["op"].as<ArduinoJson::JsonString>() != "choose" ||
          !uint_value(object["target_index"], ControlSnapshot::Choice::MAX_OPTIONS - 1, number))
        return false;
      message.target_index = static_cast<uint8_t>(number);
      if (allow_empty_choice && object["expected_index"].isNull())
        number = ControlSnapshot::Choice::EMPTY_INDEX;
      else if (!uint_value(object["expected_index"], ControlSnapshot::Choice::MAX_OPTIONS - 1, number))
        return false;
      if (!sequence(object["expected_state_generation"], message.expected_state_generation))
        return false;
      message.expected_index = static_cast<uint32_t>(number);
      message.operation = ControlOperation::CONTROL_OPERATION_CHOOSE;
      return true;
    }
#endif
    if (message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_NUMBER) {
#ifdef USE_LVGL_MATERIAL_THERMOSTAT
      if (object["op"].as<ArduinoJson::JsonString>() == "set_target") {
        if (!fields(object,
                    {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind", "token",
                     "command_seq", "op", "target_index", "expected_index", "expected_state_generation"}) ||
            !uint_value(object["target_index"], NUMBER_MAX_INDEX, number))
          return false;
        message.thermostat_target_index = static_cast<uint32_t>(number);
        message.operation = ControlOperation::CONTROL_OPERATION_THERMOSTAT_TARGET;
        if (!uint_value(object["expected_index"], NUMBER_MAX_INDEX, number) ||
            !sequence(object["expected_state_generation"], message.expected_state_generation))
          return false;
        message.expected_index = static_cast<uint32_t>(number);
        return true;
      }
#endif
      if (!fields(object, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind",
                           "token", "command_seq", "op", "direction", "expected_index", "expected_state_generation"}) ||
          object["op"].as<ArduinoJson::JsonString>() != "step" || object["direction"].is<bool>() ||
          !object["direction"].is<int>() ||
          (object["direction"].as<int>() != -1 && object["direction"].as<int>() != 1) ||
          !uint_value(object["expected_index"], NUMBER_MAX_INDEX, number) ||
          !sequence(object["expected_state_generation"], message.expected_state_generation))
        return false;
      message.operation = ControlOperation::CONTROL_OPERATION_STEP;
      message.direction = static_cast<int8_t>(object["direction"].as<int>());
      message.expected_index = static_cast<uint32_t>(number);
      return true;
    }
    if (!enum_value(object["op"], OPERATIONS, message.operation) ||
        (message.operation != ControlOperation::CONTROL_OPERATION_BRIGHTNESS &&
         message.operation != ControlOperation::CONTROL_OPERATION_TURN_ON &&
         message.operation != ControlOperation::CONTROL_OPERATION_TURN_OFF))
      return false;
    if (message.operation == ControlOperation::CONTROL_OPERATION_BRIGHTNESS) {
      if (!fields(object, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind",
                           "token", "command_seq", "op", "value"}) ||
          !uint_value(object["value"], 100, number))
        return false;
      message.value = static_cast<uint8_t>(number);
      return true;
    }
    return fields(object, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind",
                           "token", "command_seq", "op"});
  }
  return message.kind == ControlKind::CONTROL_KIND_RESULT &&
         fields(object, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind",
                         "token", "command_seq", "state_seq", "status", "reason"}) &&
         sequence(object["state_seq"], message.state.state_seq) &&
         enum_value(object["status"], RESULTS, message.result) &&
         enum_value(object["reason"], REASONS, message.reason) &&
         control_valid_generic_result(
             message.result, message.reason,
             false
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
                 || message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_ROBOT
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
                 || message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_PANEL
#endif
             ,
             false
#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS
                 || message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_PANEL
#endif
         );
}

}  // namespace

// ArduinoJson accepts a lone escaped high surrogate as an empty string.
// Refuse it before DOM decoding, including escaped field names. Valid pairs are
// decoded normally by ArduinoJson; literal UTF-8 is checked at every boundary.
static bool entity_unicode_escapes_valid(std::string_view payload) {
  bool quoted = false;
  auto unit = [&](size_t offset, uint16_t &output) {
    if (offset + 4 > payload.size())
      return false;
    uint16_t result = 0;
    for (size_t n = 0; n < 4; n++) {
      const char c = payload[offset + n];
      const int digit = c >= '0' && c <= '9'   ? c - '0'
                        : c >= 'a' && c <= 'f' ? c - 'a' + 10
                        : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                               : -1;
      if (digit < 0)
        return false;
      result = static_cast<uint16_t>((result << 4) | digit);
    }
    output = result;
    return true;
  };
  for (size_t i = 0; i < payload.size(); i++) {
    if (payload[i] == '"') {
      quoted = !quoted;
      continue;
    }
    if (!quoted || payload[i] != '\\')
      continue;
    if (++i == payload.size())
      return false;
    if (payload[i] != 'u')
      continue;
    uint16_t first;
    if (!unit(i + 1, first))
      return false;
    i += 4;
    if (first >= 0xdc00 && first <= 0xdfff)
      return false;
    if (first >= 0xd800 && first <= 0xdbff) {
      uint16_t second;
      if (i + 6 >= payload.size() || payload[i + 1] != '\\' || payload[i + 2] != 'u' || !unit(i + 3, second) ||
          second < 0xdc00 || second > 0xdfff)
        return false;
      i += 6;
    }
  }
  return true;
}

bool control_parse_entity_json_allocated(std::string_view payload, ControlMessage &output,
                                         ArduinoJson::Allocator *allocator, bool allow_empty_choice,
                                         bool allow_cover_panel
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
                                         ,
                                         ControlTextRx *text_rx
#endif
) {
  if (payload.empty() || payload.size() > CONTROL_MAX_PAYLOAD_BYTES || payload.find('\0') != std::string_view::npos ||
      !entity_unicode_escapes_valid(payload))
    return false;
  ArduinoJson::JsonDocument document(allocator);
  if (ArduinoJson::deserializeJson(document, payload.data(), payload.size(),
                                   ArduinoJson::DeserializationOption::NestingLimit(3)))
    return false;
  const auto object = document.as<JsonObjectConst>();
  ControlMessage temporary;
  temporary.generic = true;
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
  ControlTextValueView parsed_text;
#endif
  if (!decode(object, temporary, allow_empty_choice, allow_cover_panel
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
              ,
              &parsed_text
#endif
              ))
    return false;
  // Counting key separators outside strings detects duplicate keys discarded
  // by the DOM, including within nested descriptors.
  size_t separators = 0;
  bool quoted = false, escaped = false;
  size_t depth = 0;
  bool began = false, closed = false;
  for (char c : payload) {
    if (quoted) {
      if (escaped)
        escaped = false;
      else if (c == '\\')
        escaped = true;
      else if (c == '"')
        quoted = false;
    } else {
      const bool whitespace = c == ' ' || c == '\t' || c == '\r' || c == '\n';
      if ((closed && !whitespace) || (!began && !whitespace && c != '{'))
        return false;
      if (c == '"')
        quoted = true;
      else if (c == ':')
        separators++;
      else if (c == '{' || c == '[') {
        began = true;
        depth++;
      } else if (c == '}' || c == ']') {
        if (depth == 0)
          return false;
        if (--depth == 0)
          closed = true;
      }
    }
  }
  const size_t expected =
      object.size() + object["caps"].as<JsonObjectConst>().size() + object["domain"].as<JsonObjectConst>().size() +
      object["thermostat"].as<JsonObjectConst>().size() + object["cover_caps"].as<JsonObjectConst>().size() +
      object["fan_caps"].as<JsonObjectConst>().size() + object["humidifier_caps"].as<JsonObjectConst>().size() +
      object["humidity_domain"].as<JsonObjectConst>().size() + object["cover_panel"].as<JsonObjectConst>().size() +
      object["cover_panel"]["caps"].as<JsonObjectConst>().size() + object["robot_caps"].as<JsonObjectConst>().size() +
      object["panel_caps"].as<JsonObjectConst>().size() + object["panel_state"].as<JsonObjectConst>().size()
#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
      + object["target"].as<JsonObjectConst>().size()
#endif
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
      + object["light_detail"].as<JsonObjectConst>().size() +
      object["light_detail"]["domain"].as<JsonObjectConst>().size() +
      object["panel_state"]["domain"].as<JsonObjectConst>().size()
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
      + object["light_color"].as<JsonObjectConst>().size()
#endif
#endif
      ;
  if (!closed || quoted || separators != expected)
    return false;
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
  if (temporary.state.family_if<ControlTextSnapshot>() != nullptr) {
    if (text_rx == nullptr || !text_rx->value.assign(parsed_text.value))
      return false;
    text_rx->present = parsed_text.present;
  }
#endif
  output = temporary;
  return true;
}

bool control_serialize_entity_json_allocated(const ControlMessage &message, char *output, size_t capacity,
                                             size_t &length, ArduinoJson::Allocator *allocator, bool allow_empty_choice,
                                             bool allow_cover_panel
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
                                             ,
                                             ControlTextValueView text_value
#endif
) {
  if (!message.generic || output == nullptr || capacity == 0 || static_cast<size_t>(message.kind) >= std::size(KINDS) ||
      static_cast<size_t>(message.operation) >= std::size(OPERATIONS) ||
      static_cast<size_t>(message.result) >= std::size(RESULTS) ||
      static_cast<size_t>(message.reason) >= std::size(REASONS))
    return false;
  if (message.kind != ControlKind::CONTROL_KIND_CAPABILITY && !control_valid_hex(message.identity.channel))
    return false;
  if (message.kind != ControlKind::CONTROL_KIND_CAPABILITY && message.kind != ControlKind::CONTROL_KIND_BIND) {
    const auto control = message.identity.context.control_kind;
    bool supported =
        control == ControlEntityKind::CONTROL_ENTITY_LIGHT || control == ControlEntityKind::CONTROL_ENTITY_NUMBER;
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
    supported |= control == ControlEntityKind::CONTROL_ENTITY_CHOICE;
#endif
#ifdef USE_LVGL_MATERIAL_COVER_CONTROLS
    supported |= control == ControlEntityKind::CONTROL_ENTITY_COVER;
#endif
#ifdef USE_LVGL_MATERIAL_FAN_CONTROLS
    supported |= control == ControlEntityKind::CONTROL_ENTITY_FAN;
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS
    supported |= control == ControlEntityKind::CONTROL_ENTITY_HUMIDIFIER;
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
    supported |= control == ControlEntityKind::CONTROL_ENTITY_ROBOT;
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
    supported |= control == ControlEntityKind::CONTROL_ENTITY_PANEL;
#endif
    if (!supported || !control_valid_context(message.identity.context) || message.identity.open_seq == 0 ||
        message.identity.open_seq > CONTROL_MAX_SEQUENCE)
      return false;
    if (message.kind != ControlKind::CONTROL_KIND_OPEN && message.kind != ControlKind::CONTROL_KIND_CANCEL_OPEN &&
        !control_valid_hex(message.identity.token))
      return false;
  }
  if ((message.kind == ControlKind::CONTROL_KIND_GRANT || message.kind == ControlKind::CONTROL_KIND_STATE) &&
      !message.state.matches_family(message.identity.context.control_kind))
    return false;
  if ((message.kind == ControlKind::CONTROL_KIND_GRANT || message.kind == ControlKind::CONTROL_KIND_STATE) &&
      !(
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
          message.state.family_if<ControlTextSnapshot>() != nullptr
              ? control_valid_text_snapshot(message.state, text_value)
          :
#endif
#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
          message.state.family_if<ControlDatetimeSnapshot>() != nullptr ? control_valid_datetime_snapshot(message.state)
          :
#endif
#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS
          message.state.family_if<ControlActionSnapshot>() != nullptr ? control_valid_action_snapshot(message.state)
          :
#endif
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
          message.state.family_if<ControlMediaSnapshot>() != nullptr ? control_valid_media_snapshot(message.state)
          :
#endif
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
          message.state.family_if<ControlDetailSnapshot>() != nullptr ? control_valid_detail_snapshot(message.state)
          :
#endif
#ifdef USE_LVGL_MATERIAL_EXTRA_CONTROLS
          message.state.family_if<ControlExtraSnapshot>() != nullptr ? control_valid_extra_snapshot(message.state)
          :
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
          message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_PANEL
              ? control_valid_panel_snapshot(message.state)
          :
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
          message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_ROBOT
              ? control_valid_robot_snapshot(message.state)
          :
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS
          message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_HUMIDIFIER
              ? control_valid_humidifier_snapshot(message.state)
          :
#endif
#ifdef USE_LVGL_MATERIAL_FAN_CONTROLS
          message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_FAN
              ? control_valid_fan_snapshot(message.state)
          :
#endif
#ifdef USE_LVGL_MATERIAL_COVER_CONTROLS
          message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_COVER
              ? control_valid_cover_snapshot(message.state, allow_cover_panel)
          :
#endif
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
          message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_CHOICE
              ? control_valid_choice_snapshot(message.state, allow_empty_choice)
          :
#endif
          message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_NUMBER
              ? control_valid_number_snapshot(message.state)
              : control_valid_snapshot(message.state)))
    return false;
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
  if (static_cast<size_t>(message.facet) > static_cast<size_t>(
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
                                               ControlFacet::CONTROL_FACET_RANGE
#elif defined(USE_LVGL_MATERIAL_LIGHT_COLOR)
                                               ControlFacet::CONTROL_FACET_COLOR
#else
                                               ControlFacet::CONTROL_FACET_SWING_HORIZONTAL_MODE
#endif
                                               ))
    return false;
#endif
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
  const bool text_command = message.kind == ControlKind::CONTROL_KIND_COMMAND &&
                            message.operation == ControlOperation::CONTROL_OPERATION_TEXT_SET;
  if (text_command && (!text_value.present || message.state.family_if<ControlTextSnapshot>() == nullptr ||
                       control_text_validate(text_value.value, 0, CONTROL_TEXT_MAX_CODEPOINTS) !=
                           ControlTextDraftReason::CONTROL_TEXT_DRAFT_REASON_NONE))
    return false;
  if (message.state.family_if<ControlTextSnapshot>() == nullptr && (text_value.present || !text_value.value.empty()))
    return false;
#endif
  ArduinoJson::JsonDocument document(allocator);
  auto object = document.to<ArduinoJson::JsonObject>();
  object["v"] = 1;
  object["type"] = "entity_control_v1";
  object["kind"] = KINDS[static_cast<size_t>(message.kind)];
  if (message.kind == ControlKind::CONTROL_KIND_CAPABILITY) {
    object["enabled"] = message.enabled;
    object["max_payload_bytes"] = CONTROL_MAX_PAYLOAD_BYTES;
    auto kinds = object["control_kinds"].to<ArduinoJson::JsonArray>();
    kinds.add("light");
    kinds.add("number");
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
    if (message.choice_enabled)
      kinds.add("choice");
#endif
#ifdef USE_LVGL_MATERIAL_COVER_CONTROLS
    if (message.cover_enabled)
      kinds.add("cover");
#endif
#ifdef USE_LVGL_MATERIAL_FAN_CONTROLS
    if (message.fan_enabled)
      kinds.add("fan");
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS
    if (message.humidifier_enabled)
      kinds.add("humidifier");
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
    if (message.robot_enabled)
      kinds.add("robot");
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
    if (message.panel_enabled)
      kinds.add("panel");
#endif
  } else {
    object["channel"] = message.identity.channel.bytes.data();
    if (message.kind != ControlKind::CONTROL_KIND_BIND) {
      object["open_seq"] = message.identity.open_seq;
      object["slot"] = message.identity.context.slot;
      object["card_id"] = message.identity.context.card_id.bytes.data();
      object["revision"] = message.identity.context.revision;
      object["control_kind"] =
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
          message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_PANEL ? "panel" :
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
          message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_ROBOT ? "robot"
          :
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS
          message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_HUMIDIFIER ? "humidifier"
          :
#endif
#ifdef USE_LVGL_MATERIAL_FAN_CONTROLS
          message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_FAN ? "fan"
          :
#endif
#ifdef USE_LVGL_MATERIAL_COVER_CONTROLS
          message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_COVER ? "cover"
          :
#endif
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
          message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_CHOICE ? "choice"
          :
#endif
          message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_NUMBER ? "number"
                                                                                            : "light";
      if (message.kind != ControlKind::CONTROL_KIND_OPEN && message.kind != ControlKind::CONTROL_KIND_CANCEL_OPEN)
        object["token"] = message.identity.token.bytes.data();
      if (message.kind == ControlKind::CONTROL_KIND_CANCEL_OPEN || message.kind == ControlKind::CONTROL_KIND_CLOSE)
        object["reason"] = REASONS[static_cast<size_t>(message.reason)];
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
      if ((message.kind == ControlKind::CONTROL_KIND_FACET || message.kind == ControlKind::CONTROL_KIND_FACET_RESULT) &&
          message.state.family_if<ControlMediaSnapshot>() != nullptr)
        control_encode_media(object, message);
#endif
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
      if ((message.kind == ControlKind::CONTROL_KIND_OPEN || message.kind == ControlKind::CONTROL_KIND_CANCEL_OPEN) &&
          message.detail_open)
        object["detail_version"] = 1;
      if ((message.kind == ControlKind::CONTROL_KIND_FACET || message.kind == ControlKind::CONTROL_KIND_FACET_RESULT)
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
          && message.state.family_if<ControlMediaSnapshot>() == nullptr
#endif
      )
        control_encode_detail(object, message);
#endif
#ifdef USE_LVGL_MATERIAL_INFO_AGENDA
      if (message.kind == ControlKind::CONTROL_KIND_PAGE || message.kind == ControlKind::CONTROL_KIND_PAGE_RESULT)
        control_encode_agenda(object, message);
#endif
      if (message.kind == ControlKind::CONTROL_KIND_GRANT || message.kind == ControlKind::CONTROL_KIND_STATE) {
        object["state_seq"] = message.state.state_seq;
        object["ttl_ms"] = message.state.ttl_ms;
        object["title"] = message.state.title.bytes.data();
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
        if (message.state.family_if<ControlTextSnapshot>() != nullptr)
          control_encode_text(object, message, text_value);
        else
#endif
#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
            if (message.state.family_if<ControlDatetimeSnapshot>() != nullptr)
          control_encode_datetime(object, message);
        else
#endif
#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS
            if (message.state.family_if<ControlActionSnapshot>() != nullptr)
          control_encode_action(object, message);
        else
#endif
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
            if (message.state.family_if<ControlMediaSnapshot>() != nullptr)
          control_encode_media(object, message);
        else
#endif
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
            if (message.state.family_if<ControlDetailSnapshot>() != nullptr)
          control_encode_detail(object, message);
        else
#endif
#ifdef USE_LVGL_MATERIAL_EXTRA_CONTROLS
            if (message.state.family_if<ControlExtraSnapshot>() != nullptr)
          control_encode_extra(object, message);
        else
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
            if (message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_PANEL)
          control_encode_panel(object, message);
        else
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
            if (message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_ROBOT) {
          const auto &robot = *message.state.family_if<ControlSnapshot::Robot>();
          object["robot_type"] = ROBOT_TYPES[robot.type];
          object["activity"] = ROBOT_ACTIVITIES[robot.activity];
          object["available"] = robot.available;
          object["state_generation"] = robot.state_generation;
          object["eligible_actions"] = robot.eligible_actions;
          object["area_enabled"] = robot.area_enabled;
          if (robot.speed_index == ControlSnapshot::Robot::UNKNOWN)
            object["speed_index"] = nullptr;
          else
            object["speed_index"] = robot.speed_index;
          if (robot.battery_percent == ControlSnapshot::Robot::UNKNOWN)
            object["battery_percent"] = nullptr;
          else
            object["battery_percent"] = robot.battery_percent;
          auto caps = object["robot_caps"].to<ArduinoJson::JsonObject>();
          caps["actions"] = robot.caps.actions;
          for (bool area : {false, true}) {
            auto data = caps[area ? "areas" : "speeds"].to<ArduinoJson::JsonArray>();
            const auto &options = area ? robot.caps.areas : robot.caps.speeds;
            const size_t count = area ? robot.caps.area_count : robot.caps.speed_count;
            for (size_t index = 0; index < count; index++)
              data.add(options[index].bytes.data());
          }
        } else
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS
            if (message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_HUMIDIFIER) {
          const auto &humidity = *message.state.family_if<ControlSnapshot::Humidifier>();
          constexpr auto unknown = ControlSnapshot::Humidifier::UNKNOWN;
          object["humidifier_state"] = humidity.power == 1 ? "on" : humidity.power == 0 ? "off" : "unknown";
          object["device_class"] = humidity.device_class == 0   ? "humidifier"
                                   : humidity.device_class == 1 ? "dehumidifier"
                                                                : "unknown";
          object["state_generation"] = humidity.state_generation;
          object["available"] = humidity.available;
          object["humidity_text"] = humidity.target_text.bytes.data();
          object["current_humidity"] = humidity.current.bytes.data();
          if (humidity.index == unknown)
            object["humidity_index"] = nullptr;
          else
            object["humidity_index"] = humidity.index;
          if (humidity.mode_index == unknown)
            object["mode_index"] = nullptr;
          else
            object["mode_index"] = humidity.mode_index;
          if (!humidity.domain.valid())
            object["humidity_domain"] = nullptr;
          else {
            auto domain = object["humidity_domain"].to<ArduinoJson::JsonObject>();
            char minimum[24], maximum[24], step[24];
            if (!number_format_decimal(humidity.domain.minimum, minimum, sizeof(minimum)) ||
                !number_format_decimal(humidity.domain.maximum, maximum, sizeof(maximum)) ||
                !number_format_decimal(humidity.domain.step, step, sizeof(step)))
              return false;
            domain["schema"] = "numeric_steps_v1";
            domain["min"] = minimum;
            domain["max"] = maximum;
            domain["step"] = step;
            domain["unit"] = humidity.domain.unit.data();
            domain["max_index"] = humidity.domain.max_index;
          }
          auto caps = object["humidifier_caps"].to<ArduinoJson::JsonObject>();
          caps["turn_on"] = humidity.caps.turn_on;
          caps["turn_off"] = humidity.caps.turn_off;
          auto modes = caps["modes"].to<ArduinoJson::JsonArray>();
          for (size_t index = 0; index < humidity.caps.mode_count; index++)
            modes.add(humidity.caps.modes[index].bytes.data());
        } else
#endif
#ifdef USE_LVGL_MATERIAL_FAN_CONTROLS
            if (message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_FAN) {
          const auto &fan = *message.state.family_if<ControlSnapshot::Fan>();
          constexpr auto unknown = ControlSnapshot::Fan::UNKNOWN;
          object["fan_state"] = fan.power == 1 ? "on" : fan.power == 0 ? "off" : "unknown";
          object["state_generation"] = fan.state_generation;
          object["available"] = fan.available;
          object["preset_known"] = fan.preset_known;
          if (fan.percentage == unknown)
            object["percentage"] = nullptr;
          else
            object["percentage"] = fan.percentage;
          if (fan.preset_index == unknown)
            object["preset_index"] = nullptr;
          else
            object["preset_index"] = fan.preset_index;
          if (fan.oscillating == unknown)
            object["oscillating"] = nullptr;
          else
            object["oscillating"] = fan.oscillating != 0;
          if (fan.direction == unknown)
            object["direction"] = nullptr;
          else
            object["direction"] = fan.direction == 0 ? "forward" : "reverse";
          auto caps = object["fan_caps"].to<ArduinoJson::JsonObject>();
          caps["turn_on"] = fan.caps.turn_on;
          caps["turn_off"] = fan.caps.turn_off;
          caps["oscillate"] = fan.caps.oscillate;
          caps["direction"] = fan.caps.direction;
          auto speeds = caps["speeds"].to<ArduinoJson::JsonArray>();
          auto presets = caps["presets"].to<ArduinoJson::JsonArray>();
          for (size_t index = 0; index < fan.caps.speed_count; index++)
            speeds.add(fan.caps.speeds[index]);
          for (size_t index = 0; index < fan.caps.preset_count; index++)
            presets.add(fan.caps.presets[index].bytes.data());
        } else
#endif
#ifdef USE_LVGL_MATERIAL_COVER_CONTROLS
            if (message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_COVER) {
          const auto &cover = *message.state.family_if<ControlCoverSnapshot>();
          object["cover_state"] = COVER_STATES[static_cast<size_t>(cover.state)];
          object["state_generation"] = cover.state_generation;
          object["available"] = cover.available;
          if (cover.position_known)
            object["position"] = cover.position;
          else
            object["position"] = nullptr;
          auto caps = object["cover_caps"].to<ArduinoJson::JsonObject>();
          caps["open"] = cover.caps.open;
          caps["stop"] = cover.caps.stop;
          caps["close"] = cover.caps.close;
#ifdef USE_LVGL_MATERIAL_COVER_PANEL
          if (allow_cover_panel) {
            const auto &panel = cover.panel;
            auto data = object["cover_panel"].to<ArduinoJson::JsonObject>();
            data["version"] = 1;
            data["device_class"] = COVER_CLASSES[panel.device_class];
            if (panel.tilt_known)
              data["tilt_position"] = panel.tilt_position;
            else
              data["tilt_position"] = nullptr;
            auto panel_caps = data["caps"].to<ArduinoJson::JsonObject>();
            panel_caps["set_position"] = panel.caps.set_position;
            panel_caps["open_tilt"] = panel.caps.open_tilt;
            panel_caps["close_tilt"] = panel.caps.close_tilt;
            panel_caps["stop_tilt"] = panel.caps.stop_tilt;
            panel_caps["set_tilt_position"] = panel.caps.set_tilt_position;
          }
#endif
        } else
#endif
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
            if (message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_CHOICE) {
          const auto &choice = *message.state.family_if<ControlSnapshot::Choice>();
          object["state_generation"] = choice.state_generation;
          object["available"] = choice.available;
          if (choice.available && choice.index != ControlSnapshot::Choice::EMPTY_INDEX)
            object["index"] = choice.index;
          else
            object["index"] = nullptr;
          auto options = object["options"].to<ArduinoJson::JsonArray>();
          for (size_t index = 0; index < choice.count; index++)
            options.add(choice.options[index].bytes.data());
        } else
#endif
            if (message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_NUMBER) {
          const auto &number = *message.state.family_if<ControlNumberSnapshot>();
          object["state_generation"] = number.state_generation;
          if (number.available)
            object["index"] = number.index;
          else
            object["index"] = nullptr;
          object["available"] = number.available;
          object["value_text"] = number.value_text.bytes.data();
#ifdef USE_LVGL_MATERIAL_THERMOSTAT
          if (number.thermostat.present) {
            const auto &view = number.thermostat;
            auto data = object["thermostat"].to<ArduinoJson::JsonObject>();
            data["unit"] = view.fahrenheit ? "Â°F" : "Â°C";
            data["target"] = view.target.bytes.data();
            data["current"] = view.current.bytes.data();
            data["min"] = view.minimum.bytes.data();
            data["max"] = view.maximum.bytes.data();
            data["activity"] = THERMOSTAT_ACTIVITIES[view.activity];
            if (view.dial)
              data["dial"] = true;
            if (view.level == ControlThermostatSnapshot::UNKNOWN_LEVEL)
              data["level"] = nullptr;
            else
              data["level"] = view.level;
          }
          if (!number.domain.valid()) {
            object["domain"] = nullptr;
          } else
#endif
          {
            auto domain = object["domain"].to<ArduinoJson::JsonObject>();
            char minimum[24], maximum[24], step[24];
            if (!number_format_decimal(number.domain.minimum, minimum, sizeof(minimum)) ||
                !number_format_decimal(number.domain.maximum, maximum, sizeof(maximum)) ||
                !number_format_decimal(number.domain.step, step, sizeof(step)))
              return false;
            domain["schema"] = "numeric_steps_v1";
            domain["min"] = minimum;
            domain["max"] = maximum;
            domain["step"] = step;
            domain["unit"] = number.domain.unit.data();
            domain["max_index"] = number.domain.max_index;
          }
        } else {
          object["on"] = message.state.on;
          if (message.state.brightness_known)
            object["brightness_pct"] = message.state.brightness_pct;
          else
            object["brightness_pct"] = nullptr;
          auto caps = object["caps"].to<ArduinoJson::JsonObject>();
          caps["brightness"] = message.state.caps.brightness;
          caps["turn_on"] = message.state.caps.turn_on;
          caps["turn_off"] = message.state.caps.turn_off;
        }
      }
      if (message.kind == ControlKind::CONTROL_KIND_COMMAND || message.kind == ControlKind::CONTROL_KIND_RESULT)
        object["command_seq"] = message.command_seq;
      if (message.kind == ControlKind::CONTROL_KIND_COMMAND) {
        object["op"] = OPERATIONS[static_cast<size_t>(message.operation)];
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
        if (message.operation == ControlOperation::CONTROL_OPERATION_TEXT_SET)
          control_encode_text(object, message, text_value);
        else
#endif
#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
            if (message.operation == ControlOperation::CONTROL_OPERATION_DATETIME_SET)
          control_encode_datetime(object, message);
        else
#endif
#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS
            if (message.operation >= ControlOperation::CONTROL_OPERATION_ACTION_PRESS &&
                message.operation <= ControlOperation::CONTROL_OPERATION_ACTION_TRIGGER)
          control_encode_action(object, message);
        else
#endif
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
            if (message.state.family_if<ControlMediaSnapshot>() != nullptr)
          control_encode_media(object, message);
        else
#endif
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
            if (message.detail_open || message.operation == ControlOperation::CONTROL_OPERATION_FACET_NUMBER ||
                message.operation == ControlOperation::CONTROL_OPERATION_FACET_CHOICE
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
                || message.operation == ControlOperation::CONTROL_OPERATION_FACET_COLOR
#endif
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
                || message.operation == ControlOperation::CONTROL_OPERATION_FACET_RANGE
#endif
            )
          control_encode_detail(object, message);
        else
#endif
#ifdef USE_LVGL_MATERIAL_EXTRA_CONTROLS
            if (message.operation == ControlOperation::CONTROL_OPERATION_EXTRA_LOCK)
          control_encode_extra(object, message);
        else
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
            if (message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_PANEL)
          control_encode_panel(object, message);
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_CONTROLS
        if (message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_ROBOT) {
          object["expected_state_generation"] = message.expected_state_generation;
          if (message.operation == ControlOperation::CONTROL_OPERATION_ROBOT_SPEED ||
              message.operation == ControlOperation::CONTROL_OPERATION_ROBOT_AREA)
            object["target_index"] = message.robot_target_index;
        }
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS
        if (message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_HUMIDIFIER) {
          object["target_index"] = message.humidifier_target_index;
          object["expected_state_generation"] = message.expected_state_generation;
        }
#endif
#ifdef USE_LVGL_MATERIAL_FAN_CONTROLS
        if (message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_FAN) {
          object["target_index"] = message.fan_target_index;
          object["expected_state_generation"] = message.expected_state_generation;
        }
#endif
#ifdef USE_LVGL_MATERIAL_COVER_CONTROLS
        if (message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_COVER) {
          object["expected_state_generation"] = message.expected_state_generation;
#ifdef USE_LVGL_MATERIAL_COVER_PANEL
          if (message.operation == ControlOperation::CONTROL_OPERATION_COVER_SET_POSITION ||
              message.operation == ControlOperation::CONTROL_OPERATION_COVER_SET_TILT_POSITION)
            object["target_index"] = message.cover_target_index;
#endif
        }
#endif
        if (message.operation == ControlOperation::CONTROL_OPERATION_BRIGHTNESS)
          object["value"] = message.value;
        if (message.operation == ControlOperation::CONTROL_OPERATION_STEP) {
          object["direction"] = message.direction;
          object["expected_index"] = message.expected_index;
          object["expected_state_generation"] = message.expected_state_generation;
        }
#ifdef USE_LVGL_MATERIAL_THERMOSTAT
        if (message.operation == ControlOperation::CONTROL_OPERATION_THERMOSTAT_TARGET) {
          object["target_index"] = message.thermostat_target_index;
          object["expected_index"] = message.expected_index;
          object["expected_state_generation"] = message.expected_state_generation;
        }
#endif
#ifdef USE_LVGL_MATERIAL_CHOICE_SELECTOR
        if (message.operation == ControlOperation::CONTROL_OPERATION_CHOOSE) {
          object["target_index"] = message.target_index;
          if (message.expected_index == ControlSnapshot::Choice::EMPTY_INDEX)
            object["expected_index"] = nullptr;
          else
            object["expected_index"] = message.expected_index;
          object["expected_state_generation"] = message.expected_state_generation;
        }
#endif
      }
      if (message.kind == ControlKind::CONTROL_KIND_RESULT) {
        object["state_seq"] = message.state.state_seq;
        object["status"] = RESULTS[static_cast<size_t>(message.result)];
        object["reason"] = REASONS[static_cast<size_t>(message.reason)];
      }
    }
  }
  ControlMessage validated;
  const auto required = ArduinoJson::measureJson(document);
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
  ControlTextValueView validated_text;
#endif
  if (document.overflowed() ||
      !decode(object, validated, allow_empty_choice, allow_cover_panel
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
              ,
              &validated_text
#endif
              ) ||
      required > CONTROL_MAX_PAYLOAD_BYTES || required >= capacity)
    return false;
  length = ArduinoJson::serializeJson(document, output, capacity);
  return length == required;
}

}  // namespace esphome::lvgl_material

#endif
