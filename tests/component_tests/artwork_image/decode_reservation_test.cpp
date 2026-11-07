#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#define USE_ESP32_JPEG
#define ESP_LOGW(...) ((void) 0)
#define ESP_LOGI(...) ((void) 0)
#define ESP_LOGD(...) ((void) 0)

#include "actual_constants.inc"
static uint32_t millis() { return 10000; }
struct LockGuard {
  explicit LockGuard(int &) {}
};
static void register_dma_written_buffer(uint8_t *, size_t, bool) {}

namespace esp32_jpeg {
struct Allocation {
  uint8_t *data{nullptr};
  size_t size{0};
};
static Allocation live[32];
static size_t allocations = 0, frees = 0;

static bool is_live(uint8_t *data) {
  for (auto &entry : live) {
    if (entry.data == data && data != nullptr)
      return true;
  }
  return false;
}

static uint8_t *allocate_decode_output(size_t size, size_t *capacity) {
  for (auto &entry : live) {
    if (entry.data != nullptr)
      continue;
    entry.data = static_cast<uint8_t *>(std::malloc(size));
    assert(entry.data != nullptr);
    entry.size = size;
    *capacity = size;
    entry.data[0] = 0x5A;
    allocations++;
    return entry.data;
  }
  assert(false && "Host fixture exhausted its live-pointer table");
  return nullptr;
}

static void release_decode_output(uint8_t *data) {
  for (auto &entry : live) {
    if (entry.data != data || data == nullptr)
      continue;
    assert(data[0] == 0x5A);
    std::free(data);
    entry = {};
    frees++;
    return;
  }
  assert(false && "Double free or non-owned allocation");
}
}  // namespace esp32_jpeg

// Only storage declarations and platform boundaries are stand-ins. The ten
// ownership/reservation method bodies below are extracted unchanged from source.
class ArtworkImage {
 public:
  struct RetiredBuffer {
    uint8_t *data;
    size_t size;
    uint32_t retired_at;
    bool jpeg_allocator;
  };
  struct Allocator {
    void deallocate(uint8_t *data, size_t) { std::free(data); }
  } allocator_;
  uint8_t *buffer_ = nullptr, *decode_buffer_ = nullptr, *spare_buffer_ = nullptr;
  uint8_t *secondary_spare_buffer_ = nullptr, *pinned_ = nullptr;
  size_t buffer_capacity_ = 0, decode_buffer_capacity_ = 0, spare_buffer_size_ = 0;
  size_t secondary_spare_buffer_size_ = 0;
  bool buffer_uses_jpeg_allocator_ = false, decode_buffer_uses_jpeg_allocator_ = false;
  bool spare_buffer_uses_jpeg_allocator_ = false, secondary_spare_buffer_uses_jpeg_allocator_ = false;
  size_t reserved_decode_buffer_capacity_ = 0;
  uint8_t reserved_decode_buffer_count_ = 1;
  int retired_buffer_mutex_ = 0;
  std::vector<RetiredBuffer> retired_buffers_;
  bool is_busy_() const { return false; }
  bool buffer_is_pinned_(const uint8_t *data) const { return data == this->pinned_; }
  bool reserve_decode_buffer_capacity(size_t, uint8_t = 1);
  size_t compatible_decode_buffer_count_(size_t, const uint8_t *exclude = nullptr) const;
  bool store_spare_buffer_(uint8_t *, size_t, bool);
  bool should_keep_spare_buffer_(size_t, bool) const;
  void release_unused_decode_capacity();
  void cleanup_retired_buffers_(bool, bool);
  void release_spare_buffer_();
  void release_buffer_(uint8_t *, size_t, bool);
  uint8_t *loan_decode_buffer_capacity(size_t, size_t *);
  bool return_decode_buffer_capacity(uint8_t *, size_t);
};

#include "actual_reservation.inc"

static void test_smaller_reservation_after_release() {
  constexpr size_t LARGE = 3657600;
  constexpr size_t SMALL = 3145728;
  ArtworkImage owner;
  assert(owner.reserve_decode_buffer_capacity(LARGE));
  size_t capacity = 0;
  auto *loan = owner.loan_decode_buffer_capacity(LARGE, &capacity);
  assert(loan != nullptr && capacity == LARGE);
  assert(owner.return_decode_buffer_capacity(loan, capacity));
  owner.release_unused_decode_capacity();
  assert(!esp32_jpeg::is_live(loan));
  // The old implementation allocates SMALL successfully, then rejects and frees
  // it solely because the already-released LARGE minimum remained in metadata.
  assert(owner.reserve_decode_buffer_capacity(SMALL));
  assert(owner.spare_buffer_size_ == SMALL);
  owner.release_unused_decode_capacity();
  assert(owner.reserved_decode_buffer_capacity_ == 0 && owner.reserved_decode_buffer_count_ == 1);
}

