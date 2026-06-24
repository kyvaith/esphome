#include "image_decoder.h"
#include "artwork_image.h"

#include "esphome/core/log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <new>

namespace esphome {
namespace artwork_image {

static const char *const TAG = "artwork_image.decoder";

bool ImageDecoder::set_size(int width, int height) {
  bool success = this->image_->resize_(width, height) > 0;
  if (!success) {
    this->failed_ = true;
    return false;
  }
  int content_width = this->image_->decode_content_width_ > 0 ? this->image_->decode_content_width_
                                                              : this->image_->decode_buffer_width_;
  int content_height = this->image_->decode_content_height_ > 0 ? this->image_->decode_content_height_
                                                                : this->image_->decode_buffer_height_;
  this->x_offset_ = this->image_->decode_offset_x_;
  this->y_offset_ = this->image_->decode_offset_y_;
  this->x_scale_ = static_cast<double>(content_width) / width;
  this->y_scale_ = static_cast<double>(content_height) / height;
  ESP_LOGI(TAG, "Decoder geometry: source=%dx%d content=%dx%d offset=%d,%d scale=%.4f,%.4f",
           width, height, content_width, content_height, this->x_offset_, this->y_offset_, this->x_scale_,
           this->y_scale_);
  return success;
}

void ImageDecoder::draw(int x, int y, int w, int h, const Color &color) {
  if (this->failed_) {
    return;
  }
  auto width = std::min(this->image_->decode_buffer_width_,
                        this->x_offset_ + static_cast<int>(std::ceil((x + w) * this->x_scale_)));
  auto height = std::min(this->image_->decode_buffer_height_,
                         this->y_offset_ + static_cast<int>(std::ceil((y + h) * this->y_scale_)));
  for (int i = this->x_offset_ + static_cast<int>(x * this->x_scale_); i < width; i++) {
    for (int j = this->y_offset_ + static_cast<int>(y * this->y_scale_); j < height; j++) {
      this->image_->draw_pixel_(i, j, color);
    }
  }
}

void ImageDecoder::draw_rgb565_block(int x, int y, int w, int h, const uint8_t *data) {
  if (this->failed_) {
    return;
  }
  int bpp_bytes = this->image_->get_bpp() / 8;

  if (this->x_scale_ == 1.0 && this->y_scale_ == 1.0 && bpp_bytes == 2) {
    for (int row = 0; row < h; row++) {
      int dy = this->y_offset_ + y + row;
      if (dy < 0 || dy >= this->image_->decode_buffer_height_)
        continue;
      int start_x = std::max(0, this->x_offset_ + x);
      int end_x = std::min(this->x_offset_ + x + w, this->image_->decode_buffer_width_);
      if (start_x >= end_x)
        continue;
      int copy_w = end_x - start_x;
      int src_offset = (row * w + (start_x - this->x_offset_ - x)) * 2;
      int dst_pos = this->image_->get_position_(start_x, dy);
      memcpy(this->image_->decode_buffer_ + dst_pos, data + src_offset, copy_w * 2);
    }
    return;
  }

  for (int row = 0; row < h; row++) {
    for (int col = 0; col < w; col++) {
      int src_x = x + col;
      int src_y = y + row;
      int src_offset = (row * w + col) * 2;

      auto target_w = std::min(this->image_->decode_buffer_width_,
                               this->x_offset_ + static_cast<int>(std::ceil((src_x + 1) * this->x_scale_)));
      auto target_h = std::min(this->image_->decode_buffer_height_,
                               this->y_offset_ + static_cast<int>(std::ceil((src_y + 1) * this->y_scale_)));
      for (int dy = this->y_offset_ + static_cast<int>(src_y * this->y_scale_); dy < target_h; dy++) {
        for (int dx = this->x_offset_ + static_cast<int>(src_x * this->x_scale_); dx < target_w; dx++) {
          int dst_pos = this->image_->get_position_(dx, dy);
          memcpy(this->image_->decode_buffer_ + dst_pos, data + src_offset, 2);
          if (bpp_bytes > 2) {
            this->image_->decode_buffer_[dst_pos + 2] = 0xFF;
          }
        }
      }
    }
  }
}

bool ImageDecoder::adopt_rgb565_buffer(uint8_t *buffer, int buffer_width, int buffer_height, int content_width,
                                       int content_height) {
  if (buffer == nullptr || buffer_width <= 0 || buffer_height <= 0 || content_width <= 0 || content_height <= 0 ||
      content_width > buffer_width || content_height > buffer_height || this->image_->get_bpp() != 16) {
    this->failed_ = true;
    return false;
  }

  const int target_width = this->image_->fixed_width_;
  const int target_height = this->image_->fixed_height_;
  if (target_width > 0 && target_height > 0 &&
      (buffer_width != target_width || buffer_height != target_height || content_width != target_width ||
       content_height != target_height)) {
    double scale = std::min(static_cast<double>(target_width) / content_width,
                            static_cast<double>(target_height) / content_height);
    int scaled_width = std::max(1, static_cast<int>(content_width * scale));
    int scaled_height = std::max(1, static_cast<int>(content_height * scale));
    if (scaled_width > target_width) {
      scaled_width = target_width;
    }
    if (scaled_height > target_height) {
      scaled_height = target_height;
    }
    const int offset_x = (target_width - scaled_width) / 2;
    const int offset_y = (target_height - scaled_height) / 2;
    const size_t target_size = target_width * target_height * 2u;
    uint8_t *target = this->image_->allocator_.allocate(target_size);
    if (target == nullptr) {
      ESP_LOGW(TAG, "RGB565 artwork fit allocation failed: %zu bytes", target_size);
      this->failed_ = true;
      return false;
    }
    const bool covers_target = offset_x == 0 && offset_y == 0 && scaled_width == target_width &&
                               scaled_height == target_height;
    if (!covers_target) {
      memset(target, 0, target_size);
    }

    const uint32_t x_step = (static_cast<uint32_t>(content_width) << 16) / scaled_width;
    const uint32_t y_step = (static_cast<uint32_t>(content_height) << 16) / scaled_height;
    const bool exact_2x_full = covers_target && scaled_width == content_width * 2 &&
                               scaled_height == content_height * 2;
    bool copied = false;
    if (exact_2x_full) {
      const size_t row_bytes = static_cast<size_t>(scaled_width) * 2u;
      std::unique_ptr<uint8_t[]> expanded_row(new (std::nothrow) uint8_t[row_bytes]);
      if (expanded_row) {
        for (int sy = 0; sy < content_height; sy++) {
          const uint8_t *src = buffer + (sy * buffer_width * 2);
          uint8_t *row = expanded_row.get();
          for (int x = 0; x < content_width; x++) {
            const uint8_t lo = src[x * 2 + 0];
            const uint8_t hi = src[x * 2 + 1];
            row[x * 4 + 0] = lo;
            row[x * 4 + 1] = hi;
            row[x * 4 + 2] = lo;
            row[x * 4 + 3] = hi;
          }
          uint8_t *dst = target + (static_cast<size_t>(sy) * 2u * target_width * 2u);
          memcpy(dst, row, row_bytes);
          memcpy(dst + row_bytes, row, row_bytes);
          if ((sy & 0x03) == 0x03) {
            taskYIELD();
          }
        }
        copied = true;
      }
    }
    if (!copied) {
      uint32_t y_acc = 0;
      for (int y = 0; y < scaled_height; y++) {
        const int sy = static_cast<int>(y_acc >> 16);
        const uint8_t *src_row = buffer + (sy * buffer_width * 2);
        uint8_t *dst = target + (((offset_y + y) * target_width + offset_x) * 2);
        uint32_t x_acc = 0;
        for (int x = 0; x < scaled_width; x++) {
          const int sx = static_cast<int>(x_acc >> 16);
          const uint8_t *src = src_row + sx * 2;
          dst[x * 2 + 0] = src[0];
          dst[x * 2 + 1] = src[1];
          x_acc += x_step;
        }
        y_acc += y_step;
        if ((y & 0x07) == 0x07) {
          taskYIELD();
        }
      }
    }

    this->image_->allocator_.deallocate(buffer, buffer_width * buffer_height * 2u);
    this->image_->discard_decode_buffer_();
    this->image_->decode_buffer_ = target;
    this->image_->decode_buffer_width_ = target_width;
    this->image_->decode_buffer_height_ = target_height;
    this->image_->decode_content_width_ = scaled_width;
    this->image_->decode_content_height_ = scaled_height;
    this->image_->decode_offset_x_ = offset_x;
    this->image_->decode_offset_y_ = offset_y;
    this->x_offset_ = offset_x;
    this->y_offset_ = offset_y;
    this->x_scale_ = static_cast<double>(scaled_width) / content_width;
    this->y_scale_ = static_cast<double>(scaled_height) / content_height;
    ESP_LOGI(TAG, "Decoder fitted RGB565 buffer: source=%dx%d target=%dx%d content=%dx%d offset=%d,%d",
             content_width, content_height, target_width, target_height, scaled_width, scaled_height, offset_x,
             offset_y);
    return true;
  }

  if (this->image_->decode_buffer_ != buffer) {
    this->image_->discard_decode_buffer_();
    this->image_->decode_buffer_ = buffer;
  }
  this->image_->decode_buffer_width_ = buffer_width;
  this->image_->decode_buffer_height_ = buffer_height;
  this->image_->decode_content_width_ = content_width;
  this->image_->decode_content_height_ = content_height;
  this->image_->decode_offset_x_ = 0;
  this->image_->decode_offset_y_ = 0;
  this->x_offset_ = 0;
  this->y_offset_ = 0;
  this->x_scale_ = 1.0;
  this->y_scale_ = 1.0;
  ESP_LOGI(TAG, "Decoder adopted RGB565 buffer: content=%dx%d buffer=%dx%d", content_width, content_height,
           buffer_width, buffer_height);
  return true;
}

bool ImageDecoder::adopt_rgb_buffer(uint8_t *buffer, int buffer_width, int buffer_height, int content_width,
                                    int content_height) {
  if (buffer == nullptr || buffer_width <= 0 || buffer_height <= 0 || content_width <= 0 || content_height <= 0 ||
      content_width > buffer_width || content_height > buffer_height || this->image_->get_bpp() != 24) {
    this->failed_ = true;
    return false;
  }

  if (this->image_->decode_buffer_ != buffer) {
    this->image_->discard_decode_buffer_();
    this->image_->decode_buffer_ = buffer;
  }
  this->image_->decode_buffer_width_ = buffer_width;
  this->image_->decode_buffer_height_ = buffer_height;
  this->image_->decode_content_width_ = content_width;
  this->image_->decode_content_height_ = content_height;
  this->image_->decode_offset_x_ = 0;
  this->image_->decode_offset_y_ = 0;
  this->x_offset_ = 0;
  this->y_offset_ = 0;
  this->x_scale_ = 1.0;
  this->y_scale_ = 1.0;
  ESP_LOGI(TAG, "Decoder adopted RGB buffer: content=%dx%d buffer=%dx%d", content_width, content_height,
           buffer_width, buffer_height);
  return true;
}

DownloadBuffer::DownloadBuffer(size_t size) : size_(size) {
  this->buffer_ = this->allocator_.allocate(size);
  this->reset();
  if (!this->buffer_) {
    ESP_LOGE(TAG, "Initial allocation of download buffer failed!");
    this->size_ = 0;
  }
}

uint8_t *DownloadBuffer::data(size_t offset) {
  if (offset > this->size_) {
    ESP_LOGE(TAG, "Tried to access beyond download buffer bounds!!!");
    return this->buffer_;
  }
  return this->buffer_ + offset;
}

size_t DownloadBuffer::read(size_t len) {
  if (len > this->unread_) {
    ESP_LOGE(TAG, "Decoder consumed %zu bytes, but only %zu were buffered", len, this->unread_);
    len = this->unread_;
  }
  this->unread_ -= len;
  if (this->unread_ > 0) {
    memmove(this->data(), this->data(len), this->unread_);
  }
  return this->unread_;
}

size_t DownloadBuffer::resize(size_t size) {
  if (this->size_ >= size) {
    return this->size_;
  }
  uint8_t *new_buffer = this->allocator_.allocate(size);
  if (new_buffer) {
    if (this->buffer_ && this->unread_ > 0) {
      memcpy(new_buffer, this->buffer_, this->unread_);
    }
    this->allocator_.deallocate(this->buffer_, this->size_);
    this->buffer_ = new_buffer;
    this->size_ = size;
    return size;
  } else {
    ESP_LOGE(TAG, "allocation of %zu bytes failed. Biggest block in heap: %zu Bytes", size,
             this->allocator_.get_max_free_block_size());
    this->allocator_.deallocate(this->buffer_, this->size_);
    this->buffer_ = nullptr;
    this->size_ = 0;
    this->reset();
    return 0;
  }
}

}  // namespace artwork_image
}  // namespace esphome
