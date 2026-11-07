"""Closed nullable wall tuples and local draft fences through production C++."""

import copy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from host_light_control import SOURCE, arduino_json, compiler_command
from test_info_context import FEATURES, SOURCES

HERE = Path(__file__).resolve().parent
DATETIME_SOURCES = [
    *SOURCES,
    "material_entity_control_datetime",
    "material_entity_control_datetime_protocol",
    "material_entity_control_datetime_owner",
]


def corpus_header():
    corpus = json.loads((HERE / "datetime_wire_corpus.json").read_text("utf8"))
    actual_raw = {x["id"]: x["raw"] for x in corpus["messages"]}
    rows = [(x["id"], x["message"], True, True) for x in corpus["messages"]]
    grant = next(x["message"] for x in corpus["messages"] if x["id"] == "datetime-grant")
    command = next(x["message"] for x in corpus["messages"] if x["id"] == "datetime-command")

    def mutate(name, original, section, key, value, valid=False):
        item = copy.deepcopy(original)
        area = item if section is None else item[section]
        if value is ...:
            del area[key]
        else:
            area[key] = value
        rows.append((name, item, valid, True))

    for area in (None, "panel_caps", "panel_state"):
        for key in (grant if area is None else grant[area]):
            mutate(f"grant-missing-{area}-{key}", grant, area, key, ...)
        mutate(f"grant-extra-{area}", grant, area, "extra", 0)
    for area in (None, "target"):
        for key in (command if area is None else command[area]):
            mutate(f"command-missing-{area}-{key}", command, area, key, ...)
        mutate(f"command-extra-{area}", command, area, "extra", 0)
    for key in ("year", "month", "day", "hour", "minute", "second"):
        for value in (True, "1", 1.0, None, -1, 10000):
            for name, original, area in (("grant", grant, "panel_state"), ("command", command, "target")):
                mutate(f"{name}-{key}-{value!r}", original, area, key, value)
    for area, key, values in (
        ("panel_caps", "mode", ["other", None, 0]),
        ("panel_caps", "can_set", [0, 1, None, "true"]),
        ("panel_caps", "zone_text", [None, "x" * 49, "x\ny"]),
        ("panel_state", "source", [None, "invalid", "unknown", "unavailable"]),
        (None, "available", [0, 1, None, False]),
        (None, "eligible_actions", [True, 0, 2, "1"]),
        (None, "state_generation", [0, 2147483648, 1.0, True]),
    ):
        for value in values:
            mutate(f"bad-{area}-{key}-{value!r}", grant, area, key, value)
    for value in ("2026-02-29T12:00:00+01:00", "0000-01-01T00:00:00+00:00", "2026-01-01T24:00:00+00:00",
                  "2026-01-01T00:00:00+00:60", "2026-01-01T00:00:00+24:00", "2026-01-01T00:00:00Z",
                  "2026-01-01T00:00:00.123+01:00", "2026-01-01 00:00:00+01:00"):
        mutate("bad-updated-" + value, grant, "panel_state", "updated_text", value)
    for year, month, day, valid in ((1, 1, 1, True), (9999, 12, 31, True), (2000, 2, 29, True),
                                   (1900, 2, 29, False), (2028, 2, 29, True), (2027, 2, 29, False),
                                   (2028, 4, 31, False), (0, 1, 1, False)):
        for original, area in ((grant, "panel_state"), (command, "target")):
            item = copy.deepcopy(original)
            item[area].update(year=year, month=month, day=day)
            rows.append((f"gregorian-{area}-{year}-{month}-{day}", item, valid, True))
    for mode, mask in (("date", 7), ("time", 56), ("datetime", 63)):
        item = copy.deepcopy(grant)
        item["panel_caps"].update(mode=mode, zone_text="")
        for index, key in enumerate(("year", "month", "day", "hour", "minute", "second")):
            if not mask & (1 << index):
                item["panel_state"][key] = None
        rows.append(("mode-" + mode, item, True, True))
        for source in ("unknown", "unavailable"):
            unknown = copy.deepcopy(item)
            unknown["panel_state"].update(source=source, **{k: None for k in ("year", "month", "day", "hour", "minute", "second")})
            unknown.update(available=False, eligible_actions=0)
            rows.append((mode + "-" + source, unknown, True, True))
        if mode != "datetime":
            mutate(mode + "-zone", item, "panel_caps", "zone_text", "Europe/Warsaw")
    maximum = copy.deepcopy(grant)
    maximum.update(card_id="a" * 64, title='"' * 160, revision=9007199254740991)
    maximum["panel_caps"]["zone_text"] = '"' * 48
    rows.append(("max-escaped", maximum, True, True))
    # Preserve old Info/Lock/Media/Action shapes and results with the option disabled.
    legacy = json.loads((HERE / "extra_wire_corpus.json").read_text("utf8"))["messages"]
    rows.extend(("legacy-" + r["id"], json.loads(r["raw"]), True, False) for r in legacy if r.get("expected_decode"))
    encoded = []
    for name, message, valid, datetime in rows:
        raw = actual_raw.get(name, json.dumps(message, ensure_ascii=False, separators=(",", ":")))
        # RESULT has no datetime discriminator; structural decoding remains shared.
        datetime = datetime and message.get("kind") != "result"
        encoded.append((name, raw, valid, datetime))
    raw = json.dumps(grant, separators=(",", ":"))
    encoded.extend([
        ("duplicate-year", raw.replace('"year":', '"year":2028,"year":'), False, True),
        ("trailing", raw + "{}", False, True),
        ("exponent-integer", raw.replace('"year":2026', '"year":2.026e3'), False, True),
        ("escaped-NUL", raw.replace('"zone_text":', '"zone_text":"\\u0000","unknown":'), False, True),
    ])
    return "struct Case {const char *name,*raw; bool valid,datetime;};\nconst Case CASES[]={\n" + ",\n".join(
        "{" + ",".join((json.dumps(name), json.dumps(raw, ensure_ascii=False), str(int(valid)), str(int(datetime)))) + "}"
        for name, raw, valid, datetime in encoded
    ) + "};\n"


