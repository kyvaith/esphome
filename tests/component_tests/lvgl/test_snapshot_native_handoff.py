"""Exercise native handoff boundaries using the actual production function bodies."""

from __future__ import annotations

import os
from pathlib import Path
import shlex
import shutil
import subprocess

import pytest


@pytest.fixture(scope="module")
def native_handoff_executable(tmp_path_factory: pytest.TempPathFactory) -> Path:
    compiler = os.environ.get("CXX")
    command = shlex.split(compiler, posix=os.name != "nt") if compiler else []
    if not command:
        available = (
            shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
        )
        if available is None:
            pytest.skip("A host C++ compiler is required for the native handoff test")
        command = [available]
    root = Path(
        os.environ.get(
            "ESPHOME_HANDOFF_SOURCE_ROOT", Path(__file__).resolve().parents[3]
        )
    )
    component = root / "esphome/components/lvgl"
    output = tmp_path_factory.mktemp("snapshot_native_handoff")
    for filename, start_marker, end_marker in (
        (
            "lvgl_esphome.cpp",
            "void snapshot_swipe_finish_now() {",
            "bool snapshot_scroll_cleanup()",
        ),
        (
            "lvgl_direct_snapshot_compositor.cpp",
            "void LvglDirectSnapshotCompositor::complete_direct_application_() {",
            "void LvglDirectSnapshotCompositor::reset_direct_home_()",
        ),
    ):
        source = (component / filename).read_text(encoding="utf-8")
        start = source.index(start_marker)
        end = source.index(end_marker, start)
        (output / f"{filename}.inc").write_text(source[start:end], encoding="utf-8")
    executable = output / (
        "native_handoff.exe" if os.name == "nt" else "native_handoff"
    )
    compiled = subprocess.run(
        [
            *command,
            "-std=c++20",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(output),
            str(Path(__file__).with_name("snapshot_native_handoff_test.cpp")),
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


@pytest.mark.parametrize("case", range(8))
def test_snapshot_native_handoff(native_handoff_executable: Path, case: int) -> None:
    result = subprocess.run(
        [str(native_handoff_executable), str(case)],
        capture_output=True,
        text=True,
        timeout=15,
        check=False,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert f"case {case} PASS" in result.stdout
