"""Compile the actual constexpr cadence helper at multiple FreeRTOS tick rates."""

from pathlib import Path
import subprocess
import tempfile
import unittest

from host_light_control import SOURCE, compiler_command


class ScalarTickTests(unittest.TestCase):
    def test_ceiling_preserves_1000hz_and_bounds_lower_tick_rates(self):
        with tempfile.TemporaryDirectory(prefix="scalar-tick-host-") as directory:
            root = Path(directory)
            cpp = root / "ticks.cpp"
            cpp.write_text(r"""
#include "material_scalar_timing.h"
#include <cassert>
using esphome::lvgl_material::scalar_render_delay_ticks;
static_assert(scalar_render_delay_ticks(1000)==34);
static_assert(scalar_render_delay_ticks(100)==4);
static_assert(scalar_render_delay_ticks(128)==5);
int main(){
 for(const uint32_t hz : {1U,10U,100U,128U,1000U,1024U,10000U}){
  const uint64_t ticks=scalar_render_delay_ticks(hz);
  assert(ticks*1000 >= uint64_t{34}*hz);
  assert((ticks-1)*1000 < uint64_t{34}*hz);
 }
}
""")
            # Range-for initializer list is a standard dependency, not a facade.
            cpp.write_text(
                "#include <initializer_list>\n" + cpp.read_text(encoding="utf-8")
            )
            executable = root / "ticks.exe"
            run = subprocess.run(
                [
                    *compiler_command(),
                    "-std=c++20",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(SOURCE),
                    str(cpp),
                    "-o",
                    str(executable),
                ],
                capture_output=True,
                text=True,
                timeout=120,
                check=False,
            )
            self.assertEqual(0, run.returncode, run.stdout + run.stderr)
            run = subprocess.run(
                [str(executable)],
                capture_output=True,
                text=True,
                timeout=10,
                check=False,
            )
            self.assertEqual(0, run.returncode, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
