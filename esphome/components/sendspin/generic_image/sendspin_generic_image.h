#pragma once

#include "esphome/core/defines.h"

#if defined(USE_ESP32) && defined(USE_SENDSPIN_ARTWORK)

#include "esphome/components/runtime_image/runtime_image.h"
#include "esphome/components/sendspin/sendspin_hub.h"
#include "esphome/core/automation.h"
#include "esphome/core/static_task.h"

#include <sendspin/artwork_role.h>

#include <atomic>
#include <vector>

namespace esphome::sendspin_ {

class SendspinImage : public SendspinChild, public runtime_image::RuntimeImage {
 public:
  SendspinImage(int fixed_width, int fixed_height, runtime_image::ImageFormat format, image::ImageType type,
                image::Transparency transparency, bool is_big_endian = false, image::Image *placeholder = nullptr);

  void setup() override;

  template<typename F> void add_on_image_display_callback(F &&callback) {
    this->image_display_callback_.add(std::forward<F>(callback));
  }
  template<typename F> void add_on_image_error_callback(F &&callback) {
    this->image_error_callback_.add(std::forward<F>(callback));
  }
  template<typename F> void add_on_decode_start_callback(F &&callback) {
    this->decode_start_callback_.add(std::forward<F>(callback));
  }

  void set_image_source(sendspin::SendspinImageSource source) { this->source_ = source; }
  void set_slot(uint8_t slot) { this->slot_ = slot; }
  void set_deferred_decode(bool deferred_decode) { this->deferred_decode_ = deferred_decode; }
  void set_paused(bool paused);
  void set_retain_on_clear(bool retain_on_clear) { this->retain_on_clear_ = retain_on_clear; }
  void set_encoded_buffer_size(size_t encoded_buffer_size) { this->encoded_buffer_size_ = encoded_buffer_size; }

  bool is_paused() const;
  size_t memory_usage_bytes() const;

 protected:
  // Artwork thread. The encoded buffer is valid only for this call.
  void on_decode_(const uint8_t *data, size_t length);
  // Main loop thread. Trigger when art should be displayed.
  void on_display_();
  // Main loop thread. Releases the decoded image unless retain_on_clear is enabled.
  void on_clear_();
  // Any thread. Schedules the error automation on the main loop.
  void defer_image_error_();
  // Any thread. Coalesces source events into one main-loop state-machine pass.
  void schedule_process_();
  // Main loop thread. Starts deferred decode and publishes completed images.
  void process_pending_();
  bool decode_image_(const uint8_t *data, size_t length);
  bool start_decode_task_();
  static void decode_task_(void *arg);

  LazyCallbackManager<void()> image_display_callback_{};
  LazyCallbackManager<void()> image_error_callback_{};
  LazyCallbackManager<void()> decode_start_callback_{};

  sendspin::SendspinImageSource source_{sendspin::SendspinImageSource::ALBUM};
  uint8_t slot_{0};
  bool deferred_decode_{false};
  bool retain_on_clear_{false};
  size_t encoded_buffer_size_{0};

  mutable Mutex pending_mutex_{};
  bool paused_{false};
  std::vector<uint8_t> encoded_data_{};
  bool pending_encoded_image_{false};
  bool pending_display_{false};
  bool pending_clear_{false};

  std::atomic<bool> decode_busy_{false};
  std::atomic<bool> decode_ready_{false};
  std::atomic<bool> decode_failed_{false};
  std::atomic<bool> process_scheduled_{false};
  StaticTask decode_task_handle_{};
};

class SendspinImageDisplayTrigger : public Trigger<> {
 public:
  explicit SendspinImageDisplayTrigger(SendspinImage *parent) {
    parent->add_on_image_display_callback([this]() { this->trigger(); });
  }
};

class SendspinImageErrorTrigger : public Trigger<> {
 public:
  explicit SendspinImageErrorTrigger(SendspinImage *parent) {
    parent->add_on_image_error_callback([this]() { this->trigger(); });
  }
};

class SendspinImageDecodeStartTrigger : public Trigger<> {
 public:
  explicit SendspinImageDecodeStartTrigger(SendspinImage *parent) {
    parent->add_on_decode_start_callback([this]() { this->trigger(); });
  }
};

template<typename... Ts> class SendspinImagePauseAction : public Action<Ts...> {
 public:
  explicit SendspinImagePauseAction(SendspinImage *parent) : parent_(parent) {}
  void play(const Ts &...x) override { this->parent_->set_paused(true); }

 protected:
  SendspinImage *parent_;
};

template<typename... Ts> class SendspinImageResumeAction : public Action<Ts...> {
 public:
  explicit SendspinImageResumeAction(SendspinImage *parent) : parent_(parent) {}
  void play(const Ts &...x) override { this->parent_->set_paused(false); }

 protected:
  SendspinImage *parent_;
};

}  // namespace esphome::sendspin_

#endif
