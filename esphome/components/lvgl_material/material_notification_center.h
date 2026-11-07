#pragma once
#include "esphome/core/defines.h"

#ifdef USE_LVGL_MATERIAL_NOTIFICATION_CENTER
#include "material_notification.h"
#include "material_modal_scene.h"
#include "material_notification_row_cache.h"
#include "esphome/components/lvgl/lvgl_navigation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include <array>

namespace esphome::lvgl_material {

class MaterialNotificationCenter : public Component,
                                   public lvgl::NavigationTouchDelegate,
                                   public lvgl::NavigationAdmissionGuard {
 public:
  MaterialNotificationCenter(lvgl::LvglComponent *parent, lvgl::LvglNavigation *navigation,
                             MaterialNotificationOverlay *banner)
      : parent_(parent), navigation_(navigation), banner_(banner), scene_(parent) {}
  void set_root(lv_obj_t *root) { this->root_ = root; }
  void set_heading(lv_obj_t *heading) { this->heading_ = heading; }
  void set_status(lv_obj_t *status) { this->status_ = status; }
  void set_detail(lv_obj_t *detail) { this->detail_ = detail; }
  void set_detail_view(lv_obj_t *view) { this->detail_view_ = view; }
#ifdef USE_LVGL_MATERIAL_NOTIFICATION_ACTION
  void set_detail_action(lv_obj_t *label) { this->detail_action_ = label; }
#endif
  void set_list_view(lv_obj_t *view) { this->list_view_ = view; }
  void set_empty_view(lv_obj_t *view) { this->empty_view_ = view; }
  void set_empty_heading(lv_obj_t *label) { this->empty_heading_ = label; }
  void set_history_scroll(bool enabled) { this->history_scroll_ = enabled; }
  void set_isolate_native_layers(bool enabled) { this->scene_.set_isolate_native_layers(enabled); }
  void set_cached_backdrop_dim(bool enabled) { this->cached_backdrop_dim_ = enabled; }
  void set_direct_presentation(bool enabled) { this->scene_.set_direct_presentation(enabled); }
  void set_backdrop_dim_shift(uint8_t shift) { this->scene_.set_backdrop_dim_shift(shift); }
  void set_frame_profile(bool enabled) { this->scene_.set_profile(enabled); }
#ifdef USE_LVGL_MATERIAL_NOTIFICATION_ROW_CACHE
  void set_row_snapshot_cache(bool enabled) {
    this->row_cache_enabled_ = enabled;
    if (!enabled)
      this->row_cache_.clear();
  }
  void set_row_cache_band_count(uint8_t count) { this->row_cache_.set_band_count(count); }
  void set_row_cache_min_free_bytes(size_t bytes) { this->row_cache_.set_min_free_bytes(bytes); }
#endif
  void set_row_pitch(uint16_t pixels) { this->row_pitch_ = pixels; }
  void set_close_edge_pixels(uint16_t pixels) { this->close_edge_pixels_ = pixels; }
  void set_close_commit_pixels(uint16_t pixels) { this->close_commit_pixels_ = pixels; }
  void set_transition_duration(uint32_t duration) { this->transition_duration_ = duration; }
  template<typename F> void set_now_provider(F &&provider) { this->now_provider_ = std::forward<F>(provider); }
  void set_close_button(lv_obj_t *button) { this->close_ = button; }
  void set_close_icon(lv_obj_t *icon) { this->close_icon_ = icon; }
  void set_close_glyph(const std::string &glyph) { this->close_glyph_ = glyph; }
  void set_back_glyph(const std::string &glyph) { this->back_glyph_ = glyph; }
  template<typename F> void set_text_provider(F &&provider) { this->text_provider_ = std::forward<F>(provider); }
  void refresh_texts();
  void set_previous_button(lv_obj_t *button) { this->previous_ = button; }
  void set_next_button(lv_obj_t *button) { this->next_ = button; }
  void set_row(size_t index, lv_obj_t *root, lv_obj_t *title, lv_obj_t *message);
  void set_row_metadata(size_t index, lv_obj_t *source, lv_obj_t *age);
  void set_edge_pixels(uint16_t pixels) { this->edge_pixels_ = pixels; }
  void set_commit_pixels(uint16_t pixels) { this->commit_pixels_ = pixels; }
  void set_dismiss_commit_pixels(uint16_t pixels) { this->dismiss_commit_pixels_ = pixels; }
  void add_scene_controller(lvgl::DirectSceneController *controller) { this->scene_.add_scene_controller(controller); }
  template<typename F> void set_admission(F &&callback) { this->admission_ = std::forward<F>(callback); }
  template<typename F> void add_on_request_callback(F &&callback) {
    this->request_callbacks_.add(std::forward<F>(callback));
  }
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR - 6.0f; }
  void sync(const std::string &payload);
  std::string status_json() const;
  bool open();
  void close();
  bool is_open() const { return this->open_; }
  bool owns_scene() const { return this->scene_.active(); }
  bool prepare_navigation(lvgl::NavigationAdmissionKind, lvgl::LvglApplication *) override {
    return !this->scene_.active();
  }
  bool touch_begin(int32_t x, int32_t y) override;
  bool touch_update(int32_t x, int32_t y) override;
  bool touch_end() override;
  void touch_cancel() override;

