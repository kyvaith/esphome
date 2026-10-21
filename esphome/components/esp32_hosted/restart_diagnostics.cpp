#include "restart_diagnostics.h"

#if defined(USE_ESP32) && defined(USE_ESP32_HOSTED_RESTART_DIAGNOSTICS)

#include <cinttypes>
#include <cstddef>

#include "esphome/core/log.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"

extern "C" {
#include "port/esp/freertos/include/port_esp_hosted_host_config.h"
#include "esp_hosted_os_abstraction.h"
// GNU wrap names keep the original function callable from this translation unit.
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming)
int __real_hosted_restart_host(void);
int __wrap_hosted_restart_host(void);
// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming)
}


namespace esphome::esp32_hosted {

static const char *const TAG = "esp32_hosted.restart";

#if SOC_RTC_MEM_SUPPORTED

static constexpr uint32_t RESTART_RECORD_VERSION = 1;
static constexpr uint32_t RESTART_RECORD_MAGIC = 0x52535451;

struct RetainedRestartRecord {
  uint32_t version;
  uint32_t size;
  uint32_t magic;
  RestartRequest request;
  uint32_t checksum;
};
static_assert(sizeof(RetainedRestartRecord) == 60);

// No constructors or initializers: this storage survives a software reset.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static RTC_NOINIT_ATTR volatile RetainedRestartRecord s_restart_record;
// Per-boot ownership: 0 = empty, 1 = writing, 2 = published, 3 = boot snapshot.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static uint32_t s_capture_state;

static uint32_t record_checksum(const RetainedRestartRecord &record) {
  uint32_t checksum = 2166136261U;
  const uint32_t words[]{record.version, record.size, record.request.caller_pc, record.request.core,
                         record.request.tick};
  for (const auto word : words) {
    for (unsigned shift = 0; shift < 32; shift += 8)
      checksum = (checksum ^ static_cast<uint8_t>(word >> shift)) * 16777619U;
  }
  for (const auto byte : record.request.elf_sha256)
    checksum = (checksum ^ byte) * 16777619U;
  return checksum;
}

static void retain_restart_request(uint32_t caller, uint32_t core, uint32_t tick) {
  // A concurrent requester never replaces the first request or waits forever.
  // Boot consumption owns the same state, so a current request cannot be read
  // or erased as though it belonged to the previous boot.
  for (unsigned attempt = 0;; attempt++) {
    uint32_t expected = 0;
    if (__atomic_compare_exchange_n(&s_capture_state, &expected, 1, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
      break;
    if (expected == 2 || attempt == 1000)
      return;
    esp_rom_delay_us(1);
  }

  __atomic_store_n(&s_restart_record.magic, 0, __ATOMIC_RELEASE);
  RetainedRestartRecord record{};
  record.version = RESTART_RECORD_VERSION;
  record.size = sizeof(record);
  record.request.caller_pc = caller;
  record.request.core = core;
  record.request.tick = tick;
  // IDF's string getter may be configured to truncate the hash. Read all raw
  // bytes through volatile, since esptool replaces this field after compilation.
  const volatile uint8_t *sha256 = esp_app_get_description()->app_elf_sha256;
  for (size_t i = 0; i < sizeof(record.request.elf_sha256); i++)
    record.request.elf_sha256[i] = sha256[i];
  record.checksum = record_checksum(record);

  auto *destination = reinterpret_cast<volatile uint8_t *>(&s_restart_record);
  const auto *source = reinterpret_cast<const uint8_t *>(&record);
  for (size_t i = 0; i < sizeof(record); i++)
    destination[i] = source[i];
  __atomic_store_n(&s_restart_record.magic, RESTART_RECORD_MAGIC, __ATOMIC_RELEASE);
  __atomic_store_n(&s_capture_state, 2, __ATOMIC_RELEASE);
}

#endif  // SOC_RTC_MEM_SUPPORTED

void RestartDiagnosticsComponent::setup() {
#ifdef USE_ESP32_HOSTED_TRANSPORT_DIAGNOSTICS
  static constexpr const char *TRANSPORT_TAGS[]{"H_SDIO_DRV", "sdio_wrapper", "transport",
                                               "H_API", "os_wrapper_esp", "RPC_WRAP"};
  for (const char *tag : TRANSPORT_TAGS)
    esp_log_level_set(tag, ESP_LOG_INFO);
  esp_rom_printf("ESPHOME_HOSTED_TRANSPORT_DIAG enabled=1 tags=6 max_level=%u sdio_level=%u\n",
                 static_cast<unsigned>(CONFIG_LOG_MAXIMUM_LEVEL),
                 static_cast<unsigned>(esp_log_level_get("H_SDIO_DRV")));
#endif
  // The SDK's callback table and original function share one translation unit,
  // so --wrap cannot redirect that table initializer. Use its public writable
  // OS interface before transport components start. Preserve custom callbacks.
  if (g_h.funcs != nullptr && g_h.funcs->_h_restart_host == __real_hosted_restart_host) {
    g_h.funcs->_h_restart_host = __wrap_hosted_restart_host;
    esp_rom_printf("ESPHOME_HOSTED_RESTART_DIAG installed=1\n");
  }
  this->reset_reason_ = esp_reset_reason();
#if SOC_RTC_MEM_SUPPORTED
  uint32_t expected = 0;
  if (!__atomic_compare_exchange_n(&s_capture_state, &expected, 3, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
    return;

  RetainedRestartRecord record{};
  const uint32_t magic = __atomic_load_n(&s_restart_record.magic, __ATOMIC_ACQUIRE);
  // Consume immediately under snapshot ownership. The immutable payload can
  // then be copied without retaining an old marker through setup work.
  __atomic_store_n(&s_restart_record.magic, 0, __ATOMIC_RELEASE);
  auto *destination = reinterpret_cast<uint8_t *>(&record);
  const auto *source = reinterpret_cast<const volatile uint8_t *>(&s_restart_record);
  for (size_t i = 0; i < sizeof(record); i++)
    destination[i] = source[i];
  if (magic == RESTART_RECORD_MAGIC && this->reset_reason_ == ESP_RST_SW && record.version == RESTART_RECORD_VERSION &&
      record.size == sizeof(record) && record.request.core < SOC_CPU_CORES_NUM &&
      record.checksum == record_checksum(record)) {
    this->last_request_ = record.request;
    this->has_request_ = true;
    this->same_firmware_ = true;
    const volatile uint8_t *sha256 = esp_app_get_description()->app_elf_sha256;
    for (size_t i = 0; i < sizeof(record.request.elf_sha256); i++) {
      if (record.request.elf_sha256[i] != sha256[i])
        this->same_firmware_ = false;
    }
  }
  __atomic_store_n(&s_capture_state, 0, __ATOMIC_RELEASE);
#endif
}

void RestartDiagnosticsComponent::dump_config() {
  if (!this->has_request_) {
    ESP_LOGW(TAG, "ESPHOME_RESTART_RETAINED unavailable reset=%" PRIu32, this->reset_reason_);
    return;
  }
  char sha256[65];
  static constexpr char HEX[] = "0123456789abcdef";
  for (size_t i = 0; i < sizeof(this->last_request_.elf_sha256); i++) {
    sha256[2 * i] = HEX[this->last_request_.elf_sha256[i] >> 4];
    sha256[2 * i + 1] = HEX[this->last_request_.elf_sha256[i] & 0xf];
  }
  sha256[64] = '\0';
  ESP_LOGW(TAG,
           "ESPHOME_RESTART_RETAINED caller=0x%08" PRIx32 " core=%" PRIu32 " tick=%" PRIu32
           " reset=%" PRIu32 " same_firmware=%s elf_sha256=%s",
           this->last_request_.caller_pc, this->last_request_.core, this->last_request_.tick, this->reset_reason_,
           this->same_firmware_ ? "yes" : "no", sha256);
}

}  // namespace esphome::esp32_hosted

// GNU linker wrap symbols must use these names at global C linkage.
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming)
extern "C" void __real_esp_restart() __attribute__((noreturn));

extern "C" __attribute__((noinline)) int __wrap_hosted_restart_host(void) {
  const auto caller = __builtin_return_address(0);
  const auto core = static_cast<uint32_t>(esp_cpu_get_core_id());
  const auto tick = static_cast<uint32_t>(xTaskGetTickCount());
#if SOC_RTC_MEM_SUPPORTED
  esphome::esp32_hosted::retain_restart_request(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(caller)), core, tick);
#endif
  esp_rom_printf("ESPHOME_HOSTED_RESTART_DIAG caller=%p core=%u tick=%u\n", caller,
                 static_cast<unsigned>(core), static_cast<unsigned>(tick));
  return __real_hosted_restart_host();
}

extern "C" __attribute__((noinline, noreturn)) void __wrap_esp_restart() {
  const auto caller = __builtin_return_address(0);
  const auto core = static_cast<uint32_t>(esp_cpu_get_core_id());
  const auto tick = static_cast<uint32_t>(xTaskGetTickCount());
#if SOC_RTC_MEM_SUPPORTED
  esphome::esp32_hosted::retain_restart_request(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(caller)), core, tick);
#endif
  // Bypass the queued logger and runtime filters before shutdown begins.
  // This diagnoses esp_restart calls, not direct panic/no-OS or hardware resets.
  esp_rom_printf("ESPHOME_RESTART_DIAG caller=%p core=%u tick=%u\n", caller,
                 static_cast<unsigned>(core), static_cast<unsigned>(tick));
  __real_esp_restart();
}
// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming)

#endif


