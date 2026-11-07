#include "real_lvgl_test_support.h"
#include "material_entity_control_component.h"
#include "fixtures.h"
#include "material_direct_scalar_control.h"
#include "reviews.h"
#include <iostream>
#include <map>
using namespace esphome::lvgl_material;
namespace rt = real_lvgl_test;
struct Control : MaterialEntityControlComponent {
  using MaterialEntityControlComponent::MaterialEntityControlComponent;
  bool captured() const { return captured_target_ != nullptr; }
  bool scalar_ready() { return owner_.set_scalar_ready(true); }
  bool try_disable_owner() { return owner_.set_enabled(false); }
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
  bool request_ready() const {
    Request request;
    return this->read_request_(request);
  }
  uint32_t request_base() const {
    Request request;
    return this->read_request_(request) ? request.base : 0;
  }
  uint8_t request_value() const {
    Request request;
    assert(this->read_request_(request));
    return request.value;
  }
  bool alias_visible() const { return this->alias_visible_; }
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
  rt::HostDisplay display{lvgl};
  rt::NativeWidgets widgets{lv_screen_active()};
  rt::Pointer pointer{display.get()};
  esphome::lvgl::LvglNavigation navigation;
  esphome::lvgl::LvglApplication application;
  Control control{&lvgl};
  Scalar scalar{&lvgl, &control, &navigation, &application};
  std::map<std::string, lv_obj_t *> w;
  std::vector<ControlMessage> messages;
  bool check_close_reentry{false};
  bool close_reentry_rejected{false};
  bool scalar_registered{false};
  Fixture(bool light = false, bool use_scalar = false) {
    navigation.active = &application;
    w["BASE"] = widgets.root;
#include "detail_widgets.h"
    // A caller-supplied invisible recolor is not necessarily black.
    for (const char *id : {"light_detail_kelvin", "light_detail_effect"}) {
      lv_obj_set_style_recolor(w[id], lv_color_hex(0x13579B), LV_PART_MAIN);
      lv_obj_set_style_recolor_opa(w[id], LV_OPA_TRANSP, LV_PART_MAIN);
    }
    control.set_widgets(widgets.root, widgets.hit, widgets.title, widgets.value, widgets.unknown, widgets.on,
                        widgets.off);
    control.set_state_label(widgets.state);
    control.set_status_label(widgets.status);
    control.set_number_profile(&navigation, &application, widgets.on_label, widgets.off_label, LV_FONT_DEFAULT);
#include "detail_widget_bindings.h"
    control.set_initial_enabled(true);
    control.add_on_message_callback([this](const char *p) {
      MaterialEntityControlCodec codec;
      ControlMessage m;
      assert(codec.parse_entity(p, m));
      messages.push_back(m);
      if (check_close_reentry && m.kind == ControlKind::CONTROL_KIND_CLOSE)
        close_reentry_rejected = !control.try_disable_owner();
    });
    control.setup();
    assert(!control.is_failed());
    if (use_scalar) {
      scalar_registered = true;
      scalar.set_widgets(widgets.region, widgets.track, widgets.bubble, widgets.scalar_value, widgets.minimum,
                         widgets.maximum);
      scalar.set_active_color(0xD3E3FD);
      scalar.set_initial_enabled(true);
      scalar.setup();
      assert(!scalar.is_failed());
    }
    rt::advance(200);
    assert(control.bind_entity_json(
        R"({"v":1,"type":"entity_control_v1","kind":"bind","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})"));
    ArduinoJson::JsonDocument response;
    assert(control.bind_detail_features(
        R"({"v":1,"type":"entity_detail_features_v1","detail_version":1,"light_detail_version":1,"climate_panel_version":1,"channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})",
        response.to<ArduinoJson::JsonObject>()));
    open(light);
  }
  void open(bool light) {
    assert(light ? control.open_light_detail(2, "card-2", 4) : control.open_panel(2, "card-2", 4));
    if (light && control.owner().state().family_if<ControlNumberSnapshot>() != nullptr) {
      // OPEN changes identity before GRANT replaces the retained Number state.
      assert(control.owner().is_detail() && !control.owner().has_selection());
      assert(control.owner().state().family_if<ControlDetailSnapshot>() == nullptr);
      assert(control.refresh_texts());
    }
    MaterialEntityControlCodec codec;
    ControlMessage m;
    assert(codec.parse_entity(FIXTURES[light ? 0 : 3], m));
    m.kind = ControlKind::CONTROL_KIND_GRANT;
    if (light)
      m.state.brightness_pct = 70;
    m.identity = control.owner().identity();
    m.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    receive(m);
    assert(control.resume_control_view(control.get_selection_generation()));
    if (light) {
      scalar.run_one();
      scalar.loop();
      assert(control.owner().is_scalar_ready());
    }
    rt::advance(200);
  }
  void receive(const ControlMessage &m, bool accepted = true) {
    MaterialEntityControlCodec codec;
    char buffer[2049];
    size_t size;
    assert(codec.serialize_entity(m, buffer, sizeof(buffer), size));
    assert(control.receive_entity({buffer, size}) == accepted);
    lv_refr_now(display.get());
  }
  ControlMessage next() {
    ControlMessage m;
    m.generic = true;
    m.kind = ControlKind::CONTROL_KIND_STATE;
    m.identity = control.owner().identity();
    m.state = control.owner().state();
    m.state.state_seq++;
    return m;
  }
  void facet(size_t index, bool missing_glyph = false) {
    assert(messages.back().kind == ControlKind::CONTROL_KIND_FACET);
    auto requested = messages.back();
    MaterialEntityControlCodec codec;
    ControlMessage m;
    assert(codec.parse_entity(FIXTURES[index], m));
    m.kind = ControlKind::CONTROL_KIND_STATE;
    m.identity = control.owner().identity();
    m.state.state_seq = control.owner().state().state_seq + 1;
    (*m.state.family_if<ControlDetailSnapshot>()).view_seq = requested.view_seq;
    (*m.state.family_if<ControlDetailSnapshot>()).state_generation =
        (*control.owner().state().family_if<ControlDetailSnapshot>()).state_generation;
    if (missing_glyph)
      std::get<ControlFacetChoice>((*m.state.family_if<ControlDetailSnapshot>()).data)
          .options[0]
          .assign("\xE4\xB8\xAD");
    receive(m);
    assert(control.owner().is_detail_view_pending());
    if (scalar_registered && requested.facet != ControlFacet::CONTROL_FACET_MAIN)
      assert(!control.owner().is_view_ready());
    ControlMessage result;
    result.generic = true;
    result.kind = ControlKind::CONTROL_KIND_FACET_RESULT;
    result.identity = m.identity;
    result.facet = (*m.state.family_if<ControlDetailSnapshot>()).facet;
    result.view_seq = (*m.state.family_if<ControlDetailSnapshot>()).view_seq;
    result.state.state_seq = m.state.state_seq;
    result.facet_ready = true;
    receive(result);
    control.loop();
    // A facet change is not another application OPEN. Production has no
    // on_granted callback here to manually restart the selected presenter.
    assert(control.owner().is_view_ready());
    if (!control.owner().is_discrete())
      assert(scalar_registered ? control.owner().is_scalar_ready() : control.scalar_ready());
    rt::advance(200);
  }
  lv_point_t center(const char *id) {
    lv_obj_update_layout(widgets.root);
    lv_area_t a;
    lv_obj_get_coords(w.at(id), &a);
    return {(a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2};
  }
  void press(const char *id) {
    auto p = center(id);
    pointer.press(p.x, p.y);
  }
  void tap(const char *id) {
    press(id);

    pointer.release();

    control.loop();
    rt::advance(100);
  }
  bool visible(const char *id) { return lv_obj_is_visible(w.at(id)); }
  size_t commands() {
    size_t n = 0;
    for (auto &m : messages)
      n += m.kind == ControlKind::CONTROL_KIND_COMMAND;
    return n;
  }
  void unknown() {
    auto m = next();
    auto &d = (*m.state.family_if<ControlDetailSnapshot>());
    auto &main = std::get<ControlClimateMain>(d.data);
    d.available = false;
    d.editable = false;
    d.state_generation++;
    main.mode = 0;
    main.activity = 0;
    main.target.assign("");
    main.current.assign("");
    main.low.assign("");
    main.high.assign("");
    main.number.index = 0;
    main.number.known = false;
    main.number.value.assign("--");
    receive(m);
  }
  ~Fixture() { control.on_shutdown(); }
};

