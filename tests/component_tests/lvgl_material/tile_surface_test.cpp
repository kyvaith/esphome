// Narrow LVGL fakes; all payload validation and gesture policy is production code.
#include <ArduinoJson.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#define ESP_LOGE(...) (void) 0
#define ESP_LOGW(...) (void) 0
#define ESP_LOGCONFIG(...) (void) 0
template<typename T> struct PayloadValue {
  T stored{};
  bool present{false};
  template<typename... Ts> T value(Ts...) { return this->stored; }
  bool has_value() const { return this->present; }
  void set(T value) {
    this->stored = std::move(value);
    this->present = true;
  }
};
#define TEMPLATABLE_VALUE(type, name) \
 protected: \
  PayloadValue<type> name##_; \
\
 public: \
  void set_##name(type value) { this->name##_.set(std::move(value)); }
namespace esphome {
namespace setup_priority {
static constexpr float PROCESSOR = 100;
}
class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void dump_config() {}
  virtual float get_setup_priority() const { return 0; }
  void mark_failed() { this->failed_ = true; }
  bool is_failed() const { return this->failed_; }

 protected:
  bool failed_{false};
};
template<typename... Ts> class Action {
 protected:
  virtual void play(const Ts &...) {}
};
template<typename T> class LazyCallbackManager;
template<typename... Ts> class LazyCallbackManager<void(Ts...)> {
 public:
  template<typename F> void add(F &&f) { this->callbacks_.emplace_back(std::forward<F>(f)); }
  void call(Ts... values) {
    for (auto &callback : this->callbacks_)
      callback(values...);
  }

 private:
  std::vector<std::function<void(Ts...)>> callbacks_;
};
namespace json {
ArduinoJson::Allocator *heap_json_allocator() { return ArduinoJson::detail::DefaultAllocator::instance(); }
class JsonBuilder {
 public:
  JsonObject root() { return this->document_.to<JsonObject>(); }

 private:
  JsonDocument document_;
};
bool parse_json(const std::string &source, const std::function<bool(JsonObject)> &callback) {
  JsonDocument document;
  if (deserializeJson(document, source) || !document.is<JsonObject>())
    return false;
  return callback(document.as<JsonObject>());
}
}  // namespace json
}  // namespace esphome
using lv_color_t = uint32_t;
static constexpr int LV_PART_MAIN = 0;
static constexpr int LV_OBJ_FLAG_HIDDEN = 1;
static constexpr int LV_EVENT_ALL = 0;
static constexpr int LV_EVENT_PRESSED = 1;
static constexpr int LV_EVENT_CLICKED = 2;
static constexpr int LV_EVENT_PRESS_LOST = 3;
struct lv_obj_t {
  bool hidden{false};
  uint32_t background{0};
  uint32_t foreground{0};
  std::string text;
};
struct lv_event_t {
  int code;
  void *data;
};
static void lv_obj_add_flag(lv_obj_t *obj, int) { obj->hidden = true; }
static void lv_obj_remove_flag(lv_obj_t *obj, int) { obj->hidden = false; }
static bool lv_obj_has_flag(lv_obj_t *obj, int) { return obj->hidden; }
static void lv_obj_add_event_cb(lv_obj_t *, void (*)(lv_event_t *), int, void *) {}
static unsigned invalidations = 0, style_writes = 0, label_writes = 0;
static void lv_obj_invalidate(lv_obj_t *) { invalidations++; }
static const char *lv_label_get_text(lv_obj_t *obj) { return obj->text.c_str(); }
static void lv_label_set_text(lv_obj_t *obj, const char *text) {
  obj->text = text;
  label_writes++;
}
static uint32_t lv_obj_get_style_bg_color(lv_obj_t *obj, int) { return obj->background; }
static uint32_t lv_obj_get_style_text_color(lv_obj_t *obj, int) { return obj->foreground; }
static void lv_obj_set_style_bg_color(lv_obj_t *obj, uint32_t color, int) {
  obj->background = color;
  style_writes++;
}
static void lv_obj_set_style_text_color(lv_obj_t *obj, uint32_t color, int) {
  obj->foreground = color;
  style_writes++;
}
static lv_color_t lv_color_hex(uint32_t color) { return color; }
static uint32_t lv_color_to_u32(uint32_t color) { return color; }
static bool lv_color_eq(uint32_t left, uint32_t right) { return left == right; }
static void *lv_event_get_user_data(lv_event_t *event) { return event->data; }
static int lv_event_get_code(lv_event_t *event) { return event->code; }

