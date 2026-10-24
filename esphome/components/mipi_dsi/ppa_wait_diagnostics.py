"""Opt-in, read-only completion diagnostics for the reviewed native P4 SDK.

The existing MIPI owner applies the regular SDK backports first. This patch then
checks every complete input before writing any file. All added SDK instructions
are compiled out when the Kconfig option is disabled, including a narrow,
explicitly requested macroblock-bypass experiment. It never retires a PPA
transaction, gives its semaphore, changes descriptors, or recovers hardware.
"""

import hashlib
from pathlib import Path
import re

GUARD = "CONFIG_ESPHOME_PPA_WAIT_DIAGNOSTICS"

DMA2D_HELPER = r"""
#if CONFIG_ESPHOME_PPA_WAIT_DIAGNOSTICS
// ESPHome numeric PPA wait journal. No ISR logging or image/credential data.
typedef struct {
    uint32_t sequence, tick, channel, status, raw, enabled;
    uint32_t transaction, callback_user, tx_mask, state, ppa_order, claimed;
} esphome_dma2d_wait_record_t;
static DRAM_ATTR portMUX_TYPE s_esphome_dma2d_wait_lock = portMUX_INITIALIZER_UNLOCKED;
static DRAM_ATTR esphome_dma2d_wait_record_t s_esphome_dma2d_wait_records[4];
static DRAM_ATTR esphome_dma2d_wait_record_t s_esphome_dma2d_wait_first_error;
static DRAM_ATTR uint32_t s_esphome_dma2d_wait_sequence;
extern uint32_t esphome_ppa_wait_diagnostics_srm_order(void) __attribute__((weak));

static uint32_t IRAM_ATTR esphome_dma2d_wait_note(dma2d_group_t *group, dma2d_rx_channel_t *rx,
                                                uint32_t channel, uint32_t status,
                                                dma2d_trans_t *transaction)
{
    const uint32_t terminal = DMA2D_LL_EVENT_RX_SUC_EOF | DMA2D_LL_EVENT_RX_ERR_EOF |
                              DMA2D_LL_EVENT_RX_DESC_ERROR | DMA2D_LL_EVENT_RX_DESC_EMPTY;
    if (!(status & terminal)) {
        return 0;
    }
    esphome_dma2d_wait_record_t record = {
        .sequence = 0, .tick = xTaskGetTickCountFromISR(), .channel = channel, .status = status,
        .raw = group->hal.dev->in_channel[channel].in_int_raw.val,
        .enabled = group->hal.dev->in_channel[channel].in_int_ena.val,
        .transaction = (uint32_t)(uintptr_t)transaction,
        .callback_user = (uint32_t)(uintptr_t)rx->user_data,
        .tx_mask = rx->bundled_tx_channel_mask,
        .state = group->hal.dev->in_channel[channel].in_state.val,
        .ppa_order = esphome_ppa_wait_diagnostics_srm_order ? esphome_ppa_wait_diagnostics_srm_order() : 0,
        .claimed = 2, // pending decision; 0 lost, 1 claimed
    };
    portENTER_CRITICAL_ISR(&s_esphome_dma2d_wait_lock);
    if (++s_esphome_dma2d_wait_sequence == 0) {
        ++s_esphome_dma2d_wait_sequence;
    }
    record.sequence = s_esphome_dma2d_wait_sequence;
    s_esphome_dma2d_wait_records[(record.sequence - 1) % 4] = record;
    if ((status & (terminal & ~DMA2D_LL_EVENT_RX_SUC_EOF)) &&
        s_esphome_dma2d_wait_first_error.sequence == 0) {
        s_esphome_dma2d_wait_first_error = record;
    }
    portEXIT_CRITICAL_ISR(&s_esphome_dma2d_wait_lock);
    return record.sequence;
}

static void IRAM_ATTR esphome_dma2d_wait_claim(uint32_t sequence, bool claimed)
{
    if (sequence == 0) {
        return;
    }
    portENTER_CRITICAL_ISR(&s_esphome_dma2d_wait_lock);
    esphome_dma2d_wait_record_t *record = &s_esphome_dma2d_wait_records[(sequence - 1) % 4];
    if (record->sequence == sequence) {
        record->claimed = claimed;
    }
    if (s_esphome_dma2d_wait_first_error.sequence == sequence) {
        s_esphome_dma2d_wait_first_error.claimed = claimed;
    }
    portEXIT_CRITICAL_ISR(&s_esphome_dma2d_wait_lock);
}

static void esphome_dma2d_wait_print(const char *label, const esphome_dma2d_wait_record_t *r)
{
    ESP_LOGE(TAG, "wait %s seq=%"PRIu32" tick=%"PRIu32" rx=%"PRIu32" st=%08"PRIx32
             " raw=%08"PRIx32" ena=%08"PRIx32" trans=%08"PRIx32" user=%08"PRIx32
             " tx=%"PRIu32" state=%08"PRIx32" order=%08"PRIx32" claim=%"PRIu32,
             label, r->sequence, r->tick, r->channel, r->status, r->raw, r->enabled,
             r->transaction, r->callback_user, r->tx_mask, r->state, r->ppa_order, r->claimed);
}

void esphome_dma2d_wait_diagnostics(dma2d_pool_handle_t pool, dma2d_trans_t *expected)
{
    // Numeric snapshots only. Never log while holding a driver/diagnostic lock.
    dma2d_group_t *group = pool;
    esphome_dma2d_wait_record_t records[5];
    portENTER_CRITICAL(&s_esphome_dma2d_wait_lock);
    for (unsigned i = 0; i < 4; ++i) {
        records[i] = s_esphome_dma2d_wait_records[i];
    }
    records[4] = s_esphome_dma2d_wait_first_error;
    portEXIT_CRITICAL(&s_esphome_dma2d_wait_lock);
    for (unsigned i = 0; i < 5; ++i) {
        esphome_dma2d_wait_print(i == 4 ? "first_error" : "recent", &records[i]);
    }
    unsigned pending = 0, found = 0, more = 0;
    uint32_t tx_free, rx_free, head;
    portENTER_CRITICAL_SAFE(&group->spinlock);
    tx_free = group->tx_channel_free_mask;
    rx_free = group->rx_channel_free_mask;
    head = (uint32_t)(uintptr_t)TAILQ_FIRST(&group->pending_trans_tailq);
    dma2d_trans_t *item;
    TAILQ_FOREACH(item, &group->pending_trans_tailq, entry) {
        if (pending == 16) {
            more = 1;
            break;
        }
        found |= item == expected;
        ++pending;
    }
    portEXIT_CRITICAL_SAFE(&group->spinlock);
    ESP_LOGE(TAG, "wait pool expected=%08"PRIx32" started=%u tx_free=%"PRIu32" rx_free=%"PRIu32
             " pending=%u more=%u found=%u head=%08"PRIx32,
             (uint32_t)(uintptr_t)expected, expected ? atomic_load(&expected->started) : 0,
             tx_free, rx_free, pending, more, found, head);
    for (unsigned i = 0; i < DMA2D_LL_RX_CHANNELS_PER_GROUP; ++i) {
        uint32_t owner, bundled, user;
        portENTER_CRITICAL_SAFE(&group->spinlock);
        owner = (uint32_t)(uintptr_t)group->rx_chans[i]->base.status.transaction;
        bundled = group->rx_chans[i]->bundled_tx_channel_mask;
        user = (uint32_t)(uintptr_t)group->rx_chans[i]->user_data;
        portEXIT_CRITICAL_SAFE(&group->spinlock);
        ESP_LOGE(TAG, "wait rx%u owner=%08"PRIx32" user=%08"PRIx32" tx=%"PRIu32
                 " raw=%08"PRIx32" st=%08"PRIx32" ena=%08"PRIx32" state=%08"PRIx32" link=%08"PRIx32,
                 i, owner, user, bundled, group->hal.dev->in_channel[i].in_int_raw.val,
                 group->hal.dev->in_channel[i].in_int_st.val, group->hal.dev->in_channel[i].in_int_ena.val,
                 group->hal.dev->in_channel[i].in_state.val, group->hal.dev->in_channel[i].in_link_conf.val);
    }
    for (unsigned i = 0; i < DMA2D_LL_TX_CHANNELS_PER_GROUP; ++i) {
        uint32_t owner;
        portENTER_CRITICAL_SAFE(&group->spinlock);
        owner = (uint32_t)(uintptr_t)group->tx_chans[i]->base.status.transaction;
        portEXIT_CRITICAL_SAFE(&group->spinlock);
        ESP_LOGE(TAG, "wait tx%u owner=%08"PRIx32" raw=%08"PRIx32" st=%08"PRIx32
                 " ena=%08"PRIx32" state=%08"PRIx32" link=%08"PRIx32,
                 i, owner, group->hal.dev->out_channel[i].out_int_raw.val,
                 group->hal.dev->out_channel[i].out_int_st.val, group->hal.dev->out_channel[i].out_int_ena.val,
                 group->hal.dev->out_channel[i].out_state.val, group->hal.dev->out_channel[i].out_link_conf.val);
    }
}
#endif
"""

