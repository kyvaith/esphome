#include "number.h"

#if defined(USE_ESP32) && defined(USE_NUMBER)

#include "esphome/core/log.h"

namespace esphome {
namespace esp_audio_stack {

void MicGainNumber::dump_config() {
  ESP_LOGCONFIG("audio_stack.mic_gain", "Mic Gain Number (post-processor dB, range %.1f..%.1f)", this->min_db_,
                this->max_db_);
}

void MasterVolumeNumber::dump_config() {
#ifdef USE_SPEAKER
  ESP_LOGCONFIG("audio_stack.master_volume", "Master Volume Number%s",
                this->speaker_ != nullptr ? " (speaker-backed)" : "");
#else
  ESP_LOGCONFIG("audio_stack.master_volume", "Master Volume Number");
#endif
}

}  // namespace esp_audio_stack
}  // namespace esphome

#endif  // USE_ESP32 && USE_NUMBER
