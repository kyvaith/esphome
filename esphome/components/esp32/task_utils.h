#pragma once

#ifdef USE_ESP32

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "esphome/core/main_task.h"

namespace esphome::esp32 {

inline BaseType_t loop_task_core() {
#if CONFIG_FREERTOS_UNICORE
  return 0;
#else
  if (esphome_main_task_handle == nullptr)
    return tskNO_AFFINITY;
  return xTaskGetCoreID(esphome_main_task_handle);
#endif
}

inline BaseType_t background_task_core(int requested_core) {
#if CONFIG_FREERTOS_UNICORE
  return tskNO_AFFINITY;
#else
  if (requested_core == 0 || requested_core == 1)
    return static_cast<BaseType_t>(requested_core);

  const BaseType_t loop_core = loop_task_core();
  if (loop_core == 0 || loop_core == 1)
    return loop_core == 0 ? 1 : 0;

  return xPortGetCoreID() == 0 ? 1 : 0;
#endif
}

}  // namespace esphome::esp32

#endif  // USE_ESP32
