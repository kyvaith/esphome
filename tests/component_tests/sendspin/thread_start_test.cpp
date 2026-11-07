#include <algorithm>
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>
#include "thread_test_platform.h"
#include "platform/fallible_thread.h"
#include "esphome/components/sendspin/start_retry.h"

#define SS_LOGW(...) ((void) 0)
#define SS_LOGE(...) ((void) 0)
#define SENDSPIN_ENABLE_PLAYER
#define SENDSPIN_ENABLE_ARTWORK
#define SENDSPIN_ENABLE_VISUALIZER
#define SENDSPIN_ENABLE_SOURCE

namespace sendspin {
struct InboundRing {};
enum class InboundHolder { PLAYER, ARTWORK, VISUALIZER };
struct Items {
  void clear_signals() {}
  void wake_receiver() {}
  void signal(int) {}
};
struct Consumer {
  bool bound = false;
  Items item;
  bool bind(InboundRing *, InboundHolder) {
    assert(!bound);
    bound = true;
    ++test.bound;
    return true;
  }
  void unbind() {
    assert(bound);
    bound = false;
    --test.bound;
    test.trace.push_back("unbind");
  }
  Items &items() { return item; }
};
struct Flags {
  bool created = true;
  bool is_created() const { return created; }
  bool create() {
    created = true;
    return true;
  }
  void clear_all() {}
  void set(int) {}
  void clear(int) {}
  void wait(int, bool, bool, uint32_t) { ++test.waits; }
};
struct ProtocolTask {
  using Tick = std::function<uint32_t()>;
  using ClientStateMessage = int;
  static constexpr int COMMAND_STOP = 1;
  PlatformThread thread_;
  Flags event_flags_;
  Tick tick_;
  std::mutex command_mutex_;
  std::optional<int> latest_state_;
  bool start(Tick, size_t, unsigned, bool);
  void stop();
  static void thread_entry(ProtocolTask *) { ++test.entries; }
  void reset_start_failure() { thread_.clear_start_error(); }
  bool start_failure_retryable() const { return thread_.start_retryable(); }
  void drop_commands() { test.trace.push_back("drop"); }
  void publish_state(int value) { latest_state_ = value; }
  void close_accepts() {}
  void wake() {}
};
enum EventGroupBits { TASK_IDLE = 1, TASK_STOPPED = 2, COMMAND_STOP = 4, TASK_RUNNING = 8 };
struct SyncTask {
  static constexpr size_t SYNC_TASK_STACK_SIZE = 6144;
  PlatformThread sync_thread_;
  Consumer inbound_;
  Flags event_flags_;
  bool initialized = true;
  bool is_initialized() const { return initialized; }
  bool start(InboundRing *, bool, unsigned);
  void stop();
  static void thread_entry(void *) { ++test.entries; }
  void reset_start_failure() { sync_thread_.clear_start_error(); }
  bool start_failure_retryable() const { return sync_thread_.start_retryable(); }
};
enum class SendspinCodecFormat { PCM, OPUS };
struct SourceRoleConfig {
  static constexpr size_t DEFAULT_OPUS_SOURCE_TASK_STACK_SIZE = 12288, DEFAULT_SOURCE_TASK_STACK_SIZE = 8192;
  SendspinCodecFormat codec = SendspinCodecFormat::PCM;
  int buffer_location = 0;
  unsigned priority = 5;
  bool psram_stack = true;
};
constexpr int OUTBOUND_RING_ITEM_COUNT = 4;
inline size_t source_capture_ring_bytes(const SourceRoleConfig &) { return 1; }
inline size_t derive_outbound_ring_bytes(size_t, int) { return 1; }
struct OutboundRing {
  bool create(size_t, int) { return true; }
  void wake_consumer() {}
};
enum SourceTaskBits { SOURCE_TASK_IDLE = 1, SOURCE_TASK_STOPPED = 2, SOURCE_COMMAND_STOP = 4 };
struct SourceRoleData {
  SourceRoleConfig config;
};
struct SourceTask {
  PlatformThread task_thread_;
  ProtocolTask *protocol_task_ = nullptr;
  SourceRoleData *role_;
  Flags event_flags_;
  std::unique_ptr<OutboundRing> capture_ring_, outbound_ring_;
  int *opus_encoder_ = nullptr;
  void *capture_item_ = nullptr, *chunk_item_ = nullptr;
  int encoder_generation_ = 0;
  bool opus_warm_ = false;
  size_t chunk_message_bytes_ = 1;
  bool start(ProtocolTask *);
  void stop();
  static void thread_entry(void *) { ++test.entries; }
  void reset_start_failure() { task_thread_.clear_start_error(); }
  bool start_failure_retryable() const { return task_thread_.start_retryable(); }
};
struct PlatformBuffer {
  bool allocated = false;
  bool allocate(size_t, int) {
    allocated = true;
    return true;
  }
};
struct ArtworkAssembly {
  PlatformBuffer buffer;
};
struct DrainTask {
  Consumer inbound;
  PlatformThread drain_thread;
  std::array<ArtworkAssembly, 2> assemblies;
};
struct ArtworkRole {
  struct Impl {
    std::unique_ptr<DrainTask> drain_task = std::make_unique<DrainTask>();
    struct {
      unsigned priority = 5;
      bool psram_stack = true;
    } config;
    bool start(InboundRing *);
    bool signal_stop() const;
    void stop() const;
    static void drain_thread_func(Impl *) { ++test.entries; }
    static constexpr int COMMAND_STOP = 1;
  };
  std::unique_ptr<Impl> impl_ = std::make_unique<Impl>();
};
enum class VisualizerDataType { SPECTRUM };
struct VisualizerStreamConfig {
  int rate_max = 1;
  std::vector<VisualizerDataType> types;
  std::optional<int> spectrum;
};
struct VisualizerRole {
  struct Impl {
    std::unique_ptr<DrainTask> drain_task = std::make_unique<DrainTask>();
    struct {
      VisualizerStreamConfig stream;
      unsigned priority = 5;
      bool psram_stack = true;
    } config;
    struct {
      size_t buffer_capacity = 100;
    } visualizer_support;
    static constexpr size_t MIN_BUFFER_CAPACITY = 1;
    static constexpr int COMMAND_STOP = 1;
    bool start(InboundRing *);
    bool signal_stop() const;
    void stop() const;
    static void drain_thread_func(Impl *) { ++test.entries; }
  };
  std::unique_ptr<Impl> impl_ = std::make_unique<Impl>();
};
struct PlayerRole {
  struct Impl {
    std::unique_ptr<SyncTask> sync_task = std::make_unique<SyncTask>();
    bool invalid = false;
    bool start(void *, InboundRing *r) { return !invalid && sync_task->start(r, false, 5); }
    void stop() { sync_task->stop(); }
  };
  std::unique_ptr<Impl> impl_ = std::make_unique<Impl>();
};
struct SourceRole {
  struct Impl {
    SourceRoleData data;
    std::unique_ptr<SourceTask> task = std::make_unique<SourceTask>();
    Impl() { task->role_ = &data; }
    bool start(ProtocolTask *p) { return task->start(p); }
    void stop() { task->stop(); }
  };
  std::unique_ptr<Impl> impl_ = std::make_unique<Impl>();
};
struct Config {
  struct PSK {
    std::array<uint8_t, 32> bytes{};
  };
  std::optional<PSK> pairing_psk;
  std::optional<std::string> static_pairing_code;
  size_t protocol_task_stack_size = 7168;
  unsigned protocol_task_priority = 5;
  bool protocol_task_psram_stack = true;
  static constexpr size_t DEFAULT_PROTOCOL_TASK_STACK_SIZE = 7168;
};
using SendspinClientConfig = Config;
constexpr int STATIC_PAIRING_CODE_DIGITS = 6;
inline bool is_valid_static_pairing_code(const std::string &s) { return s == "123456"; }
struct RecordStore {
  RecordStore(void *, const Config &) {}
  bool has_pending_writes() { return false; }
  static bool is_usable_pairing_psk(const std::array<uint8_t, 32> &) { return false; }
};
struct ConnectionManager {
  bool open = false;
  void start() {
    open = true;
    test.trace.push_back("server-start");
  }
  void close_admission() { test.trace.push_back("close-admission"); }
  int finish_stop() {
    open = false;
    test.trace.push_back("server-stop");
    return 0;
  }
};
enum class LifecycleState { RUNNING, STOPPING, STOPPED };
struct SendspinClient {
  std::atomic<LifecycleState> lifecycle_{LifecycleState::STOPPED};
  Config config_;
  bool start_failure_retryable_ = false;
  std::unique_ptr<RecordStore> record_store_;
  std::unique_ptr<int> identity_;
  void *identity_provider_ = nullptr, *persistence_provider_ = nullptr;
  std::unique_ptr<InboundRing> inbound_ring_;
  std::unique_ptr<ProtocolTask> protocol_task_ = std::make_unique<ProtocolTask>();
  std::unique_ptr<PlayerRole> player_ = std::make_unique<PlayerRole>();
  std::unique_ptr<VisualizerRole> visualizer_ = std::make_unique<VisualizerRole>();
  std::unique_ptr<ArtworkRole> artwork_ = std::make_unique<ArtworkRole>();
  std::unique_ptr<SourceRole> source_ = std::make_unique<SourceRole>();
  std::unique_ptr<ConnectionManager> connection_manager_ = std::make_unique<ConnectionManager>();
  bool start();
  void stop_role_threads();
  bool load_or_generate_identity() {
    identity_ = std::make_unique<int>(1);
    return true;
  }
  void request_persist() {}
  void load_last_played_server() {}
  void poll_network_provider() {}
  int build_client_state() { return 1; }
  uint32_t protocol_tick() { return 0; }
  bool create_inbound_ring() {
    assert(!inbound_ring_);
    inbound_ring_ = std::make_unique<InboundRing>();
    return true;
  }
  void release_inbound_ring() {
    assert(test.bound == 0);
    test.trace.push_back("ring-release");
    inbound_ring_.reset();
  }
  void stop_test() {
    connection_manager_->close_admission();
    protocol_task_->stop();
    connection_manager_->finish_stop();
    stop_role_threads();
    release_inbound_ring();
    lifecycle_ = LifecycleState::STOPPED;
  }
};
#include "actual_start_methods.inc"
}  // namespace sendspin

