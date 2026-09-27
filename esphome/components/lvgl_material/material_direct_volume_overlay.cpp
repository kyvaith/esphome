#include "material_direct_volume_overlay.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef USE_ESP32
#include "esphome/components/esp32/task_utils.h"
#include "esp_heap_caps.h"
#endif

namespace esphome::lvgl_material {

static const char *const TAG = "lvgl_material.volume";
static constexpr float PI = 3.14159265358979323846f;
static void update_atomic_max(std::atomic<uint32_t> &target, uint32_t value) {
  uint32_t current = target.load(std::memory_order_relaxed);
  while (current < value && !target.compare_exchange_weak(current, value, std::memory_order_relaxed)) {
  }
}

static void set_hidden_without_invalidation(lv_obj_t *obj, bool hidden) {
  if (obj == nullptr)
    return;

  lv_display_t *display = lv_obj_get_display(obj);
  const bool invalidation_enabled = display != nullptr && lv_display_is_invalidation_enabled(display);
  if (invalidation_enabled)
    lv_display_enable_invalidation(display, false);
  if (hidden) {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
  if (invalidation_enabled)
    lv_display_enable_invalidation(display, true);
}

int MaterialDirectVolumeOverlay::clamp_pct(int pct) {
  if (pct < 0)
    return 0;
  if (pct > 100)
    return 100;
  return pct;
}

void MaterialDirectVolumeOverlay::setup() {
  if (this->lvgl_component_ == nullptr || this->arc_ == nullptr || this->knob_ == nullptr || this->label_ == nullptr) {
    ESP_LOGE(TAG, "Direct volume overlay configuration is incomplete");
    this->mark_failed();
    return;
  }
#ifdef USE_ESP32
  // Reserve the tiny worker before image/Lottie pipelines consume most of the
  // internal heap. Lazy creation during a gesture used to fail after a
  // gallery/camera cycle and forced full-frame work back onto the main loop.
  if (!this->ensure_worker_())
    ESP_LOGW(TAG, "Unable to reserve direct volume worker during setup; native fallback will be used");
#endif
  if (!this->update_geometry_() || !this->ensure_persistent_resources_())
    ESP_LOGW(TAG, "Unable to reserve direct volume rendering buffers during setup; will retry on first use");
}

void MaterialDirectVolumeOverlay::loop() {
#ifdef USE_ESP32
  if (!this->worker_failed_.exchange(false, std::memory_order_acq_rel))
    return;
  const int requested = this->last_requested_update_.load(std::memory_order_acquire);
  ESP_LOGW(TAG, "Direct volume worker failed; switching this gesture to native LVGL rendering");
  this->end(true);
  auto *root = this->arc_ == nullptr ? nullptr : lv_obj_get_parent(this->arc_);
  if (root != nullptr)
    lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN);
  if (requested != NO_PENDING_UPDATE)
    this->update_native_((requested >> 8) & 0xFF, requested & 0xFF);
#endif
}

void MaterialDirectVolumeOverlay::on_shutdown() {
  this->end(false);
  this->release_persistent_resources_();
}

void MaterialDirectVolumeOverlay::dump_config() {
  ESP_LOGCONFIG(TAG, "Material Direct Volume Overlay:");
  ESP_LOGCONFIG(TAG, "  Arc: %s", this->arc_ == nullptr ? "missing" : "configured");
  ESP_LOGCONFIG(TAG, "  Knob: %s", this->knob_ == nullptr ? "missing" : "configured");
  ESP_LOGCONFIG(TAG, "  Label: %s", this->label_ == nullptr ? "missing" : "configured");
  ESP_LOGCONFIG(TAG, "  Activation widget: %s", this->activation_widget_ == nullptr ? "none" : "configured");
  ESP_LOGCONFIG(TAG, "  Scrim opacity: %.0f%%", 100.0f * static_cast<float>(this->scrim_opacity_) / 255.0f);
}

void MaterialDirectVolumeOverlay::reset_visual_cache_() { this->visual_cache_ = {}; }

bool MaterialDirectVolumeOverlay::update_geometry_() {
  if (this->arc_ == nullptr || this->knob_ == nullptr || this->label_ == nullptr)
    return false;

  this->display_ = lv_obj_get_display(this->arc_);
  if (this->display_ == nullptr)
    return false;
  this->screen_width_ = lv_display_get_horizontal_resolution(this->display_);
  this->screen_height_ = lv_display_get_vertical_resolution(this->display_);
  if (this->screen_width_ <= 0 || this->screen_height_ <= 0)
    return false;

  lv_area_t arc_area{};
  lv_obj_get_coords(this->arc_, &arc_area);
  const int arc_width = lv_area_get_width(&arc_area);
  const int arc_height = lv_area_get_height(&arc_area);
  const int track_width = std::max(1, static_cast<int>(lv_obj_get_style_arc_width(this->arc_, LV_PART_MAIN)));
  this->center_x_ = (static_cast<float>(arc_area.x1) + static_cast<float>(arc_area.x2) + 1.0f) * 0.5f;
  this->center_y_ = (static_cast<float>(arc_area.y1) + static_cast<float>(arc_area.y2) + 1.0f) * 0.5f;
  this->arc_radius_ = (static_cast<float>(std::min(arc_width, arc_height)) - static_cast<float>(track_width)) * 0.5f;
  this->track_radius_ = static_cast<float>(track_width) * 0.5f;

  const int knob_width = lv_obj_get_width(this->knob_);
  const int knob_height = lv_obj_get_height(this->knob_);
  this->knob_radius_ = static_cast<float>(std::max(1, std::min(knob_width, knob_height))) * 0.5f;
  this->capture_height_ =
      std::clamp(static_cast<int>(std::lround(this->center_y_)) + this->screen_height_ / 10, 1, this->screen_height_);

  this->font_ = lv_obj_get_style_text_font(this->label_, LV_PART_MAIN);
  this->letter_space_ = lv_obj_get_style_text_letter_space(this->label_, LV_PART_MAIN);
  if (this->font_ == nullptr || this->arc_radius_ <= 0.0f)
    return false;

  lv_point_t max_text_size{};
  lv_text_get_size(&max_text_size, "100%", this->font_, this->letter_space_,
                   lv_obj_get_style_text_line_space(this->label_, LV_PART_MAIN), LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  lv_area_t label_area{};
  lv_obj_get_coords(this->label_, &label_area);
  const int label_center_x = (label_area.x1 + label_area.x2 + 1) / 2;
  const int label_center_y = (label_area.y1 + label_area.y2 + 1) / 2;
  const int text_width = std::max(static_cast<int>(max_text_size.x), static_cast<int>(lv_area_get_width(&label_area)));
  const int text_height =
      std::max(static_cast<int>(max_text_size.y), static_cast<int>(lv_area_get_height(&label_area)));
  this->label_width_ = std::min(this->screen_width_, text_width + 32);
  this->label_height_ = std::min(this->screen_height_, text_height + 32);
  this->label_x_ = std::clamp(label_center_x - this->label_width_ / 2, 0, this->screen_width_ - this->label_width_);
  this->label_y_ = std::clamp(label_center_y - this->label_height_ / 2, 0, this->screen_height_ - this->label_height_);

  this->inactive_color_ = lv_obj_get_style_arc_color(this->arc_, LV_PART_MAIN);
  this->active_color_ = lv_obj_get_style_arc_color(this->arc_, LV_PART_INDICATOR);
  this->knob_color_ = lv_obj_get_style_bg_color(this->knob_, LV_PART_MAIN);
  // A bounded band keeps the gesture allocation-free without reserving a
  // contiguous 800x480 RGB888 surface. At 128 rows a full overlay refresh is
  // at most four PPA transfers, while the common damage-only path needs one.
  this->scratch_capacity_ =
      std::max(static_cast<size_t>(this->screen_width_) * std::min(this->capture_height_, SCRATCH_BAND_ROWS),
               static_cast<size_t>(this->label_width_) * this->label_height_);
  this->activation_patch_required_capacity_ = 0;
  if (this->activation_widget_ != nullptr && lv_obj_is_valid(this->activation_widget_)) {
    lv_area_t activation_area{};
    lv_obj_get_coords(this->activation_widget_, &activation_area);
    const int patch_x1 = std::clamp(static_cast<int>(activation_area.x1), 0, this->screen_width_ - 1);
    const int patch_y1 = std::clamp(static_cast<int>(activation_area.y1), 0, this->screen_height_ - 1);
    const int patch_x2 = std::clamp(static_cast<int>(activation_area.x2), patch_x1, this->screen_width_ - 1);
    const int patch_y2 = std::clamp(static_cast<int>(activation_area.y2), patch_y1, this->screen_height_ - 1);
    this->activation_patch_required_capacity_ =
        static_cast<size_t>(patch_x2 - patch_x1 + 1) * static_cast<size_t>(patch_y2 - patch_y1 + 1);
  }
  return this->scratch_capacity_ != 0;
}

bool MaterialDirectVolumeOverlay::ensure_persistent_resources_() {
  if (this->scratch_capacity_ == 0 || this->font_ == nullptr)
    return false;

  if (this->scratch_ == nullptr || this->scratch_allocated_capacity_ < this->scratch_capacity_) {
#ifdef USE_ESP32
    if (this->scratch_ != nullptr)
      heap_caps_free(this->scratch_);
    this->scratch_ = static_cast<lv_color_t *>(
        heap_caps_malloc(this->scratch_capacity_ * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
    std::free(this->scratch_);
    this->scratch_ = static_cast<lv_color_t *>(std::malloc(this->scratch_capacity_ * sizeof(lv_color_t)));
#endif
    this->scratch_allocated_capacity_ = this->scratch_ == nullptr ? 0 : this->scratch_capacity_;
  }

  const int glyph_width = std::max(64, static_cast<int>(this->font_->line_height) * 2);
  const int glyph_height = std::max(64, static_cast<int>(this->font_->line_height) + 48);
  if (this->glyph_buffer_ == nullptr || this->glyph_buffer_width_ < glyph_width ||
      this->glyph_buffer_height_ < glyph_height) {
    if (this->glyph_buffer_ != nullptr)
      lv_draw_buf_destroy(this->glyph_buffer_);
    this->glyph_buffer_ = lv_draw_buf_create(glyph_width, glyph_height, LV_COLOR_FORMAT_A8, LV_STRIDE_AUTO);
    if (this->glyph_buffer_ != nullptr) {
      this->glyph_buffer_width_ = glyph_width;
      this->glyph_buffer_height_ = glyph_height;
    } else {
      this->glyph_buffer_width_ = 0;
      this->glyph_buffer_height_ = 0;
    }
  }

  if (this->activation_patch_required_capacity_ != 0 &&
      (this->activation_patch_ == nullptr || this->activation_patch_capacity_ < this->activation_patch_required_capacity_)) {
#ifdef USE_ESP32
    if (this->activation_patch_ != nullptr)
      heap_caps_free(this->activation_patch_);
    this->activation_patch_ = static_cast<lv_color_t *>(heap_caps_malloc(
        this->activation_patch_required_capacity_ * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
    std::free(this->activation_patch_);
    this->activation_patch_ = static_cast<lv_color_t *>(
        std::malloc(this->activation_patch_required_capacity_ * sizeof(lv_color_t)));
#endif
    this->activation_patch_capacity_ =
        this->activation_patch_ == nullptr ? 0 : this->activation_patch_required_capacity_;
  }

  return this->scratch_ != nullptr && this->glyph_buffer_ != nullptr &&
         (this->activation_patch_required_capacity_ == 0 || this->activation_patch_ != nullptr);
}

void MaterialDirectVolumeOverlay::release_persistent_resources_() {
  if (this->glyph_buffer_ != nullptr)
    lv_draw_buf_destroy(this->glyph_buffer_);
#ifdef USE_ESP32
  if (this->scratch_ != nullptr)
    heap_caps_free(this->scratch_);
  if (this->activation_patch_ != nullptr)
    heap_caps_free(this->activation_patch_);
#else
  std::free(this->scratch_);
  std::free(this->activation_patch_);
#endif
  this->glyph_buffer_ = nullptr;
  this->glyph_buffer_width_ = 0;
  this->glyph_buffer_height_ = 0;
  this->scratch_ = nullptr;
  this->scratch_allocated_capacity_ = 0;
  this->activation_patch_ = nullptr;
  this->activation_patch_capacity_ = 0;
  this->activation_patch_required_capacity_ = 0;
}

void MaterialDirectVolumeOverlay::release_() {
  if (this->background_lease_)
    this->lvgl_component_->release_presentation_frame(&this->background_lease_);
  if (this->reserved_original_frame_ != nullptr)
    this->lvgl_component_->release_reserved_presentation_frame(this->reserved_original_frame_);
#ifdef USE_ESP32
  if (this->original_ != nullptr && this->original_owned_)
    heap_caps_free(const_cast<lv_color_t *>(this->original_));
#else
  if (this->original_owned_)
    std::free(const_cast<lv_color_t *>(this->original_));
#endif
  this->original_ = nullptr;
  this->original_owned_ = false;
  this->background_ = nullptr;
  this->background_lease_ = {};
  this->reserved_original_frame_ = nullptr;
  this->stable_background_ = false;
  this->font_ = nullptr;
  this->activation_patch_valid_ = false;
  this->prepared_ = false;
  this->active_.store(false, std::memory_order_release);
  this->presented_ = false;
  this->visual_pct_ = -1;
  this->value_pct_ = -1;
  this->reset_visual_cache_();
#ifdef USE_ESP32
  this->pending_update_.store(NO_PENDING_UPDATE, std::memory_order_release);
  this->last_requested_update_.store(NO_PENDING_UPDATE, std::memory_order_release);
  this->pending_requested_us_.store(0, std::memory_order_release);
#endif
}

bool MaterialDirectVolumeOverlay::suspend_scene_controllers_() {
  if (this->scene_controllers_suspended_)
    return true;
  for (size_t index = 0; index < this->scene_controller_count_; index++) {
    if (!this->scene_controllers_[index]->suspend_for_direct_overlay()) {
      for (size_t completed = 0; completed < index; completed++)
        this->scene_controllers_[completed]->resume_after_direct_overlay();
      return false;
    }
  }
  this->scene_controllers_suspended_ = true;
  return true;
}

void MaterialDirectVolumeOverlay::resume_scene_controllers_() {
  if (!this->scene_controllers_suspended_)
    return;
  this->scene_controllers_suspended_ = false;
  for (size_t index = 0; index < this->scene_controller_count_; index++)
    this->scene_controllers_[index]->resume_after_direct_overlay();
}

bool MaterialDirectVolumeOverlay::borrow_scene_frame_() {
  for (size_t index = 0; index < this->scene_controller_count_; index++) {
    lvgl::DirectSceneFrame frame{};
    if (!this->scene_controllers_[index]->get_direct_overlay_frame(frame))
      continue;
    if (frame.data == nullptr || frame.width != this->screen_width_ || frame.height != this->screen_height_ ||
        frame.stride != this->screen_width_ * static_cast<int>(sizeof(lv_color_t))) {
      ESP_LOGW(TAG, "Ignoring incompatible direct scene frame (%dx%d stride=%d)", frame.width, frame.height,
               frame.stride);
      continue;
    }
    this->original_ = reinterpret_cast<const lv_color_t *>(frame.data);
    this->original_owned_ = false;
    return true;
  }
  return false;
}

void MaterialDirectVolumeOverlay::end(bool restore_screen) {
#ifdef USE_ESP32
  this->quiesce_worker_();
  const uint32_t requests = this->stat_requests_.load(std::memory_order_relaxed);
  const uint32_t renders = this->stat_renders_.load(std::memory_order_relaxed);
  if (requests != 0 && renders != 0) {
    ESP_LOGI(TAG, "gesture perf: requests=%u renders=%u coalesced=%u age=%uus max_age=%uus render=%uus max_render=%uus",
             static_cast<unsigned>(requests), static_cast<unsigned>(renders),
             static_cast<unsigned>(this->stat_coalesced_.load(std::memory_order_relaxed)),
             static_cast<unsigned>(this->stat_total_age_us_.load(std::memory_order_relaxed) / renders),
             static_cast<unsigned>(this->stat_max_age_us_.load(std::memory_order_relaxed)),
             static_cast<unsigned>(this->stat_total_render_us_.load(std::memory_order_relaxed) / renders),
             static_cast<unsigned>(this->stat_max_render_us_.load(std::memory_order_relaxed)));
  }
#else
  this->active_ = false;
#endif
  if (restore_screen && this->presented_ && this->original_ != nullptr) {
    this->lvgl_component_->direct_blit_rgb888_ppa(reinterpret_cast<const uint8_t *>(this->original_),
                                                  this->screen_width_ * static_cast<int>(sizeof(lv_color_t)), 0, 0,
                                                  this->screen_width_, this->screen_height_);
  }
  if (this->arc_ != nullptr)
    lv_obj_clear_flag(this->arc_, LV_OBJ_FLAG_HIDDEN);
  if (this->knob_ != nullptr)
    lv_obj_clear_flag(this->knob_, LV_OBJ_FLAG_HIDDEN);
  if (this->label_ != nullptr)
    lv_obj_clear_flag(this->label_, LV_OBJ_FLAG_HIDDEN);
  this->release_();
  if (this->frame_buffer_presentation_active_) {
    if (!this->lvgl_component_->end_frame_buffer_presentation(100))
      ESP_LOGW(TAG, "Unable to release the direct overlay framebuffer session");
    this->frame_buffer_presentation_active_ = false;
  }
  // Hand the display back to a full-screen direct scene before LVGL
  // invalidation is re-enabled. Otherwise the ready refresh timer can present
  // the page's black backing object between the restored camera frame and the
  // presenter's next JPEG frame.
  this->resume_scene_controllers_();
  if (this->invalidation_suspended_ && this->display_ != nullptr) {
    lv_display_enable_invalidation(this->display_, true);
    this->invalidation_suspended_ = false;
  }
}

float MaterialDirectVolumeOverlay::coverage_sq_(float distance_sq, float radius) {
  const float inner = radius - 0.75f;
  const float outer = radius + 0.75f;
  const float inner_sq = inner * inner;
  const float outer_sq = outer * outer;
  if (distance_sq <= inner_sq)
    return 1.0f;
  if (distance_sq >= outer_sq)
    return 0.0f;
  return (outer_sq - distance_sq) / (outer_sq - inner_sq);
}

void MaterialDirectVolumeOverlay::blend_(lv_color_t &dst, lv_color_t color, float coverage) {
  if (coverage <= 0.0f)
    return;
  const int alpha = std::clamp(static_cast<int>(std::lround(coverage * 255.0f)), 0, 255);
  dst.red = static_cast<uint8_t>(
      (static_cast<int>(dst.red) * (255 - alpha) + static_cast<int>(color.red) * alpha + 127) / 255);
  dst.green = static_cast<uint8_t>(
      (static_cast<int>(dst.green) * (255 - alpha) + static_cast<int>(color.green) * alpha + 127) / 255);
  dst.blue = static_cast<uint8_t>(
      (static_cast<int>(dst.blue) * (255 - alpha) + static_cast<int>(color.blue) * alpha + 127) / 255);
}

void MaterialDirectVolumeOverlay::draw_capsule_(lv_color_t *buffer, int stride, int origin_x, int origin_y, int width,
                                                int height, float ax, float ay, float bx, float by, float radius,
                                                lv_color_t color) {
  const int x1 = std::max(origin_x, static_cast<int>(std::floor(std::min(ax, bx) - radius - 1.0f)));
  const int y1 = std::max(origin_y, static_cast<int>(std::floor(std::min(ay, by) - radius - 1.0f)));
  const int x2 = std::min(origin_x + width - 1, static_cast<int>(std::ceil(std::max(ax, bx) + radius + 1.0f)));
  const int y2 = std::min(origin_y + height - 1, static_cast<int>(std::ceil(std::max(ay, by) + radius + 1.0f)));
  if (x1 > x2 || y1 > y2)
    return;

  const float vx = bx - ax;
  const float vy = by - ay;
  const float length_sq = vx * vx + vy * vy;
  for (int screen_y = y1; screen_y <= y2; screen_y++) {
    lv_color_t *row = buffer + static_cast<size_t>(screen_y - origin_y) * stride;
    for (int screen_x = x1; screen_x <= x2; screen_x++) {
      const float px = static_cast<float>(screen_x) + 0.5f;
      const float py = static_cast<float>(screen_y) + 0.5f;
      float t = 0.0f;
      if (length_sq > 0.0f) {
        t = ((px - ax) * vx + (py - ay) * vy) / length_sq;
        t = std::clamp(t, 0.0f, 1.0f);
      }
      const float dx = px - (ax + t * vx);
      const float dy = py - (ay + t * vy);
      blend_(row[screen_x - origin_x], color, coverage_sq_(dx * dx + dy * dy, radius));
    }
  }
}

void MaterialDirectVolumeOverlay::draw_disc_(lv_color_t *buffer, int stride, int origin_x, int origin_y, int width,
                                             int height, float cx, float cy, float radius, lv_color_t color) {
  draw_capsule_(buffer, stride, origin_x, origin_y, width, height, cx, cy, cx, cy, radius, color);
}

void MaterialDirectVolumeOverlay::point_for_pct_(int pct, float &x, float &y) const {
  const float angle = (180.0f + static_cast<float>(clamp_pct(pct)) * 1.8f) * PI / 180.0f;
  x = this->center_x_ + std::cos(angle) * this->arc_radius_;
  y = this->center_y_ + std::sin(angle) * this->arc_radius_;
}

void MaterialDirectVolumeOverlay::redraw_track_in_region_(lv_color_t *buffer, int stride, int origin_x, int origin_y,
                                                          int width, int height, int first_pct, int last_pct,
                                                          lv_color_t color) {
  first_pct = std::clamp(first_pct, 0, 100);
  last_pct = std::clamp(last_pct, first_pct, 100);
  for (int pct = first_pct; pct < last_pct; pct++) {
    draw_capsule_(buffer, stride, origin_x, origin_y, width, height, this->point_x_[pct], this->point_y_[pct],
                  this->point_x_[pct + 1], this->point_y_[pct + 1], this->track_radius_, color);
  }
}

bool MaterialDirectVolumeOverlay::rebuild_background_() {
  if (this->original_ == nullptr || this->background_ == nullptr || this->screen_width_ <= 0 ||
      this->screen_height_ <= 0)
    return false;

  const size_t screen_pixel_count = static_cast<size_t>(this->screen_width_) * this->screen_height_;
  std::memcpy(this->background_, this->original_, screen_pixel_count * sizeof(lv_color_t));
  for (int pct = 0; pct <= 100; pct++)
    this->point_for_pct_(pct, this->point_x_[pct], this->point_y_[pct]);

  const int retained_opacity = 255 - this->scrim_opacity_;
  uint8_t dim_lut[256];
  for (int value = 0; value < 256; value++)
    dim_lut[value] = static_cast<uint8_t>((value * retained_opacity + 127) / 255);
  for (size_t i = 0; i < screen_pixel_count; i++) {
    this->background_[i].red = dim_lut[this->background_[i].red];
    this->background_[i].green = dim_lut[this->background_[i].green];
    this->background_[i].blue = dim_lut[this->background_[i].blue];
  }

  // Remove the activation widget from the prepared background without
  // recapturing and dimming the complete 800x800 frame in begin(). Render
  // only the small underlying screen rectangle, then apply the same scrim.
  if (this->activation_widget_was_visible_ && this->activation_widget_ != nullptr &&
      lv_obj_is_valid(this->activation_widget_)) {
    lv_area_t widget_area{};
    lv_obj_get_coords(this->activation_widget_, &widget_area);
    const int patch_x = std::clamp(static_cast<int>(widget_area.x1), 0, this->screen_width_ - 1);
    const int patch_y = std::clamp(static_cast<int>(widget_area.y1), 0, this->screen_height_ - 1);
    const int patch_x2 = std::clamp(static_cast<int>(widget_area.x2), patch_x, this->screen_width_ - 1);
    const int patch_y2 = std::clamp(static_cast<int>(widget_area.y2), patch_y, this->screen_height_ - 1);
    const int patch_width = patch_x2 - patch_x + 1;
    const int patch_height = patch_y2 - patch_y + 1;
    set_hidden_without_invalidation(this->activation_widget_, true);
    std::array<bool, MAX_SCENE_CONTROLLERS> background_render_active{};
    for (size_t index = 0; index < this->scene_controller_count_; index++) {
      background_render_active[index] = this->scene_controllers_[index]->begin_direct_overlay_background_render();
    }
    const int max_rows = std::max(1, static_cast<int>(this->scratch_capacity_ / patch_width));
    for (int row_offset = 0; this->display_ != nullptr && row_offset < patch_height; row_offset += max_rows) {
      const int rows = std::min(max_rows, patch_height - row_offset);
      const size_t patch_pixels = static_cast<size_t>(patch_width) * rows;
      if (!this->lvgl_component_->render_display_area_rgb888(
              this->display_, reinterpret_cast<uint8_t *>(this->scratch_),
              patch_width * static_cast<int>(sizeof(lv_color_t)), patch_x, patch_y + row_offset, patch_width, rows)) {
        break;
      }
      for (size_t pixel = 0; pixel < patch_pixels; pixel++) {
        this->scratch_[pixel].red = dim_lut[this->scratch_[pixel].red];
        this->scratch_[pixel].green = dim_lut[this->scratch_[pixel].green];
        this->scratch_[pixel].blue = dim_lut[this->scratch_[pixel].blue];
      }
      for (int row = 0; row < rows; row++) {
        std::memcpy(this->background_ + static_cast<size_t>(patch_y + row_offset + row) * this->screen_width_ + patch_x,
                    this->scratch_ + static_cast<size_t>(row) * patch_width,
                    static_cast<size_t>(patch_width) * sizeof(lv_color_t));
      }
    }
    for (size_t index = this->scene_controller_count_; index > 0; index--) {
      if (background_render_active[index - 1])
        this->scene_controllers_[index - 1]->end_direct_overlay_background_render();
    }
    const size_t patch_pixels = static_cast<size_t>(patch_width) * patch_height;
    if (this->activation_patch_ != nullptr && this->activation_patch_capacity_ >= patch_pixels) {
      for (int row = 0; row < patch_height; row++) {
        std::memcpy(this->activation_patch_ + static_cast<size_t>(row) * patch_width,
                    this->background_ + static_cast<size_t>(patch_y + row) * this->screen_width_ + patch_x,
                    static_cast<size_t>(patch_width) * sizeof(lv_color_t));
      }
      this->activation_patch_x_ = patch_x;
      this->activation_patch_y_ = patch_y;
      this->activation_patch_width_ = patch_width;
      this->activation_patch_height_ = patch_height;
      this->activation_patch_valid_ = true;
    }
    set_hidden_without_invalidation(this->activation_widget_, false);
  }
  this->redraw_track_in_region_(this->background_, this->screen_width_, 0, 0, this->screen_width_,
                                this->capture_height_, 0, 100, this->inactive_color_);
  return true;
}

bool MaterialDirectVolumeOverlay::compose_region_(lv_color_t *buffer, int stride, int origin_x, int origin_y,
                                                   int width, int height, int visual_pct, int value_pct) {
  if (buffer == nullptr || this->original_ == nullptr || stride < width || origin_x < 0 || origin_y < 0 ||
      width <= 0 || height <= 0 || origin_x + width > this->screen_width_ ||
      origin_y + height > this->capture_height_)
    return false;

  if (this->stable_background_ && this->background_ != nullptr) {
    for (int row = 0; row < height; row++) {
      const size_t source_offset = static_cast<size_t>(origin_y + row) * this->screen_width_ + origin_x;
      std::memcpy(buffer + static_cast<size_t>(row) * stride, this->background_ + source_offset,
                  static_cast<size_t>(width) * sizeof(lv_color_t));
    }
  } else {
    const int retained_opacity = 255 - this->scrim_opacity_;
    for (int row = 0; row < height; row++) {
      const size_t offset = static_cast<size_t>(origin_y + row) * this->screen_width_ + origin_x;
      auto *target = buffer + static_cast<size_t>(row) * stride;
      for (int column = 0; column < width; column++) {
        const auto &source = this->original_[offset + column];
        target[column].red = static_cast<uint8_t>((static_cast<int>(source.red) * retained_opacity + 127) / 255);
        target[column].green = static_cast<uint8_t>((static_cast<int>(source.green) * retained_opacity + 127) / 255);
        target[column].blue = static_cast<uint8_t>((static_cast<int>(source.blue) * retained_opacity + 127) / 255);
      }
    }
  }

  if (!this->stable_background_ && this->activation_patch_valid_ && this->activation_patch_ != nullptr) {
    const int patch_x1 = std::max(origin_x, this->activation_patch_x_);
    const int patch_y1 = std::max(origin_y, this->activation_patch_y_);
    const int patch_x2 = std::min(origin_x + width, this->activation_patch_x_ + this->activation_patch_width_);
    const int patch_y2 = std::min(origin_y + height, this->activation_patch_y_ + this->activation_patch_height_);
    for (int screen_y = patch_y1; screen_y < patch_y2; screen_y++) {
      const int source_y = screen_y - this->activation_patch_y_;
      const int source_x = patch_x1 - this->activation_patch_x_;
      std::memcpy(buffer + static_cast<size_t>(screen_y - origin_y) * stride + patch_x1 - origin_x,
                  this->activation_patch_ + static_cast<size_t>(source_y) * this->activation_patch_width_ + source_x,
                  static_cast<size_t>(patch_x2 - patch_x1) * sizeof(lv_color_t));
    }
  }

  if (!this->stable_background_)
    this->redraw_track_in_region_(buffer, stride, origin_x, origin_y, width, height, 0, 100, this->inactive_color_);
  if (visual_pct >= 0) {
    visual_pct = clamp_pct(visual_pct);
    this->redraw_track_in_region_(buffer, stride, origin_x, origin_y, width, height, 0, visual_pct,
                                  this->active_color_);
    draw_disc_(buffer, stride, origin_x, origin_y, width, height, this->point_x_[visual_pct],
               this->point_y_[visual_pct], this->knob_radius_, this->knob_color_);
  }
  return value_pct < 0 ||
         this->draw_value_into_region_(buffer, stride, origin_x, origin_y, width, height, value_pct);
}

bool MaterialDirectVolumeOverlay::present_composed_region_(int x, int y, int width, int height, int visual_pct,
                                                             int value_pct) {
  if (this->scratch_ == nullptr || this->lvgl_component_ == nullptr || width <= 0 || height <= 0)
    return false;
  x = std::clamp(x, 0, this->screen_width_);
  y = std::clamp(y, 0, this->capture_height_);
  width = std::min(width, this->screen_width_ - x);
  height = std::min(height, this->capture_height_ - y);
  if (width <= 0 || height <= 0)
    return false;

  const int band_rows = std::max(1, static_cast<int>(this->scratch_capacity_ / static_cast<size_t>(width)));
  for (int row = 0; row < height; row += band_rows) {
    const int rows = std::min(band_rows, height - row);
    if (!this->compose_region_(this->scratch_, width, x, y + row, width, rows, visual_pct, value_pct) ||
        !this->lvgl_component_->direct_blit_rgb888_ppa_dma_target(
            reinterpret_cast<const uint8_t *>(this->scratch_), width * static_cast<int>(sizeof(lv_color_t)), x,
            y + row, width, rows)) {
      return false;
    }
  }
  return true;
}

bool MaterialDirectVolumeOverlay::restore_background_region_(int x, int y, int width, int height) {
  if (this->background_ == nullptr || this->scratch_ == nullptr || width <= 0 || height <= 0)
    return false;
  x = std::clamp(x, 0, this->screen_width_);
  y = std::clamp(y, 0, this->capture_height_);
  width = std::min(width, this->screen_width_ - x);
  height = std::min(height, this->capture_height_ - y);
  if (width <= 0 || height <= 0)
    return false;

  const int band_rows = std::max(1, static_cast<int>(this->scratch_capacity_ / static_cast<size_t>(width)));
  for (int row = 0; row < height; row += band_rows) {
    const int rows = std::min(band_rows, height - row);
    if (!this->compose_region_(this->scratch_, width, x, y + row, width, rows, -1, -1))
      return false;
    for (int local_y = 0; local_y < rows; local_y++) {
      std::memcpy(this->background_ + static_cast<size_t>(y + row + local_y) * this->screen_width_ + x,
                  this->scratch_ + static_cast<size_t>(local_y) * width,
                  static_cast<size_t>(width) * sizeof(lv_color_t));
    }
  }
  return true;
}

bool MaterialDirectVolumeOverlay::prepare() {
  // Some touch controllers can report the same uninterrupted contact as a new
  // touch after a direct-scene handoff. Preparing twice must not tear down an
  // overlay that is already visible or discard its captured scene.
  if (this->active_.load(std::memory_order_acquire) || this->prepared_)
    return true;
  this->end();
  static_assert(sizeof(lv_color_t) == 3, "The direct volume overlay requires RGB888");
  if (!this->update_geometry_())
    return false;

  // Quiesce full-screen direct producers before leasing an idle DSI buffer.
  // The currently scanned buffer then remains a stable, allocation-free copy
  // of the original scene for the complete gesture.
  if (!this->suspend_scene_controllers_()) {
    ESP_LOGW(TAG, "Unable to suspend the active direct scene");
    return false;
  }

  if (!this->lvgl_component_->begin_frame_buffer_presentation(100)) {
    ESP_LOGW(TAG, "Unable to acquire the direct overlay framebuffer session");
    this->resume_scene_controllers_();
    return false;
  }
  this->frame_buffer_presentation_active_ = true;

  display::FrameBufferView active_frame{};
  if (!this->lvgl_component_->get_presentation_active_frame(&active_frame, BufferReader::CPU) ||
      active_frame.data == nullptr || active_frame.width != static_cast<size_t>(this->screen_width_) ||
      active_frame.height != static_cast<size_t>(this->screen_height_) ||
      active_frame.stride != static_cast<size_t>(this->screen_width_) * sizeof(lv_color_t) ||
      active_frame.size < active_frame.stride * active_frame.height ||
      !this->lvgl_component_->acquire_presentation_frame(&this->background_lease_, BufferWriter::CPU, 100) ||
      this->background_lease_.width != active_frame.width || this->background_lease_.height != active_frame.height ||
      this->background_lease_.stride != active_frame.stride || this->background_lease_.size < active_frame.size) {
    ESP_LOGW(TAG, "Unable to lease compatible RGB888 presentation buffers");
    this->release_();
    this->lvgl_component_->end_frame_buffer_presentation(100);
    this->frame_buffer_presentation_active_ = false;
    this->resume_scene_controllers_();
    return false;
  }

  this->original_ = reinterpret_cast<const lv_color_t *>(active_frame.data);
  this->original_owned_ = false;
  this->background_ = reinterpret_cast<lv_color_t *>(this->background_lease_.data);
  const size_t scratch_byte_count = this->scratch_capacity_ * sizeof(lv_color_t);
  if (!this->ensure_persistent_resources_()) {
    ESP_LOGW(TAG, "Unable to allocate direct overlay scratch buffer (%uB)",
             static_cast<unsigned>(scratch_byte_count));
#ifdef USE_ESP32
    ESP_LOGW(TAG, "PSRAM after overlay allocation failure: free=%uB largest=%uB",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
#endif
    this->release_();
    this->lvgl_component_->end_frame_buffer_presentation(100);
    this->frame_buffer_presentation_active_ = false;
    this->resume_scene_controllers_();
    return false;
  }

  if (!this->rebuild_background_()) {
    this->release_();
    this->lvgl_component_->end_frame_buffer_presentation(100);
    this->frame_buffer_presentation_active_ = false;
    this->resume_scene_controllers_();
    return false;
  }

  this->prepared_ = true;
  this->visual_pct_ = -1;
  this->value_pct_ = -1;
  this->reset_visual_cache_();
  ESP_LOGD(TAG, "Prepared direct overlay from DSI pool: source=%p target=%p scratch=%uB", this->original_,
           this->background_, static_cast<unsigned>(scratch_byte_count));
  return true;
}

bool MaterialDirectVolumeOverlay::begin(int value_pct, int visual_pct) {
#ifdef USE_ESP32
  if (!this->ensure_worker_()) {
    ESP_LOGW(TAG, "Unable to start direct volume worker");
    return false;
  }
#endif
  if (!this->prepared_ && !this->prepare())
    return false;

  lv_color_t *prepared_background = this->background_;
  const bool has_initial_state = value_pct >= 0 && visual_pct >= 0;
  if (has_initial_state) {
    value_pct = clamp_pct(value_pct);
    visual_pct = clamp_pct(visual_pct);
    this->redraw_track_in_region_(prepared_background, this->screen_width_, 0, 0, this->screen_width_,
                                  this->capture_height_, 0, visual_pct, this->active_color_);
    draw_disc_(prepared_background, this->screen_width_, 0, 0, this->screen_width_, this->capture_height_,
               this->point_x_[visual_pct], this->point_y_[visual_pct], this->knob_radius_, this->knob_color_);
    if (!this->draw_value_into_region_(prepared_background, this->screen_width_, 0, 0, this->screen_width_,
                                       this->capture_height_, value_pct)) {
      ESP_LOGW(TAG, "Unable to compose the initial volume value");
      this->end(false);
      return false;
    }
  }
  if (!this->frame_buffer_presentation_active_ || prepared_background == nullptr || !this->background_lease_ ||
      !this->lvgl_component_->present_presentation_frame(&this->background_lease_, 100)) {
    ESP_LOGW(TAG, "Unable to present the prepared direct overlay framebuffer");
    this->end(false);
    return false;
  }

  // Preserve the prepared scrim and inactive arc in the third DSI buffer.
  // Touch updates then restore only their damaged rectangle from this stable
  // frame instead of re-dimming pixels and rasterizing the inactive arc.
  this->background_ = nullptr;
  const auto *original_frame = reinterpret_cast<const uint8_t *>(this->original_);
  if (this->lvgl_component_->reserve_presentation_frame(original_frame)) {
    this->reserved_original_frame_ = original_frame;
    if (this->lvgl_component_->acquire_presentation_frame(&this->background_lease_, BufferWriter::DMA, 100) &&
      this->lvgl_component_->copy_presentation_frame_rgb888(
            reinterpret_cast<const uint8_t *>(prepared_background), this->background_lease_.data,
            this->screen_width_, this->screen_height_)) {
      this->background_ = reinterpret_cast<lv_color_t *>(this->background_lease_.data);
      bool background_ready = true;
      if (has_initial_state) {
        int damage_x1 = this->label_x_;
        int damage_y1 = this->label_y_;
        int damage_x2 = this->label_x_ + this->label_width_ - 1;
        int damage_y2 = this->label_y_ + this->label_height_ - 1;
        const float track_damage_radius = std::max(this->track_radius_, this->knob_radius_) + 2.0f;
        for (int pct = 0; pct <= visual_pct; pct++) {
          damage_x1 = std::min(damage_x1,
                               static_cast<int>(std::floor(this->point_x_[pct] - track_damage_radius)));
          damage_y1 = std::min(damage_y1,
                               static_cast<int>(std::floor(this->point_y_[pct] - track_damage_radius)));
          damage_x2 = std::max(damage_x2,
                               static_cast<int>(std::ceil(this->point_x_[pct] + track_damage_radius)));
          damage_y2 = std::max(damage_y2,
                               static_cast<int>(std::ceil(this->point_y_[pct] + track_damage_radius)));
        }
        damage_x1 = std::clamp(damage_x1, 0, this->screen_width_ - 1);
        damage_y1 = std::clamp(damage_y1, 0, this->capture_height_ - 1);
        damage_x2 = std::clamp(damage_x2, damage_x1, this->screen_width_ - 1);
        damage_y2 = std::clamp(damage_y2, damage_y1, this->capture_height_ - 1);
        background_ready = this->restore_background_region_(damage_x1, damage_y1, damage_x2 - damage_x1 + 1,
                                                             damage_y2 - damage_y1 + 1);
      }
      if (!background_ready)
        background_ready = this->rebuild_background_();
      this->stable_background_ = background_ready;
    } else {
      if (this->background_lease_)
        this->lvgl_component_->release_presentation_frame(&this->background_lease_);
      this->lvgl_component_->release_reserved_presentation_frame(this->reserved_original_frame_);
      this->reserved_original_frame_ = nullptr;
      ESP_LOGW(TAG, "Unable to retain the prepared DSI background; using the compatible raster fallback");
    }
  }

  lv_obj_add_flag(this->arc_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(this->knob_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(this->label_, LV_OBJ_FLAG_HIDDEN);

  // The overlay writes directly to the display framebuffers. Native LVGL
  // refreshes (for example the one-second clock tick or gallery controls) must
  // not write a competing frame until the original scene has been restored.
  if (this->display_ != nullptr && lv_display_is_invalidation_enabled(this->display_)) {
    lv_display_enable_invalidation(this->display_, false);
    this->invalidation_suspended_ = true;
  }

  this->active_.store(true, std::memory_order_release);
#ifdef USE_ESP32
  this->pending_update_.store(NO_PENDING_UPDATE, std::memory_order_release);
  this->last_requested_update_.store(
      has_initial_state ? ((value_pct << 8) | visual_pct) : NO_PENDING_UPDATE, std::memory_order_release);
  this->worker_failed_.store(false, std::memory_order_release);
  this->pending_requested_us_.store(0, std::memory_order_release);
  this->stat_requests_.store(0, std::memory_order_release);
  this->stat_renders_.store(0, std::memory_order_release);
  this->stat_coalesced_.store(0, std::memory_order_release);
  this->stat_total_age_us_.store(0, std::memory_order_release);
  this->stat_max_age_us_.store(0, std::memory_order_release);
  this->stat_total_render_us_.store(0, std::memory_order_release);
  this->stat_max_render_us_.store(0, std::memory_order_release);
  this->ensure_worker_();
#endif
  this->visual_pct_ = has_initial_state ? visual_pct : -1;
  this->value_pct_ = has_initial_state ? value_pct : -1;
  this->visual_cache_ = has_initial_state ? VisualCache{value_pct, visual_pct} : VisualCache{};
  this->presented_ = true;
  return true;
}

void MaterialDirectVolumeOverlay::cancel_prepare() {
  if (!this->active_)
    this->end(false);
}

bool MaterialDirectVolumeOverlay::draw_value_into_region_(lv_color_t *buffer, int stride, int origin_x, int origin_y,
                                                           int width, int height, int value_pct) {
  if (buffer == nullptr || stride < width || width <= 0 || height <= 0 || this->font_ == nullptr ||
      this->glyph_buffer_ == nullptr)
    return false;
  value_pct = clamp_pct(value_pct);

  char text[8];
  std::snprintf(text, sizeof(text), "%d%%", value_pct);
  lv_font_glyph_dsc_t glyphs[4]{};
  int glyph_count = 0;
  int text_width = 0;
  for (int i = 0; text[i] != '\0' && glyph_count < 4; i++) {
    const uint32_t next = text[i + 1] == '\0' ? 0 : static_cast<uint8_t>(text[i + 1]);
    if (!lv_font_get_glyph_dsc(this->font_, &glyphs[glyph_count], static_cast<uint8_t>(text[i]), next))
      continue;
    text_width += glyphs[glyph_count].adv_w;
    if (text[i + 1] != '\0')
      text_width += this->letter_space_;
    glyph_count++;
  }

  int pen_x = static_cast<int>(std::lround(this->center_x_)) - text_width / 2;
  const int line_y = static_cast<int>(std::lround(this->center_y_)) - static_cast<int>(this->font_->line_height) / 2;
  for (int i = 0; i < glyph_count; i++) {
    auto &glyph = glyphs[i];
    const auto *draw_buf = static_cast<const lv_draw_buf_t *>(lv_font_get_glyph_bitmap(&glyph, this->glyph_buffer_));
    if (draw_buf != nullptr && glyph.box_w > 0 && glyph.box_h > 0) {
      const int glyph_x = pen_x + glyph.ofs_x;
      const int glyph_y = line_y + (this->font_->line_height - this->font_->base_line) - glyph.box_h - glyph.ofs_y;
      const int glyph_stride = lv_draw_buf_width_to_stride(glyph.box_w, LV_COLOR_FORMAT_A8);
      for (int gy = 0; gy < glyph.box_h; gy++) {
        const int screen_y = glyph_y + gy;
        if (screen_y < origin_y || screen_y >= origin_y + height)
          continue;
        const uint8_t *alpha_row = draw_buf->data + static_cast<size_t>(gy) * glyph_stride;
        lv_color_t *dest_row = buffer + static_cast<size_t>(screen_y - origin_y) * stride;
        for (int gx = 0; gx < glyph.box_w; gx++) {
          const int screen_x = glyph_x + gx;
          if (screen_x < origin_x || screen_x >= origin_x + width)
            continue;
          blend_(dest_row[screen_x - origin_x], this->active_color_, static_cast<float>(alpha_row[gx]) / 255.0f);
        }
      }
    }
    pen_x += glyph.adv_w + this->letter_space_;
    lv_font_glyph_release_draw_data(&glyph);
  }
  return true;
}

bool MaterialDirectVolumeOverlay::render_frame_(int visual_pct, int value_pct) {
  if (!this->active_.load(std::memory_order_acquire) || this->original_ == nullptr || this->scratch_ == nullptr)
    return false;
  visual_pct = clamp_pct(visual_pct);
  value_pct = clamp_pct(value_pct);

  // Recompose only the pixels damaged by the old knob and the changed track
  // segment. The stable scanout buffer retained at prepare() is the source,
  // so this path needs no full-screen heap allocation and never accumulates
  // rounding or stale-pixel errors between touch samples.
  int damage_x1 = this->screen_width_;
  int damage_y1 = this->capture_height_;
  int damage_x2 = -1;
  int damage_y2 = -1;
  auto include_point = [&](float x, float y, float radius) {
    damage_x1 = std::min(damage_x1, static_cast<int>(std::floor(x - radius)));
    damage_y1 = std::min(damage_y1, static_cast<int>(std::floor(y - radius)));
    damage_x2 = std::max(damage_x2, static_cast<int>(std::ceil(x + radius)));
    damage_y2 = std::max(damage_y2, static_cast<int>(std::ceil(y + radius)));
  };

  const bool visual_changed = this->visual_pct_ != visual_pct;
  const bool value_changed = this->value_pct_ != value_pct;
  if (visual_changed) {
    const int first = this->visual_pct_ < 0 ? 0 : std::min(this->visual_pct_, visual_pct);
    const int last = this->visual_pct_ < 0 ? visual_pct : std::max(this->visual_pct_, visual_pct);
    const float track_damage_radius = this->track_radius_ + 2.0f;
    for (int pct = first; pct <= last; pct++) {
      include_point(this->point_x_[pct], this->point_y_[pct], track_damage_radius);
    }
    if (this->visual_pct_ >= 0)
      include_point(this->point_x_[this->visual_pct_], this->point_y_[this->visual_pct_], this->knob_radius_ + 2.0f);
    include_point(this->point_x_[visual_pct], this->point_y_[visual_pct], this->knob_radius_ + 2.0f);
  }

  bool presented = true;
  if (visual_changed && damage_x2 >= damage_x1 && damage_y2 >= damage_y1) {
    damage_x1 = std::clamp(damage_x1, 0, this->screen_width_ - 1);
    damage_y1 = std::clamp(damage_y1, 0, this->capture_height_ - 1);
    damage_x2 = std::clamp(damage_x2, damage_x1, this->screen_width_ - 1);
    damage_y2 = std::clamp(damage_y2, damage_y1, this->capture_height_ - 1);
    const int label_x2 = this->label_x_ + this->label_width_ - 1;
    const int label_y2 = this->label_y_ + this->label_height_ - 1;
    const int union_x1 = std::min(damage_x1, this->label_x_);
    const int union_y1 = std::min(damage_y1, this->label_y_);
    const int union_x2 = std::max(damage_x2, label_x2);
    const int union_y2 = std::max(damage_y2, label_y2);
    const int64_t union_area = static_cast<int64_t>(union_x2 - union_x1 + 1) * (union_y2 - union_y1 + 1);
    const int64_t separate_area =
        static_cast<int64_t>(damage_x2 - damage_x1 + 1) * (damage_y2 - damage_y1 + 1) +
        static_cast<int64_t>(this->label_width_) * this->label_height_;
    if (value_changed && union_area * 4 <= separate_area * 5) {
      presented = this->present_composed_region_(union_x1, union_y1, union_x2 - union_x1 + 1,
                                                 union_y2 - union_y1 + 1, visual_pct, value_pct);
    } else {
      presented = this->present_composed_region_(damage_x1, damage_y1, damage_x2 - damage_x1 + 1,
                                                 damage_y2 - damage_y1 + 1, visual_pct, value_pct);
      if (presented && value_changed)
        presented = this->present_composed_region_(this->label_x_, this->label_y_, this->label_width_,
                                                   this->label_height_, visual_pct, value_pct);
    }
  } else if (value_changed) {
    presented = this->present_composed_region_(this->label_x_, this->label_y_, this->label_width_,
                                               this->label_height_, visual_pct, value_pct);
  }
  if (!presented)
    return false;
  this->visual_pct_ = visual_pct;
  this->value_pct_ = value_pct;
  return true;
}

bool MaterialDirectVolumeOverlay::direct_update_(int visual_pct, int value_pct) {
  if (!this->active_.load(std::memory_order_acquire))
    return false;
  visual_pct = clamp_pct(visual_pct);
  value_pct = clamp_pct(value_pct);
  if (visual_pct == this->visual_pct_ && value_pct == this->value_pct_)
    return true;
  return this->render_frame_(visual_pct, value_pct);
}

void MaterialDirectVolumeOverlay::update_native_(int value_pct, int visual_pct) {
  lv_arc_set_value(this->arc_, visual_pct);
  char text[8];
  std::snprintf(text, sizeof(text), "%d%%", value_pct);
  lv_label_set_text(this->label_, text);
  float x;
  float y;
  this->point_for_pct_(visual_pct, x, y);
  lv_obj_set_pos(this->knob_, static_cast<int>(std::lround(x - lv_obj_get_width(this->knob_) * 0.5f)),
                 static_cast<int>(std::lround(y - lv_obj_get_height(this->knob_) * 0.5f)));
}

void MaterialDirectVolumeOverlay::update(int value_pct, int visual_pct) {
  value_pct = clamp_pct(value_pct);
  visual_pct = clamp_pct(visual_pct);
#ifdef USE_ESP32
  if (this->active_.load(std::memory_order_acquire) && this->worker_handle_ != nullptr) {
    const int packed = (value_pct << 8) | visual_pct;
    if (packed == this->last_requested_update_.exchange(packed, std::memory_order_acq_rel))
      return;
    this->stat_requests_.fetch_add(1, std::memory_order_relaxed);
    const int replaced = this->pending_update_.exchange(packed, std::memory_order_acq_rel);
    if (replaced != NO_PENDING_UPDATE)
      this->stat_coalesced_.fetch_add(1, std::memory_order_relaxed);
    this->pending_requested_us_.store(micros(), std::memory_order_release);
    xTaskNotifyGive(this->worker_handle_);
    return;
  }
#endif
  if (value_pct == this->visual_cache_.value_pct && visual_pct == this->visual_cache_.visual_pct)
    return;
  this->visual_cache_.value_pct = value_pct;
  this->visual_cache_.visual_pct = visual_pct;
  if (!this->direct_update_(visual_pct, value_pct))
    this->update_native_(value_pct, visual_pct);
}

#ifdef USE_ESP32
void MaterialDirectVolumeOverlay::worker_(void *arg) {
  auto *overlay = static_cast<MaterialDirectVolumeOverlay *>(arg);
  if (overlay == nullptr) {
    vTaskDelete(nullptr);
    return;
  }
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    while (overlay->active_.load(std::memory_order_acquire)) {
      const int packed = overlay->pending_update_.exchange(NO_PENDING_UPDATE, std::memory_order_acq_rel);
      if (packed == NO_PENDING_UPDATE)
        break;
      overlay->worker_busy_.store(true, std::memory_order_release);
      const uint32_t started_us = micros();
      const uint32_t requested_us = overlay->pending_requested_us_.load(std::memory_order_acquire);
      const uint32_t age_us = started_us - requested_us;
      const bool rendered = overlay->direct_update_(packed & 0xFF, (packed >> 8) & 0xFF);
      const uint32_t render_us = micros() - started_us;
      overlay->stat_renders_.fetch_add(1, std::memory_order_relaxed);
      overlay->stat_total_age_us_.fetch_add(age_us, std::memory_order_relaxed);
      overlay->stat_total_render_us_.fetch_add(render_us, std::memory_order_relaxed);
      update_atomic_max(overlay->stat_max_age_us_, age_us);
      update_atomic_max(overlay->stat_max_render_us_, render_us);
      overlay->worker_busy_.store(false, std::memory_order_release);
      if (!rendered) {
        overlay->worker_failed_.store(true, std::memory_order_release);
        break;
      }
    }
  }
}

bool MaterialDirectVolumeOverlay::ensure_worker_() {
  if (this->worker_handle_ != nullptr)
    return true;
#if CONFIG_FREERTOS_UNICORE
  constexpr BaseType_t worker_core = tskNO_AFFINITY;
#else
  const BaseType_t worker_core = esp32::background_task_core(-1);
#endif
  constexpr uint32_t WORKER_STACK_SIZE = 4096;
  this->worker_stack_ =
      static_cast<StackType_t *>(heap_caps_aligned_alloc(16, WORKER_STACK_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (this->worker_stack_ == nullptr) {
    this->worker_stack_ =
        static_cast<StackType_t *>(heap_caps_aligned_alloc(16, WORKER_STACK_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  }
  if (this->worker_stack_ == nullptr)
    return false;
  this->worker_handle_ = xTaskCreateStaticPinnedToCore(worker_, "material_volume", WORKER_STACK_SIZE, this, 3,
                                                       this->worker_stack_, &this->worker_storage_, worker_core);
  if (this->worker_handle_ == nullptr) {
    heap_caps_free(this->worker_stack_);
    this->worker_stack_ = nullptr;
    return false;
  }
  return true;
}

void MaterialDirectVolumeOverlay::quiesce_worker_() {
  this->active_.store(false, std::memory_order_release);
  this->pending_update_.store(NO_PENDING_UPDATE, std::memory_order_release);
  if (this->worker_handle_ != nullptr)
    xTaskNotifyGive(this->worker_handle_);
  const uint32_t started = millis();
  while (this->worker_busy_.load(std::memory_order_acquire) && millis() - started < 100U)
    delay(1);
}
#endif

void MaterialDirectVolumeOverlay::update_drag_point(int value_pct, int visual_pct, int touch_x, int touch_y) {
  (void) touch_x;
  (void) touch_y;
  this->update(value_pct, visual_pct);
}

int MaterialDirectVolumeOverlay::arc_pct_from_x(int touch_x) const {
  float ratio = (static_cast<float>(touch_x) - this->center_x_) / this->arc_radius_;
  ratio = std::clamp(ratio, -1.0f, 1.0f);
  const float angle = 360.0f - std::acos(ratio) * 180.0f / PI;
  return clamp_pct(static_cast<int>(std::lround((angle - 180.0f) / 1.8f)));
}

int MaterialDirectVolumeOverlay::arc_pct_from_point(int touch_x, int touch_y) const {
  float angle =
      std::atan2(static_cast<float>(touch_y) - this->center_y_, static_cast<float>(touch_x) - this->center_x_) *
      180.0f / PI;
  if (angle < 0.0f)
    angle += 360.0f;
  if (angle < 180.0f)
    angle = touch_x < static_cast<int>(this->center_x_) ? 180.0f : 360.0f;
  return clamp_pct(static_cast<int>(std::lround((angle - 180.0f) / 1.8f)));
}

int MaterialDirectVolumeOverlay::drag_value_from_x(int touch_x, int start_x, int start_pct) const {
  const int touch_pct = this->arc_pct_from_x(touch_x);
  const int start_touch_pct = this->arc_pct_from_x(start_x);
  start_pct = clamp_pct(start_pct);
  if (touch_pct >= start_touch_pct) {
    const int span = 100 - start_touch_pct;
    if (span <= 0)
      return 100;
    return clamp_pct(start_pct + ((touch_pct - start_touch_pct) * (100 - start_pct) + span / 2) / span);
  }
  const int span = start_touch_pct;
  if (span <= 0)
    return 0;
  return clamp_pct(start_pct - ((start_touch_pct - touch_pct) * start_pct + span / 2) / span);
}

int MaterialDirectVolumeOverlay::drag_value_from_visual_pct(int visual_pct, int start_pct) const {
  visual_pct = clamp_pct(visual_pct);
  start_pct = clamp_pct(start_pct);
  if (visual_pct >= 50)
    return clamp_pct(start_pct + ((visual_pct - 50) * (100 - start_pct) + 25) / 50);
  return clamp_pct(start_pct - ((50 - visual_pct) * start_pct + 25) / 50);
}

}  // namespace esphome::lvgl_material
