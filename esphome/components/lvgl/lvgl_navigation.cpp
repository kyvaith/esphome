#include "lvgl_navigation.h"

#include "lvgl_esphome.h"
#include "lvgl_snapshot_compositor.h"

#include <algorithm>
#include <cmath>

namespace esphome::lvgl {

void LvglNavigation::add_home_page(LvPageType *page) {
  if (page != nullptr)
    this->home_pages_.push_back(page);
}

void LvglNavigation::add_application(LvglApplication *application) {
  if (application == nullptr)
    return;
  application->set_parent(this);
  this->applications_.push_back(application);
}

int LvglNavigation::find_home_page_index_() const {
  const size_t current_page = this->parent_->get_current_page();
  for (size_t index = 0; index < this->home_pages_.size(); index++) {
    if (this->home_pages_[index] != nullptr && this->home_pages_[index]->index == current_page)
      return static_cast<int>(index);
  }
  return -1;
}

LvglApplication *LvglNavigation::find_active_application_() const {
  const size_t current_page = this->parent_->get_current_page();
  for (auto *application : this->applications_) {
    if (application != nullptr && application->get_page() != nullptr && application->get_page()->index == current_page)
      return application;
  }
  return nullptr;
}

void LvglNavigation::touch_begin(int32_t x, int32_t y) {
  this->reset_touch_();

  if (auto *application = this->find_active_application_();
      application != nullptr && application->is_close_gesture_enabled()) {
    const int32_t height = this->parent_->get_height();
    const int32_t edge_start = height - static_cast<int32_t>(std::lround(height * this->close_edge_ratio_));
    if (y >= edge_start) {
      this->touch_context_ = TouchContext::APPLICATION_CLOSE;
      this->gesture_router_.begin(x, y, GestureAxis::VERTICAL);
    }
    return;
  }

  const int home_index = this->find_home_page_index_();
  if (home_index >= 0) {
    this->last_home_page_index_ = home_index;
    this->touch_context_ = TouchContext::HOME;
    this->gesture_router_.begin(x, y, GestureAxis::HORIZONTAL);
  }
}

bool LvglNavigation::touch_update(int32_t x, int32_t y) {
  if (this->touch_context_ == TouchContext::NONE)
    return false;
  const auto &sample = this->gesture_router_.update(x, y);
  if (this->touch_context_ == TouchContext::HOME && sample.captured && this->snapshot_compositor_ != nullptr) {
    if (sample.just_captured)
      this->snapshot_compositor_->begin_home(this->last_home_page_index_);
    if (this->snapshot_compositor_->is_home_active())
      this->snapshot_compositor_->update_home(sample.delta_x);
  }
  return sample.captured;
}

bool LvglNavigation::touch_end() {
  if (this->touch_context_ == TouchContext::NONE)
    return false;

  const TouchContext context = this->touch_context_;
  const GestureSample sample = this->gesture_router_.finish();
  this->touch_context_ = TouchContext::NONE;
  if (!sample.captured)
    return false;

  if (context == TouchContext::HOME && !this->home_pages_.empty()) {
    const int current = this->find_home_page_index_();
    int target = current;
    const int32_t threshold =
        std::max<int32_t>(1, static_cast<int32_t>(std::lround(this->parent_->get_width() * this->home_commit_ratio_)));
    if (current >= 0 && std::abs(sample.delta_x) >= threshold) {
      const int candidate = current + (sample.delta_x < 0 ? 1 : -1);
      if (candidate >= 0 && candidate < static_cast<int>(this->home_pages_.size()))
        target = candidate;
    }
    if (current >= 0 && target >= 0) {
      this->last_home_page_index_ = target;
      if (this->snapshot_compositor_ == nullptr || !this->snapshot_compositor_->settle_home(target))
        this->parent_->show_page(this->home_pages_[target]->index, LV_SCREEN_LOAD_ANIM_NONE, 0);
    }
  } else if (context == TouchContext::APPLICATION_CLOSE) {
    const int32_t threshold = std::max<int32_t>(
        1, static_cast<int32_t>(std::lround(this->parent_->get_height() * this->close_commit_ratio_)));
    if (sample.delta_y <= -threshold)
      this->close_application();
  }
  return true;
}

void LvglNavigation::touch_cancel() {
  if (this->snapshot_compositor_ != nullptr)
    this->snapshot_compositor_->cancel_home();
  this->reset_touch_();
}

void LvglNavigation::reset_touch_() {
  this->gesture_router_.cancel();
  this->touch_context_ = TouchContext::NONE;
}

void LvglNavigation::open_application(LvglApplication *application) {
  if (application == nullptr || application->get_page() == nullptr)
    return;
  if (this->snapshot_compositor_ != nullptr)
    this->snapshot_compositor_->cancel_home();
  if (const int home_index = this->find_home_page_index_(); home_index >= 0)
    this->last_home_page_index_ = home_index;
  this->parent_->show_page(application->get_page()->index, LV_SCREEN_LOAD_ANIM_NONE, 0);
}

void LvglNavigation::close_application() { this->show_home(); }

void LvglNavigation::show_home() {
  if (this->home_pages_.empty())
    return;
  if (this->snapshot_compositor_ != nullptr)
    this->snapshot_compositor_->cancel_home();
  const int target = std::clamp(this->last_home_page_index_, 0, static_cast<int>(this->home_pages_.size()) - 1);
  this->parent_->show_page(this->home_pages_[target]->index, LV_SCREEN_LOAD_ANIM_NONE, 0);
}

bool LvglNavigation::is_application_open(const LvglApplication *application) const {
  return application != nullptr && application == this->find_active_application_();
}

LvglApplication *LvglNavigation::get_active_application() const { return this->find_active_application_(); }

}  // namespace esphome::lvgl
