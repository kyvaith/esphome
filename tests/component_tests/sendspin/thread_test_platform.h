#pragma once
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_ERR_NO_MEM = 0x101, ESP_ERR_INVALID_ARG = 0x102, ESP_ERR_NOT_FOUND = 0x105;
constexpr uint32_t MALLOC_CAP_8BIT = 4, MALLOC_CAP_INTERNAL = 2048, MALLOC_CAP_SPIRAM = 1024;
struct esp_pthread_cfg_t {
  size_t stack_size = 0;
  int prio = 0;
  bool inherit_cfg = false;
  const char *thread_name = nullptr;
  int pin_to_core = -1;
  uint32_t stack_alloc_caps = 0;
};
struct ThreadTestState {
  int config_error = 0, create_error = 0, read_error = 0;
  bool lose_config = false, mismatch_config = false;
  std::string fail_name;
  int creates = 0, joins = 0, entries = 0, waits = 0, bound = 0;
  esp_pthread_cfg_t installed{};
  std::vector<std::string> trace;
  std::vector<std::string> handles;
};
inline ThreadTestState test;
inline esp_pthread_cfg_t esp_pthread_get_default_config() {
  return {3072, 5, false, nullptr, -1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT};
}
inline esp_err_t esp_pthread_set_cfg(const esp_pthread_cfg_t *cfg) {
  if (test.config_error && (test.fail_name.empty() || test.fail_name == cfg->thread_name))
    return test.config_error;
  if (!test.lose_config)
    test.installed = *cfg;
  return ESP_OK;
}
inline esp_err_t esp_pthread_get_cfg(esp_pthread_cfg_t *cfg) {
  if (test.read_error)
    return test.read_error;
  if (!test.installed.thread_name)
    return ESP_ERR_NOT_FOUND;
  *cfg = test.installed;
  if (test.mismatch_config)
    ++cfg->stack_size;
  return ESP_OK;
}
using TestPthread = unsigned;
#define pthread_t TestPthread
#define pthread_create test_pthread_create
#define pthread_join test_pthread_join
inline int test_pthread_create(TestPthread *handle, const void *, void *(*entry)(void *), void *arg) {
  ++test.creates;
  auto name = std::string(test.installed.thread_name);
  test.trace.push_back("create:" + name);
  if (test.create_error && (test.fail_name.empty() || test.fail_name == name))
    return test.create_error;
  test.handles.push_back(name);
  *handle = test.handles.size();
  entry(arg);  // Prove a trampoline can complete before pthread_create returns.
  return 0;
}
inline int test_pthread_join(TestPthread handle, void **) {
  assert(handle > 0 && handle <= test.handles.size());
  ++test.joins;
  test.trace.push_back("join:" + test.handles[handle - 1]);
  return 0;
}
