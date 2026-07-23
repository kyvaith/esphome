#include "esphome/core/defines.h"
#include "esphome/core/log.h"
#include "lvgl_diagnostics.h"

#ifdef USE_LVGL_DIAGNOSTICS

#include <algorithm>

namespace esphome::lvgl {

static const char *const TAG = "lvgl.diagnostics";
static constexpr uint32_t SAMPLE_PERIOD_US = 1000000;
static LvglDiagnostics *default_diagnostics = nullptr;

void LvglDiagnostics::attach(lv_display_t *display, bool direct_mode) {
  if (this->attached_ || display == nullptr)
    return;

  this->attached_ = true;
  this->direct_mode_ = direct_mode;
  lv_display_add_event_cb(display, refresh_ready_callback_, LV_EVENT_REFR_READY, this);
  lv_display_add_event_cb(display, invalidate_callback_, LV_EVENT_INVALIDATE_AREA, this);
  if (default_diagnostics == nullptr)
    default_diagnostics = this;
}

void LvglDiagnostics::update(uint32_t now_us) {
  if (!this->attached_)
    return;
  if (this->window_started_us_ == 0) {
    this->window_started_us_ = now_us;
    return;
  }

  const uint32_t elapsed_us = now_us - this->window_started_us_;
  if (elapsed_us < SAMPLE_PERIOD_US)
    return;
  this->publish_window_(elapsed_us);
  this->window_started_us_ = now_us;
}

void LvglDiagnostics::note_handler(uint32_t duration_us) {
  this->handler_total_us_ += duration_us;
  this->handler_max_us_ = std::max(this->handler_max_us_, duration_us);
}

void LvglDiagnostics::note_flush(uint32_t duration_us) {
  this->flush_total_us_ += duration_us;
  this->flush_max_us_ = std::max(this->flush_max_us_, duration_us);
}

void LvglDiagnostics::note_presented_frame() { this->frame_count_++; }

void LvglDiagnostics::refresh_ready_callback_(lv_event_t *event) {
  auto *diagnostics = static_cast<LvglDiagnostics *>(lv_event_get_user_data(event));
  diagnostics->note_presented_frame();
}

void LvglDiagnostics::invalidate_callback_(lv_event_t *event) {
  auto *diagnostics = static_cast<LvglDiagnostics *>(lv_event_get_user_data(event));
  const auto *area = static_cast<const lv_area_t *>(lv_event_get_param(event));
  if (area == nullptr)
    return;
  diagnostics->invalidated_pixels_ += static_cast<uint64_t>(lv_area_get_width(area)) * lv_area_get_height(area);
}

void LvglDiagnostics::publish_window_(uint32_t elapsed_us) {
  this->fps_ = static_cast<uint32_t>((static_cast<uint64_t>(this->frame_count_) * 1000000ULL) / elapsed_us);
  this->cpu_percent_ = std::min<uint32_t>(100, static_cast<uint32_t>((this->handler_total_us_ * 100ULL) / elapsed_us));
  this->flush_ms_ = static_cast<uint32_t>(this->flush_total_us_ / 1000ULL);
  this->flush_max_ms_ = this->flush_max_us_ / 1000U;
  this->handler_max_ms_ = this->handler_max_us_ / 1000U;
  this->invalidated_kpx_ = static_cast<uint32_t>(this->invalidated_pixels_ / 1000ULL);

  if (this->logging_enabled_) {
    ESP_LOGI(TAG, "fps=%u cpu=%u%% flush=%ums max_flush=%ums max_handler=%ums invalidated=%ukpx direct=%s",
             static_cast<unsigned>(this->fps_), static_cast<unsigned>(this->cpu_percent_),
             static_cast<unsigned>(this->flush_ms_), static_cast<unsigned>(this->flush_max_ms_),
             static_cast<unsigned>(this->handler_max_ms_), static_cast<unsigned>(this->invalidated_kpx_),
             YESNO(this->direct_mode_));
  }

  this->frame_count_ = 0;
  this->handler_total_us_ = 0;
  this->flush_total_us_ = 0;
  this->handler_max_us_ = 0;
  this->flush_max_us_ = 0;
  this->invalidated_pixels_ = 0;
}

LvglDiagnostics *get_default_diagnostics() { return default_diagnostics; }

}  // namespace esphome::lvgl

static esphome::lvgl::LvglDiagnostics *get_diagnostics() { return esphome::lvgl::get_default_diagnostics(); }

extern "C" uint32_t lvgl_esphome_get_fps() {
  auto *diagnostics = get_diagnostics();
  return diagnostics == nullptr ? 0 : diagnostics->get_fps();
}

extern "C" uint32_t lvgl_esphome_get_cpu_pct() {
  auto *diagnostics = get_diagnostics();
  return diagnostics == nullptr ? 0 : diagnostics->get_cpu_percent();
}

extern "C" uint32_t lvgl_esphome_get_flush_ms() {
  auto *diagnostics = get_diagnostics();
  return diagnostics == nullptr ? 0 : diagnostics->get_flush_ms();
}

extern "C" uint32_t lvgl_esphome_get_flush_max_ms() {
  auto *diagnostics = get_diagnostics();
  return diagnostics == nullptr ? 0 : diagnostics->get_flush_max_ms();
}

extern "C" uint32_t lvgl_esphome_get_loop_max_ms() {
  auto *diagnostics = get_diagnostics();
  return diagnostics == nullptr ? 0 : diagnostics->get_handler_max_ms();
}

extern "C" uint32_t lvgl_esphome_get_invalidated_kpx() {
  auto *diagnostics = get_diagnostics();
  return diagnostics == nullptr ? 0 : diagnostics->get_invalidated_kpx();
}

extern "C" uint32_t lvgl_esphome_get_direct_mode_active() {
  auto *diagnostics = get_diagnostics();
  return diagnostics != nullptr && diagnostics->is_direct_mode();
}

extern "C" uint32_t lvgl_esphome_get_perf_logging_enabled() {
  auto *diagnostics = get_diagnostics();
  return diagnostics != nullptr && diagnostics->is_logging_enabled();
}

extern "C" void lvgl_esphome_set_perf_logging_enabled(bool enabled) {
  auto *diagnostics = get_diagnostics();
  if (diagnostics != nullptr)
    diagnostics->set_logging_enabled(enabled);
}

#endif