#include "material_tile_surface.h.inc"
#include "material_tile_surface.cpp.inc"

class Surface : public esphome::lvgl_material::MaterialTileSurface {
 public:
  static size_t binding_size() { return sizeof(TileBinding); }
  void event(unsigned index, int code) {
    lv_event_t event{code, &this->tiles_.at(index)};
    tile_event_cb_(&event);
  }
};

class ConfigureAction : public esphome::lvgl_material::MaterialTileSurfaceConfigureAction<> {
 public:
  using MaterialTileSurfaceConfigureAction::MaterialTileSurfaceConfigureAction;
  using MaterialTileSurfaceConfigureAction::play;
};

// Production HA wire shape, with sixteen bindings and two reserved hidden
// anchors. Synthetic values only; not a captured live HA payload.
static std::string full_payload(uint32_t accent, int changed_slot = -1) {
  JsonDocument document;
  document["version"] = 1;
  document["revision"] = 7;
  document["profile"] = "echo-hub-m3-v1";
  document["theme"]["accent"] = accent;
  auto cards = document["cards"].to<JsonArray>();
  static const char *palettes[] = {"primary", "secondary", "tertiary", "surface", "primary_container"};
  for (int slot = 0; slot < 16; slot++) {
    auto card = cards.add<JsonObject>();
    card["slot"] = slot;
    card["id"] = "card-" + std::to_string(slot);
    card["visible"] = slot >= 2;
    card["icon"] = "*";
    card["title"] = "Card " + std::to_string(slot);
    card["subtitle"] = slot == changed_slot ? "2026-10-24T15:59:59+00:00" : "Ready";
    card["palette"] = palettes[slot % 5];
    card["background"] = accent;
    card["foreground"] = 0x071018;
  }
  std::string output;
  serializeJson(document, output);
  return output;
}

