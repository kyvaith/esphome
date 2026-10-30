"""Opt-in format validation keeps the existing bounded RGB565 default."""

import pytest

from esphome import config_validation as cv
from esphome.components.lvgl.snapshot import (
    SCROLL_REGION_SCHEMA,
    _validate_scroll_regions,
)


def region(**values):
    return SCROLL_REGION_SCHEMA(
        {"application": "application", "widget": "root", "backend": "direct", **values}
    )


def test_default_format_preserves_rgb565_and_memory_cap():
    result = region()
    assert result["color_format"] == "RGB565"
    assert result["color_format"].enum_value == "LV_COLOR_FORMAT_RGB565"
    assert result["max_content_size"] == 8_000_000


def test_native_format_is_explicit_and_does_not_raise_memory_cap():
    result = region(color_format="rgb888", max_content_size="4MB")
    assert result["color_format"].enum_value == "LV_COLOR_FORMAT_RGB888"
    config = {
        "backend": "direct",
        "use_display_buffers_for_preparation": False,
        "scroll_regions": [result],
    }
    assert _validate_scroll_regions(config) is config
    assert result["max_content_size"] == 4_000_000


def test_native_format_rejects_unsupported_lvgl_backend():
    config = {
        "backend": "lvgl",
        "use_display_buffers_for_preparation": False,
        "scroll_regions": [region(backend="lvgl", color_format="RGB888")],
    }
    with pytest.raises(cv.Invalid, match="require backend: direct"):
        _validate_scroll_regions(config)


@pytest.mark.parametrize("color_format", ["RGB332", "ARGB8888", "RGB", "", 24])
def test_unsupported_formats_are_rejected(color_format):
    with pytest.raises(cv.Invalid):
        region(color_format=color_format)
