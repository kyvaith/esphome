#pragma once
#include "esphome/core/defines.h"

#ifdef USE_LVGL_MATERIAL_NOTIFICATION_CENTER
#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/components/lvgl/dma2d_m2m_copy.h"
#include "esphome/components/lvgl/direct_scene_controller.h"
#include <array>
#include <functional>

namespace esphome::lvgl_material {

// One temporary base shared by all surfaces in a modal session.
// The main task owns LVGL and the modal presentation leases.
class MaterialModalScene {
 public:
  using OverlayRenderer = std::function<bool(const display::FrameBufferLease &, const lv_area_t &)>;
  using OpaqueSpans = std::function<size_t(const lv_area_t &, lvgl::Dma2dM2mCopySpan *, size_t)>;
  explicit MaterialModalScene(lvgl::LvglComponent *parent) : parent_(parent) {}
  void add_scene_controller(lvgl::DirectSceneController *controller);
  void setup();
  void set_isolate_native_layers(bool enabled) { this->isolate_native_layers_ = enabled; }
  void set_direct_presentation(bool enabled) { this->direct_presentation_ = enabled; }
  void set_backdrop_dim_shift(uint8_t shift) { this->dim_shift_ = shift; }
  bool direct() const { return this->direct_active_; }
  void set_overlay(lv_obj_t *overlay, OverlayRenderer renderer = {}, OpaqueSpans opaque = {});
  void discard_prepared_frame();
  void set_profile(bool enabled);
  void loop();
  template<typename F> void set_admission(F &&callback) { this->admission_ = std::forward<F>(callback); }
  bool begin();
  bool end();
  bool dim_background();
  bool active() const { return this->bands_[0].data != nullptr; }
  size_t band_count() const { return this->active() ? this->band_count_ : 0; }
  size_t raw_bytes() const {
    size_t bytes = 0;
    for (size_t index = 0; index < this->band_count_; index++)
      bytes += this->bands_[index].bytes;
    return bytes;
  }
  size_t restore_bytes() const {
    size_t bytes = 0;
    for (size_t index = 0; index < this->band_count_; index++) {
      const auto &band = this->bands_[index];
      if (band.low_bits != nullptr)
        bytes += ((band.bytes + 7) / 8) * this->dim_shift_;
    }
    return bytes;
  }

 protected:
  void resume_();
  bool allocate_bands_();
  void release_bands_();
  void release_restore_bits_();
  bool restore_bands_();
  void hide_native_layers_();
  void restore_native_layers_();
  void restore_brightness_();
  void show_native_backdrop_();
  void stop_direct_();
#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA)
  static void invalidate_cb_(lv_event_t *event);
  bool compose_(const display::FrameBufferLease &lease);
  bool copy_backdrop_(const display::FrameBufferLease &lease, const lv_area_t &damage, const lv_area_t *overlay);
  struct FrameDamage {
    const uint8_t *frame{};
    lv_area_t previous{};
    bool has_overlay{};
  };
  std::array<FrameDamage, 3> damage_{};
  HighFrequencyLoopRequester render_loop_{};
  lv_obj_t *overlay_{};
  OverlayRenderer overlay_renderer_{};
  OpaqueSpans opaque_spans_{};
  display::FrameBufferLease prepared_{};
  const uint8_t *pending_frame_{};
  uint32_t pending_at_{};
  bool dirty_{};
  bool render_failed_{};
  bool profile_{};
  uint32_t profiled_frames_{};
  uint32_t profile_copy_us_{};
  uint32_t profile_draw_us_{};
  uint32_t profile_max_us_{};
#endif
  static constexpr size_t BAND_CAPACITY = 8;
  static constexpr size_t BAND_ALIGNMENT = 64;
  struct Band {
    lv_obj_t *image{};
    lv_image_dsc_t descriptor{};
    uint8_t *data{};
    uint8_t *low_bits{};
    size_t bytes{};
    size_t y{};
    size_t height{};
  };
  lvgl::LvglComponent *parent_;
  std::array<lvgl::DirectSceneController *, 4> controllers_{};
  size_t count_{};
  bool suspended_{};
  bool paused_{};
  bool session_{};
  bool captured_{};
  bool restored_{};
  bool direct_presentation_{};
  bool direct_active_{};
  uint8_t dim_shift_{1};
  uint32_t session_generation_{};
  lv_obj_t *root_{};
  lv_obj_t *native_screen_{};
  lv_obj_t *native_top_{};
  lv_obj_t *native_bottom_{};
  bool isolate_native_layers_{};
  bool native_screen_hidden_{};
  bool native_top_hidden_{};
  bool native_bottom_hidden_{};
  std::array<Band, BAND_CAPACITY> bands_{};
  size_t band_count_{};
  std::function<bool()> admission_{};
};

}  // namespace esphome::lvgl_material
#endif
