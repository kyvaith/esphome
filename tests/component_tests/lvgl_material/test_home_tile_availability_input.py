"""Actual Home deferred input plus TileSurface and explicitly supplied product policy."""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import shlex
import subprocess
import textwrap
import unittest

from host_light_control import LVGL_SOURCE, SOURCE, compiler_command
import test_power_presentation_fence as power_host

HERE = Path(__file__).resolve().parent


def braced(source, marker):
    start = source.index(marker)
    opening = source.index("{", start)
    depth = 1
    index = opening + 1
    while depth:
        depth += (source[index] == "{") - (source[index] == "}")
        index += 1
    return source[start:index]


class HomeTileAvailabilityInputTests(unittest.TestCase):
    checked = staticmethod(power_host.PowerPresentationFenceTests.checked)

    @classmethod
    def setUpClass(cls):
        if not (configured := os.environ.get("ESPHOME_TEST_PRODUCT_SOURCE")):
            raise unittest.SkipTest(
                "ESPHOME_TEST_PRODUCT_SOURCE is required for product composition"
            )
        cls.product = Path(configured).resolve()
        cls.pins = {}
        cls.input_header = LVGL_SOURCE / "lvgl_esphome.h"
        cls.input_source = LVGL_SOURCE / "lvgl_esphome.cpp"
        cls.navigation_source = LVGL_SOURCE / "lvgl_navigation.cpp"
        for path in (
            cls.input_header,
            cls.input_source,
            cls.navigation_source,
            cls.product / "modules/lvgl/navigation.yaml",
            cls.product / "modules/lvgl/material.yaml",
            cls.product / "modules/common/core.yaml",
            SOURCE / "material_tile_surface.h",
            SOURCE / "material_tile_surface.cpp",
            LVGL_SOURCE / "deferred_press_context.h",
        ):
            cls.pins[path] = hashlib.sha256(path.read_bytes()).hexdigest()
        material = (cls.product / "modules/lvgl/material.yaml").read_text(
            encoding="utf-8"
        )
        surface = material.split("    - id: home_material_tiles\n", 1)[1].split(
            "      on_control_press:", 1
        )[0]
        if (
            "require_snapshot_ready: true" not in surface
            or "action_availability: true" not in surface
        ):
            raise AssertionError(
                "Actual product must enable availability and snapshot-context readiness"
            )
        navigation = (cls.product / "modules/lvgl/navigation.yaml").read_text(
            encoding="utf-8"
        )
        refresh = navigation.split("  - id: ui_refresh_managed_tile_snapshots\n", 1)[
            1
        ].split("  - id:", 1)[0]
        if (
            "- delay: 500ms" not in refresh
            or "if (ok)" not in refresh
            or ".mark_slots_ready(refresh_slots);" not in refresh
        ):
            raise AssertionError("Actual successful-refresh-only policy changed")
        guard = refresh.split("            lambda: |-\n", 1)[1].split(
            "          then:", 1
        )[0]
        cls.product_guard = textwrap.dedent(guard)
        core = (cls.product / "modules/common/core.yaml").read_text(encoding="utf-8")
        touch = core.split("    - action: debug_touch_input\n", 1)[1].split(
            "    - action:", 1
        )[0]
        if (
            not touch.index("if (phase == 2)")
            < touch.index("if (!id(boot_sequence_done))")
            < touch.index("if (phase == 0)")
        ):
            raise AssertionError(
                "Owned release must precede new-contact readiness checks"
            )
        cls.phase2 = braced(touch, "if (phase == 2)")
        power_host.PowerPresentationFenceTests.setUpClass.__func__(cls)

    @classmethod
    def tearDownClass(cls):
        try:
            if any(
                hashlib.sha256(path.read_bytes()).hexdigest() != pin
                for path, pin in cls.pins.items()
            ):
                raise AssertionError(
                    "Actual input/product policy sources changed during composition test"
                )
        finally:
            power_host.PowerPresentationFenceTests.tearDownClass.__func__(cls)

    @classmethod
    def build_control(cls, name, acceleration):
        for filename in ("material_tile_surface.h", "material_tile_surface.cpp"):
            source = "\n".join(
                line
                for line in (SOURCE / filename).read_text(encoding="utf-8").splitlines()
                if not line.startswith("#include")
            )
            (cls.root / (filename + ".inc")).write_text(source, encoding="utf-8")
        header = cls.input_header.read_text(encoding="utf-8")
        (cls.root / "home_listener_class.inc").write_text(
            braced(header, "class LVTouchListener :") + ";\n", encoding="utf-8"
        )
        source = cls.input_source.read_text(encoding="utf-8")
        body = source.split("LVTouchListener::LVTouchListener(", 1)[1].split(
            "\n#endif  // USE_LVGL_TOUCHSCREEN", 1
        )[0]
        (cls.root / "home_listener_methods.inc").write_text(
            "LVTouchListener::LVTouchListener(" + body, encoding="utf-8"
        )
        (cls.root / "home_navigation_defer.inc").write_text(
            braced(
                cls.navigation_source.read_text(encoding="utf-8"),
                "bool LvglNavigation::should_defer_press(",
            ).replace("LvglNavigation::", "HomeNavigation::", 1)
            + "\n",
            encoding="utf-8",
        )
        (cls.root / "home_product_guard.inc").write_text(
            cls.product_guard, encoding="utf-8"
        )
        (cls.root / "home_debug_release.inc").write_text(cls.phase2, encoding="utf-8")
        support = (HERE / "real_lvgl_test_support.h").read_text(encoding="utf-8")
        marker = "  bool is_paused() const { return this->paused; }"
        extra = """
  std::function<void(int32_t, int32_t)> test_navigation_begin;
  std::function<bool(int32_t, int32_t)> test_navigation_update;
  std::function<bool()> test_navigation_end;
  std::function<bool(lv_obj_t *)> test_navigation_defer;
  lv_display_t *get_disp() const { return native_display; }
  void rotate_coordinates(int32_t &, int32_t &) const {}
  void maybe_wakeup() {}
  void cancel_timeout(const char *) {}
  template<typename F> void set_timeout(const char *, uint32_t, F &&) {}
  void navigation_touch_begin(int32_t x, int32_t y) { test_navigation_begin(x, y); }
  bool navigation_touch_update(int32_t x, int32_t y) { return test_navigation_update(x, y); }
  bool navigation_touch_end() { return test_navigation_end(); }
  bool navigation_should_defer_press(lv_obj_t *target) const { return test_navigation_defer(target); }
"""
        if support.count(marker) != 1:
            raise AssertionError("Existing host-only compositor facade changed")
        (cls.root / "home_real_lvgl_test_support.h").write_text(
            support.replace(marker, marker + extra), encoding="utf-8"
        )
        target = cls.root / (name + "-home.o")
        thread_flags = ["-pthread"] if os.name != "nt" else []
        if acceleration:
            thread_flags += ["-DUSE_LVGL_DEFERRED_PRESS_CONTEXT"]
        cls.checked(
            [
                *compiler_command(),
                "-std=c++20",
                "-UNDEBUG",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-Wno-deprecated-declarations",
                "-DUSE_LVGL_MATERIAL_ENTITY_CONTROL_TILE_ROUTING",
                "-DUSE_LVGL_MATERIAL_TILE_ACTION_AVAILABILITY",
                "-DLV_CONF_INCLUDE_SIMPLE",
                "-DLV_COLOR_DEPTH=32",
                *thread_flags,
                *cls.includes,
                "-c",
                str(HERE / "home_tile_availability_input_test.cpp"),
                "-o",
                str(target),
            ]
        )
        executable = cls.root / (name + (".exe" if os.name == "nt" else ""))
        arguments = [*map(str, [target, *cls.objects]), "-o", str(executable)]
        arguments += thread_flags
        response = cls.root / (name + "-home-link.rsp")
        response.write_text(
            subprocess.list2cmdline(arguments)
            if os.name == "nt"
            else shlex.join(arguments),
            encoding="utf-8",
        )
        cls.checked([*compiler_command(), "@" + str(response)])
        return executable

    def test_actual_deferred_home_and_product_readiness_composition(self):
        for case in (
            "held_delivered_availability_aba",
            "disabled_home_contact",
            "unrelated_availability_update",
            "held_caption_aba",
            "held_rebind_acknowledged",
            "unrelated_caption_update",
        ):
            with self.subTest(case=case):
                self.assertIn(
                    f"HOME {case} PASS", self.checked([str(self.full), case]).stdout
                )

    def test_missing_snapshot_gate_is_a_meaningful_negative_control(self):
        result = subprocess.run(
            [str(self.no_acceleration), "without_snapshot_gate"],
            capture_output=True,
            text=True,
            timeout=10,
            check=False,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            "Held delivered availability must not replay a command",
            result.stdout + result.stderr,
        )

    def test_missing_raw_context_guard_reproduces_delayed_caption_aba_click(self):
        result = subprocess.run(
            [str(self.no_acceleration), "held_caption_aba"],
            capture_output=True, text=True, timeout=10, check=False,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Raw held context must not replay a command", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
