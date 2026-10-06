#include "material_tile_surface.h"

#include "esphome/components/json/json_util.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cstring>

namespace esphome::lvgl_material {

static const char *const TAG = "lvgl_material.tiles";

void MaterialTileSurface::add_tile(uint8_t slot, lv_obj_t *widget, lv_obj_t *icon, lv_obj_t *title,
                                   lv_obj_t *subtitle, bool always_hidden) {
  this->tiles_.push_back({this, widget, icon, title, subtitle, slot, 3, always_hidden});
}

void MaterialTileSurface::setup() {
  if (this->tiles_.empty()) {
    ESP_LOGE(TAG, "No tile bindings configured");
    this->mark_failed();
    return;
  }

  for (auto &tile : this->tiles_) {
    if (tile.widget == nullptr || tile.icon == nullptr || tile.title == nullptr || tile.subtitle == nullptr) {
      ESP_LOGE(TAG, "Tile %u has an incomplete widget binding", tile.slot);
      this->mark_failed();
      return;
    }
    if (tile.always_hidden)
      lv_obj_add_flag(tile.widget, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(tile.widget, tile_event_cb_, LV_EVENT_CLICKED, &tile);
  }
}

void MaterialTileSurface::dump_config() {
  ESP_LOGCONFIG(TAG, "Material Tile Surface:");
  ESP_LOGCONFIG(TAG, "  Bound tiles: %u", static_cast<unsigned>(this->tiles_.size()));
}

bool MaterialTileSurface::configure(const std::string &payload) {
  if (payload.empty())
    return false;

  this->last_changed_slots_ = 0;

  bool valid = json::parse_json(payload, [this](JsonObject root) -> bool {
    const int version = root["version"] | 0;
    if (version != 1)
      return false;

    JsonArray cards = root["cards"].as<JsonArray>();
    if (cards.isNull())
      return false;

    JsonObject theme = root["theme"].as<JsonObject>();
    const bool themed = !theme.isNull();
    if (themed)
      this->set_accent(theme["accent"] | this->accent_color_);

    for (JsonObject card : cards) {
      const int slot_value = card["slot"] | -1;
      if (slot_value < 0 || slot_value > 255)
        continue;
      TileBinding *tile = this->find_tile_(static_cast<uint8_t>(slot_value));
      if (tile == nullptr)
        continue;

      const bool visible = !tile->always_hidden && (card["visible"] | true);
      bool changed = visible == lv_obj_has_flag(tile->widget, LV_OBJ_FLAG_HIDDEN);
      if (visible) {
        lv_obj_remove_flag(tile->widget, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(tile->widget, LV_OBJ_FLAG_HIDDEN);
        if (changed && tile->slot < 32)
          this->last_changed_slots_ |= 1UL << tile->slot;
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
      const uint32_t background = themed ? this->role_background_(role) : (card["background"] | 0x332E3C);
      const uint32_t foreground = themed ? this->role_foreground_(role) : (card["foreground"] | 0xF5EEFB);
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
        if (tile->slot < 32)
          this->last_changed_slots_ |= 1UL << tile->slot;
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
      if (tile.slot < 32)
        this->last_changed_slots_ |= 1UL << tile.slot;
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
      return ((uint32_t) std::min(255, (int) r + 28) << 16) |
             ((uint32_t) std::min(255, (int) g + 20) << 8) | std::min(255, (int) b + 10);
    case 2:
      return ((uint32_t) std::min(255, (int) r + 8) << 16) |
             ((uint32_t) std::min(255, (int) g + 38) << 8) | std::min(255, (int) b + 38);
    case 4:
      return ((uint32_t) std::min(255, (int) r + 58) << 16) |
             ((uint32_t) std::min(255, (int) g + 58) << 8) | std::min(255, (int) b + 58);
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
  tile->owner->press_callback_.call(tile->slot);
}

MaterialTileSurface::TileBinding *MaterialTileSurface::find_tile_(uint8_t slot) {
  auto it = std::find_if(this->tiles_.begin(), this->tiles_.end(),
                         [slot](const TileBinding &tile) { return tile.slot == slot; });
  return it == this->tiles_.end() ? nullptr : &*it;
}

}  // namespace esphome::lvgl_material
