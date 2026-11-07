"""Humidifier pointer retirement and pooled preset view using public LVGL widgets."""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import shlex
import subprocess
import unittest

from host_light_control import HERE, SOURCE, compiler_command
import test_number_native_input as number


class RobotNativeInputTests(unittest.TestCase):
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
            SOURCE / "material_entity_control_robot.cpp",
            SOURCE / "material_entity_control_robot_component.cpp",
            SOURCE / "material_entity_control_humidifier.cpp",
            SOURCE / "material_entity_control_humidifier_component.cpp",
            SOURCE / "material_entity_control_fan.cpp",
            SOURCE / "material_entity_control_fan_component.cpp",
            SOURCE / "material_entity_control_cover.cpp",
            SOURCE / "material_entity_control_cover_component.cpp",
            SOURCE / "material_entity_control_choice.cpp",
            SOURCE / "material_entity_control_choice_component.cpp",
            HERE / "robot_native_input_test.cpp",
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
            "-DUSE_LVGL_MATERIAL_ROBOT_CONTROLS",
            "-DUSE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS",
            "-DUSE_LVGL_MATERIAL_FAN_CONTROLS",
            "-DUSE_LVGL_MATERIAL_COVER_CONTROLS",
            "-DUSE_LVGL_MATERIAL_CHOICE_SELECTOR",
            "-DUSE_LVGL_NAVIGATION_ADMISSION_GUARD",
            "-DARDUINOJSON_POOL_CAPACITY=128",
            "-DLV_CONF_INCLUDE_SIMPLE",
            "-DLV_COLOR_DEPTH=32",
        ]
        objects = []
        for path in sources:
            target = cls.root / ("robot-" + path.name + ".o")
            cls.checked(
                [*compiler_command(), *flags, *cls.includes, "-c", path, "-o", target]
            )
            objects.append(target)
        executable = cls.root / ("robot-native" + (".exe" if os.name == "nt" else ""))
        arguments = [*map(str, [*objects, *cls.objects]), "-o", str(executable)]
        response = cls.root / "robot-native-link.rsp"
        response.write_text(
            subprocess.list2cmdline(arguments)
            if os.name == "nt"
            else shlex.join(arguments),
            encoding="utf-8",
        )
        cls.checked([*compiler_command(), "@" + str(response)])
        return executable

    def test_robot_actual_pointer_modal_interrupt_and_reuse(self):
        for case in (
            "open",
            "start",
            "battery",
            "aba",
            "interrupt",
            "suction",
            "room",
            "room_aba",
            "room_battery",
            "modal_gap",
            "modal_guard",
            "unknown",
            "mower",
            "glyph",
            "handoff",
            "locate",
            "release_outside",
        ):
            with self.subTest(case=case):
                self.assertIn(
                    f"ROBOT {case} pass=1",
                    self.checked([self.executable, case]).stdout,
                )
