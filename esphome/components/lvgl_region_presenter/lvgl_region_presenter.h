#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "esphome/components/display/display.h"
#include "esphome/components/lvgl/lvgl_esphome.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"

#ifdef USE_ESP32_VARIANT_ESP32P4
#include "driver/ppa.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#endif

namespace esphome::lvgl_region_presenter {

using RegionReadyCallback = void (*)(void *);

enum class RegionSubmitResult : uint8_t {
  REJECTED,
  BUSY,
  SUBMITTED,
};

class LvglRegionPresenter : public Component {
 public:
  void set_lvgl_component(lvgl::LvglComponent *component) { this->lvgl_component_ = component; }
  void set_frame_interval(uint32_t interval_ms) { this->frame_interval_ms_ = interval_ms; }

  void setup() override;
  void on_shutdown() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR - 20.0f; }

  bool begin(uint32_t timeout_ms = 100);
  bool end(uint32_t timeout_ms = 500);
  bool is_active() const { return this->session_active_.load(std::memory_order_acquire); }

  RegionSubmitResult submit_rgb888(const uint8_t *source, size_t source_stride, int source_width, int source_height,
                                   int source_x, int source_y, display::ColorOrder source_order,
                                   BufferWriter source_writer, int x, int y, int width, int height,
                                   RegionReadyCallback ready_callback, void *ready_arg);
  RegionSubmitResult submit_argb8888(const uint8_t *background, size_t background_stride,
                                     display::ColorOrder background_order, BufferWriter background_writer,
                                     const uint8_t *foreground, size_t foreground_stride, int foreground_width,
                                     int foreground_height, int foreground_x, int foreground_y,
                                     BufferWriter foreground_writer, int x, int y, int width, int height,
                                     RegionReadyCallback ready_callback, void *ready_arg);
  bool flush(uint32_t timeout_ms = 100);

 protected:
#ifdef USE_ESP32_VARIANT_ESP32P4
  enum class Operation : uint8_t {
    COPY_RGB888,
    BLEND_ARGB8888,
    BARRIER,
    STOP,
  };

  struct RegionRequest {
    Operation operation{Operation::COPY_RGB888};
    const uint8_t *source{};
    size_t source_stride{};
    int source_width{};
    int source_height{};
    int source_x{};
    int source_y{};
    display::ColorOrder source_order{display::COLOR_ORDER_RGB};
    BufferWriter source_writer{BufferWriter::CPU};
    const uint8_t *background{};
    size_t background_stride{};
    display::ColorOrder background_order{display::COLOR_ORDER_RGB};
    BufferWriter background_writer{BufferWriter::CPU};
    int x{};
    int y{};
    int width{};
    int height{};
    RegionReadyCallback ready_callback{};
    void *ready_arg{};
  };

  struct RegionSlot {
    int x{};
    int y{};
    int width{};
    int height{};
    bool valid{};
  };

  static constexpr size_t MAX_REGIONS = 8;
  static constexpr size_t QUEUE_LENGTH = 12;
  static constexpr size_t MAX_FRAME_BUFFERS = 8;
  static constexpr size_t TASK_STACK_BYTES = 6144;

  static void task_trampoline_(void *arg);
  void task_();
  bool start_task_();
  bool process_batch_(RegionRequest *requests, size_t request_count);
  bool prepare_target_(const display::FrameBufferView &active, display::FrameBufferLease *target);
  bool compose_batch_(const RegionRequest *requests, size_t request_count, const display::FrameBufferView &active,
                      display::FrameBufferLease *target);
  bool ppa_copy_(const uint8_t *source, size_t source_stride, int source_width, int source_height, int source_x,
                 int source_y, display::ColorOrder source_order, BufferWriter source_writer, int width, int height,
                 display::FrameBufferLease *target, int target_x, int target_y, bool sync_source);
  bool ppa_blend_(const RegionRequest &request, display::FrameBufferLease *target);
  bool sync_source_(const uint8_t *source, size_t size, BufferWriter writer) const;
  bool request_matches_slot_(const RegionRequest &request, const RegionSlot &slot) const;
  RegionSlot *find_slot_(int x, int y, int width, int height, bool create);
  bool enqueue_(const RegionRequest &request, TickType_t timeout_ticks = 0);
  void complete_(const RegionRequest &request) const;
  void reset_state_();

  ppa_client_handle_t srm_client_{nullptr};
  ppa_client_handle_t blend_client_{nullptr};
  QueueHandle_t queue_{nullptr};
  StaticQueue_t queue_storage_{};
  alignas(4) std::array<uint8_t, QUEUE_LENGTH * sizeof(RegionRequest)> queue_data_{};
  TaskHandle_t task_handle_{nullptr};
  StaticTask_t task_storage_{};
  StackType_t *task_stack_{nullptr};
  SemaphoreHandle_t barrier_{nullptr};
  StaticSemaphore_t barrier_storage_{};
  SemaphoreHandle_t state_lock_{nullptr};
  StaticSemaphore_t state_lock_storage_{};
  std::array<RegionSlot, MAX_REGIONS> slots_{};
  std::array<uint32_t, MAX_FRAME_BUFFERS> buffer_generations_{};
  uint32_t base_generation_{1};
#endif

  lvgl::LvglComponent *lvgl_component_{nullptr};
  uint32_t frame_interval_ms_{16};
  std::atomic<bool> session_active_{false};
  std::atomic<bool> accepting_requests_{false};
};

template<typename... Ts>
class LvglRegionPresenterBeginAction : public Action<Ts...>, public Parented<LvglRegionPresenter> {
 public:
  void play(const Ts &...x) override { this->parent_->begin(); }
};

template<typename... Ts>
class LvglRegionPresenterEndAction : public Action<Ts...>, public Parented<LvglRegionPresenter> {
 public:
  void play(const Ts &...x) override { this->parent_->end(); }
};

}  // namespace esphome::lvgl_region_presenter
