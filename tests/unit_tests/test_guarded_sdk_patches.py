"""Exercise reviewed SDK patchers only against isolated copies of source fixtures."""

import hashlib
import importlib.util
from pathlib import Path
from types import ModuleType

import pytest

REPO = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).parent / "fixtures" / "esp32_sdk_patches"
PATCHERS = (
    (
        "esp32/cache_restart_framework_patch/patch_esp32p4_restart.py",
        "components/esp_system/port/soc/esp32p4/system_internal.c",
        "system_internal.c",
    ),
    (
        "esp32_hosted/sdmmc_framework_patch/patch_sdmmc_transaction.py",
        "components/esp_driver_sdmmc/src/sdmmc_transaction.c",
        "sdmmc_transaction.c",
    ),
)


def _load_patcher(path: str) -> ModuleType:
    source = REPO / "esphome" / "components" / path
    spec = importlib.util.spec_from_file_location(source.stem, source)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _framework(tmp_path: Path, version: str, target: str, fixture: str) -> Path:
    (tmp_path / "version.txt").write_text(version, encoding="utf-8")
    source = tmp_path / target
    source.parent.mkdir(parents=True)
    source.write_bytes((FIXTURES / fixture).read_bytes())
    return source


@pytest.mark.parametrize(("patcher", "target", "fixture"), PATCHERS)
@pytest.mark.parametrize("version", ["v5.5.5", "v5.5.6"])
def test_reviewed_preimage_and_idempotent_postimage(
    tmp_path: Path, patcher: str, target: str, fixture: str, version: str
) -> None:
    """Only the exact reviewed SDK owner changes, and repeat configuration is inert."""
    module = _load_patcher(patcher)
    source = _framework(tmp_path, version, target, fixture)
    assert hashlib.sha256(source.read_bytes()).hexdigest() == module.PRISTINE_SHA256
    assert module.patch_framework(tmp_path).startswith("installed guarded")
    assert hashlib.sha256(source.read_bytes()).hexdigest() == module.GUARDED_SHA256
    assert module.patch_framework(tmp_path) == "already guarded"
    assert (tmp_path / "version.txt").read_text(encoding="utf-8") == version
    assert {p for p in tmp_path.rglob("*") if p.is_file()} == {
        source,
        tmp_path / "version.txt",
    }


@pytest.mark.parametrize(("patcher", "target", "fixture"), PATCHERS)
@pytest.mark.parametrize("version", ["v5.5.4", "v5.5.7", "v6.0.0", "v5.5.6-dirty"])
def test_unknown_version_refuses_without_writing(
    tmp_path: Path, patcher: str, target: str, fixture: str, version: str
) -> None:
    """A matching owner file alone does not authorize a different SDK version."""
    module = _load_patcher(patcher)
    source = _framework(tmp_path, version, target, fixture)
    before = source.read_bytes()
    with pytest.raises(RuntimeError, match="requires reviewed ESP-IDF"):
        module.patch_framework(tmp_path)
    assert source.read_bytes() == before


@pytest.mark.parametrize(("patcher", "target", "fixture"), PATCHERS)
@pytest.mark.parametrize("version", ["v5.5.5", "v5.5.6"])
def test_unknown_source_refuses_without_writing(
    tmp_path: Path, patcher: str, target: str, fixture: str, version: str
) -> None:
    """Even an unrelated owner-file change requires a new explicit source review."""
    module = _load_patcher(patcher)
    source = _framework(tmp_path, version, target, fixture)
    changed = source.read_bytes() + b"\n/* Unreviewed source modification. */\n"
    source.write_bytes(changed)
    with pytest.raises(RuntimeError, match="source changed"):
        module.patch_framework(tmp_path)
    assert source.read_bytes() == changed


@pytest.mark.parametrize("version", ["v5.5.5", "v5.5.6"])
def test_known_fixed_sdmmc_owner_is_not_repatched(tmp_path: Path, version: str) -> None:
    """Recognize the exact fixed owner without wrapping or duplicating its change."""
    patcher, target, fixture = PATCHERS[1]
    module = _load_patcher(patcher)
    source = _framework(tmp_path, version, target, fixture)
    fixed = (
        source.read_text(encoding="utf-8")
        .replace(module.OLD, module.FIXED)
        .encode("utf-8")
    )
    assert hashlib.sha256(fixed).hexdigest() == module.FIXED_SHA256
    source.write_bytes(fixed)
    assert module.patch_framework(tmp_path) == "upstream fix already present"
    assert source.read_bytes() == fixed
