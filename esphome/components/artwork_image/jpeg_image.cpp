#include "jpeg_image.h"
#ifdef USE_ARTWORK_IMAGE_JPEG_SUPPORT

#include "esphome/components/display/display_buffer.h"
#include "esphome/components/esp32_jpeg/esp32_jpeg.h"
#include "esphome/core/application.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

#include <cstring>

#include "artwork_image.h"
static const char *const TAG = "artwork_image.jpeg";

namespace esphome {
namespace artwork_image {

/// Custom error manager that longjmps instead of calling exit()
struct JpegErrorMgr {
  jpeg_error_mgr pub;
  jmp_buf setjmp_buffer;
  char message[JMSG_LENGTH_MAX];
};

static void jpeg_error_exit(j_common_ptr cinfo) {
  auto *err = reinterpret_cast<JpegErrorMgr *>(cinfo->err);
  (*(cinfo->err->format_message))(cinfo, err->message);
  longjmp(err->setjmp_buffer, 1);
}

static constexpr size_t MAX_JPEG_DOWNLOAD_SIZE = 2 * 1024 * 1024;  // 2 MB
static constexpr size_t JPEG_DMA_ALIGNMENT = 128;
static bool is_sof_marker(uint8_t marker) {
  switch (marker) {
    case 0xC0:  // Baseline DCT
    case 0xC1:
    case 0xC2:  // Progressive DCT
    case 0xC3:
    case 0xC5:
    case 0xC6:
    case 0xC7:
    case 0xC9:
    case 0xCA:
    case 0xCB:
    case 0xCD:
    case 0xCE:
    case 0xCF:
      return true;
    default:
      return false;
  }
}

static bool is_progressive_sof_marker(uint8_t marker) {
  return marker == 0xC2 || marker == 0xC6 || marker == 0xCA || marker == 0xCE;
}

static bool read_jpeg_frame_info(const uint8_t *buffer, size_t size, uint32_t *width, uint32_t *height,
                                  bool *progressive, uint8_t *max_horizontal_sampling,
                                  uint8_t *max_vertical_sampling) {
  if (buffer == nullptr || size < 4 || buffer[0] != 0xFF || buffer[1] != 0xD8) {
    return false;
  }

  size_t pos = 2;
  while (pos + 3 < size) {
    while (pos < size && buffer[pos] != 0xFF) {
      pos++;
    }
    while (pos < size && buffer[pos] == 0xFF) {
      pos++;
    }
    if (pos >= size) {
      break;
    }

    const uint8_t marker = buffer[pos++];
    if (marker == 0xD9 || marker == 0xDA) {
      break;
    }
    if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
      continue;
    }
    if (pos + 1 >= size) {
      break;
    }

    const uint16_t segment_len = (static_cast<uint16_t>(buffer[pos]) << 8) | buffer[pos + 1];
    if (segment_len < 2 || pos + segment_len > size) {
      break;
    }

    if (is_sof_marker(marker) && segment_len >= 8) {
      const size_t data_pos = pos + 2;
      *height = (static_cast<uint16_t>(buffer[data_pos + 1]) << 8) | buffer[data_pos + 2];
      *width = (static_cast<uint16_t>(buffer[data_pos + 3]) << 8) | buffer[data_pos + 4];
      *progressive = is_progressive_sof_marker(marker);
      const uint8_t component_count = buffer[data_pos + 5];
      if (component_count == 0 || component_count > 3 || segment_len < 8 + component_count * 3)
        return false;
      *max_horizontal_sampling = 1;
      *max_vertical_sampling = 1;
      for (uint8_t index = 0; index < component_count; index++) {
        const uint8_t sampling = buffer[data_pos + 7 + index * 3];
        *max_horizontal_sampling = std::max<uint8_t>(*max_horizontal_sampling, sampling >> 4);
        *max_vertical_sampling = std::max<uint8_t>(*max_vertical_sampling, sampling & 0x0F);
      }
      return *width > 0 && *height > 0;
    }

    pos += segment_len;
  }

