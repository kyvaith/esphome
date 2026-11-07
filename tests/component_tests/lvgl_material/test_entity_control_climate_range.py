"""Range1 shares Detail session fences without a separate owner draft."""

from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from host_light_control import SOURCE, arduino_json, compiler_command


def fixtures():
    corpus = json.loads(
        (Path(__file__).parent / "climate_range_wire_corpus.json").read_text(
            encoding="utf8"
        )
    )
    rows = {row["name"]: json.loads(row["wire"]) for row in corpus["rows"]}
    base = rows["main-grant-negotiated-range"]
    state = rows["range-none"]
    state["state_seq"] = 2
    maximum = deepcopy(state)
    maximum.update(
        title='"' * 160,
        card_id="x" * 64,
        revision=9007199254740991,
        state_seq=2147483647,
    )
    good = [base, state, maximum]
    bad = []
    for key, value in (
        ("low_index", True),
        ("high_index", 2.5),
        ("low_index", -1),
        ("high_index", 57),
        ("low_index", 35),
        ("high_index", None),
        ("reason", "inactive_mode"),
        ("mode", "off"),
        ("editable", False),
        ("current_text", "23.0"),
        ("extra", 1),
    ):
        row = deepcopy(state)
        row["panel_state"][key] = value
        bad.append(row)
    for patch in (
        {"unit": "K"},
        {"min": "-101"},
        {"max": "301"},
        {"step": "0"},
        {"max_index": True},
    ):
        row = deepcopy(state)
        row["panel_state"]["domain"].update(patch)
        bad.append(row)
    bad += [
        state | {"available": False},
        state | {"kind": "grant"},
        state | {"eligible_actions": 0},
    ]
    return good, bad


class ClimateRangeHostTests(unittest.TestCase):
    def test_owner_and_wire(self):
        here = Path(__file__).parent
        good, bad = fixtures()
        with tempfile.TemporaryDirectory(prefix="climate-range-owner-") as temporary:
            root = Path(temporary)
            defines = root / "esphome/core/defines.h"
            defines.parent.mkdir(parents=True)
            defines.write_text("")
            lines = []
            for name, rows in (("RANGE_FIXTURES", good), ("RANGE_BAD_FIXTURES", bad)):
                lines.append(
                    "constexpr const char* "
                    + name
                    + "[]={"
                    + ",".join(
                        json.dumps(
                            json.dumps(row, ensure_ascii=False, separators=(",", ":")),
                            ensure_ascii=False,
                        )
                        for row in rows
                    )
                    + "};"
                )
            corpus = json.loads(
                (here / "climate_range_wire_corpus.json").read_text(encoding="utf8")
            )
            lines.append(
                "constexpr const char* RANGE_HA_FIXTURES[]={"
                + ",".join(
                    json.dumps(row["wire"], ensure_ascii=False)
                    for row in corpus["rows"]
                )
                + "};"
            )
            (root / "range_fixtures.h").write_text("\n".join(lines), encoding="utf8")
            units = (
                "material_entity_control",
                "material_entity_control_protocol",
                "material_entity_control_codec",
                "material_number_domain",
                "material_entity_control_generic",
                "material_entity_control_generic_protocol",
                "material_entity_control_panel",
                "material_entity_control_panel_protocol",
                "material_entity_control_detail",
                "material_entity_control_detail_protocol",
                "material_entity_control_light_color",
                "material_entity_control_climate_range",
            )
            for enabled, color in ((False, False), (True, False), (True, True)):
                executable = root / f"range-{enabled}-{color}.exe"
                command = [
                    *compiler_command(),
                    "-std=c++20",
                    "-UNDEBUG",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-DARDUINOJSON_POOL_CAPACITY=128",
                    "-DUSE_LVGL_MATERIAL_NUMBER_STEPPER",
                    "-DUSE_LVGL_MATERIAL_PANEL_CONTROLS",
                    "-DUSE_LVGL_MATERIAL_THERMOSTAT",
                    "-DUSE_LVGL_MATERIAL_DETAIL_CONTROLS",
                ]
                if enabled:
                    command.append("-DUSE_LVGL_MATERIAL_CLIMATE_RANGE")
                if color:
                    command.append("-DUSE_LVGL_MATERIAL_LIGHT_COLOR")
                command.extend(
                    [
                        "-I",
                        str(root),
                        "-I",
                        str(SOURCE),
                        "-I",
                        str(arduino_json()),
                        *[str(SOURCE / (unit + ".cpp")) for unit in units],
                        str(here / "climate_range_owner_test.cpp"),
                        "-o",
                        str(executable),
                    ]
                )
                result = subprocess.run(
                    command, capture_output=True, text=True, timeout=120, check=False
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                result = subprocess.run(
                    [str(executable)],
                    capture_output=True,
                    text=True,
                    timeout=25,
                    check=False,
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                print(result.stdout, end="")
