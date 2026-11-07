#pragma once

#include <cassert>
#include <cinttypes>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "esphome/components/sendspin/start_retry.h"

#define LOG_STR(value) value
#define ESP_LOGI(tag, format, ...) test_log(format __VA_OPT__(,) __VA_ARGS__)
#define ESP_LOGW(tag, format, ...) test_log(format __VA_OPT__(,) __VA_ARGS__)
#define ESP_LOGE(tag, format, ...) test_log(format __VA_OPT__(,) __VA_ARGS__)

namespace esphome::sendspin_ {

template<typename... Args> void test_log(const char *, Args...) {}

struct TestApp {
  uint32_t now{0};
  uint32_t get_loop_component_start_time() const { return this->now; }
};
inline TestApp App;

// Only the external client/status/clock seams are facades. The three Hub methods
// and retry state machine below are compiled from the component under test.
struct StartOutcome {
  bool success;
  bool resource_failure;
};

struct TestClient {
  std::vector<StartOutcome> outcomes;
  size_t starts{0};
  size_t stops{0};
  size_t loops{0};
  bool running{false};
  bool retryable{false};
  bool is_started() const { return this->running; }
  bool start() {
    assert(this->starts < this->outcomes.size());
    const auto result = this->outcomes[this->starts++];
    this->retryable = result.resource_failure;
    this->running = result.success;
    return result.success;
  }
  bool start_failure_retryable() const { return this->retryable; }
  void stop() {
    ++this->stops;
    this->running = false;
  }
  void loop() { ++this->loops; }
};

class SendspinHub {
 public:
  explicit SendspinHub(TestClient &client) : client_(&client) {}
  void loop();
  void set_enabled(bool enabled);
  void on_shutdown();
  bool is_client_running() const { return this->client_->is_started(); }
  bool status_has_error() const { return this->error; }
  void status_set_error(const char *message) {
    this->error = true;
    this->error_message = message;
  }
  void status_clear_error() { this->error = false; }
  void status_set_warning(const char *message) {
    this->warning = true;
    this->warning_message = message;
  }
  void status_clear_warning() { this->warning = false; }

  TestClient *client_;
  std::optional<bool> enabled_;
  std::optional<bool> unpaired_access_{true};
  SendspinStartRetry start_retry_;
  bool error{false};
  bool warning{false};
  std::string error_message;
  std::string warning_message;
};

}  // namespace esphome::sendspin_