PPA_HELPER = r"""
#if CONFIG_ESPHOME_PPA_WAIT_DIAGNOSTICS
// The override is volatile test state, never a product preference or recovery.
static DRAM_ATTR portMUX_TYPE s_esphome_ppa_wait_lock = portMUX_INITIALIZER_UNLOCKED;
static DRAM_ATTR int s_esphome_ppa_wait_bypass_request = -1;
typedef struct {
    uint32_t trans, tick, requested, computed, effective, eligible, tail_bits;
    uint32_t in_w, in_h, block_w, block_h, in_x, in_y, out_x, out_y;
} esphome_ppa_wait_srm_t;
static DRAM_ATTR esphome_ppa_wait_srm_t s_esphome_ppa_wait_srm;
static DRAM_ATTR esphome_ppa_wait_srm_t s_esphome_ppa_wait_eligible;
static DRAM_ATTR uint32_t s_esphome_ppa_wait_picked, s_esphome_ppa_wait_eligible_count, s_esphome_ppa_wait_forced;
extern void esphome_dma2d_wait_diagnostics(dma2d_pool_handle_t pool, dma2d_trans_t *expected);

bool esphome_ppa_wait_diagnostics_set_srm_bypass(int value)
{
    if (value != -1 && value != 1) {
        return false;
    }
    portENTER_CRITICAL(&s_esphome_ppa_wait_lock);
    s_esphome_ppa_wait_bypass_request = value;
    portEXIT_CRITICAL(&s_esphome_ppa_wait_lock);
    return true;
}

uint32_t IRAM_ATTR esphome_ppa_wait_diagnostics_srm_order(void)
{
    return s_platform.srm ? s_platform.hal.dev->sr_byte_order.val : 0;
}

bool IRAM_ATTR esphome_ppa_wait_diagnostics_srm_bypass(ppa_trans_t *trans, const ppa_srm_oper_t *op,
                                                    bool computed, uint32_t tail_bits)
{
    const bool eligible = op->in.srm_cm == PPA_SRM_COLOR_MODE_RGB888 &&
        op->out.srm_cm == PPA_SRM_COLOR_MODE_RGB888 && op->rotation_angle == PPA_SRM_ROTATION_ANGLE_0 &&
        op->scale_x == 1.0f && op->scale_y == 1.0f &&
        op->scale_x_int == 1 && op->scale_x_frag == 0 && op->scale_y_int == 1 && op->scale_y_frag == 0 &&
        !op->mirror_x && !op->mirror_y && tail_bits == 12U * 128U;
    portENTER_CRITICAL_SAFE(&s_esphome_ppa_wait_lock);
    const int requested = s_esphome_ppa_wait_bypass_request;
    const bool effective = computed || (requested == 1 && eligible);
    s_esphome_ppa_wait_srm = (esphome_ppa_wait_srm_t) {
        .trans = (uint32_t)(uintptr_t)trans, .tick = xTaskGetTickCountFromISR(),
        .requested = (uint32_t)requested, .computed = computed, .effective = effective,
        .eligible = eligible, .tail_bits = tail_bits, .in_w = op->in.pic_w, .in_h = op->in.pic_h,
        .block_w = op->in.block_w, .block_h = op->in.block_h, .in_x = op->in.block_offset_x,
        .in_y = op->in.block_offset_y, .out_x = op->out.block_offset_x, .out_y = op->out.block_offset_y,
    };
    ++s_esphome_ppa_wait_picked;
    if (eligible) {
        s_esphome_ppa_wait_eligible = s_esphome_ppa_wait_srm;
        ++s_esphome_ppa_wait_eligible_count;
    }
    if (effective != computed) {
        ++s_esphome_ppa_wait_forced;
    }
    portEXIT_CRITICAL_SAFE(&s_esphome_ppa_wait_lock);
    return effective;
}

static void esphome_ppa_wait_print_srm(const char *label, const esphome_ppa_wait_srm_t *srm)
{
    ESP_LOGE(TAG, "wait srm %s trans=%08"PRIx32" tick=%"PRIu32" request=%"PRId32" computed=%"PRIu32
             " effective=%"PRIu32" eligible=%"PRIu32" tail_bits=%"PRIu32
             " input=%"PRIu32"x%"PRIu32" block=%"PRIu32"x%"PRIu32" in_xy=%"PRIu32",%"PRIu32
             " out_xy=%"PRIu32",%"PRIu32,
             label, srm->trans, srm->tick, (int32_t)srm->requested, srm->computed, srm->effective, srm->eligible,
             srm->tail_bits, srm->in_w, srm->in_h, srm->block_w, srm->block_h,
             srm->in_x, srm->in_y, srm->out_x, srm->out_y);
}

void esphome_ppa_wait_diagnostics_dump_srm(void)
{
    esphome_ppa_wait_srm_t last, eligible;
    uint32_t picked, eligible_count, forced;
    int requested;
    portENTER_CRITICAL(&s_esphome_ppa_wait_lock);
    last = s_esphome_ppa_wait_srm;
    eligible = s_esphome_ppa_wait_eligible;
    picked = s_esphome_ppa_wait_picked;
    eligible_count = s_esphome_ppa_wait_eligible_count;
    forced = s_esphome_ppa_wait_forced;
    requested = s_esphome_ppa_wait_bypass_request;
    portEXIT_CRITICAL(&s_esphome_ppa_wait_lock);
    ESP_LOGE(TAG, "wait srm counters request=%d picked=%"PRIu32" eligible=%"PRIu32" forced=%"PRIu32,
             requested, picked, eligible_count, forced);
    esphome_ppa_wait_print_srm("last", &last);
    esphome_ppa_wait_print_srm("last_eligible", &eligible);
}

static void esphome_ppa_wait_diagnostics(ppa_client_t *client, ppa_engine_t *engine, ppa_trans_t *trans)
{
    uint32_t head, busy, count;
    portENTER_CRITICAL(&engine->spinlock);
    head = (uint32_t)(uintptr_t)STAILQ_FIRST(&engine->trans_stailq);
    busy = engine->busy;
    portEXIT_CRITICAL(&engine->spinlock);
    portENTER_CRITICAL(&client->spinlock);
    count = client->trans_cnt;
    portEXIT_CRITICAL(&client->spinlock);
    ESP_LOGE(TAG, "wait >1s client=%08"PRIx32" engine=%08"PRIx32" trans=%08"PRIx32
             " dma=%08"PRIx32" op=%u type=%u busy=%"PRIu32" head=%08"PRIx32" count=%"PRIu32" blocking=%u",
             (uint32_t)(uintptr_t)client, (uint32_t)(uintptr_t)engine, (uint32_t)(uintptr_t)trans,
             (uint32_t)(uintptr_t)trans->dma_trans_placeholder, client->oper_type, engine->type,
             busy, head, count, trans->blocking);
    esphome_ppa_wait_diagnostics_dump_srm();
    ppa_dev_t *dev = s_platform.hal.dev;
    ESP_LOGE(TAG, "wait ppa raw=%08"PRIx32" st=%08"PRIx32" ena=%08"PRIx32" order=%08"PRIx32
             " scale=%08"PRIx32" status=%08"PRIx32" param_err=%08"PRIx32,
             dev->int_raw.val, dev->int_st.val, dev->int_ena.val, dev->sr_byte_order.val,
             dev->sr_scal_rotate.val, dev->sr_status.val, dev->sr_param_err_st.val);
    esphome_dma2d_wait_diagnostics(s_platform.dma2d_pool_handle, trans->dma_trans_placeholder);
}
#endif
"""

