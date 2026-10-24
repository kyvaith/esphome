"""
PlatformIO build filter for LVGL on ESP32.

Excludes platform-specific drivers, draw backends, libraries, and unused
widget source files. This significantly reduces compilation time and binary size.

Used as a PlatformIO extra_scripts middleware, added by ESPHome's LVGL component.

Communication from ESPHome (__init__.py) via build flags:
  -DLVGL_USE_THORVG=1        → compile ThorVG sources
  -DLVGL_WIDGETS_USED="..."  → comma-separated list of used widget/feature names
"""

# ruff: noqa: F821
# pylint: disable=undefined-variable
from contextlib import suppress
import hashlib
from pathlib import Path
import re
import sys

env = None
with suppress(NameError):
    Import("env")  # type: ignore[name-defined]

ATOMIC_SHIM_TEXT = """#pragma once

#if defined(ESP_PLATFORM)
#include "freertos/atomic.h"
#else
#error "This atomic.h shim is intended for ESP-IDF / FreeRTOS builds only."
#endif
"""

PROFILER_NULL_FUNC_PATCHED = 'const char * func = item->func ? item->func : "<null>";'
SW_RGB888_ARTWORK_THROTTLE_MARKER = "esphome_lvgl_rgb888_artwork_should_throttle"

SW_RGB888_ARTWORK_THROTTLE_HELPER = """
#ifndef CONFIG_ESPHOME_LVGL_SW_ARTWORK_DSI_BACKPRESSURE
#define CONFIG_ESPHOME_LVGL_SW_ARTWORK_DSI_BACKPRESSURE 1
#endif

#ifndef CONFIG_ESPHOME_LVGL_SW_ARTWORK_DSI_FIFO_MIN
#define CONFIG_ESPHOME_LVGL_SW_ARTWORK_DSI_FIFO_MIN 896
#endif

#ifndef CONFIG_ESPHOME_LVGL_SW_ARTWORK_DSI_WAIT_US
#define CONFIG_ESPHOME_LVGL_SW_ARTWORK_DSI_WAIT_US 3000
#endif

#ifndef CONFIG_ESPHOME_LVGL_SW_ARTWORK_DSI_ROW_PERIOD
#define CONFIG_ESPHOME_LVGL_SW_ARTWORK_DSI_ROW_PERIOD 4
#endif

bool esphome_artwork_image_buffer_written_by_dma(const void * ptr) __attribute__((weak));
bool esphome_mipi_dsi_wait_fifo_margin(uint32_t min_depth, uint32_t timeout_us) __attribute__((weak));

static inline bool esphome_lvgl_rgb888_artwork_should_throttle(const lv_draw_sw_blend_image_dsc_t * dsc)
{
#if CONFIG_ESPHOME_LVGL_SW_ARTWORK_DSI_BACKPRESSURE
    return esphome_artwork_image_buffer_written_by_dma != NULL &&
           esphome_mipi_dsi_wait_fifo_margin != NULL &&
           dsc != NULL &&
           dsc->src_buf != NULL &&
           dsc->dest_w >= 320 &&
           dsc->dest_h >= 320 &&
           esphome_artwork_image_buffer_written_by_dma(dsc->src_buf);
#else
    LV_UNUSED(dsc);
    return false;
#endif
}

static inline void esphome_lvgl_rgb888_artwork_throttle(bool active, int32_t y)
{
#if CONFIG_ESPHOME_LVGL_SW_ARTWORK_DSI_BACKPRESSURE
    if(!active || CONFIG_ESPHOME_LVGL_SW_ARTWORK_DSI_ROW_PERIOD <= 0) return;
    if((y % CONFIG_ESPHOME_LVGL_SW_ARTWORK_DSI_ROW_PERIOD) == 0) {
        esphome_mipi_dsi_wait_fifo_margin(CONFIG_ESPHOME_LVGL_SW_ARTWORK_DSI_FIFO_MIN,
                                          CONFIG_ESPHOME_LVGL_SW_ARTWORK_DSI_WAIT_US);
    }
#else
    LV_UNUSED(active);
    LV_UNUSED(y);
#endif
}
"""

THORVG_RENDERER_MARKER = "esphome_thorvg_psram_compositor_buffers"
THORVG_RENDERER_HELPER = """
// esphome_thorvg_psram_compositor_buffers
// ThorVG compositor surfaces can be much larger than LVGL's TLSF pool.  On
// ESP targets keep these temporary pixel buffers in PSRAM instead of making
// the small LVGL pool fail after the UI has reserved its buffers.
#if defined(ESP_PLATFORM)
    #include "esp_heap_caps.h"
#endif

static inline void* _allocCompositorBuffer(size_t bytes)
{
#if defined(ESP_PLATFORM)
    return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return lv_malloc(bytes);
#endif
}

static inline void _freeCompositorBuffer(void* buffer)
{
#if defined(ESP_PLATFORM)
    heap_caps_free(buffer);
#else
    lv_free(buffer);
#endif
}
"""

THORVG_RASTER_MARKER = "esphome_thorvg_rle_bounds_guard"
THORVG_RASTER_HELPER = """
// esphome_thorvg_rle_bounds_guard
// RLE data can outlive a transformed shape.  Clip every span before it is
// used as a surface offset so a stale span cannot corrupt the framebuffer.
static inline bool _validRle(const SwRle* rle)
{
    return rle && rle->spans && rle->size > 0 && rle->size <= rle->alloc;
}

static inline bool _clipRleSpan(const SwSurface* surface, const SwSpan* span,
                                uint32_t& x, uint32_t& len)
{
    if (!surface || !span || span->len == 0 || span->y >= surface->h || span->x >= surface->w) {
        return false;
    }

    x = span->x;
    const uint32_t available = surface->w - x;
    len = (span->len < available) ? span->len : available;
    return len > 0;
}
"""


