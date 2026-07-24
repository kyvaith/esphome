#pragma once

#include "esphome/components/lvgl/ken_burns.h"
#include "esphome/core/defines.h"

#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif  // USE_BINARY_SENSOR
#ifdef USE_IMAGE
#include "esphome/components/image/image.h"
#endif  // USE_IMAGE
#ifdef USE_LVGL_ROTARY_ENCODER
#include "esphome/components/rotary_encoder/rotary_encoder.h"
#endif  // USE_LVGL_ROTARY_ENCODER

// required for clang-tidy
#ifndef LV_CONF_H
#define LV_CONF_SKIP 1  // NOLINT
#endif                  // LV_CONF_H

#include "esphome/components/display/display.h"
#include "esphome/components/display/display_color_utils.h"
#include "esphome/core/component.h"

#include <atomic>
#include <list>
#include <lvgl.h>
#include <map>
#include <utility>
#include <vector>

#ifdef USE_ESP32
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#endif

extern "C" uint32_t lvgl_esphome_get_cpu_pct(void);
extern "C" uint32_t lvgl_esphome_get_flush_ms(void);
extern "C" uint32_t lvgl_esphome_get_direct_mode_active(void);
extern "C" uint32_t lvgl_esphome_get_loop_max_ms(void);
extern "C" uint32_t lvgl_esphome_get_flush_max_ms(void);
extern "C" uint32_t lvgl_esphome_get_invalidated_kpx(void);
extern "C" uint32_t lvgl_esphome_get_perf_logging_enabled(void);
extern "C" uint32_t lvgl_esphome_get_swipe_logging_enabled(void);
extern "C" void lvgl_esphome_set_perf_logging_enabled(bool enabled);
extern "C" void lvgl_esphome_set_swipe_logging_enabled(bool enabled);
extern "C" uint32_t lvgl_esphome_get_profiler_enabled(void);
extern "C" void lvgl_esphome_set_profiler_enabled(bool enabled);
extern "C" void lvgl_esphome_profiler_flush(void);
extern "C" void lvgl_esphome_profiler_mark(const char *name);
extern "C" bool lvgl_esphome_direct_blit_rgb888(const uint8_t *src, int src_stride, int x, int y, int width,
                                                  int height);
using LvglDirectBlitReadyCallback = void (*)(void *);
enum LvglDirectBlitAsyncResult : uint8_t {
  LVGL_DIRECT_BLIT_REJECTED = 0,
  LVGL_DIRECT_BLIT_BUSY = 1,
  LVGL_DIRECT_BLIT_SUBMITTED = 2,
};
extern "C" uint8_t lvgl_esphome_direct_blit_rgb888_async(const uint8_t *src, int src_stride, int x, int y,
                                                           int width, int height,
                                                           LvglDirectBlitReadyCallback ready_callback,
                                                           void *ready_arg);
extern "C" uint8_t lvgl_esphome_direct_blend_argb8888_async(
    const uint8_t *background, int background_stride, const uint8_t *foreground, int foreground_stride,
    int foreground_width, int foreground_height, int foreground_x, int foreground_y, int x, int y, int width,
    int height, LvglDirectBlitReadyCallback ready_callback, void *ready_arg);
extern "C" void lvgl_esphome_direct_blit_rgb888_release(int x, int y, int width, int height);
extern "C" bool lvgl_esphome_direct_regions_pause(bool paused, uint32_t timeout_ms);
extern "C" void lvgl_esphome_synchronize_direct_framebuffer_area(int x, int y, int width, int height);
extern "C" bool lvgl_esphome_wait_for_direct_frame_presented(uint32_t timeout_ms);
extern "C" bool lvgl_esphome_direct_blit_xrgb8888(const uint8_t *src, int src_stride, int x, int y, int width,
                                                    int height);
extern "C" bool lvgl_esphome_direct_blit_xrgb8888_coherent(const uint8_t *src, int src_stride, int x, int y,
                                                             int width, int height);
extern "C" bool lvgl_esphome_direct_capture_rgb888(uint8_t *dst, int dst_stride, int x, int y, int width,
                                                    int height);
extern "C" bool lvgl_esphome_compose_argb8888_over_rgb888(
    const uint8_t *background, int background_stride, const uint8_t *foreground, int foreground_stride,
    int foreground_width, int foreground_height, int foreground_x, int foreground_y, uint8_t *output,
    int output_stride, int width, int height);
extern "C" bool lvgl_esphome_snapshot_cache_page(lv_obj_t *obj);
extern "C" bool lvgl_esphome_snapshot_cache_compressed_page(lv_obj_t *obj);
extern "C" bool lvgl_esphome_snapshot_app_reserve_work_buffer(lv_obj_t *obj);
extern "C" bool lvgl_esphome_snapshot_app_release_work_buffer(void);
extern "C" bool lvgl_esphome_snapshot_cache_raw_page(lv_obj_t *obj);
extern "C" bool lvgl_esphome_snapshot_cache_current_frame_raw_page(lv_obj_t *obj);
extern "C" bool lvgl_esphome_snapshot_cache_current_frame_compressed_page(lv_obj_t *obj);
extern "C" void lvgl_esphome_snapshot_dsi_quiet_ms(uint32_t quiet_ms);
extern "C" void lvgl_esphome_dsi_mark_stress(const char *label, uint32_t duration_ms);
extern "C" bool lvgl_esphome_snapshot_cache_pair(lv_obj_t *left, lv_obj_t *right, int width);
extern "C" bool lvgl_esphome_snapshot_cache_tile_window(lv_obj_t **pages, int page_count, int current_page,
                                                        int width);
