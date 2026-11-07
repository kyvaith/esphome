"""Cover panel extension is explicit, complete and subordinate to priority Stop."""

import asyncio
import unittest
from unittest.mock import patch

import test_entity_control_cover_codegen as cover
import test_entity_control_number_codegen as number

from esphome import codegen as cg, config_validation as cv
from esphome.components.lvgl.lvcode import LvglComponent
from esphome.components.lvgl.types import LvglApplication, LvglNavigation
from esphome.core import CORE, ID

entity = number.entity


class CoverPanelCodegenTests(unittest.TestCase):
    def panel(self):
        return {
            **{name: "panel_" + name for name in entity.COVER_PANEL_WIDGET_FIELDS},
            "tilt_buttons": [
                {"button": f"tilt_button_{index}", "label": f"tilt_label_{index}"}
                for index in range(3)
            ],
            "icons": [
                {"device_class": name, "widget": "icon_" + name}
                for name in entity.COVER_DEVICE_CLASSES
            ],
        }

    def test_opt_in_full_profile_and_invalid_ownership(self):
        fixture = number.NumberCodegenTests()
        base = cover.CoverCodegenTests().profile()
        CORE.reset()
        for profile in (
            base | {"panel": self.panel()},
            base
            | {
                "priority_stop": True,
                "panel": self.panel() | {"title_label": "cover_current"},
            },
            base
            | {"priority_stop": True, "panel": self.panel() | {"tilt_buttons": []}},
            base
            | {
                "priority_stop": True,
                "panel": self.panel()
                | {"icons": [{"device_class": "bad", "widget": "icon"}]},
            },
            base
            | {
                "priority_stop": True,
                "panel": self.panel()
                | {"icons": [{"device_class": "blind", "widget": "icon"}] * 2},
            },
        ):
            with self.subTest(profile=profile), self.assertRaises(cv.Invalid):
                entity.ENTITY_CONTROL_SCHEMA(fixture.enabled(cover_controller=profile))
        self.assertNotIn(
            "panel",
            entity.ENTITY_CONTROL_SCHEMA(fixture.enabled(cover_controller=base))[
                "cover_controller"
            ],
        )

    def test_generated_one_owner_profile_and_icons(self):
        CORE.reset()
        for name, kind in (
            ("ui", LvglComponent),
            ("nav", LvglNavigation),
            ("app", LvglApplication),
        ):
            cg.new_Pvariable(ID(name, type=kind, is_declaration=True))
        CORE.component_ids.add("control")
        base = cover.CoverCodegenTests().profile() | {
            "priority_stop": True,
            "panel": self.panel(),
        }
        config = entity.ENTITY_CONTROL_SCHEMA(
            number.NumberCodegenTests().enabled(cover_controller=base)
        )
        from esphome.config import iter_ids

        declared = {str(value) for value, _ in iter_ids(config) if value.id is not None}
        for value, _ in iter_ids(config):
            if value.id is None:
                value.resolve(declared)
                declared.add(str(value))

        async def widgets(conf, name):
            return [type("Widget", (), {"obj": cg.MockObj(str(conf[name]))})()]

        with patch.object(entity, "get_widgets", widgets):
            asyncio.run(entity.entity_controls_to_code([config]))
        CORE.flush_tasks()
        output = str(CORE.cpp_main_section)
        self.assertEqual(output.count("MaterialGenericEntityControlComponent(ui)"), 1)
        self.assertEqual(output.count("set_cover_panel_profile("), 1)
        self.assertEqual(output.count("set_cover_tilt_button("), 3)
        self.assertEqual(output.count("set_cover_panel_icon("), 11)
        self.assertIn(
            "USE_LVGL_MATERIAL_COVER_PANEL", {value.name for value in CORE.defines}
        )
