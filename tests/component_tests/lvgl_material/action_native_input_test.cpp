#include "real_lvgl_test_support.h"
#include "material_entity_control_component.h"
#include "action_owner_fixtures.h"
#include <iostream>
using namespace esphome::lvgl_material;
namespace rt = real_lvgl_test;
using Action = ControlActionSnapshot;
using Op = ControlOperation;
struct Control : MaterialEntityControlComponent {
  using MaterialEntityControlComponent::MaterialEntityControlComponent;
  bool ready() const { return action_ready_; }
  bool captured() const { return captured_target_ != nullptr; }
  void refresh() { update_static_(); }
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
  bool choose_for_test(bool value) { return owner_.set_action_parameter(value, esphome::millis()); }
#endif
  void profiles_for_test(uint16_t profiles) {
    extra_configured_ = profiles;
    assert(owner_.set_extra_profiles(profiles));
  }
  bool fit_for_test(lv_obj_t *label, const char *text, bool multiline) {
    return extra_text_(label, text, nullptr, nullptr, multiline);
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
  // Public Montserrat fixture omits U+00B7 used by the timestamp polish. Map only that
  // separator to its dot glyph; actual Roboto advances/glyphs are checked separately.
  lv_font_t caption_font{*LV_FONT_DEFAULT};
  lv_obj_t *common[8]{}, *icons[4]{}, *buttons[3]{}, *labels[3]{};
  std::vector<ControlMessage> messages;
  Harness(const char *name, bool dispatched = false, bool tile_event = false, int parameter_default = -2) {
#ifndef USE_LVGL_MATERIAL_ACTION_PARAMETERS
    assert(parameter_default == -2);
#endif
#ifndef USE_LVGL_MATERIAL_TILE_EVENT_CONTROLS
    assert(!tile_event);
#endif
    caption_font.get_glyph_dsc = [](const lv_font_t *, lv_font_glyph_dsc_t *dsc, uint32_t letter, uint32_t next) {
      return (LV_FONT_DEFAULT)->get_glyph_dsc(LV_FONT_DEFAULT, dsc, letter == 0xB7 ? '.' : letter, next);
    };
    navigation.active = &application;
    common[0] = rt::NativeWidgets::plain(widgets.root, 0, 0, 800, 800, 0);
    const int xywh[7][4] = {{130, 104, 540, 42}, {200, 68, 400, 28},  {130, 278, 540, 44}, {130, 328, 540, 52},
                            {180, 384, 440, 28}, {130, 418, 540, 56}, {200, 700, 400, 48}};
    for (size_t i = 1; i < 8; i++) {
      auto &r = xywh[i - 1];
      common[i] = rt::NativeWidgets::label(common[0], "", r[0], r[1], r[2], r[3]);
      lv_label_set_long_mode(common[i], LV_LABEL_LONG_WRAP);
      lv_obj_set_style_text_font(common[i], &caption_font, LV_PART_MAIN);
    }
    for (auto &icon : icons)
      icon = rt::NativeWidgets::plain(common[0], 352, 166, 96, 96, 0);
    const int geometry[3][4] = {{190, 494, 420, 76}, {174, 586, 218, 80}, {408, 586, 218, 80}};
    for (size_t i = 0; i < 3; i++) {
      auto &r = geometry[i];
      buttons[i] = lv_button_create(common[0]);
      lv_obj_set_pos(buttons[i], r[0], r[1]);
      lv_obj_set_size(buttons[i], r[2], r[3]);
      lv_obj_set_scrollable(buttons[i], false);
      lv_obj_set_style_pad_all(buttons[i], 0, LV_PART_MAIN);
      labels[i] = rt::NativeWidgets::label(buttons[i], "", 8, 8, r[2] - 16, r[3] - 16);
      lv_label_set_long_mode(labels[i], LV_LABEL_LONG_WRAP);
    }
    control.set_widgets(widgets.root, widgets.hit, widgets.title, widgets.value, widgets.unknown, widgets.on,
                        widgets.off);
    control.set_state_label(widgets.state);
    control.set_status_label(widgets.status);
    control.set_number_profile(&navigation, &application, widgets.on_label, widgets.off_label, LV_FONT_DEFAULT);
    control.set_action_profile(common[0], common[1], common[2], common[3], common[4], common[5], common[6], common[7]);
    for (size_t i = 0; i < 4; i++)
      control.set_action_icon(i, icons[i]);
    for (size_t i = 0; i < 3; i++)
      control.set_action_button(i, buttons[i], labels[i]);
    control.set_action_enabled(true);
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
    control.set_action_parameters_enabled(parameter_default >= -1);
#endif
#ifdef USE_LVGL_MATERIAL_TILE_EVENT_CONTROLS
    control.set_tile_event_enabled(tile_event);
#endif
    control.set_initial_enabled(true);
    control.add_on_message_callback([this](const char *wire) {
      MaterialEntityControlCodec codec;
      ControlMessage m;
      assert(codec.parse_entity(wire, m));
      messages.push_back(m);
    });
    control.setup();
    assert(!control.is_failed());
    rt::advance(200);
    assert(control.bind_entity_json(
        R"({"v":1,"type":"entity_control_v1","kind":"bind","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})"));
    ArduinoJson::JsonDocument response;
    if (tile_event) {
      const char *offer =
          R"({"v":1,"type":"entity_panel_extra_v1","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","profiles":[{"name":"action","version":2},{"name":"tile_event","version":1}]})";
      for (bool bind : {false, true}) {
        response.clear();
        assert(bind ? control.bind_extra_features(offer, response.to<ArduinoJson::JsonObject>())
                    : control.query_extra_features(offer, response.to<ArduinoJson::JsonObject>()));
        assert(response["profiles"].size() == 2);
        assert(response["profiles"][1]["name"].as<std::string>() == "tile_event");
      }
    }
    if (dispatched) {
      control.profiles_for_test(31);
      const char *offer =
          R"({"v":1,"type":"entity_panel_extra_v1","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","profiles":[{"name":"action","version":2},{"name":"info","version":1},{"name":"lock","version":1},{"name":"media","version":1}]})";
      for (bool bind : {false, true}) {
        response.clear();
        assert(bind ? control.bind_extra_features(offer, response.to<ArduinoJson::JsonObject>())
                    : control.query_extra_features(offer, response.to<ArduinoJson::JsonObject>()));
        assert(response["profiles"].size() == 4);
        assert(response["profiles"][0]["name"].as<std::string>() == "action");
        assert(response["profiles"][0]["version"].as<int>() == 2);
        for (size_t i = 1; i < 4; i++)
          assert(response["profiles"][i]["version"].as<int>() == 1);
        std::string wire;
        ArduinoJson::serializeJson(response, wire);
        std::cout << (bind ? "BIND2 " : "QUERY2 ") << wire << "\n";
      }
      response.clear();
    }
    if (std::strcmp(name, "button-first-use.json") == 0) {
      control.profiles_for_test(15);
      const char *offers[] = {
          R"({"v":1,"type":"entity_panel_extra_v1","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","profiles":[{"name":"info","version":1},{"name":"lock","version":1},{"name":"media","version":1}]})",
          R"({"v":1,"type":"entity_panel_extra_v1","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","profiles":[{"name":"action","version":1},{"name":"info","version":1},{"name":"lock","version":1},{"name":"media","version":1}]})"};
      const char *expected[] = {"action", "info", "lock", "media"};
      for (size_t count = 3; count <= 4; count++) {
        response.clear();
        assert(control.query_extra_features(offers[count - 3], response.to<ArduinoJson::JsonObject>()));
        assert(response["profiles"].size() == count);
        for (size_t index = 0; index < count; index++)
          assert(response["profiles"][index]["name"].as<std::string>() == expected[index + (count == 3)]);
        std::string json;
        ArduinoJson::serializeJson(response, json);
        std::cout << "QUERY" << count << " " << json << "\n";
        response.clear();
        assert(control.bind_extra_features(offers[count - 3], response.to<ArduinoJson::JsonObject>()));
        assert(response["accepted"].as<bool>() && response["profiles"].size() == count);
        for (size_t index = 0; index < count; index++)
          assert(response["profiles"][index]["name"].as<std::string>() == expected[index + (count == 3)]);
        json.clear();
        ArduinoJson::serializeJson(response, json);
        std::cout << "BIND" << count << " " << json << "\n";
      }
      control.profiles_for_test(8);
      response.clear();
    }
    assert(
        dispatched || tile_event ||
        control.bind_extra_features(
            R"({"v":1,"type":"entity_panel_extra_v1","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","profiles":[{"name":"action","version":1}]})",
            response.to<ArduinoJson::JsonObject>()));
    assert(dispatched || tile_event || response["profiles"][0]["name"].as<std::string>() == "action");
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
    if (parameter_default >= -1) {
      response.clear();
      assert(control.populate_action_parameters_features(response.to<ArduinoJson::JsonObject>()));
      assert(response["parameters_version"].as<int>() == 1);
      response.clear();
      assert(control.bind_action_parameters_features(
          R"({"v":1,"type":"entity_action_parameters_v1","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","parameters_version":1})",
          response.to<ArduinoJson::JsonObject>()));
    }
#endif
    assert(control.open_panel(2, "card-2", 4));
    const char *wire = nullptr;
    for (const auto &fixture : fixtures)
      if (std::strcmp(fixture.name, name) == 0)
        wire = fixture.wire;
    MaterialEntityControlCodec codec;
    ControlMessage m;
    assert(wire && codec.parse_entity(wire, m));
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
    if (parameter_default >= -1) {
      auto *a = m.state.family_if<Action>();
      a->parameters_version = 1;
      a->parameter_label.assign("Confirm");
      a->parameter_default = parameter_default < 0 ? Action::UNKNOWN_BOOL : parameter_default;
    }
#endif
    m.kind = ControlKind::CONTROL_KIND_GRANT;
    m.identity = control.owner().identity();
    m.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    receive(m);
    assert(control.resume_control_view(control.get_selection_generation()));
    rt::advance(300);
    assert(control.ready() && control.is_action_view() && control.owner().is_view_ready());
    assert(control.view_lifecycle().is_backend(&control));
    assert(lv_obj_is_visible(common[0]) && !lv_obj_is_visible(widgets.hit));
  }
  ~Harness() {
    const auto detach = [&](auto &&self, lv_obj_t *object) -> void {
      lv_obj_remove_event_cb_with_user_data(object, nullptr, &control);
      for (uint32_t index = 0; index < lv_obj_get_child_count(object); index++)
        self(self, lv_obj_get_child(object, index));
    };
    detach(detach, widgets.root);
  }
  void receive(const ControlMessage &m) {
    MaterialEntityControlCodec codec;
    char wire[2049];
    size_t len;
    assert(codec.serialize_entity(m, wire, sizeof(wire), len));
    assert(control.receive_entity({wire, len}));
    lv_refr_now(display.get());
  }
  ControlMessage next() {
    ControlMessage m;
    m.generic = true;
    m.kind = ControlKind::CONTROL_KIND_STATE;
    m.identity = control.owner().identity();
    m.state = control.owner().state();
    m.state.state_seq++;
    return m;
  }
  void press(size_t index) {
    lv_area_t a;
    lv_obj_get_coords(buttons[index], &a);
    pointer.press((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2);
  }
  void tap(size_t index) {
    press(index);
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
#ifdef USE_LVGL_MATERIAL_ACTION_PARAMETERS
  if (name.starts_with("params-")) {
    const bool running = name == "params-stop" || name == "params-overflow-stop";
    const int value = name == "params-overflow-stop" || name == "params-default-false" ? 0
                      : name == "params-default-true"                                  ? 1
                                                                                       : -1;
    Harness f(running ? "script-running.json" : "script-idle.json", true, false, value);
    assert(lv_obj_get_style_y(f.buttons[0], LV_PART_MAIN) == 586);
    assert(lv_obj_get_style_y(f.buttons[1], LV_PART_MAIN) == 494);
    assert(std::string(lv_label_get_text(f.labels[1])) == "No" && std::string(lv_label_get_text(f.labels[2])) == "Yes");
    if (name == "params-disabled-appearance") {
      const auto active = lv_obj_get_style_bg_color(f.widgets.on, LV_PART_MAIN);
      const auto neutral = lv_obj_get_style_bg_color(f.widgets.off, LV_PART_MAIN);
      assert(!lv_color_eq(active, neutral));
      assert(lv_obj_has_state(f.buttons[0], LV_STATE_DISABLED));
      assert(lv_color_eq(lv_obj_get_style_bg_color(f.buttons[0], LV_PART_MAIN), neutral));
      lv_refr_now(f.display.get());
      const auto before = f.lvgl.presented_pixels;
      const auto count = f.lvgl.native_flushes;
      for (int i = 0; i < 5; i++) { f.control.refresh(); lv_refr_now(f.display.get()); }
      assert(f.lvgl.native_flushes == count);
      f.tap(2);
      assert(!lv_obj_has_state(f.buttons[0], LV_STATE_DISABLED));
      assert(lv_color_eq(lv_obj_get_style_bg_color(f.buttons[0], LV_PART_MAIN), active));
      assert(std::equal(before.begin(), before.begin()+800*400*3, f.lvgl.presented_pixels.begin()));
      const size_t pixel = (624*800+225)*3;
      assert(!std::equal(before.begin()+pixel, before.begin()+pixel+3, f.lvgl.presented_pixels.begin()+pixel));
      const auto enabled_count = f.lvgl.native_flushes;
      for (int i = 0; i < 5; i++) { f.control.refresh(); lv_refr_now(f.display.get()); }
      assert(f.lvgl.native_flushes == enabled_count);
      f.tap(0);
      assert(f.commands() == 1 && f.messages.back().panel_value == 1 && f.messages.back().panel_enabled);
      assert(lv_obj_has_state(f.buttons[0], LV_STATE_DISABLED));
      assert(lv_color_eq(lv_obj_get_style_bg_color(f.buttons[0], LV_PART_MAIN), neutral));
    } else if (name == "params-overflow-stop") {
      f.control.set_text_provider([](uint8_t key) -> const char * {
        static const std::string long_text(160, 'W');
        return key == 58 || key == 61 ? long_text.c_str() : nullptr;
      });
      f.control.refresh();
      assert(std::string(lv_label_get_text(f.common[6])) == "--");
      assert(!f.control.owner().can_choose_action_parameter());
      assert(f.control.owner().can_action(Op::CONTROL_OPERATION_ACTION_STOP));
      f.tap(0);
      assert(f.commands() == 1 && f.messages.back().operation == Op::CONTROL_OPERATION_ACTION_STOP);
    } else if (name == "params-stop") {
      assert(lv_obj_has_state(f.buttons[1], LV_STATE_DISABLED));
      assert(f.control.owner().action_parameter_value() == 255);
      f.tap(0);
      assert(f.commands() == 1 && f.messages.back().operation == Op::CONTROL_OPERATION_ACTION_STOP);
      assert(f.messages.back().panel_value == 0);
    } else if (name.starts_with("params-default-")) {
      assert(f.control.owner().action_parameter_is_default());
      assert(!lv_obj_has_state(f.buttons[0], LV_STATE_DISABLED));
      assert(lv_color_eq(lv_obj_get_style_bg_color(f.buttons[0], LV_PART_MAIN),
                        lv_obj_get_style_bg_color(f.widgets.on, LV_PART_MAIN)));
      assert(std::string(lv_label_get_text(f.common[6])).find("Default") != std::string::npos);
      f.tap(0);
      assert(f.commands() == 1 && f.messages.back().panel_value == 1 &&
             f.messages.back().panel_enabled == (value != 0));
    } else {
      assert(lv_obj_has_state(f.buttons[0], LV_STATE_DISABLED));
      f.tap(1);
      assert(f.commands() == 0 && f.control.owner().action_parameter_value() == 0);
      assert(!lv_obj_has_state(f.buttons[0], LV_STATE_DISABLED));
      if (name == "params-aba") {
        f.press(0);
        assert(f.control.choose_for_test(true));
        assert(f.control.choose_for_test(false));
        f.pointer.release();
        assert(f.commands() == 0);
      } else if (name == "params-outside") {
        f.press(2);
        f.pointer.move(20, 20);
        f.pointer.release();
        assert(f.commands() == 0 && f.control.owner().action_parameter_value() == 0);
      } else if (name == "params-semantic") {
        f.press(0);
        auto m = f.next();
        m.state.family_if<Action>()->state_generation++;
        f.receive(m);
        f.pointer.release();
        assert(f.commands() == 0 && f.control.owner().action_parameter_value() == 255);
      } else if (name == "params-telemetry") {
        f.press(0);
        auto m = f.next();
        m.state.family_if<Action>()->current_known = true;
        m.state.family_if<Action>()->current = 2;
        f.receive(m);
        f.pointer.release();
        assert(f.commands() == 1 && !f.messages.back().panel_enabled);
      } else if (name == "params-unfit") {
        f.tap(2);
        lv_obj_set_width(f.common[6], 5);
        f.control.refresh();
        assert(!f.control.owner().can_action(Op::CONTROL_OPERATION_ACTION_RUN));
        assert(!f.control.owner().can_choose_action_parameter());
        for (size_t i = 1; i < 3; i++)
          assert(lv_color_eq(lv_obj_get_style_bg_color(f.buttons[i], LV_PART_MAIN),
                             lv_obj_get_style_bg_color(f.widgets.off, LV_PART_MAIN)));
        f.tap(0);
        assert(f.commands() == 0);
        lv_obj_set_width(f.common[6], 540);
        f.control.refresh();
        assert(f.control.owner().action_parameter_value() == 255 &&
               !f.control.owner().can_action(Op::CONTROL_OPERATION_ACTION_RUN));
        assert(std::string(lv_label_get_text(f.common[6])).find("Choose Yes or No") != std::string::npos);
      } else if (name == "params-retired") {
        auto close = f.next();
        close.kind = ControlKind::CONTROL_KIND_CLOSE;
        close.reason = ControlReason::CONTROL_REASON_NAVIGATION;
        f.receive(close);
        assert(!f.control.owner().has_selection());
        assert(lv_obj_get_style_y(f.buttons[0], LV_PART_MAIN) == 586);
        lv_obj_set_hidden(f.widgets.root, true);
        f.control.refresh();
        assert(lv_obj_get_style_y(f.buttons[0], LV_PART_MAIN) == 494);
        assert(lv_obj_get_style_y(f.buttons[1], LV_PART_MAIN) == 586);
      } else {
        assert(name == "params-choice");
        f.tap(2);
        assert(f.commands() == 0 && f.control.owner().action_parameter_value() == 1);
        f.tap(0);
        assert(f.commands() == 1 && f.messages.back().panel_value == 1 && f.messages.back().panel_enabled);
      }
    }
    std::cout << "RESULT " << name << " pass=1 component=" << sizeof(Control) << "\n";
    return 0;
  }
#endif
  const bool tile_event = name.starts_with("tile-");
  const char *fixture = tile_event                 ? (name == "tile-readonly" ? "tile-read-only" : "tile-first-use")
                        : name == "dispatched"     ? "automation-enabled.json"
                        : name == "stop-disabled"  ? "automation-disabled-stop-request.json"
                        : name == "script-blocked" ? "script-required.json"
                        : name == "script-running" ? "script-running.json"
                        : name == "unknown"        ? "script-unknown.json"
                        : name == "scene"          ? "scene-invalid-timestamp.json"
                                                   : "button-first-use.json";
  Harness f(fixture, name == "dispatched", tile_event);
#ifdef USE_LVGL_MATERIAL_TILE_EVENT_CONTROLS
  if (tile_event) {
    assert(f.control.is_tile_event_view());
    assert(lv_obj_is_visible(f.icons[0]));
    for (size_t index = 1; index < 4; index++)
      assert(!lv_obj_is_visible(f.icons[index]));
    assert(std::string(lv_label_get_text(f.common[2])) == "Event");
    assert(!lv_obj_is_visible(f.common[5]) && !lv_obj_is_visible(f.common[6]));
    assert(!lv_obj_is_visible(f.buttons[1]) && !lv_obj_is_visible(f.buttons[2]));
    assert(f.commands() == 0);
    if (name == "tile-readonly") {
      assert(!lv_obj_is_visible(f.buttons[0]) && lv_obj_has_state(f.buttons[0], LV_STATE_DISABLED));
      f.tap(0);
      assert(f.commands() == 0);
    } else {
      assert(std::string(lv_label_get_text(f.labels[0])) == "Send event");
      if (name == "tile-source-aba") {
        f.press(0);
        auto b = f.next();
        auto *a = b.state.family_if<Action>();
        a->state_generation++;
        a->source = 1;
        a->phase = 5;
        a->eligible = 0;
        f.receive(b);
        auto restored = f.next();
        a = restored.state.family_if<Action>();
        a->state_generation++;
        a->source = 0;
        a->phase = 0;
        a->eligible = 1;
        f.receive(restored);
        f.pointer.release();
        assert(f.commands() == 0);
      } else if (name == "tile-outside") {
        f.press(0);
        f.pointer.move(20, 20);
        f.pointer.release();
        assert(f.commands() == 0);
      } else {
        f.tap(0);
        assert(f.commands() == 1 && f.messages.back().operation == Op::CONTROL_OPERATION_ACTION_PRESS);
        auto result = f.next();
        result.kind = ControlKind::CONTROL_KIND_RESULT;
        result.state.state_seq = f.control.owner().state().state_seq;
        result.command_seq = f.messages.back().command_seq;
        result.result = ControlResult::CONTROL_RESULT_SERVICE_DISPATCHED;
        f.receive(result);
        assert(std::string(lv_label_get_text(f.common[7])) == "Event sent");
        assert(f.control.ready() && !f.control.owner().is_command_pending());
        assert(!lv_obj_has_state(f.buttons[0], LV_STATE_DISABLED));
      }
    }
    std::cout << "RESULT " << name << " pass=1 component=" << sizeof(Control) << "\n";
    return 0;
  }
#endif
  if (name == "dispatched") {
    f.tap(0);
    assert(f.commands() == 1 && f.messages.back().operation == Op::CONTROL_OPERATION_ACTION_TRIGGER);
    assert(f.control.owner().is_command_pending());
    ControlMessage result;
    result.generic = true;
    result.kind = ControlKind::CONTROL_KIND_RESULT;
    result.identity = f.control.owner().identity();
    result.command_seq = f.messages.back().command_seq;
    result.state.state_seq = f.control.owner().state().state_seq;
    result.result = ControlResult::CONTROL_RESULT_SERVICE_DISPATCHED;
    f.receive(result);
    assert(!f.control.owner().is_command_pending());
    assert(std::string(lv_label_get_text(f.common[7])) == "Request sent to HA");
    assert(f.control.ready() && !lv_obj_has_state(f.buttons[2], LV_STATE_DISABLED));
    f.tap(2);
    assert(f.commands() == 2 && f.messages.back().operation == Op::CONTROL_OPERATION_ACTION_DISABLE);
  } else if (name == "momentary") {
    assert(std::string(lv_label_get_text(f.common[3])).empty());
    assert(std::string(lv_label_get_text(f.common[4])) == "Last used\nUnknown");
    f.tap(0);
    assert(f.commands() == 1 && f.messages.back().operation == Op::CONTROL_OPERATION_ACTION_PRESS);
  } else if (name == "scene") {
    assert(lv_obj_is_visible(f.icons[1]));
    f.tap(0);
    assert(f.messages.back().operation == Op::CONTROL_OPERATION_ACTION_ACTIVATE);
  } else if (name == "script-running") {
    assert(std::string(lv_label_get_text(f.labels[0])) == "Stop");
    f.tap(0);
    assert(f.messages.back().operation == Op::CONTROL_OPERATION_ACTION_STOP);
  } else if (name == "script-blocked") {
    assert(lv_obj_is_visible(f.buttons[0]) && lv_obj_has_state(f.buttons[0], LV_STATE_DISABLED));
    assert(std::string(lv_label_get_text(f.common[6])) == "Needs parameters");
    f.tap(0);
    assert(f.commands() == 0);
  } else if (name == "stop-disabled") {
    for (auto *b : f.buttons)
      assert(lv_obj_is_visible(b) && !lv_obj_has_state(b, LV_STATE_DISABLED));
    assert(std::string(lv_label_get_text(f.labels[2])) == "Stop and disable");
    f.tap(2);
    assert(f.commands() == 1 && f.messages.back().operation == Op::CONTROL_OPERATION_ACTION_DISABLE);
  } else if (name == "unknown") {
    assert(f.control.ready());
    for (auto *b : f.buttons)
      assert(lv_obj_has_state(b, LV_STATE_DISABLED));
  } else if (name == "metadata-hold") {
    f.press(0);
    assert(f.control.captured());
    auto m = f.next();
    auto *a = m.state.family_if<Action>();
    a->last_used_status = 0;
    a->last_used.assign("2026-11-05T12:00:00+01:00");
    f.receive(m);
    assert(f.control.owner().is_dragging());
    f.pointer.release();
    assert(f.commands() == 1);
    assert(std::string(lv_label_get_text(f.common[4])).find("5 November 2026") != std::string::npos);
  } else if (name == "semantic-cancel") {
    f.press(0);
    auto m = f.next();
    m.state.family_if<Action>()->state_generation++;
    f.receive(m);
    f.pointer.release();
    assert(f.commands() == 0 && !f.control.owner().is_dragging());
  } else if (name == "outside") {
    f.press(0);
    f.pointer.move(20, 20);
    f.pointer.release();
    assert(f.commands() == 0);
  } else if (name == "retired") {
    f.tap(0);
    ControlMessage close;
    close.generic = true;
    close.kind = ControlKind::CONTROL_KIND_CLOSE;
    close.identity = f.control.owner().identity();
    close.reason = ControlReason::CONTROL_REASON_ENTITY_UNAVAILABLE;
    f.receive(close);
    assert(!f.control.owner().has_selection() && !f.control.ready());
    assert(f.control.is_action_view());
    assert(std::string(lv_label_get_text(f.common[3])) == "--");
    for (auto *b : f.buttons)
      assert(lv_obj_has_state(b, LV_STATE_DISABLED));
  } else if (name == "palette") {
    auto *icon = lv_obj_create(f.buttons[0]);
    lv_obj_set_size(icon, 12, 12);
    lv_obj_set_style_bg_color(f.widgets.on, lv_color_hex(0xA0D0F0), LV_PART_MAIN);
    lv_obj_set_style_text_color(f.widgets.on_label, lv_color_hex(0x152535), LV_PART_MAIN);
    lv_obj_set_style_text_color(f.widgets.on_label, lv_color_hex(0xFFFFFF), LV_STATE_DISABLED);
    lv_obj_add_state(f.widgets.on_label, LV_STATE_DISABLED);
    f.control.refresh();
    assert(lv_color_eq(lv_obj_get_style_text_color(icon, LV_PART_MAIN), lv_color_hex(0x152535)));
  } else if (name == "unfit") {
    f.control.set_text_provider([](uint8_t key) -> const char * { return key == 31 ? "\xE4\xB8\xAD" : nullptr; });
    f.control.refresh();
    assert(!f.control.ready() && lv_obj_has_state(f.buttons[0], LV_STATE_DISABLED));
    f.tap(0);
    assert(f.commands() == 0);
  } else if (name == "multiline-guard") {
    assert(!f.control.fit_for_test(f.common[4], "A\nB", false));
    assert(f.control.fit_for_test(f.common[4], "A\nB", true));
    assert(!f.control.fit_for_test(f.common[4], "A\rB", true));
  } else if (name == "unchanged") {
    lv_refr_now(f.display.get());
    const auto count = f.lvgl.native_flushes;
    for (int i = 0; i < 5; i++) {
      f.control.refresh();
      lv_refr_now(f.display.get());
    }
    assert(f.lvgl.native_flushes == count);
  } else
    assert(false);
  std::cout << "RESULT " << name << " pass=1 component=" << sizeof(Control) << "\n";
}
