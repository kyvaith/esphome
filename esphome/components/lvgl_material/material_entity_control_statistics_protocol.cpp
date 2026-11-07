#include "material_entity_control_codec.h"
#ifdef USE_LVGL_MATERIAL_INFO_STATISTICS
#include <initializer_list>

namespace esphome::lvgl_material {
namespace {
using Extra = ControlExtraSnapshot;
using ArduinoJson::JsonObjectConst;
using ArduinoJson::JsonVariantConst;
constexpr const char *SOURCES[] = {"known", "unknown", "unavailable"};
constexpr const char *STATUSES[] = {"loading", "ready", "empty", "unsupported", "error"};
constexpr const char *REASONS[] = {"none", "no_data", "recorder_missing", "no_metadata", "not_arithmetic",
                                  "unit_incompatible", "label_unrepresentable", "recorder_unavailable",
                                  "invalid_data", "read_failed", "read_timeout", "busy"};
bool fields(JsonObjectConst object, std::initializer_list<const char *> names) {
  if (object.isNull() || object.size() != names.size())
    return false;
  for (const char *name : names)
    if (object[name].isUnbound())
      return false;
  return true;
}
bool integer(JsonVariantConst value, uint32_t maximum, uint32_t &result) {
  if (value.is<bool>() || !value.is<uint32_t>())
    return false;
  result = value.as<uint32_t>();
  return result <= maximum;
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
  for (size_t i = 0; i < N; i++)
    if (std::string_view(string.c_str(), string.size()) == names[i]) {
      result = i;
      return true;
    }
  return false;
}
bool packed(JsonVariantConst value, Extra::Statistics &s) {
  if (!value.is<const char *>())
    return false;
  const auto string = value.as<ArduinoJson::JsonString>();
  const std::string_view bytes(string.c_str(), string.size());
  if (s.status != 1)
    return bytes.empty();
  if (bytes.size() != 288)
    return false;
  size_t at = 0;
  for (auto &point : s.points)
    for (auto &word : point) {
      uint16_t decoded = 0;
      for (size_t nibble = 0; nibble < 4; nibble++) {
        const char c = bytes[at++];
        const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (digit < 0)
          return false;
        decoded = (decoded << 4) | digit;
      }
      word = decoded;
    }
  return true;
}
}  // namespace

bool control_parse_info_statistics_bind_allocated(std::string_view payload, ControlText<32> &channel,
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
  if (!fields(object, {"v", "type", "channel", "statistics_version"}) || !integer(object["v"], 1, version) ||
      version != 1 || object["type"].as<ArduinoJson::JsonString>() != "entity_info_statistics_v1" ||
      !integer(object["statistics_version"], 1, version) || version != 1 || !text(object["channel"], parsed) ||
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

bool control_decode_statistics(JsonObjectConst object, ControlMessage &message) {
  if (message.identity.context.control_kind != ControlEntityKind::CONTROL_ENTITY_PANEL ||
      (message.kind != ControlKind::CONTROL_KIND_GRANT && message.kind != ControlKind::CONTROL_KIND_STATE))
    return false;
  uint32_t value;
  if (!fields(object, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind",
                       "token", "state_seq", "ttl_ms", "title", "panel_type", "panel_caps", "panel_state", "available",
                       "eligible_actions", "state_generation"}) ||
      object["panel_type"].as<ArduinoJson::JsonString>() != "info" ||
      !sequence(object["state_seq"], message.state.state_seq) || !integer(object["ttl_ms"], CONTROL_LEASE_MS, value) ||
      value != CONTROL_LEASE_MS || !text(object["title"], message.state.title) || !object["available"].is<bool>() ||
      !integer(object["eligible_actions"], 0, value))
    return false;
  const auto caps = object["panel_caps"].as<JsonObjectConst>();
  const auto state = object["panel_state"].as<JsonObjectConst>();
  if (!fields(caps, {"info_type", "statistics_version"}) ||
      caps["info_type"].as<ArduinoJson::JsonString>() != "statistics" ||
      !integer(caps["statistics_version"], 1, value) || value != 1 ||
      !fields(state, {"source", "status", "reason", "current_status", "window_end_hour", "valid_mask", "packed_q",
                      "flat", "axis_min_text", "axis_max_text", "unit", "start_text", "end_text", "zone_text"}))
    return false;
  auto &extra = message.state.emplace_family<Extra>();
  extra.type = 8;
  auto &s = extra.data.emplace<Extra::Statistics>();
  if (!sequence(object["state_generation"], extra.state_generation) ||
      !enumeration(state["source"], SOURCES, extra.source) || !enumeration(state["status"], STATUSES, s.status) ||
      !enumeration(state["reason"], REASONS, s.reason) || !enumeration(state["current_status"], SOURCES, s.current_status) ||
      !integer(state["window_end_hour"], UINT32_MAX, s.window_end_hour) ||
      !integer(state["valid_mask"], 0xffffff, s.valid_mask) || !state["flat"].is<bool>() || !packed(state["packed_q"], s) ||
      !text(state["axis_min_text"], s.axis_min) || !text(state["axis_max_text"], s.axis_max) ||
      !text(state["unit"], s.unit) || !text(state["start_text"], s.start) ||
      !text(state["end_text"], s.end) || !text(state["zone_text"], s.zone))
    return false;
  s.flat = state["flat"].as<bool>();
  return object["available"].as<bool>() == extra.available() && control_valid_extra_snapshot(message.state);
}

void control_encode_statistics(ArduinoJson::JsonObject object, const ControlMessage &message) {
  const auto &extra = *message.state.family_if<Extra>();
  const auto &s = std::get<Extra::Statistics>(extra.data);
  object["panel_type"] = "info";
  object["state_generation"] = extra.state_generation;
  object["available"] = extra.available();
  object["eligible_actions"] = 0;
  auto caps = object["panel_caps"].to<ArduinoJson::JsonObject>();
  caps["info_type"] = "statistics";
  caps["statistics_version"] = 1;
  auto state = object["panel_state"].to<ArduinoJson::JsonObject>();
  state["source"] = SOURCES[extra.source];
  state["status"] = STATUSES[s.status];
  state["reason"] = REASONS[s.reason];
  state["current_status"] = SOURCES[s.current_status];
  state["window_end_hour"] = s.window_end_hour;
  state["valid_mask"] = s.valid_mask;
  state["flat"] = s.flat;
  char packed_text[289];
  size_t at = 0;
  if (s.status == 1) {
    constexpr char HEX[] = "0123456789abcdef";
    for (const auto &point : s.points)
      for (uint16_t word : point)
        for (int shift = 12; shift >= 0; shift -= 4)
          packed_text[at++] = HEX[(word >> shift) & 15U];
  }
  packed_text[at] = '\0';
  // Mutable pointer makes ArduinoJson copy the bounded scratch string into its existing arena.
  state["packed_q"] = packed_text;
  state["axis_min_text"] = s.axis_min.bytes.data();
  state["axis_max_text"] = s.axis_max.bytes.data();
  state["unit"] = s.unit.bytes.data();
  state["start_text"] = s.start.bytes.data();
  state["end_text"] = s.end.bytes.data();
  state["zone_text"] = s.zone.bytes.data();
}
}  // namespace esphome::lvgl_material
#endif
