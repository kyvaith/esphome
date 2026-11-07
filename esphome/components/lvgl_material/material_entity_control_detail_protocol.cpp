#include "material_entity_control_codec.h"
#ifdef USE_LVGL_MATERIAL_DETAIL_CONTROLS
#include <initializer_list>
#include <cstring>
namespace esphome::lvgl_material {
namespace {
using ArduinoJson::JsonObjectConst;
using ArduinoJson::JsonVariantConst;
using Facet = ControlFacet;
constexpr const char *FACETS[] = {
    "main",       "color_temp_kelvin",     "effect", "humidity", "hvac_mode", "preset_mode", "fan_mode",
    "swing_mode", "swing_horizontal_mode", "color",  "range"};
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
constexpr const char *COLOR_WRITE_MODES[] = {"rgb", "hs", "xy"};
constexpr const char *COLOR_SOURCE_MODES[] = {"rgb", "hs", "xy", "color_temp", "other", "unknown"};
constexpr const char *COLOR_REASONS[] = {
    "none", "off", "unsupported_mode", "invalid_color", "invalid_brightness", "source_unavailable"};
#endif
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
constexpr const char *RANGE_REASONS[] = {"none", "inactive_mode", "invalid_range", "source_unavailable"};
#endif
constexpr const char *MODES[] = {"unknown", "off", "heat", "cool", "heat_cool", "auto", "dry", "fan_only"};
constexpr const char *ACTIVITIES[] = {"unknown", "off", "idle",       "heating",   "cooling",
                                      "drying",  "fan", "preheating", "defrosting"};
constexpr const char *REASONS[] = {"none",
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
bool fields(JsonObjectConst object, std::initializer_list<const char *> names, size_t extra = 0) {
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
bool integer(JsonVariantConst value, uint32_t high, uint32_t &out) {
  if (value.is<bool>() || !value.is<uint32_t>())
    return false;
  out = value.as<uint32_t>();
  return out <= high;
}
bool sequence(JsonVariantConst value, uint32_t &out) { return integer(value, CONTROL_MAX_SEQUENCE, out) && out != 0; }
template<size_t N> bool text(JsonVariantConst value, ControlText<N> &out) {
  if (!value.is<const char *>())
    return false;
  auto s = value.as<ArduinoJson::JsonString>();
  return number_plain_utf8({s.c_str(), s.size()}) && out.assign({s.c_str(), s.size()});
}
template<size_t N, typename T> bool enumeration(JsonVariantConst value, const char *const (&names)[N], T &out) {
  if (!value.is<const char *>())
    return false;
  auto s = value.as<ArduinoJson::JsonString>();
  for (size_t i = 0; i < N; i++)
    if (std::string_view(s.c_str(), s.size()) == names[i]) {
      out = static_cast<T>(i);
      return true;
    }
  return false;
}
bool domain(JsonVariantConst value, MaterialNumberDomain &out) {
  auto obj = value.as<JsonObjectConst>();
  ControlText<20> lo, hi, step;
  ControlText<8> unit;
  uint32_t max;
  if (!fields(obj, {"schema", "min", "max", "step", "unit", "max_index"}) ||
      obj["schema"].as<ArduinoJson::JsonString>() != "numeric_steps_v1" || !text(obj["min"], lo) ||
      !text(obj["max"], hi) || !text(obj["step"], step) || !text(obj["unit"], unit) ||
      unit.view().size() >= out.unit.size() || !integer(obj["max_index"], NUMBER_MAX_INDEX, max) ||
      !number_parse_decimal(lo.view(), out.minimum) || !number_parse_decimal(hi.view(), out.maximum) ||
      !number_parse_decimal(step.view(), out.step))
    return false;
  out.max_index = max;
  std::memcpy(out.unit.data(), unit.bytes.data(), unit.view().size() + 1);
  char canonical[24];
  for (auto pair :
       {std::pair{out.minimum, lo.view()}, std::pair{out.maximum, hi.view()}, std::pair{out.step, step.view()}})
    if (!number_format_decimal(pair.first, canonical, sizeof(canonical)) || pair.second != canonical)
      return false;
  return out.valid();
}
bool numeric(JsonObjectConst obj, ControlFacetNumber &n) {
  if (!domain(obj["domain"], n.domain) || !text(obj["value_text"], n.value) || !text(obj["current_text"], n.current))
    return false;
  n.known = !obj["index"].isNull();
  return !n.known || integer(obj["index"], n.domain.max_index, n.index);
}
void encode_domain(ArduinoJson::JsonObject o, const MaterialNumberDomain &d) {
  char lo[24], hi[24], step[24];
  number_format_decimal(d.minimum, lo, sizeof(lo));
  number_format_decimal(d.maximum, hi, sizeof(hi));
  number_format_decimal(d.step, step, sizeof(step));
  o["schema"] = "numeric_steps_v1";
  o["min"] = lo;
  o["max"] = hi;
  o["step"] = step;
  o["unit"] = d.unit.data();
  o["max_index"] = d.max_index;
}
bool facet_family(ControlEntityKind kind, Facet facet) {
  return kind == ControlEntityKind::CONTROL_ENTITY_LIGHT
             ? (facet <= Facet::CONTROL_FACET_EFFECT
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
                || facet == Facet::CONTROL_FACET_COLOR
#endif
                )
             : kind == ControlEntityKind::CONTROL_ENTITY_PANEL &&
                   (facet == Facet::CONTROL_FACET_MAIN ||
                    (facet >= Facet::CONTROL_FACET_HUMIDITY && facet <= Facet::CONTROL_FACET_SWING_HORIZONTAL_MODE)
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
                    || facet == Facet::CONTROL_FACET_RANGE
#endif
                   );
}
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
bool decode_color(JsonObjectConst object, ControlDetailSnapshot &detail) {
  uint32_t version, channel;
  if (!fields(object,
              {"version", "write_mode", "source_mode", "rgb", "hue_milli", "saturation_milli", "editable", "reason"}) ||
      !integer(object["version"], 1, version) || version != 1 || !object["editable"].is<bool>())
    return false;
  auto &color = detail.data.emplace<ControlFacetColor>();
  detail.editable = object["editable"];
  if (!enumeration(object["write_mode"], COLOR_WRITE_MODES, color.write_mode) ||
      !enumeration(object["source_mode"], COLOR_SOURCE_MODES, color.source_mode) ||
      !enumeration(object["reason"], COLOR_REASONS, color.reason))
    return false;
  color.known = !object["rgb"].isNull();
  if (color.known) {
    if (!object["rgb"].is<ArduinoJson::JsonArrayConst>() || object["rgb"].size() != 3 ||
        !integer(object["hue_milli"], 359999, color.hue_milli) ||
        !integer(object["saturation_milli"], 100000, color.saturation_milli))
      return false;
    for (size_t i = 0; i < 3; i++) {
      if (!integer(object["rgb"][i], 255, channel))
        return false;
      color.rgb[i] = channel;
    }
  } else if (!object["hue_milli"].isNull() || !object["saturation_milli"].isNull())
    return false;
  return control_valid_facet_color(color, detail.editable);
}
void encode_color(ArduinoJson::JsonObject object, const ControlDetailSnapshot &detail) {
  if (detail.color() == nullptr || !control_valid_facet_color(*detail.color(), detail.editable))
    return;
  const auto &color = *detail.color();
  object["version"] = 1;
  object["write_mode"] = COLOR_WRITE_MODES[color.write_mode];
  object["source_mode"] = COLOR_SOURCE_MODES[color.source_mode];
  object["editable"] = detail.editable;
  object["reason"] = COLOR_REASONS[color.reason];
  if (color.known) {
    auto rgb = object["rgb"].to<ArduinoJson::JsonArray>();
    for (auto value : color.rgb)
      rgb.add(value);
    object["hue_milli"] = color.hue_milli;
    object["saturation_milli"] = color.saturation_milli;
  } else {
    object["rgb"] = nullptr;
    object["hue_milli"] = nullptr;
    object["saturation_milli"] = nullptr;
  }
}
#endif
}  // namespace
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
bool control_parse_detail_range_bind_allocated(std::string_view payload, ControlText<32> &channel,
                                               ArduinoJson::Allocator *allocator) {
  if (payload.empty() || payload.size() > 256)
    return false;
  ArduinoJson::JsonDocument document(allocator);
  if (deserializeJson(document, payload.data(), payload.size(), ArduinoJson::DeserializationOption::NestingLimit(1)) ||
      document.overflowed())
    return false;
  auto object = document.as<JsonObjectConst>();
  ControlText<32> parsed;
  uint32_t version;
  if (!fields(object, {"v", "type", "channel", "range_version"}) || !integer(object["v"], 1, version) || version != 1 ||
      object["type"].as<ArduinoJson::JsonString>() != "entity_detail_range_v1" ||
      !integer(object["range_version"], 1, version) || version != 1 || !text(object["channel"], parsed) ||
      !control_valid_hex(parsed))
    return false;
  // ArduinoJson collapses duplicate keys. Reject them before admitting a bind.
  bool quoted = false, escaped = false;
  size_t separators = 0;
  for (char ch : payload) {
    if (quoted) {
      if (escaped)
        escaped = false;
      else if (ch == '\\')
        escaped = true;
      else if (ch == '"')
        quoted = false;
    } else if (ch == '"')
      quoted = true;
    else if (ch == ':')
      separators++;
  }
  if (quoted || separators != 4)
    return false;
  channel = parsed;
  return true;
}
#endif
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
bool control_parse_detail_color_bind_allocated(std::string_view payload, ControlText<32> &channel,
                                               ArduinoJson::Allocator *allocator) {
  if (payload.empty() || payload.size() > 256)
    return false;
  ArduinoJson::JsonDocument document(allocator);
  if (deserializeJson(document, payload.data(), payload.size(), ArduinoJson::DeserializationOption::NestingLimit(1)) ||
      document.overflowed())
    return false;
  auto object = document.as<JsonObjectConst>();
  ControlText<32> parsed;
  uint32_t version;
  if (!fields(object, {"v", "type", "channel", "color_version"}) || !integer(object["v"], 1, version) || version != 1 ||
      object["type"].as<ArduinoJson::JsonString>() != "entity_detail_color_v1" ||
      !integer(object["color_version"], 1, version) || version != 1 || !text(object["channel"], parsed) ||
      !control_valid_hex(parsed))
    return false;
  // ArduinoJson collapses duplicate keys. Reject them before admitting a bind.
  bool quoted = false, escaped = false;
  size_t separators = 0;
  for (char ch : payload) {
    if (quoted) {
      if (escaped)
        escaped = false;
      else if (ch == '\\')
        escaped = true;
      else if (ch == '"')
        quoted = false;
    } else if (ch == '"')
      quoted = true;
    else if (ch == ':')
      separators++;
  }
  if (quoted || separators != 4)
    return false;
  channel = parsed;
  return true;
}
#endif
bool control_parse_detail_bind_allocated(std::string_view payload, ControlText<32> &channel, uint8_t &profiles,
                                         ArduinoJson::Allocator *allocator) {
  if (payload.empty() || payload.size() > 512)
    return false;
  ArduinoJson::JsonDocument doc(allocator);
  if (deserializeJson(doc, payload.data(), payload.size(), ArduinoJson::DeserializationOption::NestingLimit(1)) ||
      doc.overflowed())
    return false;
  auto o = doc.as<JsonObjectConst>();
  uint32_t v, l, c;
  ControlText<32> parsed;
  if (!fields(o, {"v", "type", "detail_version", "light_detail_version", "climate_panel_version", "channel"}) ||
      !integer(o["v"], 1, v) || v != 1 || o["type"].as<ArduinoJson::JsonString>() != "entity_detail_features_v1" ||
      !integer(o["detail_version"], 1, v) || v != 1 || !integer(o["light_detail_version"], 1, l) ||
      !integer(o["climate_panel_version"], 1, c) || (l | c) == 0 || !text(o["channel"], parsed) ||
      !control_valid_hex(parsed))
    return false;
  bool quoted = false, escaped = false;
  size_t separators = 0;
  for (char ch : payload) {
    if (quoted) {
      if (escaped)
        escaped = false;
      else if (ch == '\\')
        escaped = true;
      else if (ch == '"')
        quoted = false;
    } else if (ch == '"')
      quoted = true;
    else if (ch == ':')
      separators++;
  }
  if (quoted || separators != 6)
    return false;
  channel = parsed;
  profiles = l | (c << 1);
  return true;
}
bool control_decode_detail(JsonObjectConst o, ControlMessage &m) {
  uint32_t value;
  const auto kind = m.identity.context.control_kind;
  if (kind != ControlEntityKind::CONTROL_ENTITY_LIGHT && kind != ControlEntityKind::CONTROL_ENTITY_PANEL)
    return false;
  if (m.kind == ControlKind::CONTROL_KIND_FACET || m.kind == ControlKind::CONTROL_KIND_FACET_RESULT) {
    const bool receipt = m.kind == ControlKind::CONTROL_KIND_FACET_RESULT;
    if (!fields(o,
                {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind", "token",
                 "facet", "view_seq"},
                receipt ? 3 : 0) ||
        !sequence(o["view_seq"], m.view_seq) || !enumeration(o["facet"], FACETS, m.facet) ||
        !facet_family(kind, m.facet))
      return false;
    if (!receipt)
      return true;
    if (!sequence(o["state_seq"], m.state.state_seq) || !enumeration(o["reason"], REASONS, m.reason))
      return false;
    auto status = o["status"].as<ArduinoJson::JsonString>();
    m.facet_ready = status == "ready";
    return (m.facet_ready && m.reason == ControlReason::CONTROL_REASON_NONE) ||
           (status == "rejected" && m.reason != ControlReason::CONTROL_REASON_NONE);
  }
  if (m.kind == ControlKind::CONTROL_KIND_COMMAND) {
    auto op = o["op"].as<ArduinoJson::JsonString>();
    const bool number = op == "facet_number", choice = op == "facet_choice";
    if (!sequence(o["command_seq"], m.command_seq) || !integer(o["view_seq"], CONTROL_MAX_SEQUENCE, m.view_seq) ||
        !sequence(o["expected_state_generation"], m.expected_state_generation))
      return false;
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
    if (op == "facet_color") {
      if (kind != ControlEntityKind::CONTROL_ENTITY_LIGHT || m.view_seq == 0 ||
          !fields(o, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind",
                      "token", "command_seq", "op", "facet", "view_seq", "expected_state_generation",
                      "target_hue_milli", "target_saturation_milli"}) ||
          o["facet"].as<ArduinoJson::JsonString>() != "color")
        return false;
      auto &color = m.state.emplace_family<ControlDetailSnapshot>().data.emplace<ControlFacetColor>();
      if (!integer(o["target_hue_milli"], 359999, color.hue_milli) ||
          !integer(o["target_saturation_milli"], 100000, color.saturation_milli))
        return false;
      m.facet = Facet::CONTROL_FACET_COLOR;
      m.operation = ControlOperation::CONTROL_OPERATION_FACET_COLOR;
      m.detail_open = true;
      return true;
    }
#endif
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
    if (op == "facet_range") {
      if (kind != ControlEntityKind::CONTROL_ENTITY_PANEL || m.view_seq == 0 ||
          !fields(o, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind",
                      "token", "command_seq", "op", "facet", "view_seq", "expected_state_generation",
                      "target_low_index", "target_high_index"}) ||
          o["facet"].as<ArduinoJson::JsonString>() != "range")
        return false;
      auto &range = m.state.emplace_family<ControlDetailSnapshot>().data.emplace<ControlFacetRange>();
      if (!integer(o["target_low_index"], NUMBER_MAX_INDEX, range.low_index) ||
          !integer(o["target_high_index"], NUMBER_MAX_INDEX, range.high_index) || range.low_index > range.high_index)
        return false;
      m.facet = Facet::CONTROL_FACET_RANGE;
      m.operation = ControlOperation::CONTROL_OPERATION_FACET_RANGE;
      m.detail_open = true;
      return true;
    }
#endif
    if (number || choice) {
      if (!fields(o, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind",
                      "token", "command_seq", "op", "facet", "view_seq", "target_index", "expected_index",
                      "expected_state_generation"}) ||
          !enumeration(o["facet"], FACETS, m.facet) || !facet_family(kind, m.facet) ||
          !integer(o["target_index"], number ? NUMBER_MAX_INDEX : 7, m.facet_target_index))
        return false;
      const bool numeric_facet =
          m.facet == Facet::CONTROL_FACET_KELVIN || m.facet == Facet::CONTROL_FACET_HUMIDITY ||
          (kind == ControlEntityKind::CONTROL_ENTITY_PANEL && m.facet == Facet::CONTROL_FACET_MAIN);
      if (number != numeric_facet || (!number && m.facet == Facet::CONTROL_FACET_MAIN) ||
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
          m.facet == Facet::CONTROL_FACET_COLOR ||
#endif
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
          m.facet == Facet::CONTROL_FACET_RANGE ||
#endif
          (m.facet != Facet::CONTROL_FACET_MAIN && m.view_seq == 0))
        return false;
      m.expected_index_known = !o["expected_index"].isNull();
      if (m.expected_index_known && !integer(o["expected_index"], number ? NUMBER_MAX_INDEX : 7, m.expected_index))
        return false;
      if (!m.expected_index_known &&
          (number || (m.facet != Facet::CONTROL_FACET_EFFECT && m.facet != Facet::CONTROL_FACET_PRESET_MODE)))
        return false;
      m.operation =
          number ? ControlOperation::CONTROL_OPERATION_FACET_NUMBER : ControlOperation::CONTROL_OPERATION_FACET_CHOICE;
      return true;
    }
    if (kind != ControlEntityKind::CONTROL_ENTITY_LIGHT ||
        !fields(o,
                {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind", "token",
                 "command_seq", "op", "view_seq", "expected_state_generation"},
                op == "brightness" ? 1 : 0))
      return false;
    m.detail_open = true;
    if (op == "brightness") {
      if (!integer(o["value"], 100, value))
        return false;
      m.value = value;
      m.operation = ControlOperation::CONTROL_OPERATION_BRIGHTNESS;
      return true;
    }
    if (op != "turn_on" && op != "turn_off")
      return false;
    m.operation =
        op == "turn_on" ? ControlOperation::CONTROL_OPERATION_TURN_ON : ControlOperation::CONTROL_OPERATION_TURN_OFF;
    return true;
  }
  if (m.kind != ControlKind::CONTROL_KIND_GRANT && m.kind != ControlKind::CONTROL_KIND_STATE)
    return false;
  auto &d = m.state.emplace_family<ControlDetailSnapshot>();
  d.present = true;
  d.climate = kind == ControlEntityKind::CONTROL_ENTITY_PANEL;
  if (!sequence(o["state_seq"], m.state.state_seq) || !integer(o["ttl_ms"], CONTROL_LEASE_MS, value) ||
      value != CONTROL_LEASE_MS || !text(o["title"], m.state.title))
    return false;
  JsonObjectConst state;
  if (d.climate) {
    if (!fields(o, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind", "token",
                    "state_seq", "ttl_ms", "title", "panel_type", "available", "panel_caps", "panel_state",
                    "eligible_actions", "state_generation"}) ||
        o["panel_type"].as<ArduinoJson::JsonString>() != "climate" || !o["available"].is<bool>() ||
        !fields(o["panel_caps"].as<JsonObjectConst>(), {"facets"}) ||
        !integer(o["panel_caps"]["facets"],
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
                 255,
#else
                 127,
#endif
                 value) ||
        !sequence(o["state_generation"], d.state_generation))
      return false;
    d.available = o["available"];
    d.facets = value;
    state = o["panel_state"].as<JsonObjectConst>();
  } else {
    if (!fields(o,
                {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind", "token",
                 "state_seq", "ttl_ms", "title", "on", "brightness_pct", "caps", "light_detail"}
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
                ,
                o["light_detail"]["facet"].as<ArduinoJson::JsonString>() == "color" ? 1 : 0
#endif
                ) ||
        !o["on"].is<bool>())
      return false;
    m.state.on = o["on"];
    m.state.brightness_known = !o["brightness_pct"].isNull();
    if (m.state.brightness_known) {
      if (!integer(o["brightness_pct"], 100, value))
        return false;
      m.state.brightness_pct = value;
    }
    auto caps = o["caps"].as<JsonObjectConst>();
    if (!fields(caps, {"brightness", "turn_on", "turn_off"}) || !caps["brightness"].is<bool>() ||
        !caps["turn_on"].is<bool>() || !caps["turn_off"].is<bool>())
      return false;
    m.state.caps = {caps["brightness"], caps["turn_on"], caps["turn_off"]};
    state = o["light_detail"].as<JsonObjectConst>();
    d.available = true;
    if (!integer(state["version"], 1, value) || value != 1 ||
        !integer(state["facets"],
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
                 7,
#else
                 3,
#endif
                 value) ||
        !sequence(state["state_generation"], d.state_generation))
      return false;
    d.facets = value;
  }
  if (!enumeration(state["facet"], FACETS, d.facet) || !facet_family(kind, d.facet) ||
      !integer(state["view_seq"], CONTROL_MAX_SEQUENCE, d.view_seq))
    return false;
  const size_t extra = d.climate ? 0 : 3;
  if (d.facet == Facet::CONTROL_FACET_MAIN) {
    if (d.climate) {
      if (!fields(state, {"facet", "view_seq", "unit", "mode", "activity", "target", "current", "low", "high", "min",
                          "max", "domain", "index", "editable"}) ||
          !state["editable"].is<bool>())
        return false;
      auto &main = d.data.emplace<ControlClimateMain>();
      d.editable = state["editable"];
      if (!state["unit"].isNull()) {
        auto unit = state["unit"].as<ArduinoJson::JsonString>();
        main.unit = unit == "°C" ? 1 : unit == "°F" ? 2 : 0;
        if (main.unit == 0)
          return false;
      }
      if (!enumeration(state["mode"], MODES, main.mode) || !enumeration(state["activity"], ACTIVITIES, main.activity) ||
          !text(state["target"], main.target) || !text(state["current"], main.current) ||
          !text(state["low"], main.low) || !text(state["high"], main.high) || !text(state["min"], main.minimum) ||
          !text(state["max"], main.maximum))
        return false;
      if (!state["domain"].isNull() && !domain(state["domain"], main.number.domain))
        return false;
      main.number.known = !state["index"].isNull();
      if (main.number.known && !integer(state["index"], main.number.domain.max_index, main.number.index))
        return false;
      main.number.value.assign(main.number.known ? main.target.view() : "--");
    } else if (!fields(state, {"facet", "view_seq"}, extra))
      return false;
  }
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
  else if (d.facet == Facet::CONTROL_FACET_COLOR) {
    if (d.climate || !fields(state, {"facet", "view_seq"}, extra) ||
        !decode_color(o["light_color"].as<JsonObjectConst>(), d))
      return false;
  }
