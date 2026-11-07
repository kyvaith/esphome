#include "material_notification_center.h"
#ifdef USE_LVGL_MATERIAL_NOTIFICATION_CENTER
#include "esphome/components/json/json_util.h"
#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "core/lv_obj_scroll_private.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace esphome::lvgl_material {

static bool valid_id(JsonVariantConst value) {
  if (!value.is<const char *>() || value.as<JsonString>().size() != 32)
    return false;
  for (size_t index = 0; index < 32; index++) {
    const char ch = value.as<const char *>()[index];
    if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f')))
      return false;
  }
  return true;
}

static bool bounded_text(JsonVariantConst value, size_t bytes) {
  return value.is<const char *>() && value.as<JsonString>().size() <= bytes &&
         std::strlen(value.as<const char *>()) == value.as<JsonString>().size();
}

void MaterialNotificationCenter::set_row(size_t index, lv_obj_t *root, lv_obj_t *title, lv_obj_t *message) {
  if (index < this->rows_.size()) {
    this->rows_[index].root = root;
    this->rows_[index].title = title;
    this->rows_[index].message = message;
  }
}

void MaterialNotificationCenter::set_row_metadata(size_t index, lv_obj_t *source, lv_obj_t *age) {
  if (index < this->rows_.size()) {
    this->rows_[index].source = source;
    this->rows_[index].age = age;
  }
}

