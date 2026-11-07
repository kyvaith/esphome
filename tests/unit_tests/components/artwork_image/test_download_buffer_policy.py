"""Encoded input allocation preference is opt-in and reaches the constructor."""

from unittest.mock import AsyncMock, patch

import pytest

from esphome import config_validation as cv
from esphome.components import artwork_image
from esphome.core import CORE


def validated(value=None, preallocate=None):
    CORE.reset()
    config = {
        "id": "test_artwork",
        "url": "http://example.test/image",
        "type": "RGB",
        "format": "JPEG",
    }
    if value is not None:
        config["prefer_external_download_buffer"] = value
    if preallocate is not None:
        config["preallocate_download_buffer"] = preallocate
    return artwork_image.ARTWORK_IMAGE_SCHEMA(config)


def test_default_preserves_internal_preference():
    assert validated()["prefer_external_download_buffer"] is False
    assert validated()["preallocate_download_buffer"] is False


@pytest.mark.parametrize("value", [False, True])
def test_explicit_preference(value):
    assert validated(value)["prefer_external_download_buffer"] is value


@pytest.mark.parametrize("value", ["not-a-boolean", [], {}, 2])
def test_invalid_preference(value):
    with pytest.raises(cv.Invalid):
        validated(value)


@pytest.mark.parametrize("value", [False, True])
def test_explicit_preallocation(value):
    assert validated(preallocate=value)["preallocate_download_buffer"] is value


@pytest.mark.parametrize("value", ["not-a-boolean", [], {}, 2])
def test_invalid_preallocation(value):
    with pytest.raises(cv.Invalid):
        validated(preallocate=value)


@pytest.mark.asyncio
@pytest.mark.parametrize("value", [None, False, True])
@pytest.mark.parametrize("preallocate", [None, False, True])
async def test_preference_is_constructor_argument(value, preallocate):
    config = validated(value, preallocate)
    with (
        patch.object(artwork_image, "cg") as cg,
        patch.object(artwork_image.IMAGE_FORMATS["JPEG"], "actions"),
        patch.object(artwork_image, "add_metadata"),
        patch.object(artwork_image, "lvgl_defines"),
        patch.object(
            artwork_image.automation,
            "build_callback_automations",
            new_callable=AsyncMock,
        ),
    ):
        cg.register_component = AsyncMock()
        cg.register_parented = AsyncMock()
        await artwork_image.to_code(config)
    args = cg.new_Pvariable.call_args.args
    assert len(args) == 12
    assert args[-1] is (preallocate is True)
    assert args[-2] is (value is True)
    assert args[-3] is False  # Original insecure-URL argument remains in place.
    assert args[7] == 65536  # Buffer-size default is unchanged.
