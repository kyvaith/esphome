"""Additive tile-event capability through the production codec and owner."""

import copy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from host_light_control import SOURCE, arduino_json, compiler_command
from test_entity_control_action import corpus as action_corpus
from test_info_context import FEATURES
from test_text_control import SOURCES

HERE = Path(__file__).resolve().parent
TILE_FEATURES = [*FEATURES, "INFO_CONTEXT", "DATETIME_CONTROLS", "TEXT_CONTROLS"]


def corpus():
    actual = json.loads((HERE / "tile_event_wire_corpus.json").read_text("utf8"))
    rows = [(r["name"], r["value"], True) for r in actual["rows"]]
    original = next(r[1] for r in rows if r[0] == "first-use")
    for section, key, values in (
        ("panel_caps", "operations", (2, 3, 255, -1, True)),
        ("panel_caps", "action_type", ("event", "tile_event_v1", 5)),
        ("panel_caps", "skip_condition", (False, True)),
        ("panel_caps", "stop_actions", (False, True)),
        ("panel_caps", "run_blocked", ("needs_parameters", "service_unavailable")),
        ("panel_state", "phase", ("enabled", "idle", "unknown")),
        ("panel_state", "current", (0, 1)),
        (None, "eligible_actions", (0, 2)),
        (None, "state_generation", (0, True)),
        (None, "available", (False, 1)),
    ):
        for value in values:
            obj = copy.deepcopy(original)
            (obj if section is None else obj[section])[key] = value
            rows.append((f"invalid-{section}-{key}-{value!r}", obj, False))
    for section in (None, "panel_caps", "panel_state"):
        for key in original if section is None else original[section]:
            obj = copy.deepcopy(original)
            del (obj if section is None else obj[section])[key]
            rows.append((f"missing-{section}-{key}", obj, False))
        obj = copy.deepcopy(original)
        (obj if section is None else obj[section])["custom_payload"] = {}
        rows.append((f"extra-{section}", obj, False))
    rows += [
        ("legacy-" + r["name"], r["message"], r["accepted"]) for r in action_corpus()
    ]
    return rows


def fixtures_header():
    rows = []
    for name, obj, accepted in corpus():
        wire = json.dumps(obj, ensure_ascii=False, separators=(",", ":"))
        tile = obj.get("panel_caps", {}).get("action_type") == "tile_event"
        rows.append(
            "{"
            + ",".join(
                (
                    json.dumps(name),
                    json.dumps(wire),
                    str(accepted).lower(),
                    str(tile).lower(),
                )
            )
            + "}"
        )
    return (
        "struct TileCase {const char *name, *wire; bool accepted, tile;};\nconst TileCase cases[]={\n"
        + ",\n".join(rows)
        + "\n};\n"
    )


class TileEventTests(unittest.TestCase):
    def test_codec_owner_and_default_off(self):
        with tempfile.TemporaryDirectory(prefix="tile-event-") as directory:
            root = Path(directory)
            defines = root / "esphome/core/defines.h"
            defines.parent.mkdir(parents=True)
            defines.write_text("")
            (root / "tile_event_fixtures.h").write_text(
                fixtures_header(), encoding="utf8"
            )
            for enabled in (False, True):
                exe = root / ("enabled.exe" if enabled else "default-off.exe")
                flags = [
                    "-DUSE_LVGL_MATERIAL_" + f
                    for f in TILE_FEATURES
                    + (["TILE_EVENT_CONTROLS"] if enabled else [])
                ]
                command = [
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
                    *[str(SOURCE / (n + ".cpp")) for n in SOURCES],
                    str(HERE / "tile_event_control_test.cpp"),
                    "-o",
                    str(exe),
                ]
                built = subprocess.run(
                    command,
                    check=False,
                    capture_output=True,
                    text=True,
                    encoding="utf8",
                )
                self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
                result = subprocess.run(
                    [str(exe)],
                    check=False,
                    capture_output=True,
                    text=True,
                    encoding="utf8",
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                decoded = dict(
                    line.split("\t", 1) for line in result.stdout.splitlines()
                )
                expected = [
                    (n, obj)
                    for n, obj, good in corpus()
                    if good
                    and (
                        enabled
                        or obj.get("panel_caps", {}).get("action_type") != "tile_event"
                    )
                ]
                self.assertEqual(len(decoded), len(expected))
                for name, obj in expected:
                    self.assertEqual(json.loads(decoded[name]), obj, name)
                print(
                    ("enabled " if enabled else "default-off ") + result.stderr.strip()
                )


if __name__ == "__main__":
    unittest.main()
