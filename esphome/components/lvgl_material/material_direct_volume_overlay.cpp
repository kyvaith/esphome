#include "material_direct_volume_overlay.h"

#include "esphome/core/log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef USE_ESP32
#include "esp_heap_caps.h"
#endif

namespace esphome::lvgl_material {

static const char *const TAG = "lvgl_material.volume";
static constexpr float PI = 3.14159265358979323846f;

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
  }
}

void MaterialDirectVolumeOverlay::on_shutdown() { this->end(false); }

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

  lv_display_t *display = lv_obj_get_display(this->arc_);
  if (display == nullptr)
    return false;
  this->screen_width_ = lv_display_get_horizontal_resolution(display);
  this->screen_height_ = lv_display_get_vertical_resolution(display);
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
  this->scratch_capacity_ = std::max(static_cast<size_t>(this->screen_width_) * this->capture_height_,
                                     static_cast<size_t>(this->label_width_) * this->label_height_);
  return this->scratch_capacity_ != 0;
}

void MaterialDirectVolumeOverlay::release_() {
  if (this->glyph_buffer_ != nullptr)
    lv_draw_buf_destroy(this->glyph_buffer_);
#ifdef USE_ESP32
  if (this->original_ != nullptr)
    heap_caps_free(this->original_);
  if (this->background_ != nullptr)
    heap_caps_free(this->background_);
  if (this->scratch_ != nullptr)
    heap_caps_free(this->scratch_);
#else
  std::free(this->original_);
  std::free(this->background_);
  std::free(this->scratch_);
#endif
  this->original_ = nullptr;
  this->background_ = nullptr;
  this->scratch_ = nullptr;
  this->glyph_buffer_ = nullptr;
  this->font_ = nullptr;
  this->scratch_capacity_ = 0;
  this->prepared_ = false;
  this->active_ = false;
  this->presented_ = false;
  this->visual_pct_ = -1;
  this->value_pct_ = -1;
  this->reset_visual_cache_();
}

