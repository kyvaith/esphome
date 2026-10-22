"""Retain current SDK JPEG cleanup while adding the optional display diagnostics."""

import hashlib
import importlib.util
from pathlib import Path
from types import ModuleType

import pytest

PATCH = (
    Path(__file__).resolve().parents[2]
    / "esphome/components/mipi_dsi/patch_espidf_dsi_dma2d.py"
)
FIXTURES = Path(__file__).parent / "fixtures/mipi_dsi_sdk"


def _load() -> ModuleType:
    spec = importlib.util.spec_from_file_location("mipi_sdk_patch", PATCH)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _jpeg_sdk(tmp_path: Path, version: str) -> Path:
    (tmp_path / "version.txt").write_text("v" + version, encoding="utf-8")
    source = tmp_path / "components/esp_driver_jpeg/jpeg_decode.c"
    source.parent.mkdir(parents=True)
    source.write_bytes(
        (FIXTURES / f"jpeg_decode_{version.replace('.', '')}.c").read_bytes()
    )
    return source


def test_import_does_not_run_main_with_process_arguments(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """Importing under pytest or another host application never treats its args as an SDK path."""
    monkeypatch.setattr(
        "sys.argv", ["host", "nonexistent-sdk-path", "--unrelated-option"]
    )
    module = _load()
    assert callable(module.main)
    assert module.env is None


@pytest.mark.parametrize("version", ["5.5.5", "5.5.6"])
def test_reviewed_decoder_patch_is_idempotent(tmp_path: Path, version: str) -> None:
    """Both reviewed SDK branches keep their real owner while configuring a repeatable patch."""
    module = _load()
    source = _jpeg_sdk(tmp_path, version)
    module._patch_jpeg_decode_csc(tmp_path)
    module._patch_jpeg_decode_dma2d_burst(tmp_path)
    first = source.read_bytes()
    module._patch_jpeg_decode_csc(tmp_path)
    module._patch_jpeg_decode_dma2d_burst(tmp_path)
    assert source.read_bytes() == first
    if version == "5.5.6":
        text = source.read_text(encoding="utf-8")
        assert hashlib.sha256(text.encode()).hexdigest() == (
            "1bcc2689b87c89705ed2b737b93a051d70900886252a30e07f130cbd58cbdd4a"
        )
        assert text.count("dma2d_dequeue(decoder_engine->dma2d_group_handle,") == 1
        assert (
            text.count(
                "if (dma2d_force_end(decoder_engine->trans_desc, &need_yield) != ESP_OK)"
            )
            == 1
        )
        assert (
            '"failed to end the transaction, it is neither pending nor in-flight"'
            in text
        )
        # Preserve upstream's newer allocation and validation improvements too.
        assert "MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT" in text
        assert (
            "jpeg_check_dma2d_buffer(bit_stream) && jpeg_check_dma2d_buffer(decode_outbuf)"
            in text
        )


def test_unreviewed_decoder_source_is_refused_without_writing(tmp_path: Path) -> None:
    """The new exact version does not authorize a changed cleanup or unrelated owner bytes."""
    module = _load()
    source = _jpeg_sdk(tmp_path, "5.5.6")
    module._patch_jpeg_decode_csc(tmp_path)
    changed = source.read_bytes() + b"\n/* Unreviewed modification. */\n"
    source.write_bytes(changed)
    with pytest.raises(RuntimeError, match="5.5.6 JPEG decode source changed"):
        module._patch_jpeg_decode_dma2d_burst(tmp_path)
    assert source.read_bytes() == changed


def test_new_cleanup_is_not_blindly_accepted_on_another_version(tmp_path: Path) -> None:
    """The new owner variant is admitted only for the exact reviewed SDK version."""
    module = _load()
    source = _jpeg_sdk(tmp_path, "5.5.6")
    module._patch_jpeg_decode_csc(tmp_path)
    before = source.read_bytes()
    (tmp_path / "version.txt").write_text("v5.5.7", encoding="utf-8")
    with pytest.raises(RuntimeError, match="error cleanup block not found"):
        module._patch_jpeg_decode_dma2d_burst(tmp_path)
    assert source.read_bytes() == before
