import importlib
from pathlib import Path
import pkgutil
import re

from esphome.automation import Trigger, build_automation, validate_automation
import esphome.codegen as cg
from esphome.components.const import (
    BYTE_ORDER_BIG,
    CONF_BYTE_ORDER,
    CONF_COLOR_DEPTH,
    CONF_DRAW_ROUNDING,
)
from esphome.components.display import Display, get_display_metadata, validate_rotation
from esphome.components.esp32 import (
    VARIANT_ESP32P4,
    add_idf_component,
    add_idf_sdkconfig_option,
    get_esp32_variant,
    idf_version,
    include_builtin_idf_component,
)
from esphome.components.esp32.const import KEY_ESP32, KEY_SDKCONFIG_OPTIONS
from esphome.components.image import (
    CONF_OPAQUE,
    IMAGE_TYPE,
    ImageBinary,
    ImageGrayscale,
    ImageRGB,
    ImageRGB111,
    ImageRGB565,
    get_image_metadata,
)
from esphome.components.psram import DOMAIN as PSRAM_DOMAIN
import esphome.config_validation as cv
from esphome.const import (
    CONF_BUFFER_SIZE,
    CONF_BUILD_FLAGS,
    CONF_COMPRESSION,
    CONF_ESPHOME,
    CONF_GROUP,
    CONF_ID,
    CONF_LOG_LEVEL,
    CONF_ON_IDLE,
    CONF_PAGES,
    CONF_PLATFORMIO_OPTIONS,
    CONF_ROTATION,
    CONF_TIMEOUT,
    CONF_TRIGGER_ID,
    CONF_TYPE,
)
from esphome.core import CORE, ID, Lambda
from esphome.cpp_generator import MockObj
from esphome.final_validate import full_config
from esphome.helpers import write_file_if_changed
from esphome.schema_extractors import SCHEMA_EXTRACT, schema_extractor
from esphome.types import ConfigType
from esphome.writer import clean_build
from esphome.yaml_util import load_yaml

from . import defines as df, lv_validation as lvalid, widgets
from .animation import ANIMATION_SCHEMA, add_animation_triggers, animations_to_code
from .automation import layers_to_code, lvgl_update
from .defines import (
    CONF_ALIGN_TO_LAMBDA_ID,
    CONF_ANIMATIONS,
    CONF_DIRECT_MODE,
    LOGGER,
    add_lv_use,
    get_focused_widgets,
    get_lv_images_used,
    get_refreshed_widgets,
)
from .encoders import (
    ENCODERS_CONFIG,
    encoders_to_code,
    get_default_group,
    initial_focus_to_code,
)
from .gradient import GRADIENT_SCHEMA, gradients_to_code
from .keypads import KEYPADS_CONFIG, keypads_to_code
from .lv_validation import lv_bool
from .lvcode import LvContext, LvglComponent, lv_event_t_ptr, lvgl_static
from .navigation import (
    CONF_NAVIGATION,
    NAVIGATION_SCHEMA,
    navigation_automations_to_code,
    navigation_to_code,
)
from .schemas import (
    DISP_BG_SCHEMA,
    FULL_STYLE_SCHEMA,
    STYLE_REMAP,
    WIDGET_TYPES,
    any_widget_schema,
    apply_style_driven_defines,
    container_schema,
    container_schema_value,
    theme_schema,
)
from .snapshot import (
    COMPRESSION_JPEG,
    CONF_JPEG_INITIAL_OUTPUT_BUFFER_SIZE,
    CONF_SNAPSHOT_COMPOSITOR,
    SNAPSHOT_SCHEMA,
    snapshot_to_code,
)
from .styles import styles_to_code, theme_to_code
from .touchscreens import touchscreen_schema, touchscreens_to_code
from .trigger import generate_align_tos, generate_triggers
from .types import (
    IdleTrigger,
    PlainTrigger,
    RotationType,
    lv_font_t,
    lv_group_t,
    lv_lambda_t,
    lv_obj_t_ptr,
    lv_style_t,
    lvgl_ns,
)
from .widgets import (
    LvScrActType,
    Widget,
    add_widgets,
    get_screen_active,
    set_obj_properties,
)

# Import only what we actually use directly in this file
from .widgets.img import CONF_IMAGE
from .widgets.keyboard import attach_textareas
from .widgets.menu import add_root_back_button_triggers, validate_menu_pages
from .widgets.msgbox import MSGBOX_SCHEMA, msgboxes_to_code
from .widgets.obj import obj_spec  # Used in LVGL_SCHEMA
from .widgets.page import (  # page_spec used in LVGL_SCHEMA
    add_pages,
    generate_page_triggers,
    page_spec,
)


def _sdkconfig_bool(name: str, default: bool) -> bool:
    sdkconfig_options = CORE.data.get(KEY_ESP32, {}).get(KEY_SDKCONFIG_OPTIONS, {})
    value = sdkconfig_options.get(name)
    if value is None:
        return default
    return _config_bool(value)


def _config_bool(value) -> bool:
    if isinstance(value, bool):
        return value
    return str(value).lower() in ("1", "y", "yes", "true")


# Widget registration happens via WidgetType.__init__ in individual widget files
# The imports below trigger creation of the widget types
# Action registration (lvgl.{widget}.update) happens automatically
# in the WidgetType.__init__ method

for module_info in pkgutil.iter_modules(widgets.__path__):
    importlib.import_module(f".widgets.{module_info.name}", package=__package__)

DOMAIN = "lvgl"
DEPENDENCIES = ["display"]
AUTO_LOAD = ["key_provider", "button"]
CODEOWNERS = [
    "@clydebarrow"
]  # LVGL 9.5.0 implementation with ThorVG enabled by default
HELLO_WORLD_FILE = "hello_world.yaml"
CONF_USE_PPA = "use_ppa"
CONF_USE_PPA_IMG = "use_ppa_img"
CONF_USE_PPA_DRAW = "use_ppa_draw"
CONF_USE_PPA_BLEND = "use_ppa_blend"
CONF_PPA_BURST_LENGTH = "ppa_burst_length"
CONF_FPS_BENCHMARK = "fps_benchmark"
CONF_PERF_MONITOR = "perf_monitor"
CONF_PROFILER = "profiler"


SIMPLE_TRIGGERS = (
    df.CONF_ON_PAUSE,
    df.CONF_ON_RESUME,
    df.CONF_ON_DRAW_START,
    df.CONF_ON_DRAW_END,
    df.CONF_ON_LANDSCAPE,
    df.CONF_ON_PORTRAIT,
)


def as_macro(macro, value):
    if value is None:
        return f"#define {macro}"
    return f"#define {macro} {value}"


LVGL_VERSION = "9.6.0"
LV_CONF_FILENAME = "lv_conf.h"
LV_CONF_H_FORMAT = """\
#pragma once
{}
"""


