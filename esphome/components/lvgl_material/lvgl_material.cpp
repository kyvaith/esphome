#include "lvgl_material.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "esphome/core/log.h"

#ifdef USE_ESP32
#include "esp_heap_caps.h"
#endif

namespace esphome::lvgl_material {

static const char *const TAG = "lvgl_material";

void MaterialDirectStateLayer::setup() {
  if (this->lvgl_component_ == nullptr || this->targets_.empty()) {
    ESP_LOGE(TAG, "Direct state layer configuration is incomplete");
    this->mark_failed();
    return;
  }
  if (!this->allocate_buffers_()) {
    ESP_LOGE(TAG, "Unable to allocate direct state layer buffers");
    this->mark_failed();
  }
}

void MaterialDirectStateLayer::on_shutdown() {
  this->abandon();
  if (this->press_in_flight_.load(std::memory_order_acquire) ||
      this->restore_in_flight_.load(std::memory_order_acquire)) {
    return;
  }
#ifdef USE_ESP32
  heap_caps_free(this->normal_buffer_);
  heap_caps_free(this->pressed_buffer_);
#else
  std::free(this->normal_buffer_);
  std::free(this->pressed_buffer_);
#endif
  this->normal_buffer_ = nullptr;
  this->pressed_buffer_ = nullptr;
  this->buffer_capacity_ = 0;
}

void MaterialDirectStateLayer::dump_config() {
  ESP_LOGCONFIG(TAG, "Material Direct State Layer:");
  ESP_LOGCONFIG(TAG, "  Targets: %u", static_cast<unsigned>(this->targets_.size()));
  ESP_LOGCONFIG(TAG, "  Pressed opacity: %u", static_cast<unsigned>(this->pressed_opacity_));
  ESP_LOGCONFIG(TAG, "  Reserved buffer pair: %u bytes", static_cast<unsigned>(this->buffer_capacity_ * 2));
}

bool MaterialDirectStateLayer::press(lv_obj_t *target) {
  if (this->is_failed() || !this->is_configured_target_(target) || this->active_ ||
      this->press_in_flight_.load(std::memory_order_acquire) ||
      this->restore_in_flight_.load(std::memory_order_acquire)) {
    return false;
  }

  lv_area_t area{};
  lv_obj_get_coords(target, &area);
  const int width = lv_area_get_width(&area);
  const int height = lv_area_get_height(&area);
  if (width <= 0 || height <= 0)
    return false;

  constexpr int BYTES_PER_PIXEL = 3;
  const int stride = width * BYTES_PER_PIXEL;
  const size_t bytes = static_cast<size_t>(stride) * height;
  if (bytes > this->buffer_capacity_ ||
      !this->lvgl_component_->direct_capture_rgb888(this->normal_buffer_, stride, area.x1, area.y1, width, height)) {
    return false;
  }

  std::memcpy(this->pressed_buffer_, this->normal_buffer_, bytes);
  const int radius = std::min<int>(lv_obj_get_style_radius(target, LV_PART_MAIN), std::min(width, height) / 2);
  const unsigned retained = 255U - this->pressed_opacity_;
  for (int py = 0; py < height; py++) {
    for (int px = 0; px < width; px++) {
      if (!inside_rounded_rect_(px, py, width, height, radius))
        continue;
      uint8_t *pixel = this->pressed_buffer_ + static_cast<size_t>(py * stride + px * BYTES_PER_PIXEL);
      pixel[0] = static_cast<uint8_t>((static_cast<unsigned>(pixel[0]) * retained + 127U) / 255U);
      pixel[1] = static_cast<uint8_t>((static_cast<unsigned>(pixel[1]) * retained + 127U) / 255U);
      pixel[2] = static_cast<uint8_t>((static_cast<unsigned>(pixel[2]) * retained + 127U) / 255U);
    }
  }

  this->x_ = area.x1;
  this->y_ = area.y1;
  this->width_ = width;
  this->height_ = height;
  this->press_in_flight_.store(true, std::memory_order_release);
  const uint8_t result = this->lvgl_component_->direct_blit_rgb888_async(
      this->pressed_buffer_, stride, this->x_, this->y_, width, height, press_ready_cb_, this);
  if (result != LVGL_DIRECT_BLIT_SUBMITTED) {
    this->press_in_flight_.store(false, std::memory_order_release);
    return false;
  }
  this->active_ = true;
  return true;
}

bool MaterialDirectStateLayer::release() {
  if (!this->active_)
    return true;
  if (this->restore_in_flight_.load(std::memory_order_acquire))
    return true;
  if (this->normal_buffer_ == nullptr || this->width_ <= 0 || this->height_ <= 0) {
    this->active_ = false;
    return true;
  }

  this->restore_in_flight_.store(true, std::memory_order_release);
  const uint8_t result = this->lvgl_component_->direct_blit_rgb888_async(
      this->normal_buffer_, this->width_ * 3, this->x_, this->y_, this->width_, this->height_, restore_ready_cb_, this);
  if (result != LVGL_DIRECT_BLIT_SUBMITTED) {
    this->restore_in_flight_.store(false, std::memory_order_release);
    return false;
  }
  this->active_ = false;
  return true;
}

void MaterialDirectStateLayer::abandon() {
  if (this->lvgl_component_ != nullptr && this->width_ > 0 && this->height_ > 0)
    this->lvgl_component_->direct_blit_rgb888_release(this->x_, this->y_, this->width_, this->height_);
  this->active_ = false;
}

void MaterialDirectStateLayer::press_ready_cb_(void *arg) {
  auto *state_layer = static_cast<MaterialDirectStateLayer *>(arg);
  if (state_layer != nullptr)
    state_layer->press_in_flight_.store(false, std::memory_order_release);
}

void MaterialDirectStateLayer::restore_ready_cb_(void *arg) {
  auto *state_layer = static_cast<MaterialDirectStateLayer *>(arg);
  if (state_layer != nullptr)
    state_layer->restore_in_flight_.store(false, std::memory_order_release);
}

bool MaterialDirectStateLayer::inside_rounded_rect_(int x, int y, int width, int height, int radius) {
  if (radius <= 0 || (x >= radius && x < width - radius) || (y >= radius && y < height - radius))
    return true;

  const int center_x2 = x < radius ? 2 * radius - 1 : 2 * (width - radius) - 1;
  const int center_y2 = y < radius ? 2 * radius - 1 : 2 * (height - radius) - 1;
  const int dx2 = 2 * x - center_x2;
  const int dy2 = 2 * y - center_y2;
  const int radius2 = 2 * radius;
  return dx2 * dx2 + dy2 * dy2 <= radius2 * radius2;
}

bool MaterialDirectStateLayer::allocate_buffers_() {
  size_t required = 0;
  for (lv_obj_t *target : this->targets_) {
    if (target == nullptr)
      return false;
    lv_obj_update_layout(target);
    lv_area_t area{};
    lv_obj_get_coords(target, &area);
    const int width = lv_area_get_width(&area);
    const int height = lv_area_get_height(&area);
    if (width <= 0 || height <= 0)
      return false;
    required = std::max(required, static_cast<size_t>(width) * height * 3);
  }
  if (required == 0)
    return false;

#ifdef USE_ESP32
  this->normal_buffer_ =
      static_cast<uint8_t *>(heap_caps_aligned_alloc(64, required, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  this->pressed_buffer_ =
      static_cast<uint8_t *>(heap_caps_aligned_alloc(64, required, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
  this->normal_buffer_ = static_cast<uint8_t *>(std::malloc(required));
  this->pressed_buffer_ = static_cast<uint8_t *>(std::malloc(required));
#endif
  if (this->normal_buffer_ == nullptr || this->pressed_buffer_ == nullptr) {
#ifdef USE_ESP32
    heap_caps_free(this->normal_buffer_);
    heap_caps_free(this->pressed_buffer_);
#else
    std::free(this->normal_buffer_);
    std::free(this->pressed_buffer_);
#endif
    this->normal_buffer_ = nullptr;
    this->pressed_buffer_ = nullptr;
    return false;
  }
  this->buffer_capacity_ = required;
  return true;
}

bool MaterialDirectStateLayer::is_configured_target_(lv_obj_t *target) const {
  return target != nullptr && std::find(this->targets_.begin(), this->targets_.end(), target) != this->targets_.end();
}

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
