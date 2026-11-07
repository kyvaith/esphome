"""Actual public LVGL Media presentation, pointer retirement and facet handoff."""

from __future__ import annotations

import hashlib
import json
import os
import shlex
import subprocess
import unittest

from host_light_control import HERE, SOURCE, compiler_command
from test_entity_control_action import ACTION_UNITS, corpus
from test_entity_control_media import MEDIA_UNITS
import test_number_native_input as number

SCENARIOS = (
    "dispatched",
    "momentary",
    "scene",
    "script-running",
    "script-blocked",
    "stop-disabled",
    "unknown",
    "metadata-hold",
    "semantic-cancel",
    "outside",
    "retired",
    "palette",
    "unfit",
    "multiline-guard",
    "unchanged",
)


def fixtures_header():
    rows = [
        "{"
        + json.dumps(r["name"])
        + ","
        + json.dumps(
            json.dumps(r["message"], ensure_ascii=False, separators=(",", ":"))
        )
        + "}"
        for r in corpus()
        if r["accepted"] and r["message"]["kind"] in ("state", "grant")
    ]
    return (
        "struct Fixture {const char *name,*wire;};const Fixture fixtures[]={"
        + ",\n".join(rows)
        + "};"
    )


class ActionNativeInputTests(unittest.TestCase):
    extra_widgets = {"arc"}
    lvgl_options = {"LV_USE_FLOAT": 1}
    extra_features = ()
    fixtures_header = staticmethod(fixtures_header)
    checked = staticmethod(number.NumberNativeInputTests.checked)

    @classmethod
    def setUpClass(cls):
        number.NumberNativeInputTests.setUpClass.__func__(cls)

    @classmethod
    def tearDownClass(cls):
        number.NumberNativeInputTests.tearDownClass.__func__(cls)

    @classmethod
    def build_control(cls):
        (cls.root / "action_owner_fixtures.h").write_text(
            cls.fixtures_header(), encoding="utf8"
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
                    "action_component",
                )
            ],
            *[name + ".cpp" for name in MEDIA_UNITS + ACTION_UNITS],
        ]
        sources = [SOURCE / name for name in units] + [
            HERE / "action_native_input_test.cpp"
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
            "-DUSE_LVGL_MATERIAL_ACTION_CONTROLS",
            *["-DUSE_LVGL_MATERIAL_" + feature for feature in cls.extra_features],
            "-DUSE_LVGL_NAVIGATION_ADMISSION_GUARD",
            "-DARDUINOJSON_POOL_CAPACITY=128",
            "-DLV_CONF_INCLUDE_SIMPLE",
            "-DLV_COLOR_DEPTH=32",
        ]
        objects = []
        for path in sources:
            target = cls.root / ("action-" + path.name + ".o")
            cls.checked(
                [*compiler_command(), *flags, *cls.includes, "-c", path, "-o", target]
            )
            objects.append(target)
        executable = cls.root / ("action-native" + (".exe" if os.name == "nt" else ""))
        arguments = [*map(str, [*objects, *cls.objects]), "-o", str(executable)]
        response = cls.root / "action-link.rsp"
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