int main(int argc, char **argv) {
  assert(argc == 2 || argc == 3);
  const int test = std::atoi(argv[1]);
  Surface surface;
  lv_obj_t button, icon, title, subtitle, hidden_button, hidden_icon, hidden_title, hidden_subtitle;
  surface.add_tile(2, &button, &icon, &title, &subtitle);
  surface.add_tile(0, &hidden_button, &hidden_icon, &hidden_title, &hidden_subtitle, true);
  surface.set_require_snapshot_ready(test != 0 && test != 1 && test != 29 && test != 30);
  surface.setup();
  int legacy = 0, contextual = 0, controls = 0;
  std::string clicked_id;
  uint64_t clicked_revision = 0;
  surface.add_on_press_callback([&](uint8_t) { legacy++; });
  surface.add_on_context_press_callback([&](uint8_t slot, const char *id, uint64_t revision) {
    assert(slot == 2);
    contextual++;
    clicked_id = id;
    clicked_revision = revision;
  });
  surface.add_on_control_press_callback([&](uint8_t slot, const char *id, uint64_t revision) {
    assert(slot == 2);
    controls++;
    clicked_id = id;
    clicked_revision = revision;
  });
  const std::string payload =
      R"({"version":1,"revision":7,"cards":[{"slot":2,"id":"card-2","title":"Lamp","subtitle":"On","icon":"*","visible":true,"background":123,"foreground":456}]})";
  const std::string control_payload =
      R"({"version":1,"revision":7,"cards":[{"slot":2,"id":"card-2","control_kind":"light","title":"Lamp","subtitle":"On","icon":"*","visible":true,"background":123,"foreground":456}]})";
  const std::string legacy_payload =
      R"({"version":1,"cards":[{"slot":2,"title":"Legacy","background":123,"foreground":456}]})";
  auto click = [&]() {
    surface.event(0, LV_EVENT_PRESSED);
    surface.event(0, LV_EVENT_CLICKED);
  };
  if (test == 0) {
    assert(surface.configure(legacy_payload));
    click();
    assert(legacy == 1 && contextual == 0);
  } else if (test == 1) {
    assert(surface.configure(payload));
    click();
    assert(legacy == 0 && contextual == 1 && clicked_id == "card-2" && clicked_revision == 7);
  } else if (test == 2) {
    assert(surface.configure(payload));
    click();
    assert(contextual == 0);
    assert(surface.get_pending_slots() == 4);
    surface.mark_slots_ready(4);
    click();
    assert(contextual == 1);
  } else if (test == 3) {
    assert(surface.configure(payload));
    surface.mark_slots_ready(4);
    surface.event(0, LV_EVENT_PRESSED);
    auto changed = payload;
    changed.replace(changed.find("card-2"), 6, "card-3");
    assert(surface.configure(changed));
    surface.mark_slots_ready(4);
    surface.event(0, LV_EVENT_CLICKED);
    assert(contextual == 0);
  } else if (test == 4) {
    assert(surface.configure(payload));
    surface.mark_slots_ready(4);
    surface.event(0, LV_EVENT_PRESSED);
    assert(surface.configure(payload));
    surface.event(0, LV_EVENT_CLICKED);
    assert(contextual == 1);
  } else if (test == 5) {
    assert(surface.configure(payload));
    surface.mark_slots_ready(4);
    surface.event(0, LV_EVENT_PRESSED);
    surface.set_accent(0x123456);
    surface.event(0, LV_EVENT_CLICKED);
    assert(contextual == 1 && legacy == 0);
    assert(surface.get_pending_slots() == 4 && surface.get_pending_context_slots() == 0);
  } else if (test == 6) {
    assert(surface.configure(payload));
    surface.mark_slots_ready(4);
    surface.event(0, LV_EVENT_PRESSED);
    surface.event(0, LV_EVENT_PRESS_LOST);
    surface.event(0, LV_EVENT_CLICKED);
    assert(contextual == 0);
  } else if (test == 7) {
    assert(surface.configure(payload));
    surface.mark_slots_ready(4);
    surface.event(0, LV_EVENT_CLICKED);
    assert(contextual == 0);
  } else if (test == 8) {
    assert(surface.configure(
        R"({"version":1,"revision":7,"cards":[{"slot":0,"id":"reserved","visible":true},{"slot":2,"id":"card","visible":false}]})"));
    assert(surface.get_pending_slots() == 0);
    click();
    surface.event(1, LV_EVENT_PRESSED);
    surface.event(1, LV_EVENT_CLICKED);
    assert(contextual == 0 && hidden_button.hidden);
  } else if (test == 9) {
    assert(surface.configure(payload));
    std::string before, after;
    assert(surface.serialize_presentation(before));
    assert(
        !surface.configure(R"({"version":1,"theme":{"accent":55},"cards":[{"slot":2,"title":"Changed"},{"slot":3}]})"));
    assert(surface.serialize_presentation(after));
    assert(before == after && title.text == "Lamp");
  } else if (test >= 10 && test <= 19) {
    const std::array<std::string, 10> invalid = {R"({"version":true,"cards":[]})",
                                                 R"({"version":1.0,"cards":[]})",
                                                 R"({"version":1,"revision":true,"cards":[]})",
                                                 R"({"version":1,"revision":9007199254740992,"cards":[]})",
                                                 R"({"version":1,"cards":[{"slot":2},{"slot":2}]})",
                                                 R"({"version":1,"cards":[{"slot":2,"visible":1}]})",
                                                 R"({"version":1,"cards":[{"slot":2,"title":null}]})",
                                                 R"({"version":1,"cards":[{"slot":2,"background":16777216}]})",
                                                 R"({"version":1,"revision":7,"cards":[{"slot":2,"id":"bad/id"}]})",
                                                 R"({"version":1,"cards":[{"slot":2,"id":"card"}]})"};
    assert(!surface.configure(invalid[test - 10]));
    assert(title.text.empty());
  } else if (test == 20) {
    assert(surface.configure(payload));
    std::string output;
    assert(!surface.serialize_presentation(output, 4));
    assert(output.empty());
    assert(surface.serialize_presentation(output));
    assert(surface.configure(output));
    assert(button.background == 123 && title.foreground == 456);
  } else if (test == 21) {
    assert(!surface.configure(std::string(32769, ' ')));
    const std::string bad =
        std::string(R"({"version":1,"cards":[{"slot":2,"title":")") + char(0xC0) + char(0xAF) + R"("}]})";
    assert(!surface.configure(bad));
  } else if (test == 22) {
    assert(surface.configure(
        R"({"version":1,"revision":8,"future":{"ignored":true},"cards":[{"slot":2,"title":"\u017b\u00f3\u0142\u0107"}]})"));
    assert(surface.get_context_version() == 0);
    surface.mark_slots_ready(4);
    click();
    assert(legacy == 1);
  } else if (test == 23) {
    assert(surface.configure(R"({"version":1,"revision":8,"cards":[{"slot":2,"id":"","title":"Vacant"}]})"));
    surface.mark_slots_ready(4);
    click();
    assert(legacy == 0 && contextual == 0);
  } else if (test == 24) {
    const std::string long_id(65, 'a');
    auto changed = payload;
    changed.replace(changed.find("card-2"), 6, long_id);
    assert(!surface.configure(changed));
    const std::string long_title(161, 'a');
    changed = payload;
    changed.replace(changed.find("Lamp"), 4, long_title);
    assert(!surface.configure(changed));
  } else if (test == 25) {
    assert(surface.configure(payload));
    surface.mark_slots_ready(4);
    surface.event(0, LV_EVENT_PRESSED);
    auto changed = payload;
    changed.replace(changed.find("revision\":7"), 11, "revision\":8");
    assert(surface.configure(changed));
    assert(surface.get_pending_slots() == 4);
    surface.mark_slots_ready(4);
    surface.event(0, LV_EVENT_CLICKED);
    assert(contextual == 0);
    assert(surface.get_configured_revision() == 8);
  } else if (test == 26) {
    assert(surface.configure(payload));
    surface.mark_slots_ready(4);
    surface.event(0, LV_EVENT_PRESSED);
    assert(surface.configure(R"({"version":1,"revision":7,"cards":[{"slot":2,"id":"card-2","visible":false}]})"));
    surface.mark_slots_ready(4);
    surface.event(0, LV_EVENT_CLICKED);
    assert(contextual == 0);
    assert(surface.get_pending_slots() == 0);
  } else if (test == 27) {
    assert(surface.configure(payload));
    std::string before, after;
    assert(surface.serialize_presentation(before));
    auto invalid = std::string(R"({"version":1,"cards":[)");
    for (int i = 0; i < 33; i++) {
      if (i)
        invalid += ',';
      invalid += R"({"slot":2})";
    }
    invalid += "]}";
    assert(!surface.configure(invalid));
    assert(surface.serialize_presentation(after));
    assert(before == after);
  } else if (test == 28) {
    assert(surface.configure(payload));
    assert(surface.configure(R"({"version":1,"revision":8,"cards":[{"slot":0,"id":"","visible":false}]})"));
    std::string output;
    assert(!surface.serialize_presentation(output));
    assert(output.empty());
  } else if (test == 29) {
    assert(surface.configure(legacy_payload));
    surface.event(0, LV_EVENT_CLICKED);
    assert(legacy == 1);
    surface.event(0, LV_EVENT_PRESSED);
    surface.set_accent(55);
    surface.event(0, LV_EVENT_CLICKED);
    assert(legacy == 2);
  } else if (test == 30) {
    assert(surface.configure(R"({"version":1,"cards":[{"slot":255,"title":"Unbound"},{"slot":2,"title":"Legacy"}]})"));
    surface.event(0, LV_EVENT_CLICKED);
    assert(legacy == 1 && title.text == "Legacy");
  } else if (test == 31) {
    assert(!surface.configure(R"({"version":1,"revision":3,"cards":[{"slot":255},{"slot":2,"id":"known"}]})"));
    assert(title.text.empty());
  } else if (test == 32) {
    auto boundary = payload;
    boundary.replace(boundary.find("card-2"), 6, std::string(64, 'a'));
    boundary.replace(boundary.find("revision\":7"), 11, "revision\":9007199254740991");
    std::string unicode;
    for (int i = 0; i < 40; i++)
      unicode += "\xF0\x9F\x92\xA1";
    boundary.replace(boundary.find("Lamp"), 4, unicode);
    assert(surface.configure(boundary));
    surface.mark_slots_ready(4);
    click();
    assert(contextual == 1 && clicked_id.size() == 64 && clicked_revision == 9007199254740991ULL);
    std::string output;
    assert(surface.serialize_presentation(output));
    assert(surface.configure(output));
    std::printf("host binding bytes=%zu\n", Surface::binding_size());
  } else if (test == 33) {
    Surface full;
    std::array<lv_obj_t, 128> widgets;
    for (uint8_t i = 0; i < 32; i++)
      full.add_tile(i, &widgets[i * 4], &widgets[i * 4 + 1], &widgets[i * 4 + 2], &widgets[i * 4 + 3]);
    full.set_require_snapshot_ready(true);
    full.setup();
    std::string all = R"({"version":1,"revision":3,"cards":[)";
    for (unsigned i = 0; i < 32; i++) {
      if (i)
        all += ',';
      all += "{\"slot\":" + std::to_string(i) + ",\"id\":\"card-" + std::to_string(i) + "\"}";
    }
    all += "]}";
    assert(full.configure(all));
    assert(full.get_pending_slots() == 0xFFFFFFFFU);
    full.mark_slots_ready(0xFFFFFFFFU);
    assert(full.get_pending_slots() == 0);
    std::string output;
    assert(full.serialize_presentation(output));
    assert(full.configure(output));
  } else if (test == 34) {
    assert(surface.configure(payload));
    surface.mark_slots_ready(4);
    for (int i = 0; i < 20; i++) {
      auto update = payload;
      update.replace(update.find("On"), 2, std::to_string(i));
      surface.event(0, LV_EVENT_PRESSED);
      assert(surface.configure(update));
      surface.set_accent(0x123400 + i);
      assert(surface.get_pending_slots() == 4 && surface.get_pending_context_slots() == 0);
      surface.event(0, LV_EVENT_CLICKED);
      assert(contextual == i + 1);
    }
    assert(legacy == 0);
  } else if (test == 35) {
    assert(surface.configure(payload));
    surface.mark_slots_ready(4);
    auto changed = payload;
    changed.replace(changed.find("card-2"), 6, "card-3");
    assert(surface.configure(changed));
    assert(surface.get_pending_context_slots() == 4);
    click();
    assert(contextual == 0);
    surface.mark_slots_ready(4);
    click();
    assert(contextual == 1 && clicked_id == "card-3");
  } else if (test == 36) {
    assert(surface.configure(payload));
    surface.mark_slots_ready(4);
    surface.event(0, LV_EVENT_PRESSED);
    assert(surface.configure(R"({"version":1,"revision":7,"cards":[{"slot":2,"id":"card-2","visible":false}]})"));
    assert(surface.get_pending_context_slots() == 0);
    assert(surface.configure(payload));
    surface.event(0, LV_EVENT_CLICKED);
    assert(contextual == 0);
    assert(surface.get_pending_context_slots() == 4);
    click();
    assert(contextual == 0);
    surface.mark_slots_ready(4);
    click();
    assert(contextual == 1);
  } else if (test == 37) {
    assert(surface.configure(payload));
    auto update = payload;
    update.replace(update.find("On"), 2, "Off");
    assert(surface.configure(update));
    assert(surface.get_pending_context_slots() == 4);
    click();
    assert(contextual == 0);
    surface.mark_slots_ready(4);
    click();
    assert(contextual == 1);
  } else if (test == 38) {
    assert(surface.configure(payload));
    surface.mark_slots_ready(4);
    surface.event(0, LV_EVENT_PRESSED);
    assert(
        !surface.configure(R"({"version":1,"theme":{"accent":55},"cards":[{"slot":2,"title":"Changed"},{"slot":3}]})"));
    surface.event(0, LV_EVENT_CLICKED);
    assert(contextual == 1);
    assert(surface.get_pending_context_slots() == 0 && surface.get_pending_slots() == 0);
  } else if (test == 39) {
    assert(surface.configure(payload));
    surface.mark_slots_ready(4);
    surface.event(0, LV_EVENT_PRESSED);
    auto update = payload;
    update.replace(update.find("Lamp"), 4, "Lamp state name");
    update.replace(update.find("\"*\""), 3, "\"!\"");
    assert(surface.configure(update));
    surface.event(0, LV_EVENT_CLICKED);
    assert(contextual == 1 && surface.get_pending_slots() == 4 && surface.get_pending_context_slots() == 0);
  } else if (test == 40 || test == 41 || test == 46) {
    Surface full;
    std::array<lv_obj_t, 64> widgets;
    for (uint8_t slot = 0; slot < 16; slot++)
      full.add_tile(slot, &widgets[slot * 4], &widgets[slot * 4 + 1], &widgets[slot * 4 + 2], &widgets[slot * 4 + 3],
                    slot < 2);
    full.set_require_snapshot_ready(true);
    full.setup();
    const auto raw = full_payload(0x36BCF0);
    assert(full.configure(raw, 0xAEE2FF));
    full.mark_slots_ready(0xFFFF);
    std::string before, after;
    assert(full.serialize_presentation(before));
    invalidations = style_writes = label_writes = 0;
    assert(
        full.configure(test == 41 ? full_payload(0x36BCF0, 6) : (test == 46 ? full_payload(0xFF9900) : raw), 0xAEE2FF));
    assert(full.get_pending_context_slots() == 0);
    if (test == 41) {
      assert(full.get_last_changed_slots() == (1U << 6) && full.get_pending_slots() == (1U << 6));
      assert(style_writes == 0 && label_writes == 1 && invalidations == 1);
      assert(widgets[6 * 4 + 3].text == "2026-10-24T15:59:59+00:00");
    } else {
      assert(full.get_last_changed_slots() == 0 && full.get_pending_slots() == 0);
      assert(style_writes == 0 && label_writes == 0 && invalidations == 0);
      assert(full.serialize_presentation(after) && before == after);
    }
  } else if (test == 42 || test == 47 || test == 48) {
    assert(surface.configure(payload, 0xAEE2FF));
    surface.mark_slots_ready(4);
    surface.event(0, LV_EVENT_PRESSED);
    std::string before, after;
    assert(surface.serialize_presentation(before));
    const auto changed = surface.get_last_changed_slots();
    invalidations = style_writes = label_writes = 0;
    if (test == 42)
      assert(!surface.configure(
          R"({"version":1,"theme":{"accent":55},"cards":[{"slot":2,"title":"Changed"},{"slot":3}]})", 0x112233));
    else if (test == 47)
      assert(!surface.configure(R"({"version":1,"theme":{"accent":false},"cards":[{"slot":2,"title":"Changed"}]})",
                                0x112233));
    else
      assert(!surface.configure(payload, 0x1000000));
    assert(surface.serialize_presentation(after) && before == after);
    assert(surface.get_last_changed_slots() == changed && surface.get_pending_slots() == 0);
    assert(surface.get_pending_context_slots() == 0 && surface.get_configured_revision() == 7);
    assert(style_writes == 0 && label_writes == 0 && invalidations == 0);
    surface.event(0, LV_EVENT_CLICKED);
    assert(contextual == 1);
  } else if (test == 43) {
    const auto themed =
        R"({"version":1,"theme":{"accent":3587312},"cards":[{"slot":2,"palette":"primary","background":123,"foreground":456}]})";
    assert(surface.configure(themed));
    assert(button.background == 0x36BCF0 && title.foreground == 0x071018);
    surface.mark_slots_ready(4);
    assert(surface.configure(themed) && surface.get_last_changed_slots() == 0);
    assert(surface.configure(legacy_payload));
    assert(button.background == 123 && title.foreground == 456);
  } else if (test == 44 || test == 45) {
    const auto unthemed = R"({"version":1,"cards":[{"slot":2,"palette":"primary","background":123,"foreground":456}]})";
    const uint32_t accent = test == 44 ? 0xAEE2FF : 0;
    assert(surface.configure(unthemed, accent));
    assert(button.background == accent && title.foreground == (accent ? 0x071018U : 0xFFFFFFU));
    surface.mark_slots_ready(4);
    invalidations = style_writes = label_writes = 0;
    assert(surface.configure(unthemed, accent) && surface.get_last_changed_slots() == 0);
    assert(style_writes == 0 && label_writes == 0 && invalidations == 0);
  } else if (test == 49) {
    const auto themed = R"({"version":1,"theme":{"accent":3587312},"cards":[{"slot":2,"palette":"primary"}]})";
    ConfigureAction default_action(&surface);
    default_action.set_payload(themed);
    default_action.play();
    assert(button.background == 0x36BCF0);
    ConfigureAction override_action(&surface);
    override_action.set_payload(themed);
    override_action.set_accent_override(0);
    override_action.play();
    assert(button.background == 0 && title.foreground == 0xFFFFFF);
  } else if (test == 50 || test == 51) {
    assert(argc == 3);
    std::ifstream input(argv[2], std::ios::binary);
    assert(input.good());
    const std::string raw{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    Surface full;
    std::array<lv_obj_t, 64> widgets;
    for (uint8_t slot = 0; slot < 16; slot++)
      full.add_tile(slot, &widgets[slot * 4], &widgets[slot * 4 + 1], &widgets[slot * 4 + 2], &widgets[slot * 4 + 3],
                    slot < 2);
    full.set_require_snapshot_ready(true);
    full.setup();
    assert(full.configure(raw, 0xAEE2FF));
    full.mark_slots_ready(0xFFFF);
    std::string update = raw;
    if (test == 51) {
      JsonDocument changed;
      assert(!deserializeJson(changed, raw));
      changed["cards"][14]["subtitle"] = "44 d 10 h";
      update.clear();
      serializeJson(changed, update);
    }
    invalidations = style_writes = label_writes = 0;
    assert(full.configure(update, 0xAEE2FF));
    assert(full.get_pending_context_slots() == 0 && style_writes == 0);
    assert(full.get_last_changed_slots() == (test == 51 ? (1U << 14) : 0));
    assert(full.get_pending_slots() == full.get_last_changed_slots());
    assert(label_writes == (test == 51 ? 1U : 0U) && invalidations == label_writes);
  }
  if (test >= 52) {
    surface.set_control_press_enabled(test != 52);
    if (test == 54)
      surface.set_require_snapshot_ready(false);
    assert(surface.configure(control_payload));
    if (test == 52) {
      surface.mark_slots_ready(4);
      click();
      assert(controls == 0 && legacy == 0 && contextual == 0);
    } else if (test == 53 || test == 54) {
      click();
      assert(controls == 0);
      surface.mark_slots_ready(4);
      click();
      assert(controls == 1 && clicked_id == "card-2" && clicked_revision == 7);
      surface.event(0, LV_EVENT_CLICKED);
      assert(controls == 1 && legacy == 0 && contextual == 0);
    } else if (test == 55 || test == 56) {
      const bool add_marker = test == 55;
      assert(surface.configure(add_marker ? payload : control_payload));
      surface.mark_slots_ready(4);
      surface.event(0, LV_EVENT_PRESSED);
      assert(surface.configure(add_marker ? control_payload : payload));
      assert(surface.get_pending_context_slots() == 4);
      surface.mark_slots_ready(4);
      surface.event(0, LV_EVENT_CLICKED);
      assert(controls == 0 && contextual == 0 && legacy == 0);
      click();
      assert(controls == (add_marker ? 1 : 0) && contextual == (add_marker ? 0 : 1) && legacy == 0);
    } else if (test == 57 || test == 58 || test == 59) {
      surface.mark_slots_ready(4);
      surface.event(0, LV_EVENT_PRESSED);
      auto changed = control_payload;
      if (test == 57)
        changed.replace(changed.find("card-2"), 6, "card-3");
      else if (test == 58)
        changed.replace(changed.find("revision\":7"), 11, "revision\":8");
      else
        changed.replace(changed.find("visible\":true"), 13, "visible\":false");
      assert(surface.configure(changed));
      surface.mark_slots_ready(4);
      surface.event(0, LV_EVENT_CLICKED);
      assert(controls == 0 && contextual == 0 && legacy == 0);
    } else if (test == 60) {
      surface.mark_slots_ready(4);
      surface.event(0, LV_EVENT_PRESSED);
      surface.event(0, LV_EVENT_PRESS_LOST);
      surface.event(0, LV_EVENT_CLICKED);
      assert(controls == 0 && contextual == 0 && legacy == 0);
    } else if (test == 61) {
      surface.mark_slots_ready(4);
      surface.event(0, LV_EVENT_PRESSED);
      auto changed = control_payload;
      changed.replace(changed.find("subtitle\":\"On"), 13, "subtitle\":\"Off");
      assert(surface.configure(changed));
      surface.set_accent(0xFF9900);
      assert(surface.get_pending_slots() == 4 && surface.get_pending_context_slots() == 0);
      surface.event(0, LV_EVENT_CLICKED);
      assert(controls == 1 && contextual == 0 && legacy == 0);
    } else if (test == 62) {
      surface.mark_slots_ready(4);
      surface.event(0, LV_EVENT_CLICKED);
      assert(controls == 0 && contextual == 0 && legacy == 0);
    } else if (test == 63) {
      assert(surface.configure(payload));
      surface.mark_slots_ready(4);
      click();
      assert(controls == 0 && contextual == 1 && legacy == 0);
    } else if (test >= 64 && test <= 71) {
      const std::array<const char *, 8> invalid = {"\"fan\"", "1", "true", "null", "{}", "[]", "\"\"", "\"LIGHT\""};
      auto changed = control_payload;
      changed.replace(changed.find("\"light\""), 7, invalid[test - 64]);
      std::string before, after;
      assert(surface.serialize_presentation(before));
      invalidations = style_writes = label_writes = 0;
      assert(!surface.configure(changed, 0xFF9900));
      assert(surface.serialize_presentation(after));
      assert(before == after && invalidations == 0 && style_writes == 0 && label_writes == 0);
    } else if (test == 72) {
      surface.mark_slots_ready(4);
      surface.event(0, LV_EVENT_PRESSED);
      std::string before, after;
      assert(surface.serialize_presentation(before));
      invalidations = style_writes = label_writes = 0;
      assert(!surface.configure(
          R"({"version":1,"revision":8,"theme":{"accent":55},"cards":[{"slot":2,"id":"new-card","title":"Wrong"},{"slot":0,"id":"reserved","control_kind":false}]})",
          0));
      assert(surface.serialize_presentation(after));
      assert(before == after && invalidations == 0 && style_writes == 0 && label_writes == 0);
      assert(surface.get_pending_slots() == 0 && surface.get_pending_context_slots() == 0);
      surface.event(0, LV_EVENT_CLICKED);
      assert(controls == 1 && contextual == 0 && legacy == 0);
    } else if (test == 73) {
      for (const char *invalid : {R"({"version":1,"revision":7,"cards":[{"slot":2,"id":"","control_kind":"light"}]})",
                                  R"({"version":1,"revision":7,"cards":[{"slot":2,"control_kind":"light"}]})",
                                  R"({"version":1,"cards":[{"slot":2,"id":"card-2","control_kind":"light"}]})"})
        assert(!surface.configure(invalid));
    } else if (test == 74) {
      surface.mark_slots_ready(4);
      std::string exported;
      assert(surface.serialize_presentation(exported));
      assert(exported.find("\"control_kind\":\"light\"") != std::string::npos);
      invalidations = style_writes = label_writes = 0;
      assert(surface.configure(exported));
      assert(surface.get_pending_slots() == 0 && surface.get_pending_context_slots() == 0);
      assert(invalidations == 0 && style_writes == 0 && label_writes == 0);
      click();
      assert(controls == 1 && contextual == 0 && legacy == 0);
    } else if (test == 75) {
      surface.mark_slots_ready(4);
      surface.event(0, LV_EVENT_PRESSED);
      surface.set_control_press_enabled(false);
      surface.set_control_press_enabled(true);
      surface.event(0, LV_EVENT_CLICKED);
      assert(controls == 0 && contextual == 0 && legacy == 0);
      click();
      assert(controls == 1);
    } else if (test == 76) {
      Surface unsupported;
      lv_obj_t b, i, t, s;
      unsupported.add_tile(2, &b, &i, &t, &s);
      unsupported.set_control_press_enabled(true);
      unsupported.setup();
      unsupported.add_on_press_callback([&](uint8_t) { legacy++; });
      unsupported.add_on_context_press_callback([&](uint8_t, const char *, uint64_t) { contextual++; });
      assert(unsupported.configure(control_payload));
      unsupported.mark_slots_ready(4);
      unsupported.event(0, LV_EVENT_PRESSED);
      unsupported.event(0, LV_EVENT_CLICKED);
      assert(legacy == 0 && contextual == 0);  // No control callback never falls through.
    } else if (test == 77) {
      surface.mark_slots_ready(4);
      invalidations = style_writes = label_writes = 0;
      assert(surface.configure(control_payload));
      assert(surface.get_pending_slots() == 0 && surface.get_pending_context_slots() == 0);
      assert(invalidations == 0 && style_writes == 0 && label_writes == 0);
      click();
      assert(controls == 1 && contextual == 0 && legacy == 0);
    } else if (test == 78) {
      assert(!surface.configure(R"({"version":1,"cards":[{"slot":2,"control_kind":"light"}]})"));
      assert(surface.configure(legacy_payload));
      surface.mark_slots_ready(4);
      click();
      assert(legacy == 1 && contextual == 0 && controls == 0);
    } else if (test == 79) {
      surface.set_require_snapshot_ready(false);
      assert(!surface.configure(
          R"({"version":1,"revision":7,"cards":[{"slot":255,"id":"unknown","control_kind":"light"}]})"));
    } else if (test == 80) {
      surface.mark_slots_ready(4);
      surface.add_on_control_press_callback(
          [&](uint8_t, const char *, uint64_t) { assert(surface.configure(payload)); });
      click();
      assert(controls == 1 && contextual == 0 && legacy == 0);
    } else if (test == 81) {
      surface.mark_slots_ready(4);
      assert(surface.configure(R"({"version":1,"revision":8,"cards":[{"slot":0,"id":"reserved"}]})"));
      click();
      assert(controls == 0 && contextual == 0 && legacy == 0);
      std::string mixed;
      assert(!surface.serialize_presentation(mixed));
    } else if (test == 82) {
      assert(surface.configure(
          R"({"version":1,"revision":7,"cards":[{"slot":0,"id":"reserved","control_kind":"light","visible":true}]})"));
      surface.mark_slots_ready(4);
      surface.event(1, LV_EVENT_PRESSED);
      surface.event(1, LV_EVENT_CLICKED);
      assert(hidden_button.hidden && controls == 0 && contextual == 0 && legacy == 0);
    }
  }
  std::printf("case %d PASS\n", test);
}
