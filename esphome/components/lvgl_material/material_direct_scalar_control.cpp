#include "material_direct_scalar_control.h"
#include "material_scalar_timing.h"
#ifdef USE_LVGL_MATERIAL_DIRECT_SCALAR_CONTROL

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "misc/cache/instance/lv_image_cache.h"
#include <algorithm>
#include <cstring>
#include <limits>
#ifdef USE_LVGL_MATERIAL_SCALAR_PERFORMANCE_METRICS
#include <cstdio>
#endif

#ifdef USE_ESP32
#include "esphome/components/esp32/task_utils.h"
#include "esp_heap_caps.h"
#endif

namespace esphome::lvgl_material {

static const char *const TAG = "lvgl_material.scalar";
static constexpr char CHARACTERS[] = "0123456789%-";

static ScalarColor color(lv_color_t value) { return {value.red, value.green, value.blue}; }

void MaterialDirectScalarControl::set_widgets(lv_obj_t *region, lv_obj_t *track, lv_obj_t *bubble, lv_obj_t *value,
                                              lv_obj_t *minimum, lv_obj_t *maximum) {
  this->region_ = region;
  this->track_ = track;
  this->bubble_ = bubble;
  this->value_ = value;
  this->minimum_ = minimum;
  this->maximum_ = maximum;
}

void MaterialDirectScalarControl::setup() {
  if (!this->initial_enabled_)
    return;
  if (this->lvgl_ == nullptr || this->control_ == nullptr || this->navigation_ == nullptr ||
      this->application_ == nullptr || this->region_ == nullptr || this->track_ == nullptr ||
      this->bubble_ == nullptr || this->value_ == nullptr || this->minimum_ == nullptr || this->maximum_ == nullptr ||
      !this->active_color_provider_) {
    this->mark_failed();
    return;
  }
  if (!this->configure_control_view_()) {
    this->mark_failed();
    return;
  }
  if (!this->ensure_resources_() || !this->cache_style_()) {
    ESP_LOGE(TAG, "Unable to admit bounded regional resources/styles; brightness remains disabled");
    this->mark_failed();
    return;
  }
  this->backing_image_ = lv_image_create(this->region_);
  if (this->backing_image_ == nullptr ||
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
      (!this->control_->uses_generic_view() && !this->navigation_->add_admission_guard(this))) {
#else
      !this->navigation_->add_admission_guard(this)) {
#endif
    this->mark_failed();
    return;
  }
  lv_obj_set_pos(this->backing_image_, 0, 0);
  lv_obj_set_size(this->backing_image_, this->style_.region.width, this->style_.region.height);
  lv_obj_remove_flag(this->backing_image_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(this->backing_image_, LV_OBJ_FLAG_HIDDEN);
  this->backing_descriptor_.header.magic = LV_IMAGE_HEADER_MAGIC;
  this->backing_descriptor_.header.cf = LV_COLOR_FORMAT_RGB888;
  this->backing_descriptor_.header.w = this->style_.region.width;
  this->backing_descriptor_.header.h = this->style_.region.height;
  this->backing_descriptor_.header.stride = this->style_.region.width * 3;
  this->backing_descriptor_.data_size = this->style_.region.width * this->style_.region.height * 3;
  this->backing_descriptor_.data = this->canvas_;
  this->control_->add_on_render_request_callback(
      [this](uint32_t request, bool known, uint8_t value) { this->request_(request, known, value); });
  this->control_->add_on_render_cancel_callback([this]() { this->cancel_(); });
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  if (!this->control_->uses_generic_view())
#endif
    this->control_->view_lifecycle().attach_application_callbacks();
  this->setup_ready_ = true;
}

bool MaterialDirectScalarControl::ensure_resources_() {
#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  lv_obj_update_layout(this->region_);
  const int width = lv_obj_get_width(this->region_), height = lv_obj_get_height(this->region_);
  if (width <= 0 || height <= 0 || width > this->lvgl_->get_width() || height > this->lvgl_->get_height() ||
      (width == this->lvgl_->get_width() && height == this->lvgl_->get_height()) ||
      lv_display_get_color_format(lv_obj_get_display(this->region_)) != LV_COLOR_FORMAT_RGB888 ||
      static_cast<size_t>(width) * height > MAX_CANVAS_BYTES / 3 ||
      lv_draw_buf_width_to_stride(width, LV_COLOR_FORMAT_RGB888) != static_cast<uint32_t>(width) * 3)
    return false;
  const size_t needed = (static_cast<size_t>(width) * height * 3 + 127) & ~size_t{127};
  // The same free canvas also serves owner-task glyph decoding before start.
  if (needed < 8192)
    return false;
  this->canvas_ = static_cast<uint8_t *>(heap_caps_aligned_alloc(128, needed, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  this->glyph_masks_ = static_cast<uint8_t *>(heap_caps_malloc(GLYPH_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  this->canvas_capacity_ = this->canvas_ == nullptr ? 0 : needed;
  return this->canvas_ != nullptr && this->glyph_masks_ != nullptr && this->ensure_worker_();
#else
  return false;  // No native slider or full-screen fallback.
#endif
}

bool MaterialDirectScalarControl::cache_font_(const lv_font_t *font, int letter_space, ScalarFont &target) {
  if (font == nullptr || font->line_height > 64 || letter_space < -16 || letter_space > 64)
    return false;
  lv_draw_buf_t scratch{};
  target.line_height = font->line_height;
  target.letter_space = letter_space;
  for (size_t i = 0; i < ScalarFont::CHARACTER_COUNT; i++) {
    lv_font_glyph_dsc_t description{};
    if (!lv_font_get_glyph_dsc(font, &description, CHARACTERS[i], 0))
      return false;
    if (description.box_w > 64 || description.box_h > 64) {
      lv_font_glyph_release_draw_data(&description);
      return false;
    }
    const size_t bytes = static_cast<size_t>(description.box_w) * description.box_h;
    if (bytes > GLYPH_BYTES - this->mask_bytes_) {
      lv_font_glyph_release_draw_data(&description);
      return false;
    }
    auto &glyph = target.glyphs[i];
    glyph = {static_cast<uint32_t>(this->mask_bytes_), description.box_w, description.box_h, description.ofs_x,
             static_cast<int16_t>(font->line_height - font->base_line - description.box_h - description.ofs_y)};
    bool valid = true;
    if (bytes != 0) {
      // Font providers write rows for this glyph, not the maximum scratch geometry.
      // Match LVGL's label renderer: describe the actual A8 target before decoding.
      valid = (description.format == LV_FONT_GLYPH_FORMAT_A1 || description.format == LV_FONT_GLYPH_FORMAT_A2 ||
               description.format == LV_FONT_GLYPH_FORMAT_A3 || description.format == LV_FONT_GLYPH_FORMAT_A4 ||
               description.format == LV_FONT_GLYPH_FORMAT_A8) &&
              lv_draw_buf_init(&scratch, description.box_w, description.box_h, LV_COLOR_FORMAT_A8, LV_STRIDE_AUTO,
                               this->canvas_, this->canvas_capacity_) == LV_RESULT_OK;
      const auto *bitmap =
          valid ? static_cast<const lv_draw_buf_t *>(lv_font_get_glyph_bitmap(&description, &scratch)) : nullptr;
      valid = bitmap != nullptr && bitmap->data != nullptr && bitmap->header.cf == LV_COLOR_FORMAT_A8 &&
              bitmap->header.w >= description.box_w && bitmap->header.h >= description.box_h;
      if (valid) {
        const size_t stride = bitmap->header.stride;
        valid = stride >= description.box_w && bitmap->data_size >= description.box_w &&
                (description.box_h <= 1 || stride <= (bitmap->data_size - description.box_w) / (description.box_h - 1));
        for (size_t y = 0; valid && y < description.box_h; y++)
          std::memcpy(this->glyph_masks_ + this->mask_bytes_ + y * description.box_w, bitmap->data + y * stride,
                      description.box_w);
      }
    }
    lv_font_glyph_release_draw_data(&description);
    if (!valid)
      return false;
    this->mask_bytes_ += bytes;
    for (size_t next = 0; next <= ScalarFont::CHARACTER_COUNT; next++) {
      lv_font_glyph_dsc_t pair{};
      if (!lv_font_get_glyph_dsc(font, &pair, CHARACTERS[i],
                                 next == ScalarFont::CHARACTER_COUNT ? 0 : CHARACTERS[next]))
        return false;
      const bool bounded = pair.adv_w <= 256;
      target.advances[i][next] = pair.adv_w;
      lv_font_glyph_release_draw_data(&pair);
      if (!bounded)
        return false;
    }
  }
  return true;
}

bool MaterialDirectScalarControl::cache_style_() {
  if (this->active_.load(std::memory_order_seq_cst) || this->worker_busy_.load(std::memory_order_seq_cst) ||
      this->source_in_flight_.load(std::memory_order_acquire) || this->alias_visible_ || this->canvas_ == nullptr ||
      this->glyph_masks_ == nullptr)
    return false;
  lv_obj_update_layout(this->region_);
  lv_area_t region{};
  lv_obj_get_coords(this->region_, &region);
  if (region.x1 < 0 || region.y1 < 0 || region.x2 >= this->lvgl_->get_width() ||
      region.y2 >= this->lvgl_->get_height() || region.x2 >= std::numeric_limits<int16_t>::max() ||
      region.y2 >= std::numeric_limits<int16_t>::max())
    return false;
  this->style_ = {};
  this->style_.region = {static_cast<int16_t>(region.x1), static_cast<int16_t>(region.y1),
                         static_cast<int16_t>(lv_area_get_width(&region)),
                         static_cast<int16_t>(lv_area_get_height(&region))};
  if (this->setup_ready_ && (static_cast<uint32_t>(this->style_.region.width) != this->backing_descriptor_.header.w ||
                             static_cast<uint32_t>(this->style_.region.height) != this->backing_descriptor_.header.h))
    return false;  // The admitted pool is fixed geometry, not a resize API.
  auto rect = [&](lv_obj_t *object) {
    lv_area_t box{};
    lv_obj_get_coords(object, &box);
    return ScalarRect{static_cast<int16_t>(box.x1 - region.x1), static_cast<int16_t>(box.y1 - region.y1),
                      static_cast<int16_t>(lv_area_get_width(&box)), static_cast<int16_t>(lv_area_get_height(&box))};
  };
  this->style_.track = rect(this->track_);
  this->style_.bubble = rect(this->bubble_);
  this->style_.minimum = rect(this->minimum_);
  this->style_.maximum = rect(this->maximum_);
  this->style_.background = color(lv_obj_get_style_bg_color(this->region_, LV_PART_MAIN));
  this->style_.inactive = color(lv_obj_get_style_bg_color(this->track_, LV_PART_MAIN));
  this->style_.active = color(lv_color_hex(this->active_color_provider_()));
  this->style_.bubble_color = color(lv_obj_get_style_bg_color(this->bubble_, LV_PART_MAIN));
  this->style_.text = color(lv_obj_get_style_text_color(this->value_, LV_PART_MAIN));
  this->style_.caption = color(lv_obj_get_style_text_color(this->minimum_, LV_PART_MAIN));
  const auto maximum_caption = color(lv_obj_get_style_text_color(this->maximum_, LV_PART_MAIN));
  if (lv_obj_get_style_bg_opa(this->region_, LV_PART_MAIN) != LV_OPA_COVER ||
      lv_obj_get_style_bg_opa(this->track_, LV_PART_MAIN) != LV_OPA_COVER ||
      lv_obj_get_style_bg_opa(this->bubble_, LV_PART_MAIN) != LV_OPA_COVER ||
      maximum_caption.red != this->style_.caption.red || maximum_caption.green != this->style_.caption.green ||
      maximum_caption.blue != this->style_.caption.blue ||
      lv_obj_get_style_text_letter_space(this->minimum_, LV_PART_MAIN) !=
          lv_obj_get_style_text_letter_space(this->maximum_, LV_PART_MAIN) ||
      std::strcmp(lv_label_get_text(this->minimum_), "0%") != 0 ||
      std::strcmp(lv_label_get_text(this->maximum_), "100%") != 0 ||
      lv_obj_get_style_text_font(this->minimum_, LV_PART_MAIN) !=
          lv_obj_get_style_text_font(this->maximum_, LV_PART_MAIN))
    return false;
  this->mask_bytes_ = 0;
  if (!this->cache_font_(lv_obj_get_style_text_font(this->value_, LV_PART_MAIN),
                         lv_obj_get_style_text_letter_space(this->value_, LV_PART_MAIN), this->style_.fonts[0]) ||
      !this->cache_font_(lv_obj_get_style_text_font(this->minimum_, LV_PART_MAIN),
                         lv_obj_get_style_text_letter_space(this->minimum_, LV_PART_MAIN), this->style_.fonts[1]))
    return false;
  this->styles_valid_ = scalar_style_valid(this->style_);
  return this->styles_valid_;
}

void MaterialDirectScalarControl::request_(uint32_t id, bool known, uint8_t value) {
  if (!this->active_.load(std::memory_order_seq_cst) || id == 0 || value > 100 ||
      !this->control_->is_selection_current(this->control_->view_lifecycle().generation()))
    return;
#ifdef USE_LVGL_MATERIAL_SCALAR_PERFORMANCE_METRICS
  this->performance_start_origin_(id);
#endif
  const bool stable = !this->control_->owner().is_dragging();
  if (stable || this->boundary_request_ != 0) {
    // State/result/release can have just invalidated native labels. Defer its
    // expected base until the main loop flushes that boundary, never recurse
    // into LVGL refresh from an input/API callback. Zero is not admissible.
    this->boundary_request_ = id;
    this->boundary_value_ = value;
    this->boundary_known_ = known;
    this->boundary_stable_ = stable;
    // Later motion replaces the candidate, never the outstanding flush duty.
    this->publish_request_(id, known, value, stable, 0);
    return;
  }
  this->boundary_request_ = 0;
  this->publish_request_(id, known, value, false, this->lvgl_->get_direct_region_base_generation());
}

void MaterialDirectScalarControl::publish_request_(uint32_t id, bool known, uint8_t value, bool stable, uint32_t base) {
#ifdef USE_LVGL_MATERIAL_SCALAR_PERFORMANCE_METRICS
  this->performance_start_origin_(id);  // Initial resume may bypass request_.
#endif
  // Atomic fields plus an even sequence form a bounded, race-free latest mailbox.
  this->request_sequence_.fetch_add(1, std::memory_order_acq_rel);
  this->requested_value_.store(value | (known ? 0x100U : 0U) | (stable ? 0x200U : 0U), std::memory_order_relaxed);
  this->requested_base_.store(base, std::memory_order_relaxed);
  this->requested_id_.store(id, std::memory_order_relaxed);
#ifdef USE_LVGL_MATERIAL_SCALAR_PERFORMANCE_METRICS
  this->performance_.requested_origin_us.store(this->performance_.origin_us, std::memory_order_relaxed);
  this->performance_.requested_origin_sequence.store(this->performance_.origin_sequence, std::memory_order_relaxed);
#endif
  this->request_sequence_.fetch_add(1, std::memory_order_release);
#ifdef USE_ESP32
  if (this->worker_handle_ != nullptr)
    xTaskNotifyGive(this->worker_handle_);
#endif
}

bool MaterialDirectScalarControl::read_request_(Request &request) const {
  for (unsigned attempt = 0; attempt < 3; attempt++) {
    const uint32_t sequence = this->request_sequence_.load(std::memory_order_acquire);
    if ((sequence & 1U) != 0)
      continue;
    const uint32_t id = this->requested_id_.load(std::memory_order_relaxed);
    const uint32_t value = this->requested_value_.load(std::memory_order_relaxed);
    const uint32_t base = this->requested_base_.load(std::memory_order_relaxed);
#ifdef USE_LVGL_MATERIAL_SCALAR_PERFORMANCE_METRICS
    const uint32_t origin_us = this->performance_.requested_origin_us.load(std::memory_order_relaxed);
    const uint32_t origin_sequence = this->performance_.requested_origin_sequence.load(std::memory_order_relaxed);
#endif
    if (sequence == this->request_sequence_.load(std::memory_order_acquire)) {
      request = {id,
                 this->control_->view_lifecycle().generation(),
                 base,
                 (value & 0x100U) != 0,
                 (value & 0x200U) != 0,
                 static_cast<uint8_t>(value & 0xFFU)};
#ifdef USE_LVGL_MATERIAL_SCALAR_PERFORMANCE_METRICS
      request.origin_us = origin_us;
      request.origin_sequence = origin_sequence;
#endif
      return id != 0 && base != 0;
    }
  }
  return false;
}

void MaterialDirectScalarControl::cancel_() {
  this->active_.store(false, std::memory_order_seq_cst);
  this->boundary_request_ = 0;
  if (this->control_ != nullptr && this->control_->view_lifecycle().is_backend(this)) {
    this->control_->view_lifecycle().cancel_close_recovery();
    this->control_->view_lifecycle().cancel_native_power_input();
  }
  this->suspension_requested_ = true;
#ifdef USE_ESP32
  if (this->worker_handle_ != nullptr)
    xTaskNotifyGive(this->worker_handle_);
#endif
}

void MaterialDirectScalarControl::retired_(void *argument) {
  auto *self = static_cast<MaterialDirectScalarControl *>(argument);
  self->source_retired_.store(true, std::memory_order_release);
#ifdef USE_ESP32
  if (self->worker_handle_ != nullptr)
    xTaskNotifyGive(self->worker_handle_);
#endif
}

bool MaterialDirectScalarControl::drain_(uint32_t timeout_ms) {
  this->active_.store(false, std::memory_order_seq_cst);
#ifdef USE_ESP32
  if (this->worker_handle_ != nullptr)
    xTaskNotifyGive(this->worker_handle_);
#endif
  const uint32_t started = millis();
  while (this->worker_busy_.load(std::memory_order_seq_cst) ||
         this->source_in_flight_.load(std::memory_order_acquire)) {
    if (millis() - started >= timeout_ms)
      return false;
    delay(1);
  }
  return true;
}

bool MaterialDirectScalarControl::is_quiescent() const {
  return !this->active_.load(std::memory_order_seq_cst) && !this->worker_busy_.load(std::memory_order_seq_cst) &&
         !this->source_in_flight_.load(std::memory_order_acquire) &&
         !this->region_owned_.load(std::memory_order_acquire) && !this->pause_owned_ && !this->handoff_pending_;
}

bool MaterialDirectScalarControl::suspend_for_snapshot(uint32_t timeout_ms) {
  if (this->control_ == nullptr)
    return this->suspend_control_view_backend(timeout_ms);
  return this->configure_control_view_() && this->control_->view_lifecycle().suspend_for_snapshot(timeout_ms);
}

bool MaterialDirectScalarControl::suspend_control_view_backend(uint32_t timeout_ms) {
  this->cancel_();
  if (!this->setup_ready_)
    return !this->region_owned_.load(std::memory_order_acquire);
  if (!this->drain_(timeout_ms))
    return false;
  if (!this->region_owned_.load(std::memory_order_acquire) && !this->pause_owned_ && !this->handoff_pending_) {
    this->suspension_requested_ = false;
    return true;
  }
  if (!this->pause_owned_) {
    // pause_start acquires exactly one lease even when its barrier is pending.
    this->pause_owned_ = true;
    this->handoff_pending_ = true;
    if (!this->lvgl_->direct_regions_pause_start()) {
      // pause_start acquired a lease but may have failed to enqueue its first
      // barrier. Return just that lease so a later explicit retry can enqueue
      // a fresh barrier; canvas/slot ownership remains retained and closed.
      this->lvgl_->direct_regions_pause(false, 0);
      this->pause_owned_ = false;
      return false;
    }
  }
  if (!this->lvgl_->direct_regions_pause_wait(timeout_ms) || this->lvgl_->get_direct_region_pause_depth() != 1 ||
      !this->lvgl_->direct_handoff_to_lvgl(timeout_ms, false))
    return false;
  if (this->region_owned_.load(std::memory_order_acquire)) {
    // Only after source retirement and coherent queue drain may this one canvas
    // be overwritten. Preserve actual settled pixels, not a guessed HA value.
    if (!this->lvgl_->direct_capture_rgb888(this->canvas_, this->style_.region.width * 3, this->style_.region.x,
                                            this->style_.region.y, this->style_.region.width,
                                            this->style_.region.height))
      return false;
    if (!this->lvgl_->direct_blit_rgb888_try_release(this->style_.region.x, this->style_.region.y,
                                                     this->style_.region.width, this->style_.region.height))
      return false;
    this->region_owned_.store(false, std::memory_order_release);
    lv_image_cache_drop(&this->backing_descriptor_);
    lv_image_set_src(this->backing_image_, &this->backing_descriptor_);
    lv_obj_remove_flag(this->backing_image_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(this->backing_image_);
    this->alias_visible_ = true;
    lv_obj_invalidate(this->backing_image_);
    lv_refr_now(lv_obj_get_display(this->region_));
  }
  // This API consumes exactly our lease even on failure. Never release another
  // producer's nested pause and never report failure as a successful handoff.
  const bool aligned = this->lvgl_->direct_handoff_to_lvgl(timeout_ms, true);
  this->pause_owned_ = false;
  if (!aligned)
    return false;
  this->handoff_pending_ = false;
  this->suspension_requested_ = false;
  return true;
}

bool MaterialDirectScalarControl::hide_alias_() {
  if (!this->alias_visible_)
    return true;
  if (!this->is_quiescent())
    return false;
  lv_obj_add_flag(this->backing_image_, LV_OBJ_FLAG_HIDDEN);
  // Finish native draw-task reads before reusing the aliased source storage.
  lv_refr_now(lv_obj_get_display(this->region_));
  lv_image_cache_drop(&this->backing_descriptor_);
  this->alias_visible_ = false;
  return true;
}

bool MaterialDirectScalarControl::resume(uint32_t generation) {
  return this->configure_control_view_() && this->control_->view_lifecycle().resume(generation);
}

bool MaterialDirectScalarControl::resume_control_view_backend(uint32_t generation) {
  if (this->control_ == nullptr || this->navigation_ == nullptr || this->application_ == nullptr ||
      this->region_ == nullptr || this->control_->view_lifecycle().overlay_generation() != 0 ||
      !this->control_->is_selection_current(generation) ||
      !this->navigation_->is_application_open(this->application_) || this->navigation_->is_transition_active() ||
      !lv_obj_is_visible(this->region_) || !this->is_quiescent())
    return false;
  this->control_->view_lifecycle().set_generation(generation);
  auto renderer_unavailable = [&]() {
    // The matching native view remains usable for validated On/Off even if
    // acceleration/resources are unavailable. Brightness is still gated.
    if (this->control_->set_view_ready(generation, true))
      this->control_->render_ready(this->control_->get_render_request_id(), false);
    return false;
  };
  if (!this->setup_ready_ || !this->hide_alias_() || !this->cache_style_())
    return renderer_unavailable();
  this->requested_id_.store(0, std::memory_order_release);
  this->suspension_requested_ = false;
  // Static state labels are flushed before capturing the expected scene base.
  if (!this->control_->set_view_ready(generation, true))
    return false;
  lv_refr_now(lv_obj_get_display(this->region_));
  this->base_generation_ = this->lvgl_->get_direct_region_base_generation();
  if (this->base_generation_ == 0)
    return false;
  this->active_.store(true, std::memory_order_seq_cst);
  this->boundary_request_ = 0;
#ifdef USE_LVGL_MATERIAL_SCALAR_PERFORMANCE_METRICS
  // A settled lifecycle resume starts at this actual renderer publication.
  // Ordinary same-ID base0/native-flush republishing preserves its origin.
  this->performance_.origin_id = 0;
#endif
  this->publish_request_(this->control_->get_render_request_id(), this->control_->owner().candidate_known(),
                         this->control_->owner().candidate(), true, this->base_generation_);
  return true;  // Brightness remains gated until matching queue/carry result.
}

bool MaterialDirectScalarControl::refresh_style(uint32_t generation) {
  if (!this->control_->is_selection_current(generation) || !this->suspend_for_snapshot())
    return false;
  return this->resume(generation);
}

bool MaterialDirectScalarControl::prepare_navigation(lvgl::NavigationAdmissionKind kind,
                                                     lvgl::LvglApplication *application) {
  return this->configure_control_view_() && this->control_->view_lifecycle().prepare_navigation(kind, application);
}

bool MaterialDirectScalarControl::suspend_for_direct_overlay() {
  if (this->control_ == nullptr)
    return this->suspend_control_view_backend(250);
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
  if (this->control_->uses_generic_view() && !this->initial_enabled_ && !this->control_->owner().is_enabled())
    return this->suspend_control_view_backend(250);
#endif
  return this->configure_control_view_() && this->control_->view_lifecycle().suspend_for_direct_overlay();
}

void MaterialDirectScalarControl::resume_after_direct_overlay() {
  if (this->control_ != nullptr && (this->control_->view_lifecycle().is_backend(this)
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
                                    || this->control_->is_registered_scalar_backend(this)
#endif
                                        ))
    this->control_->view_lifecycle().resume_after_direct_overlay();
}

bool MaterialDirectScalarControl::begin_native_power_input(uint32_t generation) {
  if (!this->setup_ready_)
    return true;  // Explicit native power remains available without acceleration.
  if (!this->control_->is_selection_current(generation) || !this->control_->owner().is_view_ready())
    return false;
  const bool active = this->active_.load(std::memory_order_seq_cst);
  if (!this->drain_(250))
    return false;
  // Retire the previous scene before LVGL flushes its newly pressed native
  // style. An already-stale current completion still fails closed.
  if (this->consume_completion_(active, false) != CompletionDecision::STABLE)
    return false;
  this->boundary_request_ = 0;
  return true;
}

bool MaterialDirectScalarControl::finish_native_power_input(uint32_t generation) {
  if (!this->setup_ready_)
    return true;
  if (!this->control_->is_selection_current(generation) || !this->control_->owner().is_view_ready() ||
      this->active_.load(std::memory_order_seq_cst) || this->worker_busy_.load(std::memory_order_seq_cst) ||
      this->source_in_flight_.load(std::memory_order_acquire))
    return false;
  // Called only by the captured lifecycle's loop, after RELEASED has updated
  // native styles and command/static labels. Keep the persistent regional
  // carry registered; no alias, framebuffer copy or input-callback refresh.
  if (!this->control_->set_view_ready(generation, true))
    return false;
  lv_refr_now(lv_obj_get_display(this->region_));
  const uint32_t base = this->lvgl_->get_direct_region_base_generation();
  if (base == 0)
    return false;
  this->boundary_request_ = 0;
  this->active_.store(true, std::memory_order_seq_cst);
  this->publish_request_(this->control_->get_render_request_id(), this->control_->owner().candidate_known(),
                         this->control_->owner().candidate(), true, base);
  return true;
}

MaterialDirectScalarControl::CompletionDecision MaterialDirectScalarControl::consume_completion_(bool active,
                                                                                                 bool refresh) {
  const uint32_t completed = this->completion_sequence_.load(std::memory_order_acquire);
  if ((completed & 1U) == 0 && completed != this->consumed_completion_) {
    const auto id = this->completed_id_.load(std::memory_order_relaxed);
    const auto selection = this->completed_selection_.load(std::memory_order_relaxed);
    const auto base = this->completed_base_.load(std::memory_order_relaxed);
    const bool accepted = this->completed_success_.load(std::memory_order_relaxed);
    if (completed != this->completion_sequence_.load(std::memory_order_acquire))
      return CompletionDecision::TORN;
    this->consumed_completion_ = completed;
    // Only the owner task decides failure against its current request. A worker
    // completion cannot tear down a newer state/motion boundary published later.
    const bool current = active &&
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
                         (!this->control_->uses_generic_view() || this->control_->view_lifecycle().is_backend(this)) &&
#endif
                         this->control_->is_selection_current(selection) &&
                         id == this->control_->get_render_request_id() &&
                         id == this->requested_id_.load(std::memory_order_acquire);
    if (current) {
#ifdef USE_LVGL_MATERIAL_SCALAR_PERFORMANCE_METRICS
      const uint32_t current_base = this->lvgl_->get_direct_region_base_generation();
      const bool success = accepted && base == current_base;
      if (this->performance_.enabled) {
        this->performance_.last_current_completion = {completed,    id,   selection, base,
                                                      current_base, true, accepted,  success};
      }
#else
      const bool success = accepted && base == this->lvgl_->get_direct_region_base_generation();
#endif
      this->control_->render_ready(id, success);
      if (!success) {
        this->cancel_();
        return CompletionDecision::CLOSED;
      } else if (refresh && !this->control_->owner().is_dragging())
        lv_refr_now(lv_obj_get_display(this->region_));
    }
  }
  return CompletionDecision::STABLE;
}

void MaterialDirectScalarControl::loop() {
  if (this->consume_completion_(this->active_.load(std::memory_order_seq_cst), true) == CompletionDecision::TORN)
    return;
  if (this->suspension_requested_) {
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
    if (this->control_ != nullptr && this->control_->uses_generic_view() &&
        !this->control_->view_lifecycle().is_backend(this))
      this->suspend_control_view_backend(250);  // Retire only our inactive source; never drive the native bridge.
    else
#endif
      this->suspend_for_snapshot();
    // A failed drain is latched closed. Explicit lifecycle callers may retry;
    // do not spend a timeout and invalidate native labels on every loop tick.
    this->suspension_requested_ = false;
  }
  if (this->control_ != nullptr && this->control_->view_lifecycle().is_backend(this))
    this->control_->view_lifecycle().loop();
  if (this->boundary_request_ != 0 && this->active_.load(std::memory_order_seq_cst) &&
      this->control_->is_selection_current(this->control_->view_lifecycle().generation())) {
    const uint32_t id = this->boundary_request_;
    this->boundary_request_ = 0;
    lv_refr_now(lv_obj_get_display(this->region_));
    this->publish_request_(id, this->boundary_known_, this->boundary_value_, this->boundary_stable_,
                           this->lvgl_->get_direct_region_base_generation());
  }
}

void MaterialDirectScalarControl::on_shutdown() {
  if (!this->suspend_for_snapshot())
    return;  // Referenced resources intentionally retained on failed drain.
#ifdef USE_ESP32
  this->stop_.store(true, std::memory_order_release);
  if (this->worker_handle_ != nullptr)
    xTaskNotifyGive(this->worker_handle_);
  const uint32_t started = millis();
  while (this->worker_handle_ != nullptr && !this->worker_exited_.load(std::memory_order_acquire)) {
    if (millis() - started >= 250)
      return;
    delay(1);
  }
  // The native image remains a canvas borrower until its object is deleted.
  if (this->backing_image_ != nullptr) {
    lv_image_cache_drop(&this->backing_descriptor_);
    lv_obj_delete(this->backing_image_);
    this->backing_image_ = nullptr;
  }
  heap_caps_free(this->canvas_);
  heap_caps_free(this->glyph_masks_);
  // The static task can still be returning through vTaskDelete on its core.
  // Keep its setup-time stack for firmware lifetime rather than free it early.
  this->canvas_ = this->glyph_masks_ = nullptr;
#endif
}

size_t MaterialDirectScalarControl::memory_usage_bytes() const {
  size_t bytes = this->canvas_ != nullptr ? this->canvas_capacity_ : 0;
  if (this->glyph_masks_ != nullptr)
    bytes += GLYPH_BYTES;
#ifdef USE_ESP32
  if (this->worker_stack_ != nullptr)
    bytes += WORKER_STACK_BYTES;  // Intentionally retained for task lifetime.
#endif
  return bytes;
}

void MaterialDirectScalarControl::log_memory_usage(const char *phase) const {
  size_t worker_bytes = 0;
#ifdef USE_ESP32
  if (this->worker_stack_ != nullptr)
    worker_bytes = WORKER_STACK_BYTES;
#endif
  ESP_LOGI(TAG,
           "memory %s named_b=%u canvas_b=%u glyph_reserved_b=%u glyph_used_b=%u worker_stack_reserved_b=%u "
           "(excludes global compositor copy, owner/framework and allocator overhead)",
           phase != nullptr ? phase : "runtime", static_cast<unsigned>(this->memory_usage_bytes()),
           static_cast<unsigned>(this->canvas_ != nullptr ? this->canvas_capacity_ : 0),
           static_cast<unsigned>(this->glyph_masks_ != nullptr ? GLYPH_BYTES : 0),
           static_cast<unsigned>(this->glyph_masks_ != nullptr ? this->mask_bytes_ : 0),
           static_cast<unsigned>(worker_bytes));
}

#ifdef USE_LVGL_MATERIAL_SCALAR_PERFORMANCE_METRICS
void MaterialDirectScalarControl::performance_start_origin_(uint32_t id) {
  if (!this->performance_.enabled || id == 0 || id == this->performance_.origin_id)
    return;
  this->performance_.origin_id = id;
  this->performance_.origin_us = micros();
  if (++this->performance_.origin_sequence == 0)
    ++this->performance_.origin_sequence;
  this->performance_.counters.note_logical_origin();
}

bool MaterialDirectScalarControl::performance_snapshot(ScalarPerformanceSnapshot &out) const {
  // Owner-task only. No callback, LVGL mutation, waits, reset or allocation here.
  if (!this->performance_.enabled || this->boundary_request_ != 0 ||
      this->worker_busy_.load(std::memory_order_seq_cst) || this->source_in_flight_.load(std::memory_order_acquire))
    return false;
  const uint32_t requested = this->request_sequence_.load(std::memory_order_acquire);
  const uint32_t completed = this->completion_sequence_.load(std::memory_order_acquire);
  if ((requested & 1U) != 0 || (completed & 1U) != 0)
    return false;
  if (this->active_.load(std::memory_order_seq_cst) &&
      this->requested_id_.load(std::memory_order_acquire) != this->completed_id_.load(std::memory_order_acquire))
    return false;
  if (!this->performance_.counters.snapshot(out))
    return false;
  std::atomic_thread_fence(std::memory_order_acquire);
  return requested == this->request_sequence_.load(std::memory_order_acquire) &&
         completed == this->completion_sequence_.load(std::memory_order_acquire) && this->boundary_request_ == 0 &&
         !this->worker_busy_.load(std::memory_order_seq_cst) &&
         !this->source_in_flight_.load(std::memory_order_acquire);
}

void MaterialDirectScalarControl::log_performance(const char *phase, bool include_histograms) const {
  char label[33]{};
  std::snprintf(label, sizeof(label), "%.32s", phase != nullptr ? phase : "explicit");
  if (this->performance_.enabled) {
    const auto &decision = this->performance_.last_current_completion;
    ESP_LOGI(TAG,
             "performance_completion %s valid=%u seq=%u id=%u selection=%u completed_base=%u current_base=%u "
             "accepted=%u success=%u (last-current-owner-decision; not-panel-ACK)",
             label, static_cast<unsigned>(decision.valid), static_cast<unsigned>(decision.sequence),
             static_cast<unsigned>(decision.id), static_cast<unsigned>(decision.selection),
             static_cast<unsigned>(decision.completed_base), static_cast<unsigned>(decision.current_base),
             static_cast<unsigned>(decision.accepted), static_cast<unsigned>(decision.success));
  }
  ScalarPerformanceSnapshot snapshot;
  if (!this->performance_snapshot(snapshot)) {
    ESP_LOGI(TAG, "performance %s unavailable (disabled, pending, in-flight or changing)", label);
    return;
  }
  ESP_LOGI(TAG,
           "performance %s seq=%u origins=%u observed=%u attempts=%u busy=%u submitted=%u retired=%u accepted=%u "
           "unconfirmed=%u superseded_accepted=%u dedup=%u rejected=%u invalid_age=%u generation=%u setup=%u active=%u "
           "(software-only cumulative)",
           label, static_cast<unsigned>(snapshot.sequence), static_cast<unsigned>(snapshot.logical_origins),
           static_cast<unsigned>(snapshot.worker_observed_origins), static_cast<unsigned>(snapshot.submission_attempts),
           static_cast<unsigned>(snapshot.busy_attempts), static_cast<unsigned>(snapshot.submitted_sources),
           static_cast<unsigned>(snapshot.retired_sources), static_cast<unsigned>(snapshot.accepted_retirements),
           static_cast<unsigned>(snapshot.unconfirmed_retirements),
           static_cast<unsigned>(snapshot.superseded_accepted_retirements),
           static_cast<unsigned>(snapshot.value_dedup_results), static_cast<unsigned>(snapshot.rejected_results),
           static_cast<unsigned>(snapshot.invalid_age_samples),
           static_cast<unsigned>(this->control_->view_lifecycle().generation()),
           static_cast<unsigned>(this->setup_ready_),
           static_cast<unsigned>(this->active_.load(std::memory_order_seq_cst)));
  for (unsigned kind = 0; kind < 2; kind++) {
    const auto &latency = kind == 0 ? snapshot.drag_phase : snapshot.stable;
    const char *name = kind == 0 ? "drag" : "stable";
    const uint32_t p95 = latency.p95_upper_ms();
    ESP_LOGI(TAG,
             "performance %s kind=%s seq=%u samples=%llu le100ms=%llu p95_available=%u p95_upper_ms=%u "
             "max_us=%u endpoint=accepted-source-retirement",
             label, name, static_cast<unsigned>(snapshot.sequence), static_cast<unsigned long long>(latency.count()),
             static_cast<unsigned long long>(latency.at_most_100ms()), static_cast<unsigned>(p95 != UINT32_MAX),
             static_cast<unsigned>(p95), static_cast<unsigned>(latency.max_us));
    if (include_histograms) {
      // At most8 uint32 values per bounded line. Explicit cold diagnostic only;
      // callers need full cumulative bins to derive a phase-specific delta p95.
      for (size_t start = 0; start < latency.bins.size(); start += 8) {
        char values[128]{};
        size_t used = 0;
        const size_t end = std::min(start + 8, latency.bins.size());
        for (size_t i = start; i < end; i++) {
          const int added = std::snprintf(values + used, sizeof(values) - used, i == start ? "%u" : ",%u",
                                          static_cast<unsigned>(latency.bins[i]));
          if (added < 0 || static_cast<size_t>(added) >= sizeof(values) - used)
            return;  // Fixed8x10-digit bound should never be exceeded.
          used += static_cast<size_t>(added);
        }
        ESP_LOGI(TAG, "performance_hist %s kind=%s seq=%u start=%u bins=%s", label, name,
                 static_cast<unsigned>(snapshot.sequence), static_cast<unsigned>(start), values);
      }
    }
  }
}
#endif

void MaterialDirectScalarControl::dump_config() {
  ESP_LOGCONFIG(TAG, "Bounded regional scalar: canvas=%uB masks=%uB worker_stack=%uB (configured capacity)",
                static_cast<unsigned>(this->canvas_capacity_), static_cast<unsigned>(this->mask_bytes_),
                static_cast<unsigned>(WORKER_STACK_BYTES));
}

#ifdef USE_ESP32
static_assert(configTICK_RATE_HZ > 0, "Scalar renderer requires a positive FreeRTOS tick rate");

bool MaterialDirectScalarControl::ensure_worker_() {
  if (this->worker_handle_ != nullptr)
    return true;
  this->worker_stack_ =
      static_cast<StackType_t *>(heap_caps_aligned_alloc(16, WORKER_STACK_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (this->worker_stack_ == nullptr)
    return false;
  this->worker_handle_ =
      xTaskCreateStaticPinnedToCore(worker_, "material_scalar", WORKER_STACK_BYTES, this, 2, this->worker_stack_,
                                    &this->worker_storage_, esp32::background_task_core(-1));
  if (this->worker_handle_ == nullptr) {
    heap_caps_free(this->worker_stack_);
    this->worker_stack_ = nullptr;
  }
  return this->worker_handle_ != nullptr;
}

void MaterialDirectScalarControl::worker_(void *argument) {
  static_cast<MaterialDirectScalarControl *>(argument)->render_worker_();
}

void MaterialDirectScalarControl::render_worker_() {
  Request last{};
  bool have_last = false;
  while (!this->stop_.load(std::memory_order_acquire)) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    while (!this->stop_.load(std::memory_order_acquire)) {
      // Busy precedes active check: stop + wait cannot miss a late waking worker.
      this->worker_busy_.store(true, std::memory_order_seq_cst);
      if (!this->active_.load(std::memory_order_seq_cst)) {
        this->worker_busy_.store(false, std::memory_order_seq_cst);
        break;
      }
      Request request;
      if (!this->read_request_(request) || (have_last && request.id == last.id)) {
        this->worker_busy_.store(false, std::memory_order_seq_cst);
        break;
      }
#ifdef USE_LVGL_MATERIAL_SCALAR_PERFORMANCE_METRICS
      bool metrics_retired = false;
      if (this->performance_.enabled && request.origin_sequence != this->performance_.observed_origin_sequence) {
        this->performance_.counters.note_worker_observed_origin();
        this->performance_.observed_origin_sequence = request.origin_sequence;
      }
#endif
      bool accepted = have_last && request.selection == last.selection && request.base == last.base &&
                      request.known == last.known && request.value == last.value && (!request.stable || last.stable) &&
                      request.base == this->lvgl_->get_direct_region_base_generation() &&
                      this->lvgl_->get_direct_region_pause_depth() == 0;
#ifdef USE_LVGL_MATERIAL_SCALAR_PERFORMANCE_METRICS
      const bool metrics_value_dedup = accepted;
#endif
      if (accepted) {
        this->coalesced_.fetch_add(1, std::memory_order_relaxed);
      } else {
        if (!scalar_raster_rgb888(this->canvas_, this->canvas_capacity_, this->style_, this->glyph_masks_,
                                  this->mask_bytes_, request.known, request.value)) {
          accepted = false;
        } else if (this->active_.load(std::memory_order_seq_cst)) {
          this->source_retired_.store(false, std::memory_order_release);
          this->source_in_flight_.store(true, std::memory_order_release);
          const auto submitted = this->lvgl_->direct_blit_rgb888_generation_async(
              this->canvas_, this->style_.region.width * 3, this->style_.region.x, this->style_.region.y,
              this->style_.region.width, this->style_.region.height, request.base, &this->result_, retired_, this,
              request.stable, true);
#ifdef USE_LVGL_MATERIAL_SCALAR_PERFORMANCE_METRICS
          if (this->performance_.enabled)
            this->performance_.counters.note_submission(submitted == LVGL_DIRECT_BLIT_SUBMITTED,
                                                        submitted == LVGL_DIRECT_BLIT_BUSY);
#endif
          if (submitted == LVGL_DIRECT_BLIT_SUBMITTED) {
            this->region_owned_.store(true, std::memory_order_release);
            // No buffer/metadata write until the exactly-once source callback.
            while (!this->source_retired_.load(std::memory_order_acquire))
              ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
            accepted = this->result_.accepted(request.base);
#ifdef USE_LVGL_MATERIAL_SCALAR_PERFORMANCE_METRICS
            metrics_retired = true;
#endif
            this->rendered_.fetch_add(1, std::memory_order_relaxed);
          }
          this->source_in_flight_.store(false, std::memory_order_release);
          if (submitted == LVGL_DIRECT_BLIT_BUSY) {
            this->worker_busy_.store(false, std::memory_order_seq_cst);
            vTaskDelay(scalar_render_delay_ticks(configTICK_RATE_HZ));
            continue;  // No callback/ownership transfer; recompose newest only.
          }
        }
      }
#ifdef USE_LVGL_MATERIAL_SCALAR_PERFORMANCE_METRICS
      if (this->performance_.enabled) {
        const uint32_t observed_us = metrics_retired && accepted ? micros() : request.origin_us;
        const bool superseded = this->requested_id_.load(std::memory_order_acquire) != request.id;
        this->performance_.counters.note_result(metrics_retired, accepted, metrics_value_dedup, request.stable,
                                                request.origin_us, observed_us, superseded);
      }
#endif
      if (accepted) {
        last = request;
        have_last = true;
      }
      this->completion_sequence_.fetch_add(1, std::memory_order_acq_rel);
      this->completed_id_.store(request.id, std::memory_order_relaxed);
      this->completed_selection_.store(request.selection, std::memory_order_relaxed);
      this->completed_base_.store(request.base, std::memory_order_relaxed);
      this->completed_success_.store(accepted, std::memory_order_relaxed);
      this->completion_sequence_.fetch_add(1, std::memory_order_release);
      this->worker_busy_.store(false, std::memory_order_seq_cst);
      // False results retire this request only. The main task arbitrates whether
      // it is still current before suspending; new work may arrive at any time.
      if (!accepted)
        break;
      vTaskDelay(scalar_render_delay_ticks(configTICK_RATE_HZ));  // Ordinary renders never exceed 30 Hz.
    }
  }
  this->worker_exited_.store(true, std::memory_order_release);
  vTaskDelete(nullptr);
}
#endif

}  // namespace esphome::lvgl_material
#endif
