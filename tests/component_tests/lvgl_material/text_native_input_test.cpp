#include "real_lvgl_test_support.h"
#include "material_entity_control_component.h"
#include "text_fixtures.h"
#include "core/lv_obj_private.h"
#include "core/lv_obj_style_private.h"
#include <iostream>
#include <map>

using namespace esphome::lvgl_material;
namespace rt = real_lvgl_test;
// Fixed product glyph coverage from the generated GF_Latin_Core Roboto fonts.
// Only advances are stand-ins: never make a missing non-ASCII glyph appear valid.
// The 18/20/24/30 px generated fonts share this exact 319-codepoint subset.
constexpr uint32_t PRODUCT_FONT_POINTS[] = {
    32,   33,   34,   35,   36,   37,   38,   39,   40,   41,   42,   43,   44,   45,   46,   47,   48,   49,   50,
    51,   52,   53,   54,   55,   56,   57,   58,   59,   60,   61,   62,   63,   64,   65,   66,   67,   68,   69,
    70,   71,   72,   73,   74,   75,   76,   77,   78,   79,   80,   81,   82,   83,   84,   85,   86,   87,   88,
    89,   90,   91,   92,   93,   94,   95,   96,   97,   98,   99,   100,  101,  102,  103,  104,  105,  106,  107,
    108,  109,  110,  111,  112,  113,  114,  115,  116,  117,  118,  119,  120,  121,  122,  123,  124,  125,  126,
    160,  161,  162,  163,  165,  167,  168,  169,  170,  171,  174,  175,  176,  180,  182,  183,  184,  186,  187,
    191,  192,  193,  194,  195,  196,  197,  198,  199,  200,  201,  202,  203,  204,  205,  206,  207,  208,  209,
    210,  211,  212,  213,  214,  215,  216,  217,  218,  219,  220,  221,  222,  223,  224,  225,  226,  227,  228,
    229,  230,  231,  232,  233,  234,  235,  236,  237,  238,  239,  240,  241,  242,  243,  244,  245,  246,  247,
    248,  249,  250,  251,  252,  253,  254,  255,  256,  257,  258,  259,  260,  261,  262,  263,  266,  267,  268,
    269,  270,  271,  272,  273,  274,  275,  278,  279,  280,  281,  282,  283,  286,  287,  288,  289,  290,  291,
    294,  295,  298,  299,  302,  303,  304,  305,  310,  311,  313,  314,  315,  316,  317,  318,  321,  322,  323,
    324,  325,  326,  327,  328,  336,  337,  338,  339,  340,  341,  344,  345,  346,  347,  350,  351,  352,  353,
    356,  357,  362,  363,  366,  367,  368,  369,  370,  371,  372,  373,  374,  375,  376,  377,  378,  379,  380,
    381,  382,  536,  537,  538,  539,  567,  710,  711,  728,  729,  730,  731,  732,  733,  768,  769,  770,  771,
    772,  774,  775,  776,  778,  779,  780,  806,  807,  808,  7808, 7809, 7810, 7811, 7812, 7813, 7838, 7922, 7923,
    8211, 8212, 8216, 8217, 8218, 8220, 8221, 8222, 8226, 8230, 8249, 8250, 8364, 8482, 8722};
