#include "material_tile_surface.h"

#include "esphome/components/json/json_util.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cstring>

namespace esphome::lvgl_material {

static const char *const TAG = "lvgl_material.tiles";

static constexpr size_t MAX_PAYLOAD_BYTES = 32768;
static constexpr size_t MAX_CARD_COUNT = 32;
static constexpr uint64_t MAX_REVISION = (1ULL << 53) - 1;
#ifdef USE_LVGL_MATERIAL_TILE_ACTION_AVAILABILITY
static constexpr uint8_t PRESS_AVAILABILITY_PRESENT = 1;
static constexpr uint8_t PRESS_AVAILABILITY_ENABLED = 2;
static constexpr uint8_t PRESS_AVAILABILITY_OWNS_DISABLED = 4;
#endif

// Validate UTF-8, including overlong encodings and surrogate code points.
static bool valid_text(JsonVariantConst value, size_t max_bytes) {
  if (!value.is<const char *>())
    return false;
  const JsonString text = value.as<JsonString>();
  if (text.size() > max_bytes || std::strlen(text.c_str()) != text.size())
    return false;
  const auto *bytes = reinterpret_cast<const uint8_t *>(text.c_str());
  for (size_t i = 0; i < text.size();) {
    const uint8_t first = bytes[i++];
    if (first < 0x80)
      continue;
    const unsigned continuation = first >= 0xC2 && first <= 0xDF   ? 1
                                  : first >= 0xE0 && first <= 0xEF ? 2
                                  : first >= 0xF0 && first <= 0xF4 ? 3
                                                                   : 0;
    if (continuation == 0 || i + continuation > text.size())
      return false;
    if ((first == 0xE0 && bytes[i] < 0xA0) || (first == 0xED && bytes[i] >= 0xA0) ||
        (first == 0xF0 && bytes[i] < 0x90) || (first == 0xF4 && bytes[i] >= 0x90))
      return false;
    for (unsigned j = 0; j < continuation; j++)
      if ((bytes[i++] & 0xC0) != 0x80)
        return false;
  }
  return true;
}

static bool valid_card_id(JsonVariantConst value) {
  if (!valid_text(value, 64))
    return false;
  const char *id = value.as<const char *>();
  for (size_t i = 0; id[i] != '\0'; i++) {
    const char character = id[i];
    const bool alphanumeric = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                              (character >= '0' && character <= '9');
    if (!alphanumeric && (i == 0 || (character != '_' && character != '-')))
      return false;
  }
  return true;
}

static bool valid_color(JsonVariantConst value) { return value.is<uint32_t>() && value.as<uint32_t>() <= 0xFFFFFFU; }

void MaterialTileSurface::add_tile(uint8_t slot, lv_obj_t *widget, lv_obj_t *icon, lv_obj_t *title, lv_obj_t *subtitle,
                                   bool always_hidden) {
  this->tiles_.push_back({this, widget, icon, title, subtitle, slot, 3, always_hidden});
}

void MaterialTileSurface::setup() {
  if (this->tiles_.empty() || this->tiles_.size() > MAX_CARD_COUNT) {
    ESP_LOGE(TAG, "Expected 1..32 tile bindings");
    this->mark_failed();
    return;
  }

  for (auto &tile : this->tiles_) {
    if (tile.widget == nullptr || tile.icon == nullptr || tile.title == nullptr || tile.subtitle == nullptr) {
      ESP_LOGE(TAG, "Tile %u has an incomplete widget binding", tile.slot);
      this->mark_failed();
      return;
    }
    if ((this->require_snapshot_ready_ || this->control_press_enabled_ || this->get_press_enabled_version() == 1) &&
        tile.slot >= 32) {
      ESP_LOGE(TAG, "Snapshot readiness requires slots below 32");
      this->mark_failed();
      return;
    }
    if (std::count_if(this->tiles_.begin(), this->tiles_.end(),
                      [&tile](const TileBinding &other) { return other.slot == tile.slot; }) != 1) {
      ESP_LOGE(TAG, "Duplicate tile binding %u", tile.slot);
      this->mark_failed();
      return;
    }
    if (tile.always_hidden)
      lv_obj_add_flag(tile.widget, LV_OBJ_FLAG_HIDDEN);
    tile.snapshot_ready =
        !this->require_snapshot_ready_ || tile.always_hidden || lv_obj_has_flag(tile.widget, LV_OBJ_FLAG_HIDDEN);
    tile.context_ready = tile.snapshot_ready;
    lv_obj_add_event_cb(tile.widget, tile_event_cb_, LV_EVENT_ALL, &tile);
  }
}

void MaterialTileSurface::dump_config() {
  ESP_LOGCONFIG(TAG, "Material Tile Surface:");
  ESP_LOGCONFIG(TAG, "  Bound tiles: %u", static_cast<unsigned>(this->tiles_.size()));
}

bool MaterialTileSurface::configure(const std::string &payload) { return this->configure_(payload, nullptr); }

void MaterialTileSurface::set_control_press_enabled(bool enabled) {
  if (this->control_press_enabled_ == enabled)
    return;
  this->control_press_enabled_ = enabled;
  // Re-enabling admission cannot resurrect a press held before disabling it.
  for (auto &tile : this->tiles_)
    if (tile.control_light)
      tile.press_valid = false;
}

#ifdef USE_LVGL_MATERIAL_DETAIL_TILE_ROUTING
void MaterialTileSurface::set_light_detail_controls_enabled(bool enabled) {
  if (this->light_detail_controls_enabled_ == enabled)
    return;
  this->light_detail_controls_enabled_ = enabled;
  for (auto &tile : this->tiles_)
    if (tile.control_detail)
      tile.press_valid = false;
}
#endif
#ifdef USE_LVGL_MATERIAL_ENTITY_CONTROL_TILE_ROUTING
void MaterialTileSurface::set_number_controls_enabled(bool enabled) {
  if (this->number_controls_enabled_ == enabled)
    return;
  this->number_controls_enabled_ = enabled;
  for (auto &tile : this->tiles_)
    if (tile.control_number)
      tile.press_valid = false;
}
#endif
#ifdef USE_LVGL_MATERIAL_CHOICE_TILE_ROUTING
void MaterialTileSurface::set_choice_controls_enabled(bool enabled) {
  if (this->choice_controls_enabled_ == enabled)
    return;
  this->choice_controls_enabled_ = enabled;
  for (auto &tile : this->tiles_)
    if (tile.control_choice)
      tile.press_valid = false;
}
#endif
#ifdef USE_LVGL_MATERIAL_COVER_TILE_ROUTING
void MaterialTileSurface::set_cover_controls_enabled(bool enabled) {
  if (this->cover_controls_enabled_ == enabled)
    return;
  this->cover_controls_enabled_ = enabled;
  for (auto &tile : this->tiles_)
    if (tile.control_cover)
      tile.press_valid = false;
}
#endif
#ifdef USE_LVGL_MATERIAL_FAN_TILE_ROUTING
void MaterialTileSurface::set_fan_controls_enabled(bool enabled) {
  if (this->fan_controls_enabled_ == enabled)
    return;
  this->fan_controls_enabled_ = enabled;
  for (auto &tile : this->tiles_)
    if (tile.control_fan)
      tile.press_valid = false;
}
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_TILE_ROUTING
void MaterialTileSurface::set_humidifier_controls_enabled(bool enabled) {
  if (this->humidifier_controls_enabled_ == enabled)
    return;
  this->humidifier_controls_enabled_ = enabled;
  for (auto &tile : this->tiles_)
    if (tile.control_humidifier)
      tile.press_valid = false;
}
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_TILE_ROUTING
void MaterialTileSurface::set_robot_controls_enabled(bool enabled) {
  if (this->robot_controls_enabled_ == enabled)
    return;
  this->robot_controls_enabled_ = enabled;
  for (auto &tile : this->tiles_)
    if (tile.control_robot)
      tile.press_valid = false;
}
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_TILE_ROUTING
void MaterialTileSurface::set_panel_controls_enabled(bool enabled) {
  if (this->panel_controls_enabled_ == enabled)
    return;
  this->panel_controls_enabled_ = enabled;
  for (auto &tile : this->tiles_)
    if (tile.control_panel)
      tile.press_valid = false;
}
#endif

bool MaterialTileSurface::configure(const std::string &payload, uint32_t accent_override) {
  if (accent_override > 0xFFFFFFU)
    return false;
  return this->configure_(payload, &accent_override);
}

bool MaterialTileSurface::configure_(const std::string &payload, const uint32_t *accent_override) {
  if (payload.empty() || payload.size() > MAX_PAYLOAD_BYTES || this->is_failed())
    return false;
  const bool valid = json::parse_json(payload, [this, accent_override](JsonObject root) -> bool {
    if (!root["version"].is<int>() || root["version"].as<int>() != 1)
      return false;
    JsonArray cards = root["cards"].as<JsonArray>();
    if (cards.isNull() || cards.size() > MAX_CARD_COUNT)
      return false;
    const bool has_revision = !root["revision"].isUnbound();
    if (has_revision && (!root["revision"].is<uint64_t>() || root["revision"].as<uint64_t>() > MAX_REVISION))
      return false;
    const uint64_t revision = has_revision ? root["revision"].as<uint64_t>() : 0;
    JsonObject theme = root["theme"].as<JsonObject>();
    const bool themed = !theme.isNull();
    if (!root["theme"].isUnbound() && (!themed || (!theme["accent"].isUnbound() && !valid_color(theme["accent"]))))
      return false;
    bool contextual = false;
    for (JsonVariant item : cards)
      if (item.is<JsonObject>() && !item["id"].isUnbound())
        contextual = true;
    // First pass is read-only. No invalid later card may partially update the
    // theme, an earlier label, a gesture context or readiness metadata.
    for (JsonVariant item : cards) {
      if (!item.is<JsonObject>())
        return false;
      JsonObject card = item.as<JsonObject>();
      if (!card["slot"].is<uint8_t>())
        return false;
      const uint8_t slot = card["slot"].as<uint8_t>();
      if (this->find_tile_(slot) == nullptr && (this->require_snapshot_ready_ || contextual))
        return false;
      unsigned occurrences = 0;
      for (JsonVariant other : cards)
        if (other.is<JsonObject>() && other["slot"].is<uint8_t>() && other["slot"].as<uint8_t>() == slot)
          occurrences++;
      if (occurrences != 1)
        return false;
      if (!card["visible"].isUnbound() && !card["visible"].is<bool>())
        return false;
      if (!card["press_enabled"].isUnbound() &&
          (!card["press_enabled"].is<bool>() || !card["control_kind"].isUnbound()))
        return false;
      for (const char *name : {"icon", "title", "subtitle", "palette"}) {
        if (!card[name].isUnbound() && !valid_text(card[name], std::strcmp(name, "icon") == 0 ? 16 : 160))
          return false;
      }
      for (const char *name : {"background", "foreground"})
        if (!card[name].isUnbound() && !valid_color(card[name]))
          return false;
      if (!card["id"].isUnbound()) {
        if (!has_revision || !valid_card_id(card["id"]))
          return false;
        contextual = true;
      }
      if (!card["control_kind"].isUnbound()) {
#ifdef USE_LVGL_MATERIAL_ENTITY_CONTROL_TILE_ROUTING
        if (!valid_text(card["control_kind"], 10))
          return false;
        const char *kind = card["control_kind"].as<const char *>();
        if ((std::strcmp(kind, "light") != 0 && !(this->number_controls_enabled_ && std::strcmp(kind, "number") == 0)
#ifdef USE_LVGL_MATERIAL_CHOICE_TILE_ROUTING
             && !(this->choice_controls_enabled_ && std::strcmp(kind, "choice") == 0)
#endif
#ifdef USE_LVGL_MATERIAL_COVER_TILE_ROUTING
             && !(this->cover_controls_enabled_ && std::strcmp(kind, "cover") == 0)
#endif
#ifdef USE_LVGL_MATERIAL_FAN_TILE_ROUTING
             && !(this->fan_controls_enabled_ && std::strcmp(kind, "fan") == 0)
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_TILE_ROUTING
             && !(this->humidifier_controls_enabled_ && std::strcmp(kind, "humidifier") == 0)
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_TILE_ROUTING
             && !(this->robot_controls_enabled_ && std::strcmp(kind, "robot") == 0)
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_TILE_ROUTING
             && !(this->panel_controls_enabled_ && std::strcmp(kind, "panel") == 0)
#endif
                 ) ||
            !has_revision ||
#else
        if (!valid_text(card["control_kind"], 5) ||
            std::strcmp(card["control_kind"].as<const char *>(), "light") != 0 || !has_revision ||
#endif
            !valid_card_id(card["id"]) || card["id"].as<const char *>()[0] == '\0' ||
            this->find_tile_(slot) == nullptr || slot >= 32)
          return false;
      }
      if (!card["control_detail_version"].isUnbound()) {
#ifdef USE_LVGL_MATERIAL_DETAIL_TILE_ROUTING
        if (!this->light_detail_controls_enabled_ || !card["control_detail_version"].is<uint8_t>() ||
            card["control_detail_version"].as<uint8_t>() != 1 || !card["control_kind"].is<const char *>() ||
            std::strcmp(card["control_kind"].as<const char *>(), "light") != 0)
          return false;
#else
        return false;
#endif
      }
    }

    this->last_changed_slots_ = 0;
    const uint8_t context_version = contextual ? 1 : 0;
    // A protocol switch changes dispatch policy even on omitted legacy slots.
    if (context_version != this->context_version_)
      for (auto &tile : this->tiles_)
        this->mark_context_changed_(tile);
    this->configured_revision_ = revision;
    this->context_version_ = context_version;
    const bool apply_theme = themed || accent_override != nullptr;
    if (apply_theme)
      this->set_accent(accent_override != nullptr ? *accent_override : (theme["accent"] | this->accent_color_));

    for (JsonObject card : cards) {
      const int slot_value = card["slot"].as<uint8_t>();
      TileBinding *tile = this->find_tile_(static_cast<uint8_t>(slot_value));
      if (tile == nullptr)
        continue;
      const bool visible = !tile->always_hidden && (card["visible"] | true);
      bool changed = visible == lv_obj_has_flag(tile->widget, LV_OBJ_FLAG_HIDDEN);
      bool context_changed = changed;
      const char *card_id = card["id"] | "";
      const bool context = contextual && card_id[0] != '\0';
      const bool control_light = !card["control_kind"].isUnbound();
#ifdef USE_LVGL_MATERIAL_DETAIL_TILE_ROUTING
      const bool control_detail = !card["control_detail_version"].isUnbound();
      if (tile->control_detail != control_detail) {
        tile->control_detail = control_detail;
        changed = context_changed = true;
      }
#endif
#ifdef USE_LVGL_MATERIAL_CHOICE_TILE_ROUTING
      const bool control_choice = control_light && std::strcmp(card["control_kind"].as<const char *>(), "choice") == 0;
      if (tile->control_choice != control_choice) {
        tile->control_choice = control_choice;
        changed = context_changed = true;
      }
#endif
#ifdef USE_LVGL_MATERIAL_COVER_TILE_ROUTING
      const bool control_cover = control_light && std::strcmp(card["control_kind"].as<const char *>(), "cover") == 0;
      if (tile->control_cover != control_cover) {
        tile->control_cover = control_cover;
        changed = context_changed = true;
      }
#endif
#ifdef USE_LVGL_MATERIAL_FAN_TILE_ROUTING
      const bool control_fan = control_light && std::strcmp(card["control_kind"].as<const char *>(), "fan") == 0;
      if (tile->control_fan != control_fan) {
        tile->control_fan = control_fan;
        changed = context_changed = true;
      }
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_TILE_ROUTING
      const bool control_humidifier =
          control_light && std::strcmp(card["control_kind"].as<const char *>(), "humidifier") == 0;
      if (tile->control_humidifier != control_humidifier) {
        tile->control_humidifier = control_humidifier;
        changed = context_changed = true;
      }
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_TILE_ROUTING
      const bool control_robot = control_light && std::strcmp(card["control_kind"].as<const char *>(), "robot") == 0;
      if (tile->control_robot != control_robot) {
        tile->control_robot = control_robot;
        changed = context_changed = true;
      }
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_TILE_ROUTING
      const bool control_panel = control_light && std::strcmp(card["control_kind"].as<const char *>(), "panel") == 0;
      if (tile->control_panel != control_panel) {
        tile->control_panel = control_panel;
        changed = context_changed = true;
      }
#endif
#ifdef USE_LVGL_MATERIAL_ENTITY_CONTROL_TILE_ROUTING
      const bool control_number = control_light && std::strcmp(card["control_kind"].as<const char *>(), "number") == 0;
      if (tile->control_number != control_number) {
        tile->control_number = control_number;
        changed = context_changed = true;
      }
#endif
      if (tile->control_light != control_light) {
        tile->control_light = control_light;
        changed = true;
        context_changed = true;
      }
#ifdef USE_LVGL_MATERIAL_TILE_ACTION_AVAILABILITY
      const bool press_present =
          this->action_availability_enabled_ && !control_light && !card["press_enabled"].isUnbound();
      const bool press_enabled = !press_present || card["press_enabled"].as<bool>();
      const bool was_press_enabled = (tile->press_availability & PRESS_AVAILABILITY_ENABLED) != 0;
      const bool was_press_present = (tile->press_availability & PRESS_AVAILABILITY_PRESENT) != 0;
      if (was_press_enabled != press_enabled) {
        changed = context_changed = true;
      } else if (was_press_present != press_present) {
        changed = true;
      }
      this->update_press_availability_(*tile, press_present, press_enabled);
#endif
      if (tile->context != context || tile->revision != revision || std::strcmp(tile->card_id.data(), card_id) != 0) {
        std::strcpy(tile->card_id.data(), card_id);
        tile->revision = revision;
        tile->context = context;
        changed = true;
        context_changed = true;
      }
      if (visible) {
        lv_obj_remove_flag(tile->widget, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(tile->widget, LV_OBJ_FLAG_HIDDEN);
        if (changed)
          this->mark_context_changed_(*tile);
        tile->snapshot_ready = true;
        tile->context_ready = true;
        continue;
      }

      const char *icon = card["icon"] | "";
      const char *title = card["title"] | "";
      const char *subtitle = card["subtitle"] | "";
      if (std::strcmp(lv_label_get_text(tile->icon), icon) != 0) {
        lv_label_set_text(tile->icon, icon);
        changed = true;
      }
      if (std::strcmp(lv_label_get_text(tile->title), title) != 0) {
        lv_label_set_text(tile->title, title);
        changed = true;
      }
      if (std::strcmp(lv_label_get_text(tile->subtitle), subtitle) != 0) {
        lv_label_set_text(tile->subtitle, subtitle);
        changed = true;
      }

      const uint8_t role = role_from_name_(card["palette"] | "surface");
      tile->role = role;
      const uint32_t background = apply_theme ? this->role_background_(role) : (card["background"] | 0x332E3C);
      const uint32_t foreground = apply_theme ? this->role_foreground_(role) : (card["foreground"] | 0xF5EEFB);
      const lv_color_t background_color = lv_color_hex(background);
      const lv_color_t foreground_color = lv_color_hex(foreground);
      if (!lv_color_eq(lv_obj_get_style_bg_color(tile->widget, LV_PART_MAIN), background_color)) {
        lv_obj_set_style_bg_color(tile->widget, background_color, LV_PART_MAIN);
        changed = true;
      }
      if (!lv_color_eq(lv_obj_get_style_text_color(tile->title, LV_PART_MAIN), foreground_color)) {
        lv_obj_set_style_text_color(tile->icon, foreground_color, LV_PART_MAIN);
        lv_obj_set_style_text_color(tile->title, foreground_color, LV_PART_MAIN);
        lv_obj_set_style_text_color(tile->subtitle, foreground_color, LV_PART_MAIN);
        changed = true;
      }
      if (changed) {
        if (context_changed)
          this->mark_context_changed_(*tile);
        else
          this->mark_changed_(*tile);
        lv_obj_invalidate(tile->widget);
      }
    }
    return true;
  });

  if (!valid) {
    ESP_LOGW(TAG, "Rejected invalid tile configuration payload");
    return false;
  }
  return true;
}

void MaterialTileSurface::set_accent(uint32_t accent) {
  this->accent_color_ = accent & 0xFFFFFFU;
  for (auto &tile : this->tiles_) {
    const uint8_t role = tile.role;
    const lv_color_t background = lv_color_hex(this->role_background_(role));
    const lv_color_t foreground = lv_color_hex(this->role_foreground_(role));
    bool changed = false;
    if (!lv_color_eq(lv_obj_get_style_bg_color(tile.widget, LV_PART_MAIN), background)) {
      lv_obj_set_style_bg_color(tile.widget, background, LV_PART_MAIN);
      changed = true;
    }
    if (!lv_color_eq(lv_obj_get_style_text_color(tile.title, LV_PART_MAIN), foreground)) {
      lv_obj_set_style_text_color(tile.icon, foreground, LV_PART_MAIN);
      lv_obj_set_style_text_color(tile.title, foreground, LV_PART_MAIN);
      lv_obj_set_style_text_color(tile.subtitle, foreground, LV_PART_MAIN);
      changed = true;
    }
    if (changed) {
      this->mark_changed_(tile);
      lv_obj_invalidate(tile.widget);
    }
  }
}

uint8_t MaterialTileSurface::role_from_name_(const char *name) {
  if (name == nullptr)
    return 3;
  if (std::strcmp(name, "primary") == 0)
    return 0;
  if (std::strcmp(name, "secondary") == 0)
    return 1;
  if (std::strcmp(name, "tertiary") == 0)
    return 2;
  if (std::strcmp(name, "primary_container") == 0)
    return 4;
  return 3;
}

uint32_t MaterialTileSurface::role_background_(uint8_t role) const {
  const uint8_t r = (this->accent_color_ >> 16) & 0xFF;
  const uint8_t g = (this->accent_color_ >> 8) & 0xFF;
  const uint8_t b = this->accent_color_ & 0xFF;
  const uint8_t dark_r = r / 5;
  const uint8_t dark_g = g / 5;
  const uint8_t dark_b = b / 5;
  switch (role) {
    case 0:
      return this->accent_color_;
    case 1:
      return ((uint32_t) std::min(255, (int) r + 28) << 16) | ((uint32_t) std::min(255, (int) g + 20) << 8) |
             std::min(255, (int) b + 10);
    case 2:
      return ((uint32_t) std::min(255, (int) r + 8) << 16) | ((uint32_t) std::min(255, (int) g + 38) << 8) |
             std::min(255, (int) b + 38);
    case 4:
      return ((uint32_t) std::min(255, (int) r + 58) << 16) | ((uint32_t) std::min(255, (int) g + 58) << 8) |
             std::min(255, (int) b + 58);
    default:
      return ((uint32_t) dark_r << 16) | ((uint32_t) dark_g << 8) | dark_b;
  }
}

uint32_t MaterialTileSurface::role_foreground_(uint8_t role) const {
  // Derive the foreground from the actual surface for this role.  The old
  // implementation only inspected the raw accent and used a fixed light
  // foreground for `surface` and `primary_container`; that made light
  // user-selected accents (especially the cyan/amber palettes) unreadable.
  const uint32_t background = this->role_background_(role);
  const uint8_t r = (background >> 16) & 0xFF;
  const uint8_t g = (background >> 8) & 0xFF;
  const uint8_t b = background & 0xFF;
  const uint32_t luminance = (uint32_t) r * 299U + (uint32_t) g * 587U + (uint32_t) b * 114U;
  return luminance > 150000U ? 0x071018 : 0xFFFFFF;
}

void MaterialTileSurface::tile_event_cb_(lv_event_t *event) {
  auto *tile = static_cast<TileBinding *>(lv_event_get_user_data(event));
  if (tile == nullptr || tile->owner == nullptr)
    return;
  auto *owner = tile->owner;
#ifdef USE_LVGL_DEFERRED_PRESS_CONTEXT
  if (lv_event_get_code(event) == lvgl::deferred_press_context_event()) {
#ifdef USE_LVGL_MATERIAL_TILE_ACTION_AVAILABILITY
    if (owner->action_availability_enabled_) {
      auto *context = static_cast<lvgl::DeferredPressContext *>(lv_event_get_param(event));
      if (context != nullptr) {
        context->valid = context->valid && owner->can_press_(*tile) &&
                         (context->capturing || context->generation == tile->generation);
        if (context->capturing)
          context->generation = tile->generation;
      }
    }
#endif
    return;
  }
#endif
  if (lv_event_get_code(event) == LV_EVENT_PRESSED) {
    tile->press_seen = true;
    tile->press_valid = owner->can_press_(*tile);
    tile->pressed_context = tile->context;
    tile->pressed_revision = tile->revision;
    tile->pressed_context_generation = tile->context_generation;
    tile->pressed_card_id = tile->card_id;
    tile->pressed_control_light = tile->control_light;
#ifdef USE_LVGL_MATERIAL_DETAIL_TILE_ROUTING
    tile->pressed_control_detail = tile->control_detail;
#endif
#ifdef USE_LVGL_MATERIAL_TILE_ACTION_AVAILABILITY
    tile->pressed_indev = owner->action_availability_enabled_ && !tile->control_light && tile->press_valid
                              ? lv_event_get_indev(event)
                              : nullptr;
#endif
#ifdef USE_LVGL_MATERIAL_ENTITY_CONTROL_TILE_ROUTING
    tile->pressed_control_number = tile->control_number;
#endif
#ifdef USE_LVGL_MATERIAL_CHOICE_TILE_ROUTING
    tile->pressed_control_choice = tile->control_choice;
#endif
#ifdef USE_LVGL_MATERIAL_COVER_TILE_ROUTING
    tile->pressed_control_cover = tile->control_cover;
#endif
#ifdef USE_LVGL_MATERIAL_FAN_TILE_ROUTING
    tile->pressed_control_fan = tile->control_fan;
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_TILE_ROUTING
    tile->pressed_control_humidifier = tile->control_humidifier;
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_TILE_ROUTING
    tile->pressed_control_robot = tile->control_robot;
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_TILE_ROUTING
    tile->pressed_control_panel = tile->control_panel;
#endif
    return;
  }
  if (lv_event_get_code(event) == LV_EVENT_PRESS_LOST) {
    tile->press_seen = true;
    tile->press_valid = false;
#ifdef USE_LVGL_MATERIAL_TILE_ACTION_AVAILABILITY
    tile->pressed_indev = nullptr;
#endif
    return;
  }
#ifdef USE_LVGL_MATERIAL_TILE_ACTION_AVAILABILITY
  if (lv_event_get_code(event) == LV_EVENT_RELEASED)
    tile->pressed_indev = nullptr;
#endif
  if (lv_event_get_code(event) != LV_EVENT_CLICKED)
    return;
  const bool legacy_synthetic =
      !tile->control_light && !tile->press_seen && !owner->require_snapshot_ready_ && owner->context_version_ == 0;
  const bool valid =
      owner->can_press_(*tile) &&
      (legacy_synthetic || (tile->press_valid && tile->pressed_context_generation == tile->context_generation &&
                            tile->pressed_revision == tile->revision && tile->pressed_context == tile->context &&
                            tile->pressed_card_id == tile->card_id && tile->pressed_control_light == tile->control_light
#ifdef USE_LVGL_MATERIAL_DETAIL_TILE_ROUTING
                            && tile->pressed_control_detail == tile->control_detail
#endif
#ifdef USE_LVGL_MATERIAL_ENTITY_CONTROL_TILE_ROUTING
                            && tile->pressed_control_number == tile->control_number
#endif
#ifdef USE_LVGL_MATERIAL_CHOICE_TILE_ROUTING
                            && tile->pressed_control_choice == tile->control_choice
#endif
#ifdef USE_LVGL_MATERIAL_COVER_TILE_ROUTING
                            && tile->pressed_control_cover == tile->control_cover
#endif
#ifdef USE_LVGL_MATERIAL_FAN_TILE_ROUTING
                            && tile->pressed_control_fan == tile->control_fan
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_TILE_ROUTING
                            && tile->pressed_control_humidifier == tile->control_humidifier
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_TILE_ROUTING
                            && tile->pressed_control_robot == tile->control_robot
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_TILE_ROUTING
                            && tile->pressed_control_panel == tile->control_panel
#endif
                            ));
  tile->press_valid = false;
  tile->press_seen = false;
#ifdef USE_LVGL_MATERIAL_TILE_ACTION_AVAILABILITY
  tile->pressed_indev = nullptr;
#endif
  if (!valid)
    return;
  if (tile->control_light) {
#ifdef USE_LVGL_MATERIAL_DETAIL_TILE_ROUTING
    if (tile->control_detail) {
      owner->entity_detail_control_press_callback_.call(tile->slot, tile->pressed_card_id.data(),
                                                        tile->pressed_revision);
      return;
    }
#endif
#ifdef USE_LVGL_MATERIAL_ENTITY_CONTROL_TILE_ROUTING
    if (owner->entity_control_callback_registered_) {
      owner->entity_control_press_callback_.call(tile->slot, tile->pressed_card_id.data(), tile->pressed_revision,
#ifdef USE_LVGL_MATERIAL_CHOICE_TILE_ROUTING
                                                 tile->pressed_control_choice ? 2 :
#endif
#ifdef USE_LVGL_MATERIAL_COVER_TILE_ROUTING
                                                 tile->pressed_control_cover ? 3
                                                 :
#endif
#ifdef USE_LVGL_MATERIAL_FAN_TILE_ROUTING
                                                 tile->pressed_control_fan ? 4
                                                 :
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_TILE_ROUTING
                                                 tile->pressed_control_humidifier ? 5
                                                 :
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_TILE_ROUTING
                                                 tile->pressed_control_robot ? 6
                                                 :
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_TILE_ROUTING
                                                 tile->pressed_control_panel ? 7
                                                 :
#endif
                                                 tile->pressed_control_number ? 1
                                                                              : 0);
      return;
    }
    if (tile->control_number)
      return;  // Unsupported Number must never fall through to Light or toggle.
#endif
#ifdef USE_LVGL_MATERIAL_CHOICE_TILE_ROUTING
    if (tile->control_choice)
      return;
#endif
#ifdef USE_LVGL_MATERIAL_COVER_TILE_ROUTING
    if (tile->control_cover)
      return;
#endif
#ifdef USE_LVGL_MATERIAL_FAN_TILE_ROUTING
    if (tile->control_fan)
      return;
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_TILE_ROUTING
    if (tile->control_humidifier)
      return;
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_TILE_ROUTING
    if (tile->control_robot)
      return;
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_TILE_ROUTING
    if (tile->control_panel)
      return;
#endif
    owner->control_press_callback_.call(tile->slot, tile->pressed_card_id.data(), tile->pressed_revision);
    return;
  }
  if (owner->context_version_ == 1) {
    if (tile->context)
      owner->context_press_callback_.call(tile->slot, tile->pressed_card_id.data(), tile->pressed_revision);
  } else {
    owner->press_callback_.call(tile->slot);
  }
}

bool MaterialTileSurface::can_press_(const TileBinding &tile) const {
  return !tile.always_hidden && !lv_obj_has_flag(tile.widget, LV_OBJ_FLAG_HIDDEN) &&
#ifdef USE_LVGL_MATERIAL_TILE_ACTION_AVAILABILITY
         (!this->action_availability_enabled_ || tile.control_light ||
          (tile.press_availability & PRESS_AVAILABILITY_ENABLED) != 0) &&
#endif
         (!tile.control_light || (this->control_press_enabled_ && tile.slot < 32 && this->context_version_ == 1 &&
                                  tile.context && tile.revision == this->configured_revision_ && tile.context_ready)) &&
#ifdef USE_LVGL_MATERIAL_DETAIL_TILE_ROUTING
         (!tile.control_detail || (this->light_detail_controls_enabled_ && this->entity_detail_callback_registered_)) &&
#endif
#ifdef USE_LVGL_MATERIAL_ENTITY_CONTROL_TILE_ROUTING
         (!tile.control_number || (this->number_controls_enabled_ && this->entity_control_callback_registered_)) &&
#endif
#ifdef USE_LVGL_MATERIAL_CHOICE_TILE_ROUTING
         (!tile.control_choice || (this->choice_controls_enabled_ && this->entity_control_callback_registered_)) &&
#endif
#ifdef USE_LVGL_MATERIAL_COVER_TILE_ROUTING
         (!tile.control_cover || (this->cover_controls_enabled_ && this->entity_control_callback_registered_)) &&
#endif
#ifdef USE_LVGL_MATERIAL_FAN_TILE_ROUTING
         (!tile.control_fan || (this->fan_controls_enabled_ && this->entity_control_callback_registered_)) &&
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_TILE_ROUTING
         (!tile.control_humidifier ||
          (this->humidifier_controls_enabled_ && this->entity_control_callback_registered_)) &&
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_TILE_ROUTING
         (!tile.control_robot || (this->robot_controls_enabled_ && this->entity_control_callback_registered_)) &&
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_TILE_ROUTING
         (!tile.control_panel || (this->panel_controls_enabled_ && this->entity_control_callback_registered_)) &&
#endif
         (!this->require_snapshot_ready_ || tile.context_ready);
}

#ifdef USE_LVGL_MATERIAL_TILE_ACTION_AVAILABILITY
void MaterialTileSurface::cancel_available_press_(TileBinding &tile) {
  auto *indev = tile.pressed_indev;
  // Command validity may already have been revoked by an intervening context
  // update. The captured input remains ours until native release/press-lost.
  const bool owned = tile.press_seen && indev != nullptr;
  tile.pressed_indev = nullptr;
  tile.press_valid = false;
  if (owned) {
    // Cancel before adding DISABLED, while LVGL still owns a pressed target.
    // Resetting alone would allow the still-held contact to press it again.
    lv_indev_wait_release(indev);
    lv_indev_reset(indev, tile.widget);
  }
}

void MaterialTileSurface::update_press_availability_(TileBinding &tile, bool present, bool enabled) {
  const bool owns_disabled = (tile.press_availability & PRESS_AVAILABILITY_OWNS_DISABLED) != 0;
  const bool disable = present && !enabled;
  bool now_owns_disabled = owns_disabled;
  if (disable) {
    this->cancel_available_press_(tile);
    if (!lv_obj_has_state(tile.widget, LV_STATE_DISABLED)) {
      lv_obj_add_state(tile.widget, LV_STATE_DISABLED);
      now_owns_disabled = true;
    }
  } else if (owns_disabled) {
    lv_obj_remove_state(tile.widget, LV_STATE_DISABLED);
    now_owns_disabled = false;
  }
  tile.press_availability = (present ? PRESS_AVAILABILITY_PRESENT : 0) | (enabled ? PRESS_AVAILABILITY_ENABLED : 0) |
                            (now_owns_disabled ? PRESS_AVAILABILITY_OWNS_DISABLED : 0);
}
#endif

void MaterialTileSurface::mark_changed_(TileBinding &tile) {
  tile.generation++;
  tile.snapshot_ready = tile.always_hidden || lv_obj_has_flag(tile.widget, LV_OBJ_FLAG_HIDDEN);
  if (tile.slot < 32 && !tile.always_hidden)
    this->last_changed_slots_ |= 1UL << tile.slot;
}

void MaterialTileSurface::mark_context_changed_(TileBinding &tile) {
  tile.context_generation++;
  tile.press_valid = false;
  tile.context_ready = tile.always_hidden || lv_obj_has_flag(tile.widget, LV_OBJ_FLAG_HIDDEN);
  this->mark_changed_(tile);
}

uint32_t MaterialTileSurface::get_pending_slots() const {
  uint32_t pending = 0;
  for (const auto &tile : this->tiles_)
    if (tile.slot < 32 && !tile.snapshot_ready && !tile.always_hidden &&
        !lv_obj_has_flag(tile.widget, LV_OBJ_FLAG_HIDDEN))
      pending |= 1UL << tile.slot;
  return pending;
}

uint32_t MaterialTileSurface::get_pending_context_slots() const {
  uint32_t pending = 0;
  for (const auto &tile : this->tiles_)
    if (tile.slot < 32 && !tile.context_ready && !tile.always_hidden &&
        !lv_obj_has_flag(tile.widget, LV_OBJ_FLAG_HIDDEN))
      pending |= 1UL << tile.slot;
  return pending;
}

void MaterialTileSurface::mark_slots_ready(uint32_t mask) {
  for (auto &tile : this->tiles_)
    if (tile.slot < 32 && (mask & (1UL << tile.slot))) {
      tile.snapshot_ready = true;
      tile.context_ready = true;
    }
}

bool MaterialTileSurface::serialize_presentation(std::string &output, size_t max_bytes) const {
  output.clear();
  if (this->is_failed())
    return false;
  JsonDocument document(json::heap_json_allocator());
  JsonObject root = document.to<JsonObject>();
  root["version"] = 1;
  root["revision"] = this->configured_revision_;
  JsonArray cards = root["cards"].to<JsonArray>();
  static constexpr const char *PALETTES[] = {"primary", "secondary", "tertiary", "surface", "primary_container"};
  for (const auto &tile : this->tiles_) {
    // A partial update may retain a card from an older revision. A single
    // wire-v1 revision cannot faithfully restore such mixed contexts.
    if (this->context_version_ == 1 && tile.context && tile.revision != this->configured_revision_)
      return false;
    if (tile.control_light && (this->context_version_ != 1 || !tile.context))
      return false;
    JsonObject card = cards.add<JsonObject>();
    card["slot"] = tile.slot;
    card["visible"] = !tile.always_hidden && !lv_obj_has_flag(tile.widget, LV_OBJ_FLAG_HIDDEN);
    card["icon"] = lv_label_get_text(tile.icon);
    card["title"] = lv_label_get_text(tile.title);
    card["subtitle"] = lv_label_get_text(tile.subtitle);
    card["palette"] = PALETTES[tile.role];
    card["background"] = lv_color_to_u32(lv_obj_get_style_bg_color(tile.widget, LV_PART_MAIN)) & 0xFFFFFFU;
    card["foreground"] = lv_color_to_u32(lv_obj_get_style_text_color(tile.title, LV_PART_MAIN)) & 0xFFFFFFU;
    if (this->context_version_ == 1)
      card["id"] = tile.card_id.data();
#ifdef USE_LVGL_MATERIAL_TILE_ACTION_AVAILABILITY
    if (this->action_availability_enabled_ && !tile.control_light &&
        (tile.press_availability & PRESS_AVAILABILITY_PRESENT) != 0)
      card["press_enabled"] = (tile.press_availability & PRESS_AVAILABILITY_ENABLED) != 0;
#endif
    if (tile.control_light)
#ifdef USE_LVGL_MATERIAL_ENTITY_CONTROL_TILE_ROUTING
      card["control_kind"] =
#ifdef USE_LVGL_MATERIAL_CHOICE_TILE_ROUTING
          tile.control_choice ? "choice" :
#endif
#ifdef USE_LVGL_MATERIAL_COVER_TILE_ROUTING
          tile.control_cover ? "cover"
          :
#endif
#ifdef USE_LVGL_MATERIAL_FAN_TILE_ROUTING
          tile.control_fan ? "fan"
          :
#endif
#ifdef USE_LVGL_MATERIAL_HUMIDIFIER_TILE_ROUTING
          tile.control_humidifier ? "humidifier"
          :
#endif
#ifdef USE_LVGL_MATERIAL_ROBOT_TILE_ROUTING
          tile.control_robot ? "robot"
          :
#endif
#ifdef USE_LVGL_MATERIAL_PANEL_TILE_ROUTING
          tile.control_panel ? "panel"
          :
#endif
          tile.control_number ? "number"
                              : "light";
#else
      card["control_kind"] = "light";
#endif
#ifdef USE_LVGL_MATERIAL_DETAIL_TILE_ROUTING
    if (tile.control_detail)
      card["control_detail_version"] = 1;
#endif
    if (!valid_text(card["icon"], 16) || !valid_text(card["title"], 160) || !valid_text(card["subtitle"], 160))
      return false;
  }
  if (document.overflowed())
    return false;
  const size_t size = measureJson(root);
  if (size > std::min(max_bytes, MAX_PAYLOAD_BYTES))
    return false;
  output.resize(size);
  return serializeJson(root, output.data(), size + 1) == size;
}

MaterialTileSurface::TileBinding *MaterialTileSurface::find_tile_(uint8_t slot) {
  auto it = std::find_if(this->tiles_.begin(), this->tiles_.end(),
                         [slot](const TileBinding &tile) { return tile.slot == slot; });
  return it == this->tiles_.end() ? nullptr : &*it;
}

}  // namespace esphome::lvgl_material
