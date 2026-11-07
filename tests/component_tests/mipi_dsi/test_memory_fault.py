"""Exercise the actual bounded diagnostic mailbox and ROM formatting."""

import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest


def test_memory_fault():
    compiler = os.environ.get("ESPHOME_TEST_CXX")
    if not compiler:
        raise unittest.SkipTest("ESPHOME_TEST_CXX is required for this native test")
    here = Path(__file__).parent
    root = here.parents[2]
    source = root / "esphome/components/mipi_dsi/memory_fault_diagnostics.cpp"
    with tempfile.TemporaryDirectory(prefix="dsi-memory-") as temporary:
        output = Path(temporary)
        stubs = output / "stubs"
        for name in (
            "esphome/core/defines.h",
            "esp_attr.h",
            "esp_rom_uart.h",
            "freertos/FreeRTOS.h",
            "soc/soc.h",
            "soc/cache_reg.h",
            "soc/dma_pms_reg.h",
            "soc/hp_peri_pms_reg.h",
            "soc/hp_system_reg.h",
            "soc/spi_mem_s_reg.h",
        ):
            header = stubs / name
            header.parent.mkdir(parents=True, exist_ok=True)
            header.write_text(
                '#include "memory_fault_test_support.h"\n', encoding="utf-8"
            )
        registers = re.findall(r"REG_READ\((\w+)\)", source.read_text(encoding="utf-8"))
        assert len(registers) == len(set(registers)) == 21
        with (stubs / "soc/soc.h").open("a", encoding="utf-8") as header:
            for index, name in enumerate(registers, 1):
                header.write(f"#define {name} {index}\n")
        executable = output / (
            "memory-fault.exe" if os.name == "nt" else "memory-fault"
        )
        command = [part.strip('"') for part in shlex.split(compiler, posix=False)]
        command += [
            "-std=c++20",
            "-O1",
            "-UNDEBUG",
            "-I" + str(stubs),
            "-I" + str(here),
            "-I" + str(root),
            str(here / "memory_fault_test.cpp"),
            "-o",
            str(executable),
        ]
        subprocess.run(command, check=True)
        subprocess.run([str(executable)], check=True, timeout=15)


if __name__ == "__main__":
    test_memory_fault()