void MaterialDirectVolumeOverlay::end(bool restore_screen) {
  if (restore_screen && this->presented_ && this->original_ != nullptr) {
    this->lvgl_component_->direct_blit_rgb888(reinterpret_cast<const uint8_t *>(this->original_),
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
  this->redraw_track_in_region_(this->background_, this->screen_width_, 0, 0, this->screen_width_,
                                this->capture_height_, 0, 100, this->inactive_color_);
  return true;
}

bool MaterialDirectVolumeOverlay::prepare() {
  this->end();
  static_assert(sizeof(lv_color_t) == 3, "The direct volume overlay requires RGB888");
  if (!this->update_geometry_())
    return false;

  const size_t screen_pixel_count = static_cast<size_t>(this->screen_width_) * this->screen_height_;
  const size_t screen_byte_count = screen_pixel_count * sizeof(lv_color_t);
  const size_t scratch_byte_count = this->scratch_capacity_ * sizeof(lv_color_t);
#ifdef USE_ESP32
  this->original_ = static_cast<lv_color_t *>(heap_caps_malloc(screen_byte_count, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  this->background_ =
      static_cast<lv_color_t *>(heap_caps_malloc(screen_byte_count, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  this->scratch_ = static_cast<lv_color_t *>(heap_caps_malloc(scratch_byte_count, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
  this->original_ = static_cast<lv_color_t *>(std::malloc(screen_byte_count));
  this->background_ = static_cast<lv_color_t *>(std::malloc(screen_byte_count));
  this->scratch_ = static_cast<lv_color_t *>(std::malloc(scratch_byte_count));
#endif
  const int glyph_width = std::max(64, static_cast<int>(this->font_->line_height) * 2);
  const int glyph_height = std::max(64, static_cast<int>(this->font_->line_height) + 48);
  this->glyph_buffer_ = lv_draw_buf_create(glyph_width, glyph_height, LV_COLOR_FORMAT_A8, LV_STRIDE_AUTO);
  if (this->original_ == nullptr || this->background_ == nullptr || this->scratch_ == nullptr ||
      this->glyph_buffer_ == nullptr ||
      !this->lvgl_component_->direct_capture_rgb888(reinterpret_cast<uint8_t *>(this->original_),
                                                    this->screen_width_ * static_cast<int>(sizeof(lv_color_t)), 0, 0,
                                                    this->screen_width_, this->screen_height_)) {
    this->release_();
    return false;
  }

  if (!this->rebuild_background_()) {
    this->release_();
    return false;
  }

  this->prepared_ = true;
  this->visual_pct_ = -1;
  this->value_pct_ = -1;
  this->reset_visual_cache_();
  return true;
}

bool MaterialDirectVolumeOverlay::begin() {
  if (!this->prepared_ && !this->prepare())
    return false;

  lv_obj_add_flag(this->arc_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(this->knob_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(this->label_, LV_OBJ_FLAG_HIDDEN);

  if (this->activation_widget_ != nullptr) {
    // The activation widget is hidden immediately before begin(). Recapture
    // the whole frame so the dimmed background remains uniform. Patching only
    // the former clock rectangle used a different source frame and left a
    // visible rectangular cutout across the volume arc.
    if (!this->lvgl_component_->direct_capture_rgb888(
            reinterpret_cast<uint8_t *>(this->original_),
            this->screen_width_ * static_cast<int>(sizeof(lv_color_t)), 0, 0, this->screen_width_,
            this->screen_height_) ||
        !this->rebuild_background_()) {
      this->end(false);
      return false;
    }
  }

  this->active_ = true;
  this->visual_pct_ = -1;
  this->value_pct_ = -1;
  this->reset_visual_cache_();
  if (!this->lvgl_component_->direct_blit_rgb888(reinterpret_cast<const uint8_t *>(this->background_),
                                                 this->screen_width_ * static_cast<int>(sizeof(lv_color_t)), 0, 0,
                                                 this->screen_width_, this->screen_height_)) {
    this->end(false);
    return false;
  }
  this->presented_ = true;
  return true;
}

void MaterialDirectVolumeOverlay::cancel_prepare() {
  if (!this->active_)
    this->end(false);
}

bool MaterialDirectVolumeOverlay::direct_draw_value_(int value_pct) {
  if (!this->active_ || this->font_ == nullptr || this->glyph_buffer_ == nullptr)
    return false;
  value_pct = clamp_pct(value_pct);
  if (value_pct == this->value_pct_)
    return true;

  for (int y = 0; y < this->label_height_; y++) {
    std::memcpy(this->scratch_ + static_cast<size_t>(y) * this->label_width_,
                this->background_ + static_cast<size_t>(this->label_y_ + y) * this->screen_width_ + this->label_x_,
                static_cast<size_t>(this->label_width_) * sizeof(lv_color_t));
  }

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
        if (screen_y < this->label_y_ || screen_y >= this->label_y_ + this->label_height_)
          continue;
        const uint8_t *alpha_row = draw_buf->data + static_cast<size_t>(gy) * glyph_stride;
        lv_color_t *dest_row = this->scratch_ + static_cast<size_t>(screen_y - this->label_y_) * this->label_width_;
        for (int gx = 0; gx < glyph.box_w; gx++) {
          const int screen_x = glyph_x + gx;
          if (screen_x < this->label_x_ || screen_x >= this->label_x_ + this->label_width_)
            continue;
          blend_(dest_row[screen_x - this->label_x_], this->active_color_, static_cast<float>(alpha_row[gx]) / 255.0f);
        }
      }
    }
    pen_x += glyph.adv_w + this->letter_space_;
    lv_font_glyph_release_draw_data(&glyph);
  }

  if (!this->lvgl_component_->direct_blit_rgb888(
          reinterpret_cast<const uint8_t *>(this->scratch_), this->label_width_ * static_cast<int>(sizeof(lv_color_t)),
          this->label_x_, this->label_y_, this->label_width_, this->label_height_)) {
    this->end();
    return false;
  }
  this->value_pct_ = value_pct;
  return true;
}

bool MaterialDirectVolumeOverlay::direct_update_(int visual_pct, int value_pct) {
  if (!this->active_ || this->background_ == nullptr || this->scratch_ == nullptr)
    return false;
  visual_pct = clamp_pct(visual_pct);
  value_pct = clamp_pct(value_pct);
  if (visual_pct == this->visual_pct_)
    return this->direct_draw_value_(value_pct);

  const int old_pct = this->visual_pct_ < 0 ? 0 : this->visual_pct_;
  const float old_x = this->point_x_[old_pct];
  const float old_y = this->point_y_[old_pct];
  const float new_x = this->point_x_[visual_pct];
  const float new_y = this->point_y_[visual_pct];
  const int margin = static_cast<int>(
      std::ceil(this->knob_radius_ + this->track_radius_ + std::max(2.0f, this->track_radius_ / 3.0f)));
  int x1 = static_cast<int>(std::floor(std::min(old_x, new_x))) - margin;
  int y1 = static_cast<int>(std::floor(std::min(old_y, new_y))) - margin;
  int x2 = static_cast<int>(std::ceil(std::max(old_x, new_x))) + margin;
  int y2 = static_cast<int>(std::ceil(std::max(old_y, new_y))) + margin;
  if (std::min(old_pct, visual_pct) <= 50 && std::max(old_pct, visual_pct) >= 50) {
    x1 = std::min(x1, static_cast<int>(std::floor(this->center_x_)) - margin);
    x2 = std::max(x2, static_cast<int>(std::ceil(this->center_x_)) + margin);
    y1 = std::min(y1, static_cast<int>(std::floor(this->center_y_ - this->arc_radius_)) - margin);
  }
  x1 = std::clamp(x1, 0, this->screen_width_ - 1);
  y1 = std::clamp(y1, 0, this->capture_height_ - 1);
  x2 = std::clamp(x2, x1, this->screen_width_ - 1);
  y2 = std::clamp(y2, y1, this->capture_height_ - 1);
  const int width = x2 - x1 + 1;
  const int height = y2 - y1 + 1;

  for (int local_y = 0; local_y < height; local_y++) {
    std::memcpy(this->scratch_ + static_cast<size_t>(local_y) * width,
                this->background_ + static_cast<size_t>(y1 + local_y) * this->screen_width_ + x1,
                static_cast<size_t>(width) * sizeof(lv_color_t));
  }
  this->redraw_track_in_region_(this->scratch_, width, x1, y1, width, height, 0, visual_pct, this->active_color_);
  draw_disc_(this->scratch_, width, x1, y1, width, height, new_x, new_y, this->knob_radius_, this->knob_color_);

  if (!this->lvgl_component_->direct_blit_rgb888(reinterpret_cast<const uint8_t *>(this->scratch_),
                                                 width * static_cast<int>(sizeof(lv_color_t)), x1, y1, width, height)) {
    this->end();
    return false;
  }
  this->visual_pct_ = visual_pct;
  return this->direct_draw_value_(value_pct);
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
  if (value_pct == this->visual_cache_.value_pct && visual_pct == this->visual_cache_.visual_pct)
    return;
  this->visual_cache_.value_pct = value_pct;
  this->visual_cache_.visual_pct = visual_pct;
  if (!this->direct_update_(visual_pct, value_pct))
    this->update_native_(value_pct, visual_pct);
}

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
