"""Cover widgets and priority behavior are explicit and default disabled."""

from __future__ import annotations

import asyncio
import unittest
from unittest.mock import patch

import test_entity_control_number_codegen as number

from esphome import codegen as cg, config_validation as cv
from esphome.components.lvgl.lvcode import LvglComponent
from esphome.components.lvgl.types import LvglApplication, LvglNavigation
from esphome.core import CORE, ID
from esphome.types import ConfigType

entity = number.entity


class CoverCodegenTests(unittest.TestCase):
    def profile(self) -> ConfigType:
        return {
            "root": "cover_root",
            "current_label": "cover_current",
            "status_label": "cover_status",
            "buttons": [
                {"button": f"cover_button_{index}", "label": f"cover_label_{index}"}
                for index in range(3)
            ],
        }

    def test_default_absence_and_complete_distinct_generic_bindings(self) -> None:
        fixture = number.NumberCodegenTests()
        CORE.reset()
        self.assertNotIn(
            "cover_controller", entity.ENTITY_CONTROL_SCHEMA(fixture.enabled())
        )
        for profile in (
            {},
            self.profile() | {"buttons": self.profile()["buttons"][:2]},
            self.profile() | {"status_label": "cover_current"},
        ):
            with self.subTest(profile=profile), self.assertRaises(cv.Invalid):
                entity.ENTITY_CONTROL_SCHEMA(fixture.enabled(cover_controller=profile))
        with self.assertRaises(cv.Invalid):
            entity.ENTITY_CONTROL_SCHEMA(
                fixture.config(cover_controller=self.profile())
            )
        valid = entity.ENTITY_CONTROL_SCHEMA(
            fixture.enabled(cover_controller=self.profile())
        )
        self.assertEqual(len(valid["cover_controller"]["buttons"]), 3)
        self.assertIs(valid["cover_controller"]["priority_stop"], False)

    def test_one_owner_three_buttons_and_explicit_priority_define(self) -> None:
        for priority in (False, True):
            with self.subTest(priority=priority):
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
                    fixture.enabled(
                        cover_controller=self.profile() | {"priority_stop": priority}
                    )
                )
                from esphome.config import iter_ids

                declared = {
                    str(id_) for id_, _ in iter_ids(config) if id_.id is not None
                }
                for id_, _ in iter_ids(config):
                    if id_.id is None:
                        id_.resolve(declared)
                        declared.add(str(id_))

                async def widgets(conf: ConfigType, name: str) -> list:
                    return [type("Widget", (), {"obj": cg.MockObj(str(conf[name]))})()]

                with patch.object(entity, "get_widgets", widgets):
                    asyncio.run(entity.entity_controls_to_code([config]))
                CORE.flush_tasks()
                generated = str(CORE.cpp_main_section)
                self.assertEqual(
                    generated.count("MaterialGenericEntityControlComponent(ui)"), 1
                )
                self.assertIn(
                    "set_cover_profile(cover_root, cover_current, cover_status)",
                    generated,
                )
                self.assertEqual(generated.count("set_cover_button("), 3)
                self.assertIn(
                    "USE_LVGL_MATERIAL_COVER_CONTROLS",
                    {define.name for define in CORE.defines},
                )
                self.assertEqual(
                    "USE_LVGL_MATERIAL_COVER_PRIORITY_STOP"
                    in {define.name for define in CORE.defines},
                    priority,
                )
                self.assertEqual(
                    "set_cover_priority_stop_enabled(true)" in generated, priority
                )
