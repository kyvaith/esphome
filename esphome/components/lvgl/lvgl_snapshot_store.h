#pragma once

#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

#ifdef USE_LVGL_SNAPSHOT_JPEG_CACHE
#include "esphome/components/esp32_jpeg/esp32_jpeg.h"
#endif

#include <cstddef>
#include <cstdint>
namespace esphome::lvgl {

enum class SnapshotCompression : uint8_t {
  NONE,
  JPEG,
};

class LvglSnapshotStore final : public Component {
 public:
  explicit LvglSnapshotStore(LvglComponent *parent) : parent_(parent) {}

  void setup() override;
  void dump_config() override;
  void on_shutdown() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR - 10.0f; }

  void set_compression(SnapshotCompression compression) { this->compression_ = compression; }
  void set_quality(uint8_t quality) { this->quality_ = quality; }
  void set_max_entries(size_t max_entries) {
    this->max_entries_ = max_entries;
    this->entries_.init(max_entries);
  }
  void set_decoded_slots(size_t decoded_slots) {
    this->decoded_slot_count_ = decoded_slots;
    this->decoded_slots_.init(decoded_slots);
    for (size_t i = 0; i < decoded_slots; i++)
      this->decoded_slots_.emplace_back();
  }
  void set_preload(bool preload) { this->preload_ = preload; }

  bool register_page(LvPageType *page);
  bool capture(LvPageType *page);
  bool capture_all();
  bool invalidate(LvPageType *page);
  void clear();

  lv_draw_buf_t *acquire(LvPageType *page);
  void release(LvPageType *page);
  bool register_object(lv_obj_t *object);
  bool capture_object(lv_obj_t *object);
  bool invalidate_object(lv_obj_t *object);
  lv_draw_buf_t *acquire_object(lv_obj_t *object);
  void release_object(lv_obj_t *object);

  size_t get_memory_bytes() const;
  size_t get_cached_count() const;
  size_t get_registered_count() const { return this->entries_.size(); }

 protected:
  struct Entry {
    lv_obj_t *object{};
    lv_draw_buf_t *raw{};
#ifdef USE_LVGL_SNAPSHOT_JPEG_CACHE
    esp32_jpeg::JpegBuffer jpeg{};
#endif
    uint32_t width{};
    uint32_t height{};
    uint32_t stride{};
    lv_color_format_t color_format{LV_COLOR_FORMAT_UNKNOWN};
    uint32_t generation{};
    uint32_t last_access{};
    uint16_t references{};
  };

  struct DecodedSlot {
    lv_draw_buf_t *buffer{};
    lv_obj_t *owner{};
    uint32_t generation{};
    uint32_t last_access{};
    uint16_t references{};
  };

  Entry *find_entry_(lv_obj_t *object);
  const Entry *find_entry_(lv_obj_t *object) const;
  DecodedSlot *find_slot_(lv_obj_t *object);
  DecodedSlot *select_slot_();
  bool prepare_slot_(DecodedSlot &slot, const Entry &entry);
  bool release_slot_(lv_obj_t *object);
  bool capture_entry_(Entry &entry);
  void clear_entry_(Entry &entry);
  void clear_slot_(DecodedSlot &slot);
  lv_color_format_t snapshot_color_format_() const;
#ifdef USE_LVGL_SNAPSHOT_JPEG_CACHE
  bool encode_(Entry &entry, lv_draw_buf_t *snapshot);
  bool decode_(const Entry &entry, lv_draw_buf_t *output);
#endif

  LvglComponent *parent_{};
  SnapshotCompression compression_{SnapshotCompression::NONE};
  uint8_t quality_{90};
  size_t max_entries_{16};
  size_t decoded_slot_count_{3};
  bool preload_{};
  uint32_t access_clock_{};
  FixedVector<Entry> entries_{};
  FixedVector<DecodedSlot> decoded_slots_{};
};

template<typename... Ts> class SnapshotCaptureAction final : public Action<Ts...> {
 public:
  SnapshotCaptureAction(LvglSnapshotStore *store, LvPageType *page) : store_(store), page_(page) {}

 protected:
  void play(const Ts &...x) override { this->store_->capture(this->page_); }

  LvglSnapshotStore *store_{};
  LvPageType *page_{};
};

template<typename... Ts> class SnapshotCaptureAllAction final : public Action<Ts...> {
 public:
  explicit SnapshotCaptureAllAction(LvglSnapshotStore *store) : store_(store) {}

 protected:
  void play(const Ts &...x) override { this->store_->capture_all(); }

  LvglSnapshotStore *store_{};
};

template<typename... Ts> class SnapshotInvalidateAction final : public Action<Ts...> {
 public:
  SnapshotInvalidateAction(LvglSnapshotStore *store, LvPageType *page) : store_(store), page_(page) {}

 protected:
  void play(const Ts &...x) override { this->store_->invalidate(this->page_); }

  LvglSnapshotStore *store_{};
  LvPageType *page_{};
};

template<typename... Ts> class SnapshotClearAction final : public Action<Ts...> {
 public:
  explicit SnapshotClearAction(LvglSnapshotStore *store) : store_(store) {}

 protected:
  void play(const Ts &...x) override { this->store_->clear(); }

  LvglSnapshotStore *store_{};
};

}  // namespace esphome::lvgl
