#include "lvgl_image_presenter.h"

#include <algorithm>
#include <cmath>

#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::lvgl_image_presenter {

static const char *const TAG = "lvgl_image_presenter";

void LvglImagePresenter::setup() {
  this->select_pan_direction_(this->pan_forward_);
  this->last_loop_ms_ = millis();

  lv_lock();
  if (this->obj_ != nullptr && lv_obj_is_valid(this->obj_))
    this->base_opacity_ = lv_obj_get_style_opa(this->obj_, LV_PART_MAIN);
  lv_unlock();

  if (this->use_direct_) {
    ESP_LOGW(TAG, "Direct presentation requested but no accelerated backend is available; using LVGL transforms");
  }
}

void LvglImagePresenter::dump_config() {
  ESP_LOGCONFIG(TAG, "LVGL Image Presenter:");
  ESP_LOGCONFIG(TAG, "  Phase duration: %ums", static_cast<unsigned>(this->phase_duration_ms_));
  ESP_LOGCONFIG(TAG, "  Frame interval: %ums", static_cast<unsigned>(this->frame_interval_ms_));
  ESP_LOGCONFIG(TAG, "  Zoom: %.3f -> %.3f", static_cast<double>(this->zoom_start_) / LV_SCALE_NONE,
                static_cast<double>(this->zoom_end_) / LV_SCALE_NONE);
  ESP_LOGCONFIG(TAG, "  Pan limit: %.1f%%", static_cast<double>(this->pan_limit_) * 100.0);
  ESP_LOGCONFIG(TAG, "  Direct backend: %s", this->use_direct_ ? "requested, unavailable" : "disabled");
}

void LvglImagePresenter::loop() {
  const uint32_t now = millis();
  const uint32_t delta = now - this->last_loop_ms_;
  if (delta < this->frame_interval_ms_)
    return;
  this->last_loop_ms_ = now;

  if (this->paused_ || this->direct_frozen_ || this->obj_ == nullptr)
    return;

  lv_lock();
  const bool visible = lv_obj_is_valid(this->obj_) && lv_obj_is_visible(this->obj_);
  lv_unlock();
  if (!visible)
    return;

  if (this->transition_state_ != TransitionState::NONE) {
    this->update_transition_(delta);
    return;
  }

  if (!this->phase_complete_) {
    this->phase_elapsed_ms_ =
        std::min(this->phase_duration_ms_,
                 this->phase_elapsed_ms_ + std::min(delta, this->frame_interval_ms_ * 3U));
    this->phase_complete_ = this->phase_elapsed_ms_ >= this->phase_duration_ms_;
    this->update_transform_(this->phase_elapsed_ms_);
  }
}

void LvglImagePresenter::restart() {
  this->transition_state_ = TransitionState::NONE;
  this->transition_source_ = nullptr;
  this->transition_elapsed_ms_ = 0;
  this->transition_failed_ = false;
  this->phase_elapsed_ms_ = 0;
  this->zooming_in_ = true;
  this->phase_complete_ = false;
  this->paused_ = false;
  this->direct_frozen_ = false;
  this->select_pan_direction_(!this->pan_forward_);
  this->last_loop_ms_ = millis();
  this->set_opacity_(this->base_opacity_);
  this->update_transform_(0);
}

bool LvglImagePresenter::pause() {
  this->paused_ = true;
  this->last_loop_ms_ = millis();
  return true;
}

bool LvglImagePresenter::pause_for_snapshot() { return this->pause(); }

bool LvglImagePresenter::complete_snapshot_handoff(uint32_t timeout_ms) {
  (void) timeout_ms;
  return true;
}

void LvglImagePresenter::resume() {
  this->paused_ = false;
  this->direct_frozen_ = false;
  this->last_loop_ms_ = millis();
}

bool LvglImagePresenter::freeze_direct() {
  this->direct_frozen_ = true;
  this->last_loop_ms_ = millis();
  return true;
}

void LvglImagePresenter::resume_direct() {
  this->direct_frozen_ = false;
  this->last_loop_ms_ = millis();
}

