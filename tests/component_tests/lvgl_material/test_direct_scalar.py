"""Actual isolated scalar worker/raster with deterministic host ownership facade."""

import asyncio
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from host_light_control import (
    FORK,
    LVGL_SOURCE,
    SOURCE,
    arduino_json,
    compiler_command,
    load_native_component,
)

HERE = Path(__file__).resolve().parent


class ScalarHostTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="scalar-component-host-")
        root = Path(cls.temp.name)
        for name in (
            "components/lvgl/lvgl_esphome.h",
            "components/lvgl/lvgl_navigation.h",
            "components/lvgl/direct_scene_controller.h",
            "components/esp32/task_utils.h",
            "core/automation.h",
            "core/component.h",
            "core/helpers.h",
            "core/string_ref.h",
            "core/application.h",
            "core/hal.h",
            "core/log.h",
            "core/defines.h",
        ):
            path = root / "esphome" / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#include "scalar_test_support.h"\n')
        # Keep the real reusable scene interface; no opaque full-screen lease is used.
        scene = FORK / "esphome/components/lvgl/direct_scene_controller.h"
        (root / "esphome/components/lvgl/direct_scene_controller.h").write_bytes(
            scene.read_bytes()
        )
        for name in (
            "freertos/FreeRTOS.h",
            "freertos/task.h",
            "esp_heap_caps.h",
            "misc/cache/instance/lv_image_cache.h",
        ):
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#include "scalar_test_support.h"\n')
        fixture = (
            (HERE / "entity_component_test.cpp")
            .read_text(encoding="utf-8")
            .split("int main(", 1)[0]
        )
        (root / "component_fixture.h").write_text(fixture)
        cls.executable = root / "scalar.exe"
        command = [
            *compiler_command(),
            "-std=c++20",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-DTEST_SCALAR",
            "-DUSE_ESP32",
            "-DUSE_MIPI_DSI",
            "-DUSE_LVGL_PPA",
            "-DLV_COLOR_DEPTH=32",
            "-DUSE_LVGL_MATERIAL_DIRECT_SCALAR_CONTROL",
            "-DUSE_LVGL_NAVIGATION_ADMISSION_GUARD",
            "-DUSE_LVGL_DIRECT_REGION_EXPECTED_GENERATION",
            "-DARDUINOJSON_POOL_CAPACITY=128",
            "-I",
            str(root),
            "-I",
            str(HERE),
            "-I",
            str(SOURCE),
            "-I",
            str(LVGL_SOURCE),
            "-I",
            str(arduino_json()),
            *[
                str(SOURCE / name)
                for name in (
                    "material_entity_control.cpp",
                    "material_entity_control_protocol.cpp",
                    "material_entity_control_codec.cpp",
                    "material_entity_control_component.cpp",
                    "material_scalar_raster.cpp",
                    "material_direct_scalar_control.cpp",
                )
            ],
            str(HERE / "scalar_component_test.cpp"),
            "-o",
            str(cls.executable),
        ]
        compiled = subprocess.run(
            command, capture_output=True, text=True, timeout=180, check=False
        )
        if compiled.returncode:
            raise AssertionError(compiled.stdout + compiled.stderr)
        cls.command = command
        cls.build_root = root

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_actual_worker_and_handoff_failures(self):
        for case in range(40):
            with self.subTest(case=case):
                run = subprocess.run(
                    [str(self.executable), str(case)],
                    capture_output=True,
                    text=True,
                    timeout=10,
                    check=False,
                )
                self.assertEqual(0, run.returncode, run.stdout + run.stderr)
                self.assertIn(f"scalar case {case} PASS", run.stdout)

    def test_missing_acceleration_keeps_brightness_closed_and_native_power_explicit(
        self,
    ):
        cpp = self.build_root / "without-acceleration.cpp"
        cpp.write_text(r"""
#include "scalar_test_support.h"
#include "component_fixture.h"
#include "material_direct_scalar_control.h"
using namespace esphome::lvgl_material;
int main(){
  Fixture entity;esphome::lvgl::LvglNavigation nav;esphome::lvgl::LvglApplication app;nav.active=&app;
  lv_obj_t region,track,bubble,value,minimum,maximum;
  MaterialDirectScalarControl scalar(&entity.lvgl,&entity.component,&nav,&app);
  scalar.set_widgets(&region,&track,&bubble,&value,&minimum,&maximum);scalar.set_active_color(0x3498FF);
  scalar.set_initial_enabled(true);scalar.setup();assert(scalar.is_failed()&&test_allocations==0);
  entity.grant();const auto generation=entity.component.get_selection_generation();
  assert(entity.component.set_view_ready(generation,false));assert(!scalar.resume(generation));
  assert(entity.component.owner().is_view_ready()&&!entity.component.owner().is_scalar_ready());
  assert(scalar.suspend_for_direct_overlay());
  assert(!scalar.resume(generation)&&!entity.component.owner().is_view_ready());
  scalar.resume_after_direct_overlay();assert(entity.component.owner().is_view_ready());
  test_event(entity.off,LV_EVENT_PRESSED,&entity.pointer);test_event(entity.off,LV_EVENT_RELEASED,&entity.pointer);
  assert(entity.commands()==1);
}
""")
        executable = self.build_root / "without-acceleration.exe"
        command = [
            item
            for item in self.command
            if item not in ("-DUSE_ESP32", "-DUSE_MIPI_DSI", "-DUSE_LVGL_PPA")
        ]
        command[command.index(str(HERE / "scalar_component_test.cpp"))] = str(cpp)
        command[command.index(str(self.executable))] = str(executable)
        result = subprocess.run(
            command, capture_output=True, text=True, timeout=180, check=False
        )
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        result = subprocess.run(
            [str(executable)], capture_output=True, text=True, timeout=10, check=False
        )
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)


class ScalarSchemaTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = load_native_component()
        cls.scalar = __import__(
            "esphome.components.lvgl_material.direct_scalar",
            fromlist=["DIRECT_SCALAR_SCHEMA"],
        )

    def config(self, **extra):
        return {
            "id": "scalar",
            "lvgl_id": "ui",
            "entity_control_id": "control",
            "navigation_id": "nav",
            "application_id": "app",
            "active_color": 0x3498FF,
            **{name: name for name in self.scalar.WIDGETS},
            **extra,
        }

    def test_default_off_distinct_widgets_and_required_color(self):
        from esphome import config_validation as cv
        from esphome.components.lvgl.types import DirectSceneController

        config = self.scalar.DIRECT_SCALAR_SCHEMA(self.config())
        self.assertIs(False, config["enabled"])
        self.assertTrue(config["id"].type.inherits_from(DirectSceneController))
        for invalid in (
            self.config(track="region"),
            {k: v for k, v in self.config().items() if k != "active_color"},
        ):
            with self.assertRaises(cv.Invalid):
                self.scalar.DIRECT_SCALAR_SCHEMA(invalid)

    def test_scalar_requires_matching_control_and_lvgl(self):
        from test_entity_control import ComponentSchemaTests

        from esphome import config_validation as cv

        control = ComponentSchemaTests().config()
        self.root.CONFIG_SCHEMA(
            {"entity_controls": [control], "direct_scalar_controls": [self.config()]}
        )
        for config in (
            {"direct_scalar_controls": [self.config()]},
            {
                "entity_controls": [control],
                "direct_scalar_controls": [self.config(lvgl_id="other")],
            },
        ):
            with self.assertRaises(cv.Invalid):
                self.root.CONFIG_SCHEMA(config)

    def test_scalar_resolves_as_existing_overlay_scene_controller(self):
        from esphome.components.lvgl.lvcode import LvglComponent
        from esphome.components.lvgl.types import lv_obj_t
        from esphome.config import Config, IDPassValidationStep
        from esphome.core import CORE, ID

        CORE.reset()
        config = Config()
        config["scalar"] = {
            "id": ID(
                "scalar",
                type=self.scalar.MaterialDirectScalarControl,
                is_declaration=True,
            )
        }
        config["widgets"] = [
            {"id": ID(name, type=type_, is_declaration=True)}
            for name, type_ in (
                ("ui", LvglComponent),
                ("arc", lv_obj_t),
                ("knob", lv_obj_t),
                ("label", lv_obj_t),
            )
        ]
        config["overlay"] = self.root.DIRECT_VOLUME_OVERLAY_SCHEMA(
            {
                "id": "overlay",
                "lvgl_id": "ui",
                "arc": "arc",
                "knob": "knob",
                "label": "label",
                "scene_controllers": ["scalar"],
            }
        )
        IDPassValidationStep().run(config)
        self.assertEqual([], config.errors)

    def test_actual_codegen_fixed_guard_count_and_provider(self):
        from esphome import codegen as cg
        from esphome.components.lvgl.lvcode import LvglComponent
        from esphome.components.lvgl.types import LvglApplication, LvglNavigation
        from esphome.components.lvgl_material.entity_control import (
            MaterialEntityControlComponent,
        )
        from esphome.core import CORE, ID, Lambda
        from esphome.cpp_helpers import get_slot_count

        CORE.reset()
        for name, type_ in (
            ("ui", LvglComponent),
            ("control", MaterialEntityControlComponent),
            ("nav", LvglNavigation),
            ("app", LvglApplication),
        ):
            cg.new_Pvariable(ID(name, type=type_, is_declaration=True))
        CORE.component_ids.add("scalar")
        config = self.scalar.DIRECT_SCALAR_SCHEMA(
            self.config(enabled=True, active_color=Lambda("return 0x3498FF;"))
        )

        async def widgets(conf, name):
            return [type("Widget", (), {"obj": cg.MockObj(str(conf[name]))})()]

        with patch.object(self.scalar, "get_widgets", widgets):
            asyncio.run(self.scalar.direct_scalars_to_code([config]))
        self.assertEqual(
            1, get_slot_count("ESPHOME_LVGL_NAVIGATION_ADMISSION_GUARD_COUNT")
        )
        CORE.flush_tasks()
        generated = str(CORE.cpp_main_section)
        self.assertIn("MaterialDirectScalarControl(ui, control, nav, app)", generated)
        self.assertIn("set_active_color_provider", generated)
        self.assertIn("set_initial_enabled(true)", generated)
        self.assertIn(
            "set_widgets(region, track, bubble, value_label, minimum_label, maximum_label)",
            generated,
        )


if __name__ == "__main__":
    unittest.main()
