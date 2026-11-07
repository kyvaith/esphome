// Exact production listener/navigation methods + real LVGL/TileSurface.
// Only platform navigation transport and successful snapshot acquisition are facades.
#include "home_real_lvgl_test_support.h"
#include <ArduinoJson.h>
#include <cstdio>
#include <string_view>

namespace esphome {
template<typename T> class Parented {
 public:
  void set_parent(T *parent) { this->parent_ = parent; }

 protected:
  T *parent_{};
};
namespace touchscreen {
struct TouchPoint {
  int id{}, x{}, y{};
  uint8_t state{};
};
using TouchPoints_t = std::vector<TouchPoint>;
class TouchListener {
 public:
  virtual ~TouchListener() = default;
  virtual void update(const TouchPoints_t &) = 0;
  virtual void release() = 0;
};
}  // namespace touchscreen
namespace lvgl {
inline bool lvgl_esphome_get_swipe_logging_enabled() { return false; }
class HomeNavigation {
 public:
  enum class TouchContext { NONE, DELEGATE, BLOCKED, HOME, APPLICATION_SCROLL };
  struct Page {
    lv_obj_t *obj{};
  };
  struct Scroll {
    bool is_active() const { return false; }
    bool owns_target(lv_obj_t *) const { return false; }
  };
  struct Application {
    Scroll *get_scroll_snapshot() { return nullptr; }
  };
  bool external_touch_capture_{false}, home_touch_takeover_{false};
  TouchContext touch_context_{TouchContext::HOME};
  lv_obj_t *home_widget_page_{};
  int gesture_home_page_index_{0};
  std::vector<lv_obj_t *> home_widgets_;
  std::vector<Page *> home_pages_;
  Application *gesture_application_{};
  bool should_defer_press(lv_obj_t *target) const;
  void set_external_touch_capture(bool value) { external_touch_capture_ = value; }
};
#include "home_navigation_defer.inc"
#include "home_listener_class.inc"
#include "home_listener_methods.inc"
}  // namespace lvgl
namespace json {
inline ArduinoJson::Allocator *heap_json_allocator() { return ArduinoJson::detail::DefaultAllocator::instance(); }
inline bool parse_json(const std::string &source, const std::function<bool(JsonObject)> &callback) {
  JsonDocument document;
  if (deserializeJson(document, source) || !document.is<JsonObject>())
    return false;
  return callback(document.as<JsonObject>());
}
}  // namespace json
}  // namespace esphome

#include "material_tile_surface.h.inc"
#include "material_tile_surface.cpp.inc"

// The condition below is extracted verbatim from the supplied product source.
#define id(name) name
static int ui_active_app = 0;
static bool ui_snapshot_io_busy = false, ui_touch_active = false;
static bool global_volume_time_press_tracking = false, global_volume_dragging = false;
static uint32_t ui_last_touch_activity_ms = 0, display_last_interaction_ms = 0;
using esphome::millis;
static bool lvgl_esphome_snapshot_is_active() { return false; }
static bool refresh_blocked() {
#include "home_product_guard.inc"
}

class Surface : public esphome::lvgl_material::MaterialTileSurface {
 public:
  void *binding_data(size_t index) { return &this->tiles_.at(index); }
  bool press_seen() const { return this->tiles_[0].press_seen; }
};

struct Fixture {
  esphome::lvgl::LvglComponent compositor;
  real_lvgl_test::HostDisplay display{compositor};
  esphome::lvgl::HomeNavigation navigation;
  esphome::lvgl::LVTouchListener listener{500, 200, &compositor};
  Surface surface;
  lv_obj_t *root{lv_obj_create(lv_screen_active())};
  lv_obj_t *buttons[2]{};
  unsigned commands{0}, refreshes{0};
  bool active{false}, ack{false};
  int origin{-1};
  uint32_t selection_generation{0};
  std::string reason;