void LvglImagePresenter::reset_transform() {
  if (this->obj_ == nullptr)
    return;

  lv_lock();
  if (lv_obj_is_valid(this->obj_)) {
    auto *display = lv_obj_get_display(this->obj_);
    const bool invalidation_enabled = display != nullptr && lv_display_is_invalidation_enabled(display);
    if (invalidation_enabled)
      lv_display_enable_invalidation(display, false);
    lv_image_set_pivot(this->obj_, lv_obj_get_width(this->obj_) / 2, lv_obj_get_height(this->obj_) / 2);
    lv_image_set_scale(this->obj_, LV_SCALE_NONE);
    lv_obj_set_pos(this->obj_, 0, 0);
    if (invalidation_enabled) {
      lv_display_enable_invalidation(display, true);
      lv_obj_invalidate(lv_obj_get_parent(this->obj_));
    }
    this->geometry_source_width_ = 0;
    this->geometry_source_height_ = 0;
    this->geometry_viewport_width_ = 0;
    this->geometry_viewport_height_ = 0;
  }
  lv_unlock();
}

bool LvglImagePresenter::transition_to(const lv_image_dsc_t *source, uint32_t duration_ms) {
  if (this->paused_ || this->direct_frozen_ || this->obj_ == nullptr || source == nullptr || source->data == nullptr ||
      source->header.w < 2 || source->header.h < 2 || this->transition_state_ != TransitionState::NONE) {
    return false;
  }

  this->transition_source_ = source;
  this->transition_duration_ms_ = std::clamp<uint32_t>(duration_ms, 200, 3000);
  this->transition_elapsed_ms_ = 0;
  this->transition_failed_ = false;
  this->transition_state_ = TransitionState::FADING_OUT;
  this->last_loop_ms_ = millis();
  return true;
}

bool LvglImagePresenter::is_transition_pending_or_active() const {
  return this->transition_state_ != TransitionState::NONE;
}

bool LvglImagePresenter::transition_failed() const {
  return this->transition_failed_.load(std::memory_order_acquire);
}

void LvglImagePresenter::log_memory_usage(const char *phase) const {
  ESP_LOGI(TAG, "%s memory=0K backend=lvgl", phase == nullptr ? "runtime" : phase);
}

void LvglImagePresenter::update_transition_(uint32_t delta_ms) {
  const uint32_t half_duration = std::max<uint32_t>(1, this->transition_duration_ms_ / 2);
  this->transition_elapsed_ms_ = std::min(half_duration, this->transition_elapsed_ms_ + delta_ms);
  const float linear =
      static_cast<float>(this->transition_elapsed_ms_) / static_cast<float>(half_duration);
  const float eased = linear * linear * (3.0f - 2.0f * linear);

  if (this->transition_state_ == TransitionState::FADING_OUT) {
    this->set_opacity_(static_cast<lv_opa_t>(std::lround((1.0f - eased) * this->base_opacity_)));
    if (this->transition_elapsed_ms_ >= half_duration) {
      this->switch_transition_source_();
      this->transition_elapsed_ms_ = 0;
      this->transition_state_ = TransitionState::FADING_IN;
    }
    return;
  }

  this->set_opacity_(static_cast<lv_opa_t>(std::lround(eased * this->base_opacity_)));
  if (this->transition_elapsed_ms_ >= half_duration) {
    this->set_opacity_(this->base_opacity_);
    this->transition_state_ = TransitionState::NONE;
    this->transition_source_ = nullptr;
    this->transition_elapsed_ms_ = 0;
  }
}

void LvglImagePresenter::set_opacity_(lv_opa_t opacity) {
  lv_lock();
  if (this->obj_ != nullptr && lv_obj_is_valid(this->obj_))
    lv_obj_set_style_opa(this->obj_, opacity, LV_PART_MAIN);
  lv_unlock();
}

void LvglImagePresenter::switch_transition_source_() {
  lv_lock();
  if (this->obj_ == nullptr || !lv_obj_is_valid(this->obj_) || this->transition_source_ == nullptr) {
    this->transition_failed_ = true;
    this->transition_state_ = TransitionState::NONE;
    lv_unlock();
    return;
  }
  lv_image_set_src(this->obj_, this->transition_source_);
  lv_unlock();

  this->phase_elapsed_ms_ = 0;
  this->phase_complete_ = false;
  this->zooming_in_ = true;
  this->select_pan_direction_(!this->pan_forward_);
  this->update_transform_(0);
}

