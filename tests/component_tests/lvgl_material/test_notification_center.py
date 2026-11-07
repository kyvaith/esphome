"""Real LVGL widgets and production notification lifecycle, with a display lease facade."""

import os
from pathlib import Path
import shlex
import subprocess
import unittest

from host_light_control import HERE, LVGL_SOURCE, SOURCE, compiler_command
import test_power_presentation_fence as host


class NotificationCenterTests(unittest.TestCase):
    extra_lvgl_options = {"LV_USE_SNAPSHOT": 1}
    checked = staticmethod(host.PowerPresentationFenceTests.checked)

    @classmethod
    def setUpClass(cls):
        host.PowerPresentationFenceTests.setUpClass.__func__(cls)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    @classmethod
    def build_control(cls, name, acceleration):
        if not acceleration:
            return cls.full
        # Match native translation units: the feature is supplied by generated
        # defines.h, never a compiler -D that could hide a missing include.
        (cls.root / "esphome/core/defines.h").write_text(
            "#pragma once\n#define USE_LVGL_MATERIAL_NOTIFICATION_CENTER\n"
            "#define USE_LVGL_NAVIGATION_ADMISSION_GUARD\n",
            encoding="utf8",
        )
        with (cls.root / "esphome/core/defines.h").open("a", encoding="utf8") as stream:
            stream.write("#define USE_LVGL_MATERIAL_NOTIFICATION_ROW_CACHE\n")
        # Only hardware transport/RTOS are facades. Images, labels, objects,
        # animation, styles, native DIRECT buffers and event geometry are LVGL.
        support = (HERE / "real_lvgl_test_support.h").read_text(encoding="utf8")
        support = support.replace(
            "#include <lvgl.h>",
            '#include <lvgl.h>\n#include "esphome/core/defines.h"\n#include "src/display/lv_display_private.h"\n#include "navigation_touch_delegate.h"',
        )
        support = support.replace(
            "namespace esphome {",
            """namespace esphome {
enum class BufferReader { CPU, DMA };
enum class BufferWriter { CPU, DMA };
namespace display {
enum ColorBitness { COLOR_BITNESS_888, COLOR_BITNESS_565 };
enum ColorOrder { COLOR_ORDER_RGB, COLOR_ORDER_BGR };
struct FrameBufferView { const uint8_t *data{}; size_t width{}, height{}, stride{}, size{};
  ColorBitness bitness{COLOR_BITNESS_888}; ColorOrder color_order{COLOR_ORDER_BGR}; bool big_endian{}; };
struct FrameBufferLease { uint8_t *data{}; size_t width{}, height{}, stride{}, size{};
  ColorBitness bitness{COLOR_BITNESS_888}; ColorOrder color_order{COLOR_ORDER_BGR}; bool big_endian{};
  explicit operator bool() const { return data != nullptr; } };
}
""",
            1,
        )
        support = support.replace(
            "  bool is_paused() const { return this->paused; }",
            """
  lv_display_t *get_disp() const { return native_display; }
  bool modal_session{}, modal_begin_ok{true}, modal_end_ok{true}, modal_copy_ok{true}, modal_end_pending{};
  unsigned modal_generation{}, modal_claims{}, modal_presentations{}, modal_releases{}, modal_copies{}, fail_copy_at{};
  display::ColorOrder modal_order{display::COLOR_ORDER_BGR};
  std::vector<uint8_t> modal_target;
  bool begin_frame_buffer_presentation(uint32_t) {
    if (!modal_begin_ok || modal_session) return false;
    modal_session = true; modal_end_pending = false; ++modal_generation;
    real_lvgl_test::record("modal_begin"); return true;
  }
  uint32_t get_frame_buffer_presentation_generation() const { return modal_generation; }
  bool get_presentation_active_frame(display::FrameBufferView *frame, BufferReader) {
    assert(modal_session); *frame = {presented_pixels.data(), static_cast<size_t>(get_width()),
      static_cast<size_t>(get_height()), static_cast<size_t>(get_width()) * 3, presented_pixels.size(),
      display::COLOR_BITNESS_888, modal_order, false}; return true;
  }
  bool copy_presentation_region_rgb888(const uint8_t *source, int sw, int sh, int sx, int sy,
                                       uint8_t *target, int width, int height) {
    assert(modal_session && source && target && sx >= 0 && sy >= 0 && sx + width <= sw && sy + height <= sh);
    ++modal_copies;
    if (!modal_copy_ok || (fail_copy_at && modal_copies == fail_copy_at)) return false;
    for (int y = 0; y < height; y++)
      std::memcpy(target + y * width * 3, source + ((y + sy) * sw + sx) * 3, width * 3);
    real_lvgl_test::record("modal_copy"); return true;
  }
  bool acquire_presentation_frame(display::FrameBufferLease *lease, BufferWriter writer, uint32_t, uint32_t gen) {
    assert(modal_session && gen == modal_generation && writer == BufferWriter::DMA);
    if (modal_end_pending) return false;
    modal_target.assign(presented_pixels.size(), 0); ++modal_claims;
    *lease = {modal_target.data(), static_cast<size_t>(get_width()), static_cast<size_t>(get_height()),
      static_cast<size_t>(get_width()) * 3, modal_target.size()}; return true;
  }
  bool present_presentation_frame(display::FrameBufferLease *lease, uint32_t) {
    assert(modal_session && lease->data == modal_target.data());
    presented_pixels = modal_target; lease->data = nullptr; ++modal_presentations;
    real_lvgl_test::record("modal_restore"); return true;
  }
  bool release_presentation_frame(display::FrameBufferLease *lease) {
    assert(modal_session); lease->data = nullptr; ++modal_releases; return true;
  }
  bool end_frame_buffer_presentation(uint32_t, uint32_t expected) {
    assert(modal_session && expected == modal_generation);
    if (!modal_end_ok) { modal_end_pending = true; return false; }
    for (auto *buffer : {native_display->buf_1, native_display->buf_2})
      if (buffer) std::memcpy(buffer->data, presented_pixels.data(), presented_pixels.size());
    modal_session = false; modal_end_pending = false; real_lvgl_test::record("modal_end"); return true;
  }
  bool wait_for_direct_frame_presented(uint32_t) { return true; }
  size_t reclaimed_bytes{}, reclaim_calls{};
  std::function<void()> reclaim_hook{};
  size_t reclaim_idle_application_previews() {
    ++reclaim_calls; if (reclaim_hook) reclaim_hook(); return reclaimed_bytes;
  }
  bool is_paused() const { return this->paused; }
""",
            1,
        )
        support = support.replace(
            "class LvglNavigation {\n public:",
            """class LvglNavigation {
 public:
  NavigationTouchDelegate *delegate{};
  void set_touch_delegate(NavigationTouchDelegate *value) { delegate = value; }
""",
            1,
        )
        support = support.replace(
            "inline void *heap_caps_aligned_alloc(size_t alignment, size_t bytes, int) {",
            "inline size_t test_free_spiram = 8 * 1024 * 1024;\n"
            "inline size_t heap_caps_get_free_size(int) { return test_free_spiram; }\n"
            "inline size_t test_max_aligned_bytes = SIZE_MAX, test_fail_aligned_call = SIZE_MAX, test_aligned_calls = 0;\n"
            "inline void *heap_caps_aligned_alloc(size_t alignment, size_t bytes, int) {\n"
            "  if (++test_aligned_calls == test_fail_aligned_call || bytes > test_max_aligned_bytes) return nullptr;",
        )
        (cls.root / "real_lvgl_test_support.h").write_text(support, encoding="utf8")
        (cls.root / "esp_cache.h").write_text(
            "#pragma once\n#include <cstddef>\n"
            "inline constexpr int ESP_OK=0, ESP_CACHE_MSYNC_FLAG_DIR_C2M=1, ESP_CACHE_MSYNC_FLAG_TYPE_DATA=2;\n"
            "inline size_t notification_cache_calls=0, notification_cache_fail_call=SIZE_MAX;\n"
            "inline int esp_cache_msync(void *, size_t, int) {return ++notification_cache_calls == notification_cache_fail_call ? -1 : 0;}\n",
            encoding="utf8",
        )
        json_header = cls.root / "esphome/components/json/json_util.h"
        json_header.parent.mkdir(parents=True, exist_ok=True)
        json_header.write_text(
            """#pragma once
#include <ArduinoJson.h>
#include <string>
namespace esphome::json {
template<typename F> std::string build_json(F callback) {
  JsonDocument document; callback(document.to<JsonObject>());
  std::string result; serializeJson(document, result); return result;
}
template<typename F> bool parse_json(const std::string &payload, F callback) {
  JsonDocument document;
  if (deserializeJson(document, payload)) return false;
  return callback(document.as<JsonObject>());
}
}
""",
            encoding="utf8",
        )
        for relative in ("components/lvgl/direct_scene_controller.h",):
            target = cls.root / "esphome" / relative
            target.write_text(
                (LVGL_SOURCE / Path(relative).name).read_text(), encoding="utf8"
            )
        flags = [
            "-std=c++20",
            "-UNDEBUG",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wno-deprecated-declarations",
            "-DUSE_ESP32",
            "-DUSE_LVGL_PPA",
            "-DLV_CONF_INCLUDE_SIMPLE",
            "-DLV_COLOR_DEPTH=32",
        ]
        objects = []
        for path in [
            SOURCE / unit
            for unit in (
                "material_notification.cpp",
                "material_modal_scene.cpp",
                "material_notification_center.cpp",
                "material_notification_row_cache.cpp",
            )
        ] + [HERE / "notification_center_test.cpp"]:
            target = cls.root / (path.name + ".o")
            cls.checked(
                [
                    *compiler_command(),
                    *flags,
                    *cls.includes,
                    "-c",
                    str(path),
                    "-o",
                    str(target),
                ]
            )
            objects.append(target)
        executable = cls.root / (
            "notifications.exe" if os.name == "nt" else "notifications"
        )
        arguments = [*map(str, [*objects, *cls.objects]), "-o", str(executable)]
        response = cls.root / "notification-link.rsp"
        response.write_text(
            subprocess.list2cmdline(arguments)
            if os.name == "nt"
            else shlex.join(arguments),
            encoding="utf8",
        )
        cls.checked([*compiler_command(), "@" + str(response)])
        return executable

    def test_scene_handoffs_gestures_stale_rows_and_wire_validation(self):
        for scenario in (
            "scene",
            "drain_failure",
            "end_failure",
            "gestures",
            "stale_row",
            "wire",
            "banner",
            "shared_attention",
            "fragmented",
            "partial_restore",
            "geometry",
            "detail_view",
            "translations",
            "modern_scroll",
            "modern_isolation",
            "cached_dim_words",
            "modern_cached_backdrop",
            "modern_cached_failure",
            "modern_all_records",
            "modern_row_intent",
            "modern_banner_bottom",
            "modern_card_cache_reflow",
            "modern_bottom",
            "modern_animation",
            "modern_card_cache",
            "modern_card_cache_low_memory",
            "modern_card_cache_reclaim",
            "modern_card_cache_geometry",
            "modern_card_cache_bands",
            "modern_card_cache_fragmented",
            "modern_card_cache_animation",
            "modern_card_cache_failure",
            "modern_age_and_prune",
        ):
            with self.subTest(scenario=scenario):
                result = self.checked([str(self.full), scenario])
                self.assertIn("PASS " + scenario, result.stdout)
