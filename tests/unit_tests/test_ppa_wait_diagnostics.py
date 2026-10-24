"""Actual SDK diagnostic snippets: bounded capture, unchanged waits, narrow opt-in."""

import ast
import hashlib
import importlib.util
import os
from pathlib import Path
import re
import shlex
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]
OWNER = ROOT / "esphome/components/mipi_dsi"
FIXTURES = Path(__file__).parent / "fixtures/mipi_dsi_sdk"


def _load():
    spec = importlib.util.spec_from_file_location(
        "ppa_wait_patch", OWNER / "ppa_wait_diagnostics.py"
    )
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _sdk(tmp_path):
    module = _load()
    (tmp_path / "version.txt").write_text("v5.5.6", encoding="utf-8")
    for relative in module.INPUTS:
        path = tmp_path / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes((FIXTURES / (path.stem + "_wait_before_556.c")).read_bytes())
    return module


def test_exact_sdk_patch_is_idempotent_and_reversible(tmp_path):
    module = _sdk(tmp_path)
    before = {name: (tmp_path / name).read_bytes() for name in module.INPUTS}
    module.patch_framework(tmp_path)
    after = {name: (tmp_path / name).read_bytes() for name in module.INPUTS}
    module.patch_framework(tmp_path)
    assert after == {name: (tmp_path / name).read_bytes() for name in module.INPUTS}
    for name, original in before.items():
        text = after[name].decode().replace("\r\n", "\n")
        for old, new in reversed(module._edits(name)):
            assert text.count(new) == 1
            # Every injected instruction/data declaration is behind the option.
            addition = new.replace(old, "", 1) if old in new else new
            assert module.GUARD in addition
            text = text.replace(new, old, 1)
        normalized = original.decode().replace("\r\n", "\n").encode()
        assert text.encode() == normalized
        assert hashlib.sha256(normalized).hexdigest() == module.INPUTS[name]


@pytest.mark.parametrize("which", range(3))
def test_unknown_source_fails_before_any_write(tmp_path, which):
    module = _sdk(tmp_path)
    names = list(module.INPUTS)
    path = tmp_path / names[which]
    path.write_bytes(path.read_bytes() + b"\n/* Unreviewed owner change. */\n")
    before = {name: (tmp_path / name).read_bytes() for name in names}
    with pytest.raises(RuntimeError, match="SDK source changed"):
        module.patch_framework(tmp_path)
    assert before == {name: (tmp_path / name).read_bytes() for name in names}


def test_unknown_version_does_not_write(tmp_path):
    module = _sdk(tmp_path)
    (tmp_path / "version.txt").write_text("v5.5.7", encoding="utf-8")
    before = {name: (tmp_path / name).read_bytes() for name in module.INPUTS}
    with pytest.raises(RuntimeError, match="reviewed ESP-IDF 5.5.6"):
        module.patch_framework(tmp_path)
    assert before == {name: (tmp_path / name).read_bytes() for name in module.INPUTS}


def _regular_backport_owners(tmp_path):
    owner_path = OWNER / "patch_espidf_dsi_dma2d.py"
    owner_spec = importlib.util.spec_from_file_location("mipi_patch", owner_path)
    owner = importlib.util.module_from_spec(owner_spec)
    owner_spec.loader.exec_module(owner)
    function = next(
        node
        for node in ast.walk(ast.parse(owner_path.read_text()))
        if isinstance(node, ast.FunctionDef)
        and node.name == "_patch_ppa_transaction_races"
    )
    patches = ast.literal_eval(function.body[1].value)
    for relative, patch in patches.items():
        path = tmp_path / relative
        if path.exists():
            continue
        # Only the preflight's real patched hunks are needed for other owners.
        # The two changed PPA owners above remain complete reviewed SDK fixtures.
        hunks = re.split(r"^@@[^\n]*\n", patch, flags=re.MULTILINE)[1:]
        text = "\n/* next reviewed hunk */\n".join(
            "".join(
                line[1:]
                for line in hunk.splitlines(keepends=True)
                if line.startswith((" ", "+"))
            )
            for hunk in hunks
        )
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    return owner, patches


def test_regular_backport_remains_idempotent_after_diagnostics(tmp_path):
    """Repeated CMake preflight sees the original owner contract without SDK rewrites."""
    module = _sdk(tmp_path)
    owner, patches = _regular_backport_owners(tmp_path)
    owner._patch_ppa_transaction_races(tmp_path)
    module.patch_framework(tmp_path)
    before = {name: (tmp_path / name).read_bytes() for name in patches}
    owner._patch_ppa_transaction_races(tmp_path)
    assert before == {name: (tmp_path / name).read_bytes() for name in patches}
    module.patch_framework(tmp_path)
    assert before == {name: (tmp_path / name).read_bytes() for name in patches}


