#pragma once

#include "lvgl_snapshot_compositor.h"

namespace esphome::lvgl {

class LvglDirectSnapshotCompositor final : public LvglSnapshotCompositor {
 public:
  LvglDirectSnapshotCompositor(LvglComponent *parent, LvglSnapshotStore *store)
      : LvglSnapshotCompositor(parent, store) {}

  bool prepare_home(int page_index) override;
  bool prepare_applications(const std::vector<LvglApplication *> &applications) override;
  bool is_home_prepared() const override { return this->home_prepared_; }
  void loop() override;
  bool begin_home(int page_index) override;
  bool take_over_home(int *page_index, int32_t *offset_x) override;
  bool update_home(int32_t delta_x) override;
  bool settle_home(int target_index, int32_t release_velocity_px_s = 0) override;
  void cancel_home() override;
  bool open_application(LvglApplication *application, int home_index) override;
  bool begin_application_close(LvglApplication *application, int home_index) override;
  bool update_application_close(int32_t delta_y) override;
  bool settle_application_close(bool close) override;
  void cancel_application() override;

 protected:
  enum class DirectApplicationPhase : uint8_t {
    NONE,
    PREPARED_CLOSE,
    OPENING,
    CLOSING,
  };

  bool can_use_direct_home_() const;
  bool can_use_direct_application_(LvglApplication *application, int home_index) const;
  bool start_direct_home_(int32_t delta_x);
  bool rebase_direct_home_(int new_current_index, int direction, int32_t current_x);
  void complete_direct_home_();
  void complete_direct_application_();
  void reset_direct_home_();
  void reset_direct_application_();

  int direct_neighbor_index_{-1};
  int32_t direct_neighbor_origin_{};
  int32_t gesture_input_shift_{};
  bool direct_pending_{};
  bool direct_active_{};
  bool direct_edge_{};
  bool widget_fallback_{};
  bool home_prepared_{};
  DirectApplicationPhase direct_application_phase_{DirectApplicationPhase::NONE};
  bool application_fallback_{};
};

}  // namespace esphome::lvgl
