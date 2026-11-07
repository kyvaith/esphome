#include "material_entity_control_codec.h"
#ifdef USE_LVGL_MATERIAL_EXTRA_CONTROLS
#include <initializer_list>

namespace esphome::lvgl_material {
namespace {
using Extra = ControlExtraSnapshot;
using ArduinoJson::JsonObjectConst;
using ArduinoJson::JsonVariantConst;
constexpr const char *TYPES[] = {"sensor", "binary_sensor", "calendar", "alarm"};
constexpr const char *SOURCES[] = {"known", "unknown", "unavailable"};
constexpr const char *TEXT_STATUS[] = {"value", "empty", "invalid", "too_long", "unknown"};
constexpr const char *EVENTS[] = {"present", "none", "invalid", "unknown"};
constexpr const char *ALARM[] = {"disarmed",       "armed_home",          "armed_away", "armed_night",
                                 "armed_vacation", "armed_custom_bypass", "pending",    "arming",
                                 "disarming",      "triggered",           "unknown"};
constexpr const char *LOCK[] = {"locked", "unlocked", "locking", "unlocking", "open", "opening", "jammed", "unknown"};
#ifdef USE_LVGL_MATERIAL_INFO_CONTEXT
constexpr const char *PERSON_PHASE[] = {"home", "away", "zone", "unknown"};
constexpr const char *SUN_PHASE[] = {"above_horizon", "below_horizon", "unknown"};
constexpr const char *VALUE_STATUS[] = {"value", "unknown", "invalid", "too_long"};
constexpr const char *NEXT_STATUS[] = {"value", "unknown", "invalid", "too_long", "stale"};
#endif
bool fields(JsonObjectConst object, std::initializer_list<const char *> names) {
  if (object.isNull() || object.size() != names.size())
    return false;
  for (const char *name : names) {
    bool found = false;
    for (auto pair : object)
      found |= std::string_view(pair.key().c_str(), pair.key().size()) == name;
    if (!found)
      return false;
  }
  return true;
}
bool integer(JsonVariantConst value, uint32_t high, uint32_t &result) {
  if (value.is<bool>() || !value.is<uint32_t>())
    return false;
  result = value.as<uint32_t>();
  return result <= high;
}
bool sequence(JsonVariantConst value, uint32_t &result) {
  return integer(value, CONTROL_MAX_SEQUENCE, result) && result != 0;
}
template<size_t N> bool text(JsonVariantConst value, ControlText<N> &result) {
  if (!value.is<const char *>())
    return false;
  const auto string = value.as<ArduinoJson::JsonString>();
  return result.assign({string.c_str(), string.size()});
}
template<size_t N> bool enumeration(JsonVariantConst value, const char *const (&names)[N], uint8_t &result) {
  if (!value.is<const char *>())
    return false;
  const auto string = value.as<ArduinoJson::JsonString>();
  const std::string_view actual(string.c_str(), string.size());
  for (size_t index = 0; index < N; index++)
    if (actual == names[index]) {
      result = index;
      return true;
    }
  return false;
}
bool optional_bool(JsonVariantConst value, uint8_t &result) {
  if (value.isNull()) {
    result = Extra::UNKNOWN_BOOL;
    return true;
  }
  if (!value.is<bool>())
    return false;
  result = value.as<bool>() ? 1 : 0;
  return true;
}
void write_bool(ArduinoJson::JsonObject object, const char *key, uint8_t value) {
  if (value == Extra::UNKNOWN_BOOL)
    object[key] = nullptr;
  else
    object[key] = value != 0;
}
}  // namespace

bool control_parse_extra_features_allocated(std::string_view payload, ControlText<32> &channel, uint16_t &profiles,
                                            uint16_t configured, bool bind, ArduinoJson::Allocator *allocator) {
  if (payload.empty() || payload.size() > 512)
    return false;
  ArduinoJson::JsonDocument document(allocator);
  if (deserializeJson(document, payload.data(), payload.size(), ArduinoJson::DeserializationOption::NestingLimit(3)))
    return false;
  const auto object = document.as<JsonObjectConst>();
  ControlText<32> parsed;
  uint32_t value;
  if (document.overflowed() || !fields(object, {"v", "type", "channel", "profiles"}) ||
      !integer(object["v"], 1, value) || value != 1 ||
      object["type"].as<ArduinoJson::JsonString>() != "entity_panel_extra_v1" || !text(object["channel"], parsed) ||
      !control_valid_hex(parsed) || !object["profiles"].is<ArduinoJson::JsonArrayConst>())
    return false;
  const auto offers = object["profiles"].as<ArduinoJson::JsonArrayConst>();
  if (offers.size() > 8 || (bind && offers.size() == 0))
    return false;
  uint16_t accepted = 0;
#ifdef USE_LVGL_MATERIAL_TILE_EVENT_CONTROLS
  bool offered_action_v2 = false, offered_tile_event = false;
#endif
#ifdef USE_LVGL_MATERIAL_INFO_CONTEXT
  bool offered_info = false, offered_context = false;
#endif
  std::string_view previous;
  for (auto offer : offers) {
    if (!fields(offer.as<JsonObjectConst>(), {"name", "version"}) || !offer["name"].is<const char *>() ||
        !integer(offer["version"], 255, value) || value == 0)
      return false;
    const auto string = offer["name"].as<ArduinoJson::JsonString>();
    const std::string_view name(string.c_str(), string.size());
    if (name.empty() || name.size() > 24 || name[0] < 'a' || name[0] > 'z' || name <= previous)
      return false;
    for (char c : name)
      if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
        return false;
    previous = name;
    uint16_t bit = value == 1 ? name == "info" ? 1 : name == "lock" ? 2 : 0 : 0;
#ifdef USE_LVGL_MATERIAL_INFO_CONTEXT
    offered_info |= value == 1 && name == "info";
    offered_context |= value == 1 && name == "info_context";
    if (value == 1 && name == "info_context")
      bit = 32;
#endif
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
    if (value == 1 && name == "media")
      bit = 4;
#endif
#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS
    if (value == 1 && name == "action")
      bit = 8;
    // Version 2 keeps the Action payload and admits the dispatched terminal.
    if (value == 2 && name == "action")
      bit = 24;
#endif
#ifdef USE_LVGL_MATERIAL_TILE_EVENT_CONTROLS
    offered_action_v2 |= value == 2 && name == "action";
    offered_tile_event |= value == 1 && name == "tile_event";
    if (value == 1 && name == "tile_event")
      bit = 256;
#endif
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
    if (value == 1 && name == "text")
      bit = 128;
#endif
#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
    if (value == 1 && name == "datetime")
      bit = 64;
#endif
    if (bind && (bit == 0 || (configured & bit) != bit))
      return false;
    if ((configured & bit) == bit)
      accepted |= bit;
  }
#ifdef USE_LVGL_MATERIAL_INFO_CONTEXT
  if (offered_context && !offered_info)
    return false;
  if ((accepted & 32U) != 0 && (accepted & 1U) == 0)
    return false;
#endif
#ifdef USE_LVGL_MATERIAL_TILE_EVENT_CONTROLS
  if ((offered_tile_event && !offered_action_v2) || ((accepted & 256U) != 0 && (accepted & 24U) != 24U))
    return false;
#endif
  // Detect duplicate keys discarded by ArduinoJson without retaining another DOM.
  size_t colons = 0;
  bool quoted = false, escaped = false;
  for (char c : payload) {
    if (quoted) {
      if (escaped)
        escaped = false;
      else if (c == '\\')
        escaped = true;
      else if (c == '"')
        quoted = false;
    } else if (c == '"')
      quoted = true;
    else if (c == ':')
      colons++;
  }
  if (quoted || colons != 4 + offers.size() * 2)
    return false;
  channel = parsed;
  profiles = accepted;
  return true;
}

bool control_decode_extra(JsonObjectConst object, ControlMessage &message) {
  if (message.identity.context.control_kind != ControlEntityKind::CONTROL_ENTITY_PANEL)
    return false;
  if (message.kind == ControlKind::CONTROL_KIND_COMMAND) {
    if (!fields(object, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind",
                         "token", "command_seq", "op", "expected_state_generation"}) ||
        object["op"].as<ArduinoJson::JsonString>() != "lock" || !sequence(object["command_seq"], message.command_seq) ||
        !sequence(object["expected_state_generation"], message.expected_state_generation))
      return false;
    message.operation = ControlOperation::CONTROL_OPERATION_EXTRA_LOCK;
    return true;
  }
  if (message.kind != ControlKind::CONTROL_KIND_GRANT && message.kind != ControlKind::CONTROL_KIND_STATE)
    return false;
  uint32_t number;
  if (!fields(object, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind",
                       "token", "state_seq", "ttl_ms", "title", "panel_type", "available", "panel_caps", "panel_state",
                       "eligible_actions", "state_generation"}) ||
      !sequence(object["state_seq"], message.state.state_seq) || !integer(object["ttl_ms"], CONTROL_LEASE_MS, number) ||
      number != CONTROL_LEASE_MS || !text(object["title"], message.state.title) || !object["available"].is<bool>())
    return false;
  auto &extra = message.state.emplace_family<Extra>();
  const auto caps = object["panel_caps"].as<JsonObjectConst>();
  const auto state = object["panel_state"].as<JsonObjectConst>();
  if (!sequence(object["state_generation"], extra.state_generation) ||
      !enumeration(state["source"], SOURCES, extra.source) || !text(state["updated_text"], extra.updated))
    return false;
  if (object["panel_type"].as<ArduinoJson::JsonString>() == "lock") {
    extra.type = 4;
    auto &lock = extra.data.emplace<Extra::Lock>();
    if (!fields(caps, {"lock"}) || !caps["lock"].is<bool>() || !fields(state, {"source", "updated_text", "phase"}) ||
        !enumeration(state["phase"], LOCK, lock.phase))
      return false;
    lock.capability = caps["lock"];
  } else {
    if (object["panel_type"].as<ArduinoJson::JsonString>() != "info" || !fields(caps, {"info_type", "device_class"}))
      return false;
    if (!enumeration(caps["info_type"], TYPES, extra.type)) {
#ifdef USE_LVGL_MATERIAL_INFO_CONTEXT
      const auto type = caps["info_type"].as<ArduinoJson::JsonString>();
      if (type == "person")
        extra.type = 5;
      else if (type == "sun")
        extra.type = 6;
      else
#endif
        return false;
    }
    if (extra.type == 0) {
      auto &sensor = extra.data.emplace<Extra::Sensor>();
      if ((!caps["device_class"].isNull() &&
           (!text(caps["device_class"], sensor.device_class) || sensor.device_class.view().empty())) ||
          !fields(state, {"source", "updated_text", "value_status", "value_text", "unit_text"}) ||
          !enumeration(state["value_status"], TEXT_STATUS, sensor.status) || !text(state["value_text"], sensor.value) ||
          !text(state["unit_text"], sensor.unit))
        return false;
    } else if (extra.type == 1 || extra.type == 3) {
      auto &observed = extra.data.emplace<Extra::State>();
      if (!fields(state,
                  {"source", "updated_text", "text_status", "state_text", extra.type == 1 ? "active" : "phase"}) ||
          !enumeration(state["text_status"], TEXT_STATUS, observed.status) || !text(state["state_text"], observed.text))
        return false;
      if (extra.type == 1) {
        if ((!caps["device_class"].isNull() &&
             (!text(caps["device_class"], observed.device_class) || observed.device_class.view().empty())) ||
            !optional_bool(state["active"], observed.phase))
          return false;
      } else if (!caps["device_class"].isNull() || !enumeration(state["phase"], ALARM, observed.phase)) {
        return false;
      }
    } else if (extra.type == 2) {
      auto &calendar = extra.data.emplace<Extra::Calendar>();
      if (!caps["device_class"].isNull() ||
          !fields(state, {"source", "updated_text", "event", "active", "summary", "start_text", "end_text", "zone_text",
                          "all_day", "location", "description", "truncated_fields"}) ||
          !enumeration(state["event"], EVENTS, calendar.event) || !optional_bool(state["active"], calendar.active) ||
          !optional_bool(state["all_day"], calendar.all_day) || !text(state["summary"], calendar.summary) ||
          !text(state["start_text"], calendar.start) || !text(state["end_text"], calendar.end) ||
          !text(state["zone_text"], calendar.zone) || !text(state["location"], calendar.location) ||
          !text(state["description"], calendar.description) || !integer(state["truncated_fields"], 7, number))
        return false;
      calendar.truncated = number;
#ifdef USE_LVGL_MATERIAL_INFO_CONTEXT
    } else if (extra.type == 5) {
      auto &person = extra.data.emplace<Extra::Person>();
      if (!caps["device_class"].isNull() ||
          !fields(state, {"source", "updated_text", "phase", "text_status", "state_text", "changed_status",
                          "changed_text", "zone_text"}) ||
          !enumeration(state["phase"], PERSON_PHASE, person.phase) ||
          !enumeration(state["text_status"], TEXT_STATUS, person.status) || !text(state["state_text"], person.text) ||
          !enumeration(state["changed_status"], VALUE_STATUS, person.changed_status) ||
          !text(state["changed_text"], person.changed) || !text(state["zone_text"], person.zone))
        return false;
    } else if (extra.type == 6) {
      auto &sun = extra.data.emplace<Extra::Sun>();
      if (!caps["device_class"].isNull() ||
          !fields(state, {"source", "updated_text", "phase", "next_rising_status", "next_rising_text",
                          "next_setting_status", "next_setting_text", "elevation_status", "elevation_text",
                          "azimuth_status", "azimuth_text", "zone_text"}) ||
          !enumeration(state["phase"], SUN_PHASE, sun.phase) ||
          !enumeration(state["next_rising_status"], NEXT_STATUS, sun.rising_status) ||
          !text(state["next_rising_text"], sun.rising) ||
          !enumeration(state["next_setting_status"], NEXT_STATUS, sun.setting_status) ||
          !text(state["next_setting_text"], sun.setting) ||
          !enumeration(state["elevation_status"], VALUE_STATUS, sun.elevation_status) ||
          !text(state["elevation_text"], sun.elevation) ||
          !enumeration(state["azimuth_status"], VALUE_STATUS, sun.azimuth_status) ||
          !text(state["azimuth_text"], sun.azimuth) || !text(state["zone_text"], sun.zone))
        return false;
#endif
    } else {
      return false;
    }
  }
  return object["available"].as<bool>() == extra.available() && integer(object["eligible_actions"], 1, number) &&
         number == control_extra_eligible(extra) && control_valid_extra_snapshot(message.state);
}

