"""Choice widgets are explicit, bounded and absent from default controls."""

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


class ChoiceCodegenTests(unittest.TestCase):
    def profile(self) -> ConfigType:
        return {
            "root": "choice_root",
            "apply_button": "choice_apply",
            "apply_label": "choice_apply_label",
            "current_label": "choice_current",
            "status_label": "choice_status",
            "neutral_reference": "off_button",
            "options": [
                {"button": f"choice_button_{index}", "label": f"choice_label_{index}"}
                for index in range(8)
            ],
        }

    def test_default_absence_and_complete_distinct_generic_bindings(self) -> None:
        fixture = number.NumberCodegenTests()
        CORE.reset()
        self.assertNotIn(
            "choice_selector", entity.ENTITY_CONTROL_SCHEMA(fixture.enabled())
        )
        for profile in (
            {},
            self.profile() | {"options": self.profile()["options"][:7]},
            self.profile() | {"apply_label": "choice_apply"},
        ):
            with self.subTest(profile=profile), self.assertRaises(cv.Invalid):
                entity.ENTITY_CONTROL_SCHEMA(fixture.enabled(choice_selector=profile))
        with self.assertRaises(cv.Invalid):
            entity.ENTITY_CONTROL_SCHEMA(fixture.config(choice_selector=self.profile()))
        valid = entity.ENTITY_CONTROL_SCHEMA(
            fixture.enabled(choice_selector=self.profile())
        )
        self.assertEqual(len(valid["choice_selector"]["options"]), 8)
        self.assertIs(valid["choice_selector"]["allow_empty_selection"], False)
        opted = entity.ENTITY_CONTROL_SCHEMA(
            fixture.enabled(
                choice_selector=self.profile() | {"allow_empty_selection": True}
            )
        )
        self.assertIs(opted["choice_selector"]["allow_empty_selection"], True)

    def test_generated_feature_has_one_owner_and_eight_existing_widget_pairs(
        self,
    ) -> None:
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
            fixture.enabled(choice_selector=self.profile())
        )
        from esphome.config import iter_ids

        declared = {str(id_) for id_, _ in iter_ids(config) if id_.id is not None}
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
            "set_choice_profile(choice_root, choice_apply, choice_apply_label, choice_current, choice_status, off_button)",
            generated,
        )
        self.assertEqual(generated.count("set_choice_option("), 8)
        self.assertIn(
            "USE_LVGL_MATERIAL_CHOICE_SELECTOR",
            {define.name for define in CORE.defines},
        )