extern "C" bool lvgl_esphome_snapshot_refresh_tile_page(lv_obj_t *page, int width);
extern "C" bool lvgl_esphome_snapshot_is_active(void);
extern "C" bool lvgl_esphome_snapshot_app_open(lv_obj_t *app, lv_obj_t *background, int width, uint32_t duration_ms);
extern "C" void lvgl_esphome_snapshot_app_release_open_hold(void);
extern "C" bool lvgl_esphome_snapshot_app_close(lv_obj_t *app, lv_obj_t *background, int width, int target_center_x,
                                                int target_center_y, uint32_t duration_ms);
extern "C" bool lvgl_esphome_snapshot_app_prepare_close(lv_obj_t *app);
extern "C" void lvgl_esphome_snapshot_app_clear_prepared_close(void);
extern "C" bool lvgl_esphome_snapshot_swipe_begin(lv_obj_t *current, lv_obj_t *next, int width, int next_x);
extern "C" bool lvgl_esphome_snapshot_swipe_edge_begin(lv_obj_t *current, int width);
extern "C" void lvgl_esphome_snapshot_swipe_set_page_indicator(int page, int page_count);
extern "C" void lvgl_esphome_snapshot_set_clock_text(const char *text);
extern "C" void lvgl_esphome_snapshot_set_clock_font(const lv_font_t *font);
extern "C" void lvgl_esphome_snapshot_swipe_update(int current_x, int next_x);
extern "C" void lvgl_esphome_snapshot_swipe_request_update(int current_x, int next_x);
extern "C" void lvgl_esphome_snapshot_swipe_finish(int current_x, int next_x, uint32_t duration_ms, bool commit);
extern "C" void lvgl_esphome_snapshot_swipe_request_finish(int current_x, int next_x, uint32_t duration_ms, bool commit);
extern "C" void lvgl_esphome_snapshot_swipe_end(void);
extern "C" bool lvgl_esphome_snapshot_scroll_prepare(lv_obj_t *obj, int viewport_w, int viewport_h);
extern "C" bool lvgl_esphome_snapshot_scroll_refresh(lv_obj_t *obj, int viewport_w, int viewport_h);
extern "C" bool lvgl_esphome_snapshot_scroll_begin(lv_obj_t *obj, int viewport_w, int viewport_h);
extern "C" void lvgl_esphome_snapshot_scroll_update(int scroll_y);
extern "C" void lvgl_esphome_snapshot_scroll_finish(int scroll_y);
extern "C" void lvgl_esphome_snapshot_scroll_finish_retain(int scroll_y);
extern "C" void lvgl_esphome_snapshot_scroll_finish_inertial(int scroll_y, int velocity_px_s);
extern "C" void lvgl_esphome_snapshot_scroll_end(void);
extern "C" size_t lvgl_esphome_snapshot_memory_bytes(void);
extern "C" void lvgl_esphome_snapshot_log_memory(const char *phase);

#ifdef USE_FONT
#include "esphome/components/font/font.h"
#endif  // USE_FONT
#ifdef USE_TOUCHSCREEN
#include "esphome/components/touchscreen/touchscreen.h"
#endif  // USE_TOUCHSCREEN

#if defined(USE_LVGL_BUTTONMATRIX) || defined(USE_LVGL_KEYBOARD)
#include "esphome/components/key_provider/key_provider.h"
#endif  // USE_LVGL_BUTTONMATRIX

