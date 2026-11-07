"""Tile-event opt-in is additive to the existing Action widget bindings."""

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


def profile():
    result = {field: "action_" + field for field in entity.ACTION_COMMON_WIDGET_FIELDS}
    result["icons"] = {
        field: "action_icon_" + field for field in entity.ACTION_ICON_FIELDS
    }
    result["buttons"] = [
        {"button": f"action_button_{i}", "label": f"action_label_{i}"} for i in range(3)
    ]
    return result


class TileEventCodegenTests(unittest.TestCase):
    def setUp(self):
        CORE.reset()
        self.fixture = number.NumberCodegenTests()

    def generate(self, action):
        for name, cls in (
            ("ui", LvglComponent),
            ("nav", LvglNavigation),
            ("app", LvglApplication),
        ):
            cg.new_Pvariable(ID(name, type=cls, is_declaration=True))
        CORE.component_ids.add("control")
        conf = entity.ENTITY_CONTROL_SCHEMA(
            self.fixture.enabled(action_controller=action)
        )
        declared = {str(key) for key, _ in iter_ids(conf) if key.id is not None}
        for key, _ in iter_ids(conf):
            if key.id is None:
                key.resolve(declared)
                declared.add(str(key))

        async def widgets(config, name):
            return [type("Widget", (), {"obj": cg.MockObj(str(config[name]))})()]

        with patch.object(entity, "get_widgets", widgets):
            asyncio.run(entity.entity_controls_to_code([conf]))
        CORE.flush_tasks()
        return str(CORE.cpp_main_section), {value.name for value in CORE.defines}

    def test_default_off_preserves_action_bindings(self):
        action = profile()
        action["enabled"] = True
        generated, defines = self.generate(action)
        self.assertIn("set_action_enabled(true)", generated)
        self.assertNotIn("USE_LVGL_MATERIAL_TILE_EVENT_CONTROLS", defines)
        self.assertNotIn("set_tile_event_enabled", generated)

    def test_explicit_opt_in_has_no_new_widgets(self):
        action = profile()
        action.update(enabled=True, tile_event=True)
        generated, defines = self.generate(action)
        self.assertIn("USE_LVGL_MATERIAL_TILE_EVENT_CONTROLS", defines)
        self.assertEqual(generated.count("set_tile_event_enabled(true)"), 1)
        self.assertEqual(generated.count("set_action_icon("), 4)
        self.assertEqual(generated.count("set_action_button("), 3)
        self.assertEqual(generated.count("set_action_profile("), 1)

    def test_requires_enabled_action(self):
        action = profile()
        action["tile_event"] = True
        with self.assertRaises(cv.Invalid):
            entity.ENTITY_CONTROL_SCHEMA(self.fixture.enabled(action_controller=action))

    def test_rejects_untyped_flag(self):
        action = profile()
        action.update(enabled=True, tile_event={"event": "custom"})
        with self.assertRaises(cv.Invalid):
            entity.ENTITY_CONTROL_SCHEMA(self.fixture.enabled(action_controller=action))