PPA_CACHE_SYNC_HELPER = """
extern bool esphome_lvgl_ppa_skip_cache_msync(const void *buffer, size_t size,
                                              int flags) __attribute__((weak));

static esp_err_t ppa_cache_msync_external_window(uint32_t window_start, uint32_t window_len,
                                                 uint32_t alignment, int flags, bool preserve_dirty)
{
    if (window_start == 0 || window_len == 0 || alignment == 0) {
        return ESP_OK;
    }

    // Let an owner of a DMA-only buffer suppress cache maintenance using the
    // exact PPA window.  Checking after alignment can include bytes belonging
    // to a neighbouring allocation when the buffer starts mid cache-line.
    if (esphome_lvgl_ppa_skip_cache_msync != NULL &&
        esphome_lvgl_ppa_skip_cache_msync((const void *)(uintptr_t)window_start,
                                          window_len, flags)) {
        return ESP_OK;
    }

    uintptr_t sync_start = PPA_ALIGN_DOWN((uintptr_t)window_start, alignment);
    uintptr_t sync_end = PPA_ALIGN_UP((uintptr_t)window_start + window_len, alignment);
    if (sync_end <= sync_start) {
        return ESP_OK;
    }

    if (!esp_ptr_external_ram((const void *)sync_start) ||
        !esp_ptr_external_ram((const void *)(sync_end - 1U))) {
        return ESP_OK;
    }

    int sync_flags = flags & ~ESP_CACHE_MSYNC_FLAG_UNALIGNED;
    if (preserve_dirty &&
        (sync_flags & (ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE)) != 0) {
        esp_err_t err = esp_cache_msync((void *)sync_start, sync_end - sync_start,
                                        ESP_CACHE_MSYNC_FLAG_DIR_C2M);
        if (err != ESP_OK) {
            return err;
        }
    }

    return esp_cache_msync((void *)sync_start, sync_end - sync_start, sync_flags);
}
"""

PPA_CACHE_REGION_START = (
    "    // Write back and invalidate necessary data "
    "(note that the window content is not continuous in the buffer)\n"
)
PPA_CACHE_REGION_END = "\n    esp_err_t ret = ESP_OK;"
PPA_CACHE_PREIMAGES = {
    (5, 5, 5): {
        "ppa_srm.c": "4d1cd887567a1f7cca3ec1a0f7272d41da9f4f350b26995adeb461ec68cb6a65",
        "ppa_blend.c": "c10bc188d7900334fa51372c7ac68a6a4cdae545f7efde20aa210f50d8d18095",
        "ppa_fill.c": "ae25335285c724e0599a5aaa3ad9437b641e4eb55a6ac4d12a45adfde5963223",
    },
    (5, 5, 6): {
        "ppa_srm.c": "c3dbb79592dc5effd6ea49e9c9e09248c6fe8318455b5463d1a86c50b0c3ef9a",
        "ppa_blend.c": "859c14a92cdfae5e71ab0882edaf44c838c6ab9fa7762f6aabd758fc78e0841d",
        "ppa_fill.c": "b87f08a49ba091a273b7aa9a84e0f04b028b365f77f22e9bd4bd7e60c855ac52",
    },
}
PPA_DESCRIPTOR_CACHE_CALLS = {
    "ppa_srm.c": (
        "esp_cache_msync((void *)srm_engine->dma_tx_desc, platform->dma_desc_mem_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);",
        "esp_cache_msync((void *)srm_engine->dma_rx_desc, platform->dma_desc_mem_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);",
    ),
    "ppa_blend.c": (
        "esp_cache_msync((void *)blend_engine->dma_tx_bg_desc, platform->dma_desc_mem_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);",
        "esp_cache_msync((void *)blend_engine->dma_tx_fg_desc, platform->dma_desc_mem_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);",
        "esp_cache_msync((void *)blend_engine->dma_rx_desc, platform->dma_desc_mem_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);",
    ),
    "ppa_fill.c": (
        "esp_cache_msync((void *)blend_engine->dma_rx_desc, platform->dma_desc_mem_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);",
    ),
}


def replace_espidf_ppa_cache_sync_helper(text):
    """Canonicalize the reviewed helper without accumulating blank lines."""
    definition = "static esp_err_t ppa_cache_msync_external_window("
    tags = list(
        re.finditer(r'static const char \*TAG = "ppa_(?:srm|blend|fill)";', text)
    )
    if len(tags) != 1 or text.count(definition) > 1:
        raise RuntimeError(
            "Unsupported ESP-IDF PPA cache helper layout; patch needs review"
        )
    tag_start = tags[0].start()
    if definition not in text:
        if "esphome_lvgl_ppa_skip_cache_msync" in text:
            raise RuntimeError(
                "Incomplete ESP-IDF PPA cache helper; patch needs review"
            )
        prefix = text[:tag_start]
    else:
        helper_start = text.index(definition)
        declarations = list(
            re.finditer(
                r"extern bool esphome_lvgl_ppa_skip_cache_msync\([^;]+?"
                r"__attribute__\(\(weak\)\);",
                text,
                re.DOTALL,
            )
        )
        expected_body = PPA_CACHE_SYNC_HELPER[
            PPA_CACHE_SYNC_HELPER.index(definition) :
        ].strip()
        if (
            len(declarations) != 1
            or declarations[0].end() > helper_start
            or text[declarations[0].end() : helper_start].strip()
            or text[helper_start:tag_start].strip() != expected_body
        ):
            raise RuntimeError("ESP-IDF PPA cache helper changed; patch needs review")
        prefix = text[: declarations[0].start()]
    return (
        prefix.rstrip()
        + "\n\n"
        + PPA_CACHE_SYNC_HELPER.strip()
        + "\n\n"
        + text[tag_start:]
    )


