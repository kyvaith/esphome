#pragma once

#include "lvgl_snapshot_compositor.h"

namespace esphome::lvgl {

class LvglDirectSnapshotCompositor final : public LvglSnapshotCompositor {
 public:
  LvglDirectSnapshotCompositor(LvglComponent *parent, LvglSnapshotStore *store)
      : LvglSnapshotCompositor(parent, store) {}

  bool prepare_home(int page_index) override;
  bool begin_home(int page_index) override;
  bool update_home(int32_t delta_x) override;
  bool settle_home(int target_index) override;
  void cancel_home() override;

 protected:
  static void completion_timer_(lv_timer_t *timer);

  bool can_use_direct_home_() const;
  bool start_direct_home_(int32_t delta_x);
  void start_completion_timer_();
  void complete_direct_home_();
  void reset_direct_home_();

  lv_timer_t *completion_timer_handle_{};
  int direct_neighbor_index_{-1};
  int32_t direct_neighbor_origin_{};
  bool direct_pending_{};
  bool direct_active_{};
  bool direct_edge_{};
  bool widget_fallback_{};
};

}  // namespace esphome::lvgl