bool LvglImagePresenter::update_transform_(uint32_t elapsed_ms) {
  if (this->obj_ == nullptr || this->phase_duration_ms_ == 0)
    return false;

  lv_lock();
  if (!lv_obj_is_valid(this->obj_)) {
    lv_unlock();
    return false;
  }

  auto *parent = lv_obj_get_parent(this->obj_);
  const int32_t viewport_width =
      parent != nullptr ? lv_obj_get_content_width(parent) : lv_obj_get_width(this->obj_);
  const int32_t viewport_height =
      parent != nullptr ? lv_obj_get_content_height(parent) : lv_obj_get_height(this->obj_);
  const int32_t source_width = lv_image_get_src_width(this->obj_);
  const int32_t source_height = lv_image_get_src_height(this->obj_);
  if (viewport_width <= 0 || viewport_height <= 0 || source_width <= 0 || source_height <= 0) {
    lv_unlock();
    return false;
  }

  const float linear =
      std::clamp(static_cast<float>(elapsed_ms) / static_cast<float>(this->phase_duration_ms_), 0.0f, 1.0f);
  const float progress = linear * linear * (3.0f - 2.0f * linear);
  const float directed = this->zooming_in_ ? progress : 1.0f - progress;

  const uint32_t cover_scale =
      std::max((static_cast<uint32_t>(viewport_width) * LV_SCALE_NONE + source_width - 1) / source_width,
               (static_cast<uint32_t>(viewport_height) * LV_SCALE_NONE + source_height - 1) / source_height);
  const uint32_t start_scale = cover_scale * this->zoom_start_ / LV_SCALE_NONE;
  const uint32_t end_scale = cover_scale * this->zoom_end_ / LV_SCALE_NONE;
  const uint32_t scale = static_cast<uint32_t>(std::lround(
      static_cast<float>(start_scale) + static_cast<float>(end_scale - start_scale) * directed));

  const int32_t scaled_width =
      static_cast<int32_t>((static_cast<int64_t>(source_width) * scale) / LV_SCALE_NONE);
  const int32_t scaled_height =
      static_cast<int32_t>((static_cast<int64_t>(source_height) * scale) / LV_SCALE_NONE);
  const float offset_x = this->target_x_ * static_cast<float>(std::max<int32_t>(0, scaled_width - viewport_width)) *
                         0.5f * directed;
  const float offset_y =
      this->target_y_ * static_cast<float>(std::max<int32_t>(0, scaled_height - viewport_height)) * 0.5f * directed;
  const int32_t x = (viewport_width - scaled_width) / 2 + static_cast<int32_t>(std::lround(offset_x));
  const int32_t y = (viewport_height - scaled_height) / 2 + static_cast<int32_t>(std::lround(offset_y));

  const int32_t base_x = (viewport_width - source_width) / 2;
  const int32_t base_y = (viewport_height - source_height) / 2;
  const bool geometry_changed = source_width != this->geometry_source_width_ ||
                                source_height != this->geometry_source_height_ ||
                                viewport_width != this->geometry_viewport_width_ ||
                                viewport_height != this->geometry_viewport_height_;

  int32_t pivot_x = source_width / 2;
  int32_t pivot_y = source_height / 2;
  const int32_t scale_delta = static_cast<int32_t>(scale) - LV_SCALE_NONE;
  if (scale_delta != 0) {
    pivot_x = static_cast<int32_t>((static_cast<int64_t>(base_x - x) * LV_SCALE_NONE) / scale_delta);
    pivot_y = static_cast<int32_t>((static_cast<int64_t>(base_y - y) * LV_SCALE_NONE) / scale_delta);
  }

  auto *display = lv_obj_get_display(this->obj_);
  const bool invalidation_enabled = display != nullptr && lv_display_is_invalidation_enabled(display);
  if (invalidation_enabled)
    lv_display_enable_invalidation(display, false);
  if (geometry_changed) {
    lv_obj_set_pos(this->obj_, base_x, base_y);
    this->geometry_source_width_ = source_width;
    this->geometry_source_height_ = source_height;
    this->geometry_viewport_width_ = viewport_width;
    this->geometry_viewport_height_ = viewport_height;
  }
  lv_image_set_pivot(this->obj_, pivot_x, pivot_y);
  lv_image_set_scale(this->obj_, scale);
  if (invalidation_enabled) {
    lv_display_enable_invalidation(display, true);
    lv_obj_invalidate(parent != nullptr ? parent : this->obj_);
  }
  lv_unlock();
  return true;
}

void LvglImagePresenter::select_pan_direction_(bool forward) {
  this->pan_forward_ = forward;
  const float direction = forward ? 1.0f : -1.0f;
  this->target_x_ = direction * this->pan_limit_;
  this->target_y_ = direction * this->pan_limit_;
}

}  // namespace esphome::lvgl_image_presenter
