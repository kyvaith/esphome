#include "session_test_support.h"
#include "session_test_speaker.h"

// Test access only; compile and exercise the production component methods unchanged.
#define protected public
#include "esphome/components/mixer/speaker/mixer_speaker.h"
#undef protected

#include <cstdio>

void *operator new(size_t size, const std::nothrow_t &) noexcept {
  if (host::fail_nothrow_after == 0) {
    host::fail_nothrow_after = -1;
    return nullptr;
  }
  if (host::fail_nothrow_after > 0)
    host::fail_nothrow_after--;
  return std::malloc(size);
}

using namespace esphome;
using namespace esphome::mixer_speaker;

struct Output : speaker::Speaker {
  std::function<void()> finishing;
  size_t play(const uint8_t *, size_t size, TickType_t) override { return size; }
  size_t play(const uint8_t *data, size_t size) override { return play(data, size, 0); }
  void start() override { state_ = speaker::STATE_RUNNING; }
  void stop() override { state_ = speaker::STATE_STOPPED; }
  void finish() override {
    stop();
    if (finishing)
      finishing();
  }
  bool has_buffered_data() const override { return false; }
};

struct Fixture {
  Output output;
  MixerSpeaker mixer;
  SourceSpeaker source;
  Fixture() {
    mixer.set_output_speaker(&output);
    mixer.set_output_channels(1);
    mixer.set_output_bits_per_sample(16);
    mixer.set_queue_mode(true);
    mixer.init_source_speakers(1);
    mixer.add_source_speaker(&source);
    mixer.setup();
    source.set_parent(&mixer);
    source.set_buffer_duration(1);
    source.set_audio_stream_info(audio::AudioStreamInfo(16, 1, 48000));
    source.setup();
  }
  void start() {
    source.start();
    source.loop();
    assert(source.source_published_ && source.state_ == speaker::STATE_RUNNING);
  }
  void stop() {
    source.stop();
    source.loop();
  }
  ~Fixture() {
    host::writing = host::destroying = host::delaying = host::pcm = {};
    assert(source.source_leases_ == 0);
    stop();
    source.loop();
    assert(source.audio_source_ == nullptr && source.ring_buffer_ == nullptr);
  }
};

static const uint8_t SAMPLE[] = {1, 2, 3, 4, 5, 6};

void two_leases(bool producer_first) {
  Fixture f;
  f.start();
  auto producer = f.source.acquire_source_();
  audio::AudioStreamInfo captured;
  auto consumer = f.source.acquire_source_(&captured);
  assert(captured == audio::AudioStreamInfo(16, 1, 48000));
  assert(f.source.source_leases_ == 2);
  assert(producer.ring()->write_without_replacement(SAMPLE, 4) == 4);
  f.source.expose_source_data_(*consumer.reader(), captured);
  assert(consumer.reader()->available() == 4);
  const auto returned = host::items_returned;
  const auto deleted = host::rings_deleted;
  f.stop();
  assert(!f.source.acquire_source_() && !f.source.source_published_);
  host::now += 6000;
  f.source.loop();  // Even the forced output STOP timeout must not free a held session.
  assert(host::rings_deleted == deleted && f.source.state_ == speaker::STATE_STOPPING);
  f.source.set_audio_stream_info(audio::AudioStreamInfo(24, 1, 24000));
  f.source.start();
  if (producer_first)
    producer = {};
  else
    consumer = {};
  f.source.loop();
  assert(f.source.source_leases_ == 1 && host::rings_deleted == deleted);
  assert(host::items_returned == returned);  // The reader's outstanding item remains owned.
  if (producer_first)
    consumer = {};
  else
    producer = {};
  assert(host::rings_deleted == deleted);  // Lease release does not run destructors.
  host::destroying = [&]() {
    assert(host::locks == 0 && f.source.source_retiring_);
    assert(f.source.start_() == ESP_ERR_INVALID_STATE);
    assert(!f.source.acquire_source_());
  };
  f.source.loop();
  host::destroying = {};
  assert(host::rings_deleted == deleted + 1 && host::items_returned == returned + 1);
  assert(f.source.state_ == speaker::STATE_STOPPED);
  f.source.loop();  // The queued start gets a fresh session only after complete retirement.
  assert(f.source.source_published_);
  auto next = f.source.acquire_source_(&captured);
  assert(captured == audio::AudioStreamInfo(24, 1, 24000));
  assert(next.reader()->available() == 0 && next.ring()->available() == 0);
}

