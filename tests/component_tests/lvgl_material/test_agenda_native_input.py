"""Agenda release-only reads using public LVGL, actual geometry and font metrics."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import unittest

from host_light_control import SOURCE, compiler_command
from test_climate_range_native_input import font_header, widget_header
import test_number_native_input as number

HERE = Path(__file__).resolve().parent
SCENARIOS = (
    "ready",
    "page",
    "slide",
    "changed",
    "metadata",
    "empty",
    "error",
    "limited",
    "retired",
    "disabled",
    "palette",
    "unfit",
)


class AgendaNativeTests(unittest.TestCase):
    extra_widgets = {"arc"}
    lvgl_options = {"LV_USE_FLOAT": 1}
    checked = staticmethod(number.NumberNativeInputTests.checked)

    @classmethod
    def setUpClass(cls):
        number.NumberNativeInputTests.setUpClass.__func__(cls)

    @classmethod
    def tearDownClass(cls):
        number.NumberNativeInputTests.tearDownClass.__func__(cls)

    @classmethod
    def build_control(cls):
        (cls.root / "agenda_widgets.h").write_text(
            widget_header(
                json.loads(
                    (HERE / "agenda_widget_fixture.json").read_text(encoding="utf8")
                )
            ),
            encoding="utf8",
        )
        (cls.root / "agenda_fonts.h").write_text(
            font_header(
                json.loads(
                    (HERE / "climate_range_font_metrics.json").read_text(
                        encoding="utf8"
                    )
                )
            ),
            encoding="utf8",
        )
        sources = [
            *[SOURCE / unit for unit in number.UNITS],
            *[
                SOURCE / ("material_entity_control_" + unit + ".cpp")
                for unit in (
                    "panel",
                    "panel_protocol",
                    "panel_component",
                    "extra",
                    "extra_protocol",
                    "extra_component",
                    "agenda",
                    "agenda_protocol",
                )
            ],
            HERE / "agenda_native_input_test.cpp",
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
            *[
                f"-DUSE_LVGL_MATERIAL_{feature}"
                for feature in (
                    "NUMBER_STEPPER",
                    "PANEL_CONTROLS",
                    "EXTRA_CONTROLS",
                    "INFO_AGENDA",
                )
            ],
            "-DUSE_LVGL_NAVIGATION_ADMISSION_GUARD",
            "-DARDUINOJSON_POOL_CAPACITY=128",
            "-DLV_CONF_INCLUDE_SIMPLE",
            "-DLV_COLOR_DEPTH=32",
        ]
        objects = []
        for path in sources:
            target = cls.root / ("agenda-" + path.name + ".o")
            cls.checked(
                [*compiler_command(), *flags, *cls.includes, "-c", path, "-o", target]
            )
            objects.append(target)
        executable = cls.root / ("agenda-native" + (".exe" if os.name == "nt" else ""))
        args = list(map(str, [*objects, *cls.objects, "-o", executable]))
        response = cls.root / "agenda-native-link.rsp"
        response.write_text(
            subprocess.list2cmdline(args) if os.name == "nt" else shlex.join(args),
            encoding="utf8",
        )
        cls.checked([*compiler_command(), "@" + str(response)])
        return executable

    def test_read_only_navigation_and_presentation(self):
        for case in SCENARIOS:
            with self.subTest(case=case):
                result = self.checked([self.executable, case])
                self.assertIn("RESULT " + case + " pass=1", result.stdout)
