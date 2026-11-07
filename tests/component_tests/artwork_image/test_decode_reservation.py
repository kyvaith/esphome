"""Exercise actual decode-pool methods with an always-successful host allocator."""

import os
import shlex
import subprocess
import tempfile
from pathlib import Path


def test_decode_reservation():
    here = Path(__file__).parent
    root = here.parents[2]
    source = Path(
        os.environ.get(
            "ESPHOME_TEST_ARTWORK_SOURCE",
            root / "esphome/components/artwork_image/artwork_image.cpp",
        )
    ).read_text(encoding="utf8")
    methods = (
        "bool ArtworkImage::reserve_decode_buffer_capacity(",
        "size_t ArtworkImage::compatible_decode_buffer_count_(",
        "bool ArtworkImage::store_spare_buffer_(",
        "bool ArtworkImage::should_keep_spare_buffer_(",
        "void ArtworkImage::release_unused_decode_capacity(",
        "void ArtworkImage::cleanup_retired_buffers_(",
        "void ArtworkImage::release_spare_buffer_(",
        "void ArtworkImage::release_buffer_(",
        "uint8_t *ArtworkImage::loan_decode_buffer_capacity(",
        "bool ArtworkImage::return_decode_buffer_capacity(",
    )
    actual = []
    constants = []
    for name in (
        "MAX_SPARE_BUFFER_SIZE",
        "RETIRED_BUFFER_GRACE_MS",
        "MAX_RETIRED_BUFFERS",
    ):
        constants.append(
            next(
                line
                for line in source.splitlines()
                if line.startswith("static constexpr ") and f" {name} =" in line
            )
        )
    for method in methods:
        start = source.index(method)
        end = source.index("\n}\n", start) + 3
        actual.append(source[start:end])
    compiler = [
        part.strip('"')
        for part in shlex.split(os.environ["ESPHOME_TEST_CXX"], posix=False)
    ]

    def run(output):
        output.mkdir(parents=True, exist_ok=True)
        (output / "actual_constants.inc").write_text(
            "\n".join(constants), encoding="utf8"
        )
        (output / "actual_reservation.inc").write_text(
            "\n".join(actual), encoding="utf8"
        )
        binary = output / ("reservation.exe" if os.name == "nt" else "reservation")
        subprocess.run(
            compiler
            + [
                "-std=c++20",
                "-O1",
                "-UNDEBUG",
                "-I" + str(output),
                str(here / "decode_reservation_test.cpp"),
                "-o",
                str(binary),
            ],
            check=True,
        )
        subprocess.run([str(binary)], check=True, timeout=10)

    if output := os.environ.get("ESPHOME_TEST_OUTPUT_DIR"):
        run(Path(output))
    else:
        with tempfile.TemporaryDirectory(prefix="artwork-reservation-") as temporary:
            run(Path(temporary))


if __name__ == "__main__":
    test_decode_reservation()
