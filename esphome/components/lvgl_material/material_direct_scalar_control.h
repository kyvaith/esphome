#pragma once

#include "esphome/core/defines.h"
#ifdef USE_LVGL_MATERIAL_DIRECT_SCALAR_CONTROL

#include "material_entity_control_component.h"
#include "material_scalar_raster.h"
#ifdef USE_LVGL_MATERIAL_SCALAR_PERFORMANCE_METRICS
#include "material_scalar_performance.h"
#endif
#include "esphome/components/lvgl/direct_scene_controller.h"
#include "esphome/components/lvgl/lvgl_navigation.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

#ifdef USE_ESP32
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace esphome::lvgl_material {

class MaterialDirectScalarControl : public Component,
                                    public lvgl::NavigationAdmissionGuard,
                                    public lvgl::DirectSceneController,
                                    public ControlViewBackend {
 public:
  static constexpr size_t MAX_CANVAS_BYTES = 192 * 1024;
  static constexpr size_t GLYPH_BYTES = 48 * 1024;
  static constexpr uint32_t WORKER_STACK_BYTES = 8192;
  MaterialDirectScalarControl(lvgl::LvglComponent *lvgl, MaterialEntityControlComponent *control,
                              lvgl::LvglNavigation *navigation, lvgl::LvglApplication *application)
      : lvgl_(lvgl), control_(control), navigation_(navigation), application_(application) {}
  void set_widgets(lv_obj_t *region, lv_obj_t *track, lv_obj_t *bubble, lv_obj_t *value, lv_obj_t *minimum,
                   lv_obj_t *maximum);
  template<typename F> void set_active_color_provider(F &&provider) {
    this->active_color_provider_ = std::forward<F>(provider);
  }
  void set_active_color(uint32_t color) {
    this->active_color_provider_ = [color]() { return color; };
  }
  void set_initial_enabled(bool enabled) { this->initial_enabled_ = enabled; }
  void setup() override;
  void loop() override;
  void on_shutdown() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR - 6.0f; }

  // Owner-task calls only. False keeps references/resources and refuses handoff.
  bool suspend_for_snapshot(uint32_t timeout_ms = 250);
  bool resume(uint32_t captured_generation);
  bool refresh_style(uint32_t captured_generation);
  bool is_quiescent() const;
  bool prepare_navigation(lvgl::NavigationAdmissionKind kind, lvgl::LvglApplication *application) override;
  bool suspend_for_direct_overlay() override;
  void resume_after_direct_overlay() override;

  // Named component-owned allocations only; shared compositor copies are
  // reported separately by the explicit LVGL direct-buffer diagnostic.
  size_t memory_usage_bytes() const;
  void log_memory_usage(const char *phase) const;
  size_t canvas_capacity() const { return this->canvas_capacity_; }
  size_t cached_glyph_bytes() const { return this->mask_bytes_; }
  uint32_t rendered_count() const { return this->rendered_.load(std::memory_order_acquire); }
  uint32_t coalesced_count() const { return this->coalesced_.load(std::memory_order_acquire); }
#ifdef USE_LVGL_MATERIAL_SCALAR_PERFORMANCE_METRICS
  // Configuration-only setter; immutable once the worker can run.
  void set_performance_metrics_enabled(bool enabled) { this->performance_.enabled = enabled; }
  // Owner-task, explicit diagnostics only. False means busy/disabled/inconsistent.
  bool performance_snapshot(ScalarPerformanceSnapshot &out) const;
  void log_performance(const char *phase, bool include_histograms = false) const;
#endif

 protected:
  bool configure_control_view_() {
#ifdef USE_LVGL_MATERIAL_NUMBER_STEPPER
    if (this->control_ != nullptr && this->control_->uses_generic_view())
      return this->initial_enabled_ &&
             this->control_->register_scalar_backend(this, this->navigation_, this->application_);
#endif
    return this->control_ != nullptr &&
           this->control_->view_lifecycle().configure(this, this->navigation_, this->application_);
  }
  bool suspend_control_view_backend(uint32_t timeout_ms) override;
  bool resume_control_view_backend(uint32_t captured_generation) override;
  bool has_native_power_fence() const override { return this->setup_ready_; }
  bool begin_native_power_input(uint32_t captured_generation) override;
  bool finish_native_power_input(uint32_t captured_generation) override;
  enum class CompletionDecision : uint8_t { STABLE, TORN, CLOSED };
  CompletionDecision consume_completion_(bool active, bool refresh);
  bool hide_control_view_alias() override { return this->hide_alias_(); }
  bool control_view_region_visible() const override {
    return this->region_ != nullptr && lv_obj_is_visible(this->region_);
  }
  struct Request {
    uint32_t id{}, selection{}, base{};
    bool known{}, stable{};
    uint8_t value{};
#ifdef USE_LVGL_MATERIAL_SCALAR_PERFORMANCE_METRICS
    uint32_t origin_us{}, origin_sequence{};
#endif
  };
#ifdef USE_LVGL_MATERIAL_SCALAR_PERFORMANCE_METRICS
  void performance_start_origin_(uint32_t id);
  // Owner task only; retained across cancellation for explicit cold diagnostics.
  // This is the LAST CURRENT decision, not a claim about the currently visible view.
  struct CurrentCompletionDecision {
    uint32_t sequence{0}, id{0}, selection{0}, completed_base{0}, current_base{0};
    bool valid{false}, accepted{false}, success{false};
  };
  static_assert(sizeof(CurrentCompletionDecision) <= 32, "Bounded completion diagnostic exceeded");
  struct PerformanceState {
    ScalarPerformanceAccumulator counters;
    CurrentCompletionDecision last_current_completion;
    std::atomic<uint32_t> requested_origin_us{0}, requested_origin_sequence{0};
    uint32_t origin_id{0}, origin_us{0}, origin_sequence{0};  // Owner task only.
    uint32_t observed_origin_sequence{0};                     // Worker only.
    bool enabled{false};                                      // Setup-time immutable.
  };
  static_assert(sizeof(PerformanceState) <= 1536, "Bounded optional diagnostic payload exceeded");
  PerformanceState performance_;
#endif
  bool cache_style_();
  bool cache_font_(const lv_font_t *font, int letter_space, ScalarFont &target);
  bool ensure_resources_();
  void request_(uint32_t id, bool known, uint8_t value);
  void publish_request_(uint32_t id, bool known, uint8_t value, bool stable, uint32_t base);
  bool read_request_(Request &request) const;
  void cancel_();
  bool drain_(uint32_t timeout_ms);
  bool hide_alias_();
  static void retired_(void *argument);
#ifdef USE_ESP32
  bool ensure_worker_();
  static void worker_(void *argument);
  void render_worker_();
  TaskHandle_t worker_handle_{nullptr};
  StackType_t *worker_stack_{nullptr};
  StaticTask_t worker_storage_{};
#endif

  lvgl::LvglComponent *lvgl_;
  MaterialEntityControlComponent *control_;
  lvgl::LvglNavigation *navigation_;
  lvgl::LvglApplication *application_;
  lv_obj_t *region_{nullptr};
  lv_obj_t *track_{nullptr};
  lv_obj_t *bubble_{nullptr};
  lv_obj_t *value_{nullptr};
  lv_obj_t *minimum_{nullptr};
  lv_obj_t *maximum_{nullptr};
  lv_obj_t *backing_image_{nullptr};
  lv_image_dsc_t backing_descriptor_{};
  ScalarRasterStyle style_{};
  uint8_t *canvas_{nullptr};
  uint8_t *glyph_masks_{nullptr};
  size_t canvas_capacity_{0}, mask_bytes_{0};
  uint32_t base_generation_{0};
  uint32_t consumed_completion_{0};
  uint32_t boundary_request_{0};
  uint8_t boundary_value_{0};
  bool boundary_known_{false};
  bool boundary_stable_{false};
  bool pause_owned_{false};
  bool handoff_pending_{false};
  bool alias_visible_{false};
  bool setup_ready_{false};
  bool initial_enabled_{false};
  bool suspension_requested_{false};
  bool styles_valid_{false};
  std::atomic<bool> active_{false}, worker_busy_{false}, source_in_flight_{false}, source_retired_{false};
  std::atomic<bool> stop_{false}, worker_exited_{false}, region_owned_{false};
  std::atomic<uint32_t> request_sequence_{0}, requested_id_{0}, requested_value_{0}, requested_base_{0};
  std::atomic<uint32_t> completed_id_{0}, completed_selection_{0}, completed_base_{0};
  std::atomic<bool> completed_success_{false};
  std::atomic<uint32_t> completion_sequence_{0}, rendered_{0}, coalesced_{0};
  lvgl::DirectRegionGenerationResult result_{};
  std::function<uint32_t()> active_color_provider_;
};

template<typename... Ts> class MaterialDirectScalarResumeAction : public Action<Ts...> {
 public:
  explicit MaterialDirectScalarResumeAction(MaterialDirectScalarControl *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(uint32_t, generation)
  void play(const Ts &...x) override { this->parent_->resume(this->generation_.value(x...)); }

 protected:
  MaterialDirectScalarControl *parent_;
};

}  // namespace esphome::lvgl_material
#endif
