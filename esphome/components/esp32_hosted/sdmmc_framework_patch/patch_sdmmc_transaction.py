"""Guarded ESP-IDF 5.5.5/5.5.6 backport, import-safe for both IDF build paths."""

import hashlib
from pathlib import Path
import sys

REVIEWED_VERSIONS = frozenset({"v5.5.5", "v5.5.6"})
PRISTINE_SHA256 = "7cea7d649edbfa655ab3c3a26ba1d3d25290841091b8900f9643559bdb1bcad6"
GUARDED_SHA256 = "198a092a3975ae1ceb65d83f2f06755106f7d81ff927c8165f3df4f18754925b"
FIXED_SHA256 = "74f5a0ee4c4f9fe54f88e0522dd40d885474564fa005a3ad8a43e130a3eda3ab"

OLD = """        ret = esp_cache_msync((void *)cmdinfo->data, cache_sync_len, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
        if (ret != ESP_OK) {
            goto out;
        }
"""
FIXED = """        esp_err_t sync_ret = esp_cache_msync((void *)cmdinfo->data, cache_sync_len, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
        if (ret == ESP_OK) {
            ret = sync_ret;
        } else if (sync_ret != ESP_OK) {
            ESP_LOGE(TAG, "cache sync failed: 0x%x", sync_ret);
        }
        if (ret != ESP_OK) {
            goto out;
        }
"""
GUARDED = (
    "#if CONFIG_ESPHOME_SDMMC_PRESERVE_TRANSACTION_ERROR\n"
    + FIXED
    + "#else\n"
    + OLD
    + "#endif\n"
)


OWNER = "esp_err_t sdmmc_host_do_transaction(int slot, sdmmc_command_t* cmdinfo)"
CACHE_PREFIX = "#if SOC_CACHE_INTERNAL_MEM_VIA_L1CACHE\n    if (cmdinfo->data) {\n"
CACHE_SUFFIX = "    }\n#endif\n\nout:\n"


def patch_framework(framework_dir: Path) -> str:
    version = (framework_dir / "version.txt").read_text(encoding="utf-8").strip()
    if version not in REVIEWED_VERSIONS:
        raise RuntimeError(
            "SDMMC backport requires reviewed ESP-IDF 5.5.5 or 5.5.6"
        )
    target = framework_dir / "components/esp_driver_sdmmc/src/sdmmc_transaction.c"
    original = target.read_bytes()
    digest = hashlib.sha256(original).hexdigest()
    if digest not in (PRISTINE_SHA256, GUARDED_SHA256, FIXED_SHA256):
        raise RuntimeError("SDMMC transaction source changed; review this backport")
    text = original.decode("utf-8").replace("\r\n", "\n")
    if text.count(OWNER) != 1:
        raise RuntimeError("Expected exactly one reviewed SDMMC transaction owner")
    start = text.index(OWNER)
    # This reviewed SDK style has only function-level closing braces at column 0.
    # Reject changed boundaries instead of searching into a subsequent function.
    end = text.find("\n}\n", start)
    if end == -1:
        raise RuntimeError("Reviewed SDMMC transaction function boundary not found")
    end += len("\n}\n")
    owner = text[start:end]
    guarded_section = CACHE_PREFIX + GUARDED + CACHE_SUFFIX
    fixed_section = CACHE_PREFIX + FIXED + CACHE_SUFFIX
    old_section = CACHE_PREFIX + OLD + CACHE_SUFFIX
    if owner.count(guarded_section) == 1:
        if text.count(GUARDED) != 1 or text.count(OLD) != 1 or text.count(FIXED) != 1:
            raise RuntimeError("Mixed or duplicate guarded SDMMC cache-sync source")
        return "already guarded"
    if owner.count(fixed_section) == 1:
        if text.count(FIXED) != 1 or OLD in text or GUARDED in text:
            raise RuntimeError("Mixed or duplicate upstream SDMMC cache-sync source")
        return "upstream fix already present"
    if (
        owner.count(old_section) != 1
        or text.count(OLD) != 1
        or FIXED in text
        or GUARDED in text
    ):
        raise RuntimeError(
            "SDMMC final cache-sync owner or P4 guard changed; review this backport"
        )
    new_owner = owner.replace(old_section, guarded_section, 1)
    new_text = text[:start] + new_owner + text[end:]
    guarded = new_text.encode("utf-8")
    if hashlib.sha256(guarded).hexdigest() != GUARDED_SHA256:
        raise RuntimeError("SDMMC postimage does not match reviewed source")
    target.write_bytes(guarded)
    return "installed guarded 5038e2ad backport"


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch_sdmmc_transaction.py ESP_IDF_PATH")
    print("ESPHome SDMMC patch: " + patch_framework(Path(sys.argv[1])))


if __name__ == "__main__":
    main()
