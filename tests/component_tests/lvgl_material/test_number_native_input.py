"""Real public LVGL input retirement for Number; no SDK/device/HA dependency."""

from __future__ import annotations

import concurrent.futures
import hashlib
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest

from host_light_control import FORK, LVGL_SOURCE, SOURCE, arduino_json, compiler_command

HERE = Path(__file__).resolve().parent
UNITS = (
    "material_entity_control.cpp",
    "material_entity_control_protocol.cpp",
    "material_entity_control_codec.cpp",
    "material_number_domain.cpp",
    "material_entity_control_generic.cpp",
    "material_entity_control_generic_protocol.cpp",
    "material_entity_control_component.cpp",
    "material_entity_control_number_component.cpp",
)


class NumberNativeInputTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (configured := os.environ.get("ESPHOME_TEST_LVGL")):
            raise unittest.SkipTest(
                "ESPHOME_TEST_LVGL must identify public LVGL9.6 sources"
            )
        cls.library = Path(configured).resolve()
        if not (cls.library / "lv_conf_template.h").is_file():
            raise AssertionError(
                "ESPHOME_TEST_LVGL must contain lv_conf_template.h and src"
            )
        cls.temporary = tempfile.TemporaryDirectory(prefix="number-native-lvgl-")
        cls.root = Path(cls.temporary.name)
        for name in (
            "components/lvgl/lvgl_esphome.h",
            "components/lvgl/lvgl_navigation.h",
            "components/esp32/task_utils.h",
            "components/api/api_server.h",
            "core/automation.h",
            "core/component.h",
            "core/helpers.h",
            "core/string_ref.h",
            "core/application.h",
            "core/hal.h",
            "core/log.h",
            "core/defines.h",
            "freertos/FreeRTOS.h",
            "freertos/task.h",
            "esp_heap_caps.h",
        ):
            path = cls.root / (
                name
                if name.startswith(("freertos/", "esp_heap"))
                else "esphome/" + name
            )
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#include "real_lvgl_test_support.h"\n', encoding="utf-8")
        config = (cls.library / "lv_conf_template.h").read_text(encoding="utf-8")
        config = config.replace(
            '#if 0 /* Set this to "1" to enable content */',
            "#if 1 /* Real native input host configuration */",
            1,
        )
        if "#if 1 /* Real native input host configuration */" not in config:
            raise AssertionError("Unexpected LVGL template activation guard")
        config = re.sub(
            r"#define LV_MEM_SIZE .*", "#define LV_MEM_SIZE (8 * 1024 * 1024U)", config
        )
        for option in (
            "LV_USE_LOG",
            "LV_USE_SYSMON",
            "LV_USE_ANIMIMG",
            "LV_THEME_DEFAULT_GROW",
        ):
            config = re.sub(
                r"#define " + option + r" .*", "#define " + option + " 0", config
            )
        for directory in (cls.library / "src/widgets").iterdir():
            if directory.is_dir() and directory.name not in {
                "button",
                "label",
                "image",
            }:
                config = re.sub(
                    r"(#define LV_USE_" + directory.name.upper() + r" )1\b",
                    r"\g<1>0",
                    config,
                )
        (cls.root / "lv_conf.h").write_text(config, encoding="utf-8")
        cls.includes = []
        for directory in (
            cls.root,
            HERE,
            SOURCE,
            LVGL_SOURCE,
            FORK,
            cls.library,
            cls.library / "include/lvgl",
            cls.library / "src",
            arduino_json(),
        ):
            cls.includes += ["-I", str(directory)]
        sources = [cls.library / "src/lv_init.c"]
        for directory in (
            "core",
            "display",
            "draw",
            "font",
            "fs",
            "image",
            "indev",
            "layouts",
            "logging",
            "misc",
            "osal",
            "stdlib",
            "themes",
            "tick",
            "widgets",
        ):
            sources.extend(sorted((cls.library / "src" / directory).rglob("*.c")))
        sources.extend(sorted((cls.library / "src/libs/bin_decoder").rglob("*.c")))
        cc = [
            os.environ.get("ESPHOME_TEST_CC", "cc"),
            *shlex.split(os.environ.get("ESPHOME_TEST_CC_ARGS", "")),
        ]

        def build_c(item):
            index, path = item
            target = cls.root / f"lvgl-{index:04}.o"
            cls.checked(
                [
                    *cc,
                    "-std=c11",
                    "-O1",
                    "-DLV_CONF_INCLUDE_SIMPLE",
                    *cls.includes,
                    "-c",
                    str(path),
                    "-o",
                    str(target),
                ]
            )
            return target

        with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
            cls.objects = list(pool.map(build_c, enumerate(sources)))
        cls.pins = {
            path: hashlib.sha256(path.read_bytes()).hexdigest()
            for path in [
                *[SOURCE / name for name in UNITS],
                *SOURCE.glob("material_*.h"),
                HERE / "number_native_input_test.cpp",
            ]
        }
        cls.executable = cls.build_control()

    @staticmethod
    def checked(command):
        result = subprocess.run(
            [str(x) for x in command],
            capture_output=True,
            text=True,
            encoding="utf-8",
            timeout=180,
            check=False,
        )
        if result.returncode:
            raise AssertionError(f"{command!r}\n{result.stdout}{result.stderr}")
        return result

    @classmethod
    def build_control(cls):
        flags = [
            "-std=c++20",
            "-UNDEBUG",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wno-deprecated-declarations",
            "-DUSE_LVGL_MATERIAL_NUMBER_STEPPER",
            "-DUSE_LVGL_NAVIGATION_ADMISSION_GUARD",
            "-DARDUINOJSON_POOL_CAPACITY=128",
            "-DLV_CONF_INCLUDE_SIMPLE",
            "-DLV_COLOR_DEPTH=32",
        ]
        objects = []
        for path in [
            *[SOURCE / unit for unit in UNITS],
            HERE / "number_native_input_test.cpp",
        ]:
            target = cls.root / ("number-" + path.name + ".o")
            cls.checked(
                [*compiler_command(), *flags, *cls.includes, "-c", path, "-o", target]
            )
            objects.append(target)
        executable = cls.root / ("number-native" + (".exe" if os.name == "nt" else ""))
        arguments = [*map(str, [*objects, *cls.objects]), "-o", str(executable)]
        response = cls.root / "number-native-link.rsp"
        # Public LVGL has hundreds of objects; keep Windows/space paths bounded.
        response.write_text(
            subprocess.list2cmdline(arguments)
            if os.name == "nt"
            else shlex.join(arguments),
            encoding="utf-8",
        )
        cls.checked([*compiler_command(), "@" + str(response)])
        return executable

    @classmethod
    def tearDownClass(cls):
        try:
            assert all(
                hashlib.sha256(p.read_bytes()).hexdigest() == digest
                for p, digest in cls.pins.items()
            )
        finally:
            cls.temporary.cleanup()

    def cases(self, names):
        for name in names:
            with self.subTest(case=name):
                self.assertIn(
                    f"RESULT {name} commands=1 captured=0 pass=1",
                    self.checked([self.executable, name]).stdout,
                )

    def test_disabled_held_button_and_first_allowed_fresh_contact(self):
        self.cases(("maximum_opposite", "minimum_opposite", "unknown_recovered"))

    def test_aba_and_reenabled_contact_wait_until_real_release(self):
        self.cases(("aba_same_index", "reenable_held"))

    def test_renewal_title_only_and_ignored_stale_preserve_press(self):
        self.cases(("renewal", "title_only", "stale_ignored"))

    def test_rejected_domain_and_generation_changes_retire_native_press(self):
        self.cases(("domain_revoked", "same_generation_revoked"))
