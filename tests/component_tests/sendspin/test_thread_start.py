"""Execute exact reviewed Sendspin startup methods with deterministic pthread/resource adapters."""

import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import unittest

HERE = Path(__file__).parent
ROOT = HERE.parents[2]
PATCH_DIR = ROOT / "esphome/components/sendspin/sendspin_memory_patch"
FUNCTIONS = {
    "protocol_task": ["bool ProtocolTask::start(", "void ProtocolTask::stop("],
    "sync_task": ["bool SyncTask::start(", "void SyncTask::stop("],
    "source_task": ["bool SourceTask::start(", "void SourceTask::stop("],
    "artwork_role": [
        "bool ArtworkRole::Impl::start(",
        "bool ArtworkRole::Impl::signal_stop(",
        "void ArtworkRole::Impl::stop(",
    ],
    "visualizer_role": [
        "bool VisualizerRole::Impl::start(",
        "bool VisualizerRole::Impl::signal_stop(",
        "void VisualizerRole::Impl::stop(",
    ],
    "client": [
        "bool SendspinClient::start(",
        "void SendspinClient::stop_role_threads(",
    ],
}


def extract(text, prefix):
    start = text.index(prefix)
    brace = text.index("{", start)
    depth = 0
    for match in re.finditer(
        r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|[{}]', text[brace:], re.DOTALL
    ):
        if match[0] == "{":
            depth += 1
        elif match[0] == "}":
            depth -= 1
            if not depth:
                return text[start : brace + match.end()]
    raise AssertionError(prefix)


def load_patch():
    spec = importlib.util.spec_from_file_location(
        "thread_patch", PATCH_DIR / "patch_sendspin_threads.py"
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def prepare_methods(output):
    module = load_patch()
    supplied = os.environ.get("ESPHOME_TEST_SENDSPIN_SOURCES")
    parts = []
    proof = []
    for stem, prefixes in FUNCTIONS.items():
        # Tracked excerpts are reviewed 0.9.3 methods with the existing memory guards.
        # The same guarded
        # transform used by CMake is applied here, not a second implementation.
        source = (HERE / "fixtures" / (stem + "_startup_093.cpp.inc")).read_text(
            encoding="utf8"
        )
        for old, new in module.replacements()[f"src/{stem}.cpp"]:
            if old in source:
                source = module.transform(source, [(old, new)])
        for prefix in prefixes:
            method = extract(source, prefix)
            if supplied:
                actual = (Path(supplied) / f"src/{stem}.cpp").read_text(encoding="utf8")
                assert method == extract(actual, prefix)
            parts.append(method)
            proof.append(
                {
                    "source": stem,
                    "function": prefix,
                    "sha256": hashlib.sha256(method.encode()).hexdigest(),
                }
            )
    (output / "actual_start_methods.inc").write_text(
        "\n\n".join(parts), encoding="utf8"
    )
    (output / "extracted.json").write_text(json.dumps(proof, indent=2), encoding="utf8")


def run_test(output):
    compiler = os.environ.get("ESPHOME_TEST_CXX")
    if not compiler:
        raise unittest.SkipTest(
            "Set ESPHOME_TEST_CXX and optional ESPHOME_TEST_CXX_ARGS for the host compiler"
        )
    output.mkdir(parents=True, exist_ok=True)
    prepare_methods(output)
    for name in ["esp_pthread.h", "esp_heap_caps.h", "pthread.h"]:
        (output / name).write_text(
            '#include "thread_test_platform.h"\n', encoding="utf8"
        )
    (output / "sdkconfig.h").write_text(
        "#define CONFIG_ESPHOME_SENDSPIN_CHECKED_THREADS 1\n"
        "#define CONFIG_ESPHOME_SENDSPIN_MEMORY_OPTIMIZATIONS 1\n",
        encoding="utf8",
    )
    (output / "platform").mkdir(exist_ok=True)
    (output / "platform/fallible_thread.h").write_bytes(
        (PATCH_DIR / "fallible_thread.h").read_bytes()
    )
    binary = output / ("thread_start.exe" if os.name == "nt" else "thread_start")
    args = shlex.split(os.environ.get("ESPHOME_TEST_CXX_ARGS", ""))
    command = [
        compiler,
        *args,
        "-std=c++20",
        "-O0",
        "-g",
        "-UNDEBUG",
        "-DESP_PLATFORM",
        "-I" + str(output),
        "-I" + str(HERE),
        "-I" + str(ROOT),
        str(HERE / "thread_start_test.cpp"),
        "-o",
        str(binary),
    ]
    process = subprocess.run(command, capture_output=True, timeout=120, check=False)
    (output / "compile.log").write_bytes(process.stdout + process.stderr)
    assert process.returncode == 0, process.stderr.decode(errors="replace")
    process = subprocess.run(
        [str(binary)], capture_output=True, timeout=30, check=False
    )
    (output / "run.log").write_bytes(process.stdout + process.stderr)
    assert process.returncode == 0, process.stderr.decode(errors="replace")


def test_checked_worker_starts():
    with tempfile.TemporaryDirectory(prefix="sendspin-start-") as directory:
        run_test(Path(directory))


if __name__ == "__main__":
    run_test(Path(os.environ["ESPHOME_TEST_OUTPUT"]))
