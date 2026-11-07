#include "material_entity_control_codec.h"
#ifdef USE_LVGL_MATERIAL_DATETIME_CONTROLS
#include <initializer_list>

namespace esphome::lvgl_material {
namespace {
using ArduinoJson::JsonObject;
using ArduinoJson::JsonObjectConst;
using ArduinoJson::JsonVariantConst;
constexpr const char *MODES[] = {"date", "time", "datetime"};
constexpr const char *SOURCES[] = {"known", "unknown", "unavailable"};
constexpr const char *FIELDS[] = {"year", "month", "day", "hour", "minute", "second"};

bool fields(JsonObjectConst object, std::initializer_list<const char *> keys) {
  if (object.isNull() || object.size() != keys.size())
    return false;
  for (const char *key : keys)
    if (object[key].isUnbound())
      return false;
  return true;
}
template<typename T> bool integer(JsonVariantConst value, uint32_t high, T &out, bool positive = false) {
  if (value.is<bool>() || !value.is<uint32_t>())
    return false;
  const auto number = value.as<uint32_t>();
  if (number > high || (positive && number == 0))
    return false;
  out = static_cast<T>(number);
  return true;
}
template<size_t N> bool text(JsonVariantConst value, ControlText<N> &out) {
  if (!value.is<const char *>())
    return false;
  const auto string = value.as<ArduinoJson::JsonString>();
  return number_plain_utf8({string.c_str(), string.size()}) && out.assign({string.c_str(), string.size()});
}
template<size_t N> bool enumeration(JsonVariantConst value, const char *const (&names)[N], uint8_t &out) {
  if (!value.is<const char *>())
    return false;
  const auto string = value.as<ArduinoJson::JsonString>();
  for (size_t index = 0; index < N; index++)
    if (std::string_view(string.c_str(), string.size()) == names[index]) {
      out = static_cast<uint8_t>(index);
      return true;
    }
  return false;
}
bool tuple(JsonObjectConst object, ControlDatetimeTuple &value) {
  for (uint8_t field = 0; field < 6; field++) {
    const auto item = object[FIELDS[field]];
    if (item.isUnbound())
      return false;
    if (!item.isNull()) {
      if (!integer(item, 9999, value.values[field]) || !control_datetime_field_valid(field, value.values[field]))
        return false;
      value.known_mask |= 1U << field;
    }
  }
  return true;
}
void write_tuple(JsonObject object, const ControlDatetimeTuple &value) {
  for (uint8_t field = 0; field < 6; field++) {
    if ((value.known_mask & (1U << field)) != 0)
      object[FIELDS[field]] = value.values[field];
    else
      object[FIELDS[field]] = nullptr;
  }
}
}  // namespace

bool control_decode_datetime(JsonObjectConst object, ControlMessage &message) {
  if (message.identity.context.control_kind != ControlEntityKind::CONTROL_ENTITY_PANEL)
    return false;
  if (message.kind == ControlKind::CONTROL_KIND_COMMAND) {
    uint8_t version;
    const auto target = object["target"].as<JsonObjectConst>();
    if (!fields(object, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind",
                         "token", "datetime_version", "command_seq", "expected_state_generation", "op", "target"}) ||
        !integer(object["datetime_version"], 1, version, true) ||
        !integer(object["command_seq"], CONTROL_MAX_SEQUENCE, message.command_seq, true) ||
        !integer(object["expected_state_generation"], CONTROL_MAX_SEQUENCE, message.expected_state_generation, true) ||
        object["op"].as<ArduinoJson::JsonString>() != "set_datetime" ||
        !fields(target, {"year", "month", "day", "hour", "minute", "second"}))
      return false;
    auto &datetime = message.state.emplace_family<ControlDatetimeSnapshot>();
    if (!tuple(target, datetime.value))
      return false;
    // The target's null mask determines its shape; the owner/HA session checks its admitted mode.
    for (uint8_t mode = 0; mode < 3; mode++) {
      const auto candidate = static_cast<ControlDatetimeMode>(mode);
      if (control_datetime_tuple_valid(datetime.value, candidate)) {
        datetime.mode = candidate;
        message.operation = ControlOperation::CONTROL_OPERATION_DATETIME_SET;
        return true;
      }
    }
    return false;
  }
  if (message.kind != ControlKind::CONTROL_KIND_GRANT && message.kind != ControlKind::CONTROL_KIND_STATE)
    return false;
  auto &datetime = message.state.emplace_family<ControlDatetimeSnapshot>();
  uint32_t ttl;
  uint8_t mode, eligible;
  if (!fields(object, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind",
                       "token", "state_seq", "ttl_ms", "title", "panel_type", "available", "panel_caps", "panel_state",
                       "eligible_actions", "state_generation"}) ||
      object["panel_type"].as<ArduinoJson::JsonString>() != "datetime" ||
      !integer(object["state_seq"], CONTROL_MAX_SEQUENCE, message.state.state_seq, true) ||
      !integer(object["ttl_ms"], CONTROL_LEASE_MS, ttl) || ttl != CONTROL_LEASE_MS ||
      !integer(object["state_generation"], CONTROL_MAX_SEQUENCE, datetime.state_generation, true) ||
      !integer(object["eligible_actions"], 1, eligible) || !text(object["title"], message.state.title) ||
      !object["available"].is<bool>())
    return false;
  const auto caps = object["panel_caps"].as<JsonObjectConst>();
  const auto state = object["panel_state"].as<JsonObjectConst>();
  if (!fields(caps, {"mode", "can_set", "zone_text"}) ||
      !fields(state, {"source", "year", "month", "day", "hour", "minute", "second", "updated_text"}) ||
      !enumeration(caps["mode"], MODES, mode) || !caps["can_set"].is<bool>() ||
      !text(caps["zone_text"], datetime.zone) || !enumeration(state["source"], SOURCES, datetime.source) ||
      !text(state["updated_text"], datetime.updated) || !tuple(state, datetime.value))
    return false;
  datetime.mode = static_cast<ControlDatetimeMode>(mode);
  datetime.can_set = caps["can_set"].as<bool>();
  return object["available"].as<bool>() == datetime.available() &&
         eligible == (datetime.available() && datetime.can_set ? 1 : 0) &&
         control_valid_datetime_snapshot(message.state);
}

void control_encode_datetime(JsonObject object, const ControlMessage &message) {
  const auto *datetime = message.state.family_if<ControlDatetimeSnapshot>();
  if (datetime == nullptr)
    return;
  if (message.kind == ControlKind::CONTROL_KIND_COMMAND) {
    object["datetime_version"] = 1;
    object["expected_state_generation"] = message.expected_state_generation;
    write_tuple(object["target"].to<JsonObject>(), datetime->value);
    return;
  }
  object["panel_type"] = "datetime";
  object["available"] = datetime->available();
  object["state_generation"] = datetime->state_generation;
  object["eligible_actions"] = datetime->available() && datetime->can_set ? 1 : 0;
  auto caps = object["panel_caps"].to<JsonObject>();
  caps["mode"] = MODES[static_cast<uint8_t>(datetime->mode)];
  caps["can_set"] = datetime->can_set;
  caps["zone_text"] = datetime->zone.bytes.data();
  auto state = object["panel_state"].to<JsonObject>();
  state["source"] = SOURCES[datetime->source];
  write_tuple(state, datetime->value);
  state["updated_text"] = datetime->updated.bytes.data();
}

}  // namespace esphome::lvgl_material
#endif
