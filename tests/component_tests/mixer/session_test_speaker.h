#pragma once
#include "session_test_support.h"
#include "esphome/components/audio/audio.h"
namespace esphome::speaker {
enum State : uint8_t { STATE_STOPPED, STATE_STARTING, STATE_RUNNING, STATE_STOPPING };
class Speaker {
 public:
  virtual ~Speaker() = default;
  virtual size_t play(const uint8_t *, size_t, TickType_t) = 0;
  virtual size_t play(const uint8_t *data, size_t length) = 0;
  virtual void start() = 0;
  virtual void stop() = 0;
  virtual void finish() { stop(); }
  virtual bool has_buffered_data() const = 0;
  virtual void set_pause_state(bool) {}
  virtual bool get_pause_state() const { return false; }
  virtual void set_mute_state(bool mute) { mute_state_ = mute; }
  virtual bool get_mute_state() { return mute_state_; }
  virtual void set_volume(float volume) { volume_ = volume; }
  virtual float get_volume() { return volume_; }
  bool is_stopped() const { return state_ == STATE_STOPPED; }
  void set_audio_stream_info(const audio::AudioStreamInfo &info) { audio_stream_info_ = info; }
  const audio::AudioStreamInfo &get_audio_stream_info() const { return audio_stream_info_; }
  template<typename T> void add_audio_output_callback(T &&) {}

 public:  // Test adapter exposes the ordinary speaker state for assertions.
  State state_{STATE_STOPPED};
  audio::AudioStreamInfo audio_stream_info_;
  std::function<void(uint32_t, int64_t)> audio_output_callback_ = [](uint32_t, int64_t) {};
  bool mute_state_{};
  float volume_{1};
};
}  // namespace esphome::speaker
