#pragma once

#include "lvgl_gesture_router.h"
#include "lvgl_esphome.h"

#include "esphome/core/automation.h"
#include "esphome/core/helpers.h"

#include <cstdint>
#include <vector>

namespace esphome::lvgl {

class LvglComponent;
class LvPageType;
class LvglNavigation;
class LvglSnapshotCompositor;
class LvglScrollSnapshotController;

class LvglApplication {
 public:
  void set_parent(LvglNavigation *parent) { this->parent_ = parent; }
  void set_page(LvPageType *page) { this->page_ = page; }
  void set_widget(lv_obj_t *widget) { this->widget_ = widget; }
  void set_close_gesture_enabled(bool enabled) { this->close_gesture_enabled_ = enabled; }
  void set_close_on_threshold(bool enabled) { this->close_on_threshold_ = enabled; }
  void set_scroll_snapshot(LvglScrollSnapshotController *controller) { this->scroll_snapshot_ = controller; }
  template<typename F> void add_on_prepare_open_callback(F &&callback) {
    this->prepare_open_callbacks_.add(std::forward<F>(callback));
  }
  template<typename F> void add_on_open_callback(F &&callback) { this->open_callbacks_.add(std::forward<F>(callback)); }
  template<typename F> void add_on_before_reveal_callback(F &&callback) {
    this->before_reveal_callbacks_.add(std::forward<F>(callback));
  }
  template<typename F> void add_on_opened_callback(F &&callback) {
    this->opened_callbacks_.add(std::forward<F>(callback));
  }
  template<typename F> void add_on_prepare_close_callback(F &&callback) {
    this->prepare_close_callbacks_.add(std::forward<F>(callback));
  }
  template<typename F> void add_on_close_callback(F &&callback) {
    this->close_callbacks_.add(std::forward<F>(callback));
  }
  template<typename F> void add_on_close_cancelled_callback(F &&callback) {
    this->close_cancelled_callbacks_.add(std::forward<F>(callback));
  }
  template<typename F> void add_on_closed_callback(F &&callback) {
    this->closed_callbacks_.add(std::forward<F>(callback));
  }

  LvglNavigation *get_parent() const { return this->parent_; }
  LvPageType *get_page() const { return this->page_; }
  lv_obj_t *get_widget() const { return this->widget_; }
  lv_obj_t *get_view() const {
    return this->widget_ != nullptr ? this->widget_ : (this->page_ != nullptr ? this->page_->obj : nullptr);
  }
  LvglScrollSnapshotController *get_scroll_snapshot() const { return this->scroll_snapshot_; }
  bool is_widget_application() const { return this->widget_ != nullptr; }
  bool is_close_gesture_enabled() const { return this->close_gesture_enabled_; }
  bool is_close_on_threshold() const { return this->close_on_threshold_; }
  bool is_close_prepared() const { return this->close_prepared_; }
  void call_on_prepare_open_callbacks() { this->prepare_open_callbacks_.call(); }
  void call_on_open_callbacks() { this->open_callbacks_.call(); }
  void call_on_before_reveal_callbacks() { this->before_reveal_callbacks_.call(); }
  void call_on_opened_callbacks() { this->opened_callbacks_.call(); }
  void call_on_prepare_close_callbacks() {
    if (this->close_prepared_)
      return;
    this->close_prepared_ = true;
    this->prepare_close_callbacks_.call();
  }
  void call_on_close_callbacks() {
    this->close_prepared_ = false;
    this->close_callbacks_.call();
  }
  void call_on_close_cancelled_callbacks() {
    if (!this->close_prepared_)
      return;
    this->close_prepared_ = false;
    this->close_cancelled_callbacks_.call();
  }
  void call_on_closed_callbacks() {
    this->close_prepared_ = false;
    this->closed_callbacks_.call();
  }

 protected:
  LvglNavigation *parent_{};
  LvPageType *page_{};
  lv_obj_t *widget_{};
  LvglScrollSnapshotController *scroll_snapshot_{};
  LazyCallbackManager<void()> prepare_open_callbacks_{};
  LazyCallbackManager<void()> open_callbacks_{};
  LazyCallbackManager<void()> before_reveal_callbacks_{};
  LazyCallbackManager<void()> opened_callbacks_{};
  LazyCallbackManager<void()> prepare_close_callbacks_{};
  LazyCallbackManager<void()> close_callbacks_{};
  LazyCallbackManager<void()> close_cancelled_callbacks_{};
  LazyCallbackManager<void()> closed_callbacks_{};
  bool close_gesture_enabled_{true};
  bool close_on_threshold_{};
  bool close_prepared_{};
};

class LvglNavigation {
 public:
  explicit LvglNavigation(LvglComponent *parent) : parent_(parent) {}