WAIT = """#if CONFIG_ESPHOME_PPA_WAIT_DIAGNOSTICS
        if (xSemaphoreTake(trans_elm->sem, pdMS_TO_TICKS(1000)) != pdTRUE) {
            esphome_ppa_wait_diagnostics(ppa_client, ppa_engine_base, trans_elm);
            // Observe only: preserve the original ownership and indefinite wait.
            xSemaphoreTake(trans_elm->sem, portMAX_DELAY);
        }
#else
        xSemaphoreTake(trans_elm->sem, portMAX_DELAY); // Given in the ISR
#endif
"""

INPUTS = {
    "components/esp_hw_support/dma/dma2d.c": "588e5875af7e84d118ef6d547e3ef8865047f63761cfd9cfc8d71bec238e3a29",
    "components/esp_driver_ppa/src/ppa_core.c": "4f09316e30675f9c9ec1ee6dfcc0ebedfd5a512867b5068af8e0abb26058e02e",
    # The reviewed native build retains the SDK's original cache synchronization.
    "components/esp_driver_ppa/src/ppa_srm.c": "acda4d7e731709a2b8273f0f2c9849acfc2688e669f262466723628b01de5c44",
}

# Same reviewed SRM owner after the separately reviewed native cache-hook repair.
# The complete normalized file is admitted, not merely a marker or cache region.
REVIEWED_CACHE_SOURCE_SHA256 = {
    "components/esp_driver_ppa/src/ppa_srm.c": "0b797fa789098f86c32ce5597dce630f4255c8f618ae15399269ce6c895a0f39",
}


