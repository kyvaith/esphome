"""Real root/schema/ID/codegen regression; no native SDK or runtime operations."""

import asyncio
import unittest
from unittest.mock import patch

import test_direct_scalar as scalar_tests
import test_entity_control as component_tests


class EntityControlComposedCodegenTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = component_tests.load_native_component()
        cls.entity = __import__(
            "esphome.components.lvgl_material.entity_control", fromlist=["open_to_code"]
        )
        cls.scalar = __import__(
            "esphome.components.lvgl_material.direct_scalar",
            fromlist=["direct_scalars_to_code"],
        )

    def run_codegen(self, *, enabled=False, legacy_only=False):
        from esphome import codegen as cg
        from esphome.components.lvgl.lvcode import LvglComponent
        from esphome.components.lvgl.types import (
            DirectSceneController,
            LvglApplication,
            LvglNavigation,
            lv_obj_t,
        )
        from esphome.config import Config, IDPassValidationStep
        from esphome.core import CORE, ID, Lambda

        CORE.reset()
        scalar_tests.ScalarSchemaTests.scalar = self.scalar
        tile = {
            "id": "tiles",
            "tiles": [
                {
                    "slot": 2,
                    "widget": "tile_root",
                    "icon_label": "tile_icon",
                    "title_label": "tile_title",
                    "subtitle_label": "tile_subtitle",
                }
            ],
            "on_press": [{"lambda": Lambda("(void) slot;")}],
            "on_context_press": [
                {"lambda": Lambda("(void) slot; (void) card_id; (void) revision;")}
            ],
        }
        if not legacy_only:
            tile.update(
                control_press_enabled=enabled,
                on_control_press=[
                    {
                        "then": [
                            {
                                "lvgl_material.entity_control.open": {
                                    "id": "control",
                                    "slot": Lambda("return slot;"),
                                    "card_id": Lambda("return std::string(card_id);"),
                                    "revision": Lambda("return revision;"),
                                }
                            }
                        ]
                    },
                    {
                        "then": [
                            {
                                "lambda": Lambda(
                                    "const auto second_control_callback_slot = slot; "
                                    "(void) second_control_callback_slot;"
                                )
                            }
                        ]
                    },
                ],
            )
        raw = {
            "tile_surfaces": [tile],
            "direct_volume_overlays": [
                {
                    "id": "overlay",
                    "lvgl_id": "ui",
                    "arc": "arc",
                    "knob": "knob",
                    "label": "volume_label",
                    "scene_controllers": (
                        ["legacy", "legacy2"]
                        if legacy_only
                        else ["scalar", "legacy", "legacy2"]
                    ),
                }
            ],
        }
        if not legacy_only:
            raw.update(
                entity_controls=[
                    component_tests.ComponentSchemaTests().config(
                        enabled=enabled,
                        **(
                            {"on_message": [{"lambda": Lambda("(void) payload;")}]}
                            if enabled
                            else {}
                        ),
                    )
                ],
                direct_scalar_controls=[
                    scalar_tests.ScalarSchemaTests().config(
                        enabled=enabled, value_label="candidate_value"
                    )
                ],
            )
        material = self.root.CONFIG_SCHEMA(raw)
        config = Config()
        config["lvgl_material"] = material
        declarations = [
            ("ui", LvglComponent),
            ("nav", LvglNavigation),
            ("app", LvglApplication),
            ("legacy", DirectSceneController),
            ("legacy2", DirectSceneController),
        ]
        widgets = {
            "tile_root",
            "tile_icon",
            "tile_title",
            "tile_subtitle",
            "arc",
            "knob",
            "volume_label",
            "candidate_value",
        }
        if not legacy_only:
            for group, fields in (
                (
                    "entity_controls",
                    (
                        "root",
                        "hit",
                        "title_label",
                        "value_label",
                        "unknown_value_label",
                        "on_button",
                        "off_button",
                    ),
                ),
                ("direct_scalar_controls", self.scalar.WIDGETS),
            ):
                widgets.update(str(material[group][0][name]) for name in fields)
        declarations += [(name, lv_obj_t) for name in sorted(widgets)]
        config["declarations"] = [
            {"id": ID(name, type=type_, is_declaration=True)}
            for name, type_ in declarations
        ]
        IDPassValidationStep().run(config)
        self.assertEqual([], config.errors)
        for name, type_ in declarations:
            cg.new_Pvariable(ID(name, type=type_, is_declaration=True))
        CORE.component_ids.update(("tiles", "overlay"))
        if not legacy_only:
            CORE.component_ids.update(("control", "scalar"))

        async def widgets_ready(conf, name):
            return [type("Widget", (), {"obj": cg.MockObj(str(conf[name]))})()]

        to_code = self.root.to_code

        async def composed():
            # All ID waits and both actual subcomponent codegens remain real.
            await asyncio.wait_for(to_code(material), timeout=1.0)

        with (
            patch.object(self.root, "get_widgets", widgets_ready),
            patch.object(self.entity, "get_widgets", widgets_ready),
            patch.object(self.scalar, "get_widgets", widgets_ready),
        ):
            asyncio.run(composed())
        CORE.flush_tasks()
        return str(CORE.cpp_main_section), tuple(sorted(map(str, CORE.defines)))

    def assert_combined(self, enabled):
        generated, _ = self.run_codegen(enabled=enabled)
        scalar = generated.index("MaterialDirectScalarControl(ui, control, nav, app)")
        callback = generated.index("tiles->add_on_control_press_callback")
        self.assertLess(scalar, callback)
        self.assertIn("MaterialEntityControlOpenAction", generated)
        self.assertLess(
            generated.index("tiles->add_on_press_callback"),
            generated.index("tiles->add_on_context_press_callback"),
        )
        self.assertLess(
            callback, generated.index("const auto second_control_callback_slot")
        )
        self.assertEqual(2, generated.count("tiles->add_on_control_press_callback"))
        positions = [
            generated.index(f"overlay->add_scene_controller({name})")
            for name in ("scalar", "legacy", "legacy2")
        ]
        self.assertEqual(sorted(positions), positions)
        for name in ("control", "scalar"):
            self.assertIn(
                f"{name}->set_initial_enabled({str(enabled).lower()})", generated
            )

    def test_enabled_combined_control_callback_and_scalar_controller_resolve(self):
        self.assert_combined(True)

    def test_disabled_combined_control_callback_and_scalar_controller_resolve(self):
        self.assert_combined(False)

    def test_legacy_missing_feature_lists_keep_original_callbacks_and_order(self):
        after = self.run_codegen(legacy_only=True)
        self.assertLess(
            after[0].index("tiles->add_on_press_callback"),
            after[0].index("tiles->add_on_context_press_callback"),
        )
        self.assertLess(
            after[0].index("overlay->add_scene_controller(legacy)"),
            after[0].index("overlay->add_scene_controller(legacy2)"),
        )
        self.assertFalse(
            any(
                "MATERIAL_DIRECT_SCALAR" in item or "EXPECTED_GENERATION" in item
                for item in after[1]
            )
        )
        self.assertNotIn("MaterialDirectScalarControl", after[0])
        self.assertNotIn("MaterialEntityControlComponent", after[0])
        self.assertNotIn("add_on_control_press_callback", after[0])


if __name__ == "__main__":
    unittest.main()
