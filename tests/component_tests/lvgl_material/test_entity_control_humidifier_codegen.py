"""A complete humidifier view is opt-in and reuses the existing application owner."""

import asyncio
import unittest
from unittest.mock import patch

import test_entity_control_number_codegen as number

from esphome import codegen as cg, config_validation as cv
from esphome.components.lvgl.lvcode import LvglComponent
from esphome.components.lvgl.types import LvglApplication, LvglNavigation
from esphome.config import iter_ids
from esphome.core import CORE, ID

entity = number.entity


class HumidifierCodegenTests(unittest.TestCase):
    def profile(self):
        return {
            **{name: f"humidifier_{name}" for name in entity.HUMIDIFIER_WIDGET_FIELDS},
            **{
                group: [
                    {
                        "button": f"humidifier_{group}_{index}",
                        "label": f"humidifier_{group}_{index}_label",
                    }
                    for index in range(8)
                ]
                for group in ("mode_options",)
            },
        }

    def test_default_and_incomplete_bindings(self):
        fixture = number.NumberCodegenTests()
        CORE.reset()
        self.assertNotIn(
            "humidifier_controller", entity.ENTITY_CONTROL_SCHEMA(fixture.enabled())
        )
        for profile in (
            {},
            self.profile() | {"mode_options": self.profile()["mode_options"][:7]},
            self.profile() | {"status_label": "humidifier_state_label"},
        ):
            with self.subTest(profile=profile), self.assertRaises(cv.Invalid):
                entity.ENTITY_CONTROL_SCHEMA(
                    fixture.enabled(humidifier_controller=profile)
                )
        with self.assertRaises(cv.Invalid):
            entity.ENTITY_CONTROL_SCHEMA(
                fixture.config(humidifier_controller=self.profile())
            )

    def test_one_owner_and_fixed_widget_pools(self):
        fixture = number.NumberCodegenTests()
        CORE.reset()
        for name, type_ in (
            ("ui", LvglComponent),
            ("nav", LvglNavigation),
            ("app", LvglApplication),
        ):
            cg.new_Pvariable(ID(name, type=type_, is_declaration=True))
        CORE.component_ids.add("control")
        config = entity.ENTITY_CONTROL_SCHEMA(
            fixture.enabled(humidifier_controller=self.profile())
        )
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
        generated = str(CORE.cpp_main_section)
        self.assertEqual(
            generated.count("MaterialGenericEntityControlComponent(ui)"), 1
        )
        self.assertEqual(generated.count("set_humidifier_profile("), 1)
        self.assertEqual(generated.count("set_humidifier_mode_option("), 8)
        self.assertIn(
            "USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS",
            {define.name for define in CORE.defines},
        )
