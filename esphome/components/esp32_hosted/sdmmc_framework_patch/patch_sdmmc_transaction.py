"""Guarded ESP-IDF 5.5.5 backport; import-safe for native and PlatformIO IDF."""

from pathlib import Path
import sys

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
    target = framework_dir / "components/esp_driver_sdmmc/src/sdmmc_transaction.c"
    original = target.read_bytes()
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
    if b"\r\n" in original:
        new_text = new_text.replace("\n", "\r\n")
    target.write_bytes(new_text.encode("utf-8"))
    return "installed guarded 5038e2ad backport"


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch_sdmmc_transaction.py ESP_IDF_PATH")
    print("ESPHome SDMMC patch: " + patch_framework(Path(sys.argv[1])))


if __name__ == "__main__":
    main()
