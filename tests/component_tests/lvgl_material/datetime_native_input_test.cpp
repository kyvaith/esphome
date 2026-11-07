#include "real_lvgl_test_support.h"
#include "material_entity_control_component.h"
#include "datetime_fixtures.h"
#include <map>
#include <iostream>

using namespace esphome::lvgl_material;
namespace rt = real_lvgl_test;
using Snapshot = ControlDatetimeSnapshot;

// The host font lacks U+00B7. Explicit stand-in for layout tests only; the
// product's existing Roboto source fonts/cmap are checked independently.
const lv_font_t *host_font() {
  static lv_font_t font = *LV_FONT_DEFAULT;
  font.get_glyph_dsc = [](const lv_font_t *actual, lv_font_glyph_dsc_t *description, uint32_t point, uint32_t next) {
    return (LV_FONT_DEFAULT)->get_glyph_dsc(actual, description, point == 0xB7 ? '.' : point, next);
  };
  return &font;
}
struct Control : MaterialEntityControlComponent {
  using MaterialEntityControlComponent::MaterialEntityControlComponent;
  bool ready() const { return datetime_ready_; }
  bool captured() const { return captured_target_ != nullptr; }
  void refresh() { update_static_(); }
  void profiles_for_test(uint8_t profiles) {
    extra_configured_ = profiles;
    assert(owner_.set_extra_profiles(profiles));
  }
};
struct Harness {
  esphome::lvgl::LvglComponent lvgl;
  rt::HostDisplay display{lvgl};
  rt::NativeWidgets widgets{lv_screen_active()};
  rt::Pointer pointer{display.get()};
  esphome::lvgl::LvglNavigation navigation;
  esphome::lvgl::LvglApplication application;
  Control control{&lvgl};
  std::map<std::string, lv_obj_t *> w;
  std::vector<ControlMessage> messages;
  Harness(const std::string &name) {
    navigation.active = &application;
    w["base"] = widgets.root;
#include "datetime_widgets.h"
    control.set_widgets(widgets.root, widgets.hit, widgets.title, widgets.value, widgets.unknown, widgets.on,
                        widgets.off);
    control.set_state_label(widgets.state);
    control.set_status_label(widgets.status);
    control.set_number_profile(&navigation, &application, widgets.on_label, widgets.off_label, LV_FONT_DEFAULT);
    control.set_datetime_enabled(true);
    control.set_initial_enabled(true);
    control.add_on_message_callback([this](const char *wire) {
      MaterialEntityControlCodec codec;
      ControlMessage message;
      assert(codec.parse_entity(wire, message));
      messages.push_back(message);
    });
    control.setup();
    assert(!control.is_failed());
    rt::advance(200);
    assert(control.bind_entity_json(
        R"({"v":1,"type":"entity_control_v1","kind":"bind","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})"));
    ArduinoJson::JsonDocument response;
    if (name == "interop") {
      control.profiles_for_test(127);
      const char *offers[] = {
          R"({"v":1,"type":"entity_panel_extra_v1","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","profiles":[{"name":"action","version":2},{"name":"info","version":1},{"name":"info_context","version":1},{"name":"lock","version":1},{"name":"media","version":1}]})",
          R"({"v":1,"type":"entity_panel_extra_v1","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","profiles":[{"name":"action","version":2},{"name":"datetime","version":1},{"name":"info","version":1},{"name":"info_context","version":1},{"name":"lock","version":1},{"name":"media","version":1}]})"};
      for (size_t i = 0; i < 2; i++)
        for (bool bind : {false, true}) {
          response.clear();
          assert(bind ? control.bind_extra_features(offers[i], response.to<ArduinoJson::JsonObject>())
                      : control.query_extra_features(offers[i], response.to<ArduinoJson::JsonObject>()));
          assert(response["profiles"].size() == i + 5);
          std::string wire;
          ArduinoJson::serializeJson(response, wire);
          std::cout << (bind ? "BIND" : "QUERY") << (i ? "119 " : "118 ") << wire << "\n";
        }
      control.profiles_for_test(64);
    }
    response.clear();
    assert(control.bind_extra_features(
        R"({"v":1,"type":"entity_panel_extra_v1","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","profiles":[{"name":"datetime","version":1}]})",
        response.to<ArduinoJson::JsonObject>()));
    open(name == "date" || name == "invalid" ? "date-grant" : name == "time" ? "time-grant" : "datetime-grant");
  }
  void open(const char *fixture_name) {
    assert(control.open_panel(29, "datetime-fixture", 379));
    MaterialEntityControlCodec codec;
    ControlMessage message;
    bool found = false;
    for (const auto &fixture : fixtures)
      if (std::strcmp(fixture.name, fixture_name) == 0) {
        assert(codec.parse_entity(fixture.wire, message));
        found = true;
      }
    assert(found);
    message.identity = control.owner().identity();
    message.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    receive(message);
    assert(control.resume_control_view(control.get_selection_generation()));
    rt::advance(300);
    assert(control.ready() && control.is_datetime_view() && control.owner().is_view_ready());
    assert(visible("root") && !lv_obj_is_visible(widgets.hit));
  }
  void receive(const ControlMessage &message) {
    MaterialEntityControlCodec codec;
    char wire[2049];
    size_t len;
    assert(codec.serialize_entity(message, wire, sizeof(wire), len));
    assert(control.receive_entity({wire, len}));
    lv_refr_now(display.get());
  }
  ControlMessage next() {
    ControlMessage message;
    message.generic = true;
    message.kind = ControlKind::CONTROL_KIND_STATE;
    message.identity = control.owner().identity();
    message.state = control.owner().state();
    message.state.state_seq++;
    return message;
  }
  const char *text(const char *id) { return lv_label_get_text(w.at(std::string("datetime_") + id)); }
  bool visible(const char *id) { return lv_obj_is_visible(w.at(std::string("datetime_") + id)); }
  lv_obj_t *object(const char *id) { return w.at(std::string("datetime_") + id); }
  void press(const char *id) {
    lv_area_t bounds;
    lv_obj_get_coords(object(id), &bounds);
    pointer.press((bounds.x1 + bounds.x2) / 2, (bounds.y1 + bounds.y2) / 2);
  }
  void tap(const char *id) {
    press(id);
    pointer.release();
    control.loop();
    rt::advance(200);
  }
  size_t commands() const {
    return std::count_if(messages.begin(), messages.end(),
                         [](const auto &m) { return m.kind == ControlKind::CONTROL_KIND_COMMAND; });
  }
};