def _replace_once(text: str, old: str, new: str) -> str:
    if text.count(old) != 1:
        raise RuntimeError("PPA wait diagnostic source anchor changed")
    return text.replace(old, new, 1)


def _edits(relative: str) -> list[tuple[str, str]]:
    includes = (
        "#if CONFIG_ESPHOME_PPA_WAIT_DIAGNOSTICS\n"
        '#include <inttypes.h>\n#include "esp_attr.h"\n#endif\n'
    )
    if relative.endswith("dma2d.c"):
        return [
            (
                'static const char *TAG = "dma2d";\n',
                'static const char *TAG = "dma2d";\n' + DMA2D_HELPER,
            ),
            ('#include "dma2d_priv.h"\n', '#include "dma2d_priv.h"\n' + includes),
            (
                "    dma2d_ll_rx_clear_interrupt_status(group->hal.dev, channel_id, intr_status);\n",
                "#if CONFIG_ESPHOME_PPA_WAIT_DIAGNOSTICS\n"
                "    const uint32_t esphome_wait_sequence = esphome_dma2d_wait_note(group, rx_chan, channel_id, intr_status, edata.transaction);\n"
                "#endif\n"
                "    dma2d_ll_rx_clear_interrupt_status(group->hal.dev, channel_id, intr_status);\n",
            ),
            (
                "    if (transaction_claimed && (intr_status & DMA2D_LL_EVENT_RX_SUC_EOF)) {\n",
                "#if CONFIG_ESPHOME_PPA_WAIT_DIAGNOSTICS\n"
                "    esphome_dma2d_wait_claim(esphome_wait_sequence, transaction_claimed);\n"
                "#endif\n"
                "    if (transaction_claimed && (intr_status & DMA2D_LL_EVENT_RX_SUC_EOF)) {\n",
            ),
        ]
    if relative.endswith("ppa_core.c"):
        return [
            ('#include "sdkconfig.h"\n', '#include "sdkconfig.h"\n' + includes),
            (
                "static esp_err_t ppa_engine_acquire(const ppa_engine_config_t *config, ppa_engine_t **ret_engine);\n",
                PPA_HELPER
                + "\nstatic esp_err_t ppa_engine_acquire(const ppa_engine_config_t *config, ppa_engine_t **ret_engine);\n",
            ),
            (
                "        xSemaphoreTake(trans_elm->sem, portMAX_DELAY); // Given in the ISR\n",
                WAIT,
            ),
        ]
    return [
        (
            'static const char *TAG = "ppa_srm";\n',
            'static const char *TAG = "ppa_srm";\n'
            "#if CONFIG_ESPHOME_PPA_WAIT_DIAGNOSTICS\n"
            "extern bool esphome_ppa_wait_diagnostics_srm_bypass(ppa_trans_t *, const ppa_srm_oper_t *, bool, uint32_t);\n"
            "#endif\n",
        ),
        (
            "    ppa_ll_srm_bypass_mb_order(platform->hal.dev, bypass_mb_order);\n",
            "#if CONFIG_ESPHOME_PPA_WAIT_DIAGNOSTICS\n"
            "    bypass_mb_order = esphome_ppa_wait_diagnostics_srm_bypass(trans_on_picked_desc->trans_elm, srm_trans_desc, bypass_mb_order, w_left * h_left * out_pixel_depth);\n"
            "#endif\n"
            "    ppa_ll_srm_bypass_mb_order(platform->hal.dev, bypass_mb_order);\n",
        ),
    ]


