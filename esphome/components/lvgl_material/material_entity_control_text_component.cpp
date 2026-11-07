#include "material_entity_control_component.h"
#ifdef USE_LVGL_MATERIAL_TEXT_CONTROLS
#include "esphome/core/log.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

namespace esphome::lvgl_material {
namespace {
using Input = ControlTextInput;
// Text controls share immutable disabled properties instead of retaining a
// separate local style and property allocation for each button and key row.
static constexpr lv_style_const_prop_t TEXT_DISABLED_PROPS[] = {
    {.prop = LV_STYLE_RECOLOR_OPA, .value = {.num = LV_OPA_TRANSP}},
    {.prop = LV_STYLE_BG_OPA, .value = {.num = LV_OPA_COVER}},
    {.prop = LV_STYLE_TEXT_OPA, .value = {.num = LV_OPA_80}},
    LV_STYLE_CONST_PROPS_END};
static LV_STYLE_CONST_INIT(TEXT_DISABLED_STYLE, TEXT_DISABLED_PROPS);
void style_text_disabled(lv_obj_t *object) {
  lv_obj_add_style(object, &TEXT_DISABLED_STYLE, static_cast<lv_style_selector_t>(LV_PART_MAIN) | LV_STATE_DISABLED);
}
enum Common : size_t { ROOT, TITLE, KIND, ICON, SURFACE, VIEWPORT, CURSOR, COUNT, STATUS, CANCEL_LABEL, SAVE_LABEL };
enum Control : size_t { UP, DOWN, SHIFT, BACKSPACE, SYMBOLS, ALTERNATE, LEFT, SPACE, RIGHT, CANCEL, SAVE };
enum Local : uint8_t { NONE, PAGE_UP, PAGE_DOWN, CASE, SYMBOL_PAGE, ALTERNATE_ROW, ALPHABET };
void hidden(lv_obj_t *object, bool value) {
  if (!object || lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN) == value)
    return;
  if (value)
    lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
}
void enabled(lv_obj_t *object, bool value) {
  if (lv_obj_has_state(object, LV_STATE_DISABLED) == !value)
    return;
  if (value)
    lv_obj_remove_state(object, LV_STATE_DISABLED);
  else
    lv_obj_add_state(object, LV_STATE_DISABLED);
}
void color(lv_obj_t *object, lv_style_prop_t property, lv_color_t value, lv_style_selector_t selector = LV_PART_MAIN) {
  lv_style_value_t previous{}, desired{};
  if (lv_obj_get_local_style_prop(object, property, &previous, selector) == LV_STYLE_RES_FOUND &&
      lv_color_eq(previous.color, value))
    return;
  desired.color = value;
  lv_obj_set_local_style_prop(object, property, desired, selector);
}
bool within(lv_obj_t *root, lv_obj_t *object) {
  for (; object; object = lv_obj_get_parent(object))
    if (object == root)
      return true;
  return false;
}
bool label(lv_obj_t *root, lv_obj_t *object) {
  return within(root, object) && lv_obj_check_type(object, &lv_label_class);
}
}  // namespace

void MaterialEntityControlComponent::set_text_profile(lv_obj_t *root, lv_obj_t *title, lv_obj_t *kind, lv_obj_t *icon,
                                                      lv_obj_t *surface, lv_obj_t *viewport, lv_obj_t *cursor,
                                                      lv_obj_t *count, lv_obj_t *status, lv_obj_t *cancel,
                                                      lv_obj_t *save) {
  if (!this->setup_ready_)
    this->text_common_ = {root, title, kind, icon, surface, viewport, cursor, count, status, cancel, save};
}
void MaterialEntityControlComponent::set_text_line(size_t index, lv_obj_t *object) {
  if (!this->setup_ready_ && index < this->text_lines_.size())
    this->text_lines_[index] = object;
}
void MaterialEntityControlComponent::set_text_keyboard(size_t index, lv_obj_t *object, uint8_t count) {
  if (!this->setup_ready_ && index < this->text_keyboards_.size()) {
    this->text_keyboards_[index] = object;
    this->text_row_lengths_[index] = count;
  }
}
void MaterialEntityControlComponent::set_text_control(size_t index, lv_obj_t *object) {
  if (!this->setup_ready_ && index < this->text_controls_.size())
    this->text_controls_[index] = object;
}
void MaterialEntityControlComponent::set_text_key_label(size_t index, lv_obj_t *object) {
  if (!this->setup_ready_ && index < this->text_key_labels_.size())
    this->text_key_labels_[index] = object;
}
void MaterialEntityControlComponent::set_text_map(size_t map, size_t row, const char *const *labels,
                                                  const uint32_t *values) {
  if (!this->setup_ready_ && map < this->text_maps_.size() && row < this->text_keyboards_.size())
    this->text_maps_[map][row] = {labels, values};
}
void MaterialEntityControlComponent::set_text_repertoire(const uint32_t *points, size_t count) {
  if (!this->setup_ready_) {
    this->text_repertoire_ = points;
    this->text_repertoire_count_ = count;
  }
}
void MaterialEntityControlComponent::set_text_layout(size_t index, int16_t surface_y, int16_t surface_height,
                                                     int16_t viewport_y, uint8_t visible_lines, int16_t count_y,
                                                     int16_t up_y, int16_t down_y, int16_t save_x) {
  if (!this->setup_ready_ && index < this->text_layouts_.size())
    this->text_layouts_[index] = {surface_y, surface_height, viewport_y, visible_lines, count_y, up_y, down_y, save_x};
}
void MaterialEntityControlComponent::set_text_line_height(uint16_t height) {
  if (!this->setup_ready_)
    this->text_line_height_ = height;
}
void MaterialEntityControlComponent::set_text_enabled(bool enabled) {
  if (!this->setup_ready_)
    this->text_enabled_ = enabled;
}