  void add_home_page(LvPageType *page);
  void set_home_widget_page(LvPageType *page) { this->home_widget_page_ = page; }
  void add_home_widget(lv_obj_t *widget);
  void add_home_indicator(lv_obj_t *indicator);
  void add_blocker(lv_obj_t *widget);
  void add_application(LvglApplication *application);
  void set_swipe_start_distance(uint16_t distance) { this->gesture_router_.set_start_distance(distance); }
  void set_axis_bias(uint16_t bias) { this->gesture_router_.set_axis_bias(bias); }
  void set_home_commit_ratio(float ratio) { this->home_commit_ratio_ = ratio; }
  void set_home_commit_pixels(uint16_t pixels) { this->home_commit_pixels_ = pixels; }
  void set_close_edge_ratio(float ratio) { this->close_edge_ratio_ = ratio; }
  void set_close_edge_pixels(uint16_t pixels) { this->close_edge_pixels_ = pixels; }
  void set_close_commit_ratio(float ratio) { this->close_commit_ratio_ = ratio; }
  void set_close_commit_pixels(uint16_t pixels) { this->close_commit_pixels_ = pixels; }
  void set_snapshot_compositor(LvglSnapshotCompositor *compositor);
  template<typename F> void add_on_home_changed_callback(F &&callback) {
    this->home_changed_callbacks_.add(std::forward<F>(callback));
  }

  void touch_begin(int32_t x, int32_t y);
  bool touch_update(int32_t x, int32_t y);
  bool touch_end();
  void touch_cancel();
  void loop();

  void open_application(LvglApplication *application);
  void close_application();
  void show_home();
  void refresh_home();
  void prepare_application_snapshots();
  bool is_home_snapshot_prepared() const;

  bool is_application_open(const LvglApplication *application) const;
  LvglApplication *get_active_application() const;
  void activate_home_view(int index);
  void prepare_application_transition(LvglApplication *application, bool opening, bool close_committed);
  void complete_application_transition(LvglApplication *application, bool opening, bool close_committed);

 protected:
  enum class TouchContext : uint8_t {
    NONE,
    HOME,
    APPLICATION_CLOSE,
    APPLICATION_SCROLL,
  };

  int find_home_view_index_() const;
  size_t get_home_view_count_() const;
  LvglApplication *find_active_application_() const;
  void activate_application_view_(LvglApplication *application);
  void deactivate_application_view_(LvglApplication *application);
  bool is_blocked_() const;
  void update_home_indicators_(int index);
  void notify_home_changed_(int index);
  void reset_touch_();
  void schedule_application_close_();
#if LV_USE_SNAPSHOT && LV_USE_IMAGE
  void release_application_scroll_(LvglApplication *application);
  void restore_application_scroll_(LvglApplication *application);
#endif

  LvglComponent *parent_{};
  LvglSnapshotCompositor *snapshot_compositor_{};
  std::vector<LvPageType *> home_pages_{};
  LvPageType *home_widget_page_{};
  std::vector<lv_obj_t *> home_widgets_{};
  std::vector<lv_obj_t *> home_indicators_{};
  std::vector<lv_obj_t *> blockers_{};
  std::vector<LvglApplication *> applications_{};
  LvglApplication *active_application_{};
  GestureRouter gesture_router_{};
  LvglApplication *gesture_application_{};
  TouchContext touch_context_{TouchContext::NONE};
  int last_home_page_index_{};
  int last_notified_home_index_{-1};
  LazyCallbackManager<void(uint16_t)> home_changed_callbacks_{};
  float home_commit_ratio_{0.25f};
  float close_edge_ratio_{0.0625f};
  float close_commit_ratio_{0.25f};
  int32_t home_commit_pixels_{-1};
  int32_t close_edge_pixels_{-1};
  int32_t close_commit_pixels_{-1};
  bool close_deferred_{};
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

template<typename... Ts> class NavigationRefreshAction final : public Action<Ts...>, public Parented<LvglNavigation> {
 protected:
  void play(const Ts &...x) override { this->parent_->refresh_home(); }
};

template<typename... Ts>
class NavigationPrepareApplicationsAction final : public Action<Ts...>, public Parented<LvglNavigation> {
 protected:
  void play(const Ts &...x) override { this->parent_->prepare_application_snapshots(); }
};

template<typename... Ts>
class NavigationHomePreparedCondition final : public Condition<Ts...>, public Parented<LvglNavigation> {
 protected:
  bool check(const Ts &...x) override { return this->parent_->is_home_snapshot_prepared(); }
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
