#pragma once

#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

#include <cstdint>
#include <array>
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
  template<typename F> void add_on_context_press_callback(F &&callback) {
    this->context_press_callback_.add(std::forward<F>(callback));
  }
  template<typename F> void add_on_control_press_callback(F &&callback) {
    this->control_press_callback_.add(std::forward<F>(callback));
  }
#ifdef USE_LVGL_MATERIAL_ENTITY_CONTROL_TILE_ROUTING
  template<typename F> void add_on_entity_control_press_callback(F &&callback) {
    this->entity_control_press_callback_.add(std::forward<F>(callback));
    this->entity_control_callback_registered_ = true;
  }
  void set_number_controls_enabled(bool enabled);
#endif
  // A recognized control marker is always passive on the ordinary callbacks.
  // Product policy explicitly opts into the separate synchronous control route.
  void set_control_press_enabled(bool enabled);
  // Gate a new action identity until its snapshot is refreshed. Cosmetic/state
  // updates still report visual pending, but must not stall the same action.
  void set_require_snapshot_ready(bool required) { this->require_snapshot_ready_ = required; }
  // Product policy may opt into live ordinary-action availability. With the
  // option absent, the existing presentation and input behavior stay intact.
  void set_action_availability_enabled(bool enabled) {
#ifdef USE_LVGL_MATERIAL_TILE_ACTION_AVAILABILITY
    this->action_availability_enabled_ = enabled;
#else
    (void) enabled;
#endif
  }
  uint8_t get_press_enabled_version() const {
#ifdef USE_LVGL_MATERIAL_TILE_ACTION_AVAILABILITY
    return this->action_availability_enabled_ ? 1 : 0;
#else
    return 0;
#endif
  }

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR - 5.0f; }

  bool configure(const std::string &payload);
  // An explicit product accent takes precedence over wire colors. Validation
  // of the complete payload still finishes before any presentation changes.
  bool configure(const std::string &payload, uint32_t accent_override);
  void set_accent(uint32_t accent);
  uint32_t get_last_changed_slots() const { return this->last_changed_slots_; }
  uint64_t get_configured_revision() const { return this->configured_revision_; }
  uint8_t get_context_version() const { return this->context_version_; }
  uint32_t get_pending_slots() const;
  uint32_t get_pending_context_slots() const;
  // Call immediately after a successful snapshot refresh on the LVGL task,
  // without yielding to another configuration update between refresh and mark.
  void mark_slots_ready(uint32_t mask);
  bool serialize_presentation(std::string &output, size_t max_bytes = 32768) const;

 protected:
  struct TileBinding {
    MaterialTileSurface *owner{nullptr};
    lv_obj_t *widget{nullptr};
    lv_obj_t *icon{nullptr};
    lv_obj_t *title{nullptr};
    lv_obj_t *subtitle{nullptr};
    uint8_t slot{0};
    // Keep the semantic palette role supplied by Home Assistant.  Accent
    // changes must preserve that role instead of deriving it from the slot
    // number, because the product deliberately mixes roles on each page.
    uint8_t role{3};
    bool always_hidden{false};
    std::array<char, 65> card_id{};
    std::array<char, 65> pressed_card_id{};
    uint64_t revision{0};
    uint64_t pressed_revision{0};
    uint32_t generation{0};
    uint32_t context_generation{0};
    uint32_t pressed_context_generation{0};
    bool context{false};
    bool pressed_context{false};
    bool press_valid{false};
    bool press_seen{false};
    bool snapshot_ready{true};
    bool context_ready{true};
    bool control_light{false};
    bool pressed_control_light{false};
#ifdef USE_LVGL_MATERIAL_ENTITY_CONTROL_TILE_ROUTING
    // The legacy control_light bit remains the passive-control gate for both
    // supported kinds. This captured tag prevents Number falling into Light.
    bool control_number{false};
    bool pressed_control_number{false};
#endif
#ifdef USE_LVGL_MATERIAL_TILE_ACTION_AVAILABILITY
    // One byte preserves optional-field/disabled-state ownership; absence is
    // the legacy enabled policy. The input belongs to this captured press only.
    uint8_t press_availability{2};
    lv_indev_t *pressed_indev{nullptr};
#endif
  };

  static void tile_event_cb_(lv_event_t *event);
  bool configure_(const std::string &payload, const uint32_t *accent_override);
  TileBinding *find_tile_(uint8_t slot);
  void mark_changed_(TileBinding &tile);
  void mark_context_changed_(TileBinding &tile);
  bool can_press_(const TileBinding &tile) const;
#ifdef USE_LVGL_MATERIAL_TILE_ACTION_AVAILABILITY
  void update_press_availability_(TileBinding &tile, bool present, bool enabled);
  void cancel_available_press_(TileBinding &tile);
#endif
  uint32_t role_background_(uint8_t role) const;
  uint32_t role_foreground_(uint8_t role) const;
  static uint8_t role_from_name_(const char *name);

  std::vector<TileBinding> tiles_;
  LazyCallbackManager<void(uint8_t)> press_callback_;
  LazyCallbackManager<void(uint8_t, const char *, uint64_t)> context_press_callback_;
  LazyCallbackManager<void(uint8_t, const char *, uint64_t)> control_press_callback_;
#ifdef USE_LVGL_MATERIAL_ENTITY_CONTROL_TILE_ROUTING
  LazyCallbackManager<void(uint8_t, const char *, uint64_t, uint8_t)> entity_control_press_callback_;
  bool entity_control_callback_registered_{false};
  bool number_controls_enabled_{false};
#endif
  uint32_t last_changed_slots_{0};
  uint32_t accent_color_{0x36BCF0};
  uint64_t configured_revision_{0};
  uint8_t context_version_{0};
  bool require_snapshot_ready_{false};
  bool control_press_enabled_{false};
#ifdef USE_LVGL_MATERIAL_TILE_ACTION_AVAILABILITY
  bool action_availability_enabled_{false};
#endif
};

template<typename... Ts> class MaterialTileSurfaceConfigureAction : public Action<Ts...> {
 public:
  explicit MaterialTileSurfaceConfigureAction(MaterialTileSurface *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(std::string, payload)
  TEMPLATABLE_VALUE(uint32_t, accent_override)

 protected:
  void play(const Ts &...x) override {
    if (this->accent_override_.has_value())
      this->parent_->configure(this->payload_.value(x...), this->accent_override_.value(x...));
    else
      this->parent_->configure(this->payload_.value(x...));
  }
  MaterialTileSurface *parent_;
};

}  // namespace esphome::lvgl_material
