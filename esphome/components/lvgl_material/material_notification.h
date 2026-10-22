#pragma once

#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"

#include <array>
#include <cstdint>
#include <string>

namespace esphome::lvgl_material {

class MaterialNotificationOverlay : public Component {
 public:
  explicit MaterialNotificationOverlay(lvgl::LvglComponent *component) : lvgl_component_(component) {}

  void set_root(lv_obj_t *root) { this->root_ = root; }
  void set_panel(lv_obj_t *panel) { this->panel_ = panel; }
  void set_icon(lv_obj_t *icon) { this->icon_ = icon; }
  void set_title(lv_obj_t *title) { this->title_ = title; }
  void set_message(lv_obj_t *message) { this->message_ = message; }
  void set_scrim_opacity(lv_opa_t opacity) { this->scrim_opacity_ = opacity; }
  void set_default_duration(uint32_t duration) { this->default_duration_ = duration; }

  void setup() override;
  void loop() override;
  void on_shutdown() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR - 5.0f; }

  void show(const std::string &title, const std::string &message, const std::string &icon, uint32_t duration = 0);
  void dismiss();

 protected:
  static constexpr size_t QUEUE_CAPACITY = 4;
  static constexpr size_t TITLE_CAPACITY = 64;
  static constexpr size_t MESSAGE_CAPACITY = 256;
  static constexpr size_t ICON_CAPACITY = 16;

  struct Notification {
    std::array<char, TITLE_CAPACITY> title{};
    std::array<char, MESSAGE_CAPACITY> message{};
    std::array<char, ICON_CAPACITY> icon{};
    uint32_t duration{0};
  };

  static void panel_translate_animation_cb_(void *object, int32_t value);
  static void scrim_animation_cb_(void *object, int32_t value);
  static void close_animation_ready_cb_(lv_anim_t *animation);
  static void root_event_cb_(lv_event_t *event);
  static void copy_text_(char *destination, size_t capacity, const std::string &source);
  void display_next_();
  void finish_close_();
  void animate_panel_(int32_t from, int32_t to, uint32_t duration, bool closing);
  void animate_scrim_(lv_opa_t from, lv_opa_t to, uint32_t duration);

  lv_obj_t *root_{nullptr};
  lv_obj_t *panel_{nullptr};
  lv_obj_t *icon_{nullptr};
  lv_obj_t *title_{nullptr};
  lv_obj_t *message_{nullptr};
  lvgl::LvglComponent *lvgl_component_{nullptr};
  std::array<Notification, QUEUE_CAPACITY> queue_{};
  uint8_t queue_head_{0};
  uint8_t queue_size_{0};
  lv_opa_t scrim_opacity_{LV_OPA_70};
  uint32_t default_duration_{6000};
  uint32_t deadline_{0};
  uint32_t close_deadline_{0};
  int32_t panel_hidden_translate_y_{0};
  bool active_{false};
  bool closing_{false};
  bool close_animation_ready_{false};
  bool direct_regions_paused_{false};
};

template<typename... Ts> class MaterialNotificationShowAction : public Action<Ts...> {
 public:
  explicit MaterialNotificationShowAction(MaterialNotificationOverlay *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(std::string, title)
  TEMPLATABLE_VALUE(std::string, message)
  TEMPLATABLE_VALUE(std::string, icon)
  TEMPLATABLE_VALUE(uint32_t, duration)

 protected:
  void play(const Ts &...x) override {
    this->parent_->show(this->title_.value(x...), this->message_.value(x...), this->icon_.value(x...),
                        this->duration_.value(x...));
  }
  MaterialNotificationOverlay *parent_;
};

template<typename... Ts> class MaterialNotificationDismissAction : public Action<Ts...> {
 public:
  explicit MaterialNotificationDismissAction(MaterialNotificationOverlay *parent) : parent_(parent) {}

 protected:
  void play(const Ts &...x) override { this->parent_->dismiss(); }
  MaterialNotificationOverlay *parent_;
};

}  // namespace esphome::lvgl_material