@pytest.mark.parametrize("order", ["cache_then_diag", "diag_then_cache"])
def test_reviewed_cache_hook_and_diagnostics_commute(tmp_path, order):
    """Native component discovery order cannot omit, duplicate, or weaken either owner."""
    from esphome.components.lvgl import lvgl_build_filter as cache

    module = _sdk(tmp_path)
    for name in ("ppa_blend", "ppa_fill"):
        target = tmp_path / "components/esp_driver_ppa/src" / (name + ".c")
        target.write_bytes((FIXTURES / (name + "_cache_before_556.c")).read_bytes())
    owner, regular = _regular_backport_owners(tmp_path)
    operations = (cache.patch_espidf_ppa_cache_sync, module.patch_framework)
    if order == "diag_then_cache":
        operations = tuple(reversed(operations))
    for operation in operations:
        operation(tmp_path)
    names = {*module.INPUTS, *regular}
    before = {name: (tmp_path / name).read_bytes() for name in names}
    # Replay both orders and the real regular-backport preflight, as native CMake
    # does repeatedly and also in a later diagnostics-disabled configuration.
    for operation in (*operations, *reversed(operations)):
        operation(tmp_path)
        owner._patch_ppa_transaction_races(tmp_path)
    assert before == {name: (tmp_path / name).read_bytes() for name in names}
    srm = (tmp_path / "components/esp_driver_ppa/src/ppa_srm.c").read_text()
    assert srm.count("static esp_err_t ppa_cache_msync_external_window(") == 1
    assert (
        srm.count("esphome_ppa_wait_diagnostics_srm_bypass(trans_on_picked_desc") == 1
    )
    restored = module.original_source("components/esp_driver_ppa/src/ppa_srm.c", srm)
    assert (
        hashlib.sha256(restored.encode()).hexdigest()
        == module.REVIEWED_CACHE_SOURCE_SHA256[
            "components/esp_driver_ppa/src/ppa_srm.c"
        ]
    )


def test_guarded_cache_variant_does_not_authorize_unrelated_edits(tmp_path):
    from esphome.components.lvgl import lvgl_build_filter as cache

    module = _sdk(tmp_path)
    srm = tmp_path / "components/esp_driver_ppa/src/ppa_srm.c"
    cache.patch_espidf_ppa_cache_sync_source(srm, (5, 5, 6))
    srm.write_bytes(srm.read_bytes() + b"\n/* Unreviewed unrelated owner edit. */\n")
    before = {name: (tmp_path / name).read_bytes() for name in module.INPUTS}
    with pytest.raises(RuntimeError, match="SDK source changed"):
        module.patch_framework(tmp_path)
    assert before == {name: (tmp_path / name).read_bytes() for name in module.INPUTS}


def test_kconfig_and_native_owner_are_explicitly_opt_in():
    kconfig = (OWNER / "mipi_dsi_framework_patch/Kconfig.projbuild").read_text()
    assert "config ESPHOME_PPA_WAIT_DIAGNOSTICS" in kconfig
    assert "default n" in kconfig
    cmake = (OWNER / "mipi_dsi_framework_patch/CMakeLists.txt").read_text()
    assert "if(CONFIG_ESPHOME_PPA_WAIT_DIAGNOSTICS)" in cmake
    assert "--ppa-wait-diagnostics" in cmake
    assert "ppa_wait_diagnostics.py" in cmake
    main = (OWNER / "patch_espidf_dsi_dma2d.py").read_text()
    assert main.index("_patch_dma2d_transaction_race(framework_path)") < main.index(
        'if "--ppa-wait-diagnostics" in sys.argv[2:]:'
    )


PREAMBLE = r"""
#include <cassert>
#include <cstdint>
#include <cstring>
#define CONFIG_ESPHOME_PPA_WAIT_DIAGNOSTICS 1
#define DRAM_ATTR
#define IRAM_ATTR
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
static int locks;
#define portENTER_CRITICAL_ISR(p) ((void)(p), ++locks)
#define portEXIT_CRITICAL_ISR(p) ((void)(p), --locks)
#define portENTER_CRITICAL(p) ((void)(p), ++locks)
#define portEXIT_CRITICAL(p) ((void)(p), --locks)
#define portENTER_CRITICAL_SAFE(p) ((void)(p), ++locks)
#define portEXIT_CRITICAL_SAFE(p) ((void)(p), --locks)
static uint32_t tick = 19;
static uint32_t xTaskGetTickCountFromISR() { return tick; }
struct Reg { uint32_t val{}; };
struct InChannel { Reg in_int_raw, in_int_ena, in_state; };
struct Device { InChannel in_channel[2]; Reg sr_byte_order; };
struct dma2d_group_t { struct { Device *dev; } hal; };
struct dma2d_rx_channel_t { void *user_data; uint32_t bundled_tx_channel_mask; };
struct dma2d_trans_t {};
using dma2d_pool_handle_t = dma2d_group_t *;
#define DMA2D_LL_EVENT_RX_SUC_EOF 1
#define DMA2D_LL_EVENT_RX_ERR_EOF 2
#define DMA2D_LL_EVENT_RX_DESC_ERROR 4
#define DMA2D_LL_EVENT_RX_DESC_EMPTY 8
"""


