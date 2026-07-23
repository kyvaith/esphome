#pragma once

#include "lvgl_snapshot_store.h"

#include <cstdint>
#include <vector>

namespace esphome::lvgl {

class LvglSnapshotCompositor {
 public:
  LvglSnapshotCompositor(LvglComponent *parent, LvglSnapshotStore *store) : parent_(parent), store_(store) {}

  void add_home_page(LvPageType *page);
  void set_settle_duration(uint32_t duration) { this->settle_duration_ = duration; }
  void set_application_transitions_enabled(bool enabled) { this->application_transitions_enabled_ = enabled; }
  void set_application_open_duration(uint32_t duration) { this->application_open_duration_ = duration; }
  void set_application_close_duration(uint32_t duration) { this->application_close_duration_ = duration; }
  void set_application_start_ratio(float ratio) { this->application_start_ratio_ = ratio; }
  void set_application_close_target(float x_ratio, float y_ratio) {
    this->application_close_target_x_ = x_ratio;
    this->application_close_target_y_ = y_ratio;
  }

  bool begin_home(int page_index);
  bool update_home(int32_t delta_x);
  bool settle_home(int target_index);
  void cancel_home();
  bool is_home_active() const { return this->home_active_; }

  bool open_application(LvPageType *application, int home_index);
  bool begin_application_close(LvPageType *application, int home_index);
  bool update_application_close(int32_t delta_y);
  bool settle_application_close(bool close);
  void cancel_application();
  bool is_application_active() const { return this->application_active_; }

 protected:
  struct Surface {
    lv_obj_t *image{};
    LvPageType *page{};
    lv_draw_buf_t *buffer{};
    int32_t origin_x{};
  };

  static void animation_exec_(void *var, int32_t value);
  static void animation_completed_(lv_anim_t *animation);
  static void application_animation_exec_(void *var, int32_t value);
  static void application_animation_completed_(lv_anim_t *animation);

  bool ensure_overlay_(LvPageType *page);
  bool bind_surface_(Surface &surface, LvPageType *page, int32_t origin_x);
  void release_surface_(Surface &surface);
  void set_home_offset_(int32_t offset);
  void complete_home_();
  void release_home_();
  bool bind_application_(LvPageType *page, bool force_capture);
  void set_application_progress_(int32_t progress);
  bool animate_application_to_(int32_t progress, uint32_t duration);
  void complete_application_();
  void release_application_();

  LvglComponent *parent_{};
  LvglSnapshotStore *store_{};
  std::vector<LvPageType *> home_pages_{};
  lv_obj_t *overlay_{};
  Surface previous_{};
  Surface current_{};
  Surface next_{};
  lv_display_t *display_{};
  int current_index_{-1};
  int target_index_{-1};
  int32_t home_offset_{};
  uint32_t settle_duration_{220};
  bool home_active_{};
  lv_obj_t *application_mask_{};
  lv_obj_t *application_image_{};
  LvPageType *application_page_{};
  lv_draw_buf_t *application_buffer_{};
  int application_home_index_{-1};
  int32_t application_progress_{};
  uint32_t application_open_duration_{500};
  uint32_t application_close_duration_{500};
  float application_start_ratio_{0.01f};
  float application_close_target_x_{0.5f};
  float application_close_target_y_{0.75f};
  bool application_transitions_enabled_{};
  bool application_active_{};
  bool application_opening_{};
  bool application_close_committed_{};
};

}  // namespace esphome::lvgl
