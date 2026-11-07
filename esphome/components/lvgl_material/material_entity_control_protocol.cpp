#include "material_entity_control_codec.h"

#include <ArduinoJson.h>
#include <cstring>
#include <initializer_list>

namespace esphome::lvgl_material {
namespace {

using ArduinoJson::JsonObjectConst;
using ArduinoJson::JsonVariantConst;
static constexpr const char *KINDS[] = {"capability", "bind",    "open",   "cancel_open", "grant",
                                        "state",      "command", "result", "close"};
static constexpr const char *OPERATIONS[] = {"brightness", "turn_on", "turn_off"};
static constexpr const char *RESULTS[] = {"pending",         "ha_accepted",   "state_matches",
                                          "already_matched", "not_confirmed", "rejected"};
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
                                          "unload"};

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

bool decode(JsonObjectConst object, ControlMessage &message) {
  uint64_t number;
  if (!uint_value(object["v"], 1, number) || number != 1 || !object["type"].is<const char *>() ||
      object["type"].as<ArduinoJson::JsonString>() != "light_brightness_v1" ||
      !enum_value(object["kind"], KINDS, message.kind))
    return false;
  if (message.kind == ControlKind::CONTROL_KIND_CAPABILITY) {
    if (!fields(object, {"v", "type", "kind", "enabled", "max_payload_bytes"}) || !object["enabled"].is<bool>() ||
        !uint_value(object["max_payload_bytes"], CONTROL_MAX_PAYLOAD_BYTES, number) ||
        number != CONTROL_MAX_PAYLOAD_BYTES)
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
  if (!control_valid_context(message.identity.context))
    return false;
  if (message.kind == ControlKind::CONTROL_KIND_OPEN)
    return fields(object, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision"});
  if (message.kind == ControlKind::CONTROL_KIND_CANCEL_OPEN)
    return fields(object, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "reason"}) &&
           enum_value(object["reason"], REASONS, message.reason) && close_reason(message.reason);
  if (!string_value(object["token"], message.identity.token) || !control_valid_hex(message.identity.token))
    return false;
  if (message.kind == ControlKind::CONTROL_KIND_CLOSE)
    return fields(object,
                  {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "token", "reason"}) &&
           enum_value(object["reason"], REASONS, message.reason) && close_reason(message.reason);
  if (message.kind == ControlKind::CONTROL_KIND_GRANT || message.kind == ControlKind::CONTROL_KIND_STATE) {
    if (!fields(object, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "token",
                         "state_seq", "ttl_ms", "title", "on", "brightness_pct", "caps"}) ||
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
    if (!enum_value(object["op"], OPERATIONS, message.operation))
      return false;
    if (message.operation == ControlOperation::CONTROL_OPERATION_BRIGHTNESS) {
      if (!fields(object, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "token",
                           "command_seq", "op", "value"}) ||
          !uint_value(object["value"], 100, number))
        return false;
      message.value = static_cast<uint8_t>(number);
      return true;
    }
    return fields(object, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "token",
                           "command_seq", "op"});
  }
  return message.kind == ControlKind::CONTROL_KIND_RESULT &&
         fields(object, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "token",
                         "command_seq", "state_seq", "status", "reason"}) &&
         sequence(object["state_seq"], message.state.state_seq) &&
         enum_value(object["status"], RESULTS, message.result) &&
         enum_value(object["reason"], REASONS, message.reason) && control_valid_result(message.result, message.reason);
}

}  // namespace

bool control_valid_hex(const ControlText<32> &text) {
  if (text.view().size() != 32)
    return false;
  for (const auto c : text.view())
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
      return false;
  return true;
}

bool control_valid_context(const ControlContext &context) {
  const auto card = context.card_id.view();
  if (context.slot < 2 || context.slot > 31 || context.revision > CONTROL_MAX_REVISION || card.empty() ||
      card.size() > 64)
    return false;
  for (size_t i = 0; i < card.size(); i++) {
    const char c = card[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (i > 0 && (c == '_' || c == '-'))))
      return false;
  }
  return true;
}

bool control_valid_snapshot(const ControlSnapshot &snapshot) {
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  if (!snapshot.has_no_family())
    return false;
#endif
  return control_valid_light_fields(snapshot);
}

bool control_valid_light_fields(const ControlSnapshot &snapshot) {
  return snapshot.state_seq >= 1 && snapshot.state_seq <= CONTROL_MAX_SEQUENCE && snapshot.ttl_ms == CONTROL_LEASE_MS &&
         snapshot.title.view().size() <= 160 && plain_utf8(snapshot.title.view()) &&
         (!snapshot.brightness_known || snapshot.brightness_pct <= 100) &&
         (snapshot.caps.brightness || !snapshot.brightness_known) &&
         (!snapshot.caps.brightness || (snapshot.caps.turn_on && snapshot.caps.turn_off));
}

bool control_valid_result(ControlResult result, ControlReason reason) {
  if (static_cast<uint8_t>(reason) > 12)
    return false;
  switch (result) {
    case ControlResult::CONTROL_RESULT_PENDING:
    case ControlResult::CONTROL_RESULT_HA_ACCEPTED:
    case ControlResult::CONTROL_RESULT_STATE_MATCHES:
    case ControlResult::CONTROL_RESULT_ALREADY_MATCHED:
      return reason == ControlReason::CONTROL_REASON_NONE;
    case ControlResult::CONTROL_RESULT_NOT_CONFIRMED:
      return reason == ControlReason::CONTROL_REASON_SERVICE_TIMEOUT ||
             reason == ControlReason::CONTROL_REASON_STATE_TIMEOUT;
    case ControlResult::CONTROL_RESULT_REJECTED:
      return reason != ControlReason::CONTROL_REASON_NONE && reason != ControlReason::CONTROL_REASON_SERVICE_TIMEOUT &&
             reason != ControlReason::CONTROL_REASON_STATE_TIMEOUT;
    default:
      return false;
  }
}

