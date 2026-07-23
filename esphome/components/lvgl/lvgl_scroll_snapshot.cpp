#include "lvgl_scroll_snapshot.h"

#include "esphome/core/log.h"

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdlib>

#if LV_USE_SNAPSHOT && LV_USE_IMAGE

namespace esphome::lvgl {

static const char *const TAG = "lvgl.snapshot_scroll";

void LvglScrollSnapshotController::setup() {
  if (this->page_ == nullptr || this->page_->obj == nullptr || this->root_ == nullptr)
    return;

  lv_obj_add_event_cb(this->page_->obj, screen_event_cb_, LV_EVENT_SCREEN_LOADED, this);
  lv_obj_add_event_cb(this->page_->obj, screen_event_cb_, LV_EVENT_SCREEN_UNLOADED, this);
  this->screen_active_ = this->page_->is_showing();
  if (this->screen_active_ && this->preload_) {
    this->prepare_pending_ = true;
    lv_async_call(async_prepare_cb_, this);
  }
}

bool LvglScrollSnapshotController::prepare() {
  this->prepare_pending_ = false;
  if (this->active_ || !this->screen_active_)
    return false;

  lv_draw_buf_t *head = nullptr;
  lv_draw_buf_t *tail = nullptr;
  int32_t tail_y = 0;
  int32_t content_height = 0;
  int32_t max_scroll_y = 0;
  if (!this->capture_(&head, &tail, &tail_y, &content_height, &max_scroll_y))
    return false;

  this->clear_buffers_();
  this->head_ = head;
  this->tail_ = tail;
  this->tail_y_ = tail_y;
  this->content_height_ = content_height;
  this->max_scroll_y_ = max_scroll_y;
  this->prepared_ = true;
  ESP_LOGD(TAG, "Prepared %" PRId32 "x%" PRId32 " scroll snapshot using %u segment(s)", this->viewport_width_,
           this->content_height_, this->tail_ == nullptr ? 1U : 2U);
  return true;
}

bool LvglScrollSnapshotController::refresh() {
  if (this->active_)
    return false;
  return this->prepare();
}

void LvglScrollSnapshotController::release() {
  this->prepare_pending_ = false;
  lv_anim_delete(this, animation_exec_);
  if (this->active_)
    this->complete_scroll_(std::clamp<int32_t>(this->visual_scroll_y_, 0, this->max_scroll_y_));
  this->clear_buffers_();
}

bool LvglScrollSnapshotController::contains(int32_t x, int32_t y) const {
  if (this->root_ == nullptr || !lv_obj_is_visible(this->root_))
    return false;
  lv_area_t area;
  lv_obj_get_coords(this->root_, &area);
  return x >= area.x1 && x <= area.x2 && y >= area.y1 && y <= area.y2;
}

void LvglScrollSnapshotController::touch_begin(int32_t y) {
  this->last_touch_y_ = y;
  this->last_touch_ms_ = millis();
  this->velocity_px_s_ = 0;
}

bool LvglScrollSnapshotController::begin() {
  if (this->active_ || this->root_ == nullptr)
    return false;
  if (!this->prepared_ && !this->prepare())
    return false;
  if (!this->ensure_overlay_())
    return false;

  this->start_scroll_y_ = std::clamp<int32_t>(lv_obj_get_scroll_y(this->root_), 0, this->max_scroll_y_);
  this->visual_scroll_y_ = this->start_scroll_y_;
  this->root_was_hidden_ = lv_obj_has_flag(this->root_, LV_OBJ_FLAG_HIDDEN);
  this->bind_images_();
  this->set_visual_scroll_(this->visual_scroll_y_);
  lv_obj_remove_flag(this->overlay_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(this->overlay_);
  if (this->display_ != nullptr)
    lv_refr_now(this->display_);
  lv_obj_add_flag(this->root_, LV_OBJ_FLAG_HIDDEN);
  this->active_ = true;
  return true;
}

void LvglScrollSnapshotController::update(int32_t delta_y, int32_t touch_y, uint32_t now) {
  if (!this->active_)
    return;

  const uint32_t elapsed = now - this->last_touch_ms_;
  if (elapsed > 0 && elapsed <= 100) {
    const int32_t instantaneous = -((touch_y - this->last_touch_y_) * 1000) / static_cast<int32_t>(elapsed);
    this->velocity_px_s_ = (this->velocity_px_s_ * 2 + instantaneous) / 3;
  }
  this->last_touch_y_ = touch_y;
  this->last_touch_ms_ = now;
  this->set_visual_scroll_(this->resist_scroll_(this->start_scroll_y_ - delta_y));
}

void LvglScrollSnapshotController::finish() {
  if (!this->active_)
    return;

  const int32_t velocity = std::clamp<int32_t>(this->velocity_px_s_, -4200, 4200);
  const int32_t travel = std::clamp<int32_t>(static_cast<int64_t>(velocity) * this->momentum_duration_ / 1000,
                                             -this->viewport_height_ * 2, this->viewport_height_ * 2);
  const int32_t raw_target = this->visual_scroll_y_ + travel;
  const int32_t final = std::clamp<int32_t>(raw_target, 0, this->max_scroll_y_);
  const int32_t overscroll = std::max<int32_t>(1, std::lround(this->viewport_height_ * this->overscroll_ratio_));
  const bool bounce = this->visual_scroll_y_ < 0 || this->visual_scroll_y_ > this->max_scroll_y_ || raw_target < 0 ||
                      raw_target > this->max_scroll_y_;
  const int32_t target = bounce ? std::clamp(raw_target, -overscroll, this->max_scroll_y_ + overscroll) : final;
  const uint32_t distance = std::abs(target - this->visual_scroll_y_);
  if (!bounce && distance < 8 && std::abs(velocity) < 80) {
    this->complete_scroll_(final);
    return;
  }

  const uint32_t duration =
      std::clamp<uint32_t>(220U + distance / 2U, 220U, std::max<uint32_t>(220U, this->max_inertia_duration_));
  this->animate_to_(target, final, duration, bounce && target != final);
}

void LvglScrollSnapshotController::cancel() {
  if (!this->active_)
    return;
  this->velocity_px_s_ = 0;
  const int32_t target = std::clamp<int32_t>(this->visual_scroll_y_, 0, this->max_scroll_y_);
  this->animate_to_(target, target, this->bounce_duration_, false);
}

void LvglScrollSnapshotController::screen_event_cb_(lv_event_t *event) {
  auto *self = static_cast<LvglScrollSnapshotController *>(lv_event_get_user_data(event));
  if (self == nullptr)
    return;

  if (lv_event_get_code(event) == LV_EVENT_SCREEN_LOADED) {
    self->screen_active_ = true;
    if (self->preload_ && !self->prepared_ && !self->prepare_pending_) {
      self->prepare_pending_ = true;
      lv_async_call(async_prepare_cb_, self);
    }
  } else if (lv_event_get_code(event) == LV_EVENT_SCREEN_UNLOADED) {
    self->screen_active_ = false;
    self->release();
  }
}

void LvglScrollSnapshotController::async_prepare_cb_(void *user_data) {
  auto *self = static_cast<LvglScrollSnapshotController *>(user_data);
  if (self != nullptr && self->screen_active_ && self->preload_ && !self->prepared_)
    self->prepare();
  else if (self != nullptr)
    self->prepare_pending_ = false;
}

void LvglScrollSnapshotController::animation_exec_(void *var, int32_t value) {
  static_cast<LvglScrollSnapshotController *>(var)->set_visual_scroll_(value);
}

void LvglScrollSnapshotController::animation_completed_(lv_anim_t *animation) {
  auto *self = static_cast<LvglScrollSnapshotController *>(lv_anim_get_user_data(animation));
  if (self == nullptr)
    return;
  if (self->bounce_pending_) {
    self->bounce_pending_ = false;
    self->animate_to_(self->animation_final_y_, self->animation_final_y_, self->bounce_duration_, false);
    return;
  }
  self->complete_scroll_(self->animation_final_y_);
}

bool LvglScrollSnapshotController::capture_(lv_draw_buf_t **head, lv_draw_buf_t **tail, int32_t *tail_y,
                                            int32_t *content_height, int32_t *max_scroll_y) {
#if LV_USE_SNAPSHOT
  if (this->root_ == nullptr)
    return false;

  auto *parent = lv_obj_get_parent(this->root_);
  lv_obj_update_layout(parent == nullptr ? this->root_ : parent);
  this->viewport_width_ = lv_obj_get_width(this->root_);
  this->viewport_height_ = lv_obj_get_height(this->root_);
  if (this->viewport_width_ <= 0 || this->viewport_height_ <= 0)
    return false;

  const int32_t old_scroll_y = lv_obj_get_scroll_y(this->root_);
  const int32_t old_height = lv_obj_get_height(this->root_);
  const bool was_hidden = lv_obj_has_flag(this->root_, LV_OBJ_FLAG_HIDDEN);
  const int32_t maximum_scroll =
      std::max<int32_t>(0, lv_obj_get_scroll_top(this->root_) + lv_obj_get_scroll_bottom(this->root_));
  const int32_t full_height = this->viewport_height_ + maximum_scroll;
#if LV_COLOR_DEPTH == 16
  constexpr lv_color_format_t color_format = LV_COLOR_FORMAT_RGB565;
#else
  constexpr lv_color_format_t color_format = LV_COLOR_FORMAT_RGB888;
#endif
  const size_t stride = lv_draw_buf_width_to_stride(this->viewport_width_, color_format);
  if (stride == 0 || static_cast<size_t>(full_height) > this->max_content_bytes_ / stride) {
    ESP_LOGW(TAG, "Scroll snapshot requires %u bytes, limit is %u", static_cast<unsigned>(stride * full_height),
             static_cast<unsigned>(this->max_content_bytes_));
    return false;
  }

  lv_obj_stop_scroll_anim(this->root_);
  lv_obj_remove_flag(this->root_, LV_OBJ_FLAG_HIDDEN);

  bool success = this->capture_segment_(0, full_height, head);
  int32_t split = 0;
  if (!success && full_height > this->viewport_height_) {
    if (*head != nullptr) {
      lv_draw_buf_destroy(*head);
      *head = nullptr;
    }
    split = (full_height + 1) / 2;
    success = this->capture_segment_(0, split, head) && this->capture_segment_(split, full_height - split, tail);
  }

  lv_obj_set_height(this->root_, old_height);
  lv_obj_scroll_to_y(this->root_, old_scroll_y, LV_ANIM_OFF);
  lv_obj_update_layout(parent == nullptr ? this->root_ : parent);
  if (was_hidden)
    lv_obj_add_flag(this->root_, LV_OBJ_FLAG_HIDDEN);

  if (!success) {
    if (*head != nullptr) {
      lv_draw_buf_destroy(*head);
      *head = nullptr;
    }
    if (*tail != nullptr) {
      lv_draw_buf_destroy(*tail);
      *tail = nullptr;
    }
    ESP_LOGW(TAG, "Failed to capture scroll content");
    return false;
  }

  *tail_y = split;
  *content_height = full_height;
  *max_scroll_y = maximum_scroll;
  return true;
#else
  return false;
#endif
}

bool LvglScrollSnapshotController::capture_segment_(int32_t segment_y, int32_t segment_height, lv_draw_buf_t **buffer) {
  if (segment_height <= 0 || buffer == nullptr)
    return false;
  auto *parent = lv_obj_get_parent(this->root_);
  lv_obj_set_height(this->root_, segment_height);
  lv_obj_update_layout(parent == nullptr ? this->root_ : parent);
  lv_obj_scroll_to_y(this->root_, segment_y, LV_ANIM_OFF);
  lv_obj_update_layout(parent == nullptr ? this->root_ : parent);
#if LV_COLOR_DEPTH == 16
  *buffer = lv_snapshot_take(this->root_, LV_COLOR_FORMAT_RGB565);
#else
  *buffer = lv_snapshot_take(this->root_, LV_COLOR_FORMAT_RGB888);
#endif
  if (*buffer != nullptr && (*buffer)->data != nullptr)
    return true;
  if (*buffer != nullptr) {
    lv_draw_buf_destroy(*buffer);
    *buffer = nullptr;
  }
  return false;
}

bool LvglScrollSnapshotController::ensure_overlay_() {
  if (this->overlay_ != nullptr)
    return true;
  if (this->root_ == nullptr)
    return false;

  this->display_ = lv_obj_get_display(this->root_);
  auto *layer = this->display_ == nullptr ? nullptr : lv_display_get_layer_top(this->display_);
  if (layer == nullptr)
    return false;

  this->overlay_ = lv_obj_create(layer);
  lv_obj_remove_style_all(this->overlay_);
  lv_obj_set_pos(this->overlay_, 0, 0);
  lv_obj_set_size(this->overlay_, this->parent_->get_width(), this->parent_->get_height());
  lv_obj_add_flag(this->overlay_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(this->overlay_, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_add_flag(this->overlay_, LV_OBJ_FLAG_FLOATING);
  lv_obj_clear_flag(this->overlay_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(this->overlay_, LV_OBJ_FLAG_SCROLLABLE);

  this->viewport_ = lv_obj_create(this->overlay_);
  lv_obj_remove_style_all(this->viewport_);
  lv_obj_add_flag(this->viewport_, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_add_flag(this->viewport_, LV_OBJ_FLAG_FLOATING);
  lv_obj_clear_flag(this->viewport_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(this->viewport_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_clip_corner(this->viewport_, true, LV_PART_MAIN);

  for (auto **image : {&this->head_image_, &this->tail_image_}) {
    *image = lv_image_create(this->viewport_);
    lv_obj_add_flag(*image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(*image, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(*image, LV_OBJ_FLAG_FLOATING);
    lv_obj_clear_flag(*image, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(*image, LV_OBJ_FLAG_SCROLLABLE);
    lv_image_set_inner_align(*image, LV_IMAGE_ALIGN_TOP_LEFT);
  }
  return true;
}

void LvglScrollSnapshotController::bind_images_() {
  lv_area_t area;
  lv_obj_get_coords(this->root_, &area);
  lv_obj_set_pos(this->viewport_, area.x1, area.y1);
  lv_obj_set_size(this->viewport_, this->viewport_width_, this->viewport_height_);
  lv_obj_set_style_bg_color(this->viewport_, lv_obj_get_style_bg_color(this->root_, LV_PART_MAIN), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(this->viewport_, lv_obj_get_style_bg_opa(this->root_, LV_PART_MAIN), LV_PART_MAIN);
  lv_obj_set_style_radius(this->viewport_, lv_obj_get_style_radius(this->root_, LV_PART_MAIN), LV_PART_MAIN);

  lv_image_set_src(this->head_image_, this->head_);
  lv_obj_set_size(this->head_image_, this->head_->header.w, this->head_->header.h);
  lv_obj_remove_flag(this->head_image_, LV_OBJ_FLAG_HIDDEN);
  if (this->tail_ != nullptr) {
    lv_image_set_src(this->tail_image_, this->tail_);
    lv_obj_set_size(this->tail_image_, this->tail_->header.w, this->tail_->header.h);
    lv_obj_remove_flag(this->tail_image_, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(this->tail_image_, LV_OBJ_FLAG_HIDDEN);
    lv_image_set_src(this->tail_image_, nullptr);
  }
}

void LvglScrollSnapshotController::set_visual_scroll_(int32_t scroll_y) {
  this->visual_scroll_y_ = scroll_y;
  if (this->head_image_ != nullptr)
    lv_obj_set_pos(this->head_image_, 0, -scroll_y);
  if (this->tail_image_ != nullptr && this->tail_ != nullptr)
    lv_obj_set_pos(this->tail_image_, 0, this->tail_y_ - scroll_y);
}

int32_t LvglScrollSnapshotController::resist_scroll_(int32_t scroll_y) const {
  const int32_t limit = std::max<int32_t>(1, std::lround(this->viewport_height_ * this->overscroll_ratio_));
  if (scroll_y < 0)
    return -std::min(limit, (-scroll_y + 2) / 3);
  if (scroll_y > this->max_scroll_y_)
    return this->max_scroll_y_ + std::min(limit, (scroll_y - this->max_scroll_y_ + 2) / 3);
  return scroll_y;
}

void LvglScrollSnapshotController::animate_to_(int32_t target, int32_t final, uint32_t duration, bool bounce) {
  lv_anim_delete(this, animation_exec_);
  this->animation_final_y_ = final;
  this->bounce_pending_ = bounce;
  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, this);
  lv_anim_set_user_data(&animation, this);
  lv_anim_set_values(&animation, this->visual_scroll_y_, target);
  lv_anim_set_duration(&animation, std::max<uint32_t>(1, duration));
  lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&animation, animation_exec_);
  lv_anim_set_completed_cb(&animation, animation_completed_);
  lv_anim_start(&animation);
}

void LvglScrollSnapshotController::complete_scroll_(int32_t scroll_y) {
  if (this->root_ == nullptr) {
    this->active_ = false;
    this->hide_overlay_();
    return;
  }

  scroll_y = std::clamp<int32_t>(scroll_y, 0, this->max_scroll_y_);
  lv_obj_scroll_to_y(this->root_, scroll_y, LV_ANIM_OFF);
  if (!this->root_was_hidden_)
    lv_obj_remove_flag(this->root_, LV_OBJ_FLAG_HIDDEN);
  if (this->display_ != nullptr)
    lv_refr_now(this->display_);
  this->hide_overlay_();
  if (this->display_ != nullptr)
    lv_refr_now(this->display_);
  this->visual_scroll_y_ = scroll_y;
  this->active_ = false;
  this->bounce_pending_ = false;
}

void LvglScrollSnapshotController::clear_buffers_() {
  if (this->head_ != nullptr) {
    lv_draw_buf_destroy(this->head_);
    this->head_ = nullptr;
  }
  if (this->tail_ != nullptr) {
    lv_draw_buf_destroy(this->tail_);
    this->tail_ = nullptr;
  }
  this->prepared_ = false;
  this->tail_y_ = 0;
  this->content_height_ = 0;
  this->max_scroll_y_ = 0;
}

void LvglScrollSnapshotController::hide_overlay_() {
  if (this->overlay_ != nullptr)
    lv_obj_add_flag(this->overlay_, LV_OBJ_FLAG_HIDDEN);
  if (this->head_image_ != nullptr)
    lv_image_set_src(this->head_image_, nullptr);
  if (this->tail_image_ != nullptr)
    lv_image_set_src(this->tail_image_, nullptr);
}

}  // namespace esphome::lvgl

#endif
