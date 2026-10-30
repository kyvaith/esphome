#include "component_test_support.h"
#include "material_entity_control_component.h"
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>

using namespace esphome::lvgl_material;
static constexpr const char *BIND =
    R"({"v":1,"type":"light_brightness_v1","kind":"bind","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})";

struct Fixture {
  esphome::lvgl::LvglComponent lvgl;
  lv_obj_t root, hit, title, value, unknown, on, off, state, status;
  lv_indev_t pointer, second;
  MaterialEntityControlComponent component{&lvgl};
  std::array<ControlMessage, 32> messages{};
  size_t message_count{0}, renderer_count{0}, cancels{0}, provider_calls{0};
  uint32_t request_id{0};
  bool register_renderer{true};
  explicit Fixture(bool enabled = true, bool renderer = true) : register_renderer(renderer) {
    test_now = 100;
    this->unknown.text = "--";
    this->component.set_widgets(&this->root, &this->hit, &this->title, &this->value, &this->unknown, &this->on,
                                &this->off);
    this->component.set_state_label(&this->state);
    this->component.set_status_label(&this->status);
    this->component.set_initial_enabled(enabled);
    this->component.add_on_message_callback([this](const char *payload) {
      assert(this->message_count < this->messages.size());
      assert(control_parse_json(payload, this->messages[this->message_count++]));
    });
    if (renderer)
      this->component.add_on_render_request_callback([this](uint32_t id, bool, uint8_t) {
        this->request_id = id;
        this->renderer_count++;
      });
    this->component.add_on_render_cancel_callback([this]() { this->cancels++; });
    this->component.setup();
    assert(!this->component.is_failed());
  }
  size_t commands() const {
    size_t count = 0;
    for (size_t i = 0; i < this->message_count; i++)
      count += this->messages[i].kind == ControlKind::CONTROL_KIND_COMMAND;
    return count;
  }
  void receive(ControlMessage message) {
    char payload[2049];
    size_t length = 0;
    assert(control_serialize_json(message, payload, sizeof(payload), length));
    assert(this->component.receive({payload, length}));
  }
  void grant(bool known = true, const char *token = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb") {
    assert(this->component.bind_json(BIND) != nullptr);
    assert(this->component.open(2, "card-light-2", 7));
    ControlMessage grant;
    grant.kind = ControlKind::CONTROL_KIND_GRANT;
    grant.identity = this->messages[this->message_count - 1].identity;
    assert(grant.identity.token.assign(token));
    grant.state.state_seq = 1;
    grant.state.title.assign("Light");
    grant.state.on = true;
    grant.state.brightness_known = known;
    grant.state.brightness_pct = known ? 50 : 0;
    grant.state.caps = {true, true, true};
    this->receive(grant);
    assert(this->component.owner().has_selection());
    assert(this->component.set_view_ready(this->component.get_selection_generation(), true));
  }
  void ready() { assert(this->request_id && this->component.render_ready(this->request_id, true)); }
  ControlMessage state_message(uint8_t value) const {
    ControlMessage state;
    state.kind = ControlKind::CONTROL_KIND_STATE;
    state.identity = this->component.owner().identity();
    state.state = this->component.owner().state();
    state.state.state_seq++;
    state.state.brightness_pct = value;
    return state;
  }
  void press(int x = 50) {
    this->pointer.point.x = x;
    test_event(this->hit, LV_EVENT_PRESSED, &this->pointer);
  }
  void move(int x) {
    this->pointer.point.x = x;
    test_event(this->hit, LV_EVENT_PRESSING, &this->pointer);
  }
  void release(int x) {
    this->pointer.point.x = x;
    test_event(this->hit, LV_EVENT_RELEASED, &this->pointer);
  }
};

