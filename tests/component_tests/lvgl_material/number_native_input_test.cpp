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
  std::vector<ControlMessage> messages;
  Fixture() {
    this->navigation.active = &this->application;
    this->control.set_widgets(this->widgets.root, this->widgets.hit, this->widgets.title, this->widgets.value,
                              this->widgets.unknown, this->widgets.on, this->widgets.off);
    this->control.set_state_label(this->widgets.state);
    this->control.set_status_label(this->widgets.status);
    this->control.set_number_profile(&this->navigation, &this->application, this->widgets.on_label,
                                     this->widgets.off_label, LV_FONT_DEFAULT);
    this->control.set_initial_enabled(true);
    this->control.add_on_message_callback([this](const char *payload) {
      MaterialEntityControlCodec codec;
      ControlMessage message;
      assert(codec.parse_entity(payload, message));
      this->messages.push_back(message);
    });
    this->control.setup();
    assert(!this->control.is_failed());
    rt::advance(200);
    assert(this->control.bind_entity_json(
        R"({"v":1,"type":"entity_control_v1","kind":"bind","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})"));
    this->admit();
  }
  void admit() {
    assert(this->control.open_number(6, "card-number-6", 3));
    ControlMessage grant;
    grant.generic = true;
    grant.kind = ControlKind::CONTROL_KIND_GRANT;
    grant.identity = this->control.owner().identity();
    assert(grant.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
    grant.state.state_seq = 1;
    grant.state.title.assign("Number");
    grant.state.emplace_family<ControlNumberSnapshot>();
    auto &number = (*grant.state.family_if<ControlNumberSnapshot>());
    number.domain.minimum = -1000000;
    number.domain.maximum = 1000000;
    number.domain.step = 250000;
    number.domain.max_index = 8;
    number.domain.unit[0] = 'C';
    number.index = 4;
    number.available = true;
    number.value_text.assign("0");
    number.state_generation = 1;
    this->receive(grant);
    assert(this->control.resume_control_view(this->control.get_selection_generation()));
    assert(this->control.owner().is_view_ready() && !this->control.owner().is_scalar_ready());
    rt::advance(200);
  }
  void receive(const ControlMessage &message, bool expected = true) {
    MaterialEntityControlCodec codec;
    char wire[2049];
    size_t length = 0;
    assert(codec.serialize_entity(message, wire, sizeof(wire), length));
    assert(this->control.receive_entity({wire, length}) == expected);
  }
  void state(uint32_t index, uint32_t generation, bool available = true, bool title_only = false) {
    ControlMessage message;
    message.generic = true;
    message.kind = ControlKind::CONTROL_KIND_STATE;
    message.identity = this->control.owner().identity();
    message.state = this->control.owner().state();
    message.state.state_seq++;
    if (title_only)
      message.state.title.assign("Number renewed title");
    auto &number = (*message.state.family_if<ControlNumberSnapshot>());
    number.index = available ? index : 0;
    number.available = available;
    number.state_generation = generation;
    int64_t value;
    char text[24];
    assert(number.domain.value_at(index, value) && number_format_decimal(value, text, sizeof(text)));
    number.value_text.assign(available ? text : "--");
    this->receive(message);
    lv_refr_now(this->display.get());
  }
  void rejected_state(bool domain) {
    ControlMessage message;
    message.generic = true;
    message.kind = ControlKind::CONTROL_KIND_STATE;
    message.identity = this->control.owner().identity();
    message.state = this->control.owner().state();
    message.state.state_seq++;
    auto &number = (*message.state.family_if<ControlNumberSnapshot>());
    if (domain) {
      number.domain.step = 500000;
      number.domain.max_index = 4;
      number.index = 2;
      number.value_text.assign("0");
      number.state_generation++;
    } else {
      number.index = 5;
      number.value_text.assign("0.25");
    }
    this->receive(message, false);
  }
  void stale_state() {
    ControlMessage message;
    message.generic = true;
    message.kind = ControlKind::CONTROL_KIND_STATE;
    message.identity = this->control.owner().identity();
    message.state = this->control.owner().state();
    this->receive(message, false);
  }
  size_t commands() const {
    size_t count = 0;
    for (const auto &message : this->messages)
      count += message.kind == ControlKind::CONTROL_KIND_COMMAND;
    return count;
  }
  void press(bool plus) { this->pointer.press(plus ? 522 : 278, 600); }
  void tap(bool plus) {
    this->press(plus);
    this->pointer.release();
    this->control.loop();
  }
  void checkpoint(const char *label) {
    std::cout << label << " capture=" << this->control.captured() << " dragging=" << this->control.owner().is_dragging()
              << " commands=" << this->commands()
              << " minus_disabled=" << lv_obj_has_state(this->widgets.on, LV_STATE_DISABLED)
              << " plus_disabled=" << lv_obj_has_state(this->widgets.off, LV_STATE_DISABLED)
              << " minus_pressed=" << lv_obj_has_state(this->widgets.on, LV_STATE_PRESSED)
              << " plus_pressed=" << lv_obj_has_state(this->widgets.off, LV_STATE_PRESSED) << std::endl;
  }
  ~Fixture() {
    this->control.on_shutdown();
    for (auto *object : {this->widgets.root, this->widgets.hit, this->widgets.on, this->widgets.off})
      lv_obj_remove_event_cb_with_user_data(object, nullptr, &this->control);
    lv_obj_delete(this->widgets.root);
  }
};

