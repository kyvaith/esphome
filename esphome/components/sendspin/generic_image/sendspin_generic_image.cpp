#include "sendspin_generic_image.h"

#if defined(USE_ESP32) && defined(USE_SENDSPIN_ARTWORK)

#include "esphome/core/log.h"

namespace esphome::sendspin_ {

static const char *const TAG = "sendspin.generic_image";

// StaticTask sizes are expressed in StackType_t entries. This reserves an
// 8 KiB stack on ESP32 targets where StackType_t is 32 bits.
static constexpr uint32_t DECODE_TASK_STACK_SIZE = 2048;
static constexpr UBaseType_t DECODE_TASK_PRIORITY = 1;

SendspinImage::SendspinImage(int fixed_width, int fixed_height, runtime_image::ImageFormat format,
                             image::ImageType type, image::Transparency transparency, bool is_big_endian,
                             image::Image *placeholder)
    : runtime_image::RuntimeImage(format, type, transparency, placeholder, is_big_endian, fixed_width, fixed_height) {}

// THREAD CONTEXT: Main loop. The decode callback registered below fires on the
// Sendspin artwork thread; display and clear callbacks fire on the main loop.
void SendspinImage::setup() {
  if (this->encoded_buffer_size_ > 0) {
    this->encoded_data_.reserve(this->encoded_buffer_size_);
  }
  if (this->deferred_decode_ && !this->start_decode_task_()) {
    ESP_LOGW(TAG, "Could not create deferred image decoder task; decoding will run on the main loop");
  }

  this->parent_->add_image_decode_callback(
      [this](uint8_t slot, const uint8_t *data, size_t length, sendspin::SendspinImageFormat format) {
        if (slot == this->slot_)
          this->on_decode_(data, length);
      });
  this->parent_->add_image_display_callback([this](uint8_t slot) {
    if (slot == this->slot_)
      this->on_display_();
  });
  this->parent_->add_image_clear_callback([this](uint8_t slot) {
    if (slot == this->slot_)
      this->on_clear_();
  });
}

bool SendspinImage::start_decode_task_() {
  if (this->decode_task_handle_.is_created())
    return true;
  return this->decode_task_handle_.create(SendspinImage::decode_task_, "sendspin_image", DECODE_TASK_STACK_SIZE, this,
                                          DECODE_TASK_PRIORITY, this->parent_->get_task_stack_in_psram());
}

bool SendspinImage::decode_image_(const uint8_t *data, size_t length) {
  if (data == nullptr || length == 0 || !this->begin_decode(length))
    return false;

  size_t total_consumed = 0;
  while (total_consumed < length) {
    int consumed = this->feed_data(const_cast<uint8_t *>(data) + total_consumed, length - total_consumed);
    if (consumed <= 0) {
      ESP_LOGE(TAG, "Error decoding image data at offset %zu", total_consumed);
      this->abort_decode();
      return false;
    }
    total_consumed += consumed;
  }
  if (!this->end_decode(false)) {
    ESP_LOGE(TAG, "Failed to finalize image after decoding");
    this->abort_decode();
    return false;
  }
  return true;
}

// THREAD CONTEXT: Sendspin artwork thread.
void SendspinImage::on_decode_(const uint8_t *data, size_t length) {
  if (!this->deferred_decode_) {
    if (!this->decode_image_(data, length))
      this->defer_image_error_();
    return;
  }

  bool paused = false;
  {
    LockGuard lock(this->pending_mutex_);
    if (data != nullptr && length > 0) {
      this->encoded_data_.assign(data, data + length);
      this->pending_encoded_image_ = true;
    } else {
      this->encoded_data_.clear();
      this->pending_encoded_image_ = false;
      this->decode_failed_.store(true, std::memory_order_release);
    }
    this->pending_clear_ = false;
    paused = this->paused_;
  }
  if (!paused)
    this->schedule_process_();
}

// THREAD CONTEXT: Main loop.
void SendspinImage::on_display_() {
  if (!this->deferred_decode_) {
    if (this->publish_pending())
      this->image_display_callback_.call();
    return;
  }

  bool paused = false;
  {
    LockGuard lock(this->pending_mutex_);
    this->pending_display_ = true;
    paused = this->paused_;
  }
  if (!paused)
    this->schedule_process_();
}

// THREAD CONTEXT: Main loop.
void SendspinImage::on_clear_() {
  if (!this->deferred_decode_) {
    if (!this->retain_on_clear_) {
      this->release();
      this->image_display_callback_.call();
    }
    return;
  }

  bool paused = false;
  {
    LockGuard lock(this->pending_mutex_);
    if (!this->decode_busy_.load(std::memory_order_acquire))
      this->encoded_data_.clear();
    this->pending_encoded_image_ = false;
    this->pending_display_ = false;
    this->pending_clear_ = true;
    paused = this->paused_;
  }
  if (!paused)
    this->schedule_process_();
}

void SendspinImage::set_paused(bool paused) {
  bool resume = false;
  {
    LockGuard lock(this->pending_mutex_);
    if (this->paused_ == paused)
      return;
    this->paused_ = paused;
    resume = !paused;
  }
  if (resume)
    this->schedule_process_();
}

bool SendspinImage::is_paused() const {
  LockGuard lock(this->pending_mutex_);
  return this->paused_;
}

size_t SendspinImage::memory_usage_bytes() const {
  size_t encoded_bytes = 0;
  {
    LockGuard lock(this->pending_mutex_);
    encoded_bytes = this->encoded_data_.capacity();
  }
  return runtime_image::RuntimeImage::memory_usage_bytes() + encoded_bytes;
}

void SendspinImage::schedule_process_() {
  if (this->process_scheduled_.exchange(true, std::memory_order_acq_rel))
    return;
  this->defer("image_process", [this]() {
    this->process_scheduled_.store(false, std::memory_order_release);
    this->process_pending_();
  });
}

// THREAD CONTEXT: Main loop.
void SendspinImage::process_pending_() {
  {
    LockGuard lock(this->pending_mutex_);
    if (this->paused_)
      return;
  }

  if (this->decode_busy_.load(std::memory_order_acquire))
    return;

  bool clear = false;
  bool start_decode = false;
  bool publish = false;
  bool decode_failed = false;
  {
    LockGuard lock(this->pending_mutex_);
    clear = this->pending_clear_;
    if (clear) {
      this->pending_clear_ = false;
      this->pending_display_ = false;
      this->pending_encoded_image_ = false;
      this->encoded_data_.clear();
    } else if (this->pending_encoded_image_) {
      this->pending_encoded_image_ = false;
      this->decode_ready_.store(false, std::memory_order_release);
      this->decode_failed_.store(false, std::memory_order_release);
      start_decode = !this->encoded_data_.empty();
      if (start_decode)
        this->decode_busy_.store(true, std::memory_order_release);
    } else if (this->decode_failed_.load(std::memory_order_acquire) && this->pending_display_) {
      this->pending_display_ = false;
      this->decode_failed_.store(false, std::memory_order_release);
      this->encoded_data_.clear();
      decode_failed = true;
    } else if (this->decode_ready_.load(std::memory_order_acquire) && this->pending_display_) {
      this->pending_display_ = false;
      this->decode_ready_.store(false, std::memory_order_release);
      this->encoded_data_.clear();
      publish = true;
    }
  }

  if (clear) {
    this->abort_decode();
    this->decode_ready_.store(false, std::memory_order_release);
    this->decode_failed_.store(false, std::memory_order_release);
    if (!this->retain_on_clear_) {
      this->release();
      this->image_display_callback_.call();
    }
    this->parent_->image_frame_done(this->slot_);
    return;
  }

  if (decode_failed) {
    this->image_error_callback_.call();
    this->parent_->image_frame_done(this->slot_);
    return;
  }

  if (start_decode) {
    this->decode_start_callback_.call();
    if (this->decode_task_handle_.is_created()) {
      xTaskNotifyGive(this->decode_task_handle_.get_handle());
    } else {
      const bool success = this->decode_image_(this->encoded_data_.data(), this->encoded_data_.size());
      this->decode_ready_.store(success, std::memory_order_release);
      this->decode_failed_.store(!success, std::memory_order_release);
      this->decode_busy_.store(false, std::memory_order_release);
      this->schedule_process_();
    }
    return;
  }

  if (publish) {
    if (this->publish_pending()) {
      this->image_display_callback_.call();
    } else {
      this->image_error_callback_.call();
    }
    this->parent_->image_frame_done(this->slot_);
  }
}

void SendspinImage::decode_task_(void *arg) {
  auto *image = static_cast<SendspinImage *>(arg);
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    const bool success = image->decode_image_(image->encoded_data_.data(), image->encoded_data_.size());
    image->decode_ready_.store(success, std::memory_order_release);
    image->decode_failed_.store(!success, std::memory_order_release);
    image->decode_busy_.store(false, std::memory_order_release);
    image->schedule_process_();
  }
}

void SendspinImage::defer_image_error_() {
  this->defer("image_error", [this]() { this->image_error_callback_.call(); });
}

}  // namespace esphome::sendspin_

#endif