namespace esphome::lvgl {

#if LV_COLOR_DEPTH == 16
using lv_color_data = uint16_t;
#endif
#if LV_COLOR_DEPTH == 32
using lv_color_data = uint32_t;
#endif

extern lv_event_code_t lv_update_event;  // NOLINT
extern std::string lv_event_code_name_for(lv_event_t *event);

lv_obj_t *lv_container_create(lv_obj_t *parent);
#if LV_USE_SCALE
void lv_scale_draw_event_cb(lv_event_t *e, int32_t range_start, int32_t range_end, lv_color_t color_start,
                            lv_color_t color_end, int width, bool local);
void lv_scale_tick_offset_event_cb(lv_event_t *e, uint16_t offset, uint16_t stride);
#endif  // LV_USE_SCALE
#if LV_COLOR_DEPTH == 16
static const display::ColorBitness LV_BITNESS = display::ColorBitness::COLOR_BITNESS_565;
#elif LV_COLOR_DEPTH == 32
static const display::ColorBitness LV_BITNESS = display::ColorBitness::COLOR_BITNESS_888;
#else   // LV_COLOR_DEPTH
static const display::ColorBitness LV_BITNESS = display::ColorBitness::COLOR_BITNESS_332;
#endif  // LV_COLOR_DEPTH

#if defined(USE_FONT) && defined(USE_LVGL_FONT)
inline void lv_obj_set_style_text_font(lv_obj_t *obj, const font::Font *font, lv_style_selector_t part) {
  lv_obj_set_style_text_font(obj, font->get_lv_font(), part);
}
inline void lv_style_set_text_font(lv_style_t *style, const font::Font *font) {
  lv_style_set_text_font(style, font->get_lv_font());
}
#endif

#ifdef USE_IMAGE
#ifdef USE_LVGL_IMAGE
// Shortcut / overload, so that the source of an image widget can easily be updated from within a lambda.
inline void lv_image_set_src(lv_obj_t *obj, image::Image *image) { ::lv_image_set_src(obj, image->get_lv_image_dsc()); }

inline void lv_obj_set_style_bitmap_mask_src(lv_obj_t *obj, image::Image *image, lv_style_selector_t selector) {
  ::lv_obj_set_style_bitmap_mask_src(obj, image->get_lv_image_dsc(), selector);
}

inline void lv_obj_set_style_bg_image_src(lv_obj_t *obj, image::Image *image, lv_style_selector_t selector) {
  ::lv_obj_set_style_bg_image_src(obj, image->get_lv_image_dsc(), selector);
}
inline void lv_style_set_bg_image_src(lv_style_t *style, image::Image *image) {
  ::lv_style_set_bg_image_src(style, image->get_lv_image_dsc());
}
inline void lv_style_set_bitmap_mask_src(lv_style_t *style, image::Image *image) {
  ::lv_style_set_bitmap_mask_src(style, image->get_lv_image_dsc());
}
#endif

#ifdef USE_LVGL_ANIMIMG
inline void lv_animimg_set_src(lv_obj_t *img, std::vector<image::Image *> images) {
  auto *dsc = static_cast<std::vector<lv_image_dsc_t *> *>(lv_obj_get_user_data(img));
  if (dsc == nullptr) {
    // object will be lazily allocated but never freed.
    dsc = new std::vector<lv_image_dsc_t *>(images.size());  // NOLINT
    lv_obj_set_user_data(img, dsc);
  }
  dsc->clear();
  for (auto &image : images) {
    dsc->push_back(image->get_lv_image_dsc());
  }
  lv_animimg_set_src(img, (const void **) dsc->data(), dsc->size());
}
#endif  // USE_LVGL_ANIMIMG
#endif  // USE_IMAGE

#ifdef USE_LVGL_METER
int16_t lv_get_needle_angle_for_value(lv_obj_t *obj, int32_t value);
#endif

#ifdef USE_LVGL_GRADIENT
/**
 *
 * @param dsc The gradient descriptor containing the color stops
 * @param pos The current position to calculate the color for
 * @return The color for the given position
 */

lv_color_t lv_grad_calculate_color(const lv_grad_dsc_t *dsc, int32_t pos);
#endif  // USE_LVGL_GRADIENT

// Parent class for things that wrap an LVGL object
class LvCompound {
 public:
  virtual ~LvCompound() = default;
  virtual void set_obj(lv_obj_t *lv_obj) { this->obj = lv_obj; }
  lv_obj_t *obj{};
};

class LvglComponent;
class LvglNavigation;

class LvPageType : public Parented<LvglComponent> {
 public:
  LvPageType(bool skip) : skip(skip) {}

  void setup(size_t index) {
    this->index = index;
    this->obj = lv_obj_create(nullptr);
  }

  bool is_showing() const;

  lv_obj_t *obj{};
  size_t index{};
  bool skip;
};

using event_callback_t = void(lv_event_t *);

class LvLambdaComponent : public Component {
 public:
  LvLambdaComponent(void (*callback)()) : callback_(callback) {}

  void setup() override { this->callback_(); }
  // execute after the LvglComponent is setup
  float get_setup_priority() const override { return setup_priority::PROCESSOR - 5; }

 protected:
  void (*callback_)();
};

template<typename... Ts> class ObjUpdateAction : public Action<Ts...> {
 public:
  explicit ObjUpdateAction(std::function<void(Ts...)> &&lamb) : lamb_(std::move(lamb)) {}

 protected:
  void play(const Ts &...x) override { this->lamb_(x...); }

  std::function<void(Ts...)> lamb_;
};
#ifdef USE_LVGL_ANIMIMG
void lv_animimg_stop(lv_obj_t *obj);
#endif  // USE_LVGL_ANIMIMG

class LvglComponent : public PollingComponent {
  constexpr static const char *const TAG = "lvgl";

 public:
  LvglComponent(std::vector<display::Display *> displays, float buffer_frac, bool full_refresh, bool direct_mode,
                int draw_rounding, bool resume_on_input, bool update_when_display_idle);
  static void static_flush_cb(lv_display_t *disp_drv, const lv_area_t *area, uint8_t *color_p);
  /**
   *
   * @param obj A widget
   * @return The position of the last indev point relative to the widget's origin.
   */
  static lv_point_t get_touch_relative_to_obj(lv_obj_t *obj);

  float get_setup_priority() const override { return setup_priority::PROCESSOR; }
  void setup() override;
  void update() override;
  void loop() override;
  template<typename F> void add_on_idle_callback(F &&callback) { this->idle_callbacks_.add(std::forward<F>(callback)); }

  static void render_end_cb(lv_event_t *event);
  static void render_start_cb(lv_event_t *event);
  void dump_config() override;
  lv_display_t *get_disp() { return this->disp_; }
  lv_obj_t *get_screen_active() { return lv_display_get_screen_active(this->disp_); }
  // Pause or resume the display.
  // @param paused If true, pause the display. If false, resume the display.
  // @param show_snow If true, show the snow effect when paused.
  void set_paused(bool paused, bool show_snow);

