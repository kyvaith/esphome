# ESP AFE - Full Audio Front-End Pipeline

> ⚠ **Important: `esp_afe` requires `i2s_audio_duplex` in front of it.** It is **not** a drop-in alternative to `esp_aec` for `intercom_api` standalone setups (dual-bus MEMS + amp without a codec). The AFE pipeline expects fixed 512-sample 16 kHz frames at a steady cadence, which only `i2s_audio_duplex` produces. If you use `intercom_api` without `i2s_audio_duplex`, set `processor_id:` to an `esp_aec` component, not `esp_afe`. See [docs/reference.md](../../../docs/reference.md#audio-processing-components) for the full topology matrix.

ESPHome component wrapping Espressif's **ESP-SR AFE** (Audio Front End)
through `esp_gmf_afe_manager`. Provides a complete audio processing pipeline
(AEC, Speech Enhancement on dual-mic targets, and optional NS/VAD/AGC stages)
in a single component, with runtime AEC control and diagnostic sensors.
Supports single-mic (MR) and dual-mic (MMR/MMNR) configurations.

## Overview

`esp_afe` uses the closed-source `esp-sr` library's AFE pipeline, which chains multiple DSP stages depending on configuration:

**Single-mic (MR) mode** (`mic_num: 1`):
```
[mic + ref] -> |AEC| -> |NS| -> |VAD| -> |AGC| -> [clean output]
```

**Dual-mic (MMR/MMNR) mode** (`se_enabled: true` with `mic_num: 2`):
```
[mic1 + mic2 + ref] -> |AEC| -> |Speech Enhancement| -> [clean output]
```

> **Note**: When Speech Enhancement is active, esp-sr prioritizes BSS over NS.
> `afe_config_check()` may clear `ns_init` on dual-mic builds. AGC can be
> configured at boot, but the stock GMF manager does not expose it as a live
> feature, so AGC changes require AFE reinit. The public dual-mic packages keep
> AGC disabled and do not expose an AGC switch.

Unlike `esp_aec` (standalone echo cancellation only), `esp_afe` provides a full signal processing pipeline. Both components implement the `AudioProcessor` interface, but they are **only** drop-in replacements behind `i2s_audio_duplex`. With standalone `intercom_api` (no duplex driver), use `esp_aec`: the AFE feed/fetch task model needs the steady producer that `i2s_audio_duplex` provides and that the standalone intercom path does not.

### When to use esp_afe vs esp_aec

| Feature | esp_aec | esp_afe |
|---------|---------|---------|
| Echo Cancellation | Yes | Yes |
| Speech Enhancement | No | Yes (dual-mic) |
| Noise Suppression | No | Yes (WebRTC, single-mic mode) |
| Voice Activity Detection | No | Yes (WebRTC) |
| Automatic Gain Control | No | Yes (WebRTC, when kept by `afe_config_check`) |
| Runtime switches in HA | AEC only | AEC and VAD. NS is single-mic only; AGC is not exposed by public dual-mic packages; SE/BSS is structural |
| Diagnostic sensors | No | Input volume, output RMS, voice presence |
| CPU usage (SR LOW_COST) | ~22% Core 0 | ~23% Core 0 (8.4% feed + 15% fetch) |
| Internal RAM overhead | ~80 KB | ~100 KB (MR), ~120 KB (MMR with Speech Enhancement) |
| Supported platforms | ESP32-S3, ESP32-P4 | ESP32-S3, ESP32-P4 |

**Choose `esp_aec`** when you need minimal RAM usage and only echo cancellation.
**Choose `esp_afe`** when you want Espressif AFE processing, VAD, optional NS/AGC, and diagnostic sensors.

## Requirements

- **ESP32-S3** or **ESP32-P4** with PSRAM
- ESP-IDF framework
- `i2s_audio_duplex` in front of the processor. `intercom_api` can use the
  processed mic only when it sits behind the duplex driver.

## Installation

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/n-IA-hane/esphome-intercom
      ref: main
    components: [audio_processor, i2s_audio_duplex, esp_afe]
```

> **Note**: `audio_processor` must be listed in `components:` because it provides the shared `AudioProcessor` interface header. It is also auto-loaded by `esp_afe`, but ESPHome's `external_components` loader requires it to be explicitly listed.

## Configuration

### Basic Setup

```yaml
esp_afe:
  id: afe_processor
  type: sr
  mode: low_cost

i2s_audio_duplex:
  id: i2s_duplex
  # ... pins ...
  processor_id: afe_processor
```

### Complete Configuration

```yaml
esp_afe:
  id: afe_processor
  type: sr                    # sr (speech recognition) or vc (voice communication)
  mode: low_cost              # low_cost or high_perf
  mic_num: 2                  # Number of microphones (1 or 2)
  se_enabled: true            # Speech Enhancement, requires mic_num: 2
  aec_enabled: true           # Echo cancellation
  aec_filter_length: 4        # Echo tail in frames (4 = 64ms)
  aec_nlp_level: aggressive   # normal, aggressive, or very_aggressive
  ns_enabled: true            # Noise suppression (WebRTC)
  vad_enabled: false          # Voice activity detection
  vad_mode: 3                 # VAD aggressiveness (0-4, higher = more aggressive)
  vad_min_speech_ms: 128      # Min speech duration to trigger VAD
  vad_min_noise_ms: 1000      # Min noise duration before VAD clears
  vad_delay_ms: 128           # VAD state transition delay
  agc_enabled: true           # Automatic gain control (WebRTC)
  agc_compression_gain: 9     # AGC compression gain (0-30 dB)
  agc_target_level: 3         # AGC target level (0-31, lower = louder)
  memory_alloc_mode: more_psram  # Memory allocation strategy
  afe_linear_gain: 1.0        # Linear gain applied to output (0.1-10.0)
  task_core: 1                # FreeRTOS task core (0 or 1)
  task_priority: 5            # esp-sr SE worker priority (default 5)
  ringbuf_size: 8             # Internal ring buffer size in frames (default 8)
```

### Configuration Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `id` | ID | Required | Component ID |
| `type` | string | `sr` | AFE type: `sr` (speech recognition), `vc` (voice communication) or `fd` (full-duplex AFE, esp-sr 2.4+) |
| `mode` | string | `low_cost` | AFE mode: `low_cost` or `high_perf` |
| `mic_num` | int | `1` | Number of microphones (1 or 2). Dual-mic configs must enable `se_enabled`; SE/BSS is structural for two-mic AFE |
| `aec_enabled` | bool | **true** | Enable acoustic echo cancellation |
| `aec_filter_length` | int | `4` | AEC filter length in frames (1-8). 4 = 64ms tail, sufficient for most setups |
| `aec_nlp_level` | string | `aggressive` | ESP-SR nonlinear echo suppression level: `normal`, `aggressive`, or `very_aggressive`. Lower levels preserve near-end wake speech better while playback is active; higher levels suppress speaker leakage harder |
| `ns_enabled` | bool | **true** | Enable noise suppression (WebRTC engine) |
| `agc_enabled` | bool | **true** | Enable automatic gain control (WebRTC engine) |
| `se_enabled` | bool | **false** | Enable Speech Enhancement / spatial source separation. Required for `mic_num: 2`; dual-mic AFE treats SE/BSS as structural and does not expose a runtime SE switch |
| `vad_enabled` | bool | **false** | Enable voice activity detection |
| `vad_mode` | int | `3` | VAD aggressiveness (0-4). Higher = rejects more noise but may miss quiet speech |
| `vad_min_speech_ms` | int | `128` | Minimum speech duration to trigger voice detection (32-60000 ms) |
| `vad_min_noise_ms` | int | `1000` | Minimum noise duration before VAD clears (64-60000 ms) |
| `vad_delay_ms` | int | `128` | VAD state transition delay (0-60000 ms) |
| `vad_mute_playback` | bool | `false` | When VAD detects speech, mute the speaker output to prevent acoustic feedback during voice commands. Useful for voice-assistant pipelines that play TTS while still listening. |
| `vad_enable_channel_trigger` | bool | `false` | Per-channel VAD triggering (multi-mic setups). esp-sr exposes which mic channel detected the speech, useful for Speech Enhancement-aware downstream consumers. |
| `continuous_vad` | bool | `false` | Allow VAD to keep the microphone/AFE path active without an external consumer. Keep `false` for intercom/VA targets where MWW/VA own the mic lifecycle. |
| `agc_compression_gain` | int | `9` | AGC compression gain in dB (0-30) |
| `agc_target_level` | int | `3` | AGC target level (0-31, lower value = louder output) |
| `memory_alloc_mode` | string | `more_psram` | Memory allocation: `more_internal`, `internal_psram_balance`, `more_psram` |
| `afe_linear_gain` | float | `1.0` | Linear gain multiplier applied to output (0.1-10.0) |
| `task_core` | int | `1` | FreeRTOS task core affinity (0 or 1) |
| `task_priority` | int | `5` | Priority for the esp-sr SE/BSS worker task. Default follows Espressif's GMF AFE manager profile. |
| `ringbuf_size` | int | `8` | Internal ring buffer size in frames (2-32). Larger = more latency tolerance, more memory |
| `feed_buf_in_psram` | bool | `false` | Place the ~3 KB sample-interleave scratch buffer in PSRAM. Default internal saves ~41 us/frame on Core 0 (the buffer is written and re-read every audio frame). Set `true` on memory-constrained builds to free internal RAM at the cost of Core 0 PSRAM traffic. |
| `feed_ring_in_psram` | bool | `false` | Place the ~12 KB feed staging ring (I2S task to GMF feed task) in PSRAM. Default internal saves ~20 us/frame on Core 0 writes. Set `true` if internal RAM headroom is tight. |
| `fetch_ring_in_psram` | bool | `false` | Place the ~4 KB fetch output ring (GMF fetch task to I2S task) in PSRAM. Default internal saves ~6.8 us/frame on Core 0 reads. Set `true` if internal RAM headroom is tight. |

> **Buffer placement guidance**: defaults are tuned for the fastest Core 0
> audio path. Total internal cost when all three flags are `false` is about
> 19 KB. On S3 full-experience builds where AFE/MWW/TLS compete for internal
> RAM, the public S3/Spotpear YAMLs place the feed scratch, feed ring and fetch
> ring in PSRAM. P4 is intentionally different: the public P4 YAMLs keep those
> AFE staging buffers internal because previous P4 runtime tests showed the
> ESP-SR path is sensitive to that placement. Each flag remains independent for
> board-specific tuning. Cumulative Core 0 cost when all are `true` is about
> 68 us/frame on Octal PSRAM 80 MHz, lower bound.

> **Defaults are designed so that a minimal config already enables AEC + NS + AGC.** You only need to declare options that differ from the defaults. In particular:
> - `aec_enabled`, `ns_enabled`, `agc_enabled` are **true** by default. Only set them if you want to **disable** a feature.
> - `se_enabled` and `vad_enabled` are **false** by default. Set `se_enabled: true` for every dual-mic AFE target; set `vad_enabled: true` only when the product explicitly needs VAD active at boot.
> - `memory_alloc_mode` defaults to `more_psram`, `task_core` to `1`, `task_priority` to `5`. Override only if your hardware requires it.
>
> **Minimal single-mic** (AEC + NS + AGC out of the box):
> ```yaml
> esp_afe:
>   id: afe_processor
>   type: sr
>   mode: low_cost
> ```
>
> **Minimal dual-mic** (adds Speech Enhancement):
> ```yaml
> esp_afe:
>   id: afe_processor
>   type: sr
>   mode: low_cost
>   mic_num: 2
>   se_enabled: true
> ```
>
> For public dual-mic full-experience profiles, set `agc_enabled: false` and
> use `packages/esp_afe/dual_mic_entities.yaml`, which intentionally omits the
> AGC switch. Everything else (AEC, memory, task settings) uses sensible
> defaults and does not need to be repeated unless the target needs board
> tuning.

### AFE Type and Mode

The combination of `type` and `mode` determines the AEC engine and DSP pipeline:

| type + mode | AEC Engine | CPU (Core 0) | MWW Compatible | Use Case |
|-------------|-----------|-------------|----------------|----------|
| `sr` + `low_cost` | `esp_aec3` (linear, SIMD) | **~23%** | **Yes** (10/10) | VA + MWW + Intercom |
| `sr` + `high_perf` | `esp_aec3` (FFT) | ~25% | Yes | Not recommended (DMA memory on S3) |
| `vc` + `low_cost` | `dios_ssp_aec` (Speex) | ~60% | No (2/10) | VoIP without wake word |
| `vc` + `high_perf` | `dios_ssp_aec` | ~64% | No | VoIP without wake word |

> **Important**: Use `sr` + `low_cost` for Voice Assistant + MWW setups. The `vc` modes add a residual echo suppressor (RES) that distorts spectral features, reducing MWW detection from 10/10 to 2/10.

NS and AGC always use the WebRTC engine regardless of type/mode. They work at boot, but the stock GMF AFE manager does not publish NS/AGC feature toggles, so this component changes them through AFE reinit (see [Feature Toggle Behavior](#feature-toggle-behavior)).

## Platform Entities

### Switch Platform

Runtime control of AFE features via Home Assistant switches:

```yaml
switch:
  - platform: esp_afe
    esp_afe_id: afe_processor
    aec:
      name: "Echo Cancellation"
      restore_mode: RESTORE_DEFAULT_ON
    ns:
      name: "Noise Suppression"
      restore_mode: RESTORE_DEFAULT_ON
    vad:
      name: "Voice Activity Detector"
      restore_mode: ALWAYS_OFF
    agc:
      name: "Auto Gain Control"
      restore_mode: RESTORE_DEFAULT_ON
```

| Switch | Icon | Description |
|--------|------|-------------|
| `aec` | `mdi:ear-hearing` | Echo cancellation toggle (live, no audio gap) |
| `ns` | `mdi:volume-off` | Noise suppression toggle (requires AFE reinit, ~70ms gap). Use only on single-mic AFE builds; esp-sr prioritizes SE/BSS over NS on dual-mic input |
| `vad` | `mdi:account-voice` | Voice activity detection toggle (live via GMF AFE manager, no rebuild) |
| `agc` | `mdi:tune-vertical` | Auto gain control toggle (requires AFE reinit). Use only on single-mic or custom diagnostic builds whose checked runtime config keeps `agc_init: true`; public dual-mic packages omit it |

Use `ALWAYS_OFF` for VAD restore on full-experience intercom targets unless the
product explicitly wants always-listening VAD. VAD is useful at runtime, but a
restored ON state must not make the parent duplex component register a
background microphone consumer during boot. If a product really needs VAD to
own the background mic path, set `continuous_vad: true` explicitly.

Dual-mic packages may also expose a diagnostic `AFE Output Source (AEC Off)`
template select. This select does not change the feed sent to ESP-SR. The
dual-mic feed still contains both microphone channels plus the playback
reference. The select only chooses which mono output is forwarded after
`fetch()` when SE/BSS is active and AEC is off.

The naming here follows the public ESP-SR contract exactly:
`afe_fetch_result_t.data` is the target output, while
`afe_fetch_result_t.raw_data` is multi-channel output data. Espressif does not
document a stable semantic name for `raw_data` channel 0/1, so the user-facing
labels intentionally say only "AFE multi-output channel". They are not physical
microphone channels.

```
TDM / codec input
  mic 1 slot --------------+
  mic 2 slot --------------+--> i2s_audio_duplex --> ESP-SR feed frame
  speaker reference slot --+                         M M [N] R
                                                         |
                                                         v
                                                ESP-SR AFE pipeline
                                                SE/BSS + AEC/AGC/VAD
                                                         |
                                                         v
                                                afe_fetch_result_t
                                                         |
                    +------------------------------------+------------------------------------+
                    |                                    |                                    |
         data: ESP-SR target output          raw_data multi-output 1           raw_data multi-output 2
                    |                                    |                                    |
                    +-------------------------- selected mono output -------------------------+
                                                         |
                                                         v
                                             MWW / Voice Assistant / intercom
```

| Option | Meaning |
|--------|---------|
| `Auto (speaker leak check)` | Default. With AEC on, use ESP-SR target output. With SE/BSS active and AEC off, use `raw_data` channel 1 so disabling AEC exposes speaker leakage for diagnostics |
| `AFE multi-output 1` | With SE/BSS active and AEC off, use `afe_fetch_result_t.raw_data` channel 0 |
| `AFE multi-output 2` | With SE/BSS active and AEC off, use `afe_fetch_result_t.raw_data` channel 1 |

The select does not import private GMF helpers. It uses public ESP-SR
`afe_fetch_result_t.raw_data` fields and is intended for AEC-off diagnostics on
dual-mic hardware. Normal AEC-on operation keeps using `result->data`, no
matter which option is selected.

### Binary Sensor Platform

```yaml
binary_sensor:
  - platform: esp_afe
    esp_afe_id: afe_processor
    vad:
      name: "Voice Presence"
      update_interval: 100ms
```

| Sensor | Device Class | Description |
|--------|-------------|-------------|
| `vad` | `sound` | Voice activity state. ON when speech detected, OFF when noise/silence. Requires `vad_enabled: true` in config |

### Sensor Platform

```yaml
sensor:
  - platform: esp_afe
    esp_afe_id: afe_processor
    input_volume:
      name: "Input Volume"
      update_interval: 250ms
    output_rms:
      name: "Output RMS"
      update_interval: 250ms
```

| Sensor | Unit | Description |
|--------|------|-------------|
| `input_volume` | dBFS | RMS level of mic input before processing. Useful for mic gain calibration |
| `output_rms` | dBFS | RMS level of processed output. Compare with input to see NS/AGC effect |

## Actions

### esp_afe.set_mode

Change the AFE type and mode at runtime. The entire AFE pipeline is recreated (~70 ms gap).

```yaml
select:
  - platform: template
    id: afe_mode_select
    name: "AEC Mode"
    options:
      - sr_low_cost
      - sr_high_perf
      - voip_low_cost
      - voip_high_perf
      - fd_low_cost
      - fd_high_perf
    initial_option: "sr_low_cost"
    optimistic: false           # do NOT auto-publish; we publish the live mode below
    restore_value: true
    set_action:
      - esp_afe.set_mode:
          id: afe_processor
          mode: !lambda 'return x;'
      - lambda: 'id(afe_mode_select).publish_state(id(afe_processor).get_mode_name());'
```

Valid mode strings: `sr_low_cost`, `sr_high_perf`, `voip_low_cost`, `voip_high_perf`, `fd_low_cost`, `fd_high_perf`.

`get_mode_name()` returns the live mode as a string after the reinit. The `optimistic: false` plus the explicit `publish_state()` at the end is the recommended pattern: it stops `template_select::control()` from auto-publishing the user-selected value over a rejected switch (e.g. when `sr_high_perf` cannot allocate the contiguous DMA-capable internal block).

## Feature Toggle Behavior

AEC, VAD, and NS/AGC toggle differently because the stock GMF AFE
manager exposes only part of the lower ESP-SR runtime control surface:

| Feature | Toggle Method | Audio Gap | Notes |
|---------|-------------|-----------|-------|
| AEC | Live GMF manager call | None | Immediate on/off via `ESP_AFE_FEATURE_AEC` |
| SE | Boot-time graph choice | N/A | Structural on dual-mic builds; single-mic users should use a single-mic config or `esp_aec` |
| NS | AFE reinit | ~70ms plus possible audio-task restart | ESP-SR exposes low-level vtable entries, but `esp_gmf_afe_manager` does not expose an NS feature enum. Not exposed on dual-mic SE/BSS builds because `afe_config_check()` prioritizes BSS over NS |
| AGC | AFE reinit | ~70ms plus possible audio-task restart | Same manager limitation as NS. Not exposed by public dual-mic packages because toggling rebuilds the AFE graph and can disturb full-experience audio under load |
| VAD | Live GMF manager call | None | Immediate on/off via `ESP_AFE_FEATURE_VAD`; GMF resets VAD after each toggle |

**Why reinit for NS/AGC?** ESP-SR's low-level AFE vtable includes
`enable_ns()`, `disable_ns()`, `enable_agc()`, and `disable_agc()`, but the
stock GMF manager keeps the AFE iface/data private and only publishes runtime
feature toggles for WakeNet, VAD, AEC, and SE. Staying on the stock manager
therefore means NS/AGC changes are represented as config changes and require
destroying and rebuilding the AFE instance.

The reinit is safe: the previous AFE is destroyed first (ESP-SR's FFT resources are a global singleton, only one instance can exist), then the new one is built. While an AFE instance is active, missing processed output is emitted as silence rather than raw pre-AFE microphone audio.

## Architecture

```
               AudioProcessor interface
                       |
              +--------+--------+
              |                 |
           EspAec            EspAfe
         (AEC only)    (AEC+NS+VAD+AGC)
              |                 |
              +--------+--------+
                       |
            +----------+-----------+
            |                      |
    i2s_audio_duplex         intercom_api
    (processor_id)         (processor_id)
```

Both `EspAec` and `EspAfe` implement `AudioProcessor`. The consumer components (`i2s_audio_duplex` and `intercom_api`) call `process(mic, ref, out)` without knowing which implementation is behind it. The supported pairings are:

| Consumer | esp_aec | esp_afe |
|----------|---------|---------|
| `i2s_audio_duplex` | yes | yes |
| `intercom_api` standalone (no `i2s_audio_duplex`) | yes | **no** (the AFE feed/fetch tasks need the steady frames `i2s_audio_duplex` produces) |

### Internal Pipeline

```
feed() ----> AFE internal tasks ----> fetch()
  |           (FreeRTOS, Core 1)         |
  |                                      |
  mic + ref interleaved             clean mono output
  (512 samples * 2 channels)        (512 samples)
```

Espressif's GMF AFE manager owns the `feed()` and `fetch()` tasks. The wrapper
keeps `process()` realtime-safe by staging complete feed frames into a NOSPLIT
bridge ring and reading processed output from an ESPHome BYTEBUF ring without
blocking. GMF tasks stay alive and are suspended when no microphone consumer is
active; prepared rings and scratch buffers remain allocated while idle.
Fetch-output writes disable partial writes, so a full output span is either
queued intact or dropped; this keeps fixed-size consumer reads sample-aligned
after overflow.

## Complete Example

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/n-IA-hane/esphome-intercom
      ref: main
    components: [audio_processor, intercom_api, i2s_audio_duplex, esp_afe]

esp_afe:
  id: afe_processor
  type: sr
  mode: low_cost
  mic_num: 2                  # 2 for dual-mic Speech Enhancement (default: 1)
  se_enabled: true            # Speech Enhancement (requires mic_num: 2, default: false)
  vad_enabled: true           # Voice activity detection (default: false)
  agc_enabled: false          # public dual-mic profiles keep AGC out of HA/runtime controls

i2s_audio_duplex:
  id: i2s_duplex
  # ... I2S pins ...
  processor_id: afe_processor
  buffers_in_psram: true

# AFE switches
switch:
  - platform: esp_afe
    esp_afe_id: afe_processor
    aec:
      name: "Echo Cancellation"
    vad:
      name: "Voice Activity Detector"

# AFE diagnostic sensors
sensor:
  - platform: esp_afe
    esp_afe_id: afe_processor
    input_volume:
      name: "Input Volume"
    output_rms:
      name: "Output RMS"

# VAD binary sensor
binary_sensor:
  - platform: esp_afe
    esp_afe_id: afe_processor
    vad:
      name: "Voice Presence"

# Runtime mode switching
select:
  - platform: template
    name: "AEC Mode"
    options:
      - sr_low_cost
      - sr_high_perf
    initial_option: sr_low_cost
    set_action:
      - esp_afe.set_mode:
          id: afe_processor
          mode: !lambda 'return x;'
```

## Memory Usage

| Component | Internal RAM | PSRAM | Notes |
|-----------|-------------|-------|-------|
| AFE framework | ~55-70 KB | Variable | Closed-source esp-sr allocations, cannot be moved to PSRAM |
| Speech Enhancement | ~15-25 KB | Variable | Only allocated when `se_enabled: true` with `mic_num: 2` |
| AEC filter | ~20-30 KB | ~60 KB | Depends on filter_length and mode |
| NS (WebRTC) | ~10-15 KB | ~10 KB | Allocated even when SE replaces it at runtime |
| AGC (WebRTC) | ~2-3 KB | ~3 KB | |
| Feed buffer | selectable | selectable | 512-1024 * 2-3 channels * 2 bytes; controlled by `feed_buf_in_psram` |
| AFE internal ring | esp-sr owned | esp-sr owned | `ringbuf_size * frame_size` inside the closed-source esp-sr instance |
| Feed bridge ring | selectable | selectable | `kBridgeRingFrames` complete NOSPLIT feed frames; controlled by `feed_ring_in_psram` |
| Fetch output ring | selectable | selectable | `kBridgeRingFrames` mono output frames; controlled by `fetch_ring_in_psram` |

With `memory_alloc_mode: more_psram`, total internal RAM usage is approximately:
- **~100 KB** for MR mode (single-mic: AEC + NS + AGC)
- **~120 KB** for MMR mode (dual-mic: AEC + Speech Enhancement)
- **~80 KB** for standalone `esp_aec`

### IRAM Optimization (Critical for ESP32-S3)

On ESP32-S3, IRAM and DRAM share the same 512 KB of SRAM. Every KB of code placed in IRAM reduces available DRAM heap by 1 KB. With `CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y`, code runs from the PSRAM instruction cache, making IRAM placement unnecessary for many application functions.

**Keep PSRAM XIP enabled on full AFE builds:**

```yaml
esp32:
  framework:
    type: esp-idf
    sdkconfig_options:
      CONFIG_SPIRAM_FETCH_INSTRUCTIONS: "y"
      CONFIG_SPIRAM_RODATA: "y"
```

Do not disable Wi-Fi/PHY IRAM paths as a default memory shortcut on full
audio devices. The saved RAM is small compared to the latency and throughput
risk on the same network path that carries TTS/media, API and intercom traffic.

> **Tip**: Use ESPHome's native `debug` sensors (`free`, `block`, `min_free`,
> `fragmentation`, `psram`, `cpu_frequency`) for firmware-level diagnostics.
> Avoid permanent template heap sensors in standard YAMLs; use targeted runtime
> logs or `runtime_diag` only for short, low-level diagnostic sessions.

