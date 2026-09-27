#include "material_notification.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cstring>

namespace esphome::lvgl_material {

static const char *const TAG = "lvgl_material.notification";

void MaterialNotificationOverlay::setup() {
  if (this->root_ == nullptr || this->panel_ == nullptr || this->icon_ == nullptr || this->title_ == nullptr ||
      this->message_ == nullptr) {
    ESP_LOGE(TAG, "Notification overlay widget binding is incomplete");
    this->mark_failed();
    return;
  }

  lv_obj_update_layout(this->root_);
  this->panel_hidden_translate_y_ = lv_obj_get_height(this->panel_);
  lv_obj_set_style_translate_y(this->panel_, this->panel_hidden_translate_y_, LV_PART_MAIN);
  lv_obj_add_flag(this->root_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(this->root_, root_event_cb_, LV_EVENT_CLICKED, this);
}

void MaterialNotificationOverlay::loop() {
  if (this->closing_ && (this->close_animation_ready_ ||
                         (this->close_deadline_ != 0 && static_cast<int32_t>(millis() - this->close_deadline_) >= 0))) {
    this->finish_close_();
    return;
  }
  if (!this->active_ || this->closing_ || this->deadline_ == 0)
    return;
  if (static_cast<int32_t>(millis() - this->deadline_) >= 0)
    this->dismiss();
}

void MaterialNotificationOverlay::on_shutdown() {
  if (this->root_ != nullptr)
    lv_anim_delete(this->root_, scrim_animation_cb_);
  if (this->panel_ != nullptr)
    lv_anim_delete(this->panel_, panel_translate_animation_cb_);
  if (this->direct_regions_paused_ && this->lvgl_component_ != nullptr) {
    this->lvgl_component_->direct_regions_pause(false, 0);
    this->direct_regions_paused_ = false;
  }
}

void MaterialNotificationOverlay::dump_config() {
  ESP_LOGCONFIG(TAG, "Material Notification Overlay:");
  ESP_LOGCONFIG(TAG, "  Queue capacity: %u", static_cast<unsigned>(QUEUE_CAPACITY));
  ESP_LOGCONFIG(TAG, "  Default duration: %ums", static_cast<unsigned>(this->default_duration_));
}

void MaterialNotificationOverlay::show(const std::string &title, const std::string &message, const std::string &icon,
                                       uint32_t duration) {
  if (this->is_failed())
    return;

  if (this->queue_size_ == QUEUE_CAPACITY) {
    this->queue_head_ = (this->queue_head_ + 1U) % QUEUE_CAPACITY;
    this->queue_size_--;
  }
  const uint8_t tail = (this->queue_head_ + this->queue_size_) % QUEUE_CAPACITY;
  Notification &notification = this->queue_[tail];
  copy_text_(notification.title.data(), notification.title.size(), title);
  copy_text_(notification.message.data(), notification.message.size(), message);
  copy_text_(notification.icon.data(), notification.icon.size(), icon);
  notification.duration = duration == 0 ? this->default_duration_ : duration;
  this->queue_size_++;

  if (!this->active_ && !this->closing_)
    this->display_next_();
}

void MaterialNotificationOverlay::dismiss() {
  if (!this->active_ || this->closing_)
    return;
  this->closing_ = true;
  this->close_animation_ready_ = false;
  this->deadline_ = 0;
  this->close_deadline_ = millis() + 300;
  this->animate_scrim_(this->scrim_opacity_, LV_OPA_TRANSP, 180);
  this->animate_panel_(lv_obj_get_style_translate_y(this->panel_, LV_PART_MAIN), this->panel_hidden_translate_y_, 220,
                       true);
}

void MaterialNotificationOverlay::display_next_() {
  if (this->queue_size_ == 0 || this->root_ == nullptr)
    return;

  if (!this->direct_regions_paused_ && this->lvgl_component_ != nullptr) {
    if (this->lvgl_component_->direct_regions_pause(true, 120)) {
      this->direct_regions_paused_ = true;
    } else {
      this->lvgl_component_->direct_regions_pause(false, 0);
      ESP_LOGW(TAG, "Unable to pause direct regions before showing notification");
    }
  }

  Notification &notification = this->queue_[this->queue_head_];
  this->queue_head_ = (this->queue_head_ + 1U) % QUEUE_CAPACITY;
  this->queue_size_--;

  lv_label_set_text(this->title_, notification.title.data());
  lv_label_set_text(this->message_, notification.message.data());
  lv_label_set_text(this->icon_, notification.icon.data());
  lv_obj_remove_flag(this->root_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(this->root_);
  if (auto *display = lv_obj_get_display(this->root_); display != nullptr)
    lv_display_trigger_activity(display);
  lv_obj_set_style_bg_opa(this->root_, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_translate_y(this->panel_, this->panel_hidden_translate_y_, LV_PART_MAIN);
  this->active_ = true;
  this->closing_ = false;
  this->close_animation_ready_ = false;
  this->close_deadline_ = 0;
  this->deadline_ = millis() + notification.duration;
  this->animate_scrim_(LV_OPA_TRANSP, this->scrim_opacity_, 180);
  this->animate_panel_(this->panel_hidden_translate_y_, 0, 260, false);
}

void MaterialNotificationOverlay::finish_close_() {
  if (this->root_ != nullptr)
    lv_obj_add_flag(this->root_, LV_OBJ_FLAG_HIDDEN);
  this->active_ = false;
  this->closing_ = false;
  this->close_animation_ready_ = false;
  this->close_deadline_ = 0;
  if (this->queue_size_ != 0) {
    this->display_next_();
    return;
  }

  if (this->direct_regions_paused_ && this->lvgl_component_ != nullptr) {
    auto *display = this->lvgl_component_->get_disp();
    if (display != nullptr) {
      lv_obj_invalidate(lv_display_get_screen_active(display));
      lv_refr_now(display);
      if (!this->lvgl_component_->wait_for_direct_frame_presented(80))
        ESP_LOGW(TAG, "Underlying frame did not reach the panel before direct regions resumed");
    }
    this->lvgl_component_->direct_regions_pause(false, 0);
    this->direct_regions_paused_ = false;
  }
}

void MaterialNotificationOverlay::panel_translate_animation_cb_(void *object, int32_t value) {
  auto *panel = static_cast<lv_obj_t *>(object);
  if (panel != nullptr)
    lv_obj_set_style_translate_y(panel, value, LV_PART_MAIN);
}

void MaterialNotificationOverlay::scrim_animation_cb_(void *object, int32_t value) {
  auto *root = static_cast<lv_obj_t *>(object);
  if (root != nullptr)
    lv_obj_set_style_bg_opa(root, static_cast<lv_opa_t>(value), LV_PART_MAIN);
}

void MaterialNotificationOverlay::close_animation_ready_cb_(lv_anim_t *animation) {
  auto *overlay = static_cast<MaterialNotificationOverlay *>(lv_anim_get_user_data(animation));
  if (overlay != nullptr)
    overlay->close_animation_ready_ = true;
}

void MaterialNotificationOverlay::root_event_cb_(lv_event_t *event) {
  auto *overlay = static_cast<MaterialNotificationOverlay *>(lv_event_get_user_data(event));
  if (overlay == nullptr || lv_event_get_target(event) != overlay->root_)
    return;
  overlay->dismiss();
}

void MaterialNotificationOverlay::copy_text_(char *destination, size_t capacity, const std::string &source) {
  if (capacity == 0)
    return;
  const size_t length = std::min(capacity - 1U, source.size());
  std::memcpy(destination, source.data(), length);
  destination[length] = '\0';
}

void MaterialNotificationOverlay::animate_panel_(int32_t from, int32_t to, uint32_t duration, bool closing) {
  lv_anim_delete(this->panel_, panel_translate_animation_cb_);
  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, this->panel_);
  lv_anim_set_values(&animation, from, to);
  lv_anim_set_duration(&animation, duration);
  lv_anim_set_path_cb(&animation, closing ? lv_anim_path_ease_in : lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&animation, panel_translate_animation_cb_);
  if (closing) {
    lv_anim_set_user_data(&animation, this);
    lv_anim_set_completed_cb(&animation, close_animation_ready_cb_);
  }
  lv_anim_start(&animation);
}

void MaterialNotificationOverlay::animate_scrim_(lv_opa_t from, lv_opa_t to, uint32_t duration) {
  lv_anim_delete(this->root_, scrim_animation_cb_);
  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, this->root_);
  lv_anim_set_values(&animation, from, to);
  lv_anim_set_duration(&animation, duration);
  lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&animation, scrim_animation_cb_);
  lv_anim_start(&animation);
}

}  // namespace esphome::lvgl_material
