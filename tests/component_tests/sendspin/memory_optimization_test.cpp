#include <algorithm>
#include <array>
#include <cassert>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#define SS_LOGW(...) ((void) 0)

namespace sendspin {
enum class SendspinCodecFormat { PCM, FLAC, OPUS };
struct AudioSupportedFormatObject {
  SendspinCodecFormat codec;
  uint8_t channels;
  uint32_t sample_rate;
  uint8_t bit_depth;
};
struct SharedRingLayout {
  static constexpr size_t align(size_t n) { return (n + 3) & ~size_t(3); }
  static constexpr size_t stored_size(size_t n) { return 8 + align(n); }
  static constexpr size_t max_item_size(size_t n) { return align(n / 2) - 8; }
};
struct InboundItemHeader {
  uint8_t pad[28];
  uint32_t data_len;
  uint32_t generation;
};
// Header layout on the 32-bit target is 32 bytes; fake item metadata lives separately below.
struct TargetInboundItemHeader {
  uint8_t bytes[32];
};
constexpr size_t AEAD_TAG_SIZE = 16;
constexpr size_t INBOUND_MAX_MESSAGE_BYTES = 65535;
constexpr size_t INBOUND_JSON_MESSAGE_BYTES = 16400;
constexpr size_t INBOUND_ITEM_STORED_OVERHEAD_BYTES = 59;
struct SendspinClientConfig {
  static constexpr size_t DEFAULT_BURST_SIZE = 8;
  static constexpr int DEFAULT_BURST_INTERVAL_MS = 10000;
};
}  // namespace sendspin

// Budget excerpts use the target header size, not the host's fake item metadata.
#define InboundItemHeader TargetInboundItemHeader
namespace sendspin {
#include "budget.inc"
}
#undef InboundItemHeader

namespace sendspin {
struct Item {
  InboundItemHeader header{};
  std::array<uint8_t, 262144> data{};
};
static const InboundItemHeader *inbound_item_header(void *item) { return &static_cast<Item *>(item)->header; }
static const uint8_t *inbound_item_data(void *item) { return static_cast<Item *>(item)->data.data(); }
static size_t live_bytes = 0, allocations = 0;
static bool fail_allocation = false;
enum class MemoryLocation { PREFER_EXTERNAL };
class PlatformBuffer {
 public:
  ~PlatformBuffer() { reset(); }
  bool allocate(size_t bytes, MemoryLocation) {
    reset();
    if (fail_allocation)
      return false;
    ptr_ = static_cast<uint8_t *>(std::malloc(bytes));
    assert(ptr_);
    size_ = bytes;
    live_bytes += bytes;
    ++allocations;
    return true;
  }
  void reset() {
    if (ptr_) {
      live_bytes -= size_;
      std::free(ptr_);
      ptr_ = nullptr;
      size_ = 0;
    }
  }
  uint8_t *data() { return ptr_; }
  size_t size() const { return size_; }

 private:
  uint8_t *ptr_{nullptr};
  size_t size_{0};
};
struct ArtworkAnnounce {
  int64_t timestamp{0};
  uint32_t total_size{0};
  uint32_t epoch{0};
};
struct ArtworkAssembly {
  enum class State { IDLE, ASSEMBLING, READY };
  PlatformBuffer buffer;
  int64_t timestamp{0};
  uint32_t total_size{0}, received{0}, epoch{0}, generation{0};
  State state{State::IDLE};
};
enum class SlotAckState { IDLE, DECODE_DELIVERED, PRESENTED };
struct SlotGate {
  SlotAckState ack_state{SlotAckState::IDLE};
  bool has_parked{false};
};
struct ArtworkDisplayUpdate {
  int64_t timestamps[4]{};
  uint32_t epochs[4]{};
  uint8_t valid_mask{0}, clear_mask{0};
};
static void merge_artwork_display_update(ArtworkDisplayUpdate &, ArtworkDisplayUpdate &&) {}
struct Atomic {
  uint32_t value{0};
  uint32_t load(std::memory_order) const { return value; }
};
struct Inbound {
  size_t returned{0};
  void return_item(void *) { ++returned; }
};
struct DrainTask {
  ArtworkAssembly assemblies[4];
  SlotGate slot_gates[4];
  std::mutex slot_mutex;
  Inbound inbound;
};
struct DisplaySlot {
  size_t published{0};
  ArtworkDisplayUpdate update{};
  uint32_t generation{0};
  template<typename F> void merge(F, ArtworkDisplayUpdate &&delta, uint32_t gen) {
    ++published;
    update = delta;
    generation = gen;
  }
};
struct EventState {
  DisplaySlot display_slot;
};
struct Listener {
  size_t calls{0}, bytes{0};
  bool stop_during_callback{false};
  Atomic *stream{nullptr};
  void on_image_decode(uint8_t, const uint8_t *data, size_t length, int) {
    assert(data && live_bytes >= length);
    for (size_t i = 0; i < length; ++i)
      assert(data[i] == 0xA5);
    ++calls;
    bytes = length;
    if (stop_during_callback)
      stream->value = 0;
  }
};
struct ArtworkRole {
  struct Impl {
    std::unique_ptr<DrainTask> drain_task{std::make_unique<DrainTask>()};
    std::unique_ptr<EventState> event_state{std::make_unique<EventState>()};
    Atomic slot_epochs[4];
    Atomic stream_active{1};
    Listener *listener{nullptr};
    bool gated[4]{};
    bool ack_enabled(uint8_t slot) const { return gated[slot]; }
    int image_format(uint8_t) const { return 0; }
    void begin_assembly(uint8_t slot, void *item);
    void add_part(uint8_t slot, void *item);
    void drop_assembly(uint8_t slot, bool release_delivered = false) const;
    void deliver(uint8_t slot);
  };
};
#include "artwork.inc"
}  // namespace sendspin

