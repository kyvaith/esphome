#include <array>
#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#if TEST_DIAGNOSTICS
#define USE_ESP32_PREFERENCES_SYNC_DIAGNOSTICS
#endif

using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
constexpr size_t UINT32_MAX_STR_SIZE = 11;
constexpr const char *TAG = "preferences";
struct NVSData {
  uint32_t key;
  std::vector<uint8_t> data;
};
std::vector<NVSData> s_pending_save;
std::array<bool, 4> changed{};
std::array<esp_err_t, 4> write_errors{};
unsigned read_calls = 0, write_calls = 0, commit_calls = 0, info_calls = 0;
unsigned tick_calls = 0, timer_calls = 0;
uint32_t tick_begin = 123, tick_end = 128;
int64_t us_begin = 1234567890123, us_end = 1234567895123;
esp_err_t commit_error = ESP_OK;
std::string diagnostic_line;

uint32_t xTaskGetTickCount() {
  assert(tick_calls < 2);
  return tick_calls++ == 0 ? tick_begin : tick_end;
}
int64_t esp_timer_get_time() {
  assert(timer_calls < 2);
  return timer_calls++ == 0 ? us_begin : us_end;
}
void uint32_to_str(char *output, uint32_t value) { std::snprintf(output, UINT32_MAX_STR_SIZE, "%" PRIu32, value); }
const char *esp_err_to_name(esp_err_t) { return "fake-error"; }
template<typename... Args> void ignore_log(Args...) {}
template<typename... Args> void info_log(const char *, const char *format, Args... args) {
  // The post-sync line must execute after all NVS operations have returned.
  assert(commit_calls == 1 && s_pending_save.empty());
  assert(tick_calls == 2 && timer_calls == 2);
  char message[512];
  const int length = std::snprintf(message, sizeof(message), format, args...);
  assert(length > 0 && static_cast<size_t>(length) < sizeof(message));
  diagnostic_line = message;
  info_calls++;
}
#define ESP_LOGVV(...) ignore_log(__VA_ARGS__)
#define ESP_LOGV(...) ignore_log(__VA_ARGS__)
#define ESP_LOGD(...) ignore_log(__VA_ARGS__)
#define ESP_LOGE(...) ignore_log(__VA_ARGS__)
#define ESP_LOGI(...) info_log(__VA_ARGS__)

esp_err_t nvs_set_blob(uint32_t, const char *key, const uint8_t *, size_t) {
  assert(commit_calls == 0 && info_calls == 0);
  assert(timer_calls == TEST_DIAGNOSTICS && tick_calls == TEST_DIAGNOSTICS);
  write_calls++;
  return write_errors.at(std::strtoul(key, nullptr, 10) - 987654300);
}
esp_err_t nvs_commit(uint32_t) {
  assert(commit_calls == 0 && info_calls == 0 && s_pending_save.empty());
  assert(timer_calls == TEST_DIAGNOSTICS && tick_calls == TEST_DIAGNOSTICS);
  commit_calls++;
  return commit_error;
}
class ESP32Preferences {
 public:
  bool sync();
  bool is_changed_(uint32_t, const NVSData &save, const char *) {
    assert(commit_calls == 0 && info_calls == 0);
    assert(timer_calls == TEST_DIAGNOSTICS && tick_calls == TEST_DIAGNOSTICS);
    read_calls++;
    return changed.at(save.key - 987654300);
  }
  uint32_t nvs_handle = 1;
};

// pytest extracts the complete actual sync function; only NVS, clocks and
// logger are fakes. This preserves the production operation/return ordering.
#include "preferences_sync_under_test.inc"

int main(int argc, char **argv) {
  assert(argc == 2);
  const unsigned test = std::strtoul(argv[1], nullptr, 10);
  assert(test < 8);
  const unsigned count = test == 0 ? 0 : test == 5 ? 3 : 1;
  int cached = 0, written = 0, failed = 0;
  for (unsigned index = 0; index < count; index++) {
    s_pending_save.push_back({987654300 + index, {42, 17}});
    changed[index] = test != 1 && !(test == 5 && index == 0);
    write_errors[index] = test == 3 || (test == 5 && index == 2) ? 4354 : ESP_OK;
    if (!changed[index])
      cached++;
    else if (write_errors[index] != ESP_OK)
      failed++;
    else
      written++;
  }
  if (test == 4)
    commit_error = 4355;
  if (test == 6) {
    tick_begin = UINT32_MAX - 1;
    tick_end = 2;
  }
  if (test == 7) {
    us_begin = 987654321012345;
    us_end = us_begin + 5000000123;
  }
  ESP32Preferences preferences;
  assert(preferences.sync() == (commit_error == ESP_OK && failed == 0));
  assert(s_pending_save.empty());
  assert(read_calls == count && write_calls == static_cast<unsigned>(written + failed));
  assert(commit_calls == (count != 0));
  const unsigned diagnostics = TEST_DIAGNOSTICS && count != 0;
  assert(info_calls == diagnostics && tick_calls == diagnostics * 2 && timer_calls == diagnostics * 2);
  if (diagnostics) {
    char expected[512];
    std::snprintf(expected, sizeof(expected),
                  "Sync diagnostics: tick_begin=%" PRIu32 " tick_end=%" PRIu32 " tick_span=%" PRIu32
                  " us_begin=%" PRIi64 " us_end=%" PRIi64 " duration_us=%" PRIi64
                  " cached=%d written=%d failed=%d commit=%d",
                  tick_begin, tick_end, static_cast<uint32_t>(tick_end - tick_begin), us_begin, us_end,
                  us_end - us_begin, cached, written, failed, commit_error);
    assert(diagnostic_line == expected);
    assert(diagnostic_line.find("987654300") == std::string::npos);
    assert(diagnostic_line.find("fake-error") == std::string::npos);
  }
  std::printf("case %u PASS\n", test);
}
