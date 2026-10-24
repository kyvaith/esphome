#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#define PPA_ALIGN_DOWN(value, alignment) ((value) & ~((uintptr_t) (alignment) - 1U))
#define PPA_ALIGN_UP(value, alignment) (((value) + (alignment) - 1U) & ~((uintptr_t) (alignment) - 1U))

using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t SYNC_FAILURE = 7;
constexpr int ESP_CACHE_MSYNC_FLAG_INVALIDATE = 1;
constexpr int ESP_CACHE_MSYNC_FLAG_UNALIGNED = 2;
constexpr int ESP_CACHE_MSYNC_FLAG_DIR_C2M = 4;
constexpr int ESP_CACHE_MSYNC_FLAG_DIR_M2C = 8;

struct Call {
  uintptr_t address;
  size_t size;
  int flags;
};
static std::vector<Call> calls;
static Call owner_check{};
static bool skip_owned;
static bool sync_fails;

bool esp_ptr_external_ram(const void *buffer) {
  const auto address = reinterpret_cast<uintptr_t>(buffer);
  return address >= 0x48000000U && address < 0x4A000000U;
}

esp_err_t esp_cache_msync(void *buffer, size_t size, int flags) {
  calls.push_back({reinterpret_cast<uintptr_t>(buffer), size, flags});
  return sync_fails ? SYNC_FAILURE : ESP_OK;
}

#include "actual_ppa_cache_helper.inc"

bool esphome_lvgl_ppa_skip_cache_msync(const void *buffer, size_t size, int flags) {
  owner_check = {reinterpret_cast<uintptr_t>(buffer), size, flags};
  return skip_owned;
}

int main() {
  // Ownership is checked before alignment can expand into a neighboring lease.
  skip_owned = true;
  assert(ppa_cache_msync_external_window(0x48000040U, 192, 128, ESP_CACHE_MSYNC_FLAG_DIR_M2C, true) == ESP_OK);
  assert(owner_check.address == 0x48000040U && owner_check.size == 192 && calls.empty());

  skip_owned = false;
  assert(ppa_cache_msync_external_window(0x48000040U, 192, 64, ESP_CACHE_MSYNC_FLAG_DIR_M2C, false) == ESP_OK);
  assert(calls.size() == 1 && calls[0].address == 0x48000040U && calls[0].size == 192);
  assert(calls[0].flags == ESP_CACHE_MSYNC_FLAG_DIR_M2C);

  // A partial destination row preserves dirty neighboring pixels before invalidation.
  calls.clear();
  assert(ppa_cache_msync_external_window(
             0x480000C0U, 96, 128, ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_UNALIGNED, true) == ESP_OK);
  assert(calls.size() == 2);
  for (const auto &call : calls)
    assert(call.address == 0x48000080U && call.size == 256);
  assert(calls[0].flags == ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  assert(calls[1].flags == ESP_CACHE_MSYNC_FLAG_DIR_M2C);

  calls.clear();
  assert(ppa_cache_msync_external_window(
             0x48000040U, 64, 64, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_INVALIDATE, true) == ESP_OK);
  assert(calls.size() == 2 && calls[0].flags == ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  assert(calls[1].flags == (ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_INVALIDATE));

  calls.clear();
  sync_fails = true;
  assert(ppa_cache_msync_external_window(0x48000040U, 64, 64, ESP_CACHE_MSYNC_FLAG_DIR_M2C, true) == SYNC_FAILURE);
  assert(calls.size() == 1);  // No invalidation after a failed preserving writeback.
  sync_fails = false;
  calls.clear();
  assert(ppa_cache_msync_external_window(0, 64, 64, ESP_CACHE_MSYNC_FLAG_DIR_M2C, true) == ESP_OK);
  assert(ppa_cache_msync_external_window(0x48000040U, 0, 64, ESP_CACHE_MSYNC_FLAG_DIR_M2C, true) == ESP_OK);
  assert(ppa_cache_msync_external_window(0x48000040U, 64, 0, ESP_CACHE_MSYNC_FLAG_DIR_M2C, true) == ESP_OK);
  assert(ppa_cache_msync_external_window(0x40000000U, 64, 64, ESP_CACHE_MSYNC_FLAG_DIR_M2C, true) == ESP_OK);
  assert(calls.empty());
  std::puts("PASS: actual PPA helper ownership-before-alignment,64/128B ranges, dirty preservation, flags and failure");
}
