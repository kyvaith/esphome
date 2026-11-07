#include "real_lvgl_test_support.h"
#include "material_entity_control_component.h"
#include "disabled_control_pixel_test.h"
#include <iostream>
using namespace esphome::lvgl_material;
namespace rt = real_lvgl_test;

struct Control : MaterialEntityControlComponent {
  using MaterialEntityControlComponent::MaterialEntityControlComponent;
  bool captured() const { return this->captured_target_ != nullptr; }
};
struct Fixture {
  esphome::lvgl::LvglComponent lvgl;
  rt::HostDisplay display{this->lvgl};
  rt::NativeWidgets widgets{lv_screen_active()};
  rt::Pointer pointer{this->display.get()};
  esphome::lvgl::LvglNavigation navigation;
  esphome::lvgl::LvglApplication application;
  Control control{&this->lvgl};
  lv_obj_t *root, *title, *value, *state, *status, *power, *oscillation, *direction, *preset, *modal, *back, *caption;
  std::array<lv_obj_t *, 8> speeds{}, speed_labels{}, presets{}, preset_labels{};
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
  Fixture(std::string_view variant = {}) {
    this->navigation.active = &this->application;
    this->root = container(this->widgets.root);
    this->title = label(root, 130, 104);
    this->value = label(root, 150, 260);
    this->state = label(root, 200, 326);
    this->caption = label(root, 200, 360);
    this->status = label(root, 200, 730);
    this->power = button(root, 280, 656, 240, 64);
    this->oscillation = button(root, 146, 576, 248, 64);
    this->direction = button(root, 406, 576, 248, 64);
    this->preset = button(root, 146, 500, 508, 64);
    for (size_t i = 0; i < 8; i++) {
      this->speeds[i] = button(root, 146 + 128 * (i % 4), 386 + 54 * (i / 4), 120, 46);
      this->speed_labels[i] = label(speeds[i], 0, 0);
    }
    this->modal = container(root);
    this->back = button(modal, 280, 650, 240, 64);
    for (size_t i = 0; i < 8; i++) {
      this->presets[i] = button(modal, 130 + 276 * (i % 2), 220 + 100 * (i / 2), 264, 72);
      this->preset_labels[i] = label(presets[i], 0, 0);
    }
    this->control.set_widgets(widgets.root, widgets.hit, widgets.title, widgets.value, widgets.unknown, widgets.on,
                              widgets.off);
    this->control.set_state_label(widgets.state);
    this->control.set_status_label(widgets.status);
    this->control.set_number_profile(&navigation, &application, widgets.on_label, widgets.off_label, LV_FONT_DEFAULT);
    this->control.set_fan_profile(root, title, value, state, status, power, label(power, 0, 0), oscillation,
                                  label(oscillation, 0, 0), direction, label(direction, 0, 0), preset,
                                  label(preset, 0, 0), modal, back);
    this->control.set_fan_speed_caption(caption);
    for (size_t i = 0; i < 8; i++) {
      this->control.set_fan_speed_button(i, speeds[i], speed_labels[i]);
      this->control.set_fan_preset_option(i, presets[i], preset_labels[i]);
    }
    this->control.set_initial_enabled(true);
    this->control.add_on_message_callback([this](const char *payload) {
      MaterialEntityControlCodec codec;
      ControlMessage message;
      assert(codec.parse_entity(payload, message));
      messages.push_back(message);
    });
    this->control.setup();
    assert(!this->control.is_failed());
    rt::advance(200);
    assert(control.bind_entity_json(
        R"({"v":1,"type":"entity_control_v1","kind":"bind","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})"));
    assert(control.open_fan(31, "fan-card", 348));
    ControlMessage grant;
    grant.generic = true;
    grant.kind = ControlKind::CONTROL_KIND_GRANT;
    grant.identity = control.owner().identity();
    grant.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    grant.state.state_seq = 1;
    grant.state.title.assign("Fan");
    grant.state.emplace_family<ControlSnapshot::Fan>();
    auto &fan = (*grant.state.family_if<ControlSnapshot::Fan>());
    fan.caps.turn_on = fan.caps.turn_off = fan.caps.oscillate = fan.caps.direction = true;
    fan.caps.speeds = {0, 33, 66, 100};
    fan.caps.speed_count = 4;
    fan.caps.presets[0].assign("Eco");
    fan.caps.presets[1].assign("Sleep");
    fan.caps.preset_count = 2;
    fan.available = fan.preset_known = true;
    fan.power = 1;
    fan.percentage = 33;
    fan.oscillating = fan.direction = 0;
    fan.state_generation = 1;
    if (variant.starts_with("preset_only") || variant == "power_only") {
      fan.caps.speed_count = 0;
      fan.caps.speeds = {};
      fan.percentage = fan.oscillating = fan.direction = ControlSnapshot::Fan::UNKNOWN;
      fan.caps.oscillate = fan.caps.direction = false;
      fan.preset_index = 0;
      if (variant != "preset_only") {
        fan.preset_index = ControlSnapshot::Fan::UNKNOWN;
        fan.preset_known = false;
        if (variant == "power_only")
          fan.caps.preset_count = 0;
      }
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
    ControlMessage message;
    message.generic = true;
    message.kind = ControlKind::CONTROL_KIND_STATE;
    message.identity = control.owner().identity();
    message.state = control.owner().state();
    message.state.state_seq++;
    return message;
  }
  void tap(int x, int y) {
    pointer.press(x, y);
    pointer.release();
    rt::advance(40);
  }
  size_t commands() const {
    size_t result = 0;
    for (const auto &message : messages)
      result += message.kind == ControlKind::CONTROL_KIND_COMMAND;
    return result;
  }
  void check_disabled_palette() {
    const auto verify = [&](lv_obj_t *button, lv_obj_t *text) {
      assert_readable_disabled_control(this->lvgl, button, text);
      if (!lv_obj_has_state(button, LV_STATE_DISABLED))
        return;
      assert(lv_color_eq(lv_obj_get_style_bg_color(button, LV_PART_MAIN),
                         lv_obj_get_style_bg_color(widgets.off, LV_PART_MAIN)));
      assert(lv_color_eq(lv_obj_get_style_text_color(text, LV_PART_MAIN),
                         lv_obj_get_style_text_color(widgets.off_label, LV_PART_MAIN)));
    };
    for (auto *button : {power, oscillation, direction})
      verify(button, lv_obj_get_child(button, 0));
    for (size_t i = 0; i < 8; i++) {
      verify(speeds[i], speed_labels[i]);
      verify(presets[i], preset_labels[i]);
    }
  }
  ~Fixture() {
    control.on_shutdown();
    // Root deletion emits delete events while the owner is still alive.
    lv_obj_delete(widgets.root);
  }
};

int main(int argc, char **argv) {
  assert(argc == 2);
  lv_init();
  const std::string_view name = argv[1];
  {
    Fixture f(name);
    f.check_disabled_palette();
    assert(!lv_obj_has_flag(f.root, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(f.widgets.title, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(f.modal, LV_OBJ_FLAG_HIDDEN));
    if (!name.starts_with("preset_only") && name != "power_only") {
      assert(std::string_view(lv_label_get_text(f.value)) == "33%");
      assert(!lv_obj_has_state(f.speeds[1], LV_STATE_DISABLED));
      assert(!lv_obj_has_flag(f.caption, LV_OBJ_FLAG_HIDDEN));
    }
    if (name == "power" || name == "renewal" || name == "aba") {
      f.pointer.press(400, 680);
      assert(f.control.captured());
      if (name != "power") {
        auto message = f.next();
        message.state.title.assign("Renamed");
        if (name == "aba")
          (*message.state.family_if<ControlSnapshot::Fan>()).state_generation += 2;
        f.receive(message);
        assert(f.control.captured() == (name == "renewal"));
      }
      f.pointer.release();
      assert(f.commands() == (name == "aba" ? 0U : 1U));
      if (f.commands()) {
        assert(f.messages.back().operation == ControlOperation::CONTROL_OPERATION_FAN_POWER);
        assert(f.messages.back().fan_target_index == 0);
      }
    } else if (name == "speed") {
      f.tap(320, 410);  // Current speed does not send a command or dim its label.
      assert(f.commands() == 0);
      f.tap(450, 410);
      assert(f.commands() == 1);
      assert(f.messages.back().operation == ControlOperation::CONTROL_OPERATION_FAN_SPEED);
      assert(f.messages.back().fan_target_index == 2);
    } else if (name.starts_with("preset_speed")) {
      auto message = f.next();
      auto &fan = *message.state.family_if<ControlSnapshot::Fan>();
      fan.state_generation++;
      fan.percentage = ControlSnapshot::Fan::UNKNOWN;
      fan.preset_known = name != "preset_speed_unknown";
      fan.preset_index = fan.preset_known ? 0 : ControlSnapshot::Fan::UNKNOWN;
      f.receive(message);
      assert(std::string_view(lv_label_get_text(f.value)) == "--");
      assert(std::string_view(lv_label_get_text(lv_obj_get_child(f.preset, 0))) ==
             (fan.preset_known ? "Mode: Eco" : "Mode: Unknown"));
      for (size_t index = 0; index < fan.caps.speed_count; index++) {
        assert(lv_obj_has_state(f.speeds[index], LV_STATE_DISABLED) == !fan.preset_known);
        assert(lv_color_eq(lv_obj_get_style_bg_color(f.speeds[index], LV_PART_MAIN),
                           lv_obj_get_style_bg_color(f.widgets.off, LV_PART_MAIN)));
      }
      f.pointer.press(450, 410);
      assert(f.commands() == 0);
      assert(f.control.captured() == fan.preset_known);
      if (name == "preset_speed_aba") {
        message.state.state_seq++;
        fan.state_generation++;
        fan.preset_index = ControlSnapshot::Fan::UNKNOWN;
        f.receive(message);
        message.state.state_seq++;
        fan.state_generation++;
        fan.preset_index = 0;
        f.receive(message);
        assert(!f.control.captured());
      } else if (name == "preset_speed_lost") {
        message.state.state_seq++;
        fan.caps.speed_count = 0;
        fan.caps.speeds = {};
        f.receive(message, false);
        assert(!f.control.captured());
        assert(!f.control.owner().has_selection());
      }
      f.pointer.release();
      rt::advance(40);
      assert(f.commands() == (name == "preset_speed" ? 1U : 0U));
      if (name == "preset_speed") {
        assert(f.messages.back().operation == ControlOperation::CONTROL_OPERATION_FAN_SPEED);
        assert(f.messages.back().fan_target_index == 2);
        assert(f.messages.back().expected_state_generation == 2);
        assert(std::string_view(lv_label_get_text(f.value)) == "--");
        // Only authoritative feedback may replace the unknown percentage.
        message.state.state_seq++;
        fan.state_generation++;
        fan.percentage = 66;
        fan.preset_index = ControlSnapshot::Fan::UNKNOWN;
        f.receive(message);
        assert(std::string_view(lv_label_get_text(f.value)) == "66%");
        assert(std::string_view(lv_label_get_text(lv_obj_get_child(f.preset, 0))) == "Mode: Manual");
        ControlMessage result;
        result.generic = true;
        result.kind = ControlKind::CONTROL_KIND_RESULT;
        result.identity = message.identity;
        result.command_seq = f.control.owner().command_sequence();
        result.state.state_seq = message.state.state_seq;
        result.result = ControlResult::CONTROL_RESULT_STATE_MATCHES;
        f.receive(result);
        f.tap(450, 410);
        assert(f.commands() == 1);  // The now-current speed is a no-op.
      }
    } else if (name == "noncanonical" || name == "speed_unknown") {
      auto message = f.next();
      (*message.state.family_if<ControlSnapshot::Fan>()).state_generation++;
      (*message.state.family_if<ControlSnapshot::Fan>()).percentage =
          name == "noncanonical" ? 67 : ControlSnapshot::Fan::UNKNOWN;
      f.receive(message);
      assert(std::string_view(lv_label_get_text(f.value)) == (name == "noncanonical" ? "67%" : "--"));
      f.tap(320, 410);
      assert(f.commands() == (name == "noncanonical" ? 1U : 0U));
      if (f.commands()) {
        assert(f.messages.back().operation == ControlOperation::CONTROL_OPERATION_FAN_SPEED);
        assert(f.messages.back().fan_target_index == 1);
      } else {
        assert(!lv_obj_has_state(f.power, LV_STATE_DISABLED));
      }
    } else if (name.starts_with("preset_only") || name == "power_only") {
      assert(lv_obj_has_flag(f.caption, LV_OBJ_FLAG_HIDDEN));
      assert(lv_obj_has_flag(f.oscillation, LV_OBJ_FLAG_HIDDEN));
      assert(lv_obj_has_flag(f.direction, LV_OBJ_FLAG_HIDDEN));
      for (auto *speed : f.speeds)
        assert(lv_obj_has_flag(speed, LV_OBJ_FLAG_HIDDEN));
      const char *hero = name == "preset_only" ? "Eco" : name == "power_only" ? "On" : "Unknown";
      assert(std::string_view(lv_label_get_text(f.value)) == hero);
      assert(!lv_obj_has_state(f.power, LV_STATE_DISABLED));
      if (name == "preset_only") {
        f.tap(400, 532);
        assert(!lv_obj_has_flag(f.modal, LV_OBJ_FLAG_HIDDEN));
        f.tap(520, 250);
        assert(f.commands() == 1);
        assert(f.messages.back().operation == ControlOperation::CONTROL_OPERATION_FAN_PRESET);
        assert(f.messages.back().fan_target_index == 1);
      } else {
        assert(name == "power_only" ? lv_obj_has_flag(f.preset, LV_OBJ_FLAG_HIDDEN)
                                    : lv_obj_has_state(f.preset, LV_STATE_DISABLED));
      }
    } else if (name == "preset") {
      f.tap(400, 532);
      assert(!lv_obj_has_flag(f.modal, LV_OBJ_FLAG_HIDDEN));
      assert(f.commands() == 0);
      f.tap(250, 250);
      assert(f.commands() == 1);
      assert(f.messages.back().operation == ControlOperation::CONTROL_OPERATION_FAN_PRESET);
      assert(f.messages.back().fan_target_index == 0);
      assert(lv_obj_has_flag(f.modal, LV_OBJ_FLAG_HIDDEN));
    } else if (name == "unknown") {
      auto message = f.next();
      auto &fan = (*message.state.family_if<ControlSnapshot::Fan>());
      fan.state_generation++;
      fan.available = fan.preset_known = false;
      fan.power = fan.percentage = fan.preset_index = fan.oscillating = fan.direction = ControlSnapshot::Fan::UNKNOWN;
      f.receive(message);
      assert(lv_obj_has_state(f.power, LV_STATE_DISABLED));
      assert(lv_obj_has_state(f.preset, LV_STATE_DISABLED));
      assert(std::string_view(lv_label_get_text(lv_obj_get_child(f.oscillation, 0))) == "Oscillation");
      assert(std::string_view(lv_label_get_text(lv_obj_get_child(f.direction, 0))) == "Direction");
      f.tap(400, 680);
      assert(f.commands() == 0);
    } else if (name == "handoff") {
      assert(f.control.open_number(2, "number", 348));
      assert(lv_obj_has_flag(f.root, LV_OBJ_FLAG_HIDDEN));
      assert(!lv_obj_has_flag(f.widgets.title, LV_OBJ_FLAG_HIDDEN));
    } else if (name == "glyphs") {
      auto message = f.next();
      (*message.state.family_if<ControlSnapshot::Fan>()).caps.presets[1].assign("\xF0\x9F\x8C\xA1");
      f.receive(message, false);  // Changed metadata retires the selection.
      assert(!f.control.owner().has_selection());
    } else {
      assert(name == "open");
    }
    if (name != "handoff")
      f.check_disabled_palette();
    std::cout << "FAN " << name << " pass=1\n";
  }
  lv_deinit();
}
