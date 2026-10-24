"""Execute the real camera input owner/allocation/stop paths with hardware fakes."""

from __future__ import annotations

import os
from pathlib import Path
import shlex
import shutil
import subprocess

import pytest


def _function(source: str, signature: str) -> str:
    start = source.index(signature)
    end = source.index("\n}\n", start) + 3
    return source[start:end]


@pytest.fixture(scope="module")
def lifecycle_executable(tmp_path_factory: pytest.TempPathFactory) -> Path:
    if compiler := os.environ.get("ESPHOME_TEST_CXX"):
        command = [compiler, *shlex.split(os.environ.get("ESPHOME_TEST_CXX_ARGS", ""))]
    elif compiler := os.environ.get("CXX"):
        command = shlex.split(compiler, posix=os.name != "nt")
    elif (
        compiler := shutil.which("c++")
        or shutil.which("g++")
        or shutil.which("clang++")
    ):
        command = [compiler]
    else:
        pytest.skip("A host C++ compiler is required for camera ownership tests")
    root = Path(__file__).resolve().parents[3]
    source = (root / "esphome/components/network_camera/network_camera.cpp").read_text(
        encoding="utf-8"
    )
    output = tmp_path_factory.mktemp("network_camera_input")
    signatures = (
        "NetworkCamera::~NetworkCamera()",
        "void NetworkCamera::setup()",
        "void NetworkCamera::stop()",
        "size_t NetworkCamera::memory_usage_bytes() const",
        "void NetworkCamera::reset_parser_()",
        "void NetworkCamera::disconnect_stream_()",
        "bool NetworkCamera::ensure_input_buffers_()",
        "void NetworkCamera::release_input_buffers_()",
        "void NetworkCamera::stream_task_()",
    )
    (output / "camera_input_actual.inc").write_text(
        "\n".join(_function(source, signature) for signature in signatures),
        encoding="utf-8",
    )
    executable = output / ("camera_input.exe" if os.name == "nt" else "camera_input")
    subprocess.run(
        [
            *command,
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(output),
            str(Path(__file__).with_name("input_buffer_lifecycle_test.cpp")),
            "-o",
            str(executable),
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    return executable


@pytest.mark.parametrize(
    "case",
    [
        "setup_lazy",
        "setup_default",
        "setup_default_failure",
        "rollback_read",
        "fallback",
        "reuse",
        "stop_deferred",
        "worker_close",
        "worker_default",
        "retry",
        "shutdown",
        "destructor",
        "accounting",
        "accounting_concurrent",
    ],
)
def test_input_buffer_lifecycle(lifecycle_executable: Path, case: str) -> None:
    subprocess.run([str(lifecycle_executable), case], check=True, capture_output=True)
