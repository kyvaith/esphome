"""Execute the production backing refresh with deterministic host ownership fakes."""

from __future__ import annotations

import os
from pathlib import Path
import shlex
import shutil
import subprocess

import pytest


@pytest.fixture(scope="module")
def backing_refresh_executable(tmp_path_factory: pytest.TempPathFactory) -> Path:
    if compiler := os.environ.get("ESPHOME_TEST_CXX"):
        command = [
            compiler,
            *shlex.split(os.environ.get("ESPHOME_TEST_CXX_ARGS", "")),
        ]
    else:
        compiler = os.environ.get("CXX")
        command = shlex.split(compiler, posix=os.name != "nt") if compiler else []
    if not command:
        available = (
            shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
        )
        if available is None:
            pytest.skip("A host C++ compiler is required for the backing refresh test")
        command = [available]
    root = Path(__file__).resolve().parents[3]
    source = (root / "esphome/components/lvgl/lvgl_esphome.cpp").read_text(
        encoding="utf-8"
    )
    start = source.index('extern "C" bool lvgl_esphome_snapshot_refresh_tile_backing(')
    end = source.index(
        'extern "C" bool lvgl_esphome_snapshot_refresh_tile_region(', start
    )
    output = tmp_path_factory.mktemp("snapshot_backing_pause")
    (output / "snapshot_backing_under_test.inc").write_text(
        source[start:end], encoding="utf-8"
    )
    executable = output / ("backing_pause.exe" if os.name == "nt" else "backing_pause")
    compiled = subprocess.run(
        [
            *command,
            "-std=c++20",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(output),
            str(Path(__file__).with_name("snapshot_backing_pause_test.cpp")),
            "-o",
            str(executable),
        ],
        capture_output=True,
        text=True,
        timeout=180,
        check=False,
    )
    assert compiled.returncode == 0, compiled.stdout + compiled.stderr
    return executable


@pytest.mark.parametrize("case", range(13))
def test_snapshot_backing_pause_balance(
    backing_refresh_executable: Path, case: int
) -> None:
    result = subprocess.run(
        [str(backing_refresh_executable), str(case)],
        capture_output=True,
        text=True,
        timeout=15,
        check=False,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert f"case {case} PASS" in result.stdout
