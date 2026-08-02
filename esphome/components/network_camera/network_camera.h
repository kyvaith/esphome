#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "esphome/components/image/image.h"
#include "esphome/components/select/select.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

#ifdef USE_ESP_IDF
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace esphome::network_camera {

class NetworkCameraSourceSelect;

enum class StreamState : uint8_t {
  IDLE,
  CONNECTING,
  STREAMING,
  RETRY_WAIT,
  ERROR,
};

struct StreamSource {
  std::string name;
  std::string url;
  std::vector<std::pair<std::string, std::string>> headers;
};

class NetworkCamera : public Component, public image::Image {
 public:
  NetworkCamera();
  ~NetworkCamera();

  void setup() override;
  void loop() override;
  void dump_config() override;
  void on_shutdown() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  void add_source(const std::string &name, const std::string &url);
  void add_source_header(size_t source_index, const std::string &name, const std::string &value);
  void set_max_frame_size(size_t size) { this->max_frame_size_ = size; }
  void set_max_dimensions(uint16_t width, uint16_t height) {
    this->max_width_ = width;
    this->max_height_ = height;
  }
  void set_frame_interval(uint32_t interval_ms) { this->frame_interval_ms_ = interval_ms; }
  void set_reconnect_interval(uint32_t interval_ms) { this->reconnect_interval_ms_ = interval_ms; }
  void set_request_timeout(uint32_t timeout_ms) { this->request_timeout_ms_ = timeout_ms; }
  void set_release_buffer_on_stop(bool release) { this->release_buffer_on_stop_ = release; }
  void set_task_core(int8_t core) { this->task_core_ = core; }
  void set_task_priority(uint8_t priority) { this->task_priority_ = priority; }
  void set_task_stack_size(uint32_t size) { this->task_stack_size_ = size; }
  void set_max_runtime_sources(size_t count) { this->max_runtime_sources_ = count; }
  void set_source_select(NetworkCameraSourceSelect *source_select) { this->source_select_ = source_select; }

  void start();
  void stop();
  void next_source();
  void previous_source();
  void select_source(size_t index);
  bool replace_sources(const std::vector<std::string> &names, const std::vector<std::string> &urls);

  bool is_running() const { return this->running_requested_.load(std::memory_order_acquire); }
  bool has_frame() const { return this->generation_.load(std::memory_order_acquire) != 0; }
  size_t source_count() const;
  size_t active_source_index() const { return this->active_source_.load(std::memory_order_acquire); }
  std::string source_name(size_t index) const;
  std::vector<std::string> source_names() const;
  std::string active_source_name() const;
  const char *state_name() const;
  float measured_fps() const { return this->measured_fps_.load(std::memory_order_relaxed); }
  uint32_t decoded_frames() const { return this->decoded_frames_.load(std::memory_order_relaxed); }
  uint32_t dropped_frames() const { return this->dropped_frames_.load(std::memory_order_relaxed); }
  uint32_t reconnects() const { return this->reconnects_.load(std::memory_order_relaxed); }
  uint32_t last_decode_us() const { return this->last_decode_us_.load(std::memory_order_relaxed); }
  size_t memory_usage_bytes() const;

  BufferWriter get_buffer_writer() const override { return BufferWriter::DMA; }
  bool acquire_buffer(image::ImageBufferLease *lease) const override;
  bool release_buffer(image::ImageBufferLease *lease) const override;

  template<typename F> void add_on_first_frame_callback(F &&callback) {
    this->first_frame_callback_.add(std::forward<F>(callback));
  }
  template<typename F> void add_on_state_callback(F &&callback) {
    this->state_callback_.add(std::forward<F>(callback));
  }
  template<typename F> void add_on_source_callback(F &&callback) {
    this->source_callback_.add(std::forward<F>(callback));
  }

 protected:
#ifdef USE_ESP_IDF
  static void stream_task_trampoline_(void *arg);
  void stream_task_();
  bool connect_stream_(const StreamSource &source);
  void disconnect_stream_();
  bool read_stream_();
  bool consume_stream_bytes_(const uint8_t *data, size_t size);
  bool finish_jpeg_frame_();
  bool decode_jpeg_frame_(const uint8_t *data, size_t size);
  bool ensure_frame_buffer_(uint32_t width, uint32_t height, size_t stride, size_t required_size);
  void release_frame_buffer_();
  void wait_for_task_wakeup_(uint32_t timeout_ms);
#endif
  void set_state_(StreamState state);
  void queue_source_event_();
  void reset_parser_();
  bool get_source_(size_t index, StreamSource *source) const;

  mutable Mutex source_mutex_{};
  std::vector<StreamSource> sources_{};
  size_t max_runtime_sources_{16};
  size_t max_frame_size_{512 * 1024};
  uint16_t max_width_{1920};
  uint16_t max_height_{1080};
  uint32_t frame_interval_ms_{67};
  uint32_t reconnect_interval_ms_{2000};
  uint32_t request_timeout_ms_{3000};
  int8_t task_core_{-1};
  uint8_t task_priority_{5};
  uint32_t task_stack_size_{8192};
  bool release_buffer_on_stop_{true};

  mutable Mutex frame_mutex_{};
  uint8_t *frame_buffer_{nullptr};
  size_t frame_capacity_{0};
  size_t frame_size_{0};
  size_t frame_stride_{0};
  uint32_t frame_width_{0};
  uint32_t frame_height_{0};
  mutable uint16_t frame_readers_{0};
  bool frame_decoding_{false};
  std::atomic<uint32_t> generation_{0};

  std::atomic<bool> running_requested_{false};
  std::atomic<bool> shutdown_requested_{false};
  std::atomic<size_t> active_source_{0};
  std::atomic<uint32_t> source_revision_{0};
  std::atomic<StreamState> state_{StreamState::IDLE};
  std::atomic<StreamState> reported_state_{StreamState::ERROR};
  std::atomic<bool> first_frame_pending_{false};
  std::atomic<bool> awaiting_first_frame_{true};
  std::atomic<bool> source_event_pending_{false};

  std::atomic<uint32_t> decoded_frames_{0};
  std::atomic<uint32_t> dropped_frames_{0};
  std::atomic<uint32_t> reconnects_{0};
  std::atomic<uint32_t> last_decode_us_{0};
  std::atomic<float> measured_fps_{0.0f};
  uint32_t fps_window_started_ms_{0};
  uint32_t fps_window_frames_{0};
  uint32_t last_frame_ms_{0};

  LazyCallbackManager<void()> first_frame_callback_{};
  LazyCallbackManager<void(std::string)> state_callback_{};
  LazyCallbackManager<void(std::string)> source_callback_{};
  NetworkCameraSourceSelect *source_select_{nullptr};

#ifdef USE_ESP_IDF
  TaskHandle_t stream_task_handle_{nullptr};
  esp_http_client_handle_t http_client_{nullptr};
  uint8_t *encoded_buffer_{nullptr};
  uint8_t *read_buffer_{nullptr};
  size_t read_buffer_size_{8192};
  size_t encoded_size_{0};
  bool jpeg_started_{false};
  bool previous_byte_ff_{false};
#endif
};

class NetworkCameraSourceSelect final : public select::Select, public Parented<NetworkCamera> {
 protected:
  void control(size_t index) override { this->parent_->select_source(index); }
};

template<typename... Ts> class NetworkCameraStartAction : public Action<Ts...>, public Parented<NetworkCamera> {
 public:
  void play(const Ts &...x) override { this->parent_->start(); }
};

template<typename... Ts> class NetworkCameraStopAction : public Action<Ts...>, public Parented<NetworkCamera> {
 public:
  void play(const Ts &...x) override { this->parent_->stop(); }
};

template<typename... Ts> class NetworkCameraNextAction : public Action<Ts...>, public Parented<NetworkCamera> {
 public:
  void play(const Ts &...x) override { this->parent_->next_source(); }
};

template<typename... Ts> class NetworkCameraPreviousAction : public Action<Ts...>, public Parented<NetworkCamera> {
 public:
  void play(const Ts &...x) override { this->parent_->previous_source(); }
};

template<typename... Ts> class NetworkCameraSelectAction : public Action<Ts...>, public Parented<NetworkCamera> {
 public:
  TEMPLATABLE_VALUE(uint16_t, index)
  void play(const Ts &...x) override { this->parent_->select_source(this->index_.value(x...)); }
};

template<typename... Ts>
class NetworkCameraReplaceSourcesAction : public Action<Ts...>, public Parented<NetworkCamera> {
 public:
  TEMPLATABLE_VALUE(std::vector<std::string>, names)
  TEMPLATABLE_VALUE(std::vector<std::string>, urls)

  void play(const Ts &...x) override {
    auto names = this->names_.value(x...);
    auto urls = this->urls_.value(x...);
    this->parent_->replace_sources(names, urls);
  }
};

}  // namespace esphome::network_camera
