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
  std::vector<ControlMessage> messages;
  Fixture(bool manual = false) {
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
    this->control.set_choice_empty_enabled(manual);
    this->control.set_initial_enabled(true);
    this->control.add_on_message_callback([this](const char *payload) {
      MaterialEntityControlCodec codec;
      ControlMessage value;
      assert(codec.parse_entity(payload, value, this->control.owner().choice_empty_active()));
      this->messages.push_back(value);
    });
    this->control.setup();
    assert(!this->control.is_failed());
    rt::advance(200);
    assert(this->control.bind_entity_json(
        R"({"v":1,"type":"entity_control_v1","kind":"bind","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})"));
    if (manual) {
      ArduinoJson::JsonDocument features;
      auto root = features.to<ArduinoJson::JsonObject>();
      assert(this->control.populate_choice_features(root) && root["empty_choice_version"].as<int>() == 1);
      assert(this->control.bind_choice_features("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 1, root));
    }
    this->admit(false, manual);
  }
  void admit(bool missing_glyph = false, bool manual = false) {
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
    choice.index = manual ? ControlSnapshot::Choice::EMPTY_INDEX : 0;
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
    assert(codec.serialize_entity(value, wire, sizeof(wire), length, this->control.owner().choice_empty_active()));
    assert(this->control.receive_entity({wire, length}) == expected);
    lv_refr_now(this->display.get());
  }
  void state(uint32_t generation, bool available = true, bool change_list = false, bool missing_glyph = false,
             bool manual = false) {
    ControlMessage value;
    value.generic = true;
    value.kind = ControlKind::CONTROL_KIND_STATE;
    value.identity = this->control.owner().identity();
    value.state = this->control.owner().state();
    value.state.state_seq++;
    (*value.state.family_if<ControlSnapshot::Choice>()).state_generation = generation;
    (*value.state.family_if<ControlSnapshot::Choice>()).available = available;
    (*value.state.family_if<ControlSnapshot::Choice>()).index = manual ? ControlSnapshot::Choice::EMPTY_INDEX : 0;
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
    lv_obj_delete(this->widgets.root);
  }
};
int main(int argc, char **argv) {
  assert(argc == 2);
  lv_init();
  const std::string_view name = argv[1];
  if (name == "palette_state_independent") {
    {
      Fixture f;
      const auto references = {f.apply, f.widgets.off};
      for (auto *object : references) {
        lv_obj_set_style_bg_color(object, lv_color_hex(0x3B2011), LV_STATE_DISABLED);
        lv_obj_set_style_text_color(object, lv_color_hex(0x7A5831), LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(object, lv_color_hex(0x512244), LV_STATE_PRESSED | LV_STATE_DISABLED);
        lv_obj_set_style_text_color(object, lv_color_hex(0x986244), LV_STATE_PRESSED | LV_STATE_DISABLED);
        lv_obj_add_state(object, static_cast<lv_state_t>(LV_STATE_DISABLED | LV_STATE_PRESSED));
      }
      const auto check = [&](uint32_t active_bg, uint32_t active_fg, uint32_t neutral_bg, uint32_t neutral_fg) {
        const auto candidate = f.control.owner().choice_candidate();
        for (size_t index = 0; index < 3; index++) {
          assert(lv_color_eq(lv_obj_get_style_bg_color(f.buttons[index], LV_PART_MAIN),
                             lv_color_hex(index == candidate ? active_bg : neutral_bg)));
          assert(lv_color_eq(lv_obj_get_style_text_color(f.labels[index], LV_PART_MAIN),
                             lv_color_hex(index == candidate ? active_fg : neutral_fg)));
        }
        assert(f.commands() == 0);
      };
      const auto palette = [&](uint32_t active_bg, uint32_t active_fg, uint32_t neutral_bg, uint32_t neutral_fg) {
        lv_obj_set_style_bg_color(f.apply, lv_color_hex(active_bg), LV_PART_MAIN);
        lv_obj_set_style_text_color(f.apply, lv_color_hex(active_fg), LV_PART_MAIN);
        lv_obj_set_style_bg_color(f.widgets.off, lv_color_hex(neutral_bg), LV_PART_MAIN);
        lv_obj_set_style_text_color(f.widgets.off, lv_color_hex(neutral_fg), LV_PART_MAIN);
      };
      palette(0xB6E4FA, 0x071018, 0x243139, 0xF5F7FA);
      // Demonstrate that ordinary getters see the poison current-state color.
      assert(lv_color_eq(lv_obj_get_style_bg_color(f.apply, LV_PART_MAIN), lv_color_hex(0x512244)));
      f.state(2);
      check(0xB6E4FA, 0x071018, 0x243139, 0xF5F7FA);
      f.state(2);  // A lease refresh must not reuse Apply's disabled appearance.
      check(0xB6E4FA, 0x071018, 0x243139, 0xF5F7FA);
      for (auto *object : references)
        assert(lv_obj_has_state(object, static_cast<lv_state_t>(LV_STATE_DISABLED | LV_STATE_PRESSED)));
      // Material theme changes the configured base while references stay disabled.
      palette(0xE7C7A9, 0x22180B, 0x372B24, 0xFFF0DE);
      f.state(3);
      check(0xE7C7A9, 0x22180B, 0x372B24, 0xFFF0DE);
      f.tap_option(1);
      assert(f.control.owner().choice_candidate() == 1);
      check(0xE7C7A9, 0x22180B, 0x372B24, 0xFFF0DE);
      std::cout << "CHOICE palette_state_independent pass=1\n";
    }
    lv_deinit();
    return 0;
  }
  if (name == "manual") {
    {
      Fixture f(true);
      assert(std::string_view(lv_label_get_text(f.current)) == "Current: No active option");
      assert(!f.control.owner().choice_candidate_known());
      assert(lv_obj_has_state(f.apply, LV_STATE_DISABLED));
      for (size_t i = 0; i < 3; i++)
        assert(!lv_obj_has_state(f.buttons[i], LV_STATE_DISABLED));
      f.tap_option(0);
      assert(f.commands() == 0);
      assert(!lv_obj_has_state(f.apply, LV_STATE_DISABLED));
      f.press_apply();
      f.state(3, true, false, false, true);
      f.pointer.release();
      assert(f.commands() == 0 && !f.control.owner().choice_candidate_known());
      f.tap_option(2);
      f.tap_apply();
      assert(f.commands() == 1);
      assert(f.messages.back().expected_index == ControlSnapshot::Choice::EMPTY_INDEX &&
             f.messages.back().target_index == 2);
      assert(!f.control.captured());
      std::cout << "CHOICE manual pass=1\n";
    }
    lv_deinit();
    return 0;
  }
  {
    Fixture f;
    assert(lv_obj_has_flag(f.widgets.on, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(f.widgets.hit, LV_OBJ_FLAG_HIDDEN));
    assert(std::string_view(lv_label_get_text(f.current)) == "Current: Eco");
    assert(lv_obj_has_state(f.apply, LV_STATE_DISABLED));
    f.tap_option(1);
    assert(f.commands() == 0 && f.control.owner().choice_candidate() == 1);
    assert(!lv_obj_has_state(f.apply, LV_STATE_DISABLED));
    assert(lv_color_eq(lv_obj_get_style_bg_color(f.buttons[1], LV_PART_MAIN),
                       lv_obj_get_style_bg_color(f.apply, LV_PART_MAIN)));
    if (name == "select_apply") {
      f.tap_option(2);
      assert(f.commands() == 0);
      f.tap_apply();
      assert(f.commands() == 1);
      assert(f.messages.back().target_index == 2 && f.messages.back().expected_index == 0);
    } else if (name == "unknown_status") {
      f.state(2, false);
      assert(f.commands() == 0);
      assert(lv_obj_has_state(f.apply, LV_STATE_DISABLED));
      assert(std::string_view(lv_label_get_text(f.status)) == "Current option unavailable");
    } else if (name == "candidate_after_result") {
      f.tap_apply();
      const auto command = f.messages.back();
      ControlMessage state;
      state.generic = true;
      state.kind = ControlKind::CONTROL_KIND_STATE;
      state.identity = f.control.owner().identity();
      state.state = f.control.owner().state();
      state.state.state_seq++;
      (*state.state.family_if<ControlSnapshot::Choice>()).state_generation++;
      (*state.state.family_if<ControlSnapshot::Choice>()).index = 1;
      f.receive(state);
      ControlMessage result;
      result.generic = true;
      result.kind = ControlKind::CONTROL_KIND_RESULT;
      result.identity = state.identity;
      result.command_seq = command.command_seq;
      result.state.state_seq = state.state.state_seq;
      result.result = ControlResult::CONTROL_RESULT_STATE_MATCHES;
      f.receive(result);
      assert(std::string_view(lv_label_get_text(f.status)) == "state_matches");
      f.tap_option(2);
      assert(f.commands() == 1 && f.control.owner().choice_candidate() == 2);
      assert(std::string_view(lv_label_get_text(f.current)) == "Current: Comfort");
      assert(std::string_view(lv_label_get_text(f.status)) == "ready");
      result.command_seq++;
      f.tap_apply();
      result.result = ControlResult::CONTROL_RESULT_REJECTED;
      result.reason = ControlReason::CONTROL_REASON_SERVICE_ERROR;
      f.receive(result);
      assert(std::string_view(lv_label_get_text(f.status)) == "rejected");
    } else if (name == "handoff") {
      assert(f.control.open_number(6, "number-card", 3));
      assert(lv_obj_has_flag(f.root, LV_OBJ_FLAG_HIDDEN));
      assert(!lv_obj_has_flag(f.widgets.on, LV_OBJ_FLAG_HIDDEN));
      f.admit();
      assert(!lv_obj_has_flag(f.root, LV_OBJ_FLAG_HIDDEN));
      f.tap_option(1);
      f.tap_apply();
      assert(f.commands() == 1);
    } else if (name == "missing_glyph") {
      f.admit(true);
      assert(f.control.owner().has_selection());
      assert(lv_obj_has_state(f.buttons[1], LV_STATE_DISABLED) && lv_obj_has_state(f.apply, LV_STATE_DISABLED));
      f.tap_option(1);
      f.tap_apply();
      assert(f.commands() == 0);
      assert(std::string_view(lv_label_get_text(f.status)) == "Unsupported text");
    } else {
      f.press_apply();
      assert(f.control.captured() && f.control.owner().is_dragging());
      if (name == "list_changed_held") {
        f.state(2, true, true);
        assert(!f.control.owner().has_selection());
        assert(std::string_view(lv_label_get_text(f.status)) == "Options changed - reopen");
        f.pointer.release();
        assert(f.commands() == 0);
        f.admit();
        f.tap_option(1);
        f.tap_apply();
        assert(f.commands() == 1);
      } else if (name == "renewal_held") {
        f.state(1);
        assert(f.control.captured() && f.control.owner().is_dragging());
        f.pointer.release();
        assert(f.commands() == 1);
      } else {
        const bool unknown = name == "unknown_held";
        assert(unknown || name == "aba_held");
        f.state(unknown ? 2 : 3, !unknown);
        assert(!f.control.captured() && !f.control.owner().is_dragging());
        f.state(4);
        f.pointer.move(402, 680);
        assert(f.commands() == 0 && !f.control.captured());
        f.pointer.release();
        assert(f.commands() == 0);
        f.tap_option(1);
        f.tap_apply();
        assert(f.commands() == 1);
      }
    }
    assert(!f.control.captured());
    std::cout << "CHOICE " << name << " pass=1\n";
  }
  lv_deinit();
  return 0;
}
