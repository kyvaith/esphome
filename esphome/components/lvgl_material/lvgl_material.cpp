#include "lvgl_material.h"

#include <algorithm>

#include "esphome/core/log.h"

namespace esphome::lvgl_material {

static const char *const TAG = "lvgl_material";

void MaterialStateLayer::setup() {
  if (this->target_ == nullptr) {
    ESP_LOGE(TAG, "State layer target is unavailable");
    this->mark_failed();
    return;
  }

  this->layer_ = lv_obj_create(this->target_);
  if (this->layer_ == nullptr) {
    ESP_LOGE(TAG, "Unable to create state layer");
    this->mark_failed();
    return;
  }

  lv_obj_remove_style_all(this->layer_);
  lv_obj_set_size(this->layer_, LV_PCT(100), LV_PCT(100));
  lv_obj_align(this->layer_, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(this->layer_, this->color_, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(this->layer_, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(this->layer_, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(this->layer_, 0, LV_PART_MAIN);
  lv_obj_add_flag(this->layer_, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_remove_flag(this->layer_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(this->layer_, LV_OBJ_FLAG_SCROLLABLE);
  this->sync_geometry_();

  lv_obj_add_event_cb(this->target_, target_event_cb_, LV_EVENT_PRESSED, this);
  lv_obj_add_event_cb(this->target_, target_event_cb_, LV_EVENT_RELEASED, this);
  lv_obj_add_event_cb(this->target_, target_event_cb_, LV_EVENT_PRESS_LOST, this);
  lv_obj_add_event_cb(this->target_, target_event_cb_, LV_EVENT_SIZE_CHANGED, this);
  lv_obj_add_event_cb(this->target_, target_event_cb_, LV_EVENT_STYLE_CHANGED, this);
  lv_obj_add_event_cb(this->target_, target_event_cb_, LV_EVENT_DELETE, this);
}

void MaterialStateLayer::dump_config() {
  ESP_LOGCONFIG(TAG, "Material State Layer:");
  ESP_LOGCONFIG(TAG, "  Pressed opacity: %u", static_cast<unsigned>(this->pressed_opacity_));
  ESP_LOGCONFIG(TAG, "  Durations: enter=%ums exit=%ums", static_cast<unsigned>(this->enter_duration_),
                static_cast<unsigned>(this->exit_duration_));
}

void MaterialStateLayer::target_event_cb_(lv_event_t *event) {
  auto *state_layer = static_cast<MaterialStateLayer *>(lv_event_get_user_data(event));
  if (state_layer == nullptr)
    return;

  switch (lv_event_get_code(event)) {
    case LV_EVENT_PRESSED:
      if (state_layer->layer_ != nullptr)
        lv_obj_move_foreground(state_layer->layer_);
      state_layer->animate_to_(state_layer->pressed_opacity_, state_layer->enter_duration_);
      break;
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
      state_layer->animate_to_(LV_OPA_TRANSP, state_layer->exit_duration_);
      break;
    case LV_EVENT_SIZE_CHANGED:
    case LV_EVENT_STYLE_CHANGED:
      state_layer->sync_geometry_();
      break;
    case LV_EVENT_DELETE:
      state_layer->target_ = nullptr;
      state_layer->layer_ = nullptr;
      break;
    default:
      break;
  }
}

void MaterialStateLayer::opacity_animation_cb_(void *object, int32_t value) {
  auto *layer = static_cast<lv_obj_t *>(object);
  if (layer != nullptr)
    lv_obj_set_style_bg_opa(layer, static_cast<lv_opa_t>(value), LV_PART_MAIN);
}

void MaterialStateLayer::animate_to_(lv_opa_t opacity, uint32_t duration) {
  if (this->layer_ == nullptr)
    return;

  lv_anim_delete(this->layer_, opacity_animation_cb_);
  if (duration == 0) {
    opacity_animation_cb_(this->layer_, opacity);
    return;
  }

  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, this->layer_);
  lv_anim_set_values(&animation, lv_obj_get_style_bg_opa(this->layer_, LV_PART_MAIN), opacity);
  lv_anim_set_duration(&animation, duration);
  lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&animation, opacity_animation_cb_);
  lv_anim_start(&animation);
}

void MaterialStateLayer::sync_geometry_() {
  if (this->target_ == nullptr || this->layer_ == nullptr)
    return;
  lv_obj_set_style_radius(this->layer_, lv_obj_get_style_radius(this->target_, LV_PART_MAIN), LV_PART_MAIN);
}

void MaterialPageIndicator::setup() {
  if (this->container_ == nullptr || this->count_ == 0 || this->active_page_ >= this->count_) {
    ESP_LOGE(TAG, "Page indicator configuration is invalid");
    this->mark_failed();
    return;
  }

  this->root_ = lv_obj_create(this->container_);
  if (this->root_ == nullptr) {
    ESP_LOGE(TAG, "Unable to create page indicator");
    this->mark_failed();
    return;
  }

  lv_obj_remove_style_all(this->root_);
  lv_obj_set_size(this->root_, LV_PCT(100), LV_PCT(100));
  lv_obj_align(this->root_, LV_ALIGN_CENTER, 0, 0);
  lv_obj_add_flag(this->root_, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_remove_flag(this->root_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(this->root_, LV_OBJ_FLAG_SCROLLABLE);

  for (uint8_t index = 0; index < this->count_; index++) {
    lv_obj_t *dot = lv_obj_create(this->root_);
    if (dot == nullptr) {
      ESP_LOGE(TAG, "Unable to create page indicator item");
      this->mark_failed();
      return;
    }
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, this->inactive_size_, this->thickness_);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    this->items_[index].owner = this;
    this->items_[index].object = dot;
    this->items_[index].width = this->inactive_size_;
  }
  this->apply_active_page_(false);
}

void MaterialPageIndicator::dump_config() {
  ESP_LOGCONFIG(TAG, "Material Page Indicator:");
  ESP_LOGCONFIG(TAG, "  Pages: %u", static_cast<unsigned>(this->count_));
  ESP_LOGCONFIG(TAG, "  Initial page: %u", static_cast<unsigned>(this->active_page_));
  ESP_LOGCONFIG(TAG, "  Transition duration: %ums", static_cast<unsigned>(this->transition_duration_));
}

void MaterialPageIndicator::set_active_page(uint16_t page, bool animated) {
  if (page >= this->count_ || page == this->active_page_)
    return;
  this->active_page_ = page;
  this->apply_active_page_(animated);
}

void MaterialPageIndicator::size_animation_cb_(void *object, int32_t value) {
  auto *item = static_cast<IndicatorItem *>(object);
  if (item == nullptr || item->owner == nullptr || item->object == nullptr)
    return;
  item->width = value;
  lv_obj_set_width(item->object, value);
  item->owner->layout_items_();
}

void MaterialPageIndicator::apply_active_page_(bool animated) {
  if (this->root_ == nullptr)
    return;

  for (uint8_t index = 0; index < this->count_; index++) {
    IndicatorItem &item = this->items_[index];
    lv_obj_t *dot = item.object;
    const bool active = index == this->active_page_;
    const int32_t target_width = active ? this->active_size_ : this->inactive_size_;
    lv_obj_set_style_bg_color(dot, active ? this->active_color_ : this->inactive_color_, LV_PART_MAIN);
    lv_anim_delete(&item, size_animation_cb_);
    if (!animated || this->transition_duration_ == 0) {
      item.width = target_width;
      lv_obj_set_width(dot, target_width);
      continue;
    }

    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, &item);
    lv_anim_set_values(&animation, item.width, target_width);
    lv_anim_set_duration(&animation, this->transition_duration_);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&animation, size_animation_cb_);
    lv_anim_start(&animation);
  }
  this->layout_items_();
}

void MaterialPageIndicator::layout_items_() {
  if (this->root_ == nullptr)
    return;

  int32_t total_width = this->gap_ * (this->count_ - 1);
  for (uint8_t index = 0; index < this->count_; index++)
    total_width += this->items_[index].width;

  int32_t cursor = -total_width / 2;
  for (uint8_t index = 0; index < this->count_; index++) {
    IndicatorItem &item = this->items_[index];
    lv_obj_align(item.object, LV_ALIGN_CENTER, cursor + item.width / 2, 0);
    cursor += item.width + this->gap_;
  }
}

}  // namespace esphome::lvgl_material