  // Returns true if the display is explicitly paused, or a blocking display update is in progress.
  bool is_paused() const;
  // If the display is paused and we have resume_on_input_ set to true, resume the display.
  void maybe_wakeup() {
    if (this->paused_ && this->resume_on_input_) {
      this->set_paused(false, false);
    }
  }

  /**
   * Initialize the LVGL library and register custom events.
   */
  static void esphome_lvgl_init();

  //  Convenience overloads for adding a callback for one or more events
  static void add_event_cb(lv_obj_t *obj, event_callback_t callback, lv_event_code_t event);
  static void add_event_cb(lv_obj_t *obj, event_callback_t callback, lv_event_code_t event1, lv_event_code_t event2);
  static void add_event_cb(lv_obj_t *obj, event_callback_t callback, lv_event_code_t event1, lv_event_code_t event2,
                           lv_event_code_t event3);

  // change the state of a widget and fire an event if changed (only needed for CHECKED)

  static void lv_obj_set_state_value(lv_obj_t *obj, lv_state_t state, bool value) {
    if (value != lv_obj_has_state(obj, state)) {
      if (value) {
        lv_obj_add_state(obj, state);
      } else {
        lv_obj_remove_state(obj, state);
      }
      if (state == LV_STATE_CHECKED)
        lv_obj_send_event(obj, lv_update_event, nullptr);
    }
  }

  // change the state of a buttonmatrix button and fire an event if changed (only needed for CHECKED)
#ifdef USE_LVGL_BUTTONMATRIX
  static void lv_buttonmatrix_set_button_ctrl_value(lv_obj_t *obj, uint32_t index, lv_buttonmatrix_ctrl_t ctrl,
                                                    bool value) {
    if (value != lv_buttonmatrix_has_button_ctrl(obj, index, ctrl)) {
      if (value) {
        lv_buttonmatrix_set_button_ctrl(obj, index, ctrl);
      } else {
        lv_buttonmatrix_clear_button_ctrl(obj, index, ctrl);
      }
      if (ctrl == LV_BUTTONMATRIX_CTRL_CHECKED)
        lv_obj_send_event(obj, lv_update_event, nullptr);
    }
  }
#endif

  void add_page(LvPageType *page);
  void show_page(size_t index, lv_screen_load_anim_t anim, uint32_t time);
  void show_next_page(lv_screen_load_anim_t anim, uint32_t time);
  void show_prev_page(lv_screen_load_anim_t anim, uint32_t time);
  void set_page_wrap(bool wrap) { this->page_wrap_ = wrap; }
  void set_navigation(LvglNavigation *navigation) { this->navigation_ = navigation; }
  void navigation_touch_begin(int32_t x, int32_t y);
  bool navigation_touch_update(int32_t x, int32_t y);
  bool navigation_touch_end();
  void navigation_touch_cancel();
  void rotate_coordinates(int32_t &x, int32_t &y) const;
  void set_big_endian(bool big_endian) { this->big_endian_ = big_endian; }
  bool is_big_endian() const { return this->big_endian_; }
  size_t get_current_page() const;
  void set_focus_mark(lv_group_t *group) { this->focus_marks_[group] = lv_group_get_focused(group); }
  void restore_focus_mark(lv_group_t *group) {
    auto *mark = this->focus_marks_[group];
    if (mark != nullptr) {
      lv_group_focus_obj(mark);
    }
  }
  // rounding factor to align bounds of update area when drawing
  size_t draw_rounding{2};

