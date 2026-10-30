#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>
#include <cassert>
#include <cstring>

inline uint32_t test_now = 100;
inline size_t label_writes = 0;
inline constexpr int lv_obj_class = 1;
inline constexpr int LV_OBJ_FLAG_CLICKABLE = 1, LV_OBJ_FLAG_SCROLLABLE = 2, LV_OBJ_FLAG_HIDDEN = 4;
inline constexpr int LV_STATE_DISABLED = 1;
inline constexpr int LV_INDEV_TYPE_POINTER = 1;
enum lv_event_code_t {
  LV_EVENT_ALL,
  LV_EVENT_PRESSED,
  LV_EVENT_PRESSING,
  LV_EVENT_RELEASED,
  LV_EVENT_PRESS_LOST,
  LV_EVENT_CLICKED,
  LV_EVENT_DELETE
};
struct lv_point_t {
  int32_t x{0}, y{0};
};
struct lv_area_t {
  int32_t x1{0}, y1{0}, x2{100}, y2{10};
};
struct lv_indev_t {
  int type{LV_INDEV_TYPE_POINTER};
  lv_point_t point;
};
struct lv_event_t;
struct lv_obj_t {
  struct Handler {
    void (*callback)(lv_event_t *);
    lv_event_code_t code;
    void *data;
  };
  int object_class{1}, flags{0}, state{0};
  std::string text;
  lv_area_t bounds;
  std::vector<Handler> handlers;
#ifdef TEST_SCALAR
  lv_obj_t *parent{nullptr};
  uint32_t background{0x102030}, text_color{0xFFFFFF};
  const void *font{nullptr};
#endif
};
struct lv_event_t {
  lv_obj_t *target;
  lv_event_code_t code;
  void *user_data;
  lv_indev_t *indev;
};
inline bool lv_obj_check_type(lv_obj_t *obj, const int *type) { return obj->object_class == *type; }
inline void lv_obj_add_flag(lv_obj_t *obj, int flag) { obj->flags |= flag; }
inline void lv_obj_remove_flag(lv_obj_t *obj, int flag) { obj->flags &= ~flag; }
inline bool lv_obj_has_flag(lv_obj_t *obj, int flag) { return (obj->flags & flag) != 0; }
inline bool lv_obj_is_visible(lv_obj_t *obj) { return !lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN); }
inline void lv_obj_add_state(lv_obj_t *obj, int state) { obj->state |= state; }
inline void lv_obj_remove_state(lv_obj_t *obj, int state) { obj->state &= ~state; }
inline const char *lv_label_get_text(lv_obj_t *obj) { return obj->text.c_str(); }
inline void lv_label_set_text(lv_obj_t *obj, const char *text) {
  obj->text = text;
  label_writes++;
}
inline void lv_obj_get_coords(lv_obj_t *obj, lv_area_t *bounds) { *bounds = obj->bounds; }
inline int lv_indev_get_type(lv_indev_t *indev) { return indev->type; }
inline void lv_indev_get_point(lv_indev_t *indev, lv_point_t *point) { *point = indev->point; }
inline void lv_obj_add_event_cb(lv_obj_t *obj, void (*callback)(lv_event_t *), lv_event_code_t code, void *data) {
  obj->handlers.push_back({callback, code, data});
}
inline void *lv_event_get_user_data(lv_event_t *event) { return event->user_data; }
inline lv_obj_t *lv_event_get_target_obj(lv_event_t *event) { return event->target; }
inline lv_event_code_t lv_event_get_code(lv_event_t *event) { return event->code; }
inline lv_indev_t *lv_event_get_indev(lv_event_t *event) { return event->indev; }
inline void test_event(lv_obj_t &obj, lv_event_code_t code, lv_indev_t *indev) {
  for (const auto &handler : obj.handlers) {
    if (handler.code == code || handler.code == LV_EVENT_ALL) {
      lv_event_t event{&obj, code, handler.data, indev};
      handler.callback(&event);
    }
  }
}

namespace esphome {
inline uint32_t millis() { return test_now; }
struct TestApp {
  uint32_t get_loop_component_start_time() const { return test_now; }
};
inline TestApp App;
namespace setup_priority {
inline constexpr float PROCESSOR = 400;
}
class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
  virtual void on_shutdown() {}
  virtual void dump_config() {}
  virtual float get_setup_priority() const { return 0; }
  bool is_failed() const { return this->failed_; }
  void mark_failed(const char * = nullptr) { this->failed_ = true; }
  void status_set_warning() { this->warning_ = true; }

 protected:
  bool failed_{false}, warning_{false};
};
class StringRef {
 public:
  StringRef() = default;
  explicit StringRef(const char *value) : value_(value) {}
  bool empty() const { return this->value_[0] == 0; }
  const char *c_str() const { return this->value_; }

 protected:
  const char *value_{""};
};
template<typename T> class LazyCallbackManager;
template<typename... Ts> class LazyCallbackManager<void(Ts...)> {
 public:
  template<typename F> void add(F &&f) { this->callbacks_.emplace_back(std::forward<F>(f)); }
  void call(Ts... value) {
    for (auto &callback : this->callbacks_)
      callback(value...);
  }
  bool empty() const { return this->callbacks_.empty(); }

 protected:
  std::vector<std::function<void(Ts...)>> callbacks_;
};
template<typename... Ts> class Action {
 public:
  virtual ~Action() = default;
  virtual void play(const Ts &...) = 0;
};
template<typename T> struct TestValue {
  T stored{};
  T value(...) const { return this->stored; }
};
namespace lvgl {
#ifndef TEST_SCALAR
class LvglComponent {
 public:
  bool paused{false};
  bool is_paused() const { return this->paused; }
};
#endif
}  // namespace lvgl
namespace api {
class APIServer {
 public:
  uint32_t call_id{0};
  bool success{false};
  std::string payload, error;
  void send_action_response(uint32_t id, bool accepted, StringRef message) {
    this->call_id = id;
    this->success = accepted;
    this->error = message.c_str();
    this->payload.clear();
  }
  void send_action_response(uint32_t id, bool accepted, StringRef message, const uint8_t *data, size_t size) {
    this->send_action_response(id, accepted, message);
    this->payload.assign(reinterpret_cast<const char *>(data), size);
  }
};
}  // namespace api
}  // namespace esphome
#define TEMPLATABLE_VALUE(type, name) \
 protected: \
  esphome::TestValue<type> name##_; \
\
 public: \
  void set_##name(type value) { this->name##_.stored = value; }
#define LOG_STR(x) x
#define ESP_LOGCONFIG(...) (void) 0
