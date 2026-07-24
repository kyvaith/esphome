#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome::lvgl {

struct Dma2dM2mCopySpan {
  const uint8_t *source;
  int source_width;
  int source_height;
  int source_x;
  int source_y;
  int target_x;
  int target_y;
  int width;
  int height;
};

bool dma2d_m2m_copy_rgb888_spans(const Dma2dM2mCopySpan *spans, size_t span_count, uint8_t *target,
                                 int target_width, int target_height);

bool dma2d_m2m_copy_rgb888_2d(const uint8_t *source, int source_width, int source_height, int source_x,
                              int source_y, uint8_t *target, int target_width, int target_height, int target_x,
                              int target_y, int block_width, int block_height);

bool dma2d_m2m_compose_rgb888_circle(const uint8_t *background, int background_stride_pixels,
                                     int background_height, const uint8_t *foreground,
                                     int foreground_stride_pixels, int foreground_height, uint8_t *target,
                                     int target_width, int target_height, int center_x, int center_y, int radius);

bool dma2d_m2m_update_rgb888_circle(const uint8_t *background, int background_stride_pixels,
                                    int background_height, const uint8_t *foreground,
                                    int foreground_stride_pixels, int foreground_height, uint8_t *target,
                                    int target_width, int target_height, int old_center_x, int old_center_y,
                                    int old_radius, int new_center_x, int new_center_y, int new_radius);

}  // namespace esphome::lvgl
