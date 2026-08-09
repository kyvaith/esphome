#include "material_voice_assistant.h"

#include "esphome/core/log.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <utility>

#ifdef USE_ESP32
#include "esp_heap_caps.h"
#include "esp_timer.h"
#endif

namespace esphome::lvgl_material {

extern "C" uint32_t lvgl_esphome_get_perf_logging_enabled(void);

static const char *const TAG = "lvgl_material.voice";
static constexpr int PHASE_MASK = 1023;
static constexpr int PHASE_HALF = 512;
static constexpr size_t WORKER_STACK_BYTES = 6144;
static constexpr uint32_t LABEL_ANIMATION_DURATION_MS = 231;
static constexpr uint32_t LABEL_ANIMATION_STEP_MS = 33;
static constexpr uint32_t TRANSCRIPT_COMMIT_INTERVAL_MS = 100;
static constexpr int MAX_TEXT_LINES = 16;
static constexpr float PI = 3.14159265358979323846f;

static uint32_t utf8_next_(const char *text, uint32_t length, uint32_t &index) {
  if (text == nullptr || index >= length)
    return 0;
  const uint8_t first = static_cast<uint8_t>(text[index++]);
  if ((first & 0x80U) == 0)
    return first;
  uint32_t value = 0;
  uint8_t remaining = 0;
  if ((first & 0xE0U) == 0xC0U) {
    value = first & 0x1FU;
    remaining = 1;
  } else if ((first & 0xF0U) == 0xE0U) {
    value = first & 0x0FU;
    remaining = 2;
  } else if ((first & 0xF8U) == 0xF0U) {
    value = first & 0x07U;
    remaining = 3;
  } else {
    return 0xFFFDU;
  }
  while (remaining-- > 0) {
    if (index >= length)
      return 0xFFFDU;
    const uint8_t continuation = static_cast<uint8_t>(text[index++]);
    if ((continuation & 0xC0U) != 0x80U)
      return 0xFFFDU;
    value = (value << 6U) | (continuation & 0x3FU);
  }
  return value;
}

void MaterialVoiceAssistant::setup() {
  if (this->lvgl_component_ == nullptr || this->root_ == nullptr || this->status_label_ == nullptr ||
      this->user_label_ == nullptr || this->assistant_label_ == nullptr || this->waveform_ == nullptr) {
    ESP_LOGE(TAG, "Voice assistant presenter configuration is incomplete");
    this->mark_failed();
    return;
  }

  for (int index = 0; index < 1024; index++)
    this->sine_table_[index] = static_cast<int16_t>(std::sin(index * 2.0f * PI / 1024.0f) * 32767.0f);

  lv_obj_update_layout(this->root_);
  if (!this->allocate_buffers_()) {
    ESP_LOGE(TAG, "Unable to allocate voice presenter buffers");
    this->mark_failed();
    return;
  }

  this->label_animations_[0].label = this->user_label_;
  this->label_animations_[1].label = this->assistant_label_;

  // These labels remain as declarative geometry/font sources, but their
  // pixels are presented by the direct text regions below. Updating native
  // labels forced a full LVGL layout/refresh and stalled the waveform for
  // 100-160 ms whenever another transcript fragment arrived.
  lv_obj_add_flag(this->status_label_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(this->user_label_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(this->assistant_label_, LV_OBJ_FLAG_HIDDEN);

#ifdef USE_ESP32
  this->worker_stack_ = static_cast<StackType_t *>(
      heap_caps_aligned_alloc(16, WORKER_STACK_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (this->worker_stack_ == nullptr) {
    this->worker_stack_ = static_cast<StackType_t *>(
        heap_caps_aligned_alloc(16, WORKER_STACK_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (this->worker_stack_ == nullptr) {
    ESP_LOGE(TAG, "Unable to allocate voice waveform worker stack");
    this->mark_failed();
    return;
  }
#if CONFIG_FREERTOS_UNICORE
  constexpr BaseType_t render_core = tskNO_AFFINITY;
#else
  const BaseType_t render_core = xPortGetCoreID();
#endif
  this->worker_handle_ = xTaskCreateStaticPinnedToCore(worker_, "voice_wave", WORKER_STACK_BYTES, this, 1,
                                                       this->worker_stack_, &this->worker_storage_, render_core);
  if (this->worker_handle_ == nullptr) {
    ESP_LOGE(TAG, "Unable to start voice waveform worker");
    heap_caps_free(this->worker_stack_);
    this->worker_stack_ = nullptr;
    this->release_buffers_();
    this->mark_failed();
  }
#endif
}

void MaterialVoiceAssistant::loop() {
  this->service_release_();
  const uint32_t now = millis();
  if (this->is_failed() || !this->active_.load(std::memory_order_acquire))
    return;
  this->service_transcripts_(now);
  this->service_label_animations_(now);
  this->service_content_animation_(now);

  if (now - this->last_frame_ms_ >= this->frame_interval_ms_) {
    uint32_t elapsed = this->frame_interval_ms_;
    if (this->last_frame_ms_ == 0) {
      this->last_frame_ms_ = now;
    } else {
      elapsed = now - this->last_frame_ms_;
      this->last_frame_ms_ += this->frame_interval_ms_;
      // Preserve the fractional cadence across loop iterations, but do not
      // attempt a burst of catch-up frames after a genuinely delayed loop.
      if (now - this->last_frame_ms_ > this->frame_interval_ms_)
        this->last_frame_ms_ = now;
    }

    const VoicePhase phase = this->phase_.load(std::memory_order_relaxed);
    uint16_t target = 0;
    if (phase == VoicePhase::LISTENING)
      target = this->input_level_q15_.load(std::memory_order_relaxed);
    else if (phase == VoicePhase::SPEAKING)
      target = this->output_level_q15_.load(std::memory_order_relaxed);

    const uint16_t current_level = this->smoothed_level_q15_.load(std::memory_order_relaxed);
    const int32_t delta = static_cast<int32_t>(target) - current_level;
    const int32_t divisor = delta >= 0 ? 3 : 7;
    this->smoothed_level_q15_.store(
        static_cast<uint16_t>(std::clamp<int32_t>(static_cast<int32_t>(current_level) + delta / divisor, 0, 32767)),
        std::memory_order_relaxed);

    const uint32_t speed = phase == VoicePhase::SPEAKING || phase == VoicePhase::LISTENING ? 92 : 48;
    this->wave_phase_.store((this->wave_phase_.load(std::memory_order_relaxed) + speed * elapsed / 33U) & PHASE_MASK,
                            std::memory_order_relaxed);
    this->breathe_phase_.store((this->breathe_phase_.load(std::memory_order_relaxed) + 7U * elapsed / 33U) & PHASE_MASK,
                               std::memory_order_relaxed);
    // Wake the independent wave worker before rasterizing a transcript phrase.
    // Text can take tens of milliseconds in PSRAM; scheduling it first made
    // the wave visibly miss the frame in which a phrase appeared.
    this->schedule_frame_();
  }

  this->service_text_regions_();
  this->log_performance_(now);
}

void MaterialVoiceAssistant::on_shutdown() {
  this->set_active(false);
  const uint32_t started = millis();
  while ((this->worker_busy_.load(std::memory_order_acquire) || this->slots_[0].in_flight.load() ||
          this->slots_[1].in_flight.load() || this->status_region_.in_flight.load() ||
          this->transcript_region_.in_flight.load()) &&
         millis() - started < 100) {
#ifdef USE_ESP32
    vTaskDelay(pdMS_TO_TICKS(1));
#endif
  }
  this->service_release_();
#ifdef USE_ESP32
  this->shutdown_requested_.store(true, std::memory_order_release);
  if (this->worker_handle_ != nullptr)
    xTaskNotifyGive(this->worker_handle_);
  while (!this->worker_stopped_.load(std::memory_order_acquire) && millis() - started < 250)
    vTaskDelay(pdMS_TO_TICKS(1));
  if (!this->worker_stopped_.load(std::memory_order_acquire)) {
    ESP_LOGE(TAG, "Voice waveform worker did not stop; preserving its buffers");
    return;
  }
#endif
  this->release_buffers_();
}

void MaterialVoiceAssistant::dump_config() {
  ESP_LOGCONFIG(TAG, "Material Voice Assistant:");
  ESP_LOGCONFIG(TAG, "  Waveform region: %dx%d at %d,%d", this->width_, this->height_, this->screen_x_,
                this->screen_y_);
  ESP_LOGCONFIG(TAG, "  Transcript region: %dx%d at %d,%d", this->transcript_region_.width,
                this->transcript_region_.height, this->transcript_region_.x, this->transcript_region_.y);
  ESP_LOGCONFIG(TAG, "  Status region: %dx%d at %d,%d", this->status_region_.width, this->status_region_.height,
                this->status_region_.x, this->status_region_.y);
  ESP_LOGCONFIG(TAG, "  Frame interval: %u ms", static_cast<unsigned>(this->frame_interval_ms_));
  ESP_LOGCONFIG(
      TAG, "  Reserved direct buffers: %u bytes",
      static_cast<unsigned>(this->buffer_bytes_ * 3 + this->status_region_.bytes + this->transcript_region_.bytes));
}

void MaterialVoiceAssistant::set_active(bool active) {
  const bool previous = this->active_.exchange(active, std::memory_order_acq_rel);
  if (active && !previous) {
    this->release_pending_.store(false, std::memory_order_release);
    this->last_frame_ms_ = 0;
    this->frame_pending_.store(true, std::memory_order_release);
    this->status_region_.pending.store(true, std::memory_order_release);
    this->transcript_region_.pending.store(true, std::memory_order_release);
#ifdef USE_ESP32
    if (this->worker_handle_ != nullptr)
      xTaskNotifyGive(this->worker_handle_);
#endif
  } else if (!active && (previous || this->region_owned_.load(std::memory_order_acquire))) {
    this->frame_pending_.store(false, std::memory_order_release);
    this->status_region_.pending.store(false, std::memory_order_release);
    this->transcript_region_.pending.store(false, std::memory_order_release);
    this->release_pending_.store(true, std::memory_order_release);
    this->input_level_q15_.store(0, std::memory_order_relaxed);
    this->output_level_q15_.store(0, std::memory_order_relaxed);
    this->smoothed_level_q15_.store(0, std::memory_order_relaxed);
  }
}

void MaterialVoiceAssistant::set_phase(const std::string &phase) {
  if (phase != this->phase_text_) {
    this->phase_text_ = phase;
    this->status_region_.pending.store(true, std::memory_order_release);
  }

  std::string normalized = phase;
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
  VoicePhase mapped = VoicePhase::IDLE;
  if (normalized.find("listen") != std::string::npos)
    mapped = VoicePhase::LISTENING;
  else if (normalized.find("think") != std::string::npos)
    mapped = VoicePhase::THINKING;
  else if (normalized.find("answer") != std::string::npos || normalized.find("speak") != std::string::npos)
    mapped = VoicePhase::SPEAKING;
  else if (normalized.find("thank") != std::string::npos)
    mapped = VoicePhase::THANKS;
  else if (normalized.find("error") != std::string::npos || normalized.find("unavailable") != std::string::npos)
    mapped = VoicePhase::ERROR;
  this->phase_.store(mapped, std::memory_order_relaxed);
}

void MaterialVoiceAssistant::set_transcripts(const std::string &user, const std::string &assistant) {
  const std::string sanitized_user = user == "..." ? "" : user;
  if (sanitized_user.empty() && assistant.empty()) {
    if (!this->requested_previous_assistant_text_.empty() || !this->requested_user_text_.empty() ||
        !this->requested_assistant_text_.empty()) {
      this->requested_previous_assistant_text_.clear();
      this->requested_user_text_.clear();
      this->requested_assistant_text_.clear();
      this->awaiting_new_assistant_ = false;
      this->animate_user_on_commit_ = false;
      this->animate_assistant_on_commit_ = false;
      this->animate_content_on_commit_ = false;
      this->transcript_dirty_ = true;
    }
    return;
  }

  const bool user_changed = this->requested_user_text_ != sanitized_user;
  const bool new_user = user_changed && !sanitized_user.empty() && !this->requested_user_text_.empty() &&
                        !this->requested_assistant_text_.empty() &&
                        !is_continuation_(this->requested_user_text_, sanitized_user);
  if (new_user) {
    // Keep only the immediately preceding assistant paragraph. The previous
    // user turn disappears, while the new user paragraph is placed below the
    // retained answer.
    this->requested_previous_assistant_text_ = this->requested_assistant_text_;
    this->requested_user_text_ = sanitized_user;
    this->requested_assistant_text_.clear();
    this->awaiting_new_assistant_ = true;
    this->animate_user_on_commit_ = true;
    this->animate_assistant_on_commit_ = false;
    this->animate_content_on_commit_ = true;
    this->transcript_dirty_ = true;
    return;
  }

  if (user_changed) {
    this->requested_user_text_ = sanitized_user;
    this->transcript_dirty_ = true;
    if (!sanitized_user.empty() && this->user_text_.empty())
      this->animate_user_on_commit_ = true;
  }

  // While a follow-up user turn is being transcribed, the ESPHome text sensor
  // still carries the preceding assistant answer. Do not mistake that stale
  // value for the first fragment of the new reply.
  if (this->awaiting_new_assistant_ && assistant == this->requested_previous_assistant_text_)
    return;

  const bool assistant_changed = this->requested_assistant_text_ != assistant;
  if (assistant_changed) {
    const bool new_assistant = !assistant.empty() &&
                               (this->requested_assistant_text_.empty() ||
                                !is_continuation_(this->requested_assistant_text_, assistant));
    this->requested_assistant_text_ = assistant;
    if (!assistant.empty())
      this->awaiting_new_assistant_ = false;
    if (new_assistant) {
      this->animate_assistant_on_commit_ = true;
      this->animate_content_on_commit_ = true;
    }
    this->transcript_dirty_ = true;
  }
}

void MaterialVoiceAssistant::set_audio_levels(float input_level, float output_level) {
  input_level = std::clamp(input_level, 0.0f, 1.0f);
  output_level = std::clamp(output_level, 0.0f, 1.0f);
  this->input_level_q15_.store(static_cast<uint16_t>(input_level * 32767.0f), std::memory_order_relaxed);
  this->output_level_q15_.store(static_cast<uint16_t>(output_level * 32767.0f), std::memory_order_relaxed);
}

bool MaterialVoiceAssistant::allocate_buffers_() {
  lv_area_t waveform_area{};
  lv_area_t status_area{};
  lv_area_t transcript_area{};
  lv_obj_get_coords(this->waveform_, &waveform_area);
  lv_obj_get_coords(this->status_label_, &status_area);
  lv_obj_get_coords(lv_obj_get_parent(this->user_label_), &transcript_area);
  this->screen_x_ = waveform_area.x1;
  this->screen_y_ = waveform_area.y1;
  this->width_ = lv_area_get_width(&waveform_area);
  this->height_ = lv_area_get_height(&waveform_area);
  lv_display_t *display = lv_obj_get_display(this->waveform_);
  this->display_height_ = display != nullptr ? lv_display_get_vertical_resolution(display) : 0;
  if (this->width_ <= 0 || this->height_ <= 0 || this->display_height_ <= 0 || lv_area_get_width(&status_area) <= 0 ||
      lv_area_get_height(&status_area) <= 0 || lv_area_get_width(&transcript_area) <= 0 ||
      lv_area_get_height(&transcript_area) <= 0 || sizeof(lv_color_t) != 3)
    return false;

  this->status_style_.font = lv_obj_get_style_text_font(this->status_label_, LV_PART_MAIN);
  this->status_style_.color = lv_obj_get_style_text_color(this->status_label_, LV_PART_MAIN);
  this->status_style_.letter_space = lv_obj_get_style_text_letter_space(this->status_label_, LV_PART_MAIN);
  this->status_style_.line_space = lv_obj_get_style_text_line_space(this->status_label_, LV_PART_MAIN);
  this->user_style_.font = lv_obj_get_style_text_font(this->user_label_, LV_PART_MAIN);
  this->user_style_.color = lv_obj_get_style_text_color(this->user_label_, LV_PART_MAIN);
  this->user_style_.letter_space = lv_obj_get_style_text_letter_space(this->user_label_, LV_PART_MAIN);
  this->user_style_.line_space = lv_obj_get_style_text_line_space(this->user_label_, LV_PART_MAIN);
  this->assistant_style_.font = lv_obj_get_style_text_font(this->assistant_label_, LV_PART_MAIN);
  this->assistant_style_.color = lv_obj_get_style_text_color(this->assistant_label_, LV_PART_MAIN);
  this->assistant_style_.letter_space = lv_obj_get_style_text_letter_space(this->assistant_label_, LV_PART_MAIN);
  this->assistant_style_.line_space = lv_obj_get_style_text_line_space(this->assistant_label_, LV_PART_MAIN);
  if (this->status_style_.font == nullptr || this->user_style_.font == nullptr ||
      this->assistant_style_.font == nullptr)
    return false;

  this->text_background_color_ = lv_obj_get_style_bg_color(this->root_, LV_PART_MAIN);
  const char *initial_status = lv_label_get_text(this->status_label_);
  if (initial_status != nullptr)
    this->phase_text_ = initial_status;

  this->status_region_.owner = this;
  this->status_region_.x = status_area.x1;
  this->status_region_.y = status_area.y1;
  this->status_region_.width = lv_area_get_width(&status_area);
  this->status_region_.height = lv_area_get_height(&status_area);
  this->status_region_.bytes =
      static_cast<size_t>(this->status_region_.width) * this->status_region_.height * sizeof(lv_color_t);

  this->transcript_region_.owner = this;
  this->transcript_region_.x = transcript_area.x1;
  this->transcript_region_.y = transcript_area.y1;
  this->transcript_region_.width = lv_area_get_width(&transcript_area);
  this->transcript_region_.height = lv_area_get_height(&transcript_area);
  this->transcript_region_.bytes =
      static_cast<size_t>(this->transcript_region_.width) * this->transcript_region_.height * sizeof(lv_color_t);
  this->transcript_panel_x_ = 0;
  this->transcript_panel_y_ = 0;
  this->transcript_panel_width_ = this->transcript_region_.width;
  this->transcript_panel_height_ = this->transcript_region_.height;

  this->pixel_count_ = static_cast<size_t>(this->width_) * this->height_;
  this->buffer_bytes_ = this->pixel_count_ * sizeof(lv_color_t);
#ifdef USE_ESP32
  this->backdrop_ =
      static_cast<lv_color_t *>(heap_caps_aligned_alloc(64, this->buffer_bytes_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  for (auto &slot : this->slots_)
    slot.pixels = static_cast<lv_color_t *>(
        heap_caps_aligned_alloc(64, this->buffer_bytes_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  this->status_region_.pixels = static_cast<lv_color_t *>(
      heap_caps_aligned_alloc(64, this->status_region_.bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  this->transcript_region_.pixels = static_cast<lv_color_t *>(
      heap_caps_aligned_alloc(64, this->transcript_region_.bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
  this->backdrop_ = static_cast<lv_color_t *>(std::malloc(this->buffer_bytes_));
  for (auto &slot : this->slots_)
    slot.pixels = static_cast<lv_color_t *>(std::malloc(this->buffer_bytes_));
  this->status_region_.pixels = static_cast<lv_color_t *>(std::malloc(this->status_region_.bytes));
  this->transcript_region_.pixels = static_cast<lv_color_t *>(std::malloc(this->transcript_region_.bytes));
#endif
  const int max_line_height = std::max({static_cast<int>(this->status_style_.font->line_height),
                                        static_cast<int>(this->user_style_.font->line_height),
                                        static_cast<int>(this->assistant_style_.font->line_height)});
  this->glyph_buffer_ = lv_draw_buf_create(std::max(96, max_line_height * 2), std::max(96, max_line_height + 48),
                                           LV_COLOR_FORMAT_A8, LV_STRIDE_AUTO);
  if (this->backdrop_ == nullptr || this->slots_[0].pixels == nullptr || this->slots_[1].pixels == nullptr ||
      this->status_region_.pixels == nullptr || this->transcript_region_.pixels == nullptr ||
      this->glyph_buffer_ == nullptr) {
    this->release_buffers_();
    return false;
  }
  for (auto &slot : this->slots_)
    slot.owner = this;
  this->build_backdrop_();
  this->fill_text_region_(this->status_region_);
  this->fill_text_region_(this->transcript_region_);
  this->status_region_.pending.store(true, std::memory_order_release);
  this->transcript_region_.pending.store(true, std::memory_order_release);
  return true;
}

void MaterialVoiceAssistant::release_buffers_() {
  if (this->glyph_buffer_ != nullptr)
    lv_draw_buf_destroy(this->glyph_buffer_);
  this->glyph_buffer_ = nullptr;
#ifdef USE_ESP32
  heap_caps_free(this->backdrop_);
  for (auto &slot : this->slots_)
    heap_caps_free(slot.pixels);
  heap_caps_free(this->status_region_.pixels);
  heap_caps_free(this->transcript_region_.pixels);
  heap_caps_free(this->worker_stack_);
#else
  std::free(this->backdrop_);
  for (auto &slot : this->slots_)
    std::free(slot.pixels);
  std::free(this->status_region_.pixels);
  std::free(this->transcript_region_.pixels);
#endif
  this->backdrop_ = nullptr;
  for (auto &slot : this->slots_) {
    slot.pixels = nullptr;
    slot.in_flight.store(false, std::memory_order_release);
  }
  this->status_region_.pixels = nullptr;
  this->status_region_.in_flight.store(false, std::memory_order_release);
  this->status_region_.owned.store(false, std::memory_order_release);
  this->transcript_region_.pixels = nullptr;
  this->transcript_region_.in_flight.store(false, std::memory_order_release);
  this->transcript_region_.owned.store(false, std::memory_order_release);
#ifdef USE_ESP32
  this->worker_stack_ = nullptr;
#endif
}

void MaterialVoiceAssistant::build_backdrop_() {
  const int gradient_start_y =
      std::clamp(static_cast<int>(this->display_height_ * this->gradient_start_), 0, this->display_height_ - 1);
  const int gradient_span = std::max(1, this->display_height_ - 1 - gradient_start_y);
  for (int y = 0; y < this->height_; y++) {
    const int absolute_y = this->screen_y_ + y;
    const int strength = std::clamp((absolute_y - gradient_start_y) * 255 / gradient_span, 0, 255);
    for (int x = 0; x < this->width_; x++) {
      lv_color_t &pixel = this->backdrop_[static_cast<size_t>(y) * this->width_ + x];
      pixel.red = static_cast<uint8_t>(this->gradient_bottom_color_.red * strength / 255);
      pixel.green = static_cast<uint8_t>(this->gradient_bottom_color_.green * strength / 255);
      pixel.blue = static_cast<uint8_t>(this->gradient_bottom_color_.blue * strength / 255);
    }
  }
  for (auto &slot : this->slots_) {
    std::memcpy(slot.pixels, this->backdrop_, this->buffer_bytes_);
    slot.dirty_y1 = 1;
    slot.dirty_y2 = 0;
  }
}

void MaterialVoiceAssistant::schedule_frame_() {
  if (lvgl_esphome_get_perf_logging_enabled() != 0)
    this->perf_requested_.fetch_add(1, std::memory_order_relaxed);
  this->frame_pending_.store(true, std::memory_order_release);
#ifdef USE_ESP32
  if (this->worker_handle_ != nullptr)
    xTaskNotifyGive(this->worker_handle_);
#else
  this->render_frame_();
#endif
}

bool MaterialVoiceAssistant::render_frame_() {
  if (!this->active_.load(std::memory_order_acquire) || this->backdrop_ == nullptr)
    return false;

  const bool perf_enabled = lvgl_esphome_get_perf_logging_enabled() != 0;
#ifdef USE_ESP32
  const int64_t render_started_us = perf_enabled ? esp_timer_get_time() : 0;
#endif

  PresentSlot *slot = nullptr;
  for (auto &candidate : this->slots_) {
    bool expected = false;
    if (candidate.in_flight.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
      slot = &candidate;
      break;
    }
  }
  if (slot == nullptr) {
    if (perf_enabled)
      this->perf_no_slot_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }

  if (slot->dirty_y1 <= slot->dirty_y2) {
    const size_t row_bytes = static_cast<size_t>(this->width_) * sizeof(lv_color_t);
    const size_t offset = static_cast<size_t>(slot->dirty_y1) * row_bytes;
    const size_t restore_bytes = static_cast<size_t>(slot->dirty_y2 - slot->dirty_y1 + 1) * row_bytes;
    std::memcpy(reinterpret_cast<uint8_t *>(slot->pixels) + offset,
                reinterpret_cast<const uint8_t *>(this->backdrop_) + offset, restore_bytes);
  }
  int dirty_y1 = this->height_;
  int dirty_y2 = -1;
  const int breathe = (this->sin_q15_(this->breathe_phase_.load(std::memory_order_relaxed)) + 32767) * 4 / 65534;
  const int energy = this->smoothed_level_q15_.load(std::memory_order_relaxed);
  const int energetic_amplitude = energy * std::max(1, this->height_ / 4) / 32767;
  const int base = std::max(4, this->height_ / 28) + breathe;
  const int phase = static_cast<int>(this->wave_phase_.load(std::memory_order_relaxed));
  this->draw_wave_(slot->pixels, phase, base + energetic_amplitude, 2355, 4710, this->primary_color_.red,
                   this->primary_color_.green, this->primary_color_.blue,
                   static_cast<uint8_t>(150 + energy * 90 / 32767), 2, dirty_y1, dirty_y2);
  this->draw_wave_(slot->pixels, phase + 174, base + 5 + energetic_amplitude * 5 / 4, 2050, 4096,
                   this->secondary_color_.red, this->secondary_color_.green, this->secondary_color_.blue,
                   static_cast<uint8_t>(112 + energy * 74 / 32767), 2, dirty_y1, dirty_y2);
  this->draw_wave_(slot->pixels, phase + 318, base + 9 + energetic_amplitude * 3 / 2, 1843, 3686,
                   this->tertiary_color_.red, this->tertiary_color_.green, this->tertiary_color_.blue,
                   static_cast<uint8_t>(72 + energy * 58 / 32767), 1, dirty_y1, dirty_y2);
  slot->dirty_y1 = dirty_y1;
  slot->dirty_y2 = dirty_y2;

  const uint8_t result = this->lvgl_component_->direct_blit_rgb888_async(
      reinterpret_cast<const uint8_t *>(slot->pixels), this->width_ * sizeof(lv_color_t), this->screen_x_,
      this->screen_y_, this->width_, this->height_, present_done_, slot);
  if (perf_enabled) {
#ifdef USE_ESP32
    const uint32_t render_us = static_cast<uint32_t>(esp_timer_get_time() - render_started_us);
    this->perf_rendered_.fetch_add(1, std::memory_order_relaxed);
    this->perf_render_total_us_.fetch_add(render_us, std::memory_order_relaxed);
    uint32_t previous_max = this->perf_render_max_us_.load(std::memory_order_relaxed);
    while (render_us > previous_max &&
           !this->perf_render_max_us_.compare_exchange_weak(previous_max, render_us, std::memory_order_relaxed)) {
    }
#endif
  }
  if (result != LVGL_DIRECT_BLIT_SUBMITTED) {
    slot->in_flight.store(false, std::memory_order_release);
    if (perf_enabled)
      this->perf_rejected_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  if (perf_enabled)
    this->perf_submitted_.fetch_add(1, std::memory_order_relaxed);
  this->region_owned_.store(true, std::memory_order_release);
  return true;
}

void MaterialVoiceAssistant::draw_wave_(lv_color_t *pixels, int phase, int amplitude, int cycles_q10,
                                        int secondary_cycles_q10, uint8_t red, uint8_t green, uint8_t blue,
                                        uint8_t opacity, int thickness, int &dirty_y1, int &dirty_y2) {
  const int horizon_q8 = this->height_ * 145;
  const int denominator = std::max(1, this->width_ - 1);
  for (int x = 0; x < this->width_; x++) {
    const int envelope = this->sin_q15_(x * PHASE_HALF / denominator);
    const int primary_phase = phase + x * cycles_q10 / denominator;
    const int secondary_phase = -phase * 7 / 11 + x * secondary_cycles_q10 / denominator;
    const int primary = this->sin_q15_(primary_phase);
    const int secondary = this->sin_q15_(secondary_phase);
    const int combined = primary + secondary * 7 / 25;
    const int displacement_q8 =
        static_cast<int>((static_cast<int64_t>(combined) * envelope * amplitude * 256) / (32767LL * 32767LL));
    const int y_q8 = horizon_q8 + displacement_q8;
    const int center_y = y_q8 >> 8;
    const int fraction = y_q8 & 0xFF;
    dirty_y1 = std::min(dirty_y1, std::max(0, center_y - thickness));
    dirty_y2 = std::max(dirty_y2, std::min(this->height_ - 1, center_y + thickness));
    for (int dy = -thickness; dy <= thickness; dy++) {
      int coverage = 255 - std::abs(dy * 256 - fraction) * 255 / std::max(256, (thickness + 1) * 256);
      coverage = std::clamp(coverage, 0, 255);
      this->blend_pixel_(pixels, x, center_y + dy, red, green, blue, static_cast<uint8_t>(opacity * coverage / 255));
    }
  }
}

void MaterialVoiceAssistant::blend_pixel_(lv_color_t *pixels, int x, int y, uint8_t red, uint8_t green, uint8_t blue,
                                          uint8_t opacity) {
  if (x < 0 || y < 0 || x >= this->width_ || y >= this->height_ || opacity == 0)
    return;
  lv_color_t &pixel = pixels[static_cast<size_t>(y) * this->width_ + x];
  const unsigned inverse = 255U - opacity;
  pixel.red = static_cast<uint8_t>((red * opacity + pixel.red * inverse + 127U) / 255U);
  pixel.green = static_cast<uint8_t>((green * opacity + pixel.green * inverse + 127U) / 255U);
  pixel.blue = static_cast<uint8_t>((blue * opacity + pixel.blue * inverse + 127U) / 255U);
}

int16_t MaterialVoiceAssistant::sin_q15_(int phase) const { return this->sine_table_[phase & PHASE_MASK]; }

void MaterialVoiceAssistant::present_done_(void *arg) {
  auto *slot = static_cast<PresentSlot *>(arg);
  if (slot == nullptr || slot->owner == nullptr)
    return;
  slot->in_flight.store(false, std::memory_order_release);
#ifdef USE_ESP32
  if (slot->owner->worker_handle_ != nullptr)
    xTaskNotifyGive(slot->owner->worker_handle_);
#endif
}

void MaterialVoiceAssistant::text_present_done_(void *arg) {
  auto *region = static_cast<TextRegion *>(arg);
  if (region == nullptr || region->owner == nullptr)
    return;
  region->in_flight.store(false, std::memory_order_release);
}

#ifdef USE_ESP32
void MaterialVoiceAssistant::worker_(void *arg) {
  auto *presenter = static_cast<MaterialVoiceAssistant *>(arg);
  if (presenter == nullptr) {
    vTaskDelete(nullptr);
    return;
  }
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (presenter->shutdown_requested_.load(std::memory_order_acquire))
      break;
    if (!presenter->active_.load(std::memory_order_acquire))
      continue;
    if (!presenter->frame_pending_.exchange(false, std::memory_order_acq_rel))
      continue;
    presenter->worker_busy_.store(true, std::memory_order_release);
    const bool submitted = presenter->render_frame_();
    presenter->worker_busy_.store(false, std::memory_order_release);
    if (!submitted && presenter->active_.load(std::memory_order_acquire))
      presenter->frame_pending_.store(true, std::memory_order_release);
  }
  presenter->worker_stopped_.store(true, std::memory_order_release);
  vTaskDelete(nullptr);
}
#endif

void MaterialVoiceAssistant::service_release_() {
  if (!this->release_pending_.load(std::memory_order_acquire) || this->worker_busy_.load(std::memory_order_acquire) ||
      this->slots_[0].in_flight.load(std::memory_order_acquire) ||
      this->slots_[1].in_flight.load(std::memory_order_acquire) ||
      this->status_region_.in_flight.load(std::memory_order_acquire) ||
      this->transcript_region_.in_flight.load(std::memory_order_acquire))
    return;
  if (this->region_owned_.load(std::memory_order_acquire) && this->lvgl_component_ != nullptr)
    this->lvgl_component_->direct_blit_rgb888_release(this->screen_x_, this->screen_y_, this->width_, this->height_);
  if (this->status_region_.owned.load(std::memory_order_acquire) && this->lvgl_component_ != nullptr)
    this->lvgl_component_->direct_blit_rgb888_release(this->status_region_.x, this->status_region_.y,
                                                      this->status_region_.width, this->status_region_.height);
  if (this->transcript_region_.owned.load(std::memory_order_acquire) && this->lvgl_component_ != nullptr)
    this->lvgl_component_->direct_blit_rgb888_release(this->transcript_region_.x, this->transcript_region_.y,
                                                      this->transcript_region_.width, this->transcript_region_.height);
  this->region_owned_.store(false, std::memory_order_release);
  this->status_region_.owned.store(false, std::memory_order_release);
  this->transcript_region_.owned.store(false, std::memory_order_release);
  this->release_pending_.store(false, std::memory_order_release);
}

void MaterialVoiceAssistant::service_transcripts_(uint32_t now) {
  if (!this->transcript_dirty_ ||
      (this->last_transcript_commit_ms_ != 0 && now - this->last_transcript_commit_ms_ < TRANSCRIPT_COMMIT_INTERVAL_MS))
    return;

  this->last_transcript_commit_ms_ = now;
  this->transcript_dirty_ = false;
  const int old_height = this->transcript_content_height_(this->previous_assistant_text_, this->user_text_,
                                                          this->assistant_text_);
  if (this->previous_assistant_text_ != this->requested_previous_assistant_text_) {
    this->previous_assistant_text_ = this->requested_previous_assistant_text_;
    this->transcript_region_.pending.store(true, std::memory_order_release);
  }
  this->set_label_text_(this->user_label_, this->user_text_, this->requested_user_text_, this->animate_user_on_commit_);
  this->set_label_text_(this->assistant_label_, this->assistant_text_, this->requested_assistant_text_,
                        this->animate_assistant_on_commit_);
  if (this->animate_content_on_commit_) {
    const int new_height = this->transcript_content_height_(this->previous_assistant_text_, this->user_text_,
                                                            this->assistant_text_);
    const int old_y = old_height > this->transcript_panel_height_
                          ? this->transcript_panel_height_ - old_height
                          : (this->transcript_panel_height_ - old_height) / 2;
    const int new_y = new_height > this->transcript_panel_height_
                          ? this->transcript_panel_height_ - new_height
                          : (this->transcript_panel_height_ - new_height) / 2;
    this->animate_content_shift_(std::clamp(old_y - new_y, 0, 72));
  }
  this->animate_user_on_commit_ = false;
  this->animate_assistant_on_commit_ = false;
  this->animate_content_on_commit_ = false;
}

int MaterialVoiceAssistant::transcript_content_height_(const std::string &previous_assistant,
                                                        const std::string &user,
                                                        const std::string &assistant) const {
  LineSpan lines[MAX_TEXT_LINES]{};
  int total_height = 0;
  int block_count = 0;
  for (const auto &block : {std::pair<const std::string *, const TextStyle *>(&previous_assistant,
                                                                              &this->assistant_style_),
                            std::pair<const std::string *, const TextStyle *>(&user, &this->user_style_),
                            std::pair<const std::string *, const TextStyle *>(&assistant,
                                                                              &this->assistant_style_)}) {
    int block_height = 0;
    if (this->layout_lines_(*block.first, *block.second, this->transcript_panel_width_, lines, MAX_TEXT_LINES,
                            block_height) == 0)
      continue;
    if (block_count++ > 0)
      total_height += 22;
    total_height += block_height;
  }
  return total_height;
}

void MaterialVoiceAssistant::fill_text_region_(TextRegion &region) {
  if (region.pixels == nullptr || region.width <= 0 || region.height <= 0)
    return;
  std::fill_n(region.pixels, static_cast<size_t>(region.width) * region.height, this->text_background_color_);
}

int MaterialVoiceAssistant::layout_lines_(const std::string &text, const TextStyle &style, int max_width,
                                          LineSpan *lines, int max_lines, int &height) const {
  height = 0;
  if (text.empty() || style.font == nullptr || lines == nullptr || max_lines <= 0 || max_width <= 0)
    return 0;

  uint32_t offset = 0;
  int line_count = 0;
  const uint32_t text_length = static_cast<uint32_t>(text.size());
  while (offset < text_length && line_count < max_lines) {
    while (offset < text_length && (text[offset] == ' ' || text[offset] == '\t'))
      offset++;
    if (offset >= text_length)
      break;
    const uint32_t line_start = offset;
    uint32_t cursor = offset;
    uint32_t drawable_end = offset;
    uint32_t next_offset = offset;
    uint32_t last_break_draw_end = UINT32_MAX;
    uint32_t last_break_next = UINT32_MAX;
    int32_t line_width = 0;
    int32_t width_at_break = 0;
    bool complete = false;

    while (cursor < text_length) {
      const uint32_t letter_start = cursor;
      const int32_t width_before_letter = line_width;
      const uint32_t letter = utf8_next_(text.c_str(), text_length, cursor);
      if (letter == '\n' || letter == '\r') {
        drawable_end = letter_start;
        next_offset = cursor;
        complete = true;
        break;
      }
      uint32_t lookahead = cursor;
      const uint32_t next = lookahead < text_length ? utf8_next_(text.c_str(), text_length, lookahead) : 0;
      lv_font_glyph_dsc_t glyph{};
      const int32_t advance = lv_font_get_glyph_dsc(style.font, &glyph, letter, next) ? glyph.adv_w : 0;
      const int32_t proposed_width = line_width + (letter_start > line_start ? style.letter_space : 0) + advance;

      if (proposed_width > max_width && letter_start > line_start) {
        if (last_break_next != UINT32_MAX) {
          drawable_end = last_break_draw_end;
          next_offset = last_break_next;
          line_width = width_at_break;
        } else {
          drawable_end = letter_start;
          next_offset = letter_start;
        }
        complete = true;
        break;
      }

      line_width = proposed_width;
      drawable_end = cursor;
      next_offset = cursor;
      if (letter == ' ' || letter == '\t') {
        last_break_draw_end = letter_start;
        last_break_next = cursor;
        width_at_break = width_before_letter;
      } else if (letter == '-') {
        last_break_draw_end = cursor;
        last_break_next = cursor;
        width_at_break = line_width;
      }
    }
    if (!complete)
      next_offset = cursor;
    if (drawable_end == line_start && next_offset == line_start) {
      uint32_t forced = line_start;
      utf8_next_(text.c_str(), text_length, forced);
      drawable_end = forced;
      next_offset = forced;
    }

    lines[line_count].offset = offset;
    lines[line_count].length = drawable_end - line_start;
    lines[line_count].width = line_width;
    line_count++;
    offset = next_offset;
  }
  if (line_count > 0)
    height = line_count * static_cast<int>(style.font->line_height) + (line_count - 1) * style.line_space;
  return line_count;
}

void MaterialVoiceAssistant::draw_text_line_(lv_color_t *pixels, int buffer_width, int buffer_height, const char *text,
                                             uint32_t length, const TextStyle &style, int x, int y, int width,
                                             int text_width, uint8_t opacity) {
  if (pixels == nullptr || text == nullptr || length == 0 || style.font == nullptr || this->glyph_buffer_ == nullptr ||
      opacity == 0)
    return;

  int pen_x = x + (width - text_width) / 2;
  uint32_t index = 0;
  while (index < length) {
    const uint32_t letter_start = index;
    const uint32_t letter = utf8_next_(text, length, index);
    if (index <= letter_start || letter == 0)
      break;
    if (letter == '\n' || letter == '\r')
      continue;
    uint32_t next_index = index;
    const uint32_t next = next_index < length ? utf8_next_(text, length, next_index) : 0;
    lv_font_glyph_dsc_t glyph{};
    if (!lv_font_get_glyph_dsc(style.font, &glyph, letter, next))
      continue;

    const auto *draw_buf = static_cast<const lv_draw_buf_t *>(lv_font_get_glyph_bitmap(&glyph, this->glyph_buffer_));
    if (draw_buf != nullptr && glyph.box_w > 0 && glyph.box_h > 0) {
      const int glyph_x = pen_x + glyph.ofs_x;
      const int glyph_y = y + (style.font->line_height - style.font->base_line) - glyph.box_h - glyph.ofs_y;
      const int glyph_stride = lv_draw_buf_width_to_stride(glyph.box_w, LV_COLOR_FORMAT_A8);
      for (int gy = 0; gy < glyph.box_h; gy++) {
        const int dest_y = glyph_y + gy;
        if (dest_y < 0 || dest_y >= buffer_height)
          continue;
        const uint8_t *alpha_row = draw_buf->data + static_cast<size_t>(gy) * glyph_stride;
        lv_color_t *dest_row = pixels + static_cast<size_t>(dest_y) * buffer_width;
        for (int gx = 0; gx < glyph.box_w; gx++) {
          const int dest_x = glyph_x + gx;
          if (dest_x < 0 || dest_x >= buffer_width)
            continue;
          const uint8_t alpha = static_cast<uint8_t>((static_cast<uint16_t>(alpha_row[gx]) * opacity + 127U) / 255U);
          if (alpha == 0)
            continue;
          lv_color_t &pixel = dest_row[dest_x];
          const unsigned inverse = 255U - alpha;
          pixel.red = static_cast<uint8_t>((style.color.red * alpha + pixel.red * inverse + 127U) / 255U);
          pixel.green = static_cast<uint8_t>((style.color.green * alpha + pixel.green * inverse + 127U) / 255U);
          pixel.blue = static_cast<uint8_t>((style.color.blue * alpha + pixel.blue * inverse + 127U) / 255U);
        }
      }
    }
    pen_x += glyph.adv_w + style.letter_space;
    lv_font_glyph_release_draw_data(&glyph);
  }
}

void MaterialVoiceAssistant::draw_text_block_(lv_color_t *pixels, int buffer_width, int buffer_height,
                                              const std::string &text, const TextStyle &style, int x, int y, int width,
                                              int height, uint8_t opacity, int translate_y) {
  LineSpan lines[MAX_TEXT_LINES]{};
  int text_height = 0;
  const int line_count = this->layout_lines_(text, style, width, lines, MAX_TEXT_LINES, text_height);
  if (line_count == 0)
    return;
  int line_y = y + (height - text_height) / 2 + translate_y;
  for (int line = 0; line < line_count; line++) {
    this->draw_text_line_(pixels, buffer_width, buffer_height, text.c_str() + lines[line].offset, lines[line].length,
                          style, x, line_y, width, lines[line].width, opacity);
    line_y += static_cast<int>(style.font->line_height) + style.line_space;
  }
}

bool MaterialVoiceAssistant::render_status_region_() {
  if (this->status_region_.pixels == nullptr)
    return false;
  this->fill_text_region_(this->status_region_);
  this->draw_text_block_(this->status_region_.pixels, this->status_region_.width, this->status_region_.height,
                         this->phase_text_, this->status_style_, 0, 0, this->status_region_.width,
                         this->status_region_.height, 255, 0);
  return true;
}

bool MaterialVoiceAssistant::render_transcript_region_() {
  if (this->transcript_region_.pixels == nullptr)
    return false;
  this->fill_text_region_(this->transcript_region_);

  LineSpan previous_assistant_lines[MAX_TEXT_LINES]{};
  LineSpan user_lines[MAX_TEXT_LINES]{};
  LineSpan assistant_lines[MAX_TEXT_LINES]{};
  int previous_assistant_height = 0;
  int user_height = 0;
  int assistant_height = 0;
  const int previous_assistant_count =
      this->layout_lines_(this->previous_assistant_text_, this->assistant_style_, this->transcript_panel_width_,
                          previous_assistant_lines, MAX_TEXT_LINES, previous_assistant_height);
  const int user_count = this->layout_lines_(this->user_text_, this->user_style_, this->transcript_panel_width_,
                                             user_lines, MAX_TEXT_LINES, user_height);
  const int assistant_count =
      this->layout_lines_(this->assistant_text_, this->assistant_style_, this->transcript_panel_width_, assistant_lines,
                          MAX_TEXT_LINES, assistant_height);
  const int block_count = (previous_assistant_count > 0 ? 1 : 0) + (user_count > 0 ? 1 : 0) +
                          (assistant_count > 0 ? 1 : 0);
  const int total_height = previous_assistant_height + user_height + assistant_height + std::max(0, block_count - 1) * 22;
  int content_y = this->transcript_panel_y_;
  if (total_height <= this->transcript_panel_height_)
    content_y += (this->transcript_panel_height_ - total_height) / 2;
  else
    content_y += this->transcript_panel_height_ - total_height;
  content_y += this->content_animation_.translate_y;

  const LabelAnimation &user_animation = this->label_animations_[0];
  const LabelAnimation &assistant_animation = this->label_animations_[1];
  if (previous_assistant_count > 0) {
    this->draw_text_block_(this->transcript_region_.pixels, this->transcript_region_.width,
                           this->transcript_region_.height, this->previous_assistant_text_, this->assistant_style_,
                           this->transcript_panel_x_, content_y, this->transcript_panel_width_,
                           previous_assistant_height, 255, 0);
    content_y += previous_assistant_height + (block_count > 1 ? 22 : 0);
  }
  if (user_count > 0) {
    this->draw_text_block_(this->transcript_region_.pixels, this->transcript_region_.width,
                           this->transcript_region_.height, this->user_text_, this->user_style_,
                           this->transcript_panel_x_, content_y, this->transcript_panel_width_, user_height,
                           user_animation.opacity, user_animation.translate_y);
    content_y += user_height + (assistant_count > 0 ? 22 : 0);
  }
  if (assistant_count > 0) {
    this->draw_text_block_(this->transcript_region_.pixels, this->transcript_region_.width,
                           this->transcript_region_.height, this->assistant_text_, this->assistant_style_,
                           this->transcript_panel_x_, content_y, this->transcript_panel_width_, assistant_height,
                           assistant_animation.opacity, assistant_animation.translate_y);
  }
  return true;
}

bool MaterialVoiceAssistant::submit_text_region_(TextRegion &region) {
  if (!region.pending.load(std::memory_order_acquire) || region.in_flight.load(std::memory_order_acquire) ||
      region.pixels == nullptr)
    return false;
  bool expected = false;
  if (!region.in_flight.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
    return false;

  region.pending.store(false, std::memory_order_release);
  const bool rendered =
      &region == &this->status_region_ ? this->render_status_region_() : this->render_transcript_region_();
  if (!rendered) {
    region.in_flight.store(false, std::memory_order_release);
    return false;
  }
  const uint8_t result = this->lvgl_component_->direct_blit_rgb888_async(
      reinterpret_cast<const uint8_t *>(region.pixels), region.width * static_cast<int>(sizeof(lv_color_t)), region.x,
      region.y, region.width, region.height, text_present_done_, &region);
  if (result != LVGL_DIRECT_BLIT_SUBMITTED) {
    region.in_flight.store(false, std::memory_order_release);
    region.pending.store(true, std::memory_order_release);
    this->perf_text_submit_busy_++;
    return false;
  }
  region.owned.store(true, std::memory_order_release);
  return true;
}

void MaterialVoiceAssistant::service_text_regions_() {
  if (!this->active_.load(std::memory_order_acquire) || this->glyph_buffer_ == nullptr)
    return;
#ifdef USE_ESP32
  const int64_t started_us = esp_timer_get_time();
#endif
  bool rendered = this->submit_text_region_(this->status_region_);
  rendered = this->submit_text_region_(this->transcript_region_) || rendered;
  if (!rendered)
    return;
#ifdef USE_ESP32
  const uint32_t elapsed_us = static_cast<uint32_t>(esp_timer_get_time() - started_us);
  this->perf_text_rendered_++;
  this->perf_text_render_total_us_ += elapsed_us;
  this->perf_text_render_max_us_ = std::max(this->perf_text_render_max_us_, elapsed_us);
#endif
}

void MaterialVoiceAssistant::log_performance_(uint32_t now) {
  const bool enabled = lvgl_esphome_get_perf_logging_enabled() != 0;
  if (!enabled) {
    this->perf_enabled_ = false;
    this->perf_window_start_ms_ = 0;
    this->perf_text_rendered_ = 0;
    this->perf_text_render_total_us_ = 0;
    this->perf_text_render_max_us_ = 0;
    this->perf_text_submit_busy_ = 0;
    return;
  }
  if (!this->perf_enabled_) {
    this->perf_enabled_ = true;
    this->perf_window_start_ms_ = now;
    this->perf_requested_.store(0, std::memory_order_relaxed);
    this->perf_rendered_.store(0, std::memory_order_relaxed);
    this->perf_render_total_us_.store(0, std::memory_order_relaxed);
    this->perf_render_max_us_.store(0, std::memory_order_relaxed);
    this->perf_submitted_.store(0, std::memory_order_relaxed);
    this->perf_no_slot_.store(0, std::memory_order_relaxed);
    this->perf_rejected_.store(0, std::memory_order_relaxed);
    this->perf_text_rendered_ = 0;
    this->perf_text_render_total_us_ = 0;
    this->perf_text_render_max_us_ = 0;
    this->perf_text_submit_busy_ = 0;
    return;
  }
  if (now - this->perf_window_start_ms_ < 2000)
    return;

  const uint32_t window_ms = now - this->perf_window_start_ms_;
  const uint32_t requested = this->perf_requested_.exchange(0, std::memory_order_relaxed);
  const uint32_t rendered = this->perf_rendered_.exchange(0, std::memory_order_relaxed);
  const uint32_t total_us = this->perf_render_total_us_.exchange(0, std::memory_order_relaxed);
  const uint32_t max_us = this->perf_render_max_us_.exchange(0, std::memory_order_relaxed);
  const uint32_t submitted = this->perf_submitted_.exchange(0, std::memory_order_relaxed);
  const uint32_t no_slot = this->perf_no_slot_.exchange(0, std::memory_order_relaxed);
  const uint32_t rejected = this->perf_rejected_.exchange(0, std::memory_order_relaxed);
  const uint32_t average_us = rendered == 0 ? 0 : total_us / rendered;
  const uint32_t rendered_fps_x10 = window_ms == 0 ? 0 : rendered * 10000U / window_ms;
  const uint32_t text_count = this->perf_text_rendered_;
  const uint32_t text_average_us = text_count == 0 ? 0 : this->perf_text_render_total_us_ / text_count;
  ESP_LOGI(TAG,
           "perf: window=%ums request=%u render=%u fps=%u.%u avg=%uus max=%uus submit=%u no_slot=%u reject=%u "
           "text=%u/%uus/%uus busy=%u",
           static_cast<unsigned>(window_ms), static_cast<unsigned>(requested), static_cast<unsigned>(rendered),
           static_cast<unsigned>(rendered_fps_x10 / 10U), static_cast<unsigned>(rendered_fps_x10 % 10U),
           static_cast<unsigned>(average_us), static_cast<unsigned>(max_us), static_cast<unsigned>(submitted),
           static_cast<unsigned>(no_slot), static_cast<unsigned>(rejected), static_cast<unsigned>(text_count),
           static_cast<unsigned>(text_average_us), static_cast<unsigned>(this->perf_text_render_max_us_),
           static_cast<unsigned>(this->perf_text_submit_busy_));
  this->perf_window_start_ms_ = now;
  this->perf_text_rendered_ = 0;
  this->perf_text_render_total_us_ = 0;
  this->perf_text_render_max_us_ = 0;
  this->perf_text_submit_busy_ = 0;
}

void MaterialVoiceAssistant::service_label_animations_(uint32_t now) {
  for (auto &animation : this->label_animations_) {
    if (!animation.active || animation.label == nullptr || now - animation.last_step_ms < LABEL_ANIMATION_STEP_MS)
      continue;
    animation.last_step_ms = now;
    const uint32_t elapsed = std::min<uint32_t>(now - animation.started_ms, LABEL_ANIMATION_DURATION_MS);
    const uint32_t progress = elapsed * 255U / LABEL_ANIMATION_DURATION_MS;
    const uint32_t inverse = 255U - progress;
    const uint32_t inverse_cubic = inverse * inverse * inverse;
    const uint8_t opacity = static_cast<uint8_t>(255U - inverse_cubic / (255U * 255U));
    const int32_t translate = static_cast<int32_t>((10U * inverse_cubic) / (255U * 255U * 255U));
    animation.opacity = opacity;
    animation.translate_y = static_cast<int8_t>(translate);
    this->transcript_region_.pending.store(true, std::memory_order_release);
    if (elapsed == LABEL_ANIMATION_DURATION_MS) {
      animation.opacity = 255;
      animation.translate_y = 0;
      animation.active = false;
    }
  }
}

void MaterialVoiceAssistant::service_content_animation_(uint32_t now) {
  if (!this->content_animation_.active ||
      now - this->content_animation_.last_step_ms < LABEL_ANIMATION_STEP_MS)
    return;
  this->content_animation_.last_step_ms = now;
  const uint32_t elapsed = std::min<uint32_t>(now - this->content_animation_.started_ms,
                                               LABEL_ANIMATION_DURATION_MS);
  const uint32_t progress = elapsed * 255U / LABEL_ANIMATION_DURATION_MS;
  const uint32_t inverse = 255U - progress;
  const uint32_t inverse_cubic = inverse * inverse * inverse;
  this->content_animation_.translate_y = static_cast<int16_t>(
      (static_cast<int32_t>(this->content_animation_.start_translate_y) * inverse_cubic) /
      (255U * 255U * 255U));
  this->transcript_region_.pending.store(true, std::memory_order_release);
  if (elapsed == LABEL_ANIMATION_DURATION_MS) {
    this->content_animation_.translate_y = 0;
    this->content_animation_.active = false;
  }
}

void MaterialVoiceAssistant::animate_label_in_(lv_obj_t *label) {
  if (label == nullptr)
    return;
  for (auto &animation : this->label_animations_) {
    if (animation.label != label)
      continue;
    animation.started_ms = millis();
    animation.last_step_ms = animation.started_ms - LABEL_ANIMATION_STEP_MS;
    animation.opacity = 0;
    animation.translate_y = 10;
    animation.active = true;
    this->transcript_region_.pending.store(true, std::memory_order_release);
    break;
  }
}

void MaterialVoiceAssistant::animate_content_shift_(int translate_y) {
  if (translate_y <= 0) {
    this->content_animation_.active = false;
    this->content_animation_.translate_y = 0;
    return;
  }
  this->content_animation_.started_ms = millis();
  this->content_animation_.last_step_ms = this->content_animation_.started_ms - LABEL_ANIMATION_STEP_MS;
  this->content_animation_.start_translate_y = static_cast<int16_t>(translate_y);
  this->content_animation_.translate_y = static_cast<int16_t>(translate_y);
  this->content_animation_.active = true;
  this->transcript_region_.pending.store(true, std::memory_order_release);
}

void MaterialVoiceAssistant::set_label_text_(lv_obj_t *label, std::string &current, const std::string &text,
                                             bool animate) {
  if (label == nullptr || current == text)
    return;
  current = text;
  this->transcript_region_.pending.store(true, std::memory_order_release);
  if (animate && !text.empty()) {
    this->animate_label_in_(label);
  } else if (text.empty()) {
    for (auto &animation : this->label_animations_) {
      if (animation.label != label)
        continue;
      animation.active = false;
      animation.opacity = 255;
      animation.translate_y = 0;
      break;
    }
  }
}

bool MaterialVoiceAssistant::is_continuation_(const std::string &previous, const std::string &next) {
  if (previous.empty() || next.empty())
    return false;
  const size_t shared = std::min(previous.size(), next.size());
  return previous.compare(0, shared, next, 0, shared) == 0;
}

}  // namespace esphome::lvgl_material
