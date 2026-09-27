#include "lvgl_material.h"

#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace esphome::lvgl_material {

static const char *const TAG = "lvgl_material.weather";

void MaterialWeatherPresenter::add_animation(const std::string &key, lv_obj_t *widget) {
  this->animations_.push_back({key, widget, nullptr});
}

void MaterialWeatherPresenter::setup() {
  if (this->root_ == nullptr || this->temperature_label_ == nullptr || this->condition_label_ == nullptr ||
      this->detail_label_ == nullptr || this->animations_.empty()) {
    ESP_LOGE(TAG, "Incomplete weather presenter binding");
    this->mark_failed();
    return;
  }

  for (const auto &animation : this->animations_) {
    if (animation.widget == nullptr) {
      ESP_LOGE(TAG, "Animation '%s' has no widget", animation.key.c_str());
      this->mark_failed();
      return;
    }
  }
  this->ensure_animation_bindings_();
  if (this->interaction_widget_ != nullptr && this->interaction_state_layer_ == nullptr) {
    this->interaction_layer_ = lv_obj_create(this->interaction_widget_);
    lv_obj_remove_style_all(this->interaction_layer_);
    lv_obj_set_style_bg_color(this->interaction_layer_, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(this->interaction_layer_, static_cast<lv_opa_t>(31), LV_PART_MAIN);
    lv_obj_set_style_border_width(this->interaction_layer_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(this->interaction_layer_, 0, LV_PART_MAIN);
    lv_obj_add_flag(this->interaction_layer_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(this->interaction_layer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(this->interaction_layer_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(this->interaction_layer_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(this->interaction_widget_, interaction_event_cb_, LV_EVENT_ALL, this);
  } else if (this->interaction_widget_ != nullptr) {
    lv_obj_add_event_cb(this->interaction_widget_, interaction_event_cb_, LV_EVENT_ALL, this);
  }
  // The Home root is constructed before the boot overlay is dismissed. Keep
  // the weather task prepared but inert until navigation presents page one.
  this->apply_animation_visibility_(false);
}

void MaterialWeatherPresenter::loop() {
  if (!this->ensure_animation_bindings_())
    return;
  const uint32_t now = millis();
  // Application transitions can start independently of a Home swipe (for
  // example, immediately after a previous application closes). Stop the
  // direct weather producer as soon as the full-screen compositor takes
  // ownership, so its one-shot completion cannot race the transition DMA.
  if (::lvgl_esphome_snapshot_is_active() || (!this->page_visible_ && !this->navigation_suspended_))
    this->suspend_for_navigation();
  if (this->navigation_resume_pending_ && static_cast<int32_t>(now - this->navigation_resume_at_) >= 0) {
    this->navigation_resume_pending_ = false;
    this->navigation_suspended_ = false;
    this->animation_visible_ = false;
    this->animation_completion_reported_ = false;
    this->apply_animation_visibility_(this->page_visible_ && this->is_root_presented_());
  }
  // A direct pressed-state frame completes asynchronously. Service its
  // restore every loop so a quick tap cannot leave the pressed frame pending
  // behind the presenter's slower visibility/animation polling interval.
  if (this->interaction_release_pending_) {
    if (!this->interaction_restore_submitted_)
      this->interaction_restore_submitted_ = this->interaction_state_layer_->release();
    if (this->interaction_restore_submitted_ && this->interaction_state_layer_->is_idle())
      this->finish_interaction_release_();
  }
  if (now - this->last_visibility_check_ < 200U)
    return;
  this->last_visibility_check_ = now;
  if (!this->interaction_pressed_ && !this->interaction_release_pending_)
    this->apply_animation_visibility_(this->is_root_presented_());
  if (this->animation_visible_ && !this->animation_completion_reported_) {
    const auto *selected = this->find_animation_(this->selected_animation_);
    if (selected != nullptr && lvgl::lottie_is_complete(selected->context)) {
      this->animation_completion_reported_ = true;
      this->request_snapshot_refresh_();
      this->animation_complete_callbacks_.call();
    }
  }
  if (this->snapshot_refresh_pending_ && this->page_visible_ && this->is_root_presented_() &&
      !this->navigation_suspended_ && !this->navigation_resume_pending_ &&
      !this->interaction_pressed_ && !this->interaction_release_pending_ &&
      static_cast<int32_t>(now - this->snapshot_refresh_at_) >= 0) {
    const auto *selected = this->find_animation_(this->selected_animation_);
    const bool animation_ready = !this->animation_visible_ || selected == nullptr || selected->context == nullptr ||
                                 (selected->context->pixel_buffer != nullptr &&
                                  selected->context->prepared_frame_ready);
    if (animation_ready)
      this->refresh_snapshot_now(true);
  }
}

void MaterialWeatherPresenter::on_shutdown() {
  if (this->interaction_widget_ != nullptr && lv_obj_is_valid(this->interaction_widget_))
    lv_obj_remove_event_cb_with_user_data(this->interaction_widget_, interaction_event_cb_, this);
  for (auto &animation : this->animations_)
    lvgl::lottie_hide(animation.context);
  this->animation_visible_ = false;
}

void MaterialWeatherPresenter::interaction_event_cb_(lv_event_t *event) {
  auto *presenter = static_cast<MaterialWeatherPresenter *>(lv_event_get_user_data(event));
  if (presenter == nullptr)
    return;
  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_PRESSED) {
    presenter->set_interaction_pressed_(true);
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    presenter->set_interaction_pressed_(false);
  }
}

void MaterialWeatherPresenter::set_interaction_pressed_(bool pressed) {
  if (pressed == this->interaction_pressed_ || this->interaction_release_pending_ ||
      (this->interaction_layer_ == nullptr && this->interaction_state_layer_ == nullptr))
    return;

  auto *selected = this->find_animation_(this->selected_animation_);
  if (pressed) {
    if (selected == nullptr || selected->context == nullptr)
      return;
    this->interaction_context_ = selected->context;
    this->interaction_pressed_at_ = millis();
    lvgl::lottie_pause(this->interaction_context_, false);
    // The Lottie task and the pressed state both own the same direct display
    // region. Never publish the state layer while a frame can still complete
    // underneath it: that produced the short horizontal bands seen on taps.
    // A busy renderer may skip one cosmetic pressed frame, but the click still
    // reaches the widget and the direct scene remains coherent.
    if (!lvgl::lottie_wait_direct_frame_buffer(this->interaction_context_, 50)) {
      lvgl::lottie_show(this->interaction_context_, false);
      this->interaction_context_ = nullptr;
      return;
    }
    if (!lvgl_esphome_wait_for_direct_frame_presented(80)) {
      lvgl::lottie_show(this->interaction_context_, false);
      this->interaction_context_ = nullptr;
      return;
    }
    lvgl::lottie_release_direct_region(this->interaction_context_);
    if (this->interaction_state_layer_ != nullptr) {
      if (!this->interaction_state_layer_->press(this->interaction_widget_)) {
        lvgl::lottie_show(this->interaction_context_, false);
        this->interaction_context_ = nullptr;
        return;
      }
      this->interaction_pressed_ = true;
      return;
    }
    this->sync_interaction_layer_();
    lv_obj_clear_flag(this->interaction_layer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(this->interaction_layer_);
    lv_obj_invalidate(this->interaction_widget_);
    this->interaction_pressed_ = true;
    return;
  }

  if (this->interaction_state_layer_ != nullptr) {
    this->interaction_release_pending_ = true;
    this->interaction_restore_submitted_ = this->interaction_state_layer_->release();
    return;
  }
  lv_obj_add_flag(this->interaction_layer_, LV_OBJ_FLAG_HIDDEN);
  this->finish_interaction_release_();
  lv_obj_invalidate(this->interaction_widget_);
}

void MaterialWeatherPresenter::finish_interaction_release_() {
  if (this->interaction_context_ != nullptr) {
    const uint32_t paused_ms = millis() - this->interaction_pressed_at_;
    this->interaction_context_->start_time_us += static_cast<int64_t>(paused_ms) * 1000;
    lvgl::lottie_show(this->interaction_context_, false);
  }
  this->interaction_context_ = nullptr;
  this->interaction_pressed_ = false;
  this->interaction_release_pending_ = false;
  this->interaction_restore_submitted_ = false;
}

void MaterialWeatherPresenter::sync_interaction_layer_() {
  const auto *selected = this->find_animation_(this->selected_animation_);
  if (selected == nullptr || selected->widget == nullptr || this->interaction_widget_ == nullptr)
    return;
  lv_area_t animation_area{};
  lv_area_t interaction_area{};
  lv_obj_get_coords(selected->widget, &animation_area);
  lv_obj_get_coords(this->interaction_widget_, &interaction_area);
  lv_obj_set_pos(this->interaction_layer_, animation_area.x1 - interaction_area.x1,
                 animation_area.y1 - interaction_area.y1);
  lv_obj_set_size(this->interaction_layer_, lv_area_get_width(&animation_area), lv_area_get_height(&animation_area));
}

void MaterialWeatherPresenter::dump_config() {
  ESP_LOGCONFIG(TAG, "Material Weather Presenter:");
  ESP_LOGCONFIG(TAG, "  Animation variants: %u", static_cast<unsigned>(this->animations_.size()));
}

void MaterialWeatherPresenter::update(const std::string &condition, bool is_day, float temperature,
                                      float apparent_temperature, float humidity) {
  const std::string normalized = normalize_condition_(condition);
  const std::string selected = this->select_animation_(normalized, is_day);
  const bool first_update = this->selected_animation_.empty();

  char temperature_text[20];
  char detail_text[48];
  if (std::isfinite(temperature)) {
    // The unit is a separate LVGL label so the C can use the design's
    // superscript position without changing the numeric label width.
    std::snprintf(temperature_text, sizeof(temperature_text), "%.0f", static_cast<double>(temperature));
  } else {
    std::snprintf(temperature_text, sizeof(temperature_text), "--");
  }
  if (std::isfinite(apparent_temperature) && std::isfinite(humidity)) {
    std::snprintf(detail_text, sizeof(detail_text), "Feels %.0f C  |  %.0f%%",
                  static_cast<double>(apparent_temperature), static_cast<double>(humidity));
  } else if (std::isfinite(humidity)) {
    std::snprintf(detail_text, sizeof(detail_text), "Humidity %.0f%%", static_cast<double>(humidity));
  } else {
    std::snprintf(detail_text, sizeof(detail_text), "Home weather");
  }

  bool labels_changed = false;
  lv_lock();
  if (std::string(lv_label_get_text(this->temperature_label_)) != temperature_text) {
    lv_label_set_text(this->temperature_label_, temperature_text);
    labels_changed = true;
  }
  const std::string display_condition = display_condition_(normalized);
  if (std::string(lv_label_get_text(this->condition_label_)) != display_condition) {
    lv_label_set_text(this->condition_label_, display_condition.c_str());
    labels_changed = true;
  }
  if (std::string(lv_label_get_text(this->detail_label_)) != detail_text) {
    lv_label_set_text(this->detail_label_, detail_text);
    labels_changed = true;
  }
  lv_unlock();

  const bool selection_changed = selected != this->selected_animation_;
  if (selection_changed) {
    this->selected_animation_ = selected;
    this->animation_visible_ = false;
    // Release the previous condition before allocating the new one. The
    // order must not depend on where either binding sits in the YAML list.
    for (auto &animation : this->animations_) {
      if (animation.key != selected)
        lvgl::lottie_unload(animation.context);
    }
  }
  // Warm only the selected variant while the Home page is still covered by
  // boot/navigation. The presenter keeps it hidden until ThorVG has produced
  // a complete first frame; exposing an allocated but empty canvas is what
  // caused the short black rectangle and made a one-shot appear to start at
  // its retained final frame.
  auto *selected_binding = this->find_animation_(this->selected_animation_);
  // Do not re-hide an already allocated animation on every HA sensor update.
  // Duplicate state publications otherwise interrupt the live frame and make
  // the regional compositor compete with a needless lifecycle transition.
  if ((first_update || this->page_visible_) && !this->navigation_suspended_ &&
      selected_binding != nullptr && selected_binding->context != nullptr &&
      selected_binding->context->pixel_buffer == nullptr) {
    lv_lock();
    lvgl::lottie_prepare(selected_binding->context);
    lv_unlock();
  }
  if (first_update || labels_changed || selection_changed)
    this->request_snapshot_refresh_();
  // HA state can arrive during boot. Updating the labels is safe, but the
  // finite animation must wait for the post-boot Home presentation callback.
  this->apply_animation_visibility_(this->page_visible_ && this->is_root_presented_());
}

void MaterialWeatherPresenter::restart_on_wake() {
  if (!this->page_visible_ || this->navigation_suspended_ || !this->is_root_presented_())
    return;
  auto *selected = this->find_animation_(this->selected_animation_);
  if (selected == nullptr || selected->context == nullptr)
    return;
  lv_lock();
  lvgl::lottie_restart(selected->context);
  lv_unlock();
  this->animation_visible_ = false;
  this->animation_completion_reported_ = false;
  this->apply_animation_visibility_(true);
}

void MaterialWeatherPresenter::suspend_for_navigation() {
  this->navigation_resume_pending_ = false;
  if (this->navigation_suspended_)
    return;
  this->navigation_suspended_ = true;
  this->navigation_suspended_at_ = millis();
  const auto *selected = this->find_animation_(this->selected_animation_);
  if (selected != nullptr && selected->context != nullptr) {
    // The Lottie task owns and drains its regional lease. Navigation already
    // owns the full-screen transition, so do not enqueue an extra LVGL
    // invalidation for the hidden canvas; that invalidation competed with the
    // first carousel frame and could expose a transient empty rectangle.
    lvgl::lottie_pause_for_navigation(selected->context);
  }
}

void MaterialWeatherPresenter::release_for_application() {
  this->navigation_resume_pending_ = false;
  this->navigation_suspended_at_ = 0;
  this->navigation_suspended_ = true;
  this->page_visible_ = false;
  this->application_suspended_ = true;
  this->animation_visible_ = false;
  this->animation_completion_reported_ = false;
  this->applied_animation_.clear();

  // Only the selected variant can own the large live pixel/work buffers;
  // non-selected variants are already released by apply_animation_visibility_.
  auto *selected = this->find_animation_(this->selected_animation_);
  if (selected == nullptr || selected->context == nullptr)
    return;
  // Resource release alone is insufficient when the animation was promoted
  // to the display top layer. Hide every weather widget before handing the
  // full-screen surface to an application, otherwise its last canvas can
  // cover the application's text until the next LVGL refresh.
  for (auto &animation : this->animations_) {
    if (animation.context != nullptr)
      lvgl::lottie_hide(animation.context);
    if (animation.widget != nullptr && lv_obj_is_valid(animation.widget))
      lv_obj_add_flag(animation.widget, LV_OBJ_FLAG_HIDDEN);
  }
  // lottie_hide() stops the worker and releases its buffers. Do not call it
  // while holding lv_lock(): the worker may be waiting for the same lock at
  // its publication boundary. Release the parsed ThorVG scene separately,
  // after every worker has been stopped.
  for (auto &animation : this->animations_)
    lvgl::lottie_release_scene(animation.context);
  ESP_LOGI(TAG, "Released selected weather animation for application handoff free=%uK largest=%uK",
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
           static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024));
}

void MaterialWeatherPresenter::resume_after_navigation(bool page_visible) {
  // Home presentation callbacks can arrive while an application transition
  // is still in flight. They must not restart the weather worker underneath
  // the application; only resume_after_application() may clear this guard.
  if (page_visible && this->application_suspended_)
    return;
  const bool was_page_visible = this->page_visible_;
  this->page_visible_ = page_visible;
  if (!page_visible) {
    auto *selected = this->find_animation_(this->selected_animation_);
    const bool retain_frame = selected != nullptr && selected->context != nullptr &&
                              selected->context->pixel_buffer != nullptr && selected->context->prepared_frame_ready;
    this->navigation_resume_pending_ = false;
    // A Home swipe only hides the current page temporarily. Keep the
    // prepared canvas suspended so the matching resume can reveal that exact
    // frame instead of restarting through an empty direct region. Application
    // handoff releases this buffer first, so it still follows the normal
    // allocation path.
    this->navigation_suspended_ = retain_frame;
    this->animation_visible_ = false;
    this->animation_completion_reported_ = false;
    this->apply_animation_visibility_(false);
    return;
  }

  // Navigation emits both a touch-end and a presented callback. Treat only
  // the edge from hidden to visible as a new one-shot presentation. Without
  // this edge detection a retained completed frame can absorb the second
  // callback and the weather page reappears frozen on its last frame.
  const bool entering_page = !was_page_visible;
  if (entering_page) {
    this->animation_visible_ = false;
    this->animation_completion_reported_ = false;
  }

  // A duplicate resume(true) must never cancel a delayed resume that was
  // scheduled by the first presentation callback.
  auto *selected = this->find_animation_(this->selected_animation_);
  const bool retained_frame = selected != nullptr && selected->context != nullptr &&
                              selected->context->pixel_buffer != nullptr && selected->context->prepared_frame_ready;
  if (retained_frame && this->navigation_suspended_ && !this->direct_overlay_suspended_ &&
      this->is_root_presented_()) {
    // A carousel return already owns a complete retained canvas. Reveal it
    // immediately; the allocation delay is only needed after resource release.
    this->navigation_suspended_ = false;
    this->navigation_resume_pending_ = false;
    lvgl::lottie_reanchor_retained_frame(selected->context);
    this->navigation_suspended_at_ = 0;
    // The retained canvas is only a visual bridge for the handoff. It must
    // not become the permanent state of a one-shot weather animation: boot
    // snapshot preparation leaves the worker on its final frame, and showing
    // it with restart=false makes Home look frozen. Mark the animation as
    // hidden for the common path so lottie_show() restarts from frame one
    // while keeping the retained canvas in place until the first new frame is
    // accepted by the direct compositor.
    this->animation_visible_ = false;
    this->animation_completion_reported_ = false;
    this->apply_animation_visibility_(true);
    return;
  }
  if (!entering_page && this->navigation_resume_pending_)
    return;
  if (!this->navigation_suspended_ && !this->navigation_resume_pending_) {
    this->apply_animation_visibility_(this->is_root_presented_());
    return;
  }
  if (this->navigation_resume_pending_)
    return;

  this->navigation_suspended_ = true;
  this->navigation_resume_pending_ = true;
  this->navigation_resume_at_ = millis() + this->resume_delay_ms_;
}

void MaterialWeatherPresenter::resume_after_application(bool page_visible) {
  this->application_suspended_ = false;
  this->resume_after_navigation(page_visible);
}

void MaterialWeatherPresenter::log_state(const char *reason) {
  auto *selected = this->find_animation_(this->selected_animation_);
  auto *context = selected != nullptr ? selected->context : nullptr;
  bool root_visible = false;
  bool root_presented = false;
  if (this->root_ != nullptr && lv_obj_is_valid(this->root_)) {
    lv_lock();
    root_visible = lv_obj_is_visible(this->root_);
    root_presented = root_visible && lv_obj_get_screen(this->root_) == lv_screen_active();
    lv_unlock();
  }
  ESP_LOGI(TAG,
           "state reason=%s page=%u root_visible=%u root_presented=%u nav_suspended=%u nav_pending=%u "
           "animation_visible=%u completion_reported=%u selected=%s applied=%s context=%p prepared=%u auto_start=%u "
           "runtime_hidden=%u complete=%u on_top=%u frame_ready=%u overlay=%u app=%u pressed=%u release=%u "
           "worker=%p stopping=%u mailbox=%u",
           reason != nullptr ? reason : "manual", this->page_visible_, root_visible, root_presented,
           this->navigation_suspended_, this->navigation_resume_pending_, this->animation_visible_,
           this->animation_completion_reported_, this->selected_animation_.c_str(), this->applied_animation_.c_str(),
           context, context != nullptr && context->pixel_buffer != nullptr, context != nullptr && context->auto_start,
           context != nullptr && context->runtime_hidden, context != nullptr && lvgl::lottie_is_complete(context),
           selected != nullptr && selected->on_top_layer, context != nullptr && context->prepared_frame_ready,
           this->direct_overlay_suspended_, this->application_suspended_, this->interaction_pressed_,
           this->interaction_release_pending_, context != nullptr ? context->task_handle : nullptr,
           context != nullptr && context->stop_requested, context != nullptr && context->native_publish_pending);
}

bool MaterialWeatherPresenter::suspend_for_direct_overlay() {
  if (this->direct_overlay_suspended_)
    return true;

  auto *selected = this->find_animation_(this->selected_animation_);
  this->direct_overlay_suspended_ = true;
  this->direct_overlay_resume_animation_ =
      this->animation_visible_ && selected != nullptr && selected->context != nullptr &&
      !selected->context->runtime_hidden && selected->context->auto_start;
  this->direct_overlay_suspended_at_ = millis();
  if (!this->direct_overlay_resume_animation_)
    return true;

  lvgl::lottie_pause(selected->context, true);
  if (!lvgl::lottie_wait_direct_frame_buffer(selected->context, 120)) {
    this->direct_overlay_suspended_ = false;
    this->direct_overlay_resume_animation_ = false;
    lvgl::lottie_show(selected->context, false);
    return false;
  }
  lvgl::lottie_release_direct_region(selected->context);
  return true;
}

void MaterialWeatherPresenter::resume_after_direct_overlay() {
  if (!this->direct_overlay_suspended_)
    return;

  const bool resume_animation = this->direct_overlay_resume_animation_;
  const uint32_t paused_ms = millis() - this->direct_overlay_suspended_at_;
  this->direct_overlay_suspended_ = false;
  this->direct_overlay_resume_animation_ = false;
  auto *selected = this->find_animation_(this->selected_animation_);
  if (!resume_animation || selected == nullptr || selected->context == nullptr || this->navigation_suspended_ ||
      !this->is_root_presented_()) {
    return;
  }
  selected->context->start_time_us += static_cast<int64_t>(paused_ms) * 1000;
  lvgl::lottie_show(selected->context, false);
}

bool MaterialWeatherPresenter::is_animation_complete() {
  auto *selected = this->find_animation_(this->selected_animation_);
  return selected == nullptr || selected->context == nullptr || lvgl::lottie_is_complete(selected->context);
}

bool MaterialWeatherPresenter::prepare_snapshot_frame(uint32_t timeout_ms) {
  auto *selected = this->find_animation_(this->selected_animation_);
  if (selected == nullptr || selected->context == nullptr)
    return false;
  auto *context = selected->context;
  this->navigation_suspended_ = true;
  this->navigation_resume_pending_ = false;
  if (!context->prepared_frame_ready && context->pixel_buffer == nullptr) {
    lv_lock();
    lvgl::lottie_prepare(context);
    lv_unlock();
  }
  // The initial render is asynchronous. Let the worker publish a complete
  // canvas in short slices instead of returning immediately and letting the
  // boot snapshot capture the pre-weather page.
  const uint32_t prepare_started = millis();
  while (!context->prepared_frame_ready && millis() - prepare_started <= timeout_ms)
    vTaskDelay(1);
  if (!context->prepared_frame_ready)
    return false;
  lvgl::lottie_pause_for_navigation(context);
  if (!lvgl::lottie_wait_direct_frame_buffer(context, timeout_ms))
    return false;
  // The prepared frame is copied into the canvas before the direct producer
  // starts. Waiting for the accepted frame keeps the boot snapshot from
  // reading a half-written DSI source.
  if (context->direct_region_active && !lvgl_esphome_wait_for_direct_frame_presented(timeout_ms))
    return false;
  if (context->pixel_buffer == nullptr || !context->prepared_frame_ready)
    return false;

  // Ask the Lottie worker to commit the retained direct frame. The operation
  // is deliberately executed on its 64 KiB stack: doing the ThorVG render
  // synchronously in loopTask can overflow the small ESPHome loop stack while
  // boot is handing the display to the snapshot compositor.
  __atomic_store_n(&context->snapshot_commit_completed, false, __ATOMIC_RELEASE);
  __atomic_store_n(&context->snapshot_commit_success, false, __ATOMIC_RELEASE);
  __atomic_store_n(&context->snapshot_commit_requested, true, __ATOMIC_RELEASE);
  lvgl::lottie_pause_for_navigation(context);
  const uint32_t commit_started = millis();
  while (!__atomic_load_n(&context->snapshot_commit_completed, __ATOMIC_ACQUIRE) &&
         millis() - commit_started <= timeout_ms) {
    vTaskDelay(1);
  }
  if (!__atomic_load_n(&context->snapshot_commit_success, __ATOMIC_ACQUIRE))
    ESP_LOGW(TAG, "Weather snapshot direct commit unavailable; using prepared LVGL canvas frame");

  // Boot captures the Home root, not its separately promoted foreground
  // layer. Keep this prepared frame in that root until navigation has built
  // its JPEG/raw cache; the post-boot resume promotes and animates it again.
  lv_lock();
  lvgl::lottie_pause_for_navigation(context);
  this->restore_animation_to_page_(*selected);
  lv_obj_clear_flag(selected->widget, LV_OBJ_FLAG_HIDDEN);
  lv_obj_update_layout(this->root_);
  lv_unlock();
  this->animation_visible_ = false;
  this->navigation_suspended_ = true;
  this->navigation_resume_pending_ = false;
  return true;
}

bool MaterialWeatherPresenter::refresh_snapshot_now(bool force_widget_render) {
  if (this->snapshot_page_ == nullptr || this->snapshot_region_ == nullptr || !this->page_visible_ ||
      !this->is_root_presented_() || this->navigation_suspended_ || this->navigation_resume_pending_ ||
      ::lvgl_esphome_snapshot_is_active())
    return false;
  const auto *selected = this->find_animation_(this->selected_animation_);
  const bool animation_ready = !this->animation_visible_ || selected == nullptr || selected->context == nullptr ||
                               (selected->context->pixel_buffer != nullptr &&
                                selected->context->prepared_frame_ready);
  if (!animation_ready)
    return false;

  // Render the registered region into the resident page cache. Scanout may
  // contain a moving animation and fixed display chrome outside this page.
  auto *completed = this->find_animation_(this->selected_animation_);
  const bool move_back_for_capture = completed != nullptr && completed->context != nullptr &&
                                     lvgl::lottie_is_complete(completed->context) && completed->on_top_layer;
  if (move_back_for_capture) {
    lv_lock();
    this->restore_animation_to_page_(*completed);
    lv_unlock();
  }

  const bool refreshed = ::lvgl_esphome_snapshot_refresh_tile_region(
      this->snapshot_page_, this->snapshot_region_, lv_obj_get_width(this->snapshot_page_), true,
      force_widget_render);
  this->snapshot_refresh_pending_ = !refreshed;
  if (!refreshed)
    this->snapshot_refresh_at_ = millis() + 250U;
  // The completed frame must be part of the Home JPEG/raw snapshot before the
  // live renderer leaves the page subtree. Otherwise the carousel's cached
  // page contains only the clock/chip and the weather animation disappears
  // whenever the direct compositor takes ownership of the screen.
  if (refreshed) {
    completed = this->find_animation_(this->selected_animation_);
    if (completed != nullptr && completed->context != nullptr &&
        lvgl::lottie_is_complete(completed->context) && !completed->on_top_layer) {
      lv_lock();
      this->promote_animation_to_top_layer_(*completed);
      lv_unlock();
    }
  } else if (move_back_for_capture) {
    // A compositor transition can legitimately defer the capture. Restore
    // the live foreground layer until the next retry instead of leaving the
    // completed weather canvas underneath the cached carousel frame.
    completed = this->find_animation_(this->selected_animation_);
    if (completed != nullptr) {
      lv_lock();
      this->promote_animation_to_top_layer_(*completed);
      lv_unlock();
    }
  }
  return refreshed;
}

void MaterialWeatherPresenter::request_snapshot_refresh_() {
  if (this->snapshot_page_ == nullptr || this->snapshot_region_ == nullptr)
    return;
  this->snapshot_refresh_pending_ = true;
  this->snapshot_refresh_at_ = millis() + this->snapshot_refresh_delay_ms_;
}

std::string MaterialWeatherPresenter::normalize_condition_(const std::string &condition) {
  std::string normalized;
  normalized.reserve(condition.size());
  for (char character : condition) {
    const unsigned char value = static_cast<unsigned char>(character);
    if (std::isalnum(value)) {
      normalized.push_back(static_cast<char>(std::tolower(value)));
    } else if (!normalized.empty() && normalized.back() != '-') {
      normalized.push_back('-');
    }
  }
  while (!normalized.empty() && normalized.back() == '-')
    normalized.pop_back();
  return normalized;
}

std::string MaterialWeatherPresenter::display_condition_(const std::string &condition) {
  if (condition.empty() || condition == "unknown" || condition == "unavailable")
    return "weather";
  std::string display = condition;
  std::replace(display.begin(), display.end(), '-', ' ');
  // The compact chip in the reference design uses a quiet lowercase label.
  // Keep the presenter deterministic instead of relying on font-case shaping.
  std::transform(display.begin(), display.end(), display.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return display;
}

std::string MaterialWeatherPresenter::select_animation_(const std::string &condition, bool is_day) const {
  if (condition == "sunny" || condition == "clear" || condition == "clear-night")
    return is_day && condition != "clear-night" ? "clear-day" : "clear-night";
  if (condition == "partlycloudy" || condition == "partly-cloudy")
    return is_day ? "partly-cloudy-day" : "partly-cloudy-night";
  if (condition == "cloudy" || condition == "windy" || condition == "windy-variant")
    return "overcast";
  if (condition == "rainy" || condition == "pouring")
    return "rain";
  if (condition == "snowy" || condition == "snowy-rainy" || condition == "hail")
    return "snow";
  if (condition == "lightning" || condition == "lightning-rainy")
    return "thunderstorms";
  if (condition == "fog")
    return "fog";
  return is_day ? "partly-cloudy-day" : "partly-cloudy-night";
}

MaterialWeatherPresenter::AnimationBinding *MaterialWeatherPresenter::find_animation_(const std::string &key) {
  auto iterator = std::find_if(this->animations_.begin(), this->animations_.end(),
                               [&key](const AnimationBinding &binding) { return binding.key == key; });
  return iterator == this->animations_.end() ? nullptr : &*iterator;
}

bool MaterialWeatherPresenter::promote_animation_to_top_layer_(AnimationBinding &animation) {
  if (animation.widget == nullptr || !lv_obj_is_valid(animation.widget))
    return false;
  if (animation.on_top_layer)
    return true;

  auto *top_layer = this->animation_layer_ != nullptr && lv_obj_is_valid(this->animation_layer_)
                        ? this->animation_layer_
                        : lv_layer_top();
  auto *parent = lv_obj_get_parent(animation.widget);
  if (top_layer == nullptr || parent == nullptr || parent == top_layer)
    return false;

  lv_area_t area{};
  lv_obj_get_coords(animation.widget, &area);
  animation.original_parent = parent;
  animation.original_x = lv_obj_get_x(animation.widget);
  animation.original_y = lv_obj_get_y(animation.widget);

  // The weather artwork is deliberately promoted only after its completed
  // frame has been captured into the Home snapshot. The retained Lottie
  // buffer then becomes a live foreground layer on subsequent Home frames,
  // while the cached page still contains the exact final frame as a bridge.
  lv_obj_set_parent(animation.widget, top_layer);
  lv_area_t layer_area{};
  lv_obj_get_coords(top_layer, &layer_area);
  lv_obj_set_pos(animation.widget, area.x1 - layer_area.x1, area.y1 - layer_area.y1);
  lv_obj_move_foreground(top_layer);
  animation.on_top_layer = true;
  return true;
}

bool MaterialWeatherPresenter::restore_animation_to_page_(AnimationBinding &animation) {
  if (!animation.on_top_layer || animation.widget == nullptr || !lv_obj_is_valid(animation.widget) ||
      animation.original_parent == nullptr || !lv_obj_is_valid(animation.original_parent))
    return false;

  lv_obj_set_parent(animation.widget, animation.original_parent);
  lv_obj_set_pos(animation.widget, animation.original_x, animation.original_y);
  animation.on_top_layer = false;
  return true;
}

bool MaterialWeatherPresenter::ensure_animation_bindings_() {
  size_t bound_count = 0;
  for (auto &animation : this->animations_) {
    if (animation.context == nullptr && animation.widget != nullptr)
      animation.context = static_cast<lvgl::LottieContext *>(lv_obj_get_user_data(animation.widget));
    if (animation.context != nullptr)
      bound_count++;
  }

  // A missing, non-selected variant must not block the selected animation.
  // Unused Lottie contexts may be deferred or rejected under PSRAM pressure.
  auto *selected = this->find_animation_(this->selected_animation_);
  const bool ready = selected != nullptr && selected->context != nullptr;
  if (ready && !this->animation_bindings_ready_) {
    this->animation_bindings_ready_ = true;
    ESP_LOGI(TAG, "Weather Lottie binding ready: %u/%u contexts, selected=%s", (unsigned) bound_count,
             (unsigned) this->animations_.size(), this->selected_animation_.c_str());
  } else if (!ready && !this->animation_bindings_wait_logged_) {
    ESP_LOGW(TAG, "Weather Lottie binding deferred: %u/%u contexts, selected=%s available=%s",
             (unsigned) bound_count, (unsigned) this->animations_.size(), this->selected_animation_.c_str(),
             selected != nullptr && selected->context != nullptr ? "YES" : "NO");
    this->animation_bindings_wait_logged_ = true;
  }
  if (!ready)
    this->animation_bindings_ready_ = false;
  return ready;
}

bool MaterialWeatherPresenter::is_root_presented_() const {
  return this->root_ != nullptr && lv_obj_is_visible(this->root_) &&
         lv_obj_get_screen(this->root_) == lv_screen_active();
}

void MaterialWeatherPresenter::apply_animation_visibility_(bool page_visible) {
  if (!this->ensure_animation_bindings_())
    return;
  const bool should_show = this->page_visible_ && page_visible && !this->navigation_suspended_ &&
                           !this->direct_overlay_suspended_ &&
                           !this->selected_animation_.empty();
  const bool was_visible = this->animation_visible_;
  const bool selection_changed = this->selected_animation_ != this->applied_animation_;
  const auto *selected = this->find_animation_(this->selected_animation_);
  const bool selected_is_presented = selected != nullptr && selected->context != nullptr &&
                                     selected->context->pixel_buffer != nullptr && !selected->context->runtime_hidden &&
                                     !lv_obj_has_flag(selected->widget, LV_OBJ_FLAG_HIDDEN);
  if (should_show == this->animation_visible_ && !selection_changed && (!should_show || selected_is_presented))
    return;

  if (should_show && selected != nullptr && selected->context != nullptr &&
      !lvgl::lottie_is_ready(selected->context)) {
    // Start loading in the background, but keep the previous snapshot/static
    // scene visible until the prepared frame is complete. This is deliberately
    // a gate rather than a timer: PSRAM pressure and ThorVG parse time vary by
    // weather asset, while a fixed delay either flashes or hides the first
    // frame.
    for (auto &animation : this->animations_) {
      if (animation.key == this->selected_animation_)
        lvgl::lottie_prepare(animation.context);
      else
        lvgl::lottie_hide(animation.context);
    }
    this->animation_visible_ = false;
    this->animation_retry_at_ = millis() + 40U;
    return;
  }

  // Do not turn a temporary PSRAM shortage into a 5 Hz allocation storm. The
  // snapshot compositor can free a large block shortly afterwards, so retry
  // lazily instead of blocking the LVGL loop or repeatedly logging errors.
  if (should_show && selected != nullptr && selected->context != nullptr &&
      selected->context->pixel_buffer == nullptr &&
      static_cast<int32_t>(millis() - this->animation_retry_at_) < 0)
    return;

  for (auto &animation : this->animations_) {
    if (should_show && animation.key == this->selected_animation_) {
      // Promotion and the show request are short LVGL operations. Keep only
      // those operations under the lock; lottie_show never waits for the
      // renderer, while lottie_hide below can wait for its task to stop.
      lv_lock();
      this->promote_animation_to_top_layer_(animation);
      lvgl::lottie_show(animation.context, !was_visible || selection_changed);
      if (animation.widget != nullptr)
        lv_obj_move_foreground(animation.widget);
      lv_unlock();
    } else if (!should_show && animation.key == this->selected_animation_) {
      // A Home-page swipe is a temporary visibility change. Keep the retained
      // buffer, but hide the top-layer canvas so it cannot float above page 2
      // or an application overlay.
      lv_lock();
      lvgl::lottie_pause(animation.context, true);
      if (animation.widget != nullptr && lv_obj_is_valid(animation.widget)) {
        lv_obj_add_flag(animation.widget, LV_OBJ_FLAG_HIDDEN);
        lv_obj_invalidate(animation.widget);
      }
      lv_unlock();
    } else {
      // This may stop a renderer and wait for its direct-region callback.
      // Never run it while lv_lock() is held.
      lvgl::lottie_hide(animation.context);
    }
  }
  if (should_show && selected != nullptr && selected->context != nullptr && selected->context->pixel_buffer == nullptr)
    this->animation_retry_at_ = millis() + 1000U;
  else if (!should_show || selected_is_presented)
    this->animation_retry_at_ = 0;
  this->applied_animation_ = this->selected_animation_;
  this->animation_visible_ = should_show;
  if (!should_show || !was_visible || selection_changed)
    this->animation_completion_reported_ = false;
}

}  // namespace esphome::lvgl_material
