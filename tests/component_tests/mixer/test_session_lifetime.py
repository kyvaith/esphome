"""Run the actual mixer/reader/ring source against deterministic RTOS adapters."""

from pathlib import Path
import os
import shlex
import subprocess
import tempfile


def test_session_lifetime():
    here = Path(__file__).parent
    root = here.parents[2]
    baseline = Path(os.environ.get("ESPHOME_TEST_BASELINE", str(root)))
    compiler = shlex.split(os.environ["ESPHOME_TEST_CXX"], posix=False)
    compiler = [part.strip('"') for part in compiler]
    with tempfile.TemporaryDirectory(prefix="mixer-session-") as tmp:
        out = Path(tmp)
        stubs = out / "stubs"
        stubs.mkdir()
        helpers = (baseline / "esphome/core/helpers.h").read_text(encoding="utf8")
        begin = helpers.index("template<typename T> class FixedVector {")
        end = helpers.index("\n};", begin) + 3
        (stubs / "actual_fixed_vector.h").write_text(helpers[begin:end], encoding="utf8")
        headers = [
            "freertos/FreeRTOS.h", "freertos/event_groups.h", "freertos/ringbuf.h",
            "esphome/core/helpers.h", "esphome/core/defines.h", "esphome/core/component.h",
            "esphome/core/application.h", "esphome/core/hal.h", "esphome/core/log.h",
            "esphome/core/static_task.h", "gain.h", "mixer.h", "pcm_convert.h", "esp_err.h",
        ]
        for name in headers:
            path = stubs / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#include "session_test_support.h"\n', encoding="utf8")
        path = stubs / "esphome/components/speaker/speaker.h"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#include "session_test_speaker.h"\n', encoding="utf8")
        sources = [
            here / "session_lifetime_test.cpp",
            root / "esphome/components/mixer/speaker/mixer_speaker.cpp",
            root / "esphome/components/audio/audio_transfer_buffer.cpp",
            root / "esphome/components/ring_buffer/ring_buffer.cpp",
            baseline / "esphome/components/audio/audio.cpp",
        ]
        binary = out / ("session.exe" if os.name == "nt" else "session")
        command = compiler + [
            "-std=c++20", "-O1", "-g", "-UNDEBUG", "-DUSE_ESP32", "-DUSE_SPEAKER",
            "-I" + str(stubs), "-I" + str(here), "-I" + str(root), "-I" + str(baseline),
            *map(str, sources), "-o", str(binary),
        ]
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True, timeout=30)


if __name__ == "__main__":
    test_session_lifetime()
