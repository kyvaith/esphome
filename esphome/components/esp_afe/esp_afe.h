#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "../audio_processor/audio_processor.h"

#ifdef USE_ESP32

#include <esp_afe_sr_iface.h>
#include <esp_afe_sr_models.h>
#include <esp_afe_config.h>
#include <esp_gmf_afe_manager.h>
#include <freertos/ringbuf.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "esphome/core/ring_buffer.h"
#include "../audio_processor/ring_buffer_caps.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace esphome {
namespace esp_afe {

using audio_processor::AudioFeature;
using audio_processor::AudioProcessor;
using audio_processor::FeatureControl;
using audio_processor::FrameSpec;
using audio_processor::ProcessorTelemetry;

/// Full Espressif AFE pipeline wrapper.
///
/// Implements AudioProcessor on top of Espressif's GMF AFE manager
/// (AEC + NS + VAD + AGC + structural dual-mic Speech Enhancement). The
/// manager owns esp-sr feed/fetch tasks; this component only bridges ESPHome's
/// realtime I2S loop into the manager callbacks.
///
/// Runtime reconfiguration that changes the AFE graph (NS/AGC or switching
/// SR/VC/FD mode) must tear the esp-sr instance down and rebuild it. AEC and
/// VAD are live-toggled through the GMF manager; SE/BSS is structural on
/// dual-mic builds. Because
/// process() is called from the consumer audio task (prio 19) while
/// config mutations come from the main thread, the two ends coordinate
/// through a lock-free drain handshake:
///
///   1. Config change acquires config_mutex_ and flips drain_request_
///      to true.
///   2. process() observes drain_request_ at the top of every frame and
///      returns silence immediately, without touching the esp-sr handle.
///   3. Config change waits for process_busy_ to clear, then it is safe
///      to destroy + rebuild the esp-sr instance.
///   4. After rebuild, drain_request_ is cleared; process() resumes
///      normal operation on the next frame.
///
/// This avoids blocking the consumer's audio task on any global mutex.
class EspAfe : public Component, public AudioProcessor {
 public:
  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::PROCESSOR; }

  // AudioProcessor interface
  bool is_initialized() const override {
    // Runtime rings are prepared while tasks are stopped. Including
    // feed_input_ring_ prevents process() from seeing a "ready" pipeline
    // during any install window before prepared runtime is complete.
    return this->afe_manager_ != nullptr &&
           this->feed_buf_ != nullptr && this->feed_input_ring_ != nullptr;
  }
  FrameSpec frame_spec() const override;
  bool process(const int16_t *in_mic, const int16_t *in_ref, int16_t *out,
               uint8_t mic_channels_in = 1) override;
  uint32_t frame_spec_revision() const override {
    return this->frame_spec_revision_.load(std::memory_order_acquire);
  }
  FeatureControl feature_control(AudioFeature feature) const override;
  bool set_feature(AudioFeature feature, bool enabled) override;
  ProcessorTelemetry telemetry() const override;
  bool reconfigure(int type, int mode) override;
  // Pause the GMF AFE manager when no consumer is listening (called by
  // i2s_audio_duplex when the last mic consumer leaves) and resume it when a
  // consumer re-attaches. Idempotent.
  void set_processing_active(bool active) override;
  bool wants_background_input() const override {
    return this->continuous_vad_ && this->vad_enabled_.load(std::memory_order_relaxed);
  }

