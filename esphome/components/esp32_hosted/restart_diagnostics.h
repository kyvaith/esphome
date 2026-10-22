#pragma once

#include "esphome/core/defines.h"

#if defined(USE_ESP32) && defined(USE_ESP32_HOSTED_RESTART_DIAGNOSTICS)

#include <cstdint>

#include "esphome/core/component.h"

namespace esphome::esp32_hosted {

struct RestartRequest {
  uint32_t caller_pc;
  uint32_t core;
  uint32_t tick;
  uint8_t elf_sha256[32];
};

class RestartDiagnosticsComponent : public Component {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

  /// The last intercepted request, only when the immediately preceding reset was software initiated.
  const RestartRequest *get_last_restart_request() const { return this->has_request_ ? &this->last_request_ : nullptr; }
  /// A different image remains reportable, but its caller must be resolved against its recorded ELF hash.
  bool is_same_firmware() const { return this->has_request_ && this->same_firmware_; }
  uint32_t get_reset_reason() const { return this->reset_reason_; }

 protected:
  RestartRequest last_request_{};
  uint32_t reset_reason_{0};
  bool has_request_{false};
  bool same_firmware_{false};
};

}  // namespace esphome::esp32_hosted

#endif
