#include "memory_fault_diagnostics.h"

#if defined(USE_ESP32_VARIANT_ESP32P4) && defined(USE_MIPI_DSI_MEMORY_FAULT_DIAGNOSTICS)
#include <cstddef>
#include "esp_rom_uart.h"
#include "freertos/FreeRTOS.h"
#include "soc/cache_reg.h"
#include "soc/dma_pms_reg.h"
#include "soc/hp_peri_pms_reg.h"
#include "soc/hp_system_reg.h"
#include "soc/soc.h"
#include "soc/spi_mem_s_reg.h"

namespace esphome::mipi_dsi {

// Positional DSIMEM1 schema: sequence, tick, IRQ core, coalesced IRQs;
// MSPI raw/AXI address, core error-response config/timeout raw/AHB write-error raw;
// L1 data-cache raw/address/attributes, L2 raw/address/attributes;
// HP CPU0/1 machine permissions, DMA SDMMC/USB/TRACE0/TRACE1 read/write masks.
// The last ten values are permission CONFIGURATION, not violation status.
// These registers are readable without clearing their latched flags. Never
// write the clear/enable/permission registers here. LP APM is not sampled.
static constexpr size_t MEMORY_FAULT_WORDS = 25;
struct MemoryFaultRecord {
  uint32_t words[MEMORY_FAULT_WORDS];
};

static DRAM_ATTR portMUX_TYPE memory_fault_mux = portMUX_INITIALIZER_UNLOCKED;
static DRAM_ATTR MemoryFaultRecord memory_fault_record;
static DRAM_ATTR uint32_t memory_fault_sequence;
static DRAM_ATTR bool memory_fault_pending;
static DRAM_ATTR char memory_fault_prefix[] = "DSIMEM1";

void IRAM_ATTR note_memory_fault_from_isr(uint32_t tick) {
  portENTER_CRITICAL_ISR(&memory_fault_mux);
  const uint32_t sequence = ++memory_fault_sequence;
  if (memory_fault_pending) {
    ++memory_fault_record.words[3];
    portEXIT_CRITICAL_ISR(&memory_fault_mux);
    return;
  }
  auto &w = memory_fault_record.words;
  w[0] = sequence;
  w[1] = tick;
  w[2] = xPortGetCoreID();
  w[3] = 0;
  w[4] = REG_READ(SPI_MEM_S_INT_RAW_REG);
  w[5] = REG_READ(SPI_MEM_S_AXI_ERR_ADDR_REG);
  w[6] = REG_READ(HP_SYSTEM_CORE_ERR_RESP_DIS_REG);
  w[7] = REG_READ(HP_SYSTEM_CORE_TIMEOUT_INT_RAW_REG);
  w[8] = REG_READ(HP_SYSTEM_AHB2AXI_BRESP_ERR_INT_RAW_REG);
  w[9] = REG_READ(CACHE_L1_CACHE_ACS_FAIL_INT_RAW_REG);
  w[10] = REG_READ(CACHE_L1_DCACHE_ACS_FAIL_ADDR_REG);
  w[11] = REG_READ(CACHE_L1_DCACHE_ACS_FAIL_ID_ATTR_REG);
  w[12] = REG_READ(CACHE_L2_CACHE_ACS_FAIL_INT_RAW_REG);
  w[13] = REG_READ(CACHE_L2_CACHE_ACS_FAIL_ADDR_REG);
  w[14] = REG_READ(CACHE_L2_CACHE_ACS_FAIL_ID_ATTR_REG);
  w[15] = REG_READ(PMS_CORE0_MM_HP_PERI_PMS_REG0_REG);
  w[16] = REG_READ(PMS_CORE1_MM_HP_PERI_PMS_REG0_REG);
  w[17] = REG_READ(PMS_DMA_SDMMC_PMS_R_REG);
  w[18] = REG_READ(PMS_DMA_SDMMC_PMS_W_REG);
  w[19] = REG_READ(PMS_DMA_USB_PMS_R_REG);
  w[20] = REG_READ(PMS_DMA_USB_PMS_W_REG);
  w[21] = REG_READ(PMS_DMA_TRACE0_PMS_R_REG);
  w[22] = REG_READ(PMS_DMA_TRACE0_PMS_W_REG);
  w[23] = REG_READ(PMS_DMA_TRACE1_PMS_R_REG);
  w[24] = REG_READ(PMS_DMA_TRACE1_PMS_W_REG);
  memory_fault_pending = true;
  portEXIT_CRITICAL_ISR(&memory_fault_mux);
}

void IRAM_ATTR emit_memory_fault_diagnostics() {
  MemoryFaultRecord record;
  portENTER_CRITICAL(&memory_fault_mux);
  if (!memory_fault_pending) {
    portEXIT_CRITICAL(&memory_fault_mux);
    return;
  }
  for (size_t i = 0; i < MEMORY_FAULT_WORDS; ++i)
    record.words[i] = memory_fault_record.words[i];
  memory_fault_pending = false;
  portEXIT_CRITICAL(&memory_fault_mux);

  // No printf, heap, API log callback or UART output while holding the lock.
  // One 234-byte record costs about 20 ms at 115200 baud: diagnostics affect
  // timing. A new IRQ can populate the next record while this one is emitted.
  for (const char *p = memory_fault_prefix; *p != '\0'; ++p)
    esp_rom_output_tx_one_char(*p);
  for (uint32_t word : record.words) {
    esp_rom_output_tx_one_char(' ');
    for (int shift = 28; shift >= 0; shift -= 4) {
      const uint8_t digit = (word >> shift) & 0xFU;
      esp_rom_output_tx_one_char(digit < 10 ? '0' + digit : 'a' + digit - 10);
    }
  }
  esp_rom_output_tx_one_char('\r');
  esp_rom_output_tx_one_char('\n');
}

}  // namespace esphome::mipi_dsi
#endif
