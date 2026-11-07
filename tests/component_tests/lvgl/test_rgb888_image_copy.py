"""Execute the actual opt-in PPA branch with real LVGL metadata and a DMA facade."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
SOURCE = HERE.parents[2] / "esphome/components/lvgl/ppa/lv_draw_ppa_img.c"


class Rgb888ImageCopyTests(unittest.TestCase):
    def test_clipped_copy_cache_lifecycle_and_fallbacks(self):
        compiler = os.environ.get("ESPHOME_TEST_CXX")
        library = os.environ.get("ESPHOME_TEST_LVGL")
        if not compiler or not library:
            self.skipTest("Set ESPHOME_TEST_CXX and ESPHOME_TEST_LVGL")
        text = SOURCE.read_text(encoding="utf8")
        begin = text.index("#ifdef USE_LVGL_RGB888_DMA2D_IMAGE_COPY")
        end = text.index("#endif", begin) + len("#endif")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            defines = root / "esphome/core/defines.h"
            defines.parent.mkdir(parents=True)
            defines.write_text("#define USE_LVGL_RGB888_DMA2D_IMAGE_COPY\n")
            config = (Path(library) / "lv_conf_template.h").read_text()
            config = config.replace(
                '#if 0 /* Set this to "1" to enable content */', "#if 1", 1
            )
            (root / "lv_conf.h").write_text(config)
            (root / "rgb888_copy_branch.inc").write_text(text[begin:end])
            binary = root / "copy.exe"
            command = [
                compiler,
                *shlex.split(os.environ.get("ESPHOME_TEST_CXX_ARGS", "")),
                "-std=c++20",
                "-UNDEBUG",
                "-I" + str(root),
                "-I" + library,
                str(HERE / "rgb888_image_copy_test.cpp"),
                "-o",
                str(binary),
            ]
            build = subprocess.run(
                command, check=False, capture_output=True, text=True, timeout=90
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            result = subprocess.run(
                [str(binary)], check=False, capture_output=True, text=True, timeout=15
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("PASS clipped RGB888", result.stdout)
