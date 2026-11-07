"""Fan pointer retirement and pooled preset view using public LVGL widgets."""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import shlex
import subprocess
import unittest

from host_light_control import HERE, SOURCE, compiler_command
import test_number_native_input as number


class FanNativeInputTests(unittest.TestCase):
    checked = staticmethod(number.NumberNativeInputTests.checked)

    @classmethod
    def setUpClass(cls):
        number.NumberNativeInputTests.setUpClass.__func__(cls)

    @classmethod
    def tearDownClass(cls):
        number.NumberNativeInputTests.tearDownClass.__func__(cls)

    @classmethod
    def build_control(cls) -> Path:
        sources = [
            *[SOURCE / unit for unit in number.UNITS],
            SOURCE / "material_entity_control_fan.cpp",
            SOURCE / "material_entity_control_fan_component.cpp",
            HERE / "fan_native_input_test.cpp",
        ]
        cls.pins.update(
            {path: hashlib.sha256(path.read_bytes()).hexdigest() for path in sources}
        )
        flags = [
            "-std=c++20",
            "-UNDEBUG",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wno-deprecated-declarations",
            "-DUSE_LVGL_MATERIAL_NUMBER_STEPPER",
            "-DUSE_LVGL_MATERIAL_FAN_CONTROLS",
            "-DUSE_LVGL_NAVIGATION_ADMISSION_GUARD",
            "-DARDUINOJSON_POOL_CAPACITY=128",
            "-DLV_CONF_INCLUDE_SIMPLE",
            "-DLV_COLOR_DEPTH=32",
        ]
        objects = []
        for path in sources:
            target = cls.root / ("fan-" + path.name + ".o")
            cls.checked(
                [*compiler_command(), *flags, *cls.includes, "-c", path, "-o", target]
            )
            objects.append(target)
        executable = cls.root / ("fan-native" + (".exe" if os.name == "nt" else ""))
        arguments = [*map(str, [*objects, *cls.objects]), "-o", str(executable)]
        response = cls.root / "fan-native-link.rsp"
        response.write_text(
            subprocess.list2cmdline(arguments)
            if os.name == "nt"
            else shlex.join(arguments),
            encoding="utf-8",
        )
        cls.checked([*compiler_command(), "@" + str(response)])
        return executable

    def test_fan_release_modal_knownness_and_handoff(self):
        for case in (
            "open",
            "power",
            "speed",
            "noncanonical",
            "speed_unknown",
            "preset_speed",
            "preset_speed_aba",
            "preset_speed_lost",
            "preset_speed_unknown",
            "preset_only",
            "preset_only_unknown",
            "power_only",
            "preset",
            "aba",
            "renewal",
            "unknown",
            "handoff",
            "glyphs",
        ):
            with self.subTest(case=case):
                self.assertIn(
                    f"FAN {case} pass=1", self.checked([self.executable, case]).stdout
                )
