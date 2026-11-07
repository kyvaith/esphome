#include "real_lvgl_test_support.h"
#include "material_entity_control_component.h"
#include "color_fixtures.h"
#include <cstring>
#include <iostream>
#include <map>
using namespace esphome::lvgl_material;
namespace rt = real_lvgl_test;
#include "color_fonts.h"
struct Control : MaterialEntityControlComponent {
  using MaterialEntityControlComponent::MaterialEntityControlComponent;
  bool ready() const { return light_color_ready_; }
  bool captured() const { return captured_target_ != nullptr; }
  bool scalar_ready() { return owner_.set_scalar_ready(true); }
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
  Fixture() {
    navigation.active = &application;
    w["BASE"] = widgets.root;
#include "color_widgets.h"
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
    control.set_text_provider([this](uint8_t id) -> const char * {
      if (control.is_light_color_view()) {
        switch (id) {
          case 14:
            return "Light color";
          case 15:
            return "Hue";
          case 16:
            return "Saturation";
          case 18:
            return "Unsaved color";
          case 19:
            return "Current color";
          case 20:
            return "Current white temperature";
          case 21:
            return polish ? "Prze\xC5\x82\xC4\x85\x63z na kolor" : "Switch to color";
          case 22:
            return "Turn the light on first";
          case 25:
            return "Apply";
          case 26:
            return "Cancel";
          case 28:
            return "Unsupported color mode";
          case 29:
            return "Color unavailable";
          case 30:
            return "Brightness unavailable";
          case 31:
            return "Source unavailable";
          case 32:
            return polish ? "," : ".";
          default:
            return nullptr;
        }
      }
      if (id == 59)
        return "Color";
      if (id == 60)
        return "White";
      return nullptr;
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
    assert(control.bind_detail_color_features(
        R"({"v":1,"type":"entity_detail_color_v1","color_version":1,"channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})",
        r.to<ArduinoJson::JsonObject>()));
    assert(control.open_light_detail(2, "card-2", 4));
    MaterialEntityControlCodec codec;
    ControlMessage m;
    assert(codec.parse_entity(COLOR_FIXTURES[0], m));
    m.identity = control.owner().identity();
    m.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    auto &d = *m.state.family_if<ControlDetailSnapshot>();
    d.facets = 7;
    receive(m);
    assert(control.resume_control_view(control.get_selection_generation()));
    assert(control.scalar_ready());
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
  void open_color(uint8_t mode = 0) {
    assert(lv_obj_get_width(w.at("light_detail_kelvin")) == 156);
    tap("light_detail_color");
    assert(messages.back().kind == ControlKind::CONTROL_KIND_FACET);
    auto request = messages.back();
    MaterialEntityControlCodec codec;
    ControlMessage m;
    assert(codec.parse_entity(COLOR_FIXTURES[1], m));
    m.identity = control.owner().identity();
    m.state.state_seq = control.owner().state().state_seq + 1;
    auto &d = *m.state.family_if<ControlDetailSnapshot>();
    d.facets = 7;
    d.view_seq = request.view_seq;
    std::get<ControlFacetColor>(d.data).source_mode = mode;
    receive(m);
    ControlMessage ack;
    ack.generic = true;
    ack.kind = ControlKind::CONTROL_KIND_FACET_RESULT;
    ack.identity = m.identity;
    ack.facet = ControlFacet::CONTROL_FACET_COLOR;
    ack.view_seq = d.view_seq;
    ack.state.state_seq = m.state.state_seq;
    ack.facet_ready = true;
    receive(ack);
    control.loop();
    rt::advance(200);
    if (!control.ready()) {
      for (auto id :
           {"kind", "title", "preview", "status", "hue_label", "saturation_label", "cancel_label", "apply_label"}) {
        std::string key = std::string("light_color_") + id;
        auto *o = w.at(key);
        std::cerr << key << "=" << lv_label_get_text(o) << " content=" << lv_obj_get_content_width(o) << "x"
                  << lv_obj_get_content_height(o) << "\n";
      }
    }
    assert(control.owner().is_view_ready());
    assert(control.ready());
    assert(lv_obj_is_visible(w.at("light_color_root")));
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
  void drag(int x = 590) {
    auto p = center("light_color_hue_slider");
    pointer.press(p.x, p.y);
    pointer.move(x, p.y);
    pointer.release();
    control.loop();
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
    Fixture f;
    f.open_color(name == "ct_seed" ? 3 : 0);
    if (name == "open") {
      assert(std::strcmp(f.text("light_color_preview"), "#4CFF5C") == 0);
      assert(std::strstr(f.text("light_color_hue_label"), "125.5") != nullptr);
      assert(std::strstr(f.text("light_color_saturation_label"), "70.3") != nullptr);
      assert(lv_obj_has_state(f.w.at("light_color_apply"), LV_STATE_DISABLED));
      assert(!lv_obj_has_flag(f.w.at("light_color_hue_slider"), LV_OBJ_FLAG_CLICKABLE));
    } else if (name == "drag_apply") {
      f.drag();
      auto d = f.control.owner().detail_color_draft();
      assert(d.active && d.hue_milli != 125500 && d.saturation_milli == 70250);
      assert(f.commands() == 0);
      f.tap("light_color_apply");
      assert(f.commands() == 1);
      auto &m = f.messages.back();
      assert(m.operation == ControlOperation::CONTROL_OPERATION_FACET_COLOR);
      assert(m.state.family_if<ControlDetailSnapshot>()->color()->hue_milli == d.hue_milli);
      f.tap("light_color_apply");
      assert(f.commands() == 1);
    } else if (name == "cancel") {
      f.drag();
      f.tap("light_color_cancel");
      assert(f.commands() == 0);
      assert(f.messages.back().kind == ControlKind::CONTROL_KIND_FACET);
      assert(f.messages.back().facet == ControlFacet::CONTROL_FACET_MAIN);
      assert(!f.control.owner().detail_color_draft().active);
    } else if (name == "ct_seed") {
      f.polish = true;
      f.control.refresh_light_color_palette();
      assert(f.control.ready());
      assert(!lv_obj_has_state(f.w.at("light_color_apply"), LV_STATE_DISABLED));
      assert(std::strstr(f.text("light_color_hue_label"), "125,5"));
      f.tap("light_color_apply");
      assert(f.commands() == 1);
      auto *c = f.messages.back().state.family_if<ControlDetailSnapshot>()->color();
      assert(c->hue_milli == 125500 && c->saturation_milli == 70250);
    } else if (name == "metadata") {
      f.drag();
      auto d = f.control.owner().detail_color_draft();
      auto m = f.next();
      m.state.title.assign("Renamed light");
      f.receive(m);
      assert(f.control.owner().detail_color_draft().hue_milli == d.hue_milli);
      assert(f.control.owner().detail_color_draft().active);
    } else if (name == "aba" || name == "held_apply") {
      f.drag();
      if (name == "held_apply")
        f.press("light_color_apply");
      else
        f.press("light_color_hue_slider");
      auto m = f.next();
      m.state.family_if<ControlDetailSnapshot>()->state_generation++;
      f.receive(m);
      f.pointer.release();
      f.control.loop();
      assert(!f.control.owner().detail_color_draft().active);
      assert(f.commands() == 0);
      assert(std::strcmp(f.text("light_color_preview"), "#4CFF5C") == 0);
    } else if (name == "readonly") {
      auto m = f.next();
      auto &d = *m.state.family_if<ControlDetailSnapshot>();
      d.state_generation++;
      d.editable = false;
      auto &c = std::get<ControlFacetColor>(d.data);
      c.reason = 4;
      f.receive(m);
      f.drag();
      f.tap("light_color_apply");
      assert(f.commands() == 0 && !f.control.owner().detail_color_draft().active);
      assert(f.control.ready());
    } else if (name == "palette") {
      f.drag();
      auto d = f.control.owner().detail_color_draft();
      std::string before = f.text("light_color_preview");
      lv_obj_set_style_bg_color(f.w.at("light_color_preview"), lv_color_hex(0x123456), 0);
      lv_obj_set_style_text_color(f.w.at("light_color_preview"), lv_color_hex(0x123456), 0);
      f.control.refresh_light_color_palette();
      assert(before == f.text("light_color_preview"));
      assert(f.control.owner().detail_color_draft().hue_milli == d.hue_milli);
      assert(lv_color_to_u32(lv_obj_get_style_bg_color(f.w.at("light_color_preview"), LV_PART_MAIN)) != 0x123456);
      assert(f.commands() == 0);
    } else if (name == "source_rgb") {
      auto m = f.next();
      auto &d = *m.state.family_if<ControlDetailSnapshot>();
      d.state_generation++;
      auto &c = std::get<ControlFacetColor>(d.data);
      c.rgb = {128, 0, 0};
      c.hue_milli = 0;
      c.saturation_milli = 100000;
      f.receive(m);
      assert(std::strcmp(f.text("light_color_preview"), "#800000") == 0);
      f.drag();
      assert(std::strcmp(f.text("light_color_preview"), "#800000") != 0);
      assert(f.commands() == 0);
    } else if (name == "outside") {
      auto p = f.center("light_color_hue_slider");
      f.pointer.press(p.x, p.y);
      f.pointer.move(900, 538);
      f.pointer.release();
      f.control.loop();
      auto d = f.control.owner().detail_color_draft();
      assert(d.active && d.hue_milli == 0 && d.saturation_milli == 70250);
      assert(f.commands() == 0);
    } else if (name == "long_title") {
      auto m = f.next();
      m.state.title.assign(std::string(120, 'W'));
      f.receive(m);
      assert(f.control.ready());
      assert(std::strcmp(f.text("light_color_title"), "--") != 0);
      assert(std::strstr(f.text("light_color_title"), "..."));
      f.drag();
      assert(f.control.owner().detail_color_draft().active && f.commands() == 0);
    } else if (name == "glyph") {
      auto m = f.next();
      m.state.title.assign("Missing \xE4\xB8\xAD");
      f.receive(m);
      assert(!f.control.ready());
      f.drag();
      f.tap("light_color_apply");
      assert(f.commands() == 0);
    } else
      assert(false);
    std::cout << "RESULT " << name << " pass=1\n";
  }
  lv_deinit();
  return 0;
}
