#include "number_test_support.h"
#include "material_entity_control_component.h"
#ifdef TEST_WITH_SCALAR
#include "material_direct_scalar_control.h"
using namespace esphome::lvgl_material;
struct Scalar : MaterialDirectScalarControl {
  using MaterialDirectScalarControl::MaterialDirectScalarControl;
  void run() {
    this->stop_.store(false);
    const auto hook = test_notify_hook;
    test_notify_hook = [this, hook]() {
      if (hook)
        hook();
      // Model a real worker blocked after a current failed completion. Production
      // stays alive until the owner handles that completion or wakes new work.
      if (!this->active_.load() ||
          (!this->worker_busy_.load() && this->completion_sequence_.load() != 0 && !this->completed_success_.load() &&
           this->completed_id_.load() == this->requested_id_.load()))
        this->stop_.store(true);
    };
    this->render_worker_();
    test_notify_hook = hook;
  }
  void late_cancel() { this->cancel_(); }
  void stop() { this->stop_.store(true); }
  uint32_t completion_sequence() const { return this->completion_sequence_.load(std::memory_order_acquire); }
  uint32_t completion_base() const { return this->completed_base_.load(std::memory_order_acquire); }
  bool completion_accepted() const { return this->completed_success_.load(std::memory_order_acquire); }
  // Worker-only motion seam; actual Component pointer dispatch is tested below.
  void request(uint32_t id, bool known, uint8_t value) {
    this->publish_request_(id, known, value, false, this->lvgl_->get_direct_region_base_generation());
  }
  bool request_ready() const {
    Request request;
    return this->read_request_(request);
  }
  void own_region() { this->region_owned_.store(true); }
  void set_borrowed(bool value) {
    this->source_in_flight_.store(value);
    this->worker_busy_.store(value);
  }
  bool alias() const { return this->alias_visible_; }
  bool owns_pause() const { return this->pause_owned_; }
  uint8_t *canvas() const { return this->canvas_; }
  const ScalarRasterStyle &style() const { return this->style_; }
  const uint8_t *masks() const { return this->glyph_masks_; }
};

#endif

#include <iostream>

using namespace esphome::lvgl_material;

