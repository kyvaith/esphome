// Production TileSurface with actual LVGL input/style callbacks and ArduinoJson.
#include "real_lvgl_test_support.h"
#include <ArduinoJson.h>
#include <cstdio>
#include <fstream>
#include <string_view>

namespace esphome::json {
inline ArduinoJson::Allocator *heap_json_allocator() { return ArduinoJson::detail::DefaultAllocator::instance(); }
inline bool parse_json(const std::string &source, const std::function<bool(JsonObject)> &callback) {
  JsonDocument document;
  if (deserializeJson(document, source) || !document.is<JsonObject>())
    return false;
  return callback(document.as<JsonObject>());
}
}  // namespace esphome::json

#include "material_tile_surface.h.inc"
#include "material_tile_surface.cpp.inc"

class Surface : public esphome::lvgl_material::MaterialTileSurface {
 public:
  static size_t binding_size() { return sizeof(TileBinding); }
  void *binding_data(size_t index) { return &this->tiles_.at(index); }
  uint32_t context_generation() const { return this->tiles_[0].context_generation; }
};

struct Fixture {
  esphome::lvgl::LvglComponent compositor;
  real_lvgl_test::HostDisplay display{compositor};
  real_lvgl_test::Pointer pointer{display.get()};
  Surface surface;
  lv_obj_t *root{lv_obj_create(lv_screen_active())};
  lv_obj_t *buttons[2]{};
  lv_obj_t *titles[2]{};
  unsigned commands{0}, typed{0};
  uint64_t revision{5};

  Fixture() {
    lv_obj_remove_style_all(root);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_size(root, 800, 800);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    surface.set_action_availability_enabled(true);
    surface.set_require_snapshot_ready(true);
    surface.set_control_press_enabled(true);
    surface.set_number_controls_enabled(true);
    for (unsigned i = 0; i < 2; i++) {
      auto *button = buttons[i] = lv_button_create(root);
      lv_obj_set_pos(button, 100 + i * 300, 100);
      lv_obj_set_size(button, 250, 120);
      lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
      auto *icon = lv_label_create(button);
      auto *title = titles[i] = lv_label_create(button);
      auto *subtitle = lv_label_create(button);
      for (auto *label : {icon, title, subtitle})
        lv_obj_remove_flag(label, LV_OBJ_FLAG_CLICKABLE);
      surface.add_tile(7 + i, button, icon, title, subtitle);
    }
    surface.add_on_context_press_callback([this](uint8_t slot, const char *id, uint64_t revision) {
      assert(slot == 7 && std::string_view(id) == "action" && revision == this->revision);
      commands++;
    });
    surface.add_on_entity_control_press_callback([this](uint8_t slot, const char *id, uint64_t revision, uint8_t) {
      assert(slot == 7 && std::string_view(id) == "action" && revision == this->revision);
      typed++;
    });
    surface.setup();
    assert(!surface.is_failed());
  }
  ~Fixture() {
    for (unsigned i = 0; i < 2; i++)
      lv_obj_remove_event_cb_with_user_data(buttons[i], nullptr, surface.binding_data(i));
    lv_obj_delete(root);
  }
  std::string payload(const char *availability = "", const char *control = "") {
    return std::string(R"({"version":1,"revision":)") + std::to_string(this->revision) +
           R"(,"cards":[{"slot":7,"id":"action","visible":true,"title":"Action")" + availability + control + "}]}";
  }
  void configure(const char *availability = "", const char *control = "") {
    assert(surface.configure(payload(availability, control)));
    surface.mark_slots_ready((1U << 7) | (1U << 8));
    real_lvgl_test::advance(30);
  }
  void tap() {
    pointer.press(225, 160);
    real_lvgl_test::advance(20);
    pointer.release();
    real_lvgl_test::advance(20);
  }
};