using namespace sendspin;
struct Fixture {
  ArtworkRole::Impl role;
  Listener listener;
  Fixture() {
    role.listener = &listener;
    listener.stream = &role.stream_active;
    for (auto &epoch : role.slot_epochs)
      epoch.value = 1;
#if !CONFIG_ESPHOME_SENDSPIN_MEMORY_OPTIMIZATIONS
    for (auto &assembly : role.drain_task->assemblies)
      assert(assembly.buffer.allocate(262144, MemoryLocation::PREFER_EXTERNAL));
#endif
  }
  void announce(uint32_t bytes, uint8_t slot = 0) {
    Item item;
    item.header.generation = 7;
    ArtworkAnnounce msg{123456, bytes, role.slot_epochs[slot].value};
    std::memcpy(item.data.data(), &msg, sizeof(msg));
    role.begin_assembly(slot, &item);
  }
  void part(size_t bytes, uint8_t slot = 0) {
    Item item;
    item.header.data_len = bytes;
    item.data.fill(0xA5);
    role.add_part(slot, &item);
  }
};

static void budget_case(int test) {
  InboundRingBudget budget;
  budget.audio_hold_bytes = 600000;
  budget.artwork_images_stored_bytes = inbound_artwork_image_stored_bytes(262144);
  budget.artwork_hold_bytes = budget.artwork_images_stored_bytes;
  assert(derive_inbound_ring_bytes(budget) == 1331472);
#if CONFIG_ESPHOME_SENDSPIN_MEMORY_OPTIMIZATIONS
  budget.audio_stored_bytes_per_second =
      inbound_min_audio_stored_bytes_per_second({{SendspinCodecFormat::PCM, 1, 48000, 16}});
  assert(budget.audio_stored_bytes_per_second == 96000);
  assert(derive_inbound_ring_bytes_preserving_audio_capacity(budget, 400000) == 1006864);
  assert(inbound_min_audio_stored_bytes_per_second(
             {{SendspinCodecFormat::PCM, 2, 48000, 16}, {SendspinCodecFormat::PCM, 1, 8000, 16}}) == 16000);
  for (auto codec : {SendspinCodecFormat::FLAC, SendspinCodecFormat::OPUS})
    assert(inbound_min_audio_stored_bytes_per_second({{codec, 1, 48000, 16}}) == 11600);
  for (auto rate : {0U, 8000U, 48000U, 192000U}) {
    if (rate == 0)
      assert(inbound_min_audio_stored_bytes_per_second({{SendspinCodecFormat::PCM, 1, rate, 16}}) == 11600);
  }
  assert(inbound_min_audio_stored_bytes_per_second({}) == 11600);
  assert(inbound_min_audio_stored_bytes_per_second({{SendspinCodecFormat::PCM, 1, 48000, 7}}) == 11600);
  if (test == 13) {
    for (size_t quota : {size_t(0), size_t(1000), size_t(600000), size_t(1000000), size_t(8000000)}) {
      for (size_t art : {size_t(0), size_t(262144), size_t(1048576)}) {
        budget.audio_hold_bytes = quota;
        budget.artwork_hold_bytes = budget.artwork_images_stored_bytes = inbound_artwork_image_stored_bytes(art);
        auto legacy = budget;
        legacy.audio_stored_bytes_per_second = 11600;
        size_t original = derive_inbound_ring_bytes(legacy);
        size_t improved = derive_inbound_ring_bytes_preserving_audio_capacity(budget, quota * 2 / 3);
        size_t old_limit = SharedRingLayout::max_item_size(original) - 32 - 16;
        size_t new_limit = SharedRingLayout::max_item_size(improved) - 32 - 16;
        assert(std::min(old_limit, quota * 2 / 3) <= std::min(new_limit, quota * 2 / 3));
        assert(improved <= original);
      }
    }
  }
#else
  (void) test;
#endif
}

