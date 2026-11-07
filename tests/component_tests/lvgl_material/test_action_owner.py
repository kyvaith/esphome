"""Typed Action owner semantics on the real closed codec/model."""

from __future__ import annotations
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from host_light_control import SOURCE, arduino_json, compiler_command
from test_entity_control_extra import features, sources
from test_entity_control_media import MEDIA_UNITS
from test_entity_control_action import ACTION_UNITS, corpus

HERE = Path(__file__).resolve().parent


class ActionOwnerTests(unittest.TestCase):
    def test_owner(self):
        with tempfile.TemporaryDirectory(prefix="action-owner-") as directory:
            root = Path(directory)
            defines = root / "esphome/core/defines.h"
            defines.parent.mkdir(parents=True)
            defines.write_text("")
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
            (root / "action_owner_fixtures.h").write_text(
                "struct Fixture {const char *name,*wire;};const Fixture fixtures[]={"
                + ",\n".join(rows)
                + "};",
                encoding="utf8",
            )
            units = [
                SOURCE / (name + ".cpp")
                for name in sources + MEDIA_UNITS + ACTION_UNITS
            ]
            exe = root / "owner.exe"
            args = [
                *compiler_command(),
                "-std=c++20",
                "-UNDEBUG",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-DARDUINOJSON_POOL_CAPACITY=128",
                *[
                    "-DUSE_LVGL_MATERIAL_" + f
                    for f in features + ["MEDIA_CONTROLS", "ACTION_CONTROLS"]
                ],
                "-I" + str(root),
                "-I" + str(SOURCE),
                "-I" + str(arduino_json()),
                *map(str, units),
                str(HERE / "action_owner_test.cpp"),
                "-o",
                str(exe),
            ]
            p = subprocess.run(args, capture_output=True, text=True, encoding="utf8")
            self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
            p = subprocess.run(
                [str(exe)], capture_output=True, text=True, encoding="utf8"
            )
            self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
            print(p.stdout.strip())


if __name__ == "__main__":
    unittest.main()