class DatetimeControlTests(unittest.TestCase):
    def test_actual_codec_owner_and_default_off(self):
        layouts = []
        with tempfile.TemporaryDirectory(prefix="datetime-control-") as temporary:
            root = Path(temporary)
            defines = root / "esphome/core/defines.h"
            defines.parent.mkdir(parents=True)
            defines.write_text("")
            (root / "datetime_corpus.h").write_text(corpus_header(), encoding="utf8")
            for enabled in (True, False):
                executable = root / f"datetime-{enabled}.exe"
                command = [
                    *compiler_command(), "-std=c++20", "-UNDEBUG", "-Wall", "-Wextra", "-Werror",
                    "-DARDUINOJSON_POOL_CAPACITY=128",
                    *[f"-DUSE_LVGL_MATERIAL_{feature}" for feature in [*FEATURES, "INFO_CONTEXT", *(["DATETIME_CONTROLS"] if enabled else [])]],
                    "-I", str(root), "-I", str(SOURCE), "-I", str(arduino_json()),
                    *[str(SOURCE / (name + ".cpp")) for name in DATETIME_SOURCES],
                    str(HERE / "datetime_control_test.cpp"), "-o", str(executable),
                ]
                result = subprocess.run(command, capture_output=True, text=True, timeout=180, check=False)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                result = subprocess.run([str(executable)], capture_output=True, text=True, encoding="utf8", timeout=30, check=False)
                print(result.stdout, end="")
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                layouts.append(json.loads(next(line[7:] for line in result.stdout.splitlines() if line.startswith("LAYOUT "))))
        self.assertEqual(layouts[0]["snapshot"], layouts[1]["snapshot"])
        self.assertEqual(layouts[0]["message"], layouts[1]["message"])
        self.assertLessEqual(layouts[0]["owner"] - layouts[1]["owner"], 64)


if __name__ == "__main__":
    unittest.main(verbosity=2)
