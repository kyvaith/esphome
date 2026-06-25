#ifdef USE_ESP32_VARIANT_ESP32P4
#include <algorithm>
#include <cinttypes>
#include <cstring>
#include <utility>
#include "mipi_dsi.h"
#include "esphome/core/helpers.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_timer.h"
#include "hal/mipi_dsi_brg_ll.h"

namespace esphome::mipi_dsi {

// Maximum bytes to log for init commands (truncated if larger)
static constexpr size_t MIPI_DSI_MAX_CMD_LOG_BYTES = 64;
static constexpr size_t DMA2D_SAFE_ALIGN_BYTES = 4;
static constexpr size_t DSI_DIAG_EVENT_COUNT = 8;
static constexpr uint32_t DSI_DIAG_HOST_DPI_BUFF_PLD_UNDER = 1UL << 19;
static constexpr uint32_t DSI_DIAG_LOG_INTERVAL_MS = 250;
static volatile uint32_t dsi_underrun_count = 0;
static volatile uint32_t dsi_underrun_total = 0;
static volatile uint32_t dsi_underrun_last_tick = 0;
static volatile uint32_t dsi_diag_event_count = 0;
static volatile uint32_t dsi_diag_write_index = 0;
static volatile uint32_t dsi_diag_same_status_suppressed = 0;
static volatile uint32_t dsi_diag_last_bridge_status = 0;
static volatile uint32_t dsi_diag_last_host_status0 = 0;
static volatile uint32_t dsi_diag_last_host_status1 = 0;
static volatile DsiDiagnosticEvent dsi_diag_events[DSI_DIAG_EVENT_COUNT];

static bool is_aligned(uintptr_t value, size_t alignment) { return (value & (alignment - 1U)) == 0; }

static esp_err_t cache_writeback_external_for_dma(const void *ptr, size_t size) {
  if (ptr == nullptr || size == 0 || !esp_ptr_external_ram(ptr))
    return ESP_OK;

  static constexpr size_t CACHE_SYNC_ALIGN_BYTES = 128;
  uintptr_t start = reinterpret_cast<uintptr_t>(ptr);
  uintptr_t aligned_start = start & ~(static_cast<uintptr_t>(CACHE_SYNC_ALIGN_BYTES) - 1U);
  uintptr_t aligned_end = (start + size + CACHE_SYNC_ALIGN_BYTES - 1U) &
                          ~(static_cast<uintptr_t>(CACHE_SYNC_ALIGN_BYTES) - 1U);
  if (aligned_end <= aligned_start)
    return ESP_OK;
  if (!esp_ptr_external_ram(reinterpret_cast<const void *>(aligned_start)) ||
      !esp_ptr_external_ram(reinterpret_cast<const void *>(aligned_end - 1U)))
    return ESP_ERR_INVALID_ARG;

  return esp_cache_msync(reinterpret_cast<void *>(aligned_start), aligned_end - aligned_start,
                         ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);
}

extern "C" void IRAM_ATTR esphome_mipi_dsi_note_underrun(void) {
  dsi_underrun_count++;
  dsi_underrun_total++;
  dsi_underrun_last_tick = static_cast<uint32_t>(xTaskGetTickCountFromISR());
}

extern "C" void IRAM_ATTR esphome_mipi_dsi_note_status(uint32_t bridge_status, uint32_t bridge_raw,
                                                        uint32_t fifo_depth, uint32_t host_status0,
                                                        uint32_t host_status1) {
  const bool bridge_underrun = (bridge_status & MIPI_DSI_BRG_LL_EVENT_UNDERRUN) != 0;
  const bool host_status = host_status0 != 0 || host_status1 != 0;
  if (!bridge_underrun && !host_status)
    return;

  if (!bridge_underrun && bridge_status == dsi_diag_last_bridge_status &&
      host_status0 == dsi_diag_last_host_status0 && host_status1 == dsi_diag_last_host_status1) {
    dsi_diag_same_status_suppressed++;
    return;
  }

  dsi_diag_last_bridge_status = bridge_status;
  dsi_diag_last_host_status0 = host_status0;
  dsi_diag_last_host_status1 = host_status1;

  const uint32_t index = dsi_diag_write_index % DSI_DIAG_EVENT_COUNT;
  dsi_diag_write_index++;
  dsi_diag_events[index].tick = static_cast<uint32_t>(xTaskGetTickCountFromISR());
  dsi_diag_events[index].bridge_status = bridge_status;
  dsi_diag_events[index].bridge_raw = bridge_raw;
  dsi_diag_events[index].fifo_depth = fifo_depth;
  dsi_diag_events[index].host_status0 = host_status0;
  dsi_diag_events[index].host_status1 = host_status1;
  dsi_diag_event_count++;
}

extern "C" esp_err_t esphome_mipi_dsi_poll_status(esp_lcd_panel_handle_t panel, uint32_t *bridge_status,
                                                   uint32_t *bridge_raw, uint32_t *fifo_depth,
                                                   uint32_t *host_status0,
                                                   uint32_t *host_status1) __attribute__((weak));

static bool IRAM_ATTR notify_color_trans_ready(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata,
                                               void *user_ctx) {
  auto *ctx = static_cast<MipiDsiCallbackContext *>(user_ctx);
  BaseType_t need_yield = pdFALSE;
  if (ctx != nullptr && ctx->async_flush_pending != nullptr && *ctx->async_flush_pending &&
      ctx->async_flush_done != nullptr) {
    *ctx->async_flush_pending = false;
    xSemaphoreGiveFromISR(ctx->async_flush_done, &need_yield);
  } else if (ctx != nullptr && ctx->color_trans_done != nullptr) {
    xSemaphoreGiveFromISR(ctx->color_trans_done, &need_yield);
  }
  return (need_yield == pdTRUE);
}

static bool IRAM_ATTR notify_refresh_done(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata,
                                          void *user_ctx) {
  auto *ctx = static_cast<MipiDsiCallbackContext *>(user_ctx);
  BaseType_t need_yield = pdFALSE;
  if (ctx != nullptr && ctx->refresh_done != nullptr)
    xSemaphoreGiveFromISR(ctx->refresh_done, &need_yield);
  return (need_yield == pdTRUE);
}

void MipiDsi::smark_failed(const LogString *message, esp_err_t err) {
  ESP_LOGE(TAG, "%s: %s", LOG_STR_ARG(message), esp_err_to_name(err));
  this->mark_failed(message);
}

void MipiDsi::setup() {
  ESP_LOGCONFIG(TAG, "Running Setup");

  if (!this->enable_pins_.empty()) {
    for (auto *pin : this->enable_pins_) {
      pin->setup();
      pin->digital_write(true);
    }
    delay(10);
  }

  esp_lcd_dsi_bus_config_t bus_config = {
      .bus_id = 0,  // index from 0, specify the DSI host to use
      .num_data_lanes =
          this->lanes_,  // Number of data lanes to use, can't set a value that exceeds the chip's capability
      .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,  // Clock source for the DPHY
      .lane_bit_rate_mbps = this->lane_bit_rate_,   // Bit rate of the data lanes, in Mbps
  };
  auto err = esp_lcd_new_dsi_bus(&bus_config, &this->bus_handle_);
  if (err != ESP_OK) {
    this->smark_failed(LOG_STR("lcd_new_dsi_bus failed"), err);
    return;
  }
  esp_lcd_dbi_io_config_t dbi_config = {
      .virtual_channel = 0,
      .lcd_cmd_bits = 8,    // according to the LCD spec
      .lcd_param_bits = 8,  // according to the LCD spec
  };
  err = esp_lcd_new_panel_io_dbi(this->bus_handle_, &dbi_config, &this->io_handle_);
  if (err != ESP_OK) {
    this->smark_failed(LOG_STR("new_panel_io_dbi failed"), err);
    return;
  }
  // clang-format off
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
  auto color_format = LCD_COLOR_FMT_RGB565;
  if (this->color_depth_ == display::COLOR_BITNESS_888) {
    color_format = LCD_COLOR_FMT_RGB888;
  }
  esp_lcd_dpi_panel_config_t dpi_config = {.virtual_channel = 0,
                                           .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
                                           .dpi_clock_freq_mhz = this->pclk_frequency_,
                                           .in_color_format = color_format,
#else
  auto pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565;
  if (this->color_depth_ == display::COLOR_BITNESS_888) {
    pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB888;
  }
  esp_lcd_dpi_panel_config_t dpi_config = {.virtual_channel = 0,
                                           .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
                                           .dpi_clock_freq_mhz = this->pclk_frequency_,
                                           .pixel_format = pixel_format,
#endif
                                           .num_fbs = 2,  // number of frame buffers to allocate
                                           .video_timing =
                                               {
                                                   .h_size = this->width_,
                                                   .v_size = this->height_,
                                                   .hsync_pulse_width = this->hsync_pulse_width_,
                                                   .hsync_back_porch = this->hsync_back_porch_,
                                                   .hsync_front_porch = this->hsync_front_porch_,
                                                   .vsync_pulse_width = this->vsync_pulse_width_,
                                                   .vsync_back_porch = this->vsync_back_porch_,
                                                   .vsync_front_porch = this->vsync_front_porch_,
                                               },
                                           .flags = {
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 0, 0)
                                               .use_dma2d = this->use_dma2d_,
#endif
                                           }};
  // clang-format on
  err = esp_lcd_new_panel_dpi(this->bus_handle_, &dpi_config, &this->handle_);
  if (err != ESP_OK) {
    this->smark_failed(LOG_STR("esp_lcd_new_panel_dpi failed"), err);
    return;
  }

  void *fb0 = nullptr;
  void *fb1 = nullptr;
  err = esp_lcd_dpi_panel_get_frame_buffer(this->handle_, 2, &fb0, &fb1);
  if (err == ESP_OK && fb0 != nullptr && fb1 != nullptr) {
    this->frame_buffers_[0] = static_cast<uint8_t *>(fb0);
    this->frame_buffers_[1] = static_cast<uint8_t *>(fb1);
    constexpr size_t cache_alignment = 128;
    ESP_LOGW(TAG, "DPI framebuffers exposed at %p / %p (%zu bytes each, cache_align=%zu, mod=%u/%u)",
             this->frame_buffers_[0], this->frame_buffers_[1], this->get_frame_buffer_size(), cache_alignment,
             (unsigned) (reinterpret_cast<uintptr_t>(this->frame_buffers_[0]) % cache_alignment),
             (unsigned) (reinterpret_cast<uintptr_t>(this->frame_buffers_[1]) % cache_alignment));
  } else {
    ESP_LOGW(TAG, "DPI framebuffer unavailable: %s", esp_err_to_name(err));
  }
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
  if (this->use_dma2d_) {
    err = esp_lcd_dpi_panel_enable_dma2d(this->handle_);
    if (err != ESP_OK) {
      this->smark_failed(LOG_STR("esp_lcd_dpi_panel_enable_dma2d failed"), err);
      return;
    }
  }
#endif
  ESP_LOGCONFIG(TAG, "DPI DMA2D draw hook: %s", YESNO(this->use_dma2d_));
  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->setup();
    this->reset_pin_->digital_write(true);
    delay(5);
    this->reset_pin_->digital_write(false);
    delay(5);
    this->reset_pin_->digital_write(true);
  } else {
    esp_lcd_panel_io_tx_param(this->io_handle_, SW_RESET_CMD, nullptr, 0);
  }
  // need to know when the display is ready for SLPOUT command - will be 120ms after reset
  auto when = millis() + 120;
  err = esp_lcd_panel_init(this->handle_);
  if (err != ESP_OK) {
    this->smark_failed(LOG_STR("esp_lcd_init failed"), err);
    return;
  }
  size_t index = 0;
  auto &vec = this->init_sequence_;
  while (index != vec.size()) {
    if (vec.size() - index < 2) {
      this->mark_failed(LOG_STR("Malformed init sequence"));
      return;
    }
    uint8_t cmd = vec[index++];
    uint8_t x = vec[index++];
    if (x == DELAY_FLAG) {
      ESP_LOGD(TAG, "Delay %dms", cmd);
      delay(cmd);
    } else {
      uint8_t num_args = x & 0x7F;
      if (vec.size() - index < num_args) {
        this->mark_failed(LOG_STR("Malformed init sequence"));
        return;
      }
      if (cmd == SLEEP_OUT) {
        // are we ready, boots?
        int duration = when - millis();
        if (duration > 0) {
          delay(duration);
        }
      }
      const auto *ptr = vec.data() + index;
#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERY_VERBOSE
      char hex_buf[format_hex_pretty_size(MIPI_DSI_MAX_CMD_LOG_BYTES)];
#endif
      ESP_LOGVV(TAG, "Command %02X, length %d, byte(s) %s", cmd, num_args,
                format_hex_pretty_to(hex_buf, ptr, num_args, '.'));
      err = esp_lcd_panel_io_tx_param(this->io_handle_, cmd, ptr, num_args);
      if (err != ESP_OK) {
        this->smark_failed(LOG_STR("lcd_panel_io_tx_param failed"), err);
        return;
      }
      index += num_args;
      if (cmd == SLEEP_OUT)
        delay(10);
    }
  }
  this->io_lock_ = xSemaphoreCreateBinary();
  this->refresh_lock_ = xSemaphoreCreateBinary();
  if (this->async_lvgl_flush_) {
    this->async_flush_done_ = xSemaphoreCreateBinary();
    if (this->async_flush_done_ == nullptr) {
      ESP_LOGW(TAG, "Async LVGL flush requested but semaphore allocation failed");
      this->async_lvgl_flush_ = false;
    }
  }
  this->callback_context_.color_trans_done = this->io_lock_;
  this->callback_context_.refresh_done = this->refresh_lock_;
  this->callback_context_.async_flush_done = this->async_flush_done_;
  this->callback_context_.async_flush_pending = &this->async_flush_pending_;
  this->start_async_flush_task_();
  esp_lcd_dpi_panel_event_callbacks_t cbs = {
      .on_color_trans_done = notify_color_trans_ready,
      .on_refresh_done = notify_refresh_done,
  };

