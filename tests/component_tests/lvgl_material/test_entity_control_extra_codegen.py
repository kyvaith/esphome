"""Default-off Extra profiles, widget binding and bounded code generation."""

import asyncio
import copy
import unittest
from unittest.mock import patch

import test_entity_control_number_codegen as number

from esphome import codegen as cg, config_validation as cv
from esphome.components.font import Font
from esphome.components.lvgl.lvcode import LvglComponent
from esphome.components.lvgl.types import LvglApplication, LvglNavigation
from esphome.config import iter_ids
from esphome.core import CORE, ID

entity = number.entity


def profile():
    result = {field: "extra_" + field for field in entity.EXTRA_COMMON_WIDGET_FIELDS}
    result.update(info=True, lock=True)
    for family, fields, font_field, _ in entity.EXTRA_PROFILES:
        result[family] = {field: f"extra_{family}_{field}" for field in fields}
        result[family][font_field] = "montserrat_16"
    return result


class ExtraCodegenTests(unittest.TestCase):
    def setUp(self):
        CORE.reset()
        self.fixture = number.NumberCodegenTests()

    def generate(self, extra=None, custom_fonts=()):
        if custom_fonts:
            CORE.loaded_integrations.add("font")
        for name, type_ in (
            ("ui", LvglComponent),
            ("nav", LvglNavigation),
            ("app", LvglApplication),
        ):
            cg.new_Pvariable(ID(name, type=type_, is_declaration=True))
        for name in custom_fonts:
            cg.new_Pvariable(ID(name, type=Font, is_declaration=True))
        CORE.component_ids.add("control")
        conf = entity.ENTITY_CONTROL_SCHEMA(
            self.fixture.enabled(
                **({} if extra is None else {"extra_controller": extra})
            )
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

    def test_default_off_does_not_enable_extra_or_old_panel(self):
        generated, defines = self.generate()
        self.assertNotIn("set_extra_", generated)
        self.assertNotIn("USE_LVGL_MATERIAL_EXTRA_CONTROLS", defines)
        self.assertNotIn("USE_LVGL_MATERIAL_PANEL_CONTROLS", defines)

    def test_exact_widget_order_one_owner_and_explicit_mask(self):
        source = profile()
        generated, defines = self.generate(source)
        self.assertEqual(
            generated.count("MaterialGenericEntityControlComponent(ui)"), 1
        )
        self.assertIn(
            "set_extra_profile("
            + ", ".join(source[field] for field in entity.EXTRA_COMMON_WIDGET_FIELDS)
            + ")",
            generated,
        )
        for family, fields, _, method in entity.EXTRA_PROFILES:
            self.assertIn(
                method
                + "("
                + ", ".join(source[family][field] for field in fields)
                + ", &lv_font_montserrat_16)",
                generated,
            )
        self.assertIn("set_extra_profiles(true, true)", generated)
        self.assertTrue(
            {
                "USE_LVGL_MATERIAL_EXTRA_CONTROLS",
                "USE_LVGL_MATERIAL_NUMBER_STEPPER",
                "USE_LVGL_MATERIAL_PANEL_CONTROLS",
            }
            <= defines
        )
        self.assertNotIn("set_timer_panel_profile(", generated)
        self.assertNotIn("set_remote_panel_profile(", generated)
        self.assertNotIn("set_power_panel_profile(", generated)
        self.assertNotIn("USE_LVGL_MATERIAL_TIMER_PANEL", defines)

    def test_independent_info_lock_and_optional_compact_font(self):
        for info, lock in ((True, False), (False, True)):
            CORE.reset()
            source = profile()
            source.update(info=info, lock=lock)
            for family, _, font_field, _ in entity.EXTRA_PROFILES:
                del source[family][font_field]
            generated, _ = self.generate(source)
            self.assertIn(
                f"set_extra_profiles({str(info).lower()}, {str(lock).lower()})",
                generated,
            )
            self.assertEqual(generated.count(", nullptr);"), 3)

    def test_reject_missing_widgets_no_profile_duplicate_and_templated_font(self):
        cases = []
        value = profile()
        value.update(info=False, lock=False)
        cases.append(value)
        for family, fields, font_field, _ in entity.EXTRA_PROFILES:
            for field in fields:
                value = profile()
                del value[family][field]
                cases.append(value)
            value = profile()
            value[family][fields[0]] = value["root"]
            cases.append(value)
            value = profile()
            value[family][font_field] = cv.Lambda("return nullptr;")
            cases.append(value)
        value = profile()
        value["calendar"]["summary_label"] = value["sensor"]["value_label"]
        cases.append(value)
        for value in cases:
            with self.assertRaises(cv.Invalid):
                entity.ENTITY_CONTROL_SCHEMA(
                    self.fixture.enabled(extra_controller=value)
                )
        with self.assertRaises(cv.Invalid):
            entity.ENTITY_CONTROL_SCHEMA(
                self.fixture.config(extra_controller=profile())
            )

    def test_explicit_widget_binding_and_custom_fonts(self):
        source = profile()
        for family, _, font_field, _ in entity.EXTRA_PROFILES:
            source[family][font_field] = (
                "compact_value" if family == "sensor" else "compact_text"
            )
        generated, _ = self.generate(
            copy.deepcopy(source), ("compact_text", "compact_value")
        )
        self.assertIn("compact_value->get_lv_font()", generated)
        self.assertEqual(generated.count("compact_text->get_lv_font()"), 2)
        self.assertIn(
            "set_extra_state_profile(extra_state_root, extra_state_state_label",
            generated,
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