def write_atomic_shim(shim):
    shim = Path(shim)
    shim.parent.mkdir(parents=True, exist_ok=True)
    if (
        not shim.exists()
        or shim.read_text(encoding="utf-8", errors="ignore") != ATOMIC_SHIM_TEXT
    ):
        shim.write_text(ATOMIC_SHIM_TEXT, encoding="utf-8")
        print("Created LVGL osal atomic.h shim:", shim)


def create_piolibdeps_atomic_shim():
    if env is None:
        return
    libdeps_dir = env.subst("$PROJECT_LIBDEPS_DIR")
    pioenv = env.subst("$PIOENV")
    if libdeps_dir and pioenv:
        write_atomic_shim(
            Path(libdeps_dir) / pioenv / "lvgl" / "src" / "osal" / "atomic.h"
        )


def patch_piolibdeps_lvgl_sources():
    if env is None:
        return
    libdeps_dir = env.subst("$PROJECT_LIBDEPS_DIR")
    pioenv = env.subst("$PIOENV")
    if not libdeps_dir or not pioenv:
        return
    patch_component_sources(Path(libdeps_dir) / pioenv / "lvgl")


def patch_ppa_blocking_user_data_source(src):
    """Keep LVGL's synchronous PPA image blend compatible with ESP-IDF."""
    src = Path(src)
    text = src.read_text(encoding="utf-8")
    pattern = (
        r"(\.mode\s*=\s*PPA_TRANS_MODE_BLOCKING,\s*\n\s*"
        r"\.user_data\s*=\s*)u(\s*,)"
    )
    patched, count = re.subn(pattern, r"\g<1>NULL\2", text)
    if count == 1:
        # LVGL registers no PPA completion callback. Blocking callers consume
        # completion on return, so the draw-unit pointer was never used here.
        src.write_text(patched, encoding="utf-8")
        print("Patched LVGL blocking PPA image user_data:", src)
        return
    null_pattern = pattern.replace("u(\\s*,)", r"(?:NULL|0)(\s*,)")
    if count == 0 and (
        re.search(null_pattern, text)
        or (".user_data" not in text and "PPA_TRANS_MODE_BLOCKING" in text)
    ):
        return
    raise RuntimeError(f"Unsupported LVGL blocking PPA image source: {src}")


def patch_profiler_builtin_source(src):
    src = Path(src)
    if not src.exists() and env is not None:
        libdeps_dir = env.subst("$PROJECT_LIBDEPS_DIR")
        pioenv = env.subst("$PIOENV")
        candidate = (
            Path(libdeps_dir)
            / pioenv
            / "lvgl"
            / "src"
            / "misc"
            / "lv_profiler_builtin.c"
        )
        if candidate.exists():
            src = candidate
    try:
        text = src.read_text(encoding="utf-8", errors="ignore")
    except OSError as err:
        print("WARNING: failed to read LVGL profiler source:", err)
        return
    if PROFILER_NULL_FUNC_PATCHED in text:
        return
    text = text.replace(
        "lv_profiler_builtin_item_t * item = &profiler_ctx->item_arr[cur++];\n"
        "        uint64_t sec = item->tick / tick_per_sec;",
        "lv_profiler_builtin_item_t * item = &profiler_ctx->item_arr[cur++];\n"
        '        const char * func = item->func ? item->func : "<null>";\n'
        "        uint64_t sec = item->tick / tick_per_sec;",
    )
    text = text.replace(
        "                    item->tag,\n                    item->func);",
        "                    item->tag,\n                    func);",
    )
    try:
        src.write_text(text, encoding="utf-8")
        print("Patched LVGL profiler null function guard:", src)
    except OSError as err:
        print("WARNING: failed to patch LVGL profiler source:", err)


def patch_sw_rgb888_artwork_throttle_source(src):
    src = Path(src)
    if not src.exists():
        return
    try:
        text = src.read_text(encoding="utf-8", errors="ignore")
    except OSError as err:
        print("WARNING: failed to read LVGL RGB888 blend source:", err)
        return
    original = text
    helper_declaration = f"static inline bool {SW_RGB888_ARTWORK_THROTTLE_MARKER}("
    if helper_declaration not in text:
        # LVGL 9.6 moved the common declarations into the private blend header.
        # Also repair generated trees where an older patch inserted only calls.
        include = '#include "lv_draw_sw_blend_private.h"\n'
        if include not in text:
            include = '#include "../../../stdlib/lv_string.h"\n'
        if include not in text:
            raise RuntimeError(f"Unsupported LVGL RGB888 blend include: {src}")
        text = text.replace(
            include,
            include + "\n" + SW_RGB888_ARTWORK_THROTTLE_HELPER.strip() + "\n",
            1,
        )

    func_start = text.find("static void LV_ATTRIBUTE_FAST_MEM rgb565_image_blend(")
    if func_start == -1:
        print("WARNING: failed to find LVGL RGB565->RGB888 blend function")
        return
    decl = "    int32_t mask_stride = dsc->mask_stride;\n"
    decl_pos = text.find(decl, func_start)
    if decl_pos == -1:
        print("WARNING: failed to find LVGL RGB565->RGB888 blend declarations")
        return
    active_declaration = (
        "    const bool esphome_artwork_throttle = "
        "esphome_lvgl_rgb888_artwork_should_throttle(dsc);\n"
    )
    if active_declaration not in text:
        insert_at = decl_pos + len(decl)
        text = text[:insert_at] + active_declaration + text[insert_at:]

    func_end = text.find("\n#endif", func_start)
    if func_end == -1:
        print("WARNING: failed to find LVGL RGB565->RGB888 blend function end")
        return
    body = text[func_start:func_end]
    old = "src_buf_c16 = drawbuf_next_row(src_buf_c16, src_stride);"
    throttle_call = "esphome_lvgl_rgb888_artwork_throttle(esphome_artwork_throttle, y);"
    rows = body.count(old)
    calls = body.count(throttle_call)
    if rows == 0 or calls not in (0, rows):
        raise RuntimeError(f"Unsupported LVGL RGB565 artwork row loops: {src}")
    if calls == 0:
        body = body.replace(old, old + "\n                    " + throttle_call)
    text = text[:func_start] + body + text[func_end:]

    if text == original:
        return
    try:
        src.write_text(text, encoding="utf-8")
        print("Patched LVGL RGB565 artwork DSI backpressure:", src)
    except OSError as err:
        print("WARNING: failed to patch LVGL RGB888 blend source:", err)


