#define LV_CONF_INCLUDE_SIMPLE
#include "esphome/core/defines.h"
#include <lvgl.h>
#include "src/image/lv_image_decoder_private.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

static constexpr int SW = 101, SH = 91, DW = 137, DH = 117, SX = 7, SY = 9, DX = 13, DY = 19, W = 80, H = 55;
static std::vector<uint8_t> source(SW * SH * 3), target(DW * DH * 3);
static lv_draw_image_dsc_t description{};
static const lv_draw_image_dsc_t *dsc = &description;
static lv_draw_buf_t destination{};
static lv_draw_buf_t *dest_buf = &destination;
static lv_layer_t draw_layer{};
static lv_layer_t *layer = &draw_layer;
static lv_area_t visible_area{DX + 10, DY + 20, DX + 10 + W - 1, DY + 20 + H - 1},
    dest_area{DX, DY, DX + W - 1, DY + H - 1};
static lv_image_decoder_dsc_t decoder_dsc{};
static lv_image_dsc_t mask_image{};
static lv_color_format_t src_cf = LV_COLOR_FORMAT_RGB888, dest_cf = LV_COLOR_FORMAT_RGB888;
static uint32_t pixel_count = W * H, src_bw = W, src_bh = H, src_stride_px = SW, src_h = SH, dest_stride_px = DW;
static uint32_t src_stride = SW * 3, dest_stride = DW * 3;
static bool source_psram = true;
static bool esp_ptr_external_ram(const void *) { return source_psram; }
static int src_bx = SX, src_by = SY, clip_w = W, clip_h = H;
static const uint8_t *src_buf = source.data();
static unsigned s_rgb888_dma_copy_tasks{};
static std::vector<int> events;
static bool dma_ok = true, fallback{};
static void lv_draw_ppa_cache_sync_area_to_memory(lv_draw_buf_t *buffer, const lv_area_t *base, const lv_area_t *area) {
  assert(buffer == dest_buf && base == &layer->buf_area && area == &visible_area);
  events.push_back(1);
}
static void lv_draw_ppa_cache_sync_area_from_memory(lv_draw_buf_t *buffer, const lv_area_t *base,
                                                    const lv_area_t *area) {
  assert(buffer == dest_buf && base == &layer->buf_area && area == &visible_area);
  events.push_back(3);
}
extern "C" void lv_image_decoder_close(lv_image_decoder_dsc_t *decoder) {
  assert(decoder == &decoder_dsc);
  events.push_back(4);
}
bool lvgl_dma2d_m2m_copy_rgb888_2d(const uint8_t *, int, int, int, int, uint8_t *, int, int, int, int, int, int)
    __attribute__((weak));
bool lvgl_dma2d_m2m_copy_rgb888_2d(const uint8_t *input, int iw, int ih, int ix, int iy, uint8_t *output, int ow,
                                   int oh, int ox, int oy, int width, int height) {
  assert(input == source.data() && output == target.data());
  assert(iw == SW && ih == SH && ix == SX && iy == SY && ow == DW && oh == DH && ox == DX && oy == DY && width == W &&
         height == H);
  events.push_back(2);
  if (!dma_ok)
    return false;
  for (int y = 0; y < height; y++)
    std::memcpy(output + ((y + oy) * ow + ox) * 3, input + ((y + iy) * iw + ix) * 3, width * 3);
  return true;
}
static void execute_actual_branch() {
#include "rgb888_copy_branch.inc"
  fallback = true;
}
static void reset() {
  src_buf = source.data();
  source_psram = true;
  description = {};
  description.opa = LV_OPA_COVER;
  description.blend_mode = LV_BLEND_MODE_NORMAL;
  description.scale_x = description.scale_y = 256;
  destination = {};
  destination.data = target.data();
  destination.header.w = DW;
  destination.header.h = DH;
  destination.header.stride = DW * 3;
  draw_layer.draw_buf = dest_buf;
  draw_layer.buf_area = {10, 20, 10 + DW - 1, 20 + DH - 1};
  src_cf = dest_cf = LV_COLOR_FORMAT_RGB888;
  pixel_count = W * H;
  src_bw = W;
  src_bh = H;
  dma_ok = true;
  events.clear();
  fallback = false;
  s_rgb888_dma_copy_tasks = 0;
  std::fill(target.begin(), target.end(), 0xA5);
}
int main() {
  for (size_t i = 0; i < source.size(); i++)
    source[i] = i % 251;
  reset();
  description.scale_x = description.scale_y = LV_SCALE_NONE;
  execute_actual_branch();
  assert(!fallback && events == std::vector<int>({1, 2, 3, 4}));
  reset();
  execute_actual_branch();
  assert(!fallback && events == std::vector<int>({1, 2, 3, 4}) && s_rgb888_dma_copy_tasks == 1);
  for (int y = 0; y < DH; y++)
    for (int x = 0; x < DW; x++)
      for (int channel = 0; channel < 3; channel++) {
        const bool inside = x >= DX && x < DX + W && y >= DY && y < DY + H;
        assert(target[(y * DW + x) * 3 + channel] ==
               (inside ? source[((y - DY + SY) * SW + x - DX + SX) * 3 + channel] : 0xA5));
      }
  for (int test = 0; test < 14; test++) {
    reset();
    switch (test) {
      case 0:
        src_cf = LV_COLOR_FORMAT_RGB565;
        break;
      case 1:
        dest_cf = LV_COLOR_FORMAT_ARGB8888;
        break;
      case 2:
        description.opa = 128;
        break;
      case 3:
        description.scale_x = 512;
        break;
      case 4:
        description.rotation = 900;
        break;
      case 5:
        description.recolor_opa = 1;
        break;
      case 6:
        description.clip_radius = 20;
        break;
      case 7:
        description.tile = true;
        break;
      case 8:
        description.bitmap_mask_src = &mask_image;
        break;
      case 9:
        pixel_count = 100000;
        break;
      case 10:
        src_bw = W + 1;
        break;
      case 11:
        description.blend_mode = LV_BLEND_MODE_ADDITIVE;
        break;
      case 12:
        source_psram = false;
        break;
      case 13:
        src_buf = target.data() + 3;
        break;
    }
    execute_actual_branch();
    assert(fallback && events.empty() && s_rgb888_dma_copy_tasks == 0);
  }
  reset();
  dma_ok = false;
  execute_actual_branch();
  assert(fallback && events == std::vector<int>({1, 2, 3}) && s_rgb888_dma_copy_tasks == 0);
  std::cout << "PASS clipped RGB888 copy/cache order/neighbours/14 fallback modes/DMA failure\n";
}