int main(int argc, char **argv) {
  assert(argc == 2);
  const std::string_view name{argv[1]};
  lv_init();
  Fixture f;
  if (name == "size") {
    std::printf("SIZE binding=%zu surface=%zu version=%u\n", Surface::binding_size(), sizeof(Surface),
                f.surface.get_press_enabled_version());
    return 0;
  }
  if (name == "legacy_default") {
    f.configure();
    f.tap();
    assert(f.commands == 1 && !lv_obj_has_state(f.buttons[0], LV_STATE_DISABLED));
    f.configure(",\"press_enabled\":false");
#ifdef USE_LVGL_MATERIAL_TILE_ACTION_AVAILABILITY
    assert(lv_obj_has_state(f.buttons[0], LV_STATE_DISABLED));
    f.tap();
    assert(f.commands == 1);
#else
    assert(f.surface.get_press_enabled_version() == 0 && !lv_obj_has_state(f.buttons[0], LV_STATE_DISABLED));
    f.tap();
    assert(f.commands == 2);
#endif
    f.configure();
    f.tap();
    assert(f.commands == (f.surface.get_press_enabled_version() ? 2U : 3U));
  } else if (name == "malformed_atomic") {
    f.configure(",\"press_enabled\":true");
    std::string before;
    assert(f.surface.serialize_presentation(before));
    for (const char *bad : {"null", "0", "1", "\"false\"", "[]", "{}"}) {
      const auto raw =
          std::string(
              R"({"version":1,"revision":5,"cards":[{"slot":7,"id":"action","title":"Changed"},{"slot":8,"id":"other","press_enabled":)") +
          bad + "}]}";
      assert(!f.surface.configure(raw));
      std::string after;
      assert(f.surface.serialize_presentation(after) && after == before);
    }
    assert(!f.surface.configure(f.payload(",\"press_enabled\":false", ",\"control_kind\":\"light\"")));
  } else if (name == "cancel_reenabled_hold" || name == "invalidated_then_disabled_hold") {
    assert(f.surface.get_press_enabled_version() == 1);
    f.configure(",\"press_enabled\":true");
    f.pointer.press(225, 160);
    real_lvgl_test::advance(20);
    assert(lv_obj_has_state(f.buttons[0], LV_STATE_PRESSED));
    if (name == "invalidated_then_disabled_hold") {
      f.revision = 6;
      f.configure(",\"press_enabled\":true");
      assert(lv_obj_has_state(f.buttons[0], LV_STATE_PRESSED));
    }
    const auto context = f.surface.context_generation();
    f.configure(",\"press_enabled\":false");
    assert(f.surface.context_generation() > context && lv_obj_has_state(f.buttons[0], LV_STATE_DISABLED));
    assert(!lv_obj_has_state(f.buttons[0], LV_STATE_PRESSED));
    f.configure(",\"press_enabled\":true");
    f.pointer.move(230, 160);
    real_lvgl_test::advance(20);
    f.pointer.release();
    real_lvgl_test::advance(20);
    assert(f.commands == 0 && !lv_obj_has_state(f.buttons[0], LV_STATE_PRESSED));
    f.tap();
    assert(f.commands == 1);
  } else if (name == "external_disabled_owned") {
    assert(f.surface.get_press_enabled_version() == 1);
    f.configure();
    lv_obj_add_state(f.buttons[0], LV_STATE_DISABLED);
    f.configure(",\"press_enabled\":false");
    f.configure(",\"press_enabled\":true");
    assert(lv_obj_has_state(f.buttons[0], LV_STATE_DISABLED));
    lv_obj_remove_state(f.buttons[0], LV_STATE_DISABLED);
    f.tap();
    assert(f.commands == 1);
  } else if (name == "snapshot_and_typed_routes") {
    f.configure(",\"press_enabled\":true");
    assert(f.surface.configure(f.payload(",\"press_enabled\":false")));
    assert((f.surface.get_pending_context_slots() & (1U << 7)) != 0);
    assert(f.surface.configure(f.payload(",\"press_enabled\":true")));
    assert((f.surface.get_pending_context_slots() & (1U << 7)) != 0);
    f.tap();
    assert(f.commands == 0);  // Enabling does not bypass the real snapshot boundary.
    f.surface.mark_slots_ready(1U << 7);
    f.tap();
    assert(f.commands == 1);
    for (const char *kind : {"light", "number"}) {
      const auto marker = std::string(",\"control_kind\":\"") + kind + "\"";
      f.configure("", marker.c_str());
      std::string serialized;
      assert(f.surface.serialize_presentation(serialized));
      assert(serialized.find("press_enabled") == std::string::npos);
      f.tap();
    }
    assert(f.commands == 1 && f.typed == 2);
  } else {
    assert(false && "Unknown case");
  }
  std::printf("RESULT %s PASS commands=%u typed=%u\n", argv[1], f.commands, f.typed);
}
