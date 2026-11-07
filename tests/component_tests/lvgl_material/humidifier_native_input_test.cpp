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
  rt::HostDisplay display{lvgl};
  rt::NativeWidgets widgets{lv_screen_active()};
  rt::Pointer pointer{display.get()};
  esphome::lvgl::LvglNavigation navigation;
  esphome::lvgl::LvglApplication application;
  Control control{&lvgl};
  lv_obj_t *root, *title, *kind, *target, *current, *state, *status, *track, *fill, *knob, *minimum, *maximum, *minus,
      *plus;
  lv_obj_t *power, *mode, *modal, *back;
  std::array<lv_obj_t *, 8> modes{}, mode_labels{};
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

  Fixture(bool zero = false) {
    navigation.active = &application;
    root = container(widgets.root);
    title = label(root, 130, 104);
    kind = label(root, 130, 150);
    target = label(root, 150, 260);
    current = label(root, 200, 326);
    state = label(root, 200, 358);
    status = label(root, 200, 674);
    track = lv_obj_create(root);
    lv_obj_set_pos(track, 146, 394);
    lv_obj_set_size(track, 508, 72);
    lv_obj_set_style_pad_all(track, 0, 0);
    lv_obj_set_style_border_width(track, 0, 0);
    lv_obj_remove_flag(track, LV_OBJ_FLAG_SCROLLABLE);
    fill = lv_obj_create(track);
    lv_obj_set_pos(fill, 20, 30);
    lv_obj_set_size(fill, 0, 12);
    lv_obj_remove_flag(fill, LV_OBJ_FLAG_CLICKABLE);
    knob = lv_obj_create(track);
    lv_obj_set_pos(knob, 0, 16);
    lv_obj_set_size(knob, 40, 40);
    lv_obj_remove_flag(knob, LV_OBJ_FLAG_CLICKABLE);
    minimum = label(root, 146, 474);
    maximum = label(root, 550, 474);
    minus = button(root, 260, 506, 64, 64);
    plus = button(root, 476, 506, 64, 64);
    power = button(root, 180, 592, 210, 64);
    mode = button(root, 410, 592, 210, 64);
    modal = container(root);
    back = button(modal, 280, 650, 240, 64);
    for (size_t i = 0; i < 8; i++) {
      modes[i] = button(modal, 130 + 276 * (i % 2), 220 + 100 * (i / 2), 264, 72);
      mode_labels[i] = label(modes[i], 0, 0);
    }
    control.set_widgets(widgets.root, widgets.hit, widgets.title, widgets.value, widgets.unknown, widgets.on,
                        widgets.off);
    control.set_state_label(widgets.state);
    control.set_status_label(widgets.status);
    control.set_number_profile(&navigation, &application, widgets.on_label, widgets.off_label, LV_FONT_DEFAULT);
    control.set_humidifier_profile(root, title, kind, target, current, state, status, track, fill, knob, minimum,
                                   maximum, minus, plus, power, label(power, 0, 0), mode, label(mode, 0, 0), modal,
                                   back);
    for (size_t i = 0; i < 8; i++)
      control.set_humidifier_mode_option(i, modes[i], mode_labels[i]);
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
    assert(control.open_humidifier(31, "humidity-card", 348));
    ControlMessage grant;
    grant.generic = true;
    grant.kind = ControlKind::CONTROL_KIND_GRANT;
    grant.identity = control.owner().identity();
    grant.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    grant.state.title.assign("Humidifier");
    grant.state.state_seq = 1;
    grant.state.emplace_family<ControlSnapshot::Humidifier>();
    auto &h = (*grant.state.family_if<ControlSnapshot::Humidifier>());
    h.available = true;
    h.power = 1;
    h.index = zero ? 0 : 15;
    h.mode_index = 0;
    h.device_class = 0;
    h.state_generation = 1;
    h.caps.turn_on = h.caps.turn_off = true;
    h.caps.mode_count = 2;
    h.caps.modes[0].assign("Eco");
    h.caps.modes[1].assign("Normal");
    h.domain.minimum = 30 * NUMBER_SCALE;
    h.domain.maximum = (zero ? 30 : 80) * NUMBER_SCALE;
    h.domain.step = NUMBER_SCALE;
    h.domain.max_index = zero ? 0 : 50;
    h.domain.unit[0] = '%';
    h.target_text.assign(zero ? "30" : "45");
    h.current.assign("39.5");
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
      assert_readable_disabled_control(this->lvgl, button, text);
      if (!lv_obj_has_state(button, LV_STATE_DISABLED))
        return;
      assert(lv_color_eq(lv_obj_get_style_bg_color(button, LV_PART_MAIN),
                         lv_obj_get_style_bg_color(widgets.off, LV_PART_MAIN)));
      assert(lv_color_eq(lv_obj_get_style_text_color(text, LV_PART_MAIN),
                         lv_obj_get_style_text_color(widgets.off_label, LV_PART_MAIN)));
    };
    verify(power, lv_obj_get_child(power, 0));
    for (size_t i = 0; i < 8; i++)
      verify(modes[i], mode_labels[i]);
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
    Fixture f(name == "zero");
    f.check_disabled_palette();
    assert(!lv_obj_has_flag(f.root, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(f.widgets.title, LV_OBJ_FLAG_HIDDEN));
    if (name == "drag" || name == "measurement" || name == "aba") {
      f.pointer.press(306, 430);
      assert(f.control.captured());
      f.pointer.move(447, 430);
      assert(std::string_view(lv_label_get_text(f.target)) == "Target: 60%");
      assert(f.commands() == 0);
      if (name != "drag") {
        auto message = f.next();
        (*message.state.family_if<ControlSnapshot::Humidifier>()).current.assign("41.25");
        if (name == "aba")
          (*message.state.family_if<ControlSnapshot::Humidifier>()).state_generation += 2;
        f.receive(message);
        assert(f.control.captured() == (name == "measurement"));
      }
      f.pointer.release();
      assert(f.commands() == (name == "aba" ? 0U : 1U));
      if (name != "aba") {
        assert(f.messages.back().operation == ControlOperation::CONTROL_OPERATION_HUMIDIFIER_TARGET);
        assert(f.messages.back().humidifier_target_index == 30);
      }
      if (name == "measurement")
        assert(std::string_view(lv_label_get_text(f.current)) == "Current: 41.25%");
    } else if (name == "power") {
      f.tap(280, 624);
      assert(f.commands() == 1);
      assert(f.messages.back().operation == ControlOperation::CONTROL_OPERATION_HUMIDIFIER_POWER);
      assert(f.messages.back().humidifier_target_index == 0);
    } else if (name == "mode") {
      f.tap(500, 624);
      assert(!lv_obj_has_flag(f.modal, LV_OBJ_FLAG_HIDDEN));
      f.tap(520, 250);
      assert(f.commands() == 1);
      assert(f.messages.back().operation == ControlOperation::CONTROL_OPERATION_HUMIDIFIER_MODE);
      assert(f.messages.back().humidifier_target_index == 1);
      assert(lv_obj_has_flag(f.modal, LV_OBJ_FLAG_HIDDEN));
    } else if (name == "unknown") {
      auto message = f.next();
      (*message.state.family_if<ControlSnapshot::Humidifier>()).index = 255;
      (*message.state.family_if<ControlSnapshot::Humidifier>()).target_text.assign("--");
      (*message.state.family_if<ControlSnapshot::Humidifier>()).state_generation++;
      f.receive(message);
      assert(lv_obj_has_state(f.track, LV_STATE_DISABLED));
      assert(!lv_obj_has_state(f.power, LV_STATE_DISABLED));
      f.tap(447, 430);
      assert(f.commands() == 0);
    } else if (name == "zero") {
      assert(lv_obj_has_state(f.track, LV_STATE_DISABLED));
      assert(lv_obj_has_state(f.minus, LV_STATE_DISABLED));
      assert(lv_obj_has_state(f.plus, LV_STATE_DISABLED));
      assert(std::string_view(lv_label_get_text(f.target)) == "Target: 30%");
      assert(!lv_obj_has_state(f.power, LV_STATE_DISABLED));
    } else if (name == "handoff") {
      assert(f.control.open_number(2, "number", 348));
      assert(lv_obj_has_flag(f.root, LV_OBJ_FLAG_HIDDEN));
      assert(!lv_obj_has_flag(f.widgets.title, LV_OBJ_FLAG_HIDDEN));
    } else
      assert(name == "open");
    if (name != "handoff")
      f.check_disabled_palette();
    std::cout << "HUMIDIFIER " << name << " pass=1\n";
  }
  lv_deinit();
}
