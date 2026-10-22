"""Preserve optional RGB888 DIRECT rendering and style transitions on LVGL 9.6."""

from __future__ import annotations

from esphome.components.lvgl import defines as df
from esphome.core import CORE


def test_transitions_expand_style_shortcuts_and_preserve_zero_duration(
    generate_main, component_config_path
) -> None:
    main_cpp = generate_main(component_config_path("style_transitions.yaml"))
    assert (
        "static lv_style_transition_dsc_t transition_obj_main_default_0_transition;"
        in main_cpp
    )
    assert "LV_STYLE_BG_COLOR" in main_cpp
    for side in ("TOP", "BOTTOM", "LEFT", "RIGHT"):
        assert f"LV_STYLE_PAD_{side}" in main_cpp
    assert "LV_STYLE_PAD_ALL" not in main_cpp
    assert "lv_anim_path_ease_out, 90, 10, nullptr" in main_cpp
    assert "lv_anim_path_ease_in_out, 0, 0, nullptr" in main_cpp
    assert "&transition_obj_main_pressed_0_transition, LV_STATE_PRESSED" in main_cpp


def test_rgb888_direct_uses_packed_color_format_with_full_refresh(
    generate_main, component_config_path
) -> None:
    main_cpp = generate_main(component_config_path("direct_rgb888.yaml"))
    defines = df.get_defines()
    assert defines["LV_COLOR_FORMAT_DEFAULT"] == "LV_COLOR_FORMAT_RGB888"
    assert defines["LV_DRAW_SW_SUPPORT_RGB888"] == "1"
    # The optional direct presenter still uses semantic depth 32 for packed RGB888.
    assert defines["LV_COLOR_DEPTH"] == "32"
    assert defines["LV_USE_PPA"] == "0"
    feature_defines = {define.name for define in CORE.defines}
    assert "USE_LVGL_PPA_IMG" in feature_defines
    assert "LV_USE_PPA_IMG" not in feature_defines
    assert "lvgl::LvglComponent({direct_display}, 1, true, true," in main_cpp
    assert "set_color_mode" not in main_cpp


def test_lottie_keeps_native_and_opaque_handoff_formats(
    generate_main, component_config_path
) -> None:
    generate_main(component_config_path("lottie_formats.yaml"))
    defines = df.get_defines()
    for color_format in ("ARGB8888", "ARGB8888_PREMULTIPLIED", "RGB888", "XRGB8888"):
        assert defines[f"LV_DRAW_SW_SUPPORT_{color_format}"] == "1"


def test_lottie_only_formats_remain_pruned_when_unused(
    generate_main, component_config_path
) -> None:
    generate_main(component_config_path("lv_conf_defaults.yaml"))
    defines = df.get_defines()
    assert "LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED" not in defines
    assert "LV_DRAW_SW_SUPPORT_XRGB8888" not in defines