  err = (esp_lcd_dpi_panel_register_event_callbacks(this->handle_, &cbs, &this->callback_context_));
  if (err != ESP_OK) {
    this->smark_failed(LOG_STR("Failed to register callbacks"), err);
    return;
  }

  ESP_LOGCONFIG(TAG, "MIPI DSI setup complete");
}

void MipiDsi::start_async_flush_task_() {
  if (!this->async_lvgl_flush_)
    return;
  if (!this->use_dma2d_) {
    ESP_LOGW(TAG, "Async LVGL flush requested but DMA2D is disabled");
    this->async_lvgl_flush_ = false;
    return;
  }
#if CONFIG_FREERTOS_UNICORE
  constexpr BaseType_t flush_core = tskNO_AFFINITY;
#else
  constexpr BaseType_t flush_core = 0;
#endif
  TaskHandle_t task_handle = nullptr;
  const BaseType_t ok = xTaskCreatePinnedToCore(&MipiDsi::async_flush_task_trampoline, "mipi_flush_ready", 4096, this,
                                                6, &task_handle, flush_core);
  if (ok != pdPASS) {
    ESP_LOGW(TAG, "Async LVGL flush task allocation failed");
    this->async_lvgl_flush_ = false;
    return;
  }
  this->async_flush_task_handle_ = task_handle;
  ESP_LOGCONFIG(TAG, "Async LVGL flush ready task enabled on core %d", (int) flush_core);
}

