#include "real_lvgl_test_support.h"
#include "material_entity_control_component.h"
#include "disabled_control_pixel_test.h"
#include <iostream>
using namespace esphome::lvgl_material;
namespace rt = real_lvgl_test;
using Op = ControlOperation;
struct Control : MaterialEntityControlComponent {
  using MaterialEntityControlComponent::MaterialEntityControlComponent;
  bool captured() const { return this->captured_target_ != nullptr; }
  bool begin_raw(lv_obj_t *target) { return this->begin_robot_input_(target, 123, esphome::millis()); }
};
struct Fixture {
  esphome::lvgl::LvglComponent lvgl;
  rt::HostDisplay display{lvgl};
  rt::NativeWidgets widgets{lv_screen_active()};
  rt::Pointer pointer{display.get()};
  esphome::lvgl::LvglNavigation navigation;
  esphome::lvgl::LvglApplication application;
  Control control{&lvgl};
  lv_obj_t *root, *title, *kind, *state, *status, *suction, *room, *modal, *back, *apply, *options_title, *battery,
      *vacuum_icon, *mower_icon;
  std::array<lv_obj_t *, 6> actions{}, action_labels{};
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
    auto *result = lv_label_create(parent);
    lv_obj_set_pos(result, x, y);
    lv_obj_remove_flag(result, LV_OBJ_FLAG_CLICKABLE);
    return result;
  }
  static lv_obj_t *button(lv_obj_t *parent, int x, int y, int width, int height) {
    auto *result = lv_button_create(parent);
    lv_obj_set_pos(result, x, y);
    lv_obj_set_size(result, width, height);
    lv_obj_set_style_pad_all(result, 0, 0);
    return result;
  }
  Fixture(bool mower = false, bool missing_glyph = false) {
    navigation.active = &application;
    root = container(widgets.root);
    title = label(root, 130, 104);
    kind = label(root, 130, 68);
    state = label(root, 180, 270);
    status = label(root, 200, 720);
    battery = label(root, 200, 322);
    vacuum_icon = label(root, 352, 156);
    mower_icon = label(root, 352, 156);
    for (size_t i = 0; i < 6; i++) {
      actions[i] = button(root, 146 + 264 * (i % 2), 350 + 80 * (i / 2), 244, 64);
      action_labels[i] = label(actions[i], 0, 0);
    }
    suction = button(root, 146, 590, 244, 64);
    room = button(root, 410, 590, 244, 64);
    modal = container(root);
    lv_obj_add_flag(modal, LV_OBJ_FLAG_CLICKABLE);
    options_title = label(modal, 130, 104);
    for (size_t i = 0; i < 8; i++) {
      options[i] = button(modal, 130 + 276 * (i % 2), 220 + 100 * (i / 2), 264, 84);
      option_labels[i] = label(options[i], 0, 0);
    }
    back = button(modal, 200, 650, 190, 64);
    apply = button(modal, 410, 650, 190, 64);
    control.set_widgets(widgets.root, widgets.hit, widgets.title, widgets.value, widgets.unknown, widgets.on,
                        widgets.off);
    control.set_state_label(widgets.state);
    control.set_status_label(widgets.status);
    control.set_number_profile(&navigation, &application, widgets.on_label, widgets.off_label, LV_FONT_DEFAULT);
    control.set_robot_profile(root, title, kind, state, status, suction, label(suction, 0, 0), modal, back, vacuum_icon,
                              mower_icon, room, label(room, 0, 0), options_title, apply, label(apply, 0, 0), battery);
    for (size_t i = 0; i < 6; i++)
      control.set_robot_action(i, actions[i], action_labels[i]);
    for (size_t i = 0; i < 8; i++)
      control.set_robot_option(i, options[i], option_labels[i]);
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
    assert(control.open_robot(31, "robot-card", 348));
    ControlMessage grant;
    grant.generic = true;
    grant.kind = ControlKind::CONTROL_KIND_GRANT;
    grant.identity = control.owner().identity();
    grant.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    grant.state.title.assign(mower ? "Mower" : "Vacuum");
    grant.state.state_seq = 1;
    grant.state.emplace_family<ControlSnapshot::Robot>();
    auto &r = (*grant.state.family_if<ControlSnapshot::Robot>());
    r.type = mower ? 1 : 0;
    r.activity = 2;
    r.available = true;
    r.state_generation = 1;
    r.caps.actions = mower ? 11 : 63;
    r.eligible_actions = mower ? 1 : 49;
    r.battery_percent = 73;
    if (!mower) {
      r.caps.speed_count = 2;
      r.caps.speeds[0].assign("Quiet");
      r.caps.speeds[1].assign(missing_glyph ? "\xE4\xB8\xAD" : "Turbo");
      r.speed_index = 0;
      r.caps.area_count = 2;
      r.caps.areas[0].assign("Kitchen");
      r.caps.areas[1].assign("Study");
      r.area_enabled = true;
    }
    receive(grant);
    assert(control.resume_control_view(control.get_selection_generation()));
    rt::advance(200);
    assert(control.owner().is_view_ready());
  }
  void receive(const ControlMessage &message, bool expected = true) {
    MaterialEntityControlCodec codec;
    char wire[2049];
    size_t length;
    assert(codec.serialize_entity(message, wire, sizeof(wire), length));
    assert(control.receive_entity({wire, length}) == expected);
    lv_refr_now(display.get());
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
  void update(uint8_t activity, uint8_t eligible, bool area = false) {
    auto message = next();
    auto &r = (*message.state.family_if<ControlSnapshot::Robot>());
    r.activity = activity;
    r.eligible_actions = eligible;
    r.area_enabled = area;
    r.state_generation++;
    receive(message);
  }
  void tap(int x, int y) {
    pointer.press(x, y);
    pointer.release();
    rt::advance(40);
  }
  size_t commands() const {
    size_t n = 0;
    for (const auto &message : messages)
      n += message.kind == ControlKind::CONTROL_KIND_COMMAND;
    return n;
  }
  void check_disabled_palette() {
    const auto verify = [&](lv_obj_t *button, lv_obj_t *text) {
      bool exposed = lv_obj_has_flag(this->modal, LV_OBJ_FLAG_HIDDEN);
      for (auto *ancestor = button; ancestor != nullptr; ancestor = lv_obj_get_parent(ancestor))
        exposed |= ancestor == this->modal;
      if (exposed)
        assert_readable_disabled_control(this->lvgl, button, text);
      if (!lv_obj_has_state(button, LV_STATE_DISABLED))
        return;
      assert(lv_color_eq(lv_obj_get_style_bg_color(button, LV_PART_MAIN),
                         lv_obj_get_style_bg_color(widgets.off, LV_PART_MAIN)));
      assert(lv_color_eq(lv_obj_get_style_text_color(text, LV_PART_MAIN),
                         lv_obj_get_style_text_color(widgets.off_label, LV_PART_MAIN)));
    };
    for (size_t i = 0; i < 6; i++)
      verify(actions[i], action_labels[i]);
    for (size_t i = 0; i < 8; i++)
      verify(options[i], option_labels[i]);
    verify(apply, lv_obj_get_child(apply, 0));
  }
  ~Fixture() {
    control.on_shutdown();
    lv_obj_delete(widgets.root);
  }
};
int main(int argc, char **argv) {
  assert(argc == 2);
  lv_init();
  const std::string_view name = argv[1];
  {
    Fixture f(name == "mower", name == "glyph");
    f.check_disabled_palette();
    assert(!lv_obj_has_flag(f.root, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(f.widgets.title, LV_OBJ_FLAG_HIDDEN));
    if (name == "start" || name == "battery" || name == "aba" || name == "release_outside") {
      f.pointer.press(260, 380);
      assert(f.control.captured());
      assert(f.commands() == 0);
      if (name == "battery" || name == "aba") {
        auto m = f.next();
        (*m.state.family_if<ControlSnapshot::Robot>()).battery_percent = 72;
        if (name == "aba")
          (*m.state.family_if<ControlSnapshot::Robot>()).state_generation += 2;
        f.receive(m);
        assert(f.control.captured() == (name == "battery"));
      }
      if (name == "release_outside")
        f.pointer.move(100, 700);
      f.pointer.release();
      assert(f.commands() == (name == "aba" || name == "release_outside" ? 0U : 1U));
      if (f.commands())
        assert(f.messages.back().operation == Op::CONTROL_OPERATION_ROBOT_START);
      if (name == "battery")
        assert(std::string_view(lv_label_get_text(f.battery)) == "Battery: 72%");
    } else if (name == "interrupt") {
      f.tap(260, 380);
      assert(f.commands() == 1);
      f.update(1, 30);
      f.pointer.press(260, 460);
      assert(f.control.captured());
      f.update(5, 22);
      assert(f.control.captured());
      f.pointer.release();
      assert(f.commands() == 2);
      assert(f.messages.back().operation == Op::CONTROL_OPERATION_ROBOT_STOP);
      assert(f.messages.back().expected_state_generation == 3);
      f.tap(520, 380);
      assert(f.commands() == 2);
    } else if (name == "suction") {
      f.tap(260, 620);
      assert(!lv_obj_has_flag(f.modal, LV_OBJ_FLAG_HIDDEN));
      assert(lv_obj_has_flag(f.apply, LV_OBJ_FLAG_HIDDEN));
      f.tap(520, 250);
      assert(f.commands() == 1);
      assert(f.messages.back().operation == Op::CONTROL_OPERATION_ROBOT_SPEED);
      assert(f.messages.back().robot_target_index == 1);
      assert(lv_obj_has_flag(f.modal, LV_OBJ_FLAG_HIDDEN));
    } else if (name == "room" || name == "room_aba" || name == "room_battery" || name == "modal_gap" ||
               name == "modal_guard") {
      f.tap(520, 620);
      assert(!lv_obj_has_flag(f.modal, LV_OBJ_FLAG_HIDDEN));
      assert(lv_obj_has_state(f.apply, LV_STATE_DISABLED));
      assert(std::string_view(lv_label_get_text(f.options_title)) == "Room");
      assert(f.commands() == 0);
      if (name == "modal_gap") {
        f.tap(260, 410);
        assert(f.commands() == 0);
        assert(!f.control.captured());
      } else if (name == "modal_guard") {
        assert(!f.control.begin_raw(f.actions[0]));
        assert(!f.control.begin_raw(f.suction));
        assert(f.commands() == 0);
      } else {
        f.tap(520, 250);
        assert(f.commands() == 0);
        assert(!lv_obj_has_state(f.apply, LV_STATE_DISABLED));
        if (name == "room_aba" || name == "room_battery") {
          auto m = f.next();
          (*m.state.family_if<ControlSnapshot::Robot>()).battery_percent = 72;
          if (name == "room_aba")
            (*m.state.family_if<ControlSnapshot::Robot>()).state_generation += 2;
          f.receive(m);
          assert(lv_obj_has_state(f.apply, LV_STATE_DISABLED) == (name == "room_aba"));
        }
        f.tap(500, 680);
        assert(f.commands() == (name == "room_aba" ? 0U : 1U));
        if (f.commands()) {
          assert(f.messages.back().operation == Op::CONTROL_OPERATION_ROBOT_AREA);
          assert(f.messages.back().robot_target_index == 1);
        }
      }
    } else if (name == "unknown") {
      auto m = f.next();
      auto &r = (*m.state.family_if<ControlSnapshot::Robot>());
      r.activity = 0;
      r.available = false;
      r.eligible_actions = 0;
      r.area_enabled = false;
      r.speed_index = r.battery_percent = 255;
      r.state_generation++;
      f.receive(m);
      assert(lv_obj_has_state(f.actions[0], LV_STATE_DISABLED));
      assert(lv_obj_has_flag(f.battery, LV_OBJ_FLAG_HIDDEN));
      f.tap(260, 380);
      assert(f.commands() == 0);
    } else if (name == "mower") {
      assert(lv_obj_has_flag(f.vacuum_icon, LV_OBJ_FLAG_HIDDEN));
      assert(!lv_obj_has_flag(f.mower_icon, LV_OBJ_FLAG_HIDDEN));
      assert(lv_obj_has_flag(f.actions[2], LV_OBJ_FLAG_HIDDEN));
      assert(lv_obj_has_flag(f.suction, LV_OBJ_FLAG_HIDDEN));
      assert(lv_obj_has_flag(f.room, LV_OBJ_FLAG_HIDDEN));
      assert(std::string_view(lv_label_get_text(f.action_labels[0])) == "Start mowing");
      f.tap(260, 380);
      assert(f.commands() == 1);
    } else if (name == "glyph") {
      assert(lv_obj_has_state(f.suction, LV_STATE_DISABLED));
      f.tap(260, 620);
      assert(f.commands() == 0);
      assert(lv_obj_has_flag(f.modal, LV_OBJ_FLAG_HIDDEN));
    } else if (name == "handoff") {
      assert(f.control.open_number(2, "number", 348));
      assert(lv_obj_has_flag(f.root, LV_OBJ_FLAG_HIDDEN));
      assert(!lv_obj_has_flag(f.widgets.title, LV_OBJ_FLAG_HIDDEN));
    } else if (name == "locate") {
      f.tap(260, 540);
      assert(f.commands() == 1);
      assert(f.messages.back().operation == Op::CONTROL_OPERATION_ROBOT_LOCATE);
      ControlMessage result;
      result.generic = true;
      result.kind = ControlKind::CONTROL_KIND_RESULT;
      result.identity = f.control.owner().identity();
      result.command_seq = f.control.owner().command_sequence();
      result.state.state_seq = f.control.owner().state().state_seq;
      result.result = ControlResult::CONTROL_RESULT_SERVICE_COMPLETED;
      f.receive(result);
      assert(!f.control.owner().is_command_pending());
      assert(std::string_view(lv_label_get_text(f.status)) == "Command accepted by HA");
    } else
      assert(name == "open");
    if (name != "handoff")
      f.check_disabled_palette();
    std::cout << "ROBOT " << name << " pass=1\n";
  }
  lv_deinit();
}
