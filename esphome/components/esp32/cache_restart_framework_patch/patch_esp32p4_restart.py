"""Install the reviewed, default-off ESP-IDF 5.5.5/5.5.6 P4 restart guard."""

import hashlib
import os
from pathlib import Path
import sys
import tempfile

SOURCE = "components/esp_system/port/soc/esp32p4/system_internal.c"
REVIEWED_VERSIONS = frozenset({"v5.5.5", "v5.5.6"})
PRISTINE_SHA256 = "7bd2af3da2a9c1592c8d9ff6f65ad7fe320690b2e480076685df068f231e40cc"
GUARDED_SHA256 = "f1a00fcae4a68738006fbc7872346f7fab8be801023191e5aa2eadfe23478056"
RESET_OLD = """    esp_cpu_reset(other_core_id);
    esp_cpu_stall(other_core_id);"""
RESET_GUARDED = """#if !CONFIG_ESPHOME_ESP32P4_CACHE_SAFE_RESTART
    esp_cpu_reset(other_core_id);
#endif
    esp_cpu_stall(other_core_id);"""
WRITEBACK_OLD = """#if CONFIG_SPIRAM
    Cache_WriteBack_All(CACHE_MAP_L1_DCACHE);
#endif"""
WRITEBACK_GUARDED = """#if CONFIG_SPIRAM
#if CONFIG_ESPHOME_ESP32P4_CACHE_SAFE_RESTART
    // The other core is stalled without reset. Let its shared sync finish before
    // programming the cache sync engine (espressif/esp-idf#19171).
    // ENA bits clear after completion; unlike DONE, idle does not require a prior sync.
    __asm__ __volatile__("fence iorw, iorw" ::: "memory");
    while (REG_GET_BIT(CACHE_SYNC_CTRL_REG, CACHE_INVALIDATE_ENA | CACHE_CLEAN_ENA |
                      CACHE_WRITEBACK_ENA | CACHE_WRITEBACK_INVALIDATE_ENA)) {
    }
    __asm__ __volatile__("fence iorw, iorw" ::: "memory");
#endif
    Cache_WriteBack_All(CACHE_MAP_L1_DCACHE);
#endif"""


def patch_framework(framework_dir: Path) -> str:
    """Reject any source outside the exact reviewed owning SDK file."""
    version = (framework_dir / "version.txt").read_text(encoding="utf-8").strip()
    if version not in REVIEWED_VERSIONS:
        raise RuntimeError(
            "Cache-safe restart backport requires reviewed ESP-IDF 5.5.5 or 5.5.6"
        )
    target = framework_dir / SOURCE
    original = target.read_bytes()
    digest = hashlib.sha256(original).hexdigest()
    if digest == GUARDED_SHA256:
        return "already guarded"
    if digest != PRISTINE_SHA256:
        raise RuntimeError("ESP32-P4 restart source changed; review this backport")
    text = original.decode("utf-8")
    if text.count(RESET_OLD) != 1 or text.count(WRITEBACK_OLD) != 1:
        raise RuntimeError("Reviewed ESP32-P4 restart owner blocks missing")
    text = text.replace(
        '#include "esp32p4/rom/cache.h"',
        '#include "esp32p4/rom/cache.h"\n#if CONFIG_ESPHOME_ESP32P4_CACHE_SAFE_RESTART\n#include "soc/cache_reg.h"\n#endif',
    )
    text = text.replace(RESET_OLD, RESET_GUARDED).replace(
        WRITEBACK_OLD, WRITEBACK_GUARDED
    )
    guarded = text.encode("utf-8")
    if hashlib.sha256(guarded).hexdigest() != GUARDED_SHA256:
        raise RuntimeError("ESP32-P4 restart postimage does not match reviewed source")
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(
            dir=target.parent, prefix=target.name + ".", suffix=".tmp", delete=False
        ) as stream:
            temporary = Path(stream.name)
            stream.write(guarded)
            stream.flush()
            os.fsync(stream.fileno())
        temporary.replace(target)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return "installed guarded cache-safe restart"


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch_esp32p4_restart.py ESP_IDF_PATH")
    print("ESPHome ESP32-P4 restart patch: " + patch_framework(Path(sys.argv[1])))


if __name__ == "__main__":
    main()
