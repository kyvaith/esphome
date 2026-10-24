#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <vector>

#define USE_ESP_IDF
#define ESP_LOGE(...) ((void) 0)
#define ESP_LOGW(...) ((void) 0)
#define ESP_LOGI(...) ((void) 0)
constexpr uint32_t MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2, MALLOC_CAP_INTERNAL = 4;
using BaseType_t = int;
using StackType_t = uint32_t;
using TaskHandle_t = void *;
struct StaticTask_t {};
constexpr int pdPASS = 1;
struct Mutex {};
struct LockGuard { explicit LockGuard(const Mutex &) {} };

std::map<void *, size_t> allocations;
std::vector<std::string> operations;
unsigned allocation_calls = 0, failed_calls = 0, free_calls = 0, notifications = 0;
std::vector<unsigned> failed_at;
bool read_inflight = false;
void *heap_caps_malloc(size_t size, uint32_t) {
  allocation_calls++;
  if (std::find(failed_at.begin(), failed_at.end(), allocation_calls) != failed_at.end())
    return nullptr;
  if (failed_calls > 0) {
    failed_calls--;
    return nullptr;
  }
  void *pointer = std::malloc(size);
  assert(pointer != nullptr);
  allocations[pointer] = size;
  operations.emplace_back("allocate");
  return pointer;
}
void *heap_caps_aligned_alloc(size_t, size_t size, uint32_t caps) { return heap_caps_malloc(size, caps); }
void heap_caps_free(void *pointer) {
  assert(!read_inflight);
  assert(allocations.erase(pointer) == 1);
  operations.emplace_back("free");
  free_calls++;
  std::free(pointer);
}
uint32_t millis() { return 0; }
int xPortGetCoreID() { return 0; }
int xTaskGetCoreID(TaskHandle_t) { return 0; }
unsigned uxTaskGetStackHighWaterMark(void *) { return 8192; }
bool esp_ptr_external_ram(void *) { return true; }
int xTaskNotifyGive(TaskHandle_t) { notifications++; return 0; }
TaskHandle_t xTaskCreateStaticPinnedToCore(void (*)(void *), const char *, uint32_t, void *, uint8_t,
                                         StackType_t *, StaticTask_t *, int) {
  return reinterpret_cast<void *>(1);
}
int xTaskCreatePinnedToCore(void (*)(void *), const char *, uint32_t, void *, uint8_t, TaskHandle_t *handle, int) {
  *handle = reinterpret_cast<void *>(1);
  return pdPASS;
}
namespace esp32 { int background_task_core(int value) { return value; } }
namespace esp32_jpeg { void preallocate_decoder(int) {} }
namespace network { bool is_connected() { return true; } }
void esp_http_client_close(void *) { assert(!read_inflight); operations.emplace_back("close"); }
void esp_http_client_cleanup(void *) { operations.emplace_back("cleanup"); }

enum class StreamState { IDLE, CONNECTING, STREAMING, RETRY_WAIT, ERROR };
struct StreamSource {};
class NetworkCamera {
 public:
  ~NetworkCamera();
  void setup();
  void stop();
  size_t memory_usage_bytes() const;
  void reset_parser_();
  void disconnect_stream_();
  bool ensure_input_buffers_();
  void release_input_buffers_();
  void stream_task_();
  static void stream_task_trampoline_(void *) {}
  void mark_failed() { failed = true; }
  void queue_source_event_() {}
  void release_frame_buffer_() { operations.emplace_back("release_frame"); frame_capacity_ = 0; }
  void set_state_(StreamState value) { state = value; }
  bool get_source_(size_t, StreamSource *) { return true; }
  bool connect_stream_(const StreamSource &) {
    assert(encoded_buffer_ != nullptr && read_buffer_ != nullptr);
    operations.emplace_back("connect");
    http_client_ = reinterpret_cast<void *>(2);
    connects++;
    return true;
  }
  bool read_stream_() {
    assert(encoded_buffer_ != nullptr && read_buffer_ != nullptr);
    operations.emplace_back("read");
    read_inflight = true;
    if (on_read) on_read();
    // stop() may signal while HTTP/parser/consumer still holds both buffers.
    assert(encoded_buffer_ != nullptr && read_buffer_ != nullptr);
    read_inflight = false;
    return true;
  }
  void wait_for_task_wakeup_(uint32_t) { if (on_wait) on_wait(); }

  Mutex source_mutex_{}, frame_mutex_{};
  std::vector<StreamSource> sources_{};
  size_t max_runtime_sources_{16}, max_frame_size_{768000}, read_buffer_size_{8192};
  uint8_t *encoded_buffer_{nullptr}, *read_buffer_{nullptr};
  std::atomic<size_t> input_buffer_bytes_{0};
  std::atomic<bool> input_allocation_failed_{false};
  size_t encoded_size_{0}, frame_capacity_{0};
  bool jpeg_started_{false}, previous_byte_ff_{false};
  bool release_buffer_on_stop_{true}, release_input_buffers_on_stop_{false};
  std::atomic<bool> shutdown_requested_{false}, running_requested_{false};
  std::atomic<uint32_t> source_revision_{0}, reconnects_{0};
  std::atomic<size_t> active_source_{0};
  uint32_t reconnect_interval_ms_{2}, request_timeout_ms_{3};
  uint32_t input_fps_window_started_ms_{0}, fps_window_started_ms_{0};
  int8_t task_core_{0};
  int resolved_task_core_{0};
  uint8_t task_priority_{1};
  uint32_t task_stack_size_{8192};
  bool task_stack_in_psram_{false}, failed{false};
  TaskHandle_t stream_task_handle_{nullptr};
  StackType_t *stream_task_stack_{nullptr};
  StaticTask_t stream_task_storage_{};
  void *http_client_{nullptr};
  StreamState state{StreamState::IDLE};
  unsigned connects{0};
  std::function<void()> on_wait, on_read;
};