def _compile_run(tmp_path, text):
    compiler = os.environ.get("ESPHOME_TEST_CXX")
    if not compiler:
        pytest.skip("Set ESPHOME_TEST_CXX to run the actual C/C++ snippet fixture")
    source = tmp_path / "fixture.cpp"
    binary = tmp_path / "fixture.exe"
    source.write_text(text, encoding="utf-8")
    command = [compiler, *shlex.split(os.environ.get("ESPHOME_TEST_CXX_ARGS", ""))]
    subprocess.run(
        [
            *command,
            "-std=c++20",
            "-Wall",
            "-Wextra",
            "-Werror",
            str(source),
            "-o",
            str(binary),
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    subprocess.run([str(binary)], check=True, capture_output=True, text=True)


def test_actual_preclear_journal_is_bounded_and_preserves_first_error(tmp_path):
    helper = _load().DMA2D_HELPER.split("static void esphome_dma2d_wait_print", 1)[0]
    helper += "\n#endif\n"
    _compile_run(
        tmp_path,
        PREAMBLE
        + helper
        + r"""
uint32_t esphome_ppa_wait_diagnostics_srm_order() { return 0x1234; }
int main() {
    Device dev{}; dma2d_group_t group{{&dev}};
    dma2d_rx_channel_t rx{reinterpret_cast<void *>(0x456), 3};
    auto *trans = reinterpret_cast<dma2d_trans_t *>(0x789);
    dev.in_channel[1].in_int_raw.val = 0xa5;
    dev.in_channel[1].in_int_ena.val = 7;
    dev.in_channel[1].in_state.val = 42;
    assert(esphome_dma2d_wait_note(&group, &rx, 1, 0, trans) == 0);
    assert(s_esphome_dma2d_wait_sequence == 0);
    const auto first = esphome_dma2d_wait_note(&group, &rx, 1, DMA2D_LL_EVENT_RX_ERR_EOF, trans);
    dev.in_channel[1].in_int_raw.val = 0; // the actual ISR clears hardware next
    esphome_dma2d_wait_claim(first, true);
    assert(s_esphome_dma2d_wait_first_error.raw == 0xa5);
    assert(s_esphome_dma2d_wait_first_error.transaction == 0x789);
    assert(s_esphome_dma2d_wait_first_error.callback_user == 0x456);
    assert(s_esphome_dma2d_wait_first_error.ppa_order == 0x1234);
    assert(s_esphome_dma2d_wait_first_error.claimed == 1);
    for (unsigned i = 0; i < 20; ++i) {
        const auto seq = esphome_dma2d_wait_note(&group, &rx, 1, DMA2D_LL_EVENT_RX_SUC_EOF, trans);
        esphome_dma2d_wait_claim(seq, i % 2);
    }
    const auto slot = s_esphome_dma2d_wait_records[(first - 1) % 4];
    esphome_dma2d_wait_claim(first, false); // late update cannot corrupt a reused slot
    assert(std::memcmp(&slot, &s_esphome_dma2d_wait_records[(first - 1) % 4], sizeof(slot)) == 0);
    assert(s_esphome_dma2d_wait_first_error.sequence == first);
    assert(s_esphome_dma2d_wait_first_error.claimed == 0);
    assert(s_esphome_dma2d_wait_sequence == 21);
    assert(locks == 0);
}
""",
    )


def test_actual_srm_override_rejects_every_ineligible_operation(tmp_path):
    helper = _load().PPA_HELPER.split("static void esphome_ppa_wait_print_srm(", 1)[0]
    helper += "\n#endif\n"
    _compile_run(
        tmp_path,
        PREAMBLE
        + r"""
struct ppa_trans_t {};
struct Picture { uint32_t srm_cm{}, pic_w{384}, pic_h{384}, block_w{356}, block_h{32}, block_offset_x{}, block_offset_y{}; };
struct ppa_srm_oper_t {
    Picture in, out;
    float scale_x{1.0f}, scale_y{1.0f};
    uint32_t rotation_angle{}, scale_x_int{1}, scale_x_frag{}, scale_y_int{1}, scale_y_frag{};
    bool mirror_x{}, mirror_y{};
};
#define PPA_SRM_COLOR_MODE_RGB888 0
#define PPA_SRM_ROTATION_ANGLE_0 0
struct { void *srm; struct { Device *dev; } hal; } s_platform{};
"""
        + helper
        + r"""
int main() {
    ppa_trans_t trans; ppa_srm_oper_t op;
    auto choose = [&](bool computed = false, uint32_t bits = 1536) {
        return esphome_ppa_wait_diagnostics_srm_bypass(&trans, &op, computed, bits);
    };
    assert(!choose()); // auto is the power-on default
    assert(choose(true)); // preserve a computed true in every mode
    assert(esphome_ppa_wait_diagnostics_set_srm_bypass(1));
    assert(choose());
    assert(s_esphome_ppa_wait_srm.eligible == 1);
    assert(s_esphome_ppa_wait_srm.computed == 0 && s_esphome_ppa_wait_srm.effective == 1);
    assert(s_esphome_ppa_wait_picked == 3 && s_esphome_ppa_wait_eligible_count == 3);
    assert(s_esphome_ppa_wait_forced == 1);
    assert(!esphome_ppa_wait_diagnostics_set_srm_bypass(0));
    assert(!esphome_ppa_wait_diagnostics_set_srm_bypass(2));
    assert(choose()); // invalid setters do not change the request
    assert(!choose(false, 1535)); assert(!choose(false, 1537));
    op.in.srm_cm = 1; assert(!choose()); op.in.srm_cm = 0;
    op.out.srm_cm = 1; assert(!choose()); op.out.srm_cm = 0;
    op.rotation_angle = 1; assert(!choose()); op.rotation_angle = 0;
    op.scale_x = 1.03f; assert(!choose()); op.scale_x = 1.0f;
    op.scale_y = 1.03f; assert(!choose()); op.scale_y = 1.0f;
    op.scale_x_int = 2; assert(!choose()); op.scale_x_int = 1;
    op.scale_x_frag = 1; assert(!choose()); op.scale_x_frag = 0;
    op.scale_y_int = 2; assert(!choose()); op.scale_y_int = 1;
    op.scale_y_frag = 1; assert(!choose()); op.scale_y_frag = 0;
    op.mirror_x = true; assert(!choose()); op.mirror_x = false;
    op.mirror_y = true; assert(!choose()); op.mirror_y = false;
    assert(choose());
    assert(esphome_ppa_wait_diagnostics_set_srm_bypass(-1));
    assert(!choose()); assert(choose(true));
    assert(s_esphome_ppa_wait_eligible.requested == 0xffffffffU);
    assert(s_esphome_ppa_wait_forced == 3);
    assert(locks == 0);
}
""",
    )


@pytest.mark.parametrize("enabled", [False, True])
def test_actual_wait_reports_once_without_completing_transaction(tmp_path, enabled):
    wait = _load().WAIT
    text = r"""
#include <cassert>
#define CONFIG_ESPHOME_PPA_WAIT_DIAGNOSTICS ENABLED
#define pdMS_TO_TICKS(ms) (ms)
#define pdTRUE 1
#define portMAX_DELAY 0xffffffffU
struct Trans { void *sem; } trans;
static int calls, reports, result;
static unsigned timeouts[3];
static int xSemaphoreTake(void *, unsigned timeout) {
    timeouts[calls++] = timeout;
    return timeout == portMAX_DELAY ? pdTRUE : result;
}
static void esphome_ppa_wait_diagnostics(void *, void *, Trans *) { ++reports; }
static void run() {
    [[maybe_unused]] void *ppa_client = nullptr, *ppa_engine_base = nullptr;
    auto *trans_elm = &trans;
WAIT_BODY
}
int main() {
    result = 0; run();
#if CONFIG_ESPHOME_PPA_WAIT_DIAGNOSTICS
    assert(calls == 2 && reports == 1);
    assert(timeouts[0] == 1000 && timeouts[1] == portMAX_DELAY);
    calls = reports = 0; result = pdTRUE; run();
    assert(calls == 1 && reports == 0 && timeouts[0] == 1000);
#else
    assert(calls == 1 && reports == 0 && timeouts[0] == portMAX_DELAY);
#endif
}
""".replace("ENABLED", "1" if enabled else "0").replace("WAIT_BODY", wait)
    # The disabled variant deliberately does not reference the observer.
    text = text.replace(
        "static void esphome_ppa_wait_diagnostics",
        "[[maybe_unused]] static void esphome_ppa_wait_diagnostics",
    )
    _compile_run(tmp_path, text)