 protected:
  struct Row {
    lv_obj_t *root{};
    lv_obj_t *title{};
    lv_obj_t *message{};
    lv_obj_t *source{};
    lv_obj_t *age{};
    uint32_t created{};
    std::array<char, 33> id{};
  };
  static bool contains_(lv_obj_t *object, int32_t x, int32_t y);
  void request_(const char *operation, const char *id = nullptr, int offset = -1);
  void render_();
  void update_row_positions_();
  void update_row_cache_(size_t prepare_limit);
  void start_reflow_(const std::array<int, 8> &old_row_for_new, const std::array<int32_t, 8> &old_tops);
  void stop_reflow_();
  static void reflow_exec_(void *object, int32_t progress);
  void set_transition_position_(int32_t y);
  int32_t transition_position_() const;
  static void transition_size_cb_(lv_event_t *event);
  void update_ages_();
  void set_scroll_y_(int32_t value);
  void request_scroll_window_();
  void finish_close_();
#ifdef USE_LVGL_MATERIAL_NOTIFICATION_ACTION
  bool detail_action_available_() const;
  void render_detail_action_();
#endif
  void animate_transition_(int32_t from, int32_t to);
  static void transition_exec_(void *object, int32_t value);
  static void transition_ready_(lv_anim_t *animation);
  const char *text_(uint8_t index) const;
  lvgl::LvglComponent *parent_;
  lvgl::LvglNavigation *navigation_;
  MaterialNotificationOverlay *banner_;
  MaterialModalScene scene_;
#ifdef USE_LVGL_MATERIAL_NOTIFICATION_ROW_CACHE
  MaterialNotificationRowCache row_cache_;
  bool row_cache_enabled_{};
#endif
  lv_obj_t *root_{};
  lv_obj_t *heading_{};
  lv_obj_t *status_{};
  lv_obj_t *detail_{};
  lv_obj_t *detail_view_{};
#ifdef USE_LVGL_MATERIAL_NOTIFICATION_ACTION
  lv_obj_t *detail_action_{};
  std::array<char, 33> action_id_{};
  std::array<char, 33> action_contact_id_{};
  uint32_t action_deadline_{};
#endif
  lv_obj_t *list_view_{};
  lv_obj_t *empty_view_{};
  lv_obj_t *empty_heading_{};
  lv_obj_t *close_{};
  lv_obj_t *close_icon_{};
  lv_obj_t *previous_{};
  lv_obj_t *next_{};
  std::array<Row, 8> rows_{};
  std::array<char, 33> channel_{};
  std::array<char, 33> captured_id_{};
  std::array<char, 33> detail_id_{};
  std::array<char, 33> pending_dismiss_id_{};
  std::array<int32_t, 8> reflow_offsets_{};
  LazyCallbackManager<void(std::string)> request_callbacks_{};
  std::function<bool()> admission_{};
  std::function<const char *(uint8_t)> text_provider_{};
  std::function<uint32_t()> now_provider_{};
  std::string close_glyph_{};
  std::string back_glyph_{};
  uint32_t sequence_{};
  uint32_t pending_at_{};
  uint32_t banner_contact_generation_{};
  uint16_t edge_pixels_{32};
  uint16_t commit_pixels_{60};
  uint16_t dismiss_commit_pixels_{};
  uint16_t row_pitch_{};
  uint16_t close_edge_pixels_{};
  uint16_t close_commit_pixels_{36};
  uint32_t transition_duration_{};
  uint32_t scroll_sample_at_{};
  uint32_t inertia_tick_{};
  uint32_t age_update_at_{};
  int32_t scroll_y_{};
  int32_t origin_y_{};
  int32_t origin_scroll_y_{};
  bool native_transition_{};
  int32_t desired_scroll_y_{};
  int32_t touch_scroll_origin_{};
  float scroll_velocity_{};
  uint8_t wire_version_{1};
  int8_t scroll_direction_{};
  int offset_{};
  int total_{};
  int count_{};
  int target_{-1};
  int32_t start_x_{};
  int32_t start_y_{};
  int32_t x_{};
  int32_t y_{};
  bool open_{};
  bool closing_{};
  bool contact_{};
  bool edge_contact_{};
  bool dragged_{};
  bool pending_{};
  bool history_scroll_{};
  bool cached_backdrop_dim_{};
  bool bottom_contact_{};
  bool bottom_completed_{};
  bool scroll_drag_{};
  bool row_swipe_{};
  bool inertia_{};
  bool opening_{};
  bool close_animating_{};
};

template<typename... Ts> class MaterialNotificationSyncAction : public Action<Ts...> {
 public:
  explicit MaterialNotificationSyncAction(MaterialNotificationCenter *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(std::string, payload)
  void play(const Ts &...x) override { this->parent_->sync(this->payload_.value(x...)); }

 protected:
  MaterialNotificationCenter *parent_;
};

}  // namespace esphome::lvgl_material
#endif
