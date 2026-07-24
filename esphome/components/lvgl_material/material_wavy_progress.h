#pragma once

#include "esphome/core/component.h"

#include "lvgl.h"

namespace esphome::lvgl_material {

class MaterialWavyProgress : public Component {
 public:
  void set_widget(lv_obj_t *widget) { this->widget_ = widget; }

  void setup() override;
  void on_shutdown() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR - 5.0f; }

  void initialize();
  void set_background(const lv_image_dsc_t *source, bool apply_scrim);
  void log_state(const char *reason);
  void set_value_basis_points(int progress);
  void set_value_permille(int progress);
  void set_value(int progress);
  void set_playing(bool playing);
  void set_direct_present(bool enabled);
  bool is_background_ready();
  bool service_present();
  void set_pending(bool pending);
  void tick(float degrees);

 protected:
  lv_obj_t *widget_{nullptr};
};

}  // namespace esphome::lvgl_material
