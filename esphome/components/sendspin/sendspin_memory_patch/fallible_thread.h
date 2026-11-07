// Checked ESP-IDF worker creation. The caller owns this object until join completes.
#pragma once

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

#if CONFIG_ESPHOME_SENDSPIN_CHECKED_THREADS
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <esp_heap_caps.h>
#include <esp_pthread.h>
#include <pthread.h>

namespace sendspin {

// The trampoline borrows this address: moving even a completed, unjoined thread is forbidden.
class PlatformThread {
 public:
  PlatformThread() = default;
  PlatformThread(const PlatformThread &) = delete;
  PlatformThread &operator=(const PlatformThread &) = delete;
  PlatformThread(PlatformThread &&) = delete;
  PlatformThread &operator=(PlatformThread &&) = delete;
  ~PlatformThread() {
    if (this->joinable_)
      std::abort();  // Same lifetime contract as std::thread; the role must stop and join first.
  }

  template<auto Entry, typename T>
  bool start(T *context, const char *name, size_t stack_size, int priority, bool stack_in_psram) {
    this->clear_start_error();
    if (this->joinable_) {
      this->error_ = EINVAL;
      return false;
    }
    this->error_phase_ = 1;  // Configuration, including read-back; no pthread_create call yet.
    esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
    cfg.stack_size = stack_size;
    cfg.prio = priority;
    cfg.thread_name = name;
    if (stack_in_psram)
      cfg.stack_alloc_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    const esp_err_t configured = esp_pthread_set_cfg(&cfg);
    if (configured != ESP_OK) {
      this->error_ = configured == ESP_ERR_NO_MEM ? ENOMEM : EINVAL;
      return false;  // Never create with a previous or default configuration after rejection.
    }
    // IDF 5.5 ignores pthread_setspecific's return value in set_cfg(). Confirm
    // the actual caller-local configuration before task creation, even on ESP_OK.
    esp_pthread_cfg_t actual{};
    const esp_err_t retrieved = esp_pthread_get_cfg(&actual);
    if (retrieved != ESP_OK || actual.stack_size != cfg.stack_size || actual.prio != cfg.prio ||
        actual.thread_name != cfg.thread_name || actual.stack_alloc_caps != cfg.stack_alloc_caps ||
        actual.inherit_cfg != cfg.inherit_cfg || actual.pin_to_core != cfg.pin_to_core) {
      this->error_ =
          retrieved == ESP_OK || retrieved == ESP_ERR_NOT_FOUND || retrieved == ESP_ERR_NO_MEM ? ENOMEM : EINVAL;
      return false;
    }
    this->context_ = context;
    this->entry_ = [](void *arg) { Entry(static_cast<T *>(arg)); };
    this->error_phase_ = 2;  // Only this phase can own a pthread-create diagnostic.
    const int created = pthread_create(&this->handle_, nullptr, &PlatformThread::run_, this);
    if (created != 0) {
      this->context_ = nullptr;
      this->entry_ = nullptr;
      this->error_ = created;
      return false;
    }
    this->joinable_ = true;
    this->error_phase_ = 0;
    return true;
  }

  bool joinable() const { return this->joinable_; }
  void join() {
    if (!this->joinable_ || pthread_join(this->handle_, nullptr) != 0)
      std::abort();  // A broken join must not allow destruction of a still-borrowed owner.
    this->joinable_ = false;
    this->context_ = nullptr;
    this->entry_ = nullptr;
  }
  void clear_start_error() {
    this->error_ = 0;
    this->error_phase_ = 0;
  }
  int start_error() const { return this->error_; }
  uint8_t start_error_phase() const { return this->error_phase_; }
  bool start_retryable() const { return this->error_ == ENOMEM || this->error_ == EAGAIN; }

 private:
  static void *run_(void *arg) {
    auto *self = static_cast<PlatformThread *>(arg);
    self->entry_(self->context_);
    return nullptr;
  }
  pthread_t handle_{};
  void *context_{nullptr};
  void (*entry_)(void *){nullptr};
  int error_{0};
  bool joinable_{false};
  uint8_t error_phase_{0};
};

}  // namespace sendspin
#endif
