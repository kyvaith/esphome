#include "esphome/components/lvgl/lvgl_esphome.h"
#include "material_notification_center.h"
#include "material_cached_dim.h"
#include "esphome/components/json/json_util.h"
#include <iostream>
#include "esp_cache.h"

using namespace esphome;
using namespace esphome::lvgl_material;
namespace rt = real_lvgl_test;

struct Scene : lvgl::DirectSceneController {
  bool paused{}, allow{true};
  unsigned resumes{};
  bool suspend_for_direct_overlay() override {
    if (!allow)
      return false;
    paused = true;
    return true;
  }
  void resume_after_direct_overlay() override {
    paused = false;
    ++resumes;
  }
};

struct Fixture {
  lvgl::LvglComponent lvgl;
  rt::HostDisplay display{lvgl};
  lvgl::LvglNavigation navigation;
  MaterialNotificationOverlay banner{&lvgl};
  MaterialNotificationCenter center{&lvgl, &navigation, &banner};
  Scene scene;
  lv_obj_t *banner_root{lv_obj_create(lv_layer_top())};
  lv_obj_t *panel{lv_obj_create(banner_root)};
  lv_obj_t *root{lv_obj_create(lv_layer_top())};
  lv_obj_t *heading{lv_label_create(root)};
  lv_obj_t *status{lv_label_create(root)};
  lv_obj_t *detail_view{lv_obj_create(root)};
  lv_obj_t *detail{lv_label_create(detail_view)};
  lv_obj_t *close{lv_obj_create(root)};
  lv_obj_t *previous{lv_obj_create(root)};
  lv_obj_t *next{lv_obj_create(root)};
  lv_obj_t *rows[8]{};
  lv_obj_t *ages[8]{};
  lv_obj_t *list{};
  uint32_t now{200120};
  std::vector<std::string> requests;
  std::vector<uint8_t> original;

  explicit Fixture(bool modern = false, bool row_cache = false) {
    lv_obj_set_size(banner_root, 800, 800);
    lv_obj_set_size(panel, 600, 264);
    lv_obj_set_pos(panel, 100, 96);
    banner.set_root(banner_root);
    banner.set_panel(panel);
    banner.set_icon(lv_label_create(panel));
    banner.set_title(lv_label_create(panel));
    banner.set_message(lv_label_create(panel));
    banner.set_from_top(true);
    banner.setup();
    lv_obj_set_size(root, 800, 800);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    center.set_root(root);
    center.set_heading(heading);
    center.set_status(status);
    center.set_detail(detail);
    center.set_detail_view(detail_view);
    center.set_close_button(close);
    center.set_previous_button(previous);
    center.set_next_button(next);
    lv_obj_set_pos(close, 600, 92);
    lv_obj_set_size(close, 56, 56);
    lv_obj_set_pos(previous, 230, 720);
    lv_obj_set_size(previous, 80, 48);
    lv_obj_set_pos(next, 490, 720);
    lv_obj_set_size(next, 80, 48);
    for (int index = 0; index < 8; index++) {
      rows[index] = lv_obj_create(root);
      lv_obj_set_pos(rows[index], 130, 154 + index * 64);
      lv_obj_set_size(rows[index], 540, 58);
      center.set_row(index, rows[index], lv_label_create(rows[index]), lv_label_create(rows[index]));
    }
    if (modern) {
      list = lv_obj_create(root);
      lv_obj_remove_style_all(list);
      lv_obj_set_pos(list, 100, 80);
      lv_obj_set_size(list, 600, 640);
      lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLLABLE);
      center.set_list_view(list);
      center.set_history_scroll(true);
      center.set_row_pitch(188);
      center.set_isolate_native_layers(true);
      center.set_close_edge_pixels(50);
      center.set_close_commit_pixels(36);
      center.set_close_button(nullptr);
      center.set_previous_button(nullptr);
      center.set_next_button(nullptr);
      for (int index = 0; index < 8; index++) {
        lv_obj_set_parent(rows[index], list);
        lv_obj_set_x(rows[index], 0);
        lv_obj_set_size(rows[index], 600, 172);
        ages[index] = lv_label_create(rows[index]);
        center.set_row_metadata(index, lv_label_create(rows[index]), ages[index]);
      }
      for (auto *button : {close, previous, next})
        lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
      center.set_now_provider([this]() { return now; });
    }
    if (row_cache) {
      center.set_row_snapshot_cache(true);
      center.set_row_cache_band_count(2);
      for (auto *row : rows) {
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 600, 172);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x214359), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(row, 44, LV_PART_MAIN);
      }
    }
    center.add_scene_controller(&scene);
    center.set_admission([]() { return true; });
    center.add_on_request_callback([this](std::string payload) { requests.push_back(payload); });
    center.setup();
    assert(!center.is_failed());
    lv_refr_now(display.get());
    std::fill(lvgl.presented_pixels.begin(), lvgl.presented_pixels.end(), 37);
    original = lvgl.presented_pixels;
    sync();
  }

  void window(int offset, int total, unsigned seq, bool detail = false, int id_shift = 0) {
    center.sync(json::build_json([&](JsonObject object) {
      object["v"] = 2;
      object["channel"] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
      object["seq"] = seq;
      object["offset"] = offset;
      object["total"] = total;
      object["language"] = "pl";
      auto rows = object["rows"].to<JsonArray>();
      for (int index = offset; index < std::min(offset + 8, total); index++) {
        char id[33];
        std::snprintf(id, sizeof(id), "%032x", index + 1 + id_shift);
        auto row = rows.add<JsonObject>();
        row["id"] = id;
        row["title"] = "Title";
        row["message"] = "Message";
        row["created"] = 200000;
      }
      if (detail) {
        auto body = object["detail"].to<JsonObject>();
        body["id"] = "00000000000000000000000000000001";
        body["title"] = "Title";
        body["message"] = "Full body";
        body["created"] = 200000;
      } else
        object["detail"] = nullptr;
    }));
  }

  JsonDocument status_json() {
    JsonDocument data;
    assert(!deserializeJson(data, center.status_json()));
    return data;
  }

  void acknowledge_windows(int total) {
    for (int step = 0; step < 4 && status_json()["pending"].as<bool>(); step++) {
      JsonDocument request;
      assert(!deserializeJson(request, requests.back()));
      assert(request["v"].as<int>() == 2 && std::strcmp(request["op"], "page") == 0);
      const int offset = std::clamp(request["offset"].as<int>(), 0, std::max(0, total - 8));
      window(offset, total, request["seq"].as<unsigned>());
    }
    assert(!status_json()["pending"].as<bool>());
  }

  void sync(const char *id = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", unsigned seq = 0,
            const char *channel = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") {
    center.sync(json::build_json([&](JsonObject object) {
      object["v"] = 1;
      object["channel"] = channel;
      object["seq"] = seq;
      object["offset"] = 0;
      object["total"] = 1;
      object["language"] = "pl";
      object["detail"] = nullptr;
      auto row = object["rows"].to<JsonArray>().add<JsonObject>();
      row["id"] = id;
      row["title"] = "Zażółć";
      row["message"] = "Test";
    }));
  }
  void begin() {
    assert(center.open());
    assert(scene.paused);
    lv_obj_update_layout(root);
  }
  void tap(int x, int y) {
    assert(center.touch_begin(x, y));
    assert(!center.touch_update(x, y));
    assert(center.touch_end());
  }
};

