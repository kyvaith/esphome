"""Add typed Number routing cases without editing the existing83 tile assertions."""

import hashlib
from pathlib import Path
import subprocess
import tempfile
import unittest

from host_light_control import SOURCE, arduino_json, compiler_command

HERE = Path(__file__).resolve().parent


class NumberTileHostTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="number-tile-host-")
        cls.root = Path(cls.temporary.name)
        cls.pins = {}
        for name in ("material_tile_surface.h", "material_tile_surface.cpp"):
            path = SOURCE / name
            cls.pins[path] = hashlib.sha256(path.read_bytes()).hexdigest()
            source = "\n".join(
                line
                for line in path.read_text(encoding="utf-8").splitlines()
                if not line.startswith("#include")
            )
            (cls.root / (name + ".inc")).write_text(source, encoding="utf-8")
        old = HERE / "tile_surface_test.cpp"
        cls.pins[old] = hashlib.sha256(old.read_bytes()).hexdigest()
        original = old.read_text(encoding="utf-8")
        anchor = "  if (test == 0) {\n"
        assert original.count(anchor) == 1, (
            "Existing tile harness changed; review additive insertion"
        )
        source = original.replace(
            anchor,
            (HERE / "tile_number_cases.inc").read_text(encoding="utf-8") + anchor,
            1,
        )
        test = cls.root / "number-tile.cpp"
        test.write_text(source, encoding="utf-8")
        cls.executables = []
        for enabled in (False, True):
            executable = cls.root / f"tile-{enabled}.exe"
            command = [*compiler_command(), "-std=c++20", "-Wall", "-Wextra", "-Werror"]
            if enabled:
                command += ["-DUSE_LVGL_MATERIAL_ENTITY_CONTROL_TILE_ROUTING"]
            cls.checked(
                [*command, "-I", cls.root, "-I", arduino_json(), test, "-o", executable]
            )
            cls.executables.append(executable)

    @staticmethod
    def checked(command):
        result = subprocess.run(
            [str(x) for x in command],
            capture_output=True,
            text=True,
            encoding="utf-8",
            timeout=180,
            check=False,
        )
        if result.returncode:
            raise AssertionError(f"{command!r}\n{result.stdout}{result.stderr}")
        return result

    @classmethod
    def tearDownClass(cls):
        try:
            assert all(
                hashlib.sha256(path.read_bytes()).hexdigest() == digest
                for path, digest in cls.pins.items()
            )
        finally:
            cls.temporary.cleanup()

    def test_existing83_with_flag_off_and_on_plus9_typed_routes(self):
        for enabled, executable in enumerate(self.executables):
            for case in range(92 if enabled else 83):
                with self.subTest(enabled=bool(enabled), case=case):
                    command = [executable, case]
                    if case in (50, 51):
                        command += [
                            HERE / "fixtures/native-readback-with-ha-accent.json"
                        ]
                    self.assertIn(f"case {case} PASS", self.checked(command).stdout)