#endif
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
  else if (d.facet == Facet::CONTROL_FACET_RANGE) {
    if (!d.climate ||
        !fields(state, {"facet", "view_seq", "domain", "low_index", "high_index", "mode", "activity", "current_text",
                        "editable", "reason"}) ||
        !state["editable"].is<bool>())
      return false;
    auto &range = d.data.emplace<ControlFacetRange>();
    d.editable = state["editable"];
    if (!domain(state["domain"], range.domain) || !enumeration(state["mode"], MODES, range.mode) ||
        !enumeration(state["activity"], ACTIVITIES, range.activity) ||
        !enumeration(state["reason"], RANGE_REASONS, range.reason) || !text(state["current_text"], range.current))
      return false;
    range.known = !state["low_index"].isNull();
    if (range.known ? (!integer(state["low_index"], range.domain.max_index, range.low_index) ||
                       !integer(state["high_index"], range.domain.max_index, range.high_index))
                    : !state["high_index"].isNull())
      return false;
  }
#endif
  else if (d.facet == Facet::CONTROL_FACET_KELVIN || d.facet == Facet::CONTROL_FACET_HUMIDITY) {
    if (!fields(state, {"facet", "view_seq", "domain", "index", "value_text", "current_text", "editable"}, extra) ||
        !state["editable"].is<bool>())
      return false;
    d.editable = state["editable"];
    if (!numeric(state, d.data.emplace<ControlFacetNumber>()))
      return false;
  } else {
    if (!fields(state, {"facet", "view_seq", "options", "index", "choice_known", "editable"}, extra) ||
        !state["choice_known"].is<bool>() || !state["editable"].is<bool>() ||
        !state["options"].is<ArduinoJson::JsonArrayConst>())
      return false;
    auto &choice = d.data.emplace<ControlFacetChoice>();
    auto options = state["options"].as<ArduinoJson::JsonArrayConst>();
    if (options.size() == 0 || options.size() > 8)
      return false;
    choice.count = options.size();
    choice.known = state["choice_known"];
    d.editable = state["editable"];
    for (size_t i = 0; i < options.size(); i++)
      if (!text(options[i], choice.options[i]))
        return false;
    if (!state["index"].isNull()) {
      if (!integer(state["index"], 7, value))
        return false;
      choice.index = value;
    }
  }
  if (d.climate) {
    const uint32_t expected = !d.editable ? 0 : d.choice() != nullptr ? 2 : 1;
    if (!integer(o["eligible_actions"], 3, value) || value != expected)
      return false;
  }
  return (m.kind != ControlKind::CONTROL_KIND_GRANT || (d.facet == Facet::CONTROL_FACET_MAIN && d.view_seq == 0)) &&
         control_valid_detail_snapshot(m.state);
}
void control_encode_detail(ArduinoJson::JsonObject o, const ControlMessage &m) {
  if (m.kind == ControlKind::CONTROL_KIND_FACET || m.kind == ControlKind::CONTROL_KIND_FACET_RESULT) {
    o["facet"] = FACETS[static_cast<size_t>(m.facet)];
    o["view_seq"] = m.view_seq;
    if (m.kind == ControlKind::CONTROL_KIND_FACET_RESULT) {
      o["state_seq"] = m.state.state_seq;
      o["status"] = m.facet_ready ? "ready" : "rejected";
      o["reason"] = REASONS[static_cast<size_t>(m.reason)];
    }
    return;
  }
  if (m.kind == ControlKind::CONTROL_KIND_COMMAND) {
    o["view_seq"] = m.view_seq;
    o["expected_state_generation"] = m.expected_state_generation;
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
    if (m.operation == ControlOperation::CONTROL_OPERATION_FACET_COLOR) {
      const auto *detail = m.state.family_if<ControlDetailSnapshot>();
      if (detail == nullptr || detail->color() == nullptr)
        return;
      const auto &target = *detail->color();
      o["facet"] = "color";
      o["target_hue_milli"] = target.hue_milli;
      o["target_saturation_milli"] = target.saturation_milli;
      return;
    }
#endif
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
    if (m.operation == ControlOperation::CONTROL_OPERATION_FACET_RANGE) {
      const auto *detail = m.state.family_if<ControlDetailSnapshot>();
      if (detail == nullptr || detail->range() == nullptr)
        return;
      o["facet"] = "range";
      o["target_low_index"] = detail->range()->low_index;
      o["target_high_index"] = detail->range()->high_index;
      return;
    }
#endif
    if (m.operation == ControlOperation::CONTROL_OPERATION_FACET_NUMBER ||
        m.operation == ControlOperation::CONTROL_OPERATION_FACET_CHOICE) {
      o["facet"] = FACETS[static_cast<size_t>(m.facet)];
      o["target_index"] = m.facet_target_index;
      if (m.expected_index_known)
        o["expected_index"] = m.expected_index;
      else
        o["expected_index"] = nullptr;
    }
    return;
  }
  const auto *detail = m.state.family_if<ControlDetailSnapshot>();
  if (detail == nullptr)
    return;
  const auto &d = *detail;
#ifdef USE_LVGL_MATERIAL_LIGHT_COLOR
  if (d.facet == Facet::CONTROL_FACET_COLOR)
    encode_color(o["light_color"].to<ArduinoJson::JsonObject>(), d);
#endif
  ArduinoJson::JsonObject state;
  if (d.climate) {
    o["panel_type"] = "climate";
    o["available"] = d.available;
    o["state_generation"] = d.state_generation;
    o["eligible_actions"] = !d.editable ? 0 : d.choice() != nullptr ? 2 : 1;
    o["panel_caps"].to<ArduinoJson::JsonObject>()["facets"] = d.facets;
    state = o["panel_state"].to<ArduinoJson::JsonObject>();
  } else {
    state = o["light_detail"].to<ArduinoJson::JsonObject>();
    state["version"] = 1;
    state["facets"] = d.facets;
    state["state_generation"] = d.state_generation;
    o["on"] = m.state.on;
    if (m.state.brightness_known)
      o["brightness_pct"] = m.state.brightness_pct;
    else
      o["brightness_pct"] = nullptr;
    auto caps = o["caps"].to<ArduinoJson::JsonObject>();
    caps["brightness"] = m.state.caps.brightness;
    caps["turn_on"] = m.state.caps.turn_on;
    caps["turn_off"] = m.state.caps.turn_off;
  }
  state["facet"] = FACETS[static_cast<size_t>(d.facet)];
  state["view_seq"] = d.view_seq;
  if (auto *main = std::get_if<ControlClimateMain>(&d.data)) {
    if (main->unit == 0)
      state["unit"] = nullptr;
    else
      state["unit"] = main->unit == 1 ? "°C" : "°F";
    state["mode"] = MODES[main->mode];
    state["activity"] = ACTIVITIES[main->activity];
    state["target"] = main->target.bytes.data();
    state["current"] = main->current.bytes.data();
    state["low"] = main->low.bytes.data();
    state["high"] = main->high.bytes.data();
    state["min"] = main->minimum.bytes.data();
    state["max"] = main->maximum.bytes.data();
    if (main->number.domain.valid())
      encode_domain(state["domain"].to<ArduinoJson::JsonObject>(), main->number.domain);
    else
      state["domain"] = nullptr;
    if (main->number.known)
      state["index"] = main->number.index;
    else
      state["index"] = nullptr;
    state["editable"] = d.editable;
  }
#ifdef USE_LVGL_MATERIAL_CLIMATE_RANGE
  else if (auto *range = d.range()) {
    encode_domain(state["domain"].to<ArduinoJson::JsonObject>(), range->domain);
    if (range->known) {
      state["low_index"] = range->low_index;
      state["high_index"] = range->high_index;
    } else {
      state["low_index"] = nullptr;
      state["high_index"] = nullptr;
    }
    state["mode"] = MODES[range->mode];
    state["activity"] = ACTIVITIES[range->activity];
    state["current_text"] = range->current.bytes.data();
    state["editable"] = d.editable;
    state["reason"] = RANGE_REASONS[range->reason];
  }
#endif
  else if (auto *n = std::get_if<ControlFacetNumber>(&d.data)) {
    encode_domain(state["domain"].to<ArduinoJson::JsonObject>(), n->domain);
    if (n->known)
      state["index"] = n->index;
    else
      state["index"] = nullptr;
    state["value_text"] = n->value.bytes.data();
    state["current_text"] = n->current.bytes.data();
    state["editable"] = d.editable;
  } else if (auto *c = d.choice()) {
    auto list = state["options"].to<ArduinoJson::JsonArray>();
    for (size_t i = 0; i < c->count; i++)
      list.add(c->options[i].bytes.data());
    if (c->index == 255)
      state["index"] = nullptr;
    else
      state["index"] = c->index;
    state["choice_known"] = c->known;
    state["editable"] = d.editable;
  }
}
}  // namespace esphome::lvgl_material
#endif
