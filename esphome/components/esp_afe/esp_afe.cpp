#include "esp_afe.h"
#include "../audio_processor/audio_utils.h"
#include "../audio_processor/scoped_lock.h"

#ifdef USE_ESP32

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#include <esp_timer.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace esphome {
namespace esp_afe {

static const char *const TAG = "esp_afe";
static const TickType_t CONFIG_MUTEX_TIMEOUT = pdMS_TO_TICKS(250);
// Max wait for process() to finish its current frame when a config change
// needs to rebuild the AFE instance. ~1.5 frame periods at 16 kHz / 512 samples.
static const TickType_t DRAIN_WAIT_TIMEOUT = pdMS_TO_TICKS(50);

struct AfeModePreset {
  const char *name;
  int type;
  int mode;
};

static constexpr AfeModePreset AFE_MODE_PRESETS[] = {
    {"sr_low_cost", AFE_TYPE_SR, AFE_MODE_LOW_COST},
    {"sr_high_perf", AFE_TYPE_SR, AFE_MODE_HIGH_PERF},
    {"voip_low_cost", AFE_TYPE_VC, AFE_MODE_LOW_COST},
    {"voip_high_perf", AFE_TYPE_VC, AFE_MODE_HIGH_PERF},
    {"fd_low_cost", 3 /* AFE_TYPE_FD */, AFE_MODE_LOW_COST},
    {"fd_high_perf", 3 /* AFE_TYPE_FD */, AFE_MODE_HIGH_PERF},
};

#if defined(USE_ESP_AUDIO_STACK_TELEMETRY) && ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_DEBUG
#define ESP_AFE_TIMING_TELEMETRY 1
#else
#define ESP_AFE_TIMING_TELEMETRY 0
#endif

// Validate function pointer using ESP-IDF's memory map knowledge.
static inline bool is_valid_func(const void *ptr) {
  return ptr != nullptr && esp_ptr_executable(ptr);
}

static const char *aec_nlp_level_name(int level) {
  switch (level) {
    case 0:
      return "NORMAL";
    case 1:
      return "AGGRESSIVE";
    case 2:
      return "VERY_AGGRESSIVE";
    default:
      return "UNKNOWN";
  }
}

void EspAfe::set_input_format_override(const char *fmt) {
  if (fmt == nullptr || fmt[0] == '\0') {
    this->input_format_override_[0] = '\0';
    return;
  }
  std::strncpy(this->input_format_override_, fmt, sizeof(this->input_format_override_) - 1);
  this->input_format_override_[sizeof(this->input_format_override_) - 1] = '\0';
}

const char *EspAfe::bss_output_source_name_() const {
  switch (this->bss_output_source_.load(std::memory_order_relaxed)) {
    case BssOutputSource::AUTO:
      return "Auto (speaker leak check)";
    case BssOutputSource::BSS_OUTPUT_0:
      return "AFE multi-output 1";
    case BssOutputSource::BSS_OUTPUT_1:
      return "AFE multi-output 2";
    default:
      return "unknown";
  }
}

bool EspAfe::set_bss_output_source_name(const char *name) {
  if (name == nullptr) {
    return false;
  }
  BssOutputSource source;
  if (std::strcmp(name, "Auto (speaker leak check)") == 0 ||
      std::strcmp(name, "Auto (AEC-off speaker leak check)") == 0 ||
      std::strcmp(name, "Auto (AFE multi-output 1)") == 0 ||
      std::strcmp(name, "Auto (AFE multi-output 2)") == 0 ||
      std::strcmp(name, "Auto (BSS output 1)") == 0 ||
      std::strcmp(name, "Auto (BSS output 2)") == 0 ||
      std::strcmp(name, "ESP-SR target output") == 0 ||
      std::strcmp(name, "result_data") == 0 ||
      std::strcmp(name, "auto") == 0) {
    source = BssOutputSource::AUTO;
  } else if (std::strcmp(name, "AFE multi-output 1") == 0 ||
             std::strcmp(name, "BSS output 1") == 0 ||
             std::strcmp(name, "raw0") == 0) {
    source = BssOutputSource::BSS_OUTPUT_0;
  } else if (std::strcmp(name, "AFE multi-output 2") == 0 ||
             std::strcmp(name, "BSS output 2") == 0 ||
             std::strcmp(name, "raw1") == 0) {
    source = BssOutputSource::BSS_OUTPUT_1;
  } else {
    ESP_LOGW(TAG, "Unknown AFE output source: %s", name);
    return false;
  }
  this->bss_output_source_.store(source, std::memory_order_relaxed);
  this->bss_output_debug_frames_.store(0, std::memory_order_relaxed);
  ESP_LOGI(TAG, "AFE output source: %s", this->bss_output_source_name_());
  return true;
}

uint16_t EspAfe::peak_i16_(const int16_t *data, int samples, int stride) {
  if (data == nullptr || samples <= 0 || stride <= 0) {
    return 0;
  }
  uint16_t peak = 0;
  for (int i = 0; i < samples; i++) {
    int32_t v = data[i * stride];
    if (v < 0) {
      v = -v;
    }
    if (v > 32768) {
      v = 32768;
    }
    if (static_cast<uint16_t>(v) > peak) {
      peak = static_cast<uint16_t>(v);
    }
  }
  return peak;
}

void EspAfe::log_bss_output_debug_(const afe_fetch_result_t *result, int out_samples,
                                   BssOutputSource selected) const {
#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_DEBUG
  if (result == nullptr || result->data == nullptr || out_samples <= 0) {
    return;
  }
  uint32_t n = this->bss_output_debug_frames_.fetch_add(1, std::memory_order_relaxed) + 1;
  if (n > 5 && (n % 32) != 0) {
    return;
  }

  float data_rms = compute_rms_dbfs_i16(result->data, static_cast<size_t>(out_samples));
  uint16_t data_peak = peak_i16_(result->data, out_samples, 1);
  float raw0_rms = -120.0f;
  float raw1_rms = -120.0f;
  uint16_t raw0_peak = 0;
  uint16_t raw1_peak = 0;
  if (result->raw_data != nullptr && result->raw_data_channels > 0) {
    raw0_rms = compute_rms_dbfs_i16(result->raw_data, static_cast<size_t>(out_samples),
                                    static_cast<size_t>(result->raw_data_channels));
    raw0_peak = peak_i16_(result->raw_data, out_samples, result->raw_data_channels);
    if (result->raw_data_channels > 1) {
      raw1_rms = compute_rms_dbfs_i16(result->raw_data + 1, static_cast<size_t>(out_samples),
                                      static_cast<size_t>(result->raw_data_channels));
      raw1_peak = peak_i16_(result->raw_data + 1, out_samples, result->raw_data_channels);
    }
  }

  const char *selected_name = "auto";
  switch (selected) {
    case BssOutputSource::BSS_OUTPUT_0:
      selected_name = "afe_multi_output_1";
      break;
    case BssOutputSource::BSS_OUTPUT_1:
      selected_name = "afe_multi_output_2";
      break;
    case BssOutputSource::AUTO:
    default:
      selected_name = "auto_afe_multi_output_2";
      break;
  }
  ESP_LOGD(TAG,
           "AFE output probe[%u]: selected=%s trigger=%d raw_ch=%d "
           "data=%.1fdB/%u bss1=%.1fdB/%u bss2=%.1fdB/%u",
           (unsigned) n, selected_name, result->trigger_channel_id, result->raw_data_channels,
           data_rms, (unsigned) data_peak, raw0_rms, (unsigned) raw0_peak,
           raw1_rms, (unsigned) raw1_peak);
#else
  (void) result;
  (void) out_samples;
  (void) selected;
#endif
}

static inline void update_peak_atomic(std::atomic<uint32_t> &peak, uint32_t value) {
  uint32_t current = peak.load(std::memory_order_relaxed);
  while (value > current &&
         !peak.compare_exchange_weak(current, value, std::memory_order_relaxed,
                                     std::memory_order_relaxed)) {
  }
}

static inline void decrement_if_nonzero(std::atomic<uint32_t> &counter) {
  uint32_t current = counter.load(std::memory_order_relaxed);
  while (current > 0 &&
         !counter.compare_exchange_weak(current, current - 1, std::memory_order_relaxed,
                                        std::memory_order_relaxed)) {
  }
}

void EspAfe::log_memory_snapshot_(const char *label) const {
  ESP_LOGI(TAG,
           "Memory[%s]: internal_free=%u largest_internal=%u dma_free=%u largest_dma=%u psram_free=%u",
           label,
           (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
           (unsigned) heap_caps_get_free_size(MALLOC_CAP_DMA),
           (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_DMA),
           (unsigned) heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static inline void silence_frame(int16_t *out, int output_samples) {
  if (out != nullptr && output_samples > 0) {
    memset(out, 0, static_cast<size_t>(output_samples) * sizeof(int16_t));
  }
}

static inline int16_t afe_ref_sample(const int16_t *in_ref, int i) {
  return in_ref != nullptr ? in_ref[i] : 0;
}

static inline void stage_afe_input_frame(int16_t *dst, const int16_t *in_mic,
                                         const int16_t *in_ref, int samples,
                                         int transport_mic_channels,
                                         int afe_mic_channels, int total_channels) {
  if (dst == nullptr || in_mic == nullptr || samples <= 0 || total_channels <= 0) {
    return;
  }
  const int mic_stride = std::max(1, transport_mic_channels);

  if (afe_mic_channels == 1) {
    if (total_channels == 2) {
      for (int i = 0; i < samples; i++) {
        dst[0] = in_mic[i * mic_stride];
        dst[1] = afe_ref_sample(in_ref, i);
        dst += 2;
      }
      return;
    }
    if (total_channels == 3) {
      for (int i = 0; i < samples; i++) {
        dst[0] = in_mic[i * mic_stride];
        dst[1] = 0;
        dst[2] = afe_ref_sample(in_ref, i);
        dst += 3;
      }
      return;
    }
  } else if (transport_mic_channels >= 2) {
    if (total_channels == 3) {
      for (int i = 0; i < samples; i++) {
        const int base = i * mic_stride;
        dst[0] = in_mic[base];
        dst[1] = in_mic[base + 1];
        dst[2] = afe_ref_sample(in_ref, i);
        dst += 3;
      }
      return;
    }
    if (total_channels == 4) {
      for (int i = 0; i < samples; i++) {
        const int base = i * mic_stride;
        dst[0] = in_mic[base];
        dst[1] = in_mic[base + 1];
        dst[2] = 0;
        dst[3] = afe_ref_sample(in_ref, i);
        dst += 4;
      }
      return;
    }
  } else {
    if (total_channels == 3) {
      for (int i = 0; i < samples; i++) {
        dst[0] = in_mic[i];
        dst[1] = 0;
        dst[2] = afe_ref_sample(in_ref, i);
        dst += 3;
      }
      return;
    }
    if (total_channels == 4) {
      for (int i = 0; i < samples; i++) {
        dst[0] = in_mic[i];
        dst[1] = 0;
        dst[2] = 0;
        dst[3] = afe_ref_sample(in_ref, i);
        dst += 4;
      }
      return;
    }
  }

  for (int i = 0; i < samples; i++) {
    memset(dst, 0, static_cast<size_t>(total_channels) * sizeof(int16_t));
    dst[0] = in_mic[i * mic_stride];
    if (afe_mic_channels >= 2 && total_channels >= 2) {
      dst[1] = transport_mic_channels >= 2 ? in_mic[i * mic_stride + 1] : 0;
    }
    const int ref_index = (afe_mic_channels >= 2)
                              ? (total_channels >= 4 ? 3 : 2)
                              : (total_channels >= 3 ? 2 : 1);
    if (ref_index < total_channels) {
      dst[ref_index] = afe_ref_sample(in_ref, i);
    }
    dst += total_channels;
  }
}

aec_mode_t EspAfe::derive_aec_mode_() const {
  const bool high = (this->afe_mode_ == AFE_MODE_HIGH_PERF);
  switch (this->afe_type_) {
    case AFE_TYPE_SR:
      return high ? AEC_MODE_SR_HIGH_PERF : AEC_MODE_SR_LOW_COST;
    case 3:  // AFE_TYPE_FD (esp-sr 2.4+, value matches the upstream enum)
      // AEC_MODE_FD_HIGH_PERF = 6, AEC_MODE_FD_LOW_COST = 5.
      return static_cast<aec_mode_t>(high ? 6 : 5);
    default:  // AFE_TYPE_VC
      return high ? AEC_MODE_VOIP_HIGH_PERF : AEC_MODE_VOIP_LOW_COST;
  }
}

int EspAfe::afe_mic_channels_() const {
  if (this->mic_num_ < 2) {
    return 1;
  }
  return 2;
}

const char *EspAfe::memory_alloc_mode_to_str_() const {
  switch (this->memory_alloc_mode_) {
    case AFE_MEMORY_ALLOC_MORE_INTERNAL:
      return "MORE_INTERNAL";
    case AFE_MEMORY_ALLOC_INTERNAL_PSRAM_BALANCE:
      return "INTERNAL_PSRAM_BALANCE";
    case AFE_MEMORY_ALLOC_MORE_PSRAM:
      return "MORE_PSRAM";
    default:
      return "UNKNOWN";
  }
}

bool EspAfe::build_instance_(AfeInstance *instance) {
  if (instance == nullptr) {
    return false;
  }

  const int afe_mic_channels = this->afe_mic_channels_();
  if (afe_mic_channels >= 2) {
    this->se_enabled_.store(true, std::memory_order_relaxed);
  }
  // Stack-allocated input format string. Default preserves the historical
  // "MR" / "MMR" shape; an optional override allows board probes to exercise
  // Espressif's documented "MMNR" dual-mic layout without changing the
  // transport-facing mic channel count.
  char fmt[5];
  if (afe_mic_channels >= 2 && this->input_format_override_[0] != '\0') {
    std::strncpy(fmt, this->input_format_override_, sizeof(fmt) - 1);
    fmt[sizeof(fmt) - 1] = '\0';
  } else {
    for (int i = 0; i < afe_mic_channels && i < 2; i++) fmt[i] = 'M';
    fmt[afe_mic_channels] = 'R';
    fmt[afe_mic_channels + 1] = '\0';
  }

  afe_config_t *cfg = afe_config_init(fmt, nullptr,
                                      static_cast<afe_type_t>(this->afe_type_),
                                      static_cast<afe_mode_t>(this->afe_mode_));

  if (cfg == nullptr) {
    ESP_LOGW(TAG, "afe_config_init returned NULL, using manual config");
    cfg = afe_config_alloc();
    if (cfg == nullptr) {
      ESP_LOGE(TAG, "Failed to allocate AFE config");
      return false;
    }
    if (!afe_parse_input_format(fmt, &cfg->pcm_config)) {
      ESP_LOGE(TAG, "Failed to parse input format: %s", fmt);
      afe_config_free(cfg);
      return false;
    }
    cfg->pcm_config.sample_rate = 16000;
    cfg->afe_mode = static_cast<afe_mode_t>(this->afe_mode_);
    cfg->afe_type = static_cast<afe_type_t>(this->afe_type_);
  }

  cfg->aec_init = true;  // always init: AEC is LIVE_TOGGLE via vtable
  cfg->aec_filter_length = this->aec_filter_length_;
  cfg->aec_mode = this->derive_aec_mode_();
  cfg->aec_nlp_level = static_cast<aec_nlp_level_t>(this->aec_nlp_level_);

  // On dual-mic AFE builds SE/BSS is structural. Espressif's AFE contract
  // downgrades to first-mic-only when SE is disabled, so dual-mic devices keep
  // it initialized and enabled by design.
  if (afe_mic_channels >= 2) {
    this->se_enabled_.store(true, std::memory_order_relaxed);
  }
  cfg->se_init = afe_mic_channels >= 2;

  cfg->ns_init = this->ns_enabled_.load(std::memory_order_relaxed);
  cfg->ns_model_name = nullptr;
  cfg->afe_ns_mode = AFE_NS_MODE_WEBRTC;

  cfg->vad_init = true;  // always init: VAD is LIVE_TOGGLE via GMF manager
  cfg->vad_mode = static_cast<vad_mode_t>(this->vad_mode_);
  cfg->vad_model_name = nullptr;
  cfg->vad_min_speech_ms = this->vad_min_speech_ms_;
  cfg->vad_min_noise_ms = this->vad_min_noise_ms_;
  cfg->vad_delay_ms = this->vad_delay_ms_;
  cfg->vad_mute_playback = this->vad_mute_playback_;
  cfg->vad_enable_channel_trigger = this->vad_enable_channel_trigger_;

  cfg->wakenet_init = false;
  cfg->wakenet_model_name = nullptr;
  cfg->wakenet_model_name_2 = nullptr;

  cfg->agc_init = this->agc_enabled_.load(std::memory_order_relaxed);
  cfg->agc_mode = AFE_AGC_MODE_WEBRTC;
  cfg->agc_compression_gain_db = this->agc_compression_gain_;
  cfg->agc_target_level_dbfs = this->agc_target_level_;

  // esp-sr spawns an internal worker task for BSS/SE using these fields.
  // Inert for simple low-cost 1-mic modes without an ESP-SR worker, active for
  // 2-mic BSS and higher-cost SR/VOIP/FD pipelines.
  cfg->afe_perferred_core = this->task_core_;
  cfg->afe_perferred_priority = this->task_priority_;
  cfg->afe_ringbuf_size = this->ringbuf_size_;
  cfg->memory_alloc_mode = static_cast<afe_memory_alloc_mode_t>(this->memory_alloc_mode_);
  cfg->afe_linear_gain = this->afe_linear_gain_;
  cfg->debug_init = false;
  // MWW and VA are external ESPHome consumers here. Keep AFE fetch output as
  // processed audio; never switch channel 0 to unprocessed mic after wake.
  cfg->fixed_first_channel = false;

  afe_config_check(cfg);

  const esp_afe_sr_iface_t *handle = esp_afe_handle_from_config(cfg);
  if (handle == nullptr) {
    ESP_LOGE(TAG, "esp_afe_handle_from_config returned NULL");
    afe_config_free(cfg);
    return false;
  }

  esp_gmf_afe_manager_cfg_t manager_cfg{};
  manager_cfg.afe_cfg = cfg;
  manager_cfg.feed_task_setting.prio = this->feed_task_priority_;
  manager_cfg.feed_task_setting.core = this->feed_task_core_;
  manager_cfg.feed_task_setting.stack_size = this->feed_task_stack_size_;
  manager_cfg.fetch_task_setting.prio = this->fetch_task_priority_;
  manager_cfg.fetch_task_setting.core = this->fetch_task_core_;
  manager_cfg.fetch_task_setting.stack_size = this->fetch_task_stack_size_;
  manager_cfg.read_cb = nullptr;
  manager_cfg.read_ctx = this;
  manager_cfg.result_cb = &EspAfe::manager_result_cb_;
  manager_cfg.result_ctx = this;

  esp_gmf_afe_manager_handle_t manager = nullptr;
  esp_gmf_err_t manager_ret = esp_gmf_afe_manager_create(&manager_cfg, &manager);
  if (manager_ret != ESP_GMF_ERR_OK || manager == nullptr) {
    ESP_LOGE(TAG, "esp_gmf_afe_manager_create failed (ret=%d)", static_cast<int>(manager_ret));
    afe_config_free(cfg);
    return false;
  }
  // gmf_ai_audio 0.8.2 accepts result_cb in the config struct but does not
  // copy it during create(); install it explicitly before the fetch task runs.
  manager_ret = esp_gmf_afe_manager_set_result_cb(manager, &EspAfe::manager_result_cb_, this);
  if (manager_ret != ESP_GMF_ERR_OK) {
    ESP_LOGE(TAG, "esp_gmf_afe_manager_set_result_cb failed (ret=%d)",
             static_cast<int>(manager_ret));
    esp_gmf_afe_manager_destroy(manager);
    afe_config_free(cfg);
    return false;
  }

  size_t feed_chunk_size = 0;
  uint8_t total_channels_u8 = 0;
  if (esp_gmf_afe_manager_get_chunk_size(manager, &feed_chunk_size) != ESP_GMF_ERR_OK ||
      esp_gmf_afe_manager_get_input_ch_num(manager, &total_channels_u8) != ESP_GMF_ERR_OK ||
      feed_chunk_size == 0 || total_channels_u8 == 0) {
    ESP_LOGE(TAG, "Failed to query GMF AFE manager frame shape");
    esp_gmf_afe_manager_destroy(manager);
    afe_config_free(cfg);
    return false;
  }

  int feed_chunksize = static_cast<int>(feed_chunk_size);
  int fetch_chunksize = feed_chunksize;
  // process_chunksize is the input quantum exposed to consumers via
  // frame_spec().input_samples. Keep it at the fetch/output cadence so callers
  // deliver one output frame per audio loop. If esp-sr needs a larger feed
  // quantum (P4/SR BSS: feed=1024, fetch=512), process() stages multiple calls
  // internally before enqueueing one feed frame.
  int process_chunksize = fetch_chunksize;
  if (process_chunksize <= 0) {
    process_chunksize = (feed_chunksize > 0) ? feed_chunksize : 0;
  }
  // Use official API for feed channel count instead of config struct (more robust
  // if esp-sr changes internal channel mapping in future versions).
  int total_channels = static_cast<int>(total_channels_u8);
  size_t feed_bytes = static_cast<size_t>(feed_chunksize) * total_channels * sizeof(int16_t);

  const uint32_t feed_buf_caps = this->feed_buf_in_psram_
      ? (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
      : (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  int16_t *feed_buf = static_cast<int16_t *>(heap_caps_aligned_alloc(16, feed_bytes, feed_buf_caps));
  if (feed_buf == nullptr && this->feed_buf_in_psram_) {
    ESP_LOGW(TAG, "feed_buf (%u bytes) fell back to internal RAM (PSRAM full/unavailable)",
             static_cast<unsigned>(feed_bytes));
    feed_buf = static_cast<int16_t *>(
        heap_caps_aligned_alloc(16, feed_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  }
  if (feed_buf == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate feed buffer (%u bytes)", static_cast<unsigned>(feed_bytes));
    esp_gmf_afe_manager_destroy(manager);
    afe_config_free(cfg);
    return false;
  }

  instance->manager = manager;
  instance->config = cfg;
  instance->feed_buf = feed_buf;
  instance->feed_chunksize = feed_chunksize;
  instance->fetch_chunksize = fetch_chunksize;
  instance->process_chunksize = process_chunksize;
  instance->total_channels = total_channels;

  return true;
}

void EspAfe::destroy_instance_(AfeInstance *instance) {
  if (instance == nullptr) {
    return;
  }
  if (instance->manager != nullptr) {
    esp_gmf_afe_manager_destroy(instance->manager);
    instance->manager = nullptr;
  }
  if (instance->config != nullptr) {
    afe_config_free(instance->config);
    instance->config = nullptr;
  }
  if (instance->feed_buf != nullptr) {
    heap_caps_free(instance->feed_buf);
    instance->feed_buf = nullptr;
  }
  instance->feed_chunksize = 0;
  instance->fetch_chunksize = 0;
  instance->process_chunksize = 0;
  instance->total_channels = 0;
}

bool EspAfe::install_instance_(AfeInstance *instance) {
  this->afe_manager_ = instance->manager;
  this->afe_config_ = instance->config;
  this->feed_buf_ = instance->feed_buf;
  this->feed_chunksize_ = instance->feed_chunksize;
  this->fetch_chunksize_ = instance->fetch_chunksize;
  this->process_chunksize_ = instance->process_chunksize;
  this->total_channels_ = instance->total_channels;
  this->staged_input_samples_ = 0;

  instance->manager = nullptr;
  instance->config = nullptr;
  instance->feed_buf = nullptr;
  instance->feed_chunksize = 0;
  instance->fetch_chunksize = 0;
  instance->process_chunksize = 0;
  instance->total_channels = 0;

  if (!this->prepare_runtime_()) {
    ESP_LOGE(TAG, "Failed to prepare AFE runtime buffers");
    return false;
  }

  // AEC is always initialized (LIVE_TOGGLE). Disable via vtable if config says off.
  if (!this->aec_enabled_.load(std::memory_order_relaxed)) {
    esp_gmf_afe_manager_enable_features(this->afe_manager_, ESP_AFE_FEATURE_AEC, false);
  }
  // VAD is always initialized (LIVE_TOGGLE). Disable via vtable if config says off.
  if (!this->vad_enabled_.load(std::memory_order_relaxed)) {
    esp_gmf_afe_manager_enable_features(this->afe_manager_, ESP_AFE_FEATURE_VAD, false);
    this->voice_present_.store(false, std::memory_order_relaxed);
  }

  // Runtime buffers are already prepared above. Apply the configured feature
  // state before resuming GMF tasks so an active reconfigure never runs even
  // one frame with stale AEC/VAD state.
  if (this->processing_active_.load(std::memory_order_acquire)) {
    if (!this->activate_manager_()) {
      ESP_LOGE(TAG, "Reconfigure: GMF AFE manager failed to resume");
      return false;
    }
  }

  return true;
}

EspAfe::AfeInstance EspAfe::detach_instance_() {
  // Drain protocol has already quiesced process() before detach is called,
  // so no more enqueues can race. First detach the result callback: GMF holds
  // its own callback mutex, so after this returns no fetch task can write into
  // our output bridge while we suspend/reset/destroy it.
  if (this->afe_manager_ != nullptr) {
    esp_gmf_afe_manager_set_result_cb(this->afe_manager_, nullptr, nullptr);
  }
  // Stop feed first while fetch can still drain esp-sr internal output; then
  // stop fetch and reset the bridge rings.
  this->flush_manager_before_suspend_();
  this->suspend_manager_();

  AfeInstance instance;
  instance.manager = this->afe_manager_;
  instance.config = this->afe_config_;
  instance.feed_buf = this->feed_buf_;
  instance.feed_chunksize = this->feed_chunksize_;
  instance.fetch_chunksize = this->fetch_chunksize_;
  instance.process_chunksize = this->process_chunksize_;
  instance.total_channels = this->total_channels_;

  this->afe_manager_ = nullptr;
  this->afe_config_ = nullptr;
  this->feed_buf_ = nullptr;
  this->feed_chunksize_ = 0;
  this->fetch_chunksize_ = 0;
  this->process_chunksize_ = 0;
  this->total_channels_ = 0;
  this->staged_input_samples_ = 0;

  return instance;
}

bool EspAfe::recreate_instance_(bool require_same_frame_sizes) {
  if (this->config_mutex_ == nullptr) {
    this->config_mutex_ = xSemaphoreCreateMutex();
    if (this->config_mutex_ == nullptr) {
      ESP_LOGE(TAG, "Failed to create config mutex");
      return false;
    }
  }

  audio_processor::ScopedLock lock(this->config_mutex_, CONFIG_MUTEX_TIMEOUT);
  if (!lock) {
    ESP_LOGW(TAG, "Timed out waiting to rebuild AFE instance");
    return false;
  }

  // Drain protocol: signal process() to bail, then wait for any in-flight
  // call to complete. process_busy_ is cleared at the end of process() with
  // release semantics; our acquire load here pairs with that store so we
  // observe a quiesced state before touching the instance. Timeout is a
  // safety net: if i2s_audio_task is stuck elsewhere we proceed anyway to
  // avoid deadlocking the reconfiguration.
  this->drain_request_.store(true, std::memory_order_release);
  TickType_t drain_deadline = xTaskGetTickCount() + DRAIN_WAIT_TIMEOUT;
  while (this->process_busy_.load(std::memory_order_acquire)) {
    if (xTaskGetTickCount() >= drain_deadline) {
      ESP_LOGW(TAG, "Drain timeout waiting for process() to quiesce, proceeding");
      break;
    }
    vTaskDelay(1);
  }

  // From here on the ScopedLock auto-releases on every return path; only the
  // drain flag has to be reset before each return.
  auto release_drain = [this]() { this->drain_request_.store(false, std::memory_order_release); };

  // esp-sr FFT resources are global: only one AFE instance can exist.
  // Must destroy the previous instance before creating the next one.
  int old_process = this->process_chunksize_;
  int old_fetch = this->fetch_chunksize_;
  AfeInstance old = this->detach_instance_();
  this->destroy_instance_(&old);
  this->release_runtime_buffers_();

  // If every user-facing feature is disabled there is nothing to build.
  // Stay in the torn-down state; process() will emit silence via the
  // afe_stopped_ fast path using the last-known frame spec.
  //
  // Guard: only take this shortcut once we have a cached spec to hand out
  // via frame_spec(). On the very first call (setup() before any successful
  // install) last_spec_* are zero; build the instance normally so downstream
  // learns the frame shape, then subsequent toggles can tear down cleanly.
  if (this->all_features_disabled_() && this->last_spec_process_size_ > 0 &&
      this->last_spec_fetch_size_ > 0) {
    bool was_running = !this->afe_stopped_.load(std::memory_order_acquire);
    this->afe_stopped_.store(true, std::memory_order_release);
    release_drain();
    if (was_running) {
      ESP_LOGI(TAG, "AFE stopped (all features disabled); output silenced");
    }
    return true;
  }

  AfeInstance next;
  if (!this->build_instance_(&next)) {
    ESP_LOGE(TAG, "Failed to build new AFE instance. AFE is DOWN until successful rebuild.");
    release_drain();
    return false;
  }

  if (require_same_frame_sizes && old_process > 0 && old_fetch > 0 &&
      (next.process_chunksize != old_process || next.fetch_chunksize != old_fetch)) {
    ESP_LOGW(TAG, "Reinit changed external frame sizes (%d/%d -> %d/%d), rejecting",
             old_process, old_fetch, next.process_chunksize, next.fetch_chunksize);
    this->destroy_instance_(&next);
    release_drain();
    return false;
  }

  // Compare against the last successfully-installed spec, not against the
  // (already-detached) `this->*_chunksize_` fields which are zero at this
  // point. Using the last_spec_* members means a rollback to the previous
  // config does not spuriously bump frame_spec_revision_, which would make
  // esp_audio_stack try to restart its audio task concurrently with our
  // fetch task recreation and race inside FreeRTOS.
  int new_mic_ch = this->afe_mic_channels_();
  bool spec_changed = (new_mic_ch != this->last_spec_mic_ch_ ||
                       next.process_chunksize != this->last_spec_process_size_ ||
                       next.fetch_chunksize != this->last_spec_fetch_size_);
  (void) old_process;
  (void) old_fetch;

  if (!this->install_instance_(&next)) {
    AfeInstance failed = this->detach_instance_();
    this->destroy_instance_(&failed);
    this->release_runtime_buffers_();
    release_drain();
    return false;
  }

  this->last_spec_process_size_ = this->process_chunksize_;
  this->last_spec_fetch_size_ = this->fetch_chunksize_;

  if (spec_changed) {
    int old_mic_ch = this->last_spec_mic_ch_;
    this->last_spec_mic_ch_ = new_mic_ch;
    // Release barrier ensures new frame_spec stores happen-before consumers
    // observe the bumped revision via acquire load.
    uint32_t new_rev = this->frame_spec_revision_.fetch_add(1, std::memory_order_release) + 1;
    ESP_LOGI(TAG, "Frame spec changed: mic_ch=%d->%d, process=%d, fetch=%d (revision %u, audio task will restart)",
             old_mic_ch, new_mic_ch, this->process_chunksize_, this->fetch_chunksize_, (unsigned) new_rev);
  }

  // First boot with every single-mic feature disabled: build once so
  // frame_spec() has a valid shape, then tear the manager back down. This
  // prevents an all-off AFE from acting like a raw passthrough.
  if (this->all_features_disabled_()) {
    AfeInstance idle = this->detach_instance_();
    this->destroy_instance_(&idle);
    this->release_runtime_buffers_();
    this->afe_stopped_.store(true, std::memory_order_release);
    release_drain();
    ESP_LOGI(TAG, "AFE stopped (all features disabled); output silenced");
    return true;
  }

  this->warmup_remaining_ = 3;
  this->frame_count_.store(0, std::memory_order_relaxed);
  this->glitch_count_.store(0, std::memory_order_relaxed);
  this->input_ring_drop_.store(0, std::memory_order_relaxed);
  this->feed_ok_.store(0, std::memory_order_relaxed);
  this->feed_rejected_.store(0, std::memory_order_relaxed);
  this->fetch_ok_.store(0, std::memory_order_relaxed);
  this->fetch_timeout_.store(0, std::memory_order_relaxed);
  this->output_ring_drop_.store(0, std::memory_order_relaxed);
  this->feed_queue_frames_.store(0, std::memory_order_relaxed);
  this->feed_queue_peak_.store(0, std::memory_order_relaxed);
  this->fetch_queue_frames_.store(0, std::memory_order_relaxed);
  this->fetch_queue_peak_.store(0, std::memory_order_relaxed);
  this->process_us_last_.store(0, std::memory_order_relaxed);
  this->process_us_max_.store(0, std::memory_order_relaxed);
  this->feed_us_last_.store(0, std::memory_order_relaxed);
  this->feed_us_max_.store(0, std::memory_order_relaxed);
  this->fetch_us_last_.store(0, std::memory_order_relaxed);
  this->fetch_us_max_.store(0, std::memory_order_relaxed);
  this->ringbuf_free_pct_.store(1.0f, std::memory_order_relaxed);
  this->voice_present_.store(false, std::memory_order_relaxed);
  this->input_volume_dbfs_.store(-120.0f, std::memory_order_relaxed);
  this->output_rms_dbfs_.store(-120.0f, std::memory_order_relaxed);
  // Clear the stopped flag: a live instance is running. Paired with the
  // acquire load in process() so the hot path sees the transition cleanly.
  bool was_stopped = this->afe_stopped_.exchange(false, std::memory_order_release);
  if (was_stopped) {
    ESP_LOGI(TAG, "AFE restarted (feature re-enabled)");
  }
  // Release drain: new instance is fully installed. The GMF feed/fetch tasks
  // already exist and are either resumed by install_instance_ or left suspended
  // until the next mic consumer attaches. process() can resume real work on the
  // next frame.
  release_drain();
  return true;
}

bool EspAfe::set_aec_enabled_runtime_(bool enabled) {
  if (this->aec_enabled_.load(std::memory_order_relaxed) == enabled) {
    return true;
  }
  if (this->config_mutex_ == nullptr) {
    ESP_LOGW(TAG, "AEC toggle requested before setup");
    return false;
  }

  // AFE currently torn down (all features were off). Flip the flag and let
  // recreate_instance_ rebuild (or stay stopped if still all-off).
  if (this->afe_stopped_.load(std::memory_order_acquire)) {
    this->aec_enabled_.store(enabled, std::memory_order_relaxed);
    if (!enabled) {
      return true;  // nothing to rebuild
    }
    return this->recreate_instance_(false);
  }

  if (!this->is_initialized()) {
    ESP_LOGW(TAG, "AEC toggle requested before initialization");
    return false;
  }

  // Hold the config mutex only across the enable/disable call. The
  // potential teardown via recreate_instance_ takes the same mutex
  // itself; calling it inside the lock would recurse on a non-recursive
  // mutex and time out.
  bool needs_rebuild = false;
  {
    audio_processor::ScopedLock lock(this->config_mutex_, CONFIG_MUTEX_TIMEOUT);
    if (!lock) {
      ESP_LOGW(TAG, "Timed out waiting to toggle AEC");
      return false;
    }

    int ret = static_cast<int>(esp_gmf_afe_manager_enable_features(
        this->afe_manager_, ESP_AFE_FEATURE_AEC, enabled));
    if (ret < 0) {
      ESP_LOGW(TAG, "GMF AFE manager %s AEC failed (ret=%d)",
               enabled ? "enable" : "disable", ret);
      return false;
    }

    ESP_LOGI(TAG, "AEC %s via GMF AFE manager", enabled ? "enabled" : "disabled");
    this->aec_enabled_.store(enabled, std::memory_order_relaxed);

    // Live toggle left AFE running. If the user just turned AEC off and
    // every other feature was already off, we should stop the whole
    // pipeline. Compute the decision while holding the lock so the
    // feature flags don't tear under a concurrent toggle.
    needs_rebuild = (!enabled && this->all_features_disabled_());
  }

  if (needs_rebuild) {
    this->recreate_instance_(false);  // no-features path tears down inside
  }
  return true;
}

bool EspAfe::set_vad_enabled_runtime_(bool enabled) {
  if (this->vad_enabled_.load(std::memory_order_relaxed) == enabled) {
    return true;
  }
  if (this->config_mutex_ == nullptr) {
    ESP_LOGW(TAG, "VAD toggle requested before setup");
    return false;
  }

  // AFE currently torn down (all features were off). Flip the flag and let
  // recreate_instance_ rebuild (or stay stopped if still all-off).
  if (this->afe_stopped_.load(std::memory_order_acquire)) {
    this->vad_enabled_.store(enabled, std::memory_order_relaxed);
    if (!enabled) {
      this->voice_present_.store(false, std::memory_order_relaxed);
      return true;
    }
    return this->recreate_instance_(false);
  }

  if (!this->is_initialized()) {
    ESP_LOGW(TAG, "VAD toggle requested before initialization");
    return false;
  }

  bool needs_rebuild = false;
  {
    audio_processor::ScopedLock lock(this->config_mutex_, CONFIG_MUTEX_TIMEOUT);
    if (!lock) {
      ESP_LOGW(TAG, "Timed out waiting to toggle VAD");
      return false;
    }

    int ret = static_cast<int>(esp_gmf_afe_manager_enable_features(
        this->afe_manager_, ESP_AFE_FEATURE_VAD, enabled));
    if (ret < 0) {
      ESP_LOGW(TAG, "GMF AFE manager %s VAD failed (ret=%d)",
               enabled ? "enable" : "disable", ret);
      return false;
    }

    ESP_LOGI(TAG, "VAD %s via GMF AFE manager", enabled ? "enabled" : "disabled");
    this->vad_enabled_.store(enabled, std::memory_order_relaxed);
    if (!enabled) {
      this->voice_present_.store(false, std::memory_order_relaxed);
    }
    needs_rebuild = (!enabled && this->all_features_disabled_());
  }

  if (needs_rebuild) {
    this->recreate_instance_(false);  // no-features path tears down inside
  }
  return true;
}

bool EspAfe::set_reinit_flag_(std::atomic<bool> &flag, bool enabled, const char *name) {
  if (flag.load(std::memory_order_relaxed) == enabled) {
    return true;
  }
  // ESP-SR can legitimately change the feed/fetch quantum when NS/AGC are
  // rebuilt. Accept that public contract and let frame_spec_revision_ make the
  // duplex task restart with the new shape.
  constexpr bool allow_frame_change = true;
  // AFE torn down (all-off) and a feature is coming back: commit the flag and
  // rebuild via recreate_instance_.
  if (this->afe_stopped_.load(std::memory_order_acquire)) {
    flag.store(enabled, std::memory_order_relaxed);
    if (!enabled) {
      return true;  // stays torn down
    }
    return this->recreate_instance_(false);
  }
  if (!this->is_initialized() || this->config_mutex_ == nullptr || this->feed_chunksize_ == 0 ||
      this->fetch_chunksize_ == 0) {
    // Pre-activation after setup should already be prepared. If this branch is
    // reached, the instance exists but runtime preparation failed or was torn
    // down; rebuild so frame_spec() and the concrete instance stay in the same
    // MR/MMR shape.
    if (this->afe_manager_ != nullptr &&
        this->config_mutex_ != nullptr && this->feed_chunksize_ > 0 &&
        this->fetch_chunksize_ > 0) {
      bool old_value = flag.load(std::memory_order_relaxed);
      flag.store(enabled, std::memory_order_relaxed);
      ESP_LOGI(TAG, "Applying %s=%s (pre-activation rebuild, frame_size_change=%s)",
               name, enabled ? "true" : "false", allow_frame_change ? "allowed" : "locked");
      if (this->recreate_instance_(!allow_frame_change)) {
        return true;
      }
      ESP_LOGW(TAG, "Failed to apply %s=%s before activation, rolling back",
               name, enabled ? "true" : "false");
      flag.store(old_value, std::memory_order_relaxed);
      if (!this->recreate_instance_(!allow_frame_change)) {
        ESP_LOGE(TAG, "Rollback also failed for %s, AFE is down", name);
      }
      return false;
    }
    // Before setup: commit immediately, build_instance_ will use it at setup.
    flag.store(enabled, std::memory_order_relaxed);
    ESP_LOGD(TAG, "Deferring %s=%s until AFE is initialized",
             name, enabled ? "true" : "false");
    return true;
  }
  // Staged config: set flag, rebuild, rollback on failure.
  // Flag must be set before rebuild because build_instance_ reads it.
  // The mutex in recreate_instance_ ensures process() either sees the old
  // instance or the new one, never a mix.
  //
  bool old_value = flag.load(std::memory_order_relaxed);
  flag.store(enabled, std::memory_order_relaxed);
  ESP_LOGI(TAG, "Applying %s=%s (rebuild, frame_size_change=%s)",
           name, enabled ? "true" : "false", allow_frame_change ? "allowed" : "locked");
  if (this->recreate_instance_(!allow_frame_change)) {
    return true;
  }
  // Rebuild failed: restore flag and try to rebuild with the previous config.
  ESP_LOGW(TAG, "Failed to apply %s=%s, rolling back", name, enabled ? "true" : "false");
  flag.store(old_value, std::memory_order_relaxed);
  if (!this->recreate_instance_(!allow_frame_change)) {
    ESP_LOGE(TAG, "Rollback also failed for %s, AFE is down", name);
  }
  return false;
}

void EspAfe::setup() {
  if (!this->recreate_instance_(false)) {
    this->mark_failed();
    return;
  }
  ESP_LOGI(TAG, "AFE setup complete, runtime prepared and idle (waiting for mic consumer)");
}

void EspAfe::dump_config() {
  ESP_LOGCONFIG(TAG, "ESP AFE (Audio Front End):");
  const char *type_name = (this->afe_type_ == AFE_TYPE_SR) ? "SR"
                          : (this->afe_type_ == 3)          ? "FD"
                                                             : "VC";
  ESP_LOGCONFIG(TAG, "  Type: %s", type_name);
  ESP_LOGCONFIG(TAG, "  Mode: %s", this->afe_mode_ == AFE_MODE_LOW_COST ? "LOW_COST" : "HIGH_PERF");
  ESP_LOGCONFIG(TAG, "  Microphones: transport=%d, afe=%d", this->mic_num_, this->afe_mic_channels_());
  ESP_LOGCONFIG(TAG, "  AEC: %s (filter_length=%d, nlp=%s)",
                this->aec_enabled_.load(std::memory_order_relaxed) ? "ON" : "OFF",
                this->aec_filter_length_, aec_nlp_level_name(this->aec_nlp_level_));
  ESP_LOGCONFIG(TAG, "  AEC scratch: ~12 KB internal (always allocated, "
                "live-toggle via esp-sr vtable; off-at-boot still pays the cost)");
  ESP_LOGCONFIG(TAG, "  NS: %s (WebRTC)",
                this->ns_enabled_.load(std::memory_order_relaxed) ? "ON" : "OFF");
  ESP_LOGCONFIG(TAG, "  VAD: %s (mode=%d, speech=%dms, noise=%dms, delay=%dms)",
                this->vad_enabled_.load(std::memory_order_relaxed) ? "ON" : "OFF",
                this->vad_mode_, this->vad_min_speech_ms_,
                this->vad_min_noise_ms_, this->vad_delay_ms_);
  ESP_LOGCONFIG(TAG, "  Continuous VAD Background Input: %s", this->continuous_vad_ ? "ON" : "OFF");
  ESP_LOGCONFIG(TAG, "  AGC: %s (gain=%ddB, target=-%ddBFS)",
                this->agc_enabled_.load(std::memory_order_relaxed) ? "ON" : "OFF",
                this->agc_compression_gain_, this->agc_target_level_);
  if (this->mic_num_ >= 2) {
    ESP_LOGCONFIG(TAG, "  Speech Enhancement: %s",
                  this->is_se_enabled() ? "ON (structural dual-mic)" : "OFF");
  } else {
    ESP_LOGCONFIG(TAG, "  Speech Enhancement: unavailable (mic_num < 2)");
  }
  ESP_LOGCONFIG(TAG, "  Input format override: %s",
                this->input_format_override_[0] ? this->input_format_override_ : "auto");
  ESP_LOGCONFIG(TAG, "  Alloc: %s, linear_gain=%.2f", this->memory_alloc_mode_to_str_(), this->afe_linear_gain_);
  ESP_LOGCONFIG(TAG, "  SE Task: core=%d, priority=%d, ringbuf=%d",
                this->task_core_, this->task_priority_, this->ringbuf_size_);
  ESP_LOGCONFIG(TAG, "  GMF Manager: feed core=%d prio=%d stack=%d, fetch core=%d prio=%d stack=%d",
                this->feed_task_core_, this->feed_task_priority_, this->feed_task_stack_size_,
                this->fetch_task_core_, this->fetch_task_priority_, this->fetch_task_stack_size_);
  ESP_LOGCONFIG(TAG, "  Process: %d samples, Feed: %d samples, Fetch: %d samples, Channels: %d",
                this->process_chunksize_, this->feed_chunksize_, this->fetch_chunksize_, this->total_channels_);
  ESP_LOGCONFIG(TAG, "  Initialized: %s", this->is_initialized() ? "YES" : "NO");
}

FrameSpec EspAfe::frame_spec() const {
  FrameSpec spec;
  spec.sample_rate = 16000;
  spec.mic_channels = this->afe_mic_channels_();
  spec.ref_channels = 1;
  // While running, use the live instance sizes. While torn down (all-off) the
  // live values are zero; fall back to the last successfully-installed spec
  // so consumers keep a stable frame shape while output is silenced.
  int live_in = this->process_chunksize_ > 0 ? this->process_chunksize_ : this->fetch_chunksize_;
  int live_out = this->fetch_chunksize_;
  spec.input_samples = live_in > 0 ? live_in : this->last_spec_process_size_;
  spec.output_samples = live_out > 0 ? live_out : this->last_spec_fetch_size_;
  return spec;
}

FeatureControl EspAfe::feature_control(AudioFeature feature) const {
  switch (feature) {
    case AudioFeature::AEC:
      return FeatureControl::LIVE_TOGGLE;
    case AudioFeature::VAD:
      return FeatureControl::LIVE_TOGGLE;
    case AudioFeature::NS:
    case AudioFeature::AGC:
      return FeatureControl::RESTART_REQUIRED;
    case AudioFeature::SE:
      return this->mic_num_ >= 2 ? FeatureControl::BOOT_ONLY : FeatureControl::NOT_SUPPORTED;
    default:
      return FeatureControl::NOT_SUPPORTED;
  }
}

bool EspAfe::set_feature(AudioFeature feature, bool enabled) {
  switch (feature) {
    case AudioFeature::AEC: return enabled ? this->enable_aec() : this->disable_aec();
    case AudioFeature::SE:  return false;
    case AudioFeature::NS:  return enabled ? this->enable_ns()  : this->disable_ns();
    case AudioFeature::VAD: return enabled ? this->enable_vad() : this->disable_vad();
    case AudioFeature::AGC: return enabled ? this->enable_agc() : this->disable_agc();
    default: return false;
  }
}

ProcessorTelemetry EspAfe::telemetry() const {
  ProcessorTelemetry t;
  t.voice_present = this->voice_present_.load(std::memory_order_relaxed);
  t.input_volume_dbfs = this->input_volume_dbfs_.load(std::memory_order_relaxed);
  t.output_rms_dbfs = this->output_rms_dbfs_.load(std::memory_order_relaxed);
  t.ringbuf_free_pct = this->ringbuf_free_pct_.load(std::memory_order_relaxed);
  t.glitch_count = this->glitch_count_.load(std::memory_order_relaxed);
  t.frame_count = this->frame_count_.load(std::memory_order_relaxed);
  t.input_ring_drop = this->input_ring_drop_.load(std::memory_order_relaxed);
  t.feed_ok = this->feed_ok_.load(std::memory_order_relaxed);
  t.feed_rejected = this->feed_rejected_.load(std::memory_order_relaxed);
  t.fetch_ok = this->fetch_ok_.load(std::memory_order_relaxed);
  t.fetch_timeout = this->fetch_timeout_.load(std::memory_order_relaxed);
  t.output_ring_drop = this->output_ring_drop_.load(std::memory_order_relaxed);
  t.feed_queue_frames = this->feed_queue_frames_.load(std::memory_order_relaxed);
  t.feed_queue_peak = this->feed_queue_peak_.load(std::memory_order_relaxed);
  t.fetch_queue_frames = this->fetch_queue_frames_.load(std::memory_order_relaxed);
  t.fetch_queue_peak = this->fetch_queue_peak_.load(std::memory_order_relaxed);
  t.process_us_last = this->process_us_last_.load(std::memory_order_relaxed);
  t.process_us_max = this->process_us_max_.load(std::memory_order_relaxed);
  t.feed_us_last = this->feed_us_last_.load(std::memory_order_relaxed);
  t.feed_us_max = this->feed_us_max_.load(std::memory_order_relaxed);
  t.fetch_us_last = this->fetch_us_last_.load(std::memory_order_relaxed);
  t.fetch_us_max = this->fetch_us_max_.load(std::memory_order_relaxed);
  t.feed_stack_high_water = this->feed_stack_high_water_last_.load(std::memory_order_relaxed);
  t.fetch_stack_high_water = this->fetch_stack_high_water_last_.load(std::memory_order_relaxed);
  return t;
}

bool EspAfe::reconfigure(int type, int mode) {
  int old_type = this->afe_type_;
  int old_mode = this->afe_mode_;
  this->afe_type_ = type;
  this->afe_mode_ = mode;
  if (this->recreate_instance_(false)) {
    const char *type_name = (this->afe_type_ == AFE_TYPE_SR) ? "SR"
                            : (this->afe_type_ == 3)          ? "FD"
                                                               : "VC";
    ESP_LOGI(TAG, "AFE reconfigured: type=%s, mode=%s", type_name,
             this->afe_mode_ == AFE_MODE_LOW_COST ? "LOW_COST" : "HIGH_PERF");
    return true;
  }
  // Rollback on failure: restore the previous config and rebuild to avoid leaving
  // the DSP permanently non-functional.
  ESP_LOGW(TAG, "reconfigure: new type=%d mode=%d build failed, rolling back to type=%d mode=%d",
           type, mode, old_type, old_mode);
  this->afe_type_ = old_type;
  this->afe_mode_ = old_mode;
  if (!this->recreate_instance_(false)) {
    ESP_LOGE(TAG, "reconfigure: rollback rebuild ALSO failed - AFE is DOWN");
  }
  return false;
}

bool EspAfe::process(const int16_t *in_mic, const int16_t *in_ref, int16_t *out,
                     uint8_t mic_channels_in) {
  const int transport_mic_channels = std::max<int>(1, mic_channels_in);
  int qs = this->process_chunksize_ > 0 ? this->process_chunksize_ : this->fetch_chunksize_;
  int os = this->fetch_chunksize_;
  if (out == nullptr) {
    this->glitch_count_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  if (in_mic == nullptr) {
    if (os > 0) {
      memset(out, 0, static_cast<size_t>(os) * sizeof(int16_t));
    }
    this->glitch_count_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  // Fast path when user has disabled every AFE feature: the instance is torn
  // down, but the caller still expects a frame-shaped output. Emit silence
  // rather than leaking raw pre-AFE mic audio to MWW, VA or intercom.
  if (this->afe_stopped_.load(std::memory_order_acquire)) {
    int pos = this->last_spec_fetch_size_ > 0 ? this->last_spec_fetch_size_ : os;
    silence_frame(out, pos);
    return false;
  }
  if (!this->is_initialized()) {
    silence_frame(out, os);
    return false;
  }

#if ESP_AFE_TIMING_TELEMETRY
  const int64_t process_start_us = esp_timer_get_time();
  auto finish_process_timing = [this, process_start_us]() {
    uint32_t elapsed_us = static_cast<uint32_t>(std::max<int64_t>(0, esp_timer_get_time() - process_start_us));
    this->process_us_last_.store(elapsed_us, std::memory_order_relaxed);
    update_peak_atomic(this->process_us_max_, elapsed_us);
  };
#else
  auto finish_process_timing = []() {};
#endif

  // Drain protocol entry: mark busy before observing drain flag. The release
  // on busy is paired with the acquire on drain_request_ in the writer
  // (recreate_instance_), so either the writer sees busy=true and waits, or
  // we see drain_request_=true and bail. We cannot see both false/false and
  // then observe a torn instance.
  this->process_busy_.store(true, std::memory_order_release);
  if (this->drain_request_.load(std::memory_order_acquire)) {
    this->process_busy_.store(false, std::memory_order_release);
    silence_frame(out, os);
    finish_process_timing();
    return false;
  }

  const int afe_mic_channels = this->afe_mic_channels_();
  qs = this->process_chunksize_ > 0 ? this->process_chunksize_ : this->fetch_chunksize_;
  os = this->fetch_chunksize_;
  int fs = this->feed_chunksize_;
  if (qs <= 0 || os <= 0 || fs <= 0 || this->feed_buf_ == nullptr) {
    silence_frame(out, os);
    this->process_busy_.store(false, std::memory_order_release);
    finish_process_timing();
    return false;
  }

  // Step 1: stage new input and feed it to AFE when a full frame is assembled.
  int offset = this->staged_input_samples_;
  if (offset + qs > fs) {
    ESP_LOGW(TAG, "AFE staging overflow (%d + %d > %d), dropping staged input", offset, qs, fs);
    offset = 0;
  }
  // Drop any partial frame staged with a different channel count: mixing
  // two layouts in feed_buf_ would feed the AFE garbage if a caller changes
  // the transport channel layout without recreating the instance first.
  if (this->last_process_mic_channels_ != 0 &&
      this->last_process_mic_channels_ != transport_mic_channels && offset > 0) {
    ESP_LOGD(TAG, "process(): mic_channels_in changed %d -> %d, resetting staged input",
             this->last_process_mic_channels_, transport_mic_channels);
    offset = 0;
  }
  this->last_process_mic_channels_ = transport_mic_channels;

  // Input RMS: compute on the transport mic before feeding the AFE pipeline.
  // Replaces data_volume (always 0 without WakeNet). Pass stride so we
  // read mic1 samples only when the transport delivers interleaved
  // channels (otherwise the RMS mixes mic1 + mic2 + reference and the
  // dBFS sensor reports nonsense).
  if (this->input_volume_sensor_enabled_ && !this->warmup_remaining_ && qs > 0) {
    this->input_volume_dbfs_.store(
        compute_rms_dbfs_i16(in_mic, static_cast<size_t>(qs),
                             static_cast<size_t>(transport_mic_channels)),
        std::memory_order_relaxed);
  }

  const bool warmup_active = this->warmup_remaining_ > 0;

  const int tc = this->total_channels_;
  int16_t *dst = this->feed_buf_ + offset * tc;
  stage_afe_input_frame(dst, in_mic, in_ref, qs, transport_mic_channels,
                        afe_mic_channels, tc);
  offset += qs;

  if (offset == fs) {
    if (this->warmup_remaining_ > 0) {
      this->warmup_remaining_--;
    }
    // Enqueue the full frame into the NOSPLIT ring. GMF's feed task pulls it
    // through manager_read_(). Non-blocking send: if the ring is full we drop
    // the frame instead of stalling the realtime I2S task.
    size_t feed_bytes = static_cast<size_t>(fs) * this->total_channels_ * sizeof(int16_t);
    if (this->feed_input_ring_ != nullptr) {
      if (!xRingbufferSend(this->feed_input_ring_, this->feed_buf_, feed_bytes, 0)) {
        this->input_ring_drop_.fetch_add(1, std::memory_order_relaxed);
      } else {
        uint32_t queued = this->feed_queue_frames_.fetch_add(1, std::memory_order_relaxed) + 1;
        update_peak_atomic(this->feed_queue_peak_, queued);
      }
    }
    offset = 0;
  }
  this->staged_input_samples_ = offset;

  // Step 2: try to pull a processed frame that the fetch task has pushed into
  // our side of the bridge. Non-blocking: if nothing is ready we emit silence,
  // never raw pre-AFE mic. MWW, VA and intercom must only see processed AFE
  // output while this component is active.
  size_t output_bytes = static_cast<size_t>(os) * sizeof(int16_t);
  bool processed = false;
  if (this->fetch_output_ring_) {
    size_t got = this->fetch_output_ring_->read(reinterpret_cast<uint8_t *>(out), output_bytes, 0);
    if (got == output_bytes) {
      processed = true;
      decrement_if_nonzero(this->fetch_queue_frames_);
    }
  }
  if (!processed) {
    silence_frame(out, os);
    if (!warmup_active) {
      this->glitch_count_.fetch_add(1, std::memory_order_relaxed);
    }
  }

  // Release drain guard BEFORE emitting telemetry / counters: those touch
  // only atomics on this and can safely race with a rebuild.
  this->process_busy_.store(false, std::memory_order_release);

  // Output-side RMS depends on the samples handed to the caller. VAD, input
  // VAD, input volume and ringbuf_free_pct are written by manager_result_()
  // when GMF fetches a frame, so this function only refreshes output RMS.
  if (processed && this->output_rms_sensor_enabled_) {
    this->output_rms_dbfs_.store(compute_rms_dbfs_i16(out, os), std::memory_order_relaxed);
  }
  this->frame_count_.fetch_add(1, std::memory_order_relaxed);
  finish_process_timing();
  return processed;
}

bool EspAfe::reinit_by_name(const std::string &name) {
  return this->reinit_by_name(name.c_str());
}

bool EspAfe::reinit_by_name(const char *name) {
  if (name == nullptr) {
    ESP_LOGW(TAG, "Unknown AFE mode: (null)");
    return false;
  }
  for (const auto &preset : AFE_MODE_PRESETS) {
    if (std::strcmp(name, preset.name) == 0) {
      return this->reconfigure(preset.type, preset.mode);
    }
  }

  ESP_LOGW(TAG, "Unknown AFE mode: %s", name);
  return false;
}

bool EspAfe::enable_aec() { return this->set_aec_enabled_runtime_(true); }
bool EspAfe::disable_aec() { return this->set_aec_enabled_runtime_(false); }
bool EspAfe::enable_ns() { return this->set_reinit_flag_(this->ns_enabled_, true, "ns_enabled"); }
bool EspAfe::disable_ns() { return this->set_reinit_flag_(this->ns_enabled_, false, "ns_enabled"); }
bool EspAfe::enable_vad() { return this->set_vad_enabled_runtime_(true); }
bool EspAfe::disable_vad() { return this->set_vad_enabled_runtime_(false); }
bool EspAfe::enable_agc() { return this->set_reinit_flag_(this->agc_enabled_, true, "agc_enabled"); }
bool EspAfe::disable_agc() { return this->set_reinit_flag_(this->agc_enabled_, false, "agc_enabled"); }

int32_t EspAfe::manager_read_cb_(void *buffer, int buf_sz, void *user_ctx, uint32_t ticks) {
  auto *self = static_cast<EspAfe *>(user_ctx);
  return self == nullptr ? 0 : self->manager_read_(buffer, buf_sz, ticks);
}

void EspAfe::manager_result_cb_(afe_fetch_result_t *result, void *user_ctx) {
  auto *self = static_cast<EspAfe *>(user_ctx);
  if (self != nullptr) {
    self->manager_result_(result);
  }
}

int32_t EspAfe::manager_read_(void *buffer, int buf_sz, uint32_t ticks) {
  if (buffer == nullptr || buf_sz <= 0 || this->feed_input_ring_ == nullptr) {
    return 0;
  }
  if (!this->processing_active_.load(std::memory_order_acquire)) {
    memset(buffer, 0, static_cast<size_t>(buf_sz));
    return buf_sz;
  }

  size_t item_size = 0;
  void *item = xRingbufferReceive(this->feed_input_ring_, &item_size, ticks);
  if (item == nullptr) {
    this->feed_rejected_.fetch_add(1, std::memory_order_relaxed);
    return 0;
  }

  decrement_if_nonzero(this->feed_queue_frames_);
  int32_t ret = 0;
  if (item_size == static_cast<size_t>(buf_sz)) {
#if ESP_AFE_TIMING_TELEMETRY
    const int64_t feed_start_us = esp_timer_get_time();
#endif
    memcpy(buffer, item, item_size);
#if ESP_AFE_TIMING_TELEMETRY
    uint32_t feed_us = static_cast<uint32_t>(std::max<int64_t>(0, esp_timer_get_time() - feed_start_us));
    this->feed_us_last_.store(feed_us, std::memory_order_relaxed);
    update_peak_atomic(this->feed_us_max_, feed_us);
#endif
    this->feed_ok_.fetch_add(1, std::memory_order_relaxed);
    ret = buf_sz;
  } else {
    ESP_LOGW(TAG, "AFE manager read size mismatch (%u != %d)",
             static_cast<unsigned>(item_size), buf_sz);
    this->feed_rejected_.fetch_add(1, std::memory_order_relaxed);
  }

  vRingbufferReturnItem(this->feed_input_ring_, item);
  return ret;
}

void EspAfe::manager_result_(afe_fetch_result_t *result) {
  if (!this->processing_active_.load(std::memory_order_acquire)) {
    return;
  }
  // Match esp_gmf_afe_result_proc: data_size is the valid audio gate.
  // ret_value is an AFE state (noise/speech/wake), not an esp_err_t.
  if (result == nullptr || result->data == nullptr || result->data_size <= 0) {
    this->fetch_timeout_.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  this->fetch_ok_.fetch_add(1, std::memory_order_relaxed);
  this->ringbuf_free_pct_.store(result->ringbuff_free_pct, std::memory_order_relaxed);

  const int result_samples = static_cast<int>(result->data_size / sizeof(int16_t));
  if (result_samples > 0 && result_samples != this->fetch_chunksize_) {
    const int old_fetch = this->fetch_chunksize_;
    this->fetch_chunksize_ = result_samples;
    this->process_chunksize_ = result_samples;
    this->last_spec_process_size_ = result_samples;
    this->last_spec_fetch_size_ = result_samples;
    uint32_t new_rev = this->frame_spec_revision_.fetch_add(1, std::memory_order_release) + 1;
    ESP_LOGI(TAG, "GMF AFE result frame size changed: fetch=%d->%d (revision %u)",
             old_fetch, result_samples, (unsigned) new_rev);
  }

  if (this->vad_enabled_.load(std::memory_order_relaxed)) {
    const bool new_voice = result->vad_state == VAD_SPEECH;
    const bool prev_voice = this->voice_present_.exchange(new_voice, std::memory_order_relaxed);
    if (new_voice != prev_voice) {
      ESP_LOGD(TAG, "VAD transition: %s -> %s (state=%d)",
               prev_voice ? "speech" : "silence", new_voice ? "speech" : "silence",
               static_cast<int>(result->vad_state));
    }
  }

  if (!this->fetch_output_ring_) {
    return;
  }

  if (result->vad_cache_size > 0 && result->vad_cache != nullptr) {
    const size_t cache_bytes = static_cast<size_t>(result->vad_cache_size);
    size_t cache_wrote = this->fetch_output_ring_->write_without_replacement(
        result->vad_cache, cache_bytes, pdMS_TO_TICKS(5), false);
    if (cache_wrote != cache_bytes) {
      this->output_ring_drop_.fetch_add(1, std::memory_order_relaxed);
    }
  }

  const size_t want = static_cast<size_t>(result->data_size);
  const int16_t *src = result->data;
  if (this->is_se_enabled() && !this->aec_enabled_.load(std::memory_order_relaxed)) {
    const int out_samples = static_cast<int>(want / sizeof(int16_t));
    constexpr int raw_channel = 1;
    if (raw_channel >= 0 && result->raw_data != nullptr &&
        raw_channel < result->raw_data_channels && this->fetch_raw_select_scratch_ == nullptr) {
      this->fetch_raw_select_scratch_ = static_cast<int16_t *>(
          heap_caps_malloc(want, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
      if (this->fetch_raw_select_scratch_ == nullptr) {
        ESP_LOGW(TAG, "AFE output source fallback: raw-select scratch alloc failed (%u bytes)",
                 (unsigned) want);
      }
    }
    if (raw_channel >= 0 && result->raw_data != nullptr &&
        raw_channel < result->raw_data_channels && this->fetch_raw_select_scratch_ != nullptr) {
      const int stride = result->raw_data_channels;
      const int16_t *raw = result->raw_data + raw_channel;
      int16_t *dst = this->fetch_raw_select_scratch_;
      for (int i = 0; i < out_samples; i++) {
        dst[i] = raw[i * stride];
      }
      src = dst;
    }
  }
  size_t wrote = this->fetch_output_ring_->write_without_replacement(src, want,
                                                                     pdMS_TO_TICKS(5), false);
  if (wrote != want) {
    this->output_ring_drop_.fetch_add(1, std::memory_order_relaxed);
  } else {
    uint32_t queued = this->fetch_queue_frames_.fetch_add(1, std::memory_order_relaxed) + 1;
    update_peak_atomic(this->fetch_queue_peak_, queued);
  }
}

bool EspAfe::activate_manager_() {
  if (this->afe_manager_ == nullptr) {
    return false;
  }
  esp_gmf_err_t ret = esp_gmf_afe_manager_set_read_cb(
      this->afe_manager_, &EspAfe::manager_read_cb_, this);
  if (ret != ESP_GMF_ERR_OK) {
    ESP_LOGW(TAG, "GMF AFE manager resume failed (ret=%d)", static_cast<int>(ret));
    return false;
  }
  return true;
}

void EspAfe::suspend_manager_() {
  if (this->afe_manager_ != nullptr) {
    esp_gmf_afe_manager_set_read_cb(this->afe_manager_, nullptr, nullptr);
    esp_gmf_afe_manager_suspend(this->afe_manager_, true);
  }
  this->drain_feed_input_ring_();
  if (this->fetch_output_ring_) {
    this->fetch_output_ring_->reset();
  }
  this->feed_queue_frames_.store(0, std::memory_order_relaxed);
  this->fetch_queue_frames_.store(0, std::memory_order_relaxed);
}

void EspAfe::flush_manager_before_suspend_() {
  if (this->afe_manager_ == nullptr || this->feed_input_ring_ == nullptr ||
      this->feed_buf_ == nullptr || this->feed_chunksize_ <= 0 || this->total_channels_ <= 0 ||
      this->afe_stopped_.load(std::memory_order_acquire)) {
    return;
  }

  const size_t feed_bytes = static_cast<size_t>(this->feed_chunksize_) *
                            this->total_channels_ * sizeof(int16_t);
  memset(this->feed_buf_, 0, feed_bytes);
  if (xRingbufferSend(this->feed_input_ring_, this->feed_buf_, feed_bytes, pdMS_TO_TICKS(10))) {
    uint32_t queued = this->feed_queue_frames_.fetch_add(1, std::memory_order_relaxed) + 1;
    update_peak_atomic(this->feed_queue_peak_, queued);
  }

  // esp_gmf_afe_manager's fetch task blocks in esp-sr fetch(). Give the feed
  // task one short window to consume the zero frame so fetch can return before
  // we suspend or destroy the manager.
  vTaskDelay(pdMS_TO_TICKS(40));
}

bool EspAfe::prepare_feed_input_ring_() {
  if (this->afe_manager_ == nullptr ||
      this->feed_chunksize_ <= 0) {
    return false;
  }

  if (this->feed_input_ring_ == nullptr) {
    // NOSPLIT ring: xRingbufferSend is atomic per-item; Receive returns a
    // complete frame. kBridgeRingFrames capacity absorbs BSS jitter without
    // memory bloat. Each NOSPLIT item adds an 8-byte header, included in sizing.
    const size_t frame_bytes = static_cast<size_t>(this->feed_chunksize_) *
                               this->total_channels_ * sizeof(int16_t);
    const size_t ring_size = (frame_bytes + kRingbufferItemHeaderBytes) * kBridgeRingFrames;
    const uint32_t feed_ring_caps = this->feed_ring_in_psram_
        ? (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
        : (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    this->feed_input_ring_storage_ = static_cast<uint8_t *>(heap_caps_malloc(ring_size, feed_ring_caps));
    if (this->feed_input_ring_storage_ == nullptr && this->feed_ring_in_psram_) {
      ESP_LOGW(TAG, "feed_input_ring (%u bytes) fell back to internal RAM (PSRAM full/unavailable)",
               (unsigned) ring_size);
      this->feed_input_ring_storage_ = static_cast<uint8_t *>(
          heap_caps_malloc(ring_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    if (this->feed_input_ring_storage_ == nullptr) {
      ESP_LOGE(TAG, "Failed to allocate AFE feed input ring storage (%u bytes)",
               (unsigned) ring_size);
      return false;
    }
    this->feed_input_ring_struct_ = static_cast<StaticRingbuffer_t *>(
        heap_caps_malloc(sizeof(StaticRingbuffer_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (this->feed_input_ring_struct_ == nullptr) {
      heap_caps_free(this->feed_input_ring_storage_);
      this->feed_input_ring_storage_ = nullptr;
      ESP_LOGE(TAG, "Failed to allocate AFE feed input ring struct");
      return false;
    }
    this->feed_input_ring_ = xRingbufferCreateStatic(
        ring_size, RINGBUF_TYPE_NOSPLIT,
        this->feed_input_ring_storage_, this->feed_input_ring_struct_);
    if (this->feed_input_ring_ == nullptr) {
      heap_caps_free(this->feed_input_ring_storage_);
      this->feed_input_ring_storage_ = nullptr;
      heap_caps_free(this->feed_input_ring_struct_);
      this->feed_input_ring_struct_ = nullptr;
      ESP_LOGE(TAG, "Failed to create AFE feed input ring");
      return false;
    }
    ESP_LOGI(TAG, "Feed input ring: %u bytes (%u per frame, %u slots, NOSPLIT)",
             (unsigned) ring_size, (unsigned) frame_bytes, (unsigned) kBridgeRingFrames);
  }
  return true;
}

void EspAfe::set_processing_active(bool active) {
  if (active) {
    bool was = this->processing_active_.exchange(true, std::memory_order_acq_rel);
    if (was) return;
    if (this->afe_stopped_.load(std::memory_order_acquire)) {
      ESP_LOGI(TAG, "AFE active requested while stopped; manager will resume when a feature is enabled");
      return;
    }
    if (!this->activate_manager_()) {
      ESP_LOGW(TAG, "AFE active: GMF manager failed to resume, staying idle");
      this->processing_active_.store(false, std::memory_order_release);
      return;
    }
    ESP_LOGI(TAG, "AFE active: GMF manager resumed");
  } else {
    if (!this->processing_active_.exchange(false, std::memory_order_acq_rel)) return;
    if (!this->afe_stopped_.load(std::memory_order_acquire)) {
      this->flush_manager_before_suspend_();
      this->suspend_manager_();
      ESP_LOGI(TAG, "AFE idle: GMF manager suspended (no mic consumers)");
    } else {
      ESP_LOGI(TAG, "AFE idle: stopped manager remains torn down");
    }
  }
}

void EspAfe::drain_feed_input_ring_() {
  if (this->feed_input_ring_ == nullptr) {
    this->feed_queue_frames_.store(0, std::memory_order_relaxed);
    return;
  }
  uint32_t dropped = 0;
  while (true) {
    size_t item_size = 0;
    void *item = xRingbufferReceive(this->feed_input_ring_, &item_size, 0);
    if (item == nullptr) {
      break;
    }
    dropped++;
    vRingbufferReturnItem(this->feed_input_ring_, item);
  }
  this->feed_queue_frames_.store(0, std::memory_order_relaxed);
  if (dropped > 0) {
    this->feed_rejected_.fetch_add(dropped, std::memory_order_relaxed);
    ESP_LOGD(TAG, "Dropped %u stale AFE feed frame(s) while stopping", (unsigned) dropped);
  }
}

bool EspAfe::prepare_fetch_output_ring_() {
  if (this->afe_manager_ == nullptr ||
      this->fetch_chunksize_ <= 0) {
    return false;
  }

  if (!this->fetch_output_ring_) {
    // kBridgeRingFrames of headroom (~128 ms at 16 kHz / 512-sample fetch) absorb
    // consumer-side jitter when process() is preempted by higher-priority
    // tasks. Placement is YAML-controlled: internal saves ~6.8 us/frame on
    // Core 0 read, PSRAM saves ~4 KB internal RAM (set fetch_ring_in_psram).
    const size_t frame_bytes = static_cast<size_t>(this->fetch_chunksize_) * sizeof(int16_t);
    this->fetch_output_ring_ = this->fetch_ring_in_psram_
        ? audio_processor::create_prefer_psram(frame_bytes * kBridgeRingFrames, "esp_afe.fetch_output_ring")
        : audio_processor::create_internal(frame_bytes * kBridgeRingFrames, "esp_afe.fetch_output_ring");
    if (!this->fetch_output_ring_) {
      ESP_LOGE(TAG, "Failed to allocate AFE fetch output ring buffer");
      return false;
    }
  }

  return true;
}

bool EspAfe::prepare_runtime_() {
  if (this->afe_stopped_.load(std::memory_order_acquire)) {
    return true;
  }
  this->log_memory_snapshot_("before_afe_prepare_runtime");
  if (!this->prepare_feed_input_ring_()) {
    return false;
  }
  if (!this->prepare_fetch_output_ring_()) {
    return false;
  }
  this->log_memory_snapshot_("after_afe_prepare_runtime");
  ESP_LOGI(TAG, "AFE runtime prepared (feed/fetch rings allocated, GMF tasks suspended)");
  return true;
}

void EspAfe::release_runtime_buffers_() {
  this->fetch_output_ring_.reset();
  if (this->fetch_raw_select_scratch_ != nullptr) {
    heap_caps_free(this->fetch_raw_select_scratch_);
    this->fetch_raw_select_scratch_ = nullptr;
  }
  this->feed_input_ring_ = nullptr;
  if (this->feed_input_ring_storage_ != nullptr) {
    heap_caps_free(this->feed_input_ring_storage_);
    this->feed_input_ring_storage_ = nullptr;
  }
  if (this->feed_input_ring_struct_ != nullptr) {
    heap_caps_free(this->feed_input_ring_struct_);
    this->feed_input_ring_struct_ = nullptr;
  }
}

EspAfe::~EspAfe() {
  // Quiesce: acquire mutex to ensure process() is not mid-frame.
  if (this->config_mutex_ != nullptr) {
    {
      audio_processor::ScopedLock lock(this->config_mutex_, pdMS_TO_TICKS(500));
      AfeInstance instance = this->detach_instance_();
      this->destroy_instance_(&instance);
      this->release_runtime_buffers_();
    }
    vSemaphoreDelete(this->config_mutex_);
    this->config_mutex_ = nullptr;
  } else {
    AfeInstance instance = this->detach_instance_();
    this->destroy_instance_(&instance);
    this->release_runtime_buffers_();
  }
}

}  // namespace esp_afe
}  // namespace esphome

#endif  // USE_ESP32
