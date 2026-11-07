"""Text default-off opt-in, exact static map reachability and geometry gates."""

import asyncio
import copy
import json
from pathlib import Path
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
    p = json.loads(
        (Path(__file__).parent / "text_widget_fixture.json").read_text(encoding="utf8")
    )["profile"]
    del p["enabled"]
    return p


class TextSchemaTests(unittest.TestCase):
    def setUp(self):
        CORE.reset()
        self.base = number.NumberCodegenTests()

    def generate(self, value=None):
        for name, t in [
            ("ui", LvglComponent),
            ("nav", LvglNavigation),
            ("app", LvglApplication),
        ]:
            cg.new_Pvariable(ID(name, type=t, is_declaration=True))
        CORE.component_ids.add("control")
        conf = entity.ENTITY_CONTROL_SCHEMA(
            self.base.enabled(**({} if value is None else {"text_controller": value}))
        )
        declared = {str(k) for k, _ in iter_ids(conf) if k.id is not None}
        for k, _ in iter_ids(conf):
            if k.id is None:
                k.resolve(declared)
                declared.add(str(k))

        async def widgets(conf, name="id"):
            return [type("Widget", (), {"obj": cg.MockObj(str(conf[name]))})()]

        with patch.object(entity, "get_widgets", widgets):
            asyncio.run(entity.entity_controls_to_code([conf]))
        CORE.flush_tasks()
        return str(CORE.cpp_main_section), {d.name for d in CORE.defines}

    def test_absent_has_no_text_dependency(self):
        source, defines = self.generate()
        self.assertNotIn("set_text_profile", source)
        self.assertNotIn("USE_LVGL_MATERIAL_TEXT_CONTROLS", defines)

    def test_default_off_no_preview_container(self):
        source, defines = self.generate(profile())
        self.assertIn("set_text_enabled(false)", source)
        self.assertNotIn("preview_apps", source)
        self.assertIn("USE_LVGL_MATERIAL_TEXT_CONTROLS", defines)

    def test_opt_in_exact_bounded_maps_sorted_repertoire_and_callback(self):
        p = profile()
        p["enabled"] = True
        source, _ = self.generate(p)
        self.assertEqual(source.count("set_text_line("), 6)
        self.assertEqual(source.count("set_text_keyboard("), 3)
        self.assertEqual(source.count("set_text_map("), 18)
        self.assertEqual(source.count("set_text_control("), 11)
        self.assertIn("set_text_repertoire(control_text_repertoire, 113)", source)
        self.assertIn(
            "static const std::vector<esphome::lvgl::LvglApplication *> control_text_preview_apps{app}",
            source,
        )
        self.assertIn(
            "nav->try_prepare_application_snapshots(control_text_preview_apps)", source
        )
        points = ", ".join(str(ord(c)) for c in sorted(p["repertoire"]))
        self.assertIn("repertoire[] = {" + points + "}", source)

    def test_invalid_profiles_reject_without_guessing(self):
        bad = []
        for key in entity.TEXT_COMMON_WIDGET_FIELDS:
            p = profile()
            del p[key]
            bad.append(p)
        p = profile()
        p["controls"]["save"] = p["root"]
        bad.append(p)
        for value in ("aa", "a\n", ""):
            p = profile()
            p["repertoire"] = value
            bad.append(p)
        for key, value in [("value", "ab"), ("label", ""), ("value", "\u2603")]:
            p = profile()
            p["maps"][0][0][key] = value
            bad.append(p)
        p = profile()
        p["maps"][0].pop()
        bad.append(p)
        p = profile()
        p["repertoire"] += "\u2603"
        bad.append(p)
        for name in ("overview", "editor"):
            p = profile()
            p["layouts"][name]["visible_lines"] = 7
            bad.append(p)
            p = profile()
            p["layouts"][name]["visible_lines"] = 0
            bad.append(p)
            p = profile()
            p["layouts"][name]["surface_height"] = 1
            bad.append(p)
        for p in bad:
            CORE.reset()
            with self.subTest(p=p), self.assertRaises(cv.Invalid):
                entity.ENTITY_CONTROL_SCHEMA(
                    self.base.enabled(text_controller=copy.deepcopy(p))
                )
        with self.assertRaises(cv.Invalid):
            entity.ENTITY_CONTROL_SCHEMA(self.base.config(text_controller=profile()))


if __name__ == "__main__":
    unittest.main()
