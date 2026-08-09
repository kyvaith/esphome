#pragma once

#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/components/lvgl_material/material_direct_marquee.h"
#include "esphome/components/lvgl_material/material_direct_volume_overlay.h"
#include "esphome/components/lvgl_material/material_voice_assistant.h"
#include "esphome/components/lvgl_material/material_wavy_progress.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace esphome::lvgl_material {

class MaterialPressedStyle : public Component {
 public:
  void add_target(lv_obj_t *target) { this->targets_.push_back(target); }
  void set_pressed_opacity(lv_opa_t opacity) { this->pressed_opacity_ = opacity; }

  void setup() override;
  void on_shutdown() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR - 5.0f; }

 protected:
  std::vector<lv_obj_t *> targets_;
  lv_style_t style_{};
  lv_opa_t pressed_opacity_{LV_OPA_10};
  bool style_initialized_{false};
};

class MaterialDirectStateLayer : public Component {
 public:
  explicit MaterialDirectStateLayer(lvgl::LvglComponent *component) : lvgl_component_(component) {}
  void add_target(lv_obj_t *target) { this->targets_.push_back(target); }
  void set_pressed_opacity(lv_opa_t opacity) { this->pressed_opacity_ = opacity; }

  void setup() override;
  void on_shutdown() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR - 5.0f; }

  bool press(lv_obj_t *target);
  bool release();
  void abandon();

 protected:
  static void press_ready_cb_(void *arg);
  static void restore_ready_cb_(void *arg);
  static bool inside_rounded_rect_(int x, int y, int width, int height, int radius);
  bool allocate_buffers_();
  bool is_configured_target_(lv_obj_t *target) const;

  lvgl::LvglComponent *lvgl_component_{nullptr};
  std::vector<lv_obj_t *> targets_;
  uint8_t *normal_buffer_{nullptr};
  uint8_t *pressed_buffer_{nullptr};
  size_t buffer_capacity_{0};
  lv_opa_t pressed_opacity_{LV_OPA_10};
  int x_{0};
  int y_{0};
  int width_{0};
  int height_{0};
  bool active_{false};
  std::atomic<bool> press_in_flight_{false};
  std::atomic<bool> restore_in_flight_{false};
};

class MaterialStateLayer : public Component {
 public:
  void set_target(lv_obj_t *target) { this->target_ = target; }
  void set_color(lv_color_t color) { this->color_ = color; }
  void set_pressed_opacity(lv_opa_t opacity) { this->pressed_opacity_ = opacity; }
  void set_durations(uint32_t enter_duration, uint32_t exit_duration) {
    this->enter_duration_ = enter_duration;
    this->exit_duration_ = exit_duration;
  }

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR - 5.0f; }

 protected:
  static void target_event_cb_(lv_event_t *event);
  static void opacity_animation_cb_(void *object, int32_t value);
  void animate_to_(lv_opa_t opacity, uint32_t duration);
  void sync_geometry_();

  lv_obj_t *target_{nullptr};
  lv_obj_t *layer_{nullptr};
  lv_color_t color_{lv_color_black()};
  lv_opa_t pressed_opacity_{LV_OPA_10};
  uint32_t enter_duration_{80};
  uint32_t exit_duration_{120};
};

class MaterialPageIndicator : public Component {
 public:
  void set_container(lv_obj_t *container) { this->container_ = container; }
  void set_count(uint8_t count) { this->count_ = count; }
  void set_initial_page(uint8_t page) { this->active_page_ = page; }
  void set_geometry(uint8_t active_size, uint8_t inactive_size, uint8_t thickness, uint8_t gap) {
    this->active_size_ = active_size;
    this->inactive_size_ = inactive_size;
    this->thickness_ = thickness;
    this->gap_ = gap;
  }
  void set_colors(lv_color_t active_color, lv_color_t inactive_color) {
    this->active_color_ = active_color;
    this->inactive_color_ = inactive_color;
  }
  void set_transition_duration(uint32_t duration) { this->transition_duration_ = duration; }

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR - 5.0f; }
  void set_active_page(uint16_t page, bool animated = true);

 protected:
  struct IndicatorItem {
    MaterialPageIndicator *owner{nullptr};
    lv_obj_t *object{nullptr};
    int32_t width{0};
  };

  static void size_animation_cb_(void *object, int32_t value);
  void apply_active_page_(bool animated);
  void layout_items_();

  lv_obj_t *container_{nullptr};
  lv_obj_t *root_{nullptr};
  IndicatorItem items_[32]{};
  lv_color_t active_color_{lv_color_white()};
  lv_color_t inactive_color_{lv_color_hex(0x777777)};
  uint32_t transition_duration_{160};
  uint8_t count_{1};
  uint8_t active_page_{0};
  uint8_t active_size_{24};
  uint8_t inactive_size_{8};
  uint8_t thickness_{8};
  uint8_t gap_{8};
};

template<typename... Ts>
class MaterialPageIndicatorSetAction : public Action<Ts...>, public Parented<MaterialPageIndicator> {
 public:
  TEMPLATABLE_VALUE(uint16_t, value)

 protected:
  void play(const Ts &...x) override { this->parent_->set_active_page(this->value_.value(x...), true); }
};

}  // namespace esphome::lvgl_material
