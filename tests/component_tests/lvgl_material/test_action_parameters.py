"""Closed ActionParams1 wire and owner fences, with legacy Action2 preserved."""

from __future__ import annotations

import copy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from host_light_control import SOURCE, arduino_json, compiler_command
from test_entity_control_action import ACTION_UNITS, corpus
from test_entity_control_extra import features, sources
from test_entity_control_media import MEDIA_UNITS

HERE = Path(__file__).resolve().parent


def parameter_cases():
    base = copy.deepcopy(
        next(r["message"] for r in corpus() if r["name"] == "script-idle.json")
    )
    base["panel_caps"].update(
        parameters_version=1,
        parameter_status="ready",
        parameter_label="Confirm",
        parameter_default=None,
    )
    rows = []

    def put(name, message, accepted=True, legacy=False):
        rows.append(
            {
                "name": name,
                "message": copy.deepcopy(message),
                "accepted": accepted,
                "legacy": legacy,
            }
        )

    for value in (None, False, True):
        m = copy.deepcopy(base)
        m["panel_caps"]["parameter_default"] = value
        put("default-" + str(value), m)
    for status, block in [
        ("unsupported", "needs_parameters"),
        ("metadata_unavailable", "metadata_unavailable"),
    ]:
        m = copy.deepcopy(base)
        m["panel_caps"].update(
            parameter_status=status, run_blocked=block, parameter_label=""
        )
        m["eligible_actions"] = 0
        put(status, m)
    m = copy.deepcopy(base)
    m["panel_caps"]["parameter_label"] = "X" * 96
    put("maximum-label", m)
    for key, value in [
        ("parameters_version", True),
        ("parameters_version", 2),
        ("parameter_status", "bad"),
        ("parameter_label", ""),
        ("parameter_label", "X" * 97),
        ("parameter_label", "bad\nline"),
        ("parameter_default", 0),
        ("parameter_default", "false"),
        ("action_type", "automation"),
        ("run_blocked", "needs_parameters"),
    ]:
        m = copy.deepcopy(base)
        m["panel_caps"][key] = value
        put("bad-" + key + "-" + str(value), m, False)
    for key in (
        "parameters_version",
        "parameter_status",
        "parameter_label",
        "parameter_default",
    ):
        m = copy.deepcopy(base)
        del m["panel_caps"][key]
        put("missing-" + key, m, False)
    for status in ("unsupported", "metadata_unavailable"):
        m = copy.deepcopy(base)
        m["panel_caps"]["parameter_status"] = status
        put("bad-contradiction-" + status, m, False)
    command = {
        k: v
        for k, v in base.items()
        if k
        in (
            "v",
            "type",
            "channel",
            "open_seq",
            "slot",
            "card_id",
            "revision",
            "control_kind",
            "token",
        )
    }
    command.update(
        kind="command",
        action_version=1,
        command_seq=1,
        expected_state_generation=1,
        op="run",
        action_parameters_version=1,
        parameter_value=False,
    )
    for value in (False, True):
        m = copy.deepcopy(command)
        m["parameter_value"] = value
        put("run-" + str(value), m)
    for key, value in [
        ("parameter_value", None),
        ("parameter_value", 0),
        ("parameter_value", "false"),
        ("action_parameters_version", True),
        ("action_parameters_version", 2),
        ("op", "stop"),
    ]:
        m = copy.deepcopy(command)
        m[key] = value
        put("bad-command-" + key + "-" + str(value), m, False)
    for key in ("parameter_value", "action_parameters_version"):
        m = copy.deepcopy(command)
        del m[key]
        put("missing-command-" + key, m, False)
    for r in corpus():
        if r["accepted"]:
            put("legacy-" + r["name"], r["message"], legacy=True)
    for row in json.loads(
        (HERE / "action_parameters_wire_corpus.json").read_text(encoding="utf8")
    ):
        put("ha-" + row["name"], row["value"])
    return base, rows


class ActionParameterTests(unittest.TestCase):
    def test_codec_and_owner_default_off(self):
        base, rows = parameter_cases()
        with tempfile.TemporaryDirectory(prefix="action-parameters-") as directory:
            root = Path(directory)
            p = root / "esphome/core/defines.h"
            p.parent.mkdir(parents=True)
            p.write_text("")
            values = []
            for r in rows:
                wire = json.dumps(
                    r["message"], ensure_ascii=False, separators=(",", ":")
                )
                values.append(
                    "{"
                    + ",".join(
                        [
                            json.dumps(r["name"]),
                            json.dumps(wire),
                            str(r["accepted"]).lower(),
                            str(r["legacy"]).lower(),
                        ]
                    )
                    + "}"
                )
            wire = json.dumps(base, separators=(",", ":"))
            values.append(
                '{"duplicate-caps",'
                + json.dumps(
                    wire.replace(
                        '"parameters_version":1',
                        '"parameters_version":1,"parameters_version":1',
                    )
                )
                + ",false,false}"
            )
            bind = {
                "v": 1,
                "type": "entity_action_parameters_v1",
                "channel": "a" * 32,
                "parameters_version": 1,
            }
            bad = []
            for key, value in [
                ("v", True),
                ("parameters_version", True),
                ("parameters_version", 0),
                ("channel", "x" * 32),
                ("extra", 1),
            ]:
                b = {**bind, key: value}
                bad.append(json.dumps(json.dumps(b, separators=(",", ":"))))
            b = json.dumps(bind, separators=(",", ":"))
            bad.extend(
                [json.dumps(b + "{}"), json.dumps(b.replace('"v":1', '"v":1,"v":1'))]
            )
            (root / "action_parameters_fixtures.h").write_text(
                "struct Case {const char *name,*wire;bool accepted,legacy;};const Case cases[]={"
                + ",\n".join(values)
                + "};\nconst char *ready_wire="
                + json.dumps(wire)
                + ";\nconst char *bad_binds[]={"
                + ",".join(bad)
                + "};",
                encoding="utf8",
            )
            for enabled in (False, True):
                exe = root / ("on.exe" if enabled else "off.exe")
                flags = (
                    features
                    + ["MEDIA_CONTROLS", "ACTION_CONTROLS"]
                    + (["ACTION_PARAMETERS"] if enabled else [])
                )
                args = [
                    *compiler_command(),
                    "-std=c++20",
                    "-UNDEBUG",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-DARDUINOJSON_POOL_CAPACITY=128",
                    *["-DUSE_LVGL_MATERIAL_" + f for f in flags],
                    "-I" + str(root),
                    "-I" + str(SOURCE),
                    "-I" + str(arduino_json()),
                    *[
                        str(SOURCE / (n + ".cpp"))
                        for n in sources + MEDIA_UNITS + ACTION_UNITS
                    ],
                    str(HERE / "action_parameters_test.cpp"),
                    "-o",
                    str(exe),
                ]
                p = subprocess.run(
                    args, capture_output=True, text=True, encoding="utf8", check=False
                )
                self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
                p = subprocess.run(
                    [str(exe)], capture_output=True, text=True, encoding="utf8", check=False
                )
                self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
                print(p.stdout.strip())


if __name__ == "__main__":
    unittest.main()
