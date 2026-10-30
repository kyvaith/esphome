"""Actual Component and codegen checks, using production sources from this fork."""

import asyncio
import importlib
from io import StringIO
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from host_light_control import (
    HERE,
    SOURCE,
    arduino_json,
    compiler_command,
    load_native_component,
)


class ComponentHostTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="entity-component-host-")
        root = Path(cls.temp.name)
        for name in (
            "components/lvgl/lvgl_esphome.h",
            "core/automation.h",
            "core/component.h",
            "core/helpers.h",
            "core/string_ref.h",
            "core/application.h",
            "core/hal.h",
            "core/log.h",
            "core/defines.h",
            "components/api/api_server.h",
        ):
            path = root / "esphome" / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#include "component_test_support.h"\n', encoding="utf-8")
        cls.executable = root / "component.exe"
        command = [
            *compiler_command(),
            "-std=c++20",
            "-Wall",
            "-Wextra",
            "-Werror",
            # ESP32-P4 defaults to128 slots. Wide host slots conservatively
            # consume more arena bytes; this does not emulate target ABI.
            "-DARDUINOJSON_POOL_CAPACITY=128",
            "-DUSE_API",
            "-DUSE_API_USER_DEFINED_ACTION_RESPONSES_JSON",
            "-I",
            str(root),
            "-I",
            str(HERE),
            "-I",
            str(SOURCE),
            "-I",
            str(arduino_json()),
            *[
                str(SOURCE / name)
                for name in (
                    "material_entity_control.cpp",
                    "material_entity_control_protocol.cpp",
                    "material_entity_control_codec.cpp",
                    "material_entity_control_component.cpp",
                )
            ],
            str(HERE / "entity_component_test.cpp"),
            "-o",
            str(cls.executable),
        ]
        compiled = subprocess.run(
            command, capture_output=True, text=True, timeout=180, check=False
        )
        if compiled.returncode:
            raise AssertionError(compiled.stdout + compiled.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_actual_component_input_protocol_and_render_admission(self):
        for case in range(24):
            with self.subTest(case=case):
                result = subprocess.run(
                    [str(self.executable), str(case)],
                    capture_output=True,
                    text=True,
                    timeout=10,
                    check=False,
                )
                self.assertEqual(0, result.returncode, result.stdout + result.stderr)
                self.assertIn(f"component case {case} PASS", result.stdout)

    def test_fresh_grant_resets_feedback_but_preserves_current_results(self):
        for case in (24, 14):
            with self.subTest(case=case):
                result = subprocess.run(
                    [str(self.executable), str(case)],
                    capture_output=True,
                    text=True,
                    timeout=10,
                    check=False,
                )
                self.assertEqual(0, result.returncode, result.stdout + result.stderr)
                self.assertIn(f"component case {case} PASS", result.stdout)

    def test_fixed_arena_roundtrips_shared_fixtures_and_refuses_overflow_atomically(
        self,
    ):
        fixtures = HERE / "fixtures/light_control"
        fixture_paths = sorted(fixtures.glob("*.json"))
        self.assertEqual(15, len(fixture_paths))
        for path in fixture_paths:
            with self.subTest(name=path.name):
                result = subprocess.run(
                    [str(self.executable), "codec", str(path)],
                    capture_output=True,
                    text=True,
                    timeout=10,
                    check=False,
                )
                self.assertEqual(0, result.returncode, result.stderr)
                self.assertEqual(
                    json.loads(path.read_text(encoding="utf-8")),
                    json.loads(result.stdout),
                )
        path = Path(self.temp.name) / "hostile.json"
        for wire in (
            '{"array":[' + ",".join("{}" for _ in range(600)) + "]}",
            " " * 2049,
            '{"v":true}',
        ):
            path.write_text(wire, encoding="utf-8")
            result = subprocess.run(
                [str(self.executable), "codec", str(path)],
                capture_output=True,
                text=True,
                timeout=10,
                check=False,
            )
            self.assertEqual(1, result.returncode, result.stdout + result.stderr)


class ComponentSchemaTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = load_native_component()
        cls.module = importlib.import_module(
            "esphome.components.lvgl_material.entity_control"
        )

    def config(self, **extra):
        return {
            "id": "control",
            "lvgl_id": "ui",
            **{
                name: name
                for name in (
                    "root",
                    "hit",
                    "title_label",
                    "value_label",
                    "unknown_value_label",
                    "on_button",
                    "off_button",
                )
            },
            **extra,
        }

    def test_default_disabled_and_additive_root_schema(self):
        config = self.module.ENTITY_CONTROL_SCHEMA(self.config())
        self.assertIs(False, config["enabled"])
        self.assertEqual(12, len(config["status_texts"]))
        root = self.root.CONFIG_SCHEMA({"entity_controls": [self.config()]})
        self.assertEqual(1, len(root["entity_controls"]))
        self.assertEqual([], root["tile_surfaces"])

    def test_enabled_requires_transport_distinct_widgets_and_bounded_literals(self):
        from esphome import config_validation as cv

        invalid = (
            self.config(enabled=True),
            self.config(hit="on_button"),
            self.config(on_text="x" * 161),
            self.config(off_text="bad\ntext"),
        )
        for config in invalid:
            with self.subTest(config=config), self.assertRaises(cv.Invalid):
                self.module.ENTITY_CONTROL_SCHEMA(config)
        with self.assertRaises(cv.Invalid):
            self.root.CONFIG_SCHEMA(
                {"entity_controls": [self.config(), self.config(id="other")]}
            )

    def test_action_schema_strict_bounds_and_full_close_reasons(self):
        from esphome import config_validation as cv

        normal = {
            "id": "control",
            "slot": 2,
            "card_id": "card-2",
            "revision": 2**53 - 1,
        }
        self.module.OPEN_SCHEMA(normal)
        for name, value in (
            ("slot", 1),
            ("slot", 258),
            ("slot", True),
            ("slot", 2.0),
            ("revision", True),
            ("revision", 2**53),
            ("card_id", "_bad"),
        ):
            with self.subTest(name=name, value=value), self.assertRaises(cv.Invalid):
                self.module.OPEN_SCHEMA({**normal, name: value})
        for reason in self.module.CLOSE_REASONS:
            self.module.INVALIDATE_SCHEMA({"id": "control", "reason": reason})
        with self.assertRaises(cv.Invalid):
            self.module.INVALIDATE_SCHEMA(
                {"id": "control", "reason": "service_timeout"}
            )

    def test_real_yaml_integer_wrappers_remain_valid_without_bool_or_float_coercion(
        self,
    ):
        from esphome import config_validation as cv, yaml_util

        config = yaml_util.parse_yaml(
            Path("isolated-action.yaml"),
            StringIO("id: control\nslot: 2\ncard_id: card-2\nrevision: 7\n"),
        )
        self.module.OPEN_SCHEMA(config)
        for value in (True, 2.0):
            with self.assertRaises(cv.Invalid):
                self.module.OPEN_SCHEMA({**config, "slot": value})

    def test_actual_component_and_action_codegen_ast(self):
        from esphome import codegen as cg
        from esphome.components.lvgl.lvcode import LvglComponent
        from esphome.core import CORE, ID

        CORE.reset()
        config = self.module.ENTITY_CONTROL_SCHEMA(self.config())
        CORE.component_ids.add("control")
        cg.new_Pvariable(ID("ui", type=LvglComponent, is_declaration=True))

        class Widget:
            def __init__(self, name):
                self.obj = cg.MockObj(name)

        async def widgets(conf, name):
            return [Widget(str(conf[name]))]

        with patch.object(self.module, "get_widgets", widgets):
            asyncio.run(self.module.entity_controls_to_code([config]))
        asyncio.run(
            self.module.view_ready_to_code(
                self.module.VIEW_READY_SCHEMA(
                    {"id": "control", "generation": 7, "value": True}
                ),
                ID("view_action", type=self.module.FlagAction, is_declaration=True),
                cg.TemplateArguments(),
                [],
            )
        )
        asyncio.run(
            self.module.open_to_code(
                self.module.OPEN_SCHEMA(
                    {"id": "control", "slot": 2, "card_id": "card-2", "revision": 7}
                ),
                ID("open_action", type=self.module.OpenAction, is_declaration=True),
                cg.TemplateArguments(),
                [],
            )
        )
        asyncio.run(
            self.module.invalidate_to_code(
                self.module.INVALIDATE_SCHEMA({"id": "control", "reason": "voice"}),
                ID(
                    "invalidate_action",
                    type=self.module.InvalidateAction,
                    is_declaration=True,
                ),
                cg.TemplateArguments(),
                [],
            )
        )
        CORE.flush_tasks()
        generated = str(CORE.cpp_main_section)
        self.assertIn(
            "new(control) lvgl_material::MaterialEntityControlComponent(ui)", generated
        )
        self.assertIn("set_initial_enabled(false)", generated)
        self.assertIn(
            "set_widgets(root, hit, title_label, value_label, unknown_value_label, on_button, off_button)",
            generated,
        )
        self.assertIn("set_slot([]() -> uint32_t", generated)
        self.assertIn("return 2;", generated)
        self.assertIn("CONTROL_REASON_VOICE", generated)
        self.assertIn("set_generation([]() -> uint32_t", generated)

    def test_all_callbacks_refuse_delayed_borrowed_or_stale_context(self):
        from esphome import config_validation as cv
        from esphome.core import Lambda

        for name in (
            "on_message",
            "on_granted",
            "on_closed",
            "on_view_event",
            "on_render_request",
            "on_render_cancel",
        ):
            for action in (
                {"delay": "1s"},
                {"wait_until": {"lambda": Lambda("return true;")}},
                {
                    "if": {
                        "condition": {"lambda": Lambda("return true;")},
                        "then": [{"delay": "1s"}],
                    }
                },
            ):
                with (
                    self.subTest(name=name, action=action),
                    self.assertRaises(cv.Invalid),
                ):
                    self.module.ENTITY_CONTROL_SCHEMA(
                        self.config(**{name: [{"then": [action]}]})
                    )

    def test_view_ready_requires_captured_strict_generation(self):
        from esphome import config_validation as cv

        self.module.VIEW_READY_SCHEMA(
            {"id": "control", "generation": 2**32 - 1, "value": True}
        )
        for extra in (
            {},
            {"generation": 0},
            {"generation": True},
            {"generation": 1.0},
            {"generation": 2**32},
        ):
            with self.subTest(extra=extra), self.assertRaises(cv.Invalid):
                self.module.VIEW_READY_SCHEMA({"id": "control", "value": True, **extra})

    def test_real_api_respond_detection_and_forwarding_codegen(self):
        from esphome import codegen as cg
        from esphome.components import api
        from esphome.core import CORE, ID, EsphomeError, Lambda

        CORE.reset()
        status = {
            "action": "example_status",
            "supports_response": "only",
            "then": [
                {"api.respond": {"data": Lambda("id(control).populate_status(root);")}}
            ],
        }
        bind = {
            "action": "example_bind",
            "supports_response": "only",
            "then": [
                {
                    "if": {
                        "condition": {
                            "lambda": Lambda(
                                "return id(control).prepare_bind_response(payload);"
                            )
                        },
                        "then": [
                            {
                                "api.respond": {
                                    "data": Lambda(
                                        "id(control).populate_bind_response(root);"
                                    )
                                }
                            }
                        ],
                        "else": [
                            {
                                "api.respond": {
                                    "success": False,
                                    "error_message": "Control bind refused",
                                }
                            }
                        ],
                    }
                }
            ],
        }
        self.assertEqual(
            "only", api._auto_detect_supports_response(status)["supports_response"]
        )
        self.assertEqual(
            "only", api._auto_detect_supports_response(bind)["supports_response"]
        )
        config = api.API_RESPOND_ACTION_SCHEMA(
            {"id": "server", "data": Lambda("id(control).populate_status(root);")}
        )
        with self.assertRaises(EsphomeError):
            asyncio.run(
                api.api_respond_to_code(
                    config,
                    ID("response", type=api.APIRespondAction),
                    cg.TemplateArguments(),
                    [],
                )
            )
        CORE.register_variable(
            ID("control", type=self.module.MaterialEntityControlComponent),
            cg.MockObj("control", op="->"),
        )
        CORE.register_variable(
            ID("server", type=api.APIServer), cg.MockObj("server", op="->")
        )
        asyncio.run(
            api.api_respond_to_code(
                config,
                ID("response", type=api.APIRespondAction, is_declaration=True),
                cg.TemplateArguments(cg.uint32),
                [(cg.uint32, "call_id")],
            )
        )
        generated = str(CORE.cpp_main_section)
        self.assertIn("APIRespondAction<uint32_t>(server)", generated)
        self.assertIn("control->populate_status(root)", generated)

    def test_actual_callback_and_locale_provider_codegen(self):
        from esphome import codegen as cg
        from esphome.components.lvgl.lvcode import LvglComponent
        from esphome.core import CORE, ID, Lambda

        CORE.reset()
        config = self.module.ENTITY_CONTROL_SCHEMA(
            self.config(
                enabled=True,
                text_provider=Lambda('return "Localized";'),
                on_message=[{"then": [{"lambda": Lambda("(void) payload;")}]}],
                on_granted=[{"then": [{"lambda": Lambda("(void) generation;")}]}],
                on_closed=[{"then": [{"lambda": Lambda("(void) generation;")}]}],
                on_render_request=[
                    {
                        "then": [
                            {
                                "lambda": Lambda(
                                    "(void) request_id; (void) known; (void) value;"
                                )
                            }
                        ]
                    }
                ],
            )
        )
        from esphome.config import iter_ids

        declared = {str(id_) for id_, _ in iter_ids(config) if id_.id is not None}
        for id_, _ in iter_ids(config):
            if id_.id is None:
                id_.resolve(declared)
                declared.add(str(id_))
        CORE.component_ids.add("control")
        cg.new_Pvariable(ID("ui", type=LvglComponent, is_declaration=True))

        async def widgets(conf, name):
            return [type("Widget", (), {"obj": cg.MockObj(str(conf[name]))})()]

        with patch.object(self.module, "get_widgets", widgets):
            asyncio.run(self.module.entity_controls_to_code([config]))
        CORE.flush_tasks()
        generated = str(CORE.cpp_main_section)
        self.assertIn("set_text_provider", generated)
        self.assertIn("add_on_message_callback", generated)
        self.assertIn("add_on_granted_callback", generated)
        self.assertIn("add_on_closed_callback", generated)
        self.assertIn("add_on_render_request_callback", generated)


if __name__ == "__main__":
    unittest.main()
