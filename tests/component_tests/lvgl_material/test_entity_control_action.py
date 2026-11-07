"""Closed Action wire grammar, bounded allocator and default-off compatibility."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from host_light_control import SOURCE, arduino_json, compiler_command
from test_entity_control_extra import features, sources
from test_entity_control_media import MEDIA_UNITS

HERE = Path(__file__).resolve().parent
ACTION_UNITS = [
    "material_entity_control_action",
    "material_entity_control_action_protocol",
    "material_entity_control_action_owner",
]


def corpus():
    rows = json.loads((HERE / "action_wire_corpus.json").read_text(encoding="utf8"))
    completed = next(
        row["message"] for row in rows
        if row["message"].get("status") == "service_completed" and row["accepted"]
    )
    for kind in ("light", "number", "choice", "cover", "fan", "humidifier", "robot", "panel"):
        rows.append({
            "name": "dispatched-" + kind,
            "message": {**completed, "control_kind": kind, "status": "service_dispatched"},
            "accepted": kind == "panel",
        })
    for reason in ("service_error", "service_timeout", "expired"):
        rows.append({
            "name": "dispatched-invalid-" + reason,
            "message": {**completed, "status": "service_dispatched", "reason": reason},
            "accepted": False,
        })
    return rows


def common_result(row):
    return row["message"]["kind"] == "result" and row["message"].get("status") != "service_dispatched"


class ActionCodecTests(unittest.TestCase):
    def test_closed_codec_default_off(self):
        with tempfile.TemporaryDirectory(prefix="action-codec-") as directory:
            root = Path(directory)
            defines = root / "esphome/core/defines.h"
            defines.parent.mkdir(parents=True)
            defines.write_text("")
            rows = []
            for row in corpus():
                wire = json.dumps(
                    row["message"], separators=(",", ":"), ensure_ascii=False
                )
                rows.append(
                    "{"
                    + ",".join(
                        (
                            json.dumps(row["name"]),
                            json.dumps(wire),
                            str(row["accepted"]).lower(),
                            str(common_result(row)).lower(),
                        )
                    )
                    + "}"
                )
            (root / "action_codec_cases.h").write_text(
                "struct Case {const char *name,*wire;bool accepted,common;};\nconst Case cases[]={"
                + ",\n".join(rows)
                + "};",
                encoding="utf8",
            )
            units = [
                SOURCE / (name + ".cpp")
                for name in sources + MEDIA_UNITS + ACTION_UNITS
            ]
            for enabled in (True, False):
                executable = root / ("enabled.exe" if enabled else "default-off.exe")
                flags = [
                    "-DUSE_LVGL_MATERIAL_" + feature
                    for feature in features
                    + ["MEDIA_CONTROLS"]
                    + (["ACTION_CONTROLS"] if enabled else [])
                ]
                result = subprocess.run(
                    [
                        *compiler_command(),
                        "-std=c++20",
                        "-UNDEBUG",
                        "-Wall",
                        "-Wextra",
                        "-Werror",
                        "-DARDUINOJSON_POOL_CAPACITY=128",
                        *flags,
                        "-I" + str(root),
                        "-I" + str(SOURCE),
                        "-I" + str(arduino_json()),
                        *map(str, units),
                        str(HERE / "action_codec_test.cpp"),
                        "-o",
                        str(executable),
                    ],
                    check=False,
                    capture_output=True,
                    text=True,
                    encoding="utf8",
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                result = subprocess.run(
                    [str(executable)], capture_output=True, text=True, encoding="utf8"
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                decoded = dict(
                    line.split("\t", 1) for line in result.stdout.splitlines()
                )
                expected = [
                    row
                    for row in corpus()
                    if row["accepted"]
                    and (enabled or common_result(row))
                ]
                self.assertEqual(len(decoded), len(expected))
                for row in expected:
                    self.assertEqual(
                        json.loads(decoded[row["name"]]), row["message"], row["name"]
                    )
                print("enabled" if enabled else "default-off", result.stderr.strip())


if __name__ == "__main__":
    unittest.main()