static void test_unavailable_caption(const std::string &name) {
  Fixture f;
  f.control.set_text_provider([&](uint8_t id) -> const char * {
    if (id != 16)
      return nullptr;
    return name == "caption_localized"      ? "Current localized value unavailable"
           : name == "caption_legacy_empty" ? ""
                                            : nullptr;
  });
  f.tap(true);
  assert(f.commands() == 1 && f.control.owner().is_command_pending());
  if (name != "caption_pending") {
    f.state(5, 2);
    ControlMessage terminal;
    terminal.generic = true;
    terminal.kind = ControlKind::CONTROL_KIND_RESULT;
    terminal.identity = f.control.owner().identity();
    terminal.command_seq = 1;
    terminal.state.state_seq = f.control.owner().state().state_seq;
    terminal.result = name == "caption_rejected" ? ControlResult::CONTROL_RESULT_REJECTED
                                                 : ControlResult::CONTROL_RESULT_STATE_MATCHES;
    terminal.reason =
        name == "caption_rejected" ? ControlReason::CONTROL_REASON_SERVICE_ERROR : ControlReason::CONTROL_REASON_NONE;
    f.receive(terminal);
  }
  const auto result = f.control.owner().last_result();
  f.state(0, 3, false);
  assert(f.control.owner().last_result() == result && f.commands() == 1);
  assert(!lv_obj_has_flag(f.widgets.unknown, LV_OBJ_FLAG_HIDDEN));
  assert(lv_obj_has_state(f.widgets.on, LV_STATE_DISABLED) && lv_obj_has_state(f.widgets.off, LV_STATE_DISABLED));
  const char *expected = name == "caption_pending"     ? "pending"
                         : name == "caption_rejected"  ? "rejected"
                         : name == "caption_localized" ? "Current localized value unavailable"
                                                       : "Current value unavailable";
  assert(std::string(lv_label_get_text(f.widgets.status)) == expected);
  assert(f.control.refresh_texts());
  assert(std::string(lv_label_get_text(f.widgets.status)) == expected);
  f.state(5, 4);
  assert(f.commands() == 1 && f.control.owner().last_result() == result);
  if (name != "caption_pending" && name != "caption_rejected")
    assert(std::string(lv_label_get_text(f.widgets.status)) == "state_matches");
  std::cout << "RESULT " << name << " pass=1" << std::endl;
}

int main(int argc, char **argv) {
  assert(argc == 2);
  const std::string name = argv[1];
  lv_init();
  if (name.starts_with("caption_")) {
    test_unavailable_caption(name);
    lv_deinit();
    return 0;
  }
  bool passed = false;
  {
    Fixture f;
    const bool minimum = name == "minimum_opposite";
    f.press(!minimum);
    assert(f.control.captured() && f.control.owner().is_dragging());
    if (name == "domain_revoked" || name == "same_generation_revoked") {
      f.rejected_state(name == "domain_revoked");
      const bool retired = !f.control.captured() && !lv_obj_has_state(f.widgets.off, LV_STATE_PRESSED);
      f.checkpoint("after_rejected_state");
      assert(!f.control.owner().has_selection() && f.commands() == 0);
      f.admit();
      f.pointer.move(524, 600);
      assert(!f.control.owner().is_dragging() && f.commands() == 0);
      f.pointer.release();
      assert(f.commands() == 0);
      f.tap(true);
      passed = retired && f.commands() == 1 && !f.control.captured();
    } else if (name == "renewal" || name == "title_only" || name == "stale_ignored") {
      if (name == "stale_ignored")
        f.stale_state();
      else
        f.state(4, 1, true, name == "title_only");
      assert(f.control.captured() && f.control.owner().is_dragging());
      f.pointer.release();
      passed = f.commands() == 1;
    } else {
      const bool unknown = name == "unknown_recovered";
      const bool aba = name == "aba_same_index";
      f.state(aba ? 4 : minimum ? 0 : 8, 2, !unknown);
      assert(!f.control.owner().is_dragging());
      f.checkpoint("after_state_held");
      if (name == "reenable_held") {
        f.state(4, 3);
        f.pointer.move(524, 600);  // Still the same physical contact; must not recapture.
      }
      f.pointer.release();
      assert(f.commands() == 0);
      f.checkpoint("after_physical_release");
      if (unknown)
        f.state(4, 3);
      const bool plus = minimum || unknown || aba || name == "reenable_held";
      f.tap(plus);
      f.checkpoint("after_first_fresh_tap");
      passed = f.commands() == 1 && !f.control.captured();
    }
    if (passed) {
      assert(f.commands() == 1);
      const auto &command = f.messages.back();
      assert(command.kind == ControlKind::CONTROL_KIND_COMMAND &&
             command.operation == ControlOperation::CONTROL_OPERATION_STEP);
      assert(command.expected_index == (*f.control.owner().state().family_if<ControlNumberSnapshot>()).index);
      assert(command.expected_state_generation ==
             (*f.control.owner().state().family_if<ControlNumberSnapshot>()).state_generation);
    }
    std::cout << "RESULT " << name << " commands=" << f.commands() << " captured=" << f.control.captured()
              << " pass=" << passed << std::endl;
  }
  lv_deinit();
  return passed ? 0 : 1;
}
