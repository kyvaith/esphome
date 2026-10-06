#pragma once

#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"

#include <cstdint>
#include <vector>

namespace esphome::lvgl_material {

// Applies a bounded semantic palette to an existing LVGL tree.  This deliberately
// walks the already allocated widgets instead of creating a second render buffer
// or rebuilding the page.  It is generic: product YAML only supplies roots.
class MaterialTheme : public Component {
 public:
  void add_root(lv_obj_t *root) { this->roots_.push_back(root); }
  void set_accent(uint32_t accent) {
    this->previous_accent_ = this->accent_color_;
    this->accent_color_ = accent & 0xFFFFFFU;
  }
  uint32_t get_accent() const { return this->accent_color_; }
  void apply();
  void setup() override {}
  void dump_config() override;

 protected:
  static bool is_surface_color_(uint32_t color);
  static bool is_primary_color_(uint32_t color);
  static bool is_secondary_color_(uint32_t color);
  static bool is_tertiary_color_(uint32_t color);
  static bool is_primary_container_color_(uint32_t color);
  static bool is_on_surface_color_(uint32_t color);
  static bool is_dark_role_text_(uint32_t color);
  static bool is_outline_color_(uint32_t color);
  static uint32_t mix_(uint32_t a, uint32_t b, uint8_t amount);
  uint32_t role_color_(uint8_t role) const;
  uint32_t role_color_for_(uint32_t accent, uint8_t role) const;
  uint32_t on_role_color_(uint8_t role) const;
  void apply_object_(lv_obj_t *object, uint8_t inherited_role = 255);

  std::vector<lv_obj_t *> roots_;
  uint32_t accent_color_{0x36BCF0};
  uint32_t previous_accent_{0x36BCF0};
};

template<typename... Ts> class MaterialThemeApplyAction : public Action<Ts...> {
 public:
  explicit MaterialThemeApplyAction(MaterialTheme *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(uint32_t, accent)

 protected:
  void play(const Ts &...x) override {
    this->parent_->set_accent(this->accent_.value(x...));
    this->parent_->apply();
  }
  MaterialTheme *parent_;
};

}  // namespace esphome::lvgl_material
