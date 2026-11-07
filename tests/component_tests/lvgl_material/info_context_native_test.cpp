#include "real_lvgl_test_support.h"
// The small host font has no U+00B7. Give this fixture an explicit dot glyph;
// product Roboto glyphs and visual geometry are validated separately.
const lv_font_t *context_font() {
  static lv_font_t font = *LV_FONT_DEFAULT;
  font.get_glyph_dsc = [](const lv_font_t *actual, lv_font_glyph_dsc_t *description, uint32_t point, uint32_t next) {
    return (LV_FONT_DEFAULT)->get_glyph_dsc(actual, description, point == 0xB7 ? '.' : point, next);
  };
  return &font;
}
#define main legacy_extra_main
#include "extra_native_input_test.cpp"
#undef main
#include "context_corpus.h"

void open_context(Fixture &fixture, const char *name) {
  fixture.control.invalidate(ControlReason::CONTROL_REASON_SELECTION_CHANGED);
  ArduinoJson::JsonDocument response;
  const char *features = R"({"v":1,"type":"entity_panel_extra_v1","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","profiles":[{"name":"info","version":1},{"name":"info_context","version":1},{"name":"lock","version":1}]})";
  assert(fixture.control.bind_extra_features(features, response.to<ArduinoJson::JsonObject>()));
  assert(fixture.control.open_panel(15, "context-fixture", 376));
  MaterialEntityControlCodec codec;
  ControlMessage value;
  bool found = false;
  for (const auto &item : CASES)
    if (std::strcmp(item.name, name) == 0) {
      assert(codec.parse_entity(item.raw, value));
      found = true;
    }
  assert(found);
  value.kind = ControlKind::CONTROL_KIND_GRANT;
  value.identity = fixture.control.owner().identity();
  value.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
  fixture.receive(value);
  assert(fixture.control.resume_control_view(fixture.control.get_selection_generation()));
  rt::advance(200);
  assert(fixture.control.owner().is_view_ready());
}