static void test_two_buffer_request_ends_with_pool_lifetime() {
  ArtworkImage owner;
  assert(owner.reserve_decode_buffer_capacity(1280000, 2));
  assert(owner.spare_buffer_ != nullptr && owner.secondary_spare_buffer_ != nullptr);
  owner.release_unused_decode_capacity();
  assert(owner.spare_buffer_ == nullptr && owner.secondary_spare_buffer_ == nullptr);
  assert(owner.reserved_decode_buffer_capacity_ == 0 && owner.reserved_decode_buffer_count_ == 1);
  size_t before = esp32_jpeg::allocations;
  assert(owner.reserve_decode_buffer_capacity(512000));
  assert(esp32_jpeg::allocations == before + 1 && owner.secondary_spare_buffer_ == nullptr);
  owner.release_unused_decode_capacity();
  owner.release_unused_decode_capacity();  // Idempotent, with no new allocation.
  assert(esp32_jpeg::allocations == before + 1);
}

static void test_active_decode_and_pinned_generations_survive() {
  ArtworkImage owner;
  size_t capacity;
  owner.buffer_ = esp32_jpeg::allocate_decode_output(3145728, &capacity);
  owner.buffer_capacity_ = capacity;
  owner.buffer_uses_jpeg_allocator_ = true;
  owner.decode_buffer_ = esp32_jpeg::allocate_decode_output(3145728, &capacity);
  owner.decode_buffer_capacity_ = capacity;
  owner.decode_buffer_uses_jpeg_allocator_ = true;
  auto *pinned = esp32_jpeg::allocate_decode_output(3657600, &capacity);
  owner.retired_buffers_.push_back({pinned, capacity, 0, true});
  owner.pinned_ = pinned;
  auto *unused = esp32_jpeg::allocate_decode_output(3657600, &capacity);
  owner.retired_buffers_.push_back({unused, capacity, 0, true});
  owner.reserved_decode_buffer_capacity_ = 3657600;
  owner.spare_buffer_ = esp32_jpeg::allocate_decode_output(3657600, &capacity);
  owner.spare_buffer_size_ = capacity;
  owner.spare_buffer_uses_jpeg_allocator_ = true;
  size_t before = esp32_jpeg::allocations;
  owner.release_unused_decode_capacity();
  assert(esp32_jpeg::allocations == before);
  assert(esp32_jpeg::is_live(owner.buffer_) && esp32_jpeg::is_live(owner.decode_buffer_));
  assert(esp32_jpeg::is_live(pinned) && !esp32_jpeg::is_live(unused));
  assert(owner.retired_buffers_.size() == 1 && owner.spare_buffer_ == nullptr);
  assert(owner.reserved_decode_buffer_capacity_ == 0 && owner.reserved_decode_buffer_count_ == 1);
  owner.pinned_ = nullptr;
  owner.cleanup_retired_buffers_(false, false);
  assert(!esp32_jpeg::is_live(pinned) && owner.retired_buffers_.empty());
  owner.release_buffer_(owner.buffer_, owner.buffer_capacity_, true);
  owner.release_buffer_(owner.decode_buffer_, owner.decode_buffer_capacity_, true);
}

static void test_outstanding_loan_remains_owned_until_return() {
  ArtworkImage owner;
  assert(owner.reserve_decode_buffer_capacity(3657600));
  size_t capacity = 0;
  auto *loan = owner.loan_decode_buffer_capacity(3657600, &capacity);
  owner.release_unused_decode_capacity();
  assert(esp32_jpeg::is_live(loan));
  // The loan owner can still use its bytes. Returning it after the reservation
  // ended safely releases it instead of silently reinstating the old large pool.
  assert(loan[0] == 0x5A);
  assert(!owner.return_decode_buffer_capacity(loan, capacity));
  assert(!esp32_jpeg::is_live(loan));
}

int main() {
  test_smaller_reservation_after_release();
  test_two_buffer_request_ends_with_pool_lifetime();
  test_active_decode_and_pinned_generations_survive();
  test_outstanding_loan_remains_owned_until_return();
  assert(esp32_jpeg::allocations == esp32_jpeg::frees);
  std::printf("PASS4 reservation lifecycle cases; allocations=%zu frees=%zu\n", esp32_jpeg::allocations,
              esp32_jpeg::frees);
}
