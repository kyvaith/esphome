"""Exercise the guarded installer with tiny SDK fixtures, never the installed SDK."""

import hashlib
import importlib.util
import json
from pathlib import Path
from types import ModuleType

import pytest

from esphome.components import esp32


def _digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _state(directory: Path) -> dict[str, bytes]:
    return {
        path.relative_to(directory).as_posix(): path.read_bytes()
        for path in directory.rglob("*")
        if path.is_file()
    }


@pytest.fixture
def framework(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> tuple[ModuleType, Path, dict]:
    """Reuse the real installer with two deterministic, hash-guarded source owners."""
    source = (
        Path(esp32.__file__).parent
        / "fast_heap_query_framework_patch/patch_fast_heap_query.py"
    )
    spec = importlib.util.spec_from_file_location("fast_heap_query_patch_test", source)
    assert spec is not None and spec.loader is not None
    patcher = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(patcher)
    sdk = tmp_path / "sdk"
    sdk.mkdir()
    (sdk / "version.txt").write_text("v5.5.6\n", encoding="utf-8")
    package = tmp_path / "patch-package"
    package.mkdir()
    patches = {}
    for name in ("first", "last"):
        path = f"components/heap/{name}.c"
        before = f"/* {name} source */\nint {name} = 0;\n"
        after = before.replace(" = 0", " = 1")
        target = sdk / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(before.encode())
        patches[path] = {
            "before": _digest(before.encode()),
            "after": _digest(after.encode()),
            "replacements": [[" = 0", " = 1"]],
        }
    (package / "patches.json").write_text(json.dumps(patches), encoding="utf-8")
    monkeypatch.setattr(patcher, "__file__", str(package / source.name))
    return patcher, sdk, patches


def test_fast_heap_patch_preflight_apply_and_idempotence(framework: tuple) -> None:
    """Checking is read-only, applying is exact, and repeated configuration is inert."""
    patcher, sdk, patches = framework
    before = _state(sdk)
    patcher.patch_framework(sdk, check_only=True)
    assert _state(sdk) == before
    patcher.patch_framework(sdk)
    for name, entry in patches.items():
        assert _digest((sdk / name).read_bytes()) == entry["after"]
    after = _state(sdk)
    assert set(after) == set(before)
    assert patcher.patch_framework(sdk) == "already guarded"
    assert _state(sdk) == after


def test_fast_heap_patch_accepts_mixed_verified_images(framework: tuple) -> None:
    """A retry can finish a previous install containing known pre- and postimages."""
    patcher, sdk, patches = framework
    first = sdk / next(iter(patches))
    first.write_bytes(first.read_bytes().replace(b" = 0", b" = 1"))
    patcher.patch_framework(sdk)
    assert all(
        _digest((sdk / n).read_bytes()) == p["after"] for n, p in patches.items()
    )


def test_fast_heap_patch_checks_all_sources_before_writing(framework: tuple) -> None:
    """A mismatch in the final file must leave even the first valid file untouched."""
    patcher, sdk, patches = framework
    last = sdk / list(patches)[-1]
    last.write_bytes(last.read_bytes() + b"/* unreviewed change */\n")
    before = _state(sdk)
    with pytest.raises(RuntimeError, match="SDK source changed"):
        patcher.patch_framework(sdk)
    assert _state(sdk) == before


@pytest.mark.parametrize("version", ["v5.5.5", "v5.5.7", "v5.5.6-dirty"])
def test_fast_heap_patch_rejects_unknown_version(
    framework: tuple, version: str
) -> None:
    """Matching owner hashes do not authorize an unreviewed SDK release."""
    patcher, sdk, _ = framework
    (sdk / "version.txt").write_text(version, encoding="utf-8")
    before = _state(sdk)
    with pytest.raises(RuntimeError, match="requires reviewed ESP-IDF 5.5.6"):
        patcher.patch_framework(sdk)
    assert _state(sdk) == before


def test_fast_heap_patch_rejects_invalid_postimage(framework: tuple) -> None:
    """The computed final bytes must match their reviewed output hash before writes."""
    patcher, sdk, patches = framework
    patches[list(patches)[-1]]["after"] = "0" * 64
    Path(patcher.__file__).with_name("patches.json").write_text(
        json.dumps(patches), encoding="utf-8"
    )
    before = _state(sdk)
    with pytest.raises(RuntimeError, match="SDK postimage mismatch"):
        patcher.patch_framework(sdk)
    assert _state(sdk) == before


def test_fast_heap_patch_honors_existing_installer_lock(framework: tuple) -> None:
    """A concurrent installer cannot rewrite files or remove another owner's lock."""
    patcher, sdk, _ = framework
    (sdk / ".esphome-fast-heap-query.lock").write_text("other owner", encoding="utf-8")
    before = _state(sdk)
    with pytest.raises(FileExistsError):
        patcher.patch_framework(sdk)
    assert _state(sdk) == before


def test_fast_heap_patch_rejects_path_escape(framework: tuple) -> None:
    """A manifest entry cannot write outside the explicitly selected SDK root."""
    patcher, sdk, patches = framework
    outside = sdk.parent / "outside.c"
    outside.write_bytes(b"outside must remain unchanged")
    patches["../outside.c"] = next(iter(patches.values()))
    Path(patcher.__file__).with_name("patches.json").write_text(
        json.dumps(patches), encoding="utf-8"
    )
    before = _state(sdk.parent)
    with pytest.raises(RuntimeError, match="outside the framework"):
        patcher.patch_framework(sdk)
    assert _state(sdk.parent) == before