int main(int argc, char **argv) {
  if (argc == 3 && std::string(argv[1]) == "codec") {
    std::ifstream file(argv[2], std::ios::binary);
    const std::string wire{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    MaterialEntityControlCodec codec;
    ControlMessage message;
    message.command_seq = 999;
    if (!codec.parse(wire, message)) {
      assert(message.command_seq == 999);
      return 1;
    }
    char output[2049];
    size_t length = 0;
    assert(codec.serialize(message, output, sizeof(output), length));
    assert(codec.high_water() > 0 && codec.high_water() <= 4096);
    std::cout.write(output, length);
    return 0;
  }
  assert(argc == 2);
  const int scenario = std::stoi(argv[1]);
  if (scenario == 0) {
    Fixture f(false);
    assert(!f.component.owner().is_enabled());
    assert(f.component.bind_json(BIND) == nullptr);
    ControlMessage capability;
    assert(control_parse_json(f.component.capability_json(), capability));
    assert(!capability.enabled);
  } else if (scenario == 1) {
    Fixture f;
    f.grant();
    assert(f.commands() == 0);
    f.press(100);
    f.release(100);
    assert(f.commands() == 0);
    assert(!f.component.render_ready(f.request_id + 1, true));
    f.ready();
    assert(f.component.owner().is_scalar_ready());
  } else if (scenario == 2) {
    Fixture f;
    f.grant();
    f.ready();
    f.press();
    const auto writes = label_writes;
    for (int x = 0; x <= 100; x++)
      f.move(x);
    assert(f.commands() == 0);
    assert(label_writes == writes);
    f.release(100);
    assert(f.commands() == 1);
    assert(f.messages[f.message_count - 1].value == 100);
    f.release(100);
    assert(f.commands() == 1);
  } else if (scenario == 3) {
    Fixture f;
    f.grant();
    f.ready();
    f.press();
    f.second.point.x = 0;
    test_event(f.hit, LV_EVENT_PRESSED, &f.second);
    test_event(f.hit, LV_EVENT_RELEASED, &f.second);
    assert(f.commands() == 0);
    f.release(75);
    assert(f.commands() == 1 && f.messages[f.message_count - 1].value == 75);
  } else if (scenario == 4) {
    Fixture f;
    f.grant();
    f.ready();
    test_event(f.hit, LV_EVENT_CLICKED, &f.pointer);
    test_event(f.on, LV_EVENT_RELEASED, &f.pointer);
    test_event(f.on, LV_EVENT_CLICKED, &f.pointer);
    assert(f.commands() == 0);
  } else if (scenario == 5) {
    Fixture f;
    f.grant();
    f.ready();
    f.press();
    f.move(100);
    test_event(f.hit, LV_EVENT_PRESS_LOST, &f.pointer);
    f.release(100);
    assert(f.commands() == 0);
    assert(f.component.owner().candidate() == 50);
  } else if (scenario == 6) {
    Fixture f;
    f.grant();
    f.ready();
    f.press();
    f.move(100);
    const auto old = f.request_id;
    f.component.invalidate(ControlReason::CONTROL_REASON_SELECTION_CHANGED);
    f.release(100);
    assert(!f.component.render_ready(old, true));
    assert(f.commands() == 0 && !f.component.owner().has_selection());
  } else if (scenario == 7) {
    Fixture f;
    f.grant();
    f.ready();
    f.press();
    f.move(100);
    auto state = f.state_message(75);
    f.receive(state);
    assert(f.value.text == "50%");
    assert(f.component.owner().candidate() == 100);
    test_event(f.hit, LV_EVENT_PRESS_LOST, &f.pointer);
    assert(f.value.text == "75%");
    assert(f.commands() == 0);
  } else if (scenario == 8) {
    Fixture f;
    f.grant();
    assert(f.component.render_ready(f.request_id, false));
    f.press(100);
    f.release(100);
    assert(f.commands() == 0);
    test_event(f.on, LV_EVENT_PRESSED, &f.pointer);
    test_event(f.on, LV_EVENT_RELEASED, &f.pointer);
    assert(f.commands() == 1 &&
           f.messages[f.message_count - 1].operation == ControlOperation::CONTROL_OPERATION_TURN_ON);
  } else if (scenario == 9) {
    Fixture f;
    f.grant();
    assert(!f.component.receive(BIND));
    const auto old = f.component.owner().identity();
    assert(!f.component.receive("{\"v\":true}"));
    assert(old == f.component.owner().identity());
    assert(!f.component.open(258, "card-light-2", 7));
    assert(!f.component.open(2, "bad id", 7));
  } else if (scenario == 10) {
    Fixture f;
    f.grant();
    f.ready();
    auto state = f.state_message(50);
    state.state.caps = {false, true, true};
    state.state.brightness_known = false;
    state.state.brightness_pct = 0;
    char payload[2049];
    size_t length;
    assert(control_serialize_json(state, payload, sizeof(payload), length));
    assert(!f.component.receive({payload, length}));
    assert(!f.component.owner().has_selection());
    assert(f.commands() == 0);
  } else if (scenario == 11) {
    Fixture f;
    f.component.set_text_provider([&](uint8_t) {
      f.provider_calls++;
      return "Translated";
    });
    f.grant();
    f.ready();
    f.press();
    const auto calls = f.provider_calls;
    f.move(75);
    f.component.refresh_texts();
    assert(f.provider_calls == calls);
    f.release(75);
    assert(f.provider_calls > calls);
  } else if (scenario == 12) {
    Fixture f;
    f.grant();
    f.ready();
    f.press();
    f.move(100);
    lv_obj_add_flag(&f.root, LV_OBJ_FLAG_HIDDEN);
    f.component.loop();
    f.release(100);
    assert(f.commands() == 0 && !f.component.owner().has_selection());
  } else if (scenario == 13) {
    Fixture f;
    f.grant();
    f.ready();
    f.lvgl.paused = true;
    f.component.loop();
    assert(f.component.owner().has_selection());
    f.press(100);
    f.release(100);
    assert(f.commands() == 0);
    f.lvgl.paused = false;
    f.press(100);
    f.release(100);
    assert(f.commands() == 1);
  } else if (scenario == 14) {
    Fixture f;
    f.grant();
    f.ready();
    test_now += 30000;
    f.component.loop();
    assert(!f.component.owner().has_selection());
    assert(f.component.get_status() == ControlUiStatus::CONTROL_UI_STATUS_EXPIRED);
  } else if (scenario == 15) {
    Fixture f;
    bool refused = false;
    f.component.add_on_message_callback([&](const char *) { refused = !f.component.open(3, "other", 8); });
    f.grant();
    assert(refused);
    assert(f.component.owner().identity().context.slot == 2);
  } else if (scenario == 16) {
    Fixture f;
    f.grant(false);
    assert(f.unknown.text == "--");
    assert(!lv_obj_has_flag(&f.unknown, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(&f.value, LV_OBJ_FLAG_HIDDEN));
    assert(f.value.text.empty());
  } else if (scenario == 17) {
    Fixture f;
    f.grant();
    const auto old = f.request_id;
    f.receive(f.state_message(75));
    assert(f.request_id > old);
    assert(!f.component.render_ready(old, true));
    f.ready();
    assert(f.component.owner().candidate() == 75);
  } else if (scenario == 18) {
    Fixture f(true, false);
    f.grant();
    assert(!f.component.owner().is_scalar_ready());
    assert(f.component.get_status() == ControlUiStatus::CONTROL_UI_STATUS_RENDERER_UNAVAILABLE);
    test_event(f.off, LV_EVENT_PRESSED, &f.pointer);
    test_event(f.off, LV_EVENT_RELEASED, &f.pointer);
    assert(f.commands() == 1);
  } else if (scenario == 19) {
    Fixture f;
    f.grant();
    f.ready();
    f.press();
    f.release(50);
    assert(f.commands() == 0);
    test_event(f.hit, LV_EVENT_PRESSED, nullptr);
    test_event(f.hit, LV_EVENT_RELEASED, nullptr);
    assert(f.commands() == 0);
  } else if (scenario == 20) {
    Fixture f;
    f.grant(false);
    f.ready();
    f.press(75);
    f.release(75);
    assert(f.commands() == 0 && !f.component.owner().candidate_known());
    f.press(75);
    f.move(100);
    f.release(100);
    assert(f.commands() == 1);
    assert(f.messages[f.message_count - 1].value == 100);
  } else if (scenario == 21) {
    Fixture f;
    ArduinoJson::JsonDocument document;
    auto root = document.to<ArduinoJson::JsonObject>();
    assert(f.component.populate_status(root));
    std::string payload;
    ArduinoJson::serializeJson(root, payload);
    ControlMessage response;
    assert(control_parse_json(payload, response));
    assert(response.kind == ControlKind::CONTROL_KIND_CAPABILITY && response.enabled);
    assert(f.component.prepare_bind_response(BIND));
    assert(f.component.populate_bind_response(root));
    payload.clear();
    ArduinoJson::serializeJson(root, payload);
    assert(control_parse_json(payload, response));
    assert(response.kind == ControlKind::CONTROL_KIND_BIND);
    assert(!f.component.populate_bind_response(root));
    assert(!f.component.prepare_bind_response("{}"));
    assert(!f.component.populate_bind_response(root));
  } else if (scenario == 22) {
    Fixture f(false);
    MaterialEntityControlFlagAction<> enable(&f.component, true);
    enable.set_value(true);
    enable.play();
    MaterialEntityControlPayloadAction<> bind(&f.component, true);
    bind.set_payload(BIND);
    bind.play();
    MaterialEntityControlOpenAction<> open(&f.component);
    open.set_slot(2);
    open.set_card_id("card-light-2");
    open.set_revision(7);
    open.play();
    assert(f.component.owner().is_open_pending());
    MaterialEntityControlPayloadAction<> receive(&f.component, false);
    receive.set_payload("{}");
    receive.play();
    assert(f.component.owner().is_open_pending());
    MaterialEntityControlFlagAction<> view(&f.component, false);
    view.set_generation(1);
    view.set_value(false);
    view.play();
    MaterialEntityControlRenderReadyAction<> ready(&f.component);
    ready.set_request_id(1);
    ready.set_value(true);
    ready.play();
    MaterialEntityControlRefreshTextsAction<> texts(&f.component);
    texts.play();
    MaterialEntityControlInvalidateAction<> cancel(&f.component, ControlReason::CONTROL_REASON_VOICE);
    cancel.play();
    assert(!f.component.owner().is_open_pending());
    MaterialEntityControlDisconnectAction<> disconnect(&f.component);
    disconnect.play();
    assert(!f.component.owner().is_bound());
    assert(f.commands() == 0);
  } else if (scenario == 23) {
    Fixture f;
    uint32_t granted = 0, closed = 0;
    f.component.add_on_granted_callback([&](uint32_t generation) { granted = generation; });
    f.component.add_on_closed_callback([&](uint32_t generation) { closed = generation; });
    f.grant();
    const auto old = granted;
    assert(old != 0 && f.component.is_selection_current(old));
    f.ready();
    f.component.invalidate(ControlReason::CONTROL_REASON_NAVIGATION);
    assert(closed == old && !f.component.is_selection_current(old));
    assert(!f.component.set_view_ready(old, true));
    f.grant();
    assert(granted != old && f.component.is_selection_current(granted));
    f.ready();
    assert(!f.component.set_view_ready(old, true));
    assert(!f.component.set_view_ready(old, false));
    assert(f.component.owner().is_scalar_ready());
    assert(f.component.set_view_ready(granted, false));
    assert(!f.component.owner().is_scalar_ready());
  } else if (scenario == 24) {
    for (const auto result :
         {ControlResult::CONTROL_RESULT_STATE_MATCHES, ControlResult::CONTROL_RESULT_REJECTED,
          ControlResult::CONTROL_RESULT_ALREADY_MATCHED, ControlResult::CONTROL_RESULT_NOT_CONFIRMED}) {
      Fixture f;
      f.grant();
      f.ready();
      test_event(f.on, LV_EVENT_PRESSED, &f.pointer);
      test_event(f.on, LV_EVENT_RELEASED, &f.pointer);
      assert(f.commands() == 1 && f.component.owner().is_command_pending());
      ControlMessage terminal;
      terminal.kind = ControlKind::CONTROL_KIND_RESULT;
      terminal.identity = f.component.owner().identity();
      terminal.command_seq = 1;
      terminal.state.state_seq = f.component.owner().state().state_seq;
      terminal.result = result;
      terminal.reason = result == ControlResult::CONTROL_RESULT_REJECTED ? ControlReason::CONTROL_REASON_SERVICE_ERROR
                        : result == ControlResult::CONTROL_RESULT_NOT_CONFIRMED
                            ? ControlReason::CONTROL_REASON_STATE_TIMEOUT
                            : ControlReason::CONTROL_REASON_NONE;
      f.receive(terminal);
      const auto terminal_status = f.status.text;
      assert(f.component.owner().last_result() == result && !f.component.owner().is_command_pending());
      f.receive(f.state_message(50));
      f.ready();
      assert(f.component.owner().last_result() == result && f.status.text == terminal_status);
      assert(f.component.invalidate(ControlReason::CONTROL_REASON_NAVIGATION));
      assert(f.component.owner().last_result() == result);
      assert(f.component.owner().last_reason() == ControlReason::CONTROL_REASON_NAVIGATION);
      f.grant(true, "cccccccccccccccccccccccccccccccc");
      f.ready();
      assert(f.commands() == 1 && f.component.owner().command_sequence() == 0);
      assert(f.component.owner().last_result() == ControlResult::CONTROL_RESULT_PENDING);
      assert(f.component.owner().last_reason() == ControlReason::CONTROL_REASON_NONE);
      assert(f.component.get_status() == ControlUiStatus::CONTROL_UI_STATUS_READY && f.status.text == "ready");
      assert(!(terminal.identity == f.component.owner().identity()));
      test_event(f.on, LV_EVENT_PRESSED, &f.pointer);
      test_event(f.on, LV_EVENT_RELEASED, &f.pointer);
      assert(f.commands() == 2 && f.component.owner().command_sequence() == 1);
      char wire[2049];
      size_t length = 0;
      assert(control_serialize_json(terminal, wire, sizeof(wire), length));
      assert(!f.component.receive({wire, length}));
      assert(f.component.owner().is_command_pending() && f.status.text == "pending");
      terminal.identity = f.component.owner().identity();
      terminal.state.state_seq = f.component.owner().state().state_seq;
      f.receive(terminal);
      assert(f.commands() == 2 && f.component.owner().last_result() == result && f.status.text == terminal_status);
    }
  } else {
    return 2;
  }
  std::cout << "component case " << scenario << " PASS\n";
}
