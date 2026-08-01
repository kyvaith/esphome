#include "network_camera.h"

#include <algorithm>
#include <cinttypes>
#include <cstring>

#include "esphome/components/esp32_jpeg/esp32_jpeg.h"
#include "esphome/components/network/util.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#ifdef USE_ESP_IDF
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#endif

namespace esphome::network_camera {

static const char *const TAG = "network_camera";

namespace {

const char *state_to_string(StreamState state) {
  switch (state) {
    case StreamState::IDLE:
      return "idle";
    case StreamState::CONNECTING:
      return "connecting";
    case StreamState::STREAMING:
      return "streaming";
    case StreamState::RETRY_WAIT:
      return "retrying";
    case StreamState::ERROR:
      return "error";
  }
  return "unknown";
}

uint32_t horizontal_mcu_alignment(esp32_jpeg::DownSampling sampling) {
  switch (sampling) {
    case esp32_jpeg::DownSampling::YUV420:
    case esp32_jpeg::DownSampling::YUV422:
      return 16;
    case esp32_jpeg::DownSampling::YUV444:
    case esp32_jpeg::DownSampling::GRAY:
      return 8;
  }
  return 16;
}

uint32_t align_up(uint32_t value, uint32_t alignment) { return (value + alignment - 1U) / alignment * alignment; }

}  // namespace

NetworkCamera::NetworkCamera() : image::Image(nullptr, 0, 0, image::IMAGE_TYPE_RGB, image::TRANSPARENCY_OPAQUE) {}

NetworkCamera::~NetworkCamera() {
#ifdef USE_ESP_IDF
  this->disconnect_stream_();
  if (this->encoded_buffer_ != nullptr)
    heap_caps_free(this->encoded_buffer_);
  this->encoded_buffer_ = nullptr;
  if (this->read_buffer_ != nullptr)
    heap_caps_free(this->read_buffer_);
  this->read_buffer_ = nullptr;
  this->release_frame_buffer_();
#endif
}

void NetworkCamera::add_source(const std::string &name, const std::string &url) {
  this->sources_.push_back({name, url, {}});
}

void NetworkCamera::add_source_header(size_t source_index, const std::string &name, const std::string &value) {
  if (source_index < this->sources_.size())
    this->sources_[source_index].headers.emplace_back(name, value);
}

void NetworkCamera::setup() {
#ifdef USE_ESP_IDF
  if (this->sources_.empty()) {
    ESP_LOGE(TAG, "At least one stream source is required");
    this->mark_failed();
    return;
  }

  this->encoded_buffer_ =
      static_cast<uint8_t *>(heap_caps_malloc(this->max_frame_size_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (this->encoded_buffer_ == nullptr)
    this->encoded_buffer_ = static_cast<uint8_t *>(heap_caps_malloc(this->max_frame_size_, MALLOC_CAP_8BIT));
  if (this->encoded_buffer_ == nullptr) {
    ESP_LOGE(TAG, "Unable to allocate the %zu-byte encoded frame buffer", this->max_frame_size_);
    this->mark_failed();
    return;
  }
  this->read_buffer_ =
      static_cast<uint8_t *>(heap_caps_malloc(this->read_buffer_size_, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (this->read_buffer_ == nullptr) {
    ESP_LOGE(TAG, "Unable to allocate the %zu-byte HTTP read buffer", this->read_buffer_size_);
    this->mark_failed();
    return;
  }

  esp32_jpeg::preallocate_decoder(static_cast<int>(this->request_timeout_ms_));
  this->fps_window_started_ms_ = millis();

#if CONFIG_FREERTOS_UNICORE
  const BaseType_t task_core = tskNO_AFFINITY;
#else
  const BaseType_t task_core = this->task_core_ < 0 ? tskNO_AFFINITY : this->task_core_;
#endif
  const BaseType_t created =
      xTaskCreatePinnedToCore(&NetworkCamera::stream_task_trampoline_, "network_camera", this->task_stack_size_, this,
                              this->task_priority_, &this->stream_task_handle_, task_core);
  if (created != pdPASS) {
    ESP_LOGE(TAG, "Unable to create stream task");
    this->mark_failed();
    return;
  }
  this->queue_source_event_();
#else
  ESP_LOGE(TAG, "Network camera requires ESP-IDF");
  this->mark_failed();
#endif
}

void NetworkCamera::loop() {
  const StreamState current = this->state_.load(std::memory_order_acquire);
  if (current != this->reported_state_.load(std::memory_order_relaxed)) {
    this->reported_state_.store(current, std::memory_order_relaxed);
    this->state_callback_.call(state_to_string(current));
  }

  if (this->source_event_pending_.exchange(false, std::memory_order_acq_rel) && !this->sources_.empty()) {
    const size_t index = std::min(this->active_source_.load(std::memory_order_acquire), this->sources_.size() - 1U);
    if (this->source_select_ != nullptr)
      this->source_select_->publish_state(index);
    this->source_callback_.call(this->sources_[index].name);
  }

  if (this->first_frame_pending_.exchange(false, std::memory_order_acq_rel))
    this->first_frame_callback_.call();
}

void NetworkCamera::dump_config() {
  ESP_LOGCONFIG(TAG, "Network Camera:");
  ESP_LOGCONFIG(TAG, "  Sources: %zu", this->sources_.size());
  for (size_t index = 0; index < this->sources_.size(); index++)
    ESP_LOGCONFIG(TAG, "    %zu: %s", index, this->sources_[index].name.c_str());
  ESP_LOGCONFIG(TAG, "  Maximum JPEG: %zu bytes", this->max_frame_size_);
  ESP_LOGCONFIG(TAG, "  Maximum dimensions: %ux%u", this->max_width_, this->max_height_);
  ESP_LOGCONFIG(TAG, "  Frame interval: %u ms", this->frame_interval_ms_);
  ESP_LOGCONFIG(TAG, "  Reconnect interval: %u ms", this->reconnect_interval_ms_);
  ESP_LOGCONFIG(TAG, "  Release decoded frame on stop: %s", YESNO(this->release_buffer_on_stop_));
  ESP_LOGCONFIG(TAG, "  Worker: core %d, priority %u, stack %u", this->task_core_, this->task_priority_,
                this->task_stack_size_);
}

void NetworkCamera::on_shutdown() {
  this->shutdown_requested_.store(true, std::memory_order_release);
  this->running_requested_.store(false, std::memory_order_release);
#ifdef USE_ESP_IDF
  if (this->stream_task_handle_ != nullptr)
    xTaskNotifyGive(this->stream_task_handle_);
#endif
}

void NetworkCamera::start() {
  if (this->sources_.empty() || this->is_failed())
    return;
  this->running_requested_.store(true, std::memory_order_release);
  this->source_revision_.fetch_add(1, std::memory_order_acq_rel);
  this->first_frame_pending_.store(false, std::memory_order_release);
  this->awaiting_first_frame_.store(true, std::memory_order_release);
  this->queue_source_event_();
#ifdef USE_ESP_IDF
  if (this->stream_task_handle_ != nullptr)
    xTaskNotifyGive(this->stream_task_handle_);
#endif
}

void NetworkCamera::stop() {
  this->running_requested_.store(false, std::memory_order_release);
#ifdef USE_ESP_IDF
  if (this->stream_task_handle_ != nullptr)
    xTaskNotifyGive(this->stream_task_handle_);
#endif
}

void NetworkCamera::next_source() {
  if (this->sources_.empty())
    return;
  this->select_source((this->active_source_.load(std::memory_order_acquire) + 1U) % this->sources_.size());
}

void NetworkCamera::previous_source() {
  if (this->sources_.empty())
    return;
  const size_t current = this->active_source_.load(std::memory_order_acquire);
  this->select_source(current == 0 ? this->sources_.size() - 1U : current - 1U);
}

void NetworkCamera::select_source(size_t index) {
  if (index >= this->sources_.size()) {
    ESP_LOGW(TAG, "Ignoring out-of-range source index %zu", index);
    return;
  }
  if (this->active_source_.exchange(index, std::memory_order_acq_rel) == index && this->is_running())
    return;
  this->source_revision_.fetch_add(1, std::memory_order_acq_rel);
  this->first_frame_pending_.store(false, std::memory_order_release);
  this->awaiting_first_frame_.store(true, std::memory_order_release);
  this->queue_source_event_();
#ifdef USE_ESP_IDF
  if (this->stream_task_handle_ != nullptr)
    xTaskNotifyGive(this->stream_task_handle_);
#endif
}

const char *NetworkCamera::state_name() const { return state_to_string(this->state_.load(std::memory_order_acquire)); }

size_t NetworkCamera::memory_usage_bytes() const {
  LockGuard lock(this->frame_mutex_);
#ifdef USE_ESP_IDF
  return this->frame_capacity_ + (this->encoded_buffer_ == nullptr ? 0 : this->max_frame_size_);
#else
  return this->frame_capacity_;
#endif
}

bool NetworkCamera::acquire_buffer(image::ImageBufferLease *lease) const {
  if (lease == nullptr)
    return false;
  LockGuard lock(this->frame_mutex_);
  if (this->frame_buffer_ == nullptr || this->frame_decoding_ || this->frame_width_ == 0 || this->frame_height_ == 0)
    return false;
  this->frame_readers_++;
  *lease = {
      .owner = this,
      .data = this->frame_buffer_,
      .size = this->frame_size_,
      .stride = this->frame_stride_,
      .width = static_cast<int>(this->frame_width_),
      .height = static_cast<int>(this->frame_height_),
      .type = image::IMAGE_TYPE_RGB,
      .transparency = image::TRANSPARENCY_OPAQUE,
      .writer = BufferWriter::DMA,
      .generation = this->generation_.load(std::memory_order_relaxed),
  };
  return true;
}

bool NetworkCamera::release_buffer(image::ImageBufferLease *lease) const {
  if (lease == nullptr || lease->owner != this)
    return false;
  LockGuard lock(this->frame_mutex_);
  if (this->frame_readers_ > 0)
    this->frame_readers_--;
  *lease = {};
  return true;
}

void NetworkCamera::set_state_(StreamState state) { this->state_.store(state, std::memory_order_release); }

void NetworkCamera::queue_source_event_() { this->source_event_pending_.store(true, std::memory_order_release); }

void NetworkCamera::reset_parser_() {
#ifdef USE_ESP_IDF
  this->encoded_size_ = 0;
  this->jpeg_started_ = false;
  this->previous_byte_ff_ = false;
#endif
}

#ifdef USE_ESP_IDF

void NetworkCamera::stream_task_trampoline_(void *arg) {
  static_cast<NetworkCamera *>(arg)->stream_task_();
  vTaskDelete(nullptr);
}

void NetworkCamera::stream_task_() {
  uint32_t connected_revision = UINT32_MAX;
  while (!this->shutdown_requested_.load(std::memory_order_acquire)) {
    if (!this->running_requested_.load(std::memory_order_acquire)) {
      this->disconnect_stream_();
      if (this->release_buffer_on_stop_)
        this->release_frame_buffer_();
      this->set_state_(StreamState::IDLE);
      this->wait_for_task_wakeup_(250);
      continue;
    }

    if (!network::is_connected()) {
      this->disconnect_stream_();
      this->set_state_(StreamState::RETRY_WAIT);
      this->wait_for_task_wakeup_(this->reconnect_interval_ms_);
      continue;
    }

    const uint32_t revision = this->source_revision_.load(std::memory_order_acquire);
    const size_t source_index = this->active_source_.load(std::memory_order_acquire);
    if (source_index >= this->sources_.size()) {
      this->set_state_(StreamState::ERROR);
      this->wait_for_task_wakeup_(this->reconnect_interval_ms_);
      continue;
    }

    if (this->http_client_ == nullptr || connected_revision != revision) {
      this->disconnect_stream_();
      this->set_state_(StreamState::CONNECTING);
      if (!this->connect_stream_(this->sources_[source_index])) {
        this->reconnects_.fetch_add(1, std::memory_order_relaxed);
        this->set_state_(StreamState::RETRY_WAIT);
        this->wait_for_task_wakeup_(this->reconnect_interval_ms_);
        continue;
      }
      connected_revision = revision;
      this->reset_parser_();
    }

    if (!this->read_stream_()) {
      this->disconnect_stream_();
      connected_revision = UINT32_MAX;
      this->reconnects_.fetch_add(1, std::memory_order_relaxed);
      if (this->running_requested_.load(std::memory_order_acquire)) {
        this->set_state_(StreamState::RETRY_WAIT);
        this->wait_for_task_wakeup_(this->reconnect_interval_ms_);
      }
    }
  }

  this->disconnect_stream_();
  this->stream_task_handle_ = nullptr;
}

bool NetworkCamera::connect_stream_(const StreamSource &source) {
  esp_http_client_config_t config{};
  config.url = source.url.c_str();
  config.method = HTTP_METHOD_GET;
  config.timeout_ms = static_cast<int>(this->request_timeout_ms_);
  config.buffer_size = 8192;
  config.buffer_size_tx = 1024;
  config.keep_alive_enable = true;
  config.disable_auto_redirect = false;
  if (source.url.rfind("https://", 0) == 0)
    config.crt_bundle_attach = esp_crt_bundle_attach;

  this->http_client_ = esp_http_client_init(&config);
  if (this->http_client_ == nullptr) {
    ESP_LOGW(TAG, "Unable to initialize HTTP client for %s", source.name.c_str());
    return false;
  }
  for (const auto &header : source.headers)
    esp_http_client_set_header(this->http_client_, header.first.c_str(), header.second.c_str());

  esp_err_t error = esp_http_client_open(this->http_client_, 0);
  if (error != ESP_OK) {
    ESP_LOGW(TAG, "Stream %s open failed: %s", source.name.c_str(), esp_err_to_name(error));
    this->disconnect_stream_();
    return false;
  }
  esp_http_client_fetch_headers(this->http_client_);
  const int status = esp_http_client_get_status_code(this->http_client_);
  if (status < 200 || status >= 300) {
    ESP_LOGW(TAG, "Stream %s returned HTTP %d", source.name.c_str(), status);
    this->disconnect_stream_();
    return false;
  }
  ESP_LOGI(TAG, "Connected to %s", source.name.c_str());
  return true;
}

void NetworkCamera::disconnect_stream_() {
  if (this->http_client_ != nullptr) {
    esp_http_client_close(this->http_client_);
    esp_http_client_cleanup(this->http_client_);
    this->http_client_ = nullptr;
  }
  this->reset_parser_();
}

bool NetworkCamera::read_stream_() {
  const int received =
      esp_http_client_read(this->http_client_, reinterpret_cast<char *>(this->read_buffer_), this->read_buffer_size_);
  if (received > 0)
    return this->consume_stream_bytes_(this->read_buffer_, static_cast<size_t>(received));
  if (received == -ESP_ERR_HTTP_EAGAIN)
    return true;
  if (received == 0 && !esp_http_client_is_complete_data_received(this->http_client_))
    return true;
  return false;
}

bool NetworkCamera::consume_stream_bytes_(const uint8_t *data, size_t size) {
  for (size_t index = 0; index < size; index++) {
    const uint8_t value = data[index];
    if (!this->jpeg_started_) {
      if (this->previous_byte_ff_ && value == 0xD8) {
        this->jpeg_started_ = true;
        this->encoded_size_ = 2;
        this->encoded_buffer_[0] = 0xFF;
        this->encoded_buffer_[1] = 0xD8;
      }
      this->previous_byte_ff_ = value == 0xFF;
      continue;
    }

    if (this->encoded_size_ >= this->max_frame_size_) {
      this->dropped_frames_.fetch_add(1, std::memory_order_relaxed);
      this->reset_parser_();
      continue;
    }
    this->encoded_buffer_[this->encoded_size_++] = value;
    if (this->previous_byte_ff_ && value == 0xD9) {
      this->finish_jpeg_frame_();
      this->reset_parser_();
      continue;
    }
    this->previous_byte_ff_ = value == 0xFF;
  }
  return true;
}

bool NetworkCamera::finish_jpeg_frame_() {
  const uint32_t now = millis();
  if (this->last_frame_ms_ != 0 && now - this->last_frame_ms_ < this->frame_interval_ms_) {
    this->dropped_frames_.fetch_add(1, std::memory_order_relaxed);
    return true;
  }
  if (!this->decode_jpeg_frame_(this->encoded_buffer_, this->encoded_size_)) {
    this->dropped_frames_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }

  this->last_frame_ms_ = now;
  this->decoded_frames_.fetch_add(1, std::memory_order_relaxed);
  this->fps_window_frames_++;
  const uint32_t elapsed = now - this->fps_window_started_ms_;
  if (elapsed >= 2000) {
    this->measured_fps_.store(static_cast<float>(this->fps_window_frames_) * 1000.0f / elapsed,
                              std::memory_order_relaxed);
    this->fps_window_frames_ = 0;
    this->fps_window_started_ms_ = now;
  }
  this->set_state_(StreamState::STREAMING);
  if (this->awaiting_first_frame_.exchange(false, std::memory_order_acq_rel))
    this->first_frame_pending_.store(true, std::memory_order_release);
  return true;
}

bool NetworkCamera::decode_jpeg_frame_(const uint8_t *data, size_t size) {
  esp32_jpeg::PictureInfo info{};
  esp_err_t error = esp32_jpeg::get_info(data, size, &info);
  if (error != ESP_OK || info.width == 0 || info.height == 0 || info.width > this->max_width_ ||
      info.height > this->max_height_) {
    ESP_LOGW(TAG, "Rejected JPEG frame: info=%s dimensions=%ux%u limit=%ux%u", esp_err_to_name(error),
             static_cast<unsigned>(info.width), static_cast<unsigned>(info.height), this->max_width_,
             this->max_height_);
    return false;
  }

  const uint32_t stride_width = align_up(info.width, horizontal_mcu_alignment(info.down_sampling));
  const size_t stride = static_cast<size_t>(stride_width) * 3U;
  const size_t required_size = esp32_jpeg::decoded_output_size(info, esp32_jpeg::PixelFormat::RGB888);
  {
    LockGuard lock(this->frame_mutex_);
    if (this->frame_readers_ != 0 || this->frame_decoding_)
      return false;
    this->frame_decoding_ = true;
  }

  if (!this->ensure_frame_buffer_(info.width, info.height, stride, required_size)) {
    LockGuard lock(this->frame_mutex_);
    this->frame_decoding_ = false;
    return false;
  }

  esp32_jpeg::DecodeConfig config{
      .output_format = esp32_jpeg::PixelFormat::RGB888,
      .rgb_order = esp32_jpeg::RgbElementOrder::BGR,
      .color_conversion = esp32_jpeg::ColorConversionStandard::BT601,
      .direct_output = true,
      .skip_output_cache_sync = true,
      .timeout_ms = static_cast<int>(this->request_timeout_ms_),
  };
  size_t written = 0;
  const int64_t started_us = esp_timer_get_time();
  error = esp32_jpeg::decode(config, data, size, this->frame_buffer_, this->frame_capacity_, &written);
  this->last_decode_us_.store(static_cast<uint32_t>(esp_timer_get_time() - started_us), std::memory_order_relaxed);

  LockGuard lock(this->frame_mutex_);
  if (error == ESP_OK && written >= stride * static_cast<size_t>(info.height)) {
    this->frame_width_ = info.width;
    this->frame_height_ = info.height;
    this->frame_stride_ = stride;
    this->frame_size_ = stride * static_cast<size_t>(info.height);
    this->width_ = static_cast<int>(info.width);
    this->height_ = static_cast<int>(info.height);
    this->stride_ = stride;
    this->data_start_ = this->frame_buffer_;
    this->generation_.fetch_add(1, std::memory_order_release);
    this->frame_decoding_ = false;
    return true;
  }
  this->frame_decoding_ = false;
  ESP_LOGW(TAG, "Hardware JPEG decode failed: %s written=%zu", esp_err_to_name(error), written);
  return false;
}

bool NetworkCamera::ensure_frame_buffer_(uint32_t width, uint32_t height, size_t stride, size_t required_size) {
  if (this->frame_buffer_ != nullptr && this->frame_capacity_ >= required_size)
    return true;
  if (this->frame_buffer_ != nullptr)
    esp32_jpeg::release_decode_output(this->frame_buffer_);
  this->frame_buffer_ = nullptr;
  this->frame_capacity_ = 0;
  this->frame_size_ = 0;

  size_t capacity = 0;
  uint8_t *buffer = esp32_jpeg::allocate_decode_output(required_size, &capacity);
  if (buffer == nullptr || capacity < required_size) {
    if (buffer != nullptr)
      esp32_jpeg::release_decode_output(buffer);
    ESP_LOGW(TAG, "Unable to allocate %zu-byte decoded frame (%ux%u stride=%zu)", required_size,
             static_cast<unsigned>(width), static_cast<unsigned>(height), stride);
    return false;
  }
  this->frame_buffer_ = buffer;
  this->frame_capacity_ = capacity;
  return true;
}

void NetworkCamera::release_frame_buffer_() {
  LockGuard lock(this->frame_mutex_);
  if (this->frame_readers_ != 0)
    return;
  if (this->frame_buffer_ != nullptr)
    esp32_jpeg::release_decode_output(this->frame_buffer_);
  this->frame_buffer_ = nullptr;
  this->frame_capacity_ = 0;
  this->frame_size_ = 0;
  this->frame_stride_ = 0;
  this->frame_width_ = 0;
  this->frame_height_ = 0;
  this->data_start_ = nullptr;
  this->width_ = 0;
  this->height_ = 0;
  this->generation_.store(0, std::memory_order_release);
}

void NetworkCamera::wait_for_task_wakeup_(uint32_t timeout_ms) { ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(timeout_ms)); }

#endif

}  // namespace esphome::network_camera
