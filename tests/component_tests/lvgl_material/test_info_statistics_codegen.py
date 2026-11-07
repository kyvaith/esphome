"""Optional Statistics1 uses one surface beside the existing read-only Info owner."""

import asyncio
from copy import deepcopy
import unittest
from unittest.mock import patch

import test_entity_control_extra_codegen as extra
import test_entity_control_number_codegen as number

from esphome import codegen as cg, config_validation as cv
from esphome.components.lvgl.lvcode import LvglComponent
from esphome.components.lvgl.types import LvglApplication, LvglNavigation
from esphome.config import iter_ids
from esphome.core import CORE, ID

entity = extra.entity


class StatisticsCodegenTests(unittest.TestCase):
    def setUp(self) -> None:
        CORE.reset()
        self.fixture = number.NumberCodegenTests()

    def generate(self, statistics: dict | None = None) -> tuple[str, set[str]]:
        for name, type_ in (
            ("ui", LvglComponent),
            ("nav", LvglNavigation),
            ("app", LvglApplication),
        ):
            cg.new_Pvariable(ID(name, type=type_, is_declaration=True))
        CORE.component_ids.add("control")
        values = {"extra_controller": extra.profile()}
        if statistics is not None:
            values["statistics_profile"] = statistics
        conf = entity.ENTITY_CONTROL_SCHEMA(self.fixture.enabled(**values))
        declared = {str(key) for key, _ in iter_ids(conf) if key.id is not None}
        for key, _ in iter_ids(conf):
            if key.id is None:
                key.resolve(declared)
                declared.add(str(key))

        async def widgets(config: dict, name: str) -> list:
            return [type("Widget", (), {"obj": cg.MockObj(str(config[name]))})()]

        with patch.object(entity, "get_widgets", widgets):
            asyncio.run(entity.entity_controls_to_code([conf]))
        CORE.flush_tasks()
        return str(CORE.cpp_main_section), {value.name for value in CORE.defines}

    def test_absent_preserves_existing_info(self) -> None:
        code, defines = self.generate()
        self.assertNotIn("set_statistics_profile", code)
        self.assertNotIn("USE_LVGL_MATERIAL_INFO_STATISTICS", defines)
        self.assertIn("set_extra_profiles(true, true)", code)

    def test_bound_surface_defaults_disabled(self) -> None:
        code, defines = self.generate({"surface": "statistics_surface"})
        self.assertIn("set_statistics_profile(statistics_surface, false)", code)
        self.assertIn("USE_LVGL_MATERIAL_INFO_STATISTICS", defines)

    def test_enabled_has_one_owner_and_no_other_optional_profile(self) -> None:
        code, defines = self.generate(
            {"surface": "statistics_surface", "enabled": True}
        )
        self.assertEqual(code.count("set_statistics_profile("), 1)
        self.assertIn("set_statistics_profile(statistics_surface, true)", code)
        self.assertEqual(code.count("MaterialGenericEntityControlComponent(ui)"), 1)
        for suffix in (
            "INFO_AGENDA",
            "INFO_CONTEXT",
            "DETAIL_CONTROLS",
            "LIGHT_COLOR",
            "CLIMATE_RANGE",
        ):
            self.assertNotIn("USE_LVGL_MATERIAL_" + suffix, defines)

    def test_requires_existing_info(self) -> None:
        value = {"statistics_profile": {"surface": "statistics_surface"}}
        with self.assertRaises(cv.Invalid):
            entity.ENTITY_CONTROL_SCHEMA(self.fixture.enabled(**value))
        value["extra_controller"] = extra.profile()
        value["extra_controller"]["info"] = False
        with self.assertRaises(cv.Invalid):
            entity.ENTITY_CONTROL_SCHEMA(self.fixture.enabled(**value))

    def test_requires_surface_and_rejects_unknown_configuration(self) -> None:
        for profile in (
            {"enabled": True},
            {"surface": "statistics_surface", "refresh_interval": "1s"},
        ):
            with self.subTest(profile=profile), self.assertRaises(cv.Invalid):
                entity.ENTITY_CONTROL_SCHEMA(
                    self.fixture.enabled(
                        extra_controller=extra.profile(), statistics_profile=profile
                    )
                )

    def test_rejects_aliases_with_all_extra_and_agenda_widgets(self) -> None:
        info = extra.profile()
        for family, fields in (
            ("person", entity.EXTRA_PERSON_WIDGET_FIELDS),
            ("sun", entity.EXTRA_SUN_WIDGET_FIELDS),
        ):
            info[family] = {field: f"{family}_{field}" for field in fields}
        agenda = {field: "agenda_" + field for field in entity.AGENDA_WIDGET_FIELDS}
        names = [info[field] for field in entity.EXTRA_COMMON_WIDGET_FIELDS]
        for family, fields, _, _ in entity.EXTRA_PROFILES:
            names.extend(info[family][field] for field in fields)
        names.extend(
            info[family][field]
            for family, fields in (
                ("person", entity.EXTRA_PERSON_WIDGET_FIELDS),
                ("sun", entity.EXTRA_SUN_WIDGET_FIELDS),
            )
            for field in fields
        )
        names.extend(agenda.values())
        for name in names:
            with self.subTest(name=name), self.assertRaises(cv.Invalid):
                entity.ENTITY_CONTROL_SCHEMA(
                    self.fixture.enabled(
                        extra_controller=deepcopy(info),
                        agenda_profile=agenda,
                        statistics_profile={"surface": name},
                    )
                )


if __name__ == "__main__":
    unittest.main()
