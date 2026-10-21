#pragma once

#include "esphome/core/defines.h"

#if defined(USE_ESP32) && defined(USE_ESP32_HOSTED_SHUTDOWN_RESET)

#include "esphome/core/component.h"

namespace esphome::esp32_hosted {

class ShutdownResetComponent : public Component {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  bool is_registered() const { return this->registered_; }
  bool is_shutdown_assertion_complete() const;
  int get_shutdown_result() const;

 protected:
  bool registered_{false};
};

}  // namespace esphome::esp32_hosted

#endif