def generate_lv_conf_h():
    # Get all possible LV_ config defines based on the widgets used in the config, and the standard LVGL options
    all_defines = set(
        df.LV_DEFINES + tuple(f"LV_USE_{w.upper()}" for w in WIDGET_TYPES)
    )
    esphome_config = CORE.config[CONF_ESPHOME]
    # User build flags come from esphome->build_flags and from the deprecated
    # esphome->platformio_options->build_flags (a string or a list).
    # Remove before 2026.12.0

    pio_build_flags = esphome_config.get(CONF_PLATFORMIO_OPTIONS, {}).get(
        CONF_BUILD_FLAGS, []
    )
    if not isinstance(pio_build_flags, list):
        pio_build_flags = [pio_build_flags]
    build_flags = [*esphome_config.get(CONF_BUILD_FLAGS, []), *pio_build_flags]
    # Extract define names from build flags like '-DLV_USE_CHART=1', '-D LV_USE_CHART',
    # or multiple defines in one string.
    define_pattern = r'-D\s*([A-Z_][A-Z0-9_]*)(?:=[^\s\'"\]]*)?'
    defines_from_flags = {
        m.group(1) for flag in build_flags for m in re.finditer(define_pattern, flag)
    }

    # Get the defines that are actually used based on the config,
    lv_defines = df.get_defines()
    clashes = defines_from_flags & lv_defines.keys()
    if clashes:
        LOGGER.warning(
            "Some defines are set both by ESPHome build flags and by LVGL configuration which may lead to unexpected behavior: %s",
            sorted(clashes),
        )
    unused_defines = all_defines - lv_defines.keys() - defines_from_flags

    # Create the content of lv_conf.h with the used defines set to their value, and the unused defines disabled
    definitions = [as_macro(m, v) for m, v in lv_defines.items()] + [
        as_macro(m, "0") for m in unused_defines
    ]
    definitions.sort()
    return LV_CONF_H_FORMAT.format("\n".join(definitions))


def multi_conf_validate(configs: list[dict]):
    displays = [config[df.CONF_DISPLAYS] for config in configs]
    # flatten the display list
    display_list = [disp for disps in displays for disp in disps]
    if len(display_list) != len(set(display_list)):
        raise cv.Invalid("A display ID may be used in only one LVGL instance")
    for config in configs:
        for item in (df.CONF_ENCODERS, df.CONF_KEYPADS):
            for enc in config.get(item, ()):
                if CONF_GROUP not in enc:
                    raise cv.Invalid(
                        f"'{item}' must have an explicit group set when using multiple LVGL instances"
                    )
    # The hidden styles a `theme:` block creates are tracked in a single map shared
    # by all LVGL instances (keyed only by widget type, not by instance), so a
    # second instance's `theme:` would silently lose to whichever instance is
    # processed first instead of doing what its config implies.
    themed_configs = sum(
        1 for config in configs if config.get(df.CONF_THEME) is not None
    )
    if themed_configs > 1:
        raise cv.Invalid(
            "'theme' may only be set on one LVGL instance when using multiple LVGL "
            "instances -- combine both themes into a single instance's 'theme:' block"
        )
    base_config = configs[0]
    for config in configs[1:]:
        for item in (
            CONF_LOG_LEVEL,
            CONF_COLOR_DEPTH,
            CONF_BYTE_ORDER,
            df.CONF_TRANSPARENCY_KEY,
            df.CONF_DEBUG_OUTLINE,
        ):
            if base_config[item] != config[item]:
                raise cv.Invalid(
                    f"Config item '{item}' must be the same for all LVGL instances"
                )


def final_validation(config_list):
    global_config = full_config.get()
    # Resolve byte_order from display metadata before multi-config validation
    for config in config_list:
        metas = [get_display_metadata(disp) for disp in config[df.CONF_DISPLAYS]]
        if any(m.has_writer for m in metas):
            raise cv.Invalid(
                "Using lambda:, pages:, auto_clear_enabled: true, or show_test_card: true in display config is not compatible with LVGL"
            )
        if any(m.rotation != 0 for m in metas):
            raise cv.Invalid(
                "use of 'rotation' in the display config is not compatible with LVGL, please set rotation in the LVGL config instead"
            )
        config[CONF_DRAW_ROUNDING] = max(
            [m.draw_rounding for m in metas] + [config[CONF_DRAW_ROUNDING]]
        )
        display_byte_orders = {
            m.byte_order for m in metas if m.byte_order is not cv.UNDEFINED
        }
        if len(display_byte_orders) > 1:
            raise cv.Invalid(
                "All displays configured for an LVGL instance must use the same byte_order"
            )
        if display_byte_orders:
            display_order = next(iter(display_byte_orders))
            if CONF_BYTE_ORDER in config:
                if config[CONF_BYTE_ORDER] != display_order:
                    raise cv.Invalid(
                        "LVGL byte order must match the display byte order",
                        [CONF_BYTE_ORDER],
                    )
            else:
                config[CONF_BYTE_ORDER] = display_order
        if CONF_BYTE_ORDER not in config:
            config[CONF_BYTE_ORDER] = BYTE_ORDER_BIG

        if (pages := config.get(CONF_PAGES)) and all(p[df.CONF_SKIP] for p in pages):
            raise cv.Invalid("At least one page must not be skipped")

        # Resolve color_depth before the display loop.
        # If the user didn't set it, auto-detect from the first MIPI DSI display found.
        user_set_depth = CONF_COLOR_DEPTH in config
        has_mipi = any(
            global_config.get_config_for_path(
                global_config.get_path_for_id(did)[:-1]
            ).get("platform", "")
            == "mipi_dsi"
            for did in config[df.CONF_DISPLAYS]
        )
        if not user_set_depth:
            if has_mipi:
                config[CONF_COLOR_DEPTH] = 32
                df.LOGGER.info(
                    "MIPI DSI display detected: auto-selecting color_depth=32 (RGB888). "
                    "Set 'color_depth: 16' in your lvgl: config to use RGB565 instead."
                )
            else:
                config[CONF_COLOR_DEPTH] = 16
        if user_set_depth and config[CONF_COLOR_DEPTH] == 16 and has_mipi:
            df.LOGGER.warning(
                "color_depth: 16 (RGB565) with MIPI DSI: PPA acceleration may be "
                "reduced. Consider color_depth: 32 for best performance."
            )
        if config.get(CONF_DIRECT_MODE, False):
            if config[CONF_BUFFER_SIZE] != 1:
                raise cv.Invalid(
                    "Direct mode requires a full-screen buffer", [CONF_BUFFER_SIZE]
                )
            if config.get(CONF_ROTATION, 0) != 0 or df.get_options().get(CONF_ROTATION):
                raise cv.Invalid(
                    "Direct mode does not support software rotation", [CONF_ROTATION]
                )
            if len(metas) != 1 or metas[0].frame_buffer_count < 2:
                raise cv.Invalid(
                    "Direct mode requires at least two native framebuffers"
                )
            expected_bytes = 3 if config[CONF_COLOR_DEPTH] == 32 else 2
            if metas[0].frame_buffer_bytes_per_pixel != expected_bytes:
                raise cv.Invalid(
                    "Direct mode requires LVGL and display color depths to match"
                )
        buffer_frac = config[CONF_BUFFER_SIZE]
        if CORE.is_esp32 and buffer_frac > 0.5 and PSRAM_DOMAIN not in global_config:
            df.LOGGER.warning("buffer_size: may need to be reduced without PSRAM")

    if len(config_list) != 1:
        multi_conf_validate(config_list)

    for img in get_lv_images_used():
        path = global_config.get_path_for_id(img.id)[:-1]
        # An image nested in another platform (sendspin's current_image) takes its type from the parent
        while path:
            image_conf = global_config.get_config_for_path(path)
            if isinstance(image_conf, dict) and CONF_TYPE in image_conf:
                break
            path = path[:-1]
        else:
            continue
        if IMAGE_TYPE.get(image_conf[CONF_TYPE]) is ImageRGB111:
            raise cv.FinalExternalInvalid(
                f"Image '{img}': LVGL does not support RGB111 images, use RGB565 or RGB instead",
                path + [CONF_TYPE],
            )
    for w in get_focused_widgets():
        path = global_config.get_path_for_id(w)
        widget_conf = global_config.get_config_for_path(path[:-1])
        if df.CONF_ADJUSTABLE in widget_conf and not widget_conf[df.CONF_ADJUSTABLE]:
            raise cv.Invalid(
                "A non adjustable arc may not be focused",
                path,
            )
    for w in get_refreshed_widgets():
        path = global_config.get_path_for_id(w)
        widget_conf = global_config.get_config_for_path(path[:-1])
        if not any(isinstance(v, (Lambda, dict)) for v in widget_conf.values()):
            raise cv.Invalid(
                f"Widget '{w}' does not have any dynamic properties to refresh",
            )
    # Do per-widget type final validation for update actions
    for widget_type, update_configs in df.get_updated_widgets().items():
        for conf in update_configs:
            for id_conf in conf.get(CONF_ID, ()):
                name = id_conf[CONF_ID]
                path = global_config.get_path_for_id(name)
                widget_conf = global_config.get_config_for_path(path[:-1])
                widget_type.final_validate(name, conf, widget_conf, path[1:])
    validate_menu_pages(global_config)


