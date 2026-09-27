#pragma once

#include "esphome/components/lvgl/direct_scene_controller.h"
#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/core/component.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#ifdef USE_ESP32
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace esphome::lvgl_material {

class MaterialDirectVolumeOverlay : public Component {
 public:
  static constexpr size_t MAX_SCENE_CONTROLLERS = 4;
  static constexpr int SCRATCH_BAND_ROWS = 128;

  explicit MaterialDirectVolumeOverlay(lvgl::LvglComponent *component) : lvgl_component_(component) {}

  void set_arc(lv_obj_t *arc) { this->arc_ = arc; }
  void set_knob(lv_obj_t *knob) { this->knob_ = knob; }
  void set_label(lv_obj_t *label) { this->label_ = label; }
  void set_activation_widget(lv_obj_t *widget) { this->activation_widget_ = widget; }
  void set_activation_widget_was_visible(bool visible) { this->activation_widget_was_visible_ = visible; }
  void set_scrim_opacity(lv_opa_t opacity) { this->scrim_opacity_ = opacity; }
  void add_scene_controller(lvgl::DirectSceneController *controller) {
    if (controller == nullptr)
      return;
    for (size_t index = 0; index < this->scene_controller_count_; index++) {
      if (this->scene_controllers_[index] == controller)
        return;
    }
    if (this->scene_controller_count_ < this->scene_controllers_.size())
      this->scene_controllers_[this->scene_controller_count_++] = controller;
  }

  void setup() override;
  void loop() override;
  void on_shutdown() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR - 5.0f; }

  bool prepare();
  bool begin(int value_pct = -1, int visual_pct = -1);
  void cancel_prepare();
  void end(bool restore_screen = true);
  bool is_active() const { return this->active_.load(std::memory_order_acquire); }

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
  bool ensure_persistent_resources_();
  void release_persistent_resources_();
  bool suspend_scene_controllers_();
  void resume_scene_controllers_();
  bool borrow_scene_frame_();
  void reset_visual_cache_();
  bool update_geometry_();
  bool rebuild_background_();
  bool compose_region_(lv_color_t *buffer, int stride, int origin_x, int origin_y, int width, int height,
                       int visual_pct, int value_pct);
  bool restore_background_region_(int x, int y, int width, int height);
  bool present_composed_region_(int x, int y, int width, int height, int visual_pct, int value_pct);
  void point_for_pct_(int pct, float &x, float &y) const;
  bool draw_value_into_region_(lv_color_t *buffer, int stride, int origin_x, int origin_y, int width, int height,
                               int value_pct);
  bool render_frame_(int visual_pct, int value_pct);
  bool direct_update_(int visual_pct, int value_pct);
  void update_native_(int value_pct, int visual_pct);
#ifdef USE_ESP32
  static void worker_(void *arg);
  bool ensure_worker_();
  void quiesce_worker_();
#endif
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
  lv_display_t *display_{nullptr};
  const lv_font_t *font_{nullptr};

  const lv_color_t *original_{nullptr};
  lv_color_t *background_{nullptr};
  lv_color_t *scratch_{nullptr};
  lv_color_t *activation_patch_{nullptr};
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
  int activation_patch_x_{0};
  int activation_patch_y_{0};
  int activation_patch_width_{0};
  int activation_patch_height_{0};
  size_t scratch_capacity_{0};
  size_t scratch_allocated_capacity_{0};
  int glyph_buffer_width_{0};
  int glyph_buffer_height_{0};
  size_t activation_patch_capacity_{0};
  size_t activation_patch_required_capacity_{0};
  bool activation_patch_valid_{false};
  bool original_owned_{false};
  display::FrameBufferLease background_lease_{};
  const uint8_t *reserved_original_frame_{nullptr};
  bool stable_background_{false};
  bool activation_widget_was_visible_{true};
  lv_opa_t scrim_opacity_{LV_OPA_70};
  lv_color_t inactive_color_{lv_color_hex(0x382949)};
  lv_color_t active_color_{lv_color_hex(0xF5EEFB)};
  lv_color_t knob_color_{lv_color_white()};
  VisualCache visual_cache_{};
  int visual_pct_{-1};
  int value_pct_{-1};
  bool prepared_{false};
  std::atomic<bool> active_{false};
  bool presented_{false};
  bool frame_buffer_presentation_active_{false};
  bool invalidation_suspended_{false};
  std::array<lvgl::DirectSceneController *, MAX_SCENE_CONTROLLERS> scene_controllers_{};
  size_t scene_controller_count_{0};
  bool scene_controllers_suspended_{false};
#ifdef USE_ESP32
  static constexpr int NO_PENDING_UPDATE = -1;
  std::atomic<int> pending_update_{NO_PENDING_UPDATE};
  std::atomic<int> last_requested_update_{NO_PENDING_UPDATE};
  std::atomic<bool> worker_busy_{false};
  std::atomic<bool> worker_failed_{false};
  std::atomic<uint32_t> pending_requested_us_{0};
  std::atomic<uint32_t> stat_requests_{0};
  std::atomic<uint32_t> stat_renders_{0};
  std::atomic<uint32_t> stat_coalesced_{0};
  std::atomic<uint32_t> stat_total_age_us_{0};
  std::atomic<uint32_t> stat_max_age_us_{0};
  std::atomic<uint32_t> stat_total_render_us_{0};
  std::atomic<uint32_t> stat_max_render_us_{0};
  TaskHandle_t worker_handle_{nullptr};
  StackType_t *worker_stack_{nullptr};
  StaticTask_t worker_storage_{};
#endif
};

}  // namespace esphome::lvgl_material
