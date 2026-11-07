"""Explicit Light detail never downgrades into legacy Light or ordinary actions."""

from __future__ import annotations

import hashlib
from pathlib import Path
import subprocess
import tempfile
import unittest

from host_light_control import HERE, SOURCE, arduino_json, compiler_command


class DetailTileHostTests(unittest.TestCase):
    def test_existing_routes_and_explicit_profile_capture(self) -> None:
        units = [
            SOURCE / name
            for name in ("material_tile_surface.h", "material_tile_surface.cpp")
        ]
        cases = [
            HERE / f"tile_{name}_cases.inc"
            for name in (
                "number",
                "choice",
                "cover",
                "fan",
                "humidifier",
                "robot",
                "panel",
                "detail",
            )
        ]
        paths = [*units, HERE / "tile_surface_test.cpp", *cases]
        pins = {path: hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}
        with tempfile.TemporaryDirectory(
            prefix="detail-tile-host-", ignore_cleanup_errors=True
        ) as temporary:
            root = Path(temporary)
            for path in units:
                (root / (path.name + ".inc")).write_text(
                    "\n".join(
                        line
                        for line in path.read_text(encoding="utf-8").splitlines()
                        if not line.startswith("#include")
                    ),
                    encoding="utf-8",
                )
            original = paths[2].read_text(encoding="utf-8")
            anchor = "  if (test == 0) {\n"
            self.assertEqual(original.count(anchor), 1)
            harness = root / "detail-tile.cpp"
            harness.write_text(
                original.replace(
                    anchor,
                    "".join(
                        path.read_text(encoding="utf-8") for path in reversed(cases)
                    )
                    + anchor,
                    1,
                ),
                encoding="utf-8",
            )
            flags = [
                "ENTITY_CONTROL",
                "CHOICE",
                "COVER",
                "FAN",
                "HUMIDIFIER",
                "ROBOT",
                "PANEL",
            ]
            for enabled in (True, False):
                executable = root / f"detail-tile-{enabled}.exe"
                defines = [f"-DUSE_LVGL_MATERIAL_{kind}_TILE_ROUTING" for kind in flags]
                if enabled:
                    defines += ["-DUSE_LVGL_MATERIAL_DETAIL_TILE_ROUTING"]
                compiled = subprocess.run(
                    [
                        *compiler_command(),
                        "-std=c++20",
                        "-Wall",
                        "-Wextra",
                        "-Werror",
                        *defines,
                        "-I",
                        str(root),
                        "-I",
                        str(arduino_json()),
                        str(harness),
                        "-o",
                        str(executable),
                    ],
                    capture_output=True,
                    text=True,
                    timeout=180,
                    check=False,
                )
                self.assertEqual(
                    compiled.returncode, 0, compiled.stdout + compiled.stderr
                )
                for case in range(175) if enabled else (0, 1, 174):
                    args = [str(executable), str(case)]
                    if case in (50, 51):
                        args += [
                            str(HERE / "fixtures/native-readback-with-ha-accent.json")
                        ]
                    with self.subTest(enabled=enabled, case=case):
                        result = subprocess.run(
                            args,
                            capture_output=True,
                            text=True,
                            timeout=20,
                            check=False,
                        )
                        self.assertEqual(
                            result.returncode, 0, result.stdout + result.stderr
                        )
                        self.assertIn(f"case {case} PASS", result.stdout)
        self.assertEqual(
            pins,
            {path: hashlib.sha256(path.read_bytes()).hexdigest() for path in paths},
        )