#include "camera_input_actual.inc"

int main(int argc, char **argv) {
  assert(argc == 2);
  const std::string test = argv[1];
  {
    NetworkCamera camera;
    if (test == "setup_lazy") {
      camera.release_input_buffers_on_stop_ = true;
      camera.setup();
      assert(!camera.failed && allocation_calls == 0 && camera.input_buffer_bytes_ == 0);
    } else if (test == "setup_default") {
      camera.setup();
      assert(!camera.failed && allocation_calls == 2 && camera.memory_usage_bytes() == 776192);
    } else if (test == "setup_default_failure") {
      failed_calls = 2;
      camera.setup();
      assert(camera.failed && camera.stream_task_handle_ == nullptr && allocations.empty());
    } else if (test == "rollback_read") {
      // Successful JPEG allocation followed by both HTTP-buffer allocation failures.
      failed_at = {2, 3};
      assert(!camera.ensure_input_buffers_());
      assert(allocation_calls == 3 && free_calls == 1 && allocations.empty());
      assert(camera.encoded_buffer_ == nullptr && camera.read_buffer_ == nullptr);
      assert(camera.memory_usage_bytes() == 0);
      assert(camera.ensure_input_buffers_());
      assert(camera.memory_usage_bytes() == 776192);
    } else if (test == "fallback") {
      failed_calls = 1;
      assert(camera.ensure_input_buffers_());
      assert(allocation_calls == 3 && camera.memory_usage_bytes() == 776192);
    } else if (test == "reuse") {
      assert(camera.ensure_input_buffers_());
      auto *encoded = camera.encoded_buffer_;
      auto *read = camera.read_buffer_;
      assert(camera.ensure_input_buffers_() && allocation_calls == 2);
      assert(camera.encoded_buffer_ == encoded && camera.read_buffer_ == read);
    } else if (test == "stop_deferred") {
      assert(camera.ensure_input_buffers_());
      camera.http_client_ = reinterpret_cast<void *>(2);
      camera.running_requested_ = true;
      camera.stream_task_handle_ = reinterpret_cast<void *>(1);
      read_inflight = true;
      camera.stop();
      assert(camera.encoded_buffer_ != nullptr && camera.read_buffer_ != nullptr);
      assert(!camera.running_requested_ && notifications == 1 && free_calls == 0);
      read_inflight = false;
    } else if (test == "worker_close" || test == "worker_default") {
      camera.release_input_buffers_on_stop_ = test == "worker_close";
      camera.running_requested_ = true;
      camera.on_read = [&]() { camera.stop(); };
      camera.on_wait = [&]() {
        assert(camera.state == StreamState::IDLE && camera.http_client_ == nullptr);
        assert(camera.encoded_size_ == 0 && !camera.jpeg_started_);
        assert(camera.memory_usage_bytes() == (test == "worker_close" ? 0U : 776192U));
        camera.shutdown_requested_ = true;
      };
      camera.stream_task_();
      assert(camera.connects == 1 && camera.memory_usage_bytes() == 0);
      auto close = std::find(operations.begin(), operations.end(), "close");
      auto freed = std::find(operations.begin(), operations.end(), "free");
      assert(close < freed);
    } else if (test == "retry") {
      camera.release_input_buffers_on_stop_ = true;
      camera.running_requested_ = true;
      failed_calls = 2;
      camera.on_wait = [&]() {
        if (camera.state == StreamState::RETRY_WAIT) {
          assert(camera.input_allocation_failed_ && !camera.failed && allocations.empty());
          assert(camera.connects == 0);
        } else {
          camera.shutdown_requested_ = true;
        }
      };
      camera.on_read = [&]() { assert(!camera.input_allocation_failed_); camera.stop(); };
      camera.stream_task_();
      assert(camera.connects == 1 && !camera.failed && allocations.empty());
    } else if (test == "shutdown") {
      assert(camera.ensure_input_buffers_());
      camera.http_client_ = reinterpret_cast<void *>(2);
      camera.shutdown_requested_ = true;
      camera.stream_task_();
      assert(allocations.empty() && camera.http_client_ == nullptr && camera.memory_usage_bytes() == 0);
    } else if (test == "destructor") {
      assert(camera.ensure_input_buffers_());
      camera.http_client_ = reinterpret_cast<void *>(2);
    } else if (test == "accounting") {
      camera.frame_capacity_ = 123;
      assert(camera.memory_usage_bytes() == 123);
      assert(camera.ensure_input_buffers_());
      assert(camera.memory_usage_bytes() == 776315);
      camera.release_input_buffers_();
      assert(camera.memory_usage_bytes() == 123);
    } else if (test == "accounting_concurrent") {
      std::atomic<bool> done{false};
      std::thread diagnostics([&]() {
        while (!done.load()) {
          const size_t bytes = camera.memory_usage_bytes();
          assert(bytes == 0 || bytes == 776192);
        }
      });
      for (unsigned iteration = 0; iteration < 100; iteration++) {
        assert(camera.ensure_input_buffers_());
        camera.release_input_buffers_();
      }
      done = true;
      diagnostics.join();
    } else {
      assert(false);
    }
    read_inflight = false;
  }
  assert(allocations.empty());
  return 0;
}