def lv_conf_path_flag(lv_conf_h_file: Path) -> str:
    """The ``-DLV_CONF_PATH`` build flag, decoded identically by the shell
    lexers (PlatformIO, host, ESP8266) and the quoted CMake arguments
    (ESP-IDF, Zephyr) the build flags pass through."""
    # forward slashes for Windows, then escape what shlex or a quoted CMake
    # argument would not pass through literally
    path = re.sub(r"[^\w/.:+=,-]", r"\\\g<0>", Path(lv_conf_h_file).as_posix())
    return f'-DLV_CONF_PATH=\\"{path}\\"'


async def to_code(configs):
    config_0 = configs[0]
    snapshot_config = config_0.get(CONF_SNAPSHOT_COMPOSITOR)
    # Global configuration
    if CORE.is_esp32:
        # Skip compiling LVGL examples and demos; ESPHome builds provide their
        # own UI tree and these sources only add compile time/flash pressure.
        add_idf_sdkconfig_option("CONFIG_LV_BUILD_EXAMPLES", False)
        add_idf_sdkconfig_option("CONFIG_LV_BUILD_DEMOS", False)
        # Match LV_USE_STDLIB_MALLOC in lv_conf.h. This also drops the builtin malloc
        # options, which LVGL 9.5 builds leave set to deprecated values.
        include_builtin_idf_component("esp_driver_jpeg")
        add_idf_sdkconfig_option("CONFIG_LV_USE_CUSTOM_MALLOC", True)
        if get_esp32_variant() == VARIANT_ESP32P4:
            add_idf_sdkconfig_option("CONFIG_LV_DRAW_BUF_ALIGN", 64)
            # The LVGL PPA driver uses cache functions added in ESP-IDF 6.0
            if idf_version() >= cv.Version(6, 0, 0):
                df.add_define("LV_USE_PPA", "1")
            else:
                df.add_define("LV_USE_PPA", "0")
            df.add_define("LV_DRAW_BUF_ALIGN", "64")
        else:
            df.add_define("LV_DRAW_BUF_ALIGN", "32")
        add_idf_component(name="lvgl/lvgl", ref=LVGL_VERSION)
    else:
        df.add_define("LV_DRAW_BUF_ALIGN", "1")
        cg.add_library("lvgl/lvgl", LVGL_VERSION)
    df.add_define("LV_DRAW_BUF_STRIDE_ALIGN", "1")
    df.add_define("LV_USE_DRAW_SW", "1")
    # LVGL 9.6 rejects 0 here unless tiled rendering is disabled
    df.add_define("LV_DRAW_SW_DRAW_UNIT_CNT", "1")
    df.add_define("LV_USE_STDLIB_SPRINTF", "LV_STDLIB_CLIB")
    df.add_define("LV_USE_STDLIB_STRING", "LV_STDLIB_CLIB")
    df.add_define("LV_USE_STDLIB_MALLOC", "LV_STDLIB_CUSTOM")
    df.add_define("LV_DEF_REFR_PERIOD", "16")
    cg.add_define("USE_LVGL")
    if snapshot_config is not None:
        cg.add_define("USE_LVGL_SNAPSHOT_STORE")
        if initial_size := snapshot_config[CONF_JPEG_INITIAL_OUTPUT_BUFFER_SIZE]:
            cg.add_define("LVGL_SNAPSHOT_JPEG_INITIAL_OUTPUT_BUFFER_SIZE", initial_size)
        df.add_define("LV_USE_SNAPSHOT", "1")
        add_lv_use(CONF_IMAGE)
        if snapshot_config[CONF_COMPRESSION] == COMPRESSION_JPEG:
            cg.add_define("USE_LVGL_SNAPSHOT_JPEG_CACHE")

    # Add build filter to exclude LVGL platform code not needed for ESP32
    # This reduces compilation time and binary size significantly
    build_filter_script = Path(__file__).parent / "lvgl_build_filter.py"
    cg.add_platformio_option("extra_scripts", [f"pre:{build_filter_script}"])

    # Define ESPHOME_ENTITY_BUTTON_COUNT for ESPHome core compatibility
    # application.h requires this symbol even when no button entities exist.
    # When the user configures button entities, ESPHome core already emits the
    # correct count in defines.h, so we must avoid redefining it (which would
    # trigger a -Wmacro-redefined warning). Only add the 0 fallback when no
    # button platform is configured.
    if not CORE.config.get("button"):
        cg.add_define("ESPHOME_ENTITY_BUTTON_COUNT", 0)

    # suppress default enabling of extra widgets
    df.add_define("_LV_KCONFIG_PRESENT")
    # Rounded cards, arcs, slider tracks/knobs and other Material-style surfaces
    # require LVGL's complex software renderer. Keep it explicit because the
    # generated lv_conf.h disables every unused LV_* option by default.
    df.add_define("LV_USE_DRAW_SW", "1")
    df.add_define("LV_DRAW_SW_DRAW_UNIT_CNT", "1")
    df.add_define("LV_DRAW_SW_COMPLEX", "1")
    # Production firmware: keep LVGL's runtime hot path lean. These checks
    # are useful while porting, but they add work to every render/touch cycle.
    df.add_define("LV_USE_ASSERT_NULL", "0")
    df.add_define("LV_USE_ASSERT_MALLOC", "0")
    df.add_define("LV_USE_ASSERT_STYLE", "0")
    df.add_define("LV_USE_ASSERT_MEM_INTEGRITY", "0")
    df.add_define("LV_USE_ASSERT_OBJ", "0")
    # Memory alignment configuration for LVGL 9.5
    df.add_define("LV_DRAW_BUF_STRIDE_ALIGN", "1")  # LVGL default
    # Keep LV_DRAW_BUF_ALIGN at LVGL default (4). Setting higher values
    # causes crashes because LVGL's internal stack/static buffers can't meet
    # stricter alignment. The custom lv_malloc_core() already provides 64-byte
    # aligned heap allocations for the actual draw buffers on ESP32.
    if any(
        config_0.get(key, False)
        for key in (
            CONF_USE_PPA,
            CONF_USE_PPA_IMG,
            CONF_USE_PPA_DRAW,
            CONF_USE_PPA_BLEND,
        )
    ):
        df.get_defines()["LV_DRAW_BUF_ALIGN"] = "4"
    use_ppa = config_0.get(CONF_USE_PPA, False)
    use_ppa_img = config_0.get(CONF_USE_PPA_IMG, False)
    use_ppa_draw = config_0.get(CONF_USE_PPA_DRAW, False)
    use_ppa_blend = config_0.get(CONF_USE_PPA_BLEND, False)

    # Image acceleration is implemented by the PPA draw unit.
    if use_ppa_img:
        use_ppa_draw = True

    # Any PPA sub-path needs the common PPA client code.
    if use_ppa_img or use_ppa_draw or use_ppa_blend:
        use_ppa = True

    if use_ppa:
        # The optional local draw unit owns PPA registration; never instantiate
        # the native LVGL PPA unit as well. Its IDF 6 cache API differs.
        df.add_define("LV_USE_PPA", "0")
        if CORE.using_toolchain_esp_idf:
            add_idf_component(
                name="lvgl_source_patch",
                path=(Path(__file__).parent / "lvgl_source_patch").as_posix(),
            )
        # LVGL 9.5 includes the PPA fix (PR #9162) natively.
        # We keep our custom PPA files as a fallback option.
        # PPA evaluate checks buffer alignment at runtime before claiming tasks.
        df.add_define("LV_PPA_BURST_LENGTH", str(config_0[CONF_PPA_BURST_LENGTH]))
        cg.add_define("USE_LVGL_PPA")
        ppa_dir = Path(__file__).parent / "ppa"
        cg.add_build_flag(f"-I{ppa_dir.as_posix()}")
    if use_ppa_draw:
        cg.add_define("USE_LVGL_PPA_DRAW_UNIT")
    if use_ppa_blend:
        cg.add_define("USE_LVGL_PPA_BLEND_HANDLER")
    if use_ppa_img:
        # Enable PPA SRM hardware rotation for images (0/90/180/270 degrees)
        cg.add_define("LV_USE_PPA_IMG")
    use_fps_benchmark = _config_bool(config_0.get(CONF_FPS_BENCHMARK, False))
    use_perf_monitor = _config_bool(config_0.get(CONF_PERF_MONITOR, False))
    use_profiler = _config_bool(config_0.get(CONF_PROFILER, False))

    if use_fps_benchmark:
        # Espressif esp_lvgl_adapter FPS sampler (P10/25/50/75/90 report).
        # lvgl_fps_benchmark_wrapper.cpp includes esphome/core/defines.h
        # and conditionally pulls in the .c — ESPHome only auto-compiles
        # .cpp at the component root.
        cg.add_define("USE_LVGL_FPS_BENCHMARK")
    if use_perf_monitor:
        # On-screen FPS/CPU overlay (bottom-right corner, native LVGL widget).
        # Perf monitor is gated by LV_USE_SYSMON in lv_conf_internal.h, which
        # is itself gated by LV_USE_LOG (text rendering for the label).
        df.add_define("LV_USE_LOG", "1")
        df.add_define("LV_USE_SYSMON", "1")
        df.add_define("LV_USE_PERF_MONITOR", "1")
        df.add_define("LV_USE_PERF_MONITOR_POS", "LV_ALIGN_BOTTOM_RIGHT")
        df.add_define("LV_USE_PERF_MONITOR_LOG_MODE", "0")
        # Signal lvgl_build_filter.py to keep src/debugging/sysmon/* sources
        # (excluded by default to save flash).
        cg.add_build_flag("-DLVGL_USE_SYSMON=1")
        # Linker wrap: sysmon reads idle%% via lv_timer_get_idle() under
        # LV_OS_NONE or lv_os_get_idle_percent() under LV_OS_FREERTOS.
        # Wrap both — our matching __wrap_* live in lvgl_esphome.cpp and
        # return 100 - our_cpu_pct so the overlay reads the same value
        # as the '[D][lvgl]: perf:' log (flush wait excluded).
        cg.add_build_flag("-Wl,--wrap=lv_timer_get_idle")
        cg.add_build_flag("-Wl,--wrap=lv_os_get_idle_percent")
    if use_profiler:
        # LVGL built-in systrace profiler. Runtime is controlled by
        # lvgl_esphome_set_profiler_enabled() so production firmware can keep
        # the instrumentation compiled out or idle.
        df.add_define("LV_USE_LOG", "1")
        df.add_define("LV_USE_PROFILER", "1")
        df.add_define("LV_USE_PROFILER_BUILTIN", "1")
        # Keep the built-in systrace buffer small enough for ESP32-P4 builds
        # with large LVGL/voice/audio firmware. The custom flush callback
        # aggregates profiler entries, so we do not need a huge in-RAM trace.
        df.add_define("LV_PROFILER_BUILTIN_BUF_SIZE", "(64 * 1024)")
        df.add_define("LV_PROFILER_BUILTIN_DEFAULT_ENABLE", "0")
        df.add_define("LV_USE_PROFILER_BUILTIN_POSIX", "0")
        df.add_define("LV_PROFILER_INCLUDE", '"misc/lv_profiler_builtin.h"')
        df.add_define("LV_PROFILER_BEGIN", "LV_PROFILER_BUILTIN_BEGIN")
        df.add_define("LV_PROFILER_END", "LV_PROFILER_BUILTIN_END")
        df.add_define("LV_PROFILER_BEGIN_TAG", "LV_PROFILER_BUILTIN_BEGIN_TAG")
        df.add_define("LV_PROFILER_END_TAG", "LV_PROFILER_BUILTIN_END_TAG")
        df.add_define("LV_PROFILER_LAYOUT", "1")
        df.add_define("LV_PROFILER_REFR", "1")
        df.add_define("LV_PROFILER_DRAW", "1")
        df.add_define("LV_PROFILER_INDEV", "0")
        df.add_define("LV_PROFILER_TIMER", "1")
        df.add_define("LV_PROFILER_EVENT", "0")
        df.add_define("LV_PROFILER_FONT", "0")
        df.add_define("LV_PROFILER_DECODER", "0")
        df.add_define("LV_PROFILER_CACHE", "0")
        df.add_define("LV_PROFILER_FS", "0")
        df.add_define("LV_PROFILER_STYLE", "0")
        cg.add_build_flag("-DLVGL_USE_PROFILER=1")
    df.add_define("LV_USE_STDLIB_MALLOC", "LV_STDLIB_CUSTOM")

    # ============================================
    # FEATURES DISABLED BY DEFAULT (enabled conditionally below)
    # ============================================
    # ThorVG, SVG, Lottie, Vector Graphics, Float, Matrix are ONLY
    # enabled when the user actually uses svg: or lottie: widgets.
    # This saves ~500KB-1MB of flash on ESP32 devices with limited storage.
    # The conditional activation happens after widget processing (see below).

    # Explicitly disable heavy features by default - they are enabled
    # conditionally after widget processing if the user actually needs them.
    df.add_define("LV_USE_LIBPNG", "0")
    df.add_define("LV_USE_LIBWEBP", "0")

    # Enable FreeRTOS threading for LVGL draw operations.
    # Required by ThorVG / Lottie which render off the main LVGL task and
    # rely on the OS abstraction's mutexes. Switching to LV_OS_NONE here
    # makes those mutexes no-op and crashes the firmware at boot
    # (OTA rolls back).
    # Side effect: LVGL sysmon's CPU%% overlay reads 100%% — a known LVGL
    # quirk with LV_OS_FREERTOS on ESPHome. Ignore the displayed CPU%%;
    # the FPS and ms numbers are still accurate.
    df.add_define("LV_USE_OS", "LV_OS_FREERTOS")

    # Refresh period: 10 ms ≈ 100 Hz attempt rate. This doesn't force
    # higher FPS, but gives touch/animation timers more chances to catch
    # the panel refresh cadence when the render loop is not saturated.
    if any(
        config_0.get(key, False)
        for key in (
            CONF_USE_PPA,
            CONF_USE_PPA_IMG,
            CONF_USE_PPA_DRAW,
            CONF_USE_PPA_BLEND,
        )
    ):
        df.get_defines()["LV_DEF_REFR_PERIOD"] = "10"

    # Used by swipe transitions to animate static page images instead of
    # re-rendering the full live widget tree on every touch update.
    df.add_define("LV_USE_SNAPSHOT", "1")

    # LVGL 9.5: Enable blur/frosted glass support (small code, useful for shadows)
    df.add_define("LV_USE_DRAW_SW_BLUR", "1")

    lv_use_log = (
        use_perf_monitor or use_profiler or _sdkconfig_bool("CONFIG_LV_USE_LOG", True)
    )
    lv_log_level = (
        f"LV_LOG_LEVEL_{df.LV_LOG_LEVELS[config_0[CONF_LOG_LEVEL]]}"
        if lv_use_log
        else "LV_LOG_LEVEL_NONE"
    )
    df.add_define("LV_LOG_LEVEL", lv_log_level)
    if lv_use_log:
        df.add_define("LV_USE_LOG", "1")
    cg.add_define(
        "LVGL_LOG_LEVEL",
        cg.RawExpression(f"ESPHOME_LOG_LEVEL_{config_0[CONF_LOG_LEVEL]}"),
    )
    # Big-endian displays get LVGL to render directly in swapped byte order
    display_format = (
        "RGB888"
        if config_0[CONF_COLOR_DEPTH] == 32
        else (
            "RGB565_SWAPPED"
            if config_0[CONF_BYTE_ORDER] == BYTE_ORDER_BIG
            else "RGB565"
        )
    )
    if config_0[CONF_COLOR_DEPTH] == 32:
        df.add_define("LV_COLOR_DEPTH", 32)
    df.add_define("LV_COLOR_FORMAT_DEFAULT", f"LV_COLOR_FORMAT_{display_format}")
    for font in df.get_lv_fonts_used():
        df.add_define(f"LV_FONT_{font.upper()}")
    df.add_define("LV_OBJ_STYLE_CACHE", "1")
    if any(config[df.CONF_CHECK_ARGS] for config in configs):
        df.add_define("LV_USE_CHECK_ARG", "1")
        df.add_define(
            "LV_CHECK_ARG_LOG_MODE",
            f"LV_CHECK_ARG_LOG_MODE_{df.LV_CHECK_ARG_LOG_MODES[config_0[CONF_LOG_LEVEL]]}",
        )
    else:
        df.add_define("LV_USE_CHECK_ARG", "0")
    df.add_define(
        "LV_COLOR_CHROMA_KEY",
        await lvalid.lv_color.process(config_0[df.CONF_TRANSPARENCY_KEY]),
    )
    cg.add_build_flag("-Isrc")

    cg.add_global(lvgl_ns.using)
    for font in df.get_esphome_fonts_used():
        await cg.get_variable(font)
    default_font = config_0[df.CONF_DEFAULT_FONT]
    if not lvalid.is_lv_font(default_font):
        df.add_define(
            "LV_FONT_CUSTOM_DECLARE", f"LV_FONT_DECLARE(*{df.DEFAULT_ESPHOME_FONT})"
        )
        globfont_id = ID(
            df.DEFAULT_ESPHOME_FONT,
            True,
            type=lv_font_t.operator("ptr").operator("const"),
        )
        # static=False because LV_FONT_CUSTOM_DECLARE creates an extern declaration
        cg.new_variable(
            globfont_id,
            MockObj(await lvalid.lv_font.process(default_font), "->").get_lv_font(),
            static=False,
        )
        df.add_define("LV_FONT_DEFAULT", df.DEFAULT_ESPHOME_FONT)
    else:
        df.add_define("LV_FONT_DEFAULT", await lvalid.lv_font.process(default_font))
    cg.add(lvgl_static.esphome_lvgl_init())
    default_group = get_default_group(config_0)
    df.get_options()[df.CONF_DEBUG_OUTLINE] = config_0[df.CONF_DEBUG_OUTLINE]

    # Create theme lambdas before any widgets.
    async with LvContext():
        for config in configs:
            await theme_to_code(config)

    for config in configs:
        frac = config[CONF_BUFFER_SIZE]
        if frac >= 0.75:
            frac = 1
        elif frac >= 0.375:
            frac = 2
        elif frac >= 0.19:
            frac = 4
        elif frac >= 0.095:
            frac = 8
        elif frac >= 0.0475:
            frac = 16
        elif frac >= 0.023:
            frac = 32
        elif frac != 0:
            frac = 64
        displays = [
            await cg.get_variable(display) for display in config[df.CONF_DISPLAYS]
        ]
        rotation_type = RotationType.ROTATION_UNUSED
        # options will have CONF_ROTATION true if rotation is changed in an automation.
        if CONF_ROTATION in config or df.get_options().get(CONF_ROTATION) is True:
            if all(
                get_display_metadata(disp).has_hardware_rotation for disp in displays
            ):
                rotation_type = RotationType.ROTATION_HARDWARE
                df.LOGGER.info("LVGL will use hardware rotation via display driver")
            else:
                rotation_type = RotationType.ROTATION_SOFTWARE
                if CORE.is_esp32 and get_esp32_variant() == VARIANT_ESP32P4:
                    df.LOGGER.info("LVGL will use software rotation (PPA accelerated)")
                else:
                    df.LOGGER.info("LVGL will use software rotation")
        lv_component = cg.new_Pvariable(
            config[CONF_ID],
            displays,
            frac,
            config[df.CONF_FULL_REFRESH],
            config[df.CONF_DIRECT_MODE],
            config[CONF_DRAW_ROUNDING],
            config[df.CONF_RESUME_ON_INPUT],
            config[df.CONF_UPDATE_WHEN_DISPLAY_IDLE],
            rotation_type,
        )
        await cg.register_component(lv_component, config)
        if rotation := config.get(CONF_ROTATION):
            cg.add(lv_component.set_rotation(rotation))
        if paused := config[df.CONF_PAUSED]:
            cg.add(lv_component.set_paused(paused, False))
        if refr_time := config.get(df.CONF_REFRESH_INTERVAL):
            cg.add(lv_component.set_refresh_interval(refr_time.total_milliseconds))
        Widget.create(config[CONF_ID], lv_component, LvScrActType(), config)

        lv_scr_act = get_screen_active(lv_component)
        async with LvContext():
            cg.add(lv_component.set_big_endian(config[CONF_BYTE_ORDER] == "big_endian"))
            await touchscreens_to_code(lv_component, config)
            await encoders_to_code(lv_component, config, default_group)
            await keypads_to_code(lv_component, config, default_group)
            await gradients_to_code(config)
            await styles_to_code(config)
            await set_obj_properties(lv_scr_act, config)
            await add_widgets(lv_scr_act, config)
            await add_pages(lv_component, config)
            # Navigation may reference blockers or indicators hosted on a display
            # layer, so all layer widgets must exist before resolving navigation.
            await layers_to_code(lv_component, config)
            await navigation_to_code(lv_component, config)
            await snapshot_to_code(
                lv_component,
                config,
                config.get(CONF_NAVIGATION),
            )
            await lvgl_update(lv_component, config)
            await msgboxes_to_code(lv_component, config)
            await animations_to_code(config.get(CONF_ANIMATIONS, []))

    for config in configs:
        CORE.add_job(navigation_automations_to_code, config)
    async with LvContext():
        # Local import to avoid circularity
        from .widgets.lv_list import finish_list_triggers

        await finish_list_triggers()
        await generate_triggers()
        await add_root_back_button_triggers()
        await generate_align_tos(configs[0])
        await attach_textareas()
        for config in configs:
            lv_component = await cg.get_variable(config[CONF_ID])
            await add_animation_triggers(config.get(CONF_ANIMATIONS, []))
            await generate_page_triggers(config)
            await initial_focus_to_code(config)
            for conf in config.get(CONF_ON_IDLE, ()):
                templ = await cg.templatable(conf[CONF_TIMEOUT], [], cg.uint32)
                idle_trigger = cg.new_Pvariable(
                    conf[CONF_TRIGGER_ID], lv_component, templ
                )
                await build_automation(idle_trigger, [], conf)
            for trigger_name in SIMPLE_TRIGGERS:
                if conf := config.get(trigger_name):
                    trigger_var = cg.new_Pvariable(conf[CONF_TRIGGER_ID])
                    await build_automation(trigger_var, [], conf)
                    cg.add(
                        getattr(
                            lv_component,
                            f"set_{trigger_name.removeprefix('on_')}_trigger",
                        )(trigger_var)
                    )

    # This must be done after all widgets are created
    styles_used = df.get_styles_used()
    apply_style_driven_defines(styles_used)
    lv_uses = df.get_lv_uses()
    normalized_lv_uses = {use.upper() for use in lv_uses}
    if "LOTTIE" in normalized_lv_uses:
        # LVGL's Lottie widget is implemented on top of canvas and image.
        # Keep both the generated LV_USE_* defines and the source filter in
        # sync even when the widget registered its use with a different case.
        lv_uses.update({"canvas", "image"})
        normalized_lv_uses.update({"CANVAS", "IMAGE"})
    # Currently always need RGB565 for the display buffer, and ARGB8888 is used for layer blending.
    lv_image_formats = {"RGB565", display_format, "ARGB8888"}

    if {
        "transform_rotation",
        "transform_scale",
        "transform_scale_x",
        "transform_scale_y",
    } & styles_used:
        df.add_define("LV_COLOR_SCREEN_TRANSP", "1")
    # ============================================
    # LV_USE_* WIDGET DEFINES
    # ============================================
    # LVGL's lv_conf_internal.h defaults LV_USE_<WIDGET>=1 for all widgets.
    # lv_theme_default.c references widget _class symbols guarded by these defines.
    # If we exclude a widget's source file via the build filter but don't set
    # LV_USE_<WIDGET>=0, the theme still references the symbol -> linker error.
    #
    # IMPORTANT: Use CANONICAL define names (the ones with #ifndef guards in
    # lv_conf_internal.h). LVGL 9.5 uses the long widget names for most widgets,
    # while ESPHome and old LVGL aliases can still use short names such as
    # "btn", "img" and "imgbtn".
    #
    # ESPHome lv_uses may contain both long names (from widget self.name, e.g. "button")
    # and short names (from get_uses(), e.g. "btn"). Map all to canonical.
    _TO_CANONICAL = {
        "BTN": "BUTTON",
        "BTNMATRIX": "BUTTONMATRIX",
        "IMG": "IMAGE",
        "IMGBTN": "IMAGEBUTTON",
        "ANIMIMAGE": "ANIMIMG",
        "SPANGROUP": "SPAN",
        "METER": "SCALE",
    }
    # All canonical LV_USE_* widget define names in LVGL v9.x
    _ALL_CANONICAL_WIDGETS = {
        "ANIMIMG",
        "ARC",
        "BAR",
        "BUTTON",
        "BUTTONMATRIX",
        "CALENDAR",
        "CANVAS",
        "CHART",
        "CHECKBOX",
        "DROPDOWN",
        "IMAGE",
        "IMAGEBUTTON",
        "KEYBOARD",
        "LABEL",
        "LED",
        "LINE",
        "LIST",
        "MENU",
        "MSGBOX",
        "ROLLER",
        "SCALE",
        "SLIDER",
        "SPAN",
        "SPINBOX",
        "SPINNER",
        "SWITCH",
        "TABLE",
        "TABVIEW",
        "TEXTAREA",
        "TILEVIEW",
        "WIN",
    }

    # Add ESPHome-specific defines; add LV_USE_* only for non-widget entries
    for use in lv_uses:
        upper = use.upper()
        cg.add_define(f"USE_LVGL_{upper}")
        # LVGL's switch widget collides with ESPHome's switch entity define.
        # Keep USE_LVGL_SWITCH/LV_USE_SWITCH, but don't enable the entity API.
        if upper != "SWITCH":
            cg.add_define(f"USE_{upper}")
        canonical = _TO_CANONICAL.get(upper, upper)
        if canonical not in _ALL_CANONICAL_WIDGETS:
            # Non-widget entry (e.g. LOG, THEME_DEFAULT, USER_DATA)
            df.add_define(f"LV_USE_{upper}")

    # Determine which canonical widget defines are needed
    _used_canonical = set()
    for use in lv_uses:
        canonical = _TO_CANONICAL.get(use.upper(), use.upper())
        if canonical in _ALL_CANONICAL_WIDGETS:
            _used_canonical.add(canonical)

    # lv_theme_default.c references lv_buttonmatrix_class unconditionally,
    # so buttonmatrix must always be compiled even if not used in the YAML.
    _THEME_REQUIRED_WIDGETS = {"BUTTONMATRIX"}
    _used_canonical |= _THEME_REQUIRED_WIDGETS
    # Also add to lv_uses so the build filter includes the source files
    for w in _THEME_REQUIRED_WIDGETS:
        _reverse = {v: k for k, v in _TO_CANONICAL.items()}
        long_name = _reverse.get(w, w).lower()
        lv_uses.add(long_name)

    # Set LV_USE_*=1 for used widgets, LV_USE_*=0 for unused (canonical names only)
    for widget in _ALL_CANONICAL_WIDGETS:
        df.add_define(f"LV_USE_{widget}", "1" if widget in _used_canonical else "0")

    # ============================================
    # CONDITIONAL HEAVY FEATURES (based on widget usage)
    # ============================================
    # Only enable ThorVG/SVG/Lottie/Vector Graphics if actually needed.
    # This saves ~500KB-1MB of flash on ESP32 devices.
    needs_thorvg = bool(
        {"THORVG_INTERNAL", "SVG", "LOTTIE", "VECTOR_GRAPHIC"} & normalized_lv_uses
    )

    if needs_thorvg:
        df.add_define("LV_USE_DRAW_SW", "1")
        df.add_define("LV_DRAW_SW_DRAW_UNIT_CNT", "1")
        df.add_define("LV_USE_FLOAT", "1")
        df.add_define("LV_USE_MATRIX", "1")
        df.add_define("LV_USE_VECTOR_GRAPHIC", "1")
        df.add_define("LV_USE_THORVG", "1")
        df.add_define("LV_USE_THORVG_INTERNAL", "1")
        df.add_define("LV_VG_LITE_THORVG_16PIXELS_ALIGN", "1")
        # Large stack for ThorVG rendering
        df.add_define("LV_DRAW_THREAD_STACK_SIZE", "(48 * 1024)")
        # Signal to lvgl_build_filter.py to compile ThorVG sources
        cg.add_build_flag("-DLVGL_USE_THORVG=1")
        df.LOGGER.info("ThorVG enabled (SVG/Lottie widgets detected)")
    else:
        df.add_define("LV_USE_FLOAT", "0")
        df.add_define("LV_USE_MATRIX", "0")
        df.add_define("LV_USE_VECTOR_GRAPHIC", "0")
        df.add_define("LV_USE_THORVG", "0")
        df.add_define("LV_USE_THORVG_INTERNAL", "0")
        df.add_define("LV_USE_SVG", "0")
        df.add_define("LV_USE_LOTTIE", "0")
        # Smaller stack when ThorVG is not used
        df.add_define("LV_DRAW_THREAD_STACK_SIZE", "(8 * 1024)")
        df.LOGGER.info("ThorVG disabled (no SVG/Lottie widgets) - saving ~500KB flash")

    # Image decoders: BMP and GIF are small, enable if image widget is used
    # lv_uses stores names as-is from add_lv_use(): lowercase from widgets, uppercase from helpers.py
    if "image" in lv_uses or "img" in lv_uses or "animimg" in lv_uses:
        df.add_define("LV_USE_BMP", "1")
        df.add_define("LV_USE_GIF", "1")
    else:
        df.add_define("LV_USE_BMP", "0")
        df.add_define("LV_USE_GIF", "0")

    # Signal to lvgl_build_filter.py which widgets are used, so it can
    # skip compiling LVGL widget source files that aren't needed.
    # Format: comma-separated list of lowercase LVGL widget names.
    widget_names = ",".join(sorted(use.lower() for use in lv_uses))
    cg.add_build_flag(f'-DLVGL_WIDGETS_USED=\\"{widget_names}\\"')

    # A few optional LVGL diagnostic paths force LV_USE_LOG while building
    # their own support code. For production builds that explicitly disable
    # LVGL logging, apply the sdkconfig choice as the final lv_conf.h value.
    if (
        not use_perf_monitor
        and not use_profiler
        and not _sdkconfig_bool("CONFIG_LV_USE_LOG", True)
    ):
        df.get_defines()["LV_USE_LOG"] = "0"

    if configs[0].get(df.CONF_THEME, {}).get(df.CONF_DARK_MODE):
        df.add_define("LV_THEME_DEFAULT_DARK", "1")

    if {
        "drop_shadow_color",
        "drop_shadow_offset_x",
        "drop_shadow_offset_y",
        "drop_shadow_opa",
        "drop_shadow_quality",
        "drop_shadow_radius",
    } & styles_used:
        lv_image_formats.add("A8")

    for image_id in get_lv_images_used():
        await cg.get_variable(image_id)
        metadata = get_image_metadata(image_id.id)
        image_type = IMAGE_TYPE[metadata.image_type]
        transparent = metadata.transparency != CONF_OPAQUE
        if image_type == ImageBinary:
            lv_image_formats.add("I1")
        if image_type == ImageGrayscale:
            lv_image_formats.add("A8")
        if image_type == ImageRGB565:
            lv_image_formats.add("RGB565A8" if transparent else "RGB565")
        if image_type == ImageRGB:
            lv_image_formats.add("ARGB8888" if transparent else "RGB888")
    if df.is_defined("LV_GRADIENT_MAX_STOPS"):
        lv_image_formats.add("RGB888")
    for fmt in lv_image_formats:
        df.add_define(f"LV_DRAW_SW_SUPPORT_{fmt}", "1")

    lv_conf_h_file = CORE.relative_src_path(LV_CONF_FILENAME)
    if write_file_if_changed(lv_conf_h_file, generate_lv_conf_h()):
        clean_build(clear_pio_cache=False)
    cg.add_build_flag("-DLV_CONF_H=1")
    cg.add_build_flag(lv_conf_path_flag(lv_conf_h_file))
    cg.add_build_flag("-DLV_KCONFIG_IGNORE")
    # Add include path for atomic.h shim (needed for LV_USE_OS=LV_OS_FREERTOS on ESP-IDF)
    # Use absolute path so it works when LVGL compiles from .piolibdeps/
    component_dir = Path(__file__).parent
    cg.add_build_flag(f"-I{component_dir.as_posix()}")

    for prop in df.get_remapped_uses():
        df.LOGGER.warning(
            "Property '%s' is deprecated, use '%s' instead", prop, STYLE_REMAP[prop]
        )
    for warning in df.get_warnings():
        df.LOGGER.warning(warning)


