#include "lvgl_snapshot_store.h"

#include "esphome/core/log.h"

#include <algorithm>
#include <utility>

#if LV_USE_SNAPSHOT

namespace esphome::lvgl {

static const char *const TAG = "lvgl.snapshot";

void LvglSnapshotStore::setup() {
  if (this->decoded_slots_.empty())
    this->decoded_slots_.resize(this->decoded_slot_count_);
  if (this->preload_ && !this->capture_all())
    this->status_set_warning(LOG_STR("Failed to preload one or more snapshots"));
}

void LvglSnapshotStore::dump_config() {
  ESP_LOGCONFIG(TAG, "LVGL Snapshot Store:");
  ESP_LOGCONFIG(TAG, "  Compression: %s", this->compression_ == SnapshotCompression::JPEG ? "JPEG" : "none");
  ESP_LOGCONFIG(TAG, "  JPEG quality: %u", this->quality_);
  ESP_LOGCONFIG(TAG, "  Maximum entries: %u", static_cast<unsigned>(this->max_entries_));
  ESP_LOGCONFIG(TAG, "  Decoded slots: %u", static_cast<unsigned>(this->decoded_slot_count_));
  ESP_LOGCONFIG(TAG, "  Registered pages: %u", static_cast<unsigned>(this->entries_.size()));
  ESP_LOGCONFIG(TAG, "  Preload: %s", YESNO(this->preload_));
}

void LvglSnapshotStore::on_shutdown() { this->clear(); }

bool LvglSnapshotStore::register_page(LvPageType *page) {
  if (page == nullptr || page->obj == nullptr)
    return false;
  if (this->find_entry_(page) != nullptr)
    return true;
  if (this->entries_.size() >= this->max_entries_) {
    ESP_LOGE(TAG, "Cannot register page: maximum entry count (%u) reached", static_cast<unsigned>(this->max_entries_));
    return false;
  }

  this->entries_.emplace_back();
  this->entries_.back().page = page;
  return true;
}

bool LvglSnapshotStore::capture(LvPageType *page) {
  auto *entry = this->find_entry_(page);
  if (entry == nullptr) {
    if (!this->register_page(page))
      return false;
    entry = this->find_entry_(page);
  }
  return entry != nullptr && this->capture_entry_(*entry);
}

bool LvglSnapshotStore::capture_all() {
  bool success = true;
  for (auto &entry : this->entries_)
    success = this->capture_entry_(entry) && success;
  return success;
}

bool LvglSnapshotStore::invalidate(LvPageType *page) {
  auto *entry = this->find_entry_(page);
  if (entry == nullptr)
    return false;
  if (entry->references != 0 || !this->release_slot_(page))
    return false;
  this->clear_entry_(*entry);
  return true;
}

void LvglSnapshotStore::clear() {
  for (auto &slot : this->decoded_slots_) {
    if (slot.references == 0)
      this->clear_slot_(slot);
  }
  for (auto &entry : this->entries_) {
    if (entry.references == 0)
      this->clear_entry_(entry);
  }
}

lv_draw_buf_t *LvglSnapshotStore::acquire(LvPageType *page) {
  auto *entry = this->find_entry_(page);
  if (entry == nullptr)
    return nullptr;

  entry->last_access = ++this->access_clock_;
  if (entry->raw != nullptr) {
    entry->references++;
    return entry->raw;
  }

#ifdef USE_LVGL_SNAPSHOT_JPEG_CACHE
  if (entry->jpeg.empty())
    return nullptr;
  if (auto *slot = this->find_slot_(page)) {
    if (slot->generation == entry->generation) {
      slot->references++;
      slot->last_access = this->access_clock_;
      return slot->buffer;
    }
    if (slot->references != 0)
      return nullptr;
    this->clear_slot_(*slot);
  }

  auto *slot = this->select_slot_();
  if (slot == nullptr || !this->prepare_slot_(*slot, *entry) || !this->decode_(*entry, slot->buffer))
    return nullptr;
  slot->owner = page;
  slot->generation = entry->generation;
  slot->last_access = this->access_clock_;
  slot->references = 1;
  return slot->buffer;
#else
  return nullptr;
#endif
}

void LvglSnapshotStore::release(LvPageType *page) {
  auto *entry = this->find_entry_(page);
  if (entry != nullptr && entry->raw != nullptr && entry->references != 0) {
    entry->references--;
    return;
  }
  if (auto *slot = this->find_slot_(page); slot != nullptr && slot->references != 0)
    slot->references--;
}

size_t LvglSnapshotStore::get_memory_bytes() const {
  size_t bytes = 0;
  for (const auto &entry : this->entries_) {
    if (entry.raw != nullptr)
      bytes += entry.raw->data_size;
#ifdef USE_LVGL_SNAPSHOT_JPEG_CACHE
    bytes += entry.jpeg.capacity();
#endif
  }
  for (const auto &slot : this->decoded_slots_) {
    if (slot.buffer != nullptr)
      bytes += slot.buffer->data_size;
  }
  return bytes;
}

size_t LvglSnapshotStore::get_cached_count() const {
  size_t count = 0;
  for (const auto &entry : this->entries_) {
    if (entry.raw != nullptr)
      count++;
#ifdef USE_LVGL_SNAPSHOT_JPEG_CACHE
    else if (!entry.jpeg.empty())
      count++;
#endif
  }
  return count;
}

LvglSnapshotStore::Entry *LvglSnapshotStore::find_entry_(LvPageType *page) {
  const auto it = std::find_if(this->entries_.begin(), this->entries_.end(),
                               [page](const Entry &entry) { return entry.page == page; });
  return it == this->entries_.end() ? nullptr : &*it;
}

const LvglSnapshotStore::Entry *LvglSnapshotStore::find_entry_(LvPageType *page) const {
  const auto it = std::find_if(this->entries_.begin(), this->entries_.end(),
                               [page](const Entry &entry) { return entry.page == page; });
  return it == this->entries_.end() ? nullptr : &*it;
}

LvglSnapshotStore::DecodedSlot *LvglSnapshotStore::find_slot_(LvPageType *page) {
  const auto it = std::find_if(this->decoded_slots_.begin(), this->decoded_slots_.end(),
                               [page](const DecodedSlot &slot) { return slot.owner == page; });
  return it == this->decoded_slots_.end() ? nullptr : &*it;
}

LvglSnapshotStore::DecodedSlot *LvglSnapshotStore::select_slot_() {
  if (this->decoded_slots_.empty())
    this->decoded_slots_.resize(this->decoded_slot_count_);

  auto *selected = static_cast<DecodedSlot *>(nullptr);
  for (auto &slot : this->decoded_slots_) {
    if (slot.references != 0)
      continue;
    if (slot.owner == nullptr)
      return &slot;
    if (selected == nullptr || slot.last_access < selected->last_access)
      selected = &slot;
  }
  if (selected != nullptr)
    this->clear_slot_(*selected);
  return selected;
}

bool LvglSnapshotStore::prepare_slot_(DecodedSlot &slot, const Entry &entry) {
  if (slot.buffer != nullptr && (slot.buffer->header.cf != entry.color_format || slot.buffer->header.w != entry.width ||
                                 slot.buffer->header.h != entry.height || slot.buffer->header.stride != entry.stride)) {
    if (lv_draw_buf_reshape(slot.buffer, entry.color_format, entry.width, entry.height, entry.stride) == nullptr) {
      lv_draw_buf_destroy(slot.buffer);
      slot.buffer = nullptr;
    }
  }
  if (slot.buffer == nullptr)
    slot.buffer = lv_draw_buf_create(entry.width, entry.height, entry.color_format, entry.stride);
  return slot.buffer != nullptr && slot.buffer->data != nullptr;
}

bool LvglSnapshotStore::release_slot_(LvPageType *page) {
  auto *slot = this->find_slot_(page);
  if (slot == nullptr)
    return true;
  if (slot->references != 0)
    return false;
  this->clear_slot_(*slot);
  return true;
}

bool LvglSnapshotStore::capture_entry_(Entry &entry) {
  if (entry.page == nullptr || entry.page->obj == nullptr || entry.references != 0 || !this->release_slot_(entry.page))
    return false;

  auto *snapshot = lv_snapshot_take(entry.page->obj, this->snapshot_color_format_());
  if (snapshot == nullptr || snapshot->data == nullptr) {
    if (snapshot != nullptr)
      lv_draw_buf_destroy(snapshot);
    ESP_LOGW(TAG, "Failed to capture page snapshot");
    return false;
  }

  this->clear_entry_(entry);
  entry.width = snapshot->header.w;
  entry.height = snapshot->header.h;
  entry.stride = snapshot->header.stride;
  entry.color_format = static_cast<lv_color_format_t>(snapshot->header.cf);
  entry.last_access = ++this->access_clock_;
  entry.generation++;

#ifdef USE_LVGL_SNAPSHOT_JPEG_CACHE
  if (this->compression_ == SnapshotCompression::JPEG && this->encode_(entry, snapshot)) {
    lv_draw_buf_destroy(snapshot);
    return true;
  }
#endif

  entry.raw = snapshot;
  return true;
}

void LvglSnapshotStore::clear_entry_(Entry &entry) {
  if (entry.raw != nullptr) {
    lv_draw_buf_destroy(entry.raw);
    entry.raw = nullptr;
  }
#ifdef USE_LVGL_SNAPSHOT_JPEG_CACHE
  entry.jpeg.release();
#endif
  entry.width = 0;
  entry.height = 0;
  entry.stride = 0;
  entry.color_format = LV_COLOR_FORMAT_UNKNOWN;
  entry.generation++;
}

void LvglSnapshotStore::clear_slot_(DecodedSlot &slot) {
  if (slot.buffer != nullptr) {
    lv_draw_buf_destroy(slot.buffer);
    slot.buffer = nullptr;
  }
  slot.owner = nullptr;
  slot.generation = 0;
  slot.last_access = 0;
  slot.references = 0;
}

lv_color_format_t LvglSnapshotStore::snapshot_color_format_() const {
#if LV_COLOR_DEPTH == 16
  return LV_COLOR_FORMAT_RGB565;
#else
  return LV_COLOR_FORMAT_RGB888;
#endif
}

#ifdef USE_LVGL_SNAPSHOT_JPEG_CACHE
bool LvglSnapshotStore::encode_(Entry &entry, lv_draw_buf_t *snapshot) {
  const auto input_format =
      entry.color_format == LV_COLOR_FORMAT_RGB565 ? esp32_jpeg::PixelFormat::RGB565 : esp32_jpeg::PixelFormat::RGB888;
  const size_t packed_stride = entry.width * esp32_jpeg::bytes_per_pixel(input_format);
  if (entry.stride != packed_stride) {
    ESP_LOGD(TAG, "JPEG compression skipped for padded snapshot stride (%u != %u)", static_cast<unsigned>(entry.stride),
             static_cast<unsigned>(packed_stride));
    return false;
  }
  const esp32_jpeg::EncodeConfig config{
      .width = entry.width,
      .height = entry.height,
      .input_format = input_format,
      .down_sampling = esp32_jpeg::DownSampling::YUV444,
      .quality = this->quality_,
      .pixel_reverse = this->parent_->is_big_endian(),
      .retain_output_buffer = false,
      .dma2d_burst_length = 8,
      .dma2d_descriptor_burst = 0,
      .timeout_ms = 120,
  };
  esp32_jpeg::JpegBuffer output;
  const esp_err_t err =
      esp32_jpeg::encode(config, static_cast<const uint8_t *>(snapshot->data), snapshot->data_size, &output);
  if (err != ESP_OK || output.empty() || output.size() >= snapshot->data_size) {
    ESP_LOGW(TAG, "JPEG compression failed; retaining raw snapshot (error=%d)", err);
    return false;
  }
  entry.jpeg = std::move(output);
  return true;
}

bool LvglSnapshotStore::decode_(const Entry &entry, lv_draw_buf_t *output) {
  const auto output_format =
      entry.color_format == LV_COLOR_FORMAT_RGB565 ? esp32_jpeg::PixelFormat::RGB565 : esp32_jpeg::PixelFormat::RGB888;
  const esp32_jpeg::DecodeConfig config{
      .output_format = output_format,
      .rgb_order = this->parent_->is_big_endian() ? esp32_jpeg::RgbElementOrder::RGB : esp32_jpeg::RgbElementOrder::BGR,
      .color_conversion = esp32_jpeg::ColorConversionStandard::BT601,
      .direct_output = true,
      .skip_output_cache_sync = false,
      .dma2d_burst_length = 8,
      .dma2d_descriptor_burst = 0,
      .timeout_ms = 120,
  };
  size_t written = 0;
  const esp_err_t err = esp32_jpeg::decode(config, entry.jpeg.data(), entry.jpeg.size(),
                                           static_cast<uint8_t *>(output->data), output->data_size, &written);
  return err == ESP_OK && written != 0;
}
#endif

}  // namespace esphome::lvgl

#endif
