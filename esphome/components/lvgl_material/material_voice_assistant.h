#pragma once

#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/core/component.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

#ifdef USE_ESP32
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

namespace esphome::lvgl_material {

class MaterialVoiceAssistant : public Component {
 public:
  explicit MaterialVoiceAssistant(lvgl::LvglComponent *component) : lvgl_component_(component) {}

  void set_root(lv_obj_t *root) { this->root_ = root; }
  void set_status_label(lv_obj_t *label) { this->status_label_ = label; }
  void set_user_label(lv_obj_t *label) { this->user_label_ = label; }
  void set_assistant_label(lv_obj_t *label) { this->assistant_label_ = label; }
  void set_waveform(lv_obj_t *waveform) { this->waveform_ = waveform; }
  void set_frame_interval(uint32_t interval_ms) { this->frame_interval_ms_ = interval_ms; }
  void set_wave_colors(lv_color_t primary, lv_color_t secondary, lv_color_t tertiary) {
    this->primary_color_ = primary;
    this->secondary_color_ = secondary;
    this->tertiary_color_ = tertiary;
  }
  void set_gradient(lv_color_t bottom, float start) {
    this->gradient_bottom_color_ = bottom;
    this->gradient_start_ = start;
  }

  void setup() override;
  void loop() override;
  void on_shutdown() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR - 5.0f; }

  void set_active(bool active);
  void set_phase(const std::string &phase);
  void set_transcripts(const std::string &user, const std::string &assistant);
  void set_audio_levels(float input_level, float output_level);

 protected:
  enum class VoicePhase : uint8_t { IDLE, LISTENING, THINKING, SPEAKING, THANKS, ERROR };

  struct PresentSlot {
    MaterialVoiceAssistant *owner{nullptr};
    lv_color_t *pixels{nullptr};
    std::atomic<bool> in_flight{false};
    int dirty_y1{1};
    int dirty_y2{0};
  };

  struct LabelAnimation {
    lv_obj_t *label{nullptr};
    uint32_t started_ms{0};
    uint32_t last_step_ms{0};
    uint8_t opacity{255};
    int8_t translate_y{0};
    bool active{false};
  };

  struct ContentAnimation {
    uint32_t started_ms{0};
    uint32_t last_step_ms{0};
    int16_t translate_y{0};
    int16_t start_translate_y{0};
    bool active{false};
  };

  struct TextStyle {
    const lv_font_t *font{nullptr};
    lv_color_t color{lv_color_hex(0xFFFFFF)};
    int32_t letter_space{0};
    int32_t line_space{0};
  };

  struct TextRegion {
    MaterialVoiceAssistant *owner{nullptr};
    lv_color_t *pixels{nullptr};
    int x{0};
    int y{0};
    int width{0};
    int height{0};
    size_t bytes{0};
    std::atomic<bool> in_flight{false};
    std::atomic<bool> pending{false};
    std::atomic<bool> owned{false};
  };

  struct LineSpan {
    uint32_t offset{0};
    uint32_t length{0};
    int32_t width{0};
  };

  static void present_done_(void *arg);
  static void text_present_done_(void *arg);
#ifdef USE_ESP32
  static void worker_(void *arg);
#endif
  bool allocate_buffers_();
  void release_buffers_();
  void build_backdrop_();
  void schedule_frame_();
  bool render_frame_();
  void draw_wave_(lv_color_t *pixels, int phase, int amplitude, int cycles_q10, int secondary_cycles_q10, uint8_t red,
                  uint8_t green, uint8_t blue, uint8_t opacity, int thickness, int &dirty_y1, int &dirty_y2);
  void blend_pixel_(lv_color_t *pixels, int x, int y, uint8_t red, uint8_t green, uint8_t blue, uint8_t opacity);
  int16_t sin_q15_(int phase) const;
  void service_release_();
  void service_transcripts_(uint32_t now);
  void service_label_animations_(uint32_t now);
  void service_content_animation_(uint32_t now);
  void service_text_regions_();
  bool submit_text_region_(TextRegion &region);
  bool render_status_region_();
  bool render_transcript_region_();
  int layout_lines_(const std::string &text, const TextStyle &style, int max_width, LineSpan *lines, int max_lines,
                    int &height) const;
  void draw_text_block_(lv_color_t *pixels, int buffer_width, int buffer_height, const std::string &text,
                        const TextStyle &style, int x, int y, int width, int height, uint8_t opacity, int translate_y);
  void draw_text_line_(lv_color_t *pixels, int buffer_width, int buffer_height, const char *text, uint32_t length,
                       const TextStyle &style, int x, int y, int width, int text_width, uint8_t opacity);
  void fill_text_region_(TextRegion &region);
  void log_performance_(uint32_t now);
  void animate_label_in_(lv_obj_t *label);
  void animate_content_shift_(int translate_y);
  void set_label_text_(lv_obj_t *label, std::string &current, const std::string &text, bool animate);
  int transcript_content_height_(const std::string &previous_assistant, const std::string &user,
                                 const std::string &assistant) const;
  static bool is_continuation_(const std::string &previous, const std::string &next);

