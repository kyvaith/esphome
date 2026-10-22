"""Execute the same bounded scroll planner and span visitor used by firmware."""

from __future__ import annotations

import os
from pathlib import Path
import shlex
import shutil
import subprocess

import pytest


def test_scroll_snapshot_geometry_and_ownership(tmp_path: Path) -> None:
    compiler = os.environ.get("CXX")
    command = shlex.split(compiler, posix=os.name != "nt") if compiler else []
    if not command:
        available = (
            shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
        )
        if available is None:
            pytest.skip("A host C++ compiler is required for this executable test")
        command = [available]
    root = Path(__file__).resolve().parents[3]
    executable = tmp_path / (
        "scroll_snapshot_test.exe" if os.name == "nt" else "scroll_snapshot_test"
    )
    source = Path(__file__).with_name("scroll_snapshot_geometry_test.cpp")
    compiled = subprocess.run(
        [
            *command,
            "-std=c++20",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(root),
            str(source),
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
    assert "all 637 viewports" in result.stdout


def test_scroll_worker_timeout_keeps_source_ownership() -> None:
    """FreeRTOS cannot run on the host; verify both actual drain-failure guards."""
    root = Path(__file__).resolve().parents[3]
    source = (root / "esphome/components/lvgl/lvgl_esphome.cpp").read_text(
        encoding="utf-8"
    )
    cleanup = source.split("bool snapshot_scroll_cleanup() {", 1)[1].split(
        "\nint snapshot_scroll_clamp_y", 1
    )[0]
    assert cleanup.index(
        "if (!snapshot_scroll_worker_stop_and_wait())"
    ) < cleanup.index("scroll_snapshot_release_segments")
    assert "return false;" in cleanup.split("scroll_snapshot_release_segments", 1)[0]
    refresh = source.split('extern "C" bool lvgl_esphome_snapshot_scroll_refresh(', 1)[
        1
    ].split('extern "C" void lvgl_esphome_snapshot_scroll_update(', 1)[0]
    assert refresh.index(
        "if (!snapshot_scroll_worker_stop_and_wait())"
    ) < refresh.index("auto refresh_segment")
    assert "for (const auto &segment : state.segments)" in refresh
    assert "refresh_segment(segment.y, segment.buffer)" in refresh
    assert "if (!refreshed)" in refresh and "snapshot_scroll_cleanup();" in refresh
    assert "snapshot_scroll_worker_stop_and_wait();" not in source
    end = source.split('extern "C" void lvgl_esphome_snapshot_scroll_end(void) {', 1)[
        1
    ].split("\n}", 1)[0]
    assert "if (!snapshot_scroll_cleanup())" in end
    assert end.index("return;") < end.index("lv_obj_invalidate")
