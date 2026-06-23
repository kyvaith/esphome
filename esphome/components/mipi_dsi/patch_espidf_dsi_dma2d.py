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
    changed = False

    for include in (
        '#include "esp_memory_utils.h"',
        '#include "hal/axi_icm_ll.h"',
        '#include "hal/cache_ll.h"',
        '#include "soc/soc_caps.h"',
    ):
        if include not in text:
            text = text.replace('#include "esp_cache.h"\n', f'#include "esp_cache.h"\n{include}\n')
            changed = True

    hook_decl = "extern void esphome_mipi_dsi_note_underrun(void) __attribute__((weak));\n"
    status_hook_decl = (
        "extern void esphome_mipi_dsi_note_status(uint32_t bridge_status, uint32_t bridge_raw,\n"
        "                                            uint32_t fifo_depth, uint32_t host_status0,\n"
        "                                            uint32_t host_status1) __attribute__((weak));\n"
    )
    if "esphome_mipi_dsi_note_underrun" not in text:
        anchor = "typedef struct esp_lcd_dpi_panel_t esp_lcd_dpi_panel_t;\n"
        if anchor not in text:
            raise RuntimeError("ESP-IDF DSI panel typedef not found; patch needs review")
        text = text.replace(anchor, f"{anchor}\n{hook_decl}{status_hook_decl}", 1)
        changed = True
    elif "esphome_mipi_dsi_note_status" not in text:
        if hook_decl not in text:
            raise RuntimeError("ESP-IDF DSI underrun hook declaration not found; patch needs review")
        text = text.replace(hook_decl, f"{hook_decl}{status_hook_decl}", 1)
        changed = True

    helper = """
static bool dpi_panel_skip_draw_buffer_msync(const void *draw_buffer)
{
    if (draw_buffer == NULL) {
        return true;
    }
    if (esp_ptr_internal(draw_buffer)) {
        return true;
    }
#if SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE && defined(CACHE_LL_L2MEM_CACHE_ADDR)
    const void *cache_addr = (const void *)CACHE_LL_L2MEM_CACHE_ADDR(draw_buffer);
    if (esp_ptr_internal(cache_addr)) {
        return true;
    }
#endif
    if (!esp_ptr_external_ram(draw_buffer)) {
        return true;
    }
    return false;
}

static esp_err_t dpi_panel_cache_msync(const void *buffer, size_t size)
{
    if (buffer == NULL || size == 0) {
        return ESP_OK;
    }
    const uintptr_t buffer_addr = (uintptr_t)buffer;
    const uintptr_t cache_align = 128;
    const uintptr_t sync_start = buffer_addr & ~(cache_align - 1);
    const uintptr_t sync_end = (buffer_addr + size + cache_align - 1) & ~(cache_align - 1);
    if (sync_end <= sync_start) {
        return ESP_OK;
    }
    if (!esp_ptr_external_ram((const void *)sync_start) ||
        !esp_ptr_external_ram((const void *)(sync_end - 1))) {
        return ESP_OK;
    }
    return esp_cache_msync((void *)sync_start, sync_end - sync_start, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
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
        changed = True
    elif "!esp_ptr_external_ram(draw_buffer)" not in text:
        old_tail = (
            "#endif\n"
            "    return false;\n"
            "}\n"
        )
        new_tail = (
            "#endif\n"
            "    if (!esp_ptr_external_ram(draw_buffer)) {\n"
            "        return true;\n"
            "    }\n"
            "    return false;\n"
            "}\n"
        )
        if old_tail not in text:
            raise RuntimeError("ESP-IDF DSI cache sync helper tail not found; patch needs review")
        text = text.replace(old_tail, new_tail, 1)
        changed = True

    if "dpi_panel_cache_msync" not in text:
        helper_anchor = "static bool dpi_panel_skip_draw_buffer_msync(const void *draw_buffer)\n"
        helper_start = text.find(helper_anchor)
        if helper_start == -1:
            raise RuntimeError("ESP-IDF DSI cache sync helper not found; patch needs review")
        helper_end = text.find("\n}\n", helper_start)
        if helper_end == -1:
            raise RuntimeError("ESP-IDF DSI cache sync helper end not found; patch needs review")
        helper_end += len("\n}\n")
        cache_helper = """