def patch_thorvg_renderer_source(src):
    src = Path(src)
    if not src.exists():
        return
    try:
        text = src.read_text(encoding="utf-8", errors="ignore")
    except OSError as err:
        print("WARNING: failed to read ThorVG renderer source:", err)
        return
    if THORVG_RENDERER_MARKER in text:
        return

    original = text
    include = "#include <algorithm>\n"
    if include not in text:
        print("WARNING: failed to find ThorVG renderer include anchor")
        return
    text = text.replace(include, include + THORVG_RENDERER_HELPER.strip() + "\n", 1)

    old_alloc = (
        "        cmp->compositor->image.data = "
        "(pixel_t*)lv_malloc(channelSize * surface->stride * surface->h);\n"
        "        LV_ASSERT_MALLOC(cmp->compositor->image.data);"
    )
    new_alloc = (
        "        const size_t bytes = static_cast<size_t>(channelSize) * surface->stride * surface->h;\n"
        "        cmp->compositor->image.data = (pixel_t*)_allocCompositorBuffer(bytes);\n"
        "        if (!cmp->compositor->image.data) {\n"
        '            TVGERR("SW_ENGINE", "Failed to allocate compositor surface (%u bytes)", (unsigned)bytes);\n'
        "            delete(cmp->compositor);\n"
        "            delete(cmp);\n"
        "            return nullptr;\n"
        "        }"
    )
    if old_alloc not in text:
        print("WARNING: failed to find ThorVG compositor allocation")
        return
    text = text.replace(old_alloc, new_alloc, 1)

    old_free = "        lv_free((*comp)->compositor->image.data);"
    if old_free not in text:
        print("WARNING: failed to find ThorVG compositor release")
        return
    text = text.replace(
        old_free, "        _freeCompositorBuffer((*comp)->compositor->image.data);", 1
    )

    old_target = "    auto cmp = request(CHANNEL_SIZE(cs));\n"
    if old_target not in text:
        print("WARNING: failed to find ThorVG compositor target request")
        return
    text = text.replace(old_target, old_target + "    if (!cmp) return nullptr;\n", 1)

    old_effect = "    auto& buffer = request(surface->channelSize)->compositor->image;"
    new_effect = (
        "    auto scratch = request(surface->channelSize);\n"
        "    if (!scratch) return false;\n"
        "    auto& buffer = scratch->compositor->image;"
    )
    if old_effect not in text:
        print("WARNING: failed to find ThorVG effect scratch request")
        return
    text = text.replace(old_effect, new_effect, 1)

    if text == original:
        return
    try:
        src.write_text(text, encoding="utf-8")
        print("Patched ThorVG compositor buffers:", src)
    except OSError as err:
        print("WARNING: failed to patch ThorVG renderer source:", err)


def patch_thorvg_raster_source(src):
    src = Path(src)
    if not src.exists():
        return
    try:
        text = src.read_text(encoding="utf-8", errors="ignore")
    except OSError as err:
        print("WARNING: failed to read ThorVG raster source:", err)
        return
    if THORVG_RASTER_MARKER in text:
        return

    original = text
    anchor = "static bool _rasterCompositeMaskedRle("
    if anchor not in text:
        print("WARNING: failed to find ThorVG RLE anchor")
        return
    text = text.replace(anchor, THORVG_RASTER_HELPER.strip() + "\n\n" + anchor, 1)

    solid_start = text.find("static bool _rasterSolidRle(")
    rle_start = text.find("static bool _rasterRle(", solid_start)
    if solid_start == -1 or rle_start == -1:
        print("WARNING: failed to find ThorVG solid RLE function")
        return
    solid = text[solid_start:rle_start]
    loop = (
        "        for (uint32_t i = 0; i < rle->size; ++i, ++span) {\n"
        "            if (span->coverage == 255) {"
    )
    guarded_loop = (
        "        for (uint32_t i = 0; i < rle->size; ++i, ++span) {\n"
        "            uint32_t x, len;\n"
        "            if (!_clipRleSpan(surface, span, x, len)) continue;\n"
        "            if (span->coverage == 255) {"
    )
    if solid.count(loop) != 2:
        print("WARNING: failed to find both ThorVG solid RLE loops")
        return
    solid = solid.replace(loop, guarded_loop, 2)
    solid = solid.replace("span->x, span->len", "x, len")
    solid = solid.replace("span->x];", "x];")
    solid = solid.replace("span->len;", "len;")
    text = text[:solid_start] + solid + text[rle_start:]

    rle_body_start = text.find("static bool _rasterRle(")
    brace = text.find("{", rle_body_start)
    if rle_body_start == -1 or brace == -1:
        print("WARNING: failed to find ThorVG RLE dispatcher")
        return
    text = (
        text[: brace + 1]
        + "\n    if (!_validRle(rle)) return false;"
        + text[brace + 1 :]
    )

    if text == original:
        return
    try:
        src.write_text(text, encoding="utf-8")
        print("Patched ThorVG RLE bounds guards:", src)
    except OSError as err:
        print("WARNING: failed to patch ThorVG raster source:", err)


