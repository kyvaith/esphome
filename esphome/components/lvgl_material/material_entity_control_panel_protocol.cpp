#include "material_entity_control_codec.h"
#ifdef USE_LVGL_MATERIAL_PANEL_CONTROLS
#include <initializer_list>
namespace esphome::lvgl_material {
namespace {
using ArduinoJson::JsonObjectConst;
using ArduinoJson::JsonVariantConst;
constexpr const char *TYPES[] = {"timer", "remote", "power"};
constexpr const char *PHASES[] = {"unknown", "idle", "active", "paused"};
constexpr const char *POWER[] = {"unknown", "off", "on"};
constexpr const char *CLASSES[] = {"unknown", "switch", "outlet"};
constexpr const char *OPERATIONS[] = {"start", "resume", "pause", "cancel", "set_duration", "power", "activity"};
bool fields(JsonObjectConst object, std::initializer_list<const char *> names, bool extra = false) {
  if (object.isNull() || object.size() != names.size() + extra)
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
}  // namespace
bool control_parse_panel_bind_allocated(std::string_view payload, ControlText<32> &channel, uint8_t &types,
                                        ArduinoJson::Allocator *allocator) {
  if (payload.empty() || payload.size() > 512)
    return false;
  ArduinoJson::JsonDocument document(allocator);
  if (deserializeJson(document, payload.data(), payload.size(), ArduinoJson::DeserializationOption::NestingLimit(3)))
    return false;
  const auto object = document.as<JsonObjectConst>();
  uint32_t value;
  ControlText<32> parsed;
  if (document.overflowed() || !fields(object, {"v", "type", "panel_version", "channel", "panel_types"}) ||
      !integer(object["v"], 1, value) || value != 1 || !integer(object["panel_version"], 1, value) || value != 1 ||
      object["type"].as<ArduinoJson::JsonString>() != "entity_panel_features_v1" || !text(object["channel"], parsed) ||
      !control_valid_hex(parsed) || !object["panel_types"].is<ArduinoJson::JsonArrayConst>())
    return false;
  const auto names = object["panel_types"].as<ArduinoJson::JsonArrayConst>();
  if (names.size() == 0 || names.size() > 3)
    return false;
  uint8_t mask = 0, last = 0;
  for (auto name : names) {
    uint8_t index;
    if (!enumeration(name, TYPES, index) || (mask != 0 && index <= last))
      return false;
    last = index;
    mask |= 1U << index;
  }
  // ArduinoJson retains the last duplicate key. Count only structural separators.
  bool quoted = false, escaped = false;
  size_t colons = 0;
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
  if (quoted || colons != 5)
    return false;
  channel = parsed;
  types = mask;
  return true;
}
bool control_decode_panel(JsonObjectConst object, ControlMessage &message) {
  uint32_t number;
  if (message.kind == ControlKind::CONTROL_KIND_COMMAND) {
    uint8_t op;
    if (!enumeration(object["op"], OPERATIONS, op) ||
        !fields(object,
                {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind", "token",
                 "command_seq", "op", "expected_state_generation"},
                op >= 4) ||
        !sequence(object["command_seq"], message.command_seq) ||
        !sequence(object["expected_state_generation"], message.expected_state_generation))
      return false;
    message.operation =
        static_cast<ControlOperation>(static_cast<uint8_t>(ControlOperation::CONTROL_OPERATION_PANEL_START) + op);
    if (op >= 4) {
      if (!integer(object[op == 4 ? "duration_seconds" : "target_index"],
                   op == 4   ? 86400
                   : op == 5 ? 1
                             : 7,
                   number) ||
          (op == 4 && number == 0))
        return false;
      message.panel_value = number;
    }
    return true;
  }
  if (message.kind != ControlKind::CONTROL_KIND_GRANT && message.kind != ControlKind::CONTROL_KIND_STATE)
    return false;
  auto &panel = message.state.emplace_family<ControlSnapshot::Panel>();
  if (!fields(object, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind",
                       "token", "state_seq", "ttl_ms", "title", "panel_type", "available", "panel_caps", "panel_state",
                       "eligible_actions", "state_generation"}) ||
      !sequence(object["state_seq"], message.state.state_seq) || !integer(object["ttl_ms"], CONTROL_LEASE_MS, number) ||
      number != CONTROL_LEASE_MS || !text(object["title"], message.state.title) ||
      !enumeration(object["panel_type"], TYPES, panel.type) || !object["available"].is<bool>() ||
      !sequence(object["state_generation"], panel.state_generation) || !integer(object["eligible_actions"], 31, number))
    return false;
  panel.available = object["available"];
  panel.eligible_actions = number;
  const auto caps = object["panel_caps"].as<JsonObjectConst>();
  const auto state = object["panel_state"].as<JsonObjectConst>();
  if (panel.type == 0) {
    if (!fields(caps, {"start", "pause", "cancel", "initial_duration"}) || !caps["start"].is<bool>() ||
        !caps["pause"].is<bool>() || !caps["cancel"].is<bool>() ||
        !fields(state, {"phase", "duration_seconds", "remaining_seconds", "finishes_at_utc_s", "sampled_at_utc_s"}) ||
        !enumeration(state["phase"], PHASES, panel.phase))
      return false;
    panel.caps.start = caps["start"];
    panel.caps.pause = caps["pause"];
    panel.caps.cancel = caps["cancel"];
    if (!caps["initial_duration"].isNull()) {
      if (!integer(caps["initial_duration"], 86400, number) || number == 0)
        return false;
      panel.caps.initial_duration = number;
    }
    for (bool remaining : {false, true}) {
      const auto value = state[remaining ? "remaining_seconds" : "duration_seconds"];
      if (!value.isNull()) {
        if (!integer(value, CONTROL_MAX_SEQUENCE, number))
          return false;
        (remaining ? panel.remaining : panel.duration) = number;
      }
    }
    if (!state["finishes_at_utc_s"].isNull()) {
      if (!integer(state["finishes_at_utc_s"], ControlSnapshot::Panel::MAX_UTC, number) ||
          number < ControlSnapshot::Panel::MIN_UTC)
        return false;
      panel.finishes_at = number;
    }
    if (!integer(state["sampled_at_utc_s"], ControlSnapshot::Panel::MAX_UTC, number) ||
        number < ControlSnapshot::Panel::MIN_UTC)
      return false;
    panel.sampled_at = number;
  } else {
    if (!caps["turn_on"].is<bool>() || !caps["turn_off"].is<bool>() || !enumeration(state["power"], POWER, panel.power))
      return false;
    panel.caps.turn_on = caps["turn_on"];
    panel.caps.turn_off = caps["turn_off"];
    if (panel.type == 1) {
      if (!fields(caps, {"turn_on", "turn_off", "activities"}) || !fields(state, {"power", "activity_index"}) ||
          !caps["activities"].is<ArduinoJson::JsonArrayConst>())
        return false;
      const auto data = caps["activities"].as<ArduinoJson::JsonArrayConst>();
      if (data.size() > panel.caps.activities.size())
        return false;
      panel.caps.activity_count = data.size();
      for (size_t index = 0; index < data.size(); index++)
        if (!text(data[index], panel.caps.activities[index]))
          return false;
      if (!state["activity_index"].isNull()) {
        if (!integer(state["activity_index"], 7, number))
          return false;
        panel.activity_index = number;
      }
    } else if (!fields(caps, {"turn_on", "turn_off", "device_class"}) || !fields(state, {"power"}) ||
               !enumeration(caps["device_class"], CLASSES, panel.caps.device_class))
      return false;
  }
  return control_valid_panel_snapshot(message.state);
}
void control_encode_panel(ArduinoJson::JsonObject object, const ControlMessage &message) {
  if (message.kind == ControlKind::CONTROL_KIND_COMMAND) {
    object["expected_state_generation"] = message.expected_state_generation;
    if (message.operation == ControlOperation::CONTROL_OPERATION_PANEL_DURATION)
      object["duration_seconds"] = message.panel_value;
    else if (message.operation == ControlOperation::CONTROL_OPERATION_PANEL_POWER ||
             message.operation == ControlOperation::CONTROL_OPERATION_PANEL_ACTIVITY)
      object["target_index"] = message.panel_value;
    return;
  }
  const auto *family = message.state.family_if<ControlSnapshot::Panel>();
  if (family == nullptr)
    return;
  const auto &panel = *family;
  object["panel_type"] = TYPES[panel.type];
  object["available"] = panel.available;
  object["eligible_actions"] = panel.eligible_actions;
  object["state_generation"] = panel.state_generation;
  auto caps = object["panel_caps"].to<ArduinoJson::JsonObject>();
  auto state = object["panel_state"].to<ArduinoJson::JsonObject>();
  if (panel.type == 0) {
    caps["start"] = panel.caps.start;
    caps["pause"] = panel.caps.pause;
    caps["cancel"] = panel.caps.cancel;
    if (panel.caps.initial_duration)
      caps["initial_duration"] = panel.caps.initial_duration;
    else
      caps["initial_duration"] = nullptr;
    state["phase"] = PHASES[panel.phase];
    state["sampled_at_utc_s"] = panel.sampled_at;
    if (panel.finishes_at)
      state["finishes_at_utc_s"] = panel.finishes_at;
    else
      state["finishes_at_utc_s"] = nullptr;
    for (bool remaining : {false, true}) {
      const auto value = remaining ? panel.remaining : panel.duration;
      const char *key = remaining ? "remaining_seconds" : "duration_seconds";
      if (value == ControlSnapshot::Panel::UNKNOWN_VALUE)
        state[key] = nullptr;
      else
        state[key] = value;
    }
  } else {
    caps["turn_on"] = panel.caps.turn_on;
    caps["turn_off"] = panel.caps.turn_off;
    state["power"] = POWER[panel.power];
    if (panel.type == 1) {
      auto data = caps["activities"].to<ArduinoJson::JsonArray>();
      for (size_t index = 0; index < panel.caps.activity_count; index++)
        data.add(panel.caps.activities[index].bytes.data());
      if (panel.activity_index == ControlSnapshot::Panel::UNKNOWN_INDEX)
        state["activity_index"] = nullptr;
      else
        state["activity_index"] = panel.activity_index;
    } else
      caps["device_class"] = CLASSES[panel.caps.device_class];
  }
}
}  // namespace esphome::lvgl_material
#endif
