# ESP-IDF workaround for ESP32-P4 MIPI DSI + DMA2D.
#
# The DPI panel driver's async framebuffer copy path calls esp_cache_msync() for
# every LVGL draw buffer. The optimized MIPI DSI component performs the required
# cache writeback before handing an internal draw buffer to ESP-IDF, so this
# patch skips ESP-IDF's duplicate sync for internal memory buffers.
#
# Remove this once the framework package contains the same guard upstream.

# ruff: noqa: F821
# pylint: disable=undefined-variable
from contextlib import suppress
from pathlib import Path
import re

env = None
with suppress(NameError):
    Import("env")  # type: ignore[name-defined]


def _read_idf_version(framework_dir: Path) -> tuple[int, int, int]:
    version_file = framework_dir / "version.txt"
    if not version_file.exists():
        raise RuntimeError("ESP-IDF version.txt not found; patch needs review")
    raw_version = version_file.read_text(encoding="utf-8").strip()
    match = re.search(r"(\d+)\.(\d+)\.(\d+)", raw_version)
    if match is None:
        raise RuntimeError(f"Unsupported ESP-IDF version string: {raw_version!r}")
    return tuple(int(part) for part in match.groups())


def _patch_idf5(framework_dir: Path) -> None:
    target = Path(framework_dir) / "components" / "esp_lcd" / "dsi" / "esp_lcd_panel_dpi.c"
    text = target.read_text(encoding="utf-8")

    for include in ('#include "esp_memory_utils.h"', '#include "hal/cache_ll.h"', '#include "soc/soc_caps.h"'):
        if include not in text:
            text = text.replace('#include "esp_cache.h"\n', f'#include "esp_cache.h"\n{include}\n')

    helper = """
static bool dpi_panel_skip_draw_buffer_msync(const void *draw_buffer)
{
    if (esp_ptr_internal(draw_buffer)) {
        return true;
    }
#if SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE && defined(CACHE_LL_L2MEM_CACHE_ADDR)
    const void *cache_addr = (const void *)CACHE_LL_L2MEM_CACHE_ADDR(draw_buffer);
    if (esp_ptr_internal(cache_addr)) {
        return true;
    }
#endif
    return false;
}
"""
    if "dpi_panel_skip_draw_buffer_msync" not in text:
        anchor = (
            "static esp_err_t dpi_panel_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, "
            "int x_end, int y_end, const void *color_data);\n"
        )
        if anchor not in text:
            raise RuntimeError("ESP-IDF DSI draw_bitmap declaration not found; patch needs review")
        text = text.replace(anchor, f"{anchor}{helper}", 1)

    old = (
        "        esp_cache_msync(draw_buffer, color_data_size, "
        "ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);"
    )
    old_guard = (
        "        if (!esp_ptr_internal(draw_buffer)) {\n"
        "            esp_cache_msync(draw_buffer, color_data_size, "
        "ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);\n"
        "        }"
    )
    new = (
        "        if (!dpi_panel_skip_draw_buffer_msync(draw_buffer)) {\n"
        "            esp_cache_msync(draw_buffer, color_data_size, "
        "ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);\n"
        "        }"
    )

    if new in text:
        print("MIPI DSI patch: ESP-IDF 5.x DMA2D internal-buffer cache sync guard already present")
        return
    if old_guard in text:
        text = text.replace(old_guard, new)
    elif old in text:
        text = text.replace(old, new)
    else:
        raise RuntimeError("ESP-IDF DSI DMA2D cache sync line not found; patch needs review")

    target.write_text(text, encoding="utf-8")
    print("MIPI DSI patch: applied ESP-IDF 5.x DMA2D internal-buffer cache sync guard")


