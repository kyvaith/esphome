#include "runtime_image.h"
#include "image_decoder.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>

#ifdef USE_RUNTIME_IMAGE_BMP
#include "bmp_decoder.h"
#endif
#ifdef USE_RUNTIME_IMAGE_JPEG_SOFTWARE
#include "jpeg_decoder.h"
#endif
#ifdef USE_RUNTIME_IMAGE_ESP32_JPEG
#include "esp32_jpeg_decoder.h"
#endif
#ifdef USE_RUNTIME_IMAGE_PNG
#include "png_decoder.h"
#endif

namespace esphome::runtime_image {

static const char *const TAG = "runtime_image";

// Widest supported format is 4 bytes/pixel, so 32767 * 32767 * 4 still fits a 32-bit size_t
static constexpr int MAX_IMAGE_DIMENSION = 32767;
static constexpr int MAX_IMAGE_BPP = 32;
static_assert((static_cast<uint64_t>(MAX_IMAGE_BPP) * MAX_IMAGE_DIMENSION + 7) / 8 * MAX_IMAGE_DIMENSION <=
                  std::numeric_limits<size_t>::max(),
              "MAX_IMAGE_DIMENSION must keep the worst-case buffer size within size_t");

inline bool is_color_on(const Color &color) {
  // This produces the most accurate monochrome conversion, but is slightly slower.
  //  return (0.2125 * color.r + 0.7154 * color.g + 0.0721 * color.b) > 127;

  // Approximation using fast integer computations; produces acceptable results
  // Equivalent to 0.25 * R + 0.5 * G + 0.25 * B
  return ((color.r >> 2) + (color.g >> 1) + (color.b >> 2)) & 0x80;
}

RuntimeImage::RuntimeImage(ImageFormat format, image::ImageType type, image::Transparency transparency,
                           image::Image *placeholder, bool is_big_endian, int fixed_width, int fixed_height)
    : Image(nullptr, 0, 0, type, transparency),
      format_(format),
      fixed_width_(fixed_width),
      fixed_height_(fixed_height),
      placeholder_(placeholder),
      is_big_endian_(is_big_endian) {}

RuntimeImage::~RuntimeImage() { this->release(); }

int RuntimeImage::get_buffer_width() const {
  if (!this->progressive_display_ && this->decoder_ != nullptr) {
    return this->decode_buffer_width_;
  }
  return this->buffer_width_;
}

int RuntimeImage::get_buffer_height() const {
  if (!this->progressive_display_ && this->decoder_ != nullptr) {
    return this->decode_buffer_height_;
  }
  return this->buffer_height_;
}

int RuntimeImage::resize(int width, int height) {
  // Use fixed dimensions if specified (0 means auto-resize)
  int target_width = this->fixed_width_ ? this->fixed_width_ : width;
  int target_height = this->fixed_height_ ? this->fixed_height_ : height;

  // When both fixed dimensions are set, scale uniformly to preserve aspect ratio
  if (this->fixed_width_ && this->fixed_height_ && width > 0 && height > 0) {
    float scale =
        std::min(static_cast<float>(this->fixed_width_) / width, static_cast<float>(this->fixed_height_) / height);
    target_width = static_cast<int>(width * scale);
    target_height = static_cast<int>(height * scale);
  }

  size_t result = this->resize_buffer_(target_width, target_height);
  if (result > 0 && this->progressive_display_) {
    // Update display dimensions for progressive display
    this->width_ = this->buffer_width_;
    this->height_ = this->buffer_height_;
    this->data_start_ = this->buffer_;
  }
  return result;
}

void RuntimeImage::draw_pixel(int x, int y, const Color &color) {
  uint8_t *target_buffer = this->progressive_display_ ? this->buffer_ : this->decode_buffer_;
  const int target_width = this->progressive_display_ ? this->buffer_width_ : this->decode_buffer_width_;
  const int target_height = this->progressive_display_ ? this->buffer_height_ : this->decode_buffer_height_;
  if (this->progressive_display_) {
    this->buffer_writer_ = BufferWriter::CPU;
  } else {
    this->decode_buffer_writer_ = BufferWriter::CPU;
  }

  if (!target_buffer) {
    ESP_LOGE(TAG, "Buffer not allocated!");
    return;
  }
  if (x < 0 || y < 0 || x >= target_width || y >= target_height) {
    ESP_LOGE(TAG, "Tried to paint a pixel (%d,%d) outside the image!", x, y);
    return;
  }

  switch (this->type_) {
    case image::IMAGE_TYPE_BINARY: {
      const uint32_t width_8 = ((target_width + 7u) / 8u) * 8u;
      uint32_t pos = x + y * width_8;
      auto bitno = 0x80 >> (pos % 8u);
      pos /= 8u;
      auto on = is_color_on(color);
      if (this->has_transparency() && color.w < 0x80)
        on = false;
      if (on) {
        target_buffer[pos] |= bitno;
      } else {
        target_buffer[pos] &= ~bitno;
      }
      break;
    }
    case image::IMAGE_TYPE_GRAYSCALE: {
      const uint32_t pos = x + y * target_width;
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
      target_buffer[pos] = gray;
      break;
    }
    case image::IMAGE_TYPE_RGB565: {
      const size_t pos = (x + y * target_width) * 2;
      Color mapped_color = color;
      this->map_chroma_key(mapped_color);
      uint16_t rgb565 = display::ColorUtil::color_to_565(mapped_color);
      if (this->is_big_endian_) {
        target_buffer[pos + 0] = static_cast<uint8_t>((rgb565 >> 8) & 0xFF);
        target_buffer[pos + 1] = static_cast<uint8_t>(rgb565 & 0xFF);
      } else {
        target_buffer[pos + 0] = static_cast<uint8_t>(rgb565 & 0xFF);
        target_buffer[pos + 1] = static_cast<uint8_t>((rgb565 >> 8) & 0xFF);
      }
      if (this->transparency_ == image::TRANSPARENCY_ALPHA_CHANNEL) {
        const size_t alpha_pos = pos / 2 + target_width * target_height * 2;
        target_buffer[alpha_pos] = color.w;
      }
      break;
    }
    case image::IMAGE_TYPE_RGB: {
      const uint32_t pos = (x + y * target_width) * this->get_bpp() / 8;
      Color mapped_color = color;
      this->map_chroma_key(mapped_color);
      target_buffer[pos + 0] = mapped_color.b;
      target_buffer[pos + 1] = mapped_color.g;
      target_buffer[pos + 2] = mapped_color.r;
      if (this->transparency_ == image::TRANSPARENCY_ALPHA_CHANNEL) {
        target_buffer[pos + 3] = color.w;
      }
      break;
    }
  }
}

void RuntimeImage::map_chroma_key(Color &color) {
  if (this->transparency_ == image::TRANSPARENCY_CHROMA_KEY) {
    if (color.g == 1 && color.r == 0 && color.b == 0) {
      color.g = 0;
    }
    if (color.w < 0x80) {
      color.r = 0;
      color.g = this->type_ == image::IMAGE_TYPE_RGB565 ? 4 : 1;
      color.b = 0;
    }
  }
}

void RuntimeImage::draw(int x, int y, display::Display *display, Color color_on, Color color_off) {
  if (this->data_start_) {
    // If we have a complete image, use the base class draw method
    Image::draw(x, y, display, color_on, color_off);
  } else if (this->placeholder_) {
    // Show placeholder while the runtime image is not available
    this->placeholder_->draw(x, y, display, color_on, color_off);
  }
  // If no image is loaded and no placeholder, nothing to draw
}

bool RuntimeImage::begin_decode(size_t expected_size) {
  LockGuard lock(this->buffer_mutex_);
  if (this->decoder_) {
    ESP_LOGW(TAG, "Decoding already in progress");
    return false;
  }

  this->release_decode_buffer_();
  this->pending_image_ = false;
  this->decoder_ = this->create_decoder_();
  if (!this->decoder_) {
    ESP_LOGE(TAG, "Failed to create decoder for format %d", this->format_);
    return false;
  }

  this->total_size_ = expected_size;
  this->decoded_bytes_ = 0;

  // Initialize decoder
  int result = this->decoder_->prepare(expected_size);
  if (result < 0) {
    ESP_LOGE(TAG, "Failed to prepare decoder: %d", result);
    this->decoder_ = nullptr;
    this->release_decode_buffer_();
    return false;
  }

  return true;
}

int RuntimeImage::feed_data(uint8_t *data, size_t len) {
  LockGuard lock(this->buffer_mutex_);
  if (!this->decoder_) {
    ESP_LOGE(TAG, "No decoder initialized");
    return -1;
  }

  int consumed = this->decoder_->decode(data, len);
  if (consumed > 0) {
    this->decoded_bytes_ += consumed;
  }

  return consumed;
}

bool RuntimeImage::end_decode(bool publish) {
  LockGuard lock(this->buffer_mutex_);
  if (!this->decoder_) {
    return false;
  }

  const size_t decoded_bytes = this->decoded_bytes_;
  this->decoder_ = nullptr;

  if (!this->progressive_display_) {
    if (this->decode_buffer_ == nullptr || this->decode_buffer_width_ <= 0 || this->decode_buffer_height_ <= 0) {
      ESP_LOGE(TAG, "Decoder completed without an image buffer");
      this->release_decode_buffer_();
      return false;
    }
    this->pending_image_ = true;
    if (publish && !this->publish_pending_locked_()) {
      return false;
    }
  }

  const int decoded_width = this->progressive_display_ || publish ? this->width_ : this->decode_buffer_width_;
  const int decoded_height = this->progressive_display_ || publish ? this->height_ : this->decode_buffer_height_;
  ESP_LOGD(TAG, "Decoding complete: %dx%d, %zu bytes", decoded_width, decoded_height, decoded_bytes);
  return true;
}

void RuntimeImage::abort_decode() {
  LockGuard lock(this->buffer_mutex_);
  this->decoder_ = nullptr;
  this->pending_image_ = false;
  this->release_decode_buffer_();
}

bool RuntimeImage::publish_pending() {
  LockGuard lock(this->buffer_mutex_);
  return this->publish_pending_locked_();
}

bool RuntimeImage::is_decode_finished() const {
  if (!this->decoder_) {
    return false;
  }
  return this->decoder_->is_finished();
}

bool RuntimeImage::has_pending_image() const {
  LockGuard lock(this->buffer_mutex_);
  return this->pending_image_ && this->decode_buffer_ != nullptr;
}

size_t RuntimeImage::active_buffer_size() const {
  LockGuard lock(this->buffer_mutex_);
  return this->buffer_ == nullptr ? 0 : this->get_buffer_size_(this->buffer_width_, this->buffer_height_);
}

size_t RuntimeImage::pending_buffer_size() const {
  LockGuard lock(this->buffer_mutex_);
  return this->decode_buffer_ == nullptr
             ? 0
             : this->get_buffer_size_(this->decode_buffer_width_, this->decode_buffer_height_);
}

size_t RuntimeImage::memory_usage_bytes() const {
  LockGuard lock(this->buffer_mutex_);
  const size_t active =
      this->buffer_ == nullptr ? 0 : this->get_buffer_size_(this->buffer_width_, this->buffer_height_);
  const size_t pending = this->decode_buffer_ == nullptr
                             ? 0
                             : this->get_buffer_size_(this->decode_buffer_width_, this->decode_buffer_height_);
  return active + pending;
}

uint32_t RuntimeImage::get_generation() const {
  LockGuard lock(this->buffer_mutex_);
  return this->generation_;
}

BufferWriter RuntimeImage::get_buffer_writer() const {
  LockGuard lock(this->buffer_mutex_);
  return this->buffer_writer_;
}

bool RuntimeImage::acquire_buffer(image::ImageBufferLease *lease) const {
  if (lease == nullptr)
    return false;
  this->buffer_mutex_.lock();
  if (this->buffer_ == nullptr || this->buffer_width_ <= 0 || this->buffer_height_ <= 0) {
    this->buffer_mutex_.unlock();
    return false;
  }
  const size_t stride = (static_cast<size_t>(this->get_bpp()) * this->buffer_width_ + 7U) / 8U;
  *lease = {
      .owner = this,
      .data = this->buffer_,
      .size = stride * static_cast<size_t>(this->buffer_height_),
      .stride = stride,
      .width = this->buffer_width_,
      .height = this->buffer_height_,
      .type = this->type_,
      .transparency = this->transparency_,
      .writer = this->buffer_writer_,
      .generation = this->generation_,
  };
  return true;
}

bool RuntimeImage::release_buffer(image::ImageBufferLease *lease) const {
  if (lease == nullptr || lease->owner != this)
    return false;
  *lease = {};
  this->buffer_mutex_.unlock();
  return true;
}

void RuntimeImage::release() {
  LockGuard lock(this->buffer_mutex_);
  // Public release is serialized with worker-task decoding.
  const bool had_visible_image = this->buffer_ != nullptr;
  this->decoder_ = nullptr;
  this->pending_image_ = false;
  this->release_decode_buffer_();
  this->release_buffer_();
  if (had_visible_image) {
    this->generation_++;
  }
}

void RuntimeImage::release_buffer_() {
  if (this->buffer_) {
    ESP_LOGV(TAG, "Releasing buffer of size %zu", this->get_buffer_size_(this->buffer_width_, this->buffer_height_));
    RAMAllocator<uint8_t> allocator;
    allocator.deallocate(this->buffer_, this->get_buffer_size_(this->buffer_width_, this->buffer_height_));
    this->buffer_ = nullptr;
    this->data_start_ = nullptr;
    this->width_ = 0;
    this->height_ = 0;
    this->buffer_width_ = 0;
    this->buffer_height_ = 0;
    this->buffer_writer_ = BufferWriter::CPU;
#ifdef USE_LVGL
    memset(&this->dsc_, 0, sizeof(this->dsc_));
#endif
  }
  this->buffer_writer_ = BufferWriter::CPU;
}

void RuntimeImage::release_decode_buffer_() {
  if (this->decode_buffer_ == nullptr) {
    this->decode_buffer_width_ = 0;
    this->decode_buffer_height_ = 0;
    this->decode_buffer_writer_ = BufferWriter::CPU;
    return;
  }

  ESP_LOGV(TAG, "Releasing decode buffer of size %zu",
           this->get_buffer_size_(this->decode_buffer_width_, this->decode_buffer_height_));
  RAMAllocator<uint8_t> allocator;
  allocator.deallocate(this->decode_buffer_,
                       this->get_buffer_size_(this->decode_buffer_width_, this->decode_buffer_height_));
  this->decode_buffer_ = nullptr;
  this->decode_buffer_width_ = 0;
  this->decode_buffer_height_ = 0;
  this->decode_buffer_writer_ = BufferWriter::CPU;
}

bool RuntimeImage::publish_pending_locked_() {
  if (!this->pending_image_ || this->decode_buffer_ == nullptr) {
    return false;
  }

  this->release_buffer_();
  this->buffer_ = this->decode_buffer_;
  this->buffer_width_ = this->decode_buffer_width_;
  this->buffer_height_ = this->decode_buffer_height_;
  this->buffer_writer_ = this->decode_buffer_writer_;
  this->decode_buffer_ = nullptr;
  this->decode_buffer_width_ = 0;
  this->decode_buffer_height_ = 0;
  this->decode_buffer_writer_ = BufferWriter::CPU;
  this->pending_image_ = false;
  this->width_ = this->buffer_width_;
  this->height_ = this->buffer_height_;
  this->data_start_ = this->buffer_;
  this->generation_++;
  return true;
}

size_t RuntimeImage::resize_buffer_(int width, int height) {
  size_t new_size = this->get_buffer_size_(width, height);

  if (new_size == 0) {
    ESP_LOGE(TAG, "Refusing to allocate buffer for invalid image dimensions %dx%d", width, height);
    return 0;
  }

  uint8_t *&target_buffer = this->progressive_display_ ? this->buffer_ : this->decode_buffer_;
  int &target_width = this->progressive_display_ ? this->buffer_width_ : this->decode_buffer_width_;
  int &target_height = this->progressive_display_ ? this->buffer_height_ : this->decode_buffer_height_;
  BufferWriter &target_writer = this->progressive_display_ ? this->buffer_writer_ : this->decode_buffer_writer_;

  if (target_buffer && target_width == width && target_height == height) {
    // Buffer already allocated with correct size
    return new_size;
  }

  // Release old buffer if dimensions changed
  if (target_buffer) {
    if (this->progressive_display_) {
      this->release_buffer_();
    } else {
      this->release_decode_buffer_();
    }
  }

  ESP_LOGD(TAG, "Allocating buffer: %dx%d, %zu bytes", width, height, new_size);
  RAMAllocator<uint8_t> allocator;
  target_buffer = allocator.allocate(new_size);

  if (!target_buffer) {
    ESP_LOGE(TAG, "Failed to allocate %zu bytes. Largest free block: %zu", new_size,
             allocator.get_max_free_block_size());
    return 0;
  }

  // Clear buffer
  memset(target_buffer, 0, new_size);

  target_width = width;
  target_height = height;
  target_writer = BufferWriter::CPU;

  return new_size;
}

bool RuntimeImage::accepts_decoded_dimensions(int width, int height) const {
  if (width <= 0 || height <= 0) {
    return false;
  }

  int target_width = this->fixed_width_ ? this->fixed_width_ : width;
  int target_height = this->fixed_height_ ? this->fixed_height_ : height;
  if (this->fixed_width_ && this->fixed_height_) {
    const float scale =
        std::min(static_cast<float>(this->fixed_width_) / width, static_cast<float>(this->fixed_height_) / height);
    target_width = static_cast<int>(width * scale);
    target_height = static_cast<int>(height * scale);
  }
  return target_width == width && target_height == height;
}

bool RuntimeImage::adopt_decode_buffer(uint8_t *buffer, int width, int height, BufferWriter writer) {
  if (buffer == nullptr || this->progressive_display_ || !this->accepts_decoded_dimensions(width, height) ||
      this->get_buffer_size_(width, height) == 0) {
    return false;
  }

  this->release_decode_buffer_();
  this->decode_buffer_ = buffer;
  this->decode_buffer_width_ = width;
  this->decode_buffer_height_ = height;
  this->decode_buffer_writer_ = writer;
  return true;
}

size_t RuntimeImage::get_buffer_size_(int width, int height) const {
  // Dimensions come from a remote image header; reject absurd values so the size math cannot overflow
  if (width <= 0 || height <= 0 || width > MAX_IMAGE_DIMENSION || height > MAX_IMAGE_DIMENSION) {
    return 0;
  }
  if (this->get_type() == image::IMAGE_TYPE_RGB565 && this->transparency_ == image::TRANSPARENCY_ALPHA_CHANNEL) {
    // Add extra alpha channel for RGB565 with alpha
    return static_cast<size_t>(width) * height * 3;
  }
  return (static_cast<size_t>(this->get_bpp()) * width + 7u) / 8u * height;
}

int RuntimeImage::get_position_(int x, int y) const { return (x + y * this->get_buffer_width()) * this->get_bpp() / 8; }

std::unique_ptr<ImageDecoder> RuntimeImage::create_decoder_() {
  switch (this->format_) {
#ifdef USE_RUNTIME_IMAGE_BMP
    case BMP:
      return make_unique<BmpDecoder>(this);
#endif
#ifdef USE_RUNTIME_IMAGE_JPEG
    case JPEG:
#ifdef USE_RUNTIME_IMAGE_ESP32_JPEG
      if (this->decoder_type_ == DecoderType::ESP32_JPEG) {
        return make_unique<Esp32JpegDecoder>(this);
      }
#endif
#ifdef USE_RUNTIME_IMAGE_JPEG_SOFTWARE
      return make_unique<JpegDecoder>(this);
#else
      break;
#endif
#endif
#ifdef USE_RUNTIME_IMAGE_PNG
    case PNG:
      return make_unique<PngDecoder>(this);
#endif
    default:
      break;
  }
  ESP_LOGE(TAG, "Unsupported image format or decoder: format=%d decoder=%d", this->format_,
           static_cast<int>(this->decoder_type_));
  return nullptr;
}

}  // namespace esphome::runtime_image