bool MaterialEntityControlComponent::setup_text_profile_() {
  auto *root = this->text_common_[ROOT];
  if (!root)
    return !this->text_enabled_;
  if (!this->number_profile_ || lv_obj_get_parent(root) != this->root_ || !this->text_line_height_)
    return false;
  for (auto *object : this->text_common_)
    if (!within(root, object))
      return false;
  for (auto index : {TITLE, KIND, COUNT, STATUS, CANCEL_LABEL, SAVE_LABEL})
    if (!label(root, this->text_common_[index]))
      return false;
  for (auto *object : this->text_lines_)
    if (!label(this->text_common_[VIEWPORT], object))
      return false;
  for (auto *object : this->text_key_labels_)
    if (!label(root, object))
      return false;
  for (const auto &layout : this->text_layouts_)
    if (!layout.visible_lines || layout.visible_lines > this->text_lines_.size() || layout.viewport_y < 0 ||
        layout.viewport_y + layout.visible_lines * this->text_line_height_ > layout.surface_height)
      return false;
  for (size_t row = 0; row < this->text_keyboards_.size(); row++) {
    auto *matrix = this->text_keyboards_[row];
    const auto count = this->text_row_lengths_[row];
    if (!within(root, matrix) || !lv_obj_check_type(matrix, &lv_buttonmatrix_class) || count == 0 || count > 16)
      return false;
    for (const auto &map : this->text_maps_) {
      if (!map[row].labels || !map[row].values || std::strcmp(map[row].labels[count], "") != 0)
        return false;
      for (size_t key = 0; key < count; key++)
        if (!map[row].labels[key] || !map[row].labels[key][0])
          return false;
    }
    lv_obj_add_event_cb(matrix, input_event_cb_, LV_EVENT_ALL, this);
    style_text_disabled(matrix);
  }
  for (auto *object : this->text_controls_) {
    if (!within(root, object))
      return false;
    lv_obj_add_event_cb(object, input_event_cb_, LV_EVENT_ALL, this);
    style_text_disabled(object);
  }
  lv_obj_add_flag(this->text_common_[VIEWPORT], LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(this->text_common_[VIEWPORT], input_event_cb_, LV_EVENT_ALL, this);
  hidden(root, true);
  if (!this->text_enabled_)
    return true;
  if (!this->text_preview_retire_)
    return false;
  // Fixed sidecars are the only retained text storage. Failure disables this
  // optional profile; other device families remain usable.
  this->text_storage_ =
      RAMAllocator<ControlTextStorage>(RAMAllocator<ControlTextStorage>::ALLOC_EXTERNAL).make_unique();
  this->text_ui_storage_ = RAMAllocator<TextUiStorage>(RAMAllocator<TextUiStorage>::ALLOC_EXTERNAL).make_unique();
  if (!this->text_storage_ || !this->text_ui_storage_) {
    this->text_storage_.reset();
    this->text_ui_storage_.reset();
    this->text_enabled_ = false;
    return true;
  }
  if (!this->owner_.set_text_storage(this->text_storage_.get(), this->text_repertoire_, this->text_repertoire_count_))
    return false;
  this->extra_configured_ |= 128U;
  this->text_apply_map_();
  return this->owner_.set_extra_profiles(this->extra_configured_);
}

uint8_t MaterialEntityControlComponent::text_map_index_() const {
  return this->text_symbol_page_ ? this->text_symbol_page_ + 1
         : this->text_alternate_ ? 4 + this->text_upper_
                                 : this->text_upper_;
}
void MaterialEntityControlComponent::text_apply_map_() {
  const auto index = this->text_map_index_();
  if (index == this->text_applied_map_)
    return;
  for (size_t row = 0; row < this->text_keyboards_.size(); row++) {
    lv_buttonmatrix_set_map(this->text_keyboards_[row], this->text_maps_[index][row].labels);
    lv_buttonmatrix_set_button_ctrl_all(
        this->text_keyboards_[row],
        static_cast<lv_buttonmatrix_ctrl_t>(LV_BUTTONMATRIX_CTRL_NO_REPEAT | LV_BUTTONMATRIX_CTRL_CLICK_TRIG));
  }
  this->text_applied_map_ = index;
}
void MaterialEntityControlComponent::service_text_preview_() {
  if (this->text_preview_pending_ && this->text_preview_retire_ && this->text_preview_retire_())
    this->text_preview_pending_ = false;
}
void MaterialEntityControlComponent::text_clear_value_() {
  if (this->text_value_retained_) {
    this->text_preview_pending_ = true;
    this->text_value_retained_ = false;
  }
  this->text_render_valid_ = false;
  // Detach static label pointers before zeroing the reusable visible pool.
  for (auto *line : this->text_lines_)
    if (line)
      lv_label_set_text_static(line, "");
  if (this->text_ui_storage_) {
    this->text_ui_storage_->visible.fill(0);
    this->text_ui_storage_->offsets.fill(0);
  }
  hidden(this->text_common_[CURSOR], true);
  this->text_first_line_ = 0;
  this->text_line_count_ = 1;
  this->text_ready_ = false;
}

uint16_t MaterialEntityControlComponent::text_cursor_line_(uint16_t cursor) const {
  size_t offset = 0;
  if (!this->text_ui_storage_ || !control_text_offset(this->owner_.text_draft().value, cursor, offset))
    return 0;
  uint16_t line = 0;
  while (line + 1 < this->text_line_count_ && this->text_ui_storage_->offsets[line + 1] <= offset)
    line++;
  return line;
}
int32_t MaterialEntityControlComponent::text_cursor_x_(uint16_t cursor) const {
  const auto value = this->owner_.text_draft().value;
  const auto *font = lv_obj_get_style_text_font(this->text_lines_[0], LV_PART_MAIN);
  const auto spacing = lv_obj_get_style_text_letter_space(this->text_lines_[0], LV_PART_MAIN);
  size_t end = 0;
  if (!control_text_offset(value, cursor, end))
    return 0;
  size_t pos = this->text_ui_storage_->offsets[this->text_cursor_line_(cursor)];
  int32_t x = 0;
  while (pos < end) {
    uint32_t point;
    if (!control_text_next(value, pos, point))
      break;
    x += lv_font_get_glyph_width(font, point, 0) + spacing;
  }
  return x;
}
uint16_t MaterialEntityControlComponent::text_cursor_at_(int32_t x, uint16_t line) const {
  const auto value = this->owner_.text_draft().value;
  const auto *font = lv_obj_get_style_text_font(this->text_lines_[0], LV_PART_MAIN);
  const auto spacing = lv_obj_get_style_text_letter_space(this->text_lines_[0], LV_PART_MAIN);
  line = std::min<uint16_t>(line, this->text_line_count_ - 1);
  const size_t start = this->text_ui_storage_->offsets[line], end = this->text_ui_storage_->offsets[line + 1];
  size_t pos = 0;
  uint16_t cursor = 0;
  int32_t advance = 0;
  while (pos < end) {
    const auto before = pos;
    uint32_t point;
    if (!control_text_next(value, pos, point))
      break;
    if (before >= start) {
      const auto width = lv_font_get_glyph_width(font, point, 0) + spacing;
      if (x < advance + width / 2) {
        pos = before;
        break;
      }
      advance += width;
    }
    cursor++;
  }
  // A wrapped boundary belongs to the following visual line.
  if (line + 1 < this->text_line_count_ && pos == end && cursor)
    cursor--;
  return cursor;
}

bool MaterialEntityControlComponent::text_render_value_(std::string_view value, uint16_t cursor, bool editing) {
  if (!this->text_ui_storage_ || value.size() > 1020)
    return false;
  auto &storage = *this->text_ui_storage_;
  auto *viewport = this->text_common_[VIEWPORT];
  const auto *font = lv_obj_get_style_text_font(this->text_lines_[0], LV_PART_MAIN);
  const auto spacing = lv_obj_get_style_text_letter_space(this->text_lines_[0], LV_PART_MAIN);
  const int32_t width = lv_obj_get_content_width(viewport) - lv_obj_get_width(this->text_common_[CURSOR]);
  if (!font || width <= 0 || lv_font_get_line_height(font) > this->text_line_height_)
    return false;
  const auto draft = this->owner_.text_draft();
  if (this->text_render_valid_ && this->text_render_revision_ == draft.revision &&
      this->text_render_generation_ == this->text_display_generation_ && this->text_render_font_ == font &&
      this->text_render_width_ == width && this->text_render_spacing_ == spacing &&
      this->text_render_first_ == this->text_first_line_ && this->text_render_cursor_ == cursor &&
      this->text_render_editing_ == editing)
    return true;
  this->text_render_valid_ = false;
  size_t pos = 0;
  int32_t x = 0;
  uint16_t count = 1;
  storage.offsets[0] = 0;
  while (pos < value.size()) {
    const auto begin = pos;
    uint32_t point;
    lv_font_glyph_dsc_t glyph{};
    if (!control_text_next(value, pos, point) || !lv_font_get_glyph_dsc(font, &glyph, point, 0) || glyph.is_placeholder)
      return false;
    const auto advance = lv_font_get_glyph_width(font, point, 0) + spacing;
    if (advance > width)
      return false;
    if (x && x + advance > width) {
      if (count >= 255)
        return false;
      storage.offsets[count++] = begin;
      x = 0;
    }
    x += advance;
  }
  storage.offsets[count] = value.size();
  this->text_line_count_ = count;
  const auto lines = this->text_layouts_[editing ? 1 : 0].visible_lines;
  this->text_first_line_ = std::min<uint16_t>(this->text_first_line_, count > lines ? count - lines : 0);
  const auto cursor_line = this->text_cursor_line_(cursor);
  if (editing) {
    if (cursor_line < this->text_first_line_)
      this->text_first_line_ = cursor_line;
    else if (cursor_line >= this->text_first_line_ + lines)
      this->text_first_line_ = cursor_line + 1 - lines;
  }
  for (auto *line : this->text_lines_)
    lv_label_set_text_static(line, "");
  storage.visible.fill(0);
  size_t destination = 0;
  for (size_t row = 0; row < this->text_lines_.size(); row++) {
    auto *object = this->text_lines_[row];
    const size_t line = this->text_first_line_ + row;
    hidden(object, row >= lines);
    if (row >= lines || line >= count)
      continue;
    const size_t begin = storage.offsets[line], bytes = storage.offsets[line + 1] - begin;
    if (destination + bytes + 1 > storage.visible.size())
      return false;
    if (bytes)
      std::memcpy(storage.visible.data() + destination, value.data() + begin, bytes);
    lv_label_set_text_static(object, storage.visible.data() + destination);
    destination += bytes + 1;
  }
  hidden(this->text_common_[CURSOR], !editing);
  if (editing)
    lv_obj_set_pos(this->text_common_[CURSOR], this->text_cursor_x_(cursor),
                   (cursor_line - this->text_first_line_) * this->text_line_height_);
  this->text_render_revision_ = draft.revision;
  this->text_render_generation_ = this->text_display_generation_;
  this->text_render_font_ = font;
  this->text_render_width_ = width;
  this->text_render_spacing_ = spacing;
  this->text_render_first_ = this->text_first_line_;
  this->text_render_cursor_ = cursor;
  this->text_render_editing_ = editing;
  this->text_render_valid_ = true;
  this->text_value_retained_ = true;
  return true;
}

void MaterialEntityControlComponent::restore_text_widgets_() {
  if (!this->text_widgets_active_)
    return;
  this->text_clear_value_();
  this->text_was_editing_ = false;
  this->text_upper_ = this->text_alternate_ = false;
  this->text_symbol_page_ = 0;
  this->text_display_generation_ = 0;
  if (!this->owner_.has_selection()) {
    for (auto *control : this->text_controls_)
      enabled(control, false);
    for (auto *matrix : this->text_keyboards_)
      hidden(matrix, true);
    this->extra_text_(this->text_common_[COUNT], "");
    this->extra_text_(this->text_common_[STATUS], this->text_(15, "Unknown"));
    return;
  }
  lv_obj_t *base[] = {this->title_,     this->hit_,        this->value_,       this->unknown_value_,
                      this->on_button_, this->off_button_, this->state_label_, this->status_label_};
  for (size_t index = 0; index < std::size(base); index++)
    hidden(base[index], (this->text_hidden_mask_ & (1U << index)) != 0);
  hidden(this->text_common_[ROOT], true);
  this->text_widgets_active_ = false;
}

void MaterialEntityControlComponent::update_text_static_() {
  const auto *state = this->owner_.state().family_if<ControlTextSnapshot>();
  if (!this->owner_.is_text() || !state || this->owner_.is_dragging() || !this->text_ui_storage_)
    return;
  lv_obj_t *base[] = {this->title_,     this->hit_,        this->value_,       this->unknown_value_,
                      this->on_button_, this->off_button_, this->state_label_, this->status_label_};
  if (!this->text_widgets_active_) {
    this->text_hidden_mask_ = 0;
    for (size_t i = 0; i < std::size(base); i++)
      if (base[i] && lv_obj_has_flag(base[i], LV_OBJ_FLAG_HIDDEN))
        this->text_hidden_mask_ |= 1U << i;
    this->text_widgets_active_ = true;
  }
  for (auto *object : base)
    hidden(object, true);
  hidden(this->text_common_[ROOT], false);
  const auto draft = this->owner_.text_draft();
  const bool editing = draft.active;
  if (state->state_generation != this->text_display_generation_ || this->text_was_editing_ != editing) {
    this->text_first_line_ = 0;
    this->text_display_generation_ = state->state_generation;
    if (!editing) {
      this->text_upper_ = this->text_alternate_ = false;
      this->text_symbol_page_ = 0;
    }
  }
  this->text_was_editing_ = editing;
  const auto &layout = this->text_layouts_[editing ? 1 : 0];
  lv_obj_set_y(this->text_common_[SURFACE], layout.surface_y);
  lv_obj_set_height(this->text_common_[SURFACE], layout.surface_height);
  lv_obj_set_y(this->text_common_[VIEWPORT], layout.viewport_y);
  lv_obj_set_height(this->text_common_[VIEWPORT], layout.visible_lines * this->text_line_height_);
  lv_obj_set_y(this->text_common_[COUNT], layout.count_y);
  lv_obj_set_y(this->text_controls_[UP], layout.up_y);
  lv_obj_set_y(this->text_controls_[DOWN], layout.down_y);
  lv_obj_set_x(this->text_controls_[SAVE], layout.save_x);
  hidden(this->text_common_[ICON], editing);
  for (auto *matrix : this->text_keyboards_)
    hidden(matrix, !editing);
  for (size_t i = SHIFT; i <= CANCEL; i++)
    hidden(this->text_controls_[i], !editing);
  lv_obj_update_layout(this->root_);
  bool complete =
      this->extra_text_(this->text_common_[TITLE], this->owner_.state().title.bytes.data(), nullptr, nullptr, true);
  complete &= this->extra_text_(this->text_common_[KIND], this->text_(18, "Text"));
  if (state->available()) {
    if (!this->text_render_value_(draft.value, draft.cursor, editing)) {
      this->text_clear_value_();
      complete = false;
    }
    if (draft.value.empty() && !editing)
      complete &= this->extra_text_(this->text_lines_[0], this->text_(19, "Empty"));
  } else {
    this->text_clear_value_();
    complete &= this->extra_text_(this->text_lines_[0], "--");
  }
  char counter[96];
  if (state->bounds_known && state->available())
    std::snprintf(counter, sizeof(counter), "%u / %u  \xC2\xB7  %u\xE2\x80\x93%u", unsigned(draft.codepoints),
                  unsigned(state->maximum), unsigned(this->text_first_line_ + 1),
                  unsigned(std::min<uint16_t>(this->text_line_count_, this->text_first_line_ + layout.visible_lines)));
  else
    counter[0] = 0;
  complete &= this->extra_text_(this->text_common_[COUNT], counter);
  complete &= this->extra_text_(this->text_common_[CANCEL_LABEL], this->text_(21, "Cancel"));
  complete &=
      this->extra_text_(this->text_common_[SAVE_LABEL], this->text_(editing ? 22 : 20, editing ? "Save" : "Edit"));
  complete &= this->extra_text_(this->text_key_labels_[0], this->text_symbol_page_ == 1   ? "#+="
                                                           : this->text_symbol_page_ == 2 ? "abc"
                                                                                          : "123");
  complete &= this->extra_text_(this->text_key_labels_[1], this->text_(23, "Alt"));
  complete &= this->extra_text_(this->text_key_labels_[2], this->text_(24, "Space"));
  this->text_apply_map_();
  this->update_status_();
  const char *status = this->text_(static_cast<uint8_t>(this->status_) + 2, this->get_status_key());
  if (this->owner_.is_bound() && !this->owner_.is_command_pending() && this->owner_.is_enabled()) {
    if (!state->available()) {
      if (state->source == 3) {
        constexpr const char *REASONS[] = {"Not supported",    "Hidden value",          "Unsupported mode",
                                           "Pattern required", "Invalid limits",        "Invalid length",
                                           "Invalid encoding", "Unsupported characters"};
        const uint8_t reason = state->reason < std::size(REASONS) ? state->reason : 0;
        status = this->text_(31 + reason, REASONS[reason]);
      } else
        status = this->text_(state->source == 1 ? 15 : 16, state->source == 1 ? "Unknown" : "Unavailable");
    } else if (!state->can_set)
      status = this->text_(25, "Read only");
    else if (editing && !draft.valid)
      status = this->text_(draft.reason == ControlTextDraftReason::CONTROL_TEXT_DRAFT_REASON_BELOW_MIN  ? 28
                           : draft.reason == ControlTextDraftReason::CONTROL_TEXT_DRAFT_REASON_RESERVED ? 30
                                                                                                        : 29,
                           "Correct the value");
    else if (draft.changed_elsewhere)
      status = this->text_(27, "Changed in Home Assistant");
    else if (draft.dirty)
      status = this->text_(26, "Unsaved changes");
  }
  complete &= this->extra_text_(this->text_common_[STATUS],
                                complete ? status : this->text_(17, "Cannot display the full value"));
  this->text_ready_ = complete;
  const auto active_bg = configured_style_color_(this->on_button_, LV_STYLE_BG_COLOR),
             active_fg = configured_style_color_(this->minus_label_, LV_STYLE_TEXT_COLOR),
             neutral_bg = configured_style_color_(this->off_button_, LV_STYLE_BG_COLOR),
             neutral_fg = configured_style_color_(this->plus_label_, LV_STYLE_TEXT_COLOR);
  color(this->text_common_[ICON], LV_STYLE_TEXT_COLOR, active_bg, LV_PART_MAIN);
  color(this->text_common_[SURFACE], LV_STYLE_BG_COLOR, neutral_bg, LV_PART_MAIN);
  color(this->text_common_[CURSOR], LV_STYLE_BG_COLOR, neutral_fg, LV_PART_MAIN);
  for (auto *line : this->text_lines_)
    color(line, LV_STYLE_TEXT_COLOR, neutral_fg, LV_PART_MAIN);
  const auto palette = [&](lv_obj_t *object, bool accent) {
    const auto bg = accent ? active_bg : neutral_bg, fg = accent ? active_fg : neutral_fg;
    color(object, LV_STYLE_BG_COLOR, bg, LV_PART_MAIN);
    color(object, LV_STYLE_TEXT_COLOR, fg, LV_PART_MAIN);
    for (uint32_t child = 0; child < lv_obj_get_child_count(object); child++)
      color(lv_obj_get_child(object, child), LV_STYLE_TEXT_COLOR, fg, LV_PART_MAIN);
  };
  const bool view = complete && this->owner_.can_text_input(Input::CONTROL_TEXT_INPUT_VIEW);
  for (size_t row = 0; row < this->text_keyboards_.size(); row++) {
    auto *matrix = this->text_keyboards_[row];
    color(matrix, LV_STYLE_BG_COLOR, neutral_bg, LV_PART_ITEMS);
    color(matrix, LV_STYLE_TEXT_COLOR, neutral_fg, LV_PART_ITEMS);
    enabled(matrix, editing && view);
    for (size_t key = 0; key < this->text_row_lengths_[row]; key++) {
      const auto scalar = this->text_maps_[this->text_map_index_()][row].values[key];
      const bool allow = editing && complete &&
                         this->owner_.can_text_input(
                             scalar ? Input::CONTROL_TEXT_INPUT_INSERT : Input::CONTROL_TEXT_INPUT_VIEW, scalar);
      if (lv_buttonmatrix_has_button_ctrl(matrix, key, LV_BUTTONMATRIX_CTRL_DISABLED) == !allow)
        continue;
      if (allow)
        lv_buttonmatrix_clear_button_ctrl(matrix, key, LV_BUTTONMATRIX_CTRL_DISABLED);
      else
        lv_buttonmatrix_set_button_ctrl(matrix, key, LV_BUTTONMATRIX_CTRL_DISABLED);
    }
  }
  const auto cursor_line = this->text_cursor_line_(draft.cursor);
  for (size_t i = 0; i < this->text_controls_.size(); i++) {
    bool allow = view;
    if (i == UP)
      allow &= editing ? cursor_line > 0 : this->text_first_line_ > 0;
    else if (i == DOWN)
      allow &= editing ? cursor_line + 1 < this->text_line_count_
                       : this->text_first_line_ + layout.visible_lines < this->text_line_count_;
    else if (i == BACKSPACE)
      allow &= this->owner_.can_text_input(Input::CONTROL_TEXT_INPUT_BACKSPACE);
    else if (i == LEFT || i == RIGHT)
      allow &= this->owner_.can_text_input(i == LEFT ? Input::CONTROL_TEXT_INPUT_MOVE_LEFT
                                                     : Input::CONTROL_TEXT_INPUT_MOVE_RIGHT);
    else if (i == SPACE)
      allow &= this->owner_.can_text_input(Input::CONTROL_TEXT_INPUT_INSERT, ' ');
    else if (i == CANCEL)
      allow &= this->owner_.can_text_input(Input::CONTROL_TEXT_INPUT_CANCEL);
    else if (i == SAVE)
      allow &= this->owner_.can_text_input(editing ? Input::CONTROL_TEXT_INPUT_SAVE
                                                   : Input::CONTROL_TEXT_INPUT_BEGIN_EDITOR);
    palette(this->text_controls_[i],
            allow && (i == SAVE || (i == SHIFT && this->text_upper_) || (i == ALTERNATE && this->text_alternate_)));
    enabled(this->text_controls_[i], allow);
  }
}

void MaterialEntityControlComponent::text_input_event_(lv_event_code_t code, lv_obj_t *target, lv_indev_t *indev,
                                                       uint32_t now) {
  if (!this->text_ui_storage_)
    return;
  Input action = Input::CONTROL_TEXT_INPUT_VIEW;
  uint32_t argument = 0;
  uint8_t local = NONE, selected = 255;
  bool found = false, matrix = false;
  const auto draft = this->owner_.text_draft();
  for (size_t row = 0; row < this->text_keyboards_.size(); row++)
    if (target == this->text_keyboards_[row]) {
      const auto key = lv_buttonmatrix_get_selected_button(target);
      if (key >= this->text_row_lengths_[row]) {
        if (this->captured_target_ == target)
          this->cancel_pointer_();
        return;
      }
      selected = key;
      argument = this->text_maps_[this->text_map_index_()][row].values[key];
      action = argument ? Input::CONTROL_TEXT_INPUT_INSERT : Input::CONTROL_TEXT_INPUT_VIEW;
      local = argument ? NONE : ALPHABET;
      found = matrix = true;
    }
  for (size_t i = 0; i < this->text_controls_.size(); i++)
    if (target == this->text_controls_[i]) {
      found = true;
      if (i == UP || i == DOWN) {
        if (draft.active) {
          const auto line = this->text_cursor_line_(draft.cursor);
          action = Input::CONTROL_TEXT_INPUT_SELECT_CURSOR;
          argument =
              this->text_cursor_at_(this->text_cursor_x_(draft.cursor), i == UP ? (line ? line - 1 : 0) : line + 1);
        } else
          local = i == UP ? PAGE_UP : PAGE_DOWN;
      } else if (i == SHIFT)
        local = CASE;
      else if (i == SYMBOLS)
        local = SYMBOL_PAGE;
      else if (i == ALTERNATE)
        local = ALTERNATE_ROW;
      else if (i == BACKSPACE)
        action = Input::CONTROL_TEXT_INPUT_BACKSPACE;
      else if (i == LEFT || i == RIGHT)
        action = i == LEFT ? Input::CONTROL_TEXT_INPUT_MOVE_LEFT : Input::CONTROL_TEXT_INPUT_MOVE_RIGHT;
      else if (i == SPACE) {
        action = Input::CONTROL_TEXT_INPUT_INSERT;
        argument = ' ';
      } else if (i == CANCEL)
        action = Input::CONTROL_TEXT_INPUT_CANCEL;
      else if (i == SAVE)
        action = draft.active ? Input::CONTROL_TEXT_INPUT_SAVE : Input::CONTROL_TEXT_INPUT_BEGIN_EDITOR;
    }
  if (target == this->text_common_[VIEWPORT] && draft.active && indev) {
    lv_point_t point;
    lv_area_t bounds;
    lv_indev_get_point(indev, &point);
    lv_obj_get_coords(target, &bounds);
    action = Input::CONTROL_TEXT_INPUT_SELECT_CURSOR;
    argument =
        this->text_cursor_at_(point.x - bounds.x1, this->text_first_line_ + std::max<int32_t>(0, point.y - bounds.y1) /
                                                                                this->text_line_height_);
    found = true;
  }
  if (!found)
    return;
  if (code == LV_EVENT_PRESSED) {
    if (this->captured_target_ || !this->text_ready_ || !indev || lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER ||
        !this->is_presented_() || !lv_obj_is_visible(target) || lv_obj_has_state(target, LV_STATE_DISABLED) ||
        this->pointer_sequence_ == std::numeric_limits<uint32_t>::max() ||
        this->text_map_generation_ == std::numeric_limits<uint32_t>::max())
      return;
#ifdef USE_LVGL_NAVIGATION_ADMISSION_GUARD
    if (this->view_lifecycle_.native_power_input_active())
      return;
#endif
    if (!this->owner_.begin_text_input(++this->pointer_sequence_, action, argument, this->text_map_generation_, now))
      return;
    this->captured_target_ = target;
    this->captured_indev_ = indev;
    this->captured_generation_ = this->owner_.admission_generation();
    this->text_local_action_ = local;
    this->text_pressed_key_ = selected;
  }
  if (target != this->captured_target_ || indev != this->captured_indev_)
    return;
  if (code == LV_EVENT_PRESS_LOST || !this->is_presented_() || !lv_obj_is_visible(target) ||
      this->captured_generation_ != this->owner_.admission_generation() || !this->owner_.is_dragging() ||
      (matrix && selected != this->text_pressed_key_)) {
    this->cancel_pointer_();
    this->process_events_();
    return;
  }
  if (code == LV_EVENT_RELEASED) {
    lv_point_t point;
    lv_area_t bounds;
    lv_indev_get_point(indev, &point);
    lv_obj_get_coords(target, &bounds);
    this->captured_target_ = nullptr;
    this->captured_indev_ = nullptr;
    if (point.x < bounds.x1 || point.x > bounds.x2 || point.y < bounds.y1 || point.y > bounds.y2)
      this->owner_.cancel_text_input();
    else if (this->owner_.release_text_input(this->pointer_sequence_, this->text_map_generation_, now)) {
      const auto local_action = this->text_local_action_;
      if (local_action == PAGE_UP)
        this->text_first_line_ = this->text_first_line_ ? this->text_first_line_ - 1 : 0;
      else if (local_action == PAGE_DOWN)
        this->text_first_line_++;
      else if (local_action == CASE)
        this->text_upper_ = !this->text_upper_;
      else if (local_action == SYMBOL_PAGE) {
        this->text_symbol_page_ = (this->text_symbol_page_ + 1) % 3;
        this->text_alternate_ = false;
      } else if (local_action == ALTERNATE_ROW) {
        this->text_symbol_page_ = 0;
        this->text_alternate_ = !this->text_alternate_;
      } else if (local_action == ALPHABET) {
        this->text_symbol_page_ = 0;
        this->text_alternate_ = false;
      } else if (matrix && action == Input::CONTROL_TEXT_INPUT_INSERT)
        this->text_alternate_ = false;
      this->text_map_generation_++;
    }
    this->text_local_action_ = NONE;
    this->update_static_();
  }
  this->process_events_();
}

void MaterialEntityControlComponent::log_text_view(const char *phase) const {
#ifdef USE_LVGL_MATERIAL_SCALAR_PERFORMANCE_METRICS
  const auto *state = this->owner_.is_text() ? this->owner_.state().family_if<ControlTextSnapshot>() : nullptr;
  const auto draft = this->owner_.text_draft();
  ESP_LOGI("scalar.performance",
           "%s text_active=%u text_retained=%u text_ready=%u text_state_seq=%u text_generation=%u "
           "text_source=%u text_draft=%u text_revision=%u text_cursor=%u text_length=%u text_first_line=%u "
           "text_lines=%u text_can_save=%u text_map=%u text_result=%u",
           phase, unsigned(state != nullptr), unsigned(this->text_widgets_active_), unsigned(this->text_ready_),
           unsigned(state ? this->owner_.state().state_seq : 0), unsigned(state ? state->state_generation : 0),
           unsigned(state ? state->source : 255), unsigned(draft.active), unsigned(draft.revision),
           unsigned(draft.cursor), unsigned(draft.codepoints), unsigned(this->text_first_line_),
           unsigned(this->text_line_count_), unsigned(draft.can_save), unsigned(this->text_map_index_()),
           unsigned(this->owner_.last_result()));
#else
  (void) phase;
#endif
}
}  // namespace esphome::lvgl_material
#endif
