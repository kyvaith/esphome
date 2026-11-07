#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

using TickType_t = uint32_t;
using UBaseType_t = uint32_t;
using BaseType_t = int;
using esp_err_t = int;
static constexpr int ESP_OK = 0, ESP_ERR_NO_MEM = 1, ESP_ERR_NOT_SUPPORTED = 2, ESP_ERR_INVALID_ARG = 3,
                     ESP_ERR_INVALID_STATE = 4;
static constexpr int pdTRUE = 1, pdPASS = 1, RINGBUF_TYPE_BYTEBUF = 0;
static constexpr TickType_t portMAX_DELAY = UINT32_MAX;
#define pdMS_TO_TICKS(x) (x)
#define ESPDEPRECATED(...)
#define ESPHOME_LOG_TAG(name, ...) static constexpr const char *name = "test"
#define ESP_LOGD(...)
#define ESP_LOGV(...)
#define ESP_LOGE(...)
#define ESP_LOGCONFIG(...)
#define LOG_STR(s) (s)
#define ESPHOME_ABORT_WITH_REASON(s) std::abort()

namespace host {
inline uint32_t now = 100;
inline int locks = 0, rings_created = 0, rings_deleted = 0, items_returned = 0;
inline bool fail_storage = false, fail_ring_init = false;
inline int fail_nothrow_after = -1;
inline std::function<void()> writing, destroying, delaying, pcm;
struct Parked {};
}  // namespace host

struct StaticRingbuffer_t {
  uint8_t *data{};
  size_t capacity{}, head{}, used{}, acquired{};
  bool alive{};
};
using RingbufHandle_t = StaticRingbuffer_t *;
inline RingbufHandle_t xRingbufferCreateStatic(size_t size, int, uint8_t *data, StaticRingbuffer_t *ring) {
  if (host::fail_ring_init || size == 0)
    return nullptr;
  *ring = {data, size, 0, 0, 0, true};
  host::rings_created++;
  return ring;
}
inline void vRingbufferDelete(RingbufHandle_t ring) {
  assert(host::locks == 0 && ring->alive && ring->acquired == 0);
  if (host::destroying)
    host::destroying();
  ring->alive = false;
  host::rings_deleted++;
}
inline int xRingbufferSend(RingbufHandle_t ring, const void *data, size_t length, TickType_t) {
  assert(host::locks == 0 && ring->alive);
  if (host::writing)
    host::writing();
  assert(ring->alive);
  if (length > ring->capacity - ring->used)
    return 0;
  for (size_t i = 0; i < length; i++)
    ring->data[(ring->head + ring->used + i) % ring->capacity] = static_cast<const uint8_t *>(data)[i];
  ring->used += length;
  return 1;
}
inline void *xRingbufferReceiveUpTo(RingbufHandle_t ring, size_t *length, TickType_t, size_t maximum) {
  assert(ring->alive && ring->acquired == 0);
  *length = std::min({ring->used, maximum, ring->capacity - ring->head});
  ring->acquired = *length;
  return *length == 0 ? nullptr : ring->data + ring->head;
}
inline void vRingbufferReturnItem(RingbufHandle_t ring, void *item) {
  assert(ring->alive && ring->acquired != 0 && item == ring->data + ring->head);
  ring->used -= ring->acquired;
  ring->head = (ring->head + ring->acquired) % ring->capacity;
  ring->acquired = 0;
  host::items_returned++;
}
inline void vRingbufferGetInfo(RingbufHandle_t ring, void *, void *, void *, void *, UBaseType_t *used) {
  assert(ring->alive);
  *used = ring->used;
}
inline size_t xRingbufferGetCurFreeSize(RingbufHandle_t ring) { return ring->capacity - ring->used; }
using EventGroupHandle_t = uint32_t *;
inline EventGroupHandle_t xEventGroupCreate() {
  static uint32_t groups[64]{};
  static size_t count = 0;
  assert(count < 64);
  return &groups[count++];
}
inline uint32_t xEventGroupGetBits(EventGroupHandle_t group) { return *group; }
inline uint32_t xEventGroupSetBits(EventGroupHandle_t group, uint32_t bits) { return *group |= bits; }
inline uint32_t xEventGroupClearBits(EventGroupHandle_t group, uint32_t bits) {
  const auto old = *group;
  *group &= ~bits;
  return old;
}
inline void vTaskDelay(TickType_t ticks) {
  host::now += ticks;
  if (host::delaying)
    host::delaying();
}
inline void vTaskSuspend(void *) { throw host::Parked{}; }

namespace esphome {
template<typename T> using optional = std::optional<T>;
using std::make_unique;
class Mutex {
 public:
  void lock() {
    mutex_.lock();
    host::locks++;
  }
  void unlock() {
    host::locks--;
    mutex_.unlock();
  }

 private:
  std::mutex mutex_;
};
class LockGuard {
 public:
  explicit LockGuard(Mutex &mutex) : mutex_(mutex) { mutex_.lock(); }
  ~LockGuard() { mutex_.unlock(); }

 private:
  Mutex &mutex_;
};
template<typename T> struct RAMAllocator {
  enum { NONE, PREFER_INTERNAL };
  explicit RAMAllocator(int = NONE) {}
  T *allocate(size_t size) { return host::fail_storage ? nullptr : static_cast<T *>(malloc(size * sizeof(T))); }
  T *reallocate(T *data, size_t size) { return static_cast<T *>(realloc(data, size * sizeof(T))); }
  void deallocate(T *data, size_t) { free(data); }
};
#include "actual_fixed_vector.h"
class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
  virtual void dump_config() {}
  void disable_loop() {}
  void enable_loop_soon_any_context() {}
  void mark_failed() { error_ = true; }
  void status_set_error(const char *) { error_ = true; }
  void status_clear_error() { error_ = false; }
  void status_momentary_error(uint32_t) { error_ = true; }
  bool status_has_error() const { return error_; }

 protected:
  bool error_{};
};
struct Application {
  void wake_loop_threadsafe() {}
};
inline Application App;
inline uint32_t millis() { return host::now; }
inline void delay(uint32_t ms) { vTaskDelay(ms); }
class StaticTask {
 public:
  bool is_created() const { return created_; }
  bool create(void (*)(void *), const char *, uint32_t, void *, UBaseType_t, bool) { return created_ = true; }
  bool deallocate() {
    created_ = false;
    return true;
  }

 private:
  bool created_{};
};
}  // namespace esphome

namespace esp_audio_libs::gain {
class GainRamp {
 public:
  void process(uint8_t *, uint8_t, uint32_t) {}
  void set_target_db_reduction_over(uint8_t, uint32_t) {}
};
}  // namespace esp_audio_libs::gain
namespace esp_audio_libs::pcm_convert {
inline void copy_frames(const uint8_t *, uint8_t *, uint8_t, uint8_t, uint8_t, uint8_t, uint32_t) {
  assert(host::locks == 0);
  if (host::pcm)
    host::pcm();
}
}  // namespace esp_audio_libs::pcm_convert
namespace esp_audio_libs::mixer {
inline void mix_frames(const uint8_t *, uint8_t, uint8_t, const uint8_t *, uint8_t, uint8_t, uint8_t *, uint8_t,
                       uint8_t, uint32_t) {
  assert(host::locks == 0);
  if (host::pcm)
    host::pcm();
}
}  // namespace esp_audio_libs::mixer
