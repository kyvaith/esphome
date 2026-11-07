#include "material_entity_control_codec.h"
#ifdef USE_LVGL_MATERIAL_MEDIA_CONTROLS
#include <initializer_list>

namespace esphome::lvgl_material {
namespace {
using Media = ControlMediaSnapshot;
using Facet = ControlMediaFacet;
using ArduinoJson::JsonObject;
using ArduinoJson::JsonObjectConst;
using ArduinoJson::JsonVariantConst;
constexpr const char *FACETS[] = {"main", "volume", "source", "sound_mode", "repeat"};
constexpr const char *SOURCES[] = {"known", "unknown", "unavailable"};
constexpr const char *DEVICES[] = {"generic", "speaker", "tv", "receiver"};
constexpr const char *PHASES[] = {"on", "off", "idle", "playing", "paused", "buffering", "standby", "unknown"};
constexpr const char *TEXT[] = {"value", "empty", "invalid", "excerpt"};
constexpr const char *OPERATIONS[] = {"play",   "pause",     "previous",    "next", "stop",    "turn_on", "turn_off",
                                      "volume", "volume_up", "volume_down", "mute", "shuffle", "choice"};
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

bool fields(JsonObjectConst object, std::initializer_list<const char *> keys, size_t extra = 0) {
  if (object.isNull() || object.size() != keys.size() + extra)
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
template<size_t N, typename T> bool enumeration(JsonVariantConst value, const char *const (&names)[N], T &out) {
  if (!value.is<const char *>())
    return false;
  const auto string = value.as<ArduinoJson::JsonString>();
  for (size_t index = 0; index < N; index++)
    if (std::string_view(string.c_str(), string.size()) == names[index]) {
      out = static_cast<T>(index);
      return true;
    }
  return false;
}
bool boolean(JsonVariantConst value, uint8_t &out) {
  if (value.isNull()) {
    out = Media::UNKNOWN_BOOL;
    return true;
  }
  if (!value.is<bool>())
    return false;
  out = value.as<bool>();
  return true;
}
void write_bool(JsonObject object, const char *key, uint8_t value) {
  if (value == Media::UNKNOWN_BOOL)
    object[key] = nullptr;
  else
    object[key] = value != 0;
}
bool volume(JsonVariantConst raw, JsonVariantConst percent, JsonVariantConst rail, Media::Volume &out) {
  out.known = !raw.isNull();
  if (!text(percent, out.percent))
    return false;
  if (out.known) {
    if (!text(raw, out.raw) || !integer(rail, 10000, out.rail))
      return false;
  } else if (!rail.isNull())
    return false;
  return control_media_volume_valid(out);
}
void write_volume(JsonObject object, const Media::Volume &value, const char *raw, const char *percent,
                  const char *rail) {
  object[percent] = value.percent.bytes.data();
  if (value.known) {
    object[raw] = value.raw.bytes.data();
    object[rail] = value.rail;
  } else {
    object[raw] = nullptr;
    object[rail] = nullptr;
  }
}
}  // namespace

bool control_decode_media(JsonObjectConst object, ControlMessage &message) {
  if (message.identity.context.control_kind != ControlEntityKind::CONTROL_ENTITY_PANEL)
    return false;
  auto &media = message.state.emplace_family<Media>();
  if (message.kind == ControlKind::CONTROL_KIND_GRANT || message.kind == ControlKind::CONTROL_KIND_STATE) {
    uint32_t ttl;
    if (!fields(object, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind",
                         "token", "state_seq", "ttl_ms", "title", "panel_type", "available", "panel_caps",
                         "panel_state", "eligible_actions", "state_generation"}) ||
        object["panel_type"].as<ArduinoJson::JsonString>() != "media" ||
        !integer(object["state_seq"], CONTROL_MAX_SEQUENCE, message.state.state_seq, true) ||
        !integer(object["ttl_ms"], CONTROL_LEASE_MS, ttl) || ttl != CONTROL_LEASE_MS ||
        !integer(object["state_generation"], CONTROL_MAX_SEQUENCE, media.state_generation, true) ||
        !integer(object["eligible_actions"], Media::ALL_OPERATIONS, media.eligible) ||
        !text(object["title"], message.state.title) || !object["available"].is<bool>())
      return false;
    const auto caps = object["panel_caps"].as<JsonObjectConst>();
    const auto state = object["panel_state"].as<JsonObjectConst>();
    if (!fields(caps, {"device_type", "operations", "facets"}) ||
        !enumeration(caps["device_type"], DEVICES, media.device_type) ||
        !integer(caps["operations"], Media::ALL_OPERATIONS, media.operations) ||
        !integer(caps["facets"], 15, media.facets) || !enumeration(state["facet"], FACETS, media.facet) ||
        !enumeration(state["source"], SOURCES, media.source) ||
        !integer(state["view_seq"], CONTROL_MAX_SEQUENCE, media.view_seq) ||
        object["available"].as<bool>() != media.available() ||
        (message.kind == ControlKind::CONTROL_KIND_GRANT && (media.facet != Facet::MAIN || media.view_seq != 0)))
      return false;
    if (media.facet == Facet::MAIN) {
      auto &main = media.data.emplace<Media::Main>();
      uint16_t eligible;
      if (!fields(state,
                  {"facet", "view_seq", "source", "phase", "eligible", "volume_raw", "volume_percent", "volume_rail",
                   "muted", "shuffle", "title_status", "media_title", "artist_status", "media_artist"}) ||
          !enumeration(state["phase"], PHASES, main.phase) ||
          !integer(state["eligible"], Media::ALL_OPERATIONS, eligible) || eligible != media.eligible ||
          !volume(state["volume_raw"], state["volume_percent"], state["volume_rail"], main.volume) ||
          !boolean(state["muted"], main.muted) || !boolean(state["shuffle"], main.shuffle) ||
          !enumeration(state["title_status"], TEXT, main.title_status) || !text(state["media_title"], main.title) ||
          !enumeration(state["artist_status"], TEXT, main.artist_status) || !text(state["media_artist"], main.artist))
        return false;
    } else {
      if (!state["editable"].is<bool>())
        return false;
      media.editable = state["editable"];
      if (media.facet == Facet::VOLUME) {
        auto &observed = media.data.emplace<Media::Volume>();
        uint8_t step;
        if (!fields(state, {"facet", "view_seq", "source", "editable", "ui_step_percent", "observed_raw",
                            "percent_text", "rail"}) ||
            !integer(state["ui_step_percent"], 1, step, true) ||
            !volume(state["observed_raw"], state["percent_text"], state["rail"], observed))
          return false;
      } else {
        auto &choice = media.data.emplace<Media::Choice>();
        if (!fields(state, {"facet", "view_seq", "source", "options", "index", "editable"}) ||
            !state["options"].is<ArduinoJson::JsonArrayConst>())
          return false;
        const auto options = state["options"].as<ArduinoJson::JsonArrayConst>();
        if (options.size() == 0 || options.size() > choice.options.size())
          return false;
        choice.count = options.size();
        for (size_t index = 0; index < options.size(); index++)
          if (!text(options[index], choice.options[index]))
            return false;
        if (!state["index"].isNull() && !integer(state["index"], choice.count - 1, choice.index))
          return false;
      }
    }
    return control_valid_media_snapshot(message.state);
  }
  uint8_t version;
  if (!integer(object["media_version"], 1, version, true) || !enumeration(object["facet"], FACETS, media.facet) ||
      !integer(object["view_seq"], CONTROL_MAX_SEQUENCE, media.view_seq,
               message.kind != ControlKind::CONTROL_KIND_COMMAND))
    return false;
  if (message.kind == ControlKind::CONTROL_KIND_FACET)
    return fields(object, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind",
                           "token", "media_version", "view_seq", "facet"});
  if (message.kind == ControlKind::CONTROL_KIND_FACET_RESULT) {
    if (!fields(object, {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind",
                         "token", "media_version", "view_seq", "facet", "state_seq", "status", "reason"}) ||
        !integer(object["state_seq"], CONTROL_MAX_SEQUENCE, message.state.state_seq, true) ||
        !enumeration(object["reason"], REASONS, message.reason))
      return false;
    media.editable = object["status"].as<ArduinoJson::JsonString>() == "ready";
    return media.editable ? message.reason == ControlReason::CONTROL_REASON_NONE
                          : object["status"].as<ArduinoJson::JsonString>() == "rejected" &&
                                message.reason != ControlReason::CONTROL_REASON_NONE &&
                                message.reason != ControlReason::CONTROL_REASON_SERVICE_TIMEOUT &&
                                message.reason != ControlReason::CONTROL_REASON_STATE_TIMEOUT;
  }
  uint8_t operation;
  if (message.kind != ControlKind::CONTROL_KIND_COMMAND || !enumeration(object["op"], OPERATIONS, operation) ||
      !integer(object["command_seq"], CONTROL_MAX_SEQUENCE, message.command_seq, true) ||
      !integer(object["expected_state_generation"], CONTROL_MAX_SEQUENCE, message.expected_state_generation, true))
    return false;
  message.operation =
      static_cast<ControlOperation>(static_cast<uint8_t>(ControlOperation::CONTROL_OPERATION_MEDIA_PLAY) + operation);
  const size_t extra = operation == 7 || operation == 12 ? 2 : operation == 10 || operation == 11 ? 1 : 0;
  if (!fields(object,
              {"v", "type", "kind", "channel", "open_seq", "slot", "card_id", "revision", "control_kind", "token",
               "media_version", "view_seq", "facet", "command_seq", "expected_state_generation", "op"},
              extra))
    return false;
  if (operation == 7) {
    auto &observed = media.data.emplace<Media::Volume>();
    return media.facet == Facet::VOLUME && text(object["expected_raw"], observed.raw) &&
           control_media_raw_valid(observed.raw.view()) && integer(object["target_percent"], 100, message.value);
  }
  if (operation == 12)
    return media.facet >= Facet::SOURCE && integer(object["expected_index"], 7, message.expected_index) &&
           integer(object["target_index"], 7, message.value);
  if (media.facet != Facet::MAIN)
    return false;
  if (operation == 10 || operation == 11) {
    if (!object["target"].is<bool>())
      return false;
    message.value = object["target"].as<bool>();
  }
  return true;
}

