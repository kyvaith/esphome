"""Actual TileSurface schema/codegen for the optional availability contract."""

import asyncio
import unittest
from unittest.mock import patch

from host_light_control import load_native_component

from esphome import codegen as cg, config_validation as cv
from esphome.components.lvgl.types import lv_obj_t
from esphome.config import Config, IDPassValidationStep
from esphome.core import CORE, ID

module = load_native_component()
FEATURE = "USE_LVGL_MATERIAL_TILE_ACTION_AVAILABILITY"
MISSING = object()


class TileAvailabilityCodegenTests(unittest.TestCase):
    def surface(self, *, enabled=MISSING, slot=2, name="tiles"):
        value = {
            "id": name,
            "tiles": [
                {
                    "slot": slot,
                    "widget": name + "_root",
                    "icon_label": name + "_icon",
                    "title_label": name + "_title",
                    "subtitle_label": name + "_subtitle",
                }
            ],
        }
        if enabled is not MISSING:
            value["action_availability"] = enabled
        return value

    def generate(self, surfaces):
        CORE.reset()
        material = module.CONFIG_SCHEMA({"tile_surfaces": surfaces})
        config = Config()
        config["lvgl_material"] = material
        widgets = {
            str(binding[name])
            for surface in material["tile_surfaces"]
            for binding in surface["tiles"]
            for name in ("widget", "icon_label", "title_label", "subtitle_label")
        }
        config["declarations"] = [
            {"id": ID(name, type=lv_obj_t, is_declaration=True)}
            for name in sorted(widgets)
        ]
        IDPassValidationStep().run(config)
        self.assertEqual([], config.errors)
        for name in sorted(widgets):
            cg.new_Pvariable(ID(name, type=lv_obj_t, is_declaration=True))
        CORE.component_ids.update(
            str(surface["id"]) for surface in material["tile_surfaces"]
        )

        async def widgets_ready(conf, name):
            return [type("Widget", (), {"obj": cg.MockObj(str(conf[name]))})()]

        async def actual_codegen():
            await asyncio.wait_for(module.to_code(material), timeout=1.0)

        with patch.object(module, "get_widgets", widgets_ready):
            asyncio.run(actual_codegen())
        CORE.flush_tasks()
        return str(CORE.cpp_main_section), {define.name for define in CORE.defines}

    def test_option_defaults_off_and_preserves_legacy_high_slots(self):
        CORE.reset()
        for enabled in (MISSING, False):
            with self.subTest(enabled=enabled):
                conf = module.TILE_SURFACE_SCHEMA(
                    self.surface(enabled=enabled, slot=255)
                )
                self.assertIs(conf["action_availability"], False)
                self.assertEqual(conf["tiles"][0]["slot"], 255)

    def test_opt_in_requires_bounded_snapshot_mask_slots(self):
        CORE.reset()
        self.assertTrue(
            module.TILE_SURFACE_SCHEMA(self.surface(enabled=True, slot=31))[
                "action_availability"
            ]
        )
        for slot in (32, 255):
            with self.subTest(slot=slot), self.assertRaises(cv.Invalid):
                module.TILE_SURFACE_SCHEMA(self.surface(enabled=True, slot=slot))

    def test_option_validation_and_existing_unique_slot_invariant(self):
        CORE.reset()
        for value in ([], {}, "not-a-boolean"):
            with self.subTest(value=value), self.assertRaises(cv.Invalid):
                module.TILE_SURFACE_SCHEMA(self.surface(enabled=value))
        value = self.surface(enabled=True)
        value["tiles"].append(dict(value["tiles"][0]))
        with self.assertRaises(cv.Invalid):
            module.TILE_SURFACE_SCHEMA(value)

    def test_default_and_explicit_false_emit_no_new_define_or_setter(self):
        default, default_defines = self.generate([self.surface()])
        explicit, explicit_defines = self.generate([self.surface(enabled=False)])
        self.assertEqual(default, explicit)
        self.assertEqual(default_defines, explicit_defines)
        self.assertNotIn(FEATURE, default_defines)
        self.assertNotIn("set_action_availability_enabled", default)

    def test_true_emits_exact_feature_and_opt_in_setter(self):
        generated, defines = self.generate([self.surface(enabled=True)])
        self.assertIn(FEATURE, defines)
        self.assertEqual(
            generated.count("tiles->set_action_availability_enabled(true)"), 1
        )
        self.assertNotIn("set_action_availability_enabled(false)", generated)
        self.assertIn(
            "tiles->add_tile(2, tiles_root, tiles_icon, tiles_title, tiles_subtitle, false)",
            generated,
        )

    def test_mixed_instances_only_configure_explicitly_enabled_surface(self):
        generated, defines = self.generate(
            [
                self.surface(enabled=True, name="opted_in"),
                self.surface(slot=255, name="legacy"),
            ]
        )
        self.assertIn(FEATURE, defines)
        self.assertEqual(generated.count("set_action_availability_enabled"), 1)
        self.assertIn("opted_in->set_action_availability_enabled(true)", generated)
        self.assertNotIn("legacy->set_action_availability_enabled", generated)


if __name__ == "__main__":
    unittest.main()
