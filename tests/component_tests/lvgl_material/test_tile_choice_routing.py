"""Add choice routes to the existing tile harness and preserve all old cases."""

from __future__ import annotations

import hashlib
from pathlib import Path
import subprocess
import tempfile
import unittest

from host_light_control import HERE, SOURCE, arduino_json, compiler_command


class ChoiceTileHostTests(unittest.TestCase):
    def test_existing92_and_ten_choice_routes_preserve_capture_and_no_fallback(
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
            )
        ]
        pins = {path: hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}
        with tempfile.TemporaryDirectory(
            prefix="choice-tile-host-", ignore_cleanup_errors=True
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
                paths[4].read_text(encoding="utf-8")
                + paths[3].read_text(encoding="utf-8")
                + anchor,
                1,
            )
            harness = root / "choice-tile.cpp"
            harness.write_text(source, encoding="utf-8")
            executable = root / "choice-tile.exe"
            command = [
                *compiler_command(),
                "-std=c++20",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-DUSE_LVGL_MATERIAL_ENTITY_CONTROL_TILE_ROUTING",
                "-DUSE_LVGL_MATERIAL_CHOICE_TILE_ROUTING",
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
            for case in range(102):
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