  // Config setters (called from Python codegen)
  void set_afe_type(int type) { this->afe_type_ = type; }
  void set_afe_mode(int mode) { this->afe_mode_ = mode; }
  void set_mic_num(int num) { this->mic_num_ = num; }
  void set_aec_enabled(bool en) { this->aec_enabled_.store(en, std::memory_order_relaxed); }
  void set_aec_filter_length(int len) { this->aec_filter_length_ = len; }
  void set_aec_nlp_level(int level) { this->aec_nlp_level_ = level; }
  void set_se_enabled(bool en) { this->se_enabled_.store(en, std::memory_order_relaxed); }
  void set_ns_enabled(bool en) { this->ns_enabled_.store(en, std::memory_order_relaxed); }
  void set_vad_enabled(bool en) { this->vad_enabled_.store(en, std::memory_order_relaxed); }
  void set_vad_mode(int mode) { this->vad_mode_ = mode; }
  void set_vad_min_speech_ms(int ms) { this->vad_min_speech_ms_ = ms; }
  void set_vad_min_noise_ms(int ms) { this->vad_min_noise_ms_ = ms; }
  void set_vad_delay_ms(int ms) { this->vad_delay_ms_ = ms; }
  void set_vad_mute_playback(bool en) { this->vad_mute_playback_ = en; }
  void set_vad_enable_channel_trigger(bool en) { this->vad_enable_channel_trigger_ = en; }
  void set_continuous_vad(bool en) { this->continuous_vad_ = en; }
  void set_agc_enabled(bool en) { this->agc_enabled_.store(en, std::memory_order_relaxed); }
  void set_agc_compression_gain(int gain) { this->agc_compression_gain_ = gain; }
  void set_agc_target_level(int level) { this->agc_target_level_ = level; }
  void set_memory_alloc_mode(int mode) { this->memory_alloc_mode_ = mode; }
  void set_afe_linear_gain(float gain) { this->afe_linear_gain_ = gain; }
  void set_task_core(int core) { this->task_core_ = core; }
  void set_task_priority(int prio) { this->task_priority_ = prio; }
  void set_ringbuf_size(int size) { this->ringbuf_size_ = size; }
  void set_input_format_override(const char *fmt);
  void set_feed_buf_in_psram(bool psram) { this->feed_buf_in_psram_ = psram; }
  void set_feed_ring_in_psram(bool psram) { this->feed_ring_in_psram_ = psram; }
  void set_fetch_ring_in_psram(bool psram) { this->fetch_ring_in_psram_ = psram; }
  void set_input_volume_sensor_enabled(bool en) { this->input_volume_sensor_enabled_ = en; }
  void set_output_rms_sensor_enabled(bool en) { this->output_rms_sensor_enabled_ = en; }

  // Runtime toggles (for switches and automations)
  bool enable_aec();
  bool disable_aec();
  bool enable_ns();
  bool disable_ns();
  bool enable_vad();
  bool disable_vad();
  bool enable_agc();
  bool disable_agc();

  bool is_aec_enabled() const { return this->aec_enabled_.load(std::memory_order_relaxed); }
  bool is_se_enabled() const {
    return this->mic_num_ >= 2;
  }
  bool is_ns_enabled() const { return this->ns_enabled_.load(std::memory_order_relaxed); }
  bool is_vad_enabled() const { return this->vad_enabled_.load(std::memory_order_relaxed); }
  bool is_agc_enabled() const { return this->agc_enabled_.load(std::memory_order_relaxed); }
  bool set_bss_output_source_name(const char *name);
  const char *get_bss_output_source_name() const { return this->bss_output_source_name_(); }
  // Current mode string ("sr_low_cost", "sr_high_perf", "voip_low_cost",
  // "voip_high_perf", "fd_low_cost", "fd_high_perf"). Used by UI templates
  // to publish the actual live mode after a set_action so optimistic selects
  // don't drift from reality.
  std::string get_mode_name() const {
    // afe_type_: 0 = SR, 1 = VC, 3 = FD (esp-sr 2.4+); afe_mode_: 0 = LOW_COST, 1 = HIGH_PERF.
    const bool high = (this->afe_mode_ == 1);
    switch (this->afe_type_) {
      case 0:  return high ? "sr_high_perf" : "sr_low_cost";
      case 3:  return high ? "fd_high_perf" : "fd_low_cost";
      default: return high ? "voip_high_perf" : "voip_low_cost";
    }
  }
  bool is_voice_present() const { return this->voice_present_.load(std::memory_order_relaxed); }
  float get_input_volume_dbfs() const { return this->input_volume_dbfs_.load(std::memory_order_relaxed); }
  float get_output_rms_dbfs() const { return this->output_rms_dbfs_.load(std::memory_order_relaxed); }

  // Reinit with a new mode string (e.g. "sr_low_cost", "voip_high_perf").
  // Caller must stop audio processing before calling this.
  bool reinit_by_name(const std::string &name);
  bool reinit_by_name(const char *name);

  ~EspAfe() override;

 protected:
  // Derive aec_mode_t from afe_type + afe_mode
  aec_mode_t derive_aec_mode_() const;
  int afe_mic_channels_() const;

  struct AfeInstance {
    esp_gmf_afe_manager_handle_t manager{nullptr};
    afe_config_t *config{nullptr};
    int16_t *feed_buf{nullptr};
    int feed_chunksize{0};
    int fetch_chunksize{0};
    int process_chunksize{0};
    int total_channels{0};
  };