def original_source(relative: str, current: str) -> str:
    """Validate/remove only our exact blocks for the regular backport's preflight.

    This is an in-memory view, never an SDK write. Native CMake invokes the
    framework patch repeatedly, including in a later diagnostics-disabled build.
    """
    original = current.replace("\r\n", "\n")
    for old, new in reversed(_edits(relative)):
        if new in original:
            original = _replace_once(original, new, old)
    allowed = {INPUTS[relative], REVIEWED_CACHE_SOURCE_SHA256.get(relative)}
    if hashlib.sha256(original.encode()).hexdigest() not in allowed:
        raise RuntimeError(f"PPA wait diagnostic SDK source changed: {relative}")
    return original


def patch_framework(framework_dir: Path) -> None:
    """Admit only exact reviewed before/after sources and prepare all writes first."""
    version = (framework_dir / "version.txt").read_text(encoding="utf-8").strip()
    if re.fullmatch(r"v?5\.5\.6", version) is None:
        raise RuntimeError("PPA wait diagnostics require reviewed ESP-IDF 5.5.6")
    prepared = []
    for relative in INPUTS:
        path = framework_dir / relative
        raw = path.read_bytes()
        newline = "\r\n" if b"\r\n" in raw else "\n"
        current = raw.decode("utf-8").replace("\r\n", "\n")
        original = original_source(relative, current)
        candidate = original
        for old, new in _edits(relative):
            candidate = _replace_once(candidate, old, new)
        if current not in (original, candidate):
            raise RuntimeError(f"Partial PPA wait diagnostic patch: {relative}")
        prepared.append((path, current, candidate, newline))
    for path, current, candidate, newline in prepared:
        if current != candidate:
            path.write_bytes(candidate.replace("\n", newline).encode("utf-8"))
