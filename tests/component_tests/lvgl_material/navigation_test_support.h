#pragma once
#include "scalar_test_support.h"

inline constexpr int LV_STATE_PRESSED = 2, LV_STATE_FOCUSED = 4, LV_STATE_HOVERED = 8, LV_STATE_CHECKED = 16,
                     LV_SCREEN_LOAD_ANIM_NONE = 0, LV_ALIGN_CENTER = 0;
using lv_state_t = int;
inline size_t test_indev_resets = 0;
inline void lv_obj_clear_state(lv_obj_t *object, int state) { object->state &= ~state; }
inline bool lv_obj_is_valid(lv_obj_t *object) { return object != nullptr; }
inline uint32_t lv_obj_get_child_count(lv_obj_t *) { return 0; }
inline lv_obj_t *lv_obj_get_child(lv_obj_t *, int32_t) { return nullptr; }
inline lv_obj_t *lv_obj_get_parent(lv_obj_t *object) { return object->parent; }
inline void lv_obj_align(lv_obj_t *, int, int, int) {}
inline void lv_indev_reset(void *, void *) { test_indev_resets++; }
inline int lv_display_get_horizontal_resolution(void *) { return 800; }
inline bool lvgl_esphome_get_swipe_logging_enabled() { return false; }
inline void lvgl_esphome_snapshot_swipe_set_page_indicator(int, int) {}
inline bool lvgl_esphome_snapshot_cache_has_raw_page(lv_obj_t *) { return false; }
inline bool lvgl_esphome_snapshot_refresh_tile_page(lv_obj_t *, int) { return true; }
inline void lvgl_esphome_snapshot_discard_pending_refresh() {}
namespace esphome {
inline uint32_t micros() { return test_now * 1000; }
template<typename... Ts> class Condition {
 public:
  virtual ~Condition() = default;
  virtual bool check(const Ts &...) = 0;
};
template<typename T> class Parented {
 protected:
  T *parent_{};

 public:
  void set_parent(T *value) { parent_ = value; }
};
namespace lvgl {
class LvglNavigation;
class LvglScrollSnapshotController {
 public:
  bool active{false}, prepared{false}, contains_touch{false};
  size_t releases{0};
  bool contains(int32_t, int32_t) const { return contains_touch; }
  bool touch_begin(int32_t) { return active; }
  bool is_active() const { return active; }
  bool is_prepared() const { return prepared; }
  bool owns_target(lv_obj_t *) const { return true; }
  uint16_t get_start_distance() const { return 0; }
  uint16_t get_axis_bias() const { return 0; }
  bool begin() {
    active = true;
    return true;
  }
  void update(int32_t, int32_t) {}
  void finish(int32_t) {}
  void cancel() { active = false; }
  void release() {
    releases++;
    active = false;
    prepared = false;
  }
  void prepare() { prepared = true; }
};
class LvglSnapshotCompositor {
 public:
  bool application_active{false}, opening{false}, closing{false}, home_active{false};
  bool begin_result{true}, prepare_result{true};
  size_t prepares{0}, begins{0}, takeovers{0}, primes{0}, cancels{0};
  std::vector<LvglApplication *> captured_applications;
  void set_navigation(LvglNavigation *) {}
  void replace_home_views(lv_obj_t **, size_t) {}
  bool is_application_active() const { return application_active; }
  bool is_application_opening() const { return opening; }
  bool is_application_closing() const { return closing; }
  bool is_home_active() const { return home_active; }
  bool is_home_prepared() const { return true; }
  bool take_over_home(int *page, int32_t *offset) {
    takeovers++;
    *page = 0;
    *offset = 0;
    return true;
  }
  bool prime_home(int) {
    primes++;
    return true;
  }
  void cancel_home_prime() {}
  bool begin_home(int) {
    begins++;
    home_active = begin_result;
    return begin_result;
  }
  void update_home(int32_t) {}
  void get_home_position(int *page, int32_t *offset) {
    *page = 0;
    *offset = 0;
  }
  bool settle_home(int, int32_t) { return true; }
  void cancel_home() {
    cancels++;
    home_active = false;
  }
  void cancel_application() {
    cancels++;
    application_active = opening = closing = false;
  }
  bool open_application(LvglApplication *, int) { return false; }
  bool begin_application_close(LvglApplication *, int) {
    begins++;
    application_active = closing = begin_result;
    return begin_result;
  }
  void update_application_close(int32_t) {}
  bool settle_application_close(bool) { return true; }
  void prepare_home(int) { prepares++; }
  bool prepare_applications(const std::vector<LvglApplication *> &applications) {
    prepares++;
    captured_applications = applications;
    return prepare_result;
  }
  void loop() {}
};
}  // namespace lvgl
}  // namespace esphome
#define ESP_LOGI(...) scalar_test_log(__VA_ARGS__)
#define ESP_LOGD(...) scalar_test_log(__VA_ARGS__)
