"""Opt-in Number schemas/codegen; uses only this checkout, no SDK/config/network."""

import asyncio
import importlib
import unittest
from unittest.mock import patch

from host_light_control import load_native_component

from esphome import codegen as cg, config_validation as cv
from esphome.components.lvgl.lvcode import LvglComponent
from esphome.components.lvgl.types import LvglApplication, LvglNavigation
from esphome.core import CORE, ID, Lambda
from esphome.cpp_helpers import get_slot_count

module = load_native_component()
entity = importlib.import_module(module.__name__ + ".entity_control")
scalar = importlib.import_module(module.__name__ + ".direct_scalar")


class NumberCodegenTests(unittest.TestCase):
    def config(self, **extra):
        value = {
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
        }
        value.update(extra)
        return value

    def enabled(self, **extra):
        value = {
            "enabled": True,
            "number_stepper": True,
            "navigation_id": "nav",
            "application_id": "app",
            "state_label": "state",
            "minus_label": "minus",
            "plus_label": "plus",
            "number_value_font": "montserrat_40",
            "on_message": [{"then": [{"lambda": Lambda("(void)payload;")}]}],
        }
        value.update(extra)
        return self.config(**value)

    def scalar_config(self):
        return {
            "id": "scalar",
            "lvgl_id": "ui",
            "entity_control_id": "control",
            "navigation_id": "nav",
            "application_id": "app",
            "enabled": True,
            "active_color": 0x3498FF,
            **{name: name for name in scalar.WIDGETS},
        }

    def test_default_off_and_metadata_fail_closed(self):
        CORE.reset()
        self.assertFalse(entity.ENTITY_CONTROL_SCHEMA(self.config())["number_stepper"])
        for fields in (
            {"number_stepper": True},
            {"navigation_id": "nav"},
            {"minus_label": "minus"},
            {"number_value_font": "montserrat_40"},
        ):
            with self.subTest(fields=fields), self.assertRaises(cv.Invalid):
                entity.ENTITY_CONTROL_SCHEMA(self.config(**fields))
        valid = self.enabled()
        self.assertIs(
            entity.ENTITY_CONTROL_SCHEMA(valid)["id"].type,
            entity.MaterialGenericEntityControlComponent,
        )
        for name in entity.NUMBER_PROFILE_FIELDS:
            invalid = self.enabled()
            invalid.pop(name)
            with self.subTest(missing=name), self.assertRaises(cv.Invalid):
                entity.ENTITY_CONTROL_SCHEMA(invalid)

    def test_generic_scalar_must_share_navigation_application(self):
        CORE.reset()
        for field in ("navigation_id", "application_id"):
            scalar_config = self.scalar_config()
            scalar_config[field] = "other"
            with self.subTest(field=field), self.assertRaises(cv.Invalid):
                module.CONFIG_SCHEMA(
                    {
                        "entity_controls": [self.enabled()],
                        "direct_scalar_controls": [scalar_config],
                    }
                )

    def test_generated_flags_single_slot_and_static_font(self):
        CORE.reset()
        for name, type_ in (
            ("ui", LvglComponent),
            ("nav", LvglNavigation),
            ("app", LvglApplication),
        ):
            cg.new_Pvariable(ID(name, type=type_, is_declaration=True))
        CORE.component_ids.update(("control", "scalar"))
        control_config = entity.ENTITY_CONTROL_SCHEMA(self.enabled())
        scalar_config = scalar.DIRECT_SCALAR_SCHEMA(self.scalar_config())
        from esphome.config import iter_ids

        declared = {
            str(id_) for id_, _ in iter_ids(control_config) if id_.id is not None
        }
        for id_, _ in iter_ids(control_config):
            if id_.id is None:
                id_.resolve(declared)
                declared.add(str(id_))

        async def widgets(conf, name):
            return [type("Widget", (), {"obj": cg.MockObj(str(conf[name]))})()]

        with (
            patch.object(entity, "get_widgets", widgets),
            patch.object(scalar, "get_widgets", widgets),
        ):
            asyncio.run(entity.entity_controls_to_code([control_config]))
            asyncio.run(
                scalar.direct_scalars_to_code(
                    [scalar_config], generic_controls={"control"}
                )
            )
        self.assertEqual(
            1, get_slot_count("ESPHOME_LVGL_NAVIGATION_ADMISSION_GUARD_COUNT")
        )
        CORE.flush_tasks()
        generated = str(CORE.cpp_main_section)
        self.assertIn("MaterialGenericEntityControlComponent(ui)", generated)
        self.assertIn(
            "set_number_profile(nav, app, minus, plus, &lv_font_montserrat_40)",
            generated,
        )
        definitions = {define.name for define in CORE.defines}
        self.assertIn("USE_LVGL_MATERIAL_NUMBER_STEPPER", definitions)
        self.assertIn("USE_LVGL_NAVIGATION_ADMISSION_GUARD", definitions)

    def test_number_only_scene_type_and_legacy_type_boundary(self):
        from esphome.components.lvgl.types import lv_obj_t
        from esphome.config import Config, IDPassValidationStep

        for generic in (True, False):
            CORE.reset()
            config = Config()
            config["control"] = {
                "id": ID(
                    "control",
                    type=entity.MaterialGenericEntityControlComponent
                    if generic
                    else entity.MaterialEntityControlComponent,
                    is_declaration=True,
                )
            }
            config["widgets"] = [
                {"id": ID(name, type=type_, is_declaration=True)}
                for name, type_ in (
                    ("ui", LvglComponent),
                    ("arc", lv_obj_t),
                    ("knob", lv_obj_t),
                    ("label", lv_obj_t),
                )
            ]
            config["overlay"] = module.DIRECT_VOLUME_OVERLAY_SCHEMA(
                {
                    "id": "overlay",
                    "lvgl_id": "ui",
                    "arc": "arc",
                    "knob": "knob",
                    "label": "label",
                    "scene_controllers": ["control"],
                }
            )
            IDPassValidationStep().run(config)
            self.assertEqual(generic, not bool(config.errors))

    def test_disabled_owner_with_explicit_profile_is_valid(self):
        CORE.reset()
        conf = entity.ENTITY_CONTROL_SCHEMA(self.enabled(enabled=False))
        self.assertTrue(conf["number_stepper"])
        self.assertFalse(conf["enabled"])
        self.assertIs(conf["id"].type, entity.MaterialGenericEntityControlComponent)

    def test_default_codegen_emits_no_new_feature_flags(self):
        CORE.reset()
        cg.new_Pvariable(ID("ui", type=LvglComponent, is_declaration=True))
        CORE.component_ids.add("control")

        async def widgets(conf, name):
            return [type("Widget", (), {"obj": cg.MockObj(str(conf[name]))})()]

        with patch.object(entity, "get_widgets", widgets):
            asyncio.run(
                entity.entity_controls_to_code(
                    [entity.ENTITY_CONTROL_SCHEMA(self.config())]
                )
            )
        CORE.flush_tasks()
        definitions = {define.name for define in CORE.defines}
        self.assertNotIn("USE_LVGL_MATERIAL_NUMBER_STEPPER", definitions)
        self.assertNotIn("USE_LVGL_MATERIAL_ENTITY_CONTROL_TILE_ROUTING", definitions)
        self.assertNotIn("USE_LVGL_NAVIGATION_ADMISSION_GUARD", definitions)

    def test_actual_esphome_font_pointer_conversion(self):
        CORE.reset()
        CORE.loaded_integrations.add("font")
        font_module = importlib.import_module("esphome.components.font")
        for name, type_ in (
            ("ui", LvglComponent),
            ("nav", LvglNavigation),
            ("app", LvglApplication),
            ("full_font", font_module.Font),
        ):
            cg.new_Pvariable(ID(name, type=type_, is_declaration=True))
        CORE.component_ids.add("control")
        config = entity.ENTITY_CONTROL_SCHEMA(
            self.enabled(number_value_font="full_font")
        )
        from esphome.config import iter_ids

        declared = {str(id_) for id_, _ in iter_ids(config) if id_.id is not None}
        for id_, _ in iter_ids(config):
            if id_.id is None:
                id_.resolve(declared)
                declared.add(str(id_))

        async def widgets(conf, name):
            return [type("Widget", (), {"obj": cg.MockObj(str(conf[name]))})()]

        with patch.object(entity, "get_widgets", widgets):
            asyncio.run(entity.entity_controls_to_code([config]))
        CORE.flush_tasks()
        self.assertIn(
            "set_number_profile(nav, app, minus, plus, full_font->get_lv_font())",
            str(CORE.cpp_main_section),
        )
        with self.assertRaises(cv.Invalid):
            entity.ENTITY_CONTROL_SCHEMA(
                self.enabled(number_value_font=Lambda("return full_font;"))
            )

    def test_typed_tile_opt_in_and_exact_callback_signature(self):
        CORE.reset()
        binding = {
            "slot": 2,
            "widget": "button",
            "icon_label": "icon",
            "title_label": "tile_title",
            "subtitle_label": "subtitle",
        }
        callback = [
            {
                "then": [
                    {
                        "lambda": Lambda(
                            "(void)slot; (void)card_id; (void)revision; (void)control_kind;"
                        )
                    }
                ]
            }
        ]
        for fields in (
            {"number_controls": True},
            {"on_entity_control_press": callback},
        ):
            with self.subTest(fields=fields), self.assertRaises(cv.Invalid):
                module.TILE_SURFACE_SCHEMA(
                    {"id": "tiles", "tiles": [binding], **fields}
                )
        disabled = module.TILE_SURFACE_SCHEMA(
            {
                "id": "tiles",
                "tiles": [binding],
                "number_controls": True,
                "on_entity_control_press": callback,
            }
        )
        self.assertFalse(disabled["control_press_enabled"])
        config = module.CONFIG_SCHEMA(
            {
                "tile_surfaces": [
                    {
                        "id": "tiles",
                        "tiles": [binding],
                        "control_press_enabled": True,
                        "number_controls": True,
                        "on_entity_control_press": callback,
                    }
                ]
            }
        )
        from esphome.config import iter_ids

        declared = {str(id_) for id_, _ in iter_ids(config) if id_.id is not None}
        for id_, _ in iter_ids(config):
            if id_.id is None:
                id_.resolve(declared)
                declared.add(str(id_))
        CORE.component_ids.add("tiles")

        async def widgets(conf, name):
            return [type("Widget", (), {"obj": cg.MockObj(str(conf[name]))})()]

        with patch.object(module, "get_widgets", widgets):
            asyncio.run(module.to_code(config))
        CORE.flush_tasks()
        generated = str(CORE.cpp_main_section)
        self.assertIn("set_number_controls_enabled(true)", generated)
        self.assertIn("add_on_entity_control_press_callback", generated)
        self.assertIn("uint8_t control_kind", generated)
        definitions = {define.name for define in CORE.defines}
        self.assertIn("USE_LVGL_MATERIAL_ENTITY_CONTROL_TILE_ROUTING", definitions)
        self.assertNotIn("USE_LVGL_MATERIAL_NUMBER_STEPPER", definitions)
