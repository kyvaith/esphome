"""Pending host build: actual bounded metrics header, no SDK/vendor dependency."""

from pathlib import Path
import subprocess
import tempfile
import unittest

from host_light_control import compiler_command

HERE = Path(__file__).resolve().parent
SOURCE = HERE.parents[2] / "esphome/components/lvgl_material"


class ScalarPerformanceTests(unittest.TestCase):
    def compile_run(self, source, enabled):
        with tempfile.TemporaryDirectory(prefix="scalar-metrics-host-") as directory:
            root = Path(directory)
            if isinstance(source, str):
                cpp = root / "default_off.cpp"
                cpp.write_text(source, encoding="utf-8")
            else:
                cpp = source
            executable = root / "metrics.exe"
            command = [*compiler_command(), "-std=c++20", "-Wall", "-Wextra", "-Werror"]
            if enabled:
                command.append("-DUSE_LVGL_MATERIAL_SCALAR_PERFORMANCE_METRICS")
            command.extend(["-I", str(SOURCE), str(cpp), "-o", str(executable)])
            result = subprocess.run(command, capture_output=True, text=True, timeout=120, check=False)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10, check=False)
            self.assertEqual(0, result.returncode, result.stdout + result.stderr)

    def test_actual_accumulator_thresholds_failures_wrap_and_snapshot(self):
        self.compile_run(HERE / "scalar_performance_test.cpp", True)

    def test_default_off_header_has_no_metrics_types_or_fields(self):
        # A leaked declaration would make these test-only declarations conflict.
        self.compile_run('''
#include "material_scalar_performance.h"
namespace esphome::lvgl_material {
struct ScalarPerformanceAccumulator { char marker; };
struct ScalarPerformanceSnapshot { char marker; };
}
int main() { static_assert(sizeof(esphome::lvgl_material::ScalarPerformanceAccumulator)==1); }
''', False)


if __name__ == "__main__":
    unittest.main()
