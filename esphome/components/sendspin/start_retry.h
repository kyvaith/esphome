#pragma once

#include <cstdint>

namespace esphome::sendspin_ {

// A finite startup series, advanced only after a real failed attempt. No scheduler
// allocation, periodic work, heap-capacity guess or delay before the first start.
class SendspinStartRetry {
 public:
  void set_limit(uint8_t limit) { this->limit_ = limit; }
  bool enabled() const { return this->limit_ != 0; }
  bool ready(uint32_t now) const {
    return !this->terminal_ && (!this->pending_ || now - this->failed_at_ >= this->delay_ms_());
  }
  bool failed(uint32_t now, bool retryable) {
    if (!retryable || this->failures_ >= this->limit_) {
      this->pending_ = false;
      this->terminal_ = true;
      return false;
    }
    ++this->failures_;
    this->failed_at_ = now;
    this->pending_ = true;
    return true;
  }
  void reset() {
    this->failures_ = 0;
    this->pending_ = false;
    this->terminal_ = false;
  }
  bool pending() const { return this->pending_; }
  bool terminal() const { return this->terminal_; }
  uint8_t attempts() const { return this->failures_; }
  uint32_t retry_delay() const { return this->delay_ms_(); }

 protected:
  uint32_t delay_ms_() const { return this->failures_ <= 1 ? 1000 : this->failures_ == 2 ? 2000 : 4000; }
  uint32_t failed_at_{0};
  uint8_t limit_{0};
  uint8_t failures_{0};
  bool pending_{false};
  bool terminal_{false};
};

}  // namespace esphome::sendspin_