def display_schema(config):
    value = cv.ensure_list(cv.use_id(Display))(config)
    value = value or [cv.use_id(Display)(config)]
    if len(set(value)) != len(value):
        raise cv.Invalid("Display IDs must be unique")
    return value


def add_hello_world(config):
    if df.CONF_WIDGETS not in config and CONF_PAGES not in config:
        df.LOGGER.info(
            "No pages or widgets configured, creating default hello_world page"
        )
        hello_world_path = Path(__file__).parent / HELLO_WORLD_FILE
        config[df.CONF_WIDGETS] = any_widget_schema()(load_yaml(hello_world_path))
    return config


FINAL_VALIDATE_SCHEMA = final_validation

# The options accepted at the top level of an `lvgl:` block, on top of the base
# object schema that `container_schema(obj_spec, ...)` supplies. Held in a
# module-level name (rather than inline) so the schema-extractor wrapper on
# CONFIG_SCHEMA below can hand the language-schema dumper the same composed
# schema the runtime validates against.
LVGL_TOP_LEVEL_SCHEMA = (
    cv.polling_component_schema("1s")
    .extend(
        {
            **{
                cv.Optional(event): validate_automation(
                    {
                        cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(
                            Trigger.template(lv_obj_t_ptr, lv_event_t_ptr)
                        ),
                    }
                )
                for event in df.LV_SCREEN_EVENT_TRIGGERS + df.LV_DISPLAY_EVENT_TRIGGERS
            },
            cv.GenerateID(CONF_ID): cv.declare_id(LvglComponent),
            cv.GenerateID(CONF_ALIGN_TO_LAMBDA_ID): cv.declare_id(lv_lambda_t),
            cv.GenerateID(df.CONF_DISPLAYS): display_schema,
            cv.Optional(CONF_COLOR_DEPTH, default=16): cv.one_of(16, 32),
            cv.Optional(df.CONF_DEFAULT_FONT, default="montserrat_14"): lvalid.lv_font,
            cv.Optional(df.CONF_FULL_REFRESH, default=False): cv.boolean,
            cv.Optional(df.CONF_DIRECT_MODE, default=False): cv.boolean,
            cv.Optional(df.CONF_UPDATE_WHEN_DISPLAY_IDLE, default=False): cv.boolean,
            cv.Optional(df.CONF_REFRESH_INTERVAL): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_DRAW_ROUNDING, default=2): cv.positive_int,
            cv.Optional(CONF_BUFFER_SIZE, default=0): cv.percentage,
            cv.Optional(CONF_ROTATION): validate_rotation,
            cv.Optional(CONF_LOG_LEVEL, default="WARN"): cv.one_of(
                *df.LV_LOG_LEVELS, upper=True
            ),
            cv.Optional(df.CONF_CHECK_ARGS, default=False): cv.boolean,
            cv.Optional(CONF_BYTE_ORDER): cv.one_of(
                "big_endian", "little_endian", lower=True
            ),
            cv.Optional(df.CONF_STYLE_DEFINITIONS): cv.ensure_list(
                cv.Schema({cv.Required(CONF_ID): cv.declare_id(lv_style_t)}).extend(
                    FULL_STYLE_SCHEMA
                )
            ),
            cv.Optional(CONF_ON_IDLE): validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(IdleTrigger),
                    cv.Required(CONF_TIMEOUT): cv.templatable(
                        cv.positive_time_period_milliseconds
                    ),
                }
            ),
            cv.Optional(CONF_PAGES): cv.ensure_list(container_schema(page_spec)),
            **{
                cv.Optional(x): validate_automation(
                    {
                        cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(PlainTrigger),
                    },
                    single=True,
                )
                for x in SIMPLE_TRIGGERS
            },
            cv.Optional(df.CONF_MSGBOXES): cv.ensure_list(MSGBOX_SCHEMA),
            cv.Optional(df.CONF_ANIMATIONS): cv.ensure_list(ANIMATION_SCHEMA),
            cv.Optional(df.CONF_PAGE_WRAP, default=True): lv_bool,
            cv.Optional(df.CONF_TOP_LAYER): container_schema(obj_spec),
            cv.Optional(df.CONF_BOTTOM_LAYER): container_schema(obj_spec),
            cv.Optional(df.CONF_TRANSPARENCY_KEY, default=0x000400): lvalid.lv_color,
            cv.Optional(df.CONF_THEME): theme_schema,
            cv.Optional(df.CONF_GRADIENTS): GRADIENT_SCHEMA,
            cv.Optional(df.CONF_TOUCHSCREENS, default=None): touchscreen_schema,
            cv.Optional(df.CONF_ENCODERS, default=None): ENCODERS_CONFIG,
            cv.Optional(df.CONF_KEYPADS, default=None): KEYPADS_CONFIG,
            cv.GenerateID(df.CONF_DEFAULT_GROUP): cv.declare_id(lv_group_t),
            cv.Optional(df.CONF_RESUME_ON_INPUT, default=True): cv.boolean,
            cv.Optional(df.CONF_PAUSED, default=False): cv.boolean,
            cv.Optional(CONF_NAVIGATION): NAVIGATION_SCHEMA,
            cv.Optional(CONF_SNAPSHOT_COMPOSITOR): SNAPSHOT_SCHEMA,
            cv.Optional(CONF_USE_PPA, default=False): cv.boolean,
            cv.Optional(CONF_USE_PPA_IMG, default=False): cv.boolean,
            cv.Optional(CONF_USE_PPA_DRAW, default=False): cv.boolean,
            cv.Optional(CONF_USE_PPA_BLEND, default=False): cv.boolean,
            cv.Optional(CONF_PPA_BURST_LENGTH, default=128): cv.one_of(
                8, 16, 32, 64, 128
            ),
            cv.Optional(CONF_FPS_BENCHMARK, default=False): cv.boolean,
            cv.Optional(CONF_PERF_MONITOR, default=False): cv.boolean,
            cv.Optional(CONF_PROFILER, default=False): cv.boolean,
            cv.Optional(df.CONF_DEBUG_OUTLINE, default=False): cv.boolean,
        }
    )
    .extend(DISP_BG_SCHEMA)
)


