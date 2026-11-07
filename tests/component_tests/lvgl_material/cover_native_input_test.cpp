#include "real_lvgl_test_support.h"
#include "material_entity_control_component.h"
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
  std::array<lv_obj_t *, 8> buttons{}, labels{};
  lv_obj_t *root, *apply, *apply_label, *current, *status;
  lv_obj_t *cover_root, *cover_current, *cover_status;
  std::array<lv_obj_t *, 3> cover_buttons{}, cover_labels{};
  std::vector<ControlMessage> messages;
  Fixture() {
    this->navigation.active = &this->application;
    this->root = lv_obj_create(this->widgets.root);
    lv_obj_set_pos(this->root, 0, 0);
    lv_obj_set_size(this->root, 800, 800);
    lv_obj_set_style_pad_all(this->root, 0, 0);
    lv_obj_set_style_border_width(this->root, 0, 0);
    lv_obj_remove_flag(this->root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(this->root, LV_OBJ_FLAG_SCROLLABLE);
    for (size_t index = 0; index < 8; index++) {
      this->buttons[index] = lv_button_create(this->root);
      lv_obj_set_pos(this->buttons[index], 130, 210 + static_cast<int32_t>(index) * 52);
      lv_obj_set_size(this->buttons[index], 540, 48);
      this->labels[index] = lv_label_create(this->buttons[index]);
      lv_obj_remove_flag(this->labels[index], LV_OBJ_FLAG_CLICKABLE);
      lv_label_set_text(this->labels[index], "");
      lv_obj_center(this->labels[index]);
    }
    this->apply = lv_button_create(this->root);
    lv_obj_set_pos(this->apply, 250, 650);
    lv_obj_set_size(this->apply, 300, 64);
    lv_obj_set_style_bg_color(this->apply, lv_color_hex(0xAEE2FF), 0);
    lv_obj_set_style_text_color(this->apply, lv_color_hex(0x071018), 0);
    this->apply_label = lv_label_create(this->apply);
    lv_obj_center(this->apply_label);
    this->current = lv_label_create(this->root);
    lv_obj_set_pos(this->current, 130, 174);
    this->status = lv_label_create(this->root);
    lv_obj_set_pos(this->status, 205, 726);
    this->control.set_widgets(this->widgets.root, this->widgets.hit, this->widgets.title, this->widgets.value,
                              this->widgets.unknown, this->widgets.on, this->widgets.off);
    this->control.set_state_label(this->widgets.state);
    this->control.set_status_label(this->widgets.status);
    this->control.set_number_profile(&this->navigation, &this->application, this->widgets.on_label,
                                     this->widgets.off_label, LV_FONT_DEFAULT);
    this->control.set_choice_profile(this->root, this->apply, this->apply_label, this->current, this->status,
                                     this->widgets.off);
    for (size_t index = 0; index < 8; index++)
      this->control.set_choice_option(index, this->buttons[index], this->labels[index]);
    this->cover_root = lv_obj_create(this->widgets.root);
    lv_obj_set_pos(this->cover_root, 0, 0);
    lv_obj_set_size(this->cover_root, 800, 800);
    lv_obj_set_style_pad_all(this->cover_root, 0, 0);
    lv_obj_set_style_border_width(this->cover_root, 0, 0);
    lv_obj_remove_flag(this->cover_root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(this->cover_root, LV_OBJ_FLAG_SCROLLABLE);
    this->cover_current = lv_label_create(this->cover_root);
    lv_obj_set_pos(this->cover_current, 130, 230);
    this->cover_status = lv_label_create(this->cover_root);
    lv_obj_set_pos(this->cover_status, 130, 654);
    this->control.set_cover_profile(this->cover_root, this->cover_current, this->cover_status);
    for (size_t i = 0; i < 3; i++) {
      this->cover_buttons[i] = lv_button_create(this->cover_root);
      lv_obj_set_pos(this->cover_buttons[i], 168, 320 + 102 * i);
      lv_obj_set_size(this->cover_buttons[i], 464, 80);
      this->cover_labels[i] = lv_label_create(this->cover_buttons[i]);
      lv_obj_center(this->cover_labels[i]);
      lv_obj_remove_flag(this->cover_labels[i], LV_OBJ_FLAG_CLICKABLE);
      this->control.set_cover_button(i, this->cover_buttons[i], this->cover_labels[i]);
    }
#ifdef USE_LVGL_MATERIAL_COVER_PRIORITY_STOP
    this->control.set_cover_priority_stop_enabled(true);
#endif
    this->control.set_initial_enabled(true);
    this->control.add_on_message_callback([this](const char *payload) {
      MaterialEntityControlCodec codec;
      ControlMessage value;
      assert(codec.parse_entity(payload, value));
      this->messages.push_back(value);
    });
    this->control.setup();
    assert(!this->control.is_failed());
    rt::advance(200);
    assert(this->control.bind_entity_json(
        R"({"v":1,"type":"entity_control_v1","kind":"bind","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})"));
#ifdef USE_LVGL_MATERIAL_COVER_PRIORITY_STOP
    ArduinoJson::JsonDocument features;
    auto root = features.to<ArduinoJson::JsonObject>();
    assert(this->control.populate_cover_features(root));
    assert(root["cover_stop_version"].as<int>() == 1);
    assert(this->control.bind_cover_features("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 1, root));
    assert(root["accepted"].as<bool>());
#endif
    this->admit_cover();
  }
  void admit_cover() {
    assert(this->control.open_cover(15, "cover-card", 85));
    ControlMessage value;
    value.generic = true;
    value.kind = ControlKind::CONTROL_KIND_GRANT;
    value.identity = this->control.owner().identity();
    value.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    value.state.state_seq = 1;
    value.state.title.assign("Cover");
    value.state.emplace_family<ControlCoverSnapshot>();
    (*value.state.family_if<ControlCoverSnapshot>()).caps = {true, true, true};
    (*value.state.family_if<ControlCoverSnapshot>()).state_generation = 1;
    (*value.state.family_if<ControlCoverSnapshot>()).available = true;
    (*value.state.family_if<ControlCoverSnapshot>()).position_known = true;
    (*value.state.family_if<ControlCoverSnapshot>()).state = ControlCoverState::CONTROL_COVER_STATE_CLOSED;
    this->receive(value);
    assert(this->control.resume_control_view(this->control.get_selection_generation()));
    rt::advance(200);
    assert(this->control.owner().is_view_ready());
  }
  void cover_state(uint32_t gen, ControlCoverState state, uint8_t position = 0, bool metadata = false) {
    ControlMessage value;
    value.generic = true;
    value.kind = ControlKind::CONTROL_KIND_STATE;
    value.identity = this->control.owner().identity();
    value.state = this->control.owner().state();
    value.state.state_seq++;
    auto &cover = (*value.state.family_if<ControlCoverSnapshot>());
    cover.state_generation = gen;
    cover.state = state;
    cover.position = position;
    cover.available = state != ControlCoverState::CONTROL_COVER_STATE_UNKNOWN;
    cover.position_known = cover.available;
    if (metadata)
      cover.caps.stop = false;
    this->receive(value, !metadata);
  }
  void tap_cover(size_t index) {
    this->pointer.press(400, 360 + 102 * index);
    this->pointer.release();
    rt::advance(40);
  }
  void admit(bool missing_glyph = false) {
    assert(this->control.open_choice(6, "choice-card", 3));
    ControlMessage value;
    value.generic = true;
    value.kind = ControlKind::CONTROL_KIND_GRANT;
    value.identity = this->control.owner().identity();
    assert(value.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
    value.state.state_seq = 1;
    value.state.title.assign("Mode");
    value.state.emplace_family<ControlSnapshot::Choice>();
    auto &choice = (*value.state.family_if<ControlSnapshot::Choice>());
    choice.count = 3;
    choice.available = true;
    choice.state_generation = 1;
    choice.options[0].assign("Eco");
    choice.options[1].assign("Comfort");
    choice.options[2].assign("Boost");
    if (missing_glyph)
      choice.options[1].assign("\xF0\x9F\x8C\xA1");
    this->receive(value);
    assert(this->control.resume_control_view(this->control.get_selection_generation()));
    rt::advance(200);
    assert(this->control.owner().is_view_ready());
  }
  void receive(const ControlMessage &value, bool expected = true) {
    MaterialEntityControlCodec codec;
    char wire[2049];
    size_t length = 0;
    assert(codec.serialize_entity(value, wire, sizeof(wire), length));
    assert(this->control.receive_entity({wire, length}) == expected);
    lv_refr_now(this->display.get());
  }
  void state(uint32_t generation, bool available = true, bool change_list = false, bool missing_glyph = false) {
    ControlMessage value;
    value.generic = true;
    value.kind = ControlKind::CONTROL_KIND_STATE;
    value.identity = this->control.owner().identity();
    value.state = this->control.owner().state();
    value.state.state_seq++;
    (*value.state.family_if<ControlSnapshot::Choice>()).state_generation = generation;
    (*value.state.family_if<ControlSnapshot::Choice>()).available = available;
    (*value.state.family_if<ControlSnapshot::Choice>()).index = 0;
    if (change_list)
      (*value.state.family_if<ControlSnapshot::Choice>()).options[1].assign("Other");
    if (missing_glyph)
      (*value.state.family_if<ControlSnapshot::Choice>()).options[1].assign("\xF0\x9F\x8C\xA1");
    this->receive(value, !change_list && !missing_glyph);
  }
  void tap_option(uint8_t index) {
    this->pointer.press(400, 234 + index * 52);
    this->pointer.release();
    rt::advance(40);
  }
  void press_apply() { this->pointer.press(400, 680); }
  void tap_apply() {
    this->press_apply();
    this->pointer.release();
    rt::advance(40);
  }
  size_t commands() const {
    size_t result = 0;
    for (const auto &value : this->messages)
      result += value.kind == ControlKind::CONTROL_KIND_COMMAND;
    return result;
  }
  ~Fixture() {
    this->control.on_shutdown();
    for (auto *object : {this->widgets.root, this->widgets.hit, this->widgets.on, this->widgets.off, this->apply})
      lv_obj_remove_event_cb_with_user_data(object, nullptr, &this->control);
    for (auto *object : this->buttons)
      lv_obj_remove_event_cb_with_user_data(object, nullptr, &this->control);
    for (auto *object : this->cover_buttons)
      lv_obj_remove_event_cb_with_user_data(object, nullptr, &this->control);
    lv_obj_delete(this->widgets.root);
  }
};

int main(int argc, char **argv) {
  assert(argc == 2);
  lv_init();
  const std::string_view name = argv[1];
  {
    Fixture f;
    assert(!lv_obj_has_flag(f.cover_root, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(f.root, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_state(f.cover_buttons[1], LV_STATE_DISABLED));
    assert(lv_obj_has_state(f.cover_buttons[2], LV_STATE_DISABLED));
    assert(!lv_obj_has_state(f.cover_buttons[0], LV_STATE_DISABLED));
    assert(std::string_view(lv_label_get_text(f.cover_current)) == "Closed  \xC2\xB7  0%");
#ifdef USE_LVGL_MATERIAL_COVER_PRIORITY_STOP
    if (name == "priority_stop" || name == "stop_unknown") {
      f.tap_cover(0);
      assert(f.commands() == 1);
      assert(!lv_obj_has_state(f.cover_buttons[1], LV_STATE_DISABLED));
      f.cover_state(2, ControlCoverState::CONTROL_COVER_STATE_OPENING, 20);
      f.pointer.press(400, 462);
      assert(f.control.captured());
      assert(f.control.owner().is_dragging());
      if (name == "stop_unknown") {
        f.cover_state(3, ControlCoverState::CONTROL_COVER_STATE_UNKNOWN);
        assert(!f.control.captured());
        f.pointer.release();
        assert(f.commands() == 1);
      } else {
        f.cover_state(3, ControlCoverState::CONTROL_COVER_STATE_OPENING, 30);
        assert(f.control.captured());
        f.pointer.release();
        assert(f.commands() == 2);
        assert(f.messages.back().operation == ControlOperation::CONTROL_OPERATION_COVER_STOP);
        assert(f.messages.back().expected_state_generation == 3);
        assert(lv_obj_has_state(f.cover_buttons[1], LV_STATE_DISABLED));
        ControlMessage result;
        result.generic = true;
        result.kind = ControlKind::CONTROL_KIND_RESULT;
        result.identity = f.control.owner().identity();
        result.command_seq = 1;
        result.state.state_seq = 3;
        result.result = ControlResult::CONTROL_RESULT_STATE_MATCHES;
        f.receive(result, false);
        assert(f.control.owner().is_command_pending());
        f.cover_state(4, ControlCoverState::CONTROL_COVER_STATE_OPEN, 30);
        result.command_seq = 2;
        result.state.state_seq = 4;
        f.receive(result);
        assert(!f.control.owner().is_command_pending());
        assert(!lv_obj_has_state(f.cover_buttons[2], LV_STATE_DISABLED));
      }
    } else
#endif
        if (name == "open") {
      f.tap_cover(0);
      assert(f.commands() == 1);
      assert(f.messages.back().operation == ControlOperation::CONTROL_OPERATION_COVER_OPEN);
    } else if (name == "motion") {
      f.cover_state(2, ControlCoverState::CONTROL_COVER_STATE_OPENING, 20);
      assert(lv_obj_has_state(f.cover_buttons[0], LV_STATE_DISABLED));
      assert(!lv_obj_has_state(f.cover_buttons[1], LV_STATE_DISABLED));
      f.tap_cover(1);
      assert(f.commands() == 1);
      assert(f.messages.back().operation == ControlOperation::CONTROL_OPERATION_COVER_STOP);
    } else if (name == "unknown") {
      f.cover_state(2, ControlCoverState::CONTROL_COVER_STATE_UNKNOWN);
      for (auto *button : f.cover_buttons)
        assert(lv_obj_has_state(button, LV_STATE_DISABLED));
      for (size_t i = 0; i < 3; i++)
        f.tap_cover(i);
      assert(f.commands() == 0);
    } else if (name == "reverse") {
      f.cover_state(2, ControlCoverState::CONTROL_COVER_STATE_CLOSING, 100);
      assert(!lv_obj_has_state(f.cover_buttons[0], LV_STATE_DISABLED));
      assert(lv_obj_has_state(f.cover_buttons[2], LV_STATE_DISABLED));
      f.tap_cover(0);
      assert(f.commands() == 1);
    } else if (name == "handoff") {
      f.admit();
      assert(lv_obj_has_flag(f.cover_root, LV_OBJ_FLAG_HIDDEN));
      assert(!lv_obj_has_flag(f.root, LV_OBJ_FLAG_HIDDEN));
      f.admit_cover();
      assert(!lv_obj_has_flag(f.cover_root, LV_OBJ_FLAG_HIDDEN));
      assert(lv_obj_has_flag(f.root, LV_OBJ_FLAG_HIDDEN));
      assert(f.control.open_number(14, "number-card", 85));
      ControlMessage value;
      value.generic = true;
      value.kind = ControlKind::CONTROL_KIND_GRANT;
      value.identity = f.control.owner().identity();
      value.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
      value.state.state_seq = 1;
      value.state.emplace_family<ControlNumberSnapshot>();
      (*value.state.family_if<ControlNumberSnapshot>()).domain.minimum = 0;
      (*value.state.family_if<ControlNumberSnapshot>()).domain.maximum = 10000000;
      (*value.state.family_if<ControlNumberSnapshot>()).domain.step = 1000000;
      (*value.state.family_if<ControlNumberSnapshot>()).domain.max_index = 10;
      (*value.state.family_if<ControlNumberSnapshot>()).state_generation = 1;
      (*value.state.family_if<ControlNumberSnapshot>()).available = true;
      (*value.state.family_if<ControlNumberSnapshot>()).value_text.assign("0");
      f.receive(value);
      assert(f.control.resume_control_view(f.control.get_selection_generation()));
      rt::advance(200);
      assert(lv_obj_has_flag(f.cover_root, LV_OBJ_FLAG_HIDDEN));
      assert(!lv_obj_has_flag(f.widgets.on, LV_OBJ_FLAG_HIDDEN));
      f.admit_cover();
      assert(f.control.open(13, "light-card", 85));
      value = {};
      value.generic = true;
      value.kind = ControlKind::CONTROL_KIND_GRANT;
      value.identity = f.control.owner().identity();
      value.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
      value.state.state_seq = 1;
      value.state.caps = {true, true, true};
      value.state.brightness_known = true;
      value.state.brightness_pct = 50;
      f.receive(value);
      assert(f.control.resume_control_view(f.control.get_selection_generation()));
      rt::advance(200);
      assert(lv_obj_has_flag(f.cover_root, LV_OBJ_FLAG_HIDDEN));
      assert(!lv_obj_has_flag(f.widgets.hit, LV_OBJ_FLAG_HIDDEN));
      f.admit_cover();
      assert(lv_obj_has_flag(f.widgets.hit, LV_OBJ_FLAG_HIDDEN));
    } else {
      f.pointer.press(400, 360);
      assert(f.control.captured());
      assert(f.control.owner().is_dragging());
      if (name == "renewal") {
        f.cover_state(1, ControlCoverState::CONTROL_COVER_STATE_CLOSED);
        assert(f.control.captured());
        f.pointer.release();
        assert(f.commands() == 1);
      } else {
        assert(name == "aba" || name == "metadata");
        f.cover_state(name == "aba" ? 3 : 1, ControlCoverState::CONTROL_COVER_STATE_CLOSED, 0, name == "metadata");
        assert(!f.control.captured());
        assert(!f.control.owner().is_dragging());
        assert(!lv_obj_has_state(f.cover_buttons[0], LV_STATE_PRESSED));
        f.pointer.move(402, 360);
        f.pointer.release();
        assert(f.commands() == 0);
      }
    }
    assert(!f.control.captured());
    std::cout << "COVER " << name << " pass=1\n";
  }
  lv_deinit();
  return 0;
}
