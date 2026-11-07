"""Independent Extra wire/admission fixtures against every enabled family."""

import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from host_light_control import SOURCE, arduino_json, compiler_command

HERE = Path(__file__).resolve().parent
sources = [
    "material_entity_control",
    "material_entity_control_protocol",
    "material_entity_control_codec",
    "material_number_domain",
    "material_entity_control_generic",
    "material_entity_control_generic_protocol",
    "material_entity_control_choice",
    "material_entity_control_cover",
    "material_entity_control_fan",
    "material_entity_control_humidifier",
    "material_entity_control_robot",
    "material_entity_control_panel",
    "material_entity_control_panel_protocol",
    "material_entity_control_detail",
    "material_entity_control_detail_protocol",
    "material_entity_control_extra",
    "material_entity_control_extra_protocol",
]
features = [
    "NUMBER_STEPPER",
    "THERMOSTAT",
    "CHOICE_SELECTOR",
    "COVER_CONTROLS",
    "COVER_PRIORITY_STOP",
    "COVER_PANEL",
    "FAN_CONTROLS",
    "HUMIDIFIER_CONTROLS",
    "ROBOT_CONTROLS",
    "PANEL_CONTROLS",
    "DETAIL_CONTROLS",
    "EXTRA_CONTROLS",
]


def corpus_header():
    data = json.loads((HERE / "extra_wire_corpus.json").read_text(encoding="utf8"))

    def quote(s):
        return json.dumps(s, ensure_ascii=False)

    def mask(p):
        return sum(
            {"info": 1, "lock": 2}.get(x if isinstance(x, str) else x["name"], 0)
            for x in p
        )

    rows = []
    for r in data["messages"]:
        admission = "unnegotiated" in r["id"]
        rows.append(
            "{"
            + ",".join(
                [
                    quote(r["id"]),
                    quote(r["raw"]),
                    str(int(r["expected_decode"] or admission)),
                    str(int(admission)),
                    str(mask(r["extra_profiles"])),
                    str(int(r["direction"] == "server")),
                ]
            )
            + "}"
        )
    lines = [
        "struct MessageCase { const char *id,*raw; bool structural,admission_reject; unsigned profiles; bool server; };",
        "const MessageCase MESSAGES[]={" + ",\n".join(rows) + "};",
    ]
    rows = []
    for r in data["feature_api"]:
        if r["endpoint"] == "entity_panel_extra_response":
            continue
        if "expected_structure_valid" not in r:
            continue
        rows.append(
            "{"
            + ",".join(
                [
                    quote(r["id"]),
                    quote(r["raw"]),
                    quote(r["current_channel"]),
                    str(mask(r["configured_profiles"])),
                    str(mask(r["previous_binding_profiles"])),
                    str(int(r["selection_active"])),
                    str(int(r["endpoint"] == "entity_panel_extra_bind")),
                    str(int(r["expected_accepted"] or False)),
                    str(mask(r["expected_profiles"])),
                    str(int(r["expected_structure_valid"])),
                ]
            )
            + "}"
        )
    lines += [
        "struct FeatureCase {const char *id,*raw,*channel; unsigned configured,previous; bool active,bind,accepted; unsigned expected; bool structure;};",
        "const FeatureCase FEATURES[]={" + ",\n".join(rows) + "};",
    ]
    return "\n".join(lines)


class ExtraCodecTests(unittest.TestCase):
    def test_independent_protocol_and_negotiation_corpus(self):
        with tempfile.TemporaryDirectory(prefix="extra-protocol-") as temporary:
            root = Path(temporary)
            defines = root / "esphome/core/defines.h"
            defines.parent.mkdir(parents=True)
            defines.write_text("")
            (root / "corpus.h").write_text(corpus_header(), encoding="utf8")
            executable = root / "corpus.exe"
            command = [
                *compiler_command(),
                "-std=c++20",
                "-UNDEBUG",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-DARDUINOJSON_POOL_CAPACITY=128",
                *[f"-DUSE_LVGL_MATERIAL_{f}" for f in features],
                "-I",
                str(root),
                "-I",
                str(SOURCE),
                "-I",
                str(arduino_json()),
                *[str(SOURCE / (name + ".cpp")) for name in sources],
                str(HERE / "extra_codec_test.cpp"),
                "-o",
                str(executable),
            ]
            result = subprocess.run(
                command, capture_output=True, text=True, timeout=120, check=False
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run(
                [str(executable)],
                capture_output=True,
                text=True,
                timeout=30,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(
                "messages=204 admissions=11 features=42 failures=0", result.stdout
            )
