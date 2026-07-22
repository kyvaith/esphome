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
from pathlib import Path
import re

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


def replace_espidf_ppa_cache_sync_helper(text):
    # Remove declarations left by earlier runs before replacing the helper.
    # The ESP-IDF package is cached between PlatformIO builds, so the patch
    # must be idempotent rather than prepending another weak declaration.
    text = re.sub(
        r"\n*extern bool esphome_lvgl_ppa_skip_cache_msync\([^;]+?"
        r"__attribute__\(\(weak\)\);\n",
        "\n",
        text,
        flags=re.DOTALL,
    )
    helper_start = text.find("static esp_err_t ppa_cache_msync_external_window(")
    if helper_start == -1:
        return text
    tag_start = text.find('\nstatic const char *TAG = "ppa_', helper_start)
    if tag_start == -1:
        return text
    return (
        text[:helper_start] + PPA_CACHE_SYNC_HELPER.strip() + "\n" + text[tag_start:]
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
        write_atomic_shim(Path(libdeps_dir) / pioenv / "lvgl" / "src" / "osal" / "atomic.h")


def patch_piolibdeps_lvgl_sources():
    if env is None:
        return
    libdeps_dir = env.subst("$PROJECT_LIBDEPS_DIR")
    pioenv = env.subst("$PIOENV")
    if not libdeps_dir or not pioenv:
        return
    lvgl_src = Path(libdeps_dir) / pioenv / "lvgl" / "src"
    patch_profiler_builtin_source(lvgl_src / "misc" / "lv_profiler_builtin.c")
    patch_sw_rgb888_artwork_throttle_source(
        lvgl_src / "draw" / "sw" / "blend" / "lv_draw_sw_blend_to_rgb888.c"
    )


def patch_profiler_builtin_source(src):
    src = Path(src)
    if not src.exists() and env is not None:
        libdeps_dir = env.subst("$PROJECT_LIBDEPS_DIR")
        pioenv = env.subst("$PIOENV")
        candidate = Path(libdeps_dir) / pioenv / "lvgl" / "src" / "misc" / "lv_profiler_builtin.c"
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
        "                    item->tag,\n"
        "                    item->func);",
        "                    item->tag,\n"
        "                    func);",
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
    if SW_RGB888_ARTWORK_THROTTLE_MARKER in text:
        return

    original = text
    text = text.replace(
        '#include "../../../stdlib/lv_string.h"\n',
        '#include "../../../stdlib/lv_string.h"\n\n' + SW_RGB888_ARTWORK_THROTTLE_HELPER.strip() + "\n",
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
    insert_at = decl_pos + len(decl)
    text = (
        text[:insert_at]
        + "    const bool esphome_artwork_throttle = esphome_lvgl_rgb888_artwork_should_throttle(dsc);\n"
        + text[insert_at:]
    )

    func_end = text.find("\n#endif", func_start)
    if func_end == -1:
        print("WARNING: failed to find LVGL RGB565->RGB888 blend function end")
        return
    body = text[func_start:func_end]
    old = "src_buf_c16 = drawbuf_next_row(src_buf_c16, src_stride);"
    new = old + "\n                    esphome_lvgl_rgb888_artwork_throttle(esphome_artwork_throttle, y);"
    body = body.replace(old, new)
    text = text[:func_start] + body + text[func_end:]

    if text == original:
        return
    try:
        src.write_text(text, encoding="utf-8")
        print("Patched LVGL RGB565 artwork DSI backpressure:", src)
    except OSError as err:
        print("WARNING: failed to patch LVGL RGB888 blend source:", err)


create_piolibdeps_atomic_shim()
patch_piolibdeps_lvgl_sources()


def patch_espidf_ppa_cache_sync_source(src):
    src = Path(src)
    if not src.exists():
        return
    try:
        text = src.read_text(encoding="utf-8", errors="ignore")
    except OSError as err:
        print("WARNING: failed to read ESP-IDF PPA source:", err)
        return

    original = text
    if "ppa_cache_msync_external_window" not in text:
        text = text.replace(
            'static const char *TAG = "ppa_',
            PPA_CACHE_SYNC_HELPER + '\nstatic const char *TAG = "ppa_',
            1,
        )
    else:
        text = replace_espidf_ppa_cache_sync_helper(text)

    replacements = {
        "esp_cache_msync((void *)in_ext_window, in_ext_window_len, "
        "ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);":
            "ppa_cache_msync_external_window(in_ext_window, in_ext_window_len, "
            "buf_alignment_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M, false);",
        "esp_cache_msync((void *)out_ext_window_aligned, "
        "PPA_ALIGN_UP(out_ext_window_len + (out_ext_window - out_ext_window_aligned), "
        "buf_alignment_size), ESP_CACHE_MSYNC_FLAG_DIR_M2C);":
            "ppa_cache_msync_external_window(out_ext_window, out_ext_window_len, "
            "buf_alignment_size, ESP_CACHE_MSYNC_FLAG_DIR_M2C, "
            "config->out.block_offset_x != 0 || new_block_w != config->out.pic_w);",
        "esp_cache_msync((void *)in_bg_ext_window_aligned, "
        "PPA_ALIGN_UP(in_bg_ext_window_len + (in_bg_ext_window - in_bg_ext_window_aligned), "
        "buf_alignment_size), ESP_CACHE_MSYNC_FLAG_DIR_C2M);":
            "ppa_cache_msync_external_window(in_bg_ext_window, in_bg_ext_window_len, "
            "buf_alignment_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M, false);",
        "esp_cache_msync((void *)in_fg_ext_window_aligned, "
        "PPA_ALIGN_UP(in_fg_ext_window_len + (in_fg_ext_window - in_fg_ext_window_aligned), "
        "buf_alignment_size), ESP_CACHE_MSYNC_FLAG_DIR_C2M);":
            "ppa_cache_msync_external_window(in_fg_ext_window, in_fg_ext_window_len, "
            "buf_alignment_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M, false);",
        "esp_cache_msync((void *)out_ext_window_aligned, "
        "PPA_ALIGN_UP(out_ext_window_len + (out_ext_window - out_ext_window_aligned), "
        "buf_alignment_size), ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_INVALIDATE);":
            "ppa_cache_msync_external_window(out_ext_window, out_ext_window_len, "
            "buf_alignment_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_INVALIDATE, true);",
    }
    for old, new in replacements.items():
        text = text.replace(old, new)

    # Upgrade sources patched by an earlier version of this script. SRM can
    # discard dirty cache lines only when it overwrites complete output rows;
    # partial blend output must preserve pixels outside the target block.
    text = text.replace(
        "buf_alignment_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);",
        "buf_alignment_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M, false);",
    )
    preserve_srm_output = (
        "config->out.block_offset_x != 0 || new_block_w != config->out.pic_w"
        if src.name == "ppa_srm.c"
        else "true"
    )
    text = text.replace(
        "buf_alignment_size, ESP_CACHE_MSYNC_FLAG_DIR_M2C);",
        "buf_alignment_size, ESP_CACHE_MSYNC_FLAG_DIR_M2C, "
        f"{preserve_srm_output});",
    )
    if src.name != "ppa_srm.c":
        text = text.replace(
            "config->out.block_offset_x != 0 || new_block_w != config->out.pic_w",
            "true",
        )
    text = text.replace(
        "buf_alignment_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_INVALIDATE);",
        "buf_alignment_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M | "
        "ESP_CACHE_MSYNC_FLAG_INVALIDATE, true);",
    )

    if text == original:
        return
    try:
        src.write_text(text, encoding="utf-8")
        print("Patched ESP-IDF PPA cache sync guards:", src)
    except OSError as err:
        print("WARNING: failed to patch ESP-IDF PPA source:", err)


def patch_espidf_ppa_cache_sync():
    if env is None:
        return
    framework_dir = env.PioPlatform().get_package_dir("framework-espidf")
    if not framework_dir:
        return
    ppa_src = Path(framework_dir) / "components" / "esp_driver_ppa" / "src"
    for name in ("ppa_srm.c", "ppa_blend.c", "ppa_fill.c"):
        patch_espidf_ppa_cache_sync_source(ppa_src / name)


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
    "btn": "button",          # ESPHome get_uses() returns "btn", LVGL file is lv_button.c
    "btnmatrix": "buttonmatrix",
    "img": "image",           # LVGL 9.x renamed lv_img → lv_image
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

    if path.endswith("/draw/sw/blend/lv_draw_sw_blend_to_rgb888.c"):
        patch_sw_rgb888_artwork_throttle_source(node.get_path())

    # Only filter files inside the LVGL library
    if "/lvgl/" not in path:
        return node

    # ===== Draw backends NOT available on ESP32 =====
    EXCLUDED_DRAW = [
        "/draw/nanovg/",           # NanoVG (OpenGL) - desktop only
        "/draw/nema_gfx/",         # NemaGFX - Renesas/Think Silicon GPU
        "/draw/nxp/",              # NXP PXP/G2D - NXP MCUs only
        "/draw/renesas/",          # Renesas Dave2D - Renesas MCUs only
        "/draw/eve/",              # FT800/FT813 EVE GPU
        "/draw/vg_lite/",          # VG-Lite - NXP/Vivante GPU
        "/draw/dma2d/",            # STM32 DMA2D - STM32 only
        "/draw/sdl/",              # SDL - desktop only
        "/draw/opengles/",         # OpenGL ES - desktop only
    ]

    # ===== Display/input drivers NOT for ESP32 =====
    EXCLUDED_DRIVERS = [
        "/drivers/wayland/",       # Linux Wayland
        "/drivers/x11/",           # Linux X11
        "/drivers/windows/",       # Windows
        "/drivers/sdl/",           # SDL desktop
        "/drivers/nuttx/",         # NuttX RTOS
        "/drivers/qnx/",          # QNX RTOS
        "/drivers/uefi/",          # UEFI firmware
        "/drivers/opengles/",      # OpenGL ES desktop
        "/drivers/draw/eve/",      # EVE display driver
        "/drivers/display/drm/",         # Linux DRM
        "/drivers/display/fb/",          # Linux framebuffer
        "/drivers/display/ft81x/",       # FT81x display
        "/drivers/display/lovyan_gfx/",  # LovyanGFX (handled by ESPHome)
        "/drivers/display/nxp_elcdif/",  # NXP eLCDIF
        "/drivers/display/renesas_glcdc/",  # Renesas GLCDC
        "/drivers/display/st_ltdc/",     # STM32 LTDC
        "/drivers/display/tft_espi/",    # TFT_eSPI (handled by ESPHome)
        "/drivers/display/nv3007/",      # NV3007
        "/drivers/libinput/",      # Linux libinput
        "/drivers/evdev/",         # Linux evdev
    ]

    # ===== Libraries NOT needed on ESP32 =====
    EXCLUDED_LIBS = [
        "/libs/gltf/",             # 3D glTF rendering (OpenGL required)
        "/libs/nanovg/",           # NanoVG (OpenGL required)
        "/libs/ffmpeg/",           # FFmpeg video decoding
        "/libs/freetype/",         # FreeType font engine (use tiny_ttf instead)
        "/libs/rlottie/",          # rlottie (use ThorVG Lottie instead)
        "/libs/libpng/",           # libpng (use pngdec/lodepng instead)
        "/libs/libjpeg_turbo/",    # libjpeg-turbo (use tjpgd instead)
        "/libs/libwebp/",          # libwebp (use ThorVG WebP instead)
        "/libs/frogfs/",           # FrogFS filesystem
        "/libs/vg_lite_driver/",   # VG-Lite driver library
        "/libs/FT800-FT813/",      # FT800/FT813 EVE library
        "/libs/fsdrv/lv_fs_win32.",     # Windows filesystem
        "/libs/fsdrv/lv_fs_uefi.",      # UEFI filesystem
        "/libs/fsdrv/lv_fs_stdio.",     # stdio filesystem (desktop)
        "/libs/fsdrv/lv_fs_arduino_sd.",  # Arduino SD (not ESP-IDF)
        "/libs/fsdrv/lv_fs_arduino_esp_littlefs.",  # Arduino LittleFS
        "/libs/fsdrv/lv_fs_frogfs.",    # FrogFS driver
        "/libs/fsdrv/lv_fs_littlefs.",  # LittleFS driver
    ]

    # ===== OS abstraction layers NOT for ESP32 (uses FreeRTOS) =====
    EXCLUDED_OSAL = [
        "/osal/lv_linux.",          # Linux
        "/osal/lv_windows.",        # Windows
        "/osal/lv_sdl2.",           # SDL2
        "/osal/lv_pthread.",        # POSIX threads
        "/osal/lv_cmsis_rtos2.",    # CMSIS RTOS2
        "/osal/lv_mqx.",            # MQX RTOS
        "/osal/lv_rtthread.",       # RT-Thread
    ]

    # ===== stdlib NOT for ESP32 (uses custom malloc) =====
    EXCLUDED_STDLIB = [
        "/stdlib/micropython/",     # MicroPython
        "/stdlib/rtthread/",        # RT-Thread
        "/stdlib/uefi/",            # UEFI
    ]

    # ===== Debug/test files NOT for production =====
    EXCLUDED_DEBUG = [
        "/debugging/monkey/",               # Monkey testing
        "/debugging/test/",                 # Test helpers
        "/debugging/vg_lite_tvg/",          # VG-Lite ThorVG debug
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
            "/libs/thorvg/",           # ThorVG vector engine (~500KB+)
            "/libs/lottie/",           # Lottie animation parser
            "/libs/svg/",              # SVG parser
            "/draw/lv_draw_vector.",   # Vector drawing operations
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
