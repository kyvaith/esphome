#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include <cstdio>

constexpr uint32_t MALLOC_CAP_INTERNAL = 1, MALLOC_CAP_SPIRAM = 2, MALLOC_CAP_8BIT = 4;
struct Allocation {
  size_t size;
  uint32_t caps;
};
static std::unordered_map<void *, Allocation> allocated;
static std::vector<Allocation> attempts;
static bool external_available = true, internal_available = true;
static void *heap_caps_malloc_prefer(size_t size, unsigned num, uint32_t first, uint32_t second) {
  for (unsigned i = 0; i < num; ++i) {
    const uint32_t caps = i == 0 ? first : second;
    attempts.push_back({size, caps});
    if (((caps & MALLOC_CAP_INTERNAL) && !internal_available) || ((caps & MALLOC_CAP_SPIRAM) && !external_available))
      continue;
    void *pointer = std::malloc(size);
    if (pointer)
      allocated[pointer] = {size, caps};
    return pointer;
  }
  return nullptr;
}
static void test_free(void *pointer) {
  if (!pointer)
    return;
  assert(allocated.erase(pointer) == 1);
  std::free(pointer);
}
static void *heap_caps_realloc_prefer(void *old, size_t size, unsigned num, uint32_t first, uint32_t second) {
  void *next = heap_caps_malloc_prefer(size, num, first, second);
  if (next) {
    if (old)
      std::memcpy(next, old, std::min(size, allocated.at(old).size));
    test_free(old);
  }
  return next;
}
static size_t heap_caps_get_free_size(uint32_t) { return 1048576; }
static size_t heap_caps_get_largest_free_block(uint32_t) { return 1048576; }
template<class T> using RAMUniquePtr = std::unique_ptr<T>;
#define USE_ESP32
#define ESP_LOGE(...) ((void) 0)
#define ESP_LOGW(...) ((void) 0)
#define free test_free
#include "actual_download_buffer.inc"
#undef free

static void reset() {
  assert(allocated.empty());
  attempts.clear();
  external_available = internal_available = true;
}
static bool external(const DownloadBuffer &b) {
  return (allocated.at(const_cast<DownloadBuffer &>(b).data()).caps & MALLOC_CAP_SPIRAM) != 0;
}
static void fill(DownloadBuffer &b) {
  for (unsigned i = 0; i < 64; ++i)
    b.append()[i] = static_cast<uint8_t>(i);
  b.write(64);
}
static void check_data(DownloadBuffer &b, unsigned start, unsigned count) {
  assert(b.unread() == count);
  for (unsigned i = 0; i < count; ++i)
    assert(b.data()[i] == static_cast<uint8_t>(start + i));
}
int main() {
  reset();
  {
    auto b = actual_initial_buffer(262144, false);
    assert(b.size() == 32768 && !external(b));
    assert(attempts.front().caps & MALLOC_CAP_INTERNAL);
  }
  reset();
  {
    auto a = actual_initial_buffer(262144, true);
    auto b = actual_initial_buffer(786432, true);
    assert(a.size() == 32768 && b.size() == 32768 && external(a) && external(b));
    for (const auto &attempt : attempts)
      assert(attempt.caps & MALLOC_CAP_SPIRAM);
    fill(a);
    auto *old = a.data();
    assert(a.resize(262144) == 262144 && a.data() != old && external(a));
    check_data(a, 0, 64);
    assert(!allocated.count(old));
    a.read(12);
    check_data(a, 12, 52);
    old = a.data();
    assert(a.shrink(128) == 128 && external(a));
    check_data(a, 12, 52);
    assert(!allocated.count(old));
    assert(a.shrink(32) == 128);
    check_data(a, 12, 52);
    external_available = false;
    old = a.data();
    assert(a.resize(512) == 512 && !external(a));
    check_data(a, 12, 52);
    assert(!allocated.count(old));
    internal_available = false;
    old = a.data();
    assert(a.resize(1024) == 512 && a.data() == old);
    check_data(a, 12, 52);
    assert(a.shrink(64) == 512 && a.data() == old);
    check_data(a, 12, 52);
    external_available = true;
    assert(a.shrink(64) == 64 && external(a));
    check_data(a, 12, 52);
  }
  reset();
  external_available = false;
  {
    auto b = actual_initial_buffer(262144, true);
    assert(b.size() == 32768 && !external(b));
    assert(attempts.size() == 2);
    assert(attempts[0].caps & MALLOC_CAP_SPIRAM);
    assert(attempts[1].caps & MALLOC_CAP_INTERNAL);
  }
  reset();
  internal_available = false;
  {
    auto b = actual_initial_buffer(262144, false);
    assert(b.size() == 32768 && external(b));
    assert(attempts[0].caps & MALLOC_CAP_INTERNAL);
  }
  reset();
  external_available = internal_available = false;
  {
    auto b = actual_initial_buffer(262144, true);
    assert(b.size() == 0 && b.unread() == 0 && b.data() == nullptr);
  }
  reset();
  {
    auto b = actual_initial_buffer(256, true);
    assert(b.size() == 256 && external(b));
    const size_t count = attempts.size();
    assert(b.resize(128) == 256 && b.shrink(512) == 256 && attempts.size() == count);
  }
  reset();
  {
    auto a = actual_initial_buffer(262144, true, true);
    auto b = actual_initial_buffer(786432, true, true);
    assert(a.size() == 262144 && b.size() == 786432 && external(a) && external(b));
    assert(attempts.size() == 2 && allocated.size() == 2);
    assert(attempts[0].size == 262144 && attempts[1].size == 786432);
    auto *first = a.data();
    auto *second = b.data();
    fill(a);
    // Existing boot reservation is now a no-op, without any temporary 32 KiB block.
    assert(a.resize(262144) == 262144 && b.resize(786432) == 786432);
    assert(attempts.size() == 2 && a.data() == first && b.data() == second);
    check_data(a, 0, 64);
    assert(a.resize(524288) == 524288 && external(a) && !allocated.count(first));
    check_data(a, 0, 64);
    assert(a.shrink(128) == 128 && external(a));
    check_data(a, 0, 64);
  }
  reset();
  {
    auto b = actual_initial_buffer(4 * 1024 * 1024, true, true);
    assert(b.size() == 2 * 1024 * 1024 && attempts.size() == 1 && external(b));
  }
  reset();
  {
    auto b = actual_initial_buffer(256, true, true);
    assert(b.size() == 256 && external(b));
  }
  reset();
  {
    auto b = actual_initial_buffer(262144, false, true);
    assert(b.size() == 262144 && !external(b));
    assert(attempts.size() == 1 && (attempts[0].caps & MALLOC_CAP_INTERNAL));
  }
  reset();
  external_available = false;
  {
    auto b = actual_initial_buffer(786432, true, true);
    assert(b.size() == 786432 && !external(b) && attempts.size() == 2);
    assert(attempts[0].size == 786432 && attempts[1].size == 786432);
  }
  reset();
  external_available = internal_available = false;
  {
    auto b = actual_initial_buffer(262144, true, true);
    assert(b.size() == 0 && b.unread() == 0 && b.data() == nullptr);
    assert(attempts.size() == 2);
    external_available = true;
    assert(b.resize(262144) == 262144 && external(b));
    fill(b);
    check_data(b, 0, 64);
  }
  reset();
  std::puts("PASS actual RAMAllocator + DownloadBuffer: legacy, external, fallback, failure, growth/shrink data and "
            "ownership; preallocation cap, no temporary blocks, no-op reserve and independent allocator policy");
}
