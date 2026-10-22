"""Tests for LVGL navigation configuration."""

from __future__ import annotations

import pytest
from voluptuous import MultipleInvalid

from esphome.components.lvgl.navigation import HOME_SCHEMA


def test_home_pages_are_supported() -> None:
    """A list of LVGL pages remains a valid home navigation model."""
    config = HOME_SCHEMA({"pages": ["home_one", "home_two"]})

    assert [page.id for page in config["pages"]] == ["home_one", "home_two"]
    assert config["widgets"] == []
    assert "page" not in config


def test_home_widgets_are_supported() -> None:
    """Full-screen widgets can share one host page."""
    config = HOME_SCHEMA(
        {
            "page": "home",
            "widgets": ["home_one", "home_two"],
        }
    )

    assert config["page"].id == "home"
    assert [widget.id for widget in config["widgets"]] == ["home_one", "home_two"]
    assert config["pages"] == []


@pytest.mark.parametrize(
    "config",
    [
        {},
        {"pages": ["home_one"], "page": "home", "widgets": ["home_two"]},
    ],
)
def test_home_requires_exactly_one_navigation_model(config: dict) -> None:
    """Page and widget navigation cannot be omitted or combined."""
    with pytest.raises(
        MultipleInvalid,
        match=r"either 'pages' or the 'page' \+ 'widgets' pair",
    ):
        HOME_SCHEMA(config)


def test_home_widgets_require_host_page() -> None:
    """Widget-backed navigation needs the page that owns the widgets."""
    with pytest.raises(MultipleInvalid, match="requires a host 'page'"):
        HOME_SCHEMA({"widgets": ["home_one"]})