def _not_on_esp8266(config: ConfigType) -> ConfigType:
    # ESP8266 does not have enough RAM for LVGL to be practical.
    if CORE.is_esp8266:
        raise cv.Invalid("LVGL is not supported on ESP8266")
    return config


LVGL_SCHEMA = cv.All(
    _not_on_esp8266,
    container_schema(obj_spec, LVGL_TOP_LEVEL_SCHEMA),
    cv.has_at_most_one_key(CONF_PAGES, df.CONF_LAYOUT),
    add_hello_world,
)


@schema_extractor("schema")
def lvgl_config_schema(config):
    """
    Can't use cv.ensure_list here because it converts an empty config to an empty list,
    rather than a default config.
    """
    if config is SCHEMA_EXTRACT:
        # CONFIG_SCHEMA is this callable wrapping `cv.All` over a container_schema
        # closure, so the language-schema dumper can't see the top-level `lvgl:`
        # fields (it would emit an empty schema). Hand it the same composed
        # obj + top-level schema the runtime validates against, plus the
        # `widgets:` key (added per-value by append_layout_schema at runtime, so
        # otherwise invisible to the dumper). Validation of real configs (the
        # branches below) is unchanged.
        return container_schema_value(obj_spec, LVGL_TOP_LEVEL_SCHEMA).extend(
            {cv.Optional(df.CONF_WIDGETS): any_widget_schema()}
        )
    if not config or isinstance(config, dict):
        return [LVGL_SCHEMA(config)]
    return cv.Schema([LVGL_SCHEMA])(config)


CONFIG_SCHEMA = lvgl_config_schema