int main(int argc, char **argv) {
  assert(argc == 2);
  int test = std::atoi(argv[1]);
  if (test >= 12)
    budget_case(test);
  else {
    Fixture f;
    auto &a = f.role.drain_task->assemblies[0];
    if (test == 0) {
#if CONFIG_ESPHOME_SENDSPIN_MEMORY_OPTIMIZATIONS
      assert(live_bytes == 0);
#else
      assert(live_bytes == 4 * 262144);
#endif
    } else if (test == 1 || test == 11) {
      size_t size = test == 11 ? 262144 : 101;
      f.announce(size);
      assert(a.state == ArtworkAssembly::State::ASSEMBLING);
      f.part(size);
      assert(f.listener.calls == 1 && f.listener.bytes == size);
      assert(f.role.event_state->display_slot.published == 1);
      assert(f.role.event_state->display_slot.update.timestamps[0] == 123456);
#if CONFIG_ESPHOME_SENDSPIN_MEMORY_OPTIMIZATIONS
      assert(live_bytes == 0);
#endif
    } else if (test == 2) {
      f.announce(100);
      f.part(40);
      assert(a.received == 40);
      assert(f.listener.calls == 0);
      assert(a.buffer.data());
      f.part(60);
      assert(f.listener.calls == 1);
    } else if (test == 3) {
      f.role.gated[0] = true;
      auto &gate = f.role.drain_task->slot_gates[0];
      gate.ack_state = SlotAckState::PRESENTED;
      f.announce(100);
      f.part(100);
      assert(a.state == ArtworkAssembly::State::READY && gate.has_parked);
      assert(f.listener.calls == 0 && a.buffer.data());
      gate.ack_state = SlotAckState::IDLE;
      f.role.deliver(0);
      assert(f.listener.calls == 1 && gate.ack_state == SlotAckState::DECODE_DELIVERED);
#if CONFIG_ESPHOME_SENDSPIN_MEMORY_OPTIMIZATIONS
      assert(live_bytes == 0);
#endif
    } else if (test == 4) {
      f.announce(100);
      f.part(30);
      f.role.drop_assembly(0);
      f.part(70);
      assert(a.state == ArtworkAssembly::State::IDLE && f.listener.calls == 0);
#if CONFIG_ESPHOME_SENDSPIN_MEMORY_OPTIMIZATIONS
      assert(live_bytes == 0);
#endif
    } else if (test == 5) {
      f.announce(100);
      f.part(30);
      f.announce(150);
      f.part(150);
      assert(f.listener.calls == 1 && f.listener.bytes == 150);
    } else if (test == 6) {
      fail_allocation = true;
      f.announce(100);
      fail_allocation = false;
      f.part(100);
#if CONFIG_ESPHOME_SENDSPIN_MEMORY_OPTIMIZATIONS
      assert(a.state == ArtworkAssembly::State::IDLE && f.listener.calls == 0 && live_bytes == 0);
#else
      assert(f.listener.calls == 1);
#endif
    } else if (test == 7) {
      f.announce(0);
      assert(f.listener.calls == 0);
      assert(f.role.event_state->display_slot.update.clear_mask == 1);
    } else if (test == 8) {
      f.announce(100);
      ++f.role.slot_epochs[0].value;
      f.part(100);
      assert(f.listener.calls == 0);
#if CONFIG_ESPHOME_SENDSPIN_MEMORY_OPTIMIZATIONS
      assert(live_bytes == 0);
#endif
    } else if (test == 9) {
      f.listener.stop_during_callback = true;
      f.announce(100);
      f.part(100);
      assert(f.listener.calls == 1 && f.role.event_state->display_slot.published == 0);
#if CONFIG_ESPHOME_SENDSPIN_MEMORY_OPTIMIZATIONS
      assert(live_bytes == 0);
#endif
    } else if (test == 10) {
      f.announce(100, 0);
      f.announce(200, 1);
      f.part(200, 1);
      assert(a.state == ArtworkAssembly::State::ASSEMBLING);
      f.part(100, 0);
      assert(f.listener.calls == 2);
    }
  }
  assert(live_bytes == 0);
  std::printf("mode %d case %d PASS\n", CONFIG_ESPHOME_SENDSPIN_MEMORY_OPTIMIZATIONS, test);
}
