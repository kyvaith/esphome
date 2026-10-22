#include "shutdown_reset.h"

#if defined(USE_ESP32) && defined(USE_ESP32_HOSTED_SHUTDOWN_RESET)

#include <cstdint>

#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "esphome/core/log.h"

extern "C" {
#include "port/esp/freertos/include/port_esp_hosted_host_config.h"
#include "esp_hosted_transport_config.h"
}

namespace esphome::esp32_hosted {

static const char *const TAG = "esp32_hosted.shutdown";
static constexpr uint32_t SHUTDOWN_RESET_ASSERT_US = 1000;
static_assert(__atomic_always_lock_free(sizeof(uint32_t), nullptr));

namespace {
struct ShutdownResetState {
  gpio_num_t pin{GPIO_NUM_NC};
  uint32_t inactive_level{0};
  uint32_t started{0};
  uint32_t completed{0};
  int result{ESP_OK};
  bool registered{false};
};

ShutdownResetState shutdown_reset;

void assert_coprocessor_reset() {
  // Sequential retries do not reconfigure the pin or extend the reset interval.
  // This does not serialize concurrent esp_restart() calls from different cores.
  if (__atomic_exchange_n(&shutdown_reset.started, 1U, __ATOMIC_ACQ_REL) != 0)
    return;

  // Restore the native GPIO function, including a pin previously using RTC IO.
  // Set its latch before enabling output, so an old latch value is not exposed.
  esp_err_t result = gpio_reset_pin(shutdown_reset.pin);
  if (result == ESP_OK)
    result = gpio_set_level(shutdown_reset.pin, shutdown_reset.inactive_level);
  if (result == ESP_OK)
    result = gpio_set_direction(shutdown_reset.pin, GPIO_MODE_OUTPUT);
  if (result != ESP_OK) {
    __atomic_store_n(&shutdown_reset.result, result, __ATOMIC_RELEASE);
    return;
  }

  // A short nominal interval before host peripheral/clock reset. No GPIO hold:
  // this does not promise that the level persists through the following ROM boot.
  esp_rom_delay_us(SHUTDOWN_RESET_ASSERT_US);
  __atomic_store_n(&shutdown_reset.completed, 1U, __ATOMIC_RELEASE);
}
}  // namespace

void ShutdownResetComponent::setup() {
  if (shutdown_reset.registered) {
    ESP_LOGE(TAG, "Shutdown reset callback already registered");
    this->mark_failed();
    return;
  }

  gpio_pin_t reset_pin{};
  const auto config_result = esp_hosted_transport_get_reset_config(&reset_pin);
  if (config_result != ESP_TRANSPORT_OK || reset_pin.port != nullptr || !GPIO_IS_VALID_OUTPUT_GPIO(reset_pin.pin)) {
    ESP_LOGE(TAG, "Invalid native coprocessor reset configuration");
    this->mark_failed();
    return;
  }

  // The SDK ends its normal reset sequence at ACTIVE (running/enabled).
  // Its opposite electrical level is the configured inactive reset control.
  shutdown_reset.pin = static_cast<gpio_num_t>(reset_pin.pin);
  shutdown_reset.inactive_level = H_RESET_VAL_INACTIVE;
  __atomic_store_n(&shutdown_reset.started, 0U, __ATOMIC_RELEASE);
  const esp_err_t result = esp_register_shutdown_handler(assert_coprocessor_reset);
  if (result != ESP_OK) {
    ESP_LOGE(TAG, "Shutdown reset registration failed: %d", static_cast<int>(result));
    this->mark_failed();
    return;
  }
  shutdown_reset.registered = true;
  this->registered_ = true;
}

bool ShutdownResetComponent::is_shutdown_assertion_complete() const {
  return __atomic_load_n(&shutdown_reset.completed, __ATOMIC_ACQUIRE) != 0;
}

int ShutdownResetComponent::get_shutdown_result() const {
  return __atomic_load_n(&shutdown_reset.result, __ATOMIC_ACQUIRE);
}

void ShutdownResetComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Coprocessor shutdown reset:");
  ESP_LOGCONFIG(TAG, "  Registered: %s", YESNO(this->registered_));
  if (this->registered_) {
    ESP_LOGCONFIG(TAG, "  Reset GPIO: %d, inactive level: %u", static_cast<int>(shutdown_reset.pin),
                  static_cast<unsigned>(shutdown_reset.inactive_level));
    ESP_LOGCONFIG(TAG, "  Nominal assertion interval: %u us", static_cast<unsigned>(SHUTDOWN_RESET_ASSERT_US));
    ESP_LOGCONFIG(TAG, "  Current-boot assertion complete: %s, GPIO result: %d",
                  YESNO(this->is_shutdown_assertion_complete()), this->get_shutdown_result());
  }
}

}  // namespace esphome::esp32_hosted

#endif
