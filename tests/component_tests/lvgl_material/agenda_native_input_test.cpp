#include "real_lvgl_test_support.h"
#include "material_entity_control_component.h"
#include <iostream>
#include <map>
using namespace esphome::lvgl_material;
namespace rt = real_lvgl_test;
using Extra = ControlExtraSnapshot;
using Agenda = Extra::Agenda;
#include "agenda_fonts.h"
struct Control : MaterialEntityControlComponent {
  using MaterialEntityControlComponent::MaterialEntityControlComponent;
  void refresh() { this->update_static_(); }
  bool ready() const { return this->agenda_ready_; }
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
  Fixture(bool admitted = true) {
    navigation.active = &application;
    w["BASE"] = widgets.root;
#include "agenda_widgets.h"
    control.set_widgets(widgets.root, widgets.hit, widgets.title, widgets.value, widgets.unknown, widgets.on,
                        widgets.off);
    control.set_state_label(widgets.state);
    control.set_status_label(widgets.status);
    control.set_number_profile(&navigation, &application, widgets.on_label, widgets.off_label, LV_FONT_DEFAULT);
    control.set_extra_profile(w["extra_control_root"], w["extra_control_title"], w["extra_control_kind"],
                              w["extra_control_updated"], w["extra_control_status"]);
    control.set_extra_sensor_profile(w["extra_sensor_root"], w["extra_sensor_value"], w["extra_sensor_unit"],
                                     w["extra_sensor_icon_unknown"], w["extra_sensor_icon_temperature"],
                                     w["extra_sensor_icon_humidity"], font_for("roboto_24"));
    control.set_extra_state_profile(
        w["extra_state_root"], w["extra_state_value"], w["extra_surface_binary"], w["extra_surface_alarm"],
        w["extra_surface_lock"], w["extra_state_icon_unknown"], w["extra_state_icon_moisture"],
        w["extra_state_icon_alarm"], w["extra_state_icon_lock_closed"], w["extra_state_icon_lock_open"],
        w["extra_state_icon_calendar"], w["extra_lock_button"], w["extra_lock_label"], font_for("roboto_24"));
    control.set_extra_calendar_profile(w["extra_calendar_root"], w["extra_calendar_badge"], w["extra_calendar_summary"],
                                       w["extra_calendar_start"], w["extra_calendar_end"], w["extra_calendar_zone"],
                                       w["extra_calendar_location"], w["extra_calendar_description"],
                                       w["extra_calendar_excerpt"], font_for("roboto_24"));
    control.set_extra_profiles(true, true);
    control.set_agenda_profile(w["extra_agenda_previous"], w["extra_agenda_previous_label"], w["extra_agenda_next"],
                               w["extra_agenda_next_label"], admitted);
    control.set_initial_enabled(true);
    control.set_text_provider([](uint8_t id) -> const char * {
      switch (id) {
        case 93:
          return "Agenda";
        case 94:
          return "Poprzednie";
        case 95:
          return "Nast\xC4\x99pne";
        case 96:
          return "\xC5\x81"
                 "adowanie wydarze\xC5\x84\xE2\x80\xA6";
        case 97:
          return "Brak wydarze\xC5\x84 w tym okresie";
        case 98:
          return "Nie uda\xC5\x82o si\xC4\x99 pobra\xC4\x87 wydarze\xC5\x84";
        case 99:
          return "Pierwsze 16 wydarze\xC5\x84";
        case 100:
          return "Nie uda\xC5\x82o si\xC4\x99 zmieni\xC4\x87 wydarzenia";
        default:
          return nullptr;
      }
    });
    control.add_on_message_callback([this](const char *wire) {
      MaterialEntityControlCodec c;
      ControlMessage m;
      assert(c.parse_entity(wire, m));
      messages.push_back(m);
    });
    control.setup();
    assert(!control.is_failed());
    rt::advance(100);
    assert(control.bind_entity_json(
        R"({"v":1,"type":"entity_control_v1","kind":"bind","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})"));
    ArduinoJson::JsonDocument response;
    assert(control.bind_extra_features(
        R"({"v":1,"type":"entity_panel_extra_v1","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","profiles":[{"name":"info","version":1},{"name":"lock","version":1}]})",
        response.to<ArduinoJson::JsonObject>()));
    assert(control.populate_info_agenda_features(response.to<ArduinoJson::JsonObject>()) == admitted);
    assert(response["agenda_version"].as<int>() == int(admitted));
    assert(
        control.bind_info_agenda_features(
            R"({"v":1,"type":"entity_info_agenda_v1","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","agenda_version":1})",
            response.to<ArduinoJson::JsonObject>()) == admitted);
    if (admitted)
      open();
  }
  ~Fixture() {
    control.disconnect();
    std::function<void(lv_obj_t *)> detach = [&](lv_obj_t *obj) {
      for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++)
        detach(lv_obj_get_child(obj, i));
      lv_obj_remove_event_cb_with_user_data(obj, nullptr, &control);
    };
    detach(widgets.root);
  }
  void receive(ControlMessage m) {
    MaterialEntityControlCodec c;
    char wire[2049];
    size_t length;
    assert(c.serialize_entity(m, wire, sizeof(wire), length));
    assert(control.receive_entity({wire, length}));
    lv_refr_now(display.get());
  }
  void open() {
    assert(control.open_panel(12, "agenda", 4));
    ControlMessage m;
    m.generic = true;
    m.kind = ControlKind::CONTROL_KIND_GRANT;
    m.identity = control.owner().identity();
    m.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    m.state.state_seq = 1;
    m.state.title.assign("Agenda domu");
    auto &e = m.state.emplace_family<Extra>();
    e.type = 7;
    e.source = 0;
    e.state_generation = 1;
    auto &a = e.data.emplace<Agenda>();
    a.status = 1;
    a.count = 2;
    a.index = 0;
    a.all_day = 0;
    a.summary.assign("Spotkanie rodzinne");
    a.start.assign("2026-11-07 18:00 +01:00");
    a.end.assign("2026-11-07 19:00 +01:00");
    a.zone.assign("Europe/Warsaw");
    a.location.assign("Dom");
    a.description.assign("Wspolna kolacja");
    receive(m);
    assert(control.resume_control_view(control.get_selection_generation()));
    rt::advance(200);
    control.refresh();
    assert(control.owner().is_view_ready());
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
  std::string text(const char *id) { return lv_label_get_text(w.at(id)); }
  bool visible(const char *id) { return lv_obj_is_visible(w.at(id)); }
  void press(const char *id = "extra_agenda_next") {
    lv_area_t a;
    lv_obj_get_coords(w.at(id), &a);
    pointer.press((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2);
  }
  size_t pages() {
    return std::count_if(messages.begin(), messages.end(),
                         [](const auto &m) { return m.kind == ControlKind::CONTROL_KIND_PAGE; });
  }
};
int main(int argc, char **argv) {
  assert(argc == 2);
  lv_init();
  std::string name = argv[1];
  Fixture f(name != "disabled");
  if (name == "disabled") {
    assert(!f.control.owner().info_agenda_bound());
    std::cout << "RESULT disabled pass=1\n";
    return 0;
  }
  assert(f.visible("extra_calendar_root") && !f.visible("extra_control_updated"));
  if (!f.control.ready()) {
    std::cerr << "not ready: " << f.text("extra_control_status") << "\n";
    return 1;
  }
  assert(lv_obj_has_state(f.w["extra_agenda_previous"], LV_STATE_DISABLED));
  assert(!lv_obj_has_state(f.w["extra_agenda_next"], LV_STATE_DISABLED));
  if (name == "ready") {
    assert(f.text("extra_calendar_badge") == "1 / 2 \xC2\xB7 Event");
    assert(f.pages() == 0);
  } else if (name == "page") {
    f.press();
    assert(f.pages() == 0);
    f.pointer.release();
    assert(f.pages() == 1 && f.control.owner().agenda_page_pending());
    auto s = f.next();
    auto &a = std::get<Agenda>(s.state.family_if<Extra>()->data);
    a.index = 1;
    a.request_seq = f.messages.back().command_seq;
    a.summary.assign("Drugie wydarzenie");
    f.receive(s);
    assert(f.control.owner().agenda_page_pending());
    ControlMessage r;
    r.generic = true;
    r.kind = ControlKind::CONTROL_KIND_PAGE_RESULT;
    r.identity = f.control.owner().identity();
    r.command_seq = a.request_seq;
    r.value = 1;
    r.enabled = true;
    r.state.state_seq = s.state.state_seq;
    f.receive(r);
    assert(!f.control.owner().agenda_page_pending());
    assert(f.text("extra_calendar_summary") == "Drugie wydarzenie");
    assert(lv_obj_has_state(f.w["extra_agenda_next"], LV_STATE_DISABLED));
    assert(!lv_obj_has_state(f.w["extra_agenda_previous"], LV_STATE_DISABLED));
  } else if (name == "slide") {
    f.press();
    f.pointer.move(730, 590);
    f.pointer.move(530, 670);
    f.pointer.release();
    assert(f.pages() == 0);
  } else if (name == "changed" || name == "metadata") {
    f.press();
    auto s = f.next();
    s.state.title.assign("Updated name");
    if (name == "changed")
      s.state.family_if<Extra>()->state_generation++;
    f.receive(s);
    f.pointer.release();
    assert(f.pages() == (name == "metadata" ? 1 : 0));
  } else if (name == "empty" || name == "error") {
    auto s = f.next();
    auto &e = *s.state.family_if<Extra>();
    e.state_generation++;
    auto &a = e.data.emplace<Agenda>();
    a.status = name == "empty" ? 1 : 2;
    f.receive(s);
    assert(!f.visible("extra_calendar_root") && f.visible("extra_state_root"));
    assert(f.text("extra_calendar_summary").empty());
    f.press();
    f.pointer.release();
    assert(f.pages() == 0);
  } else if (name == "limited") {
    auto s = f.next();
    auto &e = *s.state.family_if<Extra>();
    e.state_generation++;
    auto &a = std::get<Agenda>(e.data);
    a.count = 16;
    a.has_more = true;
    a.all_day = 1;
    a.truncated = 1;
    f.receive(s);
    assert(f.text("extra_calendar_badge").starts_with("1 / 16+"));
    assert(!f.text("extra_control_status").empty());
    f.press();
    f.pointer.release();
    assert(f.text("extra_control_status").find("16") == std::string::npos);
  } else if (name == "retired") {
    f.press();
    f.control.disconnect();
    f.pointer.release();
    assert(f.pages() == 0 && !f.visible("extra_agenda_next") && f.text("extra_calendar_summary").empty());
  } else if (name == "palette") {
    lv_obj_set_style_bg_color(f.widgets.on, lv_color_hex(0xB6E4FA), 0);
    lv_obj_set_style_text_color(f.widgets.on_label, lv_color_hex(0x071018), 0);
    lv_obj_set_style_bg_color(f.widgets.off, lv_color_hex(0x243139), 0);
    lv_obj_set_style_text_color(f.widgets.off_label, lv_color_hex(0xF5F7FA), 0);
    f.control.refresh();
    assert(lv_color_eq(lv_obj_get_style_bg_color(f.w["extra_agenda_next"], LV_PART_MAIN), lv_color_hex(0xB6E4FA)));
    assert(lv_color_eq(lv_obj_get_style_bg_color(f.w["extra_agenda_previous"], LV_PART_MAIN), lv_color_hex(0x243139)));
    assert(lv_obj_get_style_bg_opa(f.w["extra_agenda_previous"], LV_PART_MAIN) == LV_OPA_COVER);
  } else if (name == "unfit") {
    auto s = f.next();
    auto &e = *s.state.family_if<Extra>();
    e.state_generation++;
    std::get<Agenda>(e.data).summary.assign("\xE6\x97\xA5");
    f.receive(s);
    assert(!f.control.ready());
    f.press();
    f.pointer.release();
    assert(f.pages() == 0);
  } else
    assert(false);
  assert(std::none_of(f.messages.begin(), f.messages.end(),
                      [](const auto &m) { return m.kind == ControlKind::CONTROL_KIND_COMMAND; }));
  std::cout << "RESULT " << name << " pass=1\n";
}
