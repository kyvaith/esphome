#include "lvgl_image_presenter.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#ifdef USE_ESP32_JPEG
#include "esphome/components/esp32_jpeg/esp32_jpeg.h"
#endif

#ifdef USE_ESP32_VARIANT_ESP32P4
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_private/esp_cache_private.h"
#include "esp_timer.h"
#endif

namespace esphome::lvgl_image_presenter {

static const char *const TAG = "lvgl_image_presenter";

void LvglImagePresenter::setup() {
  this->select_pan_direction_(this->pan_forward_);
  this->phase_complete_.store(!this->motion_enabled_, std::memory_order_release);
  this->last_loop_ms_ = millis();
  this->present_fps_window_started_ms_ = this->last_loop_ms_;

  lv_lock();
  if (this->obj_ != nullptr && lv_obj_is_valid(this->obj_))
    this->base_opacity_ = lv_obj_get_style_opa(this->obj_, LV_PART_MAIN);
  lv_unlock();

#if defined(USE_ESP32_VARIANT_ESP32P4) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  if (this->crossfade_ && !this->create_crossfade_worker_())
    ESP_LOGW(TAG, "Direct scene crossfade worker unavailable; transitions will use the LVGL fallback");
#endif

  if (!this->use_direct_)
    return;

  if (this->use_direct_jpeg_ && this->source_ != nullptr) {
    this->direct_jpeg_registered_ = this->source_->set_jpeg_frame_consumer(this);
    if (!this->direct_jpeg_registered_) {
      ESP_LOGW(TAG, "Source does not expose encoded JPEG frames; using decoded-image presentation");
      this->use_direct_jpeg_ = false;
    }
  }

#ifdef USE_ESP32_VARIANT_ESP32P4
  ppa_client_config_t config{};
  config.oper_type = PPA_OPERATION_SRM;
  config.max_pending_trans_num = 1;
  config.data_burst_length = PPA_DATA_BURST_LENGTH_128;
  const esp_err_t error = ppa_register_client(&config, &this->direct_srm_client_);
  if (error == ESP_OK && this->lvgl_component_ != nullptr && this->source_ != nullptr) {
    this->direct_backend_ready_ = true;
  } else {
    ESP_LOGW(TAG, "Direct backend unavailable: %s",
             error == ESP_OK ? "incomplete configuration" : esp_err_to_name(error));
    if (this->direct_srm_client_ != nullptr) {
      ppa_unregister_client(this->direct_srm_client_);
      this->direct_srm_client_ = nullptr;
    }
  }
#else
  ESP_LOGW(TAG, "Direct presentation is unavailable on this platform; using LVGL transforms");
#endif
}

void LvglImagePresenter::on_shutdown() {
  this->paused_ = true;
  this->cleanup_crossfade_(false);
  this->accept_direct_jpeg_.store(false, std::memory_order_release);
  this->direct_session_deferred_.store(false, std::memory_order_release);
  this->direct_jpeg_frame_ready_.store(false, std::memory_order_release);
  this->stop_direct_session_(500);
  if (this->direct_jpeg_registered_ && this->source_ != nullptr) {
    this->source_->set_jpeg_frame_consumer(nullptr);
    this->direct_jpeg_registered_ = false;
  }
#ifdef USE_ESP32_VARIANT_ESP32P4
  if (this->direct_srm_client_ != nullptr) {
    ppa_unregister_client(this->direct_srm_client_);
    this->direct_srm_client_ = nullptr;
  }
#endif
}

void LvglImagePresenter::dump_config() {
  ESP_LOGCONFIG(TAG, "LVGL Image Presenter:");
  ESP_LOGCONFIG(TAG, "  Phase duration: %ums", static_cast<unsigned>(this->phase_duration_ms_));
  ESP_LOGCONFIG(TAG, "  Frame interval: %ums", static_cast<unsigned>(this->frame_interval_ms_));
  ESP_LOGCONFIG(TAG, "  Zoom: %.3f -> %.3f", static_cast<double>(this->zoom_start_) / LV_SCALE_NONE,
                static_cast<double>(this->zoom_end_) / LV_SCALE_NONE);
  ESP_LOGCONFIG(TAG, "  Pan limit: %.1f%%", static_cast<double>(this->pan_limit_) * 100.0);
  ESP_LOGCONFIG(TAG, "  Direct backend: %s",
                !this->use_direct_ ? "disabled" : (this->direct_backend_ready_ ? "ready" : "unavailable"));
  ESP_LOGCONFIG(TAG, "  Continuous source: %s", YESNO(this->continuous_));
  ESP_LOGCONFIG(TAG, "  Motion: %s", this->motion_enabled_ ? "enabled" : "disabled");
  ESP_LOGCONFIG(TAG, "  Crossfade: %s", YESNO(this->crossfade_));
  ESP_LOGCONFIG(TAG, "  Direct JPEG to framebuffer: %s", YESNO(this->use_direct_jpeg_));
  ESP_LOGCONFIG(TAG, "  Defer direct session until JPEG frame: %s",
                YESNO(this->defer_direct_session_until_frame_));
  ESP_LOGCONFIG(TAG, "  Presented frames: %u (%.1f fps)", static_cast<unsigned>(this->presented_frames()),
                static_cast<double>(this->measured_present_fps()));
}

void LvglImagePresenter::loop() {
#if defined(USE_ESP32_VARIANT_ESP32P4) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  this->service_crossfade_completion_();
  if (this->transition_state_ == TransitionState::CROSS_FADING)
    return;
#endif

  const uint32_t now = millis();
  const uint32_t delta = now - this->last_loop_ms_;
  if (delta < this->frame_interval_ms_)
    return;
  this->last_loop_ms_ = now;

  if (this->paused_ || this->direct_frozen_ || this->obj_ == nullptr) {
    if (this->direct_session_active_)
      this->stop_direct_session_(50);
    return;
  }

  if (!this->is_widget_visible_()) {
    if (this->direct_session_active_)
      this->stop_direct_session_(50);
    return;
  }

  if (this->use_direct_ && this->direct_backend_ready_) {
    if (this->direct_target_lease_ && !this->present_pending_direct_frame_(50))
      return;
    if (this->use_direct_jpeg_) {
      if (this->direct_session_deferred_.load(std::memory_order_acquire)) {
        // Keep LVGL in control so native loading UI remains animated. The
        // first encoded frame is only a readiness signal: this LVGL task owns
        // both ends of the display-session mutex, and the following frame
        // replaces the loader with a complete framebuffer.
        this->accept_direct_jpeg_.store(true, std::memory_order_release);
        if (this->direct_jpeg_frame_ready_.exchange(false, std::memory_order_acq_rel) &&
            this->ensure_direct_session_()) {
          this->direct_session_deferred_.store(false, std::memory_order_release);
          this->refresh_continuous_source_();
        }
        return;
      }
      if (this->ensure_direct_session_()) {
        this->accept_direct_jpeg_.store(true, std::memory_order_release);
        // Frames matching the DSI geometry are decoded straight into the
        // presentation lease. Other camera sizes are hardware-decoded by the
        // source and must still be cropped/scaled through PPA in this same
        // direct session.
        this->refresh_continuous_source_();
        return;
      }
      if (this->direct_backend_ready_)
        return;
    } else if (this->continuous_ || !this->phase_complete_) {
      this->phase_elapsed_ms_ =
          std::min(this->phase_duration_ms_, this->phase_elapsed_ms_ + std::min(delta, this->frame_interval_ms_ * 3U));
      this->phase_complete_ = this->phase_elapsed_ms_ >= this->phase_duration_ms_;
      const bool updated = this->update_direct_frame_(this->phase_elapsed_ms_);
      if (!updated && !this->direct_backend_ready_)
        this->refresh_continuous_source_();
    }
    if (this->direct_backend_ready_)
      return;
  }

  if (this->direct_session_active_) {
    if (!this->stop_direct_session_(50))
      return;
  }

  if (this->transition_state_ != TransitionState::NONE) {
    this->update_transition_(delta);
    return;
  }

  if (!this->phase_complete_) {
    this->phase_elapsed_ms_ =
        std::min(this->phase_duration_ms_, this->phase_elapsed_ms_ + std::min(delta, this->frame_interval_ms_ * 3U));
    this->phase_complete_ = this->phase_elapsed_ms_ >= this->phase_duration_ms_;
    this->update_transform_(this->phase_elapsed_ms_);
  }
  if (this->continuous_)
    this->refresh_continuous_source_();
}

void LvglImagePresenter::restart() {
  this->cleanup_crossfade_(true);
  this->transition_state_ = TransitionState::NONE;
  this->transition_source_ = nullptr;
  this->transition_elapsed_ms_ = 0;
  this->transition_failed_ = false;
  this->phase_elapsed_ms_ = 0;
  this->zooming_in_ = true;
  this->phase_complete_ = !this->motion_enabled_;
  this->paused_ = false;
  this->direct_frozen_ = false;
  this->select_pan_direction_(!this->pan_forward_);
  this->last_loop_ms_ = millis();
  this->reset_direct_frame_cache_();
  if (!this->direct_session_active_)
    this->direct_jpeg_frame_presented_.store(false, std::memory_order_release);
  if (this->use_direct_jpeg_ && this->defer_direct_session_until_frame_ && this->direct_backend_ready_ &&
      !this->direct_session_active_) {
    this->direct_session_deferred_.store(true, std::memory_order_release);
    this->direct_jpeg_frame_ready_.store(false, std::memory_order_release);
    this->accept_direct_jpeg_.store(true, std::memory_order_release);
  }
  if (!this->use_direct_ || !this->direct_backend_ready_) {
    this->set_opacity_(this->base_opacity_);
    if (this->motion_enabled_)
      this->update_transform_(0);
  }
}

bool LvglImagePresenter::pause() {
  this->paused_ = true;
  this->direct_session_deferred_.store(false, std::memory_order_release);
  this->direct_jpeg_frame_ready_.store(false, std::memory_order_release);
  this->last_loop_ms_ = millis();
#if defined(USE_ESP32_VARIANT_ESP32P4) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  if (this->transition_state_ == TransitionState::CROSS_FADING)
    this->cancel_transition();
#endif
  return this->stop_direct_session_(600);
}

bool LvglImagePresenter::pause_for_snapshot() { return this->pause(); }

bool LvglImagePresenter::complete_snapshot_handoff(uint32_t timeout_ms) {
  return this->stop_direct_session_(timeout_ms);
}

void LvglImagePresenter::resume() {
  this->paused_ = false;
  this->direct_frozen_ = false;
  this->last_loop_ms_ = millis();
  if (this->use_direct_jpeg_ && this->defer_direct_session_until_frame_ && this->direct_backend_ready_ &&
      !this->direct_session_active_ && !this->direct_jpeg_frame_presented_.load(std::memory_order_acquire)) {
    this->direct_session_deferred_.store(true, std::memory_order_release);
    this->direct_jpeg_frame_ready_.store(false, std::memory_order_release);
    this->accept_direct_jpeg_.store(true, std::memory_order_release);
  }
}

bool LvglImagePresenter::freeze_direct() {
  this->direct_frozen_ = true;
  this->last_loop_ms_ = millis();
#if defined(USE_ESP32_VARIANT_ESP32P4) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  if (this->transition_state_ == TransitionState::CROSS_FADING)
    this->cancel_transition();
#endif
  return this->stop_direct_session_(600);
}

void LvglImagePresenter::resume_direct() {
  this->direct_frozen_ = false;
  this->last_loop_ms_ = millis();
}

bool LvglImagePresenter::suspend_for_direct_overlay() {
  if (this->direct_overlay_suspended_)
    return true;
  if (this->paused_ || this->direct_frozen_ || !this->use_direct_ || !this->is_widget_visible_())
    return true;
  if (!this->pause()) {
    this->resume();
    return false;
  }
  this->direct_overlay_suspended_ = true;
  return true;
}

void LvglImagePresenter::resume_after_direct_overlay() {
  if (!this->direct_overlay_suspended_)
    return;
  this->direct_overlay_suspended_ = false;
  this->resume();
  // Reacquire the framebuffer session synchronously while the overlay still
  // has LVGL invalidation disabled. The restored camera frame then remains
  // pinned until a fresh JPEG arrives instead of briefly exposing the black
  // LVGL page behind the presenter.
  if (!this->direct_session_deferred_.load(std::memory_order_acquire) && this->use_direct_ &&
      this->direct_backend_ready_ && this->is_widget_visible_() &&
      this->ensure_direct_session_()) {
    if (this->use_direct_jpeg_)
      this->accept_direct_jpeg_.store(true, std::memory_order_release);
    this->refresh_continuous_source_();
  }
}

void LvglImagePresenter::reset_transform() {
  this->stop_direct_session_(600);
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

bool LvglImagePresenter::prepare_transition() {
  if (!this->crossfade_ || this->use_direct_ || this->obj_ == nullptr || this->source_ == nullptr) {
    return false;
  }

  // A newer image may arrive while the previous scene is still fading. Stop
  // that worker first and use the frame currently scanned out by DSI as the
  // next transition's old scene. Letting both generations continue would
  // allow a late completion to publish stale artwork.
  if (this->transition_prepared_ || this->transition_state_ != TransitionState::NONE) {
    this->cancel_transition();
  } else {
    this->cleanup_crossfade_(true);
  }
  lv_lock();
  if (!lv_obj_is_valid(this->obj_) ||
      lv_obj_get_screen(this->obj_) != lv_display_get_screen_active(lv_obj_get_display(this->obj_))) {
    lv_unlock();
    return false;
  }
  const lv_image_dsc_t *current = this->source_->get_lv_image_dsc();
  if (current != nullptr && current->data != nullptr && current->header.w >= 2 && current->header.h >= 2) {
    this->previous_dsc_ = *current;
    lv_image_set_src(this->obj_, &this->previous_dsc_);
  } else {
    // The first decoded image can replace a separate placeholder while this
    // target widget is still hidden. The pinned DSI frame is the old scene in
    // that case, so no previous source descriptor is required.
    this->previous_dsc_ = {};
  }
  this->transition_prepared_ = true;
  lv_unlock();

#if defined(USE_ESP32_VARIANT_ESP32P4) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  // Take ownership before the decoder starts overwriting its reusable image
  // buffer. The displayed frame remains immutable, while dynamic regions can
  // continue producing background-independent overlays for the upcoming
  // crossfade. Acquiring this only in the worker left a short window in which
  // a wave/marquee update could publish a rectangle from another artwork
  // generation.
  if (this->lvgl_component_ != nullptr) {
    if (!this->lvgl_component_->begin_direct_image_animation(true)) {
      this->cleanup_crossfade_(true);
      return false;
    }
    this->crossfade_direct_active_.store(true, std::memory_order_release);
  }
#endif
  return true;
}

void LvglImagePresenter::cancel_transition() {
#if defined(USE_ESP32_VARIANT_ESP32P4) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  this->stop_crossfade_worker_();
#endif
  this->transition_state_ = TransitionState::NONE;
  this->transition_source_ = nullptr;
  this->transition_elapsed_ms_ = 0;
  this->cleanup_crossfade_(true);
}

bool LvglImagePresenter::transition_to(image::Image *source, uint32_t duration_ms) {
  if (this->paused_ || this->direct_frozen_ || this->obj_ == nullptr || source == nullptr ||
      this->transition_state_ != TransitionState::NONE) {
    return false;
  }

  image::ImageBufferLease lease;
  if (!source->acquire_buffer(&lease))
    return false;
  const bool valid_source = lease.width >= 2 && lease.height >= 2;
  source->release_buffer(&lease);
  if (!valid_source)
    return false;

  if (this->crossfade_ && !this->use_direct_) {
    if (this->start_crossfade_(source, duration_ms))
      return true;

    this->cleanup_crossfade_(false);
    this->source_ = source;
    lv_lock();
    if (lv_obj_is_valid(this->obj_)) {
      lv_image_set_src(this->obj_, this->source_->get_lv_image_dsc());
      lv_display_t *display = lv_obj_get_display(this->obj_);
      if (display != nullptr && !lv_display_is_invalidation_enabled(display))
        lv_display_enable_invalidation(display, true);
      lv_obj_invalidate(lv_obj_get_screen(this->obj_));
    }
    lv_unlock();
    this->transition_source_ = nullptr;
    this->transition_elapsed_ms_ = 0;
    this->transition_state_ = TransitionState::NONE;
    this->phase_elapsed_ms_ = 0;
    this->phase_complete_ = !this->motion_enabled_;
    this->last_loop_ms_ = millis();
    return true;
  }

  if (this->use_direct_ && this->direct_backend_ready_) {
    this->source_ = source;
    lv_lock();
    if (lv_obj_is_valid(this->obj_))
      lv_image_set_src(this->obj_, this->source_->get_lv_image_dsc());
    lv_unlock();
    this->transition_source_ = nullptr;
    this->transition_elapsed_ms_ = 0;
    this->transition_failed_ = false;
    this->phase_elapsed_ms_ = 0;
    this->phase_complete_ = false;
    this->zooming_in_ = true;
    this->select_pan_direction_(!this->pan_forward_);
    this->reset_direct_frame_cache_();
    this->last_loop_ms_ = millis();
    return true;
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
  return this->transition_prepared_ || this->transition_state_ != TransitionState::NONE;
}

bool LvglImagePresenter::transition_failed() const { return this->transition_failed_.load(std::memory_order_acquire); }

void LvglImagePresenter::log_memory_usage(const char *phase) const {
  ESP_LOGI(TAG, "%s memory=0K backend=%s", phase == nullptr ? "runtime" : phase,
           this->direct_session_active_ ? "direct" : "lvgl");
}

void LvglImagePresenter::update_transition_(uint32_t delta_ms) {
  if (this->transition_state_ == TransitionState::CROSS_FADING)
    return;

  const uint32_t half_duration = std::max<uint32_t>(1, this->transition_duration_ms_ / 2);
  this->transition_elapsed_ms_ = std::min(half_duration, this->transition_elapsed_ms_ + delta_ms);
  const float linear = static_cast<float>(this->transition_elapsed_ms_) / static_cast<float>(half_duration);
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
  this->source_ = this->transition_source_;
  lv_image_set_src(this->obj_, this->source_->get_lv_image_dsc());
  lv_unlock();

  this->phase_elapsed_ms_ = 0;
  this->phase_complete_ = !this->motion_enabled_;
  this->zooming_in_ = true;
  this->select_pan_direction_(!this->pan_forward_);
  if (this->motion_enabled_)
    this->update_transform_(0);
}

bool LvglImagePresenter::start_crossfade_(image::Image *source, uint32_t duration_ms) {
  if (!this->transition_prepared_ || source == nullptr || !this->is_widget_visible_())
    return false;

  const lv_image_dsc_t *next = source->get_lv_image_dsc();
  if (next == nullptr || next->data == nullptr || next->header.w < 2 || next->header.h < 2) {
    return false;
  }
#if defined(USE_ESP32_VARIANT_ESP32P4) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  if (!this->crossfade_worker_ready_.load(std::memory_order_acquire) || this->crossfade_worker_handle_ == nullptr ||
      this->crossfade_worker_active_.load(std::memory_order_acquire) ||
      this->crossfade_worker_run_.load(std::memory_order_acquire)) {
    return false;
  }

  this->transition_source_ = source;
  this->transition_duration_ms_ = std::clamp<uint32_t>(duration_ms, 200, 3000);
  this->transition_elapsed_ms_ = 0;
  this->transition_failed_ = false;
  this->crossfade_worker_complete_.store(false, std::memory_order_release);
  this->crossfade_worker_success_.store(false, std::memory_order_release);
  this->crossfade_source_.store(source, std::memory_order_release);
  this->crossfade_worker_run_.store(true, std::memory_order_release);
  this->transition_state_ = TransitionState::CROSS_FADING;
  this->last_loop_ms_ = millis();
  xTaskNotifyGive(this->crossfade_worker_handle_);
  return true;
#else
  (void) duration_ms;
  return false;
#endif
}

void LvglImagePresenter::finish_crossfade_() {
  this->source_ = this->transition_source_;
  this->phase_elapsed_ms_ = 0;
  this->phase_complete_ = !this->motion_enabled_;
  this->zooming_in_ = true;
  this->select_pan_direction_(!this->pan_forward_);
  this->cleanup_crossfade_(true);
  this->transition_state_ = TransitionState::NONE;
  this->transition_source_ = nullptr;
  this->transition_elapsed_ms_ = 0;
  if (this->motion_enabled_)
    this->update_transform_(0);
}

void LvglImagePresenter::cleanup_crossfade_(bool show_current_source) {
#if defined(USE_ESP32_VARIANT_ESP32P4) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  this->stop_crossfade_worker_();
#endif
  lv_lock();
  if (show_current_source && this->obj_ != nullptr && lv_obj_is_valid(this->obj_) && this->source_ != nullptr)
    lv_image_set_src(this->obj_, this->source_->get_lv_image_dsc());
  lv_unlock();
  this->transition_prepared_ = false;
  this->previous_dsc_ = {};
}

bool LvglImagePresenter::is_widget_visible_() const {
  if (this->obj_ == nullptr)
    return false;
  lv_lock();
  const bool visible = lv_obj_is_valid(this->obj_) && lv_obj_is_visible(this->obj_) &&
                       lv_obj_get_screen(this->obj_) == lv_display_get_screen_active(lv_obj_get_display(this->obj_));
  lv_unlock();
  return visible;
}

bool LvglImagePresenter::is_widget_full_screen_() const {
  if (this->obj_ == nullptr)
    return false;
  lv_lock();
  bool full_screen = false;
  if (lv_obj_is_valid(this->obj_)) {
    auto *display = lv_obj_get_display(this->obj_);
    lv_obj_update_layout(this->obj_);
    lv_area_t coordinates{};
    lv_obj_get_coords(this->obj_, &coordinates);
    full_screen = display != nullptr && coordinates.x1 == 0 && coordinates.y1 == 0 &&
                  lv_area_get_width(&coordinates) == lv_display_get_horizontal_resolution(display) &&
                  lv_area_get_height(&coordinates) == lv_display_get_vertical_resolution(display);
  }
  lv_unlock();
  return full_screen;
}

bool LvglImagePresenter::ensure_direct_session_() {
  if (this->direct_session_active_)
    return true;
  if (!this->direct_backend_ready_ || this->lvgl_component_ == nullptr)
    return false;
  if (!this->is_widget_full_screen_()) {
    this->disable_direct_backend_("widget must cover the complete display");
    return false;
  }
  const uint32_t now = millis();
  if (now < this->direct_session_retry_after_ms_)
    return false;
  if (!this->lvgl_component_->begin_frame_buffer_presentation(100)) {
    this->direct_session_failures_++;
    this->direct_session_retry_after_ms_ = now + 100;
    if (this->direct_session_failures_ == 1)
      ESP_LOGW(TAG, "Display rejected the direct framebuffer session; retrying");
    if (this->direct_session_failures_ >= 5)
      this->disable_direct_backend_("display repeatedly rejected framebuffer sessions");
    return false;
  }
  this->direct_session_failures_ = 0;
  this->direct_session_active_ = true;
  this->direct_target_width_ = 0;
  this->direct_target_height_ = 0;
  this->reset_direct_frame_cache_();
  return true;
}

bool LvglImagePresenter::present_pending_direct_frame_(uint32_t timeout_ms) {
  if (!this->direct_target_lease_)
    return true;
  const uint32_t started_us = micros();
  const bool presented = this->lvgl_component_ != nullptr &&
                         this->lvgl_component_->present_presentation_frame(&this->direct_target_lease_, timeout_ms);
  if (presented)
    this->record_presented_frame_(micros() - started_us);
  return presented;
}

void LvglImagePresenter::record_presented_frame_(uint32_t elapsed_us) {
  this->presented_frames_.fetch_add(1, std::memory_order_relaxed);
  this->last_present_us_.store(elapsed_us, std::memory_order_relaxed);
  this->present_fps_window_frames_++;
  const uint32_t now = millis();
  const uint32_t elapsed_ms = now - this->present_fps_window_started_ms_;
  if (elapsed_ms >= 2000) {
    this->measured_present_fps_.store(static_cast<float>(this->present_fps_window_frames_) * 1000.0f / elapsed_ms,
                                      std::memory_order_relaxed);
    this->present_fps_window_frames_ = 0;
    this->present_fps_window_started_ms_ = now;
  }
}

void LvglImagePresenter::refresh_continuous_source_() {
  if (!this->continuous_ || this->source_ == nullptr || this->obj_ == nullptr)
    return;
  image::ImageBufferLease source;
  if (!this->source_->acquire_buffer(&source))
    return;
  const uint32_t generation = source.generation;
  this->source_->release_buffer(&source);
  if (generation == 0 || generation == this->last_fallback_source_generation_)
    return;

  if (this->use_direct_ && this->direct_backend_ready_ && this->direct_session_active_) {
    const bool rendered = this->render_direct_frame_(0);
    if (rendered && this->last_direct_source_generation_ == generation)
      this->last_fallback_source_generation_ = generation;
    return;
  }

  this->last_fallback_source_generation_ = generation;
  lv_lock();
  if (lv_obj_is_valid(this->obj_))
    lv_obj_invalidate(this->obj_);
  lv_unlock();
}

bool LvglImagePresenter::stop_direct_session_(uint32_t timeout_ms) {
  this->accept_direct_jpeg_.store(false, std::memory_order_release);
  if (!this->direct_session_active_)
    return true;
  if (this->lvgl_component_ == nullptr)
    return false;

  const uint32_t started = millis();
  do {
    if (this->direct_jpeg_busy_.load(std::memory_order_acquire)) {
      delay(1);
      continue;
    }
    if (!this->present_pending_direct_frame_(std::min<uint32_t>(timeout_ms, 50))) {
      delay(1);
      continue;
    }
    if (this->lvgl_component_->end_frame_buffer_presentation(std::min<uint32_t>(timeout_ms, 50))) {
      this->direct_session_active_ = false;
      this->direct_target_width_ = 0;
      this->direct_target_height_ = 0;
      this->reset_direct_frame_cache_();
      return true;
    }
    delay(1);
  } while (millis() - started < timeout_ms);
  return false;
}

image::JpegFrameResult LvglImagePresenter::consume_jpeg_frame(const uint8_t *data, size_t size) {
#if defined(USE_ESP32_JPEG) && defined(USE_ESP32_VARIANT_ESP32P4)
  if (!this->use_direct_jpeg_ || data == nullptr || size == 0 || this->lvgl_component_ == nullptr ||
      !this->direct_backend_ready_) {
    return image::JpegFrameResult::UNSUPPORTED;
  }
  // A direct scene pause is intentional: the overlay owns the presented
  // frame. Drop camera frames during that short handoff instead of asking the
  // source to allocate and fill a full-screen RGB fallback buffer.
  if (!this->accept_direct_jpeg_.load(std::memory_order_acquire))
    return image::JpegFrameResult::DROPPED;

  if (this->direct_session_deferred_.load(std::memory_order_acquire)) {
    this->direct_jpeg_frame_ready_.store(true, std::memory_order_release);
    return image::JpegFrameResult::DROPPED;
  }

  bool expected = false;
  if (!this->direct_jpeg_busy_.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
    return image::JpegFrameResult::DROPPED;
  struct BusyGuard {
    std::atomic<bool> &busy;
    ~BusyGuard() { this->busy.store(false, std::memory_order_release); }
  } busy_guard{this->direct_jpeg_busy_};

  if (!this->accept_direct_jpeg_.load(std::memory_order_acquire))
    return image::JpegFrameResult::DROPPED;

  esp32_jpeg::PictureInfo info{};
  if (esp32_jpeg::get_info(data, size, &info) != ESP_OK)
    return image::JpegFrameResult::UNSUPPORTED;

  display::FrameBufferLease target;
  if (!this->lvgl_component_->acquire_presentation_frame(&target, BufferWriter::DMA, 20))
    return image::JpegFrameResult::DROPPED;

  auto release_target = [&]() {
    if (target)
      this->lvgl_component_->release_presentation_frame(&target);
  };
  if (target.bitness != display::COLOR_BITNESS_888 || target.color_order == display::COLOR_ORDER_GRB ||
      target.width != info.width || target.height != info.height || target.stride != target.width * 3U ||
      target.size < target.stride * target.height) {
    release_target();
    return image::JpegFrameResult::UNSUPPORTED;
  }

  const esp32_jpeg::DecodeConfig config{
      .output_format = esp32_jpeg::PixelFormat::RGB888,
      .rgb_order = target.color_order == display::COLOR_ORDER_RGB ? esp32_jpeg::RgbElementOrder::RGB
                                                                  : esp32_jpeg::RgbElementOrder::BGR,
      .color_conversion = esp32_jpeg::ColorConversionStandard::BT601,
      .direct_output = true,
      .skip_output_cache_sync = true,
      .timeout_ms = 80,
  };
  size_t written = 0;
  const int64_t started_us = esp_timer_get_time();
  const esp_err_t error = esp32_jpeg::decode(config, data, size, target.data, target.size, &written);
  const uint32_t decode_us = static_cast<uint32_t>(esp_timer_get_time() - started_us);
  this->last_direct_jpeg_decode_us_.store(decode_us, std::memory_order_relaxed);
  if (error != ESP_OK || written < target.stride * target.height) {
    release_target();
    ESP_LOGW(TAG, "Direct JPEG decode failed: %s written=%zu", esp_err_to_name(error), written);
    return image::JpegFrameResult::DROPPED;
  }

  const uint32_t present_started_us = micros();
  if (!this->lvgl_component_->present_presentation_frame(&target, 50)) {
    release_target();
    return image::JpegFrameResult::DROPPED;
  }
  this->direct_jpeg_frames_.fetch_add(1, std::memory_order_relaxed);
  this->direct_jpeg_frame_presented_.store(true, std::memory_order_release);
  this->record_presented_frame_(micros() - present_started_us);
  return image::JpegFrameResult::CONSUMED;
#else
  (void) data;
  (void) size;
  return image::JpegFrameResult::UNSUPPORTED;
#endif
}

void LvglImagePresenter::disable_direct_backend_(const char *reason) {
  if (!this->direct_backend_failed_)
    ESP_LOGW(TAG, "Disabling direct backend: %s", reason);
  this->direct_backend_failed_ = true;
  this->direct_backend_ready_ = false;
}

void LvglImagePresenter::reset_direct_frame_cache_() {
  this->last_direct_source_data_ = nullptr;
  this->last_direct_source_generation_ = 0;
  this->last_direct_crop_x_ = -1;
  this->last_direct_crop_y_ = -1;
  this->last_direct_crop_width_ = -1;
  this->last_direct_crop_height_ = -1;
}

bool LvglImagePresenter::calculate_direct_crop_(const image::ImageBufferLease &source, size_t target_width,
                                                size_t target_height, uint32_t elapsed_ms, int *crop_x, int *crop_y,
                                                int *crop_width, int *crop_height, uint16_t *scale_q4) const {
  if (!source || source.width < 2 || source.height < 2 || target_width < 2 || target_height < 2 ||
      this->phase_duration_ms_ == 0 || crop_x == nullptr || crop_y == nullptr || crop_width == nullptr ||
      crop_height == nullptr || scale_q4 == nullptr) {
    return false;
  }

  const float cover_scale = std::max(static_cast<float>(target_width) / static_cast<float>(source.width),
                                     static_cast<float>(target_height) / static_cast<float>(source.height));
  const float configured_zoom = (static_cast<float>(this->zoom_start_) + static_cast<float>(this->zoom_end_)) /
                                (2.0f * static_cast<float>(LV_SCALE_NONE));
  const int desired_q4 = std::clamp(static_cast<int>(std::lround(cover_scale * configured_zoom * 16.0f)), 1, 255);

  uint64_t best_score = std::numeric_limits<uint64_t>::max();
  int best_q4 = 0;
  int best_width = 0;
  int best_height = 0;
  for (int q4 = 1; q4 <= 255; q4++) {
    const int width =
        static_cast<int>((static_cast<uint64_t>(target_width) * 16U + q4 - 1U) / static_cast<uint32_t>(q4));
    const int height =
        static_cast<int>((static_cast<uint64_t>(target_height) * 16U + q4 - 1U) / static_cast<uint32_t>(q4));
    if (width < 2 || height < 2 || width > source.width || height > source.height ||
        static_cast<size_t>(width) * q4 / 16U != target_width ||
        static_cast<size_t>(height) * q4 / 16U != target_height) {
      continue;
    }
    const uint64_t scale_error = static_cast<uint64_t>(std::abs(q4 - desired_q4));
    const int64_t aspect_delta = static_cast<int64_t>(width) * static_cast<int64_t>(target_height) -
                                 static_cast<int64_t>(height) * static_cast<int64_t>(target_width);
    const uint64_t aspect_error = static_cast<uint64_t>(std::abs(aspect_delta));
    const uint64_t score = scale_error * 1000000000ULL + aspect_error;
    if (score < best_score) {
      best_score = score;
      best_q4 = q4;
      best_width = width;
      best_height = height;
    }
  }
  if (best_q4 == 0)
    return false;

  const float progress =
      std::clamp(static_cast<float>(elapsed_ms) / static_cast<float>(this->phase_duration_ms_), 0.0f, 1.0f);
  const int movable_x = source.width - best_width;
  const int movable_y = source.height - best_height;
  float precise_x = static_cast<float>(movable_x) * 0.5f;
  float precise_y = static_cast<float>(movable_y) * 0.5f;
  if (movable_x >= movable_y && movable_x > 0) {
    precise_x += this->target_x_ * static_cast<float>(movable_x) * (progress - 0.5f);
  } else if (movable_y > 0) {
    precise_y += this->target_y_ * static_cast<float>(movable_y) * (progress - 0.5f);
  }

  *crop_x = std::clamp(static_cast<int>(std::lround(precise_x)), 0, movable_x);
  *crop_y = std::clamp(static_cast<int>(std::lround(precise_y)), 0, movable_y);
  *crop_width = best_width;
  *crop_height = best_height;
  *scale_q4 = static_cast<uint16_t>(best_q4);
  return true;
}

#if defined(USE_ESP32_VARIANT_ESP32P4) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
bool LvglImagePresenter::create_crossfade_worker_() {
  if (this->crossfade_worker_handle_ != nullptr)
    return true;

#if CONFIG_FREERTOS_UNICORE
  constexpr BaseType_t worker_core = tskNO_AFFINITY;
#elif defined(CONFIG_ESP_MAIN_TASK_AFFINITY_CPU0) && CONFIG_ESP_MAIN_TASK_AFFINITY_CPU0
  constexpr BaseType_t worker_core = 1;
#elif defined(CONFIG_ESP_MAIN_TASK_AFFINITY_CPU1) && CONFIG_ESP_MAIN_TASK_AFFINITY_CPU1
  constexpr BaseType_t worker_core = 0;
#else
  constexpr BaseType_t worker_core = 1;
#endif
  constexpr uint32_t worker_stack_size = 8192;
  this->crossfade_worker_stack_ =
      static_cast<StackType_t *>(heap_caps_aligned_alloc(16, worker_stack_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (this->crossfade_worker_stack_ == nullptr) {
    this->crossfade_worker_stack_ =
        static_cast<StackType_t *>(heap_caps_aligned_alloc(16, worker_stack_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (this->crossfade_worker_stack_ == nullptr)
    return false;

  this->crossfade_worker_handle_ = xTaskCreateStaticPinnedToCore(
      &LvglImagePresenter::crossfade_worker_trampoline_, "lvgl_img_fade", worker_stack_size, this, 1,
      this->crossfade_worker_stack_, &this->crossfade_worker_storage_, worker_core);
  if (this->crossfade_worker_handle_ == nullptr) {
    heap_caps_free(this->crossfade_worker_stack_);
    this->crossfade_worker_stack_ = nullptr;
    return false;
  }
  ESP_LOGI(TAG, "Direct scene crossfade worker uses %s stack",
           esp_ptr_external_ram(this->crossfade_worker_stack_) ? "PSRAM" : "internal");
  return true;
}

void LvglImagePresenter::release_crossfade_frame_() {
  if (this->crossfade_frame_ != nullptr)
    heap_caps_free(this->crossfade_frame_);
  this->crossfade_frame_ = nullptr;
  this->crossfade_frame_size_ = 0;
  this->crossfade_frame_allocation_size_ = 0;
}

bool LvglImagePresenter::stop_crossfade_worker_(uint32_t timeout_ms) {
  if (this->crossfade_worker_handle_ != nullptr) {
    this->crossfade_worker_run_.store(false, std::memory_order_release);
    xTaskNotifyGive(this->crossfade_worker_handle_);
    const uint32_t started_ms = millis();
    while (this->crossfade_worker_active_.load(std::memory_order_acquire) && millis() - started_ms < timeout_ms)
      vTaskDelay(1);
    if (this->crossfade_worker_active_.load(std::memory_order_acquire)) {
      ESP_LOGW(TAG, "Direct scene crossfade worker did not stop within %ums", static_cast<unsigned>(timeout_ms));
      return false;
    }
  }

  if (this->crossfade_direct_active_.exchange(false, std::memory_order_acq_rel) && this->lvgl_component_ != nullptr) {
    if (!this->lvgl_component_->end_direct_image_animation(timeout_ms)) {
      if (timeout_ms > 0)
        ESP_LOGW(TAG, "Direct scene crossfade framebuffer handoff is still pending");
      this->crossfade_direct_active_.store(true, std::memory_order_release);
      return false;
    }
  }
  this->release_crossfade_frame_();
  return true;
}

bool LvglImagePresenter::perform_scene_crossfade_(image::Image *source) {
  if (source == nullptr || this->lvgl_component_ == nullptr || this->crossfade_blend_client_ == nullptr ||
      this->obj_ == nullptr || !this->crossfade_worker_run_.load(std::memory_order_acquire)) {
    return false;
  }

  const int width = this->lvgl_component_->get_width();
  const int height = this->lvgl_component_->get_height();
  if (width < 2 || height < 2)
    return false;
  const size_t frame_bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 3U;
  size_t cache_alignment = 64;
  if (esp_cache_get_alignment(MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA, &cache_alignment) != ESP_OK || cache_alignment == 0 ||
      (cache_alignment & (cache_alignment - 1U)) != 0) {
    cache_alignment = 64;
  }
  const size_t allocation_bytes = (frame_bytes + cache_alignment - 1U) & ~(cache_alignment - 1U);
  this->release_crossfade_frame_();
  this->crossfade_frame_ = static_cast<uint8_t *>(
      heap_caps_aligned_alloc(cache_alignment, allocation_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (this->crossfade_frame_ == nullptr) {
    ESP_LOGW(TAG, "Unable to allocate %u-byte direct scene crossfade frame", static_cast<unsigned>(allocation_bytes));
    return false;
  }
  this->crossfade_frame_size_ = frame_bytes;
  this->crossfade_frame_allocation_size_ = allocation_bytes;

  if (!this->crossfade_direct_active_.load(std::memory_order_acquire)) {
    if (!this->lvgl_component_->begin_direct_image_animation(true)) {
      ESP_LOGW(TAG, "Direct scene crossfade could not take display ownership");
      return false;
    }
    this->crossfade_direct_active_.store(true, std::memory_order_release);
  }

  const int64_t prepare_started_us = esp_timer_get_time();
  const uint8_t *visible_old_frame = this->lvgl_component_->direct_get_stable_presented_frame(100);
  if (visible_old_frame == nullptr)
    return false;

  const lv_image_dsc_t *next = source->get_lv_image_dsc();
  bool rendered = false;
  lv_lock();
  if (next != nullptr && next->data != nullptr && lv_obj_is_valid(this->obj_) && lv_obj_is_visible(this->obj_)) {
    auto *display = lv_obj_get_display(this->obj_);
    lv_obj_t *screen = lv_obj_get_screen(this->obj_);
    if (display != nullptr && screen != nullptr && lv_obj_is_valid(screen) &&
        screen == lv_display_get_screen_active(display)) {
      lv_image_set_src(this->obj_, next);
      std::array<bool, MAX_CAPTURE_EXCLUDED_OBJECTS> excluded_was_hidden{};
      for (size_t index = 0; index < this->capture_excluded_obj_count_; index++) {
        auto *excluded = this->capture_excluded_objs_[index];
        if (excluded == nullptr || !lv_obj_is_valid(excluded))
          continue;
        excluded_was_hidden[index] = lv_obj_has_flag(excluded, LV_OBJ_FLAG_HIDDEN);
        if (!excluded_was_hidden[index])
          lv_obj_add_flag(excluded, LV_OBJ_FLAG_HIDDEN);
      }
      rendered = this->lvgl_component_->render_display_area_rgb888(display, this->crossfade_frame_, width * 3, 0, 0,
                                                                   width, height);
      for (size_t index = 0; index < this->capture_excluded_obj_count_; index++) {
        auto *excluded = this->capture_excluded_objs_[index];
        if (excluded != nullptr && lv_obj_is_valid(excluded) && !excluded_was_hidden[index])
          lv_obj_clear_flag(excluded, LV_OBJ_FLAG_HIDDEN);
      }
    }
  }
  lv_unlock();
  if (!rendered) {
    ESP_LOGW(TAG, "Unable to render the incoming LVGL scene for crossfade");
    return false;
  }

  if (esp_cache_msync(this->crossfade_frame_, this->crossfade_frame_allocation_size_, ESP_CACHE_MSYNC_FLAG_DIR_C2M) !=
      ESP_OK) {
    ESP_LOGW(TAG, "Unable to synchronize the incoming crossfade scene");
    return false;
  }
  const uint32_t prepare_us = static_cast<uint32_t>(esp_timer_get_time() - prepare_started_us);

  const uint32_t duration_ms = this->transition_duration_ms_;
  const uint32_t started_ms = millis();
  uint32_t frames = 0;
  uint64_t blend_total_us = 0;
  uint32_t blend_max_us = 0;
  bool presented = true;
  while (this->crossfade_worker_run_.load(std::memory_order_acquire)) {
    const uint32_t frame_started_ms = millis();
    const uint32_t elapsed_ms = frame_started_ms - started_ms;
    const float linear = std::min(1.0f, static_cast<float>(elapsed_ms) / static_cast<float>(duration_ms));
    const float eased = linear * linear * (3.0f - 2.0f * linear);
    const uint8_t opacity = static_cast<uint8_t>(std::lround(eased * 255.0f));
    const int64_t blend_started_us = esp_timer_get_time();
    presented = this->lvgl_component_->direct_present_rgb888_crossfade(visible_old_frame, this->crossfade_frame_,
                                                                       opacity, this->crossfade_blend_client_, true);
    const uint32_t blend_us = static_cast<uint32_t>(esp_timer_get_time() - blend_started_us);
    if (!presented)
      break;
    frames++;
    blend_total_us += blend_us;
    blend_max_us = std::max(blend_max_us, blend_us);
    if (linear >= 1.0f)
      break;
    const uint32_t frame_elapsed_ms = millis() - frame_started_ms;
    const uint32_t wait_ms =
        frame_elapsed_ms < this->frame_interval_ms_ ? this->frame_interval_ms_ - frame_elapsed_ms : 1U;
    vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(wait_ms)));
  }

  const bool completed =
      presented && this->crossfade_worker_run_.load(std::memory_order_acquire) && millis() - started_ms >= duration_ms;
  ESP_LOGI(TAG, "scene crossfade: prepare=%uus frames=%u blend_avg=%uus blend_max=%uus wall=%ums success=%s",
           static_cast<unsigned>(prepare_us), static_cast<unsigned>(frames),
           static_cast<unsigned>(blend_total_us / std::max<uint32_t>(1, frames)), static_cast<unsigned>(blend_max_us),
           static_cast<unsigned>(millis() - started_ms), YESNO(completed));
  return completed;
}

void LvglImagePresenter::service_crossfade_completion_() {
  if (!this->crossfade_worker_complete_.exchange(false, std::memory_order_acq_rel))
    return;

  const bool success = this->crossfade_worker_success_.load(std::memory_order_acquire);
  if (!this->stop_crossfade_worker_(0)) {
    this->crossfade_worker_complete_.store(true, std::memory_order_release);
    return;
  }
  if (success) {
    this->finish_crossfade_();
  } else {
    this->transition_failed_.store(true, std::memory_order_release);
    this->source_ = this->transition_source_;
    this->transition_state_ = TransitionState::NONE;
    this->transition_elapsed_ms_ = 0;
    this->cleanup_crossfade_(true);
    lv_lock();
    if (this->obj_ != nullptr && lv_obj_is_valid(this->obj_))
      lv_obj_invalidate(lv_obj_get_screen(this->obj_));
    lv_unlock();
  }
  this->crossfade_source_.store(nullptr, std::memory_order_release);
}

void LvglImagePresenter::crossfade_worker_trampoline_(void *arg) {
  static_cast<LvglImagePresenter *>(arg)->crossfade_worker_();
}

void LvglImagePresenter::crossfade_worker_() {
  this->crossfade_blend_client_ = lvgl::LvglComponent::register_direct_image_blend_client();
  this->crossfade_worker_ready_.store(this->crossfade_blend_client_ != nullptr, std::memory_order_release);
  if (this->crossfade_blend_client_ == nullptr)
    ESP_LOGE(TAG, "Unable to register task-owned PPA blend client for scene crossfade");

  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (!this->crossfade_worker_run_.load(std::memory_order_acquire))
      continue;
    this->crossfade_worker_active_.store(true, std::memory_order_release);
    image::Image *source = this->crossfade_source_.load(std::memory_order_acquire);
    const bool success = this->perform_scene_crossfade_(source);
    this->crossfade_worker_success_.store(success, std::memory_order_release);
    this->crossfade_worker_run_.store(false, std::memory_order_release);
    this->crossfade_worker_active_.store(false, std::memory_order_release);
    this->crossfade_worker_complete_.store(true, std::memory_order_release);
  }
}
#endif

#ifdef USE_ESP32_VARIANT_ESP32P4
bool LvglImagePresenter::sync_dma_source_for_ppa_(const image::ImageBufferLease &source) const {
  if (source.writer != BufferWriter::DMA || !esp_ptr_external_ram(source.data))
    return true;
  const esp_err_t error =
      esp_cache_msync(const_cast<uint8_t *>(source.data), source.size, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
  if (error != ESP_OK)
    ESP_LOGW(TAG, "Source DMA ownership transfer failed: %s", esp_err_to_name(error));
  return error == ESP_OK;
}
#endif

bool LvglImagePresenter::render_direct_frame_(uint32_t elapsed_ms) {
#ifdef USE_ESP32_VARIANT_ESP32P4
  if (this->source_ == nullptr || this->lvgl_component_ == nullptr || this->direct_srm_client_ == nullptr)
    return false;

  if (!this->present_pending_direct_frame_(50))
    return true;

  image::ImageBufferLease source;
  if (!this->source_->acquire_buffer(&source))
    return true;

  auto release_source = [&]() { this->source_->release_buffer(&source); };
  if (source.type != image::IMAGE_TYPE_RGB || source.transparency != image::TRANSPARENCY_OPAQUE ||
      source.stride % 3U != 0 || source.size < source.stride * static_cast<size_t>(source.height)) {
    release_source();
    this->disable_direct_backend_("source must be opaque RGB888");
    return false;
  }

  if (this->direct_target_width_ == 0 || this->direct_target_height_ == 0) {
    if (!this->lvgl_component_->acquire_presentation_frame(&this->direct_target_lease_, BufferWriter::DMA, 50)) {
      release_source();
      return true;
    }
    if (this->direct_target_lease_.bitness != display::COLOR_BITNESS_888 ||
        this->direct_target_lease_.color_order == display::COLOR_ORDER_GRB ||
        this->direct_target_lease_.stride % 3U != 0 ||
        this->direct_target_lease_.size < this->direct_target_lease_.stride * this->direct_target_lease_.height) {
      this->lvgl_component_->release_presentation_frame(&this->direct_target_lease_);
      release_source();
      this->disable_direct_backend_("target must be aligned RGB888 or BGR888");
      return false;
    }
    size_t alignment = 64;
    esp_cache_get_alignment(
        esp_ptr_external_ram(this->direct_target_lease_.data) ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL, &alignment);
    if (alignment == 0 || reinterpret_cast<uintptr_t>(this->direct_target_lease_.data) % alignment != 0 ||
        this->direct_target_lease_.size % alignment != 0) {
      this->lvgl_component_->release_presentation_frame(&this->direct_target_lease_);
      release_source();
      this->disable_direct_backend_("target does not meet PPA cache alignment");
      return false;
    }
    this->direct_target_width_ = this->direct_target_lease_.width;
    this->direct_target_height_ = this->direct_target_lease_.height;
  }

  int crop_x = 0;
  int crop_y = 0;
  int crop_width = 0;
  int crop_height = 0;
  uint16_t scale_q4 = 0;
  if (!this->calculate_direct_crop_(source, this->direct_target_width_, this->direct_target_height_, elapsed_ms,
                                    &crop_x, &crop_y, &crop_width, &crop_height, &scale_q4)) {
    if (this->direct_target_lease_)
      this->lvgl_component_->release_presentation_frame(&this->direct_target_lease_);
    release_source();
    this->disable_direct_backend_("image geometry cannot be represented by PPA");
    return false;
  }

  if (this->last_direct_source_data_ == source.data && this->last_direct_source_generation_ == source.generation &&
      this->last_direct_crop_x_ == crop_x && this->last_direct_crop_y_ == crop_y &&
      this->last_direct_crop_width_ == crop_width && this->last_direct_crop_height_ == crop_height) {
    if (this->direct_target_lease_)
      this->lvgl_component_->release_presentation_frame(&this->direct_target_lease_);
    release_source();
    return true;
  }

  if (!this->direct_target_lease_ &&
      !this->lvgl_component_->acquire_presentation_frame(&this->direct_target_lease_, BufferWriter::DMA, 50)) {
    release_source();
    return true;
  }
  if (this->direct_target_lease_.width != this->direct_target_width_ ||
      this->direct_target_lease_.height != this->direct_target_height_) {
    this->lvgl_component_->release_presentation_frame(&this->direct_target_lease_);
    release_source();
    this->disable_direct_backend_("target geometry changed during presentation");
    return false;
  }
  if (!this->sync_dma_source_for_ppa_(source)) {
    this->lvgl_component_->release_presentation_frame(&this->direct_target_lease_);
    release_source();
    this->disable_direct_backend_("source cache synchronization failed");
    return false;
  }

  ppa_srm_oper_config_t config{};
  config.in.buffer = source.data;
  config.in.pic_w = source.stride / 3U;
  config.in.pic_h = source.height;
  config.in.block_w = crop_width;
  config.in.block_h = crop_height;
  config.in.block_offset_x = crop_x;
  config.in.block_offset_y = crop_y;
  config.in.srm_cm = PPA_SRM_COLOR_MODE_RGB888;
  config.out.buffer = this->direct_target_lease_.data;
  config.out.buffer_size = this->direct_target_lease_.size;
  config.out.pic_w = this->direct_target_lease_.stride / 3U;
  config.out.pic_h = this->direct_target_lease_.height;
  config.out.block_offset_x = 0;
  config.out.block_offset_y = 0;
  config.out.srm_cm = PPA_SRM_COLOR_MODE_RGB888;
  config.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
  config.scale_x = static_cast<float>(scale_q4) / 16.0f;
  config.scale_y = static_cast<float>(scale_q4) / 16.0f;
  config.rgb_swap = this->direct_target_lease_.color_order == display::COLOR_ORDER_RGB;
  config.alpha_update_mode = PPA_ALPHA_NO_CHANGE;
  config.mode = PPA_TRANS_MODE_BLOCKING;

  const uint8_t *source_data = source.data;
  const uint32_t source_generation = source.generation;
  const esp_err_t error = ppa_do_scale_rotate_mirror(this->direct_srm_client_, &config);
  release_source();
  if (error != ESP_OK) {
    this->lvgl_component_->release_presentation_frame(&this->direct_target_lease_);
    ESP_LOGW(TAG, "Direct PPA frame failed: %s", esp_err_to_name(error));
    this->disable_direct_backend_("PPA frame rendering failed");
    return false;
  }

  this->last_direct_source_data_ = source_data;
  this->last_direct_source_generation_ = source_generation;
  this->last_direct_crop_x_ = crop_x;
  this->last_direct_crop_y_ = crop_y;
  this->last_direct_crop_width_ = crop_width;
  this->last_direct_crop_height_ = crop_height;
  this->present_pending_direct_frame_(50);
  return true;
#else
  (void) elapsed_ms;
  return false;
#endif
}

bool LvglImagePresenter::update_direct_frame_(uint32_t elapsed_ms) {
  if (!this->ensure_direct_session_())
    return false;
  const bool rendered = this->render_direct_frame_(elapsed_ms);
  if (!rendered && this->direct_backend_failed_)
    this->stop_direct_session_(600);
  return rendered;
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
  const int32_t viewport_width = parent != nullptr ? lv_obj_get_content_width(parent) : lv_obj_get_width(this->obj_);
  const int32_t viewport_height = parent != nullptr ? lv_obj_get_content_height(parent) : lv_obj_get_height(this->obj_);
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
  const uint32_t scale = static_cast<uint32_t>(
      std::lround(static_cast<float>(start_scale) + static_cast<float>(end_scale - start_scale) * directed));

  const int32_t scaled_width = static_cast<int32_t>((static_cast<int64_t>(source_width) * scale) / LV_SCALE_NONE);
  const int32_t scaled_height = static_cast<int32_t>((static_cast<int64_t>(source_height) * scale) / LV_SCALE_NONE);
  const float offset_x =
      this->target_x_ * static_cast<float>(std::max<int32_t>(0, scaled_width - viewport_width)) * 0.5f * directed;
  const float offset_y =
      this->target_y_ * static_cast<float>(std::max<int32_t>(0, scaled_height - viewport_height)) * 0.5f * directed;
  const int32_t x = (viewport_width - scaled_width) / 2 + static_cast<int32_t>(std::lround(offset_x));
  const int32_t y = (viewport_height - scaled_height) / 2 + static_cast<int32_t>(std::lround(offset_y));

  const int32_t base_x = (viewport_width - source_width) / 2;
  const int32_t base_y = (viewport_height - source_height) / 2;
  const bool geometry_changed =
      source_width != this->geometry_source_width_ || source_height != this->geometry_source_height_ ||
      viewport_width != this->geometry_viewport_width_ || viewport_height != this->geometry_viewport_height_;

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
