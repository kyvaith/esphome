"""Independent default-off date/time presenter bindings and metadata validation."""

import asyncio
import copy
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
    value = {key: "dt_" + key for key in entity.DATETIME_COMMON_WIDGET_FIELDS}
    value.update(
        icons={key: "dt_icon_" + key for key in ("date", "time")},
        fields={
            name: {key: f"dt_{name}_{key}" for key in ("button", "caption", "value")}
            for name in entity.DATETIME_FIELD_NAMES
        },
        digits=[f"dt_digit_{i}" for i in range(10)],
        backspace="dt_backspace",
        accept="dt_accept",
        cancel_label="dt_cancel_label",
        save_label="dt_save_label",
        controls={key: "dt_" + key for key in entity.DATETIME_CONTROL_NAMES},
        field_layouts={
            "overview": {"x": 12, "y": 80, "height": 120, "field_height": 50},
            "editor": {"x": 12, "y": 40, "height": 80, "field_height": 30},
        },
    )
    return value


class DatetimeSchemaTests(unittest.TestCase):
    def setUp(self):
        CORE.reset()
        self.base = number.NumberCodegenTests()

    def generate(self, value=None):
        for name, type_ in (
            ("ui", LvglComponent),
            ("nav", LvglNavigation),
            ("app", LvglApplication),
        ):
            cg.new_Pvariable(ID(name, type=type_, is_declaration=True))
        CORE.component_ids.add("control")
        config = entity.ENTITY_CONTROL_SCHEMA(
            self.base.enabled(
                **({} if value is None else {"datetime_controller": value})
            )
        )
        declared = {str(key) for key, _ in iter_ids(config) if key.id is not None}
        for key, _ in iter_ids(config):
            if key.id is None:
                key.resolve(declared)
                declared.add(str(key))

        async def widgets(config, name="id"):
            return [type("Widget", (), {"obj": cg.MockObj(str(config[name]))})()]

        with patch.object(entity, "get_widgets", widgets):
            asyncio.run(entity.entity_controls_to_code([config]))
        CORE.flush_tasks()
        return str(CORE.cpp_main_section), {value.name for value in CORE.defines}

    def test_absent_profile_has_no_datetime_or_peer_dependency(self):
        source, defines = self.generate()
        self.assertNotIn("set_datetime_", source)
        for feature in ("DATETIME", "ACTION", "EXTRA", "PANEL"):
            self.assertNotIn("USE_LVGL_MATERIAL_" + feature + "_CONTROLS", defines)

    def test_configured_profile_defaults_disabled_and_preserves_exact_bindings(self):
        value = profile()
        source, defines = self.generate(value)
        self.assertIn("set_datetime_enabled(false)", source)
        self.assertIn("USE_LVGL_MATERIAL_DATETIME_CONTROLS", defines)
        self.assertNotIn("USE_LVGL_MATERIAL_ACTION_CONTROLS", defines)
        self.assertNotIn("USE_LVGL_MATERIAL_INFO_CONTEXT", defines)
        self.assertIn(
            "set_datetime_profile("
            + ", ".join(value[key] for key in entity.DATETIME_COMMON_WIDGET_FIELDS)
            + ")",
            source,
        )
        for index, field in enumerate(entity.DATETIME_FIELD_NAMES):
            self.assertIn(
                f"set_datetime_field({index}, "
                + ", ".join(
                    value["fields"][field][key]
                    for key in ("button", "caption", "value")
                )
                + ")",
                source,
            )
        for index in range(10):
            self.assertIn(f"set_datetime_key({index}, dt_digit_{index})", source)
        self.assertIn("set_datetime_key(10, dt_backspace)", source)
        self.assertIn("set_datetime_key(11, dt_accept)", source)
        self.assertIn("set_datetime_field_layout(0, 12, 80, 120, 50)", source)
        self.assertIn("set_datetime_field_layout(1, 12, 40, 80, 30)", source)

    def test_explicit_enabled_is_independent_of_action_and_info(self):
        value = profile()
        value["enabled"] = True
        source, _ = self.generate(value)
        self.assertIn("set_datetime_enabled(true)", source)
        self.assertNotIn("set_extra_profiles(", source)
        self.assertEqual(source.count("MaterialGenericEntityControlComponent(ui)"), 1)

    def test_missing_duplicate_or_inconsistent_metadata_is_rejected(self):
        cases = []
        for key in entity.DATETIME_COMMON_WIDGET_FIELDS:
            value = profile()
            del value[key]
            cases.append(value)
        for field in entity.DATETIME_FIELD_NAMES:
            for key in ("button", "caption", "value"):
                value = profile()
                del value["fields"][field][key]
                cases.append(value)
                value = profile()
                value["fields"][field][key] = value["root"]
                cases.append(value)
        for count in (9, 11):
            value = profile()
            value["digits"] = [f"dt_digit_{i}" for i in range(count)]
            cases.append(value)
        value = profile()
        value["digits"][9] = value["controls"]["save"]
        cases.append(value)
        for name in ("overview", "editor"):
            for key, bad in (
                ("x", -1),
                ("y", 32768),
                ("height", 0),
                ("height", 49),
                ("field_height", 0),
            ):
                value = profile()
                value["field_layouts"][name][key] = bad
                cases.append(value)
        for value in cases:
            CORE.reset()
            with self.subTest(value=value), self.assertRaises(cv.Invalid):
                entity.ENTITY_CONTROL_SCHEMA(
                    self.base.enabled(datetime_controller=copy.deepcopy(value))
                )
        with self.assertRaises(cv.Invalid):
            entity.ENTITY_CONTROL_SCHEMA(
                self.base.config(datetime_controller=profile())
            )


if __name__ == "__main__":
    unittest.main(verbosity=2)