  return false;
}

int JpegDecoder::decode_hardware_(uint8_t *buffer, size_t size) {
#ifdef USE_ESP32_JPEG
  const size_t input_size = size;
  const bool output_rgb565 = this->image_->image_type() == image::ImageType::IMAGE_TYPE_RGB565;
  const bool output_rgb888 = this->image_->image_type() == image::ImageType::IMAGE_TYPE_RGB;
  if (!output_rgb565 && !output_rgb888) {
    return 0;
  }

  uint32_t frame_w = 0;
  uint32_t frame_h = 0;
  bool progressive = false;
  uint8_t max_horizontal_sampling = 1;
  uint8_t max_vertical_sampling = 1;
  bool frame_info_valid = read_jpeg_frame_info(buffer, size, &frame_w, &frame_h, &progressive,
                                               &max_horizontal_sampling, &max_vertical_sampling);
  if (frame_info_valid && progressive) {
    ESP_LOGD(TAG, "Hardware JPEG decode skipped: progressive JPEG %ux%u", (unsigned) frame_w, (unsigned) frame_h);
    return 0;
  }

  esp_err_t err = ESP_OK;
  if (!frame_info_valid) {
    esp32_jpeg::PictureInfo info = {};
    err = esp32_jpeg::get_info(buffer, size, &info);
    if (err != ESP_OK) {
      return 0;
    }
    frame_w = info.width;
    frame_h = info.height;
    if (info.down_sampling == esp32_jpeg::DownSampling::YUV420) {
      max_horizontal_sampling = 2;
      max_vertical_sampling = 2;
    } else if (info.down_sampling == esp32_jpeg::DownSampling::YUV422) {
      max_horizontal_sampling = 2;
      max_vertical_sampling = 1;
    }
    if (frame_w == 0 || frame_h == 0) {
      return 0;
    }
  }

  const size_t horizontal_mcu = static_cast<size_t>(max_horizontal_sampling) * 8u;
  const size_t vertical_mcu = static_cast<size_t>(max_vertical_sampling) * 8u;
  const size_t aligned_w = ((frame_w + horizontal_mcu - 1u) / horizontal_mcu) * horizontal_mcu;
  const size_t aligned_h = ((frame_h + vertical_mcu - 1u) / vertical_mcu) * vertical_mcu;
  const size_t bytes_per_pixel = output_rgb565 ? 2u : 3u;
  const size_t output_size = aligned_w * aligned_h * bytes_per_pixel;
  const uint32_t trace_id = this->image_->get_trace_id();
  esp32_jpeg::DecodeConfig cfg = {
      .output_format = output_rgb565 ? esp32_jpeg::PixelFormat::RGB565 : esp32_jpeg::PixelFormat::RGB888,
      .rgb_order = output_rgb565 ? (this->image_->is_big_endian() ? esp32_jpeg::RgbElementOrder::RGB
                                                                  : esp32_jpeg::RgbElementOrder::BGR)
                                 : esp32_jpeg::RgbElementOrder::BGR,
      .color_conversion = esp32_jpeg::ColorConversionStandard::BT601,
      .direct_output = true,
      .skip_output_cache_sync = true,
      .timeout_ms = 1000,
  };

  uint8_t *output = nullptr;
  bool output_reuses_active = false;
  bool output_uses_staging = false;
  size_t written = 0;
  uint64_t elapsed_us = 0;

  uint8_t *active_output = this->image_->try_reuse_active_buffer_for_decode(aligned_w, aligned_h, frame_w, frame_h);
  if (active_output != nullptr) {
    const uint64_t active_start_us = esp_timer_get_time();
    ESP_LOGD(TAG, "artwork trace #%u hardware JPEG active-buffer start: %ux%u output=%zu bytes", trace_id,
             (unsigned) frame_w, (unsigned) frame_h, output_size);
    err = esp32_jpeg::decode(cfg, buffer, size, active_output, output_size, &written);
    elapsed_us = esp_timer_get_time() - active_start_us;
    if (err == ESP_OK && written != 0) {
      output = active_output;
      output_reuses_active = true;
      ESP_LOGD(TAG, "artwork trace #%u hardware JPEG active-buffer finished: %zu -> %zu bytes in %lluus", trace_id,
               size, written, (unsigned long long) elapsed_us);
    } else {
      this->image_->cancel_reused_active_buffer_decode();
      ESP_LOGW(TAG, "artwork trace #%u hardware JPEG active-buffer failed err=%d written=%zu in %lluus", trace_id,
               (int) err, written, (unsigned long long) elapsed_us);
    }
  }

  uint8_t *staging_output = output == nullptr
                                ? this->image_->try_get_staging_buffer_for_decode(aligned_w, aligned_h, frame_w, frame_h)
                                : nullptr;
  if (staging_output != nullptr) {
    const uint64_t staging_start_us = esp_timer_get_time();
    ESP_LOGD(TAG, "artwork trace #%u hardware JPEG staging start: %ux%u aligned=%zux%zu output=%zu bytes", trace_id,
             (unsigned) frame_w, (unsigned) frame_h, aligned_w, aligned_h, output_size);
    err = esp32_jpeg::decode(cfg, buffer, size, staging_output, output_size, &written);
    elapsed_us = esp_timer_get_time() - staging_start_us;
    if (err == ESP_OK && written != 0) {
      output = staging_output;
      output_uses_staging = true;
      ESP_LOGD(TAG,
               "artwork trace #%u hardware JPEG staging finished: %ux%u into %zux%zu buffer, %zu -> %zu "
               "bytes in %lluus",
               trace_id, (unsigned) frame_w, (unsigned) frame_h, aligned_w, aligned_h, size, written,
               (unsigned long long) elapsed_us);
    } else {
      this->image_->cancel_staging_buffer_decode();
      ESP_LOGW(TAG, "artwork trace #%u hardware JPEG staging failed err=%d written=%zu jpeg=%zu in %lluus", trace_id,
               (int) err, written, size, (unsigned long long) elapsed_us);
    }
  }

  if (output == nullptr) {
    written = 0;
    const uint64_t allocated_start_us = esp_timer_get_time();
    ESP_LOGD(TAG, "artwork trace #%u hardware JPEG allocated start: %ux%u aligned=%zux%zu output=%zu bytes", trace_id,
             (unsigned) frame_w, (unsigned) frame_h, aligned_w, aligned_h, output_size);
    err = esp32_jpeg::decode_allocated(cfg, buffer, size, output_size, &output, &written);
    elapsed_us = esp_timer_get_time() - allocated_start_us;
    if (err != ESP_OK || written == 0) {
      ESP_LOGW(TAG, "artwork trace #%u hardware JPEG allocated failed err=%d written=%zu jpeg=%zu in %lluus", trace_id,
               (int) err, written, size, (unsigned long long) elapsed_us);
      if (static_cast<uint64_t>(frame_w) * static_cast<uint64_t>(frame_h) > 360000u) {
        ESP_LOGW(TAG, "Skipping software JPEG decode for large artwork %ux%u after hardware failure",
                 (unsigned) frame_w, (unsigned) frame_h);
        return DECODE_ERROR_UNSUPPORTED_FORMAT;
      }
      return 0;
    }
  }

  const bool adopted = output_rgb565 ? this->adopt_rgb565_buffer(output, aligned_w, aligned_h, frame_w, frame_h, true)
                                     : this->adopt_rgb_buffer(output, aligned_w, aligned_h, frame_w, frame_h, true);
  if (!adopted) {
    if (output_reuses_active) {
      this->image_->cancel_reused_active_buffer_decode();
    } else if (output_uses_staging) {
      this->image_->cancel_staging_buffer_decode();
    } else {
      esp32_jpeg::release_decode_output(output);
    }
    return DECODE_ERROR_OUT_OF_MEMORY;
  }
  // RGB888 output is adopted without a CPU fitting pass, including JPEGs whose
  // hardware output dimensions include alignment padding. Preserve DMA
  // ownership so presentation does not write back the whole image to PSRAM.
  // RGB565 fitting records its own ownership when PPA is used; an exact buffer
  // can be marked here as before.
  if (!output_rgb565 || (aligned_w == frame_w && aligned_h == frame_h)) {
    this->image_->mark_decode_buffer_written_by_dma();
  }

  this->decoded_bytes_ = input_size;
  ESP_LOGD(TAG,
           "artwork trace #%u hardware JPEG %s decode ready: %ux%u into %zux%zu buffer, %zu -> %zu bytes in %lluus",
           trace_id, output_reuses_active ? "active-buffer" : (output_uses_staging ? "staging" : "allocated"),
           (unsigned) frame_w, (unsigned) frame_h, aligned_w, aligned_h, size, written,
           (unsigned long long) elapsed_us);
  return input_size;
#else
  return 0;
#endif
}

int JpegDecoder::prepare(size_t download_size) {
  if (download_size > MAX_JPEG_DOWNLOAD_SIZE) {
    ESP_LOGE(TAG, "JPEG too large to decode: %zu bytes (max %zu). Consider using a smaller image URL.", download_size,
             MAX_JPEG_DOWNLOAD_SIZE);
    return DECODE_ERROR_OUT_OF_MEMORY;
  }
  ImageDecoder::prepare(download_size);
  auto size = this->image_->resize_download_buffer(download_size);
  if (size < download_size) {
    ESP_LOGE(TAG, "Download buffer resize failed!");
    return DECODE_ERROR_OUT_OF_MEMORY;
  }
  return 0;
}

int HOT JpegDecoder::decode(uint8_t *buffer, size_t size) {
  if (this->download_size_ == 0) {
    ESP_LOGV(TAG, "Waiting for HTTP transfer to finish before decoding JPEG with unknown length");
    return 0;
  }
  if (size < this->download_size_) {
    ESP_LOGV(TAG, "Download not complete. Size: %zu/%zu", size, this->download_size_);
    return 0;
  }
  ESP_LOGD(TAG, "JPEG decode start: %zu bytes", size);

  if (this->image_->use_hardware_jpeg()) {
    const bool owns_callbacks = this->image_->begin_decode_callbacks();
    int hw_result = this->decode_hardware_(buffer, size);
    if (owns_callbacks) {
      this->image_->complete_decode_callbacks(hw_result > 0);
    }
    if (hw_result != 0) {
      return hw_result;
    }
    ESP_LOGW(TAG, "artwork trace #%u hardware JPEG failed; software fallback disabled", this->image_->get_trace_id());
    return DECODE_ERROR_UNSUPPORTED_FORMAT;
  } else {
    ESP_LOGD(TAG, "artwork trace #%u hardware JPEG disabled for this image; using software decode",
             this->image_->get_trace_id());
  }

  jpeg_decompress_struct cinfo;
  JpegErrorMgr jerr{};

  cinfo.err = jpeg_std_error(&jerr.pub);
  jerr.pub.error_exit = jpeg_error_exit;

  // Raw pointer for longjmp safety — unique_ptr destructors are skipped by longjmp
  uint8_t *row_buffer = nullptr;

  if (setjmp(jerr.setjmp_buffer)) {
    ESP_LOGE(TAG, "JPEG decode error: %s", jerr.message);
    free(row_buffer);
    jpeg_destroy_decompress(&cinfo);
    return DECODE_ERROR_UNSUPPORTED_FORMAT;
  }

  jpeg_create_decompress(&cinfo);
  jpeg_mem_src(&cinfo, buffer, size);

  if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
    ESP_LOGE(TAG, "Could not read JPEG header");
    jpeg_destroy_decompress(&cinfo);
    return DECODE_ERROR_INVALID_TYPE;
  }