void control_encode_media(JsonObject object, const ControlMessage &message) {
  const auto *media = message.state.family_if<Media>();
  if (media == nullptr || static_cast<uint8_t>(media->facet) >= std::size(FACETS))
    return;
  if (message.kind == ControlKind::CONTROL_KIND_GRANT || message.kind == ControlKind::CONTROL_KIND_STATE) {
    object["panel_type"] = "media";
    object["available"] = media->available();
    object["state_generation"] = media->state_generation;
    object["eligible_actions"] = media->eligible;
    auto caps = object["panel_caps"].to<JsonObject>();
    caps["device_type"] = DEVICES[media->device_type];
    caps["operations"] = media->operations;
    caps["facets"] = media->facets;
    auto state = object["panel_state"].to<JsonObject>();
    state["facet"] = FACETS[static_cast<uint8_t>(media->facet)];
    state["view_seq"] = media->view_seq;
    state["source"] = SOURCES[media->source];
    if (media->facet == Facet::MAIN) {
      const auto *main = std::get_if<Media::Main>(&media->data);
      if (main == nullptr)
        return;
      state["phase"] = PHASES[main->phase];
      state["eligible"] = media->eligible;
      write_volume(state, main->volume, "volume_raw", "volume_percent", "volume_rail");
      write_bool(state, "muted", main->muted);
      write_bool(state, "shuffle", main->shuffle);
      state["title_status"] = TEXT[main->title_status];
      state["media_title"] = main->title.bytes.data();
      state["artist_status"] = TEXT[main->artist_status];
      state["media_artist"] = main->artist.bytes.data();
    } else {
      state["editable"] = media->editable;
      if (media->facet == Facet::VOLUME) {
        const auto *observed = std::get_if<Media::Volume>(&media->data);
        if (observed == nullptr)
          return;
        state["ui_step_percent"] = 1;
        write_volume(state, *observed, "observed_raw", "percent_text", "rail");
      } else {
        const auto *choice = std::get_if<Media::Choice>(&media->data);
        if (choice == nullptr)
          return;
        auto options = state["options"].to<ArduinoJson::JsonArray>();
        for (uint8_t index = 0; index < choice->count; index++)
          options.add(choice->options[index].bytes.data());
        if (choice->index == 255)
          state["index"] = nullptr;
        else
          state["index"] = choice->index;
      }
    }
    return;
  }
  object["media_version"] = 1;
  object["facet"] = FACETS[static_cast<uint8_t>(media->facet)];
  object["view_seq"] = media->view_seq;
  if (message.kind == ControlKind::CONTROL_KIND_FACET_RESULT) {
    object["state_seq"] = message.state.state_seq;
    object["status"] = media->editable ? "ready" : "rejected";
    object["reason"] = REASONS[static_cast<uint8_t>(message.reason)];
  } else if (message.kind == ControlKind::CONTROL_KIND_COMMAND) {
    const auto operation =
        static_cast<size_t>(message.operation) - static_cast<size_t>(ControlOperation::CONTROL_OPERATION_MEDIA_PLAY);
    if (operation >= std::size(OPERATIONS))
      return;
    object["op"] = OPERATIONS[operation];
    object["expected_state_generation"] = message.expected_state_generation;
    if (operation == 7) {
      const auto *observed = std::get_if<Media::Volume>(&media->data);
      if (observed == nullptr)
        return;
      object["expected_raw"] = observed->raw.bytes.data();
      object["target_percent"] = message.value;
    } else if (operation == 12) {
      object["expected_index"] = message.expected_index;
      object["target_index"] = message.value;
    } else if (operation == 10 || operation == 11)
      object["target"] = message.value != 0;
  }
}
}  // namespace esphome::lvgl_material
#endif
