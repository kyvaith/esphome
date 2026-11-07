"""Text sidecar codec and local editor through the production bounded owner."""
import copy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from host_light_control import SOURCE, arduino_json, compiler_command
from test_datetime_control import DATETIME_SOURCES
from test_info_context import FEATURES

HERE = Path(__file__).resolve().parent
SOURCES = [*DATETIME_SOURCES, "material_entity_control_text", "material_entity_control_text_owner",
           "material_entity_control_text_protocol"]


def corpus_header():
    corpus = json.loads((HERE / "text_wire_corpus.json").read_text("utf8"))
    rows = [(r["name"], r["raw"], True, r.get("text", True)) for r in corpus]
    base = json.loads(next(r["raw"] for r in corpus if r["name"] == "public"))
    cmd = json.loads(next(r["raw"] for r in corpus if r["name"] == "save"))

    def mutate(name, original, section, key, value, valid=False):
        obj = copy.deepcopy(original)
        area = obj if section is None else obj[section]
        if value is ...:
            del area[key]
        else:
            area[key] = value
        rows.append((name, json.dumps(obj, ensure_ascii=False, separators=(",", ":")), valid, True))

    for name, original, areas in (("grant", base, (None, "panel_caps", "panel_state")), ("cmd", cmd, (None,))):
        for area in areas:
            for key in original if area is None else original[area]:
                mutate(f"{name}-missing-{area}-{key}", original, area, key, ...)
            mutate(f"{name}-extra-{area}", original, area, "extra", 0)
    for area, key, values in (
        ("panel_caps", "min", [None, True, -1, 1.0, 256, "0"]),
        ("panel_caps", "max", [None, True, 0, -1, 1.0, 256, "100"]),
        ("panel_caps", "can_set", [None, 0, 1, "true"]),
        ("panel_state", "source", [None, 0, "invalid", "unknown", "unavailable", "unsupported"]),
        ("panel_state", "reason", [None, 0, "invalid", "reserved", "password", "length"]),
        ("panel_state", "value", [None, True, 0, [], {}, "unknown", "unavailable", "x" * 256, "x\ny", "a\x7fb", "a\0b"]),
        (None, "available", [None, 0, 1, False]),
        (None, "eligible_actions", [None, True, 0, 2]),
        (None, "state_generation", [0, 1.0, True, 2147483648]),
        (None, "text_version", [1]),
    ):
        for value in values:
            mutate(f"bad-{area}-{key}-{value!r}", base, area, key, value)
    for value in (None, 0, True, {}, [], "unknown", "unavailable", "x" * 256, "a\nb"):
        mutate(f"cmdvalue-{value!r}", cmd, None, "value", value)
    for key, value in (("datetime_version", 1), ("action_version", 2), ("op", "set_datetime"), ("text_version", True)):
        mutate(f"cmdmixed-{key}", cmd, None, key, value)
    for source in ("unknown", "unavailable"):
        item = copy.deepcopy(base)
        item.update(available=False, eligible_actions=0)
        item["panel_state"].update(source=source, value=None)
        rows.append((source, json.dumps(item, separators=(",", ":")), True, True))
        mutate(source + "-nullbounds", item, "panel_caps", "min", None)
    # Exhaustive reason/bounds/can_set combinations for non-known source.
    reasons = ("none", "password", "mode", "pattern", "bounds", "length", "encoding", "characters")
    for source in ("known", "unknown", "unavailable", "unsupported"):
        for reason in reasons:
            for known in (False, True):
                for can_set in (False, True):
                    item = copy.deepcopy(base)
                    item["panel_caps"] = {"min": 0 if known else None, "max": 100 if known else None, "can_set": can_set}
                    item["panel_state"] = {"source": source, "reason": reason, "value": "A" if source == "known" else None}
                    item.update(available=source == "known", eligible_actions=int(source == "known" and can_set))
                    valid = known and reason == "none" if source != "unsupported" else (
                        (reason in ("password", "mode", "pattern") and not can_set) or
                        (reason == "bounds" and not known and not can_set) or
                        (reason in ("length", "encoding", "characters") and known))
                    rows.append((f"matrix-{source}-{reason}-{known}-{can_set}", json.dumps(item, separators=(",", ":")), valid, True))
    raw = json.dumps(base, ensure_ascii=False, separators=(",", ":"))
    for label, modified in (
        ("duplicate-value", raw.replace('"value":', '"value":"hidden","value":')),
        ("escaped-key-duplicate", raw.replace('"value":', '"va\\u006cue":"hidden","value":')),
        ("duplicate-min", raw.replace('"min":', '"min":0,"min":')),
        ("trailing", raw + '{}'), ("leading-array", '[' + raw + ']'),
        ("lone-surrogate", raw.replace('Hello Ąą Żż', '\\ud800')),
        ("bad-pair", raw.replace('Hello Ąą Żż', '\\ud800\\ud800')),
        ("escaped-zero", raw.replace('Hello Ąą Żż', '\\u0000')),
        ("payload-overflow", raw.replace('Hello Ąą Żż', 'a' * 2100)),
    ):
        rows.append((label, modified, False, True))
    return "struct Case {const char *name,*raw; bool valid,text;};\nconst Case CASES[]={\n" + ",\n".join(
        "{" + ",".join((json.dumps(name), json.dumps(raw, ensure_ascii=False), str(int(valid)), str(int(text)))) + "}"
        for name, raw, valid, text in rows) + "};\n"


class TextControlTests(unittest.TestCase):
    def test_codec_owner_and_default_off(self):
        layouts = []
        with tempfile.TemporaryDirectory(prefix="text-control-") as temporary:
            root = Path(temporary)
            defines = root / "esphome/core/defines.h"
            defines.parent.mkdir(parents=True)
            defines.write_text("")
            (root / "text_corpus.h").write_text(corpus_header(), encoding="utf8")
            for enabled in (True, False):
                exe = root / f"text-{enabled}.exe"
                features = [*FEATURES, "INFO_CONTEXT", "DATETIME_CONTROLS", *(["TEXT_CONTROLS"] if enabled else [])]
                command = [*compiler_command(), "-std=c++20", "-UNDEBUG", "-Wall", "-Wextra", "-Werror",
                           "-DARDUINOJSON_POOL_CAPACITY=128", *[f"-DUSE_LVGL_MATERIAL_{x}" for x in features],
                           "-I", str(root), "-I", str(SOURCE), "-I", str(arduino_json()),
                           *[str(SOURCE / (x + ".cpp")) for x in SOURCES],
                           str(HERE / "text_control_test.cpp"), "-o", str(exe)]
                compiled = subprocess.run(command, capture_output=True, text=True, timeout=180, check=False)
                self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
                ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=40, encoding="utf8", check=False)
                print(ran.stdout, end="")
                self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
                layouts.append(json.loads(next(l[7:] for l in ran.stdout.splitlines() if l.startswith("LAYOUT "))))
        self.assertEqual(layouts[0]["snapshot"], layouts[1]["snapshot"])
        self.assertEqual(layouts[0]["message"], layouts[1]["message"])
        self.assertLessEqual(layouts[0]["owner"] - layouts[1]["owner"], 8)


if __name__ == "__main__":
    unittest.main(verbosity=2)