def patch_component_sources(component_dir):
    """Apply the same checked source fixes to native IDF and PlatformIO builds."""
    lvgl_src = Path(component_dir) / "src"
    patches = (
        (
            patch_ppa_blocking_user_data_source,
            lvgl_src / "draw" / "espressif" / "ppa" / "lv_draw_ppa_img.c",
            None,
        ),
        (
            patch_profiler_builtin_source,
            lvgl_src / "debugging" / "profiler" / "lv_profiler_builtin.c",
            PROFILER_NULL_FUNC_PATCHED,
        ),
        (
            patch_sw_rgb888_artwork_throttle_source,
            lvgl_src / "draw" / "sw" / "blend" / "lv_draw_sw_blend_to_rgb888.c",
            f"static inline bool {SW_RGB888_ARTWORK_THROTTLE_MARKER}(",
        ),
        (
            patch_thorvg_renderer_source,
            lvgl_src / "libs" / "thorvg" / "tvgSwRenderer.cpp",
            THORVG_RENDERER_MARKER,
        ),
        (
            patch_thorvg_raster_source,
            lvgl_src / "libs" / "thorvg" / "tvgSwRaster.cpp",
            THORVG_RASTER_MARKER,
        ),
    )
    for patch, source, marker in patches:
        patch(source)
        if marker is not None and marker not in source.read_text(encoding="utf-8"):
            raise RuntimeError(
                f"Unsupported LVGL source, required patch missing: {source}"
            )


create_piolibdeps_atomic_shim()
patch_piolibdeps_lvgl_sources()


def _ppa_cache_sync_replacements(name):
    preserve_output = (
        "config->out.block_offset_x != 0 || new_block_w != config->out.pic_w"
        if name == "ppa_srm.c"
        else "true"
    )
    replacements = {
        "esp_cache_msync((void *)in_ext_window, in_ext_window_len, "
        "ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);": "ppa_cache_msync_external_window(in_ext_window, in_ext_window_len, "
        "buf_alignment_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M, false);",
        "esp_cache_msync((void *)out_ext_window_aligned, "
        "PPA_ALIGN_UP(out_ext_window_len + (out_ext_window - out_ext_window_aligned), "
        "buf_alignment_size), ESP_CACHE_MSYNC_FLAG_DIR_M2C);": "ppa_cache_msync_external_window(out_ext_window, out_ext_window_len, "
        "buf_alignment_size, ESP_CACHE_MSYNC_FLAG_DIR_M2C, "
        f"{preserve_output});",
        "esp_cache_msync((void *)in_bg_ext_window_aligned, "
        "PPA_ALIGN_UP(in_bg_ext_window_len + (in_bg_ext_window - in_bg_ext_window_aligned), "
        "buf_alignment_size), ESP_CACHE_MSYNC_FLAG_DIR_C2M);": "ppa_cache_msync_external_window(in_bg_ext_window, in_bg_ext_window_len, "
        "buf_alignment_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M, false);",
        "esp_cache_msync((void *)in_fg_ext_window_aligned, "
        "PPA_ALIGN_UP(in_fg_ext_window_len + (in_fg_ext_window - in_fg_ext_window_aligned), "
        "buf_alignment_size), ESP_CACHE_MSYNC_FLAG_DIR_C2M);": "ppa_cache_msync_external_window(in_fg_ext_window, in_fg_ext_window_len, "
        "buf_alignment_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M, false);",
        "esp_cache_msync((void *)out_ext_window_aligned, "
        "PPA_ALIGN_UP(out_ext_window_len + (out_ext_window - out_ext_window_aligned), "
        "buf_alignment_size), ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_INVALIDATE);": "ppa_cache_msync_external_window(out_ext_window, out_ext_window_len, "
        "buf_alignment_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_INVALIDATE, true);",
    }
    if name == "ppa_srm.c":
        indexes = (0, 1)
    elif name == "ppa_blend.c":
        indexes = (2, 3, 1)
    elif name == "ppa_fill.c":
        indexes = (4,)
    else:
        raise RuntimeError(f"Unsupported ESP-IDF PPA source: {name}")
    items = list(replacements.items())
    return [items[index] for index in indexes]


