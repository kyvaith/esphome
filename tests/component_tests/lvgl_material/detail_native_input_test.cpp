#include "real_lvgl_test_support.h"
#include "material_entity_control_component.h"
#include "fixtures.h"
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
struct Scalar : ControlViewBackend {
  Control &control;
  bool drain{true};
  bool cancelled{false}, alias{false}, inside_pointer_release{false};
  unsigned suspends{0}, resumes{0}, hides{0}, power_starts{0}, power_finishes{0};
  explicit Scalar(Control &owner) : control(owner) {}
  bool suspend_control_view_backend(uint32_t) override {
    assert(!inside_pointer_release);
    suspends++;
    cancelled = true;
    // Production cancellation runs before the coherent native refresh, and
    // the retained alias is hidden only after that presenter has drained.
    control.view_lifecycle().cancel_close_recovery();
    control.view_lifecycle().cancel_native_power_input();
    if (drain) {
      alias = true;
      lv_refr_now(lv_display_get_default());
    }
    return drain;
  }
  bool resume_control_view_backend(uint32_t generation) override {
    resumes++;
    assert(!alias);
    cancelled = false;
    control.view_lifecycle().set_generation(generation);
    return control.set_view_ready(generation, true) && control.scalar_ready();
  }
  bool hide_control_view_alias() override {
    hides++;
    if (!drain)
      return false;
    alias = false;
    lv_refr_now(lv_display_get_default());
    return true;
  }
  bool control_view_region_visible() const override { return true; }
  bool has_native_power_fence() const override { return true; }
  bool begin_native_power_input(uint32_t) override {
    power_starts++;
    return true;
  }
  bool finish_native_power_input(uint32_t) override {
    power_finishes++;
    return true;
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
  Scalar scalar{control};
  std::map<std::string, lv_obj_t *> w;
  std::vector<ControlMessage> messages;
  bool check_close_reentry{false};
  bool close_reentry_rejected{false};
  bool scalar_registered{false};
  Fixture(bool light = false, bool use_scalar = false) {
    navigation.active = &application;
    w["BASE"] = widgets.root;
#include "detail_widgets.h"
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
      assert(control.register_scalar_backend(&scalar, &navigation, &application));
      control.add_on_render_cancel_callback([this]() { scalar.cancelled = true; });
      control.add_on_render_request_callback([this](uint32_t request, bool, uint8_t) {
        if (control.view_lifecycle().is_backend(&scalar) && !scalar.cancelled)
          assert(control.render_ready(request, true));
      });
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
    MaterialEntityControlCodec codec;
    ControlMessage m;
    assert(codec.parse_entity(FIXTURES[light ? 0 : 3], m));
    m.kind = ControlKind::CONTROL_KIND_GRANT;
    m.identity = control.owner().identity();
    m.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    receive(m);
    assert(control.resume_control_view(control.get_selection_generation()));
    if (light)
      assert(control.scalar_ready());
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
    scalar.inside_pointer_release = true;
    pointer.release();
    scalar.inside_pointer_release = false;
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
  std::string name = argv[1];
  lv_init();
  {
    bool light = name.starts_with("light_") || name == "facet_back" || name == "reopen";
    Fixture f(light, name.starts_with("light_scalar"));
    if (name == "light_caps_revoke" || name == "detail_semantic_revoke") {
      // A valid incoming STATE can revoke its selection synchronously while
      // receive_entity still owns the decoded message. Exercise the real
      // outgoing codec/callback path, including its reentrancy fence.
      if (light) {
        lv_area_t a;
        lv_obj_get_coords(f.widgets.off, &a);
        f.pointer.press((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2);
      } else {
        f.press("climate_detail_knob");
      }
      assert(f.control.captured());
      auto m = f.next();
      if (light) {
        m.state.caps.brightness = false;
        m.state.brightness_known = false;
        m.state.brightness_pct = 0;
        (*m.state.family_if<ControlDetailSnapshot>()).state_generation++;
      } else {
        std::get<ControlClimateMain>((*m.state.family_if<ControlDetailSnapshot>()).data).mode = 1;
      }
      const auto identity = m.identity;
      const auto state_seq = m.state.state_seq;
      const size_t before = f.messages.size();
      f.check_close_reentry = true;
      f.receive(m, false);
      assert(f.messages.size() == before + 1);
      const auto &close = f.messages.back();
      assert(close.kind == ControlKind::CONTROL_KIND_CLOSE && close.generic);
      assert(close.identity == identity);
      assert(close.reason == (light ? ControlReason::CONTROL_REASON_CAPABILITIES_CHANGED
                                    : ControlReason::CONTROL_REASON_SELECTION_CHANGED));
      assert(f.close_reentry_rejected && f.control.owner().is_enabled());
      assert(!f.control.owner().has_selection());
      assert(m.identity == identity && m.state.state_seq == state_seq);
      f.pointer.release();
      f.control.loop();
      assert(f.commands() == 0 && !f.control.captured());
    } else if (name.starts_with("ha_")) {
      const size_t index = name == "ha_recovery" ? 0 : name == "ha_passive" ? 1 : 2;
      assert(f.control.invalidate(ControlReason::CONTROL_REASON_USER));
      assert(f.control.open_panel(2, "card-2", 4));
      for (size_t i = 0; i < 3 && REVIEWS[index][i]; i++) {
        MaterialEntityControlCodec codec;
        ControlMessage m;
        assert(codec.parse_entity(REVIEWS[index][i], m));
        m.identity = f.control.owner().identity();
        m.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
        f.receive(m);
        assert(f.control.owner().has_selection());
        assert(f.control.resume_control_view(f.control.get_selection_generation()));
        assert(f.visible("climate_detail_root"));
        if (index == 0)
          assert(f.visible("climate_detail_knob") == (i != 1));
        else
          assert(!f.visible("climate_detail_knob"));
      }
      assert(f.commands() == 0);
      if (index == 2)
        assert(std::string(lv_label_get_text(f.w["climate_detail_limits"])).find("Range") != std::string::npos);
    } else if (name.starts_with("light_scalar")) {
      assert(f.control.view_lifecycle().is_backend(&f.scalar));
      const uint32_t generation = f.control.get_selection_generation();
      unsigned before = f.scalar.suspends;
      if (name == "light_scalar_intent_aba" || name == "light_scalar_intent_deadline") {
        const size_t count = f.messages.size();
        f.press("light_detail_kelvin");
        f.scalar.inside_pointer_release = true;
        f.pointer.release();
        f.scalar.inside_pointer_release = false;
        assert(f.scalar.suspends == before && f.messages.size() == count);
        if (name == "light_scalar_intent_aba") {
          auto changed = f.next();
          (*changed.state.family_if<ControlDetailSnapshot>()).state_generation += 2;
          f.receive(changed);
        } else {
          rt::advance(1001);
        }
        f.control.loop();
        assert(f.scalar.suspends == before && f.messages.size() == count);
        assert(f.control.owner().is_view_ready() && !f.control.owner().is_detail_view_pending());
        std::cout << "RESULT " << name << " pass=1\n";
        return 0;
      }
      if (name == "light_scalar_drain")
        f.scalar.drain = false;
      f.tap("light_detail_kelvin");
      assert(f.scalar.suspends > before);
      assert(f.control.view_lifecycle().generation() == generation);
      if (name == "light_scalar_drain") {
        assert(!f.control.owner().is_detail_view_pending());
        assert(f.control.view_lifecycle().is_backend(&f.scalar));
        assert(!f.control.owner().is_view_ready());
        assert(!f.control.owner().has_selection());
        assert(f.messages.back().kind == ControlKind::CONTROL_KIND_CLOSE);
      } else if (name == "light_scalar_pending_close") {
        auto late = f.next();
        f.application.call_on_prepare_close_callbacks();
        assert(f.control.prepare_navigation(esphome::lvgl::NavigationAdmissionKind::CLOSE, &f.application));
        f.navigation.active = nullptr;
        f.application.call_on_closed_callbacks();
        assert(!f.control.owner().has_selection());
        assert(f.messages.back().kind == ControlKind::CONTROL_KIND_CLOSE);
        f.receive(late, false);
        assert(!f.control.owner().has_selection() && !f.control.owner().is_view_ready());
        assert(f.commands() == 0);
      } else if (name == "light_scalar_rejected") {
        auto request = f.messages.back();
        ControlMessage result;
        result.generic = true;
        result.kind = ControlKind::CONTROL_KIND_FACET_RESULT;
        result.identity = f.control.owner().identity();
        result.facet = request.facet;
        result.view_seq = request.view_seq;
        result.state.state_seq = f.control.owner().state().state_seq;
        result.reason = ControlReason::CONTROL_REASON_UNSUPPORTED;
        f.receive(result);
        assert(!f.control.owner().is_detail_view_pending() && f.control.owner().is_view_ready());
        assert(f.control.owner().is_scalar_ready() && f.control.view_lifecycle().is_backend(&f.scalar));
        assert(f.control.view_lifecycle().generation() == generation && f.commands() == 0);
      } else if (name == "light_scalar_timeout") {
        rt::advance(10001);
        f.control.loop();
        assert(!f.control.owner().is_detail_view_pending() && f.control.owner().is_view_ready());
        assert(!f.control.owner().detail_view_ready());
        assert(f.control.owner().is_scalar_ready() && f.control.view_lifecycle().is_backend(&f.scalar));
        f.tap("light_detail_kelvin");
        assert(f.control.owner().is_detail_view_pending());
        assert(f.commands() == 0);
      } else {
        f.facet(1);
        assert(f.control.view_lifecycle().is_backend(&f.control));
        f.tap("detail_numeric_back");
        f.facet(0);
        assert(f.control.view_lifecycle().is_backend(&f.scalar));
        assert(f.scalar.resumes >= 2 && f.commands() == 0);
        assert(f.control.view_lifecycle().generation() == generation);
      }
    } else if (name == "light_power_aba" || name == "light_power_release") {
      auto *button = f.widgets.off;
      lv_area_t a;
      lv_obj_get_coords(button, &a);
      f.pointer.press((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2);
      assert(f.control.captured());
      if (name == "light_power_aba") {
        auto m = f.next();
        (*m.state.family_if<ControlDetailSnapshot>()).state_generation += 2;
        f.receive(m);
      }
      f.pointer.release();
      f.control.loop();
      assert(f.commands() == (name == "light_power_aba" ? 0 : 1));
      assert(!f.control.view_lifecycle().native_power_input_active());
    } else if (name == "climate_open") {
      assert(f.visible("climate_detail_mode") && f.visible("climate_detail_parameters") &&
             f.visible("climate_detail_knob"));
      assert(!f.visible("detail_numeric_root") && !f.visible("light_detail_root"));
    } else if (name == "light_open") {
      assert(f.visible("light_detail_kelvin") && f.visible("light_detail_effect"));
      assert(lv_obj_is_visible(f.widgets.on));
      assert(!lv_obj_is_visible(f.widgets.status));
      f.control.set_text_provider([](uint8_t id) -> const char * {
        switch (id) {
          case 0:
            return "Turn on";
          case 1:
            return "Turn off";
          case 55:
            return "Active";
          case 56:
            return "Inactive";
          default:
            return nullptr;
        }
      });
      f.control.refresh_texts();
      assert(std::string(lv_label_get_text(f.widgets.state)) == "Active");
    } else if (name == "light_numeric_unknown") {
      lv_obj_set_style_bg_color(f.w["detail_numeric_root"], lv_color_black(), LV_PART_MAIN);
      f.tap("light_detail_kelvin");
      f.facet(1);
      auto m = f.next();
      auto &n = std::get<ControlFacetNumber>((*m.state.family_if<ControlDetailSnapshot>()).data);
      n.known = false;
      n.index = 0;
      n.value.assign("--");
      (*m.state.family_if<ControlDetailSnapshot>()).editable = false;
      (*m.state.family_if<ControlDetailSnapshot>()).state_generation++;
      f.receive(m);
      rt::advance(200);
      auto *track = f.w["detail_numeric_track"];
      assert(lv_obj_has_state(track, LV_STATE_DISABLED));
      assert(lv_obj_get_style_bg_opa(track, LV_PART_MAIN) == LV_OPA_TRANSP);
      assert(!f.visible("detail_numeric_knob"));
      const size_t pixel = (400 * 800 + 160) * 3;
      for (size_t i = 0; i < 3; i++)
        assert(f.lvgl.presented_pixels[pixel + i] == 0);
      assert(f.commands() == 0);
    } else if (name == "light_choice_empty" || name == "light_choice_unknown") {
      f.tap("light_detail_effect");
      f.facet(2);
      auto m = f.next();
      auto &c = std::get<ControlFacetChoice>((*m.state.family_if<ControlDetailSnapshot>()).data);
      c.known = name == "light_choice_empty";
      c.index = 255;
      (*m.state.family_if<ControlDetailSnapshot>()).editable = c.known;
      (*m.state.family_if<ControlDetailSnapshot>()).state_generation++;
      f.receive(m);
      assert(!f.control.owner().is_scalar_ready());
      assert(f.control.owner().last_result() != ControlResult::CONTROL_RESULT_STATE_MATCHES);
      assert(std::string(lv_label_get_text(f.w["detail_choice_status"])) == "Choose an option");
      if (!c.known) {
        assert(lv_obj_has_state(f.w["detail_choice_option_0"], LV_STATE_DISABLED));
        assert(lv_obj_has_state(f.w["detail_choice_apply"], LV_STATE_DISABLED));
        m.state.state_seq++;
        (*m.state.family_if<ControlDetailSnapshot>()).state_generation++;
        (*m.state.family_if<ControlDetailSnapshot>()).editable = c.known = true;
        f.receive(m);
      }
      f.tap("detail_choice_option_0");
      assert(!lv_obj_has_state(f.w["detail_choice_apply"], LV_STATE_DISABLED));
      assert(std::string(lv_label_get_text(f.w["detail_choice_status"])) == "Choose an option");
      assert(f.commands() == 0);
      f.tap("detail_choice_apply");
      assert(f.commands() == 1);
    } else if (name == "light_kelvin" || name == "facet_back" || name == "reopen") {
      f.tap("light_detail_kelvin");
      assert(f.control.owner().is_detail_view_pending());
      f.facet(1);
      assert(f.visible("detail_numeric_root") && !f.visible("light_detail_root"));
      assert(f.visible("detail_numeric_knob"));
      if (name == "light_kelvin") {
        f.tap("detail_numeric_plus");
        assert(f.commands() == 1 && f.messages.back().facet_target_index == 701);
      } else {
        f.tap("detail_numeric_back");
        f.facet(0);
        assert(f.visible("light_detail_root") && f.visible("light_detail_kelvin"));
        assert(lv_obj_is_visible(f.widgets.on));
        if (name == "reopen") {
          f.control.disconnect();
          assert(f.control.bind_entity_json(
              R"({"v":1,"type":"entity_control_v1","kind":"bind","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})"));
          ArduinoJson::JsonDocument r;
          assert(f.control.bind_detail_features(
              R"({"v":1,"type":"entity_detail_features_v1","detail_version":1,"light_detail_version":1,"climate_panel_version":1,"channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})",
              r.to<ArduinoJson::JsonObject>()));
          f.open(true);
          assert(f.visible("light_detail_kelvin"));
        }
      }
    } else if (name == "climate_drag" || name == "climate_aba" || name == "climate_current") {
      f.press("climate_detail_knob");
      assert(f.control.owner().is_dragging());
      if (name == "climate_current") {
        auto m = f.next();
        std::get<ControlClimateMain>((*m.state.family_if<ControlDetailSnapshot>()).data).current.assign("22");
        f.receive(m);
        assert(f.control.owner().is_dragging());
      }
      if (name == "climate_aba") {
        auto m = f.next();
        (*m.state.family_if<ControlDetailSnapshot>()).state_generation += 2;
        f.receive(m);
        assert(!f.control.owner().is_dragging());
      }
      f.pointer.move(680, 410);
      f.pointer.release();
      f.control.loop();
      assert(f.commands() == (name == "climate_aba" ? 0 : 1));
    } else if (name.starts_with("climate_outage_")) {
      f.tap("climate_detail_plus");
      assert(f.commands() == 1);
      const auto command = f.messages.back();
      if (name != "climate_outage_pending") {
        auto state = f.next();
        auto &main = std::get<ControlClimateMain>((*state.state.family_if<ControlDetailSnapshot>()).data);
        main.number.index = command.facet_target_index;
        int64_t value;
        char text[24];
        assert(main.number.domain.value_at(main.number.index, value));
        assert(number_format_decimal(value, text, sizeof(text)));
        main.number.value.assign(text);
        main.target.assign(text);
        (*state.state.family_if<ControlDetailSnapshot>()).state_generation++;
        f.receive(state);
        ControlMessage receipt;
        receipt.generic = true;
        receipt.kind = ControlKind::CONTROL_KIND_RESULT;
        receipt.identity = command.identity;
        receipt.command_seq = command.command_seq;
        receipt.state.state_seq = state.state.state_seq;
        receipt.result = name == "climate_outage_rejected" ? ControlResult::CONTROL_RESULT_REJECTED
                                                           : ControlResult::CONTROL_RESULT_STATE_MATCHES;
        receipt.reason = name == "climate_outage_rejected" ? ControlReason::CONTROL_REASON_SERVICE_ERROR
                                                           : ControlReason::CONTROL_REASON_NONE;
        f.receive(receipt);
      }
      const std::string previous = lv_label_get_text(f.w["climate_detail_status"]);
      f.unknown();
      assert(std::string(lv_label_get_text(f.w["climate_detail_status"])) ==
             (name == "climate_outage_status" ? "Unknown" : previous));
      assert(!f.visible("climate_detail_knob") && f.commands() == 1);
    } else if (name == "climate_passive") {
      f.unknown();
      assert(f.control.owner().has_selection());
      assert(f.visible("climate_detail_mode") && !f.visible("climate_detail_knob"));
      assert(lv_obj_has_state(f.w["climate_detail_plus"], LV_STATE_DISABLED));
    } else if (name == "climate_params" || name == "climate_humidity_locale") {
      f.tap("climate_detail_parameters");
      assert(f.visible("climate_detail_parameters_root"));
      assert(f.visible("climate_detail_parameter_humidity"));
      f.tap("climate_detail_parameter_humidity");
      f.facet(4);
      assert(f.visible("detail_numeric_icon_humidity") && !f.visible("detail_numeric_icon_kelvin"));
      if (name == "climate_humidity_locale") {
        f.control.set_text_provider([](uint8_t id) -> const char * { return id == 36 ? "," : nullptr; });
        auto observed = f.next();
        std::get<ControlFacetNumber>((*observed.state.family_if<ControlDetailSnapshot>()).data).current.assign("54.2");
        f.receive(observed);
        assert(std::string(lv_label_get_text(f.w["detail_numeric_current"])) == "Current: 54,2 %");
        assert(std::get<ControlFacetNumber>((*f.control.owner().state().family_if<ControlDetailSnapshot>()).data)
                   .current.view() == "54.2");
        assert(f.commands() == 0);
      }
    } else if (name == "facet_timeout") {
      f.tap("climate_detail_mode");
      rt::advance(10001);
      f.control.loop();
      assert(!f.control.owner().is_detail_view_pending());
      assert(f.control.owner().has_selection());
      f.tap("climate_detail_mode");
      assert(f.control.owner().is_detail_view_pending());
    } else {
      f.tap("climate_detail_mode");
      f.facet(5, name == "choice_glyph");
      assert(f.visible("detail_choice_option_0") && f.visible("detail_choice_option_1"));
      assert(!f.visible("climate_detail_root"));
      if (name == "choice_unknown" || name == "choice_glyph") {
        auto m = f.next();
        auto &c = std::get<ControlFacetChoice>((*m.state.family_if<ControlDetailSnapshot>()).data);
        if (name == "choice_unknown") {
          (*m.state.family_if<ControlDetailSnapshot>()).editable = false;
          c.known = false;
          c.index = 255;
          (*m.state.family_if<ControlDetailSnapshot>()).state_generation++;
          f.receive(m);
        }
        assert(lv_obj_has_state(f.w["detail_choice_option_0"], LV_STATE_DISABLED));
        assert(!lv_obj_has_state(f.w["detail_choice_back"], LV_STATE_DISABLED));
        f.tap("detail_choice_back");
        f.facet(3);
        assert(f.visible("climate_detail_root") && f.commands() == 0);
      } else {
        f.tap("detail_choice_option_2");
        assert(f.commands() == 0);
        if (name == "choice_aba") {
          auto m = f.next();
          (*m.state.family_if<ControlDetailSnapshot>()).state_generation += 2;
          f.receive(m);
        }
        f.tap("detail_choice_apply");
        assert(f.commands() == (name == "choice_aba" ? 0 : 1));
      }
    }
    std::cout << "RESULT " << name << " pass=1\n";
  }
  lv_deinit();
}