void MaterialNotificationCenter::setup() {
  if (this->root_ == nullptr || this->heading_ == nullptr || this->status_ == nullptr || this->detail_ == nullptr ||
      (this->history_scroll_ &&
       (this->list_view_ == nullptr || this->row_pitch_ == 0 || this->close_edge_pixels_ == 0)) ||
      (!this->history_scroll_ && (this->close_ == nullptr || this->previous_ == nullptr || this->next_ == nullptr))) {
    this->mark_failed();
    return;
  }
  for (const auto &row : this->rows_) {
    if (row.root == nullptr || row.title == nullptr || row.message == nullptr) {
      this->mark_failed();
      return;
    }
  }
  this->scene_.setup();
  auto *system_layer = lv_display_get_layer_sys(this->parent_->get_disp());
  lv_obj_set_parent(this->root_, system_layer);
  lv_obj_remove_flag(this->root_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_update_layout(this->root_);
  this->origin_y_ = lv_obj_get_y(this->root_);
  this->origin_scroll_y_ = lv_obj_get_scroll_y(this->root_);
  this->native_transition_ = this->history_scroll_ && lv_obj_get_width(this->root_) == this->parent_->get_width() &&
                             lv_obj_get_height(this->root_) == this->parent_->get_height() && this->origin_y_ == 0 &&
                             lv_obj_get_x(this->root_) == 0;
  if (this->native_transition_) {
    lv_obj_set_scrollbar_mode(this->root_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(this->root_, transition_size_cb_, LV_EVENT_GET_SELF_SIZE, this);
  }
  if (this->history_scroll_) {
    lv_obj_set_scrollbar_mode(this->list_view_, LV_SCROLLBAR_MODE_OFF);
    for (size_t index = 0; index < this->rows_.size(); index++)
      lv_obj_set_y(this->rows_[index].root, index * this->row_pitch_);
  }
  this->banner_->set_system_layer(system_layer);
  this->scene_.set_admission(
      [this]() { return !this->navigation_->is_transition_active() && (!this->admission_ || this->admission_()); });
  this->banner_->set_scene(&this->scene_);
  this->navigation_->set_touch_delegate(this);
  this->navigation_->add_admission_guard(this);
#ifdef USE_LVGL_MATERIAL_NOTIFICATION_ROW_CACHE
  if (this->row_cache_enabled_) {
    this->row_cache_.set_memory_reclaim([this]() { return this->parent_->reclaim_idle_application_previews(); });
    for (size_t index = 0; index < this->rows_.size(); index++)
      this->row_cache_.setup(index, this->rows_[index].root);
  }
#endif
  lv_obj_add_flag(this->root_, LV_OBJ_FLAG_HIDDEN);
}

void MaterialNotificationCenter::loop() {
  const uint32_t now = App.get_loop_component_start_time();
#ifdef USE_LVGL_MATERIAL_NOTIFICATION_ACTION
  if (this->action_deadline_ != 0 && static_cast<int32_t>(now - this->action_deadline_) >= 0) {
    this->action_deadline_ = 0;
    this->action_id_[0] = '\0';
    if (this->open_)
      this->render_detail_action_();
  }
  if (this->action_id_[0] != '\0' && !this->open_ && !this->closing_) {
    const auto id = this->action_id_;
    this->action_id_[0] = '\0';
    if (this->detail_action_available_() && this->navigation_->is_idle_home())
      this->request_("activate", id.data());
    this->action_deadline_ = 0;
  }
#endif
  if (this->pending_ && now - this->pending_at_ >= 5000) {
    this->pending_ = false;
    const bool restore_row = this->pending_dismiss_id_[0] != '\0';
    this->pending_dismiss_id_[0] = '\0';
    if (this->open_) {
      if (restore_row)
        this->render_();
      lv_label_set_text(this->status_, this->text_(3));
    }
  }
  if (this->closing_) {
    if (!this->close_animating_)
      this->finish_close_();
    else
      this->scene_.loop();
    return;
  }
  if (!this->open_) {
    this->scene_.loop();  // The banner shares the modal scene.
    return;
  }
  if (now - this->age_update_at_ >= 60000) {
    this->age_update_at_ = now;
    this->update_ages_();
  }
  if (this->inertia_ && !this->opening_) {
    const int32_t tick_delta = static_cast<int32_t>(now - this->inertia_tick_);
    if (tick_delta < (this->scene_.direct() ? 8 : 1)) {
      this->scene_.loop();
      return;
    }
    const uint32_t elapsed = std::min<uint32_t>(32, tick_delta);
    this->inertia_tick_ = now;
    const int32_t before = this->scroll_y_;
    this->set_scroll_y_(before + static_cast<int32_t>(std::lround(this->scroll_velocity_ * elapsed)));
    this->request_scroll_window_();
    this->scroll_velocity_ *= std::exp(-static_cast<float>(elapsed) / 150.0f);
    if (std::abs(this->scroll_velocity_) < 0.03f || (before == this->scroll_y_ && !this->pending_))
      this->inertia_ = false;
  }
  this->update_row_cache_(1);
  this->scene_.loop();
}

bool MaterialNotificationCenter::open() {
  if (this->is_failed() || this->open_ || this->closing_)
    return this->open_;
  if (!this->scene_.begin())
    return false;
  if (this->cached_backdrop_dim_ && !this->scene_.dim_background()) {
    this->closing_ = !this->scene_.end();
    return false;
  }
  this->banner_->hold_for_center(true);
  this->open_ = true;
  this->scroll_y_ = this->desired_scroll_y_ = 0;
  this->scroll_direction_ = 0;
  this->detail_id_[0] = '\0';
#ifdef USE_LVGL_MATERIAL_NOTIFICATION_ACTION
  this->action_deadline_ = 0;
#endif
  this->opening_ = this->transition_duration_ != 0;
  this->set_transition_position_(this->origin_y_ - (this->opening_ ? this->parent_->get_height() : 0));
#ifdef USE_LVGL_MATERIAL_NOTIFICATION_ROW_CACHE
  this->row_cache_.set_packed(this->scene_.direct() && this->row_pitch_ >= lv_obj_get_height(this->rows_[0].root));
#endif
  this->render_();
  this->update_row_cache_(4);
  lv_obj_remove_flag(this->root_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(this->root_);
#if defined(USE_LVGL_MATERIAL_NOTIFICATION_ROW_CACHE) && defined(USE_ESP32) && defined(USE_MIPI_DSI) && \
    defined(USE_LVGL_PPA)
  if (this->history_scroll_ && this->row_cache_enabled_) {
    this->scene_.set_overlay(
        this->root_,
        [this](const auto &lease, const auto &clip) {
          return this->row_cache_.render_overlay(this->parent_, this->root_, this->list_view_, lease, clip);
        },
        [this](const auto &clip, auto *spans, size_t capacity) {
          return this->row_cache_.opaque_spans(this->list_view_, clip, spans, capacity);
        });
  } else
#endif
    this->scene_.set_overlay(this->root_);
  if (this->opening_)
    this->animate_transition_(this->origin_y_ - this->parent_->get_height(), this->origin_y_);
  this->request_("page", nullptr, this->history_scroll_ ? 0 : -1);
  return true;
}

void MaterialNotificationCenter::close() {
  this->touch_cancel();
  if (this->closing_ || !this->open_)
    return;
  this->stop_reflow_();
  this->open_ = false;
  this->closing_ = true;
  this->pending_ = this->pending_dismiss_id_[0] != '\0';
  this->opening_ = false;
  if (this->transition_duration_ != 0) {
    this->close_animating_ = true;
    this->animate_transition_(this->transition_position_(), this->origin_y_ - this->parent_->get_height());
    return;
  }
  this->finish_close_();
}

void MaterialNotificationCenter::finish_close_() {
  lv_obj_add_flag(this->root_, LV_OBJ_FLAG_HIDDEN);
#ifdef USE_LVGL_MATERIAL_NOTIFICATION_ROW_CACHE
  this->row_cache_.clear();
#endif
  this->set_transition_position_(this->origin_y_);
  // end() restores the immutable base and both LVGL buffers before resuming scenes.
  if (this->scene_.end()) {
    this->closing_ = false;
    this->banner_->hold_for_center(false);
  }
}

void MaterialNotificationCenter::transition_exec_(void *object, int32_t value) {
  static_cast<MaterialNotificationCenter *>(object)->set_transition_position_(value);
}

void MaterialNotificationCenter::transition_size_cb_(lv_event_t *event) {
  auto *center = static_cast<MaterialNotificationCenter *>(lv_event_get_user_data(event));
  auto *size = static_cast<lv_point_t *>(lv_event_get_param(event));
  // Reserve one display height of virtual travel. Layout must not clamp the
  // opening offset back to zero while a new history window changes its text.
  size->y = std::max<int32_t>(size->y, center->parent_->get_height() * 2 + center->origin_scroll_y_);
}

void MaterialNotificationCenter::set_transition_position_(int32_t y) {
  if (this->native_transition_) {
    const int32_t scroll = this->origin_scroll_y_ + this->origin_y_ - y;
    lv_obj_scroll_by_raw(this->root_, 0, lv_obj_get_scroll_y(this->root_) - scroll);
  } else {
    lv_obj_set_y(this->root_, y);
  }
}

int32_t MaterialNotificationCenter::transition_position_() const {
  return this->native_transition_ ? this->origin_y_ + this->origin_scroll_y_ - lv_obj_get_scroll_y(this->root_)
                                  : lv_obj_get_y(this->root_);
}

void MaterialNotificationCenter::transition_ready_(lv_anim_t *animation) {
  auto *center = static_cast<MaterialNotificationCenter *>(lv_anim_get_user_data(animation));
  center->opening_ = false;
  center->close_animating_ = false;
}

void MaterialNotificationCenter::animate_transition_(int32_t from, int32_t to) {
  lv_anim_delete(this, transition_exec_);
  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, this);
  lv_anim_set_user_data(&animation, this);
  lv_anim_set_values(&animation, from, to);
  lv_anim_set_duration(&animation, this->transition_duration_);
  lv_anim_set_path_cb(&animation, this->closing_ ? lv_anim_path_ease_in : lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&animation, transition_exec_);
  lv_anim_set_completed_cb(&animation, transition_ready_);
  if (lv_anim_start(&animation) == nullptr) {
    this->set_transition_position_(to);
    this->opening_ = this->close_animating_ = false;
  }
}

void MaterialNotificationCenter::request_(const char *operation, const char *id, int offset) {
  if (this->channel_[0] == '\0' || this->pending_ || this->sequence_ == 0x7FFFFFFF)
    return;
  const auto payload = json::build_json([&](JsonObject object) {
    object["v"] = this->history_scroll_ ? std::max<uint8_t>(2, this->wire_version_) : this->wire_version_;
    object["channel"] = this->channel_.data();
    object["seq"] = ++this->sequence_;
    object["op"] = operation;
    object["offset"] = offset < 0 ? this->offset_ : offset;
    if (id != nullptr)
      object["id"] = id;
  });
  this->pending_ = true;
  this->pending_at_ = millis();
  if (!this->open_) {
    this->request_callbacks_.call(payload);
    return;
  }
  const bool dismiss = id != nullptr && std::strcmp(operation, "dismiss") == 0;
  if (dismiss)
    std::strcpy(this->pending_dismiss_id_.data(), id);
  lv_label_set_text(this->status_, this->text_(2));
  if (this->history_scroll_ || dismiss)
    this->render_();
  if (dismiss)
    this->scene_.discard_prepared_frame();
  this->request_callbacks_.call(payload);
}

void MaterialNotificationCenter::sync(const std::string &payload) {
  if (payload.size() > 8192 || this->is_failed())
    return;
  const bool accepted = json::parse_json(payload, [this](JsonObject object) {
    const int version = object["v"].is<int>() ? object["v"].as<int>() : 0;
    const bool actions =
#ifdef USE_LVGL_MATERIAL_NOTIFICATION_ACTION
        version == 3 && this->detail_action_ != nullptr;
#else
        false;
#endif
    if (object.size() != 8 || (version != 1 && version != 2 && !actions) || !valid_id(object["channel"]) ||
        !object["seq"].is<uint32_t>() || object["seq"].as<uint32_t>() > 0x7FFFFFFF ||
        !object["offset"].is<unsigned>() || object["offset"].as<unsigned>() >= 100 ||
        (version == 1 && object["offset"].as<unsigned>() % 8 != 0) || !object["total"].is<unsigned>() ||
        object["total"].as<unsigned>() > 100 || !object["rows"].is<JsonArray>() ||
        object["rows"].as<JsonArray>().size() > this->rows_.size() || !bounded_text(object["language"], 2))
      return false;
    const char *channel = object["channel"];
    const bool new_channel = std::strcmp(channel, this->channel_.data()) != 0;
    const uint32_t sequence = object["seq"];
    if ((new_channel && sequence != 0) || (!new_channel && sequence < this->sequence_))
      return false;
    auto rows = object["rows"].as<JsonArrayConst>();
    if (rows.size() !=
        std::min<unsigned>(8, object["total"].as<unsigned>() -
                                  std::min(object["total"].as<unsigned>(), object["offset"].as<unsigned>())))
      return false;
    for (size_t index = 0; index < rows.size(); index++) {
      auto row = rows[index];
      if (!row.is<JsonObjectConst>() || row.size() != (version >= 2 ? 4U : 3U) || !valid_id(row["id"]) ||
          !bounded_text(row["title"], 256) || !bounded_text(row["message"], version >= 2 ? 384 : 256) ||
          (version >= 2 && !row["created"].is<uint32_t>()))
        return false;
      for (size_t previous = 0; previous < index; previous++) {
        if (std::strcmp(rows[previous]["id"].as<const char *>(), row["id"].as<const char *>()) == 0)
          return false;
      }
    }
    auto detail = object["detail"];
    const bool has_action = actions && !detail["action"].isNull();
    if (!detail.isNull() &&
        (!detail.is<JsonObject>() || detail.size() != (has_action ? 5U : 4U) || !valid_id(detail["id"]) ||
         !bounded_text(detail["title"], 256) || !bounded_text(detail["message"], 1536)))
      return false;
    auto action = detail["action"];
    if (has_action && (!action.is<JsonObject>() || action.size() != 2 || action["type"] != "camera_preview" ||
                       !action["remaining"].is<unsigned>() || action["remaining"].as<unsigned>() == 0 ||
                       action["remaining"].as<unsigned>() > 180))
      return false;
    bool same_window = !new_channel && detail.isNull() && object["offset"].as<int>() == this->offset_ &&
                       static_cast<int>(rows.size()) == this->count_;
    bool reflow = !new_channel && this->open_ && !this->opening_ && this->history_scroll_ &&
                  this->transition_duration_ != 0 && detail.isNull() && this->detail_id_[0] == '\0' &&
                  this->pending_dismiss_id_[0] != '\0' && !this->contact_ && !this->inertia_;
    std::array<int, 8> old_row_for_new;
    old_row_for_new.fill(-1);
    std::array<int32_t, 8> old_tops{};
    for (size_t index = 0; index < rows.size(); index++) {
      const char *id = rows[index]["id"];
      same_window &= std::strcmp(this->rows_[index].id.data(), id) == 0;
      if (reflow && std::strcmp(this->pending_dismiss_id_.data(), id) == 0)
        reflow = false;
      if (reflow) {
        for (int old = 0; old < this->count_; old++) {
          if (std::strcmp(this->rows_[old].id.data(), id) == 0) {
            old_row_for_new[index] = old;
            break;
          }
        }
      }
    }
    if (!same_window)
      this->stop_reflow_();
    if (reflow) {
      lv_obj_update_layout(this->list_view_);
      for (int index = 0; index < this->count_; index++) {
        lv_area_t area;
        lv_obj_get_coords(this->rows_[index].root, &area);
        old_tops[index] = area.y1;
      }
    }
    if (new_channel) {
      this->touch_cancel();
      this->detail_id_[0] = '\0';
#ifdef USE_LVGL_MATERIAL_NOTIFICATION_ACTION
      this->action_deadline_ = 0;
      this->action_id_[0] = '\0';
#endif
      if (this->history_scroll_)
        this->scroll_y_ = this->desired_scroll_y_ = 0;
    } else if (this->history_scroll_ && this->scroll_y_ > 0 && this->row_pitch_ != 0) {
      const int visible =
          std::clamp<int32_t>(this->scroll_y_ / this->row_pitch_ - this->offset_, 0, std::max(0, this->count_ - 1));
      if (this->count_ > 0) {
        for (size_t index = 0; index < rows.size(); index++) {
          if (std::strcmp(this->rows_[visible].id.data(), rows[index]["id"].as<const char *>()) == 0) {
            const int delta =
                (object["offset"].as<int>() + static_cast<int>(index) - this->offset_ - visible) * this->row_pitch_;
            this->scroll_y_ += delta;
            this->desired_scroll_y_ += delta;
            this->touch_scroll_origin_ += delta;
            break;
          }
        }
      }
    }
    std::strcpy(this->channel_.data(), channel);
    this->sequence_ = sequence;
    this->wire_version_ = version;
    this->pending_ = false;
    this->pending_dismiss_id_[0] = '\0';
    this->offset_ = object["offset"];
    this->total_ = object["total"];
    if (this->target_ >= 0 &&
        (static_cast<size_t>(this->target_) >= rows.size() ||
         std::strcmp(this->captured_id_.data(), rows[this->target_]["id"].as<const char *>()) != 0))
      this->touch_cancel();
    this->count_ = rows.size();
    for (int index = 0; index < this->count_; index++) {
      auto row = rows[index];
      std::strcpy(this->rows_[index].id.data(), row["id"].as<const char *>());
      lv_label_set_text(this->rows_[index].title, row["title"]);
      lv_label_set_text(this->rows_[index].message, row["message"]);
      this->rows_[index].created = version >= 2 ? row["created"].as<uint32_t>() : 0;
    }
    if (!detail.isNull() && this->open_) {
      std::strcpy(this->detail_id_.data(), detail["id"].as<const char *>());
#ifdef USE_LVGL_MATERIAL_NOTIFICATION_ACTION
      this->action_contact_id_[0] = '\0';
      this->action_deadline_ = has_action ? millis() + 1000U * action["remaining"].as<unsigned>() : 0;
#endif
      lv_label_set_text(this->heading_, detail["title"]);
      lv_label_set_text(this->detail_, detail["message"]);
      lv_obj_scroll_to_y(lv_obj_get_parent(this->detail_), 0, LV_ANIM_OFF);
    }
    if (this->history_scroll_)
      this->set_scroll_y_(this->desired_scroll_y_);
    this->render_();
    if (reflow)
      this->start_reflow_(old_row_for_new, old_tops);
    return true;
  });
  if (accepted)
    this->request_scroll_window_();
}

std::string MaterialNotificationCenter::status_json() const {
  return json::build_json([this](JsonObject object) {
    object["version"] = 1;
    object["open"] = this->open_;
    object["closing"] = this->closing_;
    object["pending"] = this->pending_;
    object["banner_active"] = this->banner_->is_active();
    object["offset"] = this->offset_;
    object["total"] = this->total_;
    object["rows"] = this->count_;
    object["detail_id"] = this->detail_id_.data();
#ifdef USE_LVGL_MATERIAL_NOTIFICATION_ACTION
    object["detail_action"] = this->open_ && this->detail_action_available_();
#endif
    object["raw_bytes"] = this->scene_.raw_bytes();
    object["background_bands"] = this->scene_.band_count();
    object["backdrop_restore_bytes"] = this->scene_.restore_bytes();
    object["opening"] = this->opening_;
    object["history_scroll"] = this->history_scroll_;
    object["scroll_y"] = this->scroll_y_;
    object["inertia"] = this->inertia_;
    object["native_transition"] = this->native_transition_;
#ifdef USE_LVGL_MATERIAL_NOTIFICATION_ROW_CACHE
    object["row_cache_bytes"] = this->row_cache_.bytes();
    object["row_cache_bands"] = this->row_cache_.band_count();
    object["row_cache_max_bands"] = this->row_cache_.max_band_count();
    object["row_cache_enabled"] = this->row_cache_enabled_;
    object["row_cache_ready"] = this->row_cache_.ready_count();
    object["row_cache_captures"] = this->row_cache_.captures();
#endif
  });
}

#ifdef USE_LVGL_MATERIAL_NOTIFICATION_ACTION
bool MaterialNotificationCenter::detail_action_available_() const {
  return this->detail_action_ != nullptr && this->detail_id_[0] != '\0' && this->action_deadline_ != 0 &&
         static_cast<int32_t>(millis() - this->action_deadline_) < 0;
}

void MaterialNotificationCenter::render_detail_action_() {
  if (this->detail_action_ == nullptr)
    return;
  if (!this->detail_action_available_()) {
    lv_obj_add_flag(this->detail_action_, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  const bool home = this->navigation_->get_active_application() == nullptr;
  lv_label_set_text(this->detail_action_, this->text_(home ? 9 : 10));
  lv_obj_set_style_opa(this->detail_action_, home ? LV_OPA_COVER : LV_OPA_60, LV_PART_MAIN);
  lv_obj_remove_flag(this->detail_action_, LV_OBJ_FLAG_HIDDEN);
}
#endif

void MaterialNotificationCenter::render_() {
  const bool detail = this->detail_id_[0] != '\0';
  auto *detail_view = this->detail_view_ != nullptr ? this->detail_view_ : this->detail_;
  if (this->close_icon_ != nullptr)
    lv_label_set_text(this->close_icon_, detail ? this->back_glyph_.c_str() : this->close_glyph_.c_str());
  if (!detail)
    lv_label_set_text(this->heading_, this->text_(0));
  if (detail)
    lv_obj_remove_flag(detail_view, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(detail_view, LV_OBJ_FLAG_HIDDEN);
#ifdef USE_LVGL_MATERIAL_NOTIFICATION_ACTION
  this->render_detail_action_();
#endif
  const bool loading_top = this->pending_ && this->scroll_y_ == 0 && this->offset_ > 0;
  if (this->list_view_ != nullptr) {
    if (detail || this->count_ == 0 || loading_top)
      lv_obj_add_flag(this->list_view_, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_remove_flag(this->list_view_, LV_OBJ_FLAG_HIDDEN);
  }
  if (this->empty_view_ != nullptr) {
    if (!detail && (this->total_ == 0 || this->count_ == 0 || loading_top))
      lv_obj_remove_flag(this->empty_view_, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(this->empty_view_, LV_OBJ_FLAG_HIDDEN);
  }
  if (this->empty_heading_ != nullptr)
    lv_label_set_text(this->empty_heading_, this->text_(0));
  for (size_t index = 0; index < this->rows_.size(); index++) {
    if (!detail && static_cast<int>(index) < this->count_ && this->rows_[index].id != this->pending_dismiss_id_)
      lv_obj_remove_flag(this->rows_[index].root, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(this->rows_[index].root, LV_OBJ_FLAG_HIDDEN);
  }
  for (auto *button : {this->previous_, this->next_}) {
    if (button == nullptr)
      continue;
    if (detail)
      lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_remove_flag(button, LV_OBJ_FLAG_HIDDEN);
  }
  if (this->previous_ != nullptr) {
    if (this->offset_ == 0)
      lv_obj_add_state(this->previous_, LV_STATE_DISABLED);
    else
      lv_obj_remove_state(this->previous_, LV_STATE_DISABLED);
  }
  if (this->next_ != nullptr) {
    if (this->offset_ + this->count_ >= this->total_)
      lv_obj_add_state(this->next_, LV_STATE_DISABLED);
    else
      lv_obj_remove_state(this->next_, LV_STATE_DISABLED);
  }
  if (detail)
    lv_label_set_text(this->status_, "");
  else if (this->history_scroll_ && this->pending_)
    lv_label_set_text(this->status_, this->text_(2));
  else if (this->total_ == 0)
    lv_label_set_text(this->status_, this->text_(1));
  else if (this->history_scroll_)
    lv_label_set_text(this->status_, "");
  else
    lv_label_set_text_fmt(this->status_, "%d–%d / %d", this->offset_ + 1, this->offset_ + this->count_, this->total_);
  this->update_row_positions_();
  this->update_ages_();
  this->update_row_cache_(0);
}

const char *MaterialNotificationCenter::text_(uint8_t index) const {
  if (this->text_provider_) {
    const char *text = this->text_provider_(index);
    if (text != nullptr)
      return text;
  }
  static constexpr const char *DEFAULTS[] = {"Notifications",
                                             "No notifications",
                                             "Loading...",
                                             "HA did not respond. Try again.",
                                             "Notification",
                                             "now",
                                             "min",
                                             "h",
                                             "d"};
  return index < 9 ? DEFAULTS[index] : "";
}

void MaterialNotificationCenter::update_row_positions_() {
  if (!this->history_scroll_)
    return;
  // Rows keep their original local layout. One native scroll translates their
  // coordinates without invalidating the style/layout of every row per tick.
  const int32_t local = std::max<int32_t>(0, this->scroll_y_ - this->offset_ * this->row_pitch_);
  lv_obj_scroll_to_y(this->list_view_, local, LV_ANIM_OFF);
  this->update_row_cache_(0);
}

void MaterialNotificationCenter::start_reflow_(const std::array<int, 8> &old_row_for_new,
                                               const std::array<int32_t, 8> &old_tops) {
  lv_obj_update_layout(this->list_view_);
  std::array<int32_t, 8> offsets{};
  int32_t previous_bottom = INT32_MIN;
  int32_t offset = 0;
  bool moves = false;
  for (int index = 0; index < this->count_; index++) {
    lv_area_t area;
    lv_obj_get_coords(this->rows_[index].root, &area);
    const int old_row = old_row_for_new[index];
    // A replacement at the end of the window follows the preceding card.
    // Keep the packed compositor's card spans disjoint even across a rebind.
    if (old_row >= 0)
      offset = std::max<int32_t>(0, old_tops[old_row] - area.y1);
    if (area.y1 + offset <= previous_bottom)
      return;
    previous_bottom = area.y2 + offset;
    offsets[index] = offset;
    moves |= offset != 0;
  }
  if (!moves)
    return;
  this->reflow_offsets_ = offsets;
  this->scene_.discard_prepared_frame();
  reflow_exec_(this, 256);
  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, this);
  lv_anim_set_values(&animation, 256, 0);
  lv_anim_set_duration(&animation, std::min<uint32_t>(180, this->transition_duration_));
  lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
  lv_anim_set_exec_cb(&animation, reflow_exec_);
  if (lv_anim_start(&animation) == nullptr)
    reflow_exec_(this, 0);
}

void MaterialNotificationCenter::stop_reflow_() {
  if (lv_anim_delete(this, reflow_exec_)) {
    reflow_exec_(this, 0);
    this->scene_.discard_prepared_frame();
  }
}

void MaterialNotificationCenter::reflow_exec_(void *object, int32_t progress) {
  auto *center = static_cast<MaterialNotificationCenter *>(object);
  for (size_t index = 0; index < center->rows_.size(); index++) {
    auto &offset = center->reflow_offsets_[index];
    if (offset != 0) {
      lv_obj_set_style_translate_y(center->rows_[index].root, offset * progress / 256, LV_PART_MAIN);
      if (progress == 0)
        offset = 0;
    }
  }
}

void MaterialNotificationCenter::update_row_cache_(size_t prepare_limit) {
#ifdef USE_LVGL_MATERIAL_NOTIFICATION_ROW_CACHE
  if (!this->row_cache_enabled_)
    return;
  if (!this->open_ || !this->history_scroll_ || this->detail_id_[0] != '\0') {
    this->row_cache_.clear();
    return;
  }
  for (size_t index = 0; index < this->rows_.size(); index++) {
    const auto &row = this->rows_[index];
    uint32_t hash = 2166136261U;
    for (auto *label : {row.title, row.message, row.source, row.age}) {
      if (label != nullptr) {
        for (const char *text = lv_label_get_text(label); *text != '\0'; text++)
          hash = (hash ^ static_cast<uint8_t>(*text)) * 16777619U;
      }
      hash = (hash ^ 0xFFU) * 16777619U;
    }
    this->row_cache_.set_key(index, row.id.data(), hash);
  }
  this->row_cache_.update(this->list_view_, this->count_, prepare_limit);
#else
  (void) prepare_limit;
#endif
}

void MaterialNotificationCenter::update_ages_() {
  const uint32_t now = this->now_provider_ ? this->now_provider_() : 0;
  for (int index = 0; index < this->count_; index++) {
    auto &row = this->rows_[index];
    if (row.source != nullptr && std::strcmp(lv_label_get_text(row.source), this->text_(4)) != 0)
      lv_label_set_text(row.source, this->text_(4));
    if (row.age == nullptr)
      continue;
    char text[32]{};
    if (now != 0 && row.created != 0 && now >= row.created) {
      const uint32_t age = now - row.created;
      if (age < 60)
        std::snprintf(text, sizeof(text), "%s", this->text_(5));
      else if (age < 3600)
        std::snprintf(text, sizeof(text), "%u %s", static_cast<unsigned>(age / 60), this->text_(6));
      else if (age < 86400)
        std::snprintf(text, sizeof(text), "%u %s", static_cast<unsigned>(age / 3600), this->text_(7));
      else
        std::snprintf(text, sizeof(text), "%u %s", static_cast<unsigned>(age / 86400), this->text_(8));
    }
    if (std::strcmp(lv_label_get_text(row.age), text) != 0)
      lv_label_set_text(row.age, text);
  }
}

void MaterialNotificationCenter::set_scroll_y_(int32_t value) {
  if (!this->history_scroll_ || this->list_view_ == nullptr || this->row_pitch_ == 0)
    return;
  const int32_t height = lv_obj_get_height(this->list_view_);
  const int32_t card_height = lv_obj_get_height(this->rows_[0].root);
  const int32_t end = std::max<int32_t>(0, (this->total_ - 1) * this->row_pitch_ + card_height - height);
  this->desired_scroll_y_ = std::clamp(value, int32_t{0}, end);
  if (this->desired_scroll_y_ != this->scroll_y_)
    this->scroll_direction_ = this->desired_scroll_y_ > this->scroll_y_ ? 1 : -1;
  const int32_t first = this->count_ > 0 ? this->offset_ * this->row_pitch_ : 0;
  const int32_t last =
      this->count_ > 0 ? std::max(first, (this->offset_ + this->count_ - 1) * this->row_pitch_ + card_height - height)
                       : 0;
  this->scroll_y_ = std::clamp(this->desired_scroll_y_, first, last);
  this->update_row_positions_();
}

void MaterialNotificationCenter::request_scroll_window_() {
  if (!this->history_scroll_ || !this->open_ || this->pending_ || this->detail_id_[0] != '\0' ||
      this->list_view_ == nullptr || this->row_pitch_ == 0 || this->total_ <= 8 || this->count_ == 0)
    return;
  const int first = this->scroll_y_ / this->row_pitch_;
  const int visible = (lv_obj_get_height(this->list_view_) + this->row_pitch_ - 1) / this->row_pitch_ + 1;
  int next = this->offset_;
  if (this->scroll_direction_ > 0 && first + visible >= this->offset_ + this->count_ - 1)
    next = std::clamp(first, 0, this->total_ - 8);
  else if (this->scroll_direction_ < 0 && first <= this->offset_ + 1)
    next = std::clamp(first - 4, 0, this->total_ - 8);
  if (next != this->offset_)
    this->request_("page", nullptr, next);
}

void MaterialNotificationCenter::refresh_texts() {
  if (this->root_ == nullptr || this->is_failed())
    return;
  this->render_();
  if (this->pending_)
    lv_label_set_text(this->status_, this->text_(2));
}

bool MaterialNotificationCenter::contains_(lv_obj_t *object, int32_t x, int32_t y) {
  if (object == nullptr || lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN))
    return false;
  lv_area_t area;
  lv_obj_get_coords(object, &area);
  return x >= area.x1 && x <= area.x2 && y >= area.y1 && y <= area.y2;
}

bool MaterialNotificationCenter::touch_begin(int32_t x, int32_t y) {
  this->touch_cancel();
  if (this->closing_ || this->opening_)
    return true;
  if (this->is_failed())
    return false;
  this->stop_reflow_();
  const bool at_bottom = this->close_edge_pixels_ != 0 && y >= this->parent_->get_height() - this->close_edge_pixels_;
  if (!this->open_ && at_bottom)
    this->banner_contact_generation_ = this->banner_->dismissible_generation();
  this->edge_contact_ = !this->open_ && this->banner_contact_generation_ == 0;
  if (this->edge_contact_ && (y > this->edge_pixels_ || (this->admission_ && !this->admission_()) ||
                              this->navigation_->is_transition_active()))
    return false;
  this->contact_ = true;
  this->start_x_ = this->x_ = x;
  this->start_y_ = this->y_ = y;
  this->bottom_contact_ = at_bottom;
  this->bottom_completed_ = false;
  this->touch_scroll_origin_ = this->scroll_y_;
  this->scroll_sample_at_ = millis();
  this->target_ = -1;
#ifdef USE_LVGL_MATERIAL_NOTIFICATION_ACTION
  if (this->open_ && !this->bottom_contact_ && this->detail_action_available_() &&
      contains_(this->detail_action_, x, y))
    this->action_contact_id_ = this->detail_id_;
#endif
  if (this->open_ && !this->bottom_contact_ && this->detail_id_[0] == '\0' &&
      (!this->history_scroll_ || contains_(this->list_view_, x, y))) {
    for (int index = 0; index < this->count_; index++) {
      if (contains_(this->rows_[index].root, x, y)) {
        this->target_ = index;
        this->captured_id_ = this->rows_[index].id;
        break;
      }
    }
  }
  return true;
}

bool MaterialNotificationCenter::touch_update(int32_t x, int32_t y) {
  if (!this->contact_)
    return false;
  const int32_t last_y = this->y_;
  this->x_ = x;
  this->y_ = y;
  const int dx = x - this->start_x_;
  const int dy = y - this->start_y_;
  this->dragged_ |= std::abs(dx) > 8 || std::abs(dy) > 8;
  if (this->edge_contact_) {
    if (dy >= this->commit_pixels_ && dy > std::abs(dx) && this->open())
      this->target_ = -1;
    return this->dragged_;
  }
  if (this->bottom_contact_) {
    const bool consumed = this->dragged_;
    if (!this->bottom_completed_ && -dy >= this->close_commit_pixels_ && -dy > std::abs(dx)) {
      this->bottom_completed_ = true;
      if (this->banner_contact_generation_ != 0) {
        if (this->banner_->dismissible_generation() == this->banner_contact_generation_)
          this->banner_->dismiss();
      } else if (this->detail_id_[0] != '\0') {
        this->detail_id_[0] = '\0';
        this->render_();
      } else {
        this->close();
      }
    }
    return consumed;
  }
  if (this->history_scroll_ && this->detail_id_[0] == '\0') {
    if (this->dragged_ && !this->row_swipe_ && !this->scroll_drag_) {
      this->scroll_drag_ = std::abs(dy) > std::abs(dx) + 4;
      this->row_swipe_ = std::abs(dx) > std::abs(dy) + 4;
      if (this->scroll_drag_)
        this->target_ = -1;
    }
    if (this->scroll_drag_) {
      const uint32_t now = millis();
      const uint32_t elapsed = std::max<uint32_t>(1, now - this->scroll_sample_at_);
      const float velocity = std::clamp(static_cast<float>(last_y - y) / elapsed, -3.0f, 3.0f);
      this->scroll_velocity_ = this->scroll_velocity_ * 0.5f + velocity * 0.5f;
      this->scroll_sample_at_ = now;
      this->set_scroll_y_(this->touch_scroll_origin_ + this->start_y_ - y);
      this->request_scroll_window_();
    } else if (this->row_swipe_ && this->target_ >= 0 && this->rows_[this->target_].id == this->captured_id_) {
      lv_obj_set_style_translate_x(this->rows_[this->target_].root, dx, LV_PART_MAIN);
    }
    return this->dragged_;
  }
  if (this->target_ >= 0 && std::abs(dx) > std::abs(dy) && this->rows_[this->target_].id == this->captured_id_) {
    lv_obj_set_style_translate_x(this->rows_[this->target_].root, dx, LV_PART_MAIN);
  } else if (this->detail_id_[0] != '\0' && this->dragged_) {
    lv_obj_scroll_by(lv_obj_get_parent(this->detail_), 0, y - this->start_y_, LV_ANIM_OFF);
    this->start_y_ = y;
  }
  return this->dragged_;
}

bool MaterialNotificationCenter::touch_end() {
  if (!this->contact_)
    return true;  // A channel change canceled the contact; never replay it underneath.
  const bool edge = this->edge_contact_;
  const bool dragged = this->dragged_;
  if (this->bottom_contact_) {
    this->touch_cancel();
    return true;
  }
  if (this->history_scroll_ && this->scroll_drag_) {
    const float velocity = millis() - this->scroll_sample_at_ <= 120 ? this->scroll_velocity_ : 0.0f;
    this->touch_cancel();
    if (std::abs(velocity) >= 0.1f) {
      this->inertia_ = true;
      this->scroll_velocity_ = velocity;
      this->inertia_tick_ = millis();
    }
    return true;
  }
  const int dx = this->x_ - this->start_x_;
  const int dy = this->y_ - this->start_y_;
  if (!edge && this->target_ >= 0 && this->rows_[this->target_].id == this->captured_id_ && !this->pending_) {
    const int dismiss_pixels = this->dismiss_commit_pixels_ != 0 ? this->dismiss_commit_pixels_ : this->commit_pixels_;
    if (std::abs(dx) >= dismiss_pixels && std::abs(dx) > std::abs(dy) && (!this->history_scroll_ || this->row_swipe_))
      this->request_("dismiss", this->captured_id_.data());
    else if (!dragged && contains_(this->rows_[this->target_].root, this->x_, this->y_))
      this->request_("open", this->captured_id_.data());
  } else if (!edge && !dragged) {
    // Both ends must stay inside the same control.
    auto clicked = [this](lv_obj_t *button) {
      return contains_(button, this->start_x_, this->start_y_) && contains_(button, this->x_, this->y_);
    };
#ifdef USE_LVGL_MATERIAL_NOTIFICATION_ACTION
    if (this->open_ && !this->pending_ && this->detail_action_available_() &&
        this->navigation_->get_active_application() == nullptr && this->action_contact_id_ == this->detail_id_ &&
        clicked(this->detail_action_)) {
      this->action_id_ = this->detail_id_;
      this->close();
      return true;
    }
#endif
    if (clicked(this->close_)) {
      if (this->detail_id_[0] != '\0') {
        this->detail_id_[0] = '\0';
        this->render_();
      } else {
        this->close();
      }
    } else if (!this->pending_ && clicked(this->previous_) && this->offset_ > 0) {
      this->request_("page", nullptr, std::max(0, this->offset_ - 8));
    } else if (!this->pending_ && clicked(this->next_) && this->offset_ + 8 < this->total_) {
      this->request_("page", nullptr, this->offset_ + 8);
    }
  }
  this->touch_cancel();
  return !edge || dragged;
}

void MaterialNotificationCenter::touch_cancel() {
#ifdef USE_LVGL_MATERIAL_NOTIFICATION_ACTION
  this->action_contact_id_[0] = '\0';
#endif
  if (this->target_ >= 0)
    lv_obj_set_style_translate_x(this->rows_[this->target_].root, 0, LV_PART_MAIN);
  this->target_ = -1;
  this->contact_ = false;
  this->dragged_ = false;
  this->bottom_contact_ = this->bottom_completed_ = false;
  this->banner_contact_generation_ = 0;
  this->scroll_drag_ = this->row_swipe_ = this->inertia_ = false;
  this->scroll_velocity_ = 0;
}

}  // namespace esphome::lvgl_material
#endif
