"""Replay the two-file PIE backport only in isolated, reviewed SDK fixture trees."""

import hashlib
import importlib.util
from pathlib import Path
import sys
from types import ModuleType

import pytest

REPO = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).parent / "fixtures" / "pie_context_sdk"
PATCHER = (
    REPO / "esphome/components/esp32/pie_context_framework_patch/patch_pie_context.py"
)
OFFICIAL_FIXED_HASHES = {
    "portasm.S": "661abedc2e815a1289c7c767a515d714d6dd59090a36852acfcfed196f24427e",
    "rvruntime-frames.h": "490aa603173d09761a89eca6fb0670211d85480566836850a4069748bc807238",
}


def _load_patcher(monkeypatch: pytest.MonkeyPatch) -> ModuleType:
    spec = importlib.util.spec_from_file_location("pie_context_test_patcher", PATCHER)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    monkeypatch.setitem(sys.modules, spec.name, module)
    spec.loader.exec_module(module)
    return module


@pytest.fixture
def patcher(monkeypatch: pytest.MonkeyPatch) -> ModuleType:
    return _load_patcher(monkeypatch)


def _framework(root: Path, patcher: ModuleType, version: str = "v5.5.6") -> None:
    root.mkdir(parents=True, exist_ok=True)
    (root / "version.txt").write_text(version, encoding="utf-8")
    for source in patcher.SOURCES:
        target = root / source.path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes((FIXTURES / "reviewed-5.5.6" / target.name).read_bytes())


def _tree(root: Path) -> dict[str, bytes]:
    return {
        path.relative_to(root).as_posix(): path.read_bytes()
        for path in root.rglob("*")
        if path.is_file()
    }