  display::DisplayRotation rotation{display::DISPLAY_ROTATION_0_DEGREES};
  void set_pause_trigger(Trigger<> *trigger) { this->pause_callback_ = trigger; }
  void set_resume_trigger(Trigger<> *trigger) { this->resume_callback_ = trigger; }
  void set_draw_start_trigger(Trigger<> *trigger) { this->draw_start_callback_ = trigger; }
  void set_draw_end_trigger(Trigger<> *trigger) { this->draw_end_callback_ = trigger; }
  // Check if loop() has started - safe to perform LVGL operations
  bool is_loop_started() const { return this->loop_started_; }
  uint16_t get_width() const { return this->width_; }
  uint16_t get_height() const { return this->height_; }
  void record_invalidated_area(const lv_area_t *area);
  bool snapshot_swipe_direct_render(lv_draw_buf_t *current, lv_draw_buf_t *next, int current_x, int next_x, int width);
  bool snapshot_swipe_direct_render_edge(lv_draw_buf_t *current, int current_x, int width);
  bool snapshot_swipe_direct_render_panorama(const uint8_t *panorama, int current_x, int width, int scale,
                                             int initial_next_x, int panorama_page_count, int source_page_index);
  bool snapshot_scroll_direct_render(lv_draw_buf_t *content, lv_draw_buf_t *content_tail, int content_tail_y,
                                     int scroll_y, int viewport_w, int viewport_h);
  bool snapshot_app_direct_render(lv_draw_buf_t *background, lv_draw_buf_t *app, int center_x, int center_y, int width,
                                  int height);
  bool snapshot_present_current_frame();
  bool synchronize_direct_framebuffers();
  bool wait_for_direct_frame_presented(uint32_t timeout_ms);
  void realign_direct_buffer_after_manual_present(bool synchronize = true);
  void synchronize_direct_framebuffer_area(int x, int y, int width, int height);
  void synchronize_direct_framebuffer_rows(int y, int height);
#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  static ppa_client_handle_t register_direct_image_animation_client();
  static ppa_client_handle_t register_direct_image_blend_client();
  bool direct_resolve_image_crop(const lv_image_dsc_t *source, int *source_x, int *source_y, int *source_width,
                                 int *source_height) const;
  bool direct_present_image_crop(const lv_image_dsc_t *source, int source_x, int source_y, int source_width,
                                 int source_height, ppa_client_handle_t srm_client);
  bool direct_present_rgb888_crop_dma2d(const lv_image_dsc_t *source, int source_x, int source_y,
                                        int source_width, int source_height);
  bool direct_present_rgb888_crop_subpixel(const lv_image_dsc_t *source, int source_x, int source_y,
                                           int source_width, int source_height, uint8_t subpixel_alpha,
                                           bool subpixel_vertical, ppa_client_handle_t blend_client);
  bool direct_present_scaled_rgb888_crop_subpixel(
      const lv_image_dsc_t *source, int source_x, int source_y, int source_width, int source_height,
      uint8_t subpixel_alpha, bool subpixel_vertical, uint8_t *scratch, size_t scratch_size,
      ppa_client_handle_t blend_client, ppa_client_handle_t srm_client);
  bool direct_render_image_crop_rgb888(const lv_image_dsc_t *source, int source_x, int source_y, int source_width,
                                       int source_height, uint8_t *target, size_t target_size,
                                       ppa_client_handle_t srm_client);
  bool direct_render_image_crop_rgb565(const lv_image_dsc_t *source, int source_x, int source_y, int source_width,
                                       int source_height, uint8_t *target, size_t target_size,
                                       ppa_client_handle_t srm_client);
  bool direct_render_image_crop_rgb888_bands(const lv_image_dsc_t *source, int source_x, int source_y,
                                              int source_width, int source_height, uint8_t *const *targets,
                                              const size_t *target_sizes, size_t target_count, size_t band_rows,
                                              ppa_client_handle_t srm_client);
  bool direct_present_rgb565_crossfade(const uint8_t *background, const uint8_t *foreground, uint8_t opacity,
                                       ppa_client_handle_t blend_client);
  bool direct_present_rgb888_crossfade(const uint8_t *background, const uint8_t *foreground, uint8_t opacity,
                                       ppa_client_handle_t blend_client);
  bool direct_present_rgb888_rgb565_crossfade(const uint8_t *background, const uint8_t *foreground,
                                              uint8_t opacity, ppa_client_handle_t blend_client);
  bool direct_present_rgb888_crossfade_bands(const uint8_t *background,
                                             uint8_t *const *foreground_bands,
                                             const size_t *foreground_band_sizes, size_t band_count,
                                             size_t band_rows, uint8_t opacity,
                                             ppa_client_handle_t blend_client);
  bool direct_present_image_crop_rgb888_crossfade_banded(
      const lv_image_dsc_t *source, int source_x, int source_y, int source_width, int source_height,
      const uint8_t *background, uint8_t opacity, uint8_t *scratch, size_t scratch_size,
      ppa_client_handle_t srm_client, ppa_client_handle_t blend_client);
  bool direct_present_rgb888_solid_crossfade_banded(const uint8_t *background, uint8_t opacity,
                                                     const uint8_t *solid_band, size_t solid_band_size,
                                                     ppa_client_handle_t blend_client);
  const uint8_t *direct_get_stable_presented_frame(uint32_t timeout_ms = 50);
  bool direct_present_rgb565_software(const uint8_t *source, size_t source_size);
#endif
  bool begin_direct_image_animation();
  bool end_direct_image_animation();
  bool direct_blit_rgb888(const uint8_t *src, int src_stride, int x, int y, int width, int height);
  uint8_t direct_blit_rgb888_async(const uint8_t *src, int src_stride, int x, int y, int width, int height,
                                   LvglDirectBlitReadyCallback ready_callback, void *ready_arg);
  uint8_t direct_blend_argb8888_async(
      const uint8_t *background, int background_stride, const uint8_t *foreground, int foreground_stride,
      int foreground_width, int foreground_height, int foreground_x, int foreground_y, int x, int y, int width,
      int height, LvglDirectBlitReadyCallback ready_callback, void *ready_arg);
  void direct_blit_rgb888_release(int x, int y, int width, int height);
  bool direct_regions_pause(bool paused, uint32_t timeout_ms);
  bool direct_blit_xrgb8888(const uint8_t *src, int src_stride, int x, int y, int width, int height);
  bool direct_blit_xrgb8888_coherent(const uint8_t *src, int src_stride, int x, int y, int width, int height);
  bool direct_capture_rgb888(uint8_t *dst, int dst_stride, int x, int y, int width, int height);

