"""Typed active storage preserves strict wire parsing and atomic replacement."""

from pathlib import Path
import subprocess
import tempfile
import unittest

from host_light_control import SOURCE, arduino_json, compiler_command


class StorageCodecTests(unittest.TestCase):
    def test_ordered_family_replacement_and_wrong_tag_rejection(self):
        self.assertIn("transitions=100", self._run_harness("storage_codec_test.cpp"))

    def test_pending_and_revoked_storage_across_all_family_pairs(self):
        self.assertIn(
            "PASS 100 ordered family transitions",
            self._run_harness("storage_owner_test.cpp"),
        )

    def _run_harness(self, harness):
        sources = [
            "material_entity_control.cpp",
            "material_entity_control_protocol.cpp",
            "material_entity_control_codec.cpp",
            "material_number_domain.cpp",
            "material_entity_control_generic.cpp",
            "material_entity_control_generic_protocol.cpp",
            "material_entity_control_choice.cpp",
            "material_entity_control_cover.cpp",
            "material_entity_control_fan.cpp",
            "material_entity_control_humidifier.cpp",
            "material_entity_control_robot.cpp",
            "material_entity_control_panel.cpp",
            "material_entity_control_panel_protocol.cpp",
            "material_entity_control_detail.cpp",
            "material_entity_control_detail_protocol.cpp",
        ]
        features = [
            "NUMBER_STEPPER",
            "THERMOSTAT",
            "CHOICE_SELECTOR",
            "COVER_CONTROLS",
            "COVER_PRIORITY_STOP",
            "COVER_PANEL",
            "FAN_CONTROLS",
            "HUMIDIFIER_CONTROLS",
            "ROBOT_CONTROLS",
            "PANEL_CONTROLS",
            "DETAIL_CONTROLS",
        ]
        with tempfile.TemporaryDirectory(prefix="typed-storage-") as temporary:
            root = Path(temporary)
            defines = root / "esphome/core/defines.h"
            defines.parent.mkdir(parents=True)
            defines.write_text("")
            executable = root / "storage.exe"
            command = [
                *compiler_command(),
                "-std=c++20",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-DARDUINOJSON_POOL_CAPACITY=128",
                *[f"-DUSE_LVGL_MATERIAL_{feature}" for feature in features],
                "-I",
                str(root),
                "-I",
                str(SOURCE),
                "-I",
                str(arduino_json()),
                *[str(SOURCE / name) for name in sources],
                str(Path(__file__).with_name(harness)),
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
                timeout=25,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout, end="")
            return result.stdout

    def test_conditional_alternatives_without_exceptions(self):
        number = ["NUMBER_STEPPER"]
        matrix = [
            [],
            number,
            [*number, "THERMOSTAT"],
            [*number, "CHOICE_SELECTOR"],
            [*number, "COVER_CONTROLS"],
            [*number, "COVER_CONTROLS", "COVER_PRIORITY_STOP", "COVER_PANEL"],
            [*number, "FAN_CONTROLS"],
            [*number, "HUMIDIFIER_CONTROLS"],
            [*number, "ROBOT_CONTROLS"],
            [*number, "PANEL_CONTROLS"],
            [*number, "THERMOSTAT", "PANEL_CONTROLS", "DETAIL_CONTROLS"],
            [*number, "CHOICE_SELECTOR", "COVER_CONTROLS"],
            [
                *number,
                "THERMOSTAT",
                "CHOICE_SELECTOR",
                "COVER_CONTROLS",
                "COVER_PRIORITY_STOP",
                "COVER_PANEL",
                "FAN_CONTROLS",
                "HUMIDIFIER_CONTROLS",
                "ROBOT_CONTROLS",
                "PANEL_CONTROLS",
                "DETAIL_CONTROLS",
            ],
        ]
        with tempfile.TemporaryDirectory(prefix="typed-storage-values-") as temporary:
            root = Path(temporary)
            defines = root / "esphome/core/defines.h"
            defines.parent.mkdir(parents=True)
            defines.write_text("")
            for index, features in enumerate(matrix):
                with self.subTest(features=features):
                    executable = root / f"storage-{index}.exe"
                    command = [
                        *compiler_command(),
                        "-std=c++20",
                        "-UNDEBUG",
                        "-Wall",
                        "-Wextra",
                        "-Werror",
                        "-fno-exceptions",
                        "-fno-rtti",
                        *[f"-DUSE_LVGL_MATERIAL_{feature}" for feature in features],
                        "-I",
                        str(root),
                        "-I",
                        str(SOURCE),
                        str(Path(__file__).with_name("storage_value_test.cpp")),
                        "-o",
                        str(executable),
                    ]
                    result = subprocess.run(
                        command,
                        capture_output=True,
                        text=True,
                        timeout=120,
                        check=False,
                    )
                    self.assertEqual(
                        result.returncode, 0, result.stdout + result.stderr
                    )
                    result = subprocess.run(
                        [str(executable)],
                        capture_output=True,
                        text=True,
                        timeout=20,
                        check=False,
                    )
                    self.assertEqual(
                        result.returncode, 0, result.stdout + result.stderr
                    )
