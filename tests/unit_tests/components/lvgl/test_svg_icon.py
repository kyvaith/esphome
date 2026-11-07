"""Validate the deliberately bounded SVG source contract before code generation."""

import pytest

from esphome import config_validation as cv
from esphome.components.lvgl.widgets.svg_icon import parse_path, read_svg


def svg_file(tmp_path, body, attrs='viewBox="0 0 100 100" fill="white"'):
    path = tmp_path / "icon.svg"
    path.write_text(f'<svg xmlns="http://www.w3.org/2000/svg" {attrs}>{body}</svg>', encoding="utf-8")
    return str(path)


def test_preserves_curves_and_implicit_line_pairs():
    assert parse_path("M 1 2 3 4 Q 5 6 7 8 C 9 10 11 12 13 14 Z") == [
        ("M", (1.0, 2.0)), ("L", (3.0, 4.0)), ("Q", (5.0, 6.0, 7.0, 8.0)),
        ("C", (9.0, 10.0, 11.0, 12.0, 13.0, 14.0)), ("Z", ()),
    ]


def test_multiple_closed_contours_are_retained():
    assert len(parse_path("M0 0L10 0L10 10Z M20 20L30 20L30 30Z")) == 8


@pytest.mark.parametrize("path", [
    "", "L0 0Z", "M0 0L1 1", "M0 0M2 2Z", "M0 0L1Z", "M0 0Z 1 2",
    "m0 0l1 1z", "M0 0A5 5 0 0 0 1 1Z", "M0 0LNaN 1Z", "M0 0L1e100 1Z",
    "M0 0L0 0Z ;", "Z",
])
def test_malformed_or_unsupported_path_is_rejected(path):
    with pytest.raises(cv.Invalid):
        parse_path(path)


def test_reads_monochrome_paths_with_nonzero_viewbox(tmp_path):
    path = svg_file(tmp_path, '<title>Fan</title><path d="M10 20L20 20L20 30Z"/>',
                    'viewBox="10 20 100 200" width="100" height="200" fill="white"')
    box, commands = read_svg(path)
    assert box == (10, 20, 100, 200)
    assert len(commands) == 4


@pytest.mark.parametrize("body", [
    '<image href="https://example.com/a.png"/>', '<g><path d="M0 0L1 1L2 0Z"/></g>',
    '<path d="M0 0L1 1L2 0Z" transform="rotate(30)"/>',
    '<path d="M0 0L1 1L2 0Z" stroke="white"/>',
    '<path d="M0 0L1 1L2 0Z" fill="black"/>',
    '<path d="M0 0L1 1L2 0Z" fill-rule="evenodd"/>',
    '<path d="M0 0L1 1L2 0Z"><animate attributeName="fill"/></path>',
])
def test_features_not_implemented_are_rejected(tmp_path, body):
    with pytest.raises(cv.Invalid):
        read_svg(svg_file(tmp_path, body))


@pytest.mark.parametrize("attrs", [
    'viewBox="0 0 0 100"', 'viewBox="0 0 -1 100"', 'viewBox="0 0 nan 100"',
    'viewBox="0 0 100 100" fill="url(#paint)"', 'viewBox="0 0 100 100" opacity="0.5"',
])
def test_invalid_root_is_rejected(tmp_path, attrs):
    with pytest.raises(cv.Invalid):
        read_svg(svg_file(tmp_path, '<path d="M0 0L1 1L2 0Z"/>', attrs))


def test_rejects_xml_entities(tmp_path):
    path = tmp_path / "unsafe.svg"
    path.write_text('<!DOCTYPE svg [<!ENTITY x "value">]><svg viewBox="0 0 1 1"/>', encoding="utf-8")
    with pytest.raises(cv.Invalid, match="entities"):
        read_svg(str(path))


def test_enforces_whole_asset_command_budget(tmp_path):
    with pytest.raises(cv.Invalid, match="256"):
        read_svg(svg_file(tmp_path, '<path d="M0 0L1 1L2 0Z"/>' * 65))


def test_enforces_file_size_budget(tmp_path):
    path = tmp_path / "large.svg"
    path.write_text(" " * 65537, encoding="utf-8")
    with pytest.raises(cv.Invalid, match="64 KiB"):
        read_svg(str(path))
