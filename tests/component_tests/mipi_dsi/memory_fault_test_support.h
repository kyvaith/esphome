#pragma once
#include <cassert>
#include <cstdint>
#include <string>

#define USE_ESP32_VARIANT_ESP32P4
#define USE_MIPI_DSI_MEMORY_FAULT_DIAGNOSTICS
#define IRAM_ATTR
#define DRAM_ATTR
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
inline int test_lock_depth = 0;
inline uint32_t test_core = 1;
inline uint32_t test_register_bias = 0;
inline uint32_t test_reads = 0;
inline std::string test_output;
inline void (*test_during_output)() = nullptr;
#define portENTER_CRITICAL_ISR(m) (++test_lock_depth)
#define portEXIT_CRITICAL_ISR(m) (--test_lock_depth)
#define portENTER_CRITICAL(m) (++test_lock_depth)
#define portEXIT_CRITICAL(m) (--test_lock_depth)
inline uint32_t xPortGetCoreID() { return test_core; }
inline uint32_t test_reg_read(uint32_t address) {
  assert(test_lock_depth == 1);
  ++test_reads;
  return address + test_register_bias;
}
#define REG_READ(address) test_reg_read(address)
inline int esp_rom_output_tx_one_char(uint8_t value) {
  assert(test_lock_depth == 0);
  test_output += static_cast<char>(value);
  if (test_during_output != nullptr) {
    auto callback = test_during_output;
    test_during_output = nullptr;
    callback();
  }
  return 0;
}
