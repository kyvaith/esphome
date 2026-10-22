"""Run the production preview eligibility and bounded admission retry helpers."""

from __future__ import annotations

import os
from pathlib import Path
import shlex
import shutil
import subprocess

import pytest


@pytest.fixture(scope="module")
def admission_executable(tmp_path_factory: pytest.TempPathFactory) -> Path:
    compiler = os.environ.get("CXX")
    command = shlex.split(compiler, posix=os.name != "nt") if compiler else []
    if not command:
        available = (
            shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
        )
        if available is None:
            pytest.skip("A host C++ compiler is required for the admission helper")
        command = [available]
    root = Path(__file__).resolve().parents[3]
    executable = tmp_path_factory.mktemp("snapshot_admission") / (
        "admission.exe" if os.name == "nt" else "admission"
    )
    source = Path(__file__).with_name("snapshot_cache_admission_test.cpp")
    result = subprocess.run(
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
    assert result.returncode == 0, result.stdout + result.stderr
    return executable


@pytest.mark.parametrize("case", range(12))
def test_snapshot_cache_admission(admission_executable: Path, case: int) -> None:
    result = subprocess.run(
        [str(admission_executable), str(case)],
        capture_output=True,
        text=True,
        timeout=15,
        check=False,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert f"case {case} PASS" in result.stdout


def test_reclaim_preserves_actual_reader_identities_and_invalidates_generation() -> (
    None
):
    root = Path(__file__).resolve().parents[3]
    source = (root / "esphome/components/lvgl/lvgl_esphome.cpp").read_text(
        encoding="utf-8"
    )
    reclaim = source.split(
        "SnapshotPreviewReclaimStats snapshot_reclaim_idle_application_previews(", 1
    )[1].split('extern "C" bool lvgl_esphome_snapshot_cache_tile_window(', 1)[0]
    for protected in (
        "snapshot_app_state.background_root",
        "snapshot_app_prepared_close_obj",
        "snapshot_app_work_obj",
        "snapshot_tile_window_cache.restore_pages[index]",
        "snapshot_compression_worker.entry == &entry",
        "snapshot_tile_prefetch_worker.busy.load",
        "snapshot_tile_prefetch_worker.result_ready.load",
    ):
        assert protected in reclaim
    assert "snapshot_cache_destroy_raw" not in reclaim
    assert reclaim.index("snapshot_cache_destroy_jpeg(entry);") < reclaim.index(
        "entry.generation++;"
    )
