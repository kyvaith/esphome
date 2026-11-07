#include "material_entity_control_codec.h"
#ifdef USE_LVGL_MATERIAL_INFO_AGENDA
#include <initializer_list>

namespace esphome::lvgl_material {
namespace {
using Extra = ControlExtraSnapshot;
using ArduinoJson::JsonObjectConst;
using ArduinoJson::JsonVariantConst;
constexpr const char *SOURCES[] = {"known", "unknown", "unavailable"};
constexpr const char *STATUSES[] = {"loading", "ready", "error", "unknown"};
struct Reason {
  const char *name;
  ControlReason code;
};
constexpr Reason REASONS[] = {
    {"none", ControlReason::CONTROL_REASON_NONE},
    {"unsupported", ControlReason::CONTROL_REASON_UNSUPPORTED},
    {"busy", ControlReason::CONTROL_REASON_BUSY},
    {"unauthorized", ControlReason::CONTROL_REASON_UNAUTHORIZED},
    {"stale_state", ControlReason::CONTROL_REASON_STALE_STATE},
    {"stale_sequence", ControlReason::CONTROL_REASON_STALE_SEQUENCE},
    {"sequence_conflict", ControlReason::CONTROL_REASON_SEQUENCE_CONFLICT},
    {"selection_changed", ControlReason::CONTROL_REASON_SELECTION_CHANGED},
    {"connection_lost", ControlReason::CONTROL_REASON_CONNECTION_LOST},
    {"entity_unavailable", ControlReason::CONTROL_REASON_ENTITY_UNAVAILABLE},
    {"expired", ControlReason::CONTROL_REASON_EXPIRED},
};
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
  const std::string_view actual(string.c_str(), string.size());
  for (size_t index = 0; index < N; index++)
    if (actual == names[index]) {
      result = index;
      return true;
    }
  return false;
}
}  // namespace

bool control_parse_info_agenda_bind_allocated(std::string_view payload, ControlText<32> &channel,
                                              ArduinoJson::Allocator *allocator) {
  if (payload.empty() || payload.size() > 512)
    return false;
  ArduinoJson::JsonDocument document(allocator);
  if (deserializeJson(document, payload.data(), payload.size(), ArduinoJson::DeserializationOption::NestingLimit(1)) ||
      document.overflowed())
    return false;
  const auto object = document.as<JsonObjectConst>();
  ControlText<32> parsed;
  uint32_t version;
  if (!fields(object, {"v", "type", "channel", "agenda_version"}) || !integer(object["v"], 1, version) ||
      version != 1 || object["type"].as<ArduinoJson::JsonString>() != "entity_info_agenda_v1" ||
      !integer(object["agenda_version"], 1, version) || version != 1 || !text(object["channel"], parsed) ||
      !control_valid_hex(parsed))
    return false;
  // Reject duplicate keys collapsed by ArduinoJson and non-whitespace suffixes.
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

bool control_decode_agenda(ArduinoJson::JsonObjectConst object, ControlMessage &message) {
  if (message.identity.context.control_kind != ControlEntityKind::CONTROL_ENTITY_PANEL)
    return false;
  uint32_t value;
  if (message.kind == ControlKind::CONTROL_KIND_PAGE || message.kind == ControlKind::CONTROL_KIND_PAGE_RESULT) {
    const bool request = message.kind == ControlKind::CONTROL_KIND_PAGE;
    if (!(request
              ? fields(object, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision",
                                "control_kind", "token", "request_seq", "target_index", "expected_state_generation"})
              : fields(object,
                       {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind",
                        "token", "request_seq", "target_index", "state_seq", "status", "reason"})) ||
        !sequence(object["request_seq"], message.command_seq) || !integer(object["target_index"], 15, value))
      return false;
    message.value = value;
    if (request)
      return sequence(object["expected_state_generation"], message.expected_state_generation);
    if (!sequence(object["state_seq"], message.state.state_seq))
      return false;
    const auto status = object["status"].as<ArduinoJson::JsonString>();
    if (status != "ready" && status != "rejected")
      return false;
    message.enabled = status == "ready";
    const auto reason = object["reason"].as<ArduinoJson::JsonString>();
    for (const auto &item : REASONS)
      if (reason == item.name) {
        message.reason = item.code;
        return message.enabled == (item.code == ControlReason::CONTROL_REASON_NONE);
      }
    return false;
  }
  if ((message.kind != ControlKind::CONTROL_KIND_GRANT && message.kind != ControlKind::CONTROL_KIND_STATE) ||
      !fields(object, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind",
                       "token", "state_seq", "ttl_ms", "title", "panel_type", "panel_caps", "panel_state", "available",
                       "eligible_actions", "state_generation"}) ||
      object["panel_type"].as<ArduinoJson::JsonString>() != "info" ||
      !sequence(object["state_seq"], message.state.state_seq) || !integer(object["ttl_ms"], CONTROL_LEASE_MS, value) ||
      value != CONTROL_LEASE_MS || !text(object["title"], message.state.title) || !object["available"].is<bool>() ||
      !integer(object["eligible_actions"], 0, value))
    return false;
  const auto caps = object["panel_caps"].as<JsonObjectConst>();
  const auto state = object["panel_state"].as<JsonObjectConst>();
  if (!fields(caps, {"info_type", "agenda_version"}) || caps["info_type"].as<ArduinoJson::JsonString>() != "agenda" ||
      !integer(caps["agenda_version"], 1, value) || value != 1 ||
      !fields(state, {"source", "status", "request_seq", "index", "count", "has_more", "summary", "start_text",
                      "end_text", "zone_text", "all_day", "location", "description", "truncated_fields"}))
    return false;
  auto &extra = message.state.emplace_family<Extra>();
  extra.type = 7;
  auto &agenda = extra.data.emplace<Extra::Agenda>();
  if (!sequence(object["state_generation"], extra.state_generation) ||
      !enumeration(state["source"], SOURCES, extra.source) || !enumeration(state["status"], STATUSES, agenda.status) ||
      !integer(state["request_seq"], CONTROL_MAX_SEQUENCE, agenda.request_seq) || !integer(state["count"], 16, value))
    return false;
  agenda.count = value;
  if (!state["index"].isNull()) {
    if (!integer(state["index"], 15, value))
      return false;
    agenda.index = value;
  }
  if (!state["all_day"].isNull()) {
    if (!state["all_day"].is<bool>())
      return false;
    agenda.all_day = state["all_day"].as<bool>();
  }
  if (!state["has_more"].is<bool>() || !integer(state["truncated_fields"], 7, value) ||
      !text(state["summary"], agenda.summary) || !text(state["start_text"], agenda.start) ||
      !text(state["end_text"], agenda.end) || !text(state["zone_text"], agenda.zone) ||
      !text(state["location"], agenda.location) || !text(state["description"], agenda.description))
    return false;
  agenda.truncated = value;
  agenda.has_more = state["has_more"].as<bool>();
  return object["available"].as<bool>() == extra.available() && control_valid_extra_snapshot(message.state);
}

