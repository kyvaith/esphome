#pragma once

#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/core/component.h"

#include <cstddef>
#include <cstdint>

namespace esphome::lvgl_material {

class MaterialDirectVolumeOverlay : public Component {
 public:
  explicit MaterialDirectVolumeOverlay(lvgl::LvglComponent *component) : lvgl_component_(component) {}

  void set_arc(lv_obj_t *arc) { this->arc_ = arc; }
  void set_knob(lv_obj_t *knob) { this->knob_ = knob; }
  void set_label(lv_obj_t *label) { this->label_ = label; }
  void set_activation_widget(lv_obj_t *widget) { this->activation_widget_ = widget; }
  void set_scrim_opacity(lv_opa_t opacity) { this->scrim_opacity_ = opacity; }

  void setup() override;
  void on_shutdown() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR - 5.0f; }

  bool prepare();
  bool begin();
  void cancel_prepare();
  void end(bool restore_screen = true);
  bool is_active() const { return this->active_; }

  void update(int value_pct, int visual_pct);
  void update_drag_point(int value_pct, int visual_pct, int touch_x, int touch_y);

  int arc_pct_from_x(int touch_x) const;
  int arc_pct_from_point(int touch_x, int touch_y) const;
  int drag_value_from_x(int touch_x, int start_x, int start_pct) const;
  int drag_value_from_visual_pct(int visual_pct, int start_pct) const;
  int knob_pct_from_x(int touch_x) const { return this->arc_pct_from_x(touch_x); }

  static int clamp_pct(int pct);

 protected:
  struct VisualCache {
    int value_pct{-1};
    int visual_pct{-1};
  };

  void release_();
  void reset_visual_cache_();
  bool update_geometry_();
  bool rebuild_background_();
  void point_for_pct_(int pct, float &x, float &y) const;
  bool direct_draw_value_(int value_pct);
  bool direct_update_(int visual_pct, int value_pct);
  void update_native_(int value_pct, int visual_pct);
  void redraw_track_in_region_(lv_color_t *buffer, int stride, int origin_x, int origin_y, int width, int height,
                               int first_pct, int last_pct, lv_color_t color);

  static float coverage_sq_(float distance_sq, float radius);
  static void blend_(lv_color_t &dst, lv_color_t color, float coverage);
  static void draw_capsule_(lv_color_t *buffer, int stride, int origin_x, int origin_y, int width, int height, float ax,
                            float ay, float bx, float by, float radius, lv_color_t color);
  static void draw_disc_(lv_color_t *buffer, int stride, int origin_x, int origin_y, int width, int height, float cx,
                         float cy, float radius, lv_color_t color);

  lvgl::LvglComponent *lvgl_component_{nullptr};
  lv_obj_t *arc_{nullptr};
  lv_obj_t *knob_{nullptr};
  lv_obj_t *label_{nullptr};
  lv_obj_t *activation_widget_{nullptr};
  const lv_font_t *font_{nullptr};

  lv_color_t *original_{nullptr};
  lv_color_t *background_{nullptr};
  lv_color_t *scratch_{nullptr};
  lv_draw_buf_t *glyph_buffer_{nullptr};

  float point_x_[101]{};
  float point_y_[101]{};
  float center_x_{0.0f};
  float center_y_{0.0f};
  float arc_radius_{0.0f};
  float track_radius_{0.0f};
  float knob_radius_{0.0f};
  int screen_width_{0};
  int screen_height_{0};
  int capture_height_{0};
  int letter_space_{0};
  int label_x_{0};
  int label_y_{0};
  int label_width_{0};
  int label_height_{0};
  size_t scratch_capacity_{0};
  lv_opa_t scrim_opacity_{LV_OPA_70};
  lv_color_t inactive_color_{lv_color_hex(0x382949)};
  lv_color_t active_color_{lv_color_hex(0xF5EEFB)};
  lv_color_t knob_color_{lv_color_white()};
  VisualCache visual_cache_{};
  int visual_pct_{-1};
  int value_pct_{-1};
  bool prepared_{false};
  bool active_{false};
  bool presented_{false};
};

}  // namespace esphome::lvgl_material