 protected:
#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  bool direct_present_rgb888_crossfade_banded_(
      const lv_image_dsc_t *source, int source_x, int source_y, int source_width, int source_height,
      const uint8_t *background, uint8_t opacity, uint8_t *scratch, size_t scratch_size,
      ppa_client_handle_t srm_client, ppa_client_handle_t blend_client, bool render_source);
#endif
  bool direct_blit_(const uint8_t *src, int src_stride, int x, int y, int width, int height, bool xrgb8888,
                    bool skip_dma2d = false, bool source_coherent = false);
#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  struct DirectRegionSlot {
    int x{};
    int y{};
    int width{};
    int height{};
    bool valid{};
  };
  struct DirectRegionRequest {
    enum class Operation : uint8_t { COPY_RGB888, BLEND_ARGB8888, BARRIER };

    Operation operation{Operation::COPY_RGB888};
    const uint8_t *source{};
    int source_stride{};
    int source_width{};
    int source_height{};
    int source_x{};
    int source_y{};
    const uint8_t *background{};
    int background_stride{};
    int x{};
    int y{};
    int width{};
    int height{};
    LvglDirectBlitReadyCallback ready_callback{};
    void *ready_arg{};
  };
  static constexpr size_t DIRECT_REGION_SLOT_COUNT = 4;
  static constexpr size_t DIRECT_REGION_BATCH_SIZE = 8;
  bool direct_region_ppa_copy_(const uint8_t *source, int source_width, int source_height, int source_x,
                               int source_y, int copy_width, int copy_height, uint8_t *target, size_t target_size,
                               int target_width, int target_height, int target_x, int target_y, bool sync_source,
                               bool invalidate_target);
  bool direct_region_ppa_blend_argb8888_(
      const uint8_t *background, int background_stride, const uint8_t *foreground, int foreground_stride,
      int foreground_width, int foreground_height, int foreground_x, int foreground_y, int blend_width,
      int blend_height, uint8_t *target, size_t target_size, int target_width, int target_height, int target_x,
      int target_y);
  bool start_direct_region_compositor_();
  static void direct_region_task_trampoline_(void *arg);
  void direct_region_task_();
  bool direct_region_prepare_target_(uint32_t generation, uint8_t **active, uint8_t **target);
  bool direct_region_compose_to_(const DirectRegionRequest *requests, size_t request_count, uint8_t *active,
                                 uint8_t *target);
  DirectRegionSlot *direct_region_find_slot_(int x, int y, int width, int height, bool create);
#endif
  void draw_end_();
  // Not checking for non-null callback since the
  // LVGL callback that calls it is not set in that case
  void draw_start_() const { this->draw_start_callback_->trigger(); }

  void write_random_();
  void draw_buffer_(const lv_area_t *area, lv_color_data *ptr);
  void sync_direct_framebuffer_area_(const lv_area_t *area, uint8_t *color_p);
  bool sync_direct_other_buffer_(const lv_area_t *area, uint8_t *color_p);
  uint8_t *next_direct_render_buffer_() const;
  void present_direct_render_buffer_(uint8_t *buffer);
  uint8_t *next_snapshot_render_buffer_(const uint8_t *exclude_a = nullptr,
                                        const uint8_t *exclude_b = nullptr,
                                        uint32_t wait_ms = 0);
  bool present_snapshot_render_buffer_(uint8_t *buffer, bool wait_for_active = true);
#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  bool direct_render_image_crop_(const lv_image_dsc_t *source, int source_x, int source_y, int source_width,
                                 int source_height, ppa_client_handle_t srm_client, uint8_t *target_override,
                                 size_t target_override_size, bool present, bool output_rgb565);
  bool direct_present_crossfade_(const uint8_t *background, const uint8_t *foreground, uint8_t opacity,
                                 ppa_client_handle_t blend_client, bool background_rgb565,
                                 bool foreground_rgb565);
#endif
#ifdef USE_ESP32
  struct PartialCompositorJob {
    lv_display_t *disp{};
    lv_area_t area{};
    uint8_t *color_p{};
    bool last{};
    uint64_t t0{};
  };
  bool start_partial_compositor_();
  bool partial_compositor_flush_(lv_display_t *disp_drv, const lv_area_t *area, uint8_t *color_p, uint64_t t0);
  static void partial_compositor_task_trampoline_(void *arg);
  void partial_compositor_task_();
  void partial_compositor_copy_area_(uint8_t *dst, const lv_area_t &area, const uint8_t *src,
                                     bool src_is_framebuffer = false);
  void partial_compositor_record_dirty_(const lv_area_t &area);
  void partial_compositor_sync_dirty_to_idle_();
#endif
  void flush_cb_(lv_display_t *disp_drv, const lv_area_t *area, uint8_t *color_p);

  std::vector<display::Display *> displays_{};
  size_t buffer_frac_{1};
  bool full_refresh_{};
  bool direct_mode_{};
  bool resume_on_input_{};
  bool update_when_display_idle_{};

  uint8_t *draw_buf_{};
  uint8_t *draw_buf2_{};
  uint8_t *direct_last_flushed_buf_{};
  uint8_t *snapshot_last_presented_buf_{};
  bool direct_mode_active_{false};
  bool direct_image_animation_active_{false};
  bool direct_image_framebuffer_dma_owned_[3]{};
  bool prepare_direct_framebuffer_dma_ownership_(uint8_t *framebuffer, size_t index);
  const uint8_t *direct_image_synced_source_{};
  size_t direct_image_synced_source_size_{};
