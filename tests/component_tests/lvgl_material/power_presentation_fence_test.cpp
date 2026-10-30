#include "real_lvgl_test_support.h"
#include "material_entity_control_component.h"
#include "material_direct_scalar_control.h"
#include <iostream>
#ifdef NDEBUG
#error "This reproduction requires active assertions"
#endif

using namespace esphome::lvgl_material;
namespace rt = real_lvgl_test;

struct Control : MaterialEntityControlComponent {
  using MaterialEntityControlComponent::MaterialEntityControlComponent;
  bool power_captured() const { return this->captured_target_ == this->on_button_; }
  void view_event(ControlViewEvent event) override {
    rt::record("owner_view_event", this->lvgl_->get_direct_region_base_generation(),
               static_cast<uint32_t>(event), this->get_render_request_id());
    MaterialEntityControlComponent::view_event(event);
  }
};

struct Scalar : MaterialDirectScalarControl {
  using MaterialDirectScalarControl::MaterialDirectScalarControl;
  uint32_t completion() const { return this->completion_sequence_.load(); }
  uint32_t completed_base() const { return this->completed_base_.load(); }
  bool accepted() const { return this->completed_success_.load(); }
  bool active() const { return this->active_.load(); }
  void set_borrowed_for_test(bool value) {
    this->source_in_flight_.store(value);
    this->worker_busy_.store(value);
  }
  void set_setup_ready_for_test(bool value) { this->setup_ready_ = value; }
  void stop_worker_for_test() { this->stop_.store(true); }
  void run_worker_for_test() {
#ifdef USE_ESP32
    this->stop_.store(false);
    this->render_worker_();
#endif
  }
  bool request_ready() const { Request request; return this->read_request_(request); }
  uint32_t request_base() const { Request request; return this->read_request_(request) ? request.base : 0; }
  uint8_t request_value() const { Request request; assert(this->read_request_(request)); return request.value; }
  void run_one() {
#ifdef USE_ESP32
    const auto old = this->completion();
    this->stop_.store(false);
    test_delay_hook = [this]() { this->stop_.store(true); };
    test_notify_hook = [this, old]() {
      if (!this->active_.load() || (this->completion() != old && !this->worker_busy_.load()))
        this->stop_.store(true);
    };
    this->render_worker_();
    test_notify_hook = {};
    test_delay_hook = {};
#endif
  }
};

struct Fixture {
  esphome::lvgl::LvglComponent lvgl;
  rt::HostDisplay display{this->lvgl};
  rt::NativeWidgets widgets{lv_screen_active()};
  rt::Pointer pointer{this->display.get()};
  esphome::lvgl::LvglNavigation navigation;
  esphome::lvgl::LvglApplication application;
  Control control{&this->lvgl};
  Scalar scalar{&this->lvgl, &this->control, &this->navigation, &this->application};
  std::vector<ControlMessage> messages;
  ControlIdentity admitted_identity;
  const char *origin{"setup"};

