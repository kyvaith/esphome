"""Strict detail negotiation, typed facets, and same-owner generation fencing."""

import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from host_light_control import SOURCE, arduino_json, compiler_command


class DetailOwnerHostTests(unittest.TestCase):
    def test_wire_and_owner(self):
        here = Path(__file__).parent
        with tempfile.TemporaryDirectory(prefix="detail-owner-") as temporary:
            root = Path(temporary)
            defines = root / "esphome/core/defines.h"
            defines.parent.mkdir(parents=True)
            defines.write_text("")
            fixtures = json.loads(
                (here / "detail_wire_fixtures.json").read_text(encoding="utf8")
            )
            (root / "fixtures.h").write_text(
                "constexpr const char* FIXTURES[]={"
                + ",".join(json.dumps(x, ensure_ascii=False) for x in fixtures)
                + "};",
                encoding="utf8",
            )
            sources = [
                "material_entity_control.cpp",
                "material_entity_control_protocol.cpp",
                "material_entity_control_codec.cpp",
                "material_number_domain.cpp",
                "material_entity_control_generic.cpp",
                "material_entity_control_generic_protocol.cpp",
                "material_entity_control_panel.cpp",
                "material_entity_control_panel_protocol.cpp",
                "material_entity_control_detail.cpp",
                "material_entity_control_detail_protocol.cpp",
            ]
            reviews = json.loads(
                (here / "detail_review_fixtures.json").read_text(encoding="utf8")
            )
            (root / "reviews.h").write_text(
                "constexpr const char* REVIEWS[][3]={"
                + ",".join(
                    "{"
                    + ",".join(json.dumps(x, ensure_ascii=False) for x in case)
                    + ",nullptr}"
                    if len(case) == 2
                    else "{"
                    + ",".join(json.dumps(x, ensure_ascii=False) for x in case)
                    + "}"
                    for case in reviews
                )
                + "};",
                encoding="utf8",
            )
            executable = root / "detail.exe"
            command = [
                *compiler_command(),
                "-std=c++20",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-DARDUINOJSON_POOL_CAPACITY=128",
                "-DUSE_LVGL_MATERIAL_NUMBER_STEPPER",
                "-DUSE_LVGL_MATERIAL_PANEL_CONTROLS",
                "-DUSE_LVGL_MATERIAL_THERMOSTAT",
                "-DUSE_LVGL_MATERIAL_DETAIL_CONTROLS",
                "-I",
                str(root),
                "-I",
                str(SOURCE),
                "-I",
                str(arduino_json()),
                *[str(SOURCE / x) for x in sources],
                str(here / "detail_owner_test.cpp"),
                "-o",
                str(executable),
            ]
            result = subprocess.run(
                command, check=False, capture_output=True, text=True, timeout=120
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run(
                [str(executable)],
                check=False,
                capture_output=True,
                text=True,
                timeout=25,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout, end="")
