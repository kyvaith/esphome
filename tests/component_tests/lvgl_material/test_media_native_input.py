"""Actual public LVGL Media presentation, pointer retirement and facet handoff."""

from __future__ import annotations

import hashlib
import os
import shlex
import subprocess
import unittest

from host_light_control import HERE, SOURCE, compiler_command
from test_entity_control_media import MEDIA_UNITS, fixtures_header
import test_number_native_input as number

SCENARIOS = (
    "main",
    "tv",
    "release",
    "semantic-cancel",
    "metadata-hold",
    "source-facet",
    "volume-facet",
    "volume-tap",
    "volume-drag",
    "volume-cancel",
    "volume-back",
    "volume-back-transition",
    "volume-back-transition-configured",
    "volume-drain-failure",
    "palette",
    "palette-owned-children",
    "retired",
    "unchanged",
    "unknown",
    "unavailable",
)


class MediaNativeInputTests(unittest.TestCase):
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
        (cls.root / "media_owner_fixtures.h").write_text(
            fixtures_header(), encoding="utf8"
        )
        units = [
            *number.UNITS,
            *[
                "material_entity_control_" + name + ".cpp"
                for name in (
                    "panel",
                    "panel_protocol",
                    "panel_component",
                    "extra",
                    "extra_protocol",
                    "extra_component",
                    "media_component",
                )
            ],
            *[name + ".cpp" for name in MEDIA_UNITS],
        ]
        sources = [SOURCE / name for name in units] + [
            HERE / "media_native_input_test.cpp"
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
            "-DUSE_LVGL_MATERIAL_EXTRA_CONTROLS",
            "-DUSE_LVGL_MATERIAL_MEDIA_CONTROLS",
            "-DUSE_LVGL_NAVIGATION_ADMISSION_GUARD",
            "-DARDUINOJSON_POOL_CAPACITY=128",
            "-DLV_CONF_INCLUDE_SIMPLE",
            "-DLV_COLOR_DEPTH=32",
        ]
        objects = []
        for path in sources:
            target = cls.root / ("media-" + path.name + ".o")
            cls.checked(
                [*compiler_command(), *flags, *cls.includes, "-c", path, "-o", target]
            )
            objects.append(target)
        executable = cls.root / ("media-native" + (".exe" if os.name == "nt" else ""))
        arguments = [*map(str, [*objects, *cls.objects]), "-o", str(executable)]
        response = cls.root / "media-link.rsp"
        response.write_text(
            subprocess.list2cmdline(arguments)
            if os.name == "nt"
            else shlex.join(arguments),
            encoding="utf8",
        )
        cls.checked([*compiler_command(), "@" + str(response)])
        return executable

    def test_profiles_observation_and_pointer_retirement(self):
        for case in SCENARIOS:
            with self.subTest(case=case):
                self.assertIn(
                    "RESULT " + case + " pass=1",
                    self.checked([self.executable, case]).stdout,
                )
