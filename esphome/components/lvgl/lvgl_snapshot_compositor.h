#pragma once

#include "lvgl_snapshot_store.h"

#include <cstdint>
#include <vector>

namespace esphome::lvgl {

class LvglNavigation;
class LvglApplication;

class LvglSnapshotCompositor {
 public:
  LvglSnapshotCompositor(LvglComponent *parent, LvglSnapshotStore *store) : parent_(parent), store_(store) {}
  virtual ~LvglSnapshotCompositor() = default;

  virtual void add_home_page(LvPageType *page);
  virtual void add_home_view(lv_obj_t *view);
  virtual bool prepare_home(int page_index) { return false; }
  virtual bool prepare_applications(const std::vector<LvglApplication *> &applications);
  virtual bool is_home_prepared() const { return false; }
  virtual void loop() {}
  void set_navigation(LvglNavigation *navigation) { this->navigation_ = navigation; }
  void set_settle_duration(uint32_t duration) { this->settle_duration_ = duration; }
  void set_application_transitions_enabled(bool enabled) { this->application_transitions_enabled_ = enabled; }
  void set_application_open_duration(uint32_t duration) { this->application_open_duration_ = duration; }
  void set_application_close_duration(uint32_t duration) { this->application_close_duration_ = duration; }
  void set_application_start_ratio(float ratio) { this->application_start_ratio_ = ratio; }
  void set_application_close_target(float x_ratio, float y_ratio) {
    this->application_close_target_x_ = x_ratio;
    this->application_close_target_y_ = y_ratio;
  }

  virtual bool begin_home(int page_index);
  virtual bool update_home(int32_t delta_x);
  virtual bool settle_home(int target_index);
  virtual void cancel_home();
  virtual bool is_home_active() const { return this->home_active_; }

  virtual bool open_application(LvglApplication *application, int home_index);
  virtual bool begin_application_close(LvglApplication *application, int home_index);
  virtual bool update_application_close(int32_t delta_y);
  virtual bool settle_application_close(bool close);
  virtual void cancel_application();
  virtual bool is_application_active() const { return this->application_active_; }

 protected:
  struct Surface {
    lv_obj_t *image{};
    lv_obj_t *view{};
    lv_draw_buf_t *buffer{};
    int32_t origin_x{};
  };

  static void animation_exec_(void *var, int32_t value);
  static void animation_completed_(lv_anim_t *animation);
  static void application_animation_exec_(void *var, int32_t value);
  static void application_animation_completed_(lv_anim_t *animation);

  bool ensure_overlay_(lv_obj_t *view);
  bool bind_surface_(Surface &surface, lv_obj_t *view, int32_t origin_x);
  void release_surface_(Surface &surface);
  void set_home_offset_(int32_t offset);
  void complete_home_();
  void release_home_();
  bool bind_application_(LvglApplication *application, bool force_capture);
  void set_application_progress_(int32_t progress);
  bool animate_application_to_(int32_t progress, uint32_t duration);
  void complete_application_();
  void release_application_();

  LvglComponent *parent_{};
  LvglSnapshotStore *store_{};
  LvglNavigation *navigation_{};
  std::vector<lv_obj_t *> home_views_{};
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
  LvglApplication *application_{};
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