  Fixture() {
    lv_obj_remove_style_all(root);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_size(root, 800, 800);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    navigation.home_widget_page_ = root;
    navigation.home_widgets_.push_back(root);
    compositor.test_navigation_begin = [](int32_t, int32_t) {};
    compositor.test_navigation_update = [](int32_t, int32_t) { return false; };
    compositor.test_navigation_end = []() { return false; };
    compositor.test_navigation_defer = [this](lv_obj_t *target) { return navigation.should_defer_press(target); };
    surface.set_action_availability_enabled(true);
    surface.set_require_snapshot_ready(true);
    for (unsigned i = 0; i < 2; i++) {
      auto *button = buttons[i] = lv_button_create(root);
      lv_obj_set_pos(button, 100 + i * 300, 100);
      lv_obj_set_size(button, 250, 120);
      lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
      auto *icon = lv_label_create(button);
      auto *title = lv_label_create(button);
      auto *subtitle = lv_label_create(button);
      for (auto *label : {icon, title, subtitle})
        lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
      surface.add_tile(7 + i, button, icon, title, subtitle);
    }
    surface.add_on_context_press_callback([this](uint8_t slot, const char *card_id, uint64_t revision) {
      assert(slot == 7 && std::string_view(card_id) == "action" && revision == 6);
      commands++;
    });
    surface.setup();
    assert(!surface.is_failed());
    configure(7, true);
    configure(8, true);
    surface.mark_slots_ready((1U << 7) | (1U << 8));
    real_lvgl_test::advance(60);
  }
  ~Fixture() {
    lv_indev_delete(listener.get_drv());
    for (unsigned i = 0; i < 2; i++)
      lv_obj_remove_event_cb_with_user_data(buttons[i], nullptr, surface.binding_data(i));
    lv_obj_delete(root);
  }
  void configure(unsigned slot, bool enabled, const char *subtitle = "42.5%") {
    const std::string payload = std::string(R"({"version":1,"revision":6,"cards":[{"slot":)") + std::to_string(slot) +
                                R"(,"id":")" + (slot == 7 ? "action" : "other") +
                                R"(","visible":true,"title":"Action","press_enabled":)" + (enabled ? "true" : "false") +
                                R"(,"subtitle":")" + subtitle + "\"}]}";
    assert(surface.configure(payload));
  }
  bool refresh() {
    if (refresh_blocked())
      return false;
    // Acquisition is the platform seam: acknowledge only an explicitly
    // successful synchronous capture, through the production mark method.
    surface.mark_slots_ready(surface.get_pending_context_slots());
    refreshes++;
    return true;
  }
  void down() {
    assert(!active && !ui_touch_active);
    active = ui_touch_active = true;
    origin = 0;
    ui_last_touch_activity_ms = millis();
    listener.update(esphome::touchscreen::TouchPoints_t{{0, 225, 160, 1}});
    lv_indev_read(listener.get_drv());
    real_lvgl_test::advance(20);
    assert(!surface.press_seen() && !lv_obj_has_state(buttons[0], LV_STATE_PRESSED));
  }
  void release() {
    const int phase = 2;
    const bool context_current = true, boot_sequence_done = true;
    auto &ui_debug_touch_input_active = active;
    auto &ui_debug_touch_input_origin_app = origin;
    auto &ui_debug_touch_input_selection_generation = selection_generation;
    auto &ui_navigation = navigation;
    auto &ui_touch_input = listener;
    auto reply = [this](bool accepted, const char *why) {
      ack = accepted;
      reason = why;
    };
#include "home_debug_release.inc"
    assert(false && "Actual owned phase2 must return");
  }
  void tap() {
    down();
    release();
    assert(ack && reason == "ok" && !active && !ui_touch_active);
    real_lvgl_test::advance(60);
  }
};

int main(int argc, char **argv) {
  assert(argc == 2);
  const std::string_view name{argv[1]};
  lv_init();
  Fixture f;
  if (name == "held_delivered_availability_aba" || name == "without_snapshot_gate") {
    if (name == "without_snapshot_gate")
      f.surface.set_require_snapshot_ready(false);  // Deliberate negative control, not product policy.
    f.down();
    f.configure(7, false);
    assert(lv_obj_has_state(f.buttons[0], LV_STATE_DISABLED));
    assert((f.surface.get_pending_context_slots() & (1U << 7)) != 0);
    assert(!f.refresh());
    f.configure(7, true);
    real_lvgl_test::advance(1000);
    assert(!f.refresh() && f.commands == 0);  // A long hold still cannot acknowledge a new context.
    f.release();
    assert(f.ack && f.reason == "ok" && !f.active && !ui_touch_active);
    real_lvgl_test::advance(60);  // Actual40ms child PRESS/CLICK replay runs while readiness remains false.
    assert(f.commands == 0 && "Held delivered availability must not replay a command");
    assert(!f.refresh());
    real_lvgl_test::advance(1000);
    assert(f.refresh());
    f.tap();
    assert(f.commands == 1);
  } else if (name == "disabled_home_contact") {
    f.configure(7, false);
    real_lvgl_test::advance(1000);
    assert(f.refresh());
    f.tap();
    assert(f.ack && f.commands == 0);  // Listener ACK is accepted; action eligibility is independently false.
    f.configure(7, true);
    real_lvgl_test::advance(1000);
    assert(f.refresh());
    f.tap();
    assert(f.commands == 1);
  } else if (name == "unrelated_availability_update") {
    f.down();
    f.configure(8, false);
    assert((f.surface.get_pending_context_slots() & (1U << 7)) == 0);
    f.release();
    real_lvgl_test::advance(60);
    assert(f.ack && f.commands == 1);  // Another card cannot cancel this permitted tap.
  } else if (name == "held_caption_aba" || name == "held_rebind_acknowledged") {
    f.down();
    if (name == "held_caption_aba") {
      f.configure(7, true, "80%");
      f.configure(7, true, "42.5%");
    } else {
      f.configure(7, false);
      f.configure(7, true);
      // Even if another owner acknowledges the new snapshot before release,
      // the identity observed by the original contact cannot be regained.
      f.surface.mark_slots_ready(1U << 7);
    }
    assert(!f.surface.press_seen() && !lv_obj_has_state(f.buttons[0], LV_STATE_PRESSED));
    f.release();
    real_lvgl_test::advance(1000);  // Longer than the40ms replay and500ms snapshot quiet time.
    assert(f.commands == 0 && "Raw held context must not replay a command");
    assert(f.refresh());
    f.tap();
    assert(f.commands == 1);
  } else if (name == "unrelated_caption_update") {
    f.down();
    f.configure(8, true, "80%");
    f.release();
    real_lvgl_test::advance(1000);
    assert(f.commands == 1);
  } else {
    assert(false && "Unknown Home case");
  }
  std::printf("HOME %s PASS commands=%u refreshes=%u\n", argv[1], f.commands, f.refreshes);
}