  bool build_instance_(AfeInstance *instance);
  bool recreate_instance_(bool require_same_frame_sizes);
  bool set_aec_enabled_runtime_(bool enabled);
  bool set_vad_enabled_runtime_(bool enabled);
  bool set_reinit_flag_(std::atomic<bool> &flag, bool enabled, const char *name);
  bool prepare_runtime_();
  bool prepare_feed_input_ring_();
  bool prepare_fetch_output_ring_();
  void release_runtime_buffers_();
  void log_memory_snapshot_(const char *label) const;
  void destroy_instance_(AfeInstance *instance);
  bool install_instance_(AfeInstance *instance);
  AfeInstance detach_instance_();
  const char *memory_alloc_mode_to_str_() const;

  // True when the user has every AFE feature turned off. In that state
  // running the pipeline is pointless, so recreate_instance_ and the runtime
  // toggle paths tear the instance down instead of rebuilding it.
  bool all_features_disabled_() const {
    return !this->aec_enabled_.load(std::memory_order_relaxed) &&
           !this->ns_enabled_.load(std::memory_order_relaxed) &&
           !this->agc_enabled_.load(std::memory_order_relaxed) &&
           !this->vad_enabled_.load(std::memory_order_relaxed) &&
           !this->is_se_enabled();
  }

  // GMF AFE manager and config. The config must outlive the manager because
  // esp-sr stores pointers into it.
  esp_gmf_afe_manager_handle_t afe_manager_{nullptr};
  afe_config_t *afe_config_{nullptr};

  // Feed buffer: interleaved [mic, ref, ...], [mic1, mic2, ref, ...] or
  // [mic1, mic2, N, ref, ...] depending on esp-sr input_format.
  int16_t *feed_buf_{nullptr};
  int feed_chunksize_{0};   // per-channel samples expected by feed()
  int fetch_chunksize_{0};  // mono output samples returned by fetch()
  int process_chunksize_{0};  // external process() input chunk size
  int total_channels_{2};
  int staged_input_samples_{0};
  // Last mic_channels_in seen by process(); used to drop a partial
  // staged frame if the consumer flips the channel layout without
  // passing through recreate_instance_ first.
  int last_process_mic_channels_{0};

  // i2s_audio_duplex stages full AFE feed frames into this NOSPLIT bridge.
  // Espressif's GMF AFE manager owns the feed/fetch tasks and pulls from the
  // bridge through manager_read_().
  static constexpr size_t kBridgeRingFrames = 4;
  static constexpr size_t kRingbufferItemHeaderBytes = 8;

  RingbufHandle_t feed_input_ring_{nullptr};
  uint8_t *feed_input_ring_storage_{nullptr};
  StaticRingbuffer_t *feed_input_ring_struct_{nullptr};

  static int32_t manager_read_cb_(void *buffer, int buf_sz, void *user_ctx, uint32_t ticks);
  static void manager_result_cb_(afe_fetch_result_t *result, void *user_ctx);
  int32_t manager_read_(void *buffer, int buf_sz, uint32_t ticks);
  void manager_result_(afe_fetch_result_t *result);
  bool activate_manager_();
  void suspend_manager_();
  void flush_manager_before_suspend_();
  void drain_feed_input_ring_();

  // Fetch bridge: GMF result callback writes, process() reads non-blocking.
  audio_processor::RingBufferPtr fetch_output_ring_;

  enum class BssOutputSource : uint8_t {
    AUTO = 0,
    BSS_OUTPUT_0,
    BSS_OUTPUT_1,
  };
  const char *bss_output_source_name_() const;
  static uint16_t peak_i16_(const int16_t *data, int samples, int stride);
  void log_bss_output_debug_(const afe_fetch_result_t *result, int out_samples,
                             BssOutputSource selected) const;

  // Scratch for optional deinterleaving of raw_data[n] when SE/BSS is on and
  // AEC is off. This is diagnostic only; normal AEC-on operation uses
  // result->data from ESP-SR.
  int16_t *fetch_raw_select_scratch_{nullptr};
  std::atomic<BssOutputSource> bss_output_source_{BssOutputSource::AUTO};
  mutable std::atomic<uint32_t> bss_output_debug_frames_{0};

