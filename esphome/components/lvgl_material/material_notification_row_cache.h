#pragma once
#include "esphome/core/defines.h"
#ifdef USE_LVGL_MATERIAL_NOTIFICATION_ROW_CACHE
#include <lvgl.h>
#include <array>
#include <cstdint>
#include <functional>
#include <utility>

#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA)
#include "esphome/components/lvgl/dma2d_m2m_copy.h"
#include "esphome/components/lvgl/lvgl_esphome.h"
#endif

namespace esphome::lvgl_material {

// Four reusable card surfaces, independent of the size of the HA archive.
// LVGL remains the compositor and the display buffer owner.
class MaterialNotificationRowCache {
 public:
  void set_min_free_bytes(size_t bytes) { this->min_free_bytes_ = bytes; }
  template<typename F> void set_memory_reclaim(F &&callback) { this->memory_reclaim_ = std::forward<F>(callback); }
  void set_band_count(uint8_t count) {
    if (count < 1 || count > 8 || count == this->band_count_)
      return;
    this->clear();
    this->band_count_ = count;
  }
  uint8_t band_count() const { return this->band_count_; }
  void setup(size_t index, lv_obj_t *root);
  void set_key(size_t index, const char *id, uint32_t content_hash);
  void update(lv_obj_t *viewport, size_t count, size_t prepare_limit);
  void set_packed(bool enabled) {
    if (this->packed_ != enabled) {
      this->clear();
      this->packed_ = enabled;
    }
  }
#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA)
  size_t opaque_spans(lv_obj_t *viewport, const lv_area_t &clip, lvgl::Dma2dM2mCopySpan *spans, size_t capacity);
  bool render_overlay(lvgl::LvglComponent *parent, lv_obj_t *root, lv_obj_t *viewport,
                      const display::FrameBufferLease &lease, const lv_area_t &clip);
#endif
  void clear();
  size_t bytes() const;
  size_t ready_count() const;
  size_t max_band_count() const;
  uint32_t captures() const { return this->captures_; }

 protected:
  static constexpr size_t ROWS = 8;
  static constexpr size_t SLOTS = 5;
  static constexpr size_t CHILDREN = 16;
  struct Source {
    MaterialNotificationRowCache *owner{};
    lv_obj_t *root{};
    lv_obj_t *image{};
    std::array<char, 33> id{};
    uint32_t hash{};
    uint64_t appearance{};
    uint16_t hidden_children{};
    uint8_t children{};
    lv_opa_t background_opa{};
    int8_t slot{-1};
  };
  struct Slot {
    struct Part {
      lv_image_dsc_t image{};
      int32_t x{}, y{};
      uint8_t *data{};
      size_t bytes{};
    };
    size_t bytes{};
    std::array<Part, 8> parts{};
    size_t part_count{};
    uint8_t storage_bands{};
    bool packed{};
    std::array<char, 33> id{};
    uint32_t hash{};
    uint64_t appearance{};
    bool valid{};
  };
  static void style_changed_(lv_event_t *event);
  static void draw_(lv_event_t *event);
  template<typename F> bool visit_visible_parts_(lv_obj_t *viewport, const lv_area_t &clip, F &&visit);
  static uint64_t appearance_(const Source &source);
  void refresh_appearances_();
  void restore_(Source &source);
  void attach_(Source &source, size_t slot);
  bool prepare_(Source &source, Slot &slot);
  static void release_slot_(Slot &slot);
  std::array<Source, ROWS> sources_{};
  std::array<Slot, SLOTS> slots_{};
  size_t min_free_bytes_{1024 * 1024};
  uint32_t captures_{};
  bool changing_{};
  bool direct_drawing_{};
  bool packed_{};
  bool reclaim_attempted_{};
  uint8_t band_count_{1};
  std::function<size_t()> memory_reclaim_{};
};

}  // namespace esphome::lvgl_material
#endif
