#pragma once

#include "esphome/core/defines.h"

#if defined(USE_ESP32_VARIANT_ESP32P4) && defined(USE_MIPI_DSI_MEMORY_FAULT_DIAGNOSTICS)
#include <cstdint>
#include "esp_attr.h"

namespace esphome::mipi_dsi {

// Read-only, bounded capture. The caller must be the existing DSI underrun IRQ.
void IRAM_ATTR note_memory_fault_from_isr(uint32_t tick);
// Main-loop only; bypass the heap-allocating logger/API path.
void IRAM_ATTR emit_memory_fault_diagnostics();

}  // namespace esphome::mipi_dsi
#endif