  // Config (set from Python, used in setup())
  int afe_type_{0};         // AFE_TYPE_SR
  int afe_mode_{0};         // AFE_MODE_LOW_COST
  int mic_num_{1};  // physical microphone channels available from transport
  // Feature flags exposed to UI toggles. std::atomic so a torn snapshot
  // across the 5 flags in all_features_disabled_() can't trigger a
  // spurious teardown if a future caller reads them off the main loop.
  std::atomic<bool> aec_enabled_{true};
  int aec_filter_length_{4};
  int aec_nlp_level_{1};  // AEC_NLP_LEVEL_AGGR
  std::atomic<bool> se_enabled_{false};
  std::atomic<bool> ns_enabled_{true};
  std::atomic<bool> vad_enabled_{false};
  int vad_mode_{VAD_MODE_3};
  int vad_min_speech_ms_{128};
  int vad_min_noise_ms_{1000};
  int vad_delay_ms_{128};
  bool vad_mute_playback_{false};
  bool vad_enable_channel_trigger_{false};
  bool continuous_vad_{false};
  std::atomic<bool> agc_enabled_{true};
  int agc_compression_gain_{9};
  int agc_target_level_{3};
  int memory_alloc_mode_{AFE_MEMORY_ALLOC_MORE_PSRAM};
  float afe_linear_gain_{1.0f};
  int task_core_{1};
  int task_priority_{5};
  int ringbuf_size_{8};
  char input_format_override_[5]{};
  bool feed_buf_in_psram_{false};   // ~3 KB scratch (default internal, ~41 us/frame faster on Core 0)
  bool feed_ring_in_psram_{false};  // ~12 KB staging ring (default internal, ~20 us/frame faster on Core 0)
  bool fetch_ring_in_psram_{false}; // ~4 KB output ring (default internal, ~6.8 us/frame faster on Core 0)

  // config_mutex_ serialises config-change paths (recreate_instance_,
  // set_aec_enabled_runtime_, destructor). It is NOT taken by process() on
  // the hot path. process() uses the drain protocol below instead.
  SemaphoreHandle_t config_mutex_{nullptr};

  // Drain protocol for process() vs recreate_instance_:
  //   recreate_instance_ sets drain_request_ = true and waits until
  //   process_busy_ == false before touching instance state. process() marks
  //   itself busy, then checks drain_request_; if set, it bails with
  //   silence. This removes mutex overhead from the per-frame path while
  //   preserving the invariant that process() never observes a
  //   half-demolished instance.
  std::atomic<bool> drain_request_{false};
  std::atomic<bool> process_busy_{false};

  // afe_stopped_ == true means the GMF manager is torn down for a single-mic
  // all-features-off configuration. Dual-mic builds keep SE/BSS structural, so
  // this path should not be entered there.
  std::atomic<bool> afe_stopped_{false};

  std::atomic<bool> voice_present_{false};
  std::atomic<float> input_volume_dbfs_{-120.0f};
  std::atomic<float> output_rms_dbfs_{-120.0f};
  bool input_volume_sensor_enabled_{false};
  bool output_rms_sensor_enabled_{false};
  int warmup_remaining_{3};
  std::atomic<uint32_t> frame_count_{0};
  std::atomic<uint32_t> glitch_count_{0};
  // Feed/fetch diagnostics.
  std::atomic<uint32_t> input_ring_drop_{0}; // process() could not enqueue (NOSPLIT full)
  std::atomic<uint32_t> feed_ok_{0};         // GMF manager read_cb accepted a frame
  std::atomic<uint32_t> feed_rejected_{0};   // GMF read_cb timed out or saw a bad frame
  std::atomic<uint32_t> fetch_ok_{0};        // fetch task drained a frame
  std::atomic<uint32_t> fetch_timeout_{0};   // GMF fetch returned no usable frame
  std::atomic<uint32_t> output_ring_drop_{0};// fetch_output_ring_ full
  std::atomic<uint32_t> feed_queue_frames_{0};
  std::atomic<uint32_t> feed_queue_peak_{0};
  std::atomic<uint32_t> fetch_queue_frames_{0};
  std::atomic<uint32_t> fetch_queue_peak_{0};
  std::atomic<uint32_t> process_us_last_{0};
  std::atomic<uint32_t> process_us_max_{0};
  std::atomic<uint32_t> feed_us_last_{0};
  std::atomic<uint32_t> feed_us_max_{0};
  std::atomic<uint32_t> fetch_us_last_{0};
  std::atomic<uint32_t> fetch_us_max_{0};
  mutable std::atomic<uint32_t> feed_stack_high_water_last_{0};
  mutable std::atomic<uint32_t> fetch_stack_high_water_last_{0};
  std::atomic<float> ringbuf_free_pct_{1.0f};
  std::atomic<uint32_t> frame_spec_revision_{0};
  // Tracks whether a microphone consumer wants the AFE path active. When the
  // manager is live, this also means GMF feed/fetch tasks are resumed; when an
  // all-off single-mic config has torn the manager down, the flag is preserved
  // so a later feature-enable rebuild can resume immediately.
  std::atomic<bool> processing_active_{false};
  int last_spec_mic_ch_{1};  // last published mic_channels for revision tracking (1 = default mono)
  int last_spec_process_size_{0};
  int last_spec_fetch_size_{0};
};

