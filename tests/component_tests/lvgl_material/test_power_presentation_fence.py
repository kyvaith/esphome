"""Real LVGL pointer/style/flush interleavings with the actual control/Scalar.

The caller supplies a public LVGL9.6 source tree and native C/C++ compilers.
Only ESPHome/RTOS and display-lease APIs use host facades; native LVGL objects,
glyphs, images, events, pointer input, styles and DIRECT refresh remain real.
"""
from __future__ import annotations

import concurrent.futures
import itertools
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest

from host_light_control import FORK, LVGL_SOURCE, SOURCE, arduino_json, compiler_command

HERE = Path(__file__).resolve().parent
UNITS = ("material_entity_control.cpp", "material_entity_control_protocol.cpp",
         "material_entity_control_codec.cpp", "material_entity_control_component.cpp",
         "material_scalar_raster.cpp", "material_direct_scalar_control.cpp")
NEGATIVES = ("press_lost", "invalidate", "new_grant", "snapshot_suspend", "navigation_closed",
             "stale_current", "drain_failure", "duplicate_release", "fallback_no_scalar_loop",
             "inflight_retire_during_press", "brightness_waits_for_fence_completion")


class PowerPresentationFenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (configured := os.environ.get("ESPHOME_TEST_LVGL")):
            raise unittest.SkipTest("ESPHOME_TEST_LVGL must identify public LVGL9.6 sources")
        cls.library = Path(configured).resolve()
        if not (cls.library / "lv_conf_template.h").is_file():
            raise AssertionError("ESPHOME_TEST_LVGL must contain lv_conf_template.h and src")
        cls.temporary = tempfile.TemporaryDirectory(prefix="power-native-lvgl-")
        cls.root = Path(cls.temporary.name)
        for name in (
            "components/lvgl/lvgl_esphome.h", "components/lvgl/lvgl_navigation.h",
            "components/esp32/task_utils.h", "components/api/api_server.h",
            "core/automation.h", "core/component.h", "core/helpers.h", "core/string_ref.h",
            "core/application.h", "core/hal.h", "core/log.h", "core/defines.h",
            "freertos/FreeRTOS.h", "freertos/task.h", "esp_heap_caps.h",
        ):
            path = cls.root / (name if name.startswith(("freertos/", "esp_heap")) else "esphome/" + name)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#include "real_lvgl_test_support.h"\n', encoding="utf-8")
        config = (cls.library / "lv_conf_template.h").read_text(encoding="utf-8")
        config = config.replace('#if 0 /* Set this to "1" to enable content */',
                                "#if 1 /* Real native input host configuration */", 1)
        if "#if 1 /* Real native input host configuration */" not in config:
            raise AssertionError("Unexpected LVGL template activation guard")
        config = re.sub(r"#define LV_MEM_SIZE .*", "#define LV_MEM_SIZE (8 * 1024 * 1024U)", config)
        for option in ("LV_USE_LOG", "LV_USE_SYSMON", "LV_USE_ANIMIMG", "LV_THEME_DEFAULT_GROW"):
            config = re.sub(r"#define " + option + r" .*", "#define " + option + " 0", config)
        for directory in (cls.library / "src/widgets").iterdir():
            if directory.is_dir() and directory.name not in {"button", "label", "image"}:
                config = re.sub(r"(#define LV_USE_" + directory.name.upper() + r" )1\b", r"\g<1>0", config)
        (cls.root / "lv_conf.h").write_text(config, encoding="utf-8")
        cls.includes = []
        for directory in (cls.root, HERE, SOURCE, LVGL_SOURCE, FORK, cls.library,
                          cls.library / "include/lvgl", cls.library / "src", arduino_json()):
            cls.includes += ["-I", str(directory)]
        sources = [cls.library / "src/lv_init.c"]
        for directory in ("core", "display", "draw", "font", "fs", "image", "indev", "layouts",
                          "logging", "misc", "osal", "stdlib", "themes", "tick", "widgets"):
            sources.extend(sorted((cls.library / "src" / directory).rglob("*.c")))
        sources.extend(sorted((cls.library / "src/libs/bin_decoder").rglob("*.c")))
        cc = [os.environ.get("ESPHOME_TEST_CC", "cc"),
              *shlex.split(os.environ.get("ESPHOME_TEST_CC_ARGS", ""))]

        def build_c(item):
            index, path = item
            target = cls.root / f"lvgl-{index:04}.o"
            cls.checked([*cc, "-std=c11", "-O1", "-DLV_CONF_INCLUDE_SIMPLE", *cls.includes,
                         "-c", str(path), "-o", str(target)])
            return target

        with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
            cls.objects = list(pool.map(build_c, enumerate(sources)))
        cls.full = cls.build_control("full", acceleration=True)
        cls.no_acceleration = cls.build_control("no-acceleration", acceleration=False)

    @staticmethod
    def checked(command):
        result = subprocess.run(command, capture_output=True, text=True, timeout=180, check=False)
        if result.returncode:
            raise AssertionError(f"{command!r}\n{result.stdout}{result.stderr}")
        return result

    @classmethod
    def build_control(cls, name, acceleration):
        flags = ["-std=c++20", "-UNDEBUG", "-Wall", "-Wextra", "-Werror", "-Wno-deprecated-declarations",
                 "-DUSE_LVGL_MATERIAL_DIRECT_SCALAR_CONTROL", "-DUSE_LVGL_NAVIGATION_ADMISSION_GUARD",
                 "-DUSE_LVGL_DIRECT_REGION_EXPECTED_GENERATION", "-DARDUINOJSON_POOL_CAPACITY=128",
                 "-DTEST_POWER_FENCE", "-DLV_CONF_INCLUDE_SIMPLE", "-DLV_COLOR_DEPTH=32"]
        flags += (["-DUSE_ESP32", "-DUSE_MIPI_DSI", "-DUSE_LVGL_PPA"] if acceleration
                  else ["-DTEST_NO_ACCELERATION"])
        if os.name != "nt":
            flags += ["-pthread"]
        objects = []
        for path in [*[SOURCE / unit for unit in UNITS], HERE / "power_presentation_fence_test.cpp"]:
            target = cls.root / (name + "-" + path.name + ".o")
            cls.checked([*compiler_command(), *flags, *cls.includes, "-c", str(path), "-o", str(target)])
            objects.append(target)
        executable = cls.root / (name + (".exe" if os.name == "nt" else ""))
        arguments = [*map(str, [*objects, *cls.objects]), "-o", str(executable)]
        if os.name != "nt":
            arguments += ["-pthread"]
        response = cls.root / (name + "-link.rsp")
        response.write_text(subprocess.list2cmdline(arguments) if os.name == "nt" else shlex.join(arguments),
                            encoding="utf-8")
        cls.checked([*compiler_command(), "@" + str(response)])
        return executable

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def test_changed_and_unchanged_state_across_real_native_flushes(self):
        for change, retirement, phase in itertools.product(("changed", "unchanged"),
                                                            ("before", "after"), ("prepress", "held")):
            with self.subTest(change=change, retirement=retirement, phase=phase):
                result = self.checked([str(self.full), change, retirement, phase])
                self.assertIn("RESULT commands=1 view=1 scalar=1 pressed=0", result.stdout)

    def test_cancellation_stale_completion_and_single_command_ownership(self):
        for name in NEGATIVES:
            with self.subTest(name=name):
                result = self.checked([str(self.full), name])
                self.assertIn(f"NEGATIVE {name} PASS", result.stdout)

    def test_no_acceleration_two_power_commands_without_scalar_loop(self):
        result = self.checked([str(self.no_acceleration), "no_acceleration_two_power"])
        self.assertIn("NEGATIVE no_acceleration_two_power PASS", result.stdout)