struct GenericFixture {
  esphome::lvgl::LvglComponent lvgl;
  esphome::lvgl::LvglNavigation navigation;
  esphome::lvgl::LvglApplication application;
  lv_obj_t root, hit, title, value, unknown, on, off, state, status, minus, plus;
  lv_indev_t pointer;
  lv_font_t number_font, light_font, unknown_font;
  MaterialGenericEntityControlComponent component{&this->lvgl};
  std::array<ControlMessage, 32> messages{};
  size_t count{0}, renders{0};
  explicit GenericFixture(bool enabled = true) {
    test_now = 100;
    test_missing_glyph = false;
    test_notify_hook = {};
    test_delay_hook = {};
    test_refresh_hook = {};
    this->navigation.active = &this->application;
    this->hit.bounds = {118, 448, 681, 559};
    this->value.font = &this->light_font;
    this->unknown.font = &this->unknown_font;
    this->unknown.text = "--";
    this->component.set_widgets(&this->root, &this->hit, &this->title, &this->value, &this->unknown, &this->on,
                                &this->off);
    this->component.set_state_label(&this->state);
    this->component.set_status_label(&this->status);
    this->component.set_number_profile(&this->navigation, &this->application, &this->minus, &this->plus,
                                       &this->number_font);
    this->component.set_initial_enabled(enabled);
    this->component.add_on_message_callback([this](const char *wire) {
      MaterialEntityControlCodec codec;
      assert(this->count < this->messages.size());
      assert(codec.parse_entity(wire, this->messages[this->count]) || codec.parse(wire, this->messages[this->count]));
      this->count++;
    });
    this->component.add_on_render_request_callback([this](uint32_t, bool, uint8_t) { this->renders++; });
    this->component.setup();
    assert(!this->component.is_failed());
    assert(this->navigation.guard == &this->component);
  }
  size_t commands() const {
    size_t result = 0;
    for (size_t i = 0; i < this->count; i++)
      result += this->messages[i].kind == ControlKind::CONTROL_KIND_COMMAND;
    return result;
  }
  void bind(bool generic = true) {
    ControlMessage message;
    message.kind = ControlKind::CONTROL_KIND_BIND;
    message.generic = generic;
    message.identity.channel.assign("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    char wire[2049];
    size_t size = 0;
    MaterialEntityControlCodec codec;
    assert(generic ? codec.serialize_entity(message, wire, sizeof(wire), size)
                   : codec.serialize(message, wire, sizeof(wire), size));
    assert(generic ? this->component.bind_entity_json({wire, size}) != nullptr
                   : this->component.bind_json({wire, size}) != nullptr);
  }
  bool receive(const ControlMessage &message) {
    char wire[2049];
    size_t size = 0;
    MaterialEntityControlCodec codec;
    assert(message.generic ? codec.serialize_entity(message, wire, sizeof(wire), size)
                           : codec.serialize(message, wire, sizeof(wire), size));
    return message.generic ? this->component.receive_entity({wire, size}) : this->component.receive({wire, size});
  }
  ControlMessage number_message(ControlKind kind = ControlKind::CONTROL_KIND_GRANT,
                                const char *token = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb") {
    ControlMessage message;
    message.generic = true;
    message.kind = kind;
    message.identity = this->component.owner().identity();
    message.identity.token.assign(token);
    message.state.state_seq = 1;
    message.state.title.assign("Number");
    message.state.emplace_family<ControlNumberSnapshot>();
    auto &number = (*message.state.family_if<ControlNumberSnapshot>());
    number.domain.minimum = -1000000;
    number.domain.maximum = 1000000;
    number.domain.step = 250000;
    number.domain.max_index = 8;
    number.domain.unit[0] = 'C';
    number.available = true;
    number.index = 4;
    number.value_text.assign("0");
    number.state_generation = 1;
    return message;
  }
  void open_number(const char *token = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb") {
    assert(this->component.open_number(2, "card-2", 4));
    assert(this->receive(this->number_message(ControlKind::CONTROL_KIND_GRANT, token)));
    assert(this->component.resume_control_view(this->component.get_selection_generation()));
  }
  void open_light(bool generic = true, bool brightness = true, const char *token = "cccccccccccccccccccccccccccccccc") {
    assert(this->component.open(2, "card-2", 4));
    ControlMessage grant;
    grant.generic = generic;
    grant.kind = ControlKind::CONTROL_KIND_GRANT;
    grant.identity = this->component.owner().identity();
    grant.identity.token.assign(token);
    grant.state.state_seq = 1;
    grant.state.title.assign("Light");
    grant.state.on = true;
    grant.state.caps = {brightness, true, true};
    grant.state.brightness_known = brightness;
    grant.state.brightness_pct = brightness ? 50 : 0;
    assert(this->receive(grant));
    assert(this->component.resume_control_view(this->component.get_selection_generation()));
  }
};

#ifdef TEST_WITH_SCALAR
struct MixedFixture {
  GenericFixture entity;
  lv_obj_t track, bubble, value, minimum, maximum;
  Scalar scalar{&this->entity.lvgl, &this->entity.component, &this->entity.navigation, &this->entity.application};
  explicit MixedFixture(bool enabled = true) : entity(enabled) {
    this->track.bounds = {142, 512, 657, 535};
    this->bubble.bounds = {344, 452, 455, 495};
    this->value.bounds = {356, 460, 443, 487};
    this->minimum.bounds = {142, 540, 237, 558};
    this->minimum.text = "0%";
    this->maximum.bounds = {562, 540, 657, 558};
    this->maximum.text = "100%";
    this->scalar.set_widgets(&this->entity.hit, &this->track, &this->bubble, &this->value, &this->minimum,
                             &this->maximum);
    this->scalar.set_active_color(0x3498FF);
    this->scalar.set_initial_enabled(enabled);
    this->scalar.setup();
    assert(!this->scalar.is_failed());
    assert(this->entity.navigation.guard == &this->entity.component);
  }
  void render_once() {
    test_delay_hook = [this]() { this->scalar.stop(); };
    this->scalar.run();
    test_delay_hook = {};
    this->scalar.loop();
  }
};
#endif

static void run_decimal_locale(int scenario) {
  GenericFixture f;
  const char *separator = ",";
  f.component.set_text_provider(
      [&](uint8_t id) -> const char * { return f.component.owner().is_number() && id == 15 ? separator : nullptr; });
  f.bind();
  if (scenario == 15) {
    assert(f.component.open_number(2, "card-2", 4));
    auto grant = f.number_message();
    auto &number = (*grant.state.family_if<ControlNumberSnapshot>());
    number.domain.minimum = 999999999999000000LL;
    number.domain.maximum = 1000000000000000000LL;
    number.domain.step = 1;
    number.domain.max_index = 1000000;
    number.index = 999999;
    number.value_text.assign("999999999999.999999");
    assert(f.receive(grant));
    assert(f.component.resume_control_view(f.component.get_selection_generation()));
    assert(f.value.text == "999999999999,999999");
    assert((*f.component.owner().state().family_if<ControlNumberSnapshot>()).value_text.view() ==
           "999999999999.999999");
    assert(f.commands() == 0 && f.renders == 0);
    return;
  }
  if (scenario == 13)
    test_missing_glyph_point = ',';
  f.open_number();
  auto publish = [&](uint32_t index, const char *canonical) {
    auto update = f.number_message(ControlKind::CONTROL_KIND_STATE);
    update.state = f.component.owner().state();
    update.state.state_seq++;
    (*update.state.family_if<ControlNumberSnapshot>()).index = index;
    (*update.state.family_if<ControlNumberSnapshot>()).state_generation++;
    assert((*update.state.family_if<ControlNumberSnapshot>()).value_text.assign(canonical));
    assert(f.receive(update));
  };
  publish(5, "0.25");
  const auto snapshot = f.component.owner().state();
  const auto identity = f.component.owner().identity();
  const auto generation = f.component.get_selection_generation();
  const auto messages = f.count;
  if (scenario == 11) {
    assert(f.value.text == "0,25" && f.state.text == "C");
    struct Sample {
      uint32_t index;
      const char *canonical;
      const char *displayed;
    };
    const Sample samples[] = {{3, "-0.25", "-0,25"}, {4, "0", "0"}, {8, "1", "1"}, {0, "-1", "-1"}};
    for (const auto &sample : samples) {
      publish(sample.index, sample.canonical);
      assert(f.value.text == sample.displayed);
      assert((*f.component.owner().state().family_if<ControlNumberSnapshot>()).value_text.view() == sample.canonical);
    }
    separator = ".";
    publish(5, "0.25");
    assert(f.value.text == "0.25");
  } else if (scenario == 12) {
    for (const char *invalid : std::array<const char *, 8>{nullptr, "", ".", "comma", ",,", "\n", "\xff",
                                                           "number_control.decimal_separator"}) {
      separator = invalid;
      assert(f.component.refresh_texts());
      assert(f.value.text == "0.25");
    }
    separator = ",";
    assert(f.component.refresh_texts() && f.value.text == "0,25");
  } else if (scenario == 13) {
    assert(f.value.text == "0.25" && f.state.text == "C");
    assert(f.component.owner().is_view_ready() && !(f.on.state & LV_STATE_DISABLED) &&
           !(f.off.state & LV_STATE_DISABLED));
    test_missing_glyph_point = 0;
    test_placeholder_glyph_point = ',';
    assert(f.component.refresh_texts() && f.value.text == "0.25");
    test_placeholder_glyph_point = 0;
    assert(f.component.refresh_texts() && f.value.text == "0,25");
  } else if (scenario == 14) {
    assert(f.value.text == "0,25");
    separator = ".";
    assert(f.component.refresh_texts() && f.value.text == "0.25");
    separator = ",";
    assert(f.component.refresh_texts() && f.value.text == "0,25");
    const auto writes = label_writes;
    assert(f.component.refresh_texts() && label_writes == writes);
    assert(f.component.owner().state() == snapshot && f.component.owner().identity() == identity);
    assert(f.component.get_selection_generation() == generation && f.count == messages);
    auto unknown = f.number_message(ControlKind::CONTROL_KIND_STATE);
    unknown.state = f.component.owner().state();
    unknown.state.state_seq++;
    (*unknown.state.family_if<ControlNumberSnapshot>()).state_generation++;
    (*unknown.state.family_if<ControlNumberSnapshot>()).available = false;
    (*unknown.state.family_if<ControlNumberSnapshot>()).index = 0;
    (*unknown.state.family_if<ControlNumberSnapshot>()).value_text.assign("--");
    assert(f.receive(unknown) && f.component.refresh_texts());
    assert(f.value.text == "--" && !lv_obj_has_flag(&f.unknown, LV_OBJ_FLAG_HIDDEN));
    assert((f.on.state & LV_STATE_DISABLED) && (f.off.state & LV_STATE_DISABLED));
    f.open_light(true, false);
    assert(f.value.font == &f.light_font && f.unknown.font == &f.unknown_font);
    assert(f.minus.text == "On" && f.plus.text == "Off" && !lv_obj_has_flag(&f.hit, LV_OBJ_FLAG_HIDDEN));
    assert(f.commands() == 0);
    return;
  } else {
    assert(false);
  }
  auto wire_state = f.number_message(ControlKind::CONTROL_KIND_STATE);
  wire_state.state = f.component.owner().state();
  char wire[2049];
  size_t size = 0;
  MaterialEntityControlCodec codec;
  assert(codec.serialize_entity(wire_state, wire, sizeof(wire), size));
  assert(std::string_view(wire, size).find("\"value_text\":\"0.25\"") != std::string_view::npos);
  assert(std::string_view(wire, size).find("0,25") == std::string_view::npos);
  assert(f.commands() == 0 && f.renders == 0);
}

static void run(int scenario) {
  if (scenario >= 11 && scenario <= 15) {
    run_decimal_locale(scenario);
    return;
  }
  if (scenario == 10) {
    for (const bool first_number : {false, true}) {
      for (const bool next_number : {false, true}) {
        for (const auto result :
             {ControlResult::CONTROL_RESULT_STATE_MATCHES, ControlResult::CONTROL_RESULT_REJECTED}) {
          GenericFixture f;
          f.bind();
          if (first_number)
            f.open_number();
          else
            f.open_light(true, false);
          test_event(f.off, LV_EVENT_PRESSED, &f.pointer);
          test_event(f.off, LV_EVENT_RELEASED, &f.pointer);
          assert(f.commands() == 1 && f.component.owner().is_command_pending());
          auto state = f.number_message(ControlKind::CONTROL_KIND_STATE);
          state.identity = f.component.owner().identity();
          state.state = f.component.owner().state();
          state.state.state_seq++;
          if (result == ControlResult::CONTROL_RESULT_STATE_MATCHES) {
            if (first_number) {
              (*state.state.family_if<ControlNumberSnapshot>()).index = 5;
              (*state.state.family_if<ControlNumberSnapshot>()).state_generation++;
              (*state.state.family_if<ControlNumberSnapshot>()).value_text.assign("0.25");
            } else
              state.state.on = false;
          }
          assert(f.receive(state));
          ControlMessage terminal;
          terminal.generic = true;
          terminal.kind = ControlKind::CONTROL_KIND_RESULT;
          terminal.identity = f.component.owner().identity();
          terminal.command_seq = 1;
          terminal.state.state_seq = state.state.state_seq;
          terminal.result = result;
          terminal.reason = result == ControlResult::CONTROL_RESULT_REJECTED
                                ? ControlReason::CONTROL_REASON_SERVICE_ERROR
                                : ControlReason::CONTROL_REASON_NONE;
          assert(f.receive(terminal));
          const auto terminal_status = f.status.text;
          state.state.state_seq++;
          assert(f.receive(state));
          assert(f.component.owner().last_result() == result && f.status.text == terminal_status);
          const auto identity = f.component.owner().identity();
          assert(f.component.prepare_navigation(esphome::lvgl::NavigationAdmissionKind::CLOSE, &f.application));
          f.application.call_on_prepare_close_callbacks();
          f.application.call_on_close_cancelled_callbacks();
          f.component.loop();
          assert(f.component.owner().identity() == identity && f.component.owner().is_view_ready());
          assert(f.component.owner().last_result() == result && f.status.text == terminal_status && f.commands() == 1);
          assert(f.component.invalidate(ControlReason::CONTROL_REASON_NAVIGATION));
          assert(f.component.owner().last_result() == result);
          assert(f.component.owner().last_reason() == ControlReason::CONTROL_REASON_NAVIGATION);
          if (next_number)
            f.open_number("dddddddddddddddddddddddddddddddd");
          else
            f.open_light(true, false, "dddddddddddddddddddddddddddddddd");
          assert(f.component.owner().command_sequence() == 0 && !f.component.owner().is_command_pending());
          assert(f.component.owner().last_result() == ControlResult::CONTROL_RESULT_PENDING);
          assert(f.component.owner().last_reason() == ControlReason::CONTROL_REASON_NONE);
          assert(f.component.get_status() == ControlUiStatus::CONTROL_UI_STATUS_READY && f.status.text == "ready");
          assert(!(terminal.identity == f.component.owner().identity()) && f.commands() == 1);
          test_event(f.off, LV_EVENT_PRESSED, &f.pointer);
          test_event(f.off, LV_EVENT_RELEASED, &f.pointer);
          assert(f.commands() == 2 && !f.receive(terminal));
          assert(f.component.owner().is_command_pending() && f.status.text == "pending");
          state.identity = f.component.owner().identity();
          state.state = f.component.owner().state();
          state.state.state_seq++;
          if (result == ControlResult::CONTROL_RESULT_STATE_MATCHES) {
            if (next_number) {
              (*state.state.family_if<ControlNumberSnapshot>()).index = 5;
              (*state.state.family_if<ControlNumberSnapshot>()).state_generation++;
              (*state.state.family_if<ControlNumberSnapshot>()).value_text.assign("0.25");
            } else
              state.state.on = false;
          }
          assert(f.receive(state));
          terminal.identity = f.component.owner().identity();
          terminal.state.state_seq = state.state.state_seq;
          assert(f.receive(terminal));
          assert(f.commands() == 2 && f.component.owner().last_result() == result && f.status.text == terminal_status);
        }
      }
    }
    return;
  }
  if (scenario == 9) {
    const auto allocations = test_allocations;
    GenericFixture disabled(false);
    MaterialEntityControlCodec codec;
    ControlMessage capability;
    const char *wire = disabled.component.capability_entity_json();
    assert(wire != nullptr && codec.parse_entity(wire, capability) && !capability.enabled);
    assert(!disabled.component.open_number(2, "card-2", 4));
    assert(!disabled.component.owner().is_enabled() && !disabled.component.owner().has_selection());
    assert(disabled.commands() == 0 && test_allocations == allocations);
    return;
  }
  if (scenario < 100) {
    const size_t allocations = test_allocations;
    GenericFixture f;
    f.bind();
    f.open_number();
    assert(test_allocations == allocations && f.renders == 0 && !f.component.owner().is_scalar_ready());
    assert(f.component.owner().is_view_ready() && f.value.text == "0" && f.state.text == "C");
    assert(f.value.font == &f.number_font && f.minus.text == "-" && f.plus.text == "+");
    if (scenario == 0)
      return;
    if (scenario == 1 || scenario == 2) {
      test_event(f.off, LV_EVENT_PRESSED, &f.pointer);
      if (scenario == 2) {
        auto renewal = f.number_message(ControlKind::CONTROL_KIND_STATE);
        renewal.state.state_seq = 2;
        assert(f.receive(renewal));
      }
      test_event(f.off, LV_EVENT_PRESSING, &f.pointer);
      test_event(f.off, LV_EVENT_RELEASED, &f.pointer);
      assert(f.commands() == 1 && f.messages[f.count - 1].operation == ControlOperation::CONTROL_OPERATION_STEP);
      assert(f.messages[f.count - 1].direction == 1 && f.messages[f.count - 1].expected_index == 4);
      assert(f.renders == 0 && f.component.owner().is_command_pending());
      return;
    }
    if (scenario == 3) {
      test_event(f.off, LV_EVENT_PRESSED, &f.pointer);
      auto changed = f.number_message(ControlKind::CONTROL_KIND_STATE);
      changed.state.state_seq = 2;
      (*changed.state.family_if<ControlNumberSnapshot>()).state_generation = 3;
      assert(f.receive(changed));
      test_event(f.off, LV_EVENT_RELEASED, &f.pointer);
      assert(f.commands() == 0 && !f.component.owner().is_dragging());
      return;
    }
    if (scenario == 4) {
      auto unknown = f.number_message(ControlKind::CONTROL_KIND_STATE);
      unknown.state.state_seq = 2;
      (*unknown.state.family_if<ControlNumberSnapshot>()).state_generation = 2;
      (*unknown.state.family_if<ControlNumberSnapshot>()).available = false;
      (*unknown.state.family_if<ControlNumberSnapshot>()).index = 0;
      (*unknown.state.family_if<ControlNumberSnapshot>()).value_text.assign("--");
      assert(f.receive(unknown));
      assert((f.on.state & LV_STATE_DISABLED) && (f.off.state & LV_STATE_DISABLED));
      test_event(f.off, LV_EVENT_PRESSED, &f.pointer);
      test_event(f.off, LV_EVENT_RELEASED, &f.pointer);
      assert(f.commands() == 0 && f.value.text == "--" && !lv_obj_has_flag(&f.unknown, LV_OBJ_FLAG_HIDDEN));
      return;
    }
    if (scenario == 5) {
      esphome::lvgl::DirectSceneController *scene = &f.component;
      assert(scene->suspend_for_direct_overlay() && !f.component.owner().is_view_ready());
      scene->resume_after_direct_overlay();
      assert(f.component.owner().is_view_ready());
      assert(f.component.prepare_navigation(esphome::lvgl::NavigationAdmissionKind::CLOSE, &f.application));
      f.application.call_on_prepare_close_callbacks();
      f.application.call_on_close_cancelled_callbacks();
      f.navigation.transition = true;
      f.component.loop();
      assert(!f.component.owner().is_view_ready());
      f.navigation.transition = false;
      f.component.loop();
      assert(f.component.owner().is_view_ready() && f.renders == 0);
      return;
    }
    if (scenario == 6) {
      assert(f.component.suspend_for_direct_overlay());
      assert(f.component.open_number(2, "card-2", 4));
      assert(f.receive(f.number_message()));
      const auto replacement = f.component.get_selection_generation();
      assert(!f.component.owner().is_view_ready());
      f.component.resume_after_direct_overlay();
      assert(!f.component.owner().is_view_ready() && f.component.is_selection_current(replacement));
      return;
    }
    if (scenario == 7) {
      test_missing_glyph = true;
      assert(f.component.refresh_texts());
      assert(f.state.text == "?");
      test_missing_glyph = false;
      f.open_light(true, false);
      assert(f.value.font == &f.light_font && f.unknown.font == &f.unknown_font);
      assert(f.minus.text == "On" && f.plus.text == "Off" && !lv_obj_has_flag(&f.hit, LV_OBJ_FLAG_HIDDEN));
      assert(!f.component.owner().is_scalar_ready());
      return;
    }
    if (scenario == 8) {
      const auto old = f.component.get_selection_generation();
      assert(f.component.prepare_navigation(esphome::lvgl::NavigationAdmissionKind::CLOSE, &f.application));
      f.application.call_on_prepare_close_callbacks();
      f.open_number();
      assert(old != f.component.get_selection_generation());
      f.application.call_on_closed_callbacks();
      assert(f.component.owner().has_selection() && f.component.owner().is_view_ready() && f.commands() == 0);
      return;
    }
    assert(false);
  }
#ifdef TEST_WITH_SCALAR
  if (scenario == 107) {
    MixedFixture disabled(false);
    assert(disabled.scalar.memory_usage_bytes() == 0 && !disabled.entity.component.owner().is_enabled());
    auto *scene = static_cast<esphome::lvgl::DirectSceneController *>(&disabled.scalar);
    assert(scene->suspend_for_direct_overlay());
    scene->resume_after_direct_overlay();
    disabled.scalar.loop();
    assert(disabled.entity.component.view_lifecycle().is_backend(&disabled.entity.component));
    assert(!disabled.entity.component.owner().has_selection() && !disabled.entity.component.owner().is_view_ready());
    assert(disabled.entity.commands() == 0 && disabled.scalar.memory_usage_bytes() == 0);
    return;
  }
  MixedFixture f;
  auto &g = f.entity;
  g.bind(scenario != 104);
  g.open_light(scenario != 104);
  f.render_once();
  assert(g.component.owner().is_scalar_ready());
  if (scenario == 100 || scenario == 102 || scenario == 103 || scenario == 105) {
    const auto allocations = test_allocations;
    const auto renders = g.renders;
    g.open_number();
    assert(g.component.view_lifecycle().is_backend(&g.component));
    assert(test_allocations == allocations && g.renders == renders && !g.component.owner().is_scalar_ready());
    assert(f.scalar.is_quiescent());
    if (scenario == 100)
      return;
    if (scenario == 102) {
      esphome::lvgl::DirectSceneController *scene = &f.scalar;
      assert(scene->suspend_for_direct_overlay());
      assert(g.component.view_lifecycle().is_backend(&g.component) && !g.component.owner().is_view_ready());
      scene->resume_after_direct_overlay();
      assert(g.component.owner().is_view_ready());
      f.scalar.late_cancel();
      f.scalar.loop();
      assert(g.component.owner().is_view_ready() && g.component.view_lifecycle().is_backend(&g.component));
      return;
    }
    if (scenario == 103) {
      assert(f.scalar.prepare_navigation(esphome::lvgl::NavigationAdmissionKind::CLOSE, &g.application));
      g.application.call_on_prepare_close_callbacks();
      g.application.call_on_close_cancelled_callbacks();
      assert(!g.component.owner().is_view_ready());
      f.scalar.loop();
      assert(!g.component.owner().is_view_ready());
      g.component.loop();
      assert(g.component.owner().is_view_ready());
      return;
    }
    g.open_light();
    f.render_once();
    assert(g.component.owner().is_scalar_ready() && g.component.view_lifecycle().is_backend(&f.scalar));
    assert(g.value.font == &g.light_font);
    g.open_number();
    assert(g.component.owner().is_view_ready() && !g.component.owner().is_scalar_ready());
    return;
  }
  if (scenario == 101) {
    const auto count = g.count;
    const auto generation = g.component.get_selection_generation();
    f.scalar.set_borrowed(true);
    assert(!g.component.open_number(2, "card-2", 4));
    assert(g.count == count && g.component.get_selection_generation() == generation);
    assert(g.component.view_lifecycle().is_backend(&f.scalar) && !g.component.owner().is_view_ready());
    assert(!f.scalar.is_quiescent());
    f.scalar.set_borrowed(false);
    assert(g.component.invalidate(ControlReason::CONTROL_REASON_SLEEP));
    assert(g.component.suspend_control_view());
    assert(g.component.view_lifecycle().is_backend(&f.scalar) && f.scalar.is_quiescent());
    return;
  }
  if (scenario == 104) {
    assert(!g.component.owner().is_generic() && g.component.owner().is_scalar_ready());
    assert(!g.component.open_number(2, "card-2", 4));
    assert(g.messages[0].generic == false);
    return;
  }
  if (scenario == 109 || scenario == 110) {
    const auto allocations = test_allocations;
    const auto commands = g.commands();
    test_event(g.on, LV_EVENT_PRESSED, &g.pointer);
    assert(g.component.view_lifecycle().native_power_input_active());
    if (scenario == 110) {
      test_event(g.on, LV_EVENT_RELEASED, &g.pointer);
      assert(g.commands() == commands + 1 && g.component.view_lifecycle().native_power_input_active());
    }
    g.open_number();
    assert(!g.component.view_lifecycle().native_power_input_active());
    assert(g.component.view_lifecycle().is_backend(&g.component) && g.component.owner().is_view_ready());
    assert(!g.component.owner().is_scalar_ready() && f.scalar.is_quiescent());
    test_event(g.on, LV_EVENT_RELEASED, &g.pointer);
    f.scalar.loop();
    g.component.loop();
    assert(g.commands() == commands + (scenario == 110 ? 1 : 0));
    assert(test_allocations == allocations && g.component.owner().is_view_ready());
    return;
  }
  if (scenario == 108) {
    f.scalar.set_borrowed(true);
    test_delay_hook = [&]() {
      f.scalar.set_borrowed(false);
      assert(g.component.invalidate(ControlReason::CONTROL_REASON_VOICE));
    };
    assert(!g.component.open_number(2, "card-2", 4));
    test_delay_hook = {};
    assert(!g.component.owner().has_selection() && !g.component.owner().is_view_ready());
    assert(g.component.view_lifecycle().is_backend(&f.scalar) && f.scalar.is_quiescent());
    assert(g.commands() == 0);
    for (size_t i = 0; i < g.count; i++)
      assert(g.messages[i].identity.context.control_kind != ControlEntityKind::CONTROL_ENTITY_NUMBER);
    return;
  }
#endif
  assert(false);
}

int main(int argc, char **argv) {
  assert(argc == 2);
  const int scenario = std::stoi(argv[1]);
  run(scenario);
  std::cout << "number component case " << scenario << " PASS\n";
}