def _patch_idf6_or_newer(framework_dir: Path) -> None:
    target = framework_dir / "components" / "esp_lcd" / "src" / "esp_async_fbcpy.c"
    text = target.read_text(encoding="utf-8")

    for include in ('#include "esp_memory_utils.h"', '#include "soc/soc_caps.h"'):
        if include not in text:
            text = text.replace('#include "esp_heap_caps.h"\n', f'#include "esp_heap_caps.h"\n{include}\n')

    helper = """
static bool async_fbcpy_skip_src_msync(const void *src_buffer)
{
    if (esp_ptr_internal(src_buffer)) {
        return true;
    }
#if SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE && defined(CACHE_LL_L2MEM_CACHE_ADDR)
    const void *cache_addr = (const void *)CACHE_LL_L2MEM_CACHE_ADDR(src_buffer);
    if (esp_ptr_internal(cache_addr)) {
        return true;
    }
#endif
    return false;
}
"""
    if "async_fbcpy_skip_src_msync" not in text:
        anchor = 'static const char *TAG = "async_fbcpy";\n'
        if anchor not in text:
            raise RuntimeError("ESP-IDF async_fbcpy TAG declaration not found; patch needs review")
        text = text.replace(anchor, f"{anchor}{helper}", 1)

    old = (
        "    ESP_RETURN_ON_ERROR(esp_cache_msync((void *)transaction->src_buffer + copy_head, copy_size, "
        'ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED), TAG, "writeback draw buffer failed");'
    )
    old_guard = (
        "    uint8_t *cache_sync_start = (uint8_t *)transaction->src_buffer + copy_head;\n"
        "    if (!async_fbcpy_skip_src_msync(cache_sync_start)) {\n"
        "        ESP_RETURN_ON_ERROR(esp_cache_msync(cache_sync_start, copy_size, "
        'ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED), TAG, "writeback draw buffer failed");\n'
        "    }"
    )
    new = old_guard

    if new in text:
        print("MIPI DSI patch: ESP-IDF 6.x DMA2D internal-buffer cache sync guard already present")
        return
    if old in text:
        text = text.replace(old, new, 1)
    else:
        raise RuntimeError("ESP-IDF async_fbcpy cache sync line not found; patch needs review")

    target.write_text(text, encoding="utf-8")
    print("MIPI DSI patch: applied ESP-IDF 6.x DMA2D internal-buffer cache sync guard")


def _patch_jpeg_decode_csc(framework_dir: Path) -> None:
    target = framework_dir / "components" / "esp_driver_jpeg" / "jpeg_decode.c"
    if not target.exists():
        return

    text = target.read_text(encoding="utf-8")
    helper = """
static dma2d_csc_rx_option_t jpeg_dec_select_rgb_csc(jpeg_down_sampling_type_t sample_method,
                                                     jpeg_dec_output_format_t output_format,
                                                     jpeg_yuv_rgb_conv_std_t conv_std)
{
    bool bt709 = conv_std == JPEG_YUV_RGB_CONV_STD_BT709;
    bool rgb565 = output_format == JPEG_DECODE_OUT_FORMAT_RGB565;
    switch (sample_method) {
    case JPEG_DOWN_SAMPLING_YUV444:
        return rgb565 ? (bt709 ? DMA2D_CSC_RX_YUV444_TO_RGB565_709 : DMA2D_CSC_RX_YUV444_TO_RGB565_601)
                      : (bt709 ? DMA2D_CSC_RX_YUV444_TO_RGB888_709 : DMA2D_CSC_RX_YUV444_TO_RGB888_601);
    case JPEG_DOWN_SAMPLING_YUV422:
        return rgb565 ? (bt709 ? DMA2D_CSC_RX_YUV422_TO_RGB565_709 : DMA2D_CSC_RX_YUV422_TO_RGB565_601)
                      : (bt709 ? DMA2D_CSC_RX_YUV422_TO_RGB888_709 : DMA2D_CSC_RX_YUV422_TO_RGB888_601);
    case JPEG_DOWN_SAMPLING_YUV420:
        return rgb565 ? (bt709 ? DMA2D_CSC_RX_YUV420_TO_RGB565_709 : DMA2D_CSC_RX_YUV420_TO_RGB565_601)
                      : (bt709 ? DMA2D_CSC_RX_YUV420_TO_RGB888_709 : DMA2D_CSC_RX_YUV420_TO_RGB888_601);
    default:
        return DMA2D_CSC_RX_NONE;
    }
}
"""
    if "jpeg_dec_select_rgb_csc" not in text:
        anchor = "static void jpeg_dec_config_dma_csc(jpeg_decoder_handle_t decoder_engine, dma2d_channel_handle_t rx_chan)\n"
        if anchor not in text:
            raise RuntimeError("ESP-IDF JPEG decode CSC function not found; patch needs review")
        text = text.replace(anchor, f"{helper}\n{anchor}", 1)

    old = """    if (decoder_engine->output_format == JPEG_DECODE_OUT_FORMAT_RGB565) {
        if (decoder_engine->conv_std == JPEG_YUV_RGB_CONV_STD_BT601) {
            rx_csc_option = DMA2D_CSC_RX_YUV420_TO_RGB565_601;
        } else if (decoder_engine->conv_std == JPEG_YUV_RGB_CONV_STD_BT709) {
            rx_csc_option = DMA2D_CSC_RX_YUV420_TO_RGB565_709;
        }
    } else if (decoder_engine->output_format == JPEG_DECODE_OUT_FORMAT_RGB888) {
        if (decoder_engine->conv_std == JPEG_YUV_RGB_CONV_STD_BT601) {
            rx_csc_option = DMA2D_CSC_RX_YUV420_TO_RGB888_601;
        } else if (decoder_engine->conv_std == JPEG_YUV_RGB_CONV_STD_BT709) {
            rx_csc_option = DMA2D_CSC_RX_YUV420_TO_RGB888_709;
        }
    } else if (decoder_engine->output_format == JPEG_DECODE_OUT_FORMAT_YUV444) {"""
    new = """    if (decoder_engine->output_format == JPEG_DECODE_OUT_FORMAT_RGB565 ||
        decoder_engine->output_format == JPEG_DECODE_OUT_FORMAT_RGB888) {
        rx_csc_option = jpeg_dec_select_rgb_csc(decoder_engine->sample_method, decoder_engine->output_format,
                                                decoder_engine->conv_std);
    } else if (decoder_engine->output_format == JPEG_DECODE_OUT_FORMAT_YUV444) {"""
    if new in text:
        print("MIPI DSI patch: ESP-IDF JPEG decode RGB CSC selection already present")
        return
    if old not in text:
        raise RuntimeError("ESP-IDF JPEG decode RGB CSC block not found; patch needs review")

    text = text.replace(old, new, 1)
    target.write_text(text, encoding="utf-8")
    print("MIPI DSI patch: applied ESP-IDF JPEG decode RGB CSC selection")