int main(int argc, char **argv) {
  assert(argc == 2);
  lv_init();
  const std::string name = argv[1];
  Fixture f(0);
  open_context(f, name.starts_with("sun") ? "sun-night" : "person-home-en");
  assert(!f.visible("extra_sensor_root") && !f.visible("extra_state_root") && !f.visible("extra_calendar_root"));
  if (name == "person-date") {
    std::printf("DATE changed=[%s] updated=[%s] status=[%s]\n", f.text("person_changed_label"),
                f.text("extra_control_updated"), f.text("extra_control_status"));
    assert(f.visible("person_root") && !f.visible("sun_root"));
    assert(std::string(f.text("person_state_label")) == "Home");
    assert(std::string(f.text("person_changed_label")) == "HA state since\n5 November 2026 · 19:56\nUTC+01:00");
    assert(std::string(f.text("extra_control_updated")) == "HA update · 5 November 2026 · 20:16\nUTC+01:00");
    assert(std::string(f.text("extra_control_status")) == "Information from Home Assistant");
    auto value = f.next();
    std::get<Extra::Person>(value.state.family_if<Extra>()->data).changed.assign("2026-10-25T02:30:59+02:00");
    f.receive(value);
    assert(std::string(f.text("person_changed_label")).ends_with("02:30\nUTC+02:00"));
    value = f.next();
    std::get<Extra::Person>(value.state.family_if<Extra>()->data).changed.assign("2026-10-25T02:30:01+01:00");
    f.receive(value);
    assert(std::string(f.text("person_changed_label")).ends_with("02:30\nUTC+01:00"));
  } else if (name == "person-palette") {
    lv_obj_set_style_bg_color(f.widgets.on, lv_color_hex(0xDDEEFF), LV_PART_MAIN);
    lv_obj_set_style_text_color(f.widgets.on_label, lv_color_hex(0x112233), LV_PART_MAIN);
    lv_obj_set_style_bg_color(f.widgets.off, lv_color_hex(0x223344), LV_PART_MAIN);
    lv_obj_set_style_text_color(f.widgets.off_label, lv_color_hex(0xF0E0D0), LV_PART_MAIN);
    // The icon owns a local foreground; parent inheritance must not hide it.
    lv_obj_set_style_text_color(f.w["person_icon"], lv_color_hex(0xAABBCC), LV_PART_MAIN);
    f.control.refresh();
    assert(lv_color_eq(lv_obj_get_style_text_color(f.w["person_icon"], LV_PART_MAIN), lv_color_hex(0x112233)));
    auto value = f.next();
    auto &extra = *value.state.family_if<Extra>();
    extra.source = 1;
    extra.state_generation++;
    extra.data.emplace<Extra::Person>();
    f.receive(value);
    assert(std::string(f.text("person_state_label")) == "Unknown");
    assert(std::string(f.text("person_zone_label")).empty());
    assert(std::string(f.text("person_changed_label")) == "HA state since\nTime unavailable");
    assert(lv_color_eq(lv_obj_get_style_bg_color(f.w["person_surface"], LV_PART_MAIN), lv_color_hex(0x223344)));
    assert(lv_color_eq(lv_obj_get_style_text_color(f.w["person_icon"], LV_PART_MAIN), lv_color_hex(0xF0E0D0)));
    assert(std::string(f.text("extra_control_updated")).find("20:16") != std::string::npos);
  } else if (name == "sun-values") {
    assert(f.visible("sun_root") && !f.visible("person_root"));
    assert(std::string(f.text("sun_phase_label")) == "Below horizon");
    assert(std::string(f.text("sun_rising_value")) == "6 November 2026 · 06:38\nUTC+01:00");
    assert(std::string(f.text("sun_setting_value")) == "6 November 2026 · 15:53\nUTC+01:00");
    assert(std::string(f.text("sun_elevation_value")) == "-36.43°");
    auto value = f.next();
    auto &sun = std::get<Extra::Sun>(value.state.family_if<Extra>()->data);
    sun.elevation.assign("5e-324");
    sun.rising.assign("");
    sun.rising_status = 4;
    f.receive(value);
    assert(std::string(f.text("sun_elevation_value")) == "5e-324°");
    assert(std::string(f.text("sun_rising_value")) == "Waiting for an updated time");
    assert(std::string(f.text("sun_setting_value")).starts_with("6 November"));
  } else if (name == "sun-unavailable") {
    auto value = f.next();
    auto &extra = *value.state.family_if<Extra>();
    extra.source = 2;
    extra.state_generation++;
    extra.data.emplace<Extra::Sun>();
    f.receive(value);
    assert(std::string(f.text("sun_elevation_value")) == "Unknown");
    assert(std::string(f.text("sun_rising_value")) == "Time unavailable");
    assert(std::string(f.text("sun_zone_label")).empty());
    assert(std::string(f.text("extra_control_status")) == "Unavailable");
  } else if (name == "person-fit") {
    auto value = f.next();
    std::get<Extra::Person>(value.state.family_if<Extra>()->data).text.assign("\xe4\xb8\xad");
    f.receive(value);
    assert(std::string(f.text("person_state_label")) == "--");
    assert(std::string(f.text("extra_control_status")) == "Full observation cannot be displayed");
    lv_obj_set_size(f.w["person_changed_label"], 10, 10);
    f.control.refresh();
    assert(std::string(f.text("person_changed_label")) == "--");
  } else if (name == "person-lifecycle") {
    f.control.invalidate(ControlReason::CONTROL_REASON_EXPIRED);
    assert(!f.visible("person_root") && !f.visible("sun_root"));
    assert(std::string(f.text("person_state_label")).empty() && std::string(f.text("person_changed_label")).empty());
    assert(std::string(f.text("extra_control_updated")).empty());
    f.open(2);  // Existing Calendar format is preserved after a context view.
    assert(f.visible("extra_calendar_root") && !f.visible("person_root"));
    assert(std::string(f.text("extra_calendar_start")) == "Start: 2026-11-05 18:00:00");
    f.control.invalidate(ControlReason::CONTROL_REASON_SELECTION_CHANGED);
    f.open(4);
    f.press();
    f.pointer.release();
    assert(f.commands() == 1);
  } else if (name == "person-readonly" || name == "sun-readonly") {
    for (const auto &point : {std::pair{400, 220}, {400, 390}, {400, 580}}) {
      f.pointer.press(point.first, point.second);
      f.pointer.release();
    }
    assert(f.commands() == 0 && !f.control.captured());
    const auto flushes = f.lvgl.native_flushes;
    for (unsigned i = 0; i < 5; i++) {
      f.control.refresh();
      lv_refr_now(f.display.get());
    }
    assert(f.lvgl.native_flushes == flushes);
  } else {
    assert(false);
  }
  if (name != "person-lifecycle")
    assert(f.commands() == 0);
  std::printf("RESULT %s pass=1\n", name.c_str());
}
