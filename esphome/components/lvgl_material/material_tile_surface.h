#pragma once

#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace esphome::lvgl_material {

class MaterialTileSurface : public Component {
 public:
  void add_tile(uint8_t slot, lv_obj_t *widget, lv_obj_t *icon, lv_obj_t *title, lv_obj_t *subtitle,
                bool always_hidden = false);

  template<typename F> void add_on_press_callback(F &&callback) {
    this->press_callback_.add(std::forward<F>(callback));
  }

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR - 5.0f; }

  bool configure(const std::string &payload);
  uint32_t get_last_changed_slots() const { return this->last_changed_slots_; }

 protected:
  struct TileBinding {
    MaterialTileSurface *owner{nullptr};
    lv_obj_t *widget{nullptr};
    lv_obj_t *icon{nullptr};
    lv_obj_t *title{nullptr};
    lv_obj_t *subtitle{nullptr};
    uint8_t slot{0};
    bool always_hidden{false};
  };

  static void tile_event_cb_(lv_event_t *event);
  TileBinding *find_tile_(uint8_t slot);

  std::vector<TileBinding> tiles_;
  LazyCallbackManager<void(uint8_t)> press_callback_;
  uint32_t last_changed_slots_{0};
};

template<typename... Ts> class MaterialTileSurfaceConfigureAction : public Action<Ts...> {
 public:
  explicit MaterialTileSurfaceConfigureAction(MaterialTileSurface *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(std::string, payload)

 protected:
  void play(const Ts &...x) override { this->parent_->configure(this->payload_.value(x...)); }
  MaterialTileSurface *parent_;
};

}  // namespace esphome::lvgl_material
