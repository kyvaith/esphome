#include "real_lvgl_test_support.h"
#include "material_entity_control_component.h"
#include "disabled_control_pixel_test.h"
#include "esphome/components/time/real_time_clock.h"
#include <iostream>
using namespace esphome::lvgl_material;
namespace rt = real_lvgl_test;
using Op = ControlOperation;
struct Control : MaterialEntityControlComponent {
  using MaterialEntityControlComponent::MaterialEntityControlComponent;
  bool captured() const { return this->captured_target_ != nullptr; }
  bool begin_raw(lv_obj_t *target) { return this->begin_panel_input_(target, 123, esphome::millis()); }
  uint32_t draft() const { return this->timer_draft_; }
};
struct Fixture {
  esphome::lvgl::LvglComponent lvgl;
  rt::HostDisplay display{lvgl};
  rt::NativeWidgets widgets{lv_screen_active()};
  rt::Pointer pointer{display.get()};
  esphome::lvgl::LvglNavigation navigation;
  esphome::lvgl::LvglApplication application;
  esphome::time::RealTimeClock clock;
  Control control{&lvgl};
  std::array<lv_obj_t *, 3> roots{}, titles{}, kinds{}, states{}, statuses{};
  lv_obj_t *remaining, *activity, *activity_value, *modal, *back, *apply, *options_title;
  std::array<lv_obj_t *, 2> power{}, power_labels{};
  std::array<lv_obj_t *, 3> drafts{}, icons{};
  std::array<lv_obj_t *, 6> edits{};
  std::array<lv_obj_t *, 5> actions{}, action_labels{};
  std::array<lv_obj_t *, 8> options{}, option_labels{};
  std::vector<ControlMessage> messages;
  static lv_obj_t *container(lv_obj_t *parent) {
    auto *result = lv_obj_create(parent);
    lv_obj_set_pos(result, 0, 0);
    lv_obj_set_size(result, 800, 800);
    lv_obj_set_style_pad_all(result, 0, 0);
    lv_obj_set_style_border_width(result, 0, 0);
    lv_obj_remove_flag(result, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(result, LV_OBJ_FLAG_SCROLLABLE);
    return result;
  }
  static lv_obj_t *label(lv_obj_t *parent, int x, int y) {
    auto *w = lv_label_create(parent);
    lv_obj_set_pos(w, x, y);
    lv_obj_remove_flag(w, LV_OBJ_FLAG_CLICKABLE);
    return w;
  }
  static lv_obj_t *button(lv_obj_t *parent, int x, int y, int width = 200, int height = 64) {
    auto *w = lv_button_create(parent);
    lv_obj_set_pos(w, x, y);
    lv_obj_set_size(w, width, height);
    lv_obj_set_style_pad_all(w, 0, 0);
    return w;
  }
  Fixture(uint8_t type = 0, bool missing = false, bool power_supported = true) {
    navigation.active = &application;
    for (size_t i = 0; i < 3; i++) {
      roots[i] = container(widgets.root);
      titles[i] = label(roots[i], 130, 104);
      kinds[i] = label(roots[i], 130, 68);
      states[i] = label(roots[i], 180, 236);
      statuses[i] = label(roots[i], 200, 730);
    }
    remaining = label(roots[0], 180, 276);
    for (size_t i = 0; i < 3; i++)
      drafts[i] = label(roots[0], 150 + i * 170, 450);
    for (size_t i = 0; i < 6; i++) {
      edits[i] = button(roots[0], 140 + (i / 2) * 170, 400 + (i % 2) * 100, 70, 40);
      control.set_timer_draft_button(i, edits[i]);
    }
    for (size_t i = 0; i < 5; i++) {
      actions[i] = button(roots[0], 100 + (i % 3) * 210, 570 + (i / 3) * 80);
      action_labels[i] = label(actions[i], 0, 0);
      control.set_timer_panel_action(i, actions[i], action_labels[i]);
    }
    for (size_t i = 0; i < 2; i++) {
      power[i] = button(roots[i + 1], 180, 420);
      lv_obj_add_flag(power[i], LV_OBJ_FLAG_HIDDEN);
      power_labels[i] = label(power[i], 0, 0);
    }
    activity = button(roots[1], 410, 420);
    activity_value = label(roots[1], 146, 350);
    modal = container(roots[1]);
    lv_obj_add_flag(modal, LV_OBJ_FLAG_CLICKABLE);
    options_title = label(modal, 130, 104);
    for (size_t i = 0; i < 8; i++) {
      options[i] = button(modal, 130 + (i % 2) * 276, 220 + (i / 2) * 100, 264, 84);
      option_labels[i] = label(options[i], 0, 0);
      control.set_remote_panel_option(i, options[i], option_labels[i]);
    }
    back = button(modal, 200, 650, 190);
    apply = button(modal, 410, 650, 190);
    for (size_t i = 0; i < 3; i++)
      icons[i] = label(roots[2], 352, 156);
    control.set_widgets(widgets.root, widgets.hit, widgets.title, widgets.value, widgets.unknown, widgets.on,
                        widgets.off);
    control.set_state_label(widgets.state);
    control.set_status_label(widgets.status);
    control.set_number_profile(&navigation, &application, widgets.on_label, widgets.off_label, LV_FONT_DEFAULT);
    control.set_timer_panel_profile(roots[0], titles[0], kinds[0], states[0], remaining, statuses[0], drafts[0],
                                    drafts[1], drafts[2]);
    control.set_panel_time(&clock);
    control.set_remote_panel_profile(roots[1], titles[1], kinds[1], states[1], statuses[1], power[0], power_labels[0],
                                     activity, label(activity, 0, 0), activity_value, modal, options_title, back, apply,
                                     label(apply, 0, 0));
    control.set_power_panel_profile(roots[2], titles[2], kinds[2], states[2], statuses[2], power[1], power_labels[1],
                                    icons[0], icons[1], icons[2]);
    control.set_initial_enabled(true);
    control.add_on_message_callback([this](const char *payload) {
      MaterialEntityControlCodec codec;
      ControlMessage value;
      assert(codec.parse_entity(payload, value));
      messages.push_back(value);
    });
    control.setup();
    assert(!control.is_failed());
    rt::advance(200);
    assert(control.bind_entity_json(
        R"({"v":1,"type":"entity_control_v1","kind":"bind","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})"));
    ArduinoJson::JsonDocument response;
    assert(control.populate_panel_features(response.to<ArduinoJson::JsonObject>()));
    assert(response["panel_types"].size() == 3);
    assert(!control.open_panel(31, "panel-card", 348));
    assert(control.bind_panel_features(
        R"({"v":1,"type":"entity_panel_features_v1","panel_version":1,"channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","panel_types":["timer","remote","power"]})",
        response.to<ArduinoJson::JsonObject>()));
    assert(control.open_panel(31, "panel-card", 348));
    ControlMessage grant;
    grant.generic = true;
    grant.kind = ControlKind::CONTROL_KIND_GRANT;
    grant.identity = control.owner().identity();
    grant.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    grant.state.title.assign("Panel");
    grant.state.state_seq = 1;
    grant.state.emplace_family<ControlSnapshot::Panel>();
    auto &p = (*grant.state.family_if<ControlSnapshot::Panel>());
    p.type = type;
    p.available = true;
    p.state_generation = 1;
    if (type == 0) {
      p.phase = 1;
      p.caps.start = p.caps.pause = p.caps.cancel = true;
      p.caps.initial_duration = 120;
      p.duration = 120;
      p.sampled_at = clock.timestamp;
      p.eligible_actions = 17;
    } else {
      p.power = 1;
      p.caps.turn_on = power_supported;
      p.caps.turn_off = true;
      p.eligible_actions = power_supported ? 1 : 0;
      if (type == 1 && power_supported) {
        p.caps.activity_count = 2;
        p.caps.activities[0].assign("Music");
        p.caps.activities[1].assign(missing ? "\xE4\xB8\xAD" : "Cinema");
        p.eligible_actions |= 4;
      } else if (type == 2)
        p.caps.device_class = 2;
    }
    receive(grant);
    assert(control.resume_control_view(control.get_selection_generation()));
    rt::advance(200);
    assert(control.owner().is_view_ready());
  }
  void receive(const ControlMessage &m, bool expected = true) {
    MaterialEntityControlCodec codec;
    char wire[2049];
    size_t length;
    assert(codec.serialize_entity(m, wire, sizeof(wire), length));
    assert(control.receive_entity({wire, length}) == expected);
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
  void active() {
    auto m = next();
    auto &p = (*m.state.family_if<ControlSnapshot::Panel>());
    p.phase = 2;
    p.remaining = 120;
    p.finishes_at = p.sampled_at + 120;
    p.eligible_actions = 28;
    p.state_generation++;
    receive(m);
  }
  void result(ControlResult result) {
    ControlMessage m;
    m.generic = true;
    m.kind = ControlKind::CONTROL_KIND_RESULT;
    m.identity = control.owner().identity();
    m.command_seq = control.owner().command_sequence();
    m.state.state_seq = control.owner().state().state_seq;
    m.result = result;
    receive(m);
  }
  void tap(int x, int y) {
    pointer.press(x, y);
    pointer.release();
    rt::advance(40);
  }
  size_t commands() const {
    size_t n = 0;
    for (const auto &m : messages)
      n += m.kind == ControlKind::CONTROL_KIND_COMMAND;
    return n;
  }
  ~Fixture() {
    control.on_shutdown();
    lv_obj_delete(widgets.root);
  }
};
int main(int argc, char **argv) {
  assert(argc == 2);
  const std::string name = argv[1];
  lv_init();
  {
    const uint8_t type = name.starts_with("remote") ? 1 : name.starts_with("power") ? 2 : 0;
    const bool power_supported = name != "remote_power_unsupported" && name != "power_unsupported";
    Fixture f(type, name == "remote_glyph", power_supported);
    for (size_t i = 0; i < 3; i++)
      assert(lv_obj_has_flag(f.roots[i], LV_OBJ_FLAG_HIDDEN) == (i != type));
    if (type != 0)
      assert(!lv_obj_has_flag(f.power[type - 1], LV_OBJ_FLAG_HIDDEN));
    if (name == "timer_start") {
      f.tap(180, 600);
      assert(f.commands() == 1);
      assert(f.messages.back().operation == Op::CONTROL_OPERATION_PANEL_DURATION);
      assert(f.messages.back().panel_value == 120);
      f.result(ControlResult::CONTROL_RESULT_SERVICE_COMPLETED);
      assert(std::string_view(lv_label_get_text(f.statuses[0])) == "Command accepted by HA");
    } else if (name == "timer_draft" || name == "timer_draft_aba" || name == "timer_draft_renewal") {
      f.active();
      f.pointer.press(350, 520);
      assert(f.control.captured());
      assert(f.control.draft() == 120);
      if (name != "timer_draft") {
        auto m = f.next();
        (*m.state.family_if<ControlSnapshot::Panel>()).sampled_at++;
        (*m.state.family_if<ControlSnapshot::Panel>()).remaining--;
        if (name == "timer_draft_aba")
          (*m.state.family_if<ControlSnapshot::Panel>()).state_generation += 2;
        f.receive(m);
      }
      f.pointer.release();
      assert(f.control.draft() == (name == "timer_draft_aba" ? 120U : 180U));
      assert(f.commands() == 0);
    } else if (name == "timer_clock" || name == "timer_sample" || name == "timer_zero" || name == "timer_hidden") {
      f.active();
      if (name == "timer_hidden")
        assert(f.control.suspend_control_view());
      f.clock.timestamp += name == "timer_sample" ? 40 : name == "timer_zero" ? 120 : 10;
      if (name == "timer_zero") {
        auto m = f.next();
        (*m.state.family_if<ControlSnapshot::Panel>()).sampled_at = f.clock.timestamp;
        (*m.state.family_if<ControlSnapshot::Panel>()).remaining = 0;
        f.receive(m);
      }
      rt::advance(1200);
      f.control.loop();
      const std::string_view text = lv_label_get_text(f.remaining);
      assert(text == (name == "timer_zero" ? "0:00:00" : name == "timer_clock" ? "0:01:50" : "0:02:00"));
      if (name == "timer_sample")
        assert(std::string_view(lv_label_get_text(f.states[0])).find("Sample") != std::string_view::npos);
      if (name == "timer_zero")
        assert(std::string_view(lv_label_get_text(f.states[0])).find("Waiting for HA") != std::string_view::npos);
      assert(f.commands() == 0);
    } else if (name == "timer_pause") {
      f.active();
      f.tap(560, 600);
      assert(f.commands() == 1);
      assert(f.messages.back().operation == Op::CONTROL_OPERATION_PANEL_PAUSE);
    } else if (name == "timer_release_outside") {
      f.pointer.press(180, 600);
      assert(f.control.captured());
      f.pointer.move(60, 700);
      f.pointer.release();
      assert(f.commands() == 0);
    } else if (name == "remote_apply" || name == "remote_aba" || name == "remote_gap" || name == "remote_guard") {
      f.tap(500, 450);
      assert(!lv_obj_has_flag(f.modal, LV_OBJ_FLAG_HIDDEN));
      assert(lv_obj_has_state(f.apply, LV_STATE_DISABLED));
      if (name == "remote_gap") {
        f.tap(400, 450);
        assert(f.commands() == 0);
      } else if (name == "remote_guard") {
        assert(!f.control.begin_raw(f.power[0]));
        assert(!f.control.begin_raw(f.activity));
      } else {
        f.tap(500, 250);
        assert(f.commands() == 0);
        assert(!lv_obj_has_state(f.apply, LV_STATE_DISABLED));
        if (name == "remote_aba") {
          auto m = f.next();
          (*m.state.family_if<ControlSnapshot::Panel>()).state_generation += 2;
          f.receive(m);
          assert(lv_obj_has_state(f.apply, LV_STATE_DISABLED));
        }
        f.tap(500, 680);
        assert(f.commands() == (name == "remote_aba" ? 0U : 1U));
        if (f.commands()) {
          assert(f.messages.back().operation == Op::CONTROL_OPERATION_PANEL_ACTIVITY);
          assert(f.messages.back().panel_value == 1);
        }
      }
    } else if (name == "remote_unknown" || name == "remote_glyph") {
      if (name == "remote_unknown") {
        auto m = f.next();
        (*m.state.family_if<ControlSnapshot::Panel>()).power = 2;
        (*m.state.family_if<ControlSnapshot::Panel>()).eligible_actions = 2;
        (*m.state.family_if<ControlSnapshot::Panel>()).state_generation++;
        f.receive(m);
      }
      assert(lv_obj_has_state(f.activity, LV_STATE_DISABLED));
      f.tap(500, 450);
      assert(lv_obj_has_flag(f.modal, LV_OBJ_FLAG_HIDDEN));
      f.tap(250, 450);
      assert(f.commands() == 1);
    } else if (name == "remote_power_on" || name == "power_on") {
      if (type == 2)
        assert(!lv_obj_has_flag(f.icons[2], LV_OBJ_FLAG_HIDDEN));
      f.tap(250, 450);
      assert(f.commands() == 1);
      assert(f.messages.back().operation == Op::CONTROL_OPERATION_PANEL_POWER);
      assert(f.messages.back().panel_value == 1);
    } else if (name == "remote_power_unknown" || name == "power_unknown") {
      auto m = f.next();
      (*m.state.family_if<ControlSnapshot::Panel>()).power = 0;
      (*m.state.family_if<ControlSnapshot::Panel>()).available = false;
      (*m.state.family_if<ControlSnapshot::Panel>()).eligible_actions = 0;
      (*m.state.family_if<ControlSnapshot::Panel>()).state_generation++;
      f.receive(m);
      assert(!lv_obj_has_flag(f.power[type - 1], LV_OBJ_FLAG_HIDDEN));
      assert(lv_obj_has_state(f.power[type - 1], LV_STATE_DISABLED));
      f.tap(250, 450);
      assert(f.commands() == 0);
    } else if (name == "remote_power_unsupported" || name == "power_unsupported") {
      assert(lv_obj_has_state(f.power[type - 1], LV_STATE_DISABLED));
      f.tap(250, 450);
      assert(f.commands() == 0);
      auto unsupported = f.next();
      (*unsupported.state.family_if<ControlSnapshot::Panel>()).caps.turn_off = false;
      assert(!control_valid_panel_snapshot(unsupported.state));
    } else if (name == "handoff") {
      assert(f.control.open_number(2, "number", 348));
      for (auto *root : f.roots)
        assert(lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN));
      assert(!lv_obj_has_flag(f.widgets.title, LV_OBJ_FLAG_HIDDEN));
    } else
      assert(name == "timer_open");
    if (name != "handoff") {
      for (size_t i = 0; i < 5; i++)
        assert_readable_disabled_control(f.lvgl, f.actions[i], f.action_labels[i]);
      for (size_t i = 0; i < 2; i++)
        assert_readable_disabled_control(f.lvgl, f.power[i], f.power_labels[i]);
      for (size_t i = 0; i < 8; i++)
        assert_readable_disabled_control(f.lvgl, f.options[i], f.option_labels[i]);
      assert_readable_disabled_control(f.lvgl, f.apply, lv_obj_get_child(f.apply, 0));
    }
    std::cout << "PANEL " << name << " pass=1\n";
  }
  lv_deinit();
}