  int src_w = cinfo.image_width;
  int src_h = cinfo.image_height;
  ESP_LOGD(TAG, "JPEG header: %dx%d, components=%d, progressive=%s", src_w, src_h, cinfo.num_components,
           cinfo.progressive_mode ? "yes" : "no");
  if (cinfo.progressive_mode && static_cast<uint32_t>(src_w) * static_cast<uint32_t>(src_h) > 360000u) {
    ESP_LOGW(TAG, "Progressive JPEG %dx%d is too expensive for software decode; keeping previous artwork", src_w,
             src_h);
    jpeg_destroy_decompress(&cinfo);
    return DECODE_ERROR_UNSUPPORTED_FORMAT;
  }
  // Request RGB output regardless of input colorspace
  cinfo.out_color_space = JCS_RGB;
  // Use fast integer IDCT — slightly lower quality but faster on ESP32
  // and avoids pulling in the float IDCT code path.
  cinfo.dct_method = JDCT_IFAST;

  // Use IDCT scaling to downscale during decode
  int target_w = this->image_->get_fixed_width();
  int target_h = this->image_->get_fixed_height();
  if (target_w > 0 && target_h > 0) {
    // Use the smallest IDCT downscale that is still at least the configured
    // target size. Decoding slightly smaller saves work once, but makes LVGL
    // rescale the artwork on every redraw, which hurts full-screen updates.
    int min_w = target_w;
    int min_h = target_h;
    constexpr unsigned int denoms[] = {8, 4, 2, 1};
    for (unsigned int denom : denoms) {
      cinfo.scale_num = 1;
      cinfo.scale_denom = denom;
      jpeg_calc_output_dimensions(&cinfo);
      if (static_cast<int>(cinfo.output_width) >= min_w && static_cast<int>(cinfo.output_height) >= min_h) {
        break;
      }
    }
    if (static_cast<int>(cinfo.output_width) < min_w || static_cast<int>(cinfo.output_height) < min_h) {
      cinfo.scale_num = 1;
      cinfo.scale_denom = 1;
      jpeg_calc_output_dimensions(&cinfo);
    }
  } else {
    jpeg_calc_output_dimensions(&cinfo);
  }