  lvgl::LvglComponent *lvgl_component_{nullptr};
  lv_obj_t *root_{nullptr};
  lv_obj_t *status_label_{nullptr};
  lv_obj_t *user_label_{nullptr};
  lv_obj_t *assistant_label_{nullptr};
  lv_obj_t *waveform_{nullptr};

  std::string phase_text_;
  std::string previous_assistant_text_;
  std::string user_text_;
  std::string assistant_text_;
  std::string requested_previous_assistant_text_;
  std::string requested_user_text_;
  std::string requested_assistant_text_;
  bool awaiting_new_assistant_{false};
  bool transcript_dirty_{false};
  bool animate_user_on_commit_{false};
  bool animate_assistant_on_commit_{false};
  bool animate_content_on_commit_{false};
  uint32_t last_transcript_commit_ms_{0};
  std::atomic<VoicePhase> phase_{VoicePhase::IDLE};
  std::atomic<bool> active_{false};
  std::atomic<uint16_t> input_level_q15_{0};
  std::atomic<uint16_t> output_level_q15_{0};
  std::atomic<uint16_t> smoothed_level_q15_{0};
  uint32_t last_frame_ms_{0};
  uint32_t frame_interval_ms_{33};
  std::atomic<uint16_t> wave_phase_{0};
  std::atomic<uint16_t> breathe_phase_{0};
  LabelAnimation label_animations_[2]{};
  ContentAnimation content_animation_{};

  int screen_x_{0};
  int screen_y_{0};
  int width_{0};
  int height_{0};
  int display_height_{0};
  float gradient_start_{0.625f};
  lv_color_t gradient_bottom_color_{lv_color_hex(0x2D2136)};
  lv_color_t primary_color_{lv_color_hex(0xE4C2FF)};
  lv_color_t secondary_color_{lv_color_hex(0xD9C2FF)};
  lv_color_t tertiary_color_{lv_color_hex(0x735E9A)};
  size_t pixel_count_{0};
  size_t buffer_bytes_{0};
  lv_color_t *backdrop_{nullptr};
  PresentSlot slots_[2]{};
  TextRegion status_region_{};
  TextRegion transcript_region_{};
  TextStyle status_style_{};
  TextStyle user_style_{};
  TextStyle assistant_style_{};
  lv_draw_buf_t *glyph_buffer_{nullptr};
  lv_color_t text_background_color_{lv_color_hex(0x000000)};
  int transcript_panel_x_{0};
  int transcript_panel_y_{0};
  int transcript_panel_width_{0};
  int transcript_panel_height_{0};
  std::atomic<bool> frame_pending_{false};
  std::atomic<bool> worker_busy_{false};
  std::atomic<bool> release_pending_{false};
  std::atomic<bool> region_owned_{false};
  std::atomic<uint32_t> perf_requested_{0};
  std::atomic<uint32_t> perf_rendered_{0};
  std::atomic<uint32_t> perf_render_total_us_{0};
  std::atomic<uint32_t> perf_render_max_us_{0};
  std::atomic<uint32_t> perf_submitted_{0};
  std::atomic<uint32_t> perf_no_slot_{0};
  std::atomic<uint32_t> perf_rejected_{0};
  uint32_t perf_text_rendered_{0};
  uint32_t perf_text_render_total_us_{0};
  uint32_t perf_text_render_max_us_{0};
  uint32_t perf_text_submit_busy_{0};
  uint32_t perf_window_start_ms_{0};
  bool perf_enabled_{false};
  int16_t sine_table_[1024]{};

#ifdef USE_ESP32
  TaskHandle_t worker_handle_{nullptr};
  StaticTask_t worker_storage_{};
  StackType_t *worker_stack_{nullptr};
  std::atomic<bool> shutdown_requested_{false};
  std::atomic<bool> worker_stopped_{false};
#endif
};

}  // namespace esphome::lvgl_material
