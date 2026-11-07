#include "material_entity_control_codec.h"
#ifdef USE_LVGL_MATERIAL_ACTION_CONTROLS
#include <initializer_list>

namespace esphome::lvgl_material {
namespace {
using Action = ControlActionSnapshot;
using ArduinoJson::JsonObject;
using ArduinoJson::JsonObjectConst;
using ArduinoJson::JsonVariantConst;
constexpr const char *TYPES[] = {"button",
                                 "input_button",
                                 "scene",
                                 "script",
                                 "automation"
#ifdef USE_LVGL_MATERIAL_TILE_EVENT_CONTROLS
                                 ,
                                 "tile_event"
#endif
};
constexpr const char *SOURCES[] = {"known", "unknown", "unavailable"};
constexpr const char *PHASES[] = {"stateless", "idle", "running", "enabled", "disabled", "unknown"};
constexpr const char *LAST_USED[] = {"value", "unknown", "invalid", "too_long"};
constexpr const char *BLOCKED[] = {"none", "needs_parameters", "metadata_unavailable", "service_unavailable"};
constexpr const char *OPERATIONS[] = {"press", "activate", "run", "stop", "enable", "disable", "trigger"};

bool fields(JsonObjectConst object, std::initializer_list<const char *> keys) {
  if (object.isNull() || object.size() != keys.size())
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
bool policy(JsonVariantConst value, uint8_t &out) {
  if (value.isNull()) {
    out = Action::UNKNOWN_BOOL;
    return true;
  }
  if (!value.is<bool>())
    return false;
  out = value.as<bool>();
  return true;
}
void write_policy(JsonObject object, const char *key, uint8_t value) {
  if (value == Action::UNKNOWN_BOOL)
    object[key] = nullptr;
  else
    object[key] = value != 0;
}
}  // namespace

#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
bool control_parse_action_parameters_bind_allocated(std::string_view payload, ControlText<32> &channel,
                                                    ArduinoJson::Allocator *allocator) {
  if (payload.empty() || payload.size() > 512 || payload.find('\0') != std::string_view::npos)
    return false;
  ArduinoJson::JsonDocument document(allocator);
  if (deserializeJson(document, payload.data(), payload.size(), ArduinoJson::DeserializationOption::NestingLimit(1)) ||
      document.overflowed())
    return false;
  const auto object = document.as<JsonObjectConst>();
  ControlText<32> parsed;
  uint32_t version;
  if (!fields(object, {"v", "type", "channel", "parameters_version"}) || !integer(object["v"], 1, version) ||
      version != 1 || object["type"].as<ArduinoJson::JsonString>() != "entity_action_parameters_v1" ||
      !integer(object["parameters_version"], 1, version) || version != 1 || !text(object["channel"], parsed) ||
      !control_valid_hex(parsed))
    return false;
  size_t colons = 0;
  bool quoted = false, escaped = false, finished = false;
  for (char c : payload) {
    if (finished) {
      if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
        return false;
    } else if (quoted) {
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
    else if (c == '}')
      finished = true;
  }
  if (quoted || !finished || colons != 4)
    return false;
  channel = parsed;
  return true;
}

#endif

bool control_decode_action(JsonObjectConst object, ControlMessage &message) {
  if (message.identity.context.control_kind != ControlEntityKind::CONTROL_ENTITY_PANEL)
    return false;
  if (message.kind == ControlKind::CONTROL_KIND_COMMAND) {
    uint8_t version, operation;
    bool command_fields =
        fields(object, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind",
                        "token", "action_version", "command_seq", "expected_state_generation", "op"});
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
    if (!object["action_parameters_version"].isUnbound()) {
      command_fields =
          fields(object, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind",
                          "token", "action_version", "command_seq", "expected_state_generation", "op",
                          "action_parameters_version", "parameter_value"}) &&
          integer(object["action_parameters_version"], 1, version, true) && object["parameter_value"].is<bool>() &&
          object["op"].as<ArduinoJson::JsonString>() == "run";
      message.panel_value = 1;
      message.panel_enabled = object["parameter_value"].as<bool>();
    }
#endif

    if (!command_fields || !integer(object["action_version"], 1, version, true) ||
        !integer(object["command_seq"], CONTROL_MAX_SEQUENCE, message.command_seq, true) ||
        !integer(object["expected_state_generation"], CONTROL_MAX_SEQUENCE, message.expected_state_generation, true) ||
        !enumeration(object["op"], OPERATIONS, operation))
      return false;
    message.operation = static_cast<ControlOperation>(
        static_cast<uint8_t>(ControlOperation::CONTROL_OPERATION_ACTION_PRESS) + operation);
    return true;
  }
  if (message.kind != ControlKind::CONTROL_KIND_GRANT && message.kind != ControlKind::CONTROL_KIND_STATE)
    return false;
  auto &action = message.state.emplace_family<Action>();
  uint32_t ttl;
  if (!fields(object, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind",
                       "token", "state_seq", "ttl_ms", "title", "panel_type", "available", "panel_caps", "panel_state",
                       "eligible_actions", "state_generation"}) ||
      object["panel_type"].as<ArduinoJson::JsonString>() != "action" ||
      !integer(object["state_seq"], CONTROL_MAX_SEQUENCE, message.state.state_seq, true) ||
      !integer(object["ttl_ms"], CONTROL_LEASE_MS, ttl) || ttl != CONTROL_LEASE_MS ||
      !integer(object["state_generation"], CONTROL_MAX_SEQUENCE, action.state_generation, true) ||
      !integer(object["eligible_actions"], Action::ALL_OPERATIONS, action.eligible) ||
      !text(object["title"], message.state.title) || !object["available"].is<bool>())
    return false;
  const auto caps = object["panel_caps"].as<JsonObjectConst>();
  const auto state = object["panel_state"].as<JsonObjectConst>();
  bool caps_fields = fields(caps, {"action_type", "operations", "skip_condition", "stop_actions", "run_blocked"});
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
  if (!caps["parameters_version"].isUnbound()) {
    constexpr const char *STATUS[] = {"ready", "unsupported", "metadata_unavailable"};
    caps_fields = fields(caps, {"action_type", "operations", "skip_condition", "stop_actions", "run_blocked",
                                "parameters_version", "parameter_status", "parameter_label", "parameter_default"}) &&
                  integer(caps["parameters_version"], 1, action.parameters_version, true) &&
                  enumeration(caps["parameter_status"], STATUS, action.parameter_status) &&
                  text(caps["parameter_label"], action.parameter_label) &&
                  policy(caps["parameter_default"], action.parameter_default);
  }
#endif
  if (!caps_fields || !fields(state, {"source", "phase", "last_used_status", "last_used_text", "current"}) ||
      !enumeration(caps["action_type"], TYPES, action.type) ||
      !integer(caps["operations"], Action::ALL_OPERATIONS, action.operations) ||
      !policy(caps["skip_condition"], action.skip_condition) || !policy(caps["stop_actions"], action.stop_actions) ||
      !enumeration(caps["run_blocked"], BLOCKED, action.run_blocked) ||
      !enumeration(state["source"], SOURCES, action.source) || !enumeration(state["phase"], PHASES, action.phase) ||
      !enumeration(state["last_used_status"], LAST_USED, action.last_used_status) ||
      !text(state["last_used_text"], action.last_used))
    return false;
  action.current_known = !state["current"].isNull();
  if (action.current_known && !integer(state["current"], 65535, action.current))
    return false;
  return object["available"].as<bool>() == action.available() && control_valid_action_snapshot(message.state);
}

