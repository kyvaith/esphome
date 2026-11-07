"""Negotiated external Media owner and closed wire grammar with fixed arena."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from host_light_control import SOURCE, arduino_json, compiler_command
from test_entity_control_extra import features, sources

HERE = Path(__file__).resolve().parent
MEDIA_UNITS = [
    "material_entity_control_media",
    "material_entity_control_media_protocol",
    "material_entity_control_media_owner",
]


def corpus():
    return json.loads((HERE / "media_wire_corpus.json").read_text(encoding="utf8"))


def fixtures_header():
    return "\n".join(
        "constexpr const char *"
        + row["name"].removesuffix(".json").upper().replace("-", "_")
        + "="
        + json.dumps(
            json.dumps(row["message"], separators=(",", ":")), ensure_ascii=False
        )
        + ";"
        for row in corpus()
        if row["accepted"]
    )


class MediaOwnerTests(unittest.TestCase):
    def test_owner_and_closed_codec_default_off(self):
        with tempfile.TemporaryDirectory(prefix="media-owner-") as directory:
            root = Path(directory)
            defines = root / "esphome/core/defines.h"
            defines.parent.mkdir(parents=True)
            defines.write_text("")
            (root / "media_owner_fixtures.h").write_text(
                fixtures_header(), encoding="utf8"
            )
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
                        )
                    )
                    + "}"
                )
            (root / "media_codec_cases.h").write_text(
                "struct Case {const char *name,*wire;bool accepted;};\nconst Case cases[]={"
                + ",\n".join(rows)
                + "};",
                encoding="utf8",
            )
            units = [SOURCE / (name + ".cpp") for name in sources + MEDIA_UNITS]
            for name, test, enabled in (
                ("owner", HERE / "media_owner_test.cpp", True),
                ("codec", HERE / "media_codec_test.cpp", True),
                ("default-off", HERE / "media_codec_test.cpp", False),
            ):
                executable = root / (name + ".exe")
                flags = [
                    "-DUSE_LVGL_MATERIAL_" + feature
                    for feature in features + (["MEDIA_CONTROLS"] if enabled else [])
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
                        str(test),
                        "-o",
                        str(executable),
                    ],
                    check=False,
                    capture_output=True,
                    text=True,
                    encoding="utf8",
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                result = subprocess.run(
                    [str(executable)],
                    check=False,
                    capture_output=True,
                    text=True,
                    encoding="utf8",
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                if name == "codec":
                    decoded = dict(
                        line.split("\t", 1) for line in result.stdout.splitlines()
                    )
                    for row in corpus():
                        if row["accepted"]:
                            self.assertEqual(
                                json.loads(decoded[row["name"]]), row["message"]
                            )