int main(int argc, char **argv) {
  assert(argc == 2);
  lv_init();
  std::setbuf(stdout, nullptr);
  const std::string name = argv[1];
  Harness f(name);
  if (name == "datetime" || name == "date" || name == "time") {
    const auto *state = f.control.owner().state().family_if<Snapshot>();
    const bool date = state->mode != ControlDatetimeMode::CONTROL_DATETIME_MODE_TIME;
    const bool time = state->mode != ControlDatetimeMode::CONTROL_DATETIME_MODE_DATE;
    assert(f.visible("field_day") == date && f.visible("field_second") == time);
    assert(!f.visible("editor"));
    if (time)
      assert(std::string(f.text("summary")).find(":" + std::string(f.text("second_value"))) != std::string::npos);
    const auto row_y = lv_obj_get_y(f.object(date ? "field_day" : "field_second"));
    assert(row_y == (date && time ? 0 : 48));
    f.tap(date ? "field_year" : "field_second");
    assert(f.visible("editor") && !f.visible("summary") && f.control.owner().datetime_draft().editor_open);
    assert(f.commands() == 0);
  } else if (name == "save-open") {
    f.tap("field_second");
    f.tap("key_5");
    f.tap("key_9");
    assert(std::string(f.text("editor_digits")) == "59" && f.commands() == 0);
    f.tap("save");
    assert(f.commands() == 1 && f.messages.back().operation == ControlOperation::CONTROL_OPERATION_DATETIME_SET);
    assert(f.messages.back().state.family_if<Snapshot>()->value.values[5] == 59);
    assert(f.control.owner().is_command_pending());
  } else if (name == "invalid") {
    f.tap("field_day");
    f.tap("key_3");
    f.tap("key_1");
    assert(std::string(f.text("editor_digits")) == "31");
    assert(lv_obj_has_state(f.object("save"), LV_STATE_DISABLED));
    f.tap("save");
    assert(f.commands() == 0);
    // A valid per-field value is kept in the draft, then another field can repair it.
    f.tap("field_month");
    assert(f.control.owner().datetime_draft().selected_field == 1);
    f.tap("key_3");
    f.tap("save");
    assert(f.commands() == 1);
    const auto &target = f.messages.back().state.family_if<Snapshot>()->value;
    assert(target.values[1] == 3 && target.values[2] == 31);
  } else if (name == "switch-field") {
    f.tap("field_second");
    f.tap("backspace");
    f.tap("backspace");
    assert(std::string(f.text("editor_digits")).empty());
    f.tap("field_minute");
    assert(f.control.owner().datetime_draft().selected_field == 5);
    f.tap("key_4");
    f.tap("field_minute");
    auto draft = f.control.owner().datetime_draft();
    assert(draft.selected_field == 4 && draft.value.values[5] == 4);
    f.tap("key_1");
    f.tap("key_2");
    f.tap("field_minute");
    assert(std::string(f.text("editor_digits")) == "12");
    f.tap("plus");
    assert(std::string(f.text("editor_digits")) == "13");
    f.tap("key_2");
    assert(std::string(f.text("editor_digits")) == "2" && f.commands() == 0);
  } else if (name == "cancel") {
    f.tap("field_year");
    f.tap("key_1");
    f.tap("cancel");
    assert(!f.visible("editor") && !f.control.owner().datetime_draft().active && f.commands() == 0);
    assert(std::string(f.text("year_value")) == "2026");
  } else if (name == "semantic-cancel" || name == "metadata-hold") {
    f.tap("field_second");
    f.tap("key_5");
    f.tap("key_9");
    f.press("save");
    assert(f.control.captured());
    auto message = f.next();
    auto *state = message.state.family_if<Snapshot>();
    if (name == "semantic-cancel") {
      state->value.values[5] = 1;
      state->state_generation++;
    } else
      state->updated.assign("2026-11-06T01:02:03+01:00");
    f.receive(message);
    f.pointer.release();
    f.control.loop();
    rt::advance(200);
    assert(f.commands() == (name == "metadata-hold" ? 1U : 0U));
    if (name == "semantic-cancel")
      assert(!f.control.owner().datetime_draft().active && !f.visible("editor"));
  } else if (name == "retirement") {
    f.tap("field_month");
    f.tap("key_1");
    f.control.invalidate(ControlReason::CONTROL_REASON_EXPIRED);
    assert(!f.control.owner().datetime_draft().active && f.control.is_datetime_view());
    assert(!f.visible("editor") && !f.visible("fields") && std::string(f.text("summary")) == "--");
    assert(f.control.open(19, "light-fixture", 379));
    ControlMessage grant;
    grant.generic = true;
    grant.kind = ControlKind::CONTROL_KIND_GRANT;
    grant.identity = f.control.owner().identity();
    grant.identity.token.assign("cccccccccccccccccccccccccccccccc");
    grant.state.state_seq = 1;
    grant.state.title.assign("Light");
    grant.state.caps.turn_on = true;
    grant.state.caps.turn_off = true;
    f.receive(grant);
    assert(f.control.resume_control_view(f.control.get_selection_generation()));
    rt::advance(200);
    assert(!f.visible("root") && !f.control.is_datetime_view() && lv_obj_is_visible(f.widgets.on));
    assert(f.commands() == 0);
  } else if (name == "palette") {
    lv_obj_set_style_bg_color(f.widgets.on, lv_color_hex(0xDDEEFF), LV_PART_MAIN);
    lv_obj_set_style_text_color(f.widgets.on_label, lv_color_hex(0x112233), LV_PART_MAIN);
    lv_obj_set_style_bg_color(f.widgets.off, lv_color_hex(0x223344), LV_PART_MAIN);
    lv_obj_set_style_text_color(f.widgets.off_label, lv_color_hex(0xF0E0D0), LV_PART_MAIN);
    lv_obj_add_state(f.widgets.on, LV_STATE_DISABLED);
    lv_obj_add_state(f.widgets.off, LV_STATE_PRESSED);
    f.control.refresh();
    const auto palette = [&](const char *button_id, bool active, bool enabled) {
      auto *button = f.object(button_id);
      const auto background = lv_color_hex(active ? 0xDDEEFF : 0x223344);
      const auto foreground = lv_color_hex(active ? 0x112233 : 0xF0E0D0);
      assert(lv_obj_has_state(button, LV_STATE_DISABLED) == !enabled);
      assert(lv_color_eq(lv_obj_get_style_bg_color(button, LV_PART_MAIN), background));
      assert(lv_color_eq(lv_obj_get_style_text_color(button, LV_PART_MAIN), foreground));
      // Labels and SVG stand-ins have their own local styles in the product.
      for (uint32_t i = 0; i < lv_obj_get_child_count(button); i++)
        assert(lv_color_eq(lv_obj_get_style_text_color(lv_obj_get_child(button, i), LV_PART_MAIN), foreground));
    };
    palette("save", false, false);
    palette("cancel", false, false);
    palette("accept", false, false);
    f.tap("field_second");
    palette("field_second", true, true);
    palette("save", true, true);
    palette("accept", true, true);
    assert(lv_color_eq(lv_obj_get_style_text_color(f.object("backspace_icon"), LV_PART_MAIN), lv_color_hex(0xF0E0D0)));
    assert(lv_color_eq(lv_obj_get_style_text_color(f.object("accept_icon"), LV_PART_MAIN), lv_color_hex(0x112233)));
    assert(lv_color_eq(lv_obj_get_style_text_color(f.object("second_value"), LV_PART_MAIN), lv_color_hex(0x112233)));
    f.tap("backspace");
    f.tap("backspace");
    palette("save", false, false);
    palette("accept", false, false);
    f.tap("key_5");
    palette("save", true, true);
    palette("accept", true, true);
    // A complete-fit failure must also neutralize the selected field itself.
    lv_obj_set_width(f.object("second_value"), 1);
    f.control.refresh();
    assert(!f.control.ready());
    palette("field_second", false, false);
    palette("save", false, false);
    palette("accept", false, false);
    lv_obj_set_width(f.object("second_value"), 148);
    f.control.refresh();
    assert(f.control.ready());
    palette("field_second", true, true);
    palette("save", true, true);
    f.tap("cancel");
    palette("save", false, false);
    palette("accept", false, false);
    palette("field_second", false, true);
    // Valid individual digits do not make an invalid complete date saveable.
    f.tap("field_month");
    f.tap("key_2");
    f.tap("field_day");
    f.tap("key_3");
    f.tap("key_1");
    assert(!f.control.owner().datetime_draft().tuple_valid);
    palette("save", false, false);
    palette("accept", true, true);
    f.tap("save");
    auto message = f.next();
    auto *state = message.state.family_if<Snapshot>();
    state->value.values[5] = 38;
    state->state_generation++;
    f.receive(message);
    rt::advance(200);
    assert(!f.control.owner().datetime_draft().active && !f.visible("editor"));
    palette("save", false, false);
    palette("accept", false, false);
    palette("field_day", false, true);
    assert(f.commands() == 0);
  } else if (name == "unfit") {
    lv_obj_set_width(f.object("summary"), 4);
    f.control.refresh();
    assert(!f.control.ready() && std::string(f.text("summary")) == "--");
    assert(lv_obj_has_state(f.object("field_year"), LV_STATE_DISABLED));
    f.tap("field_year");
    assert(f.commands() == 0 && !f.visible("editor"));
  } else if (name == "unchanged") {
    lv_refr_now(f.display.get());
    const auto flushes = f.lvgl.native_flushes;
    for (size_t i = 0; i < 4; i++) {
      f.control.refresh();
      lv_refr_now(f.display.get());
    }
    assert(f.lvgl.native_flushes == flushes && f.commands() == 0);
  } else
    assert(name == "interop");
  std::printf("RESULT %s pass=1\n", name.c_str());
}
