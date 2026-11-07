"""Compile actual allocator/DownloadBuffer definitions with a bounded fake capability heap."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile


def test_download_buffer_policy():
    here = Path(__file__).parent
    root = here.parents[2]
    helpers = (root / "esphome/core/helpers.h").read_text(encoding="utf8")
    decoder = root / "esphome/components/artwork_image"
    header = (decoder / "image_decoder.h").read_text(encoding="utf8")
    source = (decoder / "image_decoder.cpp").read_text(encoding="utf8")
    owner = (decoder / "artwork_image.cpp").read_text(encoding="utf8")
    allocator = helpers.split("template<class T> class RAMAllocator {", 1)[1].split(
        "template<class T> using ExternalRAMAllocator", 1
    )[0]
    declaration = header.split("class DownloadBuffer {", 1)[1].split(
        "}  // namespace artwork_image", 1
    )[0]
    methods = source.split("DownloadBuffer::DownloadBuffer(size_t size)", 1)[1].split(
        "}  // namespace artwork_image", 1
    )[0]
    expression = owner.split("      download_buffer_(", 1)[1].split(
        "),\n      download_buffer_initial_size_", 1
    )[0]
    actual = (
        "template<class T> class RAMAllocator {"
        + allocator
        + "\nclass DownloadBuffer {"
        + declaration
    )
    actual += "\nDownloadBuffer::DownloadBuffer(size_t size)" + methods
    actual += (
        "\nstatic DownloadBuffer actual_initial_buffer(size_t download_buffer_size, bool prefer_external_download_buffer, bool preallocate_download_buffer=false) {\nconstexpr size_t DOWNLOAD_BUFFER_BASE_SIZE=32*1024;\nconstexpr size_t MAX_DOWNLOAD_BUFFER_SIZE=2*1024*1024;\nreturn DownloadBuffer("
        + expression
        + ");\n}\n"
    )
    compiler = [
        p.strip('"') for p in shlex.split(os.environ["ESPHOME_TEST_CXX"], posix=False)
    ]
    with tempfile.TemporaryDirectory(prefix="artwork-policy-") as temporary:
        out = Path(temporary)
        (out / "actual_download_buffer.inc").write_text(actual, encoding="utf8")
        binary = out / ("test.exe" if os.name == "nt" else "test")
        subprocess.run(
            compiler
            + [
                "-std=c++20",
                "-O1",
                "-UNDEBUG",
                "-I" + str(out),
                str(here / "download_buffer_policy_test.cpp"),
                "-o",
                str(binary),
            ],
            check=True,
        )
        subprocess.run([str(binary)], check=True, timeout=30)


if __name__ == "__main__":
    test_download_buffer_policy()
