#include "lvgl_direct_snapshot_compositor.h"

#include "lvgl_navigation.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#if LV_USE_SNAPSHOT && LV_USE_IMAGE

namespace esphome::lvgl {

static const char *const TAG = "lvgl.snapshot.direct";

bool LvglDirectSnapshotCompositor::can_use_direct_home_() const {
  if (this->parent_ == nullptr || this->home_views_.empty() || this->parent_->get_width() <= 0)
    return false;

  return std::all_of(this->home_views_.begin(), this->home_views_.end(),
                     [](lv_obj_t *view) { return view != nullptr && lv_obj_get_parent(view) != nullptr; });
}

bool LvglDirectSnapshotCompositor::can_use_direct_application_(LvglApplication *application, int home_index) const {
  return this->application_transitions_enabled_ && !this->home_active_ && !this->application_active_ &&
         application != nullptr && application->get_view() != nullptr && this->parent_ != nullptr &&
         this->parent_->get_width() > 0 && this->parent_->get_width() == this->parent_->get_height() &&
         home_index >= 0 && home_index < static_cast<int>(this->home_views_.size()) &&
         this->home_views_[home_index] != nullptr;
}

bool LvglDirectSnapshotCompositor::prepare_home(int page_index) {
  if (!this->can_use_direct_home_() || page_index < 0 || page_index >= static_cast<int>(this->home_views_.size())) {
    this->home_prepared_ = false;
    return false;
  }
  this->home_prepared_ =
      lvgl_esphome_snapshot_cache_tile_window(this->home_views_.data(), static_cast<int>(this->home_views_.size()),
                                              page_index + 1, this->parent_->get_width());
  return this->home_prepared_;
}

bool LvglDirectSnapshotCompositor::prepare_applications(
    const std::vector<LvglApplication *> &applications) {
  bool prepared = true;
  for (auto *application : applications) {
    auto *view = application == nullptr ? nullptr : application->get_view();
    if (view != nullptr)
      prepared = lvgl_esphome_snapshot_cache_compressed_page(view) && prepared;
  }
  return prepared;
}

bool LvglDirectSnapshotCompositor::begin_home(int page_index) {
  if (this->home_active_ || page_index < 0 || page_index >= static_cast<int>(this->home_views_.size()))
    return false;

  this->widget_fallback_ = false;
  if (!this->can_use_direct_home_()) {
    this->widget_fallback_ = true;
    return LvglSnapshotCompositor::begin_home(page_index);
  }

  this->current_index_ = page_index;
  this->target_index_ = page_index;
  this->home_offset_ = 0;
  this->home_active_ = true;
  this->direct_pending_ = true;
  this->direct_active_ = false;
  this->direct_edge_ = false;
  this->direct_neighbor_index_ = -1;
  this->direct_neighbor_origin_ = 0;
  return true;
}

bool LvglDirectSnapshotCompositor::start_direct_home_(int32_t delta_x) {
  if (!this->direct_pending_ || delta_x == 0)
    return this->direct_active_;

  const int direction = delta_x < 0 ? 1 : -1;
  const int candidate = this->current_index_ + direction;
  const int32_t width = this->parent_->get_width();
  bool started = false;

  if (candidate >= 0 && candidate < static_cast<int>(this->home_views_.size())) {
    this->direct_neighbor_index_ = candidate;
    this->direct_neighbor_origin_ = direction > 0 ? width : -width;
    started = lvgl_esphome_snapshot_swipe_begin(this->home_views_[this->current_index_], this->home_views_[candidate],
                                                width, this->direct_neighbor_origin_);
  } else {
    this->direct_edge_ = true;
    started = lvgl_esphome_snapshot_swipe_edge_begin(this->home_views_[this->current_index_], width);
  }

  this->direct_pending_ = false;
  this->direct_active_ = started;
  if (started)
    return true;

  lvgl_esphome_snapshot_swipe_end();
  auto *current = this->home_views_[this->current_index_];
  auto *current_parent = lv_obj_get_parent(current);
  if (current_parent != nullptr) {
    lv_obj_align(current, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(current, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(current_parent);
  }
  if (this->direct_neighbor_index_ >= 0) {
    auto *neighbor = this->home_views_[this->direct_neighbor_index_];
    if (lv_obj_get_parent(neighbor) != nullptr) {
      lv_obj_align(neighbor, LV_ALIGN_CENTER, this->direct_neighbor_origin_, 0);
      lv_obj_add_flag(neighbor, LV_OBJ_FLAG_HIDDEN);
    }
  }
  const int fallback_index = this->current_index_;
  this->direct_edge_ = false;
  this->direct_neighbor_index_ = -1;
  this->direct_neighbor_origin_ = 0;
  this->home_active_ = false;
  this->current_index_ = -1;
  this->target_index_ = -1;
  this->widget_fallback_ = true;
  ESP_LOGW(TAG, "Direct home compositor unavailable; using the LVGL widget backend");
  return LvglSnapshotCompositor::begin_home(fallback_index);
}

bool LvglDirectSnapshotCompositor::update_home(int32_t delta_x) {
  if (this->widget_fallback_)
    return LvglSnapshotCompositor::update_home(delta_x);
  if (!this->home_active_)
    return false;
  if (this->direct_pending_) {
    this->start_direct_home_(delta_x);
    if (this->widget_fallback_)
      return LvglSnapshotCompositor::update_home(delta_x);
  }
  if (!this->direct_active_)
    return false;

  const int32_t width = this->parent_->get_width();
  int32_t current_x = std::clamp(delta_x, -width, width);
  if (this->direct_edge_)
    current_x /= 2;
  const int32_t next_x = this->direct_edge_ ? 0 : this->direct_neighbor_origin_ + current_x;
  this->home_offset_ = current_x;
  lvgl_esphome_snapshot_swipe_request_update(current_x, next_x);
  return true;
}

bool LvglDirectSnapshotCompositor::settle_home(int target_index) {
  if (this->widget_fallback_)
    return LvglSnapshotCompositor::settle_home(target_index);
  if (!this->home_active_)
    return false;

  if (this->direct_pending_) {
    this->target_index_ = this->current_index_;
    this->reset_direct_home_();
    return true;
  }
  if (!this->direct_active_)
    return false;

  target_index = std::clamp(target_index, 0, static_cast<int>(this->home_views_.size()) - 1);
  const bool commit = !this->direct_edge_ && target_index == this->direct_neighbor_index_;
  this->target_index_ = commit ? target_index : this->current_index_;
  const int32_t width = this->parent_->get_width();
  const int32_t current_x = commit ? -this->direct_neighbor_origin_ : 0;
  const int32_t next_x = commit ? 0 : this->direct_neighbor_origin_;
  const uint32_t distance = static_cast<uint32_t>(std::abs(current_x - this->home_offset_));
  const uint32_t duration = std::max<uint32_t>(80, this->settle_duration_ * distance / std::max<int32_t>(1, width));
  lvgl_esphome_snapshot_swipe_request_finish(current_x, next_x, duration, commit);
  this->start_completion_timer_();
  return true;
}

void LvglDirectSnapshotCompositor::cancel_home() {
  if (this->widget_fallback_) {
    LvglSnapshotCompositor::cancel_home();
    this->widget_fallback_ = false;
    return;
  }
  if (this->direct_pending_ || this->direct_active_)
    lvgl_esphome_snapshot_swipe_end();
  this->reset_direct_home_();
}

bool LvglDirectSnapshotCompositor::open_application(LvglApplication *application, int home_index) {
  this->application_fallback_ = false;
  if (!this->can_use_direct_application_(application, home_index)) {
    this->application_fallback_ = true;
    return LvglSnapshotCompositor::open_application(application, home_index);
  }

  const int32_t width = this->parent_->get_width();
  if (!lvgl_esphome_snapshot_app_open(application->get_view(), this->home_views_[home_index], width,
                                      this->application_open_duration_)) {
    this->application_fallback_ = true;
    return LvglSnapshotCompositor::open_application(application, home_index);
  }

  this->application_ = application;
  this->application_home_index_ = home_index;
  this->application_opening_ = true;
  this->application_close_committed_ = false;
  this->application_active_ = true;
  this->direct_application_phase_ = DirectApplicationPhase::OPENING;
  this->start_completion_timer_();
  return true;
}

bool LvglDirectSnapshotCompositor::begin_application_close(LvglApplication *application, int home_index) {
  this->application_fallback_ = false;
  if (!this->can_use_direct_application_(application, home_index) ||
      !lvgl_esphome_snapshot_app_prepare_close(application->get_view())) {
    this->application_fallback_ = true;
    return LvglSnapshotCompositor::begin_application_close(application, home_index);
  }

  this->application_ = application;
  this->application_home_index_ = home_index;
  this->application_opening_ = false;
  this->application_close_committed_ = false;
  this->application_active_ = true;
  this->direct_application_phase_ = DirectApplicationPhase::PREPARED_CLOSE;
  return true;
}

bool LvglDirectSnapshotCompositor::update_application_close(int32_t delta_y) {
  if (this->application_fallback_)
    return LvglSnapshotCompositor::update_application_close(delta_y);
  return this->direct_application_phase_ == DirectApplicationPhase::PREPARED_CLOSE;
}

bool LvglDirectSnapshotCompositor::settle_application_close(bool close) {
  if (this->application_fallback_)
    return LvglSnapshotCompositor::settle_application_close(close);
  if (this->direct_application_phase_ != DirectApplicationPhase::PREPARED_CLOSE || this->application_ == nullptr)
    return false;

  if (!close) {
    auto *application = this->application_;
    lvgl_esphome_snapshot_app_clear_prepared_close();
    lvgl_esphome_snapshot_app_release_work_buffer();
    this->reset_direct_application_();
    if (this->navigation_ != nullptr)
      this->navigation_->complete_application_transition(application, false, false);
    else if (application != nullptr)
      application->call_on_close_cancelled_callbacks();
    return true;
  }

  const int32_t width = this->parent_->get_width();
  const int32_t height = this->parent_->get_height();
  const int32_t target_x = std::lround(width * this->application_close_target_x_);
  const int32_t target_y = std::lround(height * this->application_close_target_y_);
  if (!lvgl_esphome_snapshot_app_close(this->application_->get_view(), this->home_views_[this->application_home_index_],
                                       width, target_x, target_y, this->application_close_duration_)) {
    lvgl_esphome_snapshot_app_cancel();
    lvgl_esphome_snapshot_app_release_work_buffer();
    this->reset_direct_application_();
    return false;
  }

  this->application_close_committed_ = true;
  this->direct_application_phase_ = DirectApplicationPhase::CLOSING;
  this->start_completion_timer_();
  return true;
}

void LvglDirectSnapshotCompositor::cancel_application() {
  if (this->application_fallback_) {
    LvglSnapshotCompositor::cancel_application();
    this->application_fallback_ = false;
    return;
  }
  if (this->direct_application_phase_ != DirectApplicationPhase::NONE)
    lvgl_esphome_snapshot_app_cancel();
  lvgl_esphome_snapshot_app_release_work_buffer();
  this->reset_direct_application_();
}

void LvglDirectSnapshotCompositor::completion_timer_(lv_timer_t *timer) {
  auto *compositor = static_cast<LvglDirectSnapshotCompositor *>(lv_timer_get_user_data(timer));
  if (compositor == nullptr || lvgl_esphome_snapshot_is_active())
    return;
  if (compositor->direct_application_phase_ == DirectApplicationPhase::OPENING ||
      compositor->direct_application_phase_ == DirectApplicationPhase::CLOSING)
    compositor->complete_direct_application_();
  else
    compositor->complete_direct_home_();
}

void LvglDirectSnapshotCompositor::start_completion_timer_() {
  if (this->completion_timer_handle_ == nullptr)
    this->completion_timer_handle_ = lv_timer_create(completion_timer_, 8, this);
}

void LvglDirectSnapshotCompositor::complete_direct_home_() {
  if (this->completion_timer_handle_ != nullptr) {
    lv_timer_delete(this->completion_timer_handle_);
    this->completion_timer_handle_ = nullptr;
  }
  const int target = this->target_index_;
  this->reset_direct_home_();
  if (target >= 0)
    this->prepare_home(target);
}

void LvglDirectSnapshotCompositor::complete_direct_application_() {
  if (this->completion_timer_handle_ != nullptr) {
    lv_timer_delete(this->completion_timer_handle_);
    this->completion_timer_handle_ = nullptr;
  }

  auto *application = this->application_;
  const int home_index = this->application_home_index_;
  const bool opening = this->direct_application_phase_ == DirectApplicationPhase::OPENING;
  if (this->navigation_ != nullptr) {
    this->navigation_->prepare_application_transition(application, opening, !opening);
    if (!opening)
      this->navigation_->activate_home_view(home_index);
  }

  lvgl_esphome_snapshot_app_release_open_hold();
  lvgl_esphome_snapshot_app_release_work_buffer();

  if (this->navigation_ != nullptr)
    this->navigation_->complete_application_transition(application, opening, !opening);
  this->reset_direct_application_();
  if (!opening && home_index >= 0)
    this->prepare_home(home_index);
}

void LvglDirectSnapshotCompositor::reset_direct_home_() {
  if (this->completion_timer_handle_ != nullptr) {
    lv_timer_delete(this->completion_timer_handle_);
    this->completion_timer_handle_ = nullptr;
  }
  this->direct_pending_ = false;
  this->direct_active_ = false;
  this->direct_edge_ = false;
  this->direct_neighbor_index_ = -1;
  this->direct_neighbor_origin_ = 0;
  this->home_active_ = false;
  this->current_index_ = -1;
  this->target_index_ = -1;
  this->home_offset_ = 0;
  this->widget_fallback_ = false;
}

void LvglDirectSnapshotCompositor::reset_direct_application_() {
  if (this->completion_timer_handle_ != nullptr) {
    lv_timer_delete(this->completion_timer_handle_);
    this->completion_timer_handle_ = nullptr;
  }
  this->application_ = nullptr;
  this->application_buffer_ = nullptr;
  this->application_home_index_ = -1;
  this->application_progress_ = 0;
  this->application_active_ = false;
  this->application_opening_ = false;
  this->application_close_committed_ = false;
  this->direct_application_phase_ = DirectApplicationPhase::NONE;
  this->application_fallback_ = false;
}

}  // namespace esphome::lvgl

#endif