class AfeSwitchBase : public switch_::Switch, public Component, public Parented<EspAfe> {
 public:
  float get_setup_priority() const override { return setup_priority::DATA; }

  void setup() override {
    if (this->parent_ == nullptr)
      return;

    auto initial = this->get_initial_state_with_restore_mode();
    if (initial.has_value()) {
      this->write_state(*initial);
    } else {
      this->publish_state(this->get_parent_state_());
    }
  }

 protected:
  virtual bool get_parent_state_() const = 0;

  void publish_parent_state_() {
    if (this->parent_ != nullptr) {
      this->publish_state(this->get_parent_state_());
    }
  }
};

// Switch platform classes
class AfeAecSwitch : public AfeSwitchBase {
 public:
  void write_state(bool state) override {
    if (this->parent_ == nullptr)
      return;
    if ((state && this->parent_->enable_aec()) || (!state && this->parent_->disable_aec())) {
      this->publish_state(state);
    } else {
      this->publish_parent_state_();
    }
  }

 protected:
  bool get_parent_state_() const override { return this->parent_->is_aec_enabled(); }
};

class AfeNsSwitch : public AfeSwitchBase {
 public:
  void write_state(bool state) override {
    if (this->parent_ == nullptr)
      return;
    if ((state && this->parent_->enable_ns()) || (!state && this->parent_->disable_ns())) {
      this->publish_state(state);
    } else {
      this->publish_parent_state_();
    }
  }

 protected:
  bool get_parent_state_() const override { return this->parent_->is_ns_enabled(); }
};

class AfeVadSwitch : public AfeSwitchBase {
 public:
  void write_state(bool state) override {
    if (this->parent_ == nullptr)
      return;
    if ((state && this->parent_->enable_vad()) || (!state && this->parent_->disable_vad())) {
      this->publish_state(state);
    } else {
      this->publish_parent_state_();
    }
  }

 protected:
  bool get_parent_state_() const override { return this->parent_->is_vad_enabled(); }
};

class AfeAgcSwitch : public AfeSwitchBase {
 public:
  void write_state(bool state) override {
    if (this->parent_ == nullptr)
      return;
    if ((state && this->parent_->enable_agc()) || (!state && this->parent_->disable_agc())) {
      this->publish_state(state);
    } else {
      this->publish_parent_state_();
    }
  }

 protected:
  bool get_parent_state_() const override { return this->parent_->is_agc_enabled(); }
};

class AfeVadBinarySensor : public binary_sensor::BinarySensor, public PollingComponent, public Parented<EspAfe> {
 public:
  float get_setup_priority() const override { return setup_priority::DATA; }

  void setup() override {
    if (this->parent_ != nullptr) {
      this->publish_state(this->parent_->is_voice_present());
    }
  }

  void update() override {
    if (this->parent_ != nullptr) {
      this->publish_state(this->parent_->is_voice_present());
    }
  }
};

class AfeInputVolumeSensor : public sensor::Sensor, public PollingComponent, public Parented<EspAfe> {
 public:
  float get_setup_priority() const override { return setup_priority::DATA; }

  void update() override {
    if (this->parent_ != nullptr) {
      this->publish_state(this->parent_->get_input_volume_dbfs());
    }
  }
};

class AfeOutputRmsSensor : public sensor::Sensor, public PollingComponent, public Parented<EspAfe> {
 public:
  float get_setup_priority() const override { return setup_priority::DATA; }

  void update() override {
    if (this->parent_ != nullptr) {
      this->publish_state(this->parent_->get_output_rms_dbfs());
    }
  }
};

// Action: esp_afe.set_mode
template<typename... Ts>
class SetModeAction : public Action<Ts...>, public Parented<EspAfe> {
 public:
  TEMPLATABLE_VALUE(std::string, mode)
  void play(const Ts &...x) override {
    this->parent_->reinit_by_name(this->mode_.value(x...));
  }
};

}  // namespace esp_afe
}  // namespace esphome

#endif  // USE_ESP32