const lv_font_t *host_font() {
  static lv_font_t font = *LV_FONT_DEFAULT;
  font.get_glyph_dsc = [](const lv_font_t *actual, lv_font_glyph_dsc_t *description, uint32_t point, uint32_t next) {
    if (!std::binary_search(std::begin(PRODUCT_FONT_POINTS), std::end(PRODUCT_FONT_POINTS), point))
      return false;
    return (LV_FONT_DEFAULT)->get_glyph_dsc(actual, description, point > 126 ? 'M' : point, next > 126 ? 'M' : next);
  };
  return &font;
}
struct Control : MaterialEntityControlComponent {
  using MaterialEntityControlComponent::MaterialEntityControlComponent;
  bool ready() const { return text_ready_; }
  bool captured() const { return captured_target_ != nullptr; }
  void refresh() { update_static_(); }
  uint16_t lines() const { return text_line_count_; }
  uint16_t first() const { return text_first_line_; }
  uint8_t map() const { return text_map_index_(); }
  bool pool_empty() const {
    return std::all_of(text_ui_storage_->visible.begin(), text_ui_storage_->visible.end(),
                       [](char c) { return c == 0; });
  }
};
struct Harness {
  esphome::lvgl::LvglComponent lvgl;
  rt::HostDisplay display{lvgl};
  rt::NativeWidgets widgets{lv_screen_active()};
  rt::Pointer pointer{display.get()};
  esphome::lvgl::LvglNavigation navigation;
  esphome::lvgl::LvglApplication application;
  Control control{&lvgl};
  std::map<std::string, lv_obj_t *> w;
  std::vector<std::string> wires;
  bool retire_allowed{false};
  unsigned retire_calls{0};
  Harness(const std::string &name) {
    navigation.active = &application;
    w["base"] = widgets.root;
#include "text_widgets.h"
    control.set_widgets(widgets.root, widgets.hit, widgets.title, widgets.value, widgets.unknown, widgets.on,
                        widgets.off);
    control.set_state_label(widgets.state);
    control.set_status_label(widgets.status);
    control.set_number_profile(&navigation, &application, widgets.on_label, widgets.off_label, LV_FONT_DEFAULT);
    control.set_text_preview_retire([this]() {
      retire_calls++;
      return retire_allowed;
    });
    control.set_text_enabled(true);
    control.set_initial_enabled(true);
    control.add_on_message_callback([this](const char *wire) { wires.emplace_back(wire); });
    esphome::text_fail_allocation = name == "allocation";
    control.setup();
    assert(!control.is_failed());
    assert(esphome::text_external_allocations == 2);
    rt::advance(200);
    assert(control.bind_entity_json(
        R"({"v":1,"type":"entity_control_v1","kind":"bind","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})"));
    ArduinoJson::JsonDocument response;
    const bool bound = control.bind_extra_features(
        R"({"v":1,"type":"entity_panel_extra_v1","channel":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","profiles":[{"name":"text","version":1}]})",
        response.to<ArduinoJson::JsonObject>());
    if (name == "allocation") {
      assert(!bound && response["profiles"].size() == 0);
      assert(control.open_number(30, "number-still-available", 400));
      return;
    }
    assert(bound && response["profiles"].size() == 1);
    assert(control.open_panel(29, "text-corpus", 400));
    send(name == "empty" ? "empty" : "public", false, [&](auto &doc) {
      if (name == "invalid") {
        doc["panel_state"]["value"] = "abc";
        doc["panel_caps"]["min"] = 2;
        doc["panel_caps"]["max"] = 4;
      }
      if (name == "wrap" || name == "cursor") {
        doc["panel_state"]["value"] = std::string(255, 'W');
        doc["panel_caps"]["max"] = 255;
        doc["panel_caps"]["can_set"] = name == "cursor";
        doc["eligible_actions"] = name == "cursor" ? 1 : 0;
      }
    });
    assert(control.resume_control_view(control.get_selection_generation()));
    rt::advance(300);
    assert(control.ready() && control.owner().is_text() && control.owner().is_view_ready());
    assert(lv_obj_is_visible(object("root")) && !lv_obj_is_visible(widgets.hit));
  }
  ~Harness() {
    // Firmware components outlive their LVGL tree. The stack-owned host fixture
    // must detach callbacks before its component and external sidecars die.
    control.disconnect();
    const auto detach = [&](auto &&self, lv_obj_t *object) -> void {
      lv_obj_remove_event_cb_with_user_data(object, nullptr, &control);
      for (uint32_t i = 0; i < lv_obj_get_child_count(object); i++)
        self(self, lv_obj_get_child(object, i));
    };
    detach(detach, widgets.root);
  }
  template<typename F> void send(const char *name, bool state, F &&change, bool expected = true) {
    ArduinoJson::JsonDocument doc;
    bool found = false;
    for (const auto &fixture : fixtures)
      if (std::strcmp(fixture.name, name) == 0) {
        assert(!ArduinoJson::deserializeJson(doc, fixture.wire));
        found = true;
      }
    assert(found);
    const auto &identity = control.owner().identity();
    doc["open_seq"] = identity.open_seq;
    doc["slot"] = identity.context.slot;
    doc["card_id"] = identity.context.card_id.bytes.data();
    doc["revision"] = identity.context.revision;
    if (state) {
      doc["kind"] = "state";
      doc["token"] = identity.token.bytes.data();
      doc["state_seq"] = control.owner().state().state_seq + 1;
    }
    change(doc);
    std::string wire;
    ArduinoJson::serializeJson(doc, wire);
    assert(control.receive_entity(wire) == expected);
    lv_refr_now(display.get());
  }
  lv_obj_t *object(const char *id) { return w.at(std::string("text_") + id); }
  std::string text(const char *id) { return lv_label_get_text(object(id)); }
  void press(const char *id) {
    lv_area_t bounds;
    lv_obj_get_coords(object(id), &bounds);
    pointer.press((bounds.x1 + bounds.x2) / 2, (bounds.y1 + bounds.y2) / 2);
  }
  void release() {
    pointer.release();
    control.loop();
    rt::advance(100);
  }
  void tap(const char *id) {
    press(id);
    release();
  }
  void key(uint8_t row, uint8_t index, bool release_now = true) {
    const std::string name = "keys_" + std::to_string(row);
    auto *matrix = object(name.c_str());
    lv_area_t area;
    lv_obj_get_coords(matrix, &area);
    const int count = row == 0 ? 10 : row == 1 ? 9 : 7;
    pointer.press(area.x1 + (lv_obj_get_width(matrix) / count) * index + lv_obj_get_width(matrix) / count / 2,
                  (area.y1 + area.y2) / 2);
    if (release_now)
      release();
  }
  size_t commands() const {
    size_t count = 0;
    for (const auto &wire : wires) {
      ArduinoJson::JsonDocument doc;
      assert(!ArduinoJson::deserializeJson(doc, wire));
      count += doc["kind"] == "command";
    }
    return count;
  }
  std::string value() const { return std::string(control.owner().text_draft().value); }
};