def _prepare_espidf_ppa_cache_sync_source(src, text, idf_version=None):
    """Validate the complete touched region before changing any source."""
    if idf_version is not None and idf_version not in PPA_CACHE_PREIMAGES:
        raise RuntimeError(
            f"Unsupported ESP-IDF PPA cache patch version: {idf_version}"
        )
    if text.count(PPA_CACHE_REGION_START) != 1:
        raise RuntimeError(f"Unsupported ESP-IDF PPA cache region: {src}")
    start = text.index(PPA_CACHE_REGION_START)
    end = text.find(PPA_CACHE_REGION_END, start)
    if end == -1:
        raise RuntimeError(f"Incomplete ESP-IDF PPA cache region: {src}")
    block = text[start:end]
    normalized = block
    replacements = _ppa_cache_sync_replacements(src.name)
    for old, new in replacements:
        legacy = new.rsplit(", ", 1)[0] + ");"
        variants = (old, new, legacy)
        if sum(block.count(variant) for variant in variants) != 1:
            raise RuntimeError(f"Unsupported ESP-IDF PPA cache call: {src}")
        normalized = normalized.replace(new, old).replace(legacy, old)
    digest = hashlib.sha256(normalized.encode("utf-8")).hexdigest()
    versions = (idf_version,) if idf_version is not None else PPA_CACHE_PREIMAGES
    if not any(
        digest == PPA_CACHE_PREIMAGES[version][src.name] for version in versions
    ):
        raise RuntimeError(f"ESP-IDF PPA cache preimage changed: {src} sha256={digest}")
    for old, new in replacements:
        legacy = new.rsplit(", ", 1)[0] + ");"
        block = block.replace(old, new).replace(legacy, new)
    text = text[:start] + block + text[end:]
    text = replace_espidf_ppa_cache_sync_helper(text)
    remaining = text.replace(PPA_CACHE_SYNC_HELPER.strip(), "", 1)
    for call in PPA_DESCRIPTOR_CACHE_CALLS[src.name]:
        if remaining.count(call) != 1:
            raise RuntimeError(f"ESP-IDF PPA descriptor cache call changed: {src}")
        remaining = remaining.replace(call, "", 1)
    if "esp_cache_msync(" in remaining:
        raise RuntimeError(
            f"Unreviewed ESP-IDF PPA cache call outside guarded region: {src}"
        )
    return text


def patch_espidf_ppa_cache_sync_source(src, idf_version=None):
    src = Path(src)
    original = src.read_text(encoding="utf-8")
    patched = _prepare_espidf_ppa_cache_sync_source(src, original, idf_version)
    if patched != original:
        src.write_text(patched, encoding="utf-8", newline="\n")
        print("Patched ESP-IDF PPA cache sync guards:", src)


def patch_espidf_ppa_cache_sync(framework_dir=None):
    """Apply the same checked SDK patch from native IDF or PlatformIO."""
    if framework_dir is None:
        if env is None:
            return
        framework_dir = env.PioPlatform().get_package_dir("framework-espidf")
        if not framework_dir:
            return
    framework_dir = Path(framework_dir)
    version_text = (framework_dir / "version.txt").read_text(encoding="utf-8").strip()
    match = re.fullmatch(r"v?(\d+)\.(\d+)\.(\d+)(?:[-+].*)?", version_text)
    if match is None:
        raise RuntimeError(f"Unsupported ESP-IDF version string: {version_text!r}")
    idf_version = tuple(int(part) for part in match.groups())
    if idf_version not in PPA_CACHE_PREIMAGES:
        raise RuntimeError(
            f"Unsupported ESP-IDF PPA cache patch version: {idf_version}"
        )
    ppa_src = framework_dir / "components" / "esp_driver_ppa" / "src"
    prepared = []
    for name in ("ppa_srm.c", "ppa_blend.c", "ppa_fill.c"):
        source = ppa_src / name
        original = source.read_text(encoding="utf-8")
        patched = _prepare_espidf_ppa_cache_sync_source(source, original, idf_version)
        prepared.append((source, original, patched))
    # Preflight every file first: a changed SDK must not leave a partial patch.
    for source, original, patched in prepared:
        if patched != original:
            source.write_text(patched, encoding="utf-8", newline="\n")
        print(
            f"ESPHome PPA cache source IDF={version_text} path={source} "
            f"sha256={hashlib.sha256(patched.encode('utf-8')).hexdigest()}"
        )


patch_espidf_ppa_cache_sync()

# Parse build flags from ESPHome's __init__.py
_build_flags = " ".join(env.get("BUILD_FLAGS", [])) if env is not None else ""
_thorvg_enabled = "LVGL_USE_THORVG=1" in _build_flags
_sysmon_enabled = "LVGL_USE_SYSMON=1" in _build_flags

# Extract used widgets list from build flags
# Format: -DLVGL_WIDGETS_USED=\"label,button,slider,...\"
_used_widgets = set()
_match = re.search(r'LVGL_WIDGETS_USED=\\"([^"]*)\\"', _build_flags)
if not _match:
    _match = re.search(r'LVGL_WIDGETS_USED="([^"]*)"', _build_flags)
if _match:
    _used_widgets = set(_match.group(1).split(","))

# Mapping from lv_uses names (ESPHome) to LVGL source file names
# ESPHome widget name → LVGL 9.x source file name (without lv_ prefix and .c/.h)
# Only widgets that have a DIFFERENT name in the LVGL C source need mapping.
# Most widgets have the same name (e.g., "label" → "lv_label.c")
_WIDGET_NAME_TO_LVGL_FILE = {
    "btn": "button",  # ESPHome get_uses() returns "btn", LVGL file is lv_button.c
    "btnmatrix": "buttonmatrix",
    "img": "image",  # LVGL 9.x renamed lv_img → lv_image
    "imgbtn": "imagebutton",  # LVGL 9.x renamed
}