void MipiDsi::async_flush_task_trampoline(void *arg) {
  static_cast<MipiDsi *>(arg)->async_flush_task_();
}

void MipiDsi::async_flush_task_() {
  while (true) {
    if (xSemaphoreTake(this->async_flush_done_, portMAX_DELAY) != pdTRUE)
      continue;
    if (this->async_transfer_start_us_ != 0) {
      const uint32_t done_us = (uint32_t) (esp_timer_get_time() - this->async_transfer_start_us_);
      this->async_perf_done_us_ += done_us;
      this->async_perf_done_flushes_++;
      if (done_us > this->async_perf_done_max_us_)
        this->async_perf_done_max_us_ = done_us;
      this->async_transfer_start_us_ = 0;
    }
    auto *callback = this->async_ready_callback_;
    void *arg = this->async_ready_arg_;
    this->async_ready_callback_ = nullptr;
    this->async_ready_arg_ = nullptr;
    if (callback != nullptr)
      callback(arg);
  }
}

bool MipiDsi::ensure_async_staging_buffer_(size_t size) {
  if (size == 0)
    return false;
  if (this->async_staging_buffer_ != nullptr && this->async_staging_buffer_size_ >= size)
    return true;

  if (this->async_staging_buffer_ != nullptr) {
    heap_caps_free(this->async_staging_buffer_);
    this->async_staging_buffer_ = nullptr;
    this->async_staging_buffer_size_ = 0;
  }

  const size_t aligned_size = (size + 127U) & ~size_t{127U};
  auto *buffer = static_cast<uint8_t *>(
      heap_caps_aligned_alloc(128, aligned_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
  if (buffer == nullptr) {
    ESP_LOGW(TAG, "Async LVGL flush staging buffer allocation failed (%zu bytes)", aligned_size);
    return false;
  }

  this->async_staging_buffer_ = buffer;
  this->async_staging_buffer_size_ = aligned_size;
  ESP_LOGI(TAG, "Async LVGL flush staging buffer: %p %zu bytes in PSRAM", buffer, aligned_size);
  return true;
}

bool MipiDsi::wait_for_refresh_done(uint32_t timeout_ms) {
  if (this->refresh_lock_ == nullptr)
    return false;
  while (xSemaphoreTake(this->refresh_lock_, 0) == pdTRUE) {
  }
  return xSemaphoreTake(this->refresh_lock_, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void MipiDsi::update() {
  if (this->auto_clear_enabled_) {
    this->clear();
  }
  if (this->show_test_card_) {
    this->test_card();
  } else if (this->page_ != nullptr) {
    this->page_->get_writer()(*this);
  } else if (this->writer_.has_value()) {
    (*this->writer_)(*this);
  } else {
    this->stop_poller();
  }
  if (this->buffer_ == nullptr || this->x_low_ > this->x_high_ || this->y_low_ > this->y_high_)
    return;
  ESP_LOGV(TAG, "x_low %d, y_low %d, x_high %d, y_high %d", this->x_low_, this->y_low_, this->x_high_, this->y_high_);
  int w = this->x_high_ - this->x_low_ + 1;
  int h = this->y_high_ - this->y_low_ + 1;
  this->write_to_display_(this->x_low_, this->y_low_, w, h, this->buffer_, this->x_low_, this->y_low_,
                          this->width_ - w - this->x_low_);
  // invalidate watermarks
  this->x_low_ = this->width_;
  this->y_low_ = this->height_;
  this->x_high_ = 0;
  this->y_high_ = 0;
}

void MipiDsi::loop() { this->log_dsi_diagnostics_(); }

void MipiDsi::log_dsi_diagnostics_() {
  const uint32_t underrun_total = dsi_underrun_total;
  if (underrun_total != this->last_underrun_total_) {
    const uint32_t now = millis();
    if (this->last_underrun_log_ms_ == 0 || now - this->last_underrun_log_ms_ >= DSI_DIAG_LOG_INTERVAL_MS) {
      ESP_LOGW(TAG, "dsi underrun irq: total=%" PRIu32 " (+%" PRIu32 ") tick=%" PRIu32,
               underrun_total, underrun_total - this->last_underrun_total_, dsi_underrun_last_tick);
      this->last_underrun_total_ = underrun_total;
      this->last_underrun_log_ms_ = now;
    }
  }

  if (esphome_mipi_dsi_poll_status != nullptr && this->handle_ != nullptr) {
    const uint32_t now = millis();
    if (this->last_status_poll_ms_ == 0 || now - this->last_status_poll_ms_ >= 50) {
      this->last_status_poll_ms_ = now;
      uint32_t bridge_status = 0;
      uint32_t bridge_raw = 0;
      uint32_t fifo_depth = 0;
      uint32_t host_status0 = 0;
      uint32_t host_status1 = 0;
      if (esphome_mipi_dsi_poll_status(this->handle_, &bridge_status, &bridge_raw, &fifo_depth, &host_status0,
                                       &host_status1) == ESP_OK) {
        const bool interesting = bridge_status != 0 || bridge_raw != 0 || host_status0 != 0 || host_status1 != 0;
        const bool changed = bridge_status != this->last_polled_bridge_status_ ||
                             bridge_raw != this->last_polled_bridge_raw_ ||
                             host_status0 != this->last_polled_host_status0_ ||
                             host_status1 != this->last_polled_host_status1_;
        if (interesting && changed) {
          ESP_LOGW(TAG,
                   "dsi poll: brg=0x%08" PRIx32 " raw=0x%08" PRIx32 " fifo=%" PRIu32
                   " host0=0x%08" PRIx32 " host1=0x%08" PRIx32 " dpi_under=%s",
                   bridge_status, bridge_raw, fifo_depth, host_status0, host_status1,
                   YESNO((host_status1 & DSI_DIAG_HOST_DPI_BUFF_PLD_UNDER) != 0));
          this->last_polled_bridge_status_ = bridge_status;
          this->last_polled_bridge_raw_ = bridge_raw;
          this->last_polled_host_status0_ = host_status0;
          this->last_polled_host_status1_ = host_status1;
        }
      }
    }
  }

  const uint32_t count = dsi_diag_event_count;
  if (count == this->last_diag_event_count_)
    return;

  const uint32_t now = millis();
  if (this->last_diag_log_ms_ != 0 && now - this->last_diag_log_ms_ < DSI_DIAG_LOG_INTERVAL_MS)
    return;

  const uint32_t index = (count - 1) % DSI_DIAG_EVENT_COUNT;
  DsiDiagnosticEvent event{};
  event.tick = dsi_diag_events[index].tick;
  event.bridge_status = dsi_diag_events[index].bridge_status;
  event.bridge_raw = dsi_diag_events[index].bridge_raw;
  event.fifo_depth = dsi_diag_events[index].fifo_depth;
  event.host_status0 = dsi_diag_events[index].host_status0;
  event.host_status1 = dsi_diag_events[index].host_status1;

  ESP_LOGW(TAG,
           "dsi diag: events=%" PRIu32 " (+%" PRIu32 ") suppressed=%" PRIu32 " tick=%" PRIu32
           " brg=0x%08" PRIx32 " raw=0x%08" PRIx32 " fifo=%" PRIu32 " host0=0x%08" PRIx32
           " host1=0x%08" PRIx32 " dpi_under=%s",
           count, count - this->last_diag_event_count_, dsi_diag_same_status_suppressed, event.tick,
           event.bridge_status, event.bridge_raw, event.fifo_depth, event.host_status0, event.host_status1,
           YESNO((event.host_status1 & DSI_DIAG_HOST_DPI_BUFF_PLD_UNDER) != 0));

  this->last_diag_event_count_ = count;
  this->last_diag_log_ms_ = now;
}

void MipiDsi::draw_pixels_at(int x_start, int y_start, int w, int h, const uint8_t *ptr, display::ColorOrder order,
                             display::ColorBitness bitness, bool big_endian, int x_offset, int y_offset, int x_pad) {
  if (w <= 0 || h <= 0)
    return;
  // if color mapping is required, pass the buck.
  // note that endianness is not considered here - it is assumed to match!
  if (bitness != this->color_depth_) {
    display::Display::draw_pixels_at(x_start, y_start, w, h, ptr, order, bitness, big_endian, x_offset, y_offset,
                                     x_pad);
    return;
  }
  this->write_to_display_(x_start, y_start, w, h, ptr, x_offset, y_offset, x_pad);
}

bool MipiDsi::draw_pixels_at_async(int x_start, int y_start, int w, int h, const uint8_t *ptr,
                                    display::ColorOrder order, display::ColorBitness bitness, bool big_endian,
                                    int x_offset, int y_offset, int x_pad, AsyncFlushReadyCallback ready_callback,
                                    void *ready_arg) {
  if (!this->async_lvgl_flush_ || this->async_flush_done_ == nullptr || this->async_flush_task_handle_ == nullptr ||
      ready_callback == nullptr)
    return false;
  if (w <= 0 || h <= 0 || ptr == nullptr)
    return false;
  if (!this->use_dma2d_ || bitness != this->color_depth_)
    return false;
  if (x_offset != 0 || y_offset != 0 || x_pad != 0)
    return false;
  if (this->async_flush_pending_)
    return false;

  const size_t payload_size = static_cast<size_t>(w) * static_cast<size_t>(h) * this->get_bytes_per_pixel_();
  const size_t row_bytes = static_cast<size_t>(w) * this->get_bytes_per_pixel_();
  const uintptr_t ptr_addr = reinterpret_cast<uintptr_t>(ptr);
  const bool src_internal = esp_ptr_internal(ptr);
  const bool unsafe_addr = !is_aligned(ptr_addr, DMA2D_SAFE_ALIGN_BYTES);
  const bool unsafe_row = !is_aligned(row_bytes, DMA2D_SAFE_ALIGN_BYTES);
  const bool unsafe_size = !is_aligned(payload_size, DMA2D_SAFE_ALIGN_BYTES);
  const bool src_external = esp_ptr_external_ram(ptr);
  const bool can_zero_copy = (src_internal || src_external) && !unsafe_addr && !unsafe_row && !unsafe_size;
  const uint8_t *flush_ptr = ptr;
  bool staged = false;
  uint32_t sync_us = 0;
  uint32_t copy_us = 0;
  if (can_zero_copy) {
    if (src_external) {
      const uint64_t sync_start_us = esp_timer_get_time();
      esp_err_t sync_err = cache_writeback_external_for_dma(ptr, payload_size);
      sync_us = (uint32_t) (esp_timer_get_time() - sync_start_us);
      if (sync_err != ESP_OK) {
        ESP_LOGW(TAG, "async zero-copy cache sync failed: %s ptr=%p size=%zu w=%d h=%d row=%zu",
                 esp_err_to_name(sync_err), ptr, payload_size, w, h, row_bytes);
        return false;
      }
    }
  } else {
    if (!this->ensure_async_staging_buffer_(payload_size))
      return false;
    const uint64_t copy_start_us = esp_timer_get_time();
    memcpy(this->async_staging_buffer_, ptr, payload_size);
    __sync_synchronize();
    copy_us = (uint32_t) (esp_timer_get_time() - copy_start_us);
    const uint64_t sync_start_us = esp_timer_get_time();
    esp_err_t sync_err = cache_writeback_external_for_dma(this->async_staging_buffer_, payload_size);
    sync_us = (uint32_t) (esp_timer_get_time() - sync_start_us);
    if (sync_err != ESP_OK) {
      ESP_LOGW(TAG, "async staging cache sync failed: %s size=%zu w=%d h=%d row=%zu",
               esp_err_to_name(sync_err), payload_size, w, h, row_bytes);
      return false;
    }
    flush_ptr = this->async_staging_buffer_;
    staged = true;
  }

  this->async_ready_callback_ = ready_callback;
  this->async_ready_arg_ = ready_arg;
  this->async_flush_pending_ = true;
  this->async_transfer_start_us_ = esp_timer_get_time();
  esp_err_t err = esp_lcd_panel_draw_bitmap(this->handle_, x_start, y_start, x_start + w, y_start + h, flush_ptr);
  const uint32_t submit_us = (uint32_t) (esp_timer_get_time() - this->async_transfer_start_us_);
  if (err != ESP_OK) {
    this->async_flush_pending_ = false;
    this->async_transfer_start_us_ = 0;
    this->async_ready_callback_ = nullptr;
    this->async_ready_arg_ = nullptr;
    ESP_LOGW(TAG, "async lcd_panel_draw_bitmap failed: %s", esp_err_to_name(err));
    return false;
  }
  this->async_perf_flushes_++;
  if (can_zero_copy)
    this->async_perf_zero_copy_flushes_++;
  this->async_perf_sync_us_ += sync_us;
  if (sync_us > this->async_perf_sync_max_us_)
    this->async_perf_sync_max_us_ = sync_us;
  this->async_perf_submit_us_ += submit_us;
  if (submit_us > this->async_perf_submit_max_us_)
    this->async_perf_submit_max_us_ = submit_us;
  if (staged) {
    this->async_perf_staged_flushes_++;
    if (unsafe_addr)
      this->async_perf_unsafe_addr_flushes_++;
    if (unsafe_row)
      this->async_perf_unsafe_row_flushes_++;
    if (unsafe_size)
      this->async_perf_unsafe_size_flushes_++;
    this->async_perf_staged_bytes_ += payload_size;
    this->async_perf_copy_us_ += copy_us;
    if (copy_us > this->async_perf_copy_max_us_)
      this->async_perf_copy_max_us_ = copy_us;
  }
  return true;
}

void MipiDsi::consume_async_flush_perf(AsyncFlushPerfStats *stats) {
  if (stats == nullptr)
    return;
  stats->flushes = this->async_perf_flushes_;
  stats->underruns = this->consume_underrun_count();
  stats->zero_copy_flushes = this->async_perf_zero_copy_flushes_;
  stats->staged_flushes = this->async_perf_staged_flushes_;
  stats->done_flushes = this->async_perf_done_flushes_;
  stats->unsafe_addr_flushes = this->async_perf_unsafe_addr_flushes_;
  stats->unsafe_row_flushes = this->async_perf_unsafe_row_flushes_;
  stats->unsafe_size_flushes = this->async_perf_unsafe_size_flushes_;
  stats->staged_bytes = this->async_perf_staged_bytes_;
  stats->sync_us = this->async_perf_sync_us_;
  stats->copy_us = this->async_perf_copy_us_;
  stats->submit_us = this->async_perf_submit_us_;
  stats->done_us = this->async_perf_done_us_;
  stats->sync_max_us = this->async_perf_sync_max_us_;
  stats->copy_max_us = this->async_perf_copy_max_us_;
  stats->submit_max_us = this->async_perf_submit_max_us_;
  stats->done_max_us = this->async_perf_done_max_us_;

  this->async_perf_flushes_ = 0;
  this->async_perf_zero_copy_flushes_ = 0;
  this->async_perf_staged_flushes_ = 0;
  this->async_perf_done_flushes_ = 0;
  this->async_perf_unsafe_addr_flushes_ = 0;
  this->async_perf_unsafe_row_flushes_ = 0;
  this->async_perf_unsafe_size_flushes_ = 0;
  this->async_perf_staged_bytes_ = 0;
  this->async_perf_sync_us_ = 0;
  this->async_perf_copy_us_ = 0;
  this->async_perf_submit_us_ = 0;
  this->async_perf_done_us_ = 0;
  this->async_perf_sync_max_us_ = 0;
  this->async_perf_copy_max_us_ = 0;
  this->async_perf_submit_max_us_ = 0;
  this->async_perf_done_max_us_ = 0;
}

uint32_t MipiDsi::consume_underrun_count() {
  const uint32_t count = dsi_underrun_count;
  dsi_underrun_count = 0;
  return count;
}

bool MipiDsi::present_frame_buffer(uint8_t *frame_buffer, int y_start, int y_end) {
  if (frame_buffer == nullptr || (frame_buffer != this->frame_buffers_[0] && frame_buffer != this->frame_buffers_[1]))
    return false;
  if (y_end < y_start)
    return false;
  if (this->async_lvgl_flush_) {
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(50);
    while (this->async_flush_pending_) {
      if ((int32_t) (xTaskGetTickCount() - deadline) >= 0) {
        ESP_LOGW(TAG, "present_frame_buffer timed out waiting for async LVGL flush");
        return false;
      }
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
  y_start = std::max(0, y_start);
  y_end = std::min<int>(this->height_ - 1, y_end);
  if (y_end < y_start)
    return false;
  if (this->refresh_lock_ != nullptr) {
    while (xSemaphoreTake(this->refresh_lock_, 0) == pdTRUE) {
    }
    xSemaphoreTake(this->refresh_lock_, pdMS_TO_TICKS(20));
  }
  esp_err_t err = esp_lcd_panel_draw_bitmap(this->handle_, 0, y_start, this->width_, y_end + 1, frame_buffer);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "present_frame_buffer failed: %s", esp_err_to_name(err));
    return false;
  }
  xSemaphoreTake(this->io_lock_, portMAX_DELAY);
  return true;
}

void MipiDsi::write_to_display_(int x_start, int y_start, int w, int h, const uint8_t *ptr, int x_offset, int y_offset,
                                 int x_pad) {
  esp_err_t err = ESP_OK;
  auto bytes_per_pixel = this->get_bytes_per_pixel_();
  auto stride = (x_offset + w + x_pad) * bytes_per_pixel;
  ptr += y_offset * stride + x_offset * bytes_per_pixel;  // skip to the first pixel
  // x_ and y_offset are offsets into the source buffer, unrelated to our own offsets into the display.
  if (x_offset == 0 && x_pad == 0) {
    err = esp_lcd_panel_draw_bitmap(this->handle_, x_start, y_start, x_start + w, y_start + h, ptr);
    xSemaphoreTake(this->io_lock_, portMAX_DELAY);

  } else {
    // draw line by line
    for (int y = 0; y != h; y++) {
      err = esp_lcd_panel_draw_bitmap(this->handle_, x_start, y + y_start, x_start + w, y + y_start + 1, ptr);
      if (err != ESP_OK)
        break;
      ptr += stride;  // next line
      xSemaphoreTake(this->io_lock_, portMAX_DELAY);
    }
  }
  if (err != ESP_OK)
    ESP_LOGE(TAG, "lcd_lcd_panel_draw_bitmap failed: %s", esp_err_to_name(err));
}

bool MipiDsi::check_buffer_() {
  if (this->is_failed())
    return false;
  if (this->buffer_ != nullptr)
    return true;
  // this is dependent on the enum values.
  auto bytes_per_pixel = 3 - this->color_depth_;
  RAMAllocator<uint8_t> allocator;
  this->buffer_ = allocator.allocate(this->height_ * this->width_ * bytes_per_pixel);
  if (this->buffer_ == nullptr) {
    this->mark_failed(LOG_STR("Could not allocate buffer for display!"));
    return false;
  }
  return true;
}

void MipiDsi::draw_pixel_at(int x, int y, Color color) {
  if (!this->get_clipping().inside(x, y))
    return;

  switch (this->rotation_) {
    case display::DISPLAY_ROTATION_0_DEGREES:
      break;
    case display::DISPLAY_ROTATION_90_DEGREES:
      std::swap(x, y);
      x = this->width_ - x - 1;
      break;
    case display::DISPLAY_ROTATION_180_DEGREES:
      x = this->width_ - x - 1;
      y = this->height_ - y - 1;
      break;
    case display::DISPLAY_ROTATION_270_DEGREES:
      std::swap(x, y);
      y = this->height_ - y - 1;
      break;
  }
  if (x >= this->get_width_internal() || x < 0 || y >= this->get_height_internal() || y < 0) {
    return;
  }
  if (!this->check_buffer_())
    return;
  size_t pos = (y * this->width_) + x;
  switch (this->color_depth_) {
    case display::COLOR_BITNESS_565: {
      auto *ptr_16 = reinterpret_cast<uint16_t *>(this->buffer_);
      uint8_t hi_byte = static_cast<uint8_t>(color.r & 0xF8) | (color.g >> 5);
      uint8_t lo_byte = static_cast<uint8_t>((color.g & 0x1C) << 3) | (color.b >> 3);
      uint16_t new_color = lo_byte | (hi_byte << 8);  // little endian
      if (ptr_16[pos] == new_color)
        return;
      ptr_16[pos] = new_color;
      break;
    }
    case display::COLOR_BITNESS_888:
      if (this->color_mode_ == display::COLOR_ORDER_BGR) {
        this->buffer_[pos * 3] = color.b;
        this->buffer_[pos * 3 + 1] = color.g;
        this->buffer_[pos * 3 + 2] = color.r;
      } else {
        this->buffer_[pos * 3] = color.r;
        this->buffer_[pos * 3 + 1] = color.g;
        this->buffer_[pos * 3 + 2] = color.b;
      }
      break;
    case display::COLOR_BITNESS_332:
      break;
  }
  // low and high watermark may speed up drawing from buffer
  if (x < this->x_low_)
    this->x_low_ = x;
  if (y < this->y_low_)
    this->y_low_ = y;
  if (x > this->x_high_)
    this->x_high_ = x;
  if (y > this->y_high_)
    this->y_high_ = y;
}
void MipiDsi::fill(Color color) {
  if (!this->check_buffer_())
    return;

  // If clipping is active, fall back to base implementation
  if (this->get_clipping().is_set()) {
    Display::fill(color);
    return;
  }

  switch (this->color_depth_) {
    case display::COLOR_BITNESS_565: {
      auto *ptr_16 = reinterpret_cast<uint16_t *>(this->buffer_);
      uint8_t hi_byte = static_cast<uint8_t>(color.r & 0xF8) | (color.g >> 5);
      uint8_t lo_byte = static_cast<uint8_t>((color.g & 0x1C) << 3) | (color.b >> 3);
      uint16_t new_color = lo_byte | (hi_byte << 8);  // little endian
      std::fill_n(ptr_16, this->width_ * this->height_, new_color);
      break;
    }

    case display::COLOR_BITNESS_888:
      if (this->color_mode_ == display::COLOR_ORDER_BGR) {
        for (size_t i = 0; i != this->width_ * this->height_; i++) {
          this->buffer_[i * 3 + 0] = color.b;
          this->buffer_[i * 3 + 1] = color.g;
          this->buffer_[i * 3 + 2] = color.r;
        }
      } else {
        for (size_t i = 0; i != this->width_ * this->height_; i++) {
          this->buffer_[i * 3 + 0] = color.r;
          this->buffer_[i * 3 + 1] = color.g;
          this->buffer_[i * 3 + 2] = color.b;
        }
      }

    default:
      break;
  }
}

int MipiDsi::get_width() {
  switch (this->rotation_) {
    case display::DISPLAY_ROTATION_90_DEGREES:
    case display::DISPLAY_ROTATION_270_DEGREES:
      return this->get_height_internal();
    case display::DISPLAY_ROTATION_0_DEGREES:
    case display::DISPLAY_ROTATION_180_DEGREES:
    default:
      return this->get_width_internal();
  }
}

int MipiDsi::get_height() {
  switch (this->rotation_) {
    case display::DISPLAY_ROTATION_0_DEGREES:
    case display::DISPLAY_ROTATION_180_DEGREES:
      return this->get_height_internal();
    case display::DISPLAY_ROTATION_90_DEGREES:
    case display::DISPLAY_ROTATION_270_DEGREES:
    default:
      return this->get_width_internal();
  }
}

static const uint8_t PIXEL_MODES[] = {0, 16, 18, 24};

void MipiDsi::dump_config() {
  ESP_LOGCONFIG(TAG,
                "MIPI_DSI RGB LCD"
                "\n  Model: %s"
                "\n  Width: %u"
                "\n  Height: %u"
                "\n  Rotation: %d degrees"
                "\n  DSI Lanes: %u"
                "\n  Lane Bit Rate: %.0fMbps"
                "\n  HSync Pulse Width: %u"
                "\n  HSync Back Porch: %u"
                "\n  HSync Front Porch: %u"
                "\n  VSync Pulse Width: %u"
                "\n  VSync Back Porch: %u"
                "\n  VSync Front Porch: %u"
                "\n  Buffer Color Depth: %d bit"
                "\n  Display Pixel Mode: %d bit"
                "\n  Invert Colors: %s"
                "\n  DMA2D: %s"
                "\n  Pixel Clock: %.1fMHz",
                this->model_, this->width_, this->height_, this->rotation_, this->lanes_, this->lane_bit_rate_,
                this->hsync_pulse_width_, this->hsync_back_porch_, this->hsync_front_porch_, this->vsync_pulse_width_,
                this->vsync_back_porch_, this->vsync_front_porch_, (3 - this->color_depth_) * 8, this->pixel_mode_,
                YESNO(this->invert_colors_), YESNO(this->use_dma2d_), this->pclk_frequency_);
  LOG_PIN("  Reset Pin ", this->reset_pin_);
}
}  // namespace esphome::mipi_dsi
#endif  // USE_ESP32_VARIANT_ESP32P4