int main(int argc, char **argv) {
  assert(argc == 2);
  lv_init();
  lv_font_glyph_dsc_t absent{};
  assert(!lv_font_get_glyph_dsc(host_font(), &absent, 0xAC, 0) || absent.is_placeholder);
  for (const uint32_t point : {0xB7U, 0x2013U, 0x104U, 0x17CU}) {
    lv_font_glyph_dsc_t present{};
    assert(lv_font_get_glyph_dsc(host_font(), &present, point, 0) && !present.is_placeholder);
  }
  std::setbuf(stdout, nullptr);
  const std::string name = argv[1];
  Harness f(name);
  const auto original = f.value();
  if (name == "overview") {
    assert(f.text("count") == "11 / 100  \xC2\xB7  1"
                              "\xE2\x80\x93"
                              "1");
    assert(f.text("line_0") == "Hello Ąą Żż");
    assert(!lv_obj_is_visible(f.object("keys_0")) && !lv_obj_is_visible(f.object("cursor")));
    assert(f.text("save_label") == "Edit");
  } else if (name == "empty") {
    assert(f.text("line_0") == "Empty");
    f.tap("save");
    assert(f.value().empty() && lv_obj_is_visible(f.object("cursor")) && f.text("line_0").empty());
  } else if (name == "edit") {
    f.tap("save");
    assert(f.control.owner().text_draft().active);
    f.key(0, 0, false);
    assert(f.control.captured() && f.value() == original);
    f.release();
    assert(f.value() == original + "q" && f.commands() == 0);
    f.tap("save");
    assert(f.commands() == 1 && f.control.owner().is_command_pending());
    ArduinoJson::JsonDocument doc;
    assert(!ArduinoJson::deserializeJson(doc, f.wires.back()));
    assert(doc["value"].as<std::string>() == original + "q");
  } else if (name == "polish") {
    f.tap("save");
    f.tap("alternate");
    assert(f.control.map() == 4);
    f.key(0, 0);
    assert(f.value() == original + "ą" && f.control.map() == 0);
    f.tap("shift");
    f.tap("alternate");
    f.key(0, 0);
    assert(f.value() == original + "ąĄ");
    f.tap("backspace");
    assert(f.value() == original + "ą");
    f.tap("left");
    f.key(0, 0);
    assert(f.value() == original + "Qą");
  } else if (name == "invalid") {
    f.tap("save");
    f.key(0, 0);
    f.key(0, 0);
    assert(f.value() == "abcqq" && !f.control.owner().text_draft().valid);
    assert(lv_obj_has_state(f.object("save"), LV_STATE_DISABLED));
    f.tap("save");
    assert(f.commands() == 0);
    f.tap("backspace");
    f.tap("save");
    assert(f.commands() == 1);
  } else if (name == "wrap") {
    assert(f.control.lines() > 6 && lv_obj_has_state(f.object("save"), LV_STATE_DISABLED));
    std::string reviewed = f.text("line_0");
    while (!lv_obj_has_state(f.object("down"), LV_STATE_DISABLED)) {
      f.tap("down");
      reviewed += f.text("line_0");
    }
    for (int i = 1; i < 6; i++)
      reviewed += f.text(("line_" + std::to_string(i)).c_str());
    assert(reviewed == original && f.commands() == 0);
  } else if (name == "slide") {
    f.tap("save");
    f.key(0, 0, false);
    assert(f.control.captured());
    lv_area_t area;
    lv_obj_get_coords(f.object("keys_0"), &area);
    f.pointer.move(area.x1 + 95, (area.y1 + area.y2) / 2);
    f.release();
    assert(f.value() == original && !f.control.captured() && f.commands() == 0);
  } else if (name == "cancel") {
    f.tap("save");
    f.key(0, 0);
    f.tap("cancel");
    assert(f.value() == original && !f.control.owner().text_draft().active && f.commands() == 0);
  } else if (name == "semantic" || name == "metadata") {
    f.tap("save");
    f.key(0, 0);
    f.press("save");
    assert(f.control.captured());
    f.send("public", true, [&](auto &doc) {
      if (name == "semantic") {
        doc["panel_state"]["value"] = "Changed";
        doc["state_generation"] = 2;
      } else
        doc["title"] = "Renamed";
    });
    f.release();
    assert(f.commands() == (name == "metadata" ? 1U : 0U));
    if (name == "semantic")
      assert(f.value() == "Changed" && !f.control.owner().text_draft().active);
  } else if (name == "privacy") {
    f.tap("save");
    f.key(0, 0);
    f.control.invalidate(ControlReason::CONTROL_REASON_EXPIRED);
    assert(f.value().empty() && f.control.pool_empty() && f.control.text_preview_pending());
    for (int i = 0; i < 6; i++)
      assert(f.text(("line_" + std::to_string(i)).c_str()).empty());
    assert(!f.control.open(19, "another", 400));
    f.control.loop();
    assert(f.control.text_preview_pending() && f.retire_calls > 0);
    f.retire_allowed = true;
    f.control.loop();
    assert(!f.control.text_preview_pending());
    assert(f.control.open(19, "another", 400));
  } else if (name == "password") {
    f.tap("save");
    f.key(0, 0);
    f.press("save");
    f.send("password", true, [&](auto &doc) { doc["state_generation"] = 2; }, false);
    f.release();
    assert(f.value().empty() && f.control.pool_empty() && f.control.text_preview_pending() && f.commands() == 0);
    for (int i = 0; i < 6; i++)
      assert(f.text(("line_" + std::to_string(i)).c_str()).find("Hello") == std::string::npos);
    f.retire_allowed = true;
    f.control.loop();
    assert(!f.control.text_preview_pending());
  } else if (name == "unchanged") {
    lv_refr_now(f.display.get());
    const auto flushes = f.lvgl.native_flushes;
    for (int i = 0; i < 4; i++) {
      f.control.refresh();
      lv_refr_now(f.display.get());
    }
    assert(f.lvgl.native_flushes == flushes && f.commands() == 0);
    for (int i = 0; i < 6; i++)
      lv_obj_set_style_text_letter_space(f.object(("line_" + std::to_string(i)).c_str()), 3, LV_PART_MAIN);
    f.control.refresh();
    lv_refr_now(f.display.get());
    assert(f.lvgl.native_flushes > flushes);
    const auto changed_flushes = f.lvgl.native_flushes;
    f.control.refresh();
    lv_refr_now(f.display.get());
    assert(f.lvgl.native_flushes == changed_flushes);

  } else if (name == "cursor") {
    f.tap("save");
    assert(f.control.first() > 0);
    const auto cursor = f.control.owner().text_draft().cursor;
    f.tap("up");
    assert(f.control.owner().text_draft().cursor < cursor);
    lv_area_t bounds;
    lv_obj_get_coords(f.object("viewport"), &bounds);
    f.pointer.press(bounds.x1 + 1, bounds.y1 + 1);
    f.release();
    const auto before = f.control.owner().text_draft().cursor;
    f.tap("backspace");
    assert(f.control.owner().text_draft().cursor + 1 == before);
    assert(f.value().size() == 254 && f.commands() == 0);
  } else if (name == "palette") {
    lv_obj_set_style_bg_color(f.widgets.on, lv_color_hex(0xDDEEFF), LV_PART_MAIN);
    lv_obj_set_style_text_color(f.widgets.on_label, lv_color_hex(0x112233), LV_PART_MAIN);
    lv_obj_set_style_bg_color(f.widgets.off, lv_color_hex(0x223344), LV_PART_MAIN);
    lv_obj_set_style_text_color(f.widgets.off_label, lv_color_hex(0xF0E0D0), LV_PART_MAIN);
    f.control.refresh();
    f.tap("save");
    assert(lv_obj_has_state(f.object("save"), LV_STATE_DISABLED));
    assert(lv_color_eq(lv_obj_get_style_bg_color(f.object("save"), LV_PART_MAIN), lv_color_hex(0x223344)));
    f.key(0, 0);
    assert(lv_color_eq(lv_obj_get_style_bg_color(f.object("save"), LV_PART_MAIN), lv_color_hex(0xDDEEFF)));
    assert(lv_color_eq(lv_obj_get_style_text_color(f.object("save_label"), LV_PART_MAIN), lv_color_hex(0x112233)));
    assert(lv_color_eq(lv_obj_get_style_text_color(f.object("backspace_icon"), LV_PART_MAIN), lv_color_hex(0xF0E0D0)));
    const auto draft = f.value();
    auto *matrix = f.object("keys_0");
    lv_obj_add_state(matrix, LV_STATE_DISABLED);
    f.key(0, 0);
    assert(f.value() == draft && f.commands() == 0);
    lv_obj_remove_state(matrix, LV_STATE_DISABLED);

    const lv_style_t *shared = nullptr;
    for (const char *id : {"keys_0", "keys_1", "keys_2", "up", "down", "shift", "backspace", "symbols",
                           "alternate", "left", "space", "right", "cancel", "save"}) {
      auto *object = f.object(id);
      const bool was_disabled = lv_obj_has_state(object, LV_STATE_DISABLED);
      const auto background = lv_obj_get_style_bg_color(object, LV_PART_MAIN);
      const auto foreground = lv_obj_get_style_text_color(object, LV_PART_MAIN);
      lv_obj_add_state(object, LV_STATE_DISABLED);
      rt::advance(100);
      assert(lv_obj_get_style_recolor_opa(object, LV_PART_MAIN) == LV_OPA_TRANSP);
      assert(lv_obj_get_style_bg_opa(object, LV_PART_MAIN) == LV_OPA_COVER);
      assert(lv_obj_get_style_text_opa(object, LV_PART_MAIN) == LV_OPA_80);
      assert(lv_color_eq(lv_obj_get_style_bg_color(object, LV_PART_MAIN), background));
      assert(lv_color_eq(lv_obj_get_style_text_color(object, LV_PART_MAIN), foreground));
      lv_style_value_t local{};
      for (const auto property : {LV_STYLE_RECOLOR_OPA, LV_STYLE_BG_OPA, LV_STYLE_TEXT_OPA})
        assert(lv_obj_get_local_style_prop(object, property, &local, static_cast<lv_style_selector_t>(LV_PART_MAIN) | LV_STATE_DISABLED) ==
               LV_STYLE_RES_NOT_FOUND);
      unsigned matching = 0;
      for (unsigned i = 0; i < object->style_cnt; i++) {
        const auto &entry = object->styles[i];
        if (entry.selector != (static_cast<lv_style_selector_t>(LV_PART_MAIN) | LV_STATE_DISABLED) || entry.is_theme)
          continue;
        assert(!entry.is_local && !entry.is_trans && !entry.is_disabled && entry.style->prop_cnt == 255);
        if (!shared)
          shared = entry.style;
        assert(entry.style == shared);
        matching++;
      }
      assert(matching == 1);
      if (!was_disabled)
        lv_obj_remove_state(object, LV_STATE_DISABLED);
    }
    assert(shared != nullptr);
  } else if (name == "unfit") {
    lv_obj_set_width(f.object("title"), 2);
    f.control.refresh();
    assert(!f.control.ready());
    f.tap("save");
    assert(!f.control.owner().text_draft().active && f.commands() == 0);
  } else
    assert(name == "allocation");
  assert(esphome::text_external_allocations == 2);
  std::cout << "RESULT " << name << " pass=1\n";
}