void blocking_producer() {
  Fixture f;
  f.start();
  const auto deleted = host::rings_deleted;
  host::writing = [&]() {
    assert(host::locks == 0 && f.source.source_leases_ == 1);
    f.stop();
    f.source.start();
    host::now += 6000;
    f.source.loop();
    assert(f.source.state_ == speaker::STATE_STOPPING && host::rings_deleted == deleted);
  };
  assert(f.source.play(SAMPLE, sizeof(SAMPLE), 100) == sizeof(SAMPLE));
  host::writing = {};
  assert(f.source.source_leases_ == 0 && host::rings_deleted == deleted);
  f.source.loop();
  assert(host::rings_deleted == deleted + 1);
  f.source.loop();
  assert(f.source.source_published_ && f.source.ring_buffer_->available() == 0);
}

void move_and_vector() {
  Fixture f;
  f.start();
  auto a = f.source.acquire_source_();
  auto b = f.source.acquire_source_();
  a = std::move(a);
  assert(f.source.source_leases_ == 2 && a);
  a = std::move(b);
  assert(f.source.source_leases_ == 1 && a && !b);
  {
    FixedVector<SourceSpeaker::SourceLease> leases;
    leases.init(2);
    leases.push_back(std::move(a));
    leases.push_back(f.source.acquire_source_());
    assert(f.source.source_leases_ == 2 && !a);
    leases.clear();
    assert(f.source.source_leases_ == 0);
    leases.push_back(f.source.acquire_source_());
  }
  assert(f.source.source_leases_ == 0);
  f.source.source_leases_ = UINT16_MAX;
  assert(!f.source.acquire_source_());
  f.source.source_leases_ = 0;
}

void allocation_failures() {
  Fixture f;
  for (int failure = 0; failure < 4; failure++) {
    const auto live = host::rings_created - host::rings_deleted;
    host::fail_nothrow_after = failure == 0 ? 0 : (failure == 2 ? 1 : -1);
    host::fail_storage = failure == 1;
    host::fail_ring_init = failure == 3;
    assert(f.source.start_() == ESP_ERR_NO_MEM);
    host::fail_nothrow_after = -1;
    host::fail_storage = host::fail_ring_init = false;
    assert(!f.source.acquire_source_() && !f.source.source_published_);
    assert(f.source.audio_source_ == nullptr && f.source.ring_buffer_ == nullptr);
    assert(host::rings_created - host::rings_deleted == live);
  }
  f.mixer.set_queue_mode(false);
  f.mixer.audio_stream_info_ = audio::AudioStreamInfo(16, 1, 24000);
  const auto live = host::rings_created - host::rings_deleted;
  assert(f.source.start_() == ESP_ERR_INVALID_ARG);
  assert(host::rings_created - host::rings_deleted == live);
  assert(!f.source.source_published_);
  f.mixer.set_queue_mode(true);
  f.start();
}

void borrowed_and_owned() {
  using audio::RingBufferAudioSource;
  auto ring = ring_buffer::RingBuffer::create(16);
  assert(!RingBufferAudioSource::create_borrowed(nullptr, 4, 2));
  assert(!RingBufferAudioSource::create_borrowed(ring.get(), 0, 2));
  assert(!RingBufferAudioSource::create_borrowed(ring.get(), 4, 0));
  assert(!RingBufferAudioSource::create_borrowed(ring.get(), 4, 9));
  auto reader = RingBufferAudioSource::create_borrowed(ring.get(), 16, 2);
  assert(ring->write_without_replacement(SAMPLE, 3) == 3);
  assert(reader->fill(0) == 2 && reader->available() == 2);
  const auto returned = host::items_returned;
  const auto deleted = host::rings_deleted;
  reader.reset();  // Includes an acquired item and trailing sub-frame bytes.
  assert(host::items_returned == returned + 1 && host::rings_deleted == deleted);
  assert(ring->write_without_replacement(SAMPLE, 4) == 4);
  ring.reset();
  assert(host::rings_deleted == deleted + 1);
  std::shared_ptr<ring_buffer::RingBuffer> owned = ring_buffer::RingBuffer::create(16);
  host::fail_nothrow_after = 0;
  assert(!RingBufferAudioSource::create(owned, 16, 2));
  assert(owned.use_count() == 1);
  auto owned_reader = RingBufferAudioSource::create(owned, 16, 2);
  owned->write_without_replacement(SAMPLE, 4);
  owned_reader->fill(0);
  const auto before_owned = host::rings_deleted;
  owned.reset();
  assert(host::rings_deleted == before_owned);
  owned_reader.reset();
  assert(host::rings_deleted == before_owned + 1);
}

