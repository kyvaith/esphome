"""A complete robot view is opt-in and reuses the existing application owner."""

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


class PanelCodegenTests(unittest.TestCase):
    def profile(self):
        timer = {name: f"timer_{name}" for name in entity.TIMER_PANEL_WIDGET_FIELDS}
        timer["time_id"] = "panel_clock"
        timer["draft_buttons"] = [f"draft_{i}" for i in range(6)]
        timer["actions"] = [
            {"button": f"action_{i}", "label": f"action_label_{i}"} for i in range(5)
        ]
        remote = {name: f"remote_{name}" for name in entity.REMOTE_PANEL_WIDGET_FIELDS}
        remote["options"] = [
            {"button": f"option_{i}", "label": f"option_label_{i}"} for i in range(8)
        ]
        power = {name: f"power_{name}" for name in entity.POWER_PANEL_WIDGET_FIELDS}
        return {"timer": timer, "remote": remote, "power": power}

    def test_default_and_incomplete_bindings(self):
        fixture = number.NumberCodegenTests()
        CORE.reset()
        self.assertNotIn(
            "panel_controller", entity.ENTITY_CONTROL_SCHEMA(fixture.enabled())
        )
        valid_profile = self.profile()
        for profile in (
            {},
            {"timer": valid_profile["timer"] | {"draft_buttons": []}},
            {"timer": valid_profile["timer"] | {"actions": []}},
            {"remote": valid_profile["remote"] | {"options": []}},
            {
                "power": valid_profile["power"]
                | {"title_label": valid_profile["power"]["state_label"]}
            },
        ):
            with self.subTest(profile=profile), self.assertRaises(cv.Invalid):
                entity.ENTITY_CONTROL_SCHEMA(fixture.enabled(panel_controller=profile))
        with self.assertRaises(cv.Invalid):
            entity.ENTITY_CONTROL_SCHEMA(
                fixture.config(panel_controller=self.profile())
            )

    def test_one_owner_and_fixed_widget_pools(self):
        fixture = number.NumberCodegenTests()
        CORE.reset()
        for name, type_ in (
            ("ui", LvglComponent),
            ("panel_clock", entity.RealTimeClock),
            ("nav", LvglNavigation),
            ("app", LvglApplication),
        ):
            cg.new_Pvariable(ID(name, type=type_, is_declaration=True))
        CORE.component_ids.add("control")
        config = entity.ENTITY_CONTROL_SCHEMA(
            fixture.enabled(panel_controller=self.profile())
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
        self.assertEqual(generated.count("set_timer_panel_profile("), 1)
        self.assertEqual(generated.count("set_remote_panel_option("), 8)
        self.assertEqual(generated.count("set_timer_panel_action("), 5)
        self.assertEqual(generated.count("set_timer_draft_button("), 6)
        self.assertEqual(generated.count("set_panel_time("), 1)
        self.assertEqual(generated.count("set_remote_panel_profile("), 1)
        self.assertEqual(generated.count("set_power_panel_profile("), 1)
        self.assertIn(
            "USE_LVGL_MATERIAL_PANEL_CONTROLS",
            {define.name for define in CORE.defines},
        )
