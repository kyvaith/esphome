"""Exercise the production systrace aggregator with interleaved renderer tasks."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
SOURCE = HERE.parents[2] / "esphome/components/lvgl/lvgl_esphome.cpp"


class ProfilerSummaryTests(unittest.TestCase):
    def test_interleaved_scopes_and_multiline_flushes(self):
        if not (compiler := os.environ.get("ESPHOME_TEST_CXX")):
            self.skipTest("Set ESPHOME_TEST_CXX")
        source = SOURCE.read_text(encoding="utf8")
        declarations = source[
            source.index("static constexpr size_t PROFILER_MAX_STACK") : source.index(
                "struct ProfilerManualStats"
            )
        ]
        copy_name = source[
            source.index("static void profiler_copy_name") : source.index(
                "static void profiler_reset_summary"
            )
        ]
        parser = source[
            source.index("static bool profiler_parse_line") : source.index(
                "static void profiler_print_summary"
            )
        ]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "profiler_summary.inc").write_text(
                declarations + copy_name + parser, encoding="utf8"
            )
            binary = root / "profiler.exe"
            build = subprocess.run(
                [
                    compiler,
                    *shlex.split(os.environ.get("ESPHOME_TEST_CXX_ARGS", "")),
                    "-std=c++20",
                    "-UNDEBUG",
                    "-I" + str(root),
                    str(HERE / "profiler_summary_test.cpp"),
                    "-o",
                    str(binary),
                ],
                check=False,
                capture_output=True,
                text=True,
                timeout=90,
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run(
                [str(binary)], check=False, capture_output=True, text=True, timeout=15
            )
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("PASS interleaved profiler", run.stdout)