bool control_parse_json_allocated(std::string_view payload, ControlMessage &output, ArduinoJson::Allocator *allocator) {
  if (payload.empty() || payload.size() > CONTROL_MAX_PAYLOAD_BYTES || payload.find('\0') != std::string_view::npos)
    return false;
  ArduinoJson::JsonDocument document(allocator);
  if (ArduinoJson::deserializeJson(document, payload.data(), payload.size(),
                                   ArduinoJson::DeserializationOption::NestingLimit(3)))
    return false;
  const auto object = document.as<JsonObjectConst>();
  ControlMessage temporary;
  if (!decode(object, temporary))
    return false;
  // This protocol has only one optional nested object (caps). Counting key
  // separators outside strings detects duplicate keys discarded by the DOM.
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
  const size_t expected = object.size() + object["caps"].as<JsonObjectConst>().size();
  if (!closed || quoted || separators != expected)
    return false;
  output = temporary;
  return true;
}

bool control_serialize_json_allocated(const ControlMessage &message, char *output, size_t capacity, size_t &length,
                                      ArduinoJson::Allocator *allocator) {
  if (output == nullptr || capacity == 0 || static_cast<size_t>(message.kind) >= std::size(KINDS) ||
      static_cast<size_t>(message.operation) >= std::size(OPERATIONS) ||
      static_cast<size_t>(message.result) >= std::size(RESULTS) ||
      static_cast<size_t>(message.reason) >= std::size(REASONS))
    return false;
  if (message.kind != ControlKind::CONTROL_KIND_CAPABILITY && !control_valid_hex(message.identity.channel))
    return false;
  if (message.kind != ControlKind::CONTROL_KIND_CAPABILITY && message.kind != ControlKind::CONTROL_KIND_BIND) {
    if (!control_valid_context(message.identity.context) || message.identity.open_seq == 0 ||
        message.identity.open_seq > CONTROL_MAX_SEQUENCE)
      return false;
    if (message.kind != ControlKind::CONTROL_KIND_OPEN && message.kind != ControlKind::CONTROL_KIND_CANCEL_OPEN &&
        !control_valid_hex(message.identity.token))
      return false;
  }
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  if ((message.kind == ControlKind::CONTROL_KIND_GRANT || message.kind == ControlKind::CONTROL_KIND_STATE) &&
      !message.state.has_no_family())
    return false;
#endif
  if ((message.kind == ControlKind::CONTROL_KIND_GRANT || message.kind == ControlKind::CONTROL_KIND_STATE) &&
      !control_valid_snapshot(message.state))
    return false;
  ArduinoJson::JsonDocument document(allocator);
  auto object = document.to<ArduinoJson::JsonObject>();
  object["v"] = 1;
  object["type"] = "light_brightness_v1";
  object["kind"] = KINDS[static_cast<size_t>(message.kind)];
  if (message.kind == ControlKind::CONTROL_KIND_CAPABILITY) {
    object["enabled"] = message.enabled;
    object["max_payload_bytes"] = CONTROL_MAX_PAYLOAD_BYTES;
  } else {
    object["channel"] = message.identity.channel.bytes.data();
    if (message.kind != ControlKind::CONTROL_KIND_BIND) {
      object["open_seq"] = message.identity.open_seq;
      object["slot"] = message.identity.context.slot;
      object["card_id"] = message.identity.context.card_id.bytes.data();
      object["revision"] = message.identity.context.revision;
      if (message.kind != ControlKind::CONTROL_KIND_OPEN && message.kind != ControlKind::CONTROL_KIND_CANCEL_OPEN)
        object["token"] = message.identity.token.bytes.data();
      if (message.kind == ControlKind::CONTROL_KIND_CANCEL_OPEN || message.kind == ControlKind::CONTROL_KIND_CLOSE)
        object["reason"] = REASONS[static_cast<size_t>(message.reason)];
      if (message.kind == ControlKind::CONTROL_KIND_GRANT || message.kind == ControlKind::CONTROL_KIND_STATE) {
        object["state_seq"] = message.state.state_seq;
        object["ttl_ms"] = message.state.ttl_ms;
        object["title"] = message.state.title.bytes.data();
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
      if (message.kind == ControlKind::CONTROL_KIND_COMMAND || message.kind == ControlKind::CONTROL_KIND_RESULT)
        object["command_seq"] = message.command_seq;
      if (message.kind == ControlKind::CONTROL_KIND_COMMAND) {
        object["op"] = OPERATIONS[static_cast<size_t>(message.operation)];
        if (message.operation == ControlOperation::CONTROL_OPERATION_BRIGHTNESS)
          object["value"] = message.value;
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
  if (document.overflowed() || !decode(object, validated) || required > CONTROL_MAX_PAYLOAD_BYTES ||
      required >= capacity)
    return false;
  length = ArduinoJson::serializeJson(document, output, capacity);
  return length == required;
}

bool control_parse_json(std::string_view payload, ControlMessage &output) {
  ArduinoJson::JsonDocument defaults;
  return control_parse_json_allocated(payload, output, defaults.allocator());
}

bool control_serialize_json(const ControlMessage &message, char *output, size_t capacity, size_t &length) {
  ArduinoJson::JsonDocument defaults;
  return control_serialize_json_allocated(message, output, capacity, length, defaults.allocator());
}

}  // namespace esphome::lvgl_material
