"""Native and PlatformIO use the same reviewed PPA cache ownership patch."""

from __future__ import annotations

import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

import pytest

from esphome.components.lvgl import lvgl_build_filter as patches

FIXTURES = Path(__file__).parent / "fixtures" / "ppa_cache"
NAMES = ("ppa_srm.c", "ppa_blend.c", "ppa_fill.c")


def framework(tmp_path, version="5.5.6"):
    root = tmp_path / "framework with spaces"
    root.mkdir(parents=True)
    (root / "version.txt").write_text(f"v{version}\n", encoding="utf-8")
    source_dir = root / "components" / "esp_driver_ppa" / "src"
    source_dir.mkdir(parents=True)
    for name in NAMES:
        region = (FIXTURES / f"{version}-{name.removesuffix('.c')}.inc").read_text(
            encoding="utf-8"
        )
        descriptors = (
            FIXTURES / f"descriptors-{name.removesuffix('.c')}.inc"
        ).read_text(encoding="utf-8")
        (source_dir / name).write_text(
            '#include "esp_cache.h"\n\n'
            f'static const char *TAG = "{name.removesuffix(".c")}";\n\n'
            f"void descriptor_sync(void) {{\n{descriptors}\n}}\n\n"
            "void operation(void) {\n"
            f"{region}\n    esp_err_t ret = ESP_OK;\n}}\n",
            encoding="utf-8",
        )
    return root, source_dir


def saved_sources(source_dir):
    return {name: (source_dir / name).read_bytes() for name in NAMES}


@pytest.mark.parametrize("version", ["5.5.5", "5.5.6"])
@pytest.mark.parametrize("name,call_count", list(zip(NAMES, [2, 3, 1], strict=True)))
def test_reviewed_sdk_cache_source_patch_is_idempotent(
    tmp_path, version, name, call_count
):
    _, source_dir = framework(tmp_path, version)
    source = source_dir / name
    patches.patch_espidf_ppa_cache_sync_source(source)
    first = source.read_bytes()
    patches.patch_espidf_ppa_cache_sync_source(source)
    assert source.read_bytes() == first
    text = first.decode()
    assert text.count("static esp_err_t ppa_cache_msync_external_window(") == 1
    assert text.count("extern bool esphome_lvgl_ppa_skip_cache_msync(") == 1
    assert text.count("ppa_cache_msync_external_window(") == call_count + 1
    if name == "ppa_srm.c":
        assert (
            "config->out.block_offset_x != 0 || new_block_w != config->out.pic_w"
            in text
        )
    else:
        assert "config->out.block_offset_x != 0" not in text


@pytest.mark.parametrize("version", ["5.5.5", "5.5.6"])
def test_explicit_framework_patch_preflights_and_logs_actual_source_paths(
    tmp_path, capsys, version
):
    root, source_dir = framework(tmp_path, version)
    patches.patch_espidf_ppa_cache_sync(root)
    first = saved_sources(source_dir)
    patches.patch_espidf_ppa_cache_sync(root)
    assert saved_sources(source_dir) == first
    output = capsys.readouterr().out
    for name in NAMES:
        assert str(source_dir / name) in output
    assert output.count("sha256=") == 6


@pytest.mark.parametrize("failure", ["hash", "call", "missing", "version"])
def test_unknown_sdk_fails_before_any_file_is_written(tmp_path, failure):
    root, source_dir = framework(tmp_path)
    target = source_dir / NAMES[-1]
    if failure == "hash":
        target.write_text(target.read_text().replace("fill_block_h", "changed_height"))
    elif failure == "call":
        target.write_text(target.read_text().replace("esp_cache_msync", "unknown_sync"))
    elif failure == "missing":
        target.unlink()
    else:
        (root / "version.txt").write_text("v5.5.7\n")
    before = {path: path.read_bytes() for path in source_dir.iterdir()}
    with pytest.raises((RuntimeError, FileNotFoundError)):
        patches.patch_espidf_ppa_cache_sync(root)
    assert {path: path.read_bytes() for path in source_dir.iterdir()} == before


def test_changed_preexisting_helper_fails_without_mutation(tmp_path):
    _, source_dir = framework(tmp_path)
    source = source_dir / "ppa_srm.c"
    patches.patch_espidf_ppa_cache_sync_source(source)
    source.write_text(
        source.read_text().replace("if (preserve_dirty &&", "if (false &&")
    )
    before = source.read_bytes()
    with pytest.raises(RuntimeError, match="helper changed"):
        patches.patch_espidf_ppa_cache_sync_source(source)
    assert source.read_bytes() == before