## Known Limitations

1. **Speech Enhancement replaces NS on dual-mic input**: With two microphone channels, `afe_config_check()` prioritizes SE/BSS over NS. SE/BSS is structural and is not a runtime toggle. Public dual-mic profiles keep AGC disabled and do not expose AGC controls because AGC changes require full AFE reinit.

2. **Runtime toggles**: AEC and VAD are toggled through the GMF manager without rebuilding. NS/AGC and type/mode changes still require a full AFE reinit.

3. **data_volume**: The AFE's built-in `data_volume` field is always 0.0 dB because it requires WakeNet to be active. Input/output RMS is computed locally instead.

4. **ESP-SR is closed source**: No API to move AFE internal allocations to PSRAM. The ~55-86 KB internal RAM overhead is unavoidable. Use the IRAM optimization options above to compensate.

5. **Single instance**: ESP-SR's FFT resources are a global singleton. Only one AFE (or AEC) instance can exist at a time.

## Troubleshooting

### AFE setup fails (NULL config)

Check logs for `afe_config_init returned NULL`. This means esp-sr couldn't initialize with the requested type/mode. Verify your `type` and `mode` combination is valid.

### High internal RAM usage / esp-aes: Failed to allocate memory

With AFE active, free internal RAM may drop to ~15 KB without IRAM optimization. This causes TLS failures (`esp-aes: Failed to allocate memory`) and WiFi heap corruption crashes. Solutions in order of impact:

