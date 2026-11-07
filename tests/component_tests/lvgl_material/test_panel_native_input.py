"""Panel pointer handling and capability visibility using public LVGL widgets."""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import shlex
import subprocess
import unittest

from host_light_control import HERE, SOURCE, compiler_command
import test_number_native_input as number


class PanelNativeInputTests(unittest.TestCase):
    checked = staticmethod(number.NumberNativeInputTests.checked)

    @classmethod
    def setUpClass(cls):
        number.NumberNativeInputTests.setUpClass.__func__(cls)

    @classmethod
    def tearDownClass(cls):
        number.NumberNativeInputTests.tearDownClass.__func__(cls)

    @classmethod
    def build_control(cls) -> Path:
        clock = cls.root / "esphome/components/time/real_time_clock.h"
        clock.parent.mkdir(parents=True, exist_ok=True)
        clock.write_text(
            "#pragma once\n#include <cstdint>\nnamespace esphome::time { struct ClockValue { uint32_t timestamp; bool is_valid() const { return timestamp >= 946684800U; } }; class RealTimeClock { public: uint32_t timestamp{1793790000}; ClockValue utcnow() const { return {timestamp}; } }; }\n"
        )
        sources = [
            *[SOURCE / unit for unit in number.UNITS],
            SOURCE / "material_entity_control_panel.cpp",
            SOURCE / "material_entity_control_panel_protocol.cpp",
            SOURCE / "material_entity_control_panel_component.cpp",
            SOURCE / "material_entity_control_humidifier.cpp",
            SOURCE / "material_entity_control_humidifier_component.cpp",
            SOURCE / "material_entity_control_fan.cpp",
            SOURCE / "material_entity_control_fan_component.cpp",
            SOURCE / "material_entity_control_cover.cpp",
            SOURCE / "material_entity_control_cover_component.cpp",
            SOURCE / "material_entity_control_choice.cpp",
            SOURCE / "material_entity_control_choice_component.cpp",
            HERE / "panel_native_input_test.cpp",
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
            "-DUSE_LVGL_MATERIAL_PANEL_CONTROLS",
            "-DUSE_LVGL_MATERIAL_TIMER_PANEL",
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
            target = cls.root / ("panel-" + path.name + ".o")
            cls.checked(
                [*compiler_command(), *flags, *cls.includes, "-c", path, "-o", target]
            )
            objects.append(target)
        executable = cls.root / ("panel-native" + (".exe" if os.name == "nt" else ""))
        arguments = [*map(str, [*objects, *cls.objects]), "-o", str(executable)]
        response = cls.root / "panel-native-link.rsp"
        response.write_text(
            subprocess.list2cmdline(arguments)
            if os.name == "nt"
            else shlex.join(arguments),
            encoding="utf-8",
        )
        cls.checked([*compiler_command(), "@" + str(response)])
        return executable

    def test_panel_actual_pointer_modal_interrupt_and_reuse(self):
        for case in (
            "timer_open",
            "timer_start",
            "timer_draft",
            "timer_draft_aba",
            "timer_draft_renewal",
            "timer_clock",
            "timer_sample",
            "timer_zero",
            "timer_hidden",
            "timer_pause",
            "timer_release_outside",
            "remote_apply",
            "remote_aba",
            "remote_gap",
            "remote_guard",
            "remote_unknown",
            "remote_glyph",
            "remote_power_on",
            "remote_power_unknown",
            "remote_power_unsupported",
            "power_on",
            "power_unknown",
            "power_unsupported",
            "handoff",
        ):
            with self.subTest(case=case):
                self.assertIn(
                    f"PANEL {case} pass=1",
                    self.checked([self.executable, case]).stdout,
                )
