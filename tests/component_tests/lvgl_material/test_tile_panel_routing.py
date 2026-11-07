"""Fan routes preserve existing domain capture and never fall through to toggles."""

from __future__ import annotations

import hashlib
from pathlib import Path
import subprocess
import tempfile
import unittest

from host_light_control import HERE, SOURCE, arduino_json, compiler_command


class PanelTileHostTests(unittest.TestCase):
    def test_existing142_and_ten_panel_routes_preserve_capture_and_no_fallback(
        self,
    ) -> None:
        paths = [
            SOURCE / name
            for name in ("material_tile_surface.h", "material_tile_surface.cpp")
        ]
        paths += [
            HERE / name
            for name in (
                "tile_surface_test.cpp",
                "tile_number_cases.inc",
                "tile_choice_cases.inc",
                "tile_cover_cases.inc",
                "tile_fan_cases.inc",
                "tile_humidifier_cases.inc",
                "tile_robot_cases.inc",
                "tile_panel_cases.inc",
            )
        ]
        pins = {path: hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}
        with tempfile.TemporaryDirectory(
            prefix="humidity-tile-host-", ignore_cleanup_errors=True
        ) as temporary:
            root = Path(temporary)
            for path in paths[:2]:
                source = "\n".join(
                    line
                    for line in path.read_text(encoding="utf-8").splitlines()
                    if not line.startswith("#include")
                )
                (root / (path.name + ".inc")).write_text(source, encoding="utf-8")
            original = paths[2].read_text(encoding="utf-8")
            anchor = "  if (test == 0) {\n"
            self.assertEqual(original.count(anchor), 1)
            source = original.replace(
                anchor,
                paths[9].read_text(encoding="utf-8")
                + paths[8].read_text(encoding="utf-8")
                + paths[7].read_text(encoding="utf-8")
                + paths[6].read_text(encoding="utf-8")
                + paths[5].read_text(encoding="utf-8")
                + paths[4].read_text(encoding="utf-8")
                + paths[3].read_text(encoding="utf-8")
                + anchor,
                1,
            )
            harness = root / "humidity-tile.cpp"
            harness.write_text(source, encoding="utf-8")
            executable = root / "humidity-tile.exe"
            command = [
                *compiler_command(),
                "-std=c++20",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-DUSE_LVGL_MATERIAL_ENTITY_CONTROL_TILE_ROUTING",
                "-DUSE_LVGL_MATERIAL_CHOICE_TILE_ROUTING",
                "-DUSE_LVGL_MATERIAL_COVER_TILE_ROUTING",
                "-DUSE_LVGL_MATERIAL_FAN_TILE_ROUTING",
                "-DUSE_LVGL_MATERIAL_HUMIDIFIER_TILE_ROUTING",
                "-DUSE_LVGL_MATERIAL_ROBOT_TILE_ROUTING",
                "-DUSE_LVGL_MATERIAL_PANEL_TILE_ROUTING",
                "-I",
                str(root),
                "-I",
                str(arduino_json()),
                str(harness),
                "-o",
                str(executable),
            ]
            compiled = subprocess.run(
                command, capture_output=True, text=True, timeout=120, check=False
            )
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            for case in range(152):
                args = [str(executable), str(case)]
                if case in (50, 51):
                    args += [str(HERE / "fixtures/native-readback-with-ha-accent.json")]
                with self.subTest(case=case):
                    result = subprocess.run(
                        args, capture_output=True, text=True, timeout=20, check=False
                    )
                    self.assertEqual(
                        result.returncode, 0, result.stdout + result.stderr
                    )
                    self.assertIn(f"case {case} PASS", result.stdout)
        self.assertEqual(
            pins,
            {path: hashlib.sha256(path.read_bytes()).hexdigest() for path in paths},
        )