1. **Apply IRAM optimization** (see [IRAM Optimization](#iram-optimization-critical-for-esp32-s3)) - frees ~30 KB
2. Set `memory_alloc_mode: more_psram`
3. Use a single-mic config or `esp_aec` if Speech Enhancement is not needed
4. Reduce `ringbuf_size` (minimum 2, default 8)
5. Consider using `esp_aec` instead if you don't need NS/AGC/VAD/SE

### Switch toggle has no effect

NS and AGC toggles require AFE reinit. AEC and VAD toggles are live through the GMF AFE manager. Public dual-mic packages do not expose NS or AGC toggles. If reinit is in progress, active AFE output is silenced instead of exposing raw pre-AFE microphone audio.

### Voice presence always OFF

Ensure `vad_enabled: true` in the `esp_afe` config. VAD is disabled by default.

### Audio gap when toggling NS/AGC

This is expected (~70ms). AEC and VAD can be toggled without audio interruption. Public dual-mic packages avoid NS/AGC runtime toggles.

## Logging

The component logs under the tag `esp_afe`.

- `WARN` - GMF manager resume/toggle failures, AFE setup returned NULL config, esp-sr allocation failures, mode-switch rebuild failure
- `INFO` - `AFE active: GMF manager resumed`, `AFE idle: GMF manager suspended`, `AEC/VAD enabled/disabled via GMF AFE manager`, and rebuild lifecycle messages for runtime mode switches
- `DEBUG` - bridge feed/fetch instrumentation (only when `i2s_audio_duplex.telemetry: true`), per-stage enable/disable acks

To mute AFE chatter without losing project-wide DEBUG: `logger.logs.esp_afe: INFO`.

## License

MIT License
