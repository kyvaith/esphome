#include "material_modal_scene.h"
#include "material_cached_dim.h"
#ifdef USE_LVGL_MATERIAL_NOTIFICATION_CENTER
#include "misc/cache/instance/lv_image_cache.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <algorithm>
#include <numeric>
#include <cstring>
#ifdef USE_ESP32
#include "esp_heap_caps.h"
#include "esp_cache.h"
#endif
#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA)
#include "esphome/components/lvgl/dma2d_m2m_copy.h"
#include "core/lv_obj_draw_private.h"
#include "misc/lv_area_private.h"
#endif

namespace esphome::lvgl_material {

void MaterialModalScene::add_scene_controller(lvgl::DirectSceneController *controller) {
  if (this->count_ < this->controllers_.size())
    this->controllers_[this->count_++] = controller;
}

void MaterialModalScene::setup() {
  this->root_ = lv_obj_create(lv_display_get_layer_sys(this->parent_->get_disp()));
  lv_obj_remove_style_all(this->root_);
  lv_obj_set_pos(this->root_, 0, 0);
  lv_obj_set_size(this->root_, this->parent_->get_width(), this->parent_->get_height());
  lv_obj_remove_flag(this->root_, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  lv_obj_add_flag(this->root_, LV_OBJ_FLAG_HIDDEN);
  for (auto &band : this->bands_) {
    band.image = lv_image_create(this->root_);
    lv_obj_remove_flag(band.image, LV_OBJ_FLAG_CLICKABLE);
  }
#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA)
  if (this->direct_presentation_) {
    lv_display_add_event_cb(this->parent_->get_disp(), invalidate_cb_, LV_EVENT_REFR_REQUEST, this);
    // DIRECT mode may coalesce a new invalidation into an older native area
    // while its refresh is withheld. Observe it before that coalescing.
    lv_display_add_event_cb(this->parent_->get_disp(), invalidate_cb_, LV_EVENT_INVALIDATE_AREA, this);
  }
#endif
}

bool MaterialModalScene::allocate_bands_() {
#ifdef USE_ESP32
  const size_t width = this->parent_->get_width();
  const size_t height = this->parent_->get_height();
  if (width == 0 || height == 0)
    return false;
  const size_t stride = width * 3;
  if (width > SIZE_MAX / 3 || height > SIZE_MAX / stride)
    return false;
  const size_t row_quantum = BAND_ALIGNMENT / std::gcd(stride, BAND_ALIGNMENT);
  const size_t rows = ((height + BAND_CAPACITY - 1) / BAND_CAPACITY + row_quantum - 1) / row_quantum * row_quantum;
  this->band_count_ = (height + rows - 1) / rows;
  for (size_t index = 0; index < this->band_count_; index++) {
    auto &band = this->bands_[index];
    band.y = index * rows;
    band.height = std::min(rows, height - band.y);
    band.bytes = stride * band.height;
    const size_t allocated = (band.bytes + BAND_ALIGNMENT - 1) / BAND_ALIGNMENT * BAND_ALIGNMENT;
    band.data =
        static_cast<uint8_t *>(heap_caps_aligned_alloc(BAND_ALIGNMENT, allocated, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (band.data == nullptr) {
      this->release_bands_();
      return false;
    }
  }
  return true;
#else
  return false;
#endif
}

void MaterialModalScene::release_bands_() {
  this->release_restore_bits_();
  for (auto &band : this->bands_) {
#ifdef USE_ESP32
    if (band.data != nullptr)
      heap_caps_free(band.data);
#endif
    band.data = nullptr;
  }
  this->band_count_ = 0;
}

void MaterialModalScene::release_restore_bits_() {
  for (auto &band : this->bands_) {
#ifdef USE_ESP32
    if (band.low_bits != nullptr)
      heap_caps_free(band.low_bits);
#endif
    band.low_bits = nullptr;
  }
}

bool MaterialModalScene::dim_background() {
#ifdef USE_ESP32
  if (!this->captured_)
    return false;
  if (this->bands_[0].low_bits != nullptr)
    return true;
  // Reserve all low-bit planes before modifying a pixel. Keep the allocation
  // per band so the stronger scrim does not need one large contiguous block.
  for (size_t index = 0; index < this->band_count_; index++) {
    auto &band = this->bands_[index];
    const size_t bytes = (band.bytes + 7) / 8 * this->dim_shift_;
    const size_t allocation = (bytes + BAND_ALIGNMENT - 1) / BAND_ALIGNMENT * BAND_ALIGNMENT;
    band.low_bits = static_cast<uint8_t *>(
        heap_caps_aligned_alloc(BAND_ALIGNMENT, allocation, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (band.low_bits == nullptr) {
      this->release_restore_bits_();
      return false;
    }
    std::memset(band.low_bits, 0, bytes);
  }
  for (size_t index = 0; index < this->band_count_; index++) {
    auto &band = this->bands_[index];
    const size_t bytes = (band.bytes + 7) / 8;
    for (size_t plane = 0; plane < this->dim_shift_; plane++)
      cached_dim_half(band.data, band.bytes, band.low_bits + plane * bytes, 0);
  }
  // Keep the discarded low bits so the exact original can be restored.
  for (size_t index = 0; index < this->band_count_; index++) {
    const auto &band = this->bands_[index];
    const size_t span = (band.bytes + BAND_ALIGNMENT - 1) / BAND_ALIGNMENT * BAND_ALIGNMENT;
    lv_image_cache_drop(&band.descriptor);
    if (esp_cache_msync(band.data, span, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA) != ESP_OK)
      return false;
    lv_obj_invalidate(band.image);
  }
  return true;
#else
  return false;
#endif
}

void MaterialModalScene::restore_brightness_() {
#ifdef USE_ESP32
  if (this->bands_[0].low_bits == nullptr)
    return;
  for (size_t index = 0; index < this->band_count_; index++) {
    auto &band = this->bands_[index];
    const size_t bytes = (band.bytes + 7) / 8;
    for (size_t plane = this->dim_shift_; plane != 0; plane--)
      cached_dim_restore(band.data, band.bytes, band.low_bits + (plane - 1) * bytes, 0);
    lv_image_cache_drop(&band.descriptor);
  }
  this->release_restore_bits_();
#endif
}

void MaterialModalScene::resume_() {
  if (this->suspended_) {
    this->suspended_ = false;
    for (size_t index = 0; index < this->count_; index++)
      this->controllers_[index]->resume_after_direct_overlay();
  }
  if (this->paused_) {
    this->parent_->direct_regions_pause(false, 0);
    this->paused_ = false;
  }
}

void MaterialModalScene::hide_native_layers_() {
  if (!this->isolate_native_layers_)
    return;
  auto *display = this->parent_->get_disp();
  this->native_screen_ = lv_display_get_screen_active(display);
  this->native_top_ = lv_display_get_layer_top(display);
  this->native_bottom_ = lv_display_get_layer_bottom(display);
  this->native_screen_hidden_ = lv_obj_has_flag(this->native_screen_, LV_OBJ_FLAG_HIDDEN);
  this->native_top_hidden_ = lv_obj_has_flag(this->native_top_, LV_OBJ_FLAG_HIDDEN);
  this->native_bottom_hidden_ = lv_obj_has_flag(this->native_bottom_, LV_OBJ_FLAG_HIDDEN);
  // The immutable system-layer base already owns every background pixel.
  // A hidden active screen makes LVGL fall back to its opaque bottom layer.
  // Isolate that redundant fill too: the captured bands cover every pixel.
  // Hiding these layer roots avoids redrawing the entire covered app;
  // page flags, layout, navigation and the modal's system layer stay intact.
  lv_obj_add_flag(this->native_screen_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(this->native_top_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(this->native_bottom_, LV_OBJ_FLAG_HIDDEN);
}

void MaterialModalScene::restore_native_layers_() {
  if (this->native_screen_ != nullptr && lv_obj_is_valid(this->native_screen_) && !this->native_screen_hidden_)
    lv_obj_remove_flag(this->native_screen_, LV_OBJ_FLAG_HIDDEN);
  if (this->native_top_ != nullptr && lv_obj_is_valid(this->native_top_) && !this->native_top_hidden_)
    lv_obj_remove_flag(this->native_top_, LV_OBJ_FLAG_HIDDEN);
  if (this->native_bottom_ != nullptr && lv_obj_is_valid(this->native_bottom_) && !this->native_bottom_hidden_)
    lv_obj_remove_flag(this->native_bottom_, LV_OBJ_FLAG_HIDDEN);
  this->native_screen_ = this->native_top_ = this->native_bottom_ = nullptr;
}

bool MaterialModalScene::begin() {
  if (this->captured_ && (!this->session_ || this->direct_active_))
    return true;
  if (this->active() || this->session_) {
    this->end();  // Retire a failed handoff before another capture attempt.
    return false;
  }
#if defined(USE_ESP32) && defined(USE_LVGL_PPA)
  if (this->admission_ && !this->admission_())
    return false;
  const size_t width = this->parent_->get_width();
  const size_t height = this->parent_->get_height();
  if (!this->allocate_bands_())
    return false;
  size_t suspended = 0;
  for (; suspended < this->count_; suspended++) {
    if (!this->controllers_[suspended]->suspend_for_direct_overlay()) {
      for (size_t done = 0; done < suspended; done++)
        this->controllers_[done]->resume_after_direct_overlay();
      this->release_bands_();
      return false;
    }
  }
  this->suspended_ = true;
  this->paused_ = true;  // Every pause attempt owns a balanced lease, including failure.
  if (!this->parent_->direct_regions_pause(true, 150)) {
    this->resume_();
    this->release_bands_();
    return false;
  }
  if (!this->parent_->begin_frame_buffer_presentation(150)) {
    this->resume_();
    this->release_bands_();
    return false;
  }
  this->session_ = true;
  this->session_generation_ = this->parent_->get_frame_buffer_presentation_generation();
  display::FrameBufferView source{};
  if (!this->parent_->get_presentation_active_frame(&source, BufferReader::DMA) || source.width != width ||
      source.height != height || source.stride != width * 3 || source.size < width * height * 3 ||
      source.bitness != display::COLOR_BITNESS_888 || source.color_order != display::COLOR_ORDER_BGR ||
      source.big_endian) {
    this->end();
    return false;
  }
  for (size_t index = 0; index < this->band_count_; index++) {
    auto &band = this->bands_[index];
    if (!this->parent_->copy_presentation_region_rgb888(source.data, width, height, 0, band.y, band.data, width,
                                                        band.height)) {
      this->end();
      return false;
    }
  }
  this->captured_ = true;
  this->restored_ = true;  // Capture did not change the original scanout.
#if defined(USE_MIPI_DSI)
  if (this->direct_presentation_) {
    if (!this->dim_background()) {
      this->end();
      return false;
    }
    this->hide_native_layers_();
    this->direct_active_ = true;
    return true;
  }
#endif
  if (!this->parent_->end_frame_buffer_presentation(150, this->session_generation_))
    return false;
  this->session_ = false;
  this->restored_ = false;
  this->hide_native_layers_();
  this->show_native_backdrop_();
  return true;
#else
  return false;
#endif
}

void MaterialModalScene::show_native_backdrop_() {
  const size_t width = this->parent_->get_width();
  for (size_t index = 0; index < this->bands_.size(); index++) {
    auto &band = this->bands_[index];
    if (index >= this->band_count_) {
      lv_obj_add_flag(band.image, LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    band.descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
    band.descriptor.header.cf = LV_COLOR_FORMAT_RGB888;
    band.descriptor.header.w = width;
    band.descriptor.header.h = band.height;
    band.descriptor.header.stride = width * 3;
    band.descriptor.data_size = band.bytes;
    band.descriptor.data = band.data;
    lv_obj_set_pos(band.image, 0, band.y);
    lv_image_set_src(band.image, &band.descriptor);
    lv_obj_remove_flag(band.image, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_move_foreground(this->root_);
  lv_obj_remove_flag(this->root_, LV_OBJ_FLAG_HIDDEN);
}

void MaterialModalScene::set_overlay(lv_obj_t *overlay, OverlayRenderer renderer, OpaqueSpans opaque) {
#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA)
  this->overlay_ = overlay;
  this->overlay_renderer_ = std::move(renderer);
  this->opaque_spans_ = std::move(opaque);
  this->discard_prepared_frame();
#else
  (void) overlay;
  (void) renderer;
  (void) opaque;
#endif
}

void MaterialModalScene::discard_prepared_frame() {
#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA)
  if (this->prepared_)
    this->parent_->release_presentation_frame(&this->prepared_);
  if (this->direct_active_) {
    this->dirty_ = true;
    this->render_loop_.start();
  }
#endif
}

void MaterialModalScene::set_profile(bool enabled) {
#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA)
  if (enabled) {
    this->profiled_frames_ = this->profile_copy_us_ = this->profile_draw_us_ = this->profile_max_us_ = 0;
  } else if (this->profile_ && this->profiled_frames_ != 0) {
    ESP_LOGW("lvgl_material.modal", "compose frames=%u copy_avg_us=%u overlay_avg_us=%u total_max_us=%u",
             static_cast<unsigned>(this->profiled_frames_),
             static_cast<unsigned>(this->profile_copy_us_ / this->profiled_frames_),
             static_cast<unsigned>(this->profile_draw_us_ / this->profiled_frames_),
             static_cast<unsigned>(this->profile_max_us_));
  }
  this->profile_ = enabled;
#else
  (void) enabled;
#endif
}

void MaterialModalScene::stop_direct_() {
  this->direct_active_ = false;
#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA)
  if (this->prepared_)
    this->parent_->release_presentation_frame(&this->prepared_);
  this->render_loop_.stop();
  this->damage_ = {};
  this->overlay_ = nullptr;
  this->overlay_renderer_ = {};
  this->opaque_spans_ = {};
  this->pending_frame_ = nullptr;
  this->dirty_ = false;
  this->render_failed_ = false;
#endif
}

#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA)
void MaterialModalScene::invalidate_cb_(lv_event_t *event) {
  auto *scene = static_cast<MaterialModalScene *>(lv_event_get_user_data(event));
  if (scene->direct_active_ && scene->overlay_ != nullptr) {
    scene->dirty_ = true;
    scene->render_loop_.start();
  }
}

bool MaterialModalScene::compose_(const display::FrameBufferLease &lease) {
  const uint32_t start = this->profile_ ? micros() : 0;
  const int width = this->parent_->get_width();
  const int height = this->parent_->get_height();
  if (lease.width != static_cast<size_t>(width) || lease.height != static_cast<size_t>(height) ||
      lease.stride != lease.width * 3 || lease.size < lease.stride * lease.height ||
      lease.bitness != display::COLOR_BITNESS_888 || lease.color_order != display::COLOR_ORDER_BGR ||
      lease.big_endian || lease.index >= this->damage_.size())
    return false;
  const lv_area_t screen{0, 0, width - 1, height - 1};
  lv_area_t overlay{};
  bool visible = false;
  if (!lv_obj_has_flag(this->overlay_, LV_OBJ_FLAG_HIDDEN)) {
    lv_obj_update_layout(this->overlay_);
    for (uint32_t index = 0; index < lv_obj_get_child_count(this->overlay_); index++) {
      auto *child = lv_obj_get_child(this->overlay_, index);
      if (lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN))
        continue;
      lv_area_t bounds;
      lv_obj_get_coords(child, &bounds);
      const int32_t extra = lv_obj_get_ext_draw_size(child);
      lv_area_set(&bounds, bounds.x1 - extra, bounds.y1 - extra, bounds.x2 + extra, bounds.y2 + extra);
      if (!lv_area_intersect(&bounds, &bounds, &screen))
        continue;
      if (visible)
        lv_area_join(&overlay, &overlay, &bounds);
      else
        overlay = bounds;
      visible = true;
    }
  }

  auto &history = this->damage_[lease.index];
  lv_area_t damage = screen;
  // Pixels outside the old and new overlay remain the same dimmed base on
  // this physical buffer. A buffer's first use initializes it in full.
  if (history.frame == lease.data) {
    if (!history.has_overlay && !visible)
      return true;
    damage = visible ? overlay : history.previous;
    if (visible && history.has_overlay)
      lv_area_join(&damage, &damage, &history.previous);
  }
  const uint32_t copy_start = this->profile_ ? micros() : 0;
  if (!this->copy_backdrop_(lease, damage, visible ? &overlay : nullptr))
    return false;
  const uint32_t draw_start = this->profile_ ? micros() : 0;
  if (visible &&
      !(this->overlay_renderer_ ? this->overlay_renderer_(lease, overlay)
                                : this->parent_->render_presentation_overlay_rgb888(this->overlay_, lease, overlay)))
    return false;
  history = {lease.data, overlay, visible};
  if (this->profile_) {
    const uint32_t end = micros();
    this->profiled_frames_++;
    this->profile_copy_us_ += draw_start - copy_start;
    this->profile_draw_us_ += end - draw_start;
    this->profile_max_us_ = std::max(this->profile_max_us_, end - start);
  }
  return true;
}

bool MaterialModalScene::copy_backdrop_(const display::FrameBufferLease &lease, const lv_area_t &damage,
                                        const lv_area_t *overlay) {
  // Each opaque sprite piece replaces its matching part of the frozen base.
  // There are at most three vertically disjoint opaque pieces per card.
  constexpr size_t MAX_OPAQUE = 15;
  std::array<lvgl::Dma2dM2mCopySpan, 96> spans;
  const size_t opaque =
      overlay != nullptr && this->opaque_spans_ ? this->opaque_spans_(*overlay, spans.data(), MAX_OPAQUE) : 0;
  if (opaque > MAX_OPAQUE)
    return false;
  std::sort(spans.begin(), spans.begin() + opaque,
            [](const auto &left, const auto &right) { return left.target_y < right.target_y; });
  const int left = damage.x1;
  const int right = damage.x2 + 1;
  int previous_bottom = damage.y1;
  for (size_t index = 0; index < opaque; index++) {
    const auto &piece = spans[index];
    if (piece.target_y < previous_bottom || piece.target_y + piece.height > damage.y2 + 1 || piece.target_x < left ||
        piece.target_x + piece.width > right)
      return false;
    previous_bottom = piece.target_y + piece.height;
  }

  size_t count = opaque;
  for (size_t index = 0; index < this->band_count_; index++) {
    const auto &band = this->bands_[index];
    const int top = std::max<int32_t>(damage.y1, band.y);
    const int bottom = std::min<int32_t>(damage.y2 + 1, band.y + band.height);
    auto append = [&](int x1, int x2, int y1, int y2) {
      if (x1 >= x2 || y1 >= y2)
        return true;
      if (count == spans.size())
        return false;
      spans[count++] = {band.data,
                        static_cast<int>(lease.width),
                        static_cast<int>(band.height),
                        x1,
                        y1 - static_cast<int>(band.y),
                        x1,
                        y1,
                        x2 - x1,
                        y2 - y1};
      return true;
    };
    int y = top;
    for (size_t piece_index = 0; piece_index < opaque && y < bottom; piece_index++) {
      const auto &piece = spans[piece_index];
      const int from = std::max(y, piece.target_y);
      const int to = std::min(bottom, piece.target_y + piece.height);
      if (from >= to)
        continue;
      if (!append(left, right, y, from) || !append(left, piece.target_x, from, to) ||
          !append(piece.target_x + piece.width, right, from, to))
        return false;
      y = to;
    }
    if (!append(left, right, y, bottom))
      return false;
  }
  return lvgl::dma2d_m2m_copy_rgb888_spans(spans.data(), count, lease.data, lease.width, lease.height);
}
#endif

void MaterialModalScene::loop() {
#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA)
  if (!this->direct_active_ || this->overlay_ == nullptr)
    return;
  if (this->pending_frame_ != nullptr) {
    display::FrameBufferView active{};
    if (this->parent_->get_presentation_active_frame(&active, BufferReader::DMA) &&
        active.data == this->pending_frame_) {
      this->pending_frame_ = nullptr;
    } else if (millis() - this->pending_at_ >= 150) {
      this->render_failed_ = true;
    }
  }
  if (this->render_failed_) {
    if (this->prepared_)
      this->parent_->release_presentation_frame(&this->prepared_);
    if (!this->parent_->end_frame_buffer_presentation(150, this->session_generation_))
      return;
    this->session_ = false;
    this->restored_ = false;
    auto *overlay = this->overlay_;
    this->stop_direct_();
    this->show_native_backdrop_();
    lv_obj_move_foreground(overlay);
    ESP_LOGW("lvgl_material.modal", "Direct modal presentation failed, resumed native drawing");
    return;
  }
  if (this->prepared_ && this->pending_frame_ == nullptr) {
    const uint8_t *frame = this->prepared_.data;
    if (!this->parent_->present_presentation_frame(&this->prepared_, 0, false)) {
      this->render_failed_ = true;
      return;
    }
    this->restored_ = false;
    this->pending_frame_ = frame;
    this->pending_at_ = millis();
  }
  // Compose into the third buffer while DSI scans one and stages the other.
  // Hold the new lease until the older submission is actually active.
  if (this->dirty_ && !this->prepared_ &&
      this->parent_->acquire_presentation_frame(&this->prepared_, BufferWriter::DMA, 0, this->session_generation_)) {
    this->dirty_ = false;
    this->render_failed_ = !this->compose_(this->prepared_);
  }
  if (!this->dirty_ && !this->prepared_ && this->pending_frame_ == nullptr)
    this->render_loop_.stop();
#endif
}

bool MaterialModalScene::restore_bands_() {
  this->restore_brightness_();
  display::FrameBufferLease lease{};
  if (!this->parent_->acquire_presentation_frame(&lease, BufferWriter::DMA, 150, this->session_generation_))
    return false;
  const size_t width = this->parent_->get_width();
  const size_t height = this->parent_->get_height();
  bool copied = lease.data != nullptr && lease.width == width && lease.height == height && lease.stride == width * 3 &&
                lease.size >= lease.stride * height && lease.bitness == display::COLOR_BITNESS_888 &&
                lease.color_order == display::COLOR_ORDER_BGR && !lease.big_endian;
  for (size_t index = 0; copied && index < this->band_count_; index++) {
    const auto &band = this->bands_[index];
#ifdef USE_ESP32
    const size_t span = (band.bytes + BAND_ALIGNMENT - 1) / BAND_ALIGNMENT * BAND_ALIGNMENT;
    if (esp_cache_msync(band.data, span, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA) != ESP_OK) {
      copied = false;
      break;
    }
#endif
    copied = this->parent_->copy_presentation_region_rgb888(band.data, width, band.height, 0, 0,
                                                            lease.data + band.y * lease.stride, width, band.height);
  }
  // All bands reach one idle lease before a single VSYNC presentation.
  if (!copied || !this->parent_->present_presentation_frame(&lease, 150)) {
    this->parent_->release_presentation_frame(&lease);
    return false;
  }
  this->restored_ = true;
  return true;
}

bool MaterialModalScene::end() {
  this->stop_direct_();
  if (!this->active()) {
    this->resume_();
    return true;
  }
  auto *display = this->parent_->get_disp();
  lv_display_enable_invalidation(display, false);
  struct RestoreInvalidation {
    lv_display_t *display;
    ~RestoreInvalidation() { lv_display_enable_invalidation(this->display, true); }
  } invalidation{display};
  if (!this->session_) {
    if (!this->parent_->begin_frame_buffer_presentation(150))
      return false;
    this->session_ = true;
    this->session_generation_ = this->parent_->get_frame_buffer_presentation_generation();
  }
  if (this->captured_ && !this->restored_ && !this->restore_bands_()) {
    return false;
  }
  if (!this->parent_->end_frame_buffer_presentation(150, this->session_generation_))
    return false;
  this->session_ = false;
  lv_obj_add_flag(this->root_, LV_OBJ_FLAG_HIDDEN);
  for (auto &band : this->bands_) {
    lv_image_set_src(band.image, nullptr);
    lv_image_cache_drop(&band.descriptor);
  }
  this->restore_native_layers_();
  this->resume_();  // Reacquire full-screen direct scenes before LVGL can refresh.
  this->release_bands_();
  this->captured_ = false;
  this->restored_ = false;
  return true;
}

}  // namespace esphome::lvgl_material
#endif