def test_import_with_cli_arguments_is_inert(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    """Importing from pytest or another program must never run the installer CLI."""
    monkeypatch.setattr(sys, "argv", [str(PATCHER), str(tmp_path), "esp32p4"])
    _load_patcher(monkeypatch)
    assert _tree(tmp_path) == {}


@pytest.mark.parametrize("version", ["v5.5.5", "v5.5.6"])
def test_reviewed_roundtrip_and_idempotence(
    tmp_path: Path, patcher: ModuleType, version: str
) -> None:
    """OFF restores the complete original bytes; ON matches the official correction."""
    _framework(tmp_path, patcher, version)
    before = _tree(tmp_path)
    for source in patcher.SOURCES:
        assert hashlib.sha256(before[source.path]).hexdigest() == source.pristine_sha256

    assert patcher.patch_framework(tmp_path, "esp32p4").startswith("installed guarded")
    for source in patcher.SOURCES:
        name = Path(source.path).name
        official = (FIXTURES / "fixed" / name).read_bytes()
        assert hashlib.sha256(official).hexdigest() == OFFICIAL_FIXED_HASHES[name]
        assert official.count(source.enabled.encode()) == 1

        guarded = (tmp_path / source.path).read_bytes()
        assert hashlib.sha256(guarded).hexdigest() == source.guarded_sha256
        prefix, branches = guarded.split(
            b"#if CONFIG_ESPHOME_ESP32P4_PIE_CONTEXT_FIX\n"
        )
        enabled, branches = branches.split(b"\n#else\n", 1)
        disabled, suffix = branches.split(b"\n#endif", 1)
        assert prefix + disabled + suffix == before[source.path]
        assert enabled == source.enabled.encode()
        assert disabled == source.before.encode()
        assert (
            hashlib.sha256(prefix + enabled + suffix).hexdigest() == source.fixed_sha256
        )
        assert prefix + enabled + suffix == before[source.path].replace(
            disabled, enabled, 1
        )

    after = _tree(tmp_path)
    assert patcher.patch_framework(tmp_path, "esp32p4") == "already guarded"
    assert _tree(tmp_path) == after
    assert after.keys() == before.keys()
    assert after["version.txt"] == before["version.txt"]


@pytest.mark.parametrize("version", ["v5.5.4", "v5.5.7", "v6.0.0", "v5.5.6-dirty"])
def test_unknown_version_is_unchanged(
    tmp_path: Path, patcher: ModuleType, version: str
) -> None:
    _framework(tmp_path, patcher, version)
    before = _tree(tmp_path)
    with pytest.raises(RuntimeError, match="requires reviewed ESP-IDF"):
        patcher.patch_framework(tmp_path, "esp32p4")
    assert _tree(tmp_path) == before


@pytest.mark.parametrize("target", ["esp32s3", "esp32", "ESP32P4", ""])
def test_unknown_target_is_unchanged(
    tmp_path: Path, patcher: ModuleType, target: str
) -> None:
    _framework(tmp_path, patcher)
    before = _tree(tmp_path)
    with pytest.raises(RuntimeError, match="requires reviewed target esp32p4"):
        patcher.patch_framework(tmp_path, target)
    assert _tree(tmp_path) == before


@pytest.mark.parametrize("version", ["v5.5.5", "v5.5.6"])
@pytest.mark.parametrize("changed_index", [0, 1])
def test_both_source_hashes_are_validated_before_staging(
    tmp_path: Path,
    patcher: ModuleType,
    monkeypatch: pytest.MonkeyPatch,
    version: str,
    changed_index: int,
) -> None:
    """An unknown second owner cannot leave the first owner patched."""
    _framework(tmp_path, patcher, version)
    target = tmp_path / patcher.SOURCES[changed_index].path
    target.write_bytes(target.read_bytes() + b"\n/* unreviewed owner change */\n")
    before = _tree(tmp_path)

    def refuse_staging(*args: object) -> Path:
        raise AssertionError("all source validation must precede staging")

    monkeypatch.setattr(patcher, "_stage_bytes", refuse_staging)
    with pytest.raises(RuntimeError, match="PIE source changed"):
        patcher.patch_framework(tmp_path, "esp32p4")
    assert _tree(tmp_path) == before


@pytest.mark.parametrize("missing_index", [0, 1])
def test_missing_source_is_unchanged(
    tmp_path: Path, patcher: ModuleType, missing_index: int
) -> None:
    _framework(tmp_path, patcher)
    (tmp_path / patcher.SOURCES[missing_index].path).unlink()
    before = _tree(tmp_path)
    with pytest.raises(FileNotFoundError):
        patcher.patch_framework(tmp_path, "esp32p4")
    assert _tree(tmp_path) == before


@pytest.mark.parametrize("guarded_index", [0, 1])
def test_known_partial_installation_is_completed(
    tmp_path: Path, patcher: ModuleType, guarded_index: int
) -> None:
    """A known guarded owner may coexist with a pristine owner after interruption."""
    _framework(tmp_path, patcher)
    source = patcher.SOURCES[guarded_index]
    target = tmp_path / source.path
    already_guarded = patcher.guarded_source(target.read_bytes(), source)
    target.write_bytes(already_guarded)
    assert patcher.patch_framework(tmp_path, "esp32p4").startswith("installed guarded")
    assert target.read_bytes() == already_guarded
    for source in patcher.SOURCES:
        assert (
            hashlib.sha256((tmp_path / source.path).read_bytes()).hexdigest()
            == source.guarded_sha256
        )


@pytest.mark.parametrize("changed_index", [0, 1])
def test_unconditional_fixed_source_is_not_silently_reinterpreted(
    tmp_path: Path, patcher: ModuleType, changed_index: int
) -> None:
    """The fixed macro alone does not preserve the required disabled behavior."""
    _framework(tmp_path, patcher)
    source = patcher.SOURCES[changed_index]
    target = tmp_path / source.path
    target.write_bytes(
        target.read_bytes().replace(source.before.encode(), source.enabled.encode(), 1)
    )
    before = _tree(tmp_path)
    with pytest.raises(RuntimeError, match="PIE source changed"):
        patcher.patch_framework(tmp_path, "esp32p4")
    assert _tree(tmp_path) == before


def test_staging_failure_preserves_both_sources(
    tmp_path: Path, patcher: ModuleType, monkeypatch: pytest.MonkeyPatch
) -> None:
    _framework(tmp_path, patcher)
    before = _tree(tmp_path)
    stage_bytes = patcher._stage_bytes
    count = 0

    def fail_second_source_staging(target: Path, data: bytes) -> Path:
        nonlocal count
        count += 1
        if count == 4:
            raise OSError("injected second replacement staging failure")
        return stage_bytes(target, data)

    monkeypatch.setattr(patcher, "_stage_bytes", fail_second_source_staging)
    with pytest.raises(OSError, match="injected second replacement"):
        patcher.patch_framework(tmp_path, "esp32p4")
    assert _tree(tmp_path) == before


def test_second_replace_failure_rolls_back_first_source(
    tmp_path: Path, patcher: ModuleType, monkeypatch: pytest.MonkeyPatch
) -> None:
    _framework(tmp_path, patcher)
    before = _tree(tmp_path)
    second = tmp_path / patcher.SOURCES[1].path
    replace = Path.replace
    failed = False

    def fail_second_replace(source: Path, target: Path) -> Path:
        nonlocal failed
        if Path(target) == second and not failed:
            failed = True
            raise OSError("injected second source replacement failure")
        return replace(source, target)

    monkeypatch.setattr(Path, "replace", fail_second_replace)
    with pytest.raises(OSError, match="injected second source"):
        patcher.patch_framework(tmp_path, "esp32p4")
    assert failed
    assert _tree(tmp_path) == before


def test_change_during_staging_is_not_overwritten(
    tmp_path: Path, patcher: ModuleType, monkeypatch: pytest.MonkeyPatch
) -> None:
    _framework(tmp_path, patcher)
    first = tmp_path / patcher.SOURCES[0].path
    concurrent = first.read_bytes() + b"\n/* concurrent source change */\n"
    stage_bytes = patcher._stage_bytes
    count = 0

    def change_before_commit(target: Path, data: bytes) -> Path:
        nonlocal count
        result = stage_bytes(target, data)
        count += 1
        if count == 4:
            first.write_bytes(concurrent)
        return result

    before = _tree(tmp_path)
    monkeypatch.setattr(patcher, "_stage_bytes", change_before_commit)
    with pytest.raises(RuntimeError, match="PIE source changed while staging"):
        patcher.patch_framework(tmp_path, "esp32p4")
    before[patcher.SOURCES[0].path] = concurrent
    assert _tree(tmp_path) == before
