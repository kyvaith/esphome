"""Focused validation of the optional detail view bindings."""

import asyncio
import unittest
from unittest.mock import patch

import test_entity_control_number_codegen as number

from esphome import codegen as cg, config_validation as cv
from esphome.components.lvgl.lvcode import LvglComponent
from esphome.components.lvgl.types import LvglApplication, LvglNavigation
from esphome.config import iter_ids
from esphome.core import CORE, ID
from esphome.cpp_helpers import get_slot_count

entity = number.entity


def profile():
    detail = {
        family: {field: f"detail_{family}_{field}" for field in fields}
        for family, fields, _ in entity.DETAIL_PROFILES
    }
    detail["climate"]["parameter_buttons"] = [
        {"button": f"parameter_{i}", "label": f"parameter_label_{i}"} for i in range(5)
    ]
    detail["choice"]["options"] = [
        {"button": f"option_{i}", "label": f"option_label_{i}"} for i in range(8)
    ]
    return detail


class DetailCodegenTests(unittest.TestCase):
    def setUp(self):
        CORE.reset()
        self.fixture = number.NumberCodegenTests()

    def generate(self, detail=None):
        for name, type_ in (
            ("ui", LvglComponent),
            ("nav", LvglNavigation),
            ("app", LvglApplication),
        ):
            cg.new_Pvariable(ID(name, type=type_, is_declaration=True))
        CORE.component_ids.add("control")
        extra = {} if detail is None else {"detail_controller": detail}
        config = entity.ENTITY_CONTROL_SCHEMA(self.fixture.enabled(**extra))
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
        return str(CORE.cpp_main_section), {define.name for define in CORE.defines}

    def test_default_off(self):
        generated, defines = self.generate()
        self.assertNotIn("set_light_detail_profile", generated)
        self.assertNotIn("set_detail_numeric_profile", generated)
        self.assertNotIn("USE_LVGL_MATERIAL_DETAIL_CONTROLS", defines)
        self.assertNotIn("USE_LVGL_MATERIAL_THERMOSTAT", defines)

    def test_complete_one_owner_and_exact_bound_order(self):
        source = profile()
        generated, defines = self.generate(source)
        self.assertEqual(
            generated.count("MaterialGenericEntityControlComponent(ui)"), 1
        )
        self.assertEqual(
            get_slot_count("ESPHOME_LVGL_NAVIGATION_ADMISSION_GUARD_COUNT"), 1
        )
        for family, fields, setter in entity.DETAIL_PROFILES:
            call = (
                setter
                + "("
                + ", ".join(source[family][field] for field in fields)
                + ")"
            )
            self.assertIn(call, generated)
        self.assertEqual(generated.count("set_detail_parameter("), 5)
        self.assertEqual(generated.count("set_detail_option("), 8)
        self.assertTrue(
            {
                "USE_LVGL_MATERIAL_DETAIL_CONTROLS",
                "USE_LVGL_MATERIAL_PANEL_CONTROLS",
                "USE_LVGL_MATERIAL_THERMOSTAT",
            }
            <= defines
        )

    def test_optional_families(self):
        for omit, present in (("climate", "light"), ("light", "climate")):
            with self.subTest(omit=omit):
                CORE.reset()
                detail = profile()
                del detail[omit]
                generated, _ = self.generate(detail)
                self.assertNotIn(f"set_{omit}_detail_profile(", generated)
                self.assertIn(f"set_{present}_detail_profile(", generated)
                self.assertIn("set_detail_numeric_profile(", generated)
                self.assertIn("set_detail_choice_profile(", generated)

    def test_reject_incomplete_and_unbounded_profiles(self):
        cases = []
        for key in ("numeric", "choice"):
            value = profile()
            del value[key]
            cases.append(value)
        cases.append(
            {
                key: value
                for key, value in profile().items()
                if key in ("numeric", "choice")
            }
        )
        for family, group, count in (
            ("climate", "parameter_buttons", 5),
            ("choice", "options", 8),
        ):
            for length in (0, count - 1, count + 1):
                value = profile()
                value[family][group] = value[family][group][:length] + (
                    [{"button": "extra_button", "label": "extra_label"}]
                    if length > count
                    else []
                )
                cases.append(value)
        value = profile()
        del value["light"]["status_label"]
        cases.append(value)
        for value in cases:
            with self.subTest(value=value), self.assertRaises(cv.Invalid):
                entity.ENTITY_CONTROL_SCHEMA(
                    self.fixture.enabled(detail_controller=value)
                )
        with self.assertRaises(cv.Invalid):
            entity.ENTITY_CONTROL_SCHEMA(
                self.fixture.config(detail_controller=profile())
            )

    def test_reject_duplicate_refs_across_profiles_and_groups(self):
        cases = []
        value = profile()
        value["climate"]["root"] = value["light"]["root"]
        cases.append(value)
        value = profile()
        value["choice"]["options"][1]["button"] = value["choice"]["options"][0][
            "button"
        ]
        cases.append(value)
        value = profile()
        value["climate"]["parameter_buttons"][0]["label"] = value["numeric"][
            "value_label"
        ]
        cases.append(value)
        for value in cases:
            with (
                self.subTest(value=value),
                self.assertRaisesRegex(cv.Invalid, "distinct"),
            ):
                entity.ENTITY_CONTROL_SCHEMA(
                    self.fixture.enabled(detail_controller=value)
                )


if __name__ == "__main__":
    unittest.main(verbosity=2)