void control_encode_extra(ArduinoJson::JsonObject object, const ControlMessage &message) {
  if (message.kind == ControlKind::CONTROL_KIND_COMMAND) {
    object["expected_state_generation"] = message.expected_state_generation;
    return;
  }
  const auto *extra = message.state.family_if<Extra>();
  if (extra == nullptr)
    return;
#ifdef USE_LVGL_MATERIAL_INFO_AGENDA
  if (extra->type == 7) {
    control_encode_agenda(object, message);
    return;
  }
#endif
#ifdef USE_LVGL_MATERIAL_INFO_STATISTICS
  if (extra->type == 8) {
    control_encode_statistics(object, message);
    return;
  }
#endif
  object["panel_type"] = extra->type == 4 ? "lock" : "info";
  object["state_generation"] = extra->state_generation;
  object["available"] = extra->available();
  object["eligible_actions"] = control_extra_eligible(*extra);
  auto caps = object["panel_caps"].to<ArduinoJson::JsonObject>();
  auto state = object["panel_state"].to<ArduinoJson::JsonObject>();
  state["source"] = SOURCES[extra->source];
  state["updated_text"] = extra->updated.bytes.data();
  if (extra->type != 4) {
    caps["info_type"] = extra->type == 5 ? "person" : extra->type == 6 ? "sun" : TYPES[extra->type];
    caps["device_class"] = nullptr;
  }
  if (const auto *sensor = std::get_if<Extra::Sensor>(&extra->data)) {
    if (!sensor->device_class.view().empty())
      caps["device_class"] = sensor->device_class.bytes.data();
    state["value_status"] = TEXT_STATUS[sensor->status];
    state["value_text"] = sensor->value.bytes.data();
    state["unit_text"] = sensor->unit.bytes.data();
  } else if (const auto *observed = std::get_if<Extra::State>(&extra->data)) {
    if (!observed->device_class.view().empty())
      caps["device_class"] = observed->device_class.bytes.data();
    state["text_status"] = TEXT_STATUS[observed->status];
    state["state_text"] = observed->text.bytes.data();
    if (extra->type == 1)
      write_bool(state, "active", observed->phase);
    else
      state["phase"] = ALARM[observed->phase];
  } else if (const auto *calendar = std::get_if<Extra::Calendar>(&extra->data)) {
    state["event"] = EVENTS[calendar->event];
    write_bool(state, "active", calendar->active);
    write_bool(state, "all_day", calendar->all_day);
    state["summary"] = calendar->summary.bytes.data();
    state["start_text"] = calendar->start.bytes.data();
    state["end_text"] = calendar->end.bytes.data();
    state["zone_text"] = calendar->zone.bytes.data();
    state["location"] = calendar->location.bytes.data();
    state["description"] = calendar->description.bytes.data();
    state["truncated_fields"] = calendar->truncated;
  } else if (const auto *lock = std::get_if<Extra::Lock>(&extra->data)) {
    caps["lock"] = lock->capability;
    state["phase"] = LOCK[lock->phase];
#ifdef USE_LVGL_MATERIAL_INFO_CONTEXT
  } else if (const auto *person = std::get_if<Extra::Person>(&extra->data)) {
    state["phase"] = PERSON_PHASE[person->phase];
    state["text_status"] = TEXT_STATUS[person->status];
    state["state_text"] = person->text.bytes.data();
    state["changed_status"] = VALUE_STATUS[person->changed_status];
    state["changed_text"] = person->changed.bytes.data();
    state["zone_text"] = person->zone.bytes.data();
  } else if (const auto *sun = std::get_if<Extra::Sun>(&extra->data)) {
    state["phase"] = SUN_PHASE[sun->phase];
    state["next_rising_status"] = NEXT_STATUS[sun->rising_status];
    state["next_rising_text"] = sun->rising.bytes.data();
    state["next_setting_status"] = NEXT_STATUS[sun->setting_status];
    state["next_setting_text"] = sun->setting.bytes.data();
    state["elevation_status"] = VALUE_STATUS[sun->elevation_status];
    state["elevation_text"] = sun->elevation.bytes.data();
    state["azimuth_status"] = VALUE_STATUS[sun->azimuth_status];
    state["azimuth_text"] = sun->azimuth.bytes.data();
    state["zone_text"] = sun->zone.bytes.data();
#endif
  }
}
}  // namespace esphome::lvgl_material
#endif
