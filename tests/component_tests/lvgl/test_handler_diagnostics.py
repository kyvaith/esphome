"""Compile the bounded production LVGL handler diagnostics accumulator."""

from __future__ import annotations

import os
from pathlib import Path
import shlex
import shutil
import subprocess

import pytest

from esphome.components.lvgl import CONFIG_SCHEMA
from esphome.const import KEY_CORE, KEY_TARGET_FRAMEWORK, KEY_TARGET_PLATFORM
from esphome.core import CORE


@pytest.mark.parametrize("configured", [None, False, True])
def test_handler_diagnostics_opt_in(configured: bool | None) -> None:
    CORE.data[KEY_CORE] = {
        KEY_TARGET_PLATFORM: "host",
        KEY_TARGET_FRAMEWORK: "",
    }
    config = {} if configured is None else {"handler_diagnostics": configured}
    validated = CONFIG_SCHEMA(config)[0]
    assert validated["handler_diagnostics"] is (configured is True)


def test_handler_diagnostics(tmp_path: Path) -> None:
    if compiler := os.environ.get("ESPHOME_TEST_CXX"):
        command = [compiler, *shlex.split(os.environ.get("ESPHOME_TEST_CXX_ARGS", ""))]
    elif compiler := os.environ.get("CXX"):
        command = shlex.split(compiler, posix=os.name != "nt")
    elif compiler := (
        shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
    ):
        command = [compiler]
    else:
        pytest.skip("A host C++ compiler is required for handler diagnostics")
    root = Path(__file__).resolve().parents[3]
    executable = tmp_path / ("handler.exe" if os.name == "nt" else "handler")
    compiled = subprocess.run(
        [
            *command,
            "-std=c++20",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(root),
            str(Path(__file__).with_name("handler_diagnostics_test.cpp")),
            "-o",
            str(executable),
        ],
        capture_output=True,
        text=True,
        timeout=180,
        check=False,
    )
    assert compiled.returncode == 0, compiled.stdout + compiled.stderr
    result = subprocess.run(
        [str(executable)], capture_output=True, text=True, timeout=15, check=False
    )
    assert result.returncode == 0, result.stdout + result.stderr
