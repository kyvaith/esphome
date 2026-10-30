"""Execute the actual NAV translation unit with narrow snapshot/input facades."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from host_light_control import FORK, LVGL_SOURCE, compiler_command

HERE = Path(__file__).resolve().parent
SOURCE = LVGL_SOURCE


class NavigationAdmissionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="navigation-admission-host-")
        cls.root = Path(cls.temp.name)
        for name in (
            "lvgl_navigation.h",
            "lvgl_navigation.cpp",
            "navigation_admission.h",
            "direct_region_generation.h",
        ):
            shutil.copy2(SOURCE / name, cls.root / name)
        for name in ("lvgl_gesture_router.h", "lvgl_gesture_router.cpp"):
            shutil.copy2(FORK / "esphome/components/lvgl" / name, cls.root / name)
        for name in (
            "lvgl_esphome.h",
            "lvgl_scroll_snapshot.h",
            "lvgl_snapshot_compositor.h",
            "esphome/core/automation.h",
            "esphome/core/helpers.h",
            "esphome/core/log.h",
            "esphome/core/hal.h",
        ):
            path = cls.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#include "navigation_test_support.h"\n')
        cls.executables = {}
        for enabled in (False, True):
            executable = cls.root / f"navigation-{enabled}.exe"
            command = [
                *compiler_command(),
                "-std=c++20",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-Wno-unused-parameter",
                "-DTEST_SCALAR",
                "-DTEST_REAL_NAV",
                "-DLV_USE_SNAPSHOT=1",
                "-DLV_USE_IMAGE=1",
                "-I",
                str(cls.root),
                "-I",
                str(HERE),
                str(cls.root / "lvgl_navigation.cpp"),
                str(cls.root / "lvgl_gesture_router.cpp"),
                str(HERE / "navigation_admission_test.cpp"),
                "-o",
                str(executable),
            ]
            if enabled:
                command.extend(
                    [
                        "-DUSE_LVGL_NAVIGATION_ADMISSION_GUARD",
                        "-DESPHOME_LVGL_NAVIGATION_ADMISSION_GUARD_COUNT=1",
                    ]
                )
            result = subprocess.run(
                command, capture_output=True, text=True, timeout=180, check=False
            )
            if result.returncode:
                raise AssertionError(result.stdout + result.stderr)
            cls.executables[enabled] = executable

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_default_off_original_lifecycle(self):
        result = subprocess.run(
            [str(self.executables[False]), "0"],
            capture_output=True,
            text=True,
            timeout=10,
            check=False,
        )
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertIn("navigation case 0 PASS", result.stdout)

    def test_actual_guarded_programmatic_gesture_and_capture_paths(self):
        for case in range(11):
            with self.subTest(case=case):
                result = subprocess.run(
                    [str(self.executables[True]), str(case)],
                    capture_output=True,
                    text=True,
                    timeout=10,
                    check=False,
                )
                self.assertEqual(0, result.returncode, result.stdout + result.stderr)
                self.assertIn(f"navigation case {case} PASS", result.stdout)


if __name__ == "__main__":
    unittest.main()
