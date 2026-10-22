"""Compatibility checks for the optional native LVGL source fixes."""

from __future__ import annotations

import pytest

from esphome.components.lvgl.lvgl_build_filter import (
    patch_sw_rgb888_artwork_throttle_source,
)

_BLEND_SOURCE = """#include "lv_draw_sw_blend_to_rgb888.h"
#if LV_USE_DRAW_SW
#include "lv_draw_sw_blend_private.h"

static void LV_ATTRIBUTE_FAST_MEM rgb565_image_blend(lv_draw_sw_blend_image_dsc_t * dsc, uint32_t dest_px_size)
{
    int32_t mask_stride = dsc->mask_stride;
    for(int32_t y = 0; y < dsc->dest_h; y++) {
        src_buf_c16 = drawbuf_next_row(src_buf_c16, src_stride);
    }
}
#endif
"""


@pytest.mark.parametrize("partial_patch", [False, True])
def test_rgb888_blend_patch_defines_helpers_before_calls_and_is_idempotent(
    tmp_path, partial_patch: bool
) -> None:
    source = tmp_path / "lv_draw_sw_blend_to_rgb888.c"
    text = _BLEND_SOURCE
    if partial_patch:
        text = text.replace(
            "    int32_t mask_stride = dsc->mask_stride;\n",
            "    int32_t mask_stride = dsc->mask_stride;\n"
            "    const bool esphome_artwork_throttle = "
            "esphome_lvgl_rgb888_artwork_should_throttle(dsc);\n",
        ).replace(
            "src_buf_c16 = drawbuf_next_row(src_buf_c16, src_stride);",
            "src_buf_c16 = drawbuf_next_row(src_buf_c16, src_stride);\n"
            "                    esphome_lvgl_rgb888_artwork_throttle(esphome_artwork_throttle, y);",
        )
    source.write_text(text, encoding="utf-8")

    patch_sw_rgb888_artwork_throttle_source(source)
    patched = source.read_text(encoding="utf-8")
    definition = "static inline bool esphome_lvgl_rgb888_artwork_should_throttle("
    call = "const bool esphome_artwork_throttle = esphome_lvgl_rgb888_artwork_should_throttle(dsc);"
    assert patched.index(definition) < patched.index(call)
    assert patched.count(call) == 1
    assert (
        patched.count("static inline void esphome_lvgl_rgb888_artwork_throttle(") == 1
    )
    assert (
        patched.count(
            "esphome_lvgl_rgb888_artwork_throttle(esphome_artwork_throttle, y);"
        )
        == 1
    )
    first = source.read_bytes()
    patch_sw_rgb888_artwork_throttle_source(source)
    assert source.read_bytes() == first


def test_rgb888_blend_patch_rejects_unknown_source_without_mutation(tmp_path) -> None:
    source = tmp_path / "lv_draw_sw_blend_to_rgb888.c"
    source.write_text(
        _BLEND_SOURCE.replace("lv_draw_sw_blend_private.h", "unknown.h"),
        encoding="utf-8",
    )
    first = source.read_bytes()
    with pytest.raises(RuntimeError, match="Unsupported LVGL RGB888 blend include"):
        patch_sw_rgb888_artwork_throttle_source(source)
    assert source.read_bytes() == first
