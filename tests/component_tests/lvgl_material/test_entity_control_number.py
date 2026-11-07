"""Actual opt-in Number owner/presenters with fixed host facades; no SDK/device.

Generic is compiled explicitly. The native-only executable links no Scalar unit;
all native/mixed sources and generated defines are pinned before and after tests.
The caller supplies the same C++ compiler/ArduinoJson as existing control tests.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

from host_light_control import LVGL_SOURCE, SOURCE, arduino_json, compiler_command

HERE = Path(__file__).resolve().parent
PORTABLE = (
    "material_entity_control.cpp",
    "material_entity_control_protocol.cpp",
    "material_entity_control_codec.cpp",
    "material_number_domain.cpp",
    "material_entity_control_generic.cpp",
    "material_entity_control_generic_protocol.cpp",
)
NATIVE = (
    *PORTABLE,
    "material_entity_control_component.cpp",
    "material_entity_control_number_component.cpp",
)


class NumberHostTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="entity-number-host-")
        cls.root = Path(cls.temporary.name)
        cls.pins = {
            path: hashlib.sha256(path.read_bytes()).hexdigest()
            for path in SOURCE.glob("material_*.h")
        }
        cls.pins.update(
            {
                SOURCE / name: hashlib.sha256((SOURCE / name).read_bytes()).hexdigest()
                for name in (
                    *NATIVE,
                    "material_scalar_raster.cpp",
                    "material_direct_scalar_control.cpp",
                )
            }
        )
        for name in (
            "components/lvgl/lvgl_esphome.h",
            "components/lvgl/lvgl_navigation.h",
            "components/esp32/task_utils.h",
            "components/api/api_server.h",
            "core/automation.h",
            "core/component.h",
            "core/helpers.h",
            "core/string_ref.h",
            "core/application.h",
            "core/hal.h",
            "core/log.h",
            "freertos/FreeRTOS.h",
            "freertos/task.h",
            "esp_heap_caps.h",
            "misc/cache/instance/lv_image_cache.h",
        ):
            path = cls.root / (
                name
                if name.startswith(("freertos/", "esp_heap", "misc/"))
                else "esphome/" + name
            )
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#include "number_test_support.h"\n', encoding="utf-8")
        # Definitions are generated where production code expects them. No flag
        # appears in public host defaults; only this isolated opt-in build pays.
        defines = cls.root / "esphome/core/defines.h"
        defines.write_text(
            "#define USE_LVGL_MATERIAL_NUMBER_STEPPER\n"
            "#define USE_LVGL_NAVIGATION_ADMISSION_GUARD\n",
            encoding="utf-8",
        )
        cls.pins[defines] = hashlib.sha256(defines.read_bytes()).hexdigest()
        scene = cls.root / "esphome/components/lvgl/direct_scene_controller.h"
        scene.write_bytes((LVGL_SOURCE / "direct_scene_controller.h").read_bytes())
        cls.common = [
            *compiler_command(),
            "-std=c++20",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-DTEST_SCALAR",
            "-DARDUINOJSON_POOL_CAPACITY=128",
        ]
        for include in (cls.root, HERE, SOURCE, LVGL_SOURCE, arduino_json()):
            cls.common += ["-I", str(include)]
        cls.portable = cls.build(
            "portable",
            PORTABLE,
            HERE / "number_portable_test.cpp",
            ["-DUSE_LVGL_MATERIAL_THERMOSTAT"],
        )
        cls.native = cls.build("native", NATIVE, HERE / "number_component_test.cpp")
        cls.accelerated = [
            "-DUSE_ESP32",
            "-DUSE_MIPI_DSI",
            "-DUSE_LVGL_PPA",
            "-DLV_COLOR_DEPTH=32",
            "-DUSE_LVGL_MATERIAL_DIRECT_SCALAR_CONTROL",
            "-DUSE_LVGL_DIRECT_REGION_EXPECTED_GENERATION",
        ]
        cls.mixed = cls.build(
            "mixed",
            (
                *NATIVE,
                "material_scalar_raster.cpp",
                "material_direct_scalar_control.cpp",
            ),
            HERE / "number_component_test.cpp",
            ["-DTEST_WITH_SCALAR", *cls.accelerated],
        )
        # The original assertion files are compiled verbatim, with an inactive
        # opt-in profile, to protect legacy behavior in mixed-configuration builds.
        cls.legacy = cls.build(
            "legacy",
            NATIVE,
            HERE / "entity_component_test.cpp",
            ["-DUSE_API", "-DUSE_API_USER_DEFINED_ACTION_RESPONSES_JSON"],
        )
        fixture = (
            (HERE / "entity_component_test.cpp")
            .read_text(encoding="utf-8")
            .split("int main(", 1)[0]
        )
        (cls.root / "component_fixture.h").write_text(fixture, encoding="utf-8")
        cls.scalar = cls.build(
            "scalar",
            (
                *NATIVE,
                "material_scalar_raster.cpp",
                "material_direct_scalar_control.cpp",
            ),
            HERE / "scalar_component_test.cpp",
            cls.accelerated,
        )

    @staticmethod
    def checked(command, expected=0):
        result = subprocess.run(
            [str(x) for x in command],
            capture_output=True,
            text=True,
            encoding="utf-8",
            timeout=180,
            check=False,
        )
        if result.returncode != expected:
            raise AssertionError(
                f"{command!r}\nexpected {expected}, got {result.returncode}\n{result.stdout}{result.stderr}"
            )
        return result

    @classmethod
    def build(cls, name, units, test, flags=()):
        executable = cls.root / (name + ".exe")
        cls.checked(
            [
                *cls.common,
                *flags,
                *[SOURCE / unit for unit in units],
                test,
                "-o",
                executable,
            ]
        )
        return executable

    @classmethod
    def tearDownClass(cls):
        try:
            for path, digest in cls.pins.items():
                if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
                    raise AssertionError(
                        "Production/generated source changed during host run: "
                        + str(path)
                    )
        finally:
            cls.temporary.cleanup()

    def codec(self, value, expected=0):
        path = self.root / "payload.json"
        path.write_text(
            value if isinstance(value, str) else json.dumps(value), encoding="utf-8"
        )
        return self.checked([self.portable, "codec", path], expected)

    def test_numeric_domain_owner_stale_and_semantic_generation(self):
        for case in range(18):
            with self.subTest(case=case):
                self.assertIn(
                    f"number case {case} PASS",
                    self.checked([self.portable, case]).stdout,
                )

    def test_shared_ha_fixtures_and_generic_light_compatibility(self):
        fixtures = sorted((HERE / "fixtures/entity_control").glob("*.json"))
        self.assertEqual(13, len(fixtures))
        for path in fixtures:
            value = json.loads(path.read_text(encoding="utf-8"))
            with self.subTest(fixture=path.name):
                self.assertEqual(value, json.loads(self.codec(value).stdout))
        legacy = sorted((HERE / "fixtures/light_control").glob("*.json"))
        self.assertEqual(15, len(legacy))
        for path in legacy:
            value = json.loads(path.read_text(encoding="utf-8"))
            value["type"] = "entity_control_v1"
            if value["kind"] == "capability":
                value["control_kinds"] = ["light", "number"]
            elif value["kind"] != "bind":
                value["control_kind"] = "light"
            with self.subTest(light=path.name):
                self.assertEqual(value, json.loads(self.codec(value).stdout))

    def test_strict_atomic_codec_and_bounded_arena(self):
        wire = json.loads(
            (HERE / "fixtures/entity_control/grant.json").read_text(encoding="utf-8")
        )
        changes = (
            {"v": True},
            {"index": 4.0},
            {"index": True},
            {"state_generation": True},
            {"state_generation": 0},
            {"available": 1},
            {"available": False},
            {"value_text": "0.0"},
            {"control_kind": "fan"},
            {"extra": 1},
            {"title": "\ud800"},
            {"title": "\udc00"},
        )
        for change in changes:
            with self.subTest(change=change):
                self.codec(wire | change, 1)
        for domain in (
            {"min": "-1.0"},
            {"max_index": True},
            {"step": "0"},
            {"unit": "a" * 33},
            {"schema": "float"},
            {"unit": "\u0080"},
            {"unit": "\u00a0"},
            {"unit": "\u202e"},
        ):
            with self.subTest(domain=domain):
                self.codec(wire | {"domain": wire["domain"] | domain}, 1)
        for raw in (
            " " * 2049,
            '{"v":1,"v":1}',
            json.dumps(wire).replace(
                '"schema": "numeric_steps_v1"',
                '"schema":"numeric_steps_v1","schema":"numeric_steps_v1"',
            ),
            json.dumps(wire) + "{}",
        ):
            self.codec(raw, 1)
        maximum = wire | {
            "card_id": "a" * 64,
            "title": '"' * 160,
            "index": 500000,
            "domain": {
                "schema": "numeric_steps_v1",
                "min": "-1000000000000",
                "max": "1000000000000",
                "step": "2000000",
                "unit": "u" * 32,
                "max_index": 1000000,
            },
        }
        for value in (
            maximum,
            wire
            | {"title": "\U0001f600", "domain": wire["domain"] | {"unit": "ÂµÎ©Â˛"}},
        ):
            self.assertEqual(value, json.loads(self.codec(value).stdout))

    def test_number_decimal_separator_presentation_only(self):
        for case in range(11, 16):
            with self.subTest(case=case):
                self.assertIn(
                    f"case {case} PASS", self.checked([self.native, case]).stdout
                )

    def test_thermostat_round_trips_both_writable_and_read_only_grants(self):
        grant = json.loads(
            (HERE / "fixtures/entity_control/grant.json").read_text(encoding="utf-8")
        )
        view = {
            "unit": "°C",
            "target": "21.5",
            "current": "19.2",
            "min": "16",
            "max": "30",
            "level": 39,
            "activity": "heating",
        }
        writable = grant | {"thermostat": view}
        read_only = writable | {
            "domain": None,
            "index": None,
            "available": False,
            "value_text": "--",
        }
        for snapshot in (writable, read_only):
            self.assertEqual(snapshot, json.loads(self.codec(snapshot).stdout))
            dial = snapshot | {"thermostat": view | {"dial": True}}
            self.assertEqual(dial, json.loads(self.codec(dial).stdout))
        self.codec(writable | {"domain": None}, 1)
        self.codec(writable | {"thermostat": view | {"level": 101}}, 1)
        self.codec(writable | {"thermostat": view | {"target": "21.50"}}, 1)
        self.codec(
            json.dumps(writable).replace('"level": 39', '"level": 39, "level": 39'), 1
        )
        step = json.loads(
            (HERE / "fixtures/entity_control/command.json").read_text(encoding="utf-8")
        )
        step.pop("direction")
        target = step | {"op": "set_target", "target_index": 6}
        self.assertEqual(target, json.loads(self.codec(target).stdout))
        self.codec(target | {"target_index": 1000001}, 1)

    def test_localized_number_text_is_never_admitted_on_wire(self):
        grant = json.loads(
            (HERE / "fixtures/entity_control/grant.json").read_text(encoding="utf-8")
        )
        for index, canonical, localized in (
            (5, "0.25", "0,25"),
            (3, "-0.25", "-0,25"),
        ):
            with self.subTest(canonical=canonical):
                value = grant | {"index": index, "value_text": canonical}
                self.assertEqual(value, json.loads(self.codec(value).stdout))
                self.codec(value | {"value_text": localized}, 1)

    def test_native_only_has_no_scalar_allocation_or_worker(self):
        for case in range(10):
            with self.subTest(case=case):
                self.assertIn(
                    f"case {case} PASS", self.checked([self.native, case]).stdout
                )

    def test_fresh_grant_feedback_and_same_selection_close_cancel(self):
        for executable, case in (
            (self.native, 10),
            (self.legacy, 24),
            (self.portable, 10),
        ):
            with self.subTest(executable=executable.name, case=case):
                self.assertIn(
                    f"case {case} PASS", self.checked([executable, case]).stdout
                )

    def test_mixed_overlay_backend_handoffs_and_power_fence(self):
        for case in (100, 101, 102, 103, 104, 105, 107, 108, 109, 110):
            with self.subTest(case=case):
                self.assertIn(
                    f"case {case} PASS", self.checked([self.mixed, case]).stdout
                )

    def test_legacy_raw_assertions_with_number_compiled_but_inactive(self):
        for case in range(24):
            with self.subTest(component=case):
                self.assertIn(
                    f"case {case} PASS", self.checked([self.legacy, case]).stdout
                )
        for path in (HERE / "fixtures/light_control").glob("*.json"):
            result = self.checked([self.legacy, "codec", path])
            self.assertEqual(
                json.loads(path.read_text(encoding="utf-8")), json.loads(result.stdout)
            )
        for case in range(40):
            with self.subTest(scalar=case):
                self.assertIn(
                    f"scalar case {case} PASS", self.checked([self.scalar, case]).stdout
                )