  Fixture() {
    this->navigation.active = &this->application;
    this->control.set_widgets(this->widgets.root, this->widgets.hit, this->widgets.title,
                             this->widgets.value, this->widgets.unknown, this->widgets.on, this->widgets.off);
    this->control.set_state_label(this->widgets.state);
    this->control.set_status_label(this->widgets.status);
    this->control.set_initial_enabled(true);
    this->control.add_on_message_callback([this](const char *payload) {
      ControlMessage message;
      assert(control_parse_json(payload, message));
      this->messages.push_back(message);
      rt::record("native_message", this->lvgl.generation, static_cast<uint32_t>(message.kind), message.command_seq);
    });
    this->control.add_on_render_request_callback([this](uint32_t id, bool, uint8_t value) {
      rt::record(this->origin, this->lvgl.generation, id, value);
    });
    this->control.setup();
    assert(!this->control.is_failed());
    this->scalar.set_widgets(this->widgets.region, this->widgets.track, this->widgets.bubble,
                            this->widgets.scalar_value, this->widgets.minimum, this->widgets.maximum);
    this->scalar.set_active_color(0xD3E3FD);
    this->scalar.set_initial_enabled(true);
    this->scalar.setup();
#ifdef TEST_NO_ACCELERATION
    assert(this->scalar.is_failed());
#else
    assert(!this->scalar.is_failed());
#endif
    for (auto code : {LV_EVENT_PRESSED, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST}) {
      lv_obj_add_event_cb(this->widgets.on, [](lv_event_t *event) {
        auto *self = static_cast<Fixture *>(lv_event_get_user_data(event));
        rt::record("actual_on_event_after_component", self->lvgl.generation,
                   static_cast<uint32_t>(lv_event_get_code(event)), self->control.power_captured());
      }, code, this);
    }
    rt::advance(200);
    assert(this->control.bind_json(R"({"v":1,"type":"light_brightness_v1","kind":"bind","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})"));
    assert(this->control.open(6, "card-light-6", 3));
    ControlMessage grant;
    grant.kind = ControlKind::CONTROL_KIND_GRANT;
    grant.identity = this->messages.back().identity;
    assert(grant.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
    grant.state.state_seq = 1;
    grant.state.title.assign("Light");
    grant.state.on = true;
    grant.state.brightness_known = true;
    grant.state.brightness_pct = 79;
    grant.state.caps = {true, true, true};
    this->receive(grant);
    this->admitted_identity = grant.identity;
#ifdef TEST_NO_ACCELERATION
    assert(!this->scalar.resume(this->control.get_selection_generation()));
    assert(this->control.owner().is_view_ready() && !this->control.owner().is_scalar_ready());
#else
    assert(this->scalar.resume(this->control.get_selection_generation()));
    this->scalar.run_one();
    this->scalar.loop();
    assert(this->control.owner().is_view_ready() && this->control.owner().is_scalar_ready());
#endif
    assert(!lv_obj_has_state(this->widgets.on, LV_STATE_DISABLED));
    rt::record("ready", this->lvgl.generation, this->control.get_render_request_id());
  }

  void receive(const ControlMessage &message) {
    char wire[2049]; size_t length = 0;
    assert(control_serialize_json(message, wire, sizeof(wire), length));
    assert(this->control.receive({wire, length}));
  }
  void state(bool changed) {
    this->origin = changed ? "render_origin_changed_state" : "render_origin_unchanged_state";
    ControlMessage message;
    message.kind = ControlKind::CONTROL_KIND_STATE;
    message.identity = this->control.owner().identity();
    message.state = this->control.owner().state();
    message.state.state_seq++;
    if (changed) message.state.brightness_pct = 60;
    this->receive(message);
    this->origin = "render_origin_other";
  }
  size_t commands() const {
    size_t result = 0;
    for (const auto &message : this->messages)
      result += message.kind == ControlKind::CONTROL_KIND_COMMAND;
    return result;
  }
  void checkpoint(const char *name) {
    rt::record(name, this->lvgl.generation, this->control.power_captured(), this->commands());
    std::cout << "checkpoint " << name << " captured=" << this->control.power_captured()
              << " selected=" << this->control.owner().has_selection()
              << " view=" << this->control.owner().is_view_ready()
              << " scalar=" << this->control.owner().is_scalar_ready()
              << " request=" << this->control.get_render_request_id()
              << " request_base=" << this->scalar.request_base()
              << " completion_base=" << this->scalar.completed_base()
              << " base=" << this->lvgl.generation << " accepted=" << this->scalar.accepted()
              << " commands=" << this->commands() << '\n';
  }
  ~Fixture() {
    for (auto *object : {this->widgets.root, this->widgets.hit, this->widgets.on, this->widgets.off})
      lv_obj_remove_event_cb_with_user_data(object, nullptr, &this->control);
    lv_obj_delete(this->widgets.root);
  }
};

#ifdef TEST_POWER_FENCE
#include "real_power_negative_cases.h"
#include "real_power_inflight_cases.h"
#endif

int main(int argc, char **argv) {
  assert(argc == 2 || argc == 4);
  if (argc == 2) {
#ifdef TEST_POWER_FENCE
    lv_init();
    {
      Fixture f;
      assert(run_real_power_inflight_case(f, argv[1]) || run_real_power_negative_case(f, argv[1]));
      f.scalar.on_shutdown();
    }
    lv_deinit();
    std::cout << "NEGATIVE " << argv[1] << " PASS\n";
    return 0;
#else
    return 2;
#endif
  }
  const bool changed = std::string(argv[1]) == "changed";
  const bool retire_before_flush = std::string(argv[2]) == "before";
  const bool state_while_held = std::string(argv[3]) == "held";
  lv_init();
  {
    Fixture f;
    if (state_while_held) f.pointer.press(278, 600);
    f.state(changed);
    f.scalar.loop();  // Native state boundary flush, then capture its real base.
    if (!state_while_held) f.pointer.press(278, 600);
    f.checkpoint("held_state_boundary");
    assert(lv_obj_has_state(f.widgets.on, LV_STATE_PRESSED));
    assert(lv_obj_get_style_bg_opa(f.widgets.on, LV_PART_MAIN) == 204);
    auto native_flush = [&]() {
      if (state_while_held) {
        // A real native label refresh after STATE's boundary while contact stays down.
        // This explicitly injected schedule does not claim to identify actual request7's origin.
        lv_label_set_text(f.widgets.title, "Light (native refresh)");
      }
      lv_refr_now(f.display.get());
      f.checkpoint("after_real_native_flush");
    };
    if (f.scalar.request_ready() && f.scalar.active()) {
      if (!retire_before_flush) f.lvgl.on_submit = native_flush;
      f.scalar.run_one();
      f.lvgl.on_submit = {};
      if (retire_before_flush) native_flush();
      f.scalar.loop();
    } else {
      native_flush();  // Fenced candidate intentionally has no regional work while held.
    }
    f.checkpoint("before_release");
    rt::advance(260);
#ifdef TEST_POWER_FENCE
    assert(f.control.power_captured() && f.control.owner().is_view_ready());
    assert(!f.scalar.active());
    assert(lv_obj_has_state(f.widgets.on, LV_STATE_PRESSED));
    assert(lv_obj_get_style_bg_opa(f.widgets.on, LV_PART_MAIN) == 204);
#endif
    f.pointer.release();
    f.scalar.loop();
    if (f.scalar.active() && f.scalar.request_ready()) {
      f.scalar.run_one();
      f.scalar.loop();
    }
    f.checkpoint("after_release");
#ifdef TEST_POWER_FENCE
    assert(f.scalar.request_value() == f.control.owner().candidate());
#endif
    for (const auto &message : f.messages) {
      if (message.kind == ControlKind::CONTROL_KIND_COMMAND) {
        assert(message.identity == f.admitted_identity);
        assert(message.operation == ControlOperation::CONTROL_OPERATION_TURN_ON);
        assert(message.command_seq == 1 && message.value == 0);
      }
    }
    for (const auto &event : rt::trace)
      std::cout << "trace " << event.ordinal << ' ' << event.tick << ' ' << event.tag << ' '
                << event.base << ' ' << event.a << ' ' << event.b << '\n';
    std::cout << "RESULT commands=" << f.commands() << " view=" << f.control.owner().is_view_ready()
              << " scalar=" << f.control.owner().is_scalar_ready() << " pressed="
              << lv_obj_has_state(f.widgets.on, LV_STATE_PRESSED) << '\n';
    std::cout << "HOST_SIZE component=" << sizeof(Control) << " scalar=" << sizeof(Scalar) << '\n';
#if __has_include("material_control_view_lifecycle.h")
    std::cout << "HOST_SIZE view_lifecycle=" << sizeof(ControlViewLifecycle) << '\n';
#endif
    f.scalar.on_shutdown();
  }
  lv_deinit();
}