# All LVGL widget source files (in src/widgets/) and their corresponding
# ESPHome lv_uses name. When a widget is NOT in lv_uses, its source file
# will be excluded from compilation.
_LVGL_WIDGET_FILES = {
    # LVGL file base name → set of ESPHome lv_uses names that require it
    "animimage": {"animimg", "animimage"},
    "arc": {"arc"},
    "bar": {"bar"},
    "button": {"button", "btn"},
    "buttonmatrix": {"buttonmatrix", "btnmatrix"},
    "calendar": {"calendar"},
    "canvas": {"canvas", "lottie"},
    "chart": {"chart"},
    "checkbox": {"checkbox"},
    "dropdown": {"dropdown"},
    "image": {"image", "img", "lottie"},
    "imagebutton": {"imgbtn", "imagebutton"},
    "keyboard": {"keyboard"},
    "label": {"label"},
    "led": {"led"},
    "line": {"line"},
    "list": {"list"},
    "lottie": {"lottie"},
    "menu": {"menu"},
    "msgbox": {"msgbox"},
    "roller": {"roller"},
    "scale": {"scale", "meter"},
    "slider": {"slider"},
    "span": {"span", "spangroup"},
    "spinbox": {"spinbox"},
    "spinner": {"spinner"},
    "switch": {"switch"},
    "table": {"table"},
    "tabview": {"tabview"},
    "textarea": {"textarea"},
    "tileview": {"tileview"},
    "win": {"win"},
}

# Determine which LVGL widget files are needed
_needed_widget_files = set()
for lvgl_file, use_names in _LVGL_WIDGET_FILES.items():
    if use_names & _used_widgets:
        _needed_widget_files.add(lvgl_file)

# QR code is in libs/qrcode/, not widgets/
_qrcode_needed = "qrcode" in _used_widgets

# Lottie requires LVGL canvas and image internally.
# The build filter sees only widgets explicitly used in YAML, so using
# lottie does not automatically keep lv_canvas.c unless we add it here.
if "lottie" in _used_widgets:
    _needed_widget_files.add("canvas")
    _needed_widget_files.add("image")