static esp_err_t dpi_panel_cache_msync(const void *buffer, size_t size)
{
    if (buffer == NULL || size == 0) {
        return ESP_OK;
    }
    const uintptr_t buffer_addr = (uintptr_t)buffer;
    const uintptr_t cache_align = 128;
    const uintptr_t sync_start = buffer_addr & ~(cache_align - 1);
    const uintptr_t sync_end = (buffer_addr + size + cache_align - 1) & ~(cache_align - 1);
    if (sync_end <= sync_start) {
        return ESP_OK;
    }
    if (!esp_ptr_external_ram((const void *)sync_start) ||
        !esp_ptr_external_ram((const void *)(sync_end - 1))) {
        return ESP_OK;
    }
    return esp_cache_msync((void *)sync_start, sync_end - sync_start, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
}
"""
        text = text[:helper_end] + cache_helper + text[helper_end:]
        changed = True

    old_underrun = (
        "    if (intr_status & MIPI_DSI_BRG_LL_EVENT_UNDERRUN) {\n"
        "        // when an underrun happens, the LCD display may already becomes blue\n"
    )
    new_underrun = (
        "    if (intr_status & MIPI_DSI_BRG_LL_EVENT_UNDERRUN) {\n"
        "        if (esphome_mipi_dsi_note_underrun) {\n"
        "            esphome_mipi_dsi_note_underrun();\n"
        "        }\n"
        "        // when an underrun happens, the LCD display may already becomes blue\n"
    )
    if "esphome_mipi_dsi_note_underrun();" not in text:
        if old_underrun not in text:
            raise RuntimeError("ESP-IDF DSI underrun interrupt block not found; patch needs review")
        text = text.replace(old_underrun, new_underrun, 1)
        changed = True

    status_anchor = "    uint32_t intr_status = mipi_dsi_brg_ll_get_interrupt_status(hal->bridge);\n"
    status_call = (
        status_anchor +
        "    if (esphome_mipi_dsi_note_status) {\n"
        "        esphome_mipi_dsi_note_status(intr_status, hal->bridge->int_raw.val,\n"
        "                                      hal->bridge->fifo_flow_status.raw_buf_depth,\n"
        "                                      hal->host->int_st0.val, hal->host->int_st1.val);\n"
        "    }\n"
    )
    if "esphome_mipi_dsi_note_status(intr_status" not in text:
        if status_anchor not in text:
            raise RuntimeError("ESP-IDF DSI interrupt status line not found; patch needs review")
        text = text.replace(status_anchor, status_call, 1)
        changed = True

    cache_sync_replacements = (
        (
            "esp_cache_msync(frame_buffer, fb_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED)",
            "dpi_panel_cache_msync(frame_buffer, fb_size)",
        ),
        (
            "esp_cache_msync(cache_sync_start, cache_sync_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED)",
            "dpi_panel_cache_msync(cache_sync_start, cache_sync_size)",
        ),
    )
    for old_call, new_call in cache_sync_replacements:
        if old_call in text:
            text = text.replace(old_call, new_call)
            changed = True

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
        '            ESP_RETURN_ON_ERROR(dpi_panel_cache_msync(draw_buffer, color_data_size), TAG, "writeback draw buffer failed");\n'
        "        }"
    )

    if new in text:
        pass
    elif old_guard in text:
        text = text.replace(old_guard, new)
        changed = True
    elif old in text:
        text = text.replace(old, new)
        changed = True
    else:
        raise RuntimeError("ESP-IDF DSI DMA2D cache sync line not found; patch needs review")

    flow_cleanup_replacements = (
        (
            "        .flow_controller = DW_GDMA_FLOW_CTRL_DST, // DSI bridge as the flow controller",
            "        .flow_controller = DW_GDMA_FLOW_CTRL_SELF, // DMA as the flow controller",
        ),
        (
            "    mipi_dsi_brg_ll_set_flow_controller(hal->bridge, MIPI_DSI_LL_FLOW_CONTROLLER_BRIDGE);",
            "    mipi_dsi_brg_ll_set_flow_controller(hal->bridge, MIPI_DSI_LL_FLOW_CONTROLLER_DMA);",
        ),
    )
    for old_line, new_line in flow_cleanup_replacements:
        if old_line in text:
            text = text.replace(old_line, new_line, 1)
            changed = True
    gdma_qos_replacements = (
        (
            "            .num_outstanding_requests = 5,",
            "            .num_outstanding_requests = 16,",
        ),
        (
            "            .num_outstanding_requests = 2,",
            "            .num_outstanding_requests = 4,",
        ),
        (
            "        .chan_priority = 1,",
            "        .chan_priority = 3,",
        ),
    )
    for old_line, new_line in gdma_qos_replacements:
        if new_line in text:
            continue
        if old_line not in text:
            raise RuntimeError("ESP-IDF DSI GDMA priority line not found; patch needs review")
        text = text.replace(old_line, new_line, 1)
        changed = True

    qos_anchor = '    ESP_RETURN_ON_ERROR(dw_gdma_new_channel(&dma_alloc_config, &dma_chan), TAG, "create DMA channel failed");\n'
    qos_patch = (
        qos_anchor +
        "#if CONFIG_IDF_TARGET_ESP32P4\n"
        "    // DSI scanout is a real-time PSRAM reader. Give DW-GDMA read traffic\n"
        "    // higher AXI QoS than opportunistic DMA2D/JPEG copies to avoid rare\n"
        "    // bridge FIFO underruns that otherwise show as a full-screen fallback\n"
        "    // color flash.\n"
        "    axi_icm_ll_set_dw_gdma_qos_arbiter_prio(0, 4, 15);\n"
        "    axi_icm_ll_set_dw_gdma_qos_arbiter_prio(1, 4, 15);\n"
        "#endif\n"
    )
    if qos_patch not in text:
        if qos_anchor not in text:
            raise RuntimeError("ESP-IDF DSI GDMA channel creation line not found; patch needs review")
        text = text.replace(qos_anchor, qos_patch, 1)
        changed = True
    fifo_replacements = (
        (
            "    mipi_dsi_brg_ll_set_underrun_discard_count(hal->bridge, panel_config->video_timing.h_size);",
            "    mipi_dsi_brg_ll_set_underrun_discard_count(hal->bridge, 1);",
        ),
        (
            "    mipi_dsi_brg_ll_set_burst_len(hal->bridge, 256);",
            "    hal->bridge->mem_clk_ctrl.dsi_bridge_mem_clk_force_on = 1;\n"
            "    hal->bridge->mem_clk_ctrl.dsi_mem_clk_force_on = 1;\n"
            "    mipi_dsi_brg_ll_set_burst_len(hal->bridge, 128);",
        ),
        (
            "    mipi_dsi_brg_ll_set_empty_threshold(hal->bridge, 1024 - 256);",
            "    mipi_dsi_brg_ll_set_empty_threshold(hal->bridge, 1024 - 128);",
        ),
    )
    for old_line, new_line in fifo_replacements:
        if new_line in text:
            continue
        if old_line not in text:
            raise RuntimeError("ESP-IDF DSI FIFO tuning line not found; patch needs review")
        text = text.replace(old_line, new_line, 1)
        changed = True

    credit_reset = "    mipi_dsi_brg_ll_credit_reset(hal->bridge);"
    if credit_reset in text:
        text = text.replace(f"{credit_reset}\n", "", 1)
        changed = True

    if changed:
        target.write_text(text, encoding="utf-8")
        print("MIPI DSI patch: applied ESP-IDF 5.x DMA2D/cache diagnostics patch")
    else:
        print("MIPI DSI patch: ESP-IDF 5.x DMA2D/cache diagnostics patch already present")


def _patch_idf6_or_newer(framework_dir: Path) -> None:
    target = framework_dir / "components" / "esp_lcd" / "src" / "esp_async_fbcpy.c"
    text = target.read_text(encoding="utf-8")

    for include in ('#include "esp_memory_utils.h"', '#include "soc/soc_caps.h"'):
        if include not in text:
            text = text.replace('#include "esp_heap_caps.h"\n', f'#include "esp_heap_caps.h"\n{include}\n')

    helper = """
static bool async_fbcpy_skip_src_msync(const void *src_buffer)
{
    if (src_buffer == NULL) {
        return true;
    }
    if (esp_ptr_internal(src_buffer)) {
        return true;
    }
#if SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE && defined(CACHE_LL_L2MEM_CACHE_ADDR)
    const void *cache_addr = (const void *)CACHE_LL_L2MEM_CACHE_ADDR(src_buffer);
    if (esp_ptr_internal(cache_addr)) {
        return true;
    }
#endif
    if (!esp_ptr_external_ram(src_buffer)) {
        return true;
    }
    return false;
}
"""
    if "async_fbcpy_skip_src_msync" not in text:
        anchor = 'static const char *TAG = "async_fbcpy";\n'
        if anchor not in text:
            raise RuntimeError("ESP-IDF async_fbcpy TAG declaration not found; patch needs review")
        text = text.replace(anchor, f"{anchor}{helper}", 1)
    elif "!esp_ptr_external_ram(src_buffer)" not in text:
        old_tail = (
            "#endif\n"
            "    return false;\n"
            "}\n"
        )
        new_tail = (
            "#endif\n"
            "    if (!esp_ptr_external_ram(src_buffer)) {\n"
            "        return true;\n"
            "    }\n"
            "    return false;\n"
            "}\n"
        )
        if old_tail not in text:
            raise RuntimeError("ESP-IDF async_fbcpy cache sync helper tail not found; patch needs review")
        text = text.replace(old_tail, new_tail, 1)

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


if env is not None:
    main()

