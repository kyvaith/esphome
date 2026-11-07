#include "real_lvgl_test_support.h"
#include "material_entity_control_component.h"
#include "range_fixtures.h"
#include <cstring>
#include <iostream>
#include <map>
#include <cmath>
using namespace esphome::lvgl_material;
namespace rt = real_lvgl_test;
#include "range_fonts.h"
struct Control : MaterialEntityControlComponent {
  using MaterialEntityControlComponent::MaterialEntityControlComponent;
  bool upper() const { return climate_range_upper_; }
  bool captured() const { return captured_target_ != nullptr; }
  bool scalar_ready() { return owner_.set_scalar_ready(true); }
  void cancel_pointer() { cancel_pointer_(); }
};
struct Fixture {
  esphome::lvgl::LvglComponent lvgl;
  rt::HostDisplay display{lvgl};
  rt::NativeWidgets widgets{lv_screen_active()};
  rt::Pointer pointer{display.get()};
  esphome::lvgl::LvglNavigation navigation;
  esphome::lvgl::LvglApplication application;
  Control control{&lvgl};
  std::map<std::string, lv_obj_t *> w;
  std::vector<ControlMessage> messages;
  bool polish = false;
  Fixture(bool enabled = true, const std::string &scenario = "") {
    navigation.active = &application;
    w["BASE"] = widgets.root;
#include "range_widgets.h"
    control.set_widgets(widgets.root, widgets.hit, widgets.title, widgets.value, widgets.unknown, widgets.on,
                        widgets.off);
    control.set_state_label(widgets.state);
    control.set_status_label(widgets.status);
    control.set_number_profile(&navigation, &application, widgets.on_label, widgets.off_label, LV_FONT_DEFAULT);
#include "detail_widget_bindings.h"
    control.set_light_color_profile(w["light_color_root"], w["light_color_kind"], w["light_color_title"],
                                    w["light_color_preview"], w["light_color_status"], w["light_color_hue_label"],
                                    w["light_color_hue_slider"], w["light_color_saturation_label"],
                                    w["light_color_saturation_slider"], w["light_color_cancel"],
                                    w["light_color_cancel_label"], w["light_color_apply"], w["light_color_apply_label"],
                                    w["light_detail_color"], w["light_detail_color_label"], 20, true);
    control.set_climate_range_profile(w["climate_detail_range"], w["climate_detail_range_label"], enabled);
    control.set_text_provider([this](uint8_t id) -> const char * {
      switch (id) {
        case 19:
          return polish ? "Zakres" : "Range";
        case 29:
          return "Back";
        case 61:
          return "Lower";
        case 62:
          return "Upper";
        case 63:
          return "Lower limit";
        case 64:
          return "Upper limit";
        case 65:
          return "Select heat/cool mode";
        case 66:
          return "Range unavailable";
        case 36:
          return polish ? "," : ".";
        default:
          return nullptr;
      }
    });
    control.set_initial_enabled(true);
    control.add_on_message_callback([this](const char *p) {
      MaterialEntityControlCodec codec;
      ControlMessage m;
      assert(codec.parse_entity(p, m));
      messages.push_back(m);
    });
    control.setup();
    assert(!control.is_failed());
    rt::advance(200);
    assert(control.bind_entity_json(
        R"({"v":1,"type":"entity_control_v1","kind":"bind","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})"));
    ArduinoJson::JsonDocument r;
    assert(control.bind_detail_features(
        R"({"v":1,"type":"entity_detail_features_v1","detail_version":1,"light_detail_version":1,"climate_panel_version":1,"channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})",
        r.to<ArduinoJson::JsonObject>()));
    assert(
        control.bind_detail_range_features(
            R"({"v":1,"type":"entity_detail_range_v1","range_version":1,"channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})",
            r.to<ArduinoJson::JsonObject>()) == enabled);
    assert(control.open_panel(2, "card-2", 4));
    MaterialEntityControlCodec codec;
    ControlMessage m;
    assert(codec.parse_entity(RANGE_FIXTURES[0], m));
    m.identity = control.owner().identity();
    m.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    auto &d = *m.state.family_if<ControlDetailSnapshot>();
    d.facets = enabled ? 140 : 12;
    auto &main = std::get<ControlClimateMain>(d.data);
    if (scenario == "main_negative") {
      polish = true;
      main.low.assign("-2.5");
      main.high.assign("0");
    } else if (scenario == "main_long") {
      polish = true;
      main.low.assign("-99.999999");
      main.high.assign("299.999999");
    } else if (scenario == "main_unknown") {
      main.high.assign("");
    } else if (scenario == "main_inactive") {
      main.mode = 1;
    } else if (scenario == "main_single") {
      main.mode = 3;
      main.target.assign("22.5");
      main.low.assign("");
      main.high.assign("");
      auto &number = main.number;
      number.domain.minimum = 7 * NUMBER_SCALE;
      number.domain.maximum = 35 * NUMBER_SCALE;
      number.domain.step = NUMBER_SCALE / 2;
      number.domain.max_index = 56;
      std::strcpy(number.domain.unit.data(), "\xC2\xB0"
                                             "C");
      number.index = 31;
      number.known = true;
      number.value.assign("22.5");
      d.facets |= 1;
      d.editable = true;
    }
    receive(m);
    assert(control.resume_control_view(control.get_selection_generation()));

    rt::advance(200);
  }
  void receive(const ControlMessage &m, bool accepted = true) {
    MaterialEntityControlCodec codec;
    char b[2049];
    size_t n;
    assert(codec.serialize_entity(m, b, sizeof(b), n));
    assert(control.receive_entity({b, n}) == accepted);
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
  void facet(bool range = true) {
    assert(messages.back().kind == ControlKind::CONTROL_KIND_FACET);
    auto request = messages.back();
    MaterialEntityControlCodec codec;
    ControlMessage m;
    assert(codec.parse_entity(RANGE_FIXTURES[range ? 1 : 0], m));
    m.kind = ControlKind::CONTROL_KIND_STATE;
    m.identity = control.owner().identity();
    m.state.state_seq = control.owner().state().state_seq + 1;
    auto &d = *m.state.family_if<ControlDetailSnapshot>();
    d.view_seq = request.view_seq;
    receive(m);
    ControlMessage ack;
    ack.generic = true;
    ack.kind = ControlKind::CONTROL_KIND_FACET_RESULT;
    ack.identity = m.identity;
    ack.facet = request.facet;
    ack.view_seq = d.view_seq;
    ack.state.state_seq = m.state.state_seq;
    ack.facet_ready = true;
    receive(ack);
    control.loop();
    rt::advance(200);
    assert(control.owner().is_view_ready());
    assert(lv_obj_is_visible(w.at("climate_detail_root")));
  }
  void open_range() {
    assert(lv_obj_get_width(w.at("climate_detail_range")) == 156);
    tap("climate_detail_range");
    facet();
    assert(control.is_climate_range_view());
    assert(commands() == 0);
  }
  lv_point_t center(const char *id) {
    lv_obj_update_layout(widgets.root);
    lv_area_t a;
    lv_obj_get_coords(w.at(id), &a);
    return {(a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2};
  }
  void press(const char *id) {
    auto p = center(id);
    pointer.press(p.x, p.y);
  }
  void tap(const char *id) {
    press(id);
    pointer.release();
    control.loop();
    rt::advance(100);
  }
  void move_arc(int delta) {
    auto *arc = w.at("climate_detail_arc");
    lv_area_t a;
    lv_obj_get_coords(arc, &a);
    int angle = static_cast<int>(lv_arc_get_angle_end(arc)) + delta;
    int radius = lv_obj_get_width(arc) / 2 - 14;
    pointer.move((a.x1 + a.x2) / 2 + static_cast<int>(std::cos(angle * 3.141592653589793 / 180) * radius),
                 (a.y1 + a.y2) / 2 + static_cast<int>(std::sin(angle * 3.141592653589793 / 180) * radius));
  }
  size_t commands() const {
    size_t n = 0;
    for (auto &m : messages)
      n += m.kind == ControlKind::CONTROL_KIND_COMMAND;
    return n;
  }
  const char *text(const char *id) { return lv_label_get_text(w.at(id)); }
  static void detach(lv_obj_t *o, void *data) {
    lv_obj_remove_event_cb_with_user_data(o, nullptr, data);
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++)
      detach(lv_obj_get_child(o, i), data);
  }
  ~Fixture() {
    control.on_shutdown();
    detach(widgets.root, &control);
  }
};
int main(int argc, char **argv) {
  assert(argc == 2);
  std::string name = argv[1];
  lv_init();
  {
    Fixture f(name != "range_disabled", name);
    if (name.starts_with("main_")) {
      const auto *font = lv_obj_get_style_text_font(f.w.at("climate_detail_target"), LV_PART_MAIN);
      if (name == "main_pair" || name == "main_changed") {
        assert(std::strcmp(f.text("climate_detail_target"), "21 - 24 \xC2\xB0"
                                                            "C") == 0);
        assert(lv_obj_has_flag(f.w.at("climate_detail_knob"), LV_OBJ_FLAG_HIDDEN));
        assert(lv_obj_has_state(f.w.at("climate_detail_plus"), LV_STATE_DISABLED));
        f.tap("climate_detail_plus");
        assert(f.commands() == 0);
        if (name == "main_pair") {
          f.open_range();
          assert(std::strcmp(f.text("climate_detail_target"), "21 \xC2\xB0"
                                                              "C") == 0);
          f.tap("climate_detail_range");
          f.facet(false);
          assert(std::strcmp(f.text("climate_detail_target"), "21 - 24 \xC2\xB0"
                                                              "C") == 0);
        } else {
          auto m = f.next();
          auto &d = *m.state.family_if<ControlDetailSnapshot>();
          auto &main = std::get<ControlClimateMain>(d.data);
          d.state_generation++;
          main.low.assign("22");
          main.high.assign("25");
          f.receive(m);
          assert(std::strcmp(f.text("climate_detail_target"), "22 - 25 \xC2\xB0"
                                                              "C") == 0);
          m = f.next();
          auto &next = *m.state.family_if<ControlDetailSnapshot>();
          next.state_generation++;
          std::get<ControlClimateMain>(next.data).low.assign("26");
          f.receive(m);
          assert(std::strcmp(f.text("climate_detail_target"), "-- \xC2\xB0"
                                                              "C") == 0);
        }
      } else if (name == "main_negative") {
        assert(std::strcmp(f.text("climate_detail_target"), "-2,5 - 0 \xC2\xB0"
                                                            "C") == 0);
      } else if (name == "main_long") {
        assert(std::strcmp(f.text("climate_detail_target"), "Zakres") == 0);
        assert(std::strstr(f.text("climate_detail_limits"), "-99,999999 - 299,999999"));
      } else if (name == "main_unknown" || name == "main_inactive") {
        assert(std::strcmp(f.text("climate_detail_target"), "-- \xC2\xB0"
                                                            "C") == 0);
      } else if (name == "main_single") {
        assert(std::strcmp(f.text("climate_detail_target"), "22.5 \xC2\xB0"
                                                            "C") == 0);
        assert(!lv_obj_has_flag(f.w.at("climate_detail_knob"), LV_OBJ_FLAG_HIDDEN));
        assert(!lv_obj_has_state(f.w.at("climate_detail_plus"), LV_STATE_DISABLED));
        f.tap("climate_detail_plus");
        assert(f.commands() == 1);
        assert(f.messages.back().facet_target_index == 32);
      } else
        assert(false);
      assert(lv_obj_get_style_text_font(f.w.at("climate_detail_target"), LV_PART_MAIN) == font);
      assert(f.commands() == (name == "main_single" ? 1 : 0));
    } else if (name == "range_disabled") {
      assert(std::strcmp(f.text("climate_detail_target"), "-- \xC2\xB0"
                                                          "C") == 0);
      assert(!lv_obj_is_visible(f.w.at("climate_detail_range")));
      assert(lv_obj_get_width(f.w.at("climate_detail_mode")) == 244);
      assert(f.commands() == 0);
    } else {
      f.open_range();
      if (name == "open") {
        assert(!f.control.upper());
        assert(std::strstr(f.text("climate_detail_target"), "21"));
        assert(std::strstr(f.text("climate_detail_limits"), "21 - 24"));
        assert(std::strstr(f.text("climate_detail_current"), "23"));
        assert(lv_obj_get_width(f.w.at("climate_detail_arc")) == 600);
      } else if (name == "lower" || name == "upper") {
        if (name == "upper")
          f.tap("climate_detail_parameters");
        assert(f.control.upper() == (name == "upper"));
        assert(f.commands() == 0);
        f.tap("climate_detail_plus");
        assert(f.commands() == 1);
        const auto &r = *f.messages.back().state.family_if<ControlDetailSnapshot>()->range();
        assert(r.low_index == (name == "lower" ? 29 : 28));
        assert(r.high_index == (name == "upper" ? 35 : 34));
      } else if (name == "drag" || name == "clamp") {
        f.press("climate_detail_knob");
        assert(f.control.captured());
        f.move_arc(name == "clamp" ? 100 : 15);
        assert(f.commands() == 0);
        f.pointer.release();
        f.control.loop();
        assert(f.commands() == 1);
        const auto &r = *f.messages.back().state.family_if<ControlDetailSnapshot>()->range();
        assert(r.low_index > 28 && r.low_index <= 34 && r.high_index == 34);
        if (name == "clamp")
          assert(r.low_index == 34);
      } else if (name == "cancel") {
        f.press("climate_detail_knob");
        f.move_arc(15);
        f.control.cancel_pointer();
        f.pointer.release();
        f.control.loop();
        assert(f.commands() == 0);
      } else if (name == "metadata" || name == "aba" || name == "same_generation") {
        f.press("climate_detail_knob");
        f.move_arc(15);
        auto m = f.next();
        auto &d = *m.state.family_if<ControlDetailSnapshot>();
        auto &r = std::get<ControlFacetRange>(d.data);
        if (name == "metadata") {
          r.current.assign("22.5");
          r.activity = 3;
          m.state.title.assign("Metadata");
        } else if (name == "aba")
          d.state_generation++;
        else
          r.low_index++;
        f.receive(m, name != "same_generation");
        f.pointer.release();
        f.control.loop();
        assert(f.commands() == (name == "metadata" ? 1 : 0));
      } else if (name == "readonly") {
        auto m = f.next();
        auto &d = *m.state.family_if<ControlDetailSnapshot>();
        auto &r = std::get<ControlFacetRange>(d.data);
        d.state_generation++;
        d.editable = false;
        r.mode = 1;
        r.reason = 1;
        f.receive(m);
        assert(lv_obj_has_state(f.w.at("climate_detail_plus"), LV_STATE_DISABLED));
        f.tap("climate_detail_plus");
        f.tap("climate_detail_parameters");
        assert(f.control.upper());
        assert(f.commands() == 0);
      } else if (name == "back") {
        f.tap("climate_detail_parameters");
        assert(f.control.upper());
        f.tap("climate_detail_range");
        assert(f.messages.back().facet == ControlFacet::CONTROL_FACET_MAIN);
        f.facet(false);
        assert(f.commands() == 0);
        assert(std::strcmp(f.text("climate_detail_mode_label"), "Mode") == 0);
        f.open_range();
        assert(!f.control.upper());
      } else
        assert(false);
    }
  }
  lv_deinit();
  std::cout << "RESULT " << name << " pass=1\n";
}
