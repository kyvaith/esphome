#pragma once

#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/core/automation.h"

#include <cstddef>
#include <cstdint>

namespace esphome::lvgl {

class LvglScrollSnapshotController {
 public:
  LvglScrollSnapshotController(LvglComponent *parent, LvPageType *page, lv_obj_t *root)
      : parent_(parent), page_(page), root_(root) {}

  void set_preload(bool preload) { this->preload_ = preload; }
  void set_max_content_bytes(size_t max_content_bytes) { this->max_content_bytes_ = max_content_bytes; }
  void set_overscroll_ratio(float ratio) { this->overscroll_ratio_ = ratio; }
  void set_momentum_duration(uint32_t duration) { this->momentum_duration_ = duration; }
  void set_bounce_duration(uint32_t duration) { this->bounce_duration_ = duration; }
  void set_max_inertia_duration(uint32_t duration) { this->max_inertia_duration_ = duration; }

  void setup();
  bool prepare();
  bool refresh();
  void release();

  bool contains(int32_t x, int32_t y) const;
  void touch_begin(int32_t y);
  bool begin();
  void update(int32_t delta_y, int32_t touch_y, uint32_t now);
  void finish();
  void cancel();
  bool is_active() const { return this->active_; }

 protected:
  static void screen_event_cb_(lv_event_t *event);
  static void async_prepare_cb_(void *user_data);
  static void animation_exec_(void *var, int32_t value);
  static void animation_completed_(lv_anim_t *animation);

  bool capture_(lv_draw_buf_t **head, lv_draw_buf_t **tail, int32_t *tail_y, int32_t *content_height,
                int32_t *max_scroll_y);
  bool capture_segment_(int32_t segment_y, int32_t segment_height, lv_draw_buf_t **buffer);
  bool ensure_overlay_();
  void bind_images_();
  void set_visual_scroll_(int32_t scroll_y);
  int32_t resist_scroll_(int32_t scroll_y) const;
  void animate_to_(int32_t target, int32_t final, uint32_t duration, bool bounce);
  void complete_scroll_(int32_t scroll_y);
  void clear_buffers_();
  void hide_overlay_();

  LvglComponent *parent_{};
  LvPageType *page_{};
  lv_obj_t *root_{};
  lv_display_t *display_{};
  lv_obj_t *overlay_{};
  lv_obj_t *viewport_{};
  lv_obj_t *head_image_{};
  lv_obj_t *tail_image_{};
  lv_draw_buf_t *head_{};
  lv_draw_buf_t *tail_{};
  int32_t tail_y_{};
  int32_t viewport_width_{};
  int32_t viewport_height_{};
  int32_t content_height_{};
  int32_t max_scroll_y_{};
  int32_t start_scroll_y_{};
  int32_t visual_scroll_y_{};
  int32_t last_touch_y_{};
  uint32_t last_touch_ms_{};
  int32_t velocity_px_s_{};
  int32_t animation_final_y_{};
  bool preload_{true};
  bool prepare_pending_{};
  bool prepared_{};
  bool active_{};
  bool screen_active_{};
  bool root_was_hidden_{};
  bool bounce_pending_{};
  size_t max_content_bytes_{8 * 1024 * 1024};
  float overscroll_ratio_{0.15f};
  uint32_t momentum_duration_{560};
  uint32_t bounce_duration_{320};
  uint32_t max_inertia_duration_{900};
};

template<typename... Ts> class ScrollSnapshotPrepareAction final : public Action<Ts...> {
 public:
  explicit ScrollSnapshotPrepareAction(LvglScrollSnapshotController *controller) : controller_(controller) {}

 protected:
  void play(const Ts &...x) override { this->controller_->prepare(); }

  LvglScrollSnapshotController *controller_{};
};

template<typename... Ts> class ScrollSnapshotRefreshAction final : public Action<Ts...> {
 public:
  explicit ScrollSnapshotRefreshAction(LvglScrollSnapshotController *controller) : controller_(controller) {}

 protected:
  void play(const Ts &...x) override { this->controller_->refresh(); }

  LvglScrollSnapshotController *controller_{};
};

template<typename... Ts> class ScrollSnapshotReleaseAction final : public Action<Ts...> {
 public:
  explicit ScrollSnapshotReleaseAction(LvglScrollSnapshotController *controller) : controller_(controller) {}

 protected:
  void play(const Ts &...x) override { this->controller_->release(); }

  LvglScrollSnapshotController *controller_{};
};

}  // namespace esphome::lvgl
