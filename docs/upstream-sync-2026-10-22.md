# ESPHome upstream reconciliation — 2026-10-22

This records the source audit and integration decisions for the Atmosfera ESPHome fork.
It is an integration checkpoint, not a claim that the complete firmware or every
upstream component has passed hardware validation. The validation table below must be
updated with the final build and device evidence before publication is described as
validated.

**Candidate 18 has completed the bounded native and device validation recorded
here, including matrix 13, the measured scene comparisons and five controlled
warm restart/real-Voice cycles.** Cold-power testing was declined; provider
credits prevent successful remote-response/acoustic coverage. Higher reported
LVGL-handler wall accumulation and lower memory headroom remain explicit
limitations, with no claim of global absence of regressions. Exact remote refs,
the workflow pin and canonical promotion are verified separately from device
evidence.
It contains the narrowly scoped redundant Lottie
backing-allocation correction described below. Candidate 17 was an intermediate
build with an earlier guard and was never flashed.

**Candidate 16 remains REJECTED.** Matrix 12 completes its
machine cases and restoration, but its full log contains twenty-one repeated
weather Lottie display-back-buffer allocation failures after Camera closes.
Still images and zero observed DSI underruns do not establish a working live
weather epoch. The allocation lifecycle requires correction and fresh validation.

**Candidate 15 remains REJECTED.** Matrix 11 stops during the
synthetic Voice case after a real DSI bridge underrun at 11:27:58.984889 UTC,
device tick 137278. Its seven earlier cases and seventeen saved images do not
constitute a complete matrix; Gallery, Camera, language and final restoration
were not accepted. The cause is under investigation and no diagnostic counter
or acceptance gate has been relaxed.
Candidate 16 adds opt-in preference-sync timing diagnostics only. It does not
change flash policy or establish a fix for the underrun. Its native and linked
artifact checks pass, but its later weather-allocation failure blocks hardware
acceptance.

**Candidate 14 remains REJECTED.** Matrix 10 passes its twelve
main machine cases but fails the final preference-restoration gate: Home is
prepared while application previews remain incomplete, with eleven confirmed
JPEG owned-copy allocation failures. No final restored-Home capture is accepted.
Candidate 15 implements the codec ownership correction and a narrower Settings
admission policy, but its later display failure blocks acceptance.

**Candidate 13 remains REJECTED.** A real watchdog reset at
2026-10-22 10:34:39.782 UTC followed AFE activation at 10:34:37 UTC. Its cause
remains unproven; the earlier
boot, matrix 9, held-scroll and Camera successes do not cancel this failure.
Two later bounded Voice attempts on the same image did not reproduce the reset,
but do not clear it. Candidate 14 adds an opt-in backport of a concrete official
PIE context correction; its passing build and linked-instruction gates do not
cancel any preserved hardware failure.
These earlier images remain rejected. Candidate 18's bounded validation applies
only to the scenarios and limitations recorded below, without retrospectively
explaining or clearing those earlier failures.

## Immutable inputs and history

| Input | Identity |
| --- | --- |
| Published fork checkpoint | `62806b28b7f216a70974e06c6520ae73fecd7cb2` |
| Published product checkpoint | `2903b1cad70d11d6010ee1f7877f2054a7dee51f` |
| Common ancestor | `a210c27d141ea411e3a6bc2a776cff88799cefc5`, 2026-07-23 |
| Official default branch | `dev`, confirmed from GitHub's symbolic `HEAD` |
| First official integration target | `ddcc5874ed5db795f20002289358dbfaae67c54c`, 2026-10-22 06:17:59 UTC |
| First freeze refresh | `f7dc2ecb51efe5c28298389ccc2de763d452e1af`, 2026-10-22 07:02:25 UTC |
| Second freeze refresh | `491f98f982eb9e205046a4175f7972073451de2e`, 2026-10-22 07:43:14 UTC |
| Latest official target at freeze | `7dace44d61e73a05f71df657bbf18d82a3d45890`, 2026-10-22 08:48:28 UTC |
| ESPHome version at target | `2026.11.0-dev` |
| History | Full, not shallow; 2,904 official commits after the common ancestor |
| Source range | 7,397 official changed files; 423 paths changed by the original fork |
| Initial merge | `f076d67f71ddfbd679d75a95adc23559321456aa` |
| Follow-up official merge | `cc62f65a9b6084ab5a5b960e8908f3170a29970b` |
| Second source refresh merge | `723cd716194adfc6887d68a7c215d141c5690d4a` |
| Memory corrections through | `7ccaacc97a577620f0426fd4cb77b9d9a5a2b3c6` |
| Final source refresh merge | `7a59858e70`, ordinary merge of `7dace44d61` |
| Background snapshot ownership correction | `0ec0a3504d5db1309be0a8a923d75744ccd039a0` |
| Home JPEG pressure recovery | `82025d7e410ed42884cd74a995bc8bb130febe64` |
| Encoding memory diagnostics / rejected build 13 | `bf12117b1cc177b15994eb6249e267bc61d5fed7` |
| Default-off official PIE context backport / candidate 14 | `65e1ad41e2b9622286ba2db6bb92342bdb6c2147` |
| Product opt-in for candidate 14 | `311ef6488721049ab282baaa591ad953c19e0199` |
| Completed JPEG owned-copy correction / candidate 15 | `863c431e076c57ec3284b5b9ad2a5d0257116ef4` |
| Product Settings trim-before-loan policy / candidate 15 | `63c968bf27d616cf004efd07b258350a1eef70cc` |
| Default-off preference sync diagnostics / candidate 16 | `9c8c189111a1af3dbbe7b1790a6f3c4e280cf0fc` |
| Product diagnostics opt-in / candidate 16 | `461cf0578c4c19e54f2f1b38034e85b6afb7f5cb` |
| Direct opaque Lottie backing correction / candidate 18 | `87e77c353b5979eeb7f44074c229585a2f20c4ea` |

The official repository does not have a `main` branch. In this report, the user's
request for the main branch means its actual default development branch, `dev`.
The integration is a real merge of the official target and the published fork.
Neither official history nor the published fork is rebased, squashed, replaced by a
source archive, or force-pushed. Both input commits must remain ancestors of the
published integration commit.

