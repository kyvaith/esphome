#include "real_lvgl_test_support.h"
#include "material_entity_control_component.h"
#include <iostream>
#include <map>

using namespace esphome::lvgl_material;
namespace rt = real_lvgl_test;
using Extra = ControlExtraSnapshot;
struct Control : MaterialEntityControlComponent {
  using MaterialEntityControlComponent::MaterialEntityControlComponent;
  void refresh() { this->update_static_(); }
  bool captured() const { return this->captured_target_ != nullptr; }
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
  Fixture(uint8_t type) {
    navigation.active = &application;
    w["BASE"] = widgets.root;
#include "extra_widgets.h"
#ifdef USE_LVGL_MATERIAL_INFO_CONTEXT
#include "info_context_widgets.h"
#endif
    control.set_widgets(widgets.root, widgets.hit, widgets.title, widgets.value, widgets.unknown, widgets.on,
                        widgets.off);
    control.set_state_label(widgets.state);
    control.set_status_label(widgets.status);
    control.set_number_profile(&navigation, &application, widgets.on_label, widgets.off_label, LV_FONT_DEFAULT);
    control.set_extra_profile(w["extra_control_root"], w["extra_control_title"], w["extra_control_kind"],
                              w["extra_control_updated"], w["extra_control_status"]);
    control.set_extra_sensor_profile(w["extra_sensor_root"], w["extra_sensor_value"], w["extra_sensor_unit"],
                                     w["extra_sensor_icon_unknown"], w["extra_sensor_icon_temperature"],
                                     w["extra_sensor_icon_humidity"], LV_FONT_DEFAULT);
    control.set_extra_state_profile(
        w["extra_state_root"], w["extra_state_value"], w["extra_surface_binary"], w["extra_surface_alarm"],
        w["extra_surface_lock"], w["extra_state_icon_unknown"], w["extra_state_icon_moisture"],
        w["extra_state_icon_alarm"], w["extra_state_icon_lock_closed"], w["extra_state_icon_lock_open"],
        w["extra_state_icon_calendar"], w["extra_lock_button"], w["extra_lock_label"], LV_FONT_DEFAULT);
    control.set_extra_calendar_profile(w["extra_calendar_root"], w["extra_calendar_badge"], w["extra_calendar_summary"],
                                       w["extra_calendar_start"], w["extra_calendar_end"], w["extra_calendar_zone"],
                                       w["extra_calendar_location"], w["extra_calendar_description"],
                                       w["extra_calendar_excerpt"], LV_FONT_DEFAULT);
    control.set_extra_profiles(true, true);
#ifdef USE_LVGL_MATERIAL_INFO_CONTEXT
    control.set_extra_person_profile(w["person_root"], w["person_icon"], w["person_surface"], w["person_state_label"],
                                     w["person_changed_label"], w["person_zone_label"], LV_FONT_DEFAULT);
    control.set_extra_sun_profile(w["sun_root"], w["sun_icon"], w["sun_phase_label"], w["sun_rising_caption"],
                                  w["sun_rising_value"], w["sun_setting_caption"], w["sun_setting_value"],
                                  w["sun_elevation_caption"], w["sun_elevation_value"], w["sun_azimuth_caption"],
                                  w["sun_azimuth_value"], w["sun_zone_label"]);
    control.set_extra_info_context(true);
#endif
    control.set_initial_enabled(true);
    control.add_on_message_callback([this](const char *wire) {
      MaterialEntityControlCodec codec;
      ControlMessage value;
      assert(codec.parse_entity(wire, value));
      messages.push_back(value);
    });
    control.setup();
    assert(!control.is_failed());
    rt::advance(100);
    assert(control.bind_entity_json(
        R"({"v":1,"type":"entity_control_v1","kind":"bind","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})"));
    ArduinoJson::JsonDocument response;
    const char *features =
        R"({"v":1,"type":"entity_panel_extra_v1","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","profiles":[{"name":"info","version":1},{"name":"lock","version":1}]})";
    assert(control.query_extra_features(features, response.to<ArduinoJson::JsonObject>()));
    assert(response["profiles"].size() == 2);
    assert(control.owner().bound_extra_profiles() == 0);
    assert(control.bind_extra_features(features, response.to<ArduinoJson::JsonObject>()));
    assert(control.owner().bound_extra_profiles() == 3);
    open(type);
  }
  void receive(const ControlMessage &message) {
    MaterialEntityControlCodec codec;
    char wire[2049];
    size_t length;
    assert(codec.serialize_entity(message, wire, sizeof(wire), length));
    assert(control.receive_entity({wire, length}));
    lv_refr_now(display.get());
  }
  void open(uint8_t type) {
    assert(control.open_panel(2, "card-2", 4));
    ControlMessage value;
    value.generic = true;
    value.kind = ControlKind::CONTROL_KIND_GRANT;
    value.identity = control.owner().identity();
    value.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    value.state.state_seq = 1;
    value.state.title.assign("Actual entity");
    auto &extra = value.state.emplace_family<Extra>();
    extra.type = type;
    extra.source = 0;
    extra.state_generation = 1;
    extra.updated.assign("2026-11-05 12:00:00");
    if (type == 0) {
      auto &sensor = extra.data.emplace<Extra::Sensor>();
      sensor.status = 0;
      sensor.value.assign("15.6");
      sensor.unit.assign("C");
      sensor.device_class.assign("temperature");
    } else if (type == 1 || type == 3) {
      auto &state = extra.data.emplace<Extra::State>();
      state.status = 0;
      state.phase = 0;
      state.text.assign(type == 1 ? "Dry" : "Disarmed");
      if (type == 1)
        state.device_class.assign("moisture");
    } else if (type == 2) {
      auto &calendar = extra.data.emplace<Extra::Calendar>();
      calendar.event = 0;
      calendar.active = 0;
      calendar.all_day = 0;
      calendar.summary.assign("Actual event");
      calendar.start.assign("2026-11-05 18:00:00");
      calendar.end.assign("2026-11-05 19:00:00");
      calendar.zone.assign("Europe/Warsaw");
      calendar.location.assign("Living room");
      calendar.description.assign("An observation");
    } else {
      auto &lock = extra.data.emplace<Extra::Lock>();
      lock.phase = 1;
      lock.capability = true;
    }
    receive(value);
    assert(control.resume_control_view(control.get_selection_generation()));
    rt::advance(200);
    assert(control.owner().is_view_ready());
  }
  ControlMessage next() {
    ControlMessage value;
    value.generic = true;
    value.kind = ControlKind::CONTROL_KIND_STATE;
    value.identity = control.owner().identity();
    value.state = control.owner().state();
    value.state.state_seq++;
    return value;
  }
  const char *text(const char *id) { return lv_label_get_text(w.at(id)); }
  bool visible(const char *id) { return lv_obj_is_visible(w.at(id)); }
  void press() {
    lv_area_t area;
    lv_obj_get_coords(w["extra_lock_button"], &area);
    pointer.press((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
  }
  size_t commands() const {
    return std::count_if(messages.begin(), messages.end(),
                         [](const auto &m) { return m.kind == ControlKind::CONTROL_KIND_COMMAND; });
  }
};

int main(int argc, char **argv) {
  assert(argc == 2);
  lv_init();
  const std::string name = argv[1];
  const uint8_t type = name == "binary"               ? 1
                       : name.starts_with("calendar") ? 2
                       : name == "alarm"              ? 3
                       : name.starts_with("lock")     ? 4
                                                      : 0;
  Fixture f(type);
  assert(f.visible("extra_control_root"));
  assert(!lv_obj_is_visible(f.widgets.hit));
  if (name == "sensor") {
    assert(std::string(f.text("extra_sensor_value")) == "15.6");
    assert(f.visible("extra_sensor_icon_temperature") && !f.visible("extra_state_root"));
  } else if (name == "binary") {
    assert(std::string(f.text("extra_state_value")) == "Dry");
    assert(f.visible("extra_state_icon_moisture"));
  } else if (name == "calendar") {
    assert(std::string(f.text("extra_calendar_summary")) == "Actual event");
    assert(f.visible("extra_calendar_root"));
  } else if (name == "alarm") {
    assert(std::string(f.text("extra_state_value")) == "Disarmed");
    assert(!f.visible("extra_lock_button"));
  } else if (name == "lock") {
    assert(std::string(f.text("extra_state_value")) == "Unlocked");
    assert(f.visible("extra_lock_button"));
  } else if (name == "lock-palette") {
    const auto references = {f.widgets.on, f.widgets.off, f.widgets.on_label, f.widgets.off_label};
    for (auto *object : references) {
      lv_obj_set_style_bg_color(object, lv_color_hex(0x512244), LV_STATE_DISABLED | LV_STATE_PRESSED);
      lv_obj_set_style_text_color(object, lv_color_hex(0x986244), LV_STATE_DISABLED | LV_STATE_PRESSED);
      lv_obj_add_state(object, static_cast<lv_state_t>(LV_STATE_DISABLED | LV_STATE_PRESSED));
    }
    const auto palette = [&](uint32_t active_bg, uint32_t active_fg, uint32_t neutral_bg, uint32_t neutral_fg) {
      lv_obj_set_style_bg_color(f.widgets.on, lv_color_hex(active_bg), LV_PART_MAIN);
      lv_obj_set_style_text_color(f.widgets.on_label, lv_color_hex(active_fg), LV_PART_MAIN);
      lv_obj_set_style_bg_color(f.widgets.off, lv_color_hex(neutral_bg), LV_PART_MAIN);
      lv_obj_set_style_text_color(f.widgets.off_label, lv_color_hex(neutral_fg), LV_PART_MAIN);
    };
    const auto check = [&](uint32_t bg, uint32_t fg, bool enabled) {
      assert(lv_obj_has_state(f.w["extra_lock_button"], LV_STATE_DISABLED) != enabled);
      assert(lv_color_eq(lv_obj_get_style_bg_color(f.w["extra_lock_button"], LV_PART_MAIN), lv_color_hex(bg)));
      assert(lv_color_eq(lv_obj_get_style_text_color(f.w["extra_lock_label"], LV_PART_MAIN), lv_color_hex(fg)));
      for (auto *object : references)
        assert(lv_obj_has_state(object, static_cast<lv_state_t>(LV_STATE_DISABLED | LV_STATE_PRESSED)));
      assert(f.commands() == 0);
    };
    palette(0xB6E4FA, 0x071018, 0x243139, 0xF5F7FA);
    assert(lv_color_eq(lv_obj_get_style_bg_color(f.widgets.on, LV_PART_MAIN), lv_color_hex(0x512244)));
    assert(lv_color_eq(lv_obj_get_style_text_color(f.widgets.on_label, LV_PART_MAIN), lv_color_hex(0x986244)));
    f.control.refresh();
    check(0xB6E4FA, 0x071018, true);
    f.receive(f.next());  // Same Lock state, fresh lease; retained Scalar state is unchanged.
    check(0xB6E4FA, 0x071018, true);
    auto locked = f.next();
    auto &extra = *locked.state.family_if<Extra>();
    extra.state_generation++;
    std::get<Extra::Lock>(extra.data).phase = 0;
    f.receive(locked);
    check(0x243139, 0xF5F7FA, false);
    // Base theme updates must affect both palettes while references retain their states.
    palette(0xE7C7A9, 0x22180B, 0x372B24, 0xFFF0DE);
    f.control.refresh();
    check(0x372B24, 0xFFF0DE, false);
    auto unlocked = f.next();
    auto &again = *unlocked.state.family_if<Extra>();
    again.state_generation++;
    std::get<Extra::Lock>(again.data).phase = 1;
    f.receive(unlocked);
    check(0xE7C7A9, 0x22180B, true);
  } else if (name == "unchanged") {
    const auto count = f.lvgl.native_flushes;
    for (int i = 0; i < 5; ++i) {
      f.control.refresh();
      lv_refr_now(f.display.get());
    }
    assert(f.lvgl.native_flushes == count);
  } else if (name == "missing-glyph" || name == "overflow") {
    auto value = f.next();
    auto &sensor = std::get<Extra::Sensor>(value.state.family_if<Extra>()->data);
    sensor.value.assign(name == "missing-glyph" ? "\xe4\xb8\xad" : "1234567890123456789012345678901234567890");
    if (name == "overflow")
      lv_obj_set_size(f.w["extra_sensor_value"], 30, 20);
    f.receive(value);
    assert(std::string(f.text("extra_sensor_value")) == "--");
    assert(std::string(f.text("extra_control_status")) == "Full observation cannot be displayed");
  } else if (name == "source-unknown") {
    auto value = f.next();
    auto &extra = *value.state.family_if<Extra>();
    extra.source = 1;
    extra.state_generation++;
    auto &sensor = std::get<Extra::Sensor>(extra.data);
    sensor.status = 4;
    sensor.value.assign("");
    f.receive(value);
    assert(std::string(f.text("extra_sensor_value")) == "Unknown");
    assert(std::string(f.text("extra_control_status")) == "Unknown");
  } else if (name == "unit-unsupported") {
    auto value = f.next();
    auto &sensor = std::get<Extra::Sensor>(value.state.family_if<Extra>()->data);
    sensor.unit.assign("\xe4\xb8\xad");
    f.receive(value);
    assert(std::string(f.text("extra_sensor_value")) == "--");
    assert(std::string(f.text("extra_sensor_unit")).empty());
    assert(std::string(f.text("extra_control_status")) == "Full observation cannot be displayed");
    const auto count = f.lvgl.native_flushes;
    for (int i = 0; i < 5; ++i) {
      f.control.refresh();
      lv_refr_now(f.display.get());
    }
    assert(f.lvgl.native_flushes == count);
  } else if (name == "resized-unchanged") {
    lv_obj_set_size(f.w["extra_sensor_value"], 15, 15);
    f.control.refresh();
    assert(std::string(f.text("extra_sensor_value")) == "--");
    assert(std::string(f.text("extra_control_status")) == "Full observation cannot be displayed");
  } else if (name == "calendar-caption-overflow") {
    std::string caption(160, 'a');
    f.control.set_text_provider(
        [&caption](uint8_t id) -> const char * { return id == 33 || id == 35 ? caption.c_str() : nullptr; });
    lv_obj_set_size(f.w["extra_calendar_badge"], 10000, 70);
    auto value = f.next();
    auto &calendar = std::get<Extra::Calendar>(value.state.family_if<Extra>()->data);
    calendar.all_day = 1;
    f.receive(value);
    assert(std::string(f.text("extra_calendar_badge")) == "--");
    assert(std::string(f.text("extra_control_status")) == "Full observation cannot be displayed");
  } else if (name == "lock-state-overflow") {
    lv_obj_set_size(f.w["extra_state_value"], 10, 10);
    f.control.refresh();
    assert(std::string(f.text("extra_state_value")) == "--");
    assert(lv_obj_has_state(f.w["extra_lock_button"], LV_STATE_DISABLED));
    f.press();
    f.pointer.release();
    assert(f.commands() == 0 && !f.control.captured());
  } else if (name == "lock-on-release") {
    f.press();
    assert(f.control.captured() && f.commands() == 0);
    f.pointer.release();
    assert(f.commands() == 1 && !f.control.captured());
    assert(f.messages.back().operation == ControlOperation::CONTROL_OPERATION_EXTRA_LOCK);
  } else if (name == "lock-aba") {
    f.press();
    auto value = f.next();
    auto &extra = *value.state.family_if<Extra>();
    extra.state_generation++;
    std::get<Extra::Lock>(extra.data).phase = 0;
    f.receive(value);
    value = f.next();
    auto &again = *value.state.family_if<Extra>();
    again.state_generation++;
    std::get<Extra::Lock>(again.data).phase = 1;
    f.receive(value);
    f.pointer.release();
    assert(f.commands() == 0 && !f.control.captured());
    f.press();
    f.pointer.release();
    assert(f.commands() == 1);
  } else if (name == "lock-outside") {
    f.press();
    f.pointer.move(20, 20);
    f.pointer.release();
    assert(f.commands() == 0 && !f.control.captured());
  } else if (name == "retired-pending" || name == "legacy-recovery") {
    f.control.invalidate(ControlReason::CONTROL_REASON_ENTITY_UNAVAILABLE);
    assert(f.control.is_extra_view() && !f.control.owner().has_selection());
    assert(f.visible("extra_control_root") && !f.visible("extra_sensor_root"));
    assert(std::string(f.text("extra_state_value")) == "--");
    assert(f.control.open_number(3, "card-3", 4));
    f.control.refresh();
    assert(f.control.is_extra_view() && !lv_obj_is_visible(f.widgets.hit));
    if (name == "legacy-recovery") {
      ControlMessage grant;
      grant.generic = true;
      grant.kind = ControlKind::CONTROL_KIND_GRANT;
      grant.identity = f.control.owner().identity();
      grant.identity.token.assign("cccccccccccccccccccccccccccccccc");
      grant.state.state_seq = 1;
      auto &number = grant.state.emplace_family<ControlNumberSnapshot>();
      number.domain.minimum = 0;
      number.domain.maximum = 10000000;
      number.domain.step = 1000000;
      number.domain.max_index = 10;
      number.available = true;
      number.index = 5;
      number.state_generation = 1;
      number.value_text.assign("5");
      f.receive(grant);
      assert(f.control.resume_control_view(f.control.get_selection_generation()));
      assert(!f.control.is_extra_view() && !f.visible("extra_control_root"));
    }
  } else
    assert(name == "query-before-open");
  std::cout << "RESULT " << name << " pass=1\n";
  return 0;
}
