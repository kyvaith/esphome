#include "material_notification_row_cache.h"
#ifdef USE_LVGL_MATERIAL_NOTIFICATION_ROW_CACHE
#include "core/lv_obj_draw_private.h"
#include "core/lv_refr_private.h"
#include "display/lv_display_private.h"
#include "draw/lv_draw_private.h"
#include "misc/lv_area_private.h"
#include "misc/cache/instance/lv_image_cache.h"
#include <algorithm>
#include <cstring>
#ifdef USE_ESP32
#include "esp_cache.h"
#include "esp_heap_caps.h"
#endif

namespace esphome::lvgl_material {

// Render only the owned band, including off-screen row coordinates. The display's
// native buffers and scanout are never used as scratch. This follows LVGL's
// snapshot layer lifecycle, but does not require a contiguous whole-card buffer.
static bool capture_row_band(lv_obj_t *root, lv_draw_buf_t *buffer, int32_t x, int32_t y) {
  auto *display = lv_obj_get_display(root);
  lv_area_t area;
  lv_obj_get_coords(root, &area);
  area.x1 += x;
  area.x2 = area.x1 + static_cast<int32_t>(buffer->header.w) - 1;
  area.y1 += y;
  area.y2 = area.y1 + static_cast<int32_t>(buffer->header.h) - 1;
  lv_draw_buf_clear(buffer, nullptr);
  lv_layer_t layer;
  lv_layer_init(&layer);
  layer.draw_buf = buffer;
  layer.display = display;
  layer.buf_area = area;
  layer.color_format = static_cast<lv_color_format_t>(buffer->header.cf);
  layer._clip_area = area;
  layer.phy_clip_area = area;
  lv_draw_unit_send_event(nullptr, LV_EVENT_CHILD_CREATED, &layer);
  auto *old_display = lv_refr_get_disp_refreshing();
  auto *old_layer = display->layer_head;
  display->layer_head = &layer;
  lv_refr_set_disp_refreshing(display);
  lv_obj_redraw(&layer, root);
  layer.all_tasks_added = true;
  auto pending = [&]() {
    size_t count = 0;
    for (auto *current = &layer; current != nullptr; current = current->next)
      for (auto *task = current->draw_task_head; task != nullptr; task = task->next)
        count++;
    return count;
  };
  auto discard = [](auto &&self, lv_layer_t *current) -> void {
    for (auto *task = current->draw_task_head; task != nullptr;) {
      auto *next = task->next;
      if (task->type == LV_DRAW_TASK_TYPE_LAYER) {
        auto *image = static_cast<lv_draw_image_dsc_t *>(task->draw_dsc);
        if (image->src != nullptr)
          self(self, static_cast<lv_layer_t *>(const_cast<void *>(image->src)));
      }
      lv_draw_cleanup_task(task);
      task = next;
    }
    current->draw_task_head = nullptr;
  };
  size_t previous = pending(), stalled = 0;
  bool success = true;
  while (layer.draw_task_head != nullptr) {
#if LV_USE_OS
    lv_draw_dispatch_wait_for_request();
#endif
    lv_draw_dispatch();
    const size_t count = pending();
    if (count != previous) {
      previous = count;
      stalled = 0;
    } else if (++stalled > 512) {
      lv_draw_wait_for_finish();
      discard(discard, &layer);
      success = false;
      break;
    }
  }
  display->layer_head = old_layer;
  lv_refr_set_disp_refreshing(old_display);
  if (success)
    lv_draw_unit_send_event(nullptr, LV_EVENT_SCREEN_LOAD_START, &layer);
  lv_draw_unit_send_event(nullptr, LV_EVENT_CHILD_DELETED, &layer);
  return success;
}

void MaterialNotificationRowCache::setup(size_t index, lv_obj_t *root) {
  if (index >= ROWS || root == nullptr || lv_obj_get_child_count(root) > CHILDREN)
    return;
  auto &source = this->sources_[index];
  source.owner = this;
  source.root = root;
  source.children = lv_obj_get_child_count(root);
  source.image = lv_obj_create(root);
  lv_obj_add_event_cb(source.image, draw_, LV_EVENT_DRAW_MAIN, &source);
  lv_obj_remove_style_all(source.image);
  lv_obj_remove_flag(source.image, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  lv_obj_add_flag(source.image, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_pos(source.image, 0, 0);
  lv_obj_add_event_cb(root, style_changed_, LV_EVENT_STYLE_CHANGED, &source);
  for (size_t child = 0; child < source.children; child++)
    lv_obj_add_event_cb(lv_obj_get_child(root, child), style_changed_, LV_EVENT_STYLE_CHANGED, &source);
  source.appearance = appearance_(source);
}

uint64_t MaterialNotificationRowCache::appearance_(const Source &source) {
  uint64_t key = 14695981039346656037ULL;
  auto mix = [&key](uint64_t value) { key = (key ^ value) * 1099511628211ULL; };
  auto *root = source.root;
  if (root == nullptr)
    return 0;
  mix(lv_color_to_u32(lv_obj_get_style_bg_color(root, LV_PART_MAIN)));
  const auto opacity = lv_obj_get_style_bg_opa(root, LV_PART_MAIN);
  mix(source.slot >= 0 && opacity == LV_OPA_TRANSP ? source.background_opa : opacity);
  mix(lv_obj_get_style_radius(root, LV_PART_MAIN));
  mix(lv_obj_get_style_bg_grad_dir(root, LV_PART_MAIN));
  mix(lv_obj_get_style_opa(root, LV_PART_MAIN));
  mix(lv_obj_get_style_border_width(root, LV_PART_MAIN));
  mix(lv_color_to_u32(lv_obj_get_style_border_color(root, LV_PART_MAIN)));
  mix(lv_obj_get_style_border_opa(root, LV_PART_MAIN));
  mix(lv_obj_get_style_outline_width(root, LV_PART_MAIN));
  mix(lv_obj_get_style_shadow_width(root, LV_PART_MAIN));
  mix(lv_obj_get_width(root));
  mix(lv_obj_get_height(root));
  for (size_t child = 0; child < source.children; child++) {
    auto *object = lv_obj_get_child(root, child);
    mix(lv_color_to_u32(lv_obj_get_style_text_color(object, LV_PART_MAIN)));
    mix(lv_obj_get_style_text_opa(object, LV_PART_MAIN));
    mix(reinterpret_cast<uintptr_t>(lv_obj_get_style_text_font(object, LV_PART_MAIN)));
    mix(lv_obj_get_x(object));
    mix(lv_obj_get_y(object));
    mix(lv_obj_get_width(object));
    mix(lv_obj_get_height(object));
  }
  return key;
}

void MaterialNotificationRowCache::draw_(lv_event_t *event) {
  auto *source = static_cast<Source *>(lv_event_get_user_data(event));
  if (source->slot < 0 || source->owner->direct_drawing_)
    return;
  const auto &slot = source->owner->slots_[source->slot];
  if (!slot.valid)
    return;
  lv_area_t origin;
  lv_obj_get_coords(source->image, &origin);
  for (size_t index = 0; index < slot.part_count; index++) {
    const auto &part = slot.parts[index];
    lv_draw_image_dsc_t draw;
    lv_draw_image_dsc_init(&draw);
    lv_obj_init_draw_image_dsc(source->image, LV_PART_MAIN, &draw);
    draw.src = &part.image;
    const lv_area_t area{origin.x1 + part.x, origin.y1 + part.y,
                         origin.x1 + part.x + static_cast<int32_t>(part.image.header.w) - 1,
                         origin.y1 + part.y + static_cast<int32_t>(part.image.header.h) - 1};
    lv_draw_image(lv_event_get_layer(event), &draw, &area);
  }
}

#if defined(USE_ESP32) && defined(USE_MIPI_DSI) && defined(USE_LVGL_PPA)
template<typename F>
bool MaterialNotificationRowCache::visit_visible_parts_(lv_obj_t *viewport, const lv_area_t &clip, F &&visit) {
  lv_area_t viewport_clip;
  lv_obj_get_coords(viewport, &viewport_clip);
  if (!lv_obj_has_flag(viewport, LV_OBJ_FLAG_HIDDEN) && lv_area_intersect(&viewport_clip, &viewport_clip, &clip)) {
    for (const auto &source : this->sources_) {
      if (source.slot < 0 || lv_obj_has_flag(source.root, LV_OBJ_FLAG_HIDDEN) ||
          lv_obj_has_flag(source.image, LV_OBJ_FLAG_HIDDEN))
        continue;
      const auto &slot = this->slots_[source.slot];
      if (!slot.valid)
        continue;
      lv_area_t origin;
      lv_obj_get_coords(source.image, &origin);
      for (size_t index = 0; index < slot.part_count; index++) {
        const auto &part = slot.parts[index];
        const int32_t x = origin.x1 + part.x;
        const int32_t y = origin.y1 + part.y;
        const lv_area_t area{x, y, x + static_cast<int32_t>(part.image.header.w) - 1,
                             y + static_cast<int32_t>(part.image.header.h) - 1};
        lv_area_t visible;
        if (lv_area_intersect(&visible, &area, &viewport_clip) && !visit(part, area, visible))
          return false;
      }
    }
  }
  return true;
}

size_t MaterialNotificationRowCache::opaque_spans(lv_obj_t *viewport, const lv_area_t &clip,
                                                  lvgl::Dma2dM2mCopySpan *spans, size_t capacity) {
  size_t count = 0;
  const bool complete =
      this->visit_visible_parts_(viewport, clip, [&](const auto &part, const auto &area, const auto &visible) {
        if (part.image.header.cf != LV_COLOR_FORMAT_RGB888)
          return true;
        if (count == capacity)
          return false;
        spans[count++] = {part.data,
                          static_cast<int>(part.image.header.stride / 3),
                          static_cast<int>(part.image.header.h),
                          static_cast<int>(visible.x1 - area.x1),
                          static_cast<int>(visible.y1 - area.y1),
                          static_cast<int>(visible.x1),
                          static_cast<int>(visible.y1),
                          static_cast<int>(lv_area_get_width(&visible)),
                          static_cast<int>(lv_area_get_height(&visible))};
        return true;
      });
  return complete ? count : SIZE_MAX;
}

bool MaterialNotificationRowCache::render_overlay(lvgl::LvglComponent *parent, lv_obj_t *root, lv_obj_t *viewport,
                                                  const display::FrameBufferLease &lease, const lv_area_t &clip) {
  if (!this->visit_visible_parts_(viewport, clip, [&](const auto &part, const auto &area, const auto &visible) {
        return part.image.header.cf == LV_COLOR_FORMAT_RGB888 ||
               parent->blend_presentation_overlay_argb8888(lease, part.image, area, visible);
      }))
    return false;
  // The idle lease now contains the cached cards. LVGL draws only their
  // uncached neighbors and the modal controls over the coherent DMA result.
  this->direct_drawing_ = true;
  const bool drawn = parent->render_presentation_overlay_rgb888(root, lease, clip);
  this->direct_drawing_ = false;
  return drawn;
}
#endif

void MaterialNotificationRowCache::style_changed_(lv_event_t *event) {
  auto *source = static_cast<Source *>(lv_event_get_user_data(event));
  auto *cache = source->owner;
  if (cache->changing_)
    return;
  cache->changing_ = true;
  cache->refresh_appearances_();
  cache->changing_ = false;
}

void MaterialNotificationRowCache::refresh_appearances_() {
  bool changed = false;
  for (auto &source : this->sources_) {
    if (source.root == nullptr || source.appearance == appearance_(source))
      continue;
    changed = true;
    const auto opacity = lv_obj_get_style_bg_opa(source.root, LV_PART_MAIN);
    if (source.slot >= 0 && opacity != LV_OPA_TRANSP)
      source.background_opa = opacity;
  }
  if (!changed)
    return;  // Translation during a swipe does not change a card's pixels.
  for (auto &source : this->sources_) {
    this->restore_(source);
    source.appearance = appearance_(source);
  }
  for (auto &slot : this->slots_)
    slot.valid = false;
}

void MaterialNotificationRowCache::restore_(Source &source) {
  if (source.root == nullptr || source.slot < 0)
    return;
  lv_obj_add_flag(source.image, LV_OBJ_FLAG_HIDDEN);
  lv_obj_invalidate(source.image);
  lv_obj_set_style_bg_opa(source.root, source.background_opa, LV_PART_MAIN);
  for (size_t child = 0; child < source.children; child++) {
    if (!(source.hidden_children & (1U << child)))
      lv_obj_remove_flag(lv_obj_get_child(source.root, child), LV_OBJ_FLAG_HIDDEN);
  }
  source.slot = -1;
}

void MaterialNotificationRowCache::set_key(size_t index, const char *id, uint32_t content_hash) {
  if (index >= ROWS)
    return;
  auto &source = this->sources_[index];
  if (source.root == nullptr)
    return;
  if (std::strcmp(source.id.data(), id) == 0 && source.hash == content_hash)
    return;
  this->changing_ = true;
  this->restore_(source);
  std::strncpy(source.id.data(), id, source.id.size() - 1);
  source.hash = content_hash;
  lv_obj_update_layout(source.root);
  source.appearance = appearance_(source);
  this->changing_ = false;
}

void MaterialNotificationRowCache::attach_(Source &source, size_t index) {
  if (source.slot == static_cast<int>(index))
    return;
  this->restore_(source);
  source.background_opa = lv_obj_get_style_bg_opa(source.root, LV_PART_MAIN);
  source.hidden_children = 0;
  for (size_t child = 0; child < source.children; child++) {
    auto *object = lv_obj_get_child(source.root, child);
    if (lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN))
      source.hidden_children |= 1U << child;
    lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_set_style_bg_opa(source.root, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_size(source.image, lv_obj_get_width(source.root), lv_obj_get_height(source.root));
  lv_obj_remove_flag(source.image, LV_OBJ_FLAG_HIDDEN);
  source.slot = index;
}

bool MaterialNotificationRowCache::prepare_(Source &source, Slot &slot) {
#ifdef USE_ESP32
  this->restore_(source);
  lv_obj_update_layout(source.root);
  const int32_t width = lv_obj_get_width(source.root), height = lv_obj_get_height(source.root);
  // Ext-draw effects and changing geometry retain ordinary native rendering.
  if (width <= 0 || height <= 0 || lv_obj_get_ext_draw_size(source.root) != 0 ||
      lv_obj_get_style_bg_opa(source.root, LV_PART_MAIN) != LV_OPA_COVER ||
      lv_obj_get_style_opa(source.root, LV_PART_MAIN) != LV_OPA_COVER ||
      lv_obj_get_style_border_width(source.root, LV_PART_MAIN) != 0 ||
      lv_obj_get_style_bg_grad_dir(source.root, LV_PART_MAIN) != LV_GRAD_DIR_NONE ||
      static_cast<size_t>(width) > SIZE_MAX / 4 / static_cast<size_t>(height))
    return false;
  bool fragmented = false;
  auto reset_capture = [&]() {
    slot.valid = false;
    for (auto &part : slot.parts)
      lv_image_cache_drop(&part.image);
    slot.part_count = 0;
  };
  auto capture_region = [&](int32_t x, int32_t y, int32_t w, int32_t rows, lv_color_format_t format) -> bool {
    if (w <= 0 || rows <= 0)
      return true;
    const size_t stride = lv_draw_buf_width_to_stride(w, format);
    if ((format == LV_COLOR_FORMAT_RGB888 && stride % 3 != 0) ||
        stride > (SIZE_MAX - 127U) / static_cast<size_t>(rows) || slot.part_count == slot.parts.size())
      return false;
    auto &part = slot.parts[slot.part_count];
    const size_t span = stride * rows;
    const size_t aligned = (span + 127U) & ~size_t{127U};
    if (part.data == nullptr) {
      auto allocate = [&]() -> uint8_t * {
        const size_t free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (aligned > free || this->min_free_bytes_ > free - aligned) {
          fragmented = false;
          return nullptr;
        }
        auto *data = static_cast<uint8_t *>(heap_caps_aligned_alloc(128, aligned, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        fragmented = data == nullptr;
        return data;
      };
      part.data = allocate();
      if (part.data == nullptr && !this->reclaim_attempted_ && this->memory_reclaim_) {
        this->reclaim_attempted_ = true;
        if (this->memory_reclaim_() != 0)
          part.data = allocate();
      }
      if (part.data == nullptr)
        return false;
      part.bytes = aligned;
      slot.bytes += aligned;
    }
    if (part.bytes < span)
      return false;
    lv_draw_buf_t draw;
    if (lv_draw_buf_init(&draw, w, rows, format, stride, part.data, part.bytes) != LV_RESULT_OK ||
        !capture_row_band(source.root, &draw, x, y) ||
        esp_cache_msync(part.data, part.bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA) != ESP_OK)
      return false;
    part.x = x;
    part.y = y;
    part.image = {};
    part.image.header = draw.header;
    part.image.data = part.data;
    part.image.data_size = span;
    slot.part_count++;
    return true;
  };
  auto capture = [&](uint8_t bands) -> bool {
    reset_capture();
    slot.storage_bands = bands;
    const int32_t band_rows = height / bands + (height % bands != 0);
    for (int32_t y = 0; y < height; y += band_rows) {
      if (!capture_region(0, y, width, std::min(band_rows, height - y), LV_COLOR_FORMAT_ARGB8888))
        return false;
    }
    return true;
  };
  bool captured = false;
  if (this->packed_ && (slot.packed || slot.bytes == 0)) {
    reset_capture();
    const int32_t radius =
        std::clamp<int32_t>(lv_obj_get_style_radius(source.root, LV_PART_MAIN), 0, std::min(width, height) / 2);
    const int32_t middle_width = width - 2 * radius;
    const int32_t middle_height = height - 2 * radius;
    captured = capture_region(radius, 0, middle_width, radius, LV_COLOR_FORMAT_RGB888) &&
               capture_region(0, radius, width, middle_height, LV_COLOR_FORMAT_RGB888) &&
               capture_region(radius, height - radius, middle_width, radius, LV_COLOR_FORMAT_RGB888) &&
               capture_region(0, 0, radius, radius, LV_COLOR_FORMAT_ARGB8888) &&
               capture_region(width - radius, 0, radius, radius, LV_COLOR_FORMAT_ARGB8888) &&
               capture_region(0, height - radius, radius, radius, LV_COLOR_FORMAT_ARGB8888) &&
               capture_region(width - radius, height - radius, radius, radius, LV_COLOR_FORMAT_ARGB8888);
    slot.packed = captured;
    if (!captured)
      this->release_slot_(slot);
  }
  if (!captured) {
    uint8_t bands = std::max(this->band_count_, slot.storage_bands);
    while (!capture(bands)) {
      if (!fragmented || bands == 8)
        return false;
      // Retire only this unpublished slot, then retry with smaller allocations.
      this->release_slot_(slot);
      bands = std::min<uint8_t>(8, bands * 2);
      fragmented = false;
    }
  }
  slot.id = source.id;
  slot.hash = source.hash;
  slot.appearance = appearance_(source);
  slot.valid = true;
  source.appearance = slot.appearance;
  this->captures_++;
  return true;
#else
  (void) source;
  (void) slot;
  return false;
#endif
}

void MaterialNotificationRowCache::update(lv_obj_t *viewport, size_t count, size_t prepare_limit) {
  if (viewport == nullptr)
    return;
  this->changing_ = true;
  // Color-only LVGL style updates invalidate pixels without emitting
  // STYLE_CHANGED. Also check their small appearance signature here.
  this->refresh_appearances_();
  std::array<int32_t, ROWS> visible{};
  std::array<size_t, ROWS> order{};
  const int32_t height = lv_obj_get_height(viewport), scroll = lv_obj_get_scroll_y(viewport);
  for (size_t index = 0; index < ROWS; index++) {
    order[index] = index;
    auto &source = this->sources_[index];
    if (index >= count || source.root == nullptr || source.id[0] == '\0')
      continue;
    const int32_t top = lv_obj_get_y(source.root) - scroll;
    visible[index] =
        std::max<int32_t>(0, std::min(height, top + lv_obj_get_height(source.root)) - std::max<int32_t>(0, top));
  }
  std::sort(order.begin(), order.end(),
            [&](size_t a, size_t b) { return visible[a] != visible[b] ? visible[a] > visible[b] : a < b; });
  std::array<bool, ROWS> selected{};
  const size_t limit = this->packed_ ? SLOTS : SLOTS - 1;
  for (size_t rank = 0; rank < limit && visible[order[rank]] > 0; rank++)
    selected[order[rank]] = true;
  for (size_t index = 0; index < ROWS; index++) {
    if (!selected[index])
      this->restore_(this->sources_[index]);
  }
  std::array<bool, SLOTS> used{};
  for (size_t index : order) {
    if (!selected[index])
      continue;
    auto &source = this->sources_[index];
    for (size_t slot = 0; slot < SLOTS; slot++) {
      auto &cached = this->slots_[slot];
      if (!used[slot] && cached.valid && cached.id == source.id && cached.hash == source.hash &&
          cached.appearance == source.appearance) {
        this->attach_(source, slot);
        used[slot] = true;
        break;
      }
    }
  }
  for (size_t index : order) {
    if (!selected[index] || this->sources_[index].slot >= 0 || prepare_limit == 0)
      continue;
    for (size_t slot = 0; slot < SLOTS; slot++) {
      if (used[slot])
        continue;
      if (this->prepare_(this->sources_[index], this->slots_[slot])) {
        this->attach_(this->sources_[index], slot);
        used[slot] = true;
        prepare_limit--;
      }
      break;
    }
  }
  this->changing_ = false;
}

void MaterialNotificationRowCache::release_slot_(Slot &slot) {
  for (auto &part : slot.parts) {
    lv_image_cache_drop(&part.image);
#ifdef USE_ESP32
    if (part.data != nullptr)
      heap_caps_free(part.data);
#endif
  }
  slot = {};
}

void MaterialNotificationRowCache::clear() {
  this->changing_ = true;
  for (auto &source : this->sources_)
    this->restore_(source);
  for (auto &slot : this->slots_)
    this->release_slot_(slot);
  this->changing_ = false;
  this->reclaim_attempted_ = false;
}

size_t MaterialNotificationRowCache::bytes() const {
  size_t bytes = 0;
  for (const auto &slot : this->slots_)
    bytes += slot.bytes;
  return bytes;
}

size_t MaterialNotificationRowCache::max_band_count() const {
  size_t bands = 0;
  for (const auto &slot : this->slots_) {
    if (slot.valid)
      bands = std::max(bands, slot.part_count);
  }
  return bands;
}

size_t MaterialNotificationRowCache::ready_count() const {
  return std::count_if(this->slots_.begin(), this->slots_.end(), [](const Slot &slot) { return slot.valid; });
}

}  // namespace esphome::lvgl_material
#endif