  int out_w = cinfo.output_width;
  int out_h = cinfo.output_height;
  if (out_w != src_w || out_h != src_h) {
    ESP_LOGD(TAG, "Using IDCT downscale: %dx%d -> %dx%d", src_w, src_h, out_w, out_h);
  }

  if (!this->set_size(out_w, out_h)) {
    jpeg_destroy_decompress(&cinfo);
    return DECODE_ERROR_OUT_OF_MEMORY;
  }

  jpeg_start_decompress(&cinfo);

  // Allocate row buffers (raw pointers — safe across longjmp)
  size_t row_stride = static_cast<size_t>(out_w) * 3;
  row_buffer = static_cast<uint8_t *>(heap_caps_malloc(row_stride, MALLOC_CAP_8BIT));
  if (row_buffer == nullptr) {
    ESP_LOGE(TAG, "JPEG row buffer allocation failed: %zu bytes", row_stride);
    jpeg_destroy_decompress(&cinfo);
    return DECODE_ERROR_OUT_OF_MEMORY;
  }

  bool use_rgb565 = (this->image_->image_type() == image::ImageType::IMAGE_TYPE_RGB565);
  bool big_endian = this->image_->is_big_endian();
  const uint8_t darken_percent = this->image_->get_darken_percent();
  const bool darken_rgb565 = use_rgb565 && darken_percent > 0 && darken_percent < 100;
  const uint16_t darken_keep = 100 - darken_percent;

