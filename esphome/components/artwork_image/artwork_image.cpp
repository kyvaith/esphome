#include "artwork_image.h"

#include <algorithm>
#include <cctype>
#include <cinttypes>
#include <cstdlib>
#include <cstring>
#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "esphome/core/version.h"

#ifdef USE_ESP32
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_timer.h"
#endif

#ifdef USE_ESP32_JPEG
#include "esphome/components/esp32_jpeg/esp32_jpeg.h"
#endif

#ifdef USE_ESP_IDF
#include "esp_http_client.h"
#endif

static const char *const TAG = "artwork_image";
static const char *const CONTENT_TYPE_HEADER_NAME = "content-type";
static constexpr uint32_t RETIRED_BUFFER_GRACE_MS = 250;
static constexpr size_t MAX_RETIRED_BUFFERS = 1;
static constexpr size_t MAX_DOWNLOAD_BUFFER_SIZE = 2 * 1024 * 1024;
static constexpr size_t MAX_READ_CHUNK_SIZE = 8 * 1024;
static constexpr int LOCAL_ARTWORK_HTTP_CONNECT_TIMEOUT_MS = 2500;
static constexpr int LOCAL_ARTWORK_HTTP_READ_TIMEOUT_MS = 15;
static constexpr uint32_t SLOW_ARTWORK_STAGE_MS = 30;
static constexpr uint32_t SENDSPIN_ARTWORK_PROCESS_DELAY_MS = 100;

#ifndef CONFIG_ESPHOME_ARTWORK_TRACE_VERBOSE
#define CONFIG_ESPHOME_ARTWORK_TRACE_VERBOSE 0
#endif

#include "image_decoder.h"

#ifdef USE_ARTWORK_IMAGE_BMP_SUPPORT
#include "bmp_image.h"
#endif
#ifdef USE_ARTWORK_IMAGE_JPEG_SUPPORT
#include "jpeg_image.h"
#endif
#ifdef USE_ARTWORK_IMAGE_PNG_SUPPORT
#include "png_image.h"
#endif