def _patch_dma2d_yuv2rgb_full_range(framework_dir: Path) -> None:
    target = framework_dir / "components" / "esp_hal_dma" / "include" / "hal" / "dma2d_types.h"
    text = target.read_text(encoding="utf-8")

    limited_bt601 = """#define DMA2D_COLOR_SPACE_CONV_PARAM_YUV2RGB_BT601 \\
{                                                  \\
    { 298,     0,   409,  -56906},                 \\
    { 298,  -100,  -208,   34707},                 \\
    { 298,   516,     0,  -70836},                 \\
}"""
    full_bt601 = """#define DMA2D_COLOR_SPACE_CONV_PARAM_YUV2RGB_BT601 \\
{                                                  \\
    { 256,     0,   359,  -45952},                 \\
    { 256,   -88,  -183,   34688},                 \\
    { 256,   454,     0,  -58112},                 \\
}"""

    limited_bt709 = """#define DMA2D_COLOR_SPACE_CONV_PARAM_YUV2RGB_BT709 \\
{                                                  \\
    { 298,     0,   459,  -63367},                 \\
    { 298,   -55,  -136,   19681},                 \\
    { 298,   541,     0,  -73918},                 \\
}"""
    full_bt709 = """#define DMA2D_COLOR_SPACE_CONV_PARAM_YUV2RGB_BT709 \\
{                                                  \\
    { 256,     0,   403,  -51584},                 \\
    { 256,   -48,  -120,   21504},                 \\
    { 256,   475,     0,  -60800},                 \\
}"""

    changed = False
    if full_bt601 not in text:
        if limited_bt601 not in text:
            raise RuntimeError("ESP-IDF DMA2D BT.601 YUV->RGB matrix not found; patch needs review")
        text = text.replace(limited_bt601, full_bt601, 1)
        changed = True
    if full_bt709 not in text:
        if limited_bt709 not in text:
            raise RuntimeError("ESP-IDF DMA2D BT.709 YUV->RGB matrix not found; patch needs review")
        text = text.replace(limited_bt709, full_bt709, 1)
        changed = True

    if changed:
        target.write_text(text, encoding="utf-8")
        print("MIPI DSI patch: applied ESP-IDF DMA2D full-range YUV->RGB matrices")
    else:
        print("MIPI DSI patch: ESP-IDF DMA2D full-range YUV->RGB matrices already present")


def main() -> None:
    framework_dir = env.PioPlatform().get_package_dir("framework-espidf")
    if not framework_dir:
        raise RuntimeError("framework-espidf package directory not found")

    framework_path = Path(framework_dir)
    idf_version = _read_idf_version(framework_path)
    print(f"MIPI DSI patch: detected ESP-IDF {idf_version[0]}.{idf_version[1]}.{idf_version[2]}")
    if idf_version[0] == 5:
        _patch_idf5(framework_path)
    elif idf_version[0] >= 6:
        _patch_idf6_or_newer(framework_path)
    else:
        raise RuntimeError(f"Unsupported ESP-IDF major version {idf_version[0]}; patch needs review")
    _patch_jpeg_decode_csc(framework_path)
    _patch_dma2d_yuv2rgb_full_range(framework_path)


if env is not None:
    main()