The freeze refresh found one additional official commit, `f7dc2ecb51` (#7147),
which bridges an optional Wi-Fi WISP access point to an Ethernet upstream. It
touches seven paths and has no overlap with the original fork delta. It is
included by a second ordinary merge; the product does not configure this bridge.
Both that first refreshed official target and the published fork pass ancestor
checks at the follow-up merge.

A second refresh includes three further official commits through `491f98f982`:
the optional VNC client, 40 MHz timing for the Lilygo T-Display S3 Pro SPI panel,
and host-side `aioesphomeapi` 46.21.0. They are included by another ordinary merge.
The product configures neither VNC nor that SPI panel; its generated source and
2,294-unit native compile database include neither component. This refresh does
not modify active C++/headers, native manifests, SDK patches or the product
configuration. The final copied-source/hash audit must still match the completed
binary. The host Python environment is updated to the new API dependency.

A third refresh includes `7dace44d61` (#18882), which adds optional scalar API
action variables and validated defaults. Arrays remain required. The added wire
fields are advisory metadata: `ExecuteServiceRequest` still requires every
argument, so clients apply the defaults before sending. The product does not
declare optional action variables, leaving the extra optional-argument metadata
disabled. This change nevertheless touches active API code and generated
protocol classes; it requires a fresh native build and artifact verification.
The ordinary merge preserves both official and original fork ancestry.

Two accompanying inventories make the audit reproducible:

- [All 423 original fork paths](upstream-sync-2026-10-22-files.csv), including their
  baseline, fork and official Git blob identities, subsystem, same-path upstream
  changes and integration policy.
- [All 2,904 official commits](upstream-sync-2026-10-22-commits.csv), including full
  commit identity, timestamp, subject, affected subsystems and path count.

Eighty original fork paths also changed upstream. The inventory includes 120
vendored `libjpeg-turbo-esp32` paths. Retaining this existing decoder bundle does not
mean that ESPHome gained 120 independent downstream features. The complete history
is catalogued; substantive source review concentrates on the components used by the
product and the reusable interfaces on which downstream additions depend. This is
not a claim that every unrelated upstream sensor was run or that every one of the
2,904 commits was individually tested.

## What arrived upstream

Counts below are commits touching each subsystem and overlap when one commit changes
several subsystems. They must not be added together as a unique commit total.

| Subsystem | Official commits | Relevant changes and decision |
| --- | ---: | --- |
| Core | 293 | Safer task deletion, internal TCB storage, explicit allocation helpers, fixed controller dispatch, main-loop wake isolation, flash-resident metadata, scheduler fixes. Keep upstream. |
| ESP32 | 87 | P4 stack workaround, crash cause/address/build evidence, SDK component and TLS trimming, NVS cache placement, CPU frequency handling and native build support. Keep upstream and retain narrowly guarded SDK corrections. |
| ESP32 Hosted | 20 | New dependencies, explicit 2.x/3.x support, minimum IDF gate, P4 ESP-NOW shim and OTA watchdog fixes. Keep upstream transport configuration. |
| Native ESP-IDF tools | 53 | Direct CMake/Ninja invocation, component discovery cache and local registry mirrors, Windows path/PCH fixes and incremental build improvements. Keep upstream converter. |
| Native API | 113 | Safer connection allocation, overflow lifetime fixes, bounded entity-list sending, protocol improvements, outgoing connections, optional action-variable/default metadata and onboarding wizard. Keep upstream. |
| Wi-Fi / network / socket | 78 / 15 / 28 | Safe scans, interface priority and route changes, DHCP lease cache, optional provisioning and WISP-to-Ethernet bridging, reconnecting TCP abstractions and allocation handling. Keep upstream. |
| HTTP / HA | 25 / 13 | TLS capabilities, certificate bundle ownership, additional HA import platforms and shared attribute parsing. Keep upstream; adapt custom HTTP/websocket users. |
| LVGL / MIPI DSI / display | 42 / 16 / 17 | LVGL 9.6, codegen context isolation, animation fixes, negative text spacing, additional widgets, DPHY reference selection and display pixel format changes. Preserve these while porting direct rendering and ownership extensions. |
| Font / image / runtime / online image | 6 / 7 / 20 / 8 | Current font/schema behavior, image format and decode improvements, QOI, auto-detection, dimension checks and impossible-download failure handling. Keep upstream features alongside optional hardware JPEG. |
| Audio / I2S / microphone | 22 / 31 / 8 | Shared-clock standard full duplex, microphone resampling and common audio fixes. Adopt upstream without replacing the board's different TDM/reference topology. |
| Speaker / resampler / Sendspin | 21 / 10 / 39 | Safer task/rebuffer lifetimes, independent mute and volume, source/decoder improvements, codec preferences, pairing and artwork contract updates. Adopt upstream and port narrow downstream additions. |
| Wake word / native voice assistant | 14 / 18 | Official configurable wake buffer and task options, playback under load and refused-send handling. Adopt official implementations. |
| OTA / text sensor / LEDC | 19 / 16 / 7 | OTA encryption/compression and reliability, current text filters and output behavior. Preserve separate downstream sanitize/fade features. |

### Core, memory and native build changes

- `3926612281` ([#19048](https://github.com/esphome/esphome/pull/19048)) fixes
  deletion of a running `StaticTask`. `destroy()` and `deallocate()` now return
  `bool`; callers must retry after `false` before releasing or reusing task-owned
  state. The fork's older busy-wait replacement is discarded.
- `9df7e825d3` ([#19426](https://github.com/esphome/esphome/pull/19426)) keeps the
  TCB in separately allocated internal memory even if its owning component or
  task stack is in PSRAM. Keep the official representation and allocation-failure
  handling.
- `b36c5441cb` removes the old `loop_task_handle` global. Downstream task placement
  includes `core/main_task.h` and uses `esphome_main_task_handle`, preserving the
  previous uninitialized-handle fallback.
- `6a6d263c0d` ([#20326](https://github.com/esphome/esphome/pull/20326)) uses
  notification index 1 for the main loop instead of interfering with IDF waits on
  index 0. Keep `CONFIG_FREERTOS_TASK_NOTIFICATION_ARRAY_ENTRIES=2`.
- `bbd05e4811` ([#20181](https://github.com/esphome/esphome/pull/20181)) raises
  the P4 IDF main task stack to 8192 bytes. The product's old 4096-byte override
  must not undo this fix. This is separate from ESPHome's loop-task stack.
- `211fd3ce54` replaces virtual controller dispatch with generated direct calls.
  `23d13d3a6e` removes the implicit `alloc_helpers.h` re-export. Keep the complete
  official core/codegen interfaces rather than restoring old fragments to satisfy
  one downstream component.
- `b8ba8aa1a8` directly invokes CMake and Ninja; the old `espidf/runner.py` is gone.
  `44f08d15db` already fixes generated CMake path separators on Windows. The fork's
  narrower converter patch and old test expectation are therefore subsumed.
- Preserve the fork's native `-I` project compile-flag propagation in
  `framework_helpers.py`; it is still needed by the LVGL integration and retains
  the corresponding regression test.
- API improvements include allocation-failure disconnects (`e55a8aeabe`,
  `3361d031de`), overflow double-free prevention (`fb21320826`), and avoiding a
  watchdog-triggering entity-list stall (`9179fe26ce`). Keep these implementations.
- Crash reporting now records fault address, real panic type, per-core hints and
  capture build (`e0644ab20a`, `e8dadf2852`, `b115813fbe`, `0d596a20f8`). Old
  retained evidence must not be attributed to a new firmware build.

### SDK and Hosted versions are separate from the ESPHome version

The merged code is ESPHome `2026.11.0-dev`. Its official recommended SDK is now
ESP-IDF **5.5.6** (`7c6fac91ff`, #20987). The initial reconciliation build deliberately
pins **5.5.5**, keeping the existing firmware's reviewed SDK while integration
errors are separated from an SDK change. That checkpoint does not discard the
new ESPHome source or history. After the exact source review below, validators and
patchers accept **only 5.5.5 and 5.5.6**, with the same strict source identities.
The next candidate uses official recommended 5.5.6 and requires its own fresh
native build and hardware validation; old 5.5.5 evidence remains a rollback
checkpoint, not proof about the 5.5.6 candidate.

Official Hosted configuration and dependencies are adopted:

| Component | Old fork | Official target |
| --- | --- | --- |
| `espressif/esp_hosted` | 2.12.9 | 2.12.13 |
| `espressif/esp_wifi_remote` | 1.5.1 | 1.6.5 |
| `espressif/wifi_remote_over_eppp` | 0.3.2 | 0.3.3 |
| `espressif/eppp_link` | 1.1.5 | 1.1.5 |

`89cc689864` (#19621) adds separate 2.x and opt-in 3.x configuration paths;
`3f01f9895f` rejects SDKs below 5.3 because the old RX path can double-free;
`b67ac5d0be` (#21300) retains the extended watchdog timeout through coprocessor
OTA finalization. Do not restore the fork's old mixed-version Kconfig generation.

The optional shutdown extension still uses the reviewed Hosted 2.x reset API.
Enabling it together with Hosted 3.x is rejected; ordinary upstream 3.x
configurations remain valid when the extension is off.

Both pre-existing SDK fixes remain necessary on the 5.5.6 candidate:

- [Official 5.5.6 P4 reset source](https://raw.githubusercontent.com/espressif/esp-idf/v5.5.6/components/esp_system/port/soc/esp32p4/system_internal.c)
  still resets the other CPU before shared-cache writeback. It does not contain
  the fork's cache-sync idle guard.
- [Official 5.5.6 SDMMC source](https://raw.githubusercontent.com/espressif/esp-idf/v5.5.6/components/esp_driver_sdmmc/src/sdmmc_transaction.c)
  still overwrites a failed transaction result with the result of the final cache
  synchronization.

The exact raw 5.5.5 and 5.5.6 files were downloaded independently outside the
installed SDK cache. All six reviewed files are byte-identical:

| SDK owner/interface | Shared SHA-256 |
| --- | --- |
| `esp_system/port/soc/esp32p4/system_internal.c` | `7bd2af3da2a9c1592c8d9ff6f65ad7fe320690b2e480076685df068f231e40cc` |
| `esp_driver_sdmmc/src/sdmmc_transaction.c` | `7cea7d649edbfa655ab3c3a26ba1d3d25290841091b8900f9643559bdb1bcad6` |
| `esp_system/esp_system.c` shutdown callback owner | `d23069d0226093ab1ffdfa7ec62324628fcf3573c3ec261cc8890104a1ed7dfb` |
| `soc/esp32p4/register/hw_ver1/soc/cache_reg.h` | `2752f437891245d51b7f27d8c5afea7c5b1079337e876cc775a0d9a4f10470fc` |
| `soc/esp32p4/register/hw_ver3/soc/cache_reg.h` | `77576fded69d19ce075dc4286a04e14c636e1a099632c9bf36f3f9e7be40dedc` |
| `esp_rom/esp32p4/include/esp32p4/rom/cache.h` | `e2e659763b1414a47bf5306d7f8ee6e6e42d35367bc3541de6791fd16d003806` |

The P4 guarded postimage remains
`f1a00fcae4a68738006fbc7872346f7fab8be801023191e5aa2eadfe23478056`;
the SDMMC guarded postimage remains
`198a092a3975ae1ceb65d83f2f06755106f7d81ff927c8165f3df4f18754925b`.
The SDMMC installer also recognizes the exact already-fixed owner form
`74f5a0ee4c4f9fe54f88e0522dd40d885474564fa005a3ad8a43e130a3eda3ab`;
this does not claim that released 5.5.6 contains that fix. Unknown versions and
unknown source bytes are rejected before writing. Tests use frozen source
fixtures and temporary SDK directories, never the installed SDK cache.

Matching owner files and register/ROM definitions justify this narrow allow-list
extension. They do not establish that the whole SDK or final binary is unchanged.
Check fresh owner objects, compiler commands, link/IRAM placement and hardware.
The cache-idle wait has no timeout; permanently wedged hardware
can wait indefinitely. Its low-level reset path is shared with panic restart,
while the GPIO shutdown callback is orderly-restart-only. Existing orderly
restart tests do not establish panic-recovery behavior.

The MIPI/JPEG framework patch also required a precise 5.5.6 adaptation. Official
`jpeg_decode.c` now attempts to dequeue a pending DMA2D transaction before forcing
an in-flight one to end on decode failure. The patch preserves that entire
upstream error-cleanup sequence and adds the existing diagnostic accounting around
it. It does not replace the improved upstream cleanup with the older 5.5.5 block.
The newly recognized branch is restricted to 5.5.6 and exact normalized source
identities. Its CSC-prepared input SHA-256 is
`cff5a26e22678538c1219e3e09ee12aa0641974175155c1ae89601202b241465`;
its final JPEG decoder postimage is
`1bcc2689b87c89705ed2b737b93a051d70900886252a30e07f130cbd58cbdd4a`.

The full framework patch was replayed against 16 frozen SDK owner files outside
the installed cache. Pristine and previously partially patched 5.5.6 inputs
produce identical final owners. Both 5.5.5 and 5.5.6 runs are byte-idempotent on a
second invocation. The installed 5.5.5 rollback SDK remains unchanged; its JPEG
postimage is reproduced exactly. Fifteen of its 16 complete replay postimages
match the saved rollback bytes, with only two blank-line locations differing in
`dw_gdma.c`. The patcher is also safe to import with unrelated command-line
arguments, while retaining both native CLI and SCons entry points.

### Preserved Voice watchdog and opt-in official PIE context correction

Build 13's first real Voice campaign started the single-microphone AFE fetch task
and processing pipeline before a hardware watchdog reset. The continuous UART
slice records `HP_SYS_HP_WDT_RESET` at 10:34:39.782 UTC, approximately 2.5 seconds
after activation. It contains no printed stack-overflow diagnosis, panic text or
backtrace. Decoding the saved PCs against the exact build 13 ELF places core 0
in the panic handler's busy wait and core 1 in `rtos_save_pie_coproc`, at the
128-bit Q4 save instruction. That identifies the saved execution location, not
the former owner's runtime save-area pointer or the initiating fault.

Two subsequent one-attempt diagnostics on the unchanged image, with ten seconds
and zero seconds of wake-word idle respectively, activated AFE and completed
cleanup without another observed reset. Both reached the configured backend's
explicit exhausted-credit response. They establish those two bounded lifecycle
outcomes only: they neither cancel the earlier watchdog failure nor prove a
successful conversation, acoustic full duplex or numeric AEC/reference behavior.
The first failure, both subsequent reports and their screenshots remain preserved.

The independent core audit found consistent task ABI in the linked FreeRTOS,
port and AFE translation units. The notification array change from one to two
entries accounts for `StaticTask_t` growing from 352 to 356 bytes; the PIE pointer
fields retain their offsets. Both the old and new generated configurations use
static component placement, and the candidate's embedded AFE TCB is internal and
16-byte aligned. The SDK's `eTaskGetState()` checks both current cores under its
kernel lock before returning a suspended state, and active `StaticTask` consumers
check deallocation failure. These findings do not justify undoing the official
task allocator, deletion API or required second notification entry.

Separately, official ESP-IDF commit
[`b25cb2906c7db70a86332dffbe77df64345d8a73`](https://github.com/espressif/esp-idf/commit/b25cb2906c7db70a86332dffbe77df64345d8a73)
fixes concrete PIE register save/restore defects: the missing Q3 register, XACC
packing and the SAR/SAR_BYTES/FFT_BIT_WIDTH restore mapping. The released 5.5.5
and 5.5.6 owner files both retain the older implementation. The official fix
remains present in the independently inspected master snapshot
`383dda6c182e3dabf6355160166fe007df775629`. This is a source-proven SDK defect;
its causal relationship to the intermittent build 13 watchdog is not established.

Commit `65e1ad41e2` adds advanced `enable_pie_context_fix`, default **false**.
Enabling it requires native ESP-IDF, ESP32-P4 and exactly reviewed SDK 5.5.5 or
5.5.6. Only that opt-in registers `pie_context_framework_patch` and sets
`CONFIG_ESPHOME_ESP32P4_PIE_CONTEXT_FIX=true`. The installer guards both owner
regions together: OFF contains the original reviewed region verbatim; ON contains
the exact official fixed region. It does not import unrelated differences from
the official commit's branch or alter core task policy.

| SDK owner | Pristine SHA-256 | Guarded SHA-256 |
| --- | --- | --- |
| `freertos/FreeRTOS-Kernel/portable/riscv/portasm.S` | `0bfd43a09e877654b2e6c53de6a7dd97ac98139c603c68b895509b9fd027e479` | `964ee4a1b4c9c89c61ff4b13406b23ad0ccde5849a55b370e1b504f577f0dfb6` |
| `riscv/include/riscv/rvruntime-frames.h` | `a5a493c78dd574b995fd7442816902176b0a52bec841b057b863a1f0ff0813c0` | `7b25f3cec64031fab73f1892561e98062dd24246314c33363dcfa13561beebeb` |

The installer rejects unknown targets, versions or complete owner bytes before
staging either replacement. It stages both reviewed owners and backups, checks
that their original bytes are still current, and rolls back an already replaced
owner if the second replacement fails. Known guarded/pristine mixed states can
be completed idempotently. An unconditional fixed region is not silently
accepted, because it lacks the required disabled behavior.

The focused campaign passes **67 cases**: 19 new schema/codegen cases, 24 new
two-owner replay cases, six existing restart-scope cases and eighteen existing
guarded SDK cases. Independent portable official fixtures verify enabled region
identity and disabled complete-file byte roundtrips. Tests also cover import
safety, partial known states, unknown/missing owners, staging failure, second-file
rollback and a source change during staging. These tests only modify isolated
temporary SDK fixture trees. They do not prove that a linked image contains the
intended instructions or that the watchdog is resolved; candidate 14 requires
fresh object/ELF instruction checks and repeated relevant hardware scenarios.

### Networking, TLS and Home Assistant

Upstream now trims TLS and certificate bundles unless components request them
(`72413494b0`, #18877; `c6d423159c`, #18747). A managed dependency or an included
HTTP header is not a capability request.

- Custom HTTP users must call `esp32.request_http_client()`; verified TLS users
  referencing `esp_crt_bundle_attach` must request the certificate bundle.
- Realtime websocket components accepting `wss://` request TLS. This preserves
  availability of the old path; it does not silently change certificate policy.
- `esp32.allow_insecure_tls()` is the official helper for an explicit insecure
  TLS request. Preserve the product's existing opt-in policy.
- Product dynamic player, queue and speaker-group subscriptions remain supported:
  upstream retains the `std::string`, `optional<std::string>` and
  `std::function<void(StringRef)>` overload. Public HTTP `get(std::string, ...)`
  compatibility overloads are also retained.
- `4424af8c5b` moves runtime logging to `esphome/api_client.py`; keep the official
  component compatibility shim. Drop the old local `deep_sleep` argument-removal
  workaround and install current pinned Python dependencies instead.
- Official HA light, select, text and button platforms (`dd456ea0b2`,
  `e040647530`), transition reconciliation (`8ffdbfd4ce`), list parser
  (`c433e96734`) and input-number service fix (`4f539d3391`) are retained. They do
  not automatically replace the product's dynamic catalogs or locale policy.
- Optional outgoing API connections and the onboarding wizard are retained as
  upstream capabilities; integrating them into the product is separate work.

### Audio adoption and retained board ownership

The old fork does not modify official `audio`, `i2s_audio`, `microphone`, `speaker`,
`mixer`, `resampler` or `voice_assistant` directories relative to the common
ancestor. Adopt all their current upstream changes.

- Shared-clock standard I2S duplex: `51b1fdcc8b` (#19959), `06b585d373` (#20984).
  This is not four-slot TDM and does not supply a playback-reference/AEC pipeline.
- Microphone resampler: `afba431600` (#19953). It uses bounded callback chunks and
  reusable storage, but does not align separate microphone/reference streams.
- Keep the custom TDM owner and aligned pre-AFE conversion for the active ES7210
  board. Idle remains wake-word-only; expensive AFE/AEC starts for conversation.
  Replacing only one side of mic/reference conversion would change echo timing.
- Adopt current audio decoder dependency versions, speaker/mixer/resampler lifetime
  fixes and independent mute/volume behavior. Remove the custom stale
  `esp-audio-libs` 3.2.1 pin in favor of upstream 4.1.0.
- Adopt official configurable wake-word buffer, inference task core and internal
  RAM options (`6580f4f314`, `ea76073d86`, `8060dadd6c`). The product's 1,000 ms
  buffer fits the official range. Remove duplicate downstream implementation.
- Keep official Sendspin codec preference, pairing, reconnect and metadata clearing
  improvements. Port the downstream mono-channel restriction, `artwork_url`, and
  artwork lifecycle around the current callback and frame-acknowledgement API.
- Retain the product's bounded hardware-JPEG/direct artwork path; the official
  image platform's storage and lifecycle are not automatically equivalent.

The vendored software decoder is libjpeg-turbo 3.1.1 with a downstream large-pool
PSRAM allocator. It is not an untouched upstream archive. A separate source audit
compared all 120 bundled paths with managed `espressif/libjpeg-turbo` 3.1.1~2:
87 files match, 25 wrapper differences are comments, and the remaining differences
include the allocator, ARM64EC preprocessing and documentation. Replacing this
bundle with that managed version alone would lose the allocator improvement.
This integration retains it and declares the local decoder as an explicit native
IDF component, so artwork consumers receive its exported headers.

Managed 3.2.0~1 now offers `CONFIG_LIBJPEG_TURBO_ALLOC_PREFER_SPIRAM` and the same
v8 C API. It is a useful follow-up that can remove the bundle, but its official
wrappers also move small pools to PSRAM and therefore change memory placement and
timing. Validate hardware and explicit software decode, progressive images,
the existing 360,000-pixel progressive limit, and allocation/failure behavior
before that separate replacement. Do not silently add software fallback to the
current hardware-only policy. The local source-audit evidence is in
`audio-audit/libjpeg-readonly-audit.md` under the integration evidence directory.

Successful UI phases or connection setup do not establish acoustic full duplex,
AEC, barge-in or a complete cloud conversation. A backend credit failure must be
reported separately from device transport health.

### Graphics and remaining downstream additions

Official LVGL moves from the fork's 9.5 to 9.6 (`bea0ef264a`, #20257), with managed
component integration and codegen changes. Preserve new official table/list/menu
and color-picker widgets, gradient options, animation completion fixes, text
spacing and source selection. The downstream ownership design remains required:

- Three display-owned DSI buffers, two full-screen RGB888 LVGL direct buffers,
  exclusive external-framebuffer sessions and coherent LVGL restoration.
- Pause and drain direct workers before snapshots, app changes or ownership
  handoff; never write scanned, queued or staged buffers.
- Home uses three reusable raw slots and compressed off-window backing;
  applications share one raw work buffer and compressed previews.
- Retained weather animation phase and finite-cycle behavior, direct Lottie
  loader ownership, bounded camera/gallery decoding, PPA/DMA synchronization and
  explicit component lifecycle must survive the port.
- Current upstream display changes, notably `04aa59563d` (#20884) and
  `d3faafc578` (#20799), alter pixel API/byte-order assumptions. Port custom blits
  to the current interface; do not revert the official display implementation.
- Official RTSP serving is a producer for a device camera. It does not replace
  the Home Assistant-normalized MJPEG consumer used by this product.

The independent source review checked the actual managed LVGL 9.6 refresh code.
`LV_EVENT_REFR_START` still precedes automatic direct-buffer synchronization,
`LV_EVENT_RENDER_START` follows it, and the active LVGL draw buffer swaps after
the final flush. This preserves the downstream mutex/reservation and cache-sync
boundaries. LVGL uses only its two configured buffers (`buf_3` is null); its new
upstream triple-buffer support does not claim the third display-owned DSI buffer.
RGB888 remains three bytes per pixel in BGR memory order across LVGL, framebuffer
leases and hardware camera decode. Panel MADCTL order is separate metadata. The
generated product constructor selects `ROTATION_UNUSED` with no display rotation,
so it retains the existing direct presentation branch. These are source checks;
hardware must still validate colors, buffer ownership and presentation counters.

Keep upstream image-format pruning and register the formats that custom Lottie
actually publishes. Transparent native loaders need premultiplied ARGB8888;
opaque canvas handoff can use XRGB8888. These requirements are added only when
Lottie is configured, with tests that ordinary configurations keep them pruned.
The custom image PPA feature uses `USE_LVGL_PPA_IMG`, leaving official numeric
`LV_USE_PPA_IMG` untouched. Private image-decoder includes and native source-patch
anchors follow the actual LVGL 9.6 layout.

Official selectable widgets now borrow persistent labels through
`FixedVector<const char *>`. Retain that storage-saving static API and provide
an explicit `set_options_dynamic(std::vector<std::string>)` for runtime catalogs.
Only the dynamic path allocates an owning string vector. Replacing a list keeps
the previous owner alive until LVGL text and the pointer view are replaced;
switching back to static labels releases it. Empty lists do not underflow the
selected-index clamp. The product camera picker uses this API rather than
borrowing pointers from a temporary source-name vector.

Some old fork YAML spellings follow current upstream syntax rather than keeping
aliases. The former `list.items` entries become official list `widgets` with
label/button composition. Menu `mode`, `root_back_button` and `sidebar_page`
follow the official `header_mode`, `root_back_btn` enum and explicit page model,
including `root_page`. The product uses none of those older forms. Meter and
table capabilities remain available through their current shared schemas.
MIPI transform validation is centralized in `DriverChip.transform_schema`;
MIPI DSI continues to reject `swap_xy`, as the previous driver already did.

The original fork also contains distinct features outside the large graphics and
audio owners: text HTML sanitization, per-write LEDC fade control, bounded locale
tables, task profiling, a standalone region presenter and the legacy realtime
client. Preserve them in the integration. The latter four receive `DOMAIN`
metadata matching current component conventions. The profiler action uses the
current `play(const Ts &...x)` signature, including parameterized automations;
the legacy websocket client requests TLS without changing its runtime certificate
configuration.

The standalone region presenter's pre-existing shutdown timeout fallback forcibly
deletes its worker and immediately frees its stack. It is not the active product
presentation path; it needs a separate park/drain/termination review before that
standalone component is proposed upstream. The merge does not silently rewrite
its lifecycle while other presentation interfaces are being reconciled.

The profiler remains opt-in and allocates bounded diagnostic state on first use;
it is not a reason to enable profiling in ordinary upstream builds. The locale
table is bounded to 512 entries and eight locale packs; future public schema work
should reject total-table overflow rather than relying on the existing silent
runtime cap. These pre-existing design improvements are separate from the merge.

### Hardware rejection and bounded memory corrections

The official Sendspin 0.9.3 transport has a measurable bounded memory cost. Its
1,331,472-byte shared inbound ring and eagerly allocated 262,144-byte artwork
assembly replace the old 600,000-byte audio ring and initially lazy artwork
assembly: a net 993,616 bytes (970.33 KiB). This explains nearly all of the
initial roughly 1 MiB PSRAM headroom difference. It is not evidence of a leak.
The integration retains the official protocol bounds rather than reducing image
quality or silently truncating transport frames. Separately, the main task stack
grows by 4 KiB of internal RAM; LVGL's small per-object metadata growth does not
explain a megabyte-sized PSRAM difference. Matched weather scenes have not shown
a CPU/FPS regression, but loop wall-time counters are not CPU utilisation.

The initial integrated firmware was rejected after hardware matrix attempt 3.
Gallery displayed actual photos and Camera presentation advanced, but the later
Settings/theme sequence exposed a real allocation failure. Its RGB565 scroll
surface needed 2,297,600 bytes; the previous two-piece fallback requested
1,148,803 bytes per piece while the largest free block was only about 1 MiB.
Total free PSRAM was still about 5.4 MiB. Runtime snapshot encoding also failed
when its requested scratch buffer exceeded the largest aligned allocation.
These are fragmentation failures, not evidence of a framebuffer ownership fix
or successful hardware acceptance. The failed report remains retained.

Commit `4f9a2e7b48` replaces the fixed head/tail fallback with a bounded,
row-aligned partition targeting 256 KiB pieces. The measured scroll surface now
uses nine pieces containing exactly the same 2,297,600 pixel bytes; RGB565
quality, PPA presentation and the session release point are unchanged. Complete
external loans and contiguous fast paths remain available. Capture rolls back
an incomplete partition, refresh reuses every piece and invalidates a partial
failure, and failed worker drains retain ownership before any free or handoff.
The count derives from stride, height and the existing content byte limit.

Commit `e2b85ce69f` sizes JPEG output scratch to the available aligned PSRAM,
including alignment/bookkeeping headroom, and reuses suitable retained storage.
The caller's budget remains an upper bound and image quality is unchanged.
Insufficient capacity fails the encode instead of publishing a partial JPEG;
it does not switch to software rendering or reduce image resolution.

Hardware matrix attempt 4 exposed a separate persistent Home admission failure.
After Gallery released its full-screen ownership, repeated complete reservations
could allocate only one of the required three raw Home slots. A large-block
diagnostic printed after reservation rollback described the restored heap; it
did not prove that both missing 1,920,000-byte surfaces could coexist. The earlier
test helper's machine pass did not establish a healthy three-slot window.

Commit `7ccaacc97a` gives the required raw window priority over idle compressed
application previews. Only after one failed complete reservation and rollback
may it discard eligible application JPEG previews and retry once. Actual Home
pages, Home JPEG backing, all raw surfaces, active or prepared close sources,
shared work owners and in-flight compression/prefetch readers remain protected.
Pending or active app, swipe, scroll and prefetch work refuses reclaim. Reclaim
advances the entry generation while retaining its object identity. An application
cache miss follows the existing fresh render into its borrowed DSI transition
surface. No new raw arena, lower quality or partial-window success is introduced.
The recovery log records released bytes and the retry result. Failure remains
observable if the complete raw window cannot be restored. Final Home hardware
acceptance must require all three raw slots after application return; intentional
one-slot trimming while Gallery or Camera owns the display is a different phase.

The earlier planner regression suite passes 22 tests, including executable host C++
checks of both production planners. The scroll executable sweeps all 637 legal
viewport offsets through its nine-piece surface, boundary/overscroll coverage,
each partial allocation failure and borrowed-buffer release. Separate source
checks cover the FreeRTOS timeout guards. These tests and a full native compile
are preconditions for a fresh flash and hardware matrix; they do not establish
accepted device visuals, scrolling throughput or worst-case memory headroom.
The separate admission regressions cover twelve executable eligibility/retry
cases plus actual reader-identity and generation guards. These source/host checks
do not establish recovery or segmented seam quality on the final device build.

### Preserved background-refresh underrun and ownership correction

A separate same-source Camera picker campaign on build 11 observed one DSI
bridge underrun, at tick 729938, after Camera had closed. The selected camera
source did not change, and its presentation counter advanced before closing.
The log then records a
283 ms sleeping-Home backing refresh while weather presentation had resumed.
The main matrix 7 had zero underruns; that earlier result does not cancel the
later failure. The failed campaign remains preserved, and no DMA fatal event or
framebuffer ownership corruption was established by this single observation.

Source inspection found that `snapshot_refresh_tile_backing` lacked the required
regional pause/drain contract. Disabling LVGL invalidation does not stop the
independent direct-region worker. The nested LVGL snapshot waits only its own
draw tasks, and the JPEG codec mutex serializes only codec operations. Neither
provides the missing direct-region barrier. This path already existed in R25;
unchanged source is not a reason to accept an observed runtime failure.

Commit `0ec0a3504d` resolves the configured display component and acquires one
nested `direct_regions_pause(true, 120)` lease before any cache/buffer mutation.
A local pointer-only RAII guard releases exactly that lease on every exit,
including a failed drain, encode failure or borrowed-slot restoration failure.
The lease covers capture, synchronous JPEG work and complete borrowed-slot
restoration. No new framebuffer, allocation, quality change, CPU fallback or
global unpause is introduced; other runtime functions are unchanged.

Thirteen executable host cases compile the actual production function
against narrow ownership fakes. They cover rejected inputs, timeout without
cache mutation, existing nested pauses, resident and shared work buffers,
capture/encode failures, outgoing encode failure, and borrowed-slot restoration
success/failure. Together with the earlier 35 memory tests, these are 48 distinct
focused cases. They do not emulate FreeRTOS, DMA, JPEG hardware or PSRAM bandwidth.
Build 12 and its strict artifact audit passed. Its main matrix 8 recorded zero
DSI underruns, but that does not establish causality or reproduce the exact
sleeping-Home refresh condition. The source correction is not yet a causal
explanation of the preserved build 11 event. Build 13 and its own artifact audit
pass; its Camera campaign also records zero underruns through two observed
sleeping-Home refreshes. The later Voice watchdog independently rejects whole
build 13 acceptance. Candidate 14 must repeat the relevant hardware coverage.

### Post-restoration JPEG rejection and bounded admission correction

Build 12 matrix 8 passed its twelve main machine cases, retained three raw Home
slots in all twelve sampled Home phases, and completed nine-piece Settings
scroll summaries without failed frames. Those results were insufficient for full
acceptance: after the main final-health check, restoration of the original
accent/language triggered seven JPEG `ESP_ERR_NO_MEM` results. The Home refresh
reported `range=0-0/prepared=0`, and the accent batches retained `pending=1`.
The saved log does not contain subsequent successful cache preparation. All six
preference values and a nonblack restored screenshot were obtained, but they do
not prove that the new compressed generation was ready. The original report and
post-restoration failure evidence remain preserved.

These seven errors were not the separate optional 2,297,600-byte Settings decoder
pool probes: those probes were followed by the successful bounded scroll
partition. Nor did the generic error identify which encoder allocation failed.
The old JPEG remains alive until a replacement has been encoded and copied;
that simultaneous lifetime can create pressure even when three raw slots
already exist. Final owned-copy allocation was a source-supported hypothesis,
not a proven stage from the old generic `257` result. Stable free-memory samples
after failure are not evidence of leaked temporary scratch.

Commit `82025d7e41` adds one stack-owned admission budget to synchronous Home
window preparation. A main-side cache encode reports whether its actual codec
result was `ESP_ERR_NO_MEM`. Only that result may consume the budget and invoke
the existing idle application-preview reclaim policy. If eligible bytes were
released, the exact same raw input is encoded once more. The budget is consumed
before reclaim, even if no preview can be released; later pages and restoration
work in the same preparation cannot reclaim again. Other codec errors are not
reclassified, and the entire partially prepared window is not restarted.

The context is passed explicitly through synchronous flush, restore and load
helpers. Nullable defaults leave ordinary background refresh unchanged; neither
JPEG worker receives context or cache-reclamation authority. The codec has
returned, released its lock and cleaned newly owned temporary allocations before
reclaim is attempted.
Home JPEGs, all raw buffers, prepared/current close sources, shared work owners
and in-flight readers remain protected. The existing reclaim helper additionally
queries the actual navigation owner through a read-only component accessor and
refuses all reclaim while any application remains open, including settled
Settings after its transient compositor state has retired. A failed replacement
preserves the old Home JPEG and generation; one successful replacement advances
its generation once. Reclaimed application previews advance their own generation
and can use the existing fresh borrowed-DSI rendering path when next opened.
No quality, resolution, raw-slot count or framebuffer allocation is changed.

Commit `bf12117b1c` adds error-only warnings at five existing encoder `NO_MEM`
exits: engine creation, input staging, output scratch, a codec-reported memory
failure and the final owned JPEG copy. Stage-specific requested/planned/reported
sizes, scratch capacity, recorded input capacity and heap metrics distinguish
future failures. Recorded input capacity does not imply that staging remains
allocated at every later failure stage.
The warnings change no allocation, lock, ownership, cleanup or retry policy.
The new context is not a claim that application-preview preparation after Home
must always fit; that later phase still needs explicit device evidence.

The fresh combined memory/ownership campaign passes 68 distinct tests. Its 20
new cases comprise eighteen host executions of the actual production cache
encode, tile admission and reclaim functions against narrow codec/owner fakes,
one JPEG-feature-disabled compile/run, and one stack-authority/worker-boundary
regression. They cover exact error gating, no-context behavior, empty/protected
reclaim, active navigation, same-input success/failure, retained JPEG/generation
and a budget shared across more than one Home page. They do not emulate the
ESP32 allocator, FreeRTOS, DMA or cache timing. Build 13 compiles and passes its
79-check artifact audit. Its matrix 9 confirms fresh post-restoration
`prepared=1/pending=0` and three raw slots. The new precise JPEG `NO_MEM` retry is
not exercised in that device campaign: its one JPEG retry is `259/INVALID_STATE`,
and its separate raw-window allocation warning recovers through the existing
idle-preview reclaim. Those bounded passes do not cancel the later Voice reset
or validate candidate 14.

Build 14 matrix 10 independently confirms a different unfinished pressure path.
Its twelve main cases pass, 303 saved identities match the new marker/MAC, and
all 32 captured JPEG files pass hash/dimension checks. The twelve sampled Home
phases retain all three raw slots; nine-piece Settings scrolling reports zero
failed frames. No saved DSI underrun, bridge/fatal event or audio error is
observed. However, after the original preferences are restored, the fresh Home
window reaches `range=1-3/prepared=1` while all six later application batches
remain `home=1/apps=0/pending=1`. The bounded thirty-second restoration gate
correctly fails and refuses the final restored-Home capture. The earlier main
case results do not override that failure.

All eleven new codec diagnostics identify `stage=owned_copy`, after successful
hardware encoding, with encoded/requested sizes of 148,995 or 153,606 bytes and
a still-live output scratch capacity of 1,048,448 bytes. Reported free PSRAM is
approximately 1.08 MB, but its largest remaining block is only 139,264 or 147,456
bytes. These are confirmed final-copy failures, rather than an inferred stage
from a generic error code. No preview-reclaim result is logged: the existing
Home-only admission budget does not govern the separate application-preview
encode path, and is retained unchanged while the codec ownership fix is reviewed.

The fresh boot 14 log explicitly confirms release of the reusable encoder output
at 11:04:03.994950 UTC. The bootstrap-complete flag never resets, and later LVGL
encodes disable retention. The observed runtime 1 MB scratch therefore is not
evidence of a leaked bootstrap allocation. It is the temporary encoded output
that remains alive while the codec requests a second exact-size owned JPEG copy.
Any transfer/shrink correction must preserve completed-output ownership, accurate
capacity, allocator/capability compatibility and all failure cleanup under the
codec lock. Candidate 15's source and linked image implement the correction below;
its matrix 11 display failure still blocks device acceptance.

### Completed JPEG publication and Settings admission corrections

Commit `863c431e07` changes only the final publication failure path after a
successful hardware JPEG encode. Normal exact-size PSRAM/fallback copies remain
unchanged. Only when both copies fail, and only if this call owns its fresh
scratch, it requests a capability-compatible shrink to the actual encoded size.
Successful shrink transfers that allocation into `JpegBuffer` and returns without
freeing the adopted pointer. A failed shrink follows the existing single-free
error path and preserves the caller's old output. Borrowed/global scratch is
never resized, detached or transferred. There is no encode retry, quality change,
application-preview eviction or modification of the Home admission budget.

The reviewed SDK finishes DMA and synchronizes encoded bytes to CPU cache before
returning success. Matching PSRAM or unencrypted-PSRAM capabilities plus 8-bit
access take the same-heap shrink path; its TLSF implementation trims the block
and returns its original address. The general realloc API does not promise
cache-line alignment in every case, and this completed compressed result is not
reused as hardware output scratch. The recovery log records scratch capacity
before/after and encoded size, separately from an unrecovered `owned_copy`
failure. No 1 MB slack allocation is retained per application preview.

Twenty new host executions compile the actual complete production publication
branch against deterministic allocator and output-ownership fakes. Ten cases run
under each normal/unencrypted PSRAM capability choice. They cover ordinary
copies, both matrix payload sizes, in-place and moved realloc, null shrink,
borrowed scratch success/failure, tiny/full-capacity payloads, one-time frees and
preservation of old output on failure. Combined with the existing 68 memory and
ownership tests, the fresh campaign passes 88 cases without skips. These are
control-flow/ownership tests, not a reproduction of the ESP32 fragmented heap.

The companion product commit `63c968bf27` gives Settings the existing raw-window
priority before reserving its contiguous scroll loan. One additional nested
pause/drain barrier protects the existing trim API; its own lease is balanced
even on timeout. Only successful trim clears the product's four ready-window
values. The current raw Home source and neighbouring JPEG backing are preserved;
failure keeps the old metadata and bounded segmented fallback available. Close
returns the Settings loan and idle decode capacity before refilling Home, with
a delayed normal refresh when no accent refresh is pending. No permanent arena
or extra raw screen is introduced. Matrix 11 confirms successful trim and an
attached 2,297,600-byte contiguous loan, followed by its return and restored
three-slot Home window. It stops before scroll timing or held presentation is
validated; these results do not establish performance or complete restoration.

### Candidate 15 matrix 11 display rejection

The preserved report stops during synthetic Voice, after seven earlier cases
pass. Seventeen JPEGs are saved. Five sampled Home phases retain 5,625 KiB of
raw slots, including the post-Voice-close phase. No JPEG `NO_MEM` stage or
completed-owned-copy recovery is observed before the failure, so this run does
not exercise the new codec fallback on the device.

The bridge reports one real underrun at device tick 137278, with
`brg=0x00000001/raw=0x00000001`; the subsequent monitor independently reports
one nonzero sample and `brg_under=1`. The continuous UART also contains the
driver's external-memory starvation error interleaved into the preceding
memory-report line. The later app-state report is already closed, with null
roots/buffers and no active manual compositor. This distinguishes observed
timing from an unproven ownership or diagnostic cause. Matrix 11 does not reach
Gallery, Camera, language checks or an accepted final restored-Home capture.
The failed report, raw UART and saved-image evidence remain immutable while
the display failure is investigated.

### Candidate 16 diagnostic-only observation

Commit `9c8c189111` adds `preferences.sync_diagnostics`, disabled by default on
every platform and accepted when enabled only on ESP32. Its generated define
guards all new C++ clocks and logging. A nonempty sync records RTOS ticks and
64-bit microseconds around the unchanged NVS sequence, then emits one INFO line
after commit returns. Empty sync returns before instrumentation. The line
contains only timing, cached/written/failed counts and commit status; no
preference keys or values are logged. This changes observation, not the sync
interval, persistence policy, write order or return behavior.

An independent 93-case campaign passes without skips: the 54 diagnostic/backend
cases include sixteen host executions of the complete production sync function
with diagnostics enabled and disabled; 39 existing storage regressions also
pass. Disabled preprocessing removes only the added conditional blocks and
preserves all remaining nonblank source lines. These checks do not emulate real
flash latency, shared-memory contention or the matrix 11 underrun.

Source/configuration review finds that both R25's 5.5.5 and the current 5.5.6
configuration disable cache in the non-auto-suspend, executable-from-flash
`spi1_start` branch. Version 5.5.6 adds mmap write exclusion and corrects branch
predictor ordering around shared-cache suspension; the current video DMA driver
and compiled display resource/priority settings remain unchanged. Existing INFO
logs do not locate preference sync, and UART buffering prevents exact main-loop
instruction timing from raw byte order alone. The new diagnostic spans are
intended to test correlation; they do not prove NVS caused the failure. No
auto-suspend, XIP, QoS or framebuffer behavior change is introduced.

Matrix 12 records three successful nonempty syncs: device ticks 68470–68482,
248470–248478 and 308470–308475, with durations 12,825, 8,366 and 4,866 us and
three, two and one written values respectively. All have zero cached entries,
write failures and commit errors. No DSI IRQ is observed in that saved run.
These observations validate the diagnostic format, not a cause or exclusion of
all flash-related contention.

A separate bounded 130.003-second diagnostic campaign on the same candidate 16
image completes 32 Home transitions and two Performance Logs changes. All 48
saved marker/MAC samples match. It records two actual written syncs at ticks
488470–488476 and 548470–548475, lasting 5,451 and 4,908 us, with no write/commit
errors or observed DSI IRQ. Nine positive raster windows and nineteen regional
submission windows establish progress, not physical presentation FPS. The run
contains no still captures. It verifies Home before restoring the three original
switch values and requesting live HA weather again. This limited observation
does not reproduce or clear the separate matrix 12 weather-allocation failure.

### Candidate 16 matrix 12 weather rejection

The twelve machine cases, 33 JPEG hash/dimension checks, 309 marker/MAC samples
and thirteen sampled three-slot Home phases pass. Final restoration reaches
`prepared=1`, then `home=1/apps=1/pending=0`, before the accepted restored-Home
capture. One initial raw-window admission failure recovers after reclaiming
1,514,848 bytes from five eligible idle application previews. No JPEG `NO_MEM`
stage or completed-owned-copy recovery is observed, so the new codec fallback
is still not exercised by this device matrix.

Settings first attaches and returns the 2,297,600-byte external loan. Later
opens cannot borrow that pool but successfully use the existing internally
owned contiguous fast path. The actual scroll open has an explicit zero-byte,
unattached loan and one-piece summaries with 47, 94 and 141 frames and zero
failed frames. Source review establishes the old external pointer was cleared
on its earlier positive return; the one-piece ownership conclusion is an
explicit source inference, not a logged ownership flag. The older strict
external-loan-only audit failure remains preserved separately.

Whole-firmware acceptance nevertheless fails: from 11:52:28.722521 through
11:52:32.626885 UTC, twenty-one `lvgl.lottie` errors report a failed 589,824-byte
display back buffer while the largest PSRAM block is reported as 576 KiB. This
is after Camera closes and before Settings releases the weather animation;
the same visibility epoch has no observed successful initialization. The 33
stills do not prove uninterrupted animation through this interval. The expanded
saved-log audit therefore keeps matrix 12 rejected even though all its other
structural gates pass. Its classifier retains all twenty-one errors as material
allocation failures, separately from 24 late-frame warnings and 16 component
duration warnings. This run does not validate a runtime correction or justify
a relaxed acceptance gate.

### Candidate 18 direct opaque Lottie allocation correction

Commit `87e77c353b` omits the redundant display-back allocation only for the
existing direct opaque pipeline when the scene is not scaled. Its stable native
canvas and two private render targets remain distinct. For the measured 384×384
scene, this removes one 589,824-byte request without aliasing a source being
read by PPA. The serialized opaque, transparent and scaled paths retain their
existing display backing. Dimensions, alignment, quality, worker placement,
publication, finite retained-frame handoff and cleanup algorithms are unchanged.
The earlier unconditional hidden-region drain already covers this pipeline
independently of the back-buffer predicate.

Sixteen focused host cases execute the actual production allocation group and
lifecycle excerpts. They cover the exact three-surface budget, initial native
canvas, live publication and pointer rotation, finite handoff, retained recolor,
in-flight hidden drain, cleanup and allocation failures, and preserved scaled
and non-direct backing. Heap, LVGL, hardware completion and mailbox boundaries
are fakes; these are control-flow/ownership checks, not device animation or
bandwidth measurements. Candidate 18 copies the final reviewed header hash
`f0ad52eca0bbe8029eee41f2292eed98521565aed20e09d74491fe3d0e67aca8`.
The earlier candidate 17 copied an intermediate guard, is explicitly retained
as not flashed, and supplies no hardware evidence. The fresh candidate 18 build
and exact artifact gates pass. Matrix 13 also passes its saved machine contract,
as bounded below; visual and supplemental coverage must still complete before
accepting the firmware.

### Candidate 18 saved matrix and preference observation

The independent saved-log audit of matrix 13 passes all 25 unchanged structural
gates and is byte-identical to the separately executed root audit. All twelve
main cases complete, 309 saved identities match the exact candidate marker/MAC,
and all 33 saved JPEG hashes and 800×800 dimensions match. Thirteen sampled
Home phases retain the complete 5,625-KiB raw-three window. The first Settings
open attaches and returns the external pool; the measured later open has an
explicit unattached zero-byte loan, a previously cleared external pointer and
one-piece summaries with 48, 95 and 142 frames and zero failures. Its internally
owned contiguous surface is a source-backed inference, not a logged owned flag.

The final restoration has a fresh prepared page-1 window covering pages 1–3,
then a later `home=1/apps=1/pending=0` batch, all six original preferences
commanded back and a fresh raw-three health gate before the restored capture.
No DSI underrun/bridge event, fatal/audio error, JPEG `NO_MEM`, owned-copy
recovery or Lottie display-back allocation failure is observed in this matrix.
This is fresh coverage of the prior weather-allocation window, not a claim
that earlier failed images have become accepted.

Warnings remain explicit. One initial 1,920,064-byte raw-slot reservation fails;
the existing bounded admission policy reclaims 1,686,601 bytes from five eligible
idle application previews, retries once and restores all three slots. One JPEG
retry reports error 259 (`ESP_ERR_INVALID_STATE`, not `ESP_ERR_NO_MEM`) before
later complete admission. One optional second 1,280,000-byte artwork decoder
prewarm reservation fails, retaining the first spare; the placeholder and
compositor coverage does not prove a real two-generation artwork crossfade.
An artwork HTTP-copy FIFO-margin wait times out after 6,010 us and then continues
the existing chunked copy; the adjacent stress summary has zero bridge/host
underruns. Twenty-four late-frame warnings (largest logged interval 24,100 us)
and seventeen component-duration warnings, including a 1,616-ms API operation,
are retained. Three nonempty NVS spans write successfully; one lasts 139,611 us
without a corresponding observed underrun. These measurements do not establish
historical underrun causality or a matched performance result.

A separate 130.006-second preference/weather campaign performs 32 Home edges
and two Performance Logs changes on the same candidate. Its 48 identity checks
match; two distinct successful written NVS spans last 12,377 and 4,931 us, with
four writes total, zero failed entries and successful commits. No error, fatal
or nonzero DSI event is saved. Ten positive raster windows and nineteen positive
regional submission windows are progress evidence, not physical FPS or proof
of every weather visibility epoch; 47 `object-not-presented` publication
diagnostics during the alternating-page campaign remain recorded. Home1 is
verified, original logging/mute states read back, Performance Logs is explicitly
commanded back, and a live Home Assistant weather refresh is requested.
This campaign supplies no screenshots or acoustic evidence.

The graphics owner separately reviewed all 33 matrix stills in contact sheets
and four critical originals at full size. Distinct Home pages, readable theme
contrast, complete overlapping clock digits, visible weather in held/return
stills, themed Player controls and loaders, different Gallery photos and the
restored blue/Polish Home are recorded. Existing language/accent missing-glyph
rectangles, the clipped first Settings subtitle and fixture statuses becoming
Unavailable after live Home Assistant data arrives remain explicit limits.
Stills do not establish physical FPS or motion between sampled frames.

The separate held-Settings campaign verifies source Y 336 and 186 while the
manual snapshot owner is active, with fresh render records before each capture
and safe pre/post ownership gates. All four full-size images are reviewed.
The same-open surface is one internally owned contiguous segment, with zero
failed frames; this is not a new nine-piece seam test. Closing releases the
2,243-KiB scroll allocation, restores 5,625-KiB Home raw slots and returns the
six saved preferences. The immediately sampled prepared-hidden weather state
is not mislabelled as finite completion or permanent loss of animation.

The Camera supplement confirms source 0 through the picker path, with its
presentation counter increasing from 652 to 793. Three full-size captures are
reviewed. Twenty positive logged presentation samples range from 8.8 to 14.8
FPS, with median 11.45, alongside an explicit startup zero sample and deliberate
screenshot pauses. This does not cover a different camera or visually validate
the uncaptured picker list. After a verified backlight-off precondition, actual
Sleeping Home refreshes for pages 2 and 3 complete with no saved DSI/fatal/JPEG
error; Home and the original on/1.0-brightness backlight are restored.

Three local chimes each complete an actual playback-entity PLAYING→IDLE cycle
at volume 0.5 through the embedded file source, official resampler/mixer and
board speaker routing. Physical audibility is not observed by this test.
One separate real Voice open after the accumulated graphics/memory pressure
has exactly one `start_session` event, AFE processing/feed/fetch activation and
microphone/stream open-close activity, without an observed watchdog, fatal or
DSI event. The provider rejects that attempt because Gemini prepaid credits
are exhausted. There is no proved remote reply, nonzero assistant response,
acoustic full duplex or numeric AEC-reference coverage. Cleanup restores the
saved mute/logging targets through the helper's matching callback-state cache;
it does not save an independent fresh post-restoration subscription readback.

The Voice cleanup also records a direct weather publication refusal
(`api-rejected`, result 0). It remains a warning: approximately 3.2 seconds
later the same target has a finite-retain event at frame 309, followed by a
Home widget-region refresh. This establishes forward progress, without
inventing an unlogged native admission acknowledgement or a precise cause for
the earlier refusal. The later planned R25 boot is excluded from the preserved
Voice UART epoch. The measured weather/Settings comparison is recorded below.
Current-image physical cold starts were declined; the earlier image's five
power cycles are not reused as proof.

The subsequent five-cycle warm campaign completes five explicit software
restart commands and exactly five real `start_session` events, one per boot.
Every actual retained record identifies the complete candidate18 ELF hash,
`same_firmware=yes` and software-reset reason 3. All five observed boot mute
states are true, with no normalization needed; each has thirty seconds of
continuous boot-health observation before its rapid unmute/open attempt.
The independent saved-data audit verifies 262 marker/MAC identities, thirteen
fresh preference readbacks, six raw-three Home health gates and sixteen
800×800 capture hashes. Initial and final fresh states match exactly:
System Logs=true, Performance Logs=false, Voice Assistant Mute=true and Voice
Assistant Timing Logs=true. UART review records five ROM/software-reset
starts and five successful safe-mode boot validations, without an observed
watchdog, fatal, DSI underrun, outer error or allocation failure in that span.

The graphics review covers cycles 1–4 in a twelve-image contact sheet, cycles
1 and 5 additionally at full size, and final Home through a pixel-equivalent
lossless PNG/contact and region probes. A preliminary apparent omission of
Home controls in an inline JPEG rendering is withdrawn: the unchanged source
JPEG and decoded PNG have exactly equal RGB pixels, with the complete cloud,
clock, condition chip, status, microphone and pager. Untouched follow-up
captures require no navigation, theme refresh or wake command and confirm the
same full Home. This is a review/presentation correction, not a firmware fix.

One cycle-4 direct weather publication logs `callback-timeout`. It remains
recorded; finite frame 309 and a successful 800×384 Home-region refresh follow
in that same boot before the next planned restart. No separate native
acknowledgement or exact stall cause is invented. Each Voice attempt again
receives the exhausted-credit provider error. Five warm software cycles are
bounded stability evidence, not cold-power, successful conversation, physical
audibility, acoustic full-duplex or numeric AEC-reference validation.

### Candidate 18 measured performance and memory boundaries

The saved weather harness uses the same explicit sunny and cloudy scenes.
Each image supplies twelve regular active animation windows per scene, with
their actual elapsed durations and zero `publish_missed` counts. These are
rendered-animation counters, not physical screen presentation FPS:

| Scene | R25 rendered FPS | Candidate 18 rendered FPS | Observed change |
| --- | ---: | ---: | ---: |
| Sunny | 47.6190 | 47.8985 | +0.5868% |
| Vector overcast | 26.0161 | 26.6934 | +2.6034% |

The paired Settings comparison uses the exact same frozen helper, exact-image
OTA/55-frame boot chains and at least thirty seconds after each boot trace
(actual settles 39.830621/41.358741 seconds). Both opens attach the same-sized
2,297,600-byte external contiguous one-piece surface. Three gated benchmark
actions have zero failed frames. The final cumulative mean changes from
31,941 to 31,114 us (2.59% shorter observed wall time), with recorded maxima
34,309 to 33,407 us. These are one paired run and cumulative frame timings,
not independent per-action samples, CPU time, statistical confidence or
physical FPS.

The separate task-runtime profiler obtains actual FreeRTOS runtime deltas and
normalizes each task against the summed deltas across both cores. Its complete
copied C++ is token-identical to R25. The final two stable same-scene samples
show the following, without using the first/transitional sample as a paired
CPU estimate:

| Scene | R25 busy / loopTask (%) | Candidate 18 busy / loopTask (%) |
| --- | --- | --- |
| Sunny | 43.4 / 4.4; 44.0 / 4.7 | 42.5 / 3.2; 42.9 / 3.8 |
| Vector overcast | 30.8 / 6.6; 30.4 / 6.3 | 28.5 / 4.2; 28.6 / 4.4 |

The LVGL `perf1s loop=` diagnostic has a different meaning. It sums elapsed
`esp_timer` time around `lv_timer_handler()` over a reporting window of at
least one second, subtracts separately accumulated flush time and floors the
result at zero. It is neither one ESPHome iteration nor FreeRTOS CPU runtime;
the associated `cpu=` value is derived from that same elapsed-time counter.
The complete timing/report/reset block is token-identical between the actual
copied R25 and candidate18 sources. Its whole-log median nevertheless rises
from 5,901 to 43,952.5 us; per-scene medians are 22,744.5→86,844 us for sunny
and 5,330→23,956 us for cloudy. This increase remains explicit.

The timed handler can include lock acquisition, timer callbacks, asynchronous
drawing waits and preemption. The draw wait/dispatch functions already exist
in R25; their relevant bodies are token-identical. The new outer ESPHome
`wakeable_delay` occurs after component loops return, outside this measured
span, so it cannot directly explain the diagnostic increase. Existing logs do
not measure per-lock wait or callback wall/runtime separately. No single wait,
logger/API path, scheduler change or LVGL9.6 behavior is proved to cause the
increase. Healthy render counters and sampled task CPU do not justify claiming
global absence of performance regressions.

Memory headroom is also a measured tradeoff rather than a success claim:

| Cloudy-after quantity (KiB) | R25 | Candidate 18 |
| --- | ---: | ---: |
| Free PSRAM | 4,316 | 3,950 |
| Largest PSRAM block | 2,816 | 2,112 |
| Free internal heap | 57 | 40 |
| Residual estimate | 12,144 | 13,106 |
| Accounted weather animation | 2,368 | 1,792 |

The saved scenes show residual growth of 960–965 KiB, consistent with the
separately source-derived 970.33-KiB bounded Sendspin transport/assembly cost;
integer-KiB counters do not attribute every heap byte. Removing the redundant
overcast surface saves 576 KiB, while final cloudy free PSRAM remains 366 KiB
lower, its largest block 704 KiB lower and internal free heap 17 KiB lower.
No monotonic-leak or zero-cost-upgrade claim follows from these observations.
The final image's warning/error and restoration gates remain separate from
these sparse performance samples.

## Disposition of the complete original fork

Every original path is listed in the file inventory. These grouped policies cover
all of them, including tests and vendor sources:

| Original area | Policy |
| --- | --- |
| API log client | Adopt official compatibility shim; remove obsolete dependency workaround. |
| Core StaticTask | Adopt official deletion, allocation and memory ownership completely. |
| Core buffer metadata / analysis defines | Retain generic CPU/DMA ownership metadata and add downstream defines to current official definitions. |
| Native library converter and its tests | Adopt official implementation; old Windows slash patch is subsumed. |
| Framework compile-flag helper and test | Retain downstream include flags on current official implementation. |
| ESP32 / Hosted | Adopt current official configuration and transport, retain only reviewed default-off options and task-placement helper. |
| Audio processor / AFE / custom physical stack | Retain missing TDM/AEC ownership; adapt only actual official API/dependency changes. |
| Wake word | Adopt official buffer/task options, removing duplicate custom implementation. |
| Sendspin / generic image / artwork | Adopt official core and dependencies, preserve downstream features through current callback/ACK contracts; keep vendor decoder unchanged. |
| Display / MIPI / MIPI DSI / JPEG / image / runtime image / online image / font | Adopt official additions and signatures; retain optional hardware JPEG, ownership and hardware transport extensions. |
| LVGL / Material / direct image and region presenters | Retain downstream runtime/snapshot/lifecycle features while adopting LVGL 9.6 and current official codegen/widgets. |
| Camera / gallery | Retain product-independent bounded stream and encoded-image lifecycle; update HTTP/TLS capabilities. |
| Locale / profiling / legacy realtime client | Retain distinct opt-in capabilities; apply current metadata and action/TLS compatibility. |
| Text sensor / LEDC | Retain HTML sanitization and next-fade actions on current upstream implementations. |
| Shared constants / CODEOWNERS / ignore rules | Combine downstream additions with current official definitions and policy; do not replace entire upstream files. |
| All changed/additional tests | Preserve downstream coverage, adopt updated official fixtures and expectations, add focused interface/default-off regressions. |

## Ownership and future upstream pull requests

This integration branch is a checkpoint for a working product, not one acceptable
monolithic upstream PR. Product YAML, entity IDs, locale policy, asset selection,
camera allow-lists and backend provisioning stay in the product repository.
Reusable ESPHome code derives resolution, format, stride, pins and ownership from
configuration; it must not depend on Atmosfera names or 800x800 geometry.

Suggested independent review slices:

1. Text-sensor HTML sanitization and its schema/unit/build tests.
2. LEDC next-fade duration action, with framework/platform coverage and docs.
3. Generic JPEG decoder and optional runtime-image acceleration, preserving the
   software default and compile-time source selection.
4. Display framebuffer lease/ownership and metadata interfaces, with driver tests
   before consumers or accelerators.
5. MIPI DSI buffering/presentation improvements and narrowly justified SDK
   workarounds. SDK fixes should be proposed to Espressif separately; do not
   present a private SDK patch as an already released upstream backport.
6. LVGL direct-region, snapshot, navigation and task/drain contracts as separate
   dependent changes; Material product composition does not belong in core LVGL.
7. Bounded direct loader/animation support and lifecycle tests, without enabling
   expensive hidden animations by default.
8. MJPEG network camera/image presentation and generic gallery lifecycle; service
   discovery, credentials and source allow-list policy remain integration-owned.
9. Sendspin mono negotiation and artwork lifecycle extensions, separate from
   protocol/version upgrades already supplied upstream.
10. TDM/reference and generic audio processing interfaces that extend official
    standard duplex instead of duplicating it. Acoustic evidence is required.
11. Locale tables, optional diagnostics and task placement only after generic API,
    allocation and schema limits meet current contribution requirements.
12. Pipecat and legacy backend integrations remain separate optional components;
    backend-specific policy should not be added to the standard native assistant.

Each PR needs current `dev` ancestry, an independent behavior/default-off contract,
tests across relevant targets, user documentation, the full upstream template,
and no product coordinates, local paths or credentials. Do not mix mechanical
refactoring with behavior changes.

## Validation checkpoint and completion gates

| Check | State at source-audit checkpoint |
| --- | --- |
| Official history present and non-shallow | PASS |
| Original 423 paths and official 2,904 commits inventoried | PASS |
| Core/Hosted/native focused tests | PASS: 562 passed, 2 skipped; see exact modules below |
| MIPI SDK adaptation tests | PASS: 5 passed; exact variants, rejection, idempotence and safe import |
| Complete 16-owner MIPI SDK replay | PASS on pristine 5.5.5, pristine 5.5.6 and captured partial 5.5.6 |
| Core Python lint | PASS for assigned Python changes, metadata, SDK patchers and new tests |
| Added metadata / profiler / legacy TLS changes | Source reviewed, focused Python lint and complete native product build passed |
| Combined integration focused tests | PASS at the earlier integration checkpoint: 1,540 passed, 2 skipped, including Wi-Fi/Ethernet, VNC and graphics/audio migration regressions |
| Follow-up memory / ownership regressions | PASS: one fresh combined 88-case campaign without skips, comprising the existing 68 planner/scroll/raw-admission/backing-lease/Home-JPEG cases plus 20 completed-JPEG publication cases |
| Preference diagnostic / storage regressions | PASS: separate independent 93-case campaign without skips, comprising 54 diagnostic/backend cases and 39 existing storage cases; not added to the memory campaign or earlier overlapping totals |
| Direct opaque Lottie allocation / lifecycle regressions | PASS: separate sixteen-case host campaign without skips, using actual production excerpts and deterministic external boundaries; not device animation evidence |
| Default-off PIE schema and two-owner installer campaign | PASS: 67 focused cases, comprising 43 new PIE cases and 24 existing restart/SDK cases; overlaps earlier campaigns and is not added to their totals |
| Final official API refresh | PASS: separate 145-case campaign, including 29 optional/default-argument cases; do not add overlapping campaigns to the earlier suite total |
| Python validation checkpoints | PASS: fresh independent 95-file Ruff campaign for current `87e77c353b` relative to official `7dace44d61`; supersedes the earlier 94/91-file checkpoints |
| Build 13 native product and resolved dependencies | PASS at `bf12117b1cc177b15994eb6249e267bc61d5fed7`, containing official `7dace44d61`: ESP-IDF 5.5.6, exact linked artifact audit 79/79, 500 copied source matches and three generated headers validated; marker `2026-10-22 12:20:30 +0200`, ELF SHA-256 `fd77af1e67f2112a483e4e61152836ecd76a83ab2c4159ec42bb40e1dc3f1549`; later hardware acceptance REJECTED |
| Build 13 flash and boot captures | Running marker/MAC confirmed; 55 complete six-chunk regional boot captures and final full-screen JPEG verified. Sequential regional captures do not establish animation smoothness or physical FPS. |
| Build 13 matrix 9 machine checks | PASS for twelve cases, 33 verified JPEGs and thirteen Home phases with three raw slots; fresh post-restoration `prepared=1/pending=0`; nine-piece Settings scroll with zero failed frames; no observed DSI underrun, DMA fatal or audio anomaly. Later Voice failure remains separate. |
| Build 13 held Settings snapshot | PASS for actual held nine-piece snapshot captures at source Y 336 and 186, fresh render/ownership gates, zero failed frames and original preferences restored; saved-image visual assessment remains separate. |
| Build 13 Camera picker / sleeping Home | Same source 0 retained, presented counter 672→855, two actual post-close sleeping-Home refreshes, no observed DSI underrun/fatal/JPEG error, Home/backlight restored; does not cover every source or establish causality for the old build 11 underrun. |
| Build 13 Voice | REJECTED after the real AFE/watchdog reset. Two later one-attempt diagnostics without another observed reset do not clear this failure; provider credit rejection and acoustic gaps remain explicit. |
| Candidate 14 native build / linked PIE proof | PASS for `65e1ad41e2` and product `311ef64887`: native IDF 5.5.6, strict artifact audit 79/79, 500 copied source matches plus three generated headers; marker `2026-10-22 12:53:55 +0200`, ELF SHA-256 `88254e8e5a89de23187688eada4eb7b00b08d934d87f6a6098d3e52475f3336a`. Both installed SDK owners match reviewed guarded hashes; generated PIE opt-in is enabled. Linked save/restore spans match the official correction byte-for-byte (82/78 bytes), old sequences are absent and context layout remains 216/24 bytes. Source/linked identity does not establish reset causality or hardware stability. |
| Candidate 14 flash / matrix 10 | Running marker/MAC confirmed; twelve main cases and 32 saved JPEG hash/dimension checks pass, twelve Home phases have three raw slots and nine-piece scroll has zero failed frames. REJECTED at final restoration: eleven confirmed JPEG `owned_copy` allocation failures, Home prepared but `apps=0/pending=1`, no accepted final Home capture. Performance/audio/restart acceptance and the replacement codec-fix candidate remain pending. |
| Candidate 15 native build / linked PIE proof | PASS for fork `863c431e076c57ec3284b5b9ad2a5d0257116ef4` and product `63c968bf27d616cf004efd07b258350a1eef70cc`: strict artifact audit 79/79, 500 copied source matches plus three generated headers, marker `2026-10-22 13:21:34 +0200`, ELF SHA-256 `bad582c892d0111ed4f6ac5b0c782e69a103933dd5889b54be1eba15b36b9436`. Exact official PIE save/restore spans and unchanged context sizes pass separately. |
| Candidate 15 factory configuration | PASS for a fresh tracked product archive with five CI-only placeholder secret keys; schema/configuration only, no factory firmware build or GitHub workflow run |
| Candidate 15 flash / matrix 11 | Running marker/MAC confirmed; REJECTED during synthetic Voice after a real bridge underrun at tick 137278. Seven earlier cases and seventeen saved images do not complete the matrix. Five Home phases retain three raw slots; Settings trim, attached 2,297,600-byte contiguous loan and return are observed, but no scroll timing/held coverage is accepted. No JPEG `NO_MEM` or owned-copy recovery occurs before the failure. Gallery, Camera, language and final restoration are not accepted. |
| Candidate 15 held/settings, Camera, audio and performance | Not completed; matrix 11 failure blocks whole-firmware acceptance |
| Candidate 15 warmed/cold restarts | Pending; previous R25 or rejected build 13/14 results do not validate this firmware |
| Candidate 16 configuration / native build | PASS for fork `9c8c189111a1af3dbbe7b1790a6f3c4e280cf0fc` and product `461cf0578c4c19e54f2f1b38034e85b6afb7f5cb`: strict artifact audit 79/79, marker `2026-10-22 13:42:31 +0200`, configuration hash `0x903d0abd`, ELF SHA-256 `048c70eb7759f4cbfdda19444027247f4d877ff292bda72efd46bd576ef033e8`. Exact official PIE spans and unchanged context sizes pass separately; fresh factory configuration validation also passes. These are build/configuration facts, not hardware acceptance. |
| Candidate 16 matrix 12 / preference sync correlation | REJECTED for twenty-one repeated weather Lottie display-back-buffer allocation failures after Camera. Twelve machine cases, 33 JPEGs, 309 identities, thirteen raw-three Home phases and final `prepared=1/apps=1/pending=0` restoration pass; valid internally owned one-piece scrolling is distinguished from the earlier external loan. No DSI IRQ, JPEG `NO_MEM` or codec ownership recovery is observed. Three actual written preference sync spans are captured with zero NVS failures; diagnostic correlation does not establish historical underrun causality. |
| Candidate 16 separate NVS/weather observation | Bounded PASS: 130.003 seconds, 32 Home transitions, two Performance Logs changes, 48 matching identities, two written syncs lasting 5,451/4,908 us, zero observed IRQ/error records and original three switches restored after Home verification. Raster/regional progress is not physical FPS; no still captures occur inside this campaign, and candidate 16 remains rejected for matrix 12's weather failure. |
| Candidate 16 further held / Camera / audio / performance / restart acceptance | Pending; no whole-firmware acceptance while the weather-allocation failure is unresolved |
| Candidate 17 intermediate build | Not flashed: its copied guard differs from the final reviewed header. Preserved intermediate artifact metadata is not candidate 18 identity or hardware evidence. |
| Candidate 18 native build / linked PIE proof | PASS for fork `87e77c353b5979eeb7f44074c229585a2f20c4ea` and product `461cf0578c4c19e54f2f1b38034e85b6afb7f5cb`: strict artifact audit 79/79, final source/header matches and exact official PIE spans. Marker `2026-10-22 14:08:01 +0200`; ELF SHA-256 `5e8d6baa04d729bb988c15f64df62351f0538f10a20eb85718115415150b87ea`, OTA `99e88080cc805a2fd06e08f8e50cc9c4d0a00d97d1857c398871421c6034c1a3`, factory `818c372f2446c32b59805bf19df4ddb006868ae66c7bba2c426e8ae99c6b7ebe`. |
| Candidate 18 matrix 13 | Bounded machine PASS: twelve cases, 309 matching identities, 33 JPEG hash/dimension checks, thirteen raw-three Home phases, known internally owned one-piece scroll with zero failed frames and complete fresh post-restoration cache gates. Graphics review covers all 33 stills in contact sheets and four critical full-size originals; known glyph/subtitle and live-HA fixture limits remain. No Lottie display-back OOM, JPEG `NO_MEM`, DSI underrun/bridge or fatal/audio error is observed. One initial raw allocation recovers through 1,686,601-byte/five-preview reclaim; a JPEG error-259 retry, optional decoder prewarm shortfall, FIFO-margin wait timeout and latency warnings remain explicit. |
| Candidate 18 separate NVS/weather observation | Bounded PASS: 130.006 seconds, 32 Home transitions, two Performance Logs changes, 48 matching identities and two written syncs lasting 12,377/4,931 us. Four writes, zero NVS failures, zero observed error/IRQ records and original three switches restored after Home verification. Raster/regional counters and object-not-presented diagnostics are retained; no screenshots, physical FPS or causal claim. |
| Candidate 18 held Settings | Bounded PASS: fresh actual manual-owner captures at source Y 336/186, four reviewed full-size images, same-open one-piece surface and zero failed frames; scroll allocation releases to zero and Home raw-three returns. Six observed preferences are restored. This is internally owned contiguous coverage, not new nine-piece seam coverage. |
| Candidate 18 Camera picker / sleeping Home | Bounded PASS: same source 0, presented 652→793, three reviewed full-size captures, actual Sleeping Home page-2/page-3 refreshes after verified backlight-off and restored Home/backlight on at brightness1.0. No observed DSI/fatal/JPEG error. Positive sample median11.45FPS retains startup0FPS/capture-pause and same-source limits; picker-list layout is not captured. |
| Candidate 18 local chimes / real Voice after pressure | Three actual PLAYING→IDLE chime cycles and one real `start_session`/AFE/microphone stream lifecycle complete without observed watchdog/fatal/DSI. Provider credit exhaustion limits that real attempt; remote reply, physical audibility, acoustic full duplex and numeric AEC-reference proof remain absent. Weather refusal and later finite-retain/Home-refresh progress are preserved. |
| Candidate 18 measured weather / Settings comparison | Sunny47.6190→47.8985 and vector26.0161→26.6934 rendered FPS, twelve regular windows per image/scene and missed0. Same-helper/external-one-piece Settings final mean31,941→31,114us (−2.59%), max34,309→33,407us and failed0. Stable sampled task runtime is lower; handler-wall accumulation is higher and free/largest/internal memory headroom is lower as detailed above. No global no-regression or physical-FPS claim. |
| Candidate 18 five warm restart-Voice cycles | Bounded PASS: five explicit software resets, five matching full-ELF retained records, five successful safe-mode boot validations and exactly five actual sessions. All boot mute states observed true without normalization; 262 identities, thirteen fresh preference readbacks, six raw-three gates and sixteen capture hashes pass. Graphics/UART scopes and the single cycle4 callback-timeout followed by same-boot finite/refresh progress are retained. Original/final fresh switches match. Provider credit exhaustion limits every attempt; no acoustic or cold-power claim. |
| Candidate 18 physical cold starts | Declined for this image; previous-image power cycles remain separate historical evidence. |
| Candidate 18 final bounded validation | PASS for the listed native/device/matrix/supplement/performance/warm scenarios, with measured handler-wall/memory tradeoffs and cloud/acoustic/cold-start limits explicit. Corrected lossless source-pixel QA resolves the preliminary inline-JPEG visual alarm without a runtime change. Preserved candidate13–16 failures remain historical rejections. Exact remote/pin/promotion evidence is separate. |
| Final merge ancestry | PASS: latest official target and original fork are ancestors of the final source refresh merge |
| Remote publication verification | Separate Git gate: verify exact intended refs, matching immutable workflow pin and clean canonical promotion; device validation does not establish publication. |

The corresponding build 13 OTA SHA-256 is
`fd9c8797443e7d2515d66047cdc3c514aefbcf11288b8f7059fce90f2e3ca561`;
factory SHA-256 is
`933611d86e414e94b422fab61ba34896bad40d8e56e1323800a1d0ccc179c6a8`.
These are preserved identities of the flashed but hardware-rejected build 13,
not artifact identities or acceptance evidence for candidate 14.

The 562-test run used the candidate environment and candidate tree, with coverage
options disabled for this focused invocation, on these exact modules:

```text
tests/component_tests/esp32_hosted
tests/component_tests/esp32/test_esp32.py
tests/component_tests/esp32/test_cache_safe_restart.py
tests/unit_tests/test_espidf_component.py
tests/unit_tests/test_framework_helpers.py
tests/unit_tests/test_guarded_sdk_patches.py
```

Eight new Hosted cases cover default-off compatibility on current IDF, both exact
SDK gates, the reviewed 5.5.5/5.5.6 lifecycle, diagnostic prerequisites, ordinary
Hosted 3.x and rejection of the unreviewed shutdown extension on 3.x. Six new
ESP32 cases cover native P4 acceptance on both reviewed SDKs and rejection of an
unknown SDK, chip or build toolchain, while keeping the ordinary current-SDK
default configuration valid. Eighteen SDK installer cases check real preimages,
exact/idempotent postimages, recognition of the known fixed SDMMC owner, and
unchanged files after rejection of unknown versions or source modifications.

`tests/unit_tests/test_mipi_dsi_sdk_patch.py` adds five isolated cases for the
two reviewed JPEG decoder variants, exact preservation of upstream 5.5.6 cleanup,
idempotence, unknown-source/version refusal and import safety. These tests and the
16-owner replay do not establish that the new SDK binary is safe on hardware.

Before publication is described as validated, complete these gates:

1. Fresh candidate imports and pinned Python dependencies; product config validation
   and codegen, including declared component sources and lifecycle actions.
2. Full native compile in a separate build directory; record resolved dependency
   versions, SDK source hashes, generated code, map/ELF and firmware identities.
   Check SDK patch ownership/IRAM and the P4 stack/notification configuration.
3. Flash and establish matching running identity. Test API/HA subscriptions, TLS
   paths, Hosted startup and OTA without relying on an old firmware observer.
4. Capture actual boot, Home, weather return/freeze, every carousel page, Settings
   color/snapshot refresh, Player, Assistant, Gallery and Camera screenshots.
   After restoring original preferences, verify a fresh complete Home window
   (`prepared=1`), the later accent batch `home=1/apps=1/pending=0`, and three
   allocated raw slots; successful preference readback or a live screenshot
   alone does not establish refreshed compressed backing readiness.
5. Compare like-for-like weather scenes, loop time, CPU/task load, free/largest
   memory blocks, cache lifecycle, DSI FIFO and DMA ownership counters. Sparse
   screenshot rates are not physical display FPS.
6. Exercise muted boot, playback continuity under graphics/network load, volume,
   wake/close lifecycle and microphone/reference ownership. Mark unavailable cloud
   conversation/acoustic tests explicitly rather than promoting UI tests.
7. Run repeated warmed restarts using new symbol addresses/full retained hashes;
   cold-power validation requires actual user power removal, including USB.
8. Verify both original input commits are ancestors, clean intended source changes,
   no secrets/build/cache artifacts in commits, and exact published remote heads.

The read-only promotion preread finds both canonical homes still clean at R25,
with normal fast-forward paths to the candidate branches. The product's shared
shallow boundary was removed by an authorized additive `fetch --unshallow
origin`; candidate/home HEADs and source files remain unchanged. The ESPHome
fork was already non-shallow. Exact remote tips remain unchanged and readable;
old fork `dev` stays intact, while clean official 7dace is planned for the new
`upstream/dev-2026-10-22` branch. Final committed F/P, an exact product workflow
pin to F and the unchanged strict final publication audit remain required.

The home fork interpreter is Python 3.13, imports the canonical home source and
passes `pip check` for its existing ESPHome `2026.8.0.dev0` / aioesphomeapi
`45.7.0` metadata. That is a baseline-environment sanity result, not current
source validation. The candidate environment is `2026.11.0.dev0` / `46.21.0`.
After the final clean home fast-forward, refresh the home editable installation
and dependencies with its own interpreter, verify metadata/import paths and
run configuration validation again. No package installation or environment
update is performed by this preread, and copying candidate site-packages is not
a promotion strategy.
