"""Optional read-only Info context bindings preserve legacy Extra configuration."""

import unittest

import test_entity_control_extra_codegen as extra

from esphome import config_validation as cv
from esphome.core import CORE

entity = extra.entity


def profile():
    result = extra.profile()
    result["info_context"] = True
    for family, fields in (
        ("person", entity.EXTRA_PERSON_WIDGET_FIELDS),
        ("sun", entity.EXTRA_SUN_WIDGET_FIELDS),
    ):
        result[family] = {field: f"context_{family}_{field}" for field in fields}
    result["person"]["compact_state_font"] = "montserrat_16"
    return result


class InfoContextCodegenTests(unittest.TestCase):
    def setUp(self):
        self.fixture = extra.ExtraCodegenTests()
        self.fixture.setUp()

    def test_legacy_profile_compiles_without_context(self):
        generated, defines = self.fixture.generate(extra.profile())
        self.assertNotIn("USE_LVGL_MATERIAL_INFO_CONTEXT", defines)
        self.assertNotIn("set_extra_info_context", generated)
        self.assertNotIn("set_extra_person_profile", generated)

    def test_exact_eighteen_bindings_with_optional_font(self):
        value = profile()
        generated, defines = self.fixture.generate(value)
        self.assertIn("USE_LVGL_MATERIAL_INFO_CONTEXT", defines)
        self.assertIn(
            "set_extra_person_profile("
            + ", ".join(value["person"][field] for field in entity.EXTRA_PERSON_WIDGET_FIELDS)
            + ", &lv_font_montserrat_16)",
            generated,
        )
        self.assertIn(
            "set_extra_sun_profile("
            + ", ".join(value["sun"][field] for field in entity.EXTRA_SUN_WIDGET_FIELDS)
            + ")",
            generated,
        )
        self.assertIn("set_extra_info_context(true)", generated)
        self.assertLess(generated.index("set_extra_profiles("), generated.index("set_extra_info_context("))

    def test_bound_groups_default_disabled_and_custom_font(self):
        value = profile()
        del value["info_context"]
        value["person"]["compact_state_font"] = "context_compact"
        generated, _ = self.fixture.generate(value, ("context_compact",))
        self.assertIn("set_extra_info_context(false)", generated)
        self.assertIn("context_compact->get_lv_font()", generated)

    def test_reject_partial_missing_or_shared_widgets_and_missing_base_profile(self):
        cases = []
        value = profile()
        value["info"] = False
        cases.append(value)
        for family, fields in (
            ("person", entity.EXTRA_PERSON_WIDGET_FIELDS),
            ("sun", entity.EXTRA_SUN_WIDGET_FIELDS),
        ):
            for enabled in (False, True):
                value = profile()
                value["info_context"] = enabled
                del value[family]
                cases.append(value)
            for field in fields:
                value = profile()
                del value[family][field]
                cases.append(value)
                value = profile()
                value[family][field] = value["root"]
                cases.append(value)
        value = extra.profile()
        value["info_context"] = True
        cases.append(value)
        value = profile()
        value["person"]["compact_state_font"] = cv.Lambda("return nullptr;")
        cases.append(value)
        for value in cases:
            CORE.reset()
            with self.subTest(value=value), self.assertRaises(cv.Invalid):
                entity.ENTITY_CONTROL_SCHEMA(self.fixture.fixture.enabled(extra_controller=value))


if __name__ == "__main__":
    unittest.main(verbosity=2)
