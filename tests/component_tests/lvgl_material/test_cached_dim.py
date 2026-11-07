"""Exact packed dim round trips for every low-bit byte and split/unaligned spans."""

import os
from pathlib import Path
import shlex
import shutil
import subprocess

import pytest


def test_cached_dim_all_packed_bits(tmp_path: Path) -> None:
    command = shlex.split(os.environ.get("CXX", ""), posix=os.name != "nt")
    if not command:
        compiler = shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
        if compiler is None:
            pytest.skip("A host C++ compiler is required")
        command = [compiler]
    root = Path(__file__).resolve().parents[3]
    executable = tmp_path / ("dim.exe" if os.name == "nt" else "dim")
    subprocess.run(
        [
            *command,
            "-std=c++20",
            "-UNDEBUG",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(root),
            str(Path(__file__).with_name("cached_dim_test.cpp")),
            "-o",
            str(executable),
        ],
        check=True,
        capture_output=True,
        text=True,
        timeout=180,
    )
    subprocess.run([str(executable)], check=True, timeout=15)