void mixer_pass(bool queue) {
  Fixture f;
  f.mixer.set_queue_mode(queue);
  f.start();
  assert(f.source.play(SAMPLE, 4, 0) == 4);
  const auto deleted = host::rings_deleted;
  unsigned mixed = 0;
  host::pcm = [&]() {
    mixed++;
    assert(f.source.source_leases_ == 1);
    f.stop();
    assert(!f.source.acquire_source_() && host::rings_deleted == deleted);
    xEventGroupSetBits(f.mixer.event_group_, 2);  // Stop at the next outer task iteration.
  };
  try {
    MixerSpeaker::audio_mixer_task(&f.mixer);
    assert(false);
  } catch (const host::Parked &) {
  }
  host::pcm = {};
  assert(mixed == 1 && f.source.source_leases_ == 0 && host::rings_deleted == deleted);
  f.source.loop();
  assert(host::rings_deleted == deleted + 1);
}

void empty_mixer_pass() {
  Fixture f;
  f.start();
  host::delaying = [&]() {
    assert(f.source.source_leases_ == 0);  // Empty acquired source is released before task waits.
    xEventGroupSetBits(f.mixer.event_group_, 2);
  };
  try {
    MixerSpeaker::audio_mixer_task(&f.mixer);
  } catch (const host::Parked &) {
  }
  host::delaying = {};
  assert(f.source.source_leases_ == 0);
}

void two_source_mix() {
  Fixture f;
  SourceSpeaker second;
  f.mixer.set_queue_mode(false);
  f.mixer.source_speakers_.init(2);
  f.mixer.add_source_speaker(&f.source);
  f.mixer.add_source_speaker(&second);
  second.set_parent(&f.mixer);
  second.set_buffer_duration(1);
  second.set_audio_stream_info(audio::AudioStreamInfo(16, 1, 48000));
  second.setup();
  f.start();
  second.start();
  second.loop();
  f.source.play(SAMPLE, 4, 0);
  second.play(SAMPLE, 4, 0);
  unsigned mixed = 0;
  const auto deleted = host::rings_deleted;
  host::pcm = [&]() {
    mixed++;
    assert(f.source.source_leases_ == 1 && second.source_leases_ == 1);
    f.stop();
    second.stop();
    second.loop();
    assert(host::rings_deleted == deleted && !f.source.acquire_source_() && !second.acquire_source_());
    xEventGroupSetBits(f.mixer.event_group_, 2);
  };
  try {
    MixerSpeaker::audio_mixer_task(&f.mixer);
  } catch (const host::Parked &) {
  }
  host::pcm = {};
  assert(mixed == 1 && f.source.source_leases_ == 0 && second.source_leases_ == 0);
  f.source.loop();
  second.loop();
  assert(host::rings_deleted == deleted + 2);
}

void queued_rate_switch_wait() {
  Fixture f;
  f.start();
  f.source.play(SAMPLE, 4, 0);
  f.output.set_audio_stream_info(audio::AudioStreamInfo(16, 1, 24000));
  f.output.start();
  unsigned waits = 0;
  f.output.finishing = [&]() {
    waits++;
    assert(f.source.source_leases_ == 1);
    f.stop();
    xEventGroupSetBits(f.mixer.event_group_, 2);
  };
  try {
    MixerSpeaker::audio_mixer_task(&f.mixer);
  } catch (const host::Parked &) {
  }
  assert(waits == 1 && f.source.source_leases_ == 0);
  f.output.finishing = {};
  f.source.loop();
  assert(f.source.ring_buffer_ == nullptr);
}

void finish_and_timeout(bool finish) {
  Fixture f;
  f.start();
  auto held = f.source.acquire_source_();
  if (finish) {
    f.source.finish();
  } else {
    f.source.set_timeout(1);
    host::now += 2;
  }
  f.source.loop();
  assert(f.source.state_ == speaker::STATE_STOPPING && !f.source.acquire_source_());
  f.source.loop();
  assert(f.source.ring_buffer_ != nullptr);
  held = {};
  f.source.loop();
  assert(f.source.is_stopped() && f.source.ring_buffer_ == nullptr);
}

int main() {
  two_leases(true);
  two_leases(false);
  blocking_producer();
  move_and_vector();
  allocation_failures();
  borrowed_and_owned();
  mixer_pass(true);
  mixer_pass(false);
  empty_mixer_pass();
  two_source_mix();
  queued_rate_switch_wait();
  finish_and_timeout(true);
  finish_and_timeout(false);
  assert(host::rings_created == host::rings_deleted && host::locks == 0);
  puts("PASS actual mixer session lifetime: allocation failures, producer/mixer overlap, retirement, restart, owned "
       "API");
}