void control_encode_action(JsonObject object, const ControlMessage &message) {
  if (message.kind == ControlKind::CONTROL_KIND_COMMAND) {
    object["action_version"] = 1;
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
    if (message.panel_value != 0 || message.panel_enabled) {
      // Let the closed decoder reject invalid markers/Stop combinations instead of downgrading them.
      object["action_parameters_version"] = message.panel_value;
      object["parameter_value"] = message.panel_enabled;
    }
#endif

    object["expected_state_generation"] = message.expected_state_generation;
    return;
  }
  const auto *action = message.state.family_if<Action>();
  if (action == nullptr)
    return;
  object["panel_type"] = "action";
  object["available"] = action->available();
  object["state_generation"] = action->state_generation;
  object["eligible_actions"] = action->eligible;
  auto caps = object["panel_caps"].to<JsonObject>();
  caps["action_type"] = TYPES[action->type];
  caps["operations"] = action->operations;
  write_policy(caps, "skip_condition", action->skip_condition);
  write_policy(caps, "stop_actions", action->stop_actions);
  caps["run_blocked"] = BLOCKED[action->run_blocked];
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
  if (action->parameters_version == 1) {
    constexpr const char *STATUS[] = {"ready", "unsupported", "metadata_unavailable"};
    caps["parameters_version"] = 1;
    caps["parameter_status"] = STATUS[action->parameter_status];
    caps["parameter_label"] = action->parameter_label.bytes.data();
    write_policy(caps, "parameter_default", action->parameter_default);
  }
#endif

  auto state = object["panel_state"].to<JsonObject>();
  state["source"] = SOURCES[action->source];
  state["phase"] = PHASES[action->phase];
  state["last_used_status"] = LAST_USED[action->last_used_status];
  state["last_used_text"] = action->last_used.bytes.data();
  if (action->current_known)
    state["current"] = action->current;
  else
    state["current"] = nullptr;
}

}  // namespace esphome::lvgl_material
#endif