void control_encode_agenda(ArduinoJson::JsonObject object, const ControlMessage &message) {
  if (message.kind == ControlKind::CONTROL_KIND_PAGE || message.kind == ControlKind::CONTROL_KIND_PAGE_RESULT) {
    object["request_seq"] = message.command_seq;
    object["target_index"] = message.value;
    if (message.kind == ControlKind::CONTROL_KIND_PAGE)
      object["expected_state_generation"] = message.expected_state_generation;
    else {
      object["state_seq"] = message.state.state_seq;
      object["status"] = message.enabled ? "ready" : "rejected";
      for (const auto &reason : REASONS)
        if (reason.code == message.reason)
          object["reason"] = reason.name;
    }
    return;
  }
  const auto &extra = *message.state.family_if<Extra>();
  const auto &agenda = std::get<Extra::Agenda>(extra.data);
  object["panel_type"] = "info";
  object["state_generation"] = extra.state_generation;
  object["available"] = extra.available();
  object["eligible_actions"] = 0;
  auto caps = object["panel_caps"].to<ArduinoJson::JsonObject>();
  caps["info_type"] = "agenda";
  caps["agenda_version"] = 1;
  auto state = object["panel_state"].to<ArduinoJson::JsonObject>();
  state["source"] = SOURCES[extra.source];
  state["status"] = STATUSES[agenda.status];
  state["request_seq"] = agenda.request_seq;
  if (agenda.index == 255)
    state["index"] = nullptr;
  else
    state["index"] = agenda.index;
  state["count"] = agenda.count;
  state["has_more"] = agenda.has_more;
  state["summary"] = agenda.summary.bytes.data();
  state["start_text"] = agenda.start.bytes.data();
  state["end_text"] = agenda.end.bytes.data();
  state["zone_text"] = agenda.zone.bytes.data();
  if (agenda.all_day == Extra::UNKNOWN_BOOL)
    state["all_day"] = nullptr;
  else
    state["all_day"] = agenda.all_day != 0;
  state["location"] = agenda.location.bytes.data();
  state["description"] = agenda.description.bytes.data();
  state["truncated_fields"] = agenda.truncated;
}
}  // namespace esphome::lvgl_material
#endif