def test_unrelated_sdk_diagnostics_remain_outside_reviewed_cache_regions(tmp_path):
    root, source_dir = framework(tmp_path)
    journal = "\nstatic unsigned diagnostic_counter; /* independent driver instrumentation */\n"
    for source in source_dir.iterdir():
        source.write_text(source.read_text() + journal, encoding="utf-8")
    patches.patch_espidf_ppa_cache_sync(root)
    assert all(
        data.decode().endswith(journal) for data in saved_sources(source_dir).values()
    )


def test_unreviewed_cache_call_outside_region_fails_before_any_mutation(tmp_path):
    root, source_dir = framework(tmp_path)
    source = source_dir / "ppa_fill.c"
    source.write_text(
        source.read_text() + "\nvoid extra(void) { esp_cache_msync(p, n, flags); }\n"
    )
    before = saved_sources(source_dir)
    with pytest.raises(RuntimeError, match="outside guarded region"):
        patches.patch_espidf_ppa_cache_sync(root)
    assert saved_sources(source_dir) == before


def test_legacy_patch_upgrades_once_without_duplicate_helpers(tmp_path):
    _, source_dir = framework(tmp_path)
    source = source_dir / "ppa_srm.c"
    patches.patch_espidf_ppa_cache_sync_source(source)
    source.write_text(
        source.read_text()
        .replace(
            "ESP_CACHE_MSYNC_FLAG_DIR_C2M, false);", "ESP_CACHE_MSYNC_FLAG_DIR_C2M);"
        )
        .replace(
            "ESP_CACHE_MSYNC_FLAG_DIR_M2C, "
            "config->out.block_offset_x != 0 || new_block_w != config->out.pic_w);",
            "ESP_CACHE_MSYNC_FLAG_DIR_M2C);",
        )
    )
    patches.patch_espidf_ppa_cache_sync_source(source)
    first = source.read_bytes()
    patches.patch_espidf_ppa_cache_sync_source(source)
    assert source.read_bytes() == first


def test_platformio_and_explicit_native_entry_produce_identical_sources(
    tmp_path, monkeypatch
):
    native_root, native_dir = framework(tmp_path / "native")
    pio_root, pio_dir = framework(tmp_path / "pio")

    class Platform:
        def get_package_dir(self, package):
            assert package == "framework-espidf"
            return str(pio_root)

    class Environment:
        def PioPlatform(self):
            return Platform()

    patches.patch_espidf_ppa_cache_sync(native_root)
    monkeypatch.setattr(patches, "env", Environment())
    patches.patch_espidf_ppa_cache_sync()
    assert saved_sources(native_dir) == saved_sources(pio_dir)


def test_native_cli_patches_sdk_without_platformio(tmp_path):
    root, source_dir = framework(tmp_path)
    result = subprocess.run(
        [
            sys.executable,
            "-I",
            str(Path(patches.__file__)),
            "--patch-espidf-ppa-cache-sync",
            str(root),
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stderr
    assert result.stdout.count("ESPHome PPA cache source IDF=v5.5.6 path=") == 3
    assert all(
        b"ppa_cache_msync_external_window(" in data
        for data in saved_sources(source_dir).values()
    )


def test_actual_cache_helper_preserves_owned_windows_and_dirty_neighbors(tmp_path):
    if compiler := os.environ.get("ESPHOME_TEST_CXX"):
        command = [compiler, *shlex.split(os.environ.get("ESPHOME_TEST_CXX_ARGS", ""))]
    elif compiler := os.environ.get("CXX"):
        command = shlex.split(compiler, posix=os.name != "nt")
    elif compiler := shutil.which("c++") or shutil.which("clang++"):
        command = [compiler]
    else:
        pytest.skip("A host C++ compiler is required for PPA cache ownership")
    (tmp_path / "actual_ppa_cache_helper.inc").write_text(
        patches.PPA_CACHE_SYNC_HELPER, encoding="utf-8"
    )
    executable = tmp_path / ("ppa_cache.exe" if os.name == "nt" else "ppa_cache")
    compiled = subprocess.run(
        [
            *command,
            "-std=c++20",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(tmp_path),
            str(Path(__file__).with_name("ppa_cache_source_patch_test.cpp")),
            "-o",
            str(executable),
        ],
        capture_output=True,
        text=True,
        check=False,
        timeout=120,
    )
    assert compiled.returncode == 0, compiled.stdout + compiled.stderr
    result = subprocess.run(
        [str(executable)], capture_output=True, text=True, check=False, timeout=15
    )
    assert result.returncode == 0, result.stdout + result.stderr
