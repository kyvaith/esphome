#include "real_lvgl_test_support.h"
#include "material_entity_control_component.h"
#include "media_owner_fixtures.h"
#include <iostream>

using namespace esphome::lvgl_material;
namespace rt = real_lvgl_test;
using Media = ControlMediaSnapshot;
using Facet = ControlMediaFacet;
using Op = ControlOperation;
struct Control : MaterialEntityControlComponent {
  using MaterialEntityControlComponent::MaterialEntityControlComponent;
  void refresh() { this->update_static_(); }
  bool captured() const { return this->captured_target_ != nullptr; }
};
// The real lifecycle owns this bounded test backend. It records drain/resume
// ordering only; P4 Scalar presentation remains a separate hardware gate.
struct ScalarBackend : ControlViewBackend {
  Control &control;
  unsigned resumes{0}, suspends{0};
  bool drain_ok{true};
  explicit ScalarBackend(Control &c) : control(c) {}
  bool suspend_control_view_backend(uint32_t) override {
    suspends++;
    return drain_ok;
  }
  bool resume_control_view_backend(uint32_t generation) override {
    resumes++;
    assert(control.owner().is_media_volume());
    control.view_lifecycle().set_generation(generation);
    return control.set_view_ready(generation, true);
  }
  bool hide_control_view_alias() override { return true; }
  bool control_view_region_visible() const override { return true; }
};
struct Fixture {
  esphome::lvgl::LvglComponent lvgl;
  rt::HostDisplay display{lvgl};
  rt::NativeWidgets widgets{lv_screen_active()};
  rt::Pointer pointer{display.get()};
  esphome::lvgl::LvglNavigation navigation;
  esphome::lvgl::LvglApplication application;
  Control control{&lvgl};
  ScalarBackend backend{control};
  uint32_t render_request{0};
  lv_obj_t *main[8]{}, *volume[6]{}, *choice[8]{}, *actions[12]{}, *action_labels[12]{}, *facets[3]{},
      *facet_labels[3]{}, *options[8]{}, *option_labels[8]{}, *icons[4]{};
  std::vector<ControlMessage> messages;
  lv_obj_t *label(lv_obj_t *root, int x, int y, int width = 600, int height = 36) {
    return rt::NativeWidgets::label(root, "", x, y, width, height);
  }
  lv_obj_t *button(lv_obj_t *root, int x, int y, lv_obj_t *&caption) {
    auto *b = lv_button_create(root);
    lv_obj_remove_style_all(b);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, 180, 64);
    lv_obj_set_scrollable(b, false);
    caption = label(b, 0, 14, 180, 36);
    return b;
  }
  Fixture(const char *wire = MUSIC_MAIN, bool back_theme = false, bool configured_recolor = false) {
    navigation.active = &application;
    main[0] = rt::NativeWidgets::plain(widgets.root, 0, 0, 800, 800, 0x111111);
    volume[0] = rt::NativeWidgets::plain(widgets.root, 0, 0, 800, 800, 0x111111);
    lv_obj_set_style_bg_opa(volume[0], LV_OPA_TRANSP, LV_PART_MAIN);
    choice[0] = rt::NativeWidgets::plain(widgets.root, 0, 0, 800, 800, 0x111111);
    for (size_t i = 1; i < 8; i++)
      main[i] = label(main[0], 50, 15 + static_cast<int>(i) * 40);
    for (size_t i = 1; i < 5; i++)
      volume[i] = label(volume[0], 50, 20 + static_cast<int>(i) * 50);
    for (size_t i = 1; i < 5; i++)
      choice[i] = label(choice[0], 50, 20 + static_cast<int>(i) * 50);
    lv_obj_t *unused;
    volume[5] = button(volume[0], 300, 680, unused);
    if (back_theme) {
      // Preserve the actual public button theme, unlike the plain test controls.
      // Product local styles do not remove its invisible recolor transition.
      lv_theme_apply(volume[5]);
      lv_obj_set_pos(volume[5], 294, 630);
      lv_obj_set_size(volume[5], 212, 64);
      lv_obj_set_style_radius(volume[5], 32, LV_PART_MAIN);
      lv_obj_set_style_bg_color(volume[5], lv_color_hex(0x1D2833), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(volume[5], LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_border_width(volume[5], 0, LV_PART_MAIN);
      lv_obj_set_style_pad_all(volume[5], 0, LV_PART_MAIN);
      lv_obj_set_style_shadow_width(volume[5], 0, LV_PART_MAIN);
      lv_obj_set_style_bg_opa(volume[5], LV_OPA_80, static_cast<lv_style_selector_t>(LV_PART_MAIN) | LV_STATE_PRESSED);
      if (configured_recolor)
        lv_obj_set_style_recolor(volume[5], lv_color_hex(0x123456), LV_PART_MAIN);
    }
    choice[5] = button(choice[0], 100, 690, unused);
    choice[6] = button(choice[0], 450, 690, choice[7]);
    for (size_t i = 0; i < 12; i++)
      actions[i] =
          button(main[0], 20 + static_cast<int>(i % 4) * 195, 340 + static_cast<int>(i / 4) * 80, action_labels[i]);
    // The production shared slots intentionally show just one sibling.
    lv_obj_set_pos(actions[1], 20, 340);
    lv_obj_set_pos(actions[6], 215, 420);
    for (size_t i = 0; i < 3; i++)
      facets[i] = button(main[0], 50 + static_cast<int>(i) * 230, 650, facet_labels[i]);
    for (size_t i = 0; i < 8; i++)
      options[i] =
          button(choice[0], 150 + static_cast<int>(i % 2) * 230, 280 + static_cast<int>(i / 2) * 80, option_labels[i]);
    for (auto &icon : icons)
      icon = rt::NativeWidgets::plain(main[0], 720, 20, 32, 32, 0);
    control.set_widgets(widgets.root, widgets.hit, widgets.title, widgets.value, widgets.unknown, widgets.on,
                        widgets.off);
    control.set_state_label(widgets.state);
    control.set_status_label(widgets.status);
    control.set_number_profile(&navigation, &application, widgets.on_label, widgets.off_label, LV_FONT_DEFAULT);
    control.set_media_profile(main[0], main[1], main[2], main[3], main[4], main[5], main[6], main[7], LV_FONT_DEFAULT,
                              LV_FONT_DEFAULT, LV_FONT_DEFAULT);
    control.set_media_volume_profile(volume[0], volume[1], volume[2], volume[3], volume[4], volume[5], LV_FONT_DEFAULT);
    control.set_media_choice_profile(choice[0], choice[1], choice[2], choice[3], choice[4], choice[5], choice[6],
                                     choice[7], LV_FONT_DEFAULT);
    for (size_t i = 0; i < 4; i++)
      control.set_media_icon(i, icons[i]);
    for (size_t i = 0; i < 12; i++)
      control.set_media_action(i, actions[i], action_labels[i]);
    for (size_t i = 0; i < 3; i++)
      control.set_media_facet(i, facets[i], facet_labels[i]);
    for (size_t i = 0; i < 8; i++)
      control.set_media_choice_option(i, options[i], option_labels[i]);
    control.set_media_enabled(true);
    control.set_initial_enabled(true);
    control.add_on_message_callback([this](const char *wire) {
      MaterialEntityControlCodec codec;
      ControlMessage value;
      assert(codec.parse_entity(wire, value));
      messages.push_back(value);
    });
    control.setup();
    assert(!control.is_failed());
    assert(control.register_scalar_backend(&backend, &navigation, &application));
    control.add_on_render_request_callback([this](uint32_t id, bool, uint8_t) { render_request = id; });
    rt::advance(100);
    assert(control.bind_entity_json(
        R"({"v":1,"type":"entity_control_v1","kind":"bind","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})"));
    ArduinoJson::JsonDocument response;
    assert(control.bind_extra_features(
        R"({"v":1,"type":"entity_panel_extra_v1","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","profiles":[{"name":"media","version":1}]})",
        response.to<ArduinoJson::JsonObject>()));
    assert(control.open_panel(2, "card-2", 4));
    auto grant = parse(wire);
    grant.kind = ControlKind::CONTROL_KIND_GRANT;
    grant.identity = control.owner().identity();
    grant.identity.token.assign("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    receive(grant);
    assert(control.resume_control_view(control.get_selection_generation()));
    rt::advance(200);
    assert(control.owner().is_view_ready());
  }
  ControlMessage parse(const char *wire) {
    MaterialEntityControlCodec codec;
    ControlMessage m;
    assert(codec.parse_entity(wire, m));
    return m;
  }
  void receive(const ControlMessage &m) {
    MaterialEntityControlCodec codec;
    char wire[2049];
    size_t length;
    assert(codec.serialize_entity(m, wire, sizeof(wire), length));
    assert(control.receive_entity({wire, length}));
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
  void press(lv_obj_t *obj) {
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    pointer.press((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2);
  }
  void tap(lv_obj_t *obj) {
    press(obj);
    pointer.release();
    control.loop();
  }
  size_t commands() const {
    return std::count_if(messages.begin(), messages.end(),
                         [](const auto &m) { return m.kind == ControlKind::CONTROL_KIND_COMMAND; });
  }
  const ControlMessage &last() const { return messages.back(); }
  void answer_facet(const char *wire) {
    const auto request = last();
    assert(request.kind == ControlKind::CONTROL_KIND_FACET);
    auto value = parse(wire);
    value.identity = control.owner().identity();
    value.state.state_seq = control.owner().state().state_seq + 1;
    auto *m = value.state.family_if<Media>();
    m->view_seq = request.state.family_if<Media>()->view_seq;
    receive(value);
    assert(control.owner().is_media_view_pending());
    ControlMessage reply;
    reply.generic = true;
    reply.kind = ControlKind::CONTROL_KIND_FACET_RESULT;
    reply.identity = value.identity;
    reply.state.state_seq = value.state.state_seq;
    auto &r = reply.state.emplace_family<Media>();
    r.facet = m->facet;
    r.view_seq = m->view_seq;
    r.editable = true;
    receive(reply);
    rt::advance(50);
    assert(!control.owner().is_media_view_pending());
  }
  void scalar_ready() {
    assert(render_request != 0);
    assert(control.render_ready(render_request, true));
  }
};
int main(int argc, char **argv) {
  assert(argc == 2);
  lv_init();
  std::setbuf(stdout, nullptr);
  const std::string name = argv[1];
  Fixture f(name == "tv" || name == "source-facet" ? TV_MAIN
            : name == "unknown"                    ? MAIN_UNKNOWN
            : name == "unavailable"                ? MAIN_UNAVAILABLE
            : name.starts_with("volume-")          ? MAIN_FRACTION
                                                   : MUSIC_MAIN,
            name.starts_with("volume-back-transition"), name.ends_with("-configured"));
  assert(lv_obj_is_visible(f.main[0]));
  assert(!lv_obj_is_visible(f.widgets.hit));
  if (name == "main") {
    assert(!lv_obj_is_visible(f.actions[0]) && lv_obj_is_visible(f.actions[1]));
    assert(!lv_obj_is_visible(f.actions[5]) && lv_obj_is_visible(f.actions[6]));
    assert(!lv_obj_has_state(f.actions[1], LV_STATE_DISABLED));
    assert(std::string(lv_label_get_text(f.main[7])) == "100%");
  } else if (name == "unknown" || name == "unavailable") {
    assert(f.control.owner().media_view_ready() && f.control.owner().is_view_ready());
    for (size_t i = 0; i < 12; i++)
      if (i != 7)
        assert(lv_obj_has_state(f.actions[i], LV_STATE_DISABLED));
    assert(f.commands() == 0);
  } else if (name == "tv") {
    assert(!lv_obj_is_visible(f.actions[7]) && !lv_obj_is_visible(f.main[7]));
    assert(!lv_obj_is_visible(f.actions[8]) && !lv_obj_is_visible(f.actions[9]));
  } else if (name == "release") {
    f.press(f.actions[1]);
    assert(f.control.captured() && f.commands() == 0);
    f.pointer.release();
    assert(f.commands() == 1 && f.last().operation == Op::CONTROL_OPERATION_MEDIA_PAUSE);
  } else if (name == "semantic-cancel" || name == "metadata-hold") {
    f.press(f.actions[1]);
    assert(f.control.captured());
    auto m = f.next();
    if (name == "semantic-cancel")
      m.state.family_if<Media>()->state_generation++;
    else
      std::get<Media::Main>(m.state.family_if<Media>()->data).title.assign("New metadata");
    f.receive(m);
    f.pointer.release();
    assert(f.commands() == (name == "metadata-hold" ? 1 : 0));
  } else if (name == "source-facet") {
    f.tap(f.facets[0]);
    assert(f.commands() == 0);
    f.answer_facet(TV_SOURCE);
    assert(lv_obj_is_visible(f.choice[0]) && !lv_obj_is_visible(f.main[0]));
    f.tap(f.options[1]);
    assert(f.commands() == 0);
    f.tap(f.choice[6]);
    assert(f.commands() == 1 && f.last().operation == Op::CONTROL_OPERATION_MEDIA_CHOICE);
  } else if (name.starts_with("volume-")) {
    f.tap(f.actions[7]);
    f.answer_facet(VOLUME_FRACTION);
    assert(lv_obj_is_visible(f.volume[0]) && lv_obj_is_visible(f.widgets.hit));
    assert(!lv_obj_is_visible(f.widgets.on) && !lv_obj_is_visible(f.widgets.off));
    assert(!lv_obj_is_visible(f.widgets.value) && !lv_obj_is_visible(f.widgets.unknown));
    assert(std::string(lv_label_get_text(f.volume[3])) == "42.5%");
    assert(f.backend.resumes == 1 && f.control.view_lifecycle().is_backend(&f.backend));
    assert(lv_obj_has_state(f.widgets.hit, LV_STATE_DISABLED));
    f.scalar_ready();
    assert(!lv_obj_has_state(f.widgets.hit, LV_STATE_DISABLED));
    if (name.starts_with("volume-back-transition")) {
      rt::advance(400);
      auto *back = f.volume[5];
      const auto configured = lv_obj_get_style_recolor(back, LV_PART_MAIN);
      const auto count_late = [&](const char *phase) {
        lv_refr_now(f.display.get());
        const auto boundary = f.lvgl.native_flushes;
        for (unsigned i = 0; i < 40; i++) {
          const auto previous = f.lvgl.native_flushes;
          rt::advance(10);
          if (previous != f.lvgl.native_flushes)
            std::cout << "LATE " << phase << " ms=" << (i + 1) * 10
                      << " recolor=" << lv_color_to_u32(lv_obj_get_style_recolor(back, LV_PART_MAIN))
                      << " recolor_opa=" << unsigned(lv_obj_get_style_recolor_opa(back, LV_PART_MAIN))
                      << " hit_recolor_opa=" << unsigned(lv_obj_get_style_recolor_opa(f.widgets.hit, LV_PART_MAIN))
                      << " animations=" << lv_anim_count_running() << "\n";
        }
        return f.lvgl.native_flushes - boundary;
      };
      f.pointer.press(350, 460);
      f.pointer.move(570, 460);
      f.pointer.release();
      assert(f.commands() == 1 && f.control.owner().is_command_pending());
      assert(lv_obj_has_state(back, LV_STATE_DISABLED));
      const auto down = count_late("enabled-to-disabled");
      auto state = f.next();
      auto *media = state.state.family_if<Media>();
      media->state_generation++;
      auto &volume = std::get<Media::Volume>(media->data);
      volume.raw.assign("0.8");
      volume.percent.assign("80");
      volume.rail = 8000;
      f.receive(state);
      ControlMessage result;
      result.generic = true;
      result.kind = ControlKind::CONTROL_KIND_RESULT;
      result.identity = f.control.owner().identity();
      result.command_seq = f.last().command_seq;
      result.state.state_seq = state.state.state_seq;
      result.result = ControlResult::CONTROL_RESULT_STATE_MATCHES;
      f.receive(result);
      assert(!f.control.owner().is_command_pending());
      assert(!lv_obj_has_state(back, LV_STATE_DISABLED));
      const auto up = count_late("disabled-to-enabled");
      std::cout << "BACK_TRANSITION down=" << down << " up=" << up << " configured=" << lv_color_to_u32(configured)
                << "\n";
      assert(lv_color_eq(lv_obj_get_style_recolor(back, LV_PART_MAIN), configured));
      assert(down == 0 && up == 0);
    } else if (name == "volume-tap") {
      f.tap(f.widgets.hit);
      assert(f.commands() == 0);
    } else if (name == "volume-drag" || name == "volume-cancel") {
      f.pointer.press(350, 460);
      assert(f.control.captured());
      f.pointer.move(570, 460);
      assert(f.commands() == 0 && f.control.owner().candidate() == 80);
      if (name == "volume-cancel") {
        auto m = f.next();
        m.state.family_if<Media>()->state_generation++;
        f.receive(m);
      }
      f.pointer.release();
      assert(f.commands() == (name == "volume-drag" ? 1 : 0));
      if (name == "volume-drag") {
        assert(f.last().value == 80 && f.last().operation == Op::CONTROL_OPERATION_MEDIA_VOLUME);
        assert(std::get<Media::Volume>(f.last().state.family_if<Media>()->data).raw.view() == "0.425");
      }
    } else if (name == "volume-back" || name == "volume-drain-failure") {
      f.backend.drain_ok = name != "volume-drain-failure";
      f.tap(f.volume[5]);
      assert(f.backend.suspends == 1);
      if (f.backend.drain_ok) {
        f.answer_facet(MAIN_FRACTION);
        assert(f.control.view_lifecycle().is_backend(&f.control) && lv_obj_is_visible(f.main[0]));
      } else
        assert(!f.control.owner().has_selection());
      assert(f.commands() == 0);
    } else
      assert(name == "volume-facet");
  } else if (name == "palette") {
    for (auto *obj : {f.widgets.on, f.widgets.off, f.widgets.on_label, f.widgets.off_label}) {
      lv_obj_set_style_bg_color(obj, lv_color_hex(0x512244), LV_STATE_DISABLED | LV_STATE_PRESSED);
      lv_obj_set_style_text_color(obj, lv_color_hex(0x986244), LV_STATE_DISABLED | LV_STATE_PRESSED);
      lv_obj_add_state(obj, static_cast<lv_state_t>(LV_STATE_DISABLED | LV_STATE_PRESSED));
    }
    lv_obj_set_style_bg_color(f.widgets.on, lv_color_hex(0xB6E4FA), 0);
    lv_obj_set_style_text_color(f.widgets.on_label, lv_color_hex(0x071018), 0);
    auto *svg_proxy = lv_obj_create(f.actions[1]);
    lv_obj_remove_style_all(svg_proxy);
    f.control.refresh();
    assert(lv_color_eq(lv_obj_get_style_bg_color(f.actions[1], LV_PART_MAIN), lv_color_hex(0xB6E4FA)));
    assert(lv_color_eq(lv_obj_get_style_text_color(svg_proxy, LV_PART_MAIN), lv_color_hex(0x071018)));
  } else if (name == "palette-owned-children") {
    // SvgIcon uses an ordinary lv_obj with the default theme's card style.
    // Keep that real style: stripping it would hide the production regression.
    auto make_icon = [](lv_obj_t *button) {
      auto *icon = lv_obj_create(button);
      lv_obj_set_pos(icon, 8, 4);
      lv_obj_set_size(icon, 24, 24);
      lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, LV_PART_MAIN);
      lv_obj_set_style_border_width(icon, 0, LV_PART_MAIN);
      lv_obj_set_style_pad_all(icon, 0, LV_PART_MAIN);
      lv_obj_remove_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
      return icon;
    };
    auto *neutral_icon = make_icon(f.actions[2]);
    auto *active_icon = make_icon(f.actions[1]);
    auto *nested = make_icon(neutral_icon);
    const auto nested_color = lv_obj_get_style_text_color(nested, LV_PART_MAIN);
    const auto unrelated_color = lv_obj_get_style_text_color(f.icons[0], LV_PART_MAIN);
    assert(!lv_color_eq(lv_obj_get_style_text_color(neutral_icon, LV_PART_MAIN), lv_color_hex(0xF5F7FA)));
    for (auto *obj : {f.widgets.on, f.widgets.off, f.widgets.on_label, f.widgets.off_label}) {
      lv_obj_set_style_bg_color(obj, lv_color_hex(0x512244), LV_STATE_DISABLED | LV_STATE_PRESSED);
      lv_obj_set_style_text_color(obj, lv_color_hex(0x986244), LV_STATE_DISABLED | LV_STATE_PRESSED);
      lv_obj_add_state(obj, static_cast<lv_state_t>(LV_STATE_DISABLED | LV_STATE_PRESSED));
    }
    const auto palette = [&](uint32_t active_bg, uint32_t active_fg, uint32_t neutral_bg, uint32_t neutral_fg) {
      lv_obj_set_style_bg_color(f.widgets.on, lv_color_hex(active_bg), LV_PART_MAIN);
      lv_obj_set_style_text_color(f.widgets.on_label, lv_color_hex(active_fg), LV_PART_MAIN);
      lv_obj_set_style_bg_color(f.widgets.off, lv_color_hex(neutral_bg), LV_PART_MAIN);
      lv_obj_set_style_text_color(f.widgets.off_label, lv_color_hex(neutral_fg), LV_PART_MAIN);
      f.control.refresh();
      std::cout << "CHECK media-owned-foreground neutral\n";
      assert(lv_color_eq(lv_obj_get_style_text_color(neutral_icon, LV_PART_MAIN), lv_color_hex(neutral_fg)));
      assert(lv_color_eq(lv_obj_get_style_text_color(f.action_labels[2], LV_PART_MAIN), lv_color_hex(neutral_fg)));
      assert(lv_color_eq(lv_obj_get_style_bg_color(f.actions[2], LV_PART_MAIN), lv_color_hex(neutral_bg)));
      assert(lv_color_eq(lv_obj_get_style_text_color(active_icon, LV_PART_MAIN), lv_color_hex(active_fg)));
      assert(lv_color_eq(lv_obj_get_style_text_color(f.action_labels[1], LV_PART_MAIN), lv_color_hex(active_fg)));
      assert(lv_color_eq(lv_obj_get_style_bg_color(f.actions[1], LV_PART_MAIN), lv_color_hex(active_bg)));
      // The Media contract owns immediate content only, not nested/unrelated objects.
      assert(lv_color_eq(lv_obj_get_style_text_color(nested, LV_PART_MAIN), nested_color));
      assert(lv_color_eq(lv_obj_get_style_text_color(f.icons[0], LV_PART_MAIN), unrelated_color));
    };
    palette(0xB6E4FA, 0x071018, 0x1D2833, 0xF5F7FA);
    palette(0xFFF0D8, 0x251000, 0x102030, 0xE6F0FA);
    lv_refr_now(f.display.get());
    const auto flushes = f.lvgl.native_flushes;
    for (int i = 0; i < 5; i++) {
      f.control.refresh();
      lv_refr_now(f.display.get());
    }
    assert(f.lvgl.native_flushes == flushes);
  } else if (name == "retired") {
    f.control.disconnect();
    f.control.refresh();
    assert(lv_obj_is_visible(f.main[0]));
    assert(std::string(lv_label_get_text(f.main[4])) == "--");
    assert(lv_obj_has_state(f.actions[1], LV_STATE_DISABLED));
  } else if (name == "unchanged") {
    lv_refr_now(f.display.get());
    const auto n = f.lvgl.native_flushes;
    for (int i = 0; i < 5; i++) {
      f.control.refresh();
      lv_refr_now(f.display.get());
    }
    assert(f.lvgl.native_flushes == n);
  } else
    assert(false);
  std::cout << "RESULT " << name << " pass=1\n";
}
