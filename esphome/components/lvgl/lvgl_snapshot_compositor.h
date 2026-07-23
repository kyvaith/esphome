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

  bool begin_home(int page_index);
  bool update_home(int32_t delta_x);
  bool settle_home(int target_index);
  void cancel_home();
  bool is_home_active() const { return this->home_active_; }

 protected:
  struct Surface {
    lv_obj_t *image{};
    LvPageType *page{};
    lv_draw_buf_t *buffer{};
    int32_t origin_x{};
  };

  static void animation_exec_(void *var, int32_t value);
  static void animation_completed_(lv_anim_t *animation);

  bool ensure_overlay_(LvPageType *page);
  bool bind_surface_(Surface &surface, LvPageType *page, int32_t origin_x);
  void release_surface_(Surface &surface);
  void set_home_offset_(int32_t offset);
  void complete_home_();
  void release_home_();

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
};

}  // namespace esphome::lvgl
