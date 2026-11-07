#pragma once

#include "esphome/core/hal.h"
#include <lvgl.h>
#include <algorithm>
#include <cstddef>

#if LV_USE_VECTOR_GRAPHIC
namespace esphome::lvgl {

struct SvgIconCommand {
  char operation;
  float points[6];
};

class SvgIcon {
 public:
  void setup(lv_obj_t *object, const SvgIconCommand *commands, size_t count, float x, float y, float width,
             float height) {
    this->view_x_ = x;
    this->view_y_ = y;
    this->view_width_ = width;
    this->view_height_ = height;
    this->path_ = lv_vector_path_create(LV_VECTOR_PATH_QUALITY_HIGH);
    if (this->path_ == nullptr)
      return;
    for (size_t i = 0; i < count; i++) {
      SvgIconCommand command;
      progmem_memcpy(&command, commands + i, sizeof(command));
      const lv_fpoint_t points[] = {{command.points[0], command.points[1]},
                                   {command.points[2], command.points[3]},
                                   {command.points[4], command.points[5]}};
      switch (command.operation) {
        case 'M':
          lv_vector_path_move_to(this->path_, &points[0]);
          break;
        case 'L':
          lv_vector_path_line_to(this->path_, &points[0]);
          break;
        case 'Q':
          lv_vector_path_quad_to(this->path_, &points[0], &points[1]);
          break;
        case 'C':
          lv_vector_path_cubic_to(this->path_, &points[0], &points[1], &points[2]);
          break;
        case 'Z':
          lv_vector_path_close(this->path_);
          break;
      }
    }
    lv_obj_add_event_cb(object, event_callback_, LV_EVENT_DRAW_MAIN, this);
    lv_obj_add_event_cb(object, event_callback_, LV_EVENT_DELETE, this);
  }

 protected:
  static void event_callback_(lv_event_t *event) {
    auto *self = static_cast<SvgIcon *>(lv_event_get_user_data(event));
    if (lv_event_get_code(event) == LV_EVENT_DELETE) {
      lv_vector_path_delete(self->path_);
      self->path_ = nullptr;
    } else if (self->path_ != nullptr) {
      self->draw_(lv_event_get_target_obj(event), lv_event_get_layer(event));
    }
  }

  void draw_(lv_obj_t *object, lv_layer_t *parent) {
    lv_area_t destination;
    lv_obj_get_content_coords(object, &destination);
    const int32_t width = lv_area_get_width(&destination);
    const int32_t height = lv_area_get_height(&destination);
    if (parent == nullptr || width <= 0 || height <= 0 || width > 256 || height > 256)
      return;
    const lv_opa_t opacity = lv_obj_get_style_text_opa(object, LV_PART_MAIN);
    if (opacity <= LV_OPA_MIN)
      return;

    // RGB888 vector drawing otherwise allocates an ARGB scratch surface the size
    // of the complete parent layer. A local layer bounds it to the icon and also
    // keeps ThorVG's viewport at its expected zero origin.
    const lv_area_t local{0, 0, width - 1, height - 1};
    lv_layer_t *layer = lv_draw_layer_create(parent, LV_COLOR_FORMAT_ARGB8888, &local);
    if (layer == nullptr)
      return;
    auto *vector = lv_draw_vector_dsc_create(layer);
    if (vector == nullptr) {
      lv_draw_layer_delete(layer);
      return;
    }
    layer->opa = LV_OPA_COVER;
    layer->recolor = lv_color32_make(0, 0, 0, 0);
    const float scale = std::min(width / this->view_width_, height / this->view_height_);
    lv_draw_vector_dsc_translate(vector, (width - this->view_width_ * scale) * 0.5f - this->view_x_ * scale,
                                 (height - this->view_height_ * scale) * 0.5f - this->view_y_ * scale);
    lv_draw_vector_dsc_scale(vector, scale, scale);
    lv_draw_vector_dsc_set_fill_color(vector, lv_obj_get_style_text_color(object, LV_PART_MAIN));
    lv_draw_vector_dsc_set_fill_opa(vector, LV_OPA_COVER);
    lv_draw_vector_dsc_set_stroke_opa(vector, LV_OPA_TRANSP);
    lv_draw_vector_dsc_add_path(vector, this->path_);
    lv_draw_vector(vector);
    lv_draw_vector_dsc_delete(vector);

    lv_draw_image_dsc_t composite;
    lv_draw_image_dsc_init(&composite);
    composite.src = layer;
    composite.opa = opacity;
    lv_draw_layer(parent, &composite, &destination);
  }

  lv_vector_path_t *path_{nullptr};
  float view_x_{0};
  float view_y_{0};
  float view_width_{1};
  float view_height_{1};
};

}  // namespace esphome::lvgl
#endif  // LV_USE_VECTOR_GRAPHIC
