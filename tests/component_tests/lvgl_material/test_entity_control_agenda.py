"""Agenda shares Info ownership and keeps reads separate from provider commands."""

import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from host_light_control import SOURCE, arduino_json, compiler_command

UNITS = (
    "material_entity_control",
    "material_entity_control_protocol",
    "material_entity_control_codec",
    "material_number_domain",
    "material_entity_control_generic",
    "material_entity_control_generic_protocol",
    "material_entity_control_panel",
    "material_entity_control_panel_protocol",
    "material_entity_control_extra",
    "material_entity_control_extra_protocol",
    "material_entity_control_agenda",
    "material_entity_control_agenda_protocol",
)


def fixture_header():
    rows = json.loads(
        (Path(__file__).parent / "agenda_wire_corpus.json").read_text(encoding="utf8")
    )["rows"]
    return (
        "struct Wire {const char *name, *raw;};\nconst Wire WIRES[]={\n"
        + ",\n".join(
            "{"
            + json.dumps(row["name"])
            + ","
            + json.dumps(
                json.dumps(row["value"], ensure_ascii=False, separators=(",", ":")),
                ensure_ascii=False,
            )
            + "}"
            for row in rows
        )
        + "};\n"
    )


class AgendaHostTests(unittest.TestCase):
    def test_owner_codec_and_default_disabled(self):
        here = Path(__file__).parent
        with tempfile.TemporaryDirectory(prefix="agenda-owner-") as temporary:
            root = Path(temporary)
            defines = root / "esphome/core/defines.h"
            defines.parent.mkdir(parents=True)
            defines.write_text("")
            (root / "agenda_cases.h").write_text(fixture_header(), encoding="utf8")
            for enabled in (False, True):
                with self.subTest(enabled=enabled):
                    executable = root / f"agenda-{enabled}.exe"
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
                        "-DUSE_LVGL_MATERIAL_EXTRA_CONTROLS",
                    ]
                    if enabled:
                        command.append("-DUSE_LVGL_MATERIAL_INFO_AGENDA")
                    command += [
                        "-I",
                        str(root),
                        "-I",
                        str(SOURCE),
                        "-I",
                        str(arduino_json()),
                        *[str(SOURCE / (unit + ".cpp")) for unit in UNITS],
                        str(here / "agenda_codec_test.cpp"),
                        "-o",
                        str(executable),
                    ]
                    result = subprocess.run(
                        command,
                        capture_output=True,
                        text=True,
                        timeout=120,
                        check=False,
                    )
                    self.assertEqual(
                        result.returncode, 0, result.stdout + result.stderr
                    )
                    result = subprocess.run(
                        [str(executable)],
                        capture_output=True,
                        text=True,
                        timeout=25,
                        check=False,
                    )
                    self.assertEqual(
                        result.returncode, 0, result.stdout + result.stderr
                    )
                    print(result.stdout, end="")
