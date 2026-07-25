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
import sys

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
    target = (
        Path(framework_dir) / "components" / "esp_lcd" / "dsi" / "esp_lcd_panel_dpi.c"
    )
    text = target.read_text(encoding="utf-8")
    original_text = text
    changed = False

    for include in (
        '#include "esp_memory_utils.h"',
        '#include "freertos/FreeRTOS.h"',
        '#include "hal/axi_icm_ll.h"',
        '#include "hal/cache_ll.h"',
        '#include "hal/dw_gdma_ll.h"',
        '#include "soc/soc_caps.h"',
    ):
        if include not in text:
            text = text.replace(
                '#include "esp_cache.h"\n', f'#include "esp_cache.h"\n{include}\n'
            )
            changed = True

    # Remove the task header left by the abandoned ISR-affinity experiment.
    for obsolete_include in ('#include "freertos/task.h"\n',):
        if obsolete_include in text:
            text = text.replace(obsolete_include, "", 1)
            changed = True

    # Remove the earlier experiment which moved the DW-GDMA interrupt to the
    # opposite CPU. It increased late-frame events and is superseded by the
    # hardware reload mode below.
    if '#include "esp_ipc.h"\n' in text:
        text = text.replace('#include "esp_ipc.h"\n', "", 1)
        changed = True

    hook_decl = (
        "extern void esphome_mipi_dsi_note_underrun(void) __attribute__((weak));\n"
    )
    status_hook_decl = (
        "extern void esphome_mipi_dsi_note_status(uint32_t bridge_status, uint32_t bridge_raw,\n"
        "                                            uint32_t fifo_depth, uint32_t host_status0,\n"
        "                                            uint32_t host_status1) __attribute__((weak));\n"
    )
    frame_active_hook_decl = (
        "extern bool esphome_mipi_dsi_frame_buffer_active(esp_lcd_panel_handle_t panel,\n"
        "                                                   void *frame_buffer) __attribute__((weak));\n"
    )
    frame_staged_hook_decl = (
        "extern bool esphome_mipi_dsi_frame_buffer_staged(esp_lcd_panel_handle_t panel,\n"
        "                                                   void *frame_buffer) __attribute__((weak));\n"
    )
    if "esphome_mipi_dsi_note_underrun" not in text:
        anchor = "typedef struct esp_lcd_dpi_panel_t esp_lcd_dpi_panel_t;\n"
        if anchor not in text:
            raise RuntimeError(
                "ESP-IDF DSI panel typedef not found; patch needs review"
            )
        text = text.replace(anchor, f"{anchor}\n{hook_decl}{status_hook_decl}", 1)
        changed = True
    elif "esphome_mipi_dsi_note_status" not in text:
        if hook_decl not in text:
            raise RuntimeError(
                "ESP-IDF DSI underrun hook declaration not found; patch needs review"
            )
        text = text.replace(hook_decl, f"{hook_decl}{status_hook_decl}", 1)
        changed = True
    if "esphome_mipi_dsi_frame_buffer_active" not in text:
        if status_hook_decl not in text:
            raise RuntimeError(
                "ESP-IDF DSI status hook declaration not found; patch needs review"
            )
        text = text.replace(
            status_hook_decl, f"{status_hook_decl}{frame_active_hook_decl}", 1
        )
        changed = True
    if "esphome_mipi_dsi_frame_buffer_staged" not in text:
        if frame_active_hook_decl not in text:
            raise RuntimeError(
                "ESP-IDF DSI active framebuffer hook declaration not found; patch needs review"
            )
        text = text.replace(
            frame_active_hook_decl,
            f"{frame_active_hook_decl}{frame_staged_hook_decl}",
            1,
        )
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
#ifdef CONFIG_ESPHOME_DSI_MSYNC_CHUNK
    const size_t chunk_size = CONFIG_ESPHOME_DSI_MSYNC_CHUNK;
    if (chunk_size > 0 && sync_end - sync_start > chunk_size) {
        uint8_t *ptr = (uint8_t *)sync_start;
        size_t remaining = sync_end - sync_start;
        while (remaining > 0) {
            size_t this_chunk = remaining > chunk_size ? chunk_size : remaining;
            esp_err_t ret = esp_cache_msync(ptr, this_chunk, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
            if (ret != ESP_OK) {
                return ret;
            }
            ptr += this_chunk;
            remaining -= this_chunk;
        }
        return ESP_OK;
    }
#endif
    return esp_cache_msync((void *)sync_start, sync_end - sync_start, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
}
"""
    if "dpi_panel_skip_draw_buffer_msync" not in text:
        anchor = (
            "static esp_err_t dpi_panel_draw_bitmap(esp_lcd_panel_t *panel, int x_start, int y_start, "
            "int x_end, int y_end, const void *color_data);\n"
        )
        if anchor not in text:
            raise RuntimeError(
                "ESP-IDF DSI draw_bitmap declaration not found; patch needs review"
            )
        text = text.replace(anchor, f"{anchor}{helper}", 1)
        changed = True
    elif "!esp_ptr_external_ram(draw_buffer)" not in text:
        old_tail = "#endif\n    return false;\n}\n"
        new_tail = (
            "#endif\n"
            "    if (!esp_ptr_external_ram(draw_buffer)) {\n"
            "        return true;\n"
            "    }\n"
            "    return false;\n"
            "}\n"
        )
        if old_tail not in text:
            raise RuntimeError(
                "ESP-IDF DSI cache sync helper tail not found; patch needs review"
            )
        text = text.replace(old_tail, new_tail, 1)
        changed = True

    if "dpi_panel_cache_msync" not in text:
        helper_anchor = (
            "static bool dpi_panel_skip_draw_buffer_msync(const void *draw_buffer)\n"
        )
        helper_start = text.find(helper_anchor)
        if helper_start == -1:
            raise RuntimeError(
                "ESP-IDF DSI cache sync helper not found; patch needs review"
            )
        helper_end = text.find("\n}\n", helper_start)
        if helper_end == -1:
            raise RuntimeError(
                "ESP-IDF DSI cache sync helper end not found; patch needs review"
            )
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

    poll_helper = """
esp_err_t esphome_mipi_dsi_poll_status(esp_lcd_panel_handle_t panel, uint32_t *bridge_status,
                                       uint32_t *bridge_raw, uint32_t *fifo_depth,
                                       uint32_t *host_status0, uint32_t *host_status1)
{
    if (panel == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_lcd_dpi_panel_t *dpi_panel = __containerof(panel, esp_lcd_dpi_panel_t, base);
    mipi_dsi_hal_context_t *hal = &dpi_panel->bus->hal;
    if (bridge_status) {
        *bridge_status = mipi_dsi_brg_ll_get_interrupt_status(hal->bridge);
    }
    if (bridge_raw) {
        *bridge_raw = hal->bridge->int_raw.val;
    }
    if (fifo_depth) {
        *fifo_depth = hal->bridge->fifo_flow_status.raw_buf_depth;
    }
    if (host_status0) {
        *host_status0 = hal->host->int_st0.val;
    }
    if (host_status1) {
        *host_status1 = hal->host->int_st1.val;
    }
    return ESP_OK;
}

esp_err_t esphome_mipi_dsi_poll_video_status(esp_lcd_panel_handle_t panel, uint32_t *video_status)
{
    if (panel == NULL || video_status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_lcd_dpi_panel_t *dpi_panel = __containerof(panel, esp_lcd_dpi_panel_t, base);
    mipi_dsi_hal_context_t *hal = &dpi_panel->bus->hal;
    *video_status = hal->host->vid_pkt_status.val;
    return ESP_OK;
}

esp_err_t esphome_mipi_dsi_set_frame_ack(esp_lcd_panel_handle_t panel, bool enable)
{
    if (panel == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_lcd_dpi_panel_t *dpi_panel = __containerof(panel, esp_lcd_dpi_panel_t, base);
    mipi_dsi_hal_context_t *hal = &dpi_panel->bus->hal;
    mipi_dsi_host_ll_dpi_enable_frame_ack(hal->host, enable);
    return ESP_OK;
}
"""
    if "esphome_mipi_dsi_poll_status" not in text:
        anchor = "    void *user_ctx; // User context for the callback\n};\n"
        if anchor not in text:
            raise RuntimeError(
                "ESP-IDF DSI panel struct end not found; patch needs review"
            )
        text = text.replace(anchor, f"{anchor}{poll_helper}", 1)
        changed = True
    elif "esphome_mipi_dsi_set_frame_ack" not in text:
        helper_end = text.find("\n}\n", text.find("esphome_mipi_dsi_poll_status"))
        if helper_end == -1:
            raise RuntimeError(
                "ESP-IDF DSI poll helper end not found; patch needs review"
            )
        helper_end += len("\n}\n")
        frame_ack_helper = """
esp_err_t esphome_mipi_dsi_set_frame_ack(esp_lcd_panel_handle_t panel, bool enable)
{
    if (panel == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_lcd_dpi_panel_t *dpi_panel = __containerof(panel, esp_lcd_dpi_panel_t, base);
    mipi_dsi_hal_context_t *hal = &dpi_panel->bus->hal;
    mipi_dsi_host_ll_dpi_enable_frame_ack(hal->host, enable);
    return ESP_OK;
}
"""
        text = text[:helper_end] + frame_ack_helper + text[helper_end:]
        changed = True

    video_status_helper = """
esp_err_t esphome_mipi_dsi_poll_video_status(esp_lcd_panel_handle_t panel, uint32_t *video_status)
{
    if (panel == NULL || video_status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_lcd_dpi_panel_t *dpi_panel = __containerof(panel, esp_lcd_dpi_panel_t, base);
    mipi_dsi_hal_context_t *hal = &dpi_panel->bus->hal;
    *video_status = hal->host->vid_pkt_status.val;
    return ESP_OK;
}
"""
    if "esphome_mipi_dsi_poll_video_status" not in text:
        helper_end = text.find("\n}\n", text.find("esphome_mipi_dsi_poll_status"))
        if helper_end == -1:
            raise RuntimeError(
                "ESP-IDF DSI poll helper end not found for video status; patch needs review"
            )
        helper_end += len("\n}\n")
        text = text[:helper_end] + video_status_helper + text[helper_end:]
        changed = True

    dma_ring_status_helper = """
esp_err_t esphome_mipi_dsi_poll_dma_ring(esp_lcd_panel_handle_t panel,
                                         uint32_t *lookup_failures,
                                         uint8_t *active_fb_index,
                                         uint8_t *pending_fb_index)
{
    if (panel == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_lcd_dpi_panel_t *dpi_panel = __containerof(panel, esp_lcd_dpi_panel_t, base);
    if (lookup_failures) {
        *lookup_failures = dpi_panel->dma_lli_lookup_failures;
    }
    if (active_fb_index) {
        *active_fb_index = dpi_panel->active_fb_index;
    }
    if (pending_fb_index) {
        *pending_fb_index = dpi_panel->pending_fb_index;
    }
    return ESP_OK;
}
"""
    if "esphome_mipi_dsi_poll_dma_ring" not in text:
        helper_start = text.find("esp_err_t esphome_mipi_dsi_poll_video_status")
        helper_end = text.find("\n}\n", helper_start)
        if helper_start == -1 or helper_end == -1:
            raise RuntimeError(
                "ESP-IDF DSI video status helper not found for DMA ring diagnostics"
            )
        helper_end += len("\n}\n")
        text = text[:helper_end] + dma_ring_status_helper + text[helper_end:]
        changed = True

    legacy_dma_state_fields = (
        "    uint8_t active_fb_index;                // Framebuffer currently scanned by DSI\n"
        "    uint8_t pending_fb_index;               // Framebuffer queued for the next frame boundary\n"
        "    portMUX_TYPE dma_switch_lock;           // Protect framebuffer switching from the frame ISR\n"
    )
    full_frame_dma_state_fields = (
        "    dw_gdma_lli_handle_t frame_llis[2];      // Two full-frame descriptors kept in a hardware ring\n"
        "    uint8_t frame_lli_fb_index[2];           // Framebuffer programmed into each descriptor\n"
        "    int dma_chan_id;                         // DW-GDMA channel used to identify the active descriptor\n"
        "    uint8_t active_fb_index;                 // Framebuffer currently scanned by DSI\n"
        "    uint8_t pending_fb_index;                // Latest framebuffer requested for a frame boundary\n"
        "    volatile uint32_t dma_lli_lookup_failures; // Unexpected current-descriptor address count\n"
        "    portMUX_TYPE dma_switch_lock;            // Protect descriptor switching from the frame ISR\n"
    )
    continuous_dma_state_fields = (
        "    dw_gdma_lli_handle_t frame_llis[2];      // Top and bottom half-frame descriptors in a hardware ring\n"
        "    uint8_t frame_lli_fb_index[2];           // Framebuffer programmed into each half-frame descriptor\n"
        "    int dma_chan_id;                         // DW-GDMA channel used to identify the active descriptor\n"
        "    uint8_t active_fb_index;                 // Framebuffer currently scanned by DSI\n"
        "    uint8_t pending_fb_index;                // Latest framebuffer requested for a frame boundary\n"
        "    volatile uint32_t dma_lli_lookup_failures; // Unexpected current-descriptor address count\n"
        "    portMUX_TYPE dma_switch_lock;            // Protect descriptor switching from the frame ISR\n"
    )
    dma_state_anchor = (
        "    dw_gdma_link_list_handle_t link_lists[DPI_PANEL_MAX_FB_NUM]; // DMA link list\n"
    )
    autonomous_dma_state_fields = (
        "    dw_gdma_lli_handle_t frame_llis[DPI_PANEL_MAX_FB_NUM]; // One autonomous descriptor per framebuffer\n"
        "    int dma_chan_id;                         // DW-GDMA channel used to identify the active descriptor\n"
        "    uint8_t active_fb_index;                // Framebuffer currently scanned by DSI\n"
        "    uint8_t pending_fb_index;               // Framebuffer queued for the next frame boundary\n"
        "    uint8_t switch_from_fb_index;           // Descriptor temporarily linked to pending_fb_index\n"
        "    portMUX_TYPE dma_switch_lock;           // Protect descriptor switching from the frame ISR\n"
    )
    reload_dma_state_fields = (
        "    int dma_chan_id;                         // DW-GDMA channel used for reload source updates\n"
        "    uint8_t active_fb_index;                // Framebuffer currently scanned by DSI\n"
        "    uint8_t pending_fb_index;               // Framebuffer programmed for the next frame\n"
        "    portMUX_TYPE dma_switch_lock;           // Protect framebuffer switching from the frame ISR\n"
    )
    if full_frame_dma_state_fields in text:
        text = text.replace(
            full_frame_dma_state_fields, continuous_dma_state_fields, 1
        )
        changed = True
    elif autonomous_dma_state_fields in text:
        text = text.replace(autonomous_dma_state_fields, continuous_dma_state_fields, 1)
        changed = True
    elif reload_dma_state_fields in text:
        text = text.replace(reload_dma_state_fields, continuous_dma_state_fields, 1)
        changed = True
    elif legacy_dma_state_fields in text:
        text = text.replace(legacy_dma_state_fields, continuous_dma_state_fields, 1)
        changed = True
    elif continuous_dma_state_fields not in text:
        if dma_state_anchor not in text:
            raise RuntimeError(
                "ESP-IDF DSI link-list field not found; patch needs review"
            )
        text = text.replace(
            dma_state_anchor, f"{dma_state_anchor}{continuous_dma_state_fields}", 1
        )
        changed = True

    dma_framebuffer_helpers = r"""
static esp_err_t dpi_panel_queue_framebuffer_index(esp_lcd_dpi_panel_t *dpi_panel,
                                                    uint8_t fb_index)
{
    esp_err_t result = ESP_OK;
    bool framebuffer_already_active = false;
    portENTER_CRITICAL(&dpi_panel->dma_switch_lock);
    if (fb_index == dpi_panel->active_fb_index) {
        dpi_panel->cur_fb_index = fb_index;
        framebuffer_already_active = true;
    } else {
        // The half-frame ring consumes the latest complete frame at the middle
        // of a scan. Its top half is staged while the current bottom half is
        // still transmitted, then both halves switch at the frame boundary.
        dpi_panel->pending_fb_index = fb_index;
        dpi_panel->cur_fb_index = fb_index;
    }
    portEXIT_CRITICAL(&dpi_panel->dma_switch_lock);

    // No VSYNC handoff will occur when the caller submits the framebuffer that
    // is already scanned out. Complete that no-op submission synchronously;
    // genuinely queued framebuffers are acknowledged by the DMA callback only
    // after they become active at a full-frame boundary.
    if (result == ESP_OK && framebuffer_already_active && dpi_panel->on_color_trans_done) {
        dpi_panel->on_color_trans_done(&dpi_panel->base, NULL, dpi_panel->user_ctx);
    }
    return result;
}

esp_err_t esphome_mipi_dsi_queue_dma_framebuffer(esp_lcd_panel_handle_t panel,
                                                  void *frame_buffer)
{
    if (panel == NULL || frame_buffer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_lcd_dpi_panel_t *dpi_panel = __containerof(panel, esp_lcd_dpi_panel_t, base);
    for (uint8_t index = 0; index < dpi_panel->num_fbs; index++) {
        if (dpi_panel->fbs[index] != frame_buffer) {
            continue;
        }
        // The framebuffer is complete and already DMA-visible. Queue it for
        // the next full-frame boundary.
        return dpi_panel_queue_framebuffer_index(dpi_panel, index);
    }
    return ESP_ERR_INVALID_ARG;
}
"""
    queue_start = text.find("esp_err_t esphome_mipi_dsi_queue_dma_framebuffer")
    queue_helper_signature = (
        "static esp_err_t dpi_panel_queue_framebuffer_index"
    )
    if (
        "The half-frame ring consumes the latest complete frame" not in text
        or text.count(queue_helper_signature) != 1
        or "Reload mode latches SAR at a block boundary" in text
        or "framebuffer_already_active" not in text
    ):
        if queue_start == -1:
            struct_anchor = "    void *user_ctx; // User context for the callback\n};\n"
            if struct_anchor not in text:
                raise RuntimeError(
                    "ESP-IDF DSI panel struct end not found for DMA framebuffer helper"
                )
            text = text.replace(
                struct_anchor,
                f"{struct_anchor}{dma_framebuffer_helpers}",
                1,
            )
        else:
            helper_start = text.find(
                "static inline void IRAM_ATTR dpi_panel_set_lli_next"
            )
            queue_helper_start = text.find(queue_helper_signature)
            replace_start = (
                helper_start
                if helper_start != -1
                else queue_helper_start
                if queue_helper_start != -1
                else queue_start
            )
            queue_end = text.find(
                "\nesp_err_t esphome_mipi_dsi_poll_status", queue_start
            )
            if queue_end == -1:
                raise RuntimeError(
                    "ESP-IDF DSI queue helper end not found; patch needs review"
                )
            text = (
                text[:replace_start]
                + dma_framebuffer_helpers.lstrip("\n")
                + text[queue_end:]
            )
        changed = True

    legacy_queue_callback = r"""        esp_err_t result = dpi_panel_queue_framebuffer_index(dpi_panel, index);
        if (result == ESP_OK && dpi_panel->on_color_trans_done) {
            dpi_panel->on_color_trans_done(&dpi_panel->base, NULL, dpi_panel->user_ctx);
        }
        return result;
"""
    queued_at_vsync = r"""        return dpi_panel_queue_framebuffer_index(dpi_panel, index);
"""
    if legacy_queue_callback in text:
        text = text.replace(legacy_queue_callback, queued_at_vsync, 1)
        changed = True

    if "esp_lcd_dpi_panel_frame_buf_complete_cb_t on_frame_buf_complete;" in text:
        frame_complete_callback = r"""
    if (dpi_panel->on_frame_buf_complete &&
        dpi_panel->on_frame_buf_complete(&dpi_panel->base, NULL, dpi_panel->user_ctx)) {
        yield_needed = true;
    }
"""
        vsync_callback_name = "on_vsync"
    else:
        frame_complete_callback = ""
        vsync_callback_name = "on_refresh_done"

    stable_dma_callback = r"""IRAM_ATTR
bool mipi_dsi_dma_trans_done_cb(dw_gdma_channel_handle_t chan,
                                const dw_gdma_trans_done_event_data_t *event_data,
                                void *user_data)
{
    bool yield_needed = false;
    bool framebuffer_switched = false;
    esp_lcd_dpi_panel_t *dpi_panel = (esp_lcd_dpi_panel_t *)user_data;
    (void)chan;
    (void)event_data;
    int current_slot = -1;
    const intptr_t current_lli_addr = dw_gdma_ll_channel_get_current_link_list_item_addr(
        DW_GDMA_LL_GET_HW(0), dpi_panel->dma_chan_id);
    for (int slot = 0; slot < 2; slot++) {
        uintptr_t descriptor_addr = (uintptr_t)dpi_panel->frame_llis[slot];
#if SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE && defined(CACHE_LL_L2MEM_CACHE_ADDR)
        descriptor_addr = CACHE_LL_L2MEM_CACHE_ADDR(descriptor_addr);
#endif
        if ((intptr_t)descriptor_addr == current_lli_addr) {
            current_slot = slot;
            break;
        }
    }

    dw_gdma_block_markers_t markers = {
        .is_valid = true,
        .is_last = false,
        .en_trans_done_intr = true,
    };

    if (current_slot < 0) {
        // Keep the ring alive even if the hardware reports an unexpected LLP.
        // No source address is changed because we cannot prove which descriptor
        // is inactive. The task-level diagnostics expose the miss counter.
        dpi_panel->dma_lli_lookup_failures++;
        dw_gdma_lli_set_block_markers(dpi_panel->frame_llis[0], markers);
        dw_gdma_lli_set_block_markers(dpi_panel->frame_llis[1], markers);
        return false;
    }

    // LLP identifies the descriptor whose block-done interrupt is being
    // serviced. Rewrite only that descriptor while the other half of the frame
    // is being transferred. Slot 0 stages the top half of a pending frame. Slot
    // 1 completes the old frame, commits the staged framebuffer, and points its
    // bottom half at the same framebuffer before slot 0 finishes.
    const size_t total_items = dpi_panel->fb_size * 8 / 64;
    const size_t first_half_bytes = (total_items / 2) * (64 / 8);
    uint8_t active_fb_index;
    uint8_t descriptor_fb_index;
    bool framebuffer_staged = false;
    portENTER_CRITICAL_ISR(&dpi_panel->dma_switch_lock);
    if (current_slot == 0) {
        descriptor_fb_index = dpi_panel->active_fb_index;
        if (dpi_panel->pending_fb_index < DPI_PANEL_MAX_FB_NUM) {
            descriptor_fb_index = dpi_panel->pending_fb_index;
            framebuffer_staged = descriptor_fb_index != dpi_panel->active_fb_index;
        }
        dpi_panel->frame_lli_fb_index[0] = descriptor_fb_index;
        active_fb_index = dpi_panel->active_fb_index;
    } else {
        descriptor_fb_index = dpi_panel->frame_lli_fb_index[0];
        dpi_panel->active_fb_index = descriptor_fb_index;
        dpi_panel->cur_fb_index = descriptor_fb_index;
        dpi_panel->frame_lli_fb_index[1] = descriptor_fb_index;
        if (dpi_panel->pending_fb_index == descriptor_fb_index) {
            dpi_panel->pending_fb_index = DPI_PANEL_MAX_FB_NUM;
            framebuffer_switched = true;
        }
        active_fb_index = descriptor_fb_index;
    }
    portEXIT_CRITICAL_ISR(&dpi_panel->dma_switch_lock);

    uint8_t *descriptor_source = dpi_panel->fbs[descriptor_fb_index];
    if (current_slot == 1) {
        descriptor_source += first_half_bytes;
    }
    dw_gdma_ll_lli_set_src_addr(dpi_panel->frame_llis[current_slot],
                                (uint32_t)descriptor_source);
    dw_gdma_lli_set_block_markers(dpi_panel->frame_llis[current_slot], markers);
    __sync_synchronize();

    if (current_slot == 0) {
        if (framebuffer_staged && esphome_mipi_dsi_frame_buffer_staged &&
            esphome_mipi_dsi_frame_buffer_staged(&dpi_panel->base,
                                                  dpi_panel->fbs[descriptor_fb_index])) {
            yield_needed = true;
        }
        return yield_needed;
    }

    // Reconcile the public owner on every full-frame boundary. This also keeps
    // buffers that were already scheduled before a latest-frame replacement
    // unavailable to renderers until their scanout really completes.
    if (esphome_mipi_dsi_frame_buffer_active &&
        esphome_mipi_dsi_frame_buffer_active(&dpi_panel->base,
                                              dpi_panel->fbs[active_fb_index])) {
        yield_needed = true;
    }
__ESPHOME_FRAME_COMPLETE_CALLBACK__
    if (framebuffer_switched && dpi_panel->on_color_trans_done &&
        dpi_panel->on_color_trans_done(&dpi_panel->base, NULL, dpi_panel->user_ctx)) {
        yield_needed = true;
    }

#if !MIPI_DSI_BRG_LL_EVENT_VSYNC
    if (dpi_panel->__ESPHOME_VSYNC_CALLBACK__ &&
        dpi_panel->__ESPHOME_VSYNC_CALLBACK__(&dpi_panel->base, NULL, dpi_panel->user_ctx)) {
        yield_needed = true;
    }
#endif
    return yield_needed;
}
"""
    stable_dma_callback = stable_dma_callback.replace(
        "__ESPHOME_FRAME_COMPLETE_CALLBACK__", frame_complete_callback.rstrip()
    ).replace("__ESPHOME_VSYNC_CALLBACK__", vsync_callback_name)
    callback_start = text.find("bool mipi_dsi_dma_trans_done_cb(")
    if callback_start == -1:
        raise RuntimeError(
            "ESP-IDF DSI DMA completion callback not found; patch needs review"
        )
    callback_prefix_start = text.rfind("IRAM_ATTR\n", 0, callback_start)
    if callback_prefix_start != -1 and callback_start - callback_prefix_start < 32:
        callback_start = callback_prefix_start
    callback_end = text.find("\nvoid mipi_dsi_bridge_isr_handler", callback_start)
    if callback_end == -1:
        raise RuntimeError(
            "ESP-IDF DSI DMA completion callback end not found; patch needs review"
        )
    current_callback = text[callback_start:callback_end]
    desired_callback = stable_dma_callback.rstrip("\n")
    if current_callback != desired_callback:
        text = text[:callback_start] + desired_callback + text[callback_end:]
        changed = True
    flow_helper = """
esp_err_t esphome_mipi_dsi_set_dma_flow_controller(esp_lcd_panel_handle_t panel, bool enable)
{
    if (panel == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_lcd_dpi_panel_t *dpi_panel = __containerof(panel, esp_lcd_dpi_panel_t, base);
    mipi_dsi_hal_context_t *hal = &dpi_panel->bus->hal;
    mipi_dsi_brg_ll_set_flow_controller(
        hal->bridge, enable ? MIPI_DSI_LL_FLOW_CONTROLLER_DMA : MIPI_DSI_LL_FLOW_CONTROLLER_BRIDGE);
    mipi_dsi_brg_ll_update_dpi_config(hal->bridge);
    return ESP_OK;
}
"""
    if flow_helper in text:
        text = text.replace(flow_helper, "", 1)
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
            raise RuntimeError(
                "ESP-IDF DSI underrun interrupt block not found; patch needs review"
            )
        text = text.replace(old_underrun, new_underrun, 1)
        changed = True

    status_anchor = "    uint32_t intr_status = mipi_dsi_brg_ll_get_interrupt_status(hal->bridge);\n"
    status_call = (
        status_anchor + "    if (esphome_mipi_dsi_note_status) {\n"
        "        esphome_mipi_dsi_note_status(intr_status, hal->bridge->int_raw.val,\n"
        "                                      hal->bridge->fifo_flow_status.raw_buf_depth,\n"
        "                                      hal->host->int_st0.val, hal->host->int_st1.val);\n"
        "    }\n"
    )
    if "esphome_mipi_dsi_note_status(intr_status" not in text:
        if status_anchor not in text:
            raise RuntimeError(
                "ESP-IDF DSI interrupt status line not found; patch needs review"
            )
        text = text.replace(status_anchor, status_call, 1)
        changed = True

    frame_ack_old = (
        "    // after sending a frame, the DSI device should return an ack\n"
        "    mipi_dsi_host_ll_dpi_enable_frame_ack(hal->host, true);\n"
    )
    frame_ack_new = (
        "    // Keep frame ACK enabled for panel startup. If CONFIG_ESPHOME_MIPI_DSI_DISABLE_FRAME_ACK is set,\n"
        "    // ESPHome disables it after the first confirmed refresh so startup remains deterministic.\n"
        "    mipi_dsi_host_ll_dpi_enable_frame_ack(hal->host, true);\n"
    )
    frame_ack_old_conditional = (
        "    // after sending a frame, the DSI device should return an ack\n"
        "#ifdef CONFIG_ESPHOME_MIPI_DSI_DISABLE_FRAME_ACK\n"
        "    mipi_dsi_host_ll_dpi_enable_frame_ack(hal->host, false);\n"
        "#else\n"
        "    mipi_dsi_host_ll_dpi_enable_frame_ack(hal->host, true);\n"
        "#endif\n"
    )
    if frame_ack_new not in text:
        if frame_ack_old_conditional in text:
            text = text.replace(frame_ack_old_conditional, frame_ack_new, 1)
        elif frame_ack_old in text:
            text = text.replace(frame_ack_old, frame_ack_new, 1)
        else:
            raise RuntimeError(
                "ESP-IDF DSI frame ACK line not found; patch needs review"
            )
        changed = True

    burst_old = (
        "    // using the burst mode because it's energy-efficient\n"
        "    mipi_dsi_host_ll_dpi_set_video_burst_type(hal->host, MIPI_DSI_LL_VIDEO_BURST_WITH_SYNC_PULSES);\n"
    )
    burst_new = (
        "    // using the burst mode because it's energy-efficient\n"
        "#ifdef CONFIG_ESPHOME_MIPI_DSI_NON_BURST_SYNC_PULSES\n"
        "    mipi_dsi_host_ll_dpi_set_video_burst_type(hal->host, MIPI_DSI_LL_VIDEO_NON_BURST_WITH_SYNC_PULSES);\n"
        "#else\n"
        "    mipi_dsi_host_ll_dpi_set_video_burst_type(hal->host, MIPI_DSI_LL_VIDEO_BURST_WITH_SYNC_PULSES);\n"
        "#endif\n"
    )
    if burst_new not in text:
        if burst_old not in text:
            raise RuntimeError(
                "ESP-IDF DSI video burst mode line not found; patch needs review"
            )
        text = text.replace(burst_old, burst_new, 1)
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
    doubled_new = (
        "        if (!dpi_panel_skip_draw_buffer_msync(draw_buffer)) {\n"
        "            if (!dpi_panel_skip_draw_buffer_msync(draw_buffer)) {\n"
        '            ESP_RETURN_ON_ERROR(dpi_panel_cache_msync(draw_buffer, color_data_size), TAG, "writeback draw buffer failed");\n'
        "        }\n"
        "        }"
    )

    if doubled_new in text:
        text = text.replace(doubled_new, new, 1)
        changed = True
    elif new in text:
        pass
    elif old_guard in text:
        text = text.replace(old_guard, new)
        changed = True
    elif old in text:
        text = text.replace(old, new)
        changed = True
    else:
        raise RuntimeError(
            "ESP-IDF DSI DMA2D cache sync line not found; patch needs review"
        )

    flow_controller_old_lines = (
        "        .flow_controller = DW_GDMA_FLOW_CTRL_DST, // DSI bridge as the flow controller",
        "        .flow_controller = DW_GDMA_FLOW_CTRL_SELF, // DMA as the flow controller",
    )
    flow_controller_new = (
        "#ifdef CONFIG_ESPHOME_MIPI_DSI_DMA_FLOW_CONTROLLER\n"
        "        .flow_controller = DW_GDMA_FLOW_CTRL_SELF, // DMA as the flow controller\n"
        "#else\n"
        "        .flow_controller = DW_GDMA_FLOW_CTRL_DST, // DSI bridge as the flow controller\n"
        "#endif"
    )
    if flow_controller_new not in text:
        for old_line in flow_controller_old_lines:
            if old_line in text:
                text = text.replace(old_line, flow_controller_new, 1)
                changed = True
                break
        else:
            raise RuntimeError(
                "ESP-IDF DSI GDMA flow-controller line not found; patch needs review"
            )

    bridge_flow_old_lines = (
        "    mipi_dsi_brg_ll_set_flow_controller(hal->bridge, MIPI_DSI_LL_FLOW_CONTROLLER_BRIDGE);",
        "    mipi_dsi_brg_ll_set_flow_controller(hal->bridge, MIPI_DSI_LL_FLOW_CONTROLLER_DMA);",
    )
    bridge_flow_new = (
        "#ifdef CONFIG_ESPHOME_MIPI_DSI_DMA_FLOW_CONTROLLER\n"
        "    mipi_dsi_brg_ll_set_flow_controller(hal->bridge, MIPI_DSI_LL_FLOW_CONTROLLER_DMA);\n"
        "#else\n"
        "    mipi_dsi_brg_ll_set_flow_controller(hal->bridge, MIPI_DSI_LL_FLOW_CONTROLLER_BRIDGE);\n"
        "#endif"
    )
    bridge_flow_safe_start = (
        "    // Start with bridge flow control. If CONFIG_ESPHOME_MIPI_DSI_DMA_FLOW_CONTROLLER is set,\n"
        "    // ESPHome switches to DMA flow control after the first confirmed refresh.\n"
        "    mipi_dsi_brg_ll_set_flow_controller(hal->bridge, MIPI_DSI_LL_FLOW_CONTROLLER_BRIDGE);"
    )
    bridge_flow_safe_current = (
        "    // Keep DSI bridge flow control at startup. The DMA flow-controller option is used\n"
        "    // for async frame copies; switching the live DSI bridge at runtime can stall scanout.\n"
        "    mipi_dsi_brg_ll_set_flow_controller(hal->bridge, MIPI_DSI_LL_FLOW_CONTROLLER_BRIDGE);"
    )
    bridge_flow_old_conditional = (
        "#ifdef CONFIG_ESPHOME_MIPI_DSI_DMA_FLOW_CONTROLLER\n"
        "    mipi_dsi_brg_ll_set_flow_controller(hal->bridge, MIPI_DSI_LL_FLOW_CONTROLLER_DMA);\n"
        "#else\n"
        "    mipi_dsi_brg_ll_set_flow_controller(hal->bridge, MIPI_DSI_LL_FLOW_CONTROLLER_BRIDGE);\n"
        "#endif"
    )
    if bridge_flow_new not in text:
        if bridge_flow_safe_current in text:
            text = text.replace(bridge_flow_safe_current, bridge_flow_new, 1)
            changed = True
        elif bridge_flow_safe_start in text:
            text = text.replace(bridge_flow_safe_start, bridge_flow_new, 1)
            changed = True
        elif bridge_flow_old_conditional in text:
            text = text.replace(bridge_flow_old_conditional, bridge_flow_new, 1)
            changed = True
        else:
            for old_line in bridge_flow_old_lines:
                if old_line in text:
                    text = text.replace(old_line, bridge_flow_new, 1)
                    changed = True
                    break
            else:
                raise RuntimeError(
                    "ESP-IDF DSI bridge flow-controller line not found; patch needs review"
                )
    stale_bridge_flow_comment = (
        "    // Start with bridge flow control. If CONFIG_ESPHOME_MIPI_DSI_DMA_FLOW_CONTROLLER is set,\n"
        "    // ESPHome switches to DMA flow control after the first confirmed refresh.\n"
    )
    if stale_bridge_flow_comment in text:
        text = text.replace(stale_bridge_flow_comment, "", 1)
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
            raise RuntimeError(
                "ESP-IDF DSI GDMA priority line not found; patch needs review"
            )
        text = text.replace(old_line, new_line, 1)
        changed = True

    gdma_intr_priority_anchor = "        .chan_priority = 3,\n"
    gdma_intr_priority_patch = (
        gdma_intr_priority_anchor
        + "#if defined(CONFIG_ESPHOME_MIPI_DSI_GDMA_INTR_PRIORITY) && "
        "CONFIG_ESPHOME_MIPI_DSI_GDMA_INTR_PRIORITY > 0\n"
        "        .intr_priority = CONFIG_ESPHOME_MIPI_DSI_GDMA_INTR_PRIORITY,\n"
        "#endif\n"
    )
    if gdma_intr_priority_patch not in text:
        if gdma_intr_priority_anchor not in text:
            raise RuntimeError(
                "ESP-IDF DSI GDMA channel priority line not found; patch needs review"
            )
        text = text.replace(
            gdma_intr_priority_anchor, gdma_intr_priority_patch, 1
        )
        changed = True

    qos_anchor = '    ESP_RETURN_ON_ERROR(dw_gdma_new_channel(&dma_alloc_config, &dma_chan), TAG, "create DMA channel failed");\n'
    old_qos_patch = (
        qos_anchor + "#if CONFIG_IDF_TARGET_ESP32P4\n"
        "    // DSI scanout is a real-time PSRAM reader. Give DW-GDMA read traffic\n"
        "    // higher AXI QoS than opportunistic DMA2D/JPEG copies to avoid rare\n"
        "    // bridge FIFO underruns that otherwise show as a full-screen fallback\n"
        "    // color flash.\n"
        "    axi_icm_ll_set_dw_gdma_qos_arbiter_prio(0, 4, 15);\n"
        "    axi_icm_ll_set_dw_gdma_qos_arbiter_prio(1, 4, 15);\n"
        "#endif\n"
    )
    qos_patch = (
        qos_anchor + "#if CONFIG_IDF_TARGET_ESP32P4\n"
        "    // DSI scanout is a real-time PSRAM reader. Give DW-GDMA read traffic\n"
        "    // higher AXI QoS than opportunistic DMA2D/JPEG copies to avoid rare\n"
        "    // bridge FIFO underruns that otherwise show as a full-screen fallback\n"
        "    // color flash.\n"
        "    axi_icm_ll_set_dw_gdma_qos_arbiter_prio(0, 4, 15);\n"
        "    axi_icm_ll_set_dw_gdma_qos_arbiter_prio(1, 4, 15);\n"
        "    // JPEG decode and PPA operations use the DMA2D AXI master and can burst\n"
        "    // enough PSRAM traffic to starve the DSI bridge FIFO without latching a\n"
        "    // bridge underrun interrupt. Keep DMA2D accelerated, but make it less\n"
        "    // aggressive than scanout.\n"
        "    axi_icm_ll_set_dma2d_qos_arbiter_prio(1, 1);\n"
        "    axi_icm_ll_set_qos_burstiness(AXI_ICM_MASTER_DMA2D, 8, AXI_ICM_ACCESS_READ);\n"
        "    axi_icm_ll_set_qos_burstiness(AXI_ICM_MASTER_DMA2D, 8, AXI_ICM_ACCESS_WRITE);\n"
        "#endif\n"
    )
    if qos_patch not in text:
        if old_qos_patch in text:
            text = text.replace(old_qos_patch, qos_patch, 1)
        elif qos_anchor in text:
            text = text.replace(qos_anchor, qos_patch, 1)
        else:
            raise RuntimeError(
                "ESP-IDF DSI GDMA channel creation line not found; patch needs review"
            )
        changed = True

    # Migrate framework caches patched by the failed ISR-affinity experiment.
    affinity_marker = "static void dpi_panel_register_dma_callbacks_ipc(void *arg)"
    cache_helper_anchor = (
        "static bool dpi_panel_skip_draw_buffer_msync(const void *draw_buffer)\n"
    )
    if affinity_marker in text:
        affinity_start = text.rfind(
            "#if !CONFIG_FREERTOS_UNICORE", 0, text.find(affinity_marker)
        )
        affinity_end = text.find(cache_helper_anchor, text.find(affinity_marker))
        if affinity_start == -1 or affinity_end == -1:
            raise RuntimeError(
                "ESP-IDF DSI ISR-affinity helper bounds not found; patch needs review"
            )
        text = text[:affinity_start] + text[affinity_end:]
        changed = True

    dma_callback_registration = (
        "    ESP_RETURN_ON_ERROR(dpi_panel_register_dma_callbacks(dma_chan, &dsi_dma_cbs, dpi_panel), "
        'TAG, "register DMA callbacks failed");'
    )
    upstream_dma_callback_registration = (
        "    ESP_RETURN_ON_ERROR(dw_gdma_channel_register_event_callbacks(dma_chan, &dsi_dma_cbs, dpi_panel), "
        'TAG, "register DMA callbacks failed");'
    )
    if dma_callback_registration in text:
        text = text.replace(
            dma_callback_registration,
            upstream_dma_callback_registration,
            1,
        )
        changed = True
    elif upstream_dma_callback_registration not in text:
        raise RuntimeError(
            "ESP-IDF DSI DMA callback registration line not found; patch needs review"
        )

    list_transfer_type = (
        "            .block_transfer_type = DW_GDMA_BLOCK_TRANSFER_LIST,\n"
    )
    reload_transfer_type = (
        "            .block_transfer_type = DW_GDMA_BLOCK_TRANSFER_RELOAD,\n"
    )
    list_transfer_count = text.count(list_transfer_type)
    reload_transfer_count = text.count(reload_transfer_type)
    if reload_transfer_count:
        text = text.replace(reload_transfer_type, list_transfer_type)
        changed = True
    elif list_transfer_count != 2:
        raise RuntimeError(
            "ESP-IDF DSI DMA transfer modes not found; patch needs review"
        )

    callback_mode_old = (
        "    dw_gdma_event_callbacks_t dsi_dma_cbs = {\n"
        "        .on_full_trans_done = mipi_dsi_dma_trans_done_cb,\n"
        "    };\n"
    )
    callback_mode_continuous = (
        "    dw_gdma_event_callbacks_t dsi_dma_cbs = {\n"
        "        .on_block_trans_done = mipi_dsi_dma_trans_done_cb,\n"
        "    };\n"
    )
    if callback_mode_old in text:
        text = text.replace(callback_mode_old, callback_mode_continuous, 1)
        changed = True
    elif callback_mode_continuous not in text:
        raise RuntimeError(
            "ESP-IDF DSI DMA callback mode not found; patch needs review"
        )

    create_link_anchor = (
        "static esp_err_t dpi_panel_create_dma_link(esp_lcd_dpi_panel_t *dpi_panel)\n"
        "{\n"
    )
    legacy_create_link_init = (
        create_link_anchor
        + "    portMUX_INITIALIZE(&dpi_panel->dma_switch_lock);\n"
        "    dpi_panel->active_fb_index = 0;\n"
        "    dpi_panel->pending_fb_index = DPI_PANEL_MAX_FB_NUM;\n"
    )
    create_link_init = (
        create_link_anchor
        + "    portMUX_INITIALIZE(&dpi_panel->dma_switch_lock);\n"
        "    dpi_panel->dma_chan_id = -1;\n"
        "    dpi_panel->active_fb_index = 0;\n"
        "    dpi_panel->pending_fb_index = DPI_PANEL_MAX_FB_NUM;\n"
        "    dpi_panel->dma_lli_lookup_failures = 0;\n"
    )
    reload_create_link_init = (
        create_link_anchor
        + "    portMUX_INITIALIZE(&dpi_panel->dma_switch_lock);\n"
        "    dpi_panel->dma_chan_id = -1;\n"
        "    dpi_panel->active_fb_index = 0;\n"
        "    dpi_panel->pending_fb_index = DPI_PANEL_MAX_FB_NUM;\n"
    )
    normalized_text = re.sub(
        r"(?:    dpi_panel->dma_lli_lookup_failures = 0;\n)+",
        "    dpi_panel->dma_lli_lookup_failures = 0;\n",
        text,
        count=1,
    )
    if normalized_text != text:
        text = normalized_text
        changed = True
    if legacy_create_link_init in text:
        text = text.replace(legacy_create_link_init, create_link_init, 1)
        changed = True
    elif reload_create_link_init in text:
        text = text.replace(reload_create_link_init, create_link_init, 1)
        changed = True
    if create_link_init not in text:
        if create_link_anchor not in text:
            raise RuntimeError(
                "ESP-IDF DSI DMA link constructor not found; patch needs review"
            )
        text = text.replace(create_link_anchor, create_link_init, 1)
        changed = True

    channel_store_anchor = "    dpi_panel->dma_chan = dma_chan;\n"
    channel_store_patch = (
        channel_store_anchor
        + "    ESP_RETURN_ON_ERROR(dw_gdma_channel_get_id(dma_chan, &dpi_panel->dma_chan_id),\n"
        '                        TAG, "get DMA channel ID failed");\n'
    )
    if channel_store_patch not in text:
        if channel_store_anchor not in text:
            raise RuntimeError(
                "ESP-IDF DSI DMA channel assignment not found; patch needs review"
            )
        text = text.replace(channel_store_anchor, channel_store_patch, 1)
        changed = True

    legacy_link_list_config = (
        "    dw_gdma_link_list_config_t link_list_config = {\n"
        "        .num_items = DPI_PANEL_MIN_DMA_NODES_PER_LINK,\n"
        "        .link_type = DW_GDMA_LINKED_LIST_TYPE_SINGLY,\n"
        "    };\n"
    )
    continuous_link_list_config = (
        "    dw_gdma_link_list_config_t link_list_config = {\n"
        "        .num_items = 2,\n"
        "        .link_type = DW_GDMA_LINKED_LIST_TYPE_CIRCULAR,\n"
        "    };\n"
    )
    if legacy_link_list_config in text:
        text = text.replace(legacy_link_list_config, continuous_link_list_config, 1)
        changed = True
    elif continuous_link_list_config not in text:
        raise RuntimeError(
            "ESP-IDF DSI link-list configuration not found; patch needs review"
        )

    descriptor_setup_legacy = (
        "        dw_gdma_lli_config_transfer(dw_gdma_link_list_get_item(link_list, 0), &dma_transfer_config);\n"
        "        dw_gdma_block_markers_t markers = {\n"
        "            .is_valid = true,\n"
        "            .is_last = true,\n"
        "        };\n"
        "        dw_gdma_lli_set_block_markers(dw_gdma_link_list_get_item(link_list, 0), markers);\n"
    )
    descriptor_setup_full_frames = (
        "        for (int slot = 0; slot < 2; slot++) {\n"
        "            dw_gdma_lli_handle_t frame_lli = dw_gdma_link_list_get_item(link_list, slot);\n"
        "            dw_gdma_lli_config_transfer(frame_lli, &dma_transfer_config);\n"
        "            dw_gdma_block_markers_t markers = {\n"
        "                .is_valid = true,\n"
        "                .is_last = false,\n"
        "                .en_trans_done_intr = true,\n"
        "            };\n"
        "            dw_gdma_lli_set_block_markers(frame_lli, markers);\n"
        "            if (i == 0) {\n"
        "                dpi_panel->frame_llis[slot] = frame_lli;\n"
        "                dpi_panel->frame_lli_fb_index[slot] = 0;\n"
        "            }\n"
        "        }\n"
    )
    descriptor_setup_continuous = (
        "        const size_t total_items = dpi_panel->fb_size * 8 / 64;\n"
        "        const size_t first_half_items = total_items / 2;\n"
        "        const size_t first_half_bytes = first_half_items * (64 / 8);\n"
        "        for (int slot = 0; slot < 2; slot++) {\n"
        "            dw_gdma_lli_handle_t frame_lli = dw_gdma_link_list_get_item(link_list, slot);\n"
        "            dma_transfer_config.src.addr = (uint32_t)(dpi_panel->fbs[i] +\n"
        "                                                       (slot == 0 ? 0 : first_half_bytes));\n"
        "            dma_transfer_config.size = slot == 0 ? first_half_items : total_items - first_half_items;\n"
        "            dw_gdma_lli_config_transfer(frame_lli, &dma_transfer_config);\n"
        "            dw_gdma_block_markers_t markers = {\n"
        "                .is_valid = true,\n"
        "                .is_last = false,\n"
        "                .en_trans_done_intr = true,\n"
        "            };\n"
        "            dw_gdma_lli_set_block_markers(frame_lli, markers);\n"
        "            if (i == 0) {\n"
        "                dpi_panel->frame_llis[slot] = frame_lli;\n"
        "                dpi_panel->frame_lli_fb_index[slot] = 0;\n"
        "            }\n"
        "        }\n"
    )
    descriptor_setup_autonomous = (
        "        dw_gdma_lli_handle_t frame_lli = dw_gdma_link_list_get_item(link_list, 0);\n"
        "        dpi_panel->frame_llis[i] = frame_lli;\n"
        "        dw_gdma_lli_config_transfer(frame_lli, &dma_transfer_config);\n"
        "        dpi_panel_set_lli_next(frame_lli, frame_lli);\n"
        "        dw_gdma_block_markers_t markers = {\n"
        "            .is_valid = true,\n"
        "            .is_last = false,\n"
        "            .en_trans_done_intr = true,\n"
        "        };\n"
        "        dw_gdma_lli_set_block_markers(frame_lli, markers);\n"
    )
    if descriptor_setup_full_frames in text:
        text = text.replace(
            descriptor_setup_full_frames, descriptor_setup_continuous, 1
        )
        changed = True
    elif descriptor_setup_autonomous in text:
        text = text.replace(
            descriptor_setup_autonomous, descriptor_setup_continuous, 1
        )
        changed = True
    elif descriptor_setup_legacy in text:
        text = text.replace(
            descriptor_setup_legacy, descriptor_setup_continuous, 1
        )
        changed = True
    elif descriptor_setup_continuous not in text:
        raise RuntimeError(
            "ESP-IDF DSI frame descriptor setup not found; patch needs review"
        )

    initial_fb_anchor = (
        "    // by default, we use the fb0 as the first working frame buffer\n"
        "    dpi_panel->cur_fb_index = 0;\n"
    )
    legacy_initial_fb_patch = (
        "    // Start with fb0 and switch queued framebuffers only at full-frame boundaries.\n"
        "    dpi_panel->cur_fb_index = 0;\n"
        "    dpi_panel->active_fb_index = 0;\n"
        "    dpi_panel->pending_fb_index = DPI_PANEL_MAX_FB_NUM;\n"
    )
    full_frame_initial_fb_patch = (
        "    // Start both ring descriptors on fb0. GDMA advances continuously;\n"
        "    // completed descriptors are retargeted only from the block ISR.\n"
        "    dpi_panel->cur_fb_index = 0;\n"
        "    dpi_panel->active_fb_index = 0;\n"
        "    dpi_panel->pending_fb_index = DPI_PANEL_MAX_FB_NUM;\n"
    )
    initial_fb_patch = (
        "    // Start both half-frame descriptors on fb0. GDMA advances\n"
        "    // continuously and framebuffers change only at full-frame boundaries.\n"
        "    dpi_panel->cur_fb_index = 0;\n"
        "    dpi_panel->active_fb_index = 0;\n"
        "    dpi_panel->pending_fb_index = DPI_PANEL_MAX_FB_NUM;\n"
    )
    reload_initial_fb_patch = (
        "    // Start with fb0. Reload mode repeats the frame autonomously, so\n"
        "    // scanout no longer depends on a per-frame ISR restart.\n"
        "    dpi_panel->cur_fb_index = 0;\n"
        "    dpi_panel->active_fb_index = 0;\n"
        "    dpi_panel->pending_fb_index = DPI_PANEL_MAX_FB_NUM;\n"
    )
    old_initial_fb_patch = (
        "    // Start with fb0. Every descriptor self-loops, so scanout no longer\n"
        "    // depends on a per-frame ISR restart.\n"
        "    dpi_panel->cur_fb_index = 0;\n"
        "    dpi_panel->active_fb_index = 0;\n"
        "    dpi_panel->pending_fb_index = DPI_PANEL_MAX_FB_NUM;\n"
        "    dpi_panel->switch_from_fb_index = DPI_PANEL_MAX_FB_NUM;\n"
    )
    if full_frame_initial_fb_patch in text:
        text = text.replace(full_frame_initial_fb_patch, initial_fb_patch, 1)
        changed = True
    elif old_initial_fb_patch in text:
        text = text.replace(old_initial_fb_patch, initial_fb_patch, 1)
        changed = True
    elif reload_initial_fb_patch in text:
        text = text.replace(reload_initial_fb_patch, initial_fb_patch, 1)
        changed = True
    elif legacy_initial_fb_patch in text:
        text = text.replace(legacy_initial_fb_patch, initial_fb_patch, 1)
        changed = True
    if initial_fb_patch not in text:
        if initial_fb_anchor not in text:
            raise RuntimeError(
                "ESP-IDF DSI initial framebuffer selection not found; patch needs review"
            )
        text = text.replace(initial_fb_anchor, initial_fb_patch, 1)
        changed = True

    list_start = (
        "    link_list = dpi_panel->link_lists[0];\n"
        "    dw_gdma_channel_use_link_list(dma_chan, link_list);\n"
        "    // enable the DMA channel\n"
        "    dw_gdma_channel_enable_ctrl(dma_chan, true);\n"
    )
    reload_start = (
        "    dma_transfer_config.src.addr = (uint32_t)dpi_panel->fbs[0];\n"
        "    ESP_RETURN_ON_ERROR(dw_gdma_channel_config_transfer(dma_chan, &dma_transfer_config),\n"
        '                        TAG, "configure DMA reload transfer failed");\n'
        "    dw_gdma_block_markers_t reload_markers = {\n"
        "        .is_valid = true,\n"
        "        .is_last = false,\n"
        "        .en_trans_done_intr = true,\n"
        "    };\n"
        "    ESP_RETURN_ON_ERROR(dw_gdma_channel_set_block_markers(dma_chan, reload_markers),\n"
        '                        TAG, "configure DMA reload markers failed");\n'
        "    // Reload mode repeats the block without a per-frame stop/restart.\n"
        "    dw_gdma_channel_enable_ctrl(dma_chan, true);\n"
    )
    if reload_start in text:
        text = text.replace(reload_start, list_start, 1)
        changed = True
    elif list_start not in text:
        raise RuntimeError(
            "ESP-IDF DSI DMA startup block not found; patch needs review"
        )

    draw_fb_select_old = "        dpi_panel->cur_fb_index = draw_buf_fb_index;\n"
    draw_fb_select_new = (
        "        ESP_RETURN_ON_ERROR(dpi_panel_queue_framebuffer_index(dpi_panel, draw_buf_fb_index),\n"
        '                            TAG, "queue framebuffer failed");\n'
    )
    if draw_fb_select_new not in text:
        if draw_fb_select_old not in text:
            raise RuntimeError(
                "ESP-IDF DSI direct framebuffer selection not found; patch needs review"
            )
        text = text.replace(draw_fb_select_old, draw_fb_select_new, 1)
        changed = True

    # A full framebuffer submission is complete only after the DMA callback
    # switches the LIST descriptor at a frame boundary. The stock callback in
    # draw_bitmap reports completion immediately after queueing, which lets a
    # second producer overwrite or queue the pending framebuffer too early.
    immediate_draw_callback = (
        f"{draw_fb_select_new}"
        "        // invoke the trans done callback\n"
        "        if (dpi_panel->on_color_trans_done) {\n"
        "            dpi_panel->on_color_trans_done(&dpi_panel->base, NULL, dpi_panel->user_ctx);\n"
        "        }\n"
    )
    if immediate_draw_callback in text:
        text = text.replace(immediate_draw_callback, draw_fb_select_new, 1)
        changed = True

    single_block_bridge = (
        "    mipi_dsi_brg_ll_set_multi_block_number(hal->bridge, DPI_PANEL_MIN_DMA_NODES_PER_LINK);\n"
    )
    half_frame_bridge = (
        "#ifdef CONFIG_ESPHOME_MIPI_DSI_DMA_FLOW_CONTROLLER\n"
        "    // The continuous DMA ring divides one video frame into two blocks.\n"
        "    mipi_dsi_brg_ll_set_multi_block_number(hal->bridge, 2);\n"
        "#else\n"
        "    mipi_dsi_brg_ll_set_multi_block_number(hal->bridge, DPI_PANEL_MIN_DMA_NODES_PER_LINK);\n"
        "#endif\n"
    )
    if single_block_bridge in text and half_frame_bridge not in text:
        text = text.replace(single_block_bridge, half_frame_bridge, 1)
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
            raise RuntimeError(
                "ESP-IDF DSI FIFO tuning line not found; patch needs review"
            )
        text = text.replace(old_line, new_line, 1)
        changed = True

    legacy_fifo_tuning = (
        "    hal->bridge->mem_clk_ctrl.dsi_bridge_mem_clk_force_on = 1;\n"
        "    hal->bridge->mem_clk_ctrl.dsi_mem_clk_force_on = 1;\n"
        "    mipi_dsi_brg_ll_set_burst_len(hal->bridge, 128);"
    )
    overwide_fifo_tuning = (
        "    hal->bridge->mem_clk_ctrl.dsi_bridge_mem_clk_force_on = 1;\n"
        "    hal->bridge->mem_clk_ctrl.dsi_mem_clk_force_on = 1;\n"
        "    mipi_dsi_brg_ll_set_burst_len(hal->bridge, 256);"
    )
    if overwide_fifo_tuning in text:
        text = text.replace(overwide_fifo_tuning, legacy_fifo_tuning, 1)
        changed = True
    overwide_empty_threshold = (
        "    mipi_dsi_brg_ll_set_empty_threshold(hal->bridge, 1024 - 256);"
    )
    desired_empty_threshold = (
        "    mipi_dsi_brg_ll_set_empty_threshold(hal->bridge, 1024 - 128);"
    )
    if overwide_empty_threshold in text:
        text = text.replace(overwide_empty_threshold, desired_empty_threshold, 1)
        changed = True

    credit_reset = "    mipi_dsi_brg_ll_credit_reset(hal->bridge);"
    if credit_reset in text:
        text = text.replace(f"{credit_reset}\n", "", 1)
        changed = True

    if changed and text != original_text:
        target.write_text(text, encoding="utf-8")
        print("MIPI DSI patch: applied ESP-IDF 5.x DMA2D/cache diagnostics patch")
    else:
        print(
            "MIPI DSI patch: ESP-IDF 5.x DMA2D/cache diagnostics patch already present"
        )


def _patch_continuous_hs_clock(framework_dir: Path) -> None:
    bus_target = (
        framework_dir
        / "components"
        / "esp_lcd"
        / "dsi"
        / "esp_lcd_mipi_dsi_bus.c"
    )
    panel_target = (
        framework_dir / "components" / "esp_lcd" / "dsi" / "esp_lcd_panel_dpi.c"
    )

    bus_text = bus_target.read_text(encoding="utf-8")
    early_force_hs = (
        "#ifdef CONFIG_ESPHOME_MIPI_DSI_CONTINUOUS_HS_CLOCK\n"
        "    mipi_dsi_host_ll_set_clock_lane_state(hal->host, MIPI_DSI_LL_CLOCK_LANE_STATE_HS);\n"
        "#else\n"
        "    mipi_dsi_host_ll_set_clock_lane_state(hal->host, bus_config->flags.clock_lane_force_hs ? "
        "MIPI_DSI_LL_CLOCK_LANE_STATE_HS : MIPI_DSI_LL_CLOCK_LANE_STATE_AUTO);\n"
        "#endif\n"
    )
    upstream_bus_state = (
        "    mipi_dsi_host_ll_set_clock_lane_state(hal->host, bus_config->flags.clock_lane_force_hs ? "
        "MIPI_DSI_LL_CLOCK_LANE_STATE_HS : MIPI_DSI_LL_CLOCK_LANE_STATE_AUTO);\n"
    )
    if early_force_hs in bus_text:
        bus_text = bus_text.replace(early_force_hs, upstream_bus_state, 1)
        bus_target.write_text(bus_text, encoding="utf-8")
        print("MIPI DSI patch: restored command-mode clock lane handling")

    panel_text = panel_target.read_text(encoding="utf-8")
    late_clock_lane = (
        "#ifdef CONFIG_ESPHOME_MIPI_DSI_CONTINUOUS_HS_CLOCK\n"
        "    mipi_dsi_host_ll_set_clock_lane_state(hal->host, MIPI_DSI_LL_CLOCK_LANE_STATE_HS);\n"
        "#else\n"
        "    mipi_dsi_host_ll_set_clock_lane_state(hal->host, MIPI_DSI_LL_CLOCK_LANE_STATE_AUTO);\n"
        "#endif\n"
    )
    if late_clock_lane in panel_text:
        return

    idf54_clock_lane = (
        "    // switch the clock lane to high speed mode\n"
        "    mipi_dsi_host_ll_set_clock_lane_state(hal->host, MIPI_DSI_LL_CLOCK_LANE_STATE_AUTO);\n"
    )
    video_mode_anchor = (
        "    // enable the video mode\n"
        "    mipi_dsi_host_ll_enable_video_mode(hal->host, true);\n"
    )
    if idf54_clock_lane in panel_text:
        replacement = (
            "    // switch the clock lane after panel commands have completed\n"
            f"{late_clock_lane}"
        )
        panel_text = panel_text.replace(idf54_clock_lane, replacement, 1)
    elif video_mode_anchor in panel_text:
        replacement = (
            f"{video_mode_anchor}"
            "    // Force continuous HS only after panel commands have completed.\n"
            f"{late_clock_lane}"
        )
        panel_text = panel_text.replace(video_mode_anchor, replacement, 1)
    else:
        raise RuntimeError(
            "ESP-IDF DSI video-mode clock lane anchor not found; patch needs review"
        )

    panel_target.write_text(panel_text, encoding="utf-8")
    print("MIPI DSI patch: continuous HS starts with the DPI video stream")


def _patch_idf6_or_newer(framework_dir: Path) -> None:
    target = framework_dir / "components" / "esp_lcd" / "src" / "esp_async_fbcpy.c"
    text = target.read_text(encoding="utf-8")

    for include in ('#include "esp_memory_utils.h"', '#include "soc/soc_caps.h"'):
        if include not in text:
            text = text.replace(
                '#include "esp_heap_caps.h"\n',
                f'#include "esp_heap_caps.h"\n{include}\n',
            )

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
            raise RuntimeError(
                "ESP-IDF async_fbcpy TAG declaration not found; patch needs review"
            )
        text = text.replace(anchor, f"{anchor}{helper}", 1)
    elif "!esp_ptr_external_ram(src_buffer)" not in text:
        old_tail = "#endif\n    return false;\n}\n"
        new_tail = (
            "#endif\n"
            "    if (!esp_ptr_external_ram(src_buffer)) {\n"
            "        return true;\n"
            "    }\n"
            "    return false;\n"
            "}\n"
        )
        if old_tail not in text:
            raise RuntimeError(
                "ESP-IDF async_fbcpy cache sync helper tail not found; patch needs review"
            )
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
        print(
            "MIPI DSI patch: ESP-IDF 6.x DMA2D internal-buffer cache sync guard already present"
        )
        return
    if old in text:
        text = text.replace(old, new, 1)
    else:
        raise RuntimeError(
            "ESP-IDF async_fbcpy cache sync line not found; patch needs review"
        )

    target.write_text(text, encoding="utf-8")
    print("MIPI DSI patch: applied ESP-IDF 6.x DMA2D internal-buffer cache sync guard")


def _patch_jpeg_decode_csc(framework_dir: Path) -> None:
    target = framework_dir / "components" / "esp_driver_jpeg" / "jpeg_decode.c"
    if not target.exists():
        print(
            "MIPI DSI patch: ESP-IDF JPEG decode driver not found; skipping RGB CSC patch"
        )
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
            raise RuntimeError(
                "ESP-IDF JPEG decode CSC function not found; patch needs review"
            )
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
        raise RuntimeError(
            "ESP-IDF JPEG decode RGB CSC block not found; patch needs review"
        )

    text = text.replace(old, new, 1)
    target.write_text(text, encoding="utf-8")
    print("MIPI DSI patch: applied ESP-IDF JPEG decode RGB CSC selection")


def _patch_jpeg_decode_dma2d_burst(framework_dir: Path) -> None:
    target = framework_dir / "components" / "esp_driver_jpeg" / "jpeg_decode.c"
    if not target.exists():
        print(
            "MIPI DSI patch: ESP-IDF JPEG decode driver not found; skipping DMA2D burst patch"
        )
        return

    text = target.read_text(encoding="utf-8")
    helper = """
__attribute__((weak)) int esphome_esp32_jpeg_dma2d_burst_length(void)
{
#ifdef CONFIG_ESPHOME_JPEG_DMA2D_BURST_LENGTH
    return CONFIG_ESPHOME_JPEG_DMA2D_BURST_LENGTH;
#else
    return 128;
#endif
}

__attribute__((weak)) bool esphome_esp32_jpeg_dma2d_desc_burst_enabled(void)
{
#ifdef CONFIG_ESPHOME_JPEG_DMA2D_DESC_BURST_DISABLE
    return false;
#else
    return true;
#endif
}

static uint32_t jpeg_dec_select_dma2d_burst_length(void)
{
    switch (esphome_esp32_jpeg_dma2d_burst_length()) {
    case 1:
    case 8:
        return 8;
    case 16:
        return 16;
    case 32:
        return 32;
    case 64:
        return 64;
    case 128:
    default:
        return 128;
    }
}

static bool jpeg_dec_select_dma2d_desc_burst_en(void)
{
    return esphome_esp32_jpeg_dma2d_desc_burst_enabled();
}
"""
    changed = False
    include = '#include "esp_memory_utils.h"'
    if include not in text:
        anchor = '#include "esp_heap_caps.h"\n'
        if anchor not in text:
            raise RuntimeError(
                "ESP-IDF JPEG decode heap include not found; patch needs review"
            )
        text = text.replace(anchor, f"{anchor}{include}\n", 1)
        changed = True

    anchor = "static void jpeg_dec_config_dma_trans_ability(jpeg_decoder_handle_t decoder_engine)\n"
    anchor_pos = text.find(anchor)
    if anchor_pos < 0:
        raise RuntimeError(
            "ESP-IDF JPEG decode DMA2D transfer ability function not found; patch needs review"
        )
    helper_start = text.find(
        "__attribute__((weak)) int esphome_esp32_jpeg_dma2d_burst_length"
    )
    if helper_start < 0:
        helper_start = text.find(
            "static dma2d_data_burst_length_t jpeg_dec_select_dma2d_burst_length"
        )
    if 0 <= helper_start < anchor_pos:
        current_helper = text[helper_start:anchor_pos].strip()
        if current_helper != helper.strip():
            text = text[:helper_start] + f"{helper}\n" + text[anchor_pos:]
            changed = True
    else:
        text = text[:anchor_pos] + f"{helper}\n" + text[anchor_pos:]
        changed = True

    output_msync_helper = """
__attribute__((weak)) bool esphome_esp32_jpeg_skip_output_cache_msync(void)
{
    return false;
}

static esp_err_t jpeg_dec_output_cache_msync(void *buffer, size_t size)
{
#ifdef CONFIG_ESPHOME_JPEG_DMA2D_RUNTIME_SKIP_POST_OUTPUT_MSYNC
    if (esphome_esp32_jpeg_skip_output_cache_msync()) {
        return ESP_OK;
    }
#endif
#ifdef CONFIG_ESPHOME_JPEG_DMA2D_SKIP_POST_OUTPUT_MSYNC
    return ESP_OK;
#endif
    size_t cache_align = 0;
    esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &cache_align);
    if (cache_align == 0) {
        cache_align = cache_hal_get_cache_line_size(CACHE_LL_LEVEL_EXT_MEM, CACHE_TYPE_DATA);
    }
    if (cache_align > 0) {
        uintptr_t start = (uintptr_t)buffer;
        uintptr_t aligned_start = start & ~(uintptr_t)(cache_align - 1);
        uintptr_t end = start + size;
        uintptr_t aligned_end = (end + cache_align - 1) & ~(uintptr_t)(cache_align - 1);
        buffer = (void *)aligned_start;
        size = aligned_end - aligned_start;
    }
#ifdef CONFIG_ESPHOME_JPEG_DMA2D_OUTPUT_MSYNC_CHUNK
    const size_t chunk_size = CONFIG_ESPHOME_JPEG_DMA2D_OUTPUT_MSYNC_CHUNK;
    if (chunk_size > 0 && size > chunk_size && esp_ptr_external_ram(buffer)) {
        uint8_t *ptr = (uint8_t *)buffer;
        size_t remaining = size;
        while (remaining > 0) {
            size_t this_chunk = remaining > chunk_size ? chunk_size : remaining;
            esp_err_t ret = esp_cache_msync(ptr, this_chunk, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "JPEG output msync chunk failed ret=%d ptr=%p size=%zu align=%zu",
                         ret, ptr, this_chunk, cache_align);
                return ret;
            }
            ptr += this_chunk;
            remaining -= this_chunk;
#ifdef CONFIG_ESPHOME_JPEG_DMA2D_OUTPUT_MSYNC_YIELD
            if (remaining > 0) {
                vTaskDelay(1);
            }
#endif
        }
        return ESP_OK;
    }
#endif
    esp_err_t ret = esp_cache_msync(buffer, size, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "JPEG output msync failed ret=%d ptr=%p size=%zu align=%zu",
                 ret, buffer, size, cache_align);
    }
    return ret;
}

static esp_err_t jpeg_dec_pre_output_cache_msync(void *buffer, size_t size)
{
    size_t cache_align = 0;
    esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &cache_align);
    if (cache_align == 0) {
        cache_align = cache_hal_get_cache_line_size(CACHE_LL_LEVEL_EXT_MEM, CACHE_TYPE_DATA);
    }
    if (cache_align > 0) {
        uintptr_t start = (uintptr_t)buffer;
        uintptr_t aligned_start = start & ~(uintptr_t)(cache_align - 1);
        uintptr_t end = start + size;
        uintptr_t aligned_end = (end + cache_align - 1) & ~(uintptr_t)(cache_align - 1);
        buffer = (void *)aligned_start;
        size = aligned_end - aligned_start;
    }
#ifdef CONFIG_ESPHOME_JPEG_DMA2D_PRE_OUTPUT_MSYNC_CHUNK
    const size_t chunk_size = CONFIG_ESPHOME_JPEG_DMA2D_PRE_OUTPUT_MSYNC_CHUNK;
    if (chunk_size > 0 && size > chunk_size && esp_ptr_external_ram(buffer)) {
        uint8_t *ptr = (uint8_t *)buffer;
        size_t remaining = size;
        while (remaining > 0) {
            size_t this_chunk = remaining > chunk_size ? chunk_size : remaining;
            esp_err_t ret = esp_cache_msync(ptr, this_chunk, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "JPEG pre-output msync chunk failed ret=%d ptr=%p size=%zu align=%zu",
                         ret, ptr, this_chunk, cache_align);
                return ret;
            }
            ptr += this_chunk;
            remaining -= this_chunk;
#ifdef CONFIG_ESPHOME_JPEG_DMA2D_PRE_OUTPUT_MSYNC_YIELD
            if (remaining > 0) {
                vTaskDelay(1);
            }
#endif
        }
        return ESP_OK;
    }
#endif
    esp_err_t ret = esp_cache_msync(buffer, size, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "JPEG pre-output msync failed ret=%d ptr=%p size=%zu align=%zu",
                 ret, buffer, size, cache_align);
    }
    return ret;
}
"""
    process_anchor = (
        "esp_err_t jpeg_decoder_process(jpeg_decoder_handle_t decoder_engine"
    )
    process_pos = text.find(process_anchor)
    if process_pos < 0:
        raise RuntimeError(
            "ESP-IDF JPEG decoder process function not found; patch needs review"
        )
    helper_pos = text.find("static esp_err_t jpeg_dec_output_cache_msync")
    weak_pos = text.find(
        "__attribute__((weak)) bool esphome_esp32_jpeg_skip_output_cache_msync"
    )
    if helper_pos >= 0:
        block_start = weak_pos if 0 <= weak_pos < helper_pos else helper_pos
        if block_start > process_pos:
            raise RuntimeError(
                "ESP-IDF JPEG output msync helper moved after process function; patch needs review"
            )
        current_helper = text[block_start:process_pos].strip()
        if current_helper != output_msync_helper.strip():
            text = text[:block_start] + f"{output_msync_helper}\n" + text[process_pos:]
            process_pos = text.find(process_anchor)
            changed = True
    else:
        text = text[:process_pos] + f"{output_msync_helper}\n" + text[process_pos:]
        changed = True

    new = ".data_burst_length = jpeg_dec_select_dma2d_burst_length(),"
    if new not in text:
        old_candidates = (
            ".data_burst_length = DMA2D_DATA_BURST_LENGTH_128,",
            ".data_burst_length = 128,",
        )
        old = next((candidate for candidate in old_candidates if text.count(candidate) == 2), None)
        if old is None:
            raise RuntimeError(
                "ESP-IDF JPEG decode DMA2D burst lines not found; patch needs review"
            )
        text = text.replace(old, new)
        changed = True

    old = ".desc_burst_en = true,"
    new = ".desc_burst_en = jpeg_dec_select_dma2d_desc_burst_en(),"
    if new not in text:
        count = text.count(old)
        if count != 2:
            raise RuntimeError(
                "ESP-IDF JPEG decode DMA2D desc burst lines not found; patch needs review"
            )
        text = text.replace(old, new)
        changed = True

    old = (
        "    // Before 2DDMA starts, invalidate cache ahead of time.\n"
        "    ret = esp_cache_msync((void*)decoder_engine->decoded_buf, outbuf_size, ESP_CACHE_MSYNC_FLAG_DIR_M2C);\n"
        "    assert(ret == ESP_OK);\n"
    )
    new = (
        "    // Before 2DDMA starts, invalidate cache ahead of time.\n"
        "    ret = jpeg_dec_pre_output_cache_msync((void*)decoder_engine->decoded_buf, outbuf_size);\n"
        "    if (ret != ESP_OK) {\n"
        '        ESP_LOGE(TAG, "JPEG pre-output msync failed ret=%d ptr=%p size=%" PRIu32,\n'
        "                 ret, (void*)decoder_engine->decoded_buf, outbuf_size);\n"
        "    }\n"
        "    assert(ret == ESP_OK);\n"
    )
    if new not in text:
        current_with_log = (
            "    // Before 2DDMA starts, invalidate cache ahead of time.\n"
            "#ifndef CONFIG_ESPHOME_JPEG_DMA2D_SKIP_PRE_OUTPUT_MSYNC\n"
            "    ret = esp_cache_msync((void*)decoder_engine->decoded_buf, outbuf_size, ESP_CACHE_MSYNC_FLAG_DIR_M2C);\n"
            "    if (ret != ESP_OK) {\n"
            '        ESP_LOGE(TAG, "JPEG pre-output msync failed ret=%d ptr=%p size=%" PRIu32,\n'
            "                 ret, (void*)decoder_engine->decoded_buf, outbuf_size);\n"
            "    }\n"
            "    assert(ret == ESP_OK);\n"
            "#endif\n"
        )
        current = (
            "    // Before 2DDMA starts, invalidate cache ahead of time.\n"
            "#ifndef CONFIG_ESPHOME_JPEG_DMA2D_SKIP_PRE_OUTPUT_MSYNC\n"
            "    ret = esp_cache_msync((void*)decoder_engine->decoded_buf, outbuf_size, ESP_CACHE_MSYNC_FLAG_DIR_M2C);\n"
            "    assert(ret == ESP_OK);\n"
            "#endif\n"
        )
        if current_with_log in text:
            text = text.replace(current_with_log, new, 1)
        elif current in text:
            text = text.replace(current, new, 1)
        elif old in text:
            text = text.replace(old, new, 1)
        else:
            raise RuntimeError(
                "ESP-IDF JPEG decode pre-output msync block not found; patch needs review"
            )
        changed = True

    old = "            ret = esp_cache_msync((void*)decoder_engine->decoded_buf, outbuf_size, ESP_CACHE_MSYNC_FLAG_DIR_M2C);"
    current = "            ret = jpeg_dec_output_cache_msync((void*)decoder_engine->decoded_buf, outbuf_size);"
    new = "            ret = jpeg_dec_output_cache_msync((void*)decoder_engine->decoded_buf, *out_size);"
    if new not in text:
        if current in text:
            text = text.replace(current, new, 1)
        elif old in text:
            text = text.replace(old, new, 1)
        else:
            raise RuntimeError(
                "ESP-IDF JPEG decode post-output msync line not found; patch needs review"
            )
        changed = True

    old = (
        "    xSemaphoreGive(decoder_engine->codec_base->codec_mutex);\n"
        "    if (decoder_engine->codec_base->pm_lock) {\n"
        '        ESP_RETURN_ON_ERROR(esp_pm_lock_release(decoder_engine->codec_base->pm_lock), TAG, "release pm_lock failed");\n'
        "    }\n"
        "    return ESP_OK;\n"
    )
    new = (
        "    xSemaphoreGive(decoder_engine->codec_base->codec_mutex);\n"
        "    if (decoder_engine->codec_base->pm_lock) {\n"
        "        ret = esp_pm_lock_release(decoder_engine->codec_base->pm_lock);\n"
        "        if (ret != ESP_OK) {\n"
        '            ESP_LOGE(TAG, "JPEG release pm_lock failed ret=%d", ret);\n'
        "            return ret;\n"
        "        }\n"
        "    }\n"
        "    return ESP_OK;\n"
    )
    old_with_pm_guard = (
        "    xSemaphoreGive(decoder_engine->codec_base->codec_mutex);\n"
        "#if CONFIG_PM_ENABLE\n"
        "    if (decoder_engine->codec_base->pm_lock) {\n"
        '        ESP_RETURN_ON_ERROR(esp_pm_lock_release(decoder_engine->codec_base->pm_lock), TAG, "release pm_lock failed");\n'
        "    }\n"
        "#endif\n"
        "    return ESP_OK;\n"
    )
    new_with_pm_guard = (
        "    xSemaphoreGive(decoder_engine->codec_base->codec_mutex);\n"
        "#if CONFIG_PM_ENABLE\n"
        "    if (decoder_engine->codec_base->pm_lock) {\n"
        "        ret = esp_pm_lock_release(decoder_engine->codec_base->pm_lock);\n"
        "        if (ret != ESP_OK) {\n"
        '            ESP_LOGE(TAG, "JPEG release pm_lock failed ret=%d", ret);\n'
        "            return ret;\n"
        "        }\n"
        "    }\n"
        "#endif\n"
        "    return ESP_OK;\n"
    )
    if new not in text and new_with_pm_guard not in text:
        if old_with_pm_guard in text:
            text = text.replace(old_with_pm_guard, new_with_pm_guard, 1)
        elif old in text:
            text = text.replace(old, new, 1)
        else:
            raise RuntimeError(
                "ESP-IDF JPEG decode success cleanup block not found; patch needs review"
            )
        changed = True

    old = (
        "err1:\n"
        "    dma2d_force_end(decoder_engine->trans_desc, &need_yield);\n"
        "err2:\n"
        "    xSemaphoreGive(decoder_engine->codec_base->codec_mutex);\n"
    )
    new = (
        "err1:\n"
        '    ESP_LOGE(TAG, "JPEG decoder error path ret=%d out_size=%" PRIu32, ret, *out_size);\n'
        "    dma2d_force_end(decoder_engine->trans_desc, &need_yield);\n"
        "err2:\n"
        '    ESP_LOGE(TAG, "JPEG decoder cleanup path ret=%d out_size=%" PRIu32, ret, *out_size);\n'
        "    xSemaphoreGive(decoder_engine->codec_base->codec_mutex);\n"
    )
    if new not in text:
        if old not in text:
            raise RuntimeError(
                "ESP-IDF JPEG decode error cleanup block not found; patch needs review"
            )
        text = text.replace(old, new, 1)
        changed = True

    old = (
        "        if (jpeg_dma2d_event.jpgd_status != 0) {\n"
        "            uint32_t status = jpeg_dma2d_event.jpgd_status;\n"
        "            s_decoder_error_log_print(status);\n"
        "            ret = ESP_ERR_INVALID_STATE;\n"
        "            goto err1;\n"
        "        }\n"
    )
    current = (
        "        if (jpeg_dma2d_event.jpgd_status != 0) {\n"
        "            uint32_t status = jpeg_dma2d_event.jpgd_status;\n"
        '            ESP_LOGE(TAG, "JPEG decoder raw status=0x%08lx", (unsigned long) status);\n'
        "            s_decoder_error_log_print(status);\n"
        "            ret = ESP_ERR_INVALID_STATE;\n"
        "            goto err1;\n"
        "        }\n"
    )
    new = (
        "        if (jpeg_dma2d_event.jpgd_status != 0) {\n"
        "            uint32_t status = jpeg_dma2d_event.jpgd_status;\n"
        "            esphome_esp32_jpeg_report_status(status);\n"
        '            ESP_LOGE(TAG, "JPEG decoder raw status=0x%08lx", (unsigned long) status);\n'
        "            s_decoder_error_log_print(status);\n"
        "            ret = ESP_ERR_INVALID_STATE;\n"
        "            goto err1;\n"
        "        }\n"
    )
    if new not in text:
        if current in text:
            text = text.replace(current, new, 1)
        elif old in text:
            text = text.replace(old, new, 1)
        else:
            raise RuntimeError(
                "ESP-IDF JPEG decode status block not found; patch needs review"
            )
        changed = True

    report_decl = (
        "__attribute__((weak)) void esphome_esp32_jpeg_report_status(uint32_t status)\n"
        "{\n"
        "    (void)status;\n"
        "}\n\n"
    )
    if report_decl not in text:
        anchor = "__attribute__((weak)) bool esphome_esp32_jpeg_skip_output_cache_msync(void)\n"
        if anchor not in text:
            raise RuntimeError(
                "ESP-IDF JPEG runtime hook anchor not found; patch needs review"
            )
        text = text.replace(anchor, report_decl + anchor, 1)
        changed = True

    if changed:
        target.write_text(text, encoding="utf-8")
        print("MIPI DSI patch: applied ESP-IDF JPEG decode DMA2D burst override")
    else:
        print(
            "MIPI DSI patch: ESP-IDF JPEG decode DMA2D burst override already present"
        )


def _patch_jpeg_encode_dma2d_burst(framework_dir: Path) -> None:
    target = framework_dir / "components" / "esp_driver_jpeg" / "jpeg_encode.c"
    if not target.exists():
        print(
            "MIPI DSI patch: ESP-IDF JPEG encode driver not found; skipping DMA2D burst patch"
        )
        return

    text = target.read_text(encoding="utf-8")
    helper = """
__attribute__((weak)) int esphome_esp32_jpeg_encoder_dma2d_burst_length(void)
{
#ifdef CONFIG_ESPHOME_JPEG_ENCODER_DMA2D_BURST_LENGTH
    return CONFIG_ESPHOME_JPEG_ENCODER_DMA2D_BURST_LENGTH;
#else
    return 128;
#endif
}

__attribute__((weak)) bool esphome_esp32_jpeg_encoder_dma2d_desc_burst_enabled(void)
{
#ifdef CONFIG_ESPHOME_JPEG_ENCODER_DMA2D_DESC_BURST_DISABLE
    return false;
#else
    return true;
#endif
}

__attribute__((weak)) int esphome_esp32_jpeg_encoder_dma2d_band_height(void)
{
#ifdef CONFIG_ESPHOME_JPEG_ENCODER_DMA2D_BAND_HEIGHT
    return CONFIG_ESPHOME_JPEG_ENCODER_DMA2D_BAND_HEIGHT;
#else
    return 0;
#endif
}

static uint32_t jpeg_enc_select_dma2d_burst_length(void)
{
    switch (esphome_esp32_jpeg_encoder_dma2d_burst_length()) {
    case 1:
    case 8:
        return 8;
    case 16:
        return 16;
    case 32:
        return 32;
    case 64:
        return 64;
    case 128:
    default:
        return 128;
    }
}

static bool jpeg_enc_select_dma2d_desc_burst_en(void)
{
    return esphome_esp32_jpeg_encoder_dma2d_desc_burst_enabled();
}
"""
    changed = False
    declaration_anchor = (
        "static void s_encoder_error_log_print(uint32_t status);\n"
    )
    declaration_patch = (
        declaration_anchor
        + "\n#define ESPHOME_JPEG_ENCODER_MAX_TX_DESCRIPTORS 64\n"
        + "int esphome_esp32_jpeg_encoder_dma2d_band_height(void);\n"
    )
    if declaration_patch not in text:
        if declaration_anchor not in text:
            raise RuntimeError(
                "ESP-IDF JPEG encode declaration anchor not found; patch needs review"
            )
        text = text.replace(declaration_anchor, declaration_patch, 1)
        changed = True

    allocation_candidates = (
        (
            "    encoder_engine->txlink = (dma2d_descriptor_t*)heap_caps_aligned_calloc(alignment, 1, sizeof(dma2d_descriptor_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | JPEG_MEM_ALLOC_CAPS);\n"
        ),
        (
            "    encoder_engine->txlink = (dma2d_descriptor_t*)heap_caps_aligned_calloc(alignment, 1, dma_desc_mem_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | JPEG_MEM_ALLOC_CAPS);\n"
        ),
    )
    allocation_new = (
        "    encoder_engine->txlink = (dma2d_descriptor_t*)heap_caps_aligned_calloc(\n"
        "        alignment, 1, dma_desc_mem_size * ESPHOME_JPEG_ENCODER_MAX_TX_DESCRIPTORS,\n"
        "        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL | JPEG_MEM_ALLOC_CAPS);\n"
    )
    if allocation_new not in text:
        allocation_old = next(
            (candidate for candidate in allocation_candidates if candidate in text),
            None,
        )
        if allocation_old is None:
            raise RuntimeError(
                "ESP-IDF JPEG encode TX descriptor allocation not found; patch needs review"
            )
        text = text.replace(allocation_old, allocation_new, 1)
        changed = True

    descriptor_candidates = tuple(
        (
            "    // 2D direction\n"
            f"    memset(encoder_engine->txlink, 0, {memset_size});\n"
            "    s_cfg_desc(encoder_engine, encoder_engine->txlink, JPEG_DMA2D_2D_ENABLE, DMA2D_DESCRIPTOR_BLOCK_RW_MODE_MULTIPLE, dma_vb, dma_hb, JPEG_DMA2D_EOF_NOT_LAST, "
            f"dma2d_desc_pixel_format_to_pbyte_value({picture_format}), "
            "DMA2D_DESCRIPTOR_BUFFER_OWNER_DMA, encoder_engine->header_info->origin_v, encoder_engine->header_info->origin_h, raw_buffer, NULL);\n"
        )
        for memset_size in (
            "sizeof(dma2d_descriptor_t)",
            "encoder_engine->dma_desc_size",
        )
        for picture_format in ("picture_format", "encoder_engine->picture_format")
    )
    descriptor_chain_marker = (
        "// Split large PSRAM sources into a descriptor chain."
    )
    descriptor_old = next(
        (candidate for candidate in descriptor_candidates if candidate in text), None
    )
    if descriptor_old is not None:
        picture_format = (
            "encoder_engine->picture_format"
            if "encoder_engine->picture_format" in descriptor_old
            else "picture_format"
        )
    else:
        picture_format = "picture_format"
    descriptor_new = (
        "    // Split large PSRAM sources into a descriptor chain. With descriptor\n"
        "    // bursting disabled this creates arbitration points for real-time DSI\n"
        "    // scanout while the JPEG codec continues to process one image.\n"
        "    uint32_t band_height = (uint32_t)esphome_esp32_jpeg_encoder_dma2d_band_height();\n"
        "    uint32_t tx_desc_count = 1;\n"
        "    if (band_height > 0 && band_height < encoder_engine->header_info->origin_v) {\n"
        "        uint32_t min_band_height = (encoder_engine->header_info->origin_v +\n"
        "                                    ESPHOME_JPEG_ENCODER_MAX_TX_DESCRIPTORS - 1) /\n"
        "                                   ESPHOME_JPEG_ENCODER_MAX_TX_DESCRIPTORS;\n"
        "        if (band_height < min_band_height) {\n"
        "            band_height = min_band_height;\n"
        "        }\n"
        "        band_height = ((band_height + dma_vb - 1) / dma_vb) * dma_vb;\n"
        "        tx_desc_count = (encoder_engine->header_info->origin_v + band_height - 1) / band_height;\n"
        "    } else {\n"
        "        band_height = encoder_engine->header_info->origin_v;\n"
        "    }\n"
        "    memset(encoder_engine->txlink, 0, encoder_engine->dma_desc_size * tx_desc_count);\n"
        "    const size_t raw_stride = encoder_engine->header_info->origin_h * encoder_engine->bytes_per_pixel;\n"
        "    for (uint32_t index = 0; index < tx_desc_count; index++) {\n"
        "        const uint32_t y = index * band_height;\n"
        "        const uint32_t rows = MIN(band_height, encoder_engine->header_info->origin_v - y);\n"
        "        dma2d_descriptor_t *desc = (dma2d_descriptor_t *)\n"
        "            ((uint8_t *)encoder_engine->txlink + index * encoder_engine->dma_desc_size);\n"
        "        dma2d_descriptor_t *next_desc = index + 1 < tx_desc_count\n"
        "            ? (dma2d_descriptor_t *)((uint8_t *)encoder_engine->txlink +\n"
        "                                     (index + 1) * encoder_engine->dma_desc_size)\n"
        "            : NULL;\n"
        "        s_cfg_desc(encoder_engine, desc, JPEG_DMA2D_2D_ENABLE,\n"
        "                   DMA2D_DESCRIPTOR_BLOCK_RW_MODE_MULTIPLE, dma_vb, dma_hb,\n"
        "                   JPEG_DMA2D_EOF_NOT_LAST, "
        f"dma2d_desc_pixel_format_to_pbyte_value({picture_format}),\n"
        "                   DMA2D_DESCRIPTOR_BUFFER_OWNER_DMA, rows, encoder_engine->header_info->origin_h,\n"
        "                   raw_buffer + y * raw_stride, next_desc);\n"
        "    }\n"
    )
    if descriptor_chain_marker not in text:
        if descriptor_old is None:
            raise RuntimeError(
                "ESP-IDF JPEG encode TX descriptor setup not found; patch needs review"
            )
        text = text.replace(descriptor_old, descriptor_new, 1)
        changed = True

    anchor = (
        "static void jpeg_enc_config_dma_trans_ability(jpeg_encoder_handle_t encoder_engine)\n"
    )
    anchor_pos = text.find(anchor)
    if anchor_pos < 0:
        raise RuntimeError(
            "ESP-IDF JPEG encode DMA2D transfer ability function not found; patch needs review"
        )

    helper_start = text.find(
        "__attribute__((weak)) int esphome_esp32_jpeg_encoder_dma2d_burst_length"
    )
    if helper_start < 0:
        helper_start = text.find(
            "static dma2d_data_burst_length_t jpeg_enc_select_dma2d_burst_length"
        )
    if 0 <= helper_start < anchor_pos:
        current_helper = text[helper_start:anchor_pos].strip()
        if current_helper != helper.strip():
            text = text[:helper_start] + f"{helper}\n" + text[anchor_pos:]
            changed = True
    else:
        text = text[:anchor_pos] + f"{helper}\n" + text[anchor_pos:]
        changed = True

    new = ".data_burst_length = jpeg_enc_select_dma2d_burst_length(),"
    if new not in text:
        old_candidates = (
            ".data_burst_length = DMA2D_DATA_BURST_LENGTH_128,",
            ".data_burst_length = 128,",
        )
        old = next((candidate for candidate in old_candidates if text.count(candidate) == 2), None)
        if old is None:
            raise RuntimeError(
                "ESP-IDF JPEG encode DMA2D burst lines not found; patch needs review"
            )
        text = text.replace(old, new)
        changed = True

    old = ".desc_burst_en = true,"
    new = ".desc_burst_en = jpeg_enc_select_dma2d_desc_burst_en(),"
    if new not in text:
        count = text.count(old)
        if count != 2:
            raise RuntimeError(
                "ESP-IDF JPEG encode DMA2D desc burst lines not found; patch needs review"
            )
        text = text.replace(old, new)
        changed = True

    if changed:
        target.write_text(text, encoding="utf-8")
        print("MIPI DSI patch: applied ESP-IDF JPEG encode DMA2D burst override")
    else:
        print(
            "MIPI DSI patch: ESP-IDF JPEG encode DMA2D burst override already present"
        )


def _patch_dma2d_yuv2rgb_full_range(framework_dir: Path) -> None:
    candidates = (
        framework_dir
        / "components"
        / "esp_hal_dma"
        / "include"
        / "hal"
        / "dma2d_types.h",
        framework_dir / "components" / "hal" / "include" / "hal" / "dma2d_types.h",
    )
    target = next((candidate for candidate in candidates if candidate.exists()), None)
    if target is None:
        print(
            "MIPI DSI patch: ESP-IDF DMA2D types header not found; skipping full-range YUV->RGB patch"
        )
        return

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
            raise RuntimeError(
                "ESP-IDF DMA2D BT.601 YUV->RGB matrix not found; patch needs review"
            )
        text = text.replace(limited_bt601, full_bt601, 1)
        changed = True
    if full_bt709 not in text:
        if limited_bt709 not in text:
            raise RuntimeError(
                "ESP-IDF DMA2D BT.709 YUV->RGB matrix not found; patch needs review"
            )
        text = text.replace(limited_bt709, full_bt709, 1)
        changed = True

    if changed:
        target.write_text(text, encoding="utf-8")
        print("MIPI DSI patch: applied ESP-IDF DMA2D full-range YUV->RGB matrices")
    else:
        print(
            "MIPI DSI patch: ESP-IDF DMA2D full-range YUV->RGB matrices already present"
        )


def _patch_ppa_srm_dma_stall(framework_dir: Path) -> None:
    """Backport Espressif's conditional DIG-734 macro-block workaround."""
    ll_target = (
        framework_dir
        / "components"
        / "hal"
        / "esp32p4"
        / "include"
        / "hal"
        / "ppa_ll.h"
    )
    srm_target = (
        framework_dir / "components" / "esp_driver_ppa" / "src" / "ppa_srm.c"
    )
    if not ll_target.exists() or not srm_target.exists():
        print("MIPI DSI patch: ESP-IDF PPA SRM driver not found; skipping DIG-734 patch")
        return

    srm_text = srm_target.read_text(encoding="utf-8")

    ll_text = ll_target.read_text(encoding="utf-8")
    ll_changed = False
    if "ppa_ll_srm_bypass_mb_order" not in ll_text:
        helper = """
/**
 * @brief Whether to bypass the macro block order function in PPA SRM
 *
 * @param dev Peripheral instance address
 * @param enable True to bypass; False to not bypass
 */
static inline void ppa_ll_srm_bypass_mb_order(ppa_dev_t *dev, bool enable)
{
    dev->sr_byte_order.sr_macro_bk_ro_bypass = enable;
}

"""
        anchor = "//////////////////////////////////// Blending ////////////////////////////////////////"
        if anchor not in ll_text:
            raise RuntimeError("ESP-IDF PPA LL blending marker not found; DIG-734 patch needs review")
        ll_text = ll_text.replace(anchor, f"{helper}{anchor}", 1)
        ll_changed = True

    workaround = """    // Hardware bug workaround (DIG-734)
    uint32_t w_out = srm_trans_desc->in.block_w * srm_trans_desc->scale_x_int +
                     srm_trans_desc->in.block_w * srm_trans_desc->scale_x_frag /
                         PPA_LL_SRM_SCALING_FRAG_MAX;
    uint32_t w_divisor =
        (ppa_out_color_mode == PPA_SRM_COLOR_MODE_ARGB8888 ||
         ppa_out_color_mode == PPA_SRM_COLOR_MODE_RGB888)
            ? 32
            : 64;
    uint32_t w_left = w_out % w_divisor;
    w_left = (w_left == 0) ? w_divisor : w_left;
    uint32_t h_mb =
        (ppa_ll_srm_get_mb_size(platform->hal.dev) == PPA_LL_SRM_MB_SIZE_16_16) ? 16 : 32;
    uint32_t h_in_left = srm_trans_desc->in.block_h % h_mb;
    h_in_left = (h_in_left == 0) ? h_mb : h_in_left;
    uint32_t h_left = h_in_left * srm_trans_desc->scale_y_int +
                      h_in_left * srm_trans_desc->scale_y_frag /
                          PPA_LL_SRM_SCALING_FRAG_MAX;
    const uint32_t dma2d_fifo_depth_bits = 12 * 128;
    color_space_pixel_format_t out_pixel_format = {
        .color_type_id = ppa_out_color_mode,
    };
    uint32_t out_pixel_depth = color_hal_pixel_format_get_bit_depth(out_pixel_format);
    bool bypass_mb_order = false;
    if (((w_out > w_divisor) || (srm_trans_desc->in.block_h > h_mb)) &&
        ((w_left * h_left * out_pixel_depth) < dma2d_fifo_depth_bits)) {
        bypass_mb_order = true;
    }
    ppa_ll_srm_bypass_mb_order(platform->hal.dev, bypass_mb_order);

"""

    forced_workaround = (
        "    // Temporary Espressif workaround for PPA macro-block ordering.\n"
        "    ppa_ll_srm_bypass_mb_order(platform->hal.dev, true);\n\n"
    )
    if forced_workaround in srm_text:
        # Older project builds may already have been mutated by a previous
        # revision of this script. Restore the upstream conditional fix rather
        # than leaving the framework package permanently patched incorrectly.
        srm_text = srm_text.replace(forced_workaround, workaround, 1)
    elif "Hardware bug workaround (DIG-734)" not in srm_text:
        anchor = "    ppa_ll_srm_start(platform->hal.dev);"
        if anchor not in srm_text:
            raise RuntimeError(
                "ESP-IDF PPA SRM start call not found; macro-block patch needs review"
            )
        srm_text = srm_text.replace(anchor, f"{workaround}{anchor}", 1)

    if ll_changed:
        ll_target.write_text(ll_text, encoding="utf-8")
    srm_target.write_text(srm_text, encoding="utf-8")
    print("MIPI DSI patch: ESP-IDF PPA DIG-734 workaround is conditional")


def main() -> None:
    if len(sys.argv) > 1:
        framework_dir = sys.argv[1]
    elif env is not None:
        framework_dir = env.PioPlatform().get_package_dir("framework-espidf")
    else:
        raise RuntimeError("ESP-IDF framework path is required outside PlatformIO")
    if not framework_dir:
        raise RuntimeError("framework-espidf package directory not found")

    framework_path = Path(framework_dir)
    idf_version = _read_idf_version(framework_path)
    print(
        f"MIPI DSI patch: detected ESP-IDF {idf_version[0]}.{idf_version[1]}.{idf_version[2]}"
    )
    if idf_version[0] == 5:
        _patch_idf5(framework_path)
    elif idf_version[0] >= 6:
        _patch_idf6_or_newer(framework_path)
    else:
        raise RuntimeError(
            f"Unsupported ESP-IDF major version {idf_version[0]}; patch needs review"
        )

    _patch_continuous_hs_clock(framework_path)
    _patch_jpeg_decode_csc(framework_path)
    _patch_jpeg_decode_dma2d_burst(framework_path)
    _patch_jpeg_encode_dma2d_burst(framework_path)
    _patch_dma2d_yuv2rgb_full_range(framework_path)
    _patch_ppa_srm_dma_stall(framework_path)


if env is not None or len(sys.argv) > 1:
    main()