static void probe_entry(int *v) { ++*v; }
static void primitive() {
  using sendspin::PlatformThread;
  static_assert(!std::is_move_constructible_v<PlatformThread> && !std::is_copy_constructible_v<PlatformThread>);
  for (int error : {ESP_ERR_INVALID_ARG, ESP_ERR_NO_MEM}) {
    test = {};
    test.config_error = error;
    PlatformThread t;
    int called = 0;
    assert(!t.start<probe_entry>(&called, "probe", 7168, 5, true));
    assert(!t.joinable() && called == 0 && test.creates == 0);
    assert(t.start_retryable() == (error == ESP_ERR_NO_MEM));
    assert(t.start_error_phase() == 1);
  }
  for (int mode = 0; mode < 3; ++mode) {
    test = {};
    test.lose_config = mode == 0;
    test.mismatch_config = mode == 1;
    test.read_error = mode == 2 ? ESP_ERR_NO_MEM : 0;
    PlatformThread t;
    int called = 0;
    assert(!t.start<probe_entry>(&called, "probe", 7168, 5, true));
    assert(t.start_retryable() && test.creates == 0 && called == 0);
    assert(t.start_error_phase() == 1);
  }
  for (int error : {ENOMEM, EAGAIN, EINVAL}) {
    test = {};
    test.create_error = error;
    PlatformThread t;
    int called = 0;
    assert(!t.start<probe_entry>(&called, "probe", 7168, 5, true));
    assert(!t.joinable() && called == 0);
    assert(t.start_retryable() == (error != EINVAL));
    assert(t.start_error_phase() == 2);
    test.create_error = 0;
    assert(t.start<probe_entry>(&called, "probe", 7168, 5, true));
    assert(called == 1 && t.joinable() && t.start_error() == 0);
    assert(t.start_error_phase() == 0);
    assert(test.installed.stack_size == 7168 && test.installed.prio == 5);
    assert(test.installed.stack_alloc_caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    assert(!t.start<probe_entry>(&called, "probe", 7168, 5, true));
    assert(!t.start_retryable());
    t.join();
    assert(!t.joinable() && test.joins == 1);
  }
}
static void lifecycle() {
  const std::array<const char *, 5> names = {"SsSync", "SsVis", "SsArt", "SsSrc", "SsProto"};
  for (size_t position = 0; position < names.size(); ++position)
    for (int kind = 0; kind < 3; ++kind) {
      test = {};
      test.fail_name = names[position];
      if (kind == 0)
        test.config_error = ESP_ERR_INVALID_ARG;
      else
        test.create_error = kind == 1 ? ENOMEM : EAGAIN;
      sendspin::SendspinClient c;
      assert(!c.start());
      assert(c.lifecycle_ == sendspin::LifecycleState::STOPPED);
      assert(c.start_failure_retryable_ == (kind != 0));
      assert(test.joins == int(position));
      assert(test.entries == int(position));
      assert(test.bound == 0 && !c.inbound_ring_ && !c.connection_manager_->open);
      assert(!c.source_->impl_->task->outbound_ring_);
      assert(!c.protocol_task_->latest_state_);
      if (position == 4) {
        auto close = std::find(test.trace.begin(), test.trace.end(), "close-admission");
        auto stop = std::find(test.trace.begin(), test.trace.end(), "server-stop");
        auto drop = std::find(stop, test.trace.end(), "drop");
        auto release = std::find(test.trace.begin(), test.trace.end(), "ring-release");
        assert(close < stop && stop < drop && drop < release);
      }
      c.config_.static_pairing_code = "bad";
      assert(!c.start() && !c.start_failure_retryable_);
      c.config_.static_pairing_code.reset();
      test.config_error = 0;
      test.create_error = 0;
      assert(c.start());
      assert(c.lifecycle_ == sendspin::LifecycleState::RUNNING && !c.start_failure_retryable_);
      c.stop_test();
      assert(test.bound == 0 && test.joins == int(position) + 5);
      assert(c.start());
      c.stop_test();
      assert(test.bound == 0 && test.joins == int(position) + 10);
    }
  test = {};
  sendspin::SendspinClient c;
  c.protocol_task_->event_flags_.created = false;
  assert(!c.start() && !c.start_failure_retryable_);
  assert(test.bound == 0 && test.joins == 4);
  assert(!c.protocol_task_->latest_state_ && !c.inbound_ring_ && !c.connection_manager_->open);
}
static void retry_policy() {
  using esphome::sendspin_::SendspinStartRetry;
  SendspinStartRetry r;
  assert(r.ready(0));
  assert(!r.failed(0, true) && r.terminal());
  r.reset();
  r.set_limit(4);
  uint32_t now = 0xfffffe00;
  assert(r.ready(now));
  for (int i = 0; i < 4; ++i) {
    assert(r.failed(now, true));
    const auto delay = r.retry_delay();
    assert(!r.ready(now + delay - 1) && r.ready(now + delay));
    now += delay;
  }
  assert(!r.failed(now, true) && !r.ready(now + 100000));
  r.reset();
  assert(r.ready(now));
  assert(!r.failed(now, false) && r.terminal());
  r.reset();
  assert(r.failed(now, true));
  r.reset();
  assert(!r.pending() && r.attempts() == 0 && r.ready(now));
}
int main() {
  primitive();
  lifecycle();
  retry_policy();
}