  int y = 0;
  while (cinfo.output_scanline < cinfo.output_height) {
    uint8_t *row_ptr = row_buffer;
    jpeg_read_scanlines(&cinfo, &row_ptr, 1);

    if ((y & 63) == 0) {
      App.feed_wdt();
    }

    if (use_rgb565) {
      // Convert RGB888 -> RGB565 in-place (2 bpp fits within the 3 bpp
      // source buffer, so no separate allocation needed).  We read forward
      // and write forward; the write pointer never overtakes the read
      // pointer because 2 < 3.
      uint8_t *dst = row_buffer;
      for (int x = 0; x < out_w; x++) {
        uint8_t r = row_buffer[x * 3 + 0];
        uint8_t g = row_buffer[x * 3 + 1];
        uint8_t b = row_buffer[x * 3 + 2];
        if (darken_rgb565) {
          r = static_cast<uint8_t>((static_cast<uint16_t>(r) * darken_keep) / 100);
          g = static_cast<uint8_t>((static_cast<uint16_t>(g) * darken_keep) / 100);
          b = static_cast<uint8_t>((static_cast<uint16_t>(b) * darken_keep) / 100);
        }
        uint16_t rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        if (big_endian) {
          dst[0] = rgb565 >> 8;
          dst[1] = rgb565 & 0xFF;
        } else {
          dst[0] = rgb565 & 0xFF;
          dst[1] = rgb565 >> 8;
        }
        dst += 2;
      }
      this->draw_rgb565_block(0, y, out_w, 1, row_buffer);
    } else {
      // Per-pixel draw for other image types
      for (int x = 0; x < out_w; x++) {
        Color color(row_buffer[x * 3 + 0], row_buffer[x * 3 + 1], row_buffer[x * 3 + 2]);
        this->draw(x, y, 1, 1, color);
      }
    }
    y++;
  }

  jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);
  free(row_buffer);

  this->decoded_bytes_ = size;
  if (darken_rgb565) {
    this->image_->mark_decode_buffer_darkened(darken_percent);
  }
  ESP_LOGD(TAG, "JPEG decode finished: output=%dx%d", out_w, out_h);
  return size;
}

}  // namespace artwork_image
}  // namespace esphome

#endif  // USE_ARTWORK_IMAGE_JPEG_SUPPORT
