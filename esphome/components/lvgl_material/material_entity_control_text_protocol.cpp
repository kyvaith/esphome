#include "material_entity_control_codec.h"
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
#include <initializer_list>

namespace esphome::lvgl_material {
namespace {
using ArduinoJson::JsonObject;
using ArduinoJson::JsonObjectConst;
using ArduinoJson::JsonVariantConst;
constexpr const char *SOURCES[] = {"known", "unknown", "unavailable", "unsupported"};
constexpr const char *REASONS[] = {"none", "password", "mode", "pattern", "bounds", "length", "encoding", "characters"};
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
template<size_t N> bool enumeration(JsonVariantConst value, const char *const (&names)[N], uint8_t &out) {
  if (!value.is<const char *>())
    return false;
  const auto string = value.as<ArduinoJson::JsonString>();
  for (size_t i = 0; i < N; i++)
    if (std::string_view(string.c_str(), string.size()) == names[i]) {
      out = i;
      return true;
    }
  return false;
}
bool read_value(JsonVariantConst item, ControlTextValueView &value) {
  if (item.isUnbound())
    return false;
  if (item.isNull()) {
    value = {};
    return true;
  }
  if (!item.is<const char *>())
    return false;
  const auto string = item.as<ArduinoJson::JsonString>();
  value = {{string.c_str(), string.size()}, true};
  return control_text_validate(value.value, 0, CONTROL_TEXT_MAX_CODEPOINTS) ==
         ControlTextDraftReason::CONTROL_TEXT_DRAFT_REASON_NONE;
}
}  // namespace

bool control_decode_text(JsonObjectConst object, ControlMessage &message, ControlTextValueView &value) {
  if (message.identity.context.control_kind != ControlEntityKind::CONTROL_ENTITY_PANEL)
    return false;
  if (message.kind == ControlKind::CONTROL_KIND_COMMAND) {
    uint8_t version;
    if (!fields(object, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind",
                         "token", "text_version", "command_seq", "expected_state_generation", "op", "value"}) ||
        !integer(object["text_version"], 1, version, true) ||
        !integer(object["command_seq"], CONTROL_MAX_SEQUENCE, message.command_seq, true) ||
        !integer(object["expected_state_generation"], CONTROL_MAX_SEQUENCE, message.expected_state_generation, true) ||
        object["op"].as<ArduinoJson::JsonString>() != "set_value" || !read_value(object["value"], value) ||
        !value.present)
      return false;
    message.state.emplace_family<ControlTextSnapshot>();
    message.operation = ControlOperation::CONTROL_OPERATION_TEXT_SET;
    return true;
  }
  if (message.kind != ControlKind::CONTROL_KIND_GRANT && message.kind != ControlKind::CONTROL_KIND_STATE)
    return false;
  auto &text = message.state.emplace_family<ControlTextSnapshot>();
  uint32_t ttl;
  uint8_t eligible;
  if (!fields(object, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind",
                       "token", "state_seq", "ttl_ms", "title", "panel_type", "available", "panel_caps", "panel_state",
                       "eligible_actions", "state_generation"}) ||
      object["panel_type"].as<ArduinoJson::JsonString>() != "text" ||
      !integer(object["state_seq"], CONTROL_MAX_SEQUENCE, message.state.state_seq, true) ||
      !integer(object["ttl_ms"], CONTROL_LEASE_MS, ttl) || ttl != CONTROL_LEASE_MS ||
      !integer(object["state_generation"], CONTROL_MAX_SEQUENCE, text.state_generation, true) ||
      !integer(object["eligible_actions"], 1, eligible) || !object["title"].is<const char *>() ||
      !object["available"].is<bool>())
    return false;
  const auto title = object["title"].as<ArduinoJson::JsonString>();
  if (!message.state.title.assign({title.c_str(), title.size()}))
    return false;
  const auto caps = object["panel_caps"].as<JsonObjectConst>();
  const auto state = object["panel_state"].as<JsonObjectConst>();
  if (!fields(caps, {"min", "max", "can_set"}) || !fields(state, {"source", "value", "reason"}) ||
      !caps["can_set"].is<bool>() || !enumeration(state["source"], SOURCES, text.source) ||
      !enumeration(state["reason"], REASONS, text.reason) || !read_value(state["value"], value))
    return false;
  text.can_set = caps["can_set"].as<bool>();
  text.bounds_known = !caps["min"].isNull();
  if (text.bounds_known ? !integer(caps["min"], 255, text.minimum) || !integer(caps["max"], 255, text.maximum, true)
                        : !caps["max"].isNull())
    return false;
  return object["available"].as<bool>() == text.available() && eligible == (text.available() && text.can_set ? 1 : 0) &&
         control_valid_text_snapshot(message.state, value);
}

void control_encode_text(JsonObject object, const ControlMessage &message, ControlTextValueView value) {
  const auto *text = message.state.family_if<ControlTextSnapshot>();
  if (text == nullptr)
    return;
  if (message.kind == ControlKind::CONTROL_KIND_COMMAND) {
    object["text_version"] = 1;
    object["expected_state_generation"] = message.expected_state_generation;
    object["value"] = value.value.empty() ? std::string_view("") : value.value;
    return;
  }
  object["panel_type"] = "text";
  object["available"] = text->available();
  object["state_generation"] = text->state_generation;
  object["eligible_actions"] = text->available() && text->can_set ? 1 : 0;
  auto caps = object["panel_caps"].to<JsonObject>();
  if (text->bounds_known) {
    caps["min"] = text->minimum;
    caps["max"] = text->maximum;
  } else {
    caps["min"] = nullptr;
    caps["max"] = nullptr;
  }
  caps["can_set"] = text->can_set;
  auto state = object["panel_state"].to<JsonObject>();
  state["source"] = SOURCES[text->source];
  state["reason"] = REASONS[text->reason];
  if (value.present)
    state["value"] = value.value.empty() ? std::string_view("") : value.value;
  else
    state["value"] = nullptr;
}

}  // namespace esphome::lvgl_material
#endif
