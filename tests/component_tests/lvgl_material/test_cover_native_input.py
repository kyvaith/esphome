"""Choice pointer retirement and presentation with public LVGL, not a widget mock."""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import shlex
import subprocess
import unittest

from host_light_control import HERE, SOURCE, compiler_command
import test_number_native_input as number


class CoverNativeInputTests(unittest.TestCase):
    HARNESS = "cover_native_input_test.cpp"
    EXTRA_FLAGS = ()
    checked = staticmethod(number.NumberNativeInputTests.checked)

    @classmethod
    def setUpClass(cls) -> None:
        number.NumberNativeInputTests.setUpClass.__func__(cls)

    @classmethod
    def tearDownClass(cls) -> None:
        number.NumberNativeInputTests.tearDownClass.__func__(cls)

    @classmethod
    def build_control(cls) -> Path:
        sources = [
            *[SOURCE / unit for unit in number.UNITS],
            SOURCE / "material_entity_control_choice.cpp",
            SOURCE / "material_entity_control_choice_component.cpp",
            SOURCE / "material_entity_control_cover.cpp",
            SOURCE / "material_entity_control_cover_component.cpp",
            HERE / cls.HARNESS,
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
            "-DUSE_LVGL_MATERIAL_CHOICE_SELECTOR",
            "-DUSE_LVGL_MATERIAL_COVER_CONTROLS",
            "-DUSE_LVGL_NAVIGATION_ADMISSION_GUARD",
            "-DARDUINOJSON_POOL_CAPACITY=128",
            "-DLV_CONF_INCLUDE_SIMPLE",
            "-DLV_COLOR_DEPTH=32",
            *cls.EXTRA_FLAGS,
        ]
        objects = []
        for path in sources:
            target = cls.root / ("cover-" + path.name + ".o")
            cls.checked(
                [*compiler_command(), *flags, *cls.includes, "-c", path, "-o", target]
            )
            objects.append(target)
        executable = cls.root / ("cover-native" + (".exe" if os.name == "nt" else ""))
        arguments = [*map(str, [*objects, *cls.objects]), "-o", str(executable)]
        response = cls.root / "cover-native-link.rsp"
        response.write_text(
            subprocess.list2cmdline(arguments)
            if os.name == "nt"
            else shlex.join(arguments),
            encoding="utf-8",
        )
        cls.checked([*compiler_command(), "@" + str(response)])
        return executable

    def test_local_selection_apply_held_changes_and_domain_handoff(self) -> None:
        for case in (
            "open",
            "motion",
            "unknown",
            "reverse",
            "handoff",
            "renewal",
            "aba",
            "metadata",
        ):
            with self.subTest(case=case):
                result = self.checked([self.executable, case])
                self.assertIn(f"COVER {case} pass=1", result.stdout)
