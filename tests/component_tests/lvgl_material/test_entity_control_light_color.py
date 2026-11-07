"""Color1 has bounded storage and shares the existing Detail session fences."""

from copy import deepcopy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from host_light_control import SOURCE, arduino_json, compiler_command


def fixtures():
    here = Path(__file__).parent
    base = json.loads(
        json.loads((here / "detail_wire_fixtures.json").read_text(encoding="utf8"))[0]
    )
    base["kind"] = "grant"
    base["state_seq"] = 1
    base["light_detail"].update(facets=4, state_generation=1, facet="main", view_seq=0)
    color = deepcopy(base)
    color.update(kind="state", state_seq=2)
    color["light_detail"].update(facet="color", view_seq=1)
    color["light_color"] = {
        "version": 1,
        "write_mode": "rgb",
        "source_mode": "rgb",
        "rgb": [76, 255, 92],
        "hue_milli": 125500,
        "saturation_milli": 70250,
        "editable": True,
        "reason": "none",
    }
    good = [base, color]
    for reason, mode, known in (
        ("off", "unknown", False),
        ("unsupported_mode", "other", False),
        ("invalid_color", "hs", False),
        ("invalid_brightness", "color_temp", True),
        ("source_unavailable", "unknown", False),
    ):
        row = deepcopy(color)
        row["light_color"].update(reason=reason, source_mode=mode, editable=False)
        row["on"] = reason not in ("off", "source_unavailable")
        if not known:
            row["light_color"].update(rgb=None, hue_milli=None, saturation_milli=None)
        good.append(row)
    maximum = deepcopy(color)
    maximum.update(
        title='"' * 160,
        card_id="x" * 64,
        revision=9007199254740991,
        state_seq=2147483647,
    )
    maximum["light_color"].update(
        rgb=[255, 255, 255], hue_milli=359999, saturation_milli=100000
    )
    good.append(maximum)
    command = {
        key: value
        for key, value in base.items()
        if key
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
        command_seq=1,
        op="facet_color",
        facet="color",
        view_seq=1,
        expected_state_generation=1,
        target_hue_milli=359999,
        target_saturation_milli=100000,
    )
    good.append(command)
    bad = []
    for key, value in (
        ("hue_milli", 360000),
        ("hue_milli", -1),
        ("hue_milli", True),
        ("saturation_milli", 100001),
        ("saturation_milli", 5.0),
        ("rgb", [255, 0]),
        ("rgb", [255, True, 0]),
        ("rgb", None),
        ("write_mode", "rgbw"),
        ("editable", False),
        ("source_mode", "unknown"),
    ):
        row = deepcopy(color)
        row["light_color"][key] = value
        bad.append(row)
    bad.extend(
        command | patch
        for patch in (
            {"extra": 1},
            {"brightness": 255},
            {"target_hue_milli": 360000},
            {"target_saturation_milli": True},
            {"control_kind": "panel"},
            {"facet": "effect"},
            {"view_seq": 0},
            {"op": "facet_choice"},
        )
    )
    bad.append(base | {"light_color": color["light_color"]})
    bad.append(color | {"kind": "grant"})
    bad.append(color | {"brightness": 200})
    bad.append(color | {"on": False})
    return good, bad


class LightColorHostTests(unittest.TestCase):
    def test_owner_and_wire(self):
        here = Path(__file__).parent
        good, bad = fixtures()
        with tempfile.TemporaryDirectory(prefix="light-color-owner-") as temporary:
            root = Path(temporary)
            defines = root / "esphome/core/defines.h"
            defines.parent.mkdir(parents=True)
            defines.write_text("")
            lines = []
            for name, rows in (("COLOR_FIXTURES", good), ("COLOR_BAD_FIXTURES", bad)):
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
                (here / "light_color_wire_corpus.json").read_text(encoding="utf8")
            )
            lines.append(
                "constexpr const char* COLOR_HA_FIXTURES[]={"
                + ",".join(
                    json.dumps(row["wire"], ensure_ascii=False)
                    for row in corpus["rows"]
                )
                + "};"
            )
            (root / "color_fixtures.h").write_text("\n".join(lines), encoding="utf8")
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
            )
            for enabled in (False, True):
                executable = root / f"color-{enabled}.exe"
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
                        str(here / "light_color_owner_test.cpp"),
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