def lvgl_src_filter(build_env, node):
    """Skip compilation of LVGL source files not needed for ESP32."""
    path = str(node.get_path()).replace("\\", "/")

    # ESP-IDF / FreeRTOS atomic.h fix for LVGL compiled from .piolibdeps.
    #
    # lv_freertos.c contains:
    #   #include "atomic.h"
    #
    # The shim atomic.h from the external component is not reliably visible
    # when PlatformIO compiles LVGL as a library from .piolibdeps.
    #
    # Put atomic.h directly next to lv_freertos.c so quoted include resolution
    # always finds it first.
    if path.endswith("/osal/lv_freertos.c"):
        try:
            src = Path(node.get_path())
            write_atomic_shim(src.parent / "atomic.h")

        except OSError as err:
            print("WARNING: failed to create LVGL osal atomic.h shim:", err)

    if path.endswith("/misc/lv_profiler_builtin.c"):
        patch_profiler_builtin_source(node.get_path())

    if path.endswith("/draw/espressif/ppa/lv_draw_ppa_img.c"):
        patch_ppa_blocking_user_data_source(node.get_path())

    if path.endswith("/draw/sw/blend/lv_draw_sw_blend_to_rgb888.c"):
        patch_sw_rgb888_artwork_throttle_source(node.get_path())

    if path.endswith("/libs/thorvg/tvgSwRenderer.cpp"):
        patch_thorvg_renderer_source(node.get_path())

    if path.endswith("/libs/thorvg/tvgSwRaster.cpp"):
        patch_thorvg_raster_source(node.get_path())

    # Only filter files inside the LVGL library
    if "/lvgl/" not in path:
        return node

    # ===== Draw backends NOT available on ESP32 =====
    EXCLUDED_DRAW = [
        "/draw/nanovg/",  # NanoVG (OpenGL) - desktop only
        "/draw/nema_gfx/",  # NemaGFX - Renesas/Think Silicon GPU
        "/draw/nxp/",  # NXP PXP/G2D - NXP MCUs only
        "/draw/renesas/",  # Renesas Dave2D - Renesas MCUs only
        "/draw/eve/",  # FT800/FT813 EVE GPU
        "/draw/vg_lite/",  # VG-Lite - NXP/Vivante GPU
        "/draw/dma2d/",  # STM32 DMA2D - STM32 only
        "/draw/sdl/",  # SDL - desktop only
        "/draw/opengles/",  # OpenGL ES - desktop only
    ]

    # ===== Display/input drivers NOT for ESP32 =====
    EXCLUDED_DRIVERS = [
        "/drivers/wayland/",  # Linux Wayland
        "/drivers/x11/",  # Linux X11
        "/drivers/windows/",  # Windows
        "/drivers/sdl/",  # SDL desktop
        "/drivers/nuttx/",  # NuttX RTOS
        "/drivers/qnx/",  # QNX RTOS
        "/drivers/uefi/",  # UEFI firmware
        "/drivers/opengles/",  # OpenGL ES desktop
        "/drivers/draw/eve/",  # EVE display driver
        "/drivers/display/drm/",  # Linux DRM
        "/drivers/display/fb/",  # Linux framebuffer
        "/drivers/display/ft81x/",  # FT81x display
        "/drivers/display/lovyan_gfx/",  # LovyanGFX (handled by ESPHome)
        "/drivers/display/nxp_elcdif/",  # NXP eLCDIF
        "/drivers/display/renesas_glcdc/",  # Renesas GLCDC
        "/drivers/display/st_ltdc/",  # STM32 LTDC
        "/drivers/display/tft_espi/",  # TFT_eSPI (handled by ESPHome)
        "/drivers/display/nv3007/",  # NV3007
        "/drivers/libinput/",  # Linux libinput
        "/drivers/evdev/",  # Linux evdev
    ]

    # ===== Libraries NOT needed on ESP32 =====
    EXCLUDED_LIBS = [
        "/libs/gltf/",  # 3D glTF rendering (OpenGL required)
        "/libs/nanovg/",  # NanoVG (OpenGL required)
        "/libs/ffmpeg/",  # FFmpeg video decoding
        "/libs/freetype/",  # FreeType font engine (use tiny_ttf instead)
        "/libs/rlottie/",  # rlottie (use ThorVG Lottie instead)
        "/libs/libpng/",  # libpng (use pngdec/lodepng instead)
        "/libs/libjpeg_turbo/",  # libjpeg-turbo (use tjpgd instead)
        "/libs/libwebp/",  # libwebp (use ThorVG WebP instead)
        "/libs/frogfs/",  # FrogFS filesystem
        "/libs/vg_lite_driver/",  # VG-Lite driver library
        "/libs/FT800-FT813/",  # FT800/FT813 EVE library
        "/libs/fsdrv/lv_fs_win32.",  # Windows filesystem
        "/libs/fsdrv/lv_fs_uefi.",  # UEFI filesystem
        "/libs/fsdrv/lv_fs_stdio.",  # stdio filesystem (desktop)
        "/libs/fsdrv/lv_fs_arduino_sd.",  # Arduino SD (not ESP-IDF)
        "/libs/fsdrv/lv_fs_arduino_esp_littlefs.",  # Arduino LittleFS
        "/libs/fsdrv/lv_fs_frogfs.",  # FrogFS driver
        "/libs/fsdrv/lv_fs_littlefs.",  # LittleFS driver
    ]

    # ===== OS abstraction layers NOT for ESP32 (uses FreeRTOS) =====
    EXCLUDED_OSAL = [
        "/osal/lv_linux.",  # Linux
        "/osal/lv_windows.",  # Windows
        "/osal/lv_sdl2.",  # SDL2
        "/osal/lv_pthread.",  # POSIX threads
        "/osal/lv_cmsis_rtos2.",  # CMSIS RTOS2
        "/osal/lv_mqx.",  # MQX RTOS
        "/osal/lv_rtthread.",  # RT-Thread
    ]

    # ===== stdlib NOT for ESP32 (uses custom malloc) =====
    EXCLUDED_STDLIB = [
        "/stdlib/micropython/",  # MicroPython
        "/stdlib/rtthread/",  # RT-Thread
        "/stdlib/uefi/",  # UEFI
    ]

    # ===== Debug/test files NOT for production =====
    EXCLUDED_DEBUG = [
        "/debugging/monkey/",  # Monkey testing
        "/debugging/test/",  # Test helpers
        "/debugging/vg_lite_tvg/",  # VG-Lite ThorVG debug
    ]
    if not _sysmon_enabled:
        EXCLUDED_DEBUG.append("/debugging/sysmon/lv_sysmon.")

    # Combine platform exclusions (always applied)
    all_excluded = (
        EXCLUDED_DRAW
        + EXCLUDED_DRIVERS
        + EXCLUDED_LIBS
        + EXCLUDED_OSAL
        + EXCLUDED_STDLIB
        + EXCLUDED_DEBUG
    )

    # ===== Conditionally exclude ThorVG/SVG/Lottie when not needed =====
    if not _thorvg_enabled:
        all_excluded += [
            "/libs/thorvg/",  # ThorVG vector engine (~500KB+)
            "/libs/lottie/",  # Lottie animation parser
            "/libs/svg/",  # SVG parser
            "/draw/lv_draw_vector.",  # Vector drawing operations
            "/draw/sw/lv_draw_sw_vector.",  # SW vector renderer
        ]

    # ===== Conditionally exclude QR code library =====
    if not _qrcode_needed:
        all_excluded.append("/libs/qrcode/")

    # ===== Conditionally exclude GIF / BMP libraries =====
    # Must match the gating in components/lvgl/__init__.py where LV_USE_BMP /
    # LV_USE_GIF are set to 1 when any of 'image', 'img', or 'animimg' widgets
    # are used. If we exclude the source while the defines are on, lv_init()
    # references lv_bmp_init / lv_gif_init that no longer have definitions.
    if (
        "image" not in _used_widgets
        and "img" not in _used_widgets
        and "animimg" not in _used_widgets
    ):
        all_excluded.append("/libs/gif/")
        all_excluded.append("/libs/bmp/")

    # Check platform/library exclusions first
    for pattern in all_excluded:
        if pattern in path:
            return None  # Skip this file

    # ===== Per-widget source file exclusion =====
    # LVGL v9.x uses subdirectories: src/widgets/menu/lv_menu.c
    # LVGL v8.x used flat layout: src/widgets/lv_menu.c
    # Support both patterns.
    if _used_widgets and "/widgets/" in path and "/lv_" in path:
        match = re.search(r"/widgets/(?:\w+/)?lv_(\w+)\.[ch]", path)
        if match:
            widget_file_name = match.group(1)
            if widget_file_name not in _needed_widget_files:
                return None  # Skip: widget not used

    return node


if env is not None:
    env.AddBuildMiddleware(lvgl_src_filter)
elif __name__ == "__main__":
    if len(sys.argv) != 3:
        raise RuntimeError("Expected a source patch command and its explicit directory")
    if sys.argv[1] == "--patch-component-sources":
        patch_component_sources(sys.argv[2])
    elif sys.argv[1] == "--patch-ppa-blocking-user-data":
        patch_ppa_blocking_user_data_source(
            Path(sys.argv[2])
            / "src"
            / "draw"
            / "espressif"
            / "ppa"
            / "lv_draw_ppa_img.c"
        )
    elif sys.argv[1] == "--patch-espidf-ppa-cache-sync":
        patch_espidf_ppa_cache_sync(sys.argv[2])
    else:
        raise RuntimeError(f"Unknown LVGL source patch command: {sys.argv[1]}")
