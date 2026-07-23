#include "lvgl_snapshot_compositor.h"

#include <algorithm>
#include <cstdlib>

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

void LvglSnapshotCompositor::animation_exec_(void *var, int32_t value) {
  static_cast<LvglSnapshotCompositor *>(var)->set_home_offset_(value);
}

void LvglSnapshotCompositor::animation_completed_(lv_anim_t *animation) {
  static_cast<LvglSnapshotCompositor *>(lv_anim_get_user_data(animation))->complete_home_();
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
    lv_image_set_src(surface.image, nullptr);
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

}  // namespace esphome::lvgl