#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA) && LV_COLOR_DEPTH == 32
  DirectRegionSlot direct_region_slots_[DIRECT_REGION_SLOT_COUNT]{};
  QueueHandle_t direct_region_queue_{};
  TaskHandle_t direct_region_task_handle_{};
  StackType_t *direct_region_task_stack_{};
  StaticTask_t direct_region_task_storage_{};
  SemaphoreHandle_t direct_region_barrier_{};
  StaticSemaphore_t direct_region_barrier_storage_{};
  std::atomic<uint32_t> direct_region_base_generation_{1};
  std::atomic<uint32_t> direct_region_copy_submitted_{0};
  std::atomic<uint32_t> direct_region_blend_submitted_{0};
  std::atomic<uint32_t> direct_region_submit_busy_{0};
  std::atomic<bool> direct_region_paused_{false};
  uint32_t direct_region_buffer_generation_[3]{};
#endif
#ifdef USE_ESP32
  static constexpr size_t PARTIAL_COMPOSITOR_MAX_DIRTY_AREAS = 48;
  QueueHandle_t partial_compositor_queue_{};
  TaskHandle_t partial_compositor_task_handle_{};
  uint8_t *partial_compositor_front_buffer_{};
  uint8_t *partial_compositor_back_buffer_{};
  lv_area_t partial_compositor_dirty_areas_[PARTIAL_COMPOSITOR_MAX_DIRTY_AREAS]{};
  size_t partial_compositor_dirty_count_{0};
  bool partial_compositor_full_dirty_{false};
  bool partial_compositor_active_{false};
  uint64_t perf_compositor_us_{0};
  uint64_t perf_compositor_ready_us_{0};
  uint64_t perf_compositor_px_{0};
  uint32_t perf_compositor_jobs_{0};
  uint32_t perf_compositor_max_us_{0};
  uint32_t perf_compositor_ready_max_us_{0};
#endif
  lv_display_t *disp_{};
  uint16_t width_{};
  uint16_t height_{};
  bool paused_{};
  std::vector<LvPageType *> pages_{};
  size_t current_page_{0};
  bool show_snow_{};
  bool page_wrap_{true};
  LvglNavigation *navigation_{};
  bool big_endian_{};
  std::map<lv_group_t *, lv_obj_t *> focus_marks_{};

  CallbackManager<void(uint32_t)> idle_callbacks_{};
  Trigger<> *pause_callback_{};
  Trigger<> *resume_callback_{};
  Trigger<> *draw_start_callback_{};
  Trigger<> *draw_end_callback_{};
  lv_color_t *rotate_buf_{};
  bool buffers_configured_{false};  // Track if lv_display_set_buffers() has been called
  size_t buf_bytes_{0};              // Store buffer size for delayed configuration
  bool loop_started_{false};  // safe to perform LVGL ops only after loop() starts
  // Sliding 1s perf window: time spent inside lv_timer_handler() vs wall,
  // minus the synchronous flush wait (DMA blocking, not CPU work).
  uint64_t perf_window_start_us_{0};
  uint64_t perf_busy_us_{0};
  uint64_t perf_flush_us_{0};
  uint64_t perf_invalidated_px_{0};
  uint32_t perf_invalidated_areas_{0};
  uint64_t perf_flush_px_{0};
  uint32_t perf_loop_max_us_{0};
  uint32_t perf_flush_max_us_{0};
};

class IdleTrigger : public Trigger<> {
 public:
  explicit IdleTrigger(LvglComponent *parent, TemplatableFn<uint32_t> timeout);

 protected:
  TemplatableFn<uint32_t> timeout_;
  bool is_idle_{};
};

template<typename... Ts> class LvglAction : public Action<Ts...>, public Parented<LvglComponent> {
 public:
  explicit LvglAction(std::function<void(LvglComponent *)> &&lamb) : action_(std::move(lamb)) {}

 protected:
  void play(const Ts &...x) override {
    lv_lock();
    this->action_(this->parent_);
    lv_unlock();
  }
  std::function<void(LvglComponent *)> action_{};
};

template<typename Tc, typename... Ts> class LvglCondition : public Condition<Ts...>, public Parented<Tc> {
 public:
  LvglCondition(std::function<bool(Tc *)> &&condition_lambda) : condition_lambda_(std::move(condition_lambda)) {}
  bool check(const Ts &...x) override { return this->condition_lambda_(this->parent_); }

 protected:
  std::function<bool(Tc *)> condition_lambda_{};
};

#ifdef USE_LVGL_TOUCHSCREEN
class LVTouchListener : public touchscreen::TouchListener, public Parented<LvglComponent> {
 public:
  LVTouchListener(uint16_t long_press_time, uint16_t long_press_repeat_time, LvglComponent *parent);
  void update(const touchscreen::TouchPoints_t &tpoints) override;
  void release() override;
  lv_indev_t *get_drv() { return this->drv_; }

 protected:
  lv_indev_t *drv_{};
  touchscreen::TouchPoint touch_point_{};
  bool touch_pressed_{};
  bool raw_touch_active_{};
  bool navigation_touch_captured_{};
};
#endif  // USE_LVGL_TOUCHSCREEN

#ifdef USE_LVGL_METER

void lv_image_set_needle_value(lv_obj_t *obj, int value);
void lv_arc_set_needle_value(lv_obj_t *obj, int value);

class IndicatorLine : public LvCompound {
 public:
  IndicatorLine() = default;

  void set_obj(lv_obj_t *lv_obj) override;

  void set_value(int value);

 private:
  void update_length_();

  int16_t angle_{};
  lv_point_precise_t points_[2]{};
};
#endif

