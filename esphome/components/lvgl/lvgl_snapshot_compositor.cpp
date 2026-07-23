#include "lvgl_snapshot_compositor.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#if LV_USE_SNAPSHOT && LV_USE_IMAGE

namespace esphome::lvgl {

void LvglSnapshotCompositor::add_home_page(LvPageType *page) {
  if (page != nullptr)
    this->home_pages_.push_back(page);
}

bool LvglSnapshotCompositor::begin_home(int page_index) {
  if (this->home_active_ || page_index < 0 || page_index >= static_cast<int>(this->home_pages_.size()))
    return false;

  auto *page = this->home_pages_[page_index];
  if (!this->ensure_overlay_(page) || !this->bind_surface_(this->current_, page, 0))
    return false;

  const int32_t width = this->parent_->get_width();
  if (page_index > 0 && !this->bind_surface_(this->previous_, this->home_pages_[page_index - 1], -width)) {
    this->release_home_();
    return false;
  }
  if (page_index + 1 < static_cast<int>(this->home_pages_.size()) &&
      !this->bind_surface_(this->next_, this->home_pages_[page_index + 1], width)) {
    this->release_home_();
    return false;
  }

  this->current_index_ = page_index;
  this->target_index_ = page_index;
  this->home_offset_ = 0;
  this->home_active_ = true;
  this->set_home_offset_(0);
  lv_obj_remove_flag(this->overlay_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(this->overlay_);
  return true;
}

bool LvglSnapshotCompositor::update_home(int32_t delta_x) {
  if (!this->home_active_)
    return false;

  const int32_t width = this->parent_->get_width();
  delta_x = std::clamp(delta_x, -width, width);
  if ((this->current_index_ == 0 && delta_x > 0) ||
      (this->current_index_ + 1 == static_cast<int>(this->home_pages_.size()) && delta_x < 0))
    delta_x /= 2;
  this->set_home_offset_(delta_x);
  return true;
}

bool LvglSnapshotCompositor::settle_home(int target_index) {
  if (!this->home_active_)
    return false;
  if (target_index < 0 || target_index >= static_cast<int>(this->home_pages_.size()))
    target_index = this->current_index_;

  this->target_index_ = target_index;
  int32_t target_offset = 0;
  if (target_index > this->current_index_)
    target_offset = -this->parent_->get_width();
  else if (target_index < this->current_index_)
    target_offset = this->parent_->get_width();

  if (target_offset == this->home_offset_) {
    this->complete_home_();
    return true;
  }

  lv_anim_delete(this, animation_exec_);
  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, this);
  lv_anim_set_user_data(&animation, this);
  lv_anim_set_values(&animation, this->home_offset_, target_offset);
  const uint32_t distance = std::abs(target_offset - this->home_offset_);
  const uint32_t width = std::max<int32_t>(1, this->parent_->get_width());
  const uint32_t duration = std::max<uint32_t>(80, this->settle_duration_ * distance / width);
  lv_anim_set_duration(&animation, duration);
  lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&animation, animation_exec_);
  lv_anim_set_completed_cb(&animation, animation_completed_);
  lv_anim_start(&animation);
  return true;
}

void LvglSnapshotCompositor::cancel_home() {
  lv_anim_delete(this, animation_exec_);
  this->release_home_();
}

