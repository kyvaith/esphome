#include "lvgl_direct_snapshot_compositor.h"

#include "lvgl_navigation.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#if LV_USE_SNAPSHOT && LV_USE_IMAGE

namespace esphome::lvgl {

static const char *const TAG = "lvgl.snapshot.direct";

bool LvglDirectSnapshotCompositor::can_use_direct_home_() const {
  if (this->parent_ == nullptr || this->home_views_.empty() || this->parent_->get_width() <= 0)
    return false;

  return std::all_of(this->home_views_.begin(), this->home_views_.end(),
                     [](lv_obj_t *view) { return view != nullptr && lv_obj_get_parent(view) != nullptr; });
}

bool LvglDirectSnapshotCompositor::can_use_direct_application_(LvglApplication *application, int home_index) const {
  return this->application_transitions_enabled_ && !this->home_active_ && !this->application_active_ &&
         application != nullptr && application->get_view() != nullptr && this->parent_ != nullptr &&
         this->parent_->get_width() > 0 && this->parent_->get_width() == this->parent_->get_height() &&
         home_index >= 0 && home_index < static_cast<int>(this->home_views_.size()) &&
         this->home_views_[home_index] != nullptr;
}

bool LvglDirectSnapshotCompositor::ensure_home_transition_source_(int home_index) {
  if (home_index < 0 || home_index >= static_cast<int>(this->home_views_.size()) || this->parent_ == nullptr)
    return false;

  auto *home = this->home_views_[home_index];
  if (lvgl_esphome_snapshot_cache_has_raw_page(home))
    return true;

  // A Home page can fall outside the three-slot raw window after a diagnostic
  // jump or an interrupted prefetch. App transitions must not answer that by
  // allocating a fourth 800x800 RGB888 surface: fragmented PSRAM frequently
  // has less than the required 1.92 MB contiguous even when total free memory
  // is sufficient. Restore the invariant once and keep using the fixed slots.
  const bool prepared = lvgl_esphome_snapshot_cache_tile_window(
      this->home_views_.data(), static_cast<int>(this->home_views_.size()), home_index + 1,
      this->parent_->get_width());
  if (!prepared || !lvgl_esphome_snapshot_cache_has_raw_page(home)) {
    ESP_LOGW(TAG, "Home transition source unavailable page=%d", home_index);
    return false;
  }
  ESP_LOGI(TAG, "Restored Home transition source page=%d from fixed tile window", home_index);
  return true;
}

bool LvglDirectSnapshotCompositor::prepare_home(int page_index) {
  if (!this->can_use_direct_home_() || page_index < 0 || page_index >= static_cast<int>(this->home_views_.size())) {
    this->home_prepared_ = false;
    return false;
  }
  this->home_prepared_ = lvgl_esphome_snapshot_cache_tile_window(
      this->home_views_.data(), static_cast<int>(this->home_views_.size()), page_index + 1, this->parent_->get_width());
  return this->home_prepared_;
}

bool LvglDirectSnapshotCompositor::prepare_applications(const std::vector<LvglApplication *> &applications) {
  bool prepared = true;
  bool needs_black_transition = false;
  for (auto *application : applications) {
    auto *view = application == nullptr ? nullptr : application->get_view();
    if (application != nullptr && application->uses_black_open_transition_snapshot())
      needs_black_transition = true;
    else if (view != nullptr)
      prepared = lvgl_esphome_snapshot_cache_compressed_page(view) && prepared;
  }

  // App JPEG encoding and the transition frame are both full-screen RGB888
  // surfaces. Release the temporary encoder scratch before reserving the
  // transition frame so the two never fragment PSRAM side by side. The DSI
  // source lease is preparation-only and must not remain held on Home.
  lvgl_esphome_snapshot_app_release_work_buffer();
  if (needs_black_transition) {
    prepared = this->ensure_black_application_buffer_() && prepared;
    this->release_external_application_transition_buffer_();
  }
  return prepared;
}

bool LvglDirectSnapshotCompositor::prime_home(int page_index) {
  if (this->home_regions_primed_)
    return true;
  if (this->home_active_ || !this->can_use_direct_home_() || page_index < 0 ||
      page_index >= static_cast<int>(this->home_views_.size()))
    return false;
  this->home_regions_primed_ = this->parent_->direct_regions_pause_start();
  if (!this->home_regions_primed_)
    this->parent_->direct_regions_pause(false, 0);
  return this->home_regions_primed_;
}

void LvglDirectSnapshotCompositor::cancel_home_prime() {
  if (!this->home_regions_primed_)
    return;
  this->parent_->direct_regions_pause(false, 0);
  this->home_regions_primed_ = false;
}

bool LvglDirectSnapshotCompositor::begin_home(int page_index) {
  if (this->home_active_ || page_index < 0 || page_index >= static_cast<int>(this->home_views_.size())) {
    this->cancel_home_prime();
    return false;
  }

  this->widget_fallback_ = false;
  if (!this->can_use_direct_home_()) {
    this->cancel_home_prime();
    this->widget_fallback_ = true;
    return LvglSnapshotCompositor::begin_home(page_index);
  }

  this->current_index_ = page_index;
  this->target_index_ = page_index;
  this->home_offset_ = 0;
  this->home_active_ = true;
  this->direct_pending_ = true;
  this->direct_active_ = false;
  this->direct_edge_ = false;
  this->direct_neighbor_index_ = -1;
  this->direct_neighbor_origin_ = 0;
  this->gesture_input_shift_ = 0;
  return true;
}

bool LvglDirectSnapshotCompositor::take_over_home(int *page_index, int32_t *offset_x) {
  if (this->widget_fallback_)
    return LvglSnapshotCompositor::take_over_home(page_index, offset_x);
  if (!this->home_active_ || !this->direct_active_)
    return false;

  int current_x = 0;
  int next_x = 0;
  if (!lvgl_esphome_snapshot_swipe_pause(&current_x, &next_x))
    return false;
  // The settle animation may have finished decoding the page beyond its
  // destination while this gesture was taking over. Publish that ready slot
  // now so a chained swipe can rebase onto it without exposing a black page.
  lvgl_esphome_snapshot_cache_complete_tile_prefetch(0);
  this->home_offset_ = current_x;
  this->target_index_ = this->current_index_;
  this->gesture_input_shift_ = 0;
  if (page_index != nullptr)
    *page_index = this->current_index_;
  if (offset_x != nullptr)
    *offset_x = this->direct_edge_ ? current_x * 2 : current_x;
  return true;
}

bool LvglDirectSnapshotCompositor::start_direct_home_(int32_t delta_x) {
  if (!this->direct_pending_ || delta_x == 0)
    return this->direct_active_;

  const int direction = delta_x < 0 ? 1 : -1;
  const int candidate = this->current_index_ + direction;
  const int32_t width = this->parent_->get_width();
  bool started = false;

  if (candidate >= 0 && candidate < static_cast<int>(this->home_views_.size())) {
    this->direct_neighbor_index_ = candidate;
    this->direct_neighbor_origin_ = direction > 0 ? width : -width;
    started = lvgl_esphome_snapshot_swipe_begin(this->home_views_[this->current_index_], this->home_views_[candidate],
                                                width, this->direct_neighbor_origin_, this->home_regions_primed_);
  } else {
    this->direct_edge_ = true;
    started = lvgl_esphome_snapshot_swipe_edge_begin(this->home_views_[this->current_index_], width,
                                                     this->home_regions_primed_);
  }
  // The swipe state either adopted the primed pause or released it while
  // falling back. From this point it owns the direct-region handoff.
  this->home_regions_primed_ = false;

  this->direct_pending_ = false;
  this->direct_active_ = started;
  if (started)
    return true;

  lvgl_esphome_snapshot_swipe_end();
  auto *current = this->home_views_[this->current_index_];
  auto *current_parent = lv_obj_get_parent(current);
  if (current_parent != nullptr) {
    lv_obj_align(current, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(current, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(current_parent);
  }
  if (this->direct_neighbor_index_ >= 0) {
    auto *neighbor = this->home_views_[this->direct_neighbor_index_];
    if (lv_obj_get_parent(neighbor) != nullptr) {
      lv_obj_align(neighbor, LV_ALIGN_CENTER, this->direct_neighbor_origin_, 0);
      lv_obj_add_flag(neighbor, LV_OBJ_FLAG_HIDDEN);
    }
  }
  const int fallback_index = this->current_index_;
  this->direct_edge_ = false;
  this->direct_neighbor_index_ = -1;
  this->direct_neighbor_origin_ = 0;
  this->home_active_ = false;
  this->current_index_ = -1;
  this->target_index_ = -1;
  this->widget_fallback_ = true;
  ESP_LOGW(TAG, "Direct home compositor unavailable; using the LVGL widget backend");
  return LvglSnapshotCompositor::begin_home(fallback_index);
}

bool LvglDirectSnapshotCompositor::rebase_direct_home_(int new_current_index, int direction, int32_t current_x) {
  if (new_current_index < 0 || new_current_index >= static_cast<int>(this->home_views_.size()) || direction == 0)
    return false;
  // Non-blocking: if the predictive JPEG worker has completed, make its raw
  // tile visible before snapshot_swipe_rebase() looks it up. A running worker
  // remains untouched and the next pointer update will retry the rebase.
  lvgl_esphome_snapshot_cache_complete_tile_prefetch(0);
  const int32_t width = this->parent_->get_width();
  const int candidate = new_current_index + direction;
  const int32_t origin = direction > 0 ? width : -width;
  bool rebased = false;
  if (candidate >= 0 && candidate < static_cast<int>(this->home_views_.size())) {
    rebased = lvgl_esphome_snapshot_swipe_rebase(this->home_views_[new_current_index], this->home_views_[candidate],
                                                 width, origin, current_x);
  } else {
    rebased = lvgl_esphome_snapshot_swipe_rebase_edge(this->home_views_[new_current_index], width, current_x / 2);
  }
  if (!rebased)
    return false;

  this->current_index_ = new_current_index;
  this->target_index_ = new_current_index;
  this->direct_neighbor_index_ = candidate >= 0 && candidate < static_cast<int>(this->home_views_.size()) ? candidate
                                                                                                           : -1;
  this->direct_neighbor_origin_ = this->direct_neighbor_index_ >= 0 ? origin : 0;
  this->direct_edge_ = this->direct_neighbor_index_ < 0;
  this->home_offset_ = this->direct_edge_ ? current_x / 2 : current_x;
  return true;
}

bool LvglDirectSnapshotCompositor::update_home(int32_t delta_x) {
  if (this->widget_fallback_)
    return LvglSnapshotCompositor::update_home(delta_x);
  if (!this->home_active_)
    return false;
  if (this->direct_pending_) {
    this->start_direct_home_(delta_x);
    if (this->widget_fallback_)
      return LvglSnapshotCompositor::update_home(delta_x);
  }
  if (!this->direct_active_)
    return false;

  const int32_t width = this->parent_->get_width();
  int32_t input_x = delta_x - this->gesture_input_shift_;

  while (!this->direct_edge_ && this->direct_neighbor_index_ >= 0 &&
         ((this->direct_neighbor_origin_ > 0 && input_x <= -width) ||
          (this->direct_neighbor_origin_ < 0 && input_x >= width))) {
    const int direction = this->direct_neighbor_origin_ > 0 ? 1 : -1;
    this->gesture_input_shift_ += direction > 0 ? -width : width;
    input_x = delta_x - this->gesture_input_shift_;
    if (!this->rebase_direct_home_(this->direct_neighbor_index_, direction, input_x))
      break;
  }

  const int desired_direction = input_x < 0 ? 1 : (input_x > 0 ? -1 : 0);
  const int active_direction = this->direct_edge_ ? 0 : (this->direct_neighbor_origin_ > 0 ? 1 : -1);
  if (desired_direction != 0 && desired_direction != active_direction)
    this->rebase_direct_home_(this->current_index_, desired_direction, input_x);

  int32_t current_x = std::clamp(input_x, -width, width);
  if (this->direct_edge_)
    current_x /= 2;
  const int32_t next_x = this->direct_edge_ ? 0 : this->direct_neighbor_origin_ + current_x;
  this->home_offset_ = current_x;
  lvgl_esphome_snapshot_swipe_request_update(current_x, next_x);
  return true;
}

bool LvglDirectSnapshotCompositor::settle_home(int target_index, int32_t release_velocity_px_s) {
  if (this->widget_fallback_)
    return LvglSnapshotCompositor::settle_home(target_index, release_velocity_px_s);
  if (!this->home_active_)
    return false;

  if (this->direct_pending_) {
    this->target_index_ = this->current_index_;
    this->reset_direct_home_();
    return true;
  }
  if (!this->direct_active_)
    return false;

  target_index = std::clamp(target_index, 0, static_cast<int>(this->home_views_.size()) - 1);
  const bool commit = !this->direct_edge_ && target_index == this->direct_neighbor_index_;
  this->target_index_ = commit ? target_index : this->current_index_;
  const int32_t current_x = commit ? -this->direct_neighbor_origin_ : 0;
  const int32_t next_x = commit ? 0 : this->direct_neighbor_origin_;
  uint32_t duration = this->home_settle_duration_(current_x, release_velocity_px_s);
  if (this->direct_edge_) {
    const uint32_t width = std::max<int32_t>(1, this->parent_->get_width());
    const uint32_t distance = static_cast<uint32_t>(std::abs(this->home_offset_));
    duration = 240U + std::min<uint32_t>(160U, distance * 320U / width);
  }
  if (commit) {
    // Decode the page beyond the destination while the visible settle still
    // has hundreds of milliseconds left. Starting this only after handoff
    // left a fast follow-up swipe with no raw page-four source.
    lvgl_esphome_snapshot_cache_prefetch_tile_window(this->home_views_.data(),
                                                     static_cast<int>(this->home_views_.size()),
                                                     this->target_index_ + 1, this->parent_->get_width());
  }
  lvgl_esphome_snapshot_swipe_request_finish(current_x, next_x, duration, commit, this->direct_edge_);
  return true;
}

void LvglDirectSnapshotCompositor::cancel_home() {
  this->cancel_home_prime();
  if (this->widget_fallback_) {
    LvglSnapshotCompositor::cancel_home();
    this->widget_fallback_ = false;
    return;
  }
  if (this->direct_pending_ || this->direct_active_)
    lvgl_esphome_snapshot_swipe_end();
  lvgl_esphome_snapshot_cache_complete_tile_prefetch(1000);
  this->reset_direct_home_();
}

bool LvglDirectSnapshotCompositor::open_application(LvglApplication *application, int home_index) {
  this->application_fallback_ = false;
  if (!this->can_use_direct_application_(application, home_index)) {
    if (lvgl_esphome_get_swipe_logging_enabled())
      ESP_LOGI(TAG, "application open uses widget fallback app=%p home=%d", application, home_index);
    this->application_fallback_ = true;
    return LvglSnapshotCompositor::open_application(application, home_index);
  }
  if (!this->ensure_home_transition_source_(home_index)) {
    this->application_fallback_ = true;
    return LvglSnapshotCompositor::open_application(application, home_index);
  }

  const int32_t width = this->parent_->get_width();
  bool started = false;
  if (application->uses_black_open_transition_snapshot()) {
    started = this->ensure_black_application_buffer_() &&
              lvgl_esphome_snapshot_app_open_with_buffer(application->get_view(), this->home_views_[home_index],
                                                         this->black_application_buffer_, width,
                                                         this->application_open_duration_);
  } else if (this->ensure_application_transition_buffer_()) {
    // Reuse the permanently allocated full-screen transition surface as the
    // JPEG decode target. Reallocating a second 800x800 RGB888 buffer after
    // artwork/gallery activity failed once PSRAM's largest free block dropped
    // below 1.92 MB, leaving the compositor active with no animation frames.
    started = lvgl_esphome_snapshot_app_open_cached_with_buffer(
        application->get_view(), this->home_views_[home_index], this->black_application_buffer_, width,
        this->application_open_duration_);
    // The cached-open helper may have written either a JPEG frame or a fresh
    // LVGL snapshot before a later startup check failed. Restore black lazily
    // before the next black transition in either case.
    this->black_application_buffer_is_black_ = false;
  }
  if (!started) {
    if (lvgl_esphome_get_swipe_logging_enabled())
      ESP_LOGI(TAG, "application open direct start failed app=%p home=%d", application, home_index);
    this->application_fallback_ = true;
    return LvglSnapshotCompositor::open_application(application, home_index);
  }

  if (lvgl_esphome_get_swipe_logging_enabled())
    ESP_LOGI(TAG, "application open direct start app=%p home=%d duration=%ums", application, home_index,
             static_cast<unsigned>(this->application_open_duration_));

  this->application_ = application;
  this->application_home_index_ = home_index;
  this->application_opening_ = true;
  this->application_close_committed_ = false;
  this->application_active_ = true;
  this->direct_application_phase_ = DirectApplicationPhase::OPENING;
  return true;
}

bool LvglDirectSnapshotCompositor::begin_application_close(LvglApplication *application, int home_index) {
  this->application_fallback_ = false;
  if (!this->can_use_direct_application_(application, home_index)) {
    this->application_fallback_ = true;
    return LvglSnapshotCompositor::begin_application_close(application, home_index);
  }
  if (!this->ensure_home_transition_source_(home_index)) {
    this->application_fallback_ = true;
    return LvglSnapshotCompositor::begin_application_close(application, home_index);
  }

  const bool prepared = application != nullptr && application->uses_black_close_transition_snapshot()
                            ? this->ensure_black_application_buffer_() &&
                                  lvgl_esphome_snapshot_app_prepare_close_with_buffer(
                                      application->get_view(), this->black_application_buffer_)
                            : application != nullptr &&
                                  lvgl_esphome_snapshot_app_prepare_close(application->get_view());
  if (!prepared) {
    this->application_fallback_ = true;
    return LvglSnapshotCompositor::begin_application_close(application, home_index);
  }

  this->application_ = application;
  this->application_home_index_ = home_index;
  this->application_opening_ = false;
  this->application_close_committed_ = false;
  this->application_active_ = true;
  this->direct_application_phase_ = DirectApplicationPhase::PREPARED_CLOSE;
  return true;
}

bool LvglDirectSnapshotCompositor::update_application_close(int32_t delta_y) {
  if (this->application_fallback_)
    return LvglSnapshotCompositor::update_application_close(delta_y);
  return this->direct_application_phase_ == DirectApplicationPhase::PREPARED_CLOSE;
}

bool LvglDirectSnapshotCompositor::settle_application_close(bool close) {
  if (this->application_fallback_)
    return LvglSnapshotCompositor::settle_application_close(close);
  if (this->direct_application_phase_ != DirectApplicationPhase::PREPARED_CLOSE || this->application_ == nullptr)
    return false;

  if (!close) {
    auto *application = this->application_;
    lvgl_esphome_snapshot_app_clear_prepared_close();
    lvgl_esphome_snapshot_app_release_work_buffer();
    this->reset_direct_application_();
    if (this->navigation_ != nullptr)
      this->navigation_->complete_application_transition(application, false, false);
    else if (application != nullptr)
      application->call_on_close_cancelled_callbacks();
    return true;
  }

  const int32_t width = this->parent_->get_width();
  const int32_t height = this->parent_->get_height();
  const int32_t target_x = std::lround(width * this->application_close_target_x_);
  const int32_t target_y = std::lround(height * this->application_close_target_y_);
  if (!lvgl_esphome_snapshot_app_close(this->application_->get_view(), this->home_views_[this->application_home_index_],
                                       width, target_x, target_y, this->application_close_duration_)) {
    lvgl_esphome_snapshot_app_cancel();
    lvgl_esphome_snapshot_app_release_work_buffer();
    this->reset_direct_application_();
    return false;
  }

  this->application_close_committed_ = true;
  this->direct_application_phase_ = DirectApplicationPhase::CLOSING;
  return true;
}

void LvglDirectSnapshotCompositor::cancel_application() {
  if (this->application_fallback_) {
    LvglSnapshotCompositor::cancel_application();
    this->application_fallback_ = false;
    return;
  }
  if (this->direct_application_phase_ != DirectApplicationPhase::NONE)
    lvgl_esphome_snapshot_app_cancel();
  lvgl_esphome_snapshot_app_release_work_buffer();
  this->reset_direct_application_();
  this->release_external_application_transition_buffer_();
}

void LvglDirectSnapshotCompositor::loop() {
  if (lvgl_esphome_snapshot_is_active())
    return;

  // A zero-time completion attempt at the end of a swipe can legitimately
  // race the JPEG worker by a few milliseconds. Polling here is non-blocking
  // and makes the completed page visible to the tile window as soon as the
  // worker finishes; otherwise that slot can remain marked busy until another
  // navigation operation happens to service it.
  lvgl_esphome_snapshot_cache_complete_tile_prefetch(0);

  if (this->direct_application_phase_ == DirectApplicationPhase::OPENING ||
      this->direct_application_phase_ == DirectApplicationPhase::CLOSING) {
    this->complete_direct_application_();
  } else if (this->direct_active_) {
    this->complete_direct_home_();
  }
}

void LvglDirectSnapshotCompositor::complete_direct_home_() {
  const int target = this->target_index_;
  const int source = this->current_index_;
  const int direction = target > source ? 1 : (target < source ? -1 : 0);
  // Finalize a completed prediction without waiting for hardware JPEG. A
  // previous decode may still be running, but it only owns the page behind
  // the committed direction and must never hold the final frame or touch loop.
  lvgl_esphome_snapshot_cache_complete_tile_prefetch(0);
  this->reset_direct_home_();
  if (target >= 0) {
    // settle_home() already moved the adjacent tile window asynchronously.
    // Re-running prepare_home() here encoded every dirty full-screen tile on
    // the LVGL loop (hundreds of milliseconds) exactly when the user was most
    // likely to begin the next gesture. The completed prefetch is the prepared
    // window; dirty outgoing pages are encoded only when their slot is reused.
    this->home_prepared_ = true;
    lv_obj_align(this->home_views_[target], LV_ALIGN_CENTER, 0, 0);
    if (this->navigation_ != nullptr) {
      this->navigation_->activate_home_view(target, true);
    } else {
      lv_obj_remove_flag(this->home_views_[target], LV_OBJ_FLAG_HIDDEN);
      lvgl_esphome_snapshot_discard_pending_refresh();
    }
    if (lv_obj_has_flag(this->home_views_[target], LV_OBJ_FLAG_HIDDEN)) {
      ESP_LOGW(TAG, "Home handoff left target %d hidden; recovering visibility", target);
      lv_obj_remove_flag(this->home_views_[target], LV_OBJ_FLAG_HIDDEN);
    }
    if (direction != 0) {
      // Keep the three reusable raw slots symmetric around the committed page:
      // previous/current/next. Directional bias evicted page N-1 immediately
      // after a forward settle, so an equally common reverse gesture had to
      // wait for a full-screen JPEG decode and could expose an empty native
      // page. The symmetric window makes both directions immediately usable;
      // edge pages naturally consume only two slots.
      lvgl_esphome_snapshot_cache_prefetch_tile_window(this->home_views_.data(),
                                                       static_cast<int>(this->home_views_.size()), target + 1,
                                                       this->parent_->get_width());
    }
  }
}

void LvglDirectSnapshotCompositor::complete_direct_application_() {
  auto *application = this->application_;
  const int home_index = this->application_home_index_;
  const bool opening = this->direct_application_phase_ == DirectApplicationPhase::OPENING;
  if (this->navigation_ != nullptr) {
    this->navigation_->prepare_application_transition(application, opening, !opening);
    if (!opening)
      this->navigation_->activate_home_view(home_index);
  }

  lvgl_esphome_snapshot_app_release_open_hold();
  lvgl_esphome_snapshot_app_release_work_buffer();

  if (this->navigation_ != nullptr)
    this->navigation_->complete_application_transition(application, opening, !opening);
  // The transition source is only needed while the animation is running. It
  // is a real DSI framebuffer, so retaining its lease would permanently
  // remove one of the three scanout buffers from the normal LVGL pool. That
  // leaves later native refreshes with no idle target and causes frame-boundary
  // timeouts after a few application transitions.
  this->release_external_application_transition_buffer_();
  this->reset_direct_application_();
  if (!opening && home_index >= 0) {
    if (!lvgl_esphome_snapshot_tile_buffers_ready()) {
      // A memory-intensive app may return idle neighbour slots to PSRAM.
      // Its close callback releases image buffers before we restore the Home
      // working set; ordinary app closes keep the allocation-free fast path.
      this->prepare_home(home_index);
      return;
    }
    // The close animation reads its background from the retained Home tile
    // window, so those raw slots are already ready for the next gesture.
    // prepare_home() also flushes every dirty slot to JPEG and used to block
    // the LVGL loop for several hundred milliseconds immediately after close.
    // Let the normal window-prefetch worker encode a slot only when it is
    // actually recycled for a page outside the current three-page window.
    this->home_prepared_ = true;
  }
}

void LvglDirectSnapshotCompositor::reset_direct_home_() {
  this->direct_pending_ = false;
  this->direct_active_ = false;
  this->direct_edge_ = false;
  this->direct_neighbor_index_ = -1;
  this->direct_neighbor_origin_ = 0;
  this->gesture_input_shift_ = 0;
  this->home_active_ = false;
  this->current_index_ = -1;
  this->target_index_ = -1;
  this->home_offset_ = 0;
  this->widget_fallback_ = false;
}

void LvglDirectSnapshotCompositor::reset_direct_application_() {
  this->application_ = nullptr;
  this->application_buffer_ = nullptr;
  this->application_home_index_ = -1;
  this->application_progress_ = 0;
  this->application_active_ = false;
  this->application_opening_ = false;
  this->application_close_committed_ = false;
  this->direct_application_phase_ = DirectApplicationPhase::NONE;
  this->application_fallback_ = false;
}

}  // namespace esphome::lvgl

#endif