#ifdef USE_LVGL_KEY_LISTENER
class LVEncoderListener : public Parented<LvglComponent> {
 public:
  LVEncoderListener(lv_indev_type_t type, uint16_t long_press_time, uint16_t long_press_repeat_time);

#ifdef USE_BINARY_SENSOR
  void add_button(binary_sensor::BinarySensor *button, lv_key_t key) {
    button->add_on_state_callback([this, key](bool state) { this->event(key, state); });
  }
#endif

#ifdef USE_LVGL_ROTARY_ENCODER
  void set_sensor(rotary_encoder::RotaryEncoderSensor *sensor) {
    sensor->register_listener([this](int32_t count) { this->set_count(count); });
  }
#endif  // USE_LVGL_ROTARY_ENCODER

  void event(int key, bool pressed) {
    if (!this->parent_->is_paused()) {
      this->pressed_ = pressed;
      this->key_ = key;
    } else if (!pressed) {
      // maybe wakeup on release if paused
      this->parent_->maybe_wakeup();
    }
  }

  void set_count(int32_t count) {
    if (!this->parent_->is_paused()) {
      this->count_ = count;
    } else {
      this->parent_->maybe_wakeup();
    }
  }

  // LVGL 9.5: Set rotary encoder sensitivity multiplier
  void set_sensitivity(float sensitivity) { this->sensitivity_ = sensitivity; }

  lv_indev_t *get_drv() { return this->drv_; }

 protected:
  lv_indev_t *drv_{};
  bool pressed_{};
  int32_t count_{};
  int32_t last_count_{};
  int key_{};
  float sensitivity_{1.0f};
};
#endif  //  USE_LVGL_KEY_LISTENER

#ifdef USE_LVGL_LINE
class LvLineType : public LvCompound {
 public:
  void set_points(FixedVector<lv_point_precise_t> points) {
    this->points_ = std::move(points);
    lv_line_set_points(this->obj, this->points_.begin(), this->points_.size());
  }

 protected:
  FixedVector<lv_point_precise_t> points_{};
};
#endif
#if defined(USE_LVGL_DROPDOWN) || defined(LV_USE_ROLLER)
class LvSelectable : public LvCompound {
 public:
  virtual size_t get_selected_index() = 0;
  virtual void set_selected_index(size_t index, lv_anim_enable_t anim) = 0;
  void set_selected_text(const std::string &text, lv_anim_enable_t anim);
  std::string get_selected_text();
  const std::vector<std::string> &get_options() { return this->options_; }
  void set_options(std::vector<std::string> options);

 protected:
  virtual void set_option_string(const char *options) = 0;
  std::vector<std::string> options_{};
};

#ifdef USE_LVGL_DROPDOWN
class LvDropdownType : public LvSelectable {
 public:
  size_t get_selected_index() override { return lv_dropdown_get_selected(this->obj); }
  void set_selected_index(size_t index, lv_anim_enable_t anim) override { lv_dropdown_set_selected(this->obj, index); }

 protected:
  void set_option_string(const char *options) override { lv_dropdown_set_options(this->obj, options); }
};
#endif  // USE_LVGL_DROPDOWN

#ifdef USE_LVGL_ROLLER
class LvRollerType : public LvSelectable {
 public:
  size_t get_selected_index() override { return lv_roller_get_selected(this->obj); }
  void set_selected_index(size_t index, lv_anim_enable_t anim) override {
    lv_roller_set_selected(this->obj, index, anim);
  }
  void set_mode(lv_roller_mode_t mode) { this->mode_ = mode; }

 protected:
  void set_option_string(const char *options) override { lv_roller_set_options(this->obj, options, this->mode_); }
  lv_roller_mode_t mode_{LV_ROLLER_MODE_NORMAL};
};
#endif
#endif  // defined(USE_LVGL_DROPDOWN) || defined(LV_USE_ROLLER)

#ifdef USE_LVGL_BUTTONMATRIX
class LvButtonMatrixType : public key_provider::KeyProvider, public LvCompound {
 public:
  void set_obj(lv_obj_t *lv_obj) override;
  uint16_t get_selected() { return lv_buttonmatrix_get_selected_button(this->obj); }
  void set_key(size_t idx, uint8_t key) { this->key_map_[idx] = key; }

 protected:
  std::map<size_t, uint8_t> key_map_{};
};
#endif  // USE_LVGL_BUTTONMATRIX

#ifdef USE_LVGL_KEYBOARD
class LvKeyboardType : public key_provider::KeyProvider, public LvCompound {
 public:
  void set_obj(lv_obj_t *lv_obj) override;
};
#endif  // USE_LVGL_KEYBOARD

#ifdef USE_LVGL_CALENDAR
class LvCalendarType : public LvCompound {
 public:
  uint16_t get_selected_year() {
    lv_calendar_date_t date;
    lv_calendar_get_pressed_date(this->obj, &date);
    return date.year;
  }
  uint8_t get_selected_month() {
    lv_calendar_date_t date;
    lv_calendar_get_pressed_date(this->obj, &date);
    return date.month;
  }
  uint8_t get_selected_day() {
    lv_calendar_date_t date;
    lv_calendar_get_pressed_date(this->obj, &date);
    return date.day;
  }
};
#endif  // USE_LVGL_CALENDAR
}  // namespace esphome::lvgl