int main(int argc, char **argv) {
  assert(argc == 2);
  lv_init();
  const std::string scenario = argv[1];
  Fixture f(scenario.starts_with("modern_"), scenario.starts_with("modern_card_cache"));
  if (scenario == "scene") {
    f.begin();
    assert(!f.navigation.guard->prepare_navigation(lvgl::NavigationAdmissionKind::HOME, nullptr));
    assert(test_aligned_allocations.size() == 8);
    f.center.close();
    assert(!f.scene.paused && f.lvgl.pause_depth == 0 && !f.lvgl.modal_session);
    assert(test_aligned_allocations.empty());
    assert(f.lvgl.presented_pixels == f.original);
    assert(std::memcmp(f.display.get()->buf_1->data, f.original.data(), f.original.size()) == 0);
    assert(std::memcmp(f.display.get()->buf_2->data, f.original.data(), f.original.size()) == 0);
  } else if (scenario == "drain_failure") {
    f.scene.allow = false;
    assert(!f.center.open());
    assert(f.requests.empty());
    assert(test_aligned_allocations.empty() && f.lvgl.pause_depth == 0);
    f.scene.allow = true;
    f.lvgl.pause_start_ok = false;
    assert(!f.center.open());
    assert(!f.scene.paused && f.lvgl.pause_depth == 0 && test_aligned_allocations.empty());
  } else if (scenario == "end_failure") {
    f.begin();
    f.lvgl.modal_end_ok = false;
    f.center.close();
    assert(f.scene.paused && test_aligned_allocations.size() == 8 && f.lvgl.modal_session);
    f.lvgl.modal_end_ok = true;
    f.center.loop();
    assert(!f.scene.paused && test_aligned_allocations.empty() && f.lvgl.pause_depth == 0);
  } else if (scenario == "gestures") {
    assert(!f.center.touch_begin(400, 100));
    assert(f.center.touch_begin(400, 10));
    assert(!f.center.touch_update(400, 10));
    assert(!f.center.touch_end());  // stationary top tap
    assert(f.center.touch_begin(400, 10));
    assert(f.center.touch_update(400, 100));
    assert(f.center.is_open());
    assert(f.center.touch_end());
    f.sync("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", 1);
    f.tap(400, 180);
    assert(f.requests.back().find("\"op\":\"open\"") != std::string::npos);
    f.sync("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", 2);
    assert(f.center.touch_begin(400, 180));
    assert(f.center.touch_update(510, 182));
    assert(f.center.touch_end());
    assert(f.requests.back().find("\"op\":\"dismiss\"") != std::string::npos);
  } else if (scenario == "stale_row") {
    f.begin();
    f.sync("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", 1);
    assert(f.center.touch_begin(400, 180));
    f.sync("cccccccccccccccccccccccccccccccc", 1);
    assert(f.center.touch_end());
    assert(f.requests.size() == 1);  // no replacement tap
    f.center.touch_begin(400, 180);
    f.center.touch_update(510, 182);
    f.sync("dddddddddddddddddddddddddddddddd", 0, "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee");
    assert(f.center.touch_end());
    assert(f.requests.size() == 1);  // rebind cancels held contact
  } else if (scenario == "wire") {
    f.begin();
    auto old = f.requests.size();
    f.center.sync("{\"v\":1}");
    f.sync("invalid", 1);
    f.sync("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", 0);  // stale sequence cannot clear pending
    f.tap(400, 180);
    assert(f.requests.size() == old);
    f.sync("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", 1);
    f.tap(400, 180);
    assert(f.requests.size() == old + 1);
  } else if (scenario == "banner") {
    f.banner.show("Zażółć", "Test", "!", 6000);
    assert(f.banner.is_active());
    assert(f.scene.paused);
    assert(lv_obj_get_style_translate_y(f.panel, LV_PART_MAIN) < 0);
    f.banner.dismiss();
    rt::advance(350);
    f.banner.loop();
    assert(!f.banner.is_active() && !f.scene.paused && test_aligned_allocations.empty());
    assert(f.lvgl.pause_depth == 0 && f.lvgl.presented_pixels == f.original);
  } else if (scenario == "shared_attention") {
    assert(!f.center.owns_scene());
    f.banner.show("A", "B", "!", 6000);
    assert(f.banner.is_active() && test_aligned_allocations.size() == 8);
    assert(f.center.owns_scene());
    f.begin();
    assert(!f.banner.is_active() && test_aligned_allocations.size() == 8);
    f.banner.show("C", "D", "!", 6000);
    f.center.close();
    f.banner.loop();
    assert(!f.banner.is_active() && test_aligned_allocations.empty() && f.lvgl.pause_depth == 0);
    assert(!f.center.owns_scene());
  } else if (scenario == "fragmented") {
    test_max_aligned_bytes = 1792 * 1024;  // Reproduced native47 Voice largest block: 1824KiB.
    f.begin();
    assert(test_aligned_allocations.size() == 8);
    f.center.close();
    assert(test_aligned_allocations.empty() && !f.lvgl.modal_session);
    test_max_aligned_bytes = 239999;
    assert(!f.center.open());
    assert(test_aligned_allocations.empty() && f.lvgl.pause_depth == 0);
  } else if (scenario == "partial_restore") {
    f.begin();
    std::fill(f.lvgl.presented_pixels.begin(), f.lvgl.presented_pixels.end(), 99);
    f.lvgl.fail_copy_at = f.lvgl.modal_copies + 4;
    f.center.close();
    assert(f.lvgl.modal_presentations == 0 && f.lvgl.modal_releases == 1);
    assert(std::all_of(f.lvgl.presented_pixels.begin(), f.lvgl.presented_pixels.end(), [](auto x) { return x == 99; }));
    assert(test_aligned_allocations.size() == 8 && f.scene.paused);
    f.lvgl.fail_copy_at = 0;
    f.center.loop();
    assert(f.lvgl.modal_presentations == 1 && f.lvgl.presented_pixels == f.original);
    assert(test_aligned_allocations.empty() && !f.scene.paused);
  } else if (scenario == "geometry") {
    lvgl::LvglComponent smaller;
    rt::HostDisplay other_display{smaller, 320, 241};
    for (size_t index = 0; index < smaller.presented_pixels.size(); index++)
      smaller.presented_pixels[index] = static_cast<uint8_t>(index / (320 * 3));
    const auto pixels = smaller.presented_pixels;
    MaterialModalScene modal{&smaller};
    modal.setup();
    assert(modal.begin());
    std::fill(smaller.presented_pixels.begin(), smaller.presented_pixels.end(), 255);
    assert(modal.end() && smaller.presented_pixels == pixels && test_aligned_allocations.empty());
  } else if (scenario == "detail_view") {
    f.begin();
    assert(lv_obj_has_flag(f.detail_view, LV_OBJ_FLAG_HIDDEN));
    f.center.sync(json::build_json([](JsonObject object) {
      object["v"] = 1;
      object["channel"] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
      object["seq"] = 1;
      object["offset"] = 0;
      object["total"] = 1;
      object["language"] = "pl";
      auto row = object["rows"].to<JsonArray>().add<JsonObject>();
      row["id"] = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
      row["title"] = "Test";
      row["message"] = "Excerpt";
      auto detail = object["detail"].to<JsonObject>();
      detail["id"] = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
      detail["created"] = 1;
      detail["title"] = "Test";
      detail["message"] = "Full body";
    }));
    assert(!lv_obj_has_flag(f.detail_view, LV_OBJ_FLAG_HIDDEN));
    assert(!lv_obj_has_flag(f.detail, LV_OBJ_FLAG_HIDDEN));
    f.tap(628, 120);
    assert(lv_obj_has_flag(f.detail_view, LV_OBJ_FLAG_HIDDEN));
    assert(!lv_obj_has_flag(f.rows[0], LV_OBJ_FLAG_HIDDEN));
    f.center.close();
  } else if (scenario == "cached_dim_words") {
    for (unsigned seed = 0; seed < 256; seed++) {
      for (size_t length : {0U, 1U, 3U, 7U, 8U, 9U, 15U, 16U, 17U, 63U}) {
        for (size_t offset : {0U, 1U, 7U, 8U, 9U}) {
          std::vector<uint8_t> pixels(length + 2, 0xA5), expected;
          std::vector<uint8_t> bits((offset + length + 7) / 8 + 1), expected_bits(bits.size());
          for (size_t byte = 0; byte < length; byte++) {
            pixels[byte + 1] = (seed * 31 + byte * 43) & 255;
            expected_bits[(offset + byte) >> 3] |= (pixels[byte + 1] & 1U) << ((offset + byte) & 7U);
          }
          const auto original = pixels;
          expected = pixels;
          for (size_t byte = 1; byte <= length; byte++)
            expected[byte] >>= 1;
          const size_t split = length / 3;
          cached_dim_half(pixels.data() + 1, split, bits.data(), offset);
          cached_dim_half(pixels.data() + 1 + split, length - split, bits.data(), offset + split);
          assert(pixels == expected && bits == expected_bits);
          cached_dim_restore(pixels.data() + 1, split, bits.data(), offset);
          cached_dim_restore(pixels.data() + 1 + split, length - split, bits.data(), offset + split);
          assert(pixels == original);
        }
      }
    }
  } else if (scenario == "modern_cached_backdrop") {
    for (size_t index = 0; index < f.original.size(); index++)
      f.original[index] = index % 256;
    f.lvgl.presented_pixels = f.original;
    f.center.set_cached_backdrop_dim(true);
    for (uint8_t shift : {1, 2}) {
      f.center.set_backdrop_dim_shift(shift);
      f.begin();
      assert(f.status_json()["backdrop_restore_bytes"].as<size_t>() == 240000U * shift);
      assert(test_aligned_allocations.size() == 16);
      for (size_t index = 0; index < 8; index++) {
        const auto *pixels = static_cast<const uint8_t *>(test_aligned_allocations[index].first);
        for (size_t byte = 0; byte < 240000; byte++)
          assert(pixels[byte] == (f.original[index * 240000 + byte] >> shift));
      }
      assert(static_cast<const uint8_t *>(test_aligned_allocations[8].first)[0] == 0xAA);
      f.lvgl.fail_copy_at = f.lvgl.modal_copies + 4;
      f.center.close();
      assert(test_aligned_allocations.size() == 8 && f.scene.paused);
      f.lvgl.fail_copy_at = 0;
      f.center.loop();
      assert(test_aligned_allocations.empty() && f.lvgl.presented_pixels == f.original);
      assert(std::memcmp(f.display.get()->buf_1->data, f.original.data(), f.original.size()) == 0);
      assert(std::memcmp(f.display.get()->buf_2->data, f.original.data(), f.original.size()) == 0);
      assert(lv_display_is_invalidation_enabled(f.display.get()));
    }
  } else if (scenario == "modern_cached_failure") {
    f.center.set_cached_backdrop_dim(true);
    test_fail_aligned_call = test_aligned_calls + 12;
    assert(!f.center.open() && !f.center.owns_scene() && test_aligned_allocations.empty());
    assert(f.lvgl.presented_pixels == f.original);
    test_fail_aligned_call = SIZE_MAX;
    notification_cache_fail_call = notification_cache_calls + 3;
    assert(!f.center.open() && !f.center.owns_scene() && test_aligned_allocations.empty());
    assert(f.lvgl.presented_pixels == f.original);
  } else if (scenario == "modern_isolation") {
    auto *screen = lv_display_get_screen_active(f.display.get());
    auto *top = lv_display_get_layer_top(f.display.get());
    auto *bottom = lv_display_get_layer_bottom(f.display.get());
    unsigned bottom_draws = 0;
    lv_obj_add_event_cb(
        bottom, [](lv_event_t *event) { ++*static_cast<unsigned *>(lv_event_get_user_data(event)); },
        LV_EVENT_DRAW_MAIN_BEGIN, &bottom_draws);
    assert(!lv_obj_has_flag(screen, LV_OBJ_FLAG_HIDDEN) && !lv_obj_has_flag(top, LV_OBJ_FLAG_HIDDEN));
    f.begin();
    assert(lv_obj_has_flag(screen, LV_OBJ_FLAG_HIDDEN) && lv_obj_has_flag(top, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(bottom, LV_OBJ_FLAG_HIDDEN));
    lv_obj_invalidate(f.root);
    lv_refr_now(f.display.get());
    assert(bottom_draws == 0);  // No opaque software clear beneath the complete captured base.
    assert(!lv_obj_has_flag(f.root, LV_OBJ_FLAG_HIDDEN));
    f.lvgl.modal_end_ok = false;
    f.center.close();
    assert(lv_obj_has_flag(screen, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(bottom, LV_OBJ_FLAG_HIDDEN));
    f.lvgl.modal_end_ok = true;
    f.center.loop();
    assert(!lv_obj_has_flag(screen, LV_OBJ_FLAG_HIDDEN) && !lv_obj_has_flag(top, LV_OBJ_FLAG_HIDDEN));
    assert(!lv_obj_has_flag(bottom, LV_OBJ_FLAG_HIDDEN));
    lv_obj_add_flag(top, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(bottom, LV_OBJ_FLAG_HIDDEN);
    f.begin();
    f.center.close();
    assert(lv_obj_has_flag(top, LV_OBJ_FLAG_HIDDEN));  // preserve an already hidden layer
    assert(lv_obj_has_flag(bottom, LV_OBJ_FLAG_HIDDEN));
    assert(!lv_obj_has_flag(screen, LV_OBJ_FLAG_HIDDEN));
  } else if (scenario == "modern_scroll") {
    f.window(0, 30, 0);
    f.begin();
    f.window(0, 30, 1);
    assert(std::strcmp(lv_label_get_text(f.ages[0]), "2 min") == 0);
    assert(f.center.touch_begin(400, 600));
    rt::advance(20);
    assert(f.center.touch_update(400, 0));
    assert(f.status_json()["scroll_y"].as<int>() == 600);
    assert(lv_obj_get_scroll_y(f.list) == 600);
    f.acknowledge_windows(30);
    assert(f.status_json()["offset"].as<int>() == 3);
    assert(f.status_json()["scroll_y"].as<int>() == 600);
    assert(lv_obj_get_scroll_y(f.list) == 36);
    lv_area_t row_area;
    lv_obj_get_coords(f.rows[0], &row_area);
    assert(row_area.y1 == 80 - 36);
    assert(f.center.touch_end());
    rt::advance(20);
    f.center.loop();
    assert(f.status_json()["scroll_y"].as<int>() > 600);
    f.center.touch_begin(400, 300);
    assert(!f.status_json()["inertia"].as<bool>());
    f.center.touch_cancel();
    f.center.close();
    assert(test_aligned_allocations.empty());
  } else if (scenario == "modern_all_records") {
    f.window(0, 100, 0);
    f.begin();
    f.window(0, 100, 1);
    for (int index = 0; index < 36; index++) {
      assert(f.center.touch_begin(400, 680));
      rt::advance(20);
      assert(f.center.touch_update(400, 80));
      f.acknowledge_windows(100);
      rt::advance(130);
      assert(f.center.touch_end());
    }
    assert(f.status_json()["offset"].as<int>() == 92);
    assert(f.status_json()["scroll_y"].as<int>() == 18144);
    for (int index = 0; index < 36; index++) {
      assert(f.center.touch_begin(400, 140));
      rt::advance(20);
      assert(f.center.touch_update(400, 700));
      f.acknowledge_windows(100);
      rt::advance(130);
      assert(f.center.touch_end());
    }
    assert(f.status_json()["offset"].as<int>() == 0 && f.status_json()["scroll_y"].as<int>() == 0);
    assert(test_aligned_allocations.size() == 8);
    f.center.close();
  } else if (scenario == "modern_row_intent") {
    f.center.set_dismiss_commit_pixels(36);
    f.window(0, 3, 0);
    f.begin();
    f.window(0, 3, 1);
    auto requests = f.requests.size();
    assert(f.center.touch_begin(400, 180));
    rt::advance(20);
    f.center.touch_update(460, 240);
    assert(f.center.touch_end());
    assert(f.requests.size() == requests);  // ambiguous diagonal is neither tap nor dismissal
    assert(f.center.touch_begin(400, 180));
    f.center.touch_update(435, 180);
    assert(f.center.touch_end());
    assert(f.requests.size() == requests);
    assert(!lv_obj_has_flag(f.rows[0], LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_get_style_translate_x(f.rows[0], LV_PART_MAIN) == 0);
    assert(f.center.touch_begin(400, 180));
    rt::advance(20);
    assert(f.center.touch_update(436, 184));
    assert(f.center.touch_end());
    assert(f.requests.size() == requests + 1);
    assert(f.requests.back().find("\"op\":\"dismiss\"") != std::string::npos);
    assert(lv_obj_has_flag(f.rows[0], LV_OBJ_FLAG_HIDDEN));
    f.window(0, 3, 1);  // Stale window must not reveal the card.
    f.center.refresh_texts();
    assert(lv_obj_has_flag(f.rows[0], LV_OBJ_FLAG_HIDDEN));
    f.window(0, 2, 2);
    assert(!lv_obj_has_flag(f.rows[0], LV_OBJ_FLAG_HIDDEN));
    assert(f.center.touch_begin(400, 180));
    f.center.touch_update(364, 180);
    assert(f.center.touch_end());
    assert(lv_obj_has_flag(f.rows[0], LV_OBJ_FLAG_HIDDEN));
    f.center.close();
    f.begin();
    assert(f.status_json()["pending"].as<bool>());
    assert(lv_obj_has_flag(f.rows[0], LV_OBJ_FLAG_HIDDEN));
    rt::advance(5001);
    f.center.loop();
    assert(!lv_obj_has_flag(f.rows[0], LV_OBJ_FLAG_HIDDEN));
    assert(f.center.touch_begin(400, 180));
    f.center.touch_update(364, 180);
    assert(f.center.touch_end());
    assert(lv_obj_has_flag(f.rows[0], LV_OBJ_FLAG_HIDDEN));
    f.window(0, 0, 4);
    assert(lv_obj_has_flag(f.rows[0], LV_OBJ_FLAG_HIDDEN));
    f.window(0, 2, 5);
    f.tap(400, 180);
    assert(f.requests.back().find("\"op\":\"open\"") != std::string::npos);
    f.center.close();
  } else if (scenario == "modern_banner_bottom") {
    assert(!f.center.touch_begin(400, 780));
    f.banner.show("First", "Message", "!", 6000);
    const auto generation = f.banner.dismissible_generation();
    assert(generation != 0);
    assert(f.center.touch_begin(400, 780));
    f.center.touch_update(400, 745);
    assert(f.center.touch_end());
    assert(f.banner.dismissible_generation() == generation);
    assert(f.center.touch_begin(400, 780));
    f.center.touch_update(445, 744);
    assert(f.banner.dismissible_generation() == generation);
    assert(f.center.touch_update(400, 744));
    assert(f.banner.is_active() && f.banner.dismissible_generation() == 0);
    assert(f.center.touch_end());
    rt::advance(350);
    f.banner.loop();
    assert(!f.banner.is_active() && f.requests.empty() && !f.center.owns_scene());
    f.banner.show("Expiring", "Message", "!", 10);
    f.banner.show("Next", "Message", "!", 6000);
    assert(f.center.touch_begin(400, 780));
    rt::advance(20);
    f.banner.loop();
    rt::advance(350);
    f.banner.loop();
    const auto replacement = f.banner.dismissible_generation();
    assert(replacement != 0);
    f.center.touch_update(400, 720);
    assert(f.center.touch_end());
    assert(f.banner.dismissible_generation() == replacement);
  } else if (scenario == "modern_card_cache_reflow") {
    f.center.set_transition_duration(240);
    f.center.set_dismiss_commit_pixels(36);
    f.window(0, 3, 0);
    f.begin();
    f.window(0, 3, 1);
    rt::advance(270);
    auto top = [](lv_obj_t *row) {
      lv_obj_update_layout(row);
      lv_area_t area;
      lv_obj_get_coords(row, &area);
      return area.y1;
    };
    const int32_t final_top = top(f.rows[0]);
    const int32_t old_top = top(f.rows[1]);
    const int32_t old_last_top = top(f.rows[2]);
    assert(f.center.touch_begin(400, 180));
    f.center.touch_update(436, 180);
    assert(f.center.touch_end());
    assert(lv_obj_has_flag(f.rows[0], LV_OBJ_FLAG_HIDDEN));
    assert(top(f.rows[1]) == old_top);
    f.window(0, 2, 2, false, 1);
    assert(top(f.rows[0]) == old_top);
    assert(top(f.rows[1]) == old_last_top);
    assert(lv_obj_has_flag(f.rows[2], LV_OBJ_FLAG_HIDDEN));
    rt::advance(90);
    const int32_t intermediate_top = top(f.rows[0]);
    assert(final_top < intermediate_top && intermediate_top < old_top);
    f.window(0, 2, 2, false, 1);  // Repeated acknowledgement does not restart or stop the motion.
    assert(top(f.rows[0]) == intermediate_top);
    rt::advance(120);
    assert(top(f.rows[0]) == final_top);
    assert(top(f.rows[1]) == old_top);
    f.tap(400, 180);
    assert(f.requests.back().find("\"id\":\"00000000000000000000000000000002\"") != std::string::npos);
    f.center.close();
  } else if (scenario == "modern_bottom") {
    f.window(0, 3, 0);
    f.begin();
    f.window(0, 3, 1, true);
    assert(f.center.touch_begin(400, 780));
    assert(f.center.touch_update(400, 730));
    assert(f.center.is_open() && f.status_json()["detail_id"].as<std::string>().empty());
    f.center.touch_update(400, 650);
    assert(f.center.is_open());  // one gesture goes back only once
    assert(f.center.touch_end());
    assert(f.center.touch_begin(400, 780));
    assert(f.center.touch_update(400, 730));
    assert(!f.center.is_open() && f.center.touch_end() && test_aligned_allocations.empty());
  } else if (scenario == "modern_animation") {
    f.center.set_transition_duration(240);
    f.window(0, 3, 0);
    f.begin();
    assert(lv_obj_get_y(f.root) == 0 && lv_obj_get_scroll_y(f.root) == 800 && f.status_json()["opening"].as<bool>());
    rt::advance(270);
    f.center.loop();
    assert(lv_obj_get_y(f.root) == 0 && !f.status_json()["opening"].as<bool>());
    assert(lv_obj_get_scroll_y(f.root) == 0);
    f.center.close();
    assert(f.center.owns_scene() && !lv_obj_has_flag(f.root, LV_OBJ_FLAG_HIDDEN));
    rt::advance(270);
    f.center.loop();
    assert(!f.center.owns_scene() && lv_obj_has_flag(f.root, LV_OBJ_FLAG_HIDDEN));
  } else if (scenario == "modern_card_cache") {
    f.window(0, 20, 0);
    f.begin();
    f.window(0, 20, 1);
    auto status = f.status_json();
    assert(status["row_cache_ready"].as<int>() == 4 && status["row_cache_captures"].as<int>() == 4);
    assert(status["row_cache_bytes"].as<size_t>() == 4U * 2 * 206464);
    assert(lv_obj_get_style_bg_opa(f.rows[0], LV_PART_MAIN) == LV_OPA_TRANSP);
    // Compare actual LVGL pixels from the composite cache against the source widgets.
    auto *cached = lv_snapshot_take(f.rows[0], LV_COLOR_FORMAT_ARGB8888);
    assert(cached != nullptr);
    f.center.set_row_snapshot_cache(false);
    auto *native = lv_snapshot_take(f.rows[0], LV_COLOR_FORMAT_ARGB8888);
    assert(native != nullptr && cached->header.stride == native->header.stride);
    assert(cached->data[(600 - 1) * 4 + 3] == 0);
    assert(cached->data[(86 * 600 + 300) * 4 + 3] == 255);
    for (int y = 0; y < 172; y++) {
      for (int x = 0; x < 600; x++) {
        const size_t offset = y * cached->header.stride + x * 4;
        assert(std::abs(int(cached->data[offset + 3]) - int(native->data[offset + 3])) <= 1);
        if (native->data[offset + 3] == 0)
          continue;
        for (int channel = 0; channel < 3; channel++)
          assert(std::abs(int(cached->data[offset + channel]) - int(native->data[offset + channel])) <= 2);
      }
    }
    lv_draw_buf_destroy(cached);
    lv_draw_buf_destroy(native);
    f.center.set_row_snapshot_cache(true);
    for (int tick = 0; tick < 4; tick++)
      f.center.loop();
    f.center.loop();
    assert(f.status_json()["row_cache_captures"].as<int>() == 8);
    f.center.touch_begin(400, 600);
    rt::advance(20);
    f.center.touch_update(400, 412);
    f.center.loop();
    f.center.touch_cancel();
    assert(f.status_json()["row_cache_captures"].as<int>() == 9);
    f.window(1, 20, 2);
    f.center.loop();
    assert(f.status_json()["row_cache_captures"].as<int>() == 9);  // Stable IDs reuse surfaces after window shift.
    lv_obj_set_style_translate_x(f.rows[0], 42, LV_PART_MAIN);
    assert(f.status_json()["row_cache_ready"].as<int>() == 4);
    lv_obj_set_style_translate_x(f.rows[0], 0, LV_PART_MAIN);
    assert(f.status_json()["row_cache_ready"].as<int>() == 4);
    f.center.set_row_snapshot_cache(false);
    f.center.loop();
    assert(!f.status_json()["row_cache_enabled"].as<bool>() && f.status_json()["row_cache_bytes"].as<int>() == 0);
    f.center.set_row_snapshot_cache(true);
    for (int tick = 0; tick < 4; tick++)
      f.center.loop();
    assert(f.status_json()["row_cache_ready"].as<int>() == 4);
    f.tap(400, 180);
    assert(f.requests.back().find("\"op\":\"open\"") != std::string::npos);
    lv_obj_set_style_bg_color(f.rows[0], lv_color_hex(0x123456), LV_PART_MAIN);
    f.center.loop();
    assert(f.status_json()["row_cache_ready"].as<int>() == 1);
    lv_obj_set_style_bg_opa(f.rows[0], LV_OPA_50, LV_PART_MAIN);
    f.center.loop();
    assert(lv_obj_get_style_bg_opa(f.rows[0], LV_PART_MAIN) == LV_OPA_50);
    f.window(1, 20, 3, true);
    assert(f.status_json()["row_cache_bytes"].as<int>() == 0);
    f.center.close();
    assert(test_aligned_allocations.empty());
  } else if (scenario == "modern_card_cache_low_memory") {
    f.window(0, 8, 0);
    test_free_spiram = 1048576 + 206464 - 1;
    f.begin();
    f.center.loop();
    assert(f.status_json()["row_cache_bytes"].as<int>() == 0);
    assert(lv_obj_get_style_bg_opa(f.rows[0], LV_PART_MAIN) == LV_OPA_COVER);
    f.center.close();
    assert(test_aligned_allocations.empty());
  } else if (scenario == "modern_card_cache_reclaim") {
    f.window(0, 8, 0);
    // A pressure reclaim which releases nothing must not be retried per frame.
    test_free_spiram = 1048576;
    f.begin();
    for (int tick = 0; tick < 12; tick++)
      f.center.loop();
    assert(f.lvgl.reclaim_calls == 1 && f.status_json()["row_cache_bytes"].as<int>() == 0);
    f.center.close();
    // Fragmented allocation: one failed allocation is followed by a successful
    // bounded reclaim retry, without dropping the configured reserve.
    test_free_spiram = 8 * 1024 * 1024;
    f.lvgl.reclaimed_bytes = 1024;
    test_fail_aligned_call = test_aligned_calls + 10;  // 8 bands + low bits, then first card.
    f.begin();
    assert(f.lvgl.reclaim_calls == 2);
    assert(f.status_json()["row_cache_ready"].as<int>() == 4);
    f.center.close();
    assert(test_aligned_allocations.empty());
    test_free_spiram = 1048576;
    f.lvgl.reclaimed_bytes = 3 * 1024 * 1024;
    f.lvgl.reclaim_hook = []() { test_free_spiram = 4 * 1024 * 1024; };
    f.begin();
    assert(f.lvgl.reclaim_calls == 3 && f.status_json()["row_cache_ready"].as<int>() == 4);
    f.center.close();
    assert(test_aligned_allocations.empty());
  } else if (scenario == "modern_card_cache_animation") {
    f.window(0, 8, 0);
    f.center.set_transition_duration(240);
    auto *layer = f.display.get()->layer_head;
    f.begin();  // Source rows begin above the physical viewport during drawer entry.
    assert(f.status_json()["row_cache_ready"].as<int>() == 4 && f.display.get()->layer_head == layer);
    rt::advance(270);
    f.center.loop();
    auto *cached = lv_snapshot_take(f.rows[0], LV_COLOR_FORMAT_ARGB8888);
    f.center.set_row_snapshot_cache(false);
    auto *native = lv_snapshot_take(f.rows[0], LV_COLOR_FORMAT_ARGB8888);
    assert(cached && native);
    for (int y = 0; y < 172; y++) {
      for (int x = 0; x < 600; x++) {
        const size_t byte = y * cached->header.stride + x * 4;
        assert(std::abs(int(cached->data[byte + 3]) - int(native->data[byte + 3])) <= 1);
        if (native->data[byte + 3] != 0) {
          for (int channel = 0; channel < 3; channel++)
            assert(std::abs(int(cached->data[byte + channel]) - int(native->data[byte + channel])) <= 2);
        }
      }
    }
    lv_draw_buf_destroy(cached);
    lv_draw_buf_destroy(native);
    f.center.close();
    rt::advance(270);
    f.center.loop();
    assert(test_aligned_allocations.empty());
  } else if (scenario == "modern_card_cache_fragmented") {
    test_max_aligned_bytes = 300000;
    f.window(0, 8, 0);
    f.center.set_row_cache_band_count(1);
    f.begin();
    assert(f.status_json()["row_cache_ready"].as<int>() == 4);
    assert(f.status_json()["row_cache_max_bands"].as<int>() == 2);
    f.center.close();
    assert(test_aligned_allocations.empty());
    f.center.set_row_snapshot_cache(false);
    f.begin();                        // The modal background has its own240KB allocation granularity.
    test_max_aligned_bytes = 100000;  // Forced fallback through2 and4 to8 bands.
    f.center.set_row_snapshot_cache(true);
    for (int tick = 0; tick < 4; tick++)
      f.center.loop();
    assert(f.status_json()["row_cache_ready"].as<int>() == 4);
    assert(f.status_json()["row_cache_max_bands"].as<int>() == 8);
    const auto captures = f.status_json()["row_cache_captures"].as<int>();
    f.center.loop();
    assert(f.status_json()["row_cache_captures"].as<int>() == captures);  // No allocation/shape churn.
    f.center.close();
    assert(test_aligned_allocations.empty());
  } else if (scenario == "modern_card_cache_bands") {
    f.window(0, 8, 0);
    f.begin();
    for (uint8_t bands : {1, 2, 8}) {
      f.center.set_row_cache_band_count(bands);
      assert(f.status_json()["row_cache_bands"].as<int>() == bands);
      for (int tick = 0; tick < 4; tick++)
        f.center.loop();
      assert(f.status_json()["row_cache_ready"].as<int>() == 4);
      auto *cached = lv_snapshot_take(f.rows[0], LV_COLOR_FORMAT_ARGB8888);
      f.center.set_row_snapshot_cache(false);
      auto *native = lv_snapshot_take(f.rows[0], LV_COLOR_FORMAT_ARGB8888);
      assert(cached && native && cached->header.stride == native->header.stride);
      for (int y = 0; y < 172; y++) {
        for (int x = 0; x < 600; x++) {
          const size_t byte = y * cached->header.stride + x * 4;
          assert(std::abs(int(cached->data[byte + 3]) - int(native->data[byte + 3])) <= 1);
          if (native->data[byte + 3] != 0) {
            for (int channel = 0; channel < 3; channel++)
              assert(std::abs(int(cached->data[byte + channel]) - int(native->data[byte + channel])) <= 2);
          }
        }
      }
      lv_draw_buf_destroy(cached);
      lv_draw_buf_destroy(native);
      f.center.set_row_snapshot_cache(true);
    }
    f.center.set_row_cache_band_count(0);
    assert(f.status_json()["row_cache_bands"].as<int>() == 8);
    f.center.set_row_cache_band_count(9);
    assert(f.status_json()["row_cache_bands"].as<int>() == 8);
    f.center.close();
    assert(test_aligned_allocations.empty());
  } else if (scenario == "modern_card_cache_geometry") {
    // Odd RGB strides, square cards and clamped pill radii use identical native pixels.
    for (int radius : {0, 43, 500}) {
      for (auto *row : f.rows) {
        lv_obj_set_size(row, 599, 173);
        lv_obj_set_style_radius(row, radius, LV_PART_MAIN);
      }
      f.window(0, 8, 0);
      f.begin();
      assert(f.status_json()["row_cache_ready"].as<int>() == 4);
      auto *cached = lv_snapshot_take(f.rows[0], LV_COLOR_FORMAT_ARGB8888);
      f.center.set_row_snapshot_cache(false);
      auto *native = lv_snapshot_take(f.rows[0], LV_COLOR_FORMAT_ARGB8888);
      assert(cached && native);
      for (int y = 0; y < 173; y++) {
        for (int x = 0; x < 599; x++) {
          const size_t a = y * cached->header.stride + x * 4;
          const size_t b = y * native->header.stride + x * 4;
          assert(std::abs(int(cached->data[a + 3]) - int(native->data[b + 3])) <= 1);
          if (native->data[b + 3] != 0) {
            for (int channel = 0; channel < 3; channel++)
              assert(std::abs(int(cached->data[a + channel]) - int(native->data[b + channel])) <= 2);
          }
        }
      }
      lv_draw_buf_destroy(cached);
      lv_draw_buf_destroy(native);
      f.center.close();
      f.center.set_row_snapshot_cache(true);
      assert(test_aligned_allocations.empty());
    }
  } else if (scenario == "modern_card_cache_failure") {
    f.window(0, 8, 0);
    notification_cache_fail_call = notification_cache_calls + 1;
    f.begin();
    assert(lv_obj_get_style_bg_opa(f.rows[0], LV_PART_MAIN) == LV_OPA_COVER);
    f.center.loop();
    notification_cache_fail_call = notification_cache_calls + 1;
    f.center.close();
    assert(f.center.owns_scene() && f.status_json()["row_cache_bytes"].as<int>() == 0);
    f.center.loop();
    assert(test_aligned_allocations.empty());
    assert(f.lvgl.presented_pixels == f.original);
  } else if (scenario == "modern_age_and_prune") {
    f.window(0, 30, 0);
    f.begin();
    f.window(0, 30, 1);
    f.now += 3600;
    f.center.refresh_texts();
    assert(std::strcmp(lv_label_get_text(f.ages[0]), "1 h") == 0);
    f.now = 0;
    f.center.refresh_texts();
    assert(std::strcmp(lv_label_get_text(f.ages[0]), "") == 0);
    f.center.touch_begin(400, 600);
    rt::advance(20);
    f.center.touch_update(400, 0);
    f.acknowledge_windows(30);
    f.window(0, 2, f.status_json()["pending"].as<bool>() ? 99 : 98);
    assert(f.status_json()["scroll_y"].as<int>() == 0);
    f.center.touch_cancel();
    f.center.close();
  } else if (scenario == "translations") {
    bool english = false;
    f.center.set_text_provider(
        [&](uint8_t index) { return index == 0 ? (english ? "Notifications" : "Powiadomienia") : "..."; });
    f.begin();
    assert(std::strcmp(lv_label_get_text(f.heading), "Powiadomienia") == 0);
    english = true;
    f.center.refresh_texts();
    assert(std::strcmp(lv_label_get_text(f.heading), "Notifications") == 0);
    f.center.close();
  } else {
    assert(false);
  }
  std::cout << "PASS " << scenario << '\n';
}
