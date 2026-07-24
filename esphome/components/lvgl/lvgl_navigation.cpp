#include "lvgl_navigation.h"

#include "lvgl_esphome.h"

#if LV_USE_SNAPSHOT && LV_USE_IMAGE
#include "lvgl_scroll_snapshot.h"
#include "lvgl_snapshot_compositor.h"
#endif

#include <algorithm>
#include <cmath>

namespace esphome::lvgl {

void LvglNavigation::add_home_page(LvPageType *page) {
  if (page != nullptr)
    this->home_pages_.push_back(page);
}

void LvglNavigation::add_home_widget(lv_obj_t *widget) {
  if (widget == nullptr)
    return;
  if (this->home_widgets_.empty())
    lv_obj_remove_flag(widget, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(widget, LV_OBJ_FLAG_HIDDEN);
  this->home_widgets_.push_back(widget);
}

void LvglNavigation::add_application(LvglApplication *application) {
  if (application == nullptr)
    return;
  application->set_parent(this);
  if (application->get_widget() != nullptr)
    lv_obj_add_flag(application->get_widget(), LV_OBJ_FLAG_HIDDEN);
  this->applications_.push_back(application);
}

void LvglNavigation::set_snapshot_compositor(LvglSnapshotCompositor *compositor) {
  this->snapshot_compositor_ = compositor;
#if LV_USE_SNAPSHOT && LV_USE_IMAGE
  if (compositor != nullptr)
    compositor->set_navigation(this);
#endif
}

int LvglNavigation::find_home_view_index_() const {
  const size_t current_page = this->parent_->get_current_page();
  if (this->home_widget_page_ != nullptr && this->home_widget_page_->index == current_page) {
    for (size_t index = 0; index < this->home_widgets_.size(); index++) {
      if (this->home_widgets_[index] != nullptr && !lv_obj_has_flag(this->home_widgets_[index], LV_OBJ_FLAG_HIDDEN))
        return static_cast<int>(index);
    }
    if (!this->home_widgets_.empty())
      return std::clamp(this->last_home_page_index_, 0, static_cast<int>(this->home_widgets_.size()) - 1);
  }
  for (size_t index = 0; index < this->home_pages_.size(); index++) {
    if (this->home_pages_[index] != nullptr && this->home_pages_[index]->index == current_page)
      return static_cast<int>(index);
  }
  return -1;
}

LvglApplication *LvglNavigation::find_active_application_() const {
  if (this->active_application_ != nullptr)
    return this->active_application_;
  const size_t current_page = this->parent_->get_current_page();
  for (auto *application : this->applications_) {
    if (application != nullptr && !application->is_widget_application() && application->get_page() != nullptr &&
        application->get_page()->index == current_page)
      return application;
  }
  return nullptr;
}

void LvglNavigation::touch_begin(int32_t x, int32_t y) {
  this->reset_touch_();

  if (auto *application = this->find_active_application_(); application != nullptr) {
    const int32_t height = this->parent_->get_height();
    const int32_t edge_start = height - static_cast<int32_t>(std::lround(height * this->close_edge_ratio_));
    if (application->is_close_gesture_enabled() && y >= edge_start) {
      this->gesture_application_ = application;
      this->touch_context_ = TouchContext::APPLICATION_CLOSE;
      this->gesture_router_.begin(x, y, GestureAxis::VERTICAL);
      return;
    }
#if LV_USE_SNAPSHOT && LV_USE_IMAGE
    if (auto *scroll = application->get_scroll_snapshot(); scroll != nullptr && scroll->contains(x, y)) {
      this->gesture_application_ = application;
      this->touch_context_ = TouchContext::APPLICATION_SCROLL;
      scroll->touch_begin(y);
      this->gesture_router_.begin(x, y, GestureAxis::VERTICAL);
    }
#endif
    return;
  }

  const int home_index = this->find_home_view_index_();
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
#if LV_USE_SNAPSHOT && LV_USE_IMAGE
  if (this->touch_context_ == TouchContext::HOME && sample.captured && this->snapshot_compositor_ != nullptr) {
    if (sample.just_captured)
      this->snapshot_compositor_->begin_home(this->last_home_page_index_);
    if (this->snapshot_compositor_->is_home_active())
      this->snapshot_compositor_->update_home(sample.delta_x);
  } else if (this->touch_context_ == TouchContext::APPLICATION_CLOSE && sample.captured &&
             this->snapshot_compositor_ != nullptr) {
    if (sample.just_captured && this->gesture_application_ != nullptr)
      this->snapshot_compositor_->begin_application_close(this->gesture_application_, this->last_home_page_index_);
    if (this->snapshot_compositor_->is_application_active())
      this->snapshot_compositor_->update_application_close(sample.delta_y);
  } else if (this->touch_context_ == TouchContext::APPLICATION_SCROLL && sample.captured &&
             this->gesture_application_ != nullptr) {
    auto *scroll = this->gesture_application_->get_scroll_snapshot();
    if (scroll != nullptr) {
      if (sample.just_captured && !scroll->begin()) {
        this->reset_touch_();
        return false;
      }
      if (scroll->is_active())
        scroll->update(sample.delta_y, y, millis());
    }
  }
#endif
  return sample.captured;
}

bool LvglNavigation::touch_end() {
  if (this->touch_context_ == TouchContext::NONE)
    return false;

  const TouchContext context = this->touch_context_;
  auto *gesture_application = this->gesture_application_;
  const GestureSample sample = this->gesture_router_.finish();
  this->touch_context_ = TouchContext::NONE;
  this->gesture_application_ = nullptr;
  if (!sample.captured)
    return false;

  if (context == TouchContext::HOME && this->get_home_view_count_() != 0) {
    const int current = this->find_home_view_index_();
    int target = current;
    const int32_t threshold =
        std::max<int32_t>(1, static_cast<int32_t>(std::lround(this->parent_->get_width() * this->home_commit_ratio_)));
    if (current >= 0 && std::abs(sample.delta_x) >= threshold) {
      const int candidate = current + (sample.delta_x < 0 ? 1 : -1);
      if (candidate >= 0 && candidate < static_cast<int>(this->get_home_view_count_()))
        target = candidate;
    }
    if (current >= 0 && target >= 0) {
      this->last_home_page_index_ = target;
#if LV_USE_SNAPSHOT && LV_USE_IMAGE
      if (this->snapshot_compositor_ == nullptr || !this->snapshot_compositor_->settle_home(target))
#endif
        this->activate_home_view(target);
    }
    return true;
  }

  if (context == TouchContext::APPLICATION_CLOSE) {
    const int32_t threshold = std::max<int32_t>(
        1, static_cast<int32_t>(std::lround(this->parent_->get_height() * this->close_commit_ratio_)));
    const bool close = sample.delta_y <= -threshold;
#if LV_USE_SNAPSHOT && LV_USE_IMAGE
    if (this->snapshot_compositor_ != nullptr && this->snapshot_compositor_->is_application_active()) {
      if (close && gesture_application != nullptr)
        gesture_application->call_on_close_callbacks();
      if (!this->snapshot_compositor_->settle_application_close(close)) {
        this->snapshot_compositor_->cancel_application();
        if (close && gesture_application != nullptr) {
          this->deactivate_application_view_(gesture_application);
          this->active_application_ = nullptr;
          this->activate_home_view(this->last_home_page_index_);
          gesture_application->call_on_closed_callbacks();
        }
      }
      return true;
    }
#endif
    if (close && gesture_application != nullptr) {
      gesture_application->call_on_close_callbacks();
      this->deactivate_application_view_(gesture_application);
      this->active_application_ = nullptr;
      this->activate_home_view(this->last_home_page_index_);
      gesture_application->call_on_closed_callbacks();
    }
    return true;
  }

#if LV_USE_SNAPSHOT && LV_USE_IMAGE
  if (context == TouchContext::APPLICATION_SCROLL && gesture_application != nullptr) {
    if (auto *scroll = gesture_application->get_scroll_snapshot(); scroll != nullptr)
      scroll->finish();
  }
#endif
  return true;
}

void LvglNavigation::touch_cancel() {
#if LV_USE_SNAPSHOT && LV_USE_IMAGE
  if (this->touch_context_ == TouchContext::APPLICATION_SCROLL && this->gesture_application_ != nullptr) {
    if (auto *scroll = this->gesture_application_->get_scroll_snapshot(); scroll != nullptr)
      scroll->cancel();
  } else if (this->snapshot_compositor_ != nullptr) {
    if (this->snapshot_compositor_->is_application_active())
      this->snapshot_compositor_->settle_application_close(false);
    else
      this->snapshot_compositor_->cancel_home();
  }
#endif
  this->reset_touch_();
}

void LvglNavigation::reset_touch_() {
  this->gesture_router_.cancel();
  this->gesture_application_ = nullptr;
  this->touch_context_ = TouchContext::NONE;
}

void LvglNavigation::open_application(LvglApplication *application) {
  if (application == nullptr || application->get_page() == nullptr || application->get_view() == nullptr ||
      this->find_active_application_() != nullptr)
    return;
#if LV_USE_SNAPSHOT && LV_USE_IMAGE
  if (this->snapshot_compositor_ != nullptr) {
    this->snapshot_compositor_->cancel_home();
    this->snapshot_compositor_->cancel_application();
  }
#endif
  if (const int home_index = this->find_home_view_index_(); home_index >= 0)
    this->last_home_page_index_ = home_index;
  this->active_application_ = application;
#if LV_USE_SNAPSHOT && LV_USE_IMAGE
  if (this->snapshot_compositor_ != nullptr &&
      this->snapshot_compositor_->open_application(application, this->last_home_page_index_)) {
    application->call_on_open_callbacks();
    return;
  }
#endif
  application->call_on_open_callbacks();
  this->activate_application_view_(application);
  application->call_on_opened_callbacks();
}

void LvglNavigation::close_application() {
  auto *application = this->find_active_application_();
#if LV_USE_SNAPSHOT && LV_USE_IMAGE
  if (application != nullptr && this->snapshot_compositor_ != nullptr &&
      this->snapshot_compositor_->begin_application_close(application, this->last_home_page_index_)) {
    application->call_on_close_callbacks();
    if (this->snapshot_compositor_->settle_application_close(true))
      return;
    this->snapshot_compositor_->cancel_application();
    this->deactivate_application_view_(application);
    this->active_application_ = nullptr;
    this->activate_home_view(this->last_home_page_index_);
    application->call_on_closed_callbacks();
    return;
  }
#endif
  if (application != nullptr) {
    application->call_on_close_callbacks();
    this->deactivate_application_view_(application);
    this->active_application_ = nullptr;
  }
  this->activate_home_view(this->last_home_page_index_);
  if (application != nullptr)
    application->call_on_closed_callbacks();
}

void LvglNavigation::show_home() {
  if (this->get_home_view_count_() == 0)
    return;
  auto *application = this->find_active_application_();
#if LV_USE_SNAPSHOT && LV_USE_IMAGE
  if (this->snapshot_compositor_ != nullptr) {
    this->snapshot_compositor_->cancel_home();
    this->snapshot_compositor_->cancel_application();
  }
#endif
  if (application != nullptr) {
    application->call_on_close_callbacks();
    this->deactivate_application_view_(application);
    this->active_application_ = nullptr;
  }
  const int target = std::clamp(this->last_home_page_index_, 0, static_cast<int>(this->get_home_view_count_()) - 1);
  this->activate_home_view(target);
  if (application != nullptr)
    application->call_on_closed_callbacks();
}

bool LvglNavigation::is_application_open(const LvglApplication *application) const {
  return application != nullptr && application == this->find_active_application_();
}

LvglApplication *LvglNavigation::get_active_application() const { return this->find_active_application_(); }

void LvglNavigation::prepare_application_transition(LvglApplication *application, bool opening, bool close_committed) {
  if (application == nullptr)
    return;
  if (opening || !close_committed)
    this->activate_application_view_(application);
  else
    this->deactivate_application_view_(application);
}

void LvglNavigation::complete_application_transition(LvglApplication *application, bool opening, bool close_committed) {
  if (application == nullptr)
    return;
  if (opening)
    application->call_on_opened_callbacks();
  else if (close_committed) {
    this->active_application_ = nullptr;
    application->call_on_closed_callbacks();
  }
}

void LvglNavigation::activate_application_view_(LvglApplication *application) {
  if (application == nullptr || application->get_page() == nullptr)
    return;
  this->parent_->show_page(application->get_page()->index, LV_SCREEN_LOAD_ANIM_NONE, 0);
  if (application->get_widget() != nullptr) {
    lv_obj_remove_flag(application->get_widget(), LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(application->get_widget());
  }
}

void LvglNavigation::deactivate_application_view_(LvglApplication *application) {
  if (application != nullptr && application->get_widget() != nullptr)
    lv_obj_add_flag(application->get_widget(), LV_OBJ_FLAG_HIDDEN);
}

size_t LvglNavigation::get_home_view_count_() const {
  return this->home_widgets_.empty() ? this->home_pages_.size() : this->home_widgets_.size();
}

void LvglNavigation::activate_home_view(int index) {
  if (index < 0 || index >= static_cast<int>(this->get_home_view_count_()))
    return;
  this->last_home_page_index_ = index;
  if (this->home_widgets_.empty()) {
    auto *page = this->home_pages_[index];
    if (page != nullptr)
      this->parent_->show_page(page->index, LV_SCREEN_LOAD_ANIM_NONE, 0);
    return;
  }
  if (this->home_widget_page_ == nullptr)
    return;
  for (size_t view_index = 0; view_index < this->home_widgets_.size(); view_index++) {
    auto *widget = this->home_widgets_[view_index];
    if (widget == nullptr)
      continue;
    if (view_index == static_cast<size_t>(index))
      lv_obj_remove_flag(widget, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(widget, LV_OBJ_FLAG_HIDDEN);
  }
  this->parent_->show_page(this->home_widget_page_->index, LV_SCREEN_LOAD_ANIM_NONE, 0);
}

}  // namespace esphome::lvgl
