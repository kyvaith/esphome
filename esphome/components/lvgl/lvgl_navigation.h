#pragma once

#include "lvgl_gesture_router.h"

#include "esphome/core/automation.h"
#include "esphome/core/helpers.h"

#include <cstdint>
#include <vector>

namespace esphome::lvgl {

class LvglComponent;
class LvPageType;
class LvglNavigation;
class LvglSnapshotCompositor;

class LvglApplication {
 public:
  void set_parent(LvglNavigation *parent) { this->parent_ = parent; }
  void set_page(LvPageType *page) { this->page_ = page; }
  void set_close_gesture_enabled(bool enabled) { this->close_gesture_enabled_ = enabled; }

  LvglNavigation *get_parent() const { return this->parent_; }
  LvPageType *get_page() const { return this->page_; }
  bool is_close_gesture_enabled() const { return this->close_gesture_enabled_; }

 protected:
  LvglNavigation *parent_{};
  LvPageType *page_{};
  bool close_gesture_enabled_{true};
};

class LvglNavigation {
 public:
  explicit LvglNavigation(LvglComponent *parent) : parent_(parent) {}

  void add_home_page(LvPageType *page);
  void add_application(LvglApplication *application);
  void set_swipe_start_distance(uint16_t distance) { this->gesture_router_.set_start_distance(distance); }
  void set_axis_bias(uint16_t bias) { this->gesture_router_.set_axis_bias(bias); }
  void set_home_commit_ratio(float ratio) { this->home_commit_ratio_ = ratio; }
  void set_close_edge_ratio(float ratio) { this->close_edge_ratio_ = ratio; }
  void set_close_commit_ratio(float ratio) { this->close_commit_ratio_ = ratio; }
  void set_snapshot_compositor(LvglSnapshotCompositor *compositor) { this->snapshot_compositor_ = compositor; }

  void touch_begin(int32_t x, int32_t y);
  bool touch_update(int32_t x, int32_t y);
  bool touch_end();
  void touch_cancel();

  void open_application(LvglApplication *application);
  void close_application();
  void show_home();

  bool is_application_open(const LvglApplication *application) const;
  LvglApplication *get_active_application() const;

 protected:
  enum class TouchContext : uint8_t {
    NONE,
    HOME,
    APPLICATION_CLOSE,
  };

  int find_home_page_index_() const;
  LvglApplication *find_active_application_() const;
  void reset_touch_();

  LvglComponent *parent_{};
  LvglSnapshotCompositor *snapshot_compositor_{};
  std::vector<LvPageType *> home_pages_{};
  std::vector<LvglApplication *> applications_{};
  GestureRouter gesture_router_{};
  LvglApplication *gesture_application_{};
  TouchContext touch_context_{TouchContext::NONE};
  int last_home_page_index_{};
  float home_commit_ratio_{0.25f};
  float close_edge_ratio_{0.0625f};
  float close_commit_ratio_{0.25f};
};

template<typename... Ts> class NavigationOpenAction final : public Action<Ts...> {
 public:
  explicit NavigationOpenAction(LvglApplication *application) : application_(application) {}

 protected:
  void play(const Ts &...x) override {
    if (this->application_ != nullptr && this->application_->get_parent() != nullptr)
      this->application_->get_parent()->open_application(this->application_);
  }

  LvglApplication *application_{};
};

template<typename... Ts> class NavigationCloseAction final : public Action<Ts...>, public Parented<LvglNavigation> {
 protected:
  void play(const Ts &...x) override { this->parent_->close_application(); }
};

template<typename... Ts> class NavigationHomeAction final : public Action<Ts...>, public Parented<LvglNavigation> {
 protected:
  void play(const Ts &...x) override { this->parent_->show_home(); }
};

template<typename... Ts> class NavigationIsOpenCondition final : public Condition<Ts...> {
 public:
  explicit NavigationIsOpenCondition(LvglApplication *application) : application_(application) {}

 protected:
  bool check(const Ts &...x) override {
    return this->application_ != nullptr && this->application_->get_parent() != nullptr &&
           this->application_->get_parent()->is_application_open(this->application_);
  }

  LvglApplication *application_{};
};

}  // namespace esphome::lvgl
