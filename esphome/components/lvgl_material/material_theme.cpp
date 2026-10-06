#include "material_theme.h"

#include "esphome/core/log.h"

#include <algorithm>

namespace esphome::lvgl_material {

static const char *const TAG = "lvgl_material.theme";

static uint32_t rgb_(lv_color_t color) {
  return (static_cast<uint32_t>(color.red) << 16) |
         (static_cast<uint32_t>(color.green) << 8) | static_cast<uint32_t>(color.blue);
}

bool MaterialTheme::is_surface_color_(uint32_t c) {
  return c == 0x071018 || c == 0x332E3C || c == 0x211A24 || c == 0x2D2136 || c == 0x303030 || c == 0x404040 || c == 0x474747;
}

bool MaterialTheme::is_primary_color_(uint32_t c) {
  return c == 0xE9DDFF || c == 0xEADDFF || c == 0xE4C2FF || c == 0xD9C2FF || c == 0x36BCF0;
}

bool MaterialTheme::is_secondary_color_(uint32_t c) {
  return c == 0xBAC3FF || c == 0xC2E7FF || c == 0x4FC3F7 || c == 0xD3E3FD;
}

bool MaterialTheme::is_tertiary_color_(uint32_t c) {
  return c == 0xB7EDE2 || c == 0xC3EDCF || c == 0x26A69A || c == 0x8CEED1;
}

bool MaterialTheme::is_primary_container_color_(uint32_t c) {
  return c == 0x633F1C || c == 0x8AD8F8;
}

bool MaterialTheme::is_on_surface_color_(uint32_t c) {
  return c == 0xF5EEFB || c == 0xF5F7FA || c == 0xF2F2F2 || c == 0xFFFFFF ||
         c == 0xD9D2E0 || c == 0xC4C7C5 || c == 0xA8A8AE || c == 0x85858D;
}

bool MaterialTheme::is_dark_role_text_(uint32_t c) {
  return c == 0x071018 || c == 0x0C1649 || c == 0x08201C || c == 0x210F48 ||
         c == 0x35105C || c == 0x3F006B || c == 0x400060;
}

bool MaterialTheme::is_outline_color_(uint32_t c) {
  return c == 0x5C5F5E || c == 0x5C5368 || c == 0xCFC5D8 || c == 0x4D3D76 || c == 0x382949 || c == 0x633F1C;
}

uint32_t MaterialTheme::mix_(uint32_t a, uint32_t b, uint8_t amount) {
  const uint8_t ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
  const uint8_t br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
  return (static_cast<uint32_t>(ar + ((static_cast<int>(br) - ar) * amount) / 255) << 16) |
         (static_cast<uint32_t>(ag + ((static_cast<int>(bg) - ag) * amount) / 255) << 8) |
         static_cast<uint32_t>(ab + ((static_cast<int>(bb) - ab) * amount) / 255);
}

uint32_t MaterialTheme::role_color_for_(uint32_t accent, uint8_t role) const {
  const uint32_t dark = mix_(accent, 0x000000, 204);
  switch (role) {
    case 1: return accent;                       // primary
    case 2: return mix_(accent, 0xFFFFFF, 72);   // secondary
    case 3: return mix_(accent, 0xFFFFFF, 112);  // tertiary
    case 4: return mix_(accent, 0x000000, 160);  // outline/scrim
    case 5: return mix_(accent, 0xFFFFFF, 150);  // primary container
    default: return dark;                       // surface
  }
}

uint32_t MaterialTheme::role_color_(uint8_t role) const {
  return this->role_color_for_(this->accent_color_, role);
}

uint32_t MaterialTheme::on_role_color_(uint8_t role) const {
  const uint32_t value = this->role_color_(role);
  const uint8_t r = (value >> 16) & 0xFF, g = (value >> 8) & 0xFF, b = value & 0xFF;
  const uint32_t luminance = static_cast<uint32_t>(r) * 299U + static_cast<uint32_t>(g) * 587U +
                              static_cast<uint32_t>(b) * 114U;
  return luminance > 145000U ? 0x071018 : 0xFFFFFF;
}

void MaterialTheme::apply_object_(lv_obj_t *object, uint8_t inherited_role) {
  if (object == nullptr)
    return;
  const lv_color_t bg = lv_obj_get_style_bg_color(object, LV_PART_MAIN);
  const lv_color_t text = lv_obj_get_style_text_color(object, LV_PART_MAIN);
  const uint32_t bg_rgb = rgb_(bg);
  const uint32_t text_rgb = rgb_(text);
  uint8_t role = 255;
  if (is_surface_color_(bg_rgb)) role = 0;
  else if (is_primary_color_(bg_rgb)) role = 1;
  else if (is_secondary_color_(bg_rgb)) role = 2;
  else if (is_tertiary_color_(bg_rgb)) role = 3;
  else if (is_primary_container_color_(bg_rgb)) role = 5;
  else if (is_outline_color_(bg_rgb)) role = 4;
  if (role == 255) {
    if (bg_rgb == this->previous_accent_) {
      role = 1;
    }
    for (uint8_t candidate = 0; role == 255 && candidate < 6; candidate++) {
      if (bg_rgb == this->role_color_for_(this->previous_accent_, candidate)) {
        role = candidate;
        break;
      }
    }
  }
  if (role != 255) {
    const uint32_t replacement = role_color_(role);
    lv_obj_set_style_bg_color(object, lv_color_hex(replacement), LV_PART_MAIN);
  }

  // Transparent labels can inherit a background style from their parent.
  // That style must not suppress recolouring the label's independent text.
  // Preserve contrast against the actual visible surface, and recognize the
  // previous generated colours so repeated palette changes remain reversible.
  const uint8_t text_surface = lv_obj_get_style_bg_opa(object, LV_PART_MAIN) == LV_OPA_TRANSP
                                   ? inherited_role
                                   : (role == 255 ? inherited_role : role);
  const bool primary_text = is_primary_color_(text_rgb) || text_rgb == this->previous_accent_;
  const bool secondary_text = is_secondary_color_(text_rgb) || is_tertiary_color_(text_rgb) ||
                              text_rgb == this->role_color_for_(this->previous_accent_, 2) ||
                              text_rgb == this->role_color_for_(this->previous_accent_, 3);
  const bool outline_text = is_outline_color_(text_rgb) ||
                            text_rgb == mix_(this->previous_accent_, 0xFFFFFF, 160);
  const bool neutral_text = is_on_surface_color_(text_rgb) || is_dark_role_text_(text_rgb);
  if (text_surface != 255 && text_surface != 0 &&
      (primary_text || secondary_text || outline_text || neutral_text)) {
    lv_obj_set_style_text_color(object, lv_color_hex(on_role_color_(text_surface)), LV_PART_MAIN);
  } else if (primary_text) {
    lv_obj_set_style_text_color(object, lv_color_hex(this->accent_color_), LV_PART_MAIN);
  } else if (secondary_text) {
    lv_obj_set_style_text_color(object, lv_color_hex(role_color_(2)), LV_PART_MAIN);
  } else if (outline_text) {
    lv_obj_set_style_text_color(object, lv_color_hex(mix_(this->accent_color_, 0xFFFFFF, 160)), LV_PART_MAIN);
  } else if (text_surface == 0 && neutral_text) {
    lv_obj_set_style_text_color(object, lv_color_hex(on_role_color_(0)), LV_PART_MAIN);
  }
  const uint32_t children = lv_obj_get_child_count(object);
  for (uint32_t i = 0; i < children; i++)
    this->apply_object_(lv_obj_get_child(object, static_cast<int32_t>(i)), text_surface);
}

void MaterialTheme::apply() {
  for (auto *root : this->roots_) this->apply_object_(root);
  ESP_LOGD(TAG, "Applied accent #%06X to %u LVGL roots", static_cast<unsigned>(this->accent_color_),
           static_cast<unsigned>(this->roots_.size()));
}

void MaterialTheme::dump_config() {
  ESP_LOGCONFIG(TAG, "Material runtime theme: %u roots, accent #%06X",
                static_cast<unsigned>(this->roots_.size()), static_cast<unsigned>(this->accent_color_));
}

}  // namespace esphome::lvgl_material
