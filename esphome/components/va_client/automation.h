#pragma once

#include "esphome/core/automation.h"
#include "va_client.h"

#include <string>

namespace esphome {
namespace va_client {

class OnPhaseTrigger : public Trigger<std::string> {
 public:
  explicit OnPhaseTrigger(VaClient *parent) { parent->add_on_phase_trigger(this); }
};

class OnRepeatedFailureTrigger : public Trigger<> {
 public:
  explicit OnRepeatedFailureTrigger(VaClient *parent) {
    parent->add_on_repeated_failure_trigger(this);
  }
};

// Fires when the device opens a follow-up mic window (i.e. server's
// request_follow_up message landed and the audio buffer has drained).
// yaml uses this to play the wake chime + flip the LED to "listening"
// so the user knows the assistant is waiting for their answer.
class OnFollowupOpenedTrigger : public Trigger<> {
 public:
  explicit OnFollowupOpenedTrigger(VaClient *parent) {
    parent->add_on_followup_opened_trigger(this);
  }
};

}  // namespace va_client
}  // namespace esphome
