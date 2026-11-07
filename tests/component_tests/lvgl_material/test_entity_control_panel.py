"""Robot admission, bounded interrupt successor and strict wire round trips."""

from pathlib import Path
import subprocess
import tempfile
import unittest

from host_light_control import SOURCE, arduino_json, compiler_command


class PanelOwnerHostTests(unittest.TestCase):
    def test_generation_and_strict_wire(self):
        sources = [
            "material_entity_control.cpp",
            "material_entity_control_protocol.cpp",
            "material_entity_control_codec.cpp",
            "material_number_domain.cpp",
            "material_entity_control_generic.cpp",
            "material_entity_control_generic_protocol.cpp",
            "material_entity_control_panel.cpp",
            "material_entity_control_panel_protocol.cpp",
        ]
        for siblings in (False, True):
            with (
                self.subTest(siblings=siblings),
                tempfile.TemporaryDirectory(prefix="panel-owner-") as temporary,
            ):
                root = Path(temporary)
                defines = root / "esphome/core/defines.h"
                defines.parent.mkdir(parents=True)
                defines.write_text("")
                harness = root / "panel.cpp"
                harness.write_text(
                    (Path(__file__).parent / "panel_owner_test.cpp").read_text(),
                    encoding="utf8",
                )
                executable = root / "panel.exe"
                optional = (
                    [
                        "-DUSE_LVGL_MATERIAL_CHOICE_SELECTOR",
                        "-DUSE_LVGL_MATERIAL_COVER_CONTROLS",
                        "-DUSE_LVGL_MATERIAL_THERMOSTAT",
                        "-DUSE_LVGL_MATERIAL_FAN_CONTROLS",
                        "-DUSE_LVGL_MATERIAL_ROBOT_CONTROLS",
                        str(SOURCE / "material_entity_control_robot.cpp"),
                        "-DUSE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS",
                        str(SOURCE / "material_entity_control_humidifier.cpp"),
                        str(SOURCE / "material_entity_control_fan.cpp"),
                        str(SOURCE / "material_entity_control_choice.cpp"),
                        str(SOURCE / "material_entity_control_cover.cpp"),
                    ]
                    if siblings
                    else []
                )
                command = [
                    *compiler_command(),
                    "-std=c++20",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-DARDUINOJSON_POOL_CAPACITY=128",
                    "-DUSE_LVGL_MATERIAL_NUMBER_STEPPER",
                    "-DUSE_LVGL_MATERIAL_PANEL_CONTROLS",
                    *optional,
                    "-I",
                    str(root),
                    "-I",
                    str(SOURCE),
                    "-I",
                    str(arduino_json()),
                    *[str(SOURCE / name) for name in sources],
                    str(harness),
                    "-o",
                    str(executable),
                ]
                result = subprocess.run(
                    command, capture_output=True, text=True, timeout=120, check=False
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                result = subprocess.run(
                    [str(executable)],
                    capture_output=True,
                    text=True,
                    timeout=20,
                    check=False,
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                print(result.stdout, end="")