bool LvglSnapshotCompositor::open_application(LvPageType *application, int home_index) {
  if (!this->application_transitions_enabled_ || this->home_active_ || this->application_active_ ||
      application == nullptr || home_index < 0 || home_index >= static_cast<int>(this->home_pages_.size()))
    return false;
  if (!this->ensure_overlay_(application) || !this->bind_application_(application, false))
    return false;

  this->application_home_index_ = home_index;
  this->application_opening_ = true;
  this->application_close_committed_ = false;
  this->application_active_ = true;
  this->set_application_progress_(0);
  lv_obj_remove_flag(this->overlay_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(this->application_mask_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(this->overlay_);
  if (this->display_ != nullptr)
    lv_refr_now(this->display_);
  return this->animate_application_to_(1000, this->application_open_duration_);
}

bool LvglSnapshotCompositor::begin_application_close(LvPageType *application, int home_index) {
  if (!this->application_transitions_enabled_ || this->home_active_ || this->application_active_ ||
      application == nullptr || home_index < 0 || home_index >= static_cast<int>(this->home_pages_.size()))
    return false;
  if (!this->ensure_overlay_(application) || !this->bind_application_(application, true))
    return false;

  this->application_home_index_ = home_index;
  this->application_opening_ = false;
  this->application_close_committed_ = false;
  this->application_active_ = true;
  this->set_application_progress_(1000);
  lv_obj_remove_flag(this->overlay_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(this->application_mask_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(this->overlay_);
  if (this->display_ != nullptr)
    lv_refr_now(this->display_);
  this->parent_->show_page(this->home_pages_[home_index]->index, LV_SCREEN_LOAD_ANIM_NONE, 0);
  return true;
}

bool LvglSnapshotCompositor::update_application_close(int32_t delta_y) {
  if (!this->application_active_ || this->application_opening_)
    return false;
  const int32_t height = std::max<int32_t>(1, this->parent_->get_height());
  const int32_t progress = 1000 + std::clamp<int32_t>(delta_y, -height, 0) * 1000 / height;
  this->set_application_progress_(progress);
  return true;
}

bool LvglSnapshotCompositor::settle_application_close(bool close) {
  if (!this->application_active_ || this->application_opening_)
    return false;
  this->application_close_committed_ = close;
  const int32_t target = close ? 0 : 1000;
  const uint32_t distance = std::abs(target - this->application_progress_);
  const uint32_t duration = std::max<uint32_t>(80, this->application_close_duration_ * distance / 1000);
  return this->animate_application_to_(target, duration);
}

void LvglSnapshotCompositor::cancel_application() {
  lv_anim_delete(this, application_animation_exec_);
  this->release_application_();
}

void LvglSnapshotCompositor::animation_exec_(void *var, int32_t value) {
  static_cast<LvglSnapshotCompositor *>(var)->set_home_offset_(value);
}

void LvglSnapshotCompositor::animation_completed_(lv_anim_t *animation) {
  static_cast<LvglSnapshotCompositor *>(lv_anim_get_user_data(animation))->complete_home_();
}

void LvglSnapshotCompositor::application_animation_exec_(void *var, int32_t value) {
  static_cast<LvglSnapshotCompositor *>(var)->set_application_progress_(value);
}

void LvglSnapshotCompositor::application_animation_completed_(lv_anim_t *animation) {
  static_cast<LvglSnapshotCompositor *>(lv_anim_get_user_data(animation))->complete_application_();
}

bool LvglSnapshotCompositor::ensure_overlay_(LvPageType *page) {
  if (this->overlay_ != nullptr)
    return true;
  if (page == nullptr || page->obj == nullptr)
    return false;

  this->display_ = lv_obj_get_display(page->obj);
  auto *layer = lv_display_get_layer_top(this->display_);
  if (layer == nullptr)
    return false;

  this->overlay_ = lv_obj_create(layer);
  lv_obj_remove_style_all(this->overlay_);
  lv_obj_set_pos(this->overlay_, 0, 0);
  lv_obj_set_size(this->overlay_, this->parent_->get_width(), this->parent_->get_height());
  lv_obj_add_flag(this->overlay_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(this->overlay_, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_add_flag(this->overlay_, LV_OBJ_FLAG_FLOATING);
  lv_obj_clear_flag(this->overlay_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(this->overlay_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_clip_corner(this->overlay_, true, LV_PART_MAIN);

  for (auto *surface : {&this->previous_, &this->current_, &this->next_}) {
    surface->image = lv_image_create(this->overlay_);
    lv_obj_add_flag(surface->image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(surface->image, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(surface->image, LV_OBJ_FLAG_FLOATING);
    lv_obj_clear_flag(surface->image, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(surface->image, LV_OBJ_FLAG_CLICKABLE);
  }

  this->application_mask_ = lv_obj_create(this->overlay_);
  lv_obj_remove_style_all(this->application_mask_);
  lv_obj_add_flag(this->application_mask_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(this->application_mask_, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_add_flag(this->application_mask_, LV_OBJ_FLAG_FLOATING);
  lv_obj_clear_flag(this->application_mask_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(this->application_mask_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(this->application_mask_, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_clip_corner(this->application_mask_, true, LV_PART_MAIN);

  this->application_image_ = lv_image_create(this->application_mask_);
  lv_obj_add_flag(this->application_image_, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_add_flag(this->application_image_, LV_OBJ_FLAG_FLOATING);
  lv_obj_clear_flag(this->application_image_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(this->application_image_, LV_OBJ_FLAG_CLICKABLE);
  lv_image_set_inner_align(this->application_image_, LV_IMAGE_ALIGN_STRETCH);
  return true;
}

bool LvglSnapshotCompositor::bind_surface_(Surface &surface, LvPageType *page, int32_t origin_x) {
  if (page == nullptr || surface.image == nullptr)
    return false;

  auto *buffer = this->store_->acquire(page);
  if (buffer == nullptr && this->store_->capture(page))
    buffer = this->store_->acquire(page);
  if (buffer == nullptr)
    return false;

  surface.page = page;
  surface.buffer = buffer;
  surface.origin_x = origin_x;
  lv_image_set_src(surface.image, buffer);
  lv_obj_set_pos(surface.image, origin_x, 0);
  lv_obj_remove_flag(surface.image, LV_OBJ_FLAG_HIDDEN);
  return true;
}

void LvglSnapshotCompositor::release_surface_(Surface &surface) {
  if (surface.image != nullptr) {
    lv_obj_add_flag(surface.image, LV_OBJ_FLAG_HIDDEN);
    ::lv_image_set_src(surface.image, nullptr);
  }
  if (surface.page != nullptr && surface.buffer != nullptr)
    this->store_->release(surface.page);
  surface.page = nullptr;
  surface.buffer = nullptr;
  surface.origin_x = 0;
}

void LvglSnapshotCompositor::set_home_offset_(int32_t offset) {
  this->home_offset_ = offset;
  for (auto *surface : {&this->previous_, &this->current_, &this->next_}) {
    if (surface->buffer != nullptr)
      lv_obj_set_x(surface->image, surface->origin_x + offset);
  }
}

void LvglSnapshotCompositor::complete_home_() {
  if (!this->home_active_)
    return;

  const int target = std::clamp(this->target_index_, 0, static_cast<int>(this->home_pages_.size()) - 1);
  this->parent_->show_page(this->home_pages_[target]->index, LV_SCREEN_LOAD_ANIM_NONE, 0);
  lv_obj_add_flag(this->overlay_, LV_OBJ_FLAG_HIDDEN);
  if (this->display_ != nullptr)
    lv_refr_now(this->display_);
  this->release_home_();
}

void LvglSnapshotCompositor::release_home_() {
  this->release_surface_(this->previous_);
  this->release_surface_(this->current_);
  this->release_surface_(this->next_);
  if (this->overlay_ != nullptr)
    lv_obj_add_flag(this->overlay_, LV_OBJ_FLAG_HIDDEN);
  this->current_index_ = -1;
  this->target_index_ = -1;
  this->home_offset_ = 0;
  this->home_active_ = false;
}

bool LvglSnapshotCompositor::bind_application_(LvPageType *page, bool force_capture) {
  if (page == nullptr || this->application_image_ == nullptr)
    return false;
  if (force_capture && !this->store_->capture(page))
    return false;

  auto *buffer = this->store_->acquire(page);
  if (buffer == nullptr && this->store_->capture(page))
    buffer = this->store_->acquire(page);
  if (buffer == nullptr)
    return false;

  this->application_page_ = page;
  this->application_buffer_ = buffer;
  lv_image_set_src(this->application_image_, buffer);
  return true;
}

void LvglSnapshotCompositor::set_application_progress_(int32_t progress) {
  this->application_progress_ = std::clamp<int32_t>(progress, 0, 1000);
  if (this->application_mask_ == nullptr || this->application_image_ == nullptr)
    return;

  const int32_t width = std::max<int32_t>(1, this->parent_->get_width());
  const int32_t height = std::max<int32_t>(1, this->parent_->get_height());
  const int32_t start_width = std::max<int32_t>(1, std::lround(width * this->application_start_ratio_));
  const int32_t start_height = std::max<int32_t>(1, std::lround(height * this->application_start_ratio_));
  const int32_t frame_width = start_width + (width - start_width) * this->application_progress_ / 1000;
  const int32_t frame_height = start_height + (height - start_height) * this->application_progress_ / 1000;

  const int32_t screen_center_x = width / 2;
  const int32_t screen_center_y = height / 2;
  int32_t target_x = screen_center_x;
  int32_t target_y = screen_center_y;
  if (!this->application_opening_) {
    target_x = std::lround(width * this->application_close_target_x_);
    target_y = std::lround(height * this->application_close_target_y_);
  }
  const int32_t center_x = target_x + (screen_center_x - target_x) * this->application_progress_ / 1000;
  const int32_t center_y = target_y + (screen_center_y - target_y) * this->application_progress_ / 1000;

  lv_obj_set_pos(this->application_mask_, center_x - frame_width / 2, center_y - frame_height / 2);
  lv_obj_set_size(this->application_mask_, frame_width, frame_height);
  lv_obj_set_style_radius(this->application_mask_,
                          std::min(frame_width, frame_height) * (1000 - this->application_progress_) / 2000,
                          LV_PART_MAIN);
  lv_obj_set_pos(this->application_image_, 0, 0);
  lv_obj_set_size(this->application_image_, frame_width, frame_height);
}

bool LvglSnapshotCompositor::animate_application_to_(int32_t progress, uint32_t duration) {
  if (!this->application_active_)
    return false;
  progress = std::clamp<int32_t>(progress, 0, 1000);
  if (progress == this->application_progress_) {
    this->complete_application_();
    return true;
  }

  lv_anim_delete(this, application_animation_exec_);
  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, this);
  lv_anim_set_user_data(&animation, this);
  lv_anim_set_values(&animation, this->application_progress_, progress);
  lv_anim_set_duration(&animation, std::max<uint32_t>(1, duration));
  lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&animation, application_animation_exec_);
  lv_anim_set_completed_cb(&animation, application_animation_completed_);
  lv_anim_start(&animation);
  return true;
}

void LvglSnapshotCompositor::complete_application_() {
  if (!this->application_active_)
    return;

  if (this->application_opening_ || !this->application_close_committed_)
    this->parent_->show_page(this->application_page_->index, LV_SCREEN_LOAD_ANIM_NONE, 0);
  lv_obj_add_flag(this->application_mask_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(this->overlay_, LV_OBJ_FLAG_HIDDEN);
  if (this->display_ != nullptr)
    lv_refr_now(this->display_);
  this->release_application_();
}

void LvglSnapshotCompositor::release_application_() {
  if (this->application_mask_ != nullptr)
    lv_obj_add_flag(this->application_mask_, LV_OBJ_FLAG_HIDDEN);
  if (this->application_image_ != nullptr)
    ::lv_image_set_src(this->application_image_, nullptr);
  if (this->application_page_ != nullptr && this->application_buffer_ != nullptr)
    this->store_->release(this->application_page_);
  this->application_page_ = nullptr;
  this->application_buffer_ = nullptr;
  this->application_home_index_ = -1;
  this->application_progress_ = 0;
  this->application_active_ = false;
  this->application_opening_ = false;
  this->application_close_committed_ = false;
}

}  // namespace esphome::lvgl

#endif
