"""Action timestamps retain fractions and DST offsets in bounded display text."""

import os
from pathlib import Path
import subprocess

from host_light_control import compiler_command


def test_action_timestamp_presentation(tmp_path: Path) -> None:
    root = Path(__file__).resolve().parents[3]
    executable = tmp_path / ("action_timestamp.exe" if os.name == "nt" else "action_timestamp")
    subprocess.run(
        [
            *compiler_command(),
            "-std=c++20",
            "-UNDEBUG",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(root),
            str(Path(__file__).with_name("action_timestamp_test.cpp")),
            "-o",
            str(executable),
        ],
        check=True,
        capture_output=True,
        text=True,
        timeout=180,
    )
    subprocess.run([str(executable)], check=True, timeout=15)