namespace esphome {
namespace artwork_image {

using image::ImageType;

static void log_slow_artwork_stage(const char *stage, uint32_t start_ms) {
  const uint32_t elapsed = millis() - start_ms;
  if (elapsed > SLOW_ARTWORK_STAGE_MS) {
    ESP_LOGW(TAG, "Artwork slow stage: %s took %" PRIu32 "ms", stage, elapsed);
  }
}

static uint64_t artwork_trace_now_us() {
#ifdef USE_ESP32
  return esp_timer_get_time();
#else
  return static_cast<uint64_t>(millis()) * 1000ULL;
#endif
}

static const char *image_format_to_string(ImageFormat format) {
  switch (format) {
    case ImageFormat::AUTO:
      return "auto";
    case ImageFormat::JPEG:
      return "jpeg";
    case ImageFormat::PNG:
      return "png";
    case ImageFormat::BMP:
      return "bmp";
    case ImageFormat::HEIC:
      return "heic";
    default:
      return "unknown";
  }
}

#ifdef USE_SENDSPIN_ARTWORK
static const char *sendspin_format_to_string(sendspin::SendspinImageFormat format) {
  switch (format) {
    case sendspin::SendspinImageFormat::JPEG:
      return "jpeg";
    case sendspin::SendspinImageFormat::PNG:
      return "png";
    case sendspin::SendspinImageFormat::BMP:
      return "bmp";
    default:
      return "unknown";
  }
}
#endif

void ArtworkImage::log_memory_summary_(const char *stage) const {
#ifdef USE_ESP32
  ESP_LOGW(TAG,
           "Artwork memory %s: image=%dx%d buffer=%uKB retired=%zu psram=%uK/%uK internal=%uK/%uK",
           stage, this->buffer_width_, this->buffer_height_, (unsigned) (this->get_buffer_size_() / 1024),
           this->retired_buffers_.size(), (unsigned) (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
           (unsigned) (heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024),
           (unsigned) (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
           (unsigned) (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
#else
  ESP_LOGW(TAG, "Artwork memory %s: image=%dx%d buffer=%uKB retired=%zu", stage, this->buffer_width_,
           this->buffer_height_, (unsigned) (this->get_buffer_size_() / 1024), this->retired_buffers_.size());
#endif
}

void ArtworkImage::begin_trace_(const char *stage, size_t bytes) {
  this->trace_id_ = ++this->trace_next_id_;
  this->trace_start_us_ = artwork_trace_now_us();
  this->trace_event_(stage, bytes);
}

void ArtworkImage::trace_event_(const char *stage, size_t bytes) const {
  if constexpr (!CONFIG_ESPHOME_ARTWORK_TRACE_VERBOSE) {
    (void) stage;
    (void) bytes;
    return;
  }
  if (this->trace_id_ == 0) {
    return;
  }
  const uint64_t now_us = artwork_trace_now_us();
  const uint64_t elapsed_us = this->trace_start_us_ == 0 ? 0 : now_us - this->trace_start_us_;
#ifdef USE_ESP32
  ESP_LOGW(TAG,
           "artwork trace #%u +%lluus %s bytes=%zu active=%p image=%dx%d decode=%p %dx%d retired=%zu psram=%uK/%uK "
           "internal=%uK/%uK",
           this->trace_id_, (unsigned long long) elapsed_us, stage, bytes, this->buffer_, this->buffer_width_,
           this->buffer_height_, this->decode_buffer_, this->decode_buffer_width_, this->decode_buffer_height_,
           this->retired_buffers_.size(), (unsigned) (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
           (unsigned) (heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024),
           (unsigned) (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
           (unsigned) (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
#else
  ESP_LOGW(TAG, "artwork trace #%u +%lluus %s bytes=%zu active=%p image=%dx%d decode=%p %dx%d retired=%zu",
           this->trace_id_, (unsigned long long) elapsed_us, stage, bytes, this->buffer_, this->buffer_width_,
           this->buffer_height_, this->decode_buffer_, this->decode_buffer_width_, this->decode_buffer_height_,
           this->retired_buffers_.size());
#endif
}

static void sync_artwork_buffer_for_dma(const void *ptr, size_t size, bool written_by_dma) {
#if defined(USE_ESP32) && defined(USE_ESP_IDF)
  if (ptr == nullptr || size == 0 || !esp_ptr_external_ram(ptr)) {
    return;
  }
  constexpr size_t alignment = 64;
  const uintptr_t start = reinterpret_cast<uintptr_t>(ptr);
  const uintptr_t aligned_start = start & ~(static_cast<uintptr_t>(alignment) - 1U);
  const uintptr_t aligned_end = (start + size + alignment - 1U) & ~(static_cast<uintptr_t>(alignment) - 1U);
  if (aligned_end <= aligned_start || !esp_ptr_external_ram(reinterpret_cast<const void *>(aligned_start)) ||
      !esp_ptr_external_ram(reinterpret_cast<const void *>(aligned_end - 1U))) {
    return;
  }
  const uint64_t start_us = esp_timer_get_time();
  const uint32_t direction = written_by_dma ? ESP_CACHE_MSYNC_FLAG_DIR_M2C : ESP_CACHE_MSYNC_FLAG_DIR_C2M;
  esp_cache_msync(reinterpret_cast<void *>(aligned_start), aligned_end - aligned_start,
                  direction | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);
  const uint64_t elapsed_us = esp_timer_get_time() - start_us;
  if (elapsed_us > 30000) {
    ESP_LOGW(TAG, "Artwork cache sync %s took %lluus size=%zu aligned=%zu",
             written_by_dma ? "M2C" : "C2M", (unsigned long long) elapsed_us, size,
             aligned_end - aligned_start);
  }
#else
  (void) ptr;
  (void) size;
  (void) written_by_dma;
#endif
}

#ifdef USE_ESP_IDF
class LocalHttpContainer : public http_request::HttpContainer {
 public:
  explicit LocalHttpContainer(esp_http_client_handle_t client) : client_(client) {}

  void add_response_header(const std::string &name, const std::string &value) {
    this->response_headers_.push_back({name, value});
  }

  int read(uint8_t *buf, size_t max_len) override {
    const uint32_t start = millis();
    int read = esp_http_client_read(this->client_, reinterpret_cast<char *>(buf), max_len);
    log_slow_artwork_stage("local-read", start);
    if (read == -ESP_ERR_HTTP_EAGAIN) {
      return 0;
    }
    if (read > 0) {
      this->bytes_read_ += read;
    }
    return read;
  }

  bool is_read_complete() const override {
    if (HttpContainer::is_read_complete()) {
      return true;
    }
    return this->is_chunked_ && esp_http_client_is_complete_data_received(this->client_);
  }

  void end() override {
    if (this->client_ != nullptr) {
      esp_http_client_close(this->client_);
      esp_http_client_cleanup(this->client_);
      this->client_ = nullptr;
    }
  }

 protected:
  esp_http_client_handle_t client_{nullptr};
};

static esp_err_t insecure_local_http_event_handler(esp_http_client_event_t *evt) {
  auto *container = static_cast<LocalHttpContainer *>(evt->user_data);
  if (container == nullptr || evt->event_id != HTTP_EVENT_ON_HEADER) {
    return ESP_OK;
  }
  const std::string header_name = str_lower_case(evt->header_key);
  if (header_name == CONTENT_TYPE_HEADER_NAME) {
    container->add_response_header(header_name, evt->header_value);
  }
  return ESP_OK;
}
#endif

inline bool is_color_on(const Color &color) {
  // This produces the most accurate monochrome conversion, but is slightly slower.
  //  return (0.2125 * color.r + 0.7154 * color.g + 0.0721 * color.b) > 127;

  // Approximation using fast integer computations; produces acceptable results
  // Equivalent to 0.25 * R + 0.5 * G + 0.25 * B
  return ((color.r >> 2) + (color.g >> 1) + (color.b >> 2)) & 0x80;
}

ArtworkImage::ArtworkImage(const std::string &url, int width, int height, ImageFormat format, ImageType type,
                         image::Transparency transparency, uint32_t download_buffer_size, bool is_big_endian,
                         bool allow_insecure_local_urls)
    : Image(nullptr, 0, 0, type, transparency),
      buffer_(nullptr),
      download_buffer_(download_buffer_size),
      download_buffer_initial_size_(download_buffer_size),
      format_(format),
      fixed_width_(width),
      fixed_height_(height),
      is_big_endian_(is_big_endian),
      allow_insecure_local_urls_(allow_insecure_local_urls),
      buffer_width_(0),
      buffer_height_(0),
      start_time_(0) {
  this->set_url(url);
}

void ArtworkImage::setup() {
#ifdef USE_SENDSPIN_ARTWORK
  if (this->sendspin_hub_ == nullptr) {
    return;
  }
  this->sendspin_hub_->add_artwork_image_callback(
      [this](uint8_t slot, const uint8_t *data, size_t length, sendspin::SendspinImageFormat format) {
        if (slot != this->sendspin_slot_) {
          return;
        }
        bool paused = false;
        {
          LockGuard guard(this->sendspin_pending_lock_);
          this->begin_trace_("sendspin-image", length);
          if constexpr (CONFIG_ESPHOME_ARTWORK_TRACE_VERBOSE) {
            ESP_LOGW(TAG, "artwork trace #%u sendspin image slot=%u format=%s length=%zu", this->trace_id_, slot,
                     sendspin_format_to_string(format), length);
          }
          this->pending_sendspin_data_.assign(data, data + length);
          this->pending_sendspin_format_ = format;
          this->pending_sendspin_image_ = true;
          this->pending_sendspin_clear_ = false;
          this->sendspin_decode_failed_.store(false, std::memory_order_release);
          paused = this->sendspin_paused_;
        }
        if (!paused) {
          this->queue_sendspin_process_();
        }
      });
  this->sendspin_hub_->add_artwork_clear_callback([this](uint8_t slot) {
    if (slot != this->sendspin_slot_) {
      return;
    }
    bool paused = false;
    {
      LockGuard guard(this->sendspin_pending_lock_);
      this->begin_trace_("sendspin-clear");
      if constexpr (CONFIG_ESPHOME_ARTWORK_TRACE_VERBOSE) {
        ESP_LOGW(TAG, "artwork trace #%u sendspin clear slot=%u", this->trace_id_, slot);
      }
      this->pending_sendspin_data_.clear();
      this->pending_sendspin_image_ = false;
      this->pending_sendspin_display_ = false;
      this->pending_sendspin_clear_ = true;
      paused = this->sendspin_paused_;
    }
    if (!paused) {
      this->queue_sendspin_process_();
    }
  });
  this->sendspin_hub_->add_artwork_display_callback([this](uint8_t slot) {
    if (slot != this->sendspin_slot_) {
      return;
    }
    bool paused = false;
    {
      LockGuard guard(this->sendspin_pending_lock_);
      this->trace_event_("sendspin-display");
      if constexpr (CONFIG_ESPHOME_ARTWORK_TRACE_VERBOSE) {
        ESP_LOGW(TAG, "artwork trace #%u sendspin display slot=%u", this->trace_id_, slot);
      }
      this->pending_sendspin_display_ = true;
      paused = this->sendspin_paused_;
    }
    if (!paused) {
      this->queue_sendspin_process_();
    }
  });
#endif
}

#ifdef USE_SENDSPIN_ARTWORK
void ArtworkImage::set_sendspin_paused(bool paused) {
  bool resume = false;
  {
    LockGuard guard(this->sendspin_pending_lock_);
    if (this->sendspin_paused_ == paused) {
      return;
    }
    this->sendspin_paused_ = paused;
    resume = !paused;
  }
  if (resume) {
    this->trace_event_("sendspin-resume");
    this->queue_sendspin_process_();
  }
}

void ArtworkImage::queue_sendspin_process_() {
  if (this->sendspin_process_queued_.exchange(true, std::memory_order_acq_rel)) {
    return;
  }
  this->defer([this]() {
    this->trace_event_("sendspin-process-defer");
    this->sendspin_process_queued_.store(false, std::memory_order_release);
    this->cancel_timeout("sendspin_artwork_process");
    this->set_timeout("sendspin_artwork_process", SENDSPIN_ARTWORK_PROCESS_DELAY_MS,
                      [this]() { this->process_pending_sendspin_(); });
  });
}

void ArtworkImage::process_pending_sendspin_() {
  std::vector<uint8_t> data;
  sendspin::SendspinImageFormat format{sendspin::SendspinImageFormat::JPEG};
  bool has_image = false;
  bool display = false;
  bool clear = false;

  {
    LockGuard guard(this->sendspin_pending_lock_);
    if (this->sendspin_paused_) {
      this->trace_event_("sendspin-process-paused");
      return;
    }
    data = std::move(this->pending_sendspin_data_);
    format = this->pending_sendspin_format_;
    has_image = this->pending_sendspin_image_;
    display = this->pending_sendspin_display_;
    clear = this->pending_sendspin_clear_;
    this->pending_sendspin_image_ = false;
    this->pending_sendspin_display_ = false;
    this->pending_sendspin_clear_ = false;
  }
  this->trace_event_("sendspin-process-start", data.size());

  if (clear) {
    this->trace_event_("sendspin-process-clear");
    this->release();
    this->download_error_callback_.call();
    return;
  }

  if (has_image && !data.empty()) {
    ImageFormat image_format = ImageFormat::AUTO;
    switch (format) {
      case sendspin::SendspinImageFormat::JPEG:
        image_format = ImageFormat::JPEG;
        break;
      case sendspin::SendspinImageFormat::PNG:
        image_format = ImageFormat::PNG;
        break;
      case sendspin::SendspinImageFormat::BMP:
        image_format = ImageFormat::BMP;
        break;
    }
    if constexpr (CONFIG_ESPHOME_ARTWORK_TRACE_VERBOSE) {
      ESP_LOGW(TAG, "artwork trace #%u decode request format=%s size=%zu display=%s", this->trace_id_,
               image_format_to_string(image_format), data.size(), YESNO(display));
    }
    this->sendspin_decode_failed_.store(false, std::memory_order_release);
    if (!this->decode_encoded_image_(image_format, data.data(), data.size(), false)) {
      this->sendspin_decode_failed_.store(true, std::memory_order_release);
    }
  }

  if (!display) {
    this->trace_event_("sendspin-process-wait-display");
    return;
  }
  if (display && !has_image && !this->sendspin_decode_failed_.load(std::memory_order_acquire) &&
      !this->sendspin_decode_ready_.load(std::memory_order_acquire)) {
    LockGuard guard(this->sendspin_pending_lock_);
    if (!this->sendspin_paused_) {
      this->pending_sendspin_display_ = true;
    }
    this->trace_event_("sendspin-display-before-decode-ready");
    return;
  }
  if (this->sendspin_decode_failed_.load(std::memory_order_acquire) ||
      this->sendspin_decode_ready_.load(std::memory_order_acquire)) {
    this->trace_event_("sendspin-queue-finish");
    this->queue_sendspin_finish_();
  }
}

void ArtworkImage::queue_sendspin_finish_() {
  if (this->sendspin_finish_queued_.exchange(true, std::memory_order_acq_rel)) {
    return;
  }
  this->defer([this]() {
    this->trace_event_("sendspin-finish-defer");
    this->sendspin_finish_queued_.store(false, std::memory_order_release);
    if (this->sendspin_decode_failed_.exchange(false, std::memory_order_acq_rel)) {
      this->trace_event_("sendspin-finish-error");
      this->download_error_callback_.call();
      return;
    }
    if (this->sendspin_decode_ready_.exchange(false, std::memory_order_acq_rel)) {
      this->trace_event_("sendspin-finish-download");
      this->finish_download_();
    }
  });
}
#endif

void ArtworkImage::draw(int x, int y, display::Display *display, Color color_on, Color color_off) {
  if (this->data_start_) {
    Image::draw(x, y, display, color_on, color_off);
  } else if (this->placeholder_) {
    this->placeholder_->draw(x, y, display, color_on, color_off);
  }
}

void ArtworkImage::apply_rgb_darken_once(uint8_t percent) {
  if (this->buffer_ == nullptr || percent == 0 || percent >= 100 || this->type_ != image::IMAGE_TYPE_RGB) {
    return;
  }
  if (this->darkened_buffer_ == this->buffer_ && this->darkened_percent_ == percent) {
    return;
  }

  const uint32_t start = millis();
  const size_t size = this->get_buffer_size_();
  if (percent == 50) {
    for (size_t i = 0; i < size; i++) {
      this->buffer_[i] >>= 1;
    }
  } else {
    const uint16_t keep = 100 - percent;
    for (size_t i = 0; i < size; i++) {
      this->buffer_[i] = static_cast<uint8_t>((static_cast<uint16_t>(this->buffer_[i]) * keep) / 100);
    }
  }
  this->darkened_buffer_ = this->buffer_;
  this->darkened_percent_ = percent;
  sync_artwork_buffer_for_dma(this->buffer_, size, false);
  log_slow_artwork_stage("darken-buffer", start);
}

#ifdef USE_LVGL
void ArtworkImage::prepare_lvgl_dsc_() {
  this->lvgl_dsc_slot_ = this->lvgl_dsc_slot_ == 0 ? 1 : 0;
  auto *dsc = &this->lvgl_dsc_slots_[this->lvgl_dsc_slot_];
  memset(dsc, 0, sizeof(*dsc));
  dsc->data = this->data_start_;
  dsc->header.reserved_2 = 0;
  dsc->header.stride = this->get_width_stride();
  dsc->header.w = this->width_;
  dsc->header.h = this->height_;
  dsc->data_size = this->get_width_stride() * this->get_height();
  switch (this->get_type()) {
    case image::IMAGE_TYPE_BINARY:
      dsc->header.cf = LV_COLOR_FORMAT_A1;
      break;
    case image::IMAGE_TYPE_GRAYSCALE:
      dsc->header.cf = LV_COLOR_FORMAT_A8;
      break;
    case image::IMAGE_TYPE_RGB:
      dsc->header.cf = this->transparency_ == image::TRANSPARENCY_ALPHA_CHANNEL ? LV_COLOR_FORMAT_ARGB8888
                                                                                : LV_COLOR_FORMAT_RGB888;
      break;
    case image::IMAGE_TYPE_RGB565:
      dsc->header.cf = this->transparency_ == image::TRANSPARENCY_ALPHA_CHANNEL ? LV_COLOR_FORMAT_RGB565A8
                                                                                : LV_COLOR_FORMAT_RGB565;
      break;
  }
}

lv_image_dsc_t *ArtworkImage::get_lv_image_dsc() {
  auto *dsc = &this->lvgl_dsc_slots_[this->lvgl_dsc_slot_];
  if (dsc->data != this->data_start_ || dsc->header.w != this->width_ || dsc->header.h != this->height_ ||
      dsc->header.stride != this->get_width_stride()) {
    this->prepare_lvgl_dsc_();
    dsc = &this->lvgl_dsc_slots_[this->lvgl_dsc_slot_];
  }
  return dsc;
}
#endif

void ArtworkImage::release(bool immediate) {
  this->update_pending_ = false;
  this->pending_url_.clear();
  this->end_connection_();
  this->retire_active_buffer_();
  this->cleanup_retired_buffers_(immediate);
  if (immediate) {
    this->release_spare_buffer_();
  }
  if (!this->retired_buffers_.empty()) {
    this->enable_loop();
  }
}

uint8_t *ArtworkImage::try_get_staging_buffer_for_decode(int width, int height, int content_width,
                                                         int content_height) {
  if (this->decode_buffer_ != nullptr) {
    return nullptr;
  }
  if (width <= 0 || height <= 0 || content_width != width || content_height != height) {
    return nullptr;
  }
  const size_t size = this->get_buffer_size_(width, height);
  if (this->spare_buffer_ != nullptr && this->spare_buffer_size_ >= size) {
    if (this->spare_buffer_uses_jpeg_allocator_) {
      this->decode_buffer_ = this->spare_buffer_;
      this->decode_buffer_uses_jpeg_allocator_ = true;
      this->spare_buffer_ = nullptr;
      this->spare_buffer_size_ = 0;
      this->spare_buffer_uses_jpeg_allocator_ = false;
    } else {
      this->release_spare_buffer_();
    }
  }
  if (this->decode_buffer_ == nullptr) {
#ifdef USE_ESP32_JPEG
    size_t capacity = 0;
    this->decode_buffer_ = esp32_jpeg::allocate_decode_output(size, &capacity);
    this->decode_buffer_uses_jpeg_allocator_ = this->decode_buffer_ != nullptr;
    if (this->decode_buffer_ == nullptr || capacity < size) {
      if (this->decode_buffer_ != nullptr) {
        esp32_jpeg::release_decode_output(this->decode_buffer_);
        this->decode_buffer_ = nullptr;
        this->decode_buffer_uses_jpeg_allocator_ = false;
      }
      ESP_LOGW(TAG,
               "Hardware JPEG staging allocation failed: requested=%zu capacity=%zu. Biggest block in heap: %zu "
               "Bytes",
               size, capacity, this->allocator_.get_max_free_block_size());
      return nullptr;
    }
#else
    this->decode_buffer_ = this->allocator_.allocate(size);
    this->decode_buffer_uses_jpeg_allocator_ = false;
    if (this->decode_buffer_ == nullptr) {
      ESP_LOGW(TAG, "Hardware JPEG staging allocation failed: %zu bytes. Biggest block in heap: %zu Bytes", size,
               this->allocator_.get_max_free_block_size());
      return nullptr;
    }
#endif
  } else {
    ESP_LOGD(TAG, "Reusing JPEG-compatible artwork staging buffer");
  }
  this->decode_buffer_reuses_active_ = false;
  this->decode_buffer_width_ = width;
  this->decode_buffer_height_ = height;
  this->decode_content_width_ = content_width;
  this->decode_content_height_ = content_height;
  this->decode_offset_x_ = 0;
  this->decode_offset_y_ = 0;
  this->decode_buffer_written_by_dma_ = false;
  ESP_LOGW(TAG, "Using artwork staging buffer for %dx%d hardware decode (%zu bytes)", width, height, size);
  return this->decode_buffer_;
}

void ArtworkImage::cancel_staging_buffer_decode() {
  if (!this->decode_buffer_) {
    return;
  }
  const size_t size = this->get_decode_buffer_size_();
  if (!this->decode_buffer_reuses_active_ && this->spare_buffer_ == nullptr) {
    this->spare_buffer_ = this->decode_buffer_;
    this->spare_buffer_size_ = size;
    this->spare_buffer_uses_jpeg_allocator_ = this->decode_buffer_uses_jpeg_allocator_;
  } else if (!this->decode_buffer_reuses_active_) {
    this->release_buffer_(this->decode_buffer_, size, this->decode_buffer_uses_jpeg_allocator_);
  }
  this->decode_buffer_ = nullptr;
  this->decode_buffer_reuses_active_ = false;
  this->decode_buffer_uses_jpeg_allocator_ = false;
  this->decode_buffer_width_ = 0;
  this->decode_buffer_height_ = 0;
  this->decode_content_width_ = 0;
  this->decode_content_height_ = 0;
  this->decode_offset_x_ = 0;
  this->decode_offset_y_ = 0;
  this->decode_buffer_written_by_dma_ = false;
}

size_t ArtworkImage::resize_(int width_in, int height_in) {
  int width = this->fixed_width_;
  int height = this->fixed_height_;
  int content_width = width;
  int content_height = height;
  int offset_x = 0;
  int offset_y = 0;
  if (this->is_auto_resize_()) {
    width = width_in;
    height = height_in;
    content_width = width;
    content_height = height;
  } else if (width_in > 0 && height_in > 0) {
    if (width_in != height_in) {
      double scale = std::min(
        static_cast<double>(this->fixed_width_) / width_in,
        static_cast<double>(this->fixed_height_) / height_in
      );
      content_width = std::max(1, (static_cast<int>(width_in * scale) + 3) & ~3);
      content_height = std::max(1, (static_cast<int>(height_in * scale) + 3) & ~3);
      if (content_width > this->fixed_width_) content_width = this->fixed_width_;
      if (content_height > this->fixed_height_) content_height = this->fixed_height_;
      offset_x = (this->fixed_width_ - content_width) / 2;
      offset_y = (this->fixed_height_ - content_height) / 2;
    }
  }
  size_t new_size = this->get_buffer_size_(width, height);
  if (this->decode_buffer_) {
    if (new_size <= this->get_decode_buffer_size_()) {
      this->decode_buffer_width_ = width;
      this->decode_buffer_height_ = height;
      this->decode_content_width_ = content_width;
      this->decode_content_height_ = content_height;
      this->decode_offset_x_ = offset_x;
      this->decode_offset_y_ = offset_y;
      this->decode_buffer_written_by_dma_ = false;
      memset(this->decode_buffer_, 0, new_size);
      ESP_LOGI(TAG, "Artwork fit: source=%dx%d target=%dx%d content=%dx%d offset=%d,%d",
               width_in, height_in, width, height, content_width, content_height, offset_x, offset_y);
      return new_size;
    }
    this->release_buffer_(this->decode_buffer_, this->get_decode_buffer_size_(),
                          this->decode_buffer_uses_jpeg_allocator_);
    this->decode_buffer_ = nullptr;
    this->decode_buffer_uses_jpeg_allocator_ = false;
    this->decode_buffer_width_ = 0;
    this->decode_buffer_height_ = 0;
    this->decode_content_width_ = 0;
    this->decode_content_height_ = 0;
    this->decode_offset_x_ = 0;
    this->decode_offset_y_ = 0;
    this->decode_buffer_written_by_dma_ = false;
  }
  ESP_LOGD(TAG, "Allocating decode buffer of %zu bytes", new_size);
  this->decode_buffer_ = this->allocator_.allocate(new_size);
  this->decode_buffer_uses_jpeg_allocator_ = false;
  if (this->decode_buffer_ == nullptr) {
    ESP_LOGE(TAG, "allocation of %zu bytes failed. Biggest block in heap: %zu Bytes", new_size,
             this->allocator_.get_max_free_block_size());
    this->end_connection_();
    return 0;
  }
  this->decode_buffer_width_ = width;
  this->decode_buffer_height_ = height;
  this->decode_content_width_ = content_width;
  this->decode_content_height_ = content_height;
  this->decode_offset_x_ = offset_x;
  this->decode_offset_y_ = offset_y;
  this->decode_buffer_written_by_dma_ = false;
  memset(this->decode_buffer_, 0, new_size);
  ESP_LOGI(TAG, "Artwork fit: source=%dx%d target=%dx%d content=%dx%d offset=%d,%d",
           width_in, height_in, width, height, content_width, content_height, offset_x, offset_y);
  return new_size;
}

void ArtworkImage::request_update_url(const std::string &url) {
  if (!this->validate_url_(url)) {
    return;
  }
  if (this->is_busy_()) {
    this->queue_pending_update_(url);
    return;
  }
  this->url_ = url;
  this->update();
}

void ArtworkImage::update() {
  if (this->is_busy_()) {
    this->queue_pending_update_(this->url_);
    return;
  }
  ESP_LOGI(TAG, "Updating image %s", this->url_.c_str());
  this->log_state_("request-start");

  std::vector<http_request::Header> headers = {};

  http_request::Header accept_header;
  accept_header.name = "Accept";
  std::string accept_mime_type;
  switch (this->format_) {
    case ImageFormat::AUTO:
      accept_mime_type = "image/jpeg, image/png";
      break;
#ifdef USE_ARTWORK_IMAGE_BMP_SUPPORT
    case ImageFormat::BMP:
      accept_mime_type = "image/bmp";
      break;
#endif  // USE_ARTWORK_IMAGE_BMP_SUPPORT
#ifdef USE_ARTWORK_IMAGE_JPEG_SUPPORT
    case ImageFormat::JPEG:
      accept_mime_type = "image/jpeg";
      break;
#endif  // USE_ARTWORK_IMAGE_JPEG_SUPPORT
#ifdef USE_ARTWORK_IMAGE_PNG_SUPPORT
    case ImageFormat::PNG:
      accept_mime_type = "image/png";
      break;
#endif  // USE_ARTWORK_IMAGE_PNG_SUPPORT
    default:
      accept_mime_type = "image/*";
  }
  accept_header.value = accept_mime_type + ",*/*;q=0.8";

  headers.push_back(accept_header);

  for (auto &header : this->request_headers_) {
    headers.push_back(http_request::Header{header.first, header.second.value()});
  }

  if (this->should_use_local_idf_url_(this->url_)) {
    this->downloader_ = this->get_local_idf_(this->url_, headers);
  } else {
    this->downloader_ = this->parent_->get(this->url_, headers, {CONTENT_TYPE_HEADER_NAME});
  }

  if (this->downloader_ == nullptr) {
    ESP_LOGE(TAG, "Download failed.");
    this->end_connection_();
    this->download_error_callback_.call();
    this->start_pending_update_();
    return;
  }

  int http_code = this->downloader_->status_code;
  this->log_state_("response-ready");
  if (http_code == HTTP_CODE_NOT_MODIFIED) {
    // Image hasn't changed on server. Skip download.
    ESP_LOGI(TAG, "Server returned HTTP 304 (Not Modified). Download skipped.");
    this->end_connection_();
    this->download_finished_callback_.call(true);
    this->start_pending_update_();
    return;
  }
  if (http_code != HTTP_CODE_OK) {
    ESP_LOGE(TAG, "HTTP result: %d", http_code);
    this->end_connection_();
    this->download_error_callback_.call();
    this->start_pending_update_();
    return;
  }

  ESP_LOGD(TAG, "Starting download");
  size_t total_size = this->get_sane_content_length_();

  if (this->format_ == ImageFormat::AUTO) {
    ESP_LOGD(TAG, "Deferring auto image format detection until magic bytes are available");
    this->log_state_("format-detect-wait");
    this->start_time_ = ::time(nullptr);
    this->last_data_millis_ = millis();
    this->enable_loop();
    return;
  }

  ImageFormat resolved = this->detect_format_();
  if (!this->create_decoder_(resolved, total_size)) {
    this->end_connection_();
    this->download_error_callback_.call();
    this->start_pending_update_();
    return;
  }
  this->log_state_("decoder-ready");
  ESP_LOGI(TAG, "Downloading image (Size: %zu)", total_size);
  this->start_time_ = ::time(nullptr);
  this->last_data_millis_ = millis();
  this->enable_loop();
}

bool ArtworkImage::should_use_local_idf_url_(const std::string &url) const {
  bool is_http = url.rfind("http://", 0) == 0;
  bool is_https = url.rfind("https://", 0) == 0;
  if (!is_http && !(is_https && this->allow_insecure_local_urls_)) {
    return false;
  }

  size_t host_start = is_https ? 8 : 7;
  size_t host_end = url.find_first_of("/?#", host_start);
  std::string authority = url.substr(host_start, host_end == std::string::npos ? std::string::npos : host_end - host_start);
  size_t at = authority.rfind('@');
  if (at != std::string::npos) {
    authority = authority.substr(at + 1);
  }

  std::string host;
  if (!authority.empty() && authority.front() == '[') {
    size_t end = authority.find(']');
    host = end == std::string::npos ? authority : authority.substr(1, end - 1);
  } else {
    size_t colon = authority.find(':');
    host = colon == std::string::npos ? authority : authority.substr(0, colon);
  }

  std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c) { return std::tolower(c); });
  return this->is_private_or_local_host_(host);
}

bool ArtworkImage::is_private_or_local_host_(const std::string &host) const {
  if (host == "localhost" || host == "homeassistant.local" ||
      (host.size() > 6 && host.compare(host.size() - 6, 6, ".local") == 0)) {
    return true;
  }
  if (host.rfind("fe80:", 0) == 0 || host == "::1") {
    return true;
  }

  int parts[4] = {-1, -1, -1, -1};
  const char *cursor = host.c_str();
  char *end = nullptr;
  for (int i = 0; i < 4; i++) {
    long value = std::strtol(cursor, &end, 10);
    if (end == cursor || value < 0 || value > 255) {
      return false;
    }
    parts[i] = static_cast<int>(value);
    if (i < 3) {
      if (*end != '.') return false;
      cursor = end + 1;
    } else if (*end != '\0') {
      return false;
    }
  }

  return parts[0] == 10 || parts[0] == 127 || (parts[0] == 192 && parts[1] == 168) ||
         (parts[0] == 172 && parts[1] >= 16 && parts[1] <= 31) || (parts[0] == 169 && parts[1] == 254);
}

std::shared_ptr<http_request::HttpContainer> ArtworkImage::get_local_idf_(
    const std::string &url, const std::vector<http_request::Header> &headers) {
#ifdef USE_ESP_IDF
  bool secure = url.rfind("https://", 0) == 0;
  if (secure) {
    ESP_LOGW(TAG, "Using insecure TLS for local artwork URL: %s", url.c_str());
  } else {
    ESP_LOGD(TAG, "Using guarded local artwork request: %s", url.c_str());
  }
  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.method = HTTP_METHOD_GET;
  config.timeout_ms = std::min<int>(this->parent_->get_timeout(), LOCAL_ARTWORK_HTTP_CONNECT_TIMEOUT_MS);
  config.disable_auto_redirect = false;
  config.max_redirection_count = 3;
  config.auth_type = HTTP_AUTH_TYPE_BASIC;
  config.event_handler = insecure_local_http_event_handler;

  uint32_t stage_start = millis();
  esp_http_client_handle_t client = esp_http_client_init(&config);
  log_slow_artwork_stage("local-init", stage_start);
  if (client == nullptr) {
    ESP_LOGE(TAG, "Local artwork request failed; client could not be initialized");
    return nullptr;
  }

  auto container = std::make_shared<LocalHttpContainer>(client);
  container->set_parent(this->parent_);
  container->set_secure(secure);
  esp_http_client_set_user_data(client, static_cast<void *>(container.get()));

  for (const auto &header : headers) {
    esp_http_client_set_header(client, header.name.c_str(), header.value.c_str());
  }

  const uint32_t start = millis();
  App.feed_wdt();
  stage_start = millis();
  esp_err_t err = esp_http_client_open(client, 0);
  log_slow_artwork_stage("local-open", stage_start);
  App.feed_wdt();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Local artwork request failed: %s", esp_err_to_name(err));
    container->end();
    return nullptr;
  }

  stage_start = millis();
  int content_length = esp_http_client_fetch_headers(client);
  log_slow_artwork_stage("local-fetch-headers", stage_start);
  App.feed_wdt();
  container->content_length = content_length > 0 ? static_cast<size_t>(content_length) : 0;
  container->set_chunked(esp_http_client_is_chunked_response(client));
  container->status_code = esp_http_client_get_status_code(client);
  container->duration_ms = millis() - start;
  esp_http_client_set_timeout_ms(client, LOCAL_ARTWORK_HTTP_READ_TIMEOUT_MS);
  return container;
#else
  return this->parent_->get(url, headers, {CONTENT_TYPE_HEADER_NAME});
#endif
}

size_t ArtworkImage::get_sane_content_length_() const {
  if (!this->downloader_) {
    return 0;
  }
  size_t content_length = this->downloader_->content_length;
  if (content_length > MAX_DOWNLOAD_BUFFER_SIZE) {
    ESP_LOGW(TAG, "Ignoring invalid artwork content length: %zu", content_length);
    return 0;
  }
  return content_length;
}

void ArtworkImage::loop() {
  this->cleanup_retired_buffers_(false);
  if (!this->decoder_ && !this->downloader_) {
    if (this->retired_buffers_.empty()) {
      this->disable_loop();
    }
    return;
  }

  // Deferred decoder creation for AUTO format: read data for magic-byte detection
  if (!this->decoder_ && this->downloader_) {
    if (!this->ensure_download_buffer_capacity_()) {
      this->fail_download_();
      return;
    }

    size_t available = std::min(this->download_buffer_.free_capacity(),
                                std::min(this->download_buffer_initial_size_, MAX_READ_CHUNK_SIZE));
    auto len = this->downloader_->read(this->download_buffer_.append(), available);
    bool transfer_complete = false;
    if (len > 0) {
      this->download_buffer_.write(len);
      this->last_data_millis_ = millis();
    } else if (len < 0) {
      ESP_LOGE(TAG, "Download failed while detecting image format: %d", len);
      this->fail_download_();
      return;
    } else if (this->downloader_->is_read_complete()) {
      transfer_complete = true;
      if (this->download_buffer_.unread() < 12) {
        ESP_LOGE(TAG, "Download finished before enough data was received to detect image format");
        this->fail_download_();
        return;
      }
    }

    if (this->download_buffer_.unread() < 12) {
      if (millis() - this->last_data_millis_ > DOWNLOAD_STALL_TIMEOUT_MS) {
        ESP_LOGE(TAG, "Download stalled waiting for format detection bytes");
        this->end_connection_();
        this->download_error_callback_.call();
        this->start_pending_update_();
      }
      return;
    }

    ImageFormat resolved = this->detect_format_();
    if (resolved == ImageFormat::AUTO) {
      ESP_LOGE(TAG, "Could not determine image format from headers or file content");
      this->end_connection_();
      this->download_error_callback_.call();
      this->start_pending_update_();
      return;
    }

    size_t total_size = this->get_sane_content_length_();
    if (total_size == 0 && transfer_complete) {
      total_size = this->downloader_->get_bytes_read();
    }
    if (!this->create_decoder_(resolved, total_size)) {
      this->end_connection_();
      this->download_error_callback_.call();
      this->start_pending_update_();
      return;
    }
    this->log_state_("decoder-ready");
    ESP_LOGI(TAG, "Downloading image (Size: %zu)", total_size);

    // Feed already-buffered data to the newly created decoder
    if (!this->decode_buffered_data_()) {
      this->fail_download_();
      return;
    }
    if (this->decoder_->is_finished()) {
      this->finish_download_();
    }
    return;
  }

  if (this->decoder_->is_finished()) {
    this->finish_download_();
    return;
  }
  if (this->downloader_ == nullptr) {
    ESP_LOGE(TAG, "Downloader not instantiated; cannot download");
    return;
  }

  if (!this->ensure_download_buffer_capacity_()) {
    this->fail_download_();
    return;
  }

  size_t available = std::min(this->download_buffer_.free_capacity(),
                              std::min(this->download_buffer_initial_size_, MAX_READ_CHUNK_SIZE));
  auto len = this->downloader_->read(this->download_buffer_.append(), available);
  if (len > 0) {
    this->download_buffer_.write(len);
    this->last_data_millis_ = millis();
    if (!this->decode_buffered_data_()) {
      this->fail_download_();
      return;
    }
    if (this->decoder_->is_finished()) {
      this->finish_download_();
    }
    return;
  }

  if (len < 0) {
    ESP_LOGE(TAG, "Download failed while reading image data: %d", len);
    this->fail_download_();
    return;
  }

  if (this->downloader_->is_read_complete()) {
    if (this->decoder_->has_unknown_download_size()) {
      this->decoder_->set_download_size(this->downloader_->get_bytes_read());
      ESP_LOGD(TAG, "HTTP transfer complete; inferred image size: %zu bytes", this->downloader_->get_bytes_read());
    }
    if (!this->decode_buffered_data_()) {
      this->fail_download_();
      return;
    }
    if (this->decoder_->is_finished()) {
      this->finish_download_();
      return;
    }
    ESP_LOGE(TAG, "HTTP transfer finished before image decoder completed");
    this->fail_download_();
    return;
  }

  if (millis() - this->last_data_millis_ > DOWNLOAD_STALL_TIMEOUT_MS) {
    ESP_LOGE(TAG, "Download stalled: no data received for %" PRIu32 "ms (buffered %zu bytes)",
             DOWNLOAD_STALL_TIMEOUT_MS, this->download_buffer_.unread());
    this->fail_download_();
    return;
  }
}

void ArtworkImage::map_chroma_key(Color &color) {
  if (this->transparency_ == image::TRANSPARENCY_CHROMA_KEY) {
    if (color.g == 1 && color.r == 0 && color.b == 0) {
      color.g = 0;
    }
    if (color.w < 0x80) {
      color.r = 0;
      color.g = this->type_ == ImageType::IMAGE_TYPE_RGB565 ? 4 : 1;
      color.b = 0;
    }
  }
}

void ArtworkImage::draw_pixel_(int x, int y, Color color) {
  if (!this->decode_buffer_) {
    ESP_LOGE(TAG, "Decode buffer not allocated!");
    return;
  }
  if (x < 0 || y < 0 || x >= this->decode_buffer_width_ || y >= this->decode_buffer_height_) {
    ESP_LOGE(TAG, "Tried to paint a pixel (%d,%d) outside the image!", x, y);
    return;
  }
  uint32_t pos = this->get_position_(x, y);
  switch (this->type_) {
    case ImageType::IMAGE_TYPE_BINARY: {
      const uint32_t width_8 = ((this->decode_buffer_width_ + 7u) / 8u) * 8u;
      pos = x + y * width_8;
      auto bitno = 0x80 >> (pos % 8u);
      pos /= 8u;
      auto on = is_color_on(color);
      if (this->has_transparency() && color.w < 0x80)
        on = false;
      if (on) {
        this->decode_buffer_[pos] |= bitno;
      } else {
        this->decode_buffer_[pos] &= ~bitno;
      }
      break;
    }
    case ImageType::IMAGE_TYPE_GRAYSCALE: {
      auto gray = static_cast<uint8_t>(0.2125 * color.r + 0.7154 * color.g + 0.0721 * color.b);
      if (this->transparency_ == image::TRANSPARENCY_CHROMA_KEY) {
        if (gray == 1) {
          gray = 0;
        }
        if (color.w < 0x80) {
          gray = 1;
        }
      } else if (this->transparency_ == image::TRANSPARENCY_ALPHA_CHANNEL) {
        if (color.w != 0xFF)
          gray = color.w;
      }
      this->decode_buffer_[pos] = gray;
      break;
    }
    case ImageType::IMAGE_TYPE_RGB565: {
      this->map_chroma_key(color);
      uint16_t col565 = display::ColorUtil::color_to_565(color);
      if (this->is_big_endian_) {
        this->decode_buffer_[pos + 0] = static_cast<uint8_t>((col565 >> 8) & 0xFF);
        this->decode_buffer_[pos + 1] = static_cast<uint8_t>(col565 & 0xFF);
      } else {
        this->decode_buffer_[pos + 0] = static_cast<uint8_t>(col565 & 0xFF);
        this->decode_buffer_[pos + 1] = static_cast<uint8_t>((col565 >> 8) & 0xFF);
      }
      if (this->transparency_ == image::TRANSPARENCY_ALPHA_CHANNEL) {
        this->decode_buffer_[pos + 2] = color.w;
      }
      break;
    }
    case ImageType::IMAGE_TYPE_RGB: {
      this->map_chroma_key(color);
      this->decode_buffer_[pos + 0] = color.b;
      this->decode_buffer_[pos + 1] = color.g;
      this->decode_buffer_[pos + 2] = color.r;
      if (this->transparency_ == image::TRANSPARENCY_ALPHA_CHANNEL) {
        this->decode_buffer_[pos + 3] = color.w;
      }
      break;
    }
  }
}

ImageFormat ArtworkImage::detect_format_() {
  if (this->format_ != ImageFormat::AUTO) {
    return this->format_;
  }

  // Prefer magic bytes because Home Assistant proxy headers can be stale for
  // identical media_player_proxy paths whose cache parameter points at new art.
  if (this->download_buffer_.unread() >= 4) {
    const uint8_t *data = this->download_buffer_.data();
    if (data[0] == 0xFF && data[1] == 0xD8) {
      if (this->detect_progressive_jpeg_()) {
        ESP_LOGW(TAG, "Detected progressive JPEG from magic bytes: %s", this->url_.c_str());
      } else {
        ESP_LOGD(TAG, "Detected JPEG from magic bytes; decoder will report baseline/progressive from the header");
      }
      return ImageFormat::JPEG;
    }
    if (data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47) {
      ESP_LOGD(TAG, "Detected PNG from magic bytes");
      return ImageFormat::PNG;
    }
    if (this->detect_heic_()) {
      ESP_LOGW(TAG, "Detected HEIC/HEIF from file signature");
      return ImageFormat::HEIC;
    }
    if (data[0] == 0x42 && data[1] == 0x4D) {
      ESP_LOGD(TAG, "Detected BMP from magic bytes");
      return ImageFormat::BMP;
    }
  }

  // Fallback: Content-Type header
  if (this->downloader_) {
    std::string ct = str_lower_case(this->downloader_->get_response_header(CONTENT_TYPE_HEADER_NAME));
    if (ct.find("image/jpeg") != std::string::npos || ct.find("image/jpg") != std::string::npos) {
      ESP_LOGD(TAG, "Detected JPEG from Content-Type: %s", ct.c_str());
      return ImageFormat::JPEG;
    }
    if (ct.find("image/png") != std::string::npos) {
      ESP_LOGD(TAG, "Detected PNG from Content-Type: %s", ct.c_str());
      return ImageFormat::PNG;
    }
    if (ct.find("image/heic") != std::string::npos || ct.find("image/heif") != std::string::npos) {
      ESP_LOGW(TAG, "Detected HEIC/HEIF from Content-Type: %s", ct.c_str());
      return ImageFormat::HEIC;
    }
    if (ct.find("image/bmp") != std::string::npos) {
      ESP_LOGD(TAG, "Detected BMP from Content-Type: %s", ct.c_str());
      return ImageFormat::BMP;
    }
  }

  return ImageFormat::AUTO;
}

bool ArtworkImage::detect_progressive_jpeg_() {
  size_t len = this->download_buffer_.unread();
  const uint8_t *data = this->download_buffer_.data();
  if (len < 4 || data[0] != 0xFF || data[1] != 0xD8) {
    return false;
  }

  size_t pos = 2;
  while (pos + 3 < len) {
    while (pos < len && data[pos] != 0xFF) pos++;
    while (pos < len && data[pos] == 0xFF) pos++;
    if (pos >= len) break;

    uint8_t marker = data[pos++];
    if (marker == 0xDA || marker == 0xD9) {
      break;
    }
    if (marker >= 0xD0 && marker <= 0xD7) {
      continue;
    }
    if (pos + 1 >= len) break;
    uint16_t segment_len = (static_cast<uint16_t>(data[pos]) << 8) | data[pos + 1];
    if (segment_len < 2) break;

    if (marker == 0xC2) {
      return true;
    }
    if (marker == 0xC0) {
      return false;
    }
    pos += segment_len;
  }
  return false;
}

bool ArtworkImage::detect_heic_() {
  size_t len = this->download_buffer_.unread();
  const uint8_t *data = this->download_buffer_.data();
  if (len < 12) {
    return false;
  }
  if (data[4] != 'f' || data[5] != 't' || data[6] != 'y' || data[7] != 'p') {
    return false;
  }

  for (size_t pos = 8; pos + 3 < len && pos < 64; pos += 4) {
    if ((data[pos] == 'h' && data[pos + 1] == 'e' && data[pos + 2] == 'i' &&
         (data[pos + 3] == 'c' || data[pos + 3] == 'x')) ||
        (data[pos] == 'h' && data[pos + 1] == 'e' && data[pos + 2] == 'v' &&
         (data[pos + 3] == 'c' || data[pos + 3] == 'x')) ||
        (data[pos] == 'm' && data[pos + 1] == 'i' && data[pos + 2] == 'f' && data[pos + 3] == '1') ||
        (data[pos] == 'm' && data[pos + 1] == 's' && data[pos + 2] == 'f' && data[pos + 3] == '1')) {
      return true;
    }
  }
  return false;
}

bool ArtworkImage::create_decoder_(ImageFormat format, size_t total_size) {
  if (format == ImageFormat::HEIC) {
    ESP_LOGE(TAG, "HEIC/HEIF artwork detected, but no native HEIC decoder is bundled for this firmware");
    return false;
  }
#ifdef USE_ARTWORK_IMAGE_BMP_SUPPORT
  if (format == ImageFormat::BMP) {
    ESP_LOGD(TAG, "Allocating BMP decoder");
    this->decoder_ = make_unique<BmpDecoder>(this);
  }
#endif
#ifdef USE_ARTWORK_IMAGE_JPEG_SUPPORT
  if (format == ImageFormat::JPEG) {
    ESP_LOGD(TAG, "Allocating JPEG decoder");
    this->decoder_ = esphome::make_unique<JpegDecoder>(this);
  }
#endif
#ifdef USE_ARTWORK_IMAGE_PNG_SUPPORT
  if (format == ImageFormat::PNG) {
    ESP_LOGD(TAG, "Allocating PNG decoder");
    this->decoder_ = make_unique<PngDecoder>(this);
  }
#endif
  if (!this->decoder_) {
    ESP_LOGE(TAG, "Could not instantiate decoder. Image format unsupported: %d", format);
    return false;
  }
  if (this->decoder_->prepare(total_size) < 0) {
    this->decoder_.reset();
    return false;
  }
  return true;
}

void ArtworkImage::discard_decode_buffer_() {
  if (this->decode_buffer_) {
    if (!this->decode_buffer_reuses_active_) {
      const size_t size = this->get_decode_buffer_size_();
      if (this->spare_buffer_ == nullptr) {
        this->spare_buffer_ = this->decode_buffer_;
        this->spare_buffer_size_ = size;
        this->spare_buffer_uses_jpeg_allocator_ = this->decode_buffer_uses_jpeg_allocator_;
      } else {
        this->release_buffer_(this->decode_buffer_, size, this->decode_buffer_uses_jpeg_allocator_);
      }
    }
    this->decode_buffer_ = nullptr;
  }
  this->decode_buffer_reuses_active_ = false;
  this->decode_buffer_uses_jpeg_allocator_ = false;
  this->decode_buffer_width_ = 0;
  this->decode_buffer_height_ = 0;
  this->decode_content_width_ = 0;
  this->decode_content_height_ = 0;
  this->decode_offset_x_ = 0;
  this->decode_offset_y_ = 0;
  this->decode_buffer_written_by_dma_ = false;
}

void ArtworkImage::release_spare_buffer_() {
  if (this->spare_buffer_ != nullptr) {
    this->release_buffer_(this->spare_buffer_, this->spare_buffer_size_, this->spare_buffer_uses_jpeg_allocator_);
    this->spare_buffer_ = nullptr;
    this->spare_buffer_size_ = 0;
    this->spare_buffer_uses_jpeg_allocator_ = false;
  }
}

void ArtworkImage::release_buffer_(uint8_t *buffer, size_t size, bool jpeg_allocator) {
  if (buffer == nullptr) {
    return;
  }
#ifdef USE_ESP32_JPEG
  if (jpeg_allocator) {
    esp32_jpeg::release_decode_output(buffer);
    return;
  }
#else
  (void) jpeg_allocator;
#endif
  this->allocator_.deallocate(buffer, size);
}

bool ArtworkImage::promote_decode_buffer_() {
  if (!this->decode_buffer_) {
    ESP_LOGE(TAG, "Decode finished without a decoded image buffer");
    return false;
  }
  if (this->decode_buffer_width_ <= 0 || this->decode_buffer_height_ <= 0) {
    ESP_LOGE(TAG, "Decode finished with invalid dimensions: %dx%d", this->decode_buffer_width_,
             this->decode_buffer_height_);
    return false;
  }

  const bool reused_active_buffer = this->decode_buffer_reuses_active_;
  const bool written_by_dma = this->decode_buffer_written_by_dma_;
  const bool jpeg_allocator = this->decode_buffer_uses_jpeg_allocator_;
  if (!reused_active_buffer) {
    this->retire_active_buffer_();
  }
  this->buffer_ = this->decode_buffer_;
  this->buffer_uses_jpeg_allocator_ = jpeg_allocator;
  this->buffer_width_ = this->decode_buffer_width_;
  this->buffer_height_ = this->decode_buffer_height_;
  this->buffer_content_width_ = this->decode_content_width_;
  this->buffer_content_height_ = this->decode_content_height_;
  this->buffer_offset_x_ = this->decode_offset_x_;
  this->buffer_offset_y_ = this->decode_offset_y_;
  this->darkened_buffer_ = nullptr;
  this->darkened_percent_ = 0;
  ESP_LOGI(TAG, "Artwork buffer ready: image=%dx%d content=%dx%d offset=%d,%d",
           this->buffer_width_, this->buffer_height_, this->buffer_content_width_, this->buffer_content_height_,
           this->buffer_offset_x_, this->buffer_offset_y_);
  this->decode_buffer_ = nullptr;
  this->decode_buffer_reuses_active_ = false;
  this->decode_buffer_uses_jpeg_allocator_ = false;
  this->decode_buffer_width_ = 0;
  this->decode_buffer_height_ = 0;
  this->decode_content_width_ = 0;
  this->decode_content_height_ = 0;
  this->decode_offset_x_ = 0;
  this->decode_offset_y_ = 0;
  this->decode_buffer_written_by_dma_ = false;

  this->data_start_ = this->buffer_;
  this->width_ = this->buffer_width_;
  this->height_ = this->buffer_height_;
  if (!written_by_dma) {
    sync_artwork_buffer_for_dma(this->buffer_, this->get_buffer_size_(), false);
  }
  this->apply_rgb_darken_once(this->darken_percent_);
#ifdef USE_LVGL
  this->prepare_lvgl_dsc_();
#endif
  return true;
}

void ArtworkImage::retire_active_buffer_() {
  if (!this->buffer_) {
    return;
  }
  auto *retired = this->buffer_;
  this->retired_buffers_.push_back(
      RetiredBuffer{retired, this->get_buffer_size_(), millis(), this->buffer_uses_jpeg_allocator_});
  this->buffer_ = nullptr;
  this->buffer_uses_jpeg_allocator_ = false;
  this->data_start_ = nullptr;
  this->buffer_width_ = 0;
  this->buffer_height_ = 0;
  this->buffer_content_width_ = 0;
  this->buffer_content_height_ = 0;
  this->buffer_offset_x_ = 0;
  this->buffer_offset_y_ = 0;
  if (this->darkened_buffer_ == retired) {
    this->darkened_buffer_ = nullptr;
    this->darkened_percent_ = 0;
  }
  this->width_ = 0;
  this->height_ = 0;
  this->cleanup_retired_buffers_(false);
}

void ArtworkImage::cleanup_retired_buffers_(bool force) {
  uint32_t now = millis();
  auto it = this->retired_buffers_.begin();
  while (it != this->retired_buffers_.end()) {
    if (force || now - it->retired_at >= RETIRED_BUFFER_GRACE_MS ||
        this->retired_buffers_.size() > MAX_RETIRED_BUFFERS) {
      if (!force && this->spare_buffer_ == nullptr) {
        this->spare_buffer_ = it->data;
        this->spare_buffer_size_ = it->size;
        this->spare_buffer_uses_jpeg_allocator_ = it->jpeg_allocator;
      } else {
        this->release_buffer_(it->data, it->size, it->jpeg_allocator);
      }
      it = this->retired_buffers_.erase(it);
    } else {
      ++it;
    }
  }
  if (force) {
    this->release_spare_buffer_();
  }
}

bool ArtworkImage::ensure_download_buffer_capacity_() {
  if (this->download_buffer_.free_capacity() > 0) {
    return true;
  }

  size_t current_size = this->download_buffer_.size();
  size_t target_size = current_size == 0 ? this->download_buffer_initial_size_ : current_size * 2;
  if (target_size > MAX_DOWNLOAD_BUFFER_SIZE) {
    target_size = MAX_DOWNLOAD_BUFFER_SIZE;
  }
  if (target_size <= current_size) {
    ESP_LOGE(TAG, "Artwork download exceeded %zu bytes", MAX_DOWNLOAD_BUFFER_SIZE);
    return false;
  }

  ESP_LOGD(TAG, "Growing download buffer from %zu to %zu bytes", current_size, target_size);
  return this->download_buffer_.resize(target_size) == target_size;
}

bool ArtworkImage::decode_encoded_image_(ImageFormat format, const uint8_t *data, size_t length, bool finish_on_decode) {
  this->trace_event_("decode-encoded-start", length);
  if (data == nullptr || length == 0) {
    ESP_LOGE(TAG, "Sendspin artwork image is empty");
    return false;
  }
  if (length > MAX_DOWNLOAD_BUFFER_SIZE) {
    ESP_LOGE(TAG, "Sendspin artwork image too large: %zu bytes (max %zu)", length, MAX_DOWNLOAD_BUFFER_SIZE);
    return false;
  }

  const uint32_t start = millis();
  this->end_connection_();
  this->trace_event_("decode-after-end-connection", length);

#ifdef USE_ARTWORK_IMAGE_JPEG_SUPPORT
  if (format == ImageFormat::JPEG) {
    // SendSpin already delivers a complete encoded image. Avoid copying it
    // into DownloadBuffer before the JPEG decoder copies it into its DMA input
    // buffer. This keeps artwork changes from doing one extra PSRAM pass.
    this->download_buffer_.reset();
    this->decoder_ = esphome::make_unique<JpegDecoder>(this);
    this->decoder_->set_download_size(length);
    const int fed = this->decoder_->decode(const_cast<uint8_t *>(data), length);
    log_slow_artwork_stage("sendspin-jpeg-direct-decode", start);
    this->trace_event_("decode-direct-jpeg-end", static_cast<size_t>(std::max(fed, 0)));
    if (fed < 0) {
      ESP_LOGE(TAG, "Error when decoding JPEG artwork.");
      this->end_connection_();
      return false;
    }
    if (static_cast<size_t>(fed) > length || !this->decoder_->is_finished()) {
      ESP_LOGE(TAG, "JPEG artwork decoder did not finish after %zu bytes", length);
      this->end_connection_();
      return false;
    }
    this->start_time_ = ::time(nullptr);
    if (finish_on_decode) {
      this->finish_download_();
    } else {
#ifdef USE_SENDSPIN_ARTWORK
      this->decoder_.reset();
      this->sendspin_decode_ready_.store(true, std::memory_order_release);
      this->trace_event_("decode-ready-deferred", length);
#else
      this->finish_download_();
#endif
    }
    this->trace_event_("decode-encoded-end", length);
    return true;
  }
#endif

  this->download_buffer_.reset();
  if (this->download_buffer_.resize(length) < length) {
    ESP_LOGE(TAG, "Sendspin artwork buffer resize failed: %zu bytes", length);
    return false;
  }
  memcpy(this->download_buffer_.append(), data, length);
  this->download_buffer_.write(length);
  this->trace_event_("decode-buffer-filled", length);

  if (!this->create_decoder_(format, length)) {
    this->end_connection_();
    return false;
  }
  if (!this->decode_buffered_data_()) {
    this->end_connection_();
    return false;
  }
  if (!this->decoder_->is_finished()) {
    ESP_LOGE(TAG, "Sendspin artwork decoder did not finish after %zu bytes", length);
    this->end_connection_();
    return false;
  }
  this->trace_event_("decode-complete", length);

  this->start_time_ = ::time(nullptr);
  if (finish_on_decode) {
    this->finish_download_();
  } else {
#ifdef USE_SENDSPIN_ARTWORK
    this->decoder_.reset();
    this->download_buffer_.reset();
    this->sendspin_decode_ready_.store(true, std::memory_order_release);
    this->trace_event_("decode-ready-deferred", length);
#else
    this->finish_download_();
#endif
  }
  log_slow_artwork_stage("sendspin-decode", start);
  this->trace_event_("decode-encoded-end", length);
  return true;
}

bool ArtworkImage::decode_buffered_data_() {
  if (!this->decoder_ || this->download_buffer_.unread() == 0) {
    return true;
  }

  size_t unread = this->download_buffer_.unread();
  const uint32_t start = millis();
  this->trace_event_("decode-buffered-start", unread);
  auto fed = this->decoder_->decode(this->download_buffer_.data(), unread);
  log_slow_artwork_stage("decode-buffered", start);
  this->trace_event_("decode-buffered-end", static_cast<size_t>(std::max(fed, 0)));
  if (fed < 0) {
    ESP_LOGE(TAG, "Error when decoding image.");
    return false;
  }
  if (static_cast<size_t>(fed) > unread) {
    ESP_LOGE(TAG, "Decoder consumed %d bytes, but only %zu were buffered", fed, unread);
    return false;
  }
  this->download_buffer_.read(fed);
  return true;
}

void ArtworkImage::finish_download_() {
  this->trace_event_("finish-start");
  uint32_t stage_start = millis();
  if (!this->promote_decode_buffer_()) {
    this->fail_download_();
    return;
  }
  log_slow_artwork_stage("finish-promote", stage_start);
  this->trace_event_("finish-promote");
  this->log_state_("download-complete");
  ESP_LOGD(TAG, "Image fully downloaded, read %zu bytes, width/height = %d/%d",
           this->downloader_ ? this->downloader_->get_bytes_read() : 0, this->width_, this->height_);
  ESP_LOGD(TAG, "Total time: %" PRIu32 "s", (uint32_t) (::time(nullptr) - this->start_time_));
  App.feed_wdt();
  stage_start = millis();
#ifdef USE_LVGL
#if ESPHOME_VERSION_CODE >= VERSION_CODE(2026, 4, 0)
  this->get_lv_image_dsc();
#else
  this->get_lv_img_dsc();
#endif
#endif
  log_slow_artwork_stage("finish-lvgl-descriptor", stage_start);
  this->trace_event_("finish-lvgl-descriptor");
  this->log_state_("lvgl-descriptor-ready");
  stage_start = millis();
  this->log_memory_summary_("ready");
  log_slow_artwork_stage("finish-memory-log", stage_start);
  this->trace_event_("finish-memory-log");
  App.feed_wdt();
  stage_start = millis();
  this->end_connection_();
  log_slow_artwork_stage("finish-end-connection", stage_start);
  this->trace_event_("finish-end-connection");
  this->defer([this]() {
    uint32_t stage_start = millis();
    this->trace_event_("finish-callback-start");
    this->download_finished_callback_.call(false);
    log_slow_artwork_stage("finish-callback", stage_start);
    this->trace_event_("finish-callback-end");
    App.feed_wdt();
    this->log_state_("download-callback-finished");
    stage_start = millis();
    this->start_pending_update_();
    log_slow_artwork_stage("finish-start-pending", stage_start);
  });
}

void ArtworkImage::fail_download_() {
  this->end_connection_();
  this->defer([this]() {
    this->download_error_callback_.call();
    this->start_pending_update_();
  });
}

void ArtworkImage::queue_pending_update_(const std::string &url) {
  if (!this->validate_url_(url)) {
    return;
  }
  bool replaced = this->update_pending_ && this->pending_url_ != url;
  this->pending_url_ = url;
  this->update_pending_ = true;
  ESP_LOGW(TAG, "Artwork update %s while busy; latest URL will run after current work finishes",
           replaced ? "re-queued" : "queued");
  this->log_state_("update-queued");
}

void ArtworkImage::start_pending_update_() {
  if (!this->update_pending_ || this->is_busy_()) {
    return;
  }
  std::string url = this->pending_url_;
  this->pending_url_.clear();
  this->update_pending_ = false;
  ESP_LOGI(TAG, "Starting queued artwork update");
  this->url_ = url;
  this->update();
}

void ArtworkImage::log_state_(const char *stage) {
  size_t heap_free = 0;
  size_t heap_largest = this->allocator_.get_max_free_block_size();
#ifdef USE_ESP32
  heap_free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  heap_largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
#endif
  size_t bytes_read = this->downloader_ ? this->downloader_->get_bytes_read() : 0;
  size_t content_length = this->downloader_ ? this->downloader_->content_length : 0;
  ESP_LOGD(TAG,
           "State %-24s url_len=%zu http=%zu/%zu dl_buf=%zu/%zu image=%dx%d content=%dx%d@%d,%d decode=%dx%d content=%dx%d@%d,%d retired=%zu heap_free=%zu heap_largest=%zu pending=%s",
           stage, this->url_.size(), bytes_read, content_length, this->download_buffer_.unread(),
           this->download_buffer_.size(), this->buffer_width_, this->buffer_height_, this->buffer_content_width_,
           this->buffer_content_height_, this->buffer_offset_x_, this->buffer_offset_y_, this->decode_buffer_width_,
           this->decode_buffer_height_, this->decode_content_width_, this->decode_content_height_,
           this->decode_offset_x_, this->decode_offset_y_, this->retired_buffers_.size(), heap_free, heap_largest,
           this->update_pending_ ? "yes" : "no");
}

void ArtworkImage::end_connection_() {
  if (this->downloader_) {
    this->downloader_->end();
    this->downloader_ = nullptr;
  }
  this->decoder_.reset();
  this->discard_decode_buffer_();
  this->download_buffer_.reset();
}

bool ArtworkImage::validate_url_(const std::string &url) {
  if ((url.length() < 8) || !url.starts_with("http") || (url.find("://") == std::string::npos)) {
    ESP_LOGE(TAG, "URL is invalid and/or must be prefixed with 'http://' or 'https://'");
    return false;
  }
  return true;
}

}  // namespace artwork_image
}  // namespace esphome
