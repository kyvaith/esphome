"""Closed HA fixtures and adversarial Info context values through the actual codec."""

import copy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from host_light_control import SOURCE, arduino_json, compiler_command
import test_entity_control_extra as extra

HERE = Path(__file__).resolve().parent
FEATURES = [*extra.features, "MEDIA_CONTROLS", "ACTION_CONTROLS"]
SOURCES = [
    *extra.sources,
    "material_entity_control_media",
    "material_entity_control_media_owner",
    "material_entity_control_media_protocol",
    "material_entity_control_action",
    "material_entity_control_action_owner",
    "material_entity_control_action_protocol",
    "material_entity_control_info_context",
]


def corpus_header():
    fixtures = json.loads((HERE / "info_context_wire_corpus.json").read_text("utf8"))["fixtures"]
    rows = [(r["id"], r["raw"], True, True) for r in fixtures]
    base = {r["id"]: json.loads(r["raw"]) for r in fixtures}

    def mutation(name, fixture, area, key, value, valid=False):
        item = copy.deepcopy(base[fixture])
        target = item if area is None else item[area]
        if value is ...:
            del target[key]
        else:
            target[key] = value
        rows.append((name, json.dumps(item, ensure_ascii=False, separators=(",", ":")), valid, True))

    for kind, fixture in (("person", "person-home-en"), ("sun", "sun-day")):
        for key in base[fixture]["panel_state"]:
            mutation(f"{kind}-missing-{key}", fixture, "panel_state", key, ...)
        for key, value in (("extra", 1), ("source", "broken"), ("updated_text", "2026-11-05T20:00:00Z")):
            mutation(f"{kind}-{key}", fixture, "panel_state", key, value)
        mutation(f"{kind}-class", fixture, "panel_caps", "device_class", "temperature")
        mutation(f"{kind}-command-eligibility", fixture, None, "eligible_actions", 1)
        mutation(f"{kind}-knownness", fixture, None, "available", False)
        mutation(f"{kind}-unknown-retained", fixture, "panel_state", "source", "unknown")
        raw = base[fixture]
        mutation(f"{kind}-phase", fixture, "panel_state", "phase", "unexpected")
        mutation(f"{kind}-zone-length", fixture, "panel_state", "zone_text", "x" * 49)
        mutation(f"{kind}-zone-missing-value", fixture, "panel_state", "zone_text", "")
        rows.append((f"{kind}-duplicate", json.dumps(raw).replace('"source":', '"source":"known","source":'), False, True))
    for value in (
        "2025-02-29T12:00:00+01:00", "2024-02-30T12:00:00+01:00", "0000-01-01T00:00:00+00:00",
        "2026-13-01T00:00:00+00:00", "2026-01-00T00:00:00+00:00", "2026-01-01T24:00:00+00:00",
        "2026-01-01T00:60:00+00:00", "2026-01-01T00:00:60+00:00", "2026-01-01T00:00:00+24:00",
        "2026-01-01T00:00:00+01:60", "2026-01-01T00:00:00.001+01:00", "2026-01-01 00:00:00+01:00",
    ):
        mutation("invalid-date-" + value, "person-home-en", "panel_state", "changed_text", value)
    mutation("valid-leap", "person-home-en", "panel_state", "changed_text", "2024-02-29T23:59:59-03:30", True)
    for key, values in (
        ("elevation_text", ["90.0000000000000001", "-90.000000000000001", "91", "1e99"]),
        ("azimuth_text", ["360.000000000000001", "-0.000000000000001", "361", "1e99"]),
    ):
        for value in [*values, "NaN", "inf", "1e", "1.2.3", "1,2.3", "1,2", ".", "1°", " 1", "1 ", "", "x" * 25]:
            mutation(key + value, "sun-day", "panel_state", key, value)
    for value in ("-90", "90", "9e1", "0.0000000000000000000001", "5e-324", "-5e-324", ".5", "+1", "1.", "0e999999999"):
        mutation("valid-angle-" + value, "sun-day", "panel_state", "elevation_text", value, True)
    for value in ("-0", "360", "3.6e2", "5e-324"):
        mutation("valid-azimuth-" + value, "sun-day", "panel_state", "azimuth_text", value, True)
    for key in ("next_rising_status", "elevation_status"):
        mutation("status-retains-value-" + key, "sun-day", "panel_state", key, "unknown")
    mutation("person-status-retains-value", "person-home-en", "panel_state", "text_status", "empty")
    mutation("person-phase-unknown-value", "person-home-en", "panel_state", "phase", "unknown")
    mutation("person-text-overflow", "person-home-en", "panel_state", "state_text", "ą" * 49)
    mutation("person-text-control", "person-home-en", "panel_state", "state_text", "A\nB")
    mutation("person-unknown-domain-retained", "person-unknown", "panel_state", "zone_text", "Europe/Warsaw")

    # Exercise the largest fully escaped admitted shape with full identity.
    item = copy.deepcopy(base["person-zone"])
    item["card_id"] = "q" * 64
    item["title"] = '"' * 160
    item["panel_state"]["state_text"] = '"' * 96
    item["panel_state"]["zone_text"] = '"' * 48
    rows.append(("max-escaped-texts", json.dumps(item, separators=(",", ":")), True, True))
    legacy = json.loads((HERE / "extra_wire_corpus.json").read_text("utf8"))["messages"]
    for index in (0, 3, 6, 9, 12):
        row = legacy[index]
        rows.append((row["id"], row["raw"], True, False))
    quote = lambda value: json.dumps(value, ensure_ascii=False)
    return "struct Case {const char *name,*raw; bool valid,context;};\nconst Case CASES[]={\n" + ",\n".join(
        "{" + ",".join((quote(name), quote(raw), str(int(valid)), str(int(context)))) + "}"
        for name, raw, valid, context in rows
    ) + "};\n"


class InfoContextTests(unittest.TestCase):
    def test_closed_codec_owner_and_default_off(self):
        with tempfile.TemporaryDirectory(prefix="info-context-") as temporary:
            root = Path(temporary)
            defines = root / "esphome/core/defines.h"
            defines.parent.mkdir(parents=True)
            defines.write_text("")
            (root / "context_corpus.h").write_text(corpus_header(), encoding="utf8")
            legacy = []
            layouts = []
            for enabled in (True, False):
                executable = root / f"context-{enabled}.exe"
                command = [
                    *compiler_command(), "-std=c++20", "-UNDEBUG", "-Wall", "-Wextra", "-Werror",
                    "-DARDUINOJSON_POOL_CAPACITY=128",
                    *[f"-DUSE_LVGL_MATERIAL_{f}" for f in [*FEATURES, *(["INFO_CONTEXT"] if enabled else [])]],
                    "-I", str(root), "-I", str(SOURCE), "-I", str(arduino_json()),
                    *[str(SOURCE / (name + ".cpp")) for name in SOURCES],
                    str(HERE / "info_context_test.cpp"), "-o", str(executable),
                ]
                result = subprocess.run(command, capture_output=True, text=True, timeout=120, check=False)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                result = subprocess.run([str(executable)], capture_output=True, text=True, encoding="utf8", timeout=30, check=False)
                print(result.stdout, end="")
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                legacy.append([line for line in result.stdout.splitlines() if line.startswith("LEGACY ")])
                layouts.append(result.stdout.split("extra=", 1)[1].strip())
            self.assertEqual(legacy[0], legacy[1])
            self.assertEqual(layouts[0], layouts[1])


if __name__ == "__main__":
    unittest.main(verbosity=2)
