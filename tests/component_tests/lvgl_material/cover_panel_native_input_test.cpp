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
  rt::NativeWidgets old{lv_screen_active()};
  rt::Pointer pointer{display.get()};
  esphome::lvgl::LvglNavigation navigation;
  esphome::lvgl::LvglApplication application;
  Control control{&lvgl};
  lv_obj_t *root, *title, *kind, *state, *status, *current, *tilt_label;
  lv_obj_t *groups[2], *tracks[2], *fills[2], *knobs[2], *icons[2];
  lv_obj_t *buttons[2][3], *labels[2][3];
  std::vector<ControlMessage> messages;
  lv_obj_t *object(lv_obj_t *parent, int x, int y, int w, int h) {
    auto *widget = lv_obj_create(parent);
    lv_obj_remove_style_all(widget);
    lv_obj_set_pos(widget, x, y);
    lv_obj_set_size(widget, w, h);
    lv_obj_remove_flag(widget, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(widget, LV_OBJ_FLAG_SCROLLABLE);
    return widget;
  }
  lv_obj_t *text(lv_obj_t *parent, int x, int y) {
    auto *widget = lv_label_create(parent);
    lv_obj_set_pos(widget, x, y);
    lv_label_set_text(widget, "");
    return widget;
  }
  Fixture(bool tilt_only = false, bool read_only = false) {
    navigation.active = &application;
    root = object(old.root, 0, 0, 800, 800);
    title = text(root, 130, 104);
    kind = text(root, 130, 68);
    state = text(root, 130, 264);
    status = text(root, 130, 720);
    for (int axis = 0; axis < 2; axis++) {
      groups[axis] = object(root, 146, 306 + 190 * axis, 508, 180);
      tracks[axis] = object(groups[axis], 0, 38, 508, 64);
      lv_obj_add_flag(tracks[axis], LV_OBJ_FLAG_CLICKABLE);
      fills[axis] = object(tracks[axis], 20, 26, 0, 12);
      knobs[axis] = object(tracks[axis], 0, 12, 40, 40);
      for (int index = 0; index < 3; index++) {
        buttons[axis][index] = lv_button_create(groups[axis]);
        lv_obj_set_pos(buttons[axis][index], index * 174, 112);
        lv_obj_set_size(buttons[axis][index], 156, 56);
        lv_obj_set_style_text_opa(buttons[axis][index], LV_OPA_40, LV_STATE_DISABLED);
        lv_obj_set_style_bg_opa(buttons[axis][index], LV_OPA_30, LV_STATE_DISABLED);
        labels[axis][index] = text(buttons[axis][index], 0, 0);
        lv_obj_center(labels[axis][index]);
      }
    }
    current = text(groups[0], 0, 0);
    tilt_label = text(groups[1], 0, 0);
    icons[0] = object(root, 350, 160, 40, 40);
    icons[1] = object(root, 350, 160, 50, 50);
    control.set_widgets(old.root, old.hit, old.title, old.value, old.unknown, old.on, old.off);
    control.set_state_label(old.state);
    control.set_status_label(old.status);
    control.set_number_profile(&navigation, &application, old.on_label, old.off_label, LV_FONT_DEFAULT);
    control.set_cover_profile(root, current, status);
    control.set_cover_priority_stop_enabled(true);
    control.set_cover_panel_profile(title, kind, state, groups[0], tracks[0], fills[0], knobs[0], groups[1], tilt_label,
                                    tracks[1], fills[1], knobs[1]);
    control.set_cover_panel_icon(0, icons[0]);
    control.set_cover_panel_icon(2, icons[1]);
    for (int index = 0; index < 3; index++) {
      control.set_cover_button(index, buttons[0][index], labels[0][index]);
      control.set_cover_tilt_button(index, buttons[1][index], labels[1][index]);
    }
    control.set_initial_enabled(true);
    control.add_on_message_callback([this](const char *payload) {
      MaterialEntityControlCodec codec;
      ControlMessage message;
      assert(codec.parse_entity(payload, message, false, true));
      messages.push_back(message);
    });
    control.setup();
    assert(!control.is_failed());
    rt::advance(200);
    assert(control.bind_entity_json(
        R"({"v":1,"type":"entity_control_v1","kind":"bind","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})"));
    ArduinoJson::JsonDocument document;
    auto features = document.to<ArduinoJson::JsonObject>();
    assert(control.populate_cover_panel_features(features) && features["cover_panel_version"].as<int>() == 1);
    assert(control.bind_cover_panel_features("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 1, features));
    assert(!features["accepted"].as<bool>());
    assert(control.bind_cover_features("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 1, features));
    assert(features["accepted"].as<bool>());
    assert(control.bind_cover_panel_features("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 1, features));
    assert(features["accepted"].as<bool>());
    admit(tilt_only, read_only);
  }
  void admit(bool tilt_only = false, bool read_only = false) {
    assert(control.open_cover(17, "blind", 87));
    ControlMessage value;
    value.generic = true;
    value.kind = ControlKind::CONTROL_KIND_GRANT;
    value.identity = control.owner().identity();
    value.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    value.state.title.assign("Living room blind");
    value.state.state_seq = 1;
    value.state.emplace_family<ControlCoverSnapshot>();
    auto &cover = (*value.state.family_if<ControlCoverSnapshot>());
    cover.state_generation = 1;
    cover.available = true;
    cover.state = ControlCoverState::CONTROL_COVER_STATE_OPEN;
    cover.position_known = !tilt_only;
    cover.position = tilt_only ? 0 : 40;
    cover.caps = {!tilt_only, !tilt_only, !tilt_only};
    cover.panel.present = true;
    cover.panel.device_class = 2;
    cover.panel.tilt_known = true;
    cover.panel.tilt_position = 30;
    cover.panel.caps = {!tilt_only && !read_only, true, true, true, !read_only};
    receive(value);
    assert(control.resume_control_view(control.get_selection_generation()));
    rt::advance(200);
    assert(control.owner().is_view_ready());
  }
  void receive(ControlMessage value, bool expected = true) {
    MaterialEntityControlCodec codec;
    char wire[2049];
    size_t length;
    assert(codec.serialize_entity(value, wire, sizeof(wire), length, false, true));
    assert(control.receive_entity({wire, length}) == expected);
    lv_refr_now(display.get());
  }
  void update(bool generation = true, bool unknown = false) {
    ControlMessage value;
    value.generic = true;
    value.kind = ControlKind::CONTROL_KIND_STATE;
    value.identity = control.owner().identity();
    value.state = control.owner().state();
    value.state.state_seq++;
    if (generation)
      (*value.state.family_if<ControlCoverSnapshot>()).state_generation += 2;
    if (unknown) {
      (*value.state.family_if<ControlCoverSnapshot>()).available = false;
      (*value.state.family_if<ControlCoverSnapshot>()).state = ControlCoverState::CONTROL_COVER_STATE_UNKNOWN;
      (*value.state.family_if<ControlCoverSnapshot>()).position_known = false;
      (*value.state.family_if<ControlCoverSnapshot>()).position = 0;
      (*value.state.family_if<ControlCoverSnapshot>()).panel.tilt_known = false;
      (*value.state.family_if<ControlCoverSnapshot>()).panel.tilt_position = 0;
    }
    receive(value);
  }
  void press(lv_obj_t *widget) {
    lv_area_t bounds;
    lv_obj_get_coords(widget, &bounds);
    pointer.press((bounds.x1 + bounds.x2) / 2, (bounds.y1 + bounds.y2) / 2);
  }
  void tap(lv_obj_t *widget) {
    press(widget);
    pointer.release();
    rt::advance(40);
  }
  void drag(int axis) {
    press(tracks[axis]);
    assert(control.captured());
    pointer.move(634, 376 + 190 * axis);
  }
  size_t commands() const {
    size_t count = 0;
    for (auto &m : messages)
      count += m.kind == ControlKind::CONTROL_KIND_COMMAND;
    return count;
  }
  void check_disabled_palette() {
    const auto verify = [&](lv_obj_t *button, lv_obj_t *text) {
      assert_readable_disabled_control(this->lvgl, button, text);
      if (!lv_obj_has_state(button, LV_STATE_DISABLED))
        return;
      assert(lv_color_eq(lv_obj_get_style_bg_color(button, LV_PART_MAIN),
                         lv_obj_get_style_bg_color(old.off, LV_PART_MAIN)));
      assert(lv_color_eq(lv_obj_get_style_text_color(text, LV_PART_MAIN),
                         lv_obj_get_style_text_color(old.off_label, LV_PART_MAIN)));
    };
    for (size_t axis = 0; axis < 2; axis++)
      for (size_t i = 0; i < 3; i++)
        verify(buttons[axis][i], labels[axis][i]);
  }
  ~Fixture() {
    control.on_shutdown();
    lv_obj_delete(old.root);
  }
};
int main(int argc, char **argv) {
  assert(argc == 2);
  lv_init();
  const std::string_view name = argv[1];
  {
    Fixture f(name == "tilt_only", name == "read_only");
    if (name != "legacy_restore")
      f.check_disabled_palette();
    if (name == "open") {
      assert(lv_obj_is_visible(f.title) && !lv_obj_is_visible(f.old.title));
      assert(lv_obj_is_visible(f.icons[1]) && !lv_obj_is_visible(f.icons[0]));
      assert(std::string(lv_label_get_text(f.current)) == "Position: 40%");
      assert(std::string(lv_label_get_text(f.tilt_label)) == "Tilt: 30%");
      assert(lv_obj_get_x(f.knobs[0]) == 187 && lv_obj_get_y(f.knobs[0]) == 12);
      assert(lv_obj_get_x(f.fills[0]) == 20 && lv_obj_get_y(f.fills[0]) == 26);
    } else if (name == "position" || name == "tilt") {
      const int axis = name == "tilt";
      f.drag(axis);
      assert(f.commands() == 0);
      assert(std::string(lv_label_get_text(axis ? f.tilt_label : f.current)).find("100%") != std::string::npos);
      f.pointer.release();
      assert(f.commands() == 1);
      assert(f.messages.back().cover_target_index == 100);
      assert(f.messages.back().operation == (axis ? ControlOperation::CONTROL_OPERATION_COVER_SET_TILT_POSITION
                                                  : ControlOperation::CONTROL_OPERATION_COVER_SET_POSITION));
    } else if (name == "aba" || name == "renewal") {
      f.drag(0);
      f.update(name == "aba");
      f.pointer.release();
      assert(f.commands() == (name == "renewal" ? 1 : 0));
    } else if (name == "stop" || name == "tilt_stop") {
      const int axis = name == "tilt_stop";
      f.drag(axis);
      f.pointer.release();
      assert(f.commands() == 1);
      assert(lv_obj_has_state(f.buttons[1 - axis][1], LV_STATE_DISABLED));
      f.press(f.buttons[axis][1]);
      assert(f.control.captured());
      f.update();
      f.pointer.release();
      assert(f.commands() == 2);
      assert(f.messages.back().expected_state_generation == 3);
      assert(lv_obj_has_state(f.buttons[axis][1], LV_STATE_DISABLED));
    } else if (name == "unknown") {
      f.update(true, true);
      for (int axis = 0; axis < 2; axis++) {
        assert(lv_obj_has_state(f.tracks[axis], LV_STATE_DISABLED));
        assert(!lv_obj_is_visible(f.knobs[axis]));
      }
      assert(f.commands() == 0);
    } else if (name == "tilt_only") {
      assert(!lv_obj_is_visible(f.groups[0]) && lv_obj_is_visible(f.groups[1]));
      f.tap(f.buttons[1][0]);
      assert(f.commands() == 1);
    } else if (name == "read_only") {
      assert(!lv_obj_is_visible(f.tracks[0]) && !lv_obj_is_visible(f.tracks[1]));
      assert(std::string(lv_label_get_text(f.current)) == "Position: 40%");
      assert(lv_obj_is_visible(f.current));
    } else if (name == "caption_capability") {
      f.control.invalidate(ControlReason::CONTROL_REASON_SELECTION_CHANGED);
      f.control.loop();
      f.admit(false, true);
      ControlMessage unknown;
      unknown.generic = true;
      unknown.kind = ControlKind::CONTROL_KIND_STATE;
      unknown.identity = f.control.owner().identity();
      unknown.state = f.control.owner().state();
      unknown.state.state_seq++;
      (*unknown.state.family_if<ControlCoverSnapshot>()).position_known = false;
      (*unknown.state.family_if<ControlCoverSnapshot>()).position = 0;
      (*unknown.state.family_if<ControlCoverSnapshot>()).panel.tilt_known = false;
      (*unknown.state.family_if<ControlCoverSnapshot>()).panel.tilt_position = 0;
      (*unknown.state.family_if<ControlCoverSnapshot>()).state_generation++;
      f.receive(unknown);
      assert(!lv_obj_is_visible(f.current));
      assert(lv_obj_is_visible(f.groups[0]));
      assert(lv_obj_is_visible(f.buttons[0][0]));
    } else if (name == "legacy_restore") {
      assert(f.control.bind_entity_json(
          R"({"v":1,"type":"entity_control_v1","kind":"bind","channel":"cccccccccccccccccccccccccccccccc"})"));
      assert(f.control.open_cover(17, "legacy", 87));
      ControlMessage grant;
      grant.generic = true;
      grant.kind = ControlKind::CONTROL_KIND_GRANT;
      grant.identity = f.control.owner().identity();
      grant.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
      grant.state.title.assign("Legacy");
      grant.state.state_seq = 1;
      grant.state.emplace_family<ControlCoverSnapshot>();
      (*grant.state.family_if<ControlCoverSnapshot>()).state = ControlCoverState::CONTROL_COVER_STATE_OPEN;
      (*grant.state.family_if<ControlCoverSnapshot>()).available = true;
      (*grant.state.family_if<ControlCoverSnapshot>()).state_generation = 1;
      (*grant.state.family_if<ControlCoverSnapshot>()).caps = {true, true, true};
      MaterialEntityControlCodec codec;
      char wire[2049];
      size_t length;
      assert(codec.serialize_entity(grant, wire, sizeof(wire), length));
      assert(f.control.receive_entity({wire, length}));
      assert(f.control.resume_control_view(f.control.get_selection_generation()));
      assert(lv_obj_is_visible(f.current));
      lv_style_value_t value{};
      assert(lv_obj_get_local_style_prop(f.buttons[0][0], LV_STYLE_TEXT_OPA, &value, LV_STATE_DISABLED) ==
                 LV_STYLE_RES_FOUND &&
             value.num == LV_OPA_40);
      assert(lv_obj_get_local_style_prop(f.buttons[0][0], LV_STYLE_BG_OPA, &value, LV_STATE_DISABLED) ==
                 LV_STYLE_RES_FOUND &&
             value.num == LV_OPA_30);
      assert(lv_obj_get_local_style_prop(f.buttons[0][0], LV_STYLE_RECOLOR_OPA, &value, LV_STATE_DISABLED) ==
             LV_STYLE_RES_NOT_FOUND);
    } else if (name == "reopen") {
      f.control.invalidate(ControlReason::CONTROL_REASON_SELECTION_CHANGED);
      f.control.loop();
      f.admit();
      assert(lv_obj_is_visible(f.title) && lv_obj_is_visible(f.icons[1]));
      assert(std::string(lv_label_get_text(f.current)) == "Position: 40%");
    } else
      assert(false);
    if (name != "legacy_restore")
      f.check_disabled_palette();
  }
  std::cout << "COVER PANEL " << name << " pass=1\n";
}
