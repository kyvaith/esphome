"""Live action eligibility uses real LVGL listeners and the existing host build."""

from __future__ import annotations

import os
from pathlib import Path
import re
import shlex
import subprocess
import unittest

from host_light_control import SOURCE, compiler_command
import test_power_presentation_fence as power_host

HERE = Path(__file__).resolve().parent


class TileAvailabilityInputTests(unittest.TestCase):
    checked = staticmethod(power_host.PowerPresentationFenceTests.checked)

    @classmethod
    def setUpClass(cls):
        # Reuse one public LVGL configuration/compiler/object lifecycle.
        power_host.PowerPresentationFenceTests.setUpClass.__func__(cls)

    @classmethod
    def tearDownClass(cls):
        power_host.PowerPresentationFenceTests.tearDownClass.__func__(cls)

    @classmethod
    def build_control(cls, name, acceleration):
        for filename in ("material_tile_surface.h", "material_tile_surface.cpp"):
            source = (SOURCE / filename).read_text(encoding="utf-8")
            source = "\n".join(
                line for line in source.splitlines() if not line.startswith("#include")
            )
            (cls.root / (filename + ".inc")).write_text(source, encoding="utf-8")
        target = cls.root / (name + "-tile.o")
        flags = [
            "-std=c++20",
            "-UNDEBUG",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wno-deprecated-declarations",
            "-DUSE_LVGL_MATERIAL_ENTITY_CONTROL_TILE_ROUTING",
            "-DLV_CONF_INCLUDE_SIMPLE",
            "-DLV_COLOR_DEPTH=32",
        ]
        if acceleration:
            flags += ["-DUSE_LVGL_MATERIAL_TILE_ACTION_AVAILABILITY"]
        if os.name != "nt":
            flags += ["-pthread"]
        cls.checked(
            [
                *compiler_command(),
                *flags,
                *cls.includes,
                "-c",
                str(HERE / "tile_availability_input_test.cpp"),
                "-o",
                str(target),
            ]
        )
        executable = cls.root / (name + (".exe" if os.name == "nt" else ""))
        arguments = [*map(str, [target, *cls.objects]), "-o", str(executable)]
        if os.name != "nt":
            arguments += ["-pthread"]
        response = cls.root / (name + "-tile-link.rsp")
        response.write_text(
            subprocess.list2cmdline(arguments)
            if os.name == "nt"
            else shlex.join(arguments),
            encoding="utf-8",
        )
        cls.checked([*compiler_command(), "@" + str(response)])
        return executable

    def test_legacy_default_off_and_atomic_wire_types(self):
        for executable in (self.full, self.no_acceleration):
            for case in ("legacy_default", "malformed_atomic"):
                with self.subTest(executable=executable, case=case):
                    self.assertIn(
                        f"RESULT {case} PASS",
                        self.checked([str(executable), case]).stdout,
                    )

    def test_actual_held_press_cancel_and_disabled_style_ownership(self):
        for case in (
            "cancel_reenabled_hold",
            "invalidated_then_disabled_hold",
            "external_disabled_owned",
            "snapshot_and_typed_routes",
        ):
            with self.subTest(case=case):
                self.assertIn(
                    f"RESULT {case} PASS", self.checked([str(self.full), case]).stdout
                )

    def test_bounded_state_cost_for_product_sixteen_bindings(self):
        sizes = []
        for executable in (self.no_acceleration, self.full):
            match = re.search(
                r"SIZE binding=(\d+) surface=(\d+) version=(\d+)",
                self.checked([str(executable), "size"]).stdout,
            )
            self.assertIsNotNone(match)
            sizes.append(tuple(map(int, match.groups())))
        off, on = sizes
        self.assertEqual((off[2], on[2]), (0, 1))
        delta = 16 * (on[0] - off[0]) + on[1] - off[1]
        self.assertGreaterEqual(delta, 0)
        self.assertLessEqual(delta, 128)
        print(
            f"TILE_AVAILABILITY_SIZE off_binding={off[0]} on_binding={on[0]} off_surface={off[1]} on_surface={on[1]} total16_delta={delta}"
        )


if __name__ == "__main__":
    unittest.main()