int main(int argc, char **argv) {
  assert(argc == 2);
  const std::string name = argv[1];
  lv_init();
  {
    Fixture f(true, true);
    if (name == "idle_unknown") {
      auto state = f.next();
      state.state.brightness_known = false;
      state.state.brightness_pct = 0;
      (*state.state.family_if<ControlDetailSnapshot>()).state_generation++;
      f.receive(state);
      f.scalar.loop();
      f.scalar.run_one();
      f.scalar.loop();
      rt::advance(200);
      assert(lv_obj_is_visible(f.widgets.unknown));
      assert(!lv_obj_is_visible(f.widgets.value));
    }
    if (name.starts_with("retired_")) {
      // The displayed Scalar canvas is an old observation after a real CLOSE,
      // not a new unknown render request. Hide its parent without freeing or
      // touching the presenter's borrowed source; the normal loop drains it.
      const auto was_selected = f.control.get_selection_generation();
      assert(lv_obj_is_visible(f.widgets.hit));
      ControlMessage close;
      close.generic = true;
      close.kind = ControlKind::CONTROL_KIND_CLOSE;
      close.identity = f.control.owner().identity();
      close.reason = ControlReason::CONTROL_REASON_ENTITY_UNAVAILABLE;
      if (name == "retired_inflight_recovery") {
        f.pointer.press(300, 464);
        f.pointer.move(400, 464);
        f.lvgl.immediate_retire = false;
        f.lvgl.on_submit = [&]() {
          const auto *borrowed = f.lvgl.borrowed;
          assert(borrowed != nullptr);
          f.receive(close);
          // Revocation hides the parent but must neither free nor replace a
          // source still borrowed by the presentation transport.
          assert(f.lvgl.borrowed == borrowed);
          assert(!lv_obj_is_visible(f.widgets.hit));
          f.lvgl.retire();
        };
        f.scalar.run_one();
        f.lvgl.on_submit = {};
        f.lvgl.immediate_retire = true;
        f.pointer.release();
      } else {
        f.receive(close);
      }
      assert(!f.control.owner().has_selection());
      assert(!f.scalar.active());
      assert(!lv_obj_is_visible(f.widgets.hit));
      assert(std::string(lv_label_get_text(f.widgets.state)) == "--");
      f.scalar.loop();
      assert(f.scalar.is_quiescent());
      assert(f.scalar.alias_visible());
      rt::advance(200);
      for (int y = 410; y < 522; y++) {
        for (int x = 118; x < 682; x++) {
          const size_t pixel = (y * 800 + x) * 3;
          assert(f.lvgl.presented_pixels[pixel] == 0x30);
          assert(f.lvgl.presented_pixels[pixel + 1] == 0x20);
          assert(f.lvgl.presented_pixels[pixel + 2] == 0x10);
        }
      }
      const auto flushes = f.lvgl.native_flushes;
      for (unsigned i = 0; i < 5; i++) {
        assert(f.control.refresh_texts());
        lv_refr_now(f.display.get());
      }
      assert(f.lvgl.native_flushes == flushes);
      assert(f.commands() == 0);
      // A cancelled OPEN has not granted any fresh source. Its interim view
      // must stay neutral, rather than restoring the old alias prematurely.
      assert(f.control.open_light_detail(2, "card-2", 4));
      assert(!lv_obj_is_visible(f.widgets.hit));
      assert(f.control.invalidate(ControlReason::CONTROL_REASON_USER));
      assert(!lv_obj_is_visible(f.widgets.hit));
      if (name == "retired_number_recovery") {
        assert(f.control.open_number(6, "card-number-6", 3));
        ControlMessage grant;
        grant.generic = true;
        grant.kind = ControlKind::CONTROL_KIND_GRANT;
        grant.identity = f.control.owner().identity();
        grant.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
        grant.state.state_seq = 1;
        grant.state.title.assign("Number");
        auto &number = grant.state.emplace_family<ControlNumberSnapshot>();
        number.domain.minimum = -1000000;
        number.domain.maximum = 1000000;
        number.domain.step = 250000;
        number.domain.max_index = 8;
        number.domain.unit[0] = 'C';
        number.index = 4;
        number.available = true;
        number.value_text.assign("0");
        number.state_generation = 1;
        f.receive(grant);
        assert(f.control.resume_control_view(f.control.get_selection_generation()));
        assert(f.control.owner().is_number() && f.control.owner().is_view_ready());
        assert(!f.visible("light_detail_root"));
        assert(lv_obj_is_visible(f.widgets.value));
        assert(!lv_obj_is_visible(f.widgets.hit));
        assert(f.control.invalidate(ControlReason::CONTROL_REASON_USER));
        f.open(true);
        assert(f.visible("light_detail_kelvin"));
      } else if (name != "retired_legacy_recovery") {
        f.open(true);
        assert(f.visible("light_detail_kelvin"));
      } else {
        assert(f.control.open(2, "card-2", 4));
        MaterialEntityControlCodec codec;
        ControlMessage m;
        assert(codec.parse_entity(FIXTURES[0], m));
        m.state.clear_family();
        m.detail_open = false;
        m.kind = ControlKind::CONTROL_KIND_GRANT;
        m.identity = f.control.owner().identity();
        m.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
        f.receive(m);
        assert(f.control.resume_control_view(f.control.get_selection_generation()));
        f.scalar.run_one();
        f.scalar.loop();
        assert(!f.visible("light_detail_root"));
        assert(lv_obj_is_visible(f.widgets.status));
      }
      assert(f.control.get_selection_generation() != was_selected);
      assert(lv_obj_is_visible(f.widgets.hit));
      assert(lv_obj_is_visible(f.widgets.value));
      assert(std::string(lv_label_get_text(f.widgets.state)) == "On");
      assert(!f.scalar.alias_visible() && f.scalar.active());
      assert(f.control.owner().is_scalar_ready() && f.commands() == 0);
    } else if (name == "addon_transition") {
      auto *button = f.w["light_detail_kelvin"];
      lv_obj_add_state(button, LV_STATE_DISABLED);
      rt::advance(200);
      assert(lv_color_eq(lv_obj_get_style_recolor(button, LV_PART_MAIN), lv_color_hex(0x13579B)));
      lv_obj_remove_state(button, LV_STATE_DISABLED);
      lv_refr_now(f.display.get());
      const auto before = f.lvgl.native_flushes;
      assert(lv_anim_count_running() == 0);
      rt::advance(200);
      assert(f.lvgl.native_flushes == before);
      assert(lv_color_eq(lv_obj_get_style_recolor(button, LV_PART_MAIN), lv_color_hex(0x13579B)));
    } else if (name.starts_with("idle_")) {
      // No transport, pointer movement, timer, or source change: the same
      // static view must not invalidate an accepted regional base.
      const auto before = f.lvgl.native_flushes;
      const auto base = f.lvgl.generation;
      for (unsigned i = 0; i < 5; i++) {
        assert(f.control.refresh_texts());
        lv_refr_now(f.display.get());
      }
      assert(f.lvgl.native_flushes == before);
      assert(f.lvgl.generation == base);
      assert(f.commands() == 0);
    } else {
      f.pointer.press(300, 464);
      f.pointer.move(400, 464);
      assert(f.control.owner().is_dragging());
      f.scalar.run_one();
      f.scalar.loop();
      assert(f.commands() == 0);
      if (name == "brightness_capture") {
        // capture_screen pauses and resumes the direct source globally. It
        // does not grant another application view or end the held pointer.
        assert(f.lvgl.direct_regions_pause_start());
        assert(f.lvgl.direct_regions_pause_wait(250));
        assert(f.lvgl.direct_regions_pause(false, 250));
        assert(f.control.owner().is_dragging());
      }
      f.pointer.release();
      assert(f.commands() == 1);
      assert(f.messages.back().value == 50);
      auto state = f.next();
      state.state.brightness_pct = 50;
      (*state.state.family_if<ControlDetailSnapshot>()).state_generation++;
      f.receive(state);
      ControlMessage receipt;
      receipt.generic = true;
      receipt.kind = ControlKind::CONTROL_KIND_RESULT;
      receipt.identity = state.identity;
      receipt.command_seq = f.messages.back().command_seq;
      receipt.result = ControlResult::CONTROL_RESULT_STATE_MATCHES;
      receipt.reason = ControlReason::CONTROL_REASON_NONE;
      receipt.state.state_seq = state.state.state_seq;
      f.receive(receipt);
      f.scalar.loop();
      const auto base = f.lvgl.generation;
      f.lvgl.immediate_retire = false;
      f.lvgl.on_submit = [&]() {
        if (name == "brightness_refresh") {
          assert(f.control.refresh_texts());
          lv_refr_now(f.display.get());
        } else if (name == "late_base_rejected") {
          // A real unrelated native scene change still fails closed. The
          // fix must not weaken the generation check or force readiness.
          lv_obj_invalidate(f.widgets.root);
          lv_refr_now(f.display.get());
        }
        f.lvgl.retire();
      };
      f.scalar.run_one();
      f.scalar.loop();
      if (name == "late_base_rejected") {
        assert(!f.scalar.active());
        assert(!f.control.owner().is_view_ready());
        assert(!f.control.owner().is_scalar_ready());
        assert(!f.scalar.accepted());
      } else {
        // Revisit the same request after its completion callback. Previously
        // that callback hid and revealed the unchanged value, invalidating
        // this base and causing the next real worker iteration to fail.
        for (unsigned i = 0; i < 6; i++) {
          rt::advance(20);
          f.scalar.run_one();
          f.scalar.loop();
          assert(f.scalar.active());
          assert(f.control.owner().is_view_ready());
          assert(f.control.owner().is_scalar_ready());
          assert(f.scalar.accepted());
          assert(f.lvgl.generation == base);
        }
      }
      assert(f.commands() == 1);
      assert(!f.control.owner().is_command_pending());
    }
  }
  std::cout << "RESULT " << name << " pass=1\n";
}
