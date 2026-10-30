#include "material_entity_control_codec.h"

#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER

#include <ArduinoJson.h>
#include <cstring>
#include <initializer_list>

namespace esphome::lvgl_material {
namespace {

using ArduinoJson::JsonObjectConst;
using ArduinoJson::JsonVariantConst;
static constexpr const char *KINDS[] = {"capability", "bind",    "open",   "cancel_open", "grant",
                                        "state",      "command", "result", "close"};
static constexpr const char *OPERATIONS[] = {"brightness", "turn_on", "turn_off", "step"};
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
      object["type"].as<ArduinoJson::JsonString>() != "entity_control_v1" ||
      !enum_value(object["kind"], KINDS, message.kind))
    return false;
  if (message.kind == ControlKind::CONTROL_KIND_CAPABILITY) {
    if (!fields(object, {"v", "type", "kind", "enabled", "max_payload_bytes", "control_kinds"}) ||
        !object["enabled"].is<bool>() || !uint_value(object["max_payload_bytes"], CONTROL_MAX_PAYLOAD_BYTES, number) ||
        number != CONTROL_MAX_PAYLOAD_BYTES)
      return false;
    const auto kinds = object["control_kinds"].as<ArduinoJson::JsonArrayConst>();
    if (kinds.size() != 2 || kinds[0].as<ArduinoJson::JsonString>() != "light" ||
        kinds[1].as<ArduinoJson::JsonString>() != "number")
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
  else if (control == "number")
    message.identity.context.control_kind = ControlEntityKind::CONTROL_ENTITY_NUMBER;
  else
    return false;
  if (!control_valid_context(message.identity.context))
    return false;
  if (message.kind == ControlKind::CONTROL_KIND_OPEN)
    return fields(object, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind"});
  if (message.kind == ControlKind::CONTROL_KIND_CANCEL_OPEN)
    return fields(object, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind",
                           "reason"}) &&
           enum_value(object["reason"], REASONS, message.reason) && close_reason(message.reason);
  if (!string_value(object["token"], message.identity.token) || !control_valid_hex(message.identity.token))
    return false;
  if (message.kind == ControlKind::CONTROL_KIND_CLOSE)
    return fields(object, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind",
                           "token", "reason"}) &&
           enum_value(object["reason"], REASONS, message.reason) && close_reason(message.reason);
  if ((message.kind == ControlKind::CONTROL_KIND_GRANT || message.kind == ControlKind::CONTROL_KIND_STATE) &&
      message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_NUMBER) {
    if (!fields(object,
                {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind", "token",
                 "state_seq", "ttl_ms", "title", "domain", "index", "available", "value_text", "state_generation"}) ||
        !sequence(object["state_seq"], message.state.state_seq) ||
        !uint_value(object["ttl_ms"], CONTROL_LEASE_MS, number) || number != CONTROL_LEASE_MS ||
        !string_value(object["title"], message.state.title) || !object["available"].is<bool>() ||
        !sequence(object["state_generation"], message.state.number.state_generation) ||
        !string_value(object["value_text"], message.state.number.value_text))
      return false;
    auto &state = message.state.number;
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
    if (message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_NUMBER) {
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
        message.operation == ControlOperation::CONTROL_OPERATION_STEP)
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
         control_valid_generic_result(message.result, message.reason);
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
                                         ArduinoJson::Allocator *allocator) {
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
  const size_t expected =
      object.size() + object["caps"].as<JsonObjectConst>().size() + object["domain"].as<JsonObjectConst>().size();
  if (!closed || quoted || separators != expected)
    return false;
  output = temporary;
  return true;
}

bool control_serialize_entity_json_allocated(const ControlMessage &message, char *output, size_t capacity,
                                             size_t &length, ArduinoJson::Allocator *allocator) {
  if (!message.generic || output == nullptr || capacity == 0 || static_cast<size_t>(message.kind) >= std::size(KINDS) ||
      static_cast<size_t>(message.operation) >= std::size(OPERATIONS) ||
      static_cast<size_t>(message.result) >= std::size(RESULTS) ||
      static_cast<size_t>(message.reason) >= std::size(REASONS))
    return false;
  if (message.kind != ControlKind::CONTROL_KIND_CAPABILITY && !control_valid_hex(message.identity.channel))
    return false;
  if (message.kind != ControlKind::CONTROL_KIND_CAPABILITY && message.kind != ControlKind::CONTROL_KIND_BIND) {
    if (static_cast<uint8_t>(message.identity.context.control_kind) > 1 ||
        !control_valid_context(message.identity.context) || message.identity.open_seq == 0 ||
        message.identity.open_seq > CONTROL_MAX_SEQUENCE)
      return false;
    if (message.kind != ControlKind::CONTROL_KIND_OPEN && message.kind != ControlKind::CONTROL_KIND_CANCEL_OPEN &&
        !control_valid_hex(message.identity.token))
      return false;
  }
  if ((message.kind == ControlKind::CONTROL_KIND_GRANT || message.kind == ControlKind::CONTROL_KIND_STATE) &&
      !(message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_NUMBER
            ? control_valid_number_snapshot(message.state)
            : control_valid_snapshot(message.state)))
    return false;
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
  } else {
    object["channel"] = message.identity.channel.bytes.data();
    if (message.kind != ControlKind::CONTROL_KIND_BIND) {
      object["open_seq"] = message.identity.open_seq;
      object["slot"] = message.identity.context.slot;
      object["card_id"] = message.identity.context.card_id.bytes.data();
      object["revision"] = message.identity.context.revision;
      object["control_kind"] =
          message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_NUMBER ? "number" : "light";
      if (message.kind != ControlKind::CONTROL_KIND_OPEN && message.kind != ControlKind::CONTROL_KIND_CANCEL_OPEN)
        object["token"] = message.identity.token.bytes.data();
      if (message.kind == ControlKind::CONTROL_KIND_CANCEL_OPEN || message.kind == ControlKind::CONTROL_KIND_CLOSE)
        object["reason"] = REASONS[static_cast<size_t>(message.reason)];
      if (message.kind == ControlKind::CONTROL_KIND_GRANT || message.kind == ControlKind::CONTROL_KIND_STATE) {
        object["state_seq"] = message.state.state_seq;
        object["ttl_ms"] = message.state.ttl_ms;
        object["title"] = message.state.title.bytes.data();
        if (message.identity.context.control_kind == ControlEntityKind::CONTROL_ENTITY_NUMBER) {
          const auto &number = message.state.number;
          object["state_generation"] = number.state_generation;
          if (number.available)
            object["index"] = number.index;
          else
            object["index"] = nullptr;
          object["available"] = number.available;
          object["value_text"] = number.value_text.bytes.data();
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
        if (message.operation == ControlOperation::CONTROL_OPERATION_BRIGHTNESS)
          object["value"] = message.value;
        if (message.operation == ControlOperation::CONTROL_OPERATION_STEP) {
          object["direction"] = message.direction;
          object["expected_index"] = message.expected_index;
          object["expected_state_generation"] = message.expected_state_generation;
        }
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

}  // namespace esphome::lvgl_material

#endif
