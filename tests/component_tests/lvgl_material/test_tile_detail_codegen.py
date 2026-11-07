"""Light detail routing requires an explicit synchronous callback and bounded slot."""

import asyncio
import unittest
from unittest.mock import patch

from host_light_control import load_native_component

from esphome import codegen as cg, config_validation as cv
from esphome.config import iter_ids
from esphome.core import CORE, Lambda

module = load_native_component()


class DetailTileCodegenTests(unittest.TestCase):
    def tile(self, **extra):
        return {
            "id": "tiles",
            "tiles": [
                {
                    "slot": 2,
                    "widget": "tile",
                    "icon_label": "icon",
                    "title_label": "title",
                    "subtitle_label": "subtitle",
                }
            ],
            **extra,
        }

    def callback(self):
        return [
            {"then": [{"lambda": Lambda("(void)slot; (void)card_id; (void)revision;")}]}
        ]

    def test_default_pair_and_synchronous_admission(self) -> None:
        CORE.reset()
        self.assertFalse(
            module.TILE_SURFACE_SCHEMA(self.tile())["light_detail_controls"]
        )
        for fields in (
            {"light_detail_controls": True},
            {"on_entity_detail_control_press": self.callback()},
            {
                "light_detail_controls": True,
                "on_entity_detail_control_press": [{"then": [{"delay": "1s"}]}],
            },
        ):
            with self.subTest(fields=fields), self.assertRaises(cv.Invalid):
                module.TILE_SURFACE_SCHEMA(self.tile(**fields))
        valid = self.tile(
            light_detail_controls=True, on_entity_detail_control_press=self.callback()
        )
        self.assertFalse(module.TILE_SURFACE_SCHEMA(valid)["number_controls"])
        self.assertFalse(module.TILE_SURFACE_SCHEMA(valid)["control_press_enabled"])
        valid["tiles"][0]["slot"] = 32
        with self.assertRaises(cv.Invalid):
            module.TILE_SURFACE_SCHEMA(valid)

    def test_detail_callback_and_define_without_typed_route(self) -> None:
        CORE.reset()
        config = module.CONFIG_SCHEMA(
            {
                "tile_surfaces": [
                    self.tile(
                        light_detail_controls=True,
                        control_press_enabled=True,
                        on_entity_detail_control_press=self.callback(),
                    )
                ]
            }
        )
        declared = {str(id_) for id_, _ in iter_ids(config) if id_.id is not None}
        for id_, _ in iter_ids(config):
            if id_.id is None:
                id_.resolve(declared)
                declared.add(str(id_))
        CORE.component_ids.add("tiles")

        async def widgets(conf, name):
            return [type("Widget", (), {"obj": cg.MockObj(str(conf[name]))})()]

        with patch.object(module, "get_widgets", widgets):
            asyncio.run(module.to_code(config))
        CORE.flush_tasks()
        generated = str(CORE.cpp_main_section)
        self.assertIn("set_light_detail_controls_enabled(true)", generated)
        self.assertEqual(
            generated.count("add_on_entity_detail_control_press_callback"), 1
        )
        self.assertNotIn("add_on_entity_control_press_callback", generated)
        self.assertNotIn("control_kind", generated)
        defines = {define.name for define in CORE.defines}
        self.assertIn("USE_LVGL_MATERIAL_DETAIL_TILE_ROUTING", defines)
        self.assertNotIn("USE_LVGL_MATERIAL_ENTITY_CONTROL_TILE_ROUTING", defines)
