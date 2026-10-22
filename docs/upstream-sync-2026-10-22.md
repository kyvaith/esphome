# ESPHome upstream reconciliation — 2026-10-22

This records the source audit and integration decisions for the Atmosfera ESPHome fork.
It is an integration checkpoint, not a claim that the complete firmware or every
upstream component has passed hardware validation. The validation table below must be
updated with the final build and device evidence before publication is described as
validated.

## Immutable inputs and history

| Input | Identity |
| --- | --- |
| Published fork checkpoint | `62806b28b7f216a70974e06c6520ae73fecd7cb2` |
| Published product checkpoint | `2903b1cad70d11d6010ee1f7877f2054a7dee51f` |
| Common ancestor | `a210c27d141ea411e3a6bc2a776cff88799cefc5`, 2026-07-23 |
| Official default branch | `dev`, confirmed from GitHub's symbolic `HEAD` |
| First official integration target | `ddcc5874ed5db795f20002289358dbfaae67c54c`, 2026-10-22 06:17:59 UTC |
| Latest official target at freeze | `f7dc2ecb51efe5c28298389ccc2de763d452e1af`, 2026-10-22 07:02:25 UTC |
| ESPHome version at target | `2026.11.0-dev` |
| History | Full, not shallow; 2,900 official commits after the common ancestor |
| Source range | 7,370 official changed files; 423 paths changed by the original fork |
| Initial merge | `f076d67f71ddfbd679d75a95adc23559321456aa` |
| Follow-up official merge | `cc62f65a9b6084ab5a5b960e8908f3170a29970b` |

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
Both the latest official target and the published fork pass ancestor checks at
the follow-up merge.

Two accompanying inventories make the audit reproducible:

- [All 423 original fork paths](upstream-sync-2026-10-22-files.csv), including their
  baseline, fork and official Git blob identities, subsystem, same-path upstream
  changes and integration policy.
- [All 2,900 official commits](upstream-sync-2026-10-22-commits.csv), including full
  commit identity, timestamp, subject, affected subsystems and path count.

Eighty original fork paths also changed upstream. The inventory includes 120
vendored `libjpeg-turbo-esp32` paths. Keeping an unchanged vendored decoder does not
mean that ESPHome gained 120 independent downstream features. The complete history
is catalogued; substantive source review concentrates on the components used by the
product and the reusable interfaces on which downstream additions depend. This is
not a claim that every unrelated upstream sensor was run or that every one of the
2,900 commits was individually tested.

## What arrived upstream

Counts below are commits touching each subsystem and overlap when one commit changes
several subsystems. They must not be added together as a unique commit total.

| Subsystem | Official commits | Relevant changes and decision |
| --- | ---: | --- |
| Core | 292 | Safer task deletion, internal TCB storage, explicit allocation helpers, fixed controller dispatch, main-loop wake isolation, flash-resident metadata, scheduler fixes. Keep upstream. |
| ESP32 | 87 | P4 stack workaround, crash cause/address/build evidence, SDK component and TLS trimming, NVS cache placement, CPU frequency handling and native build support. Keep upstream and retain narrowly guarded SDK corrections. |
| ESP32 Hosted | 20 | New dependencies, explicit 2.x/3.x support, minimum IDF gate, P4 ESP-NOW shim and OTA watchdog fixes. Keep upstream transport configuration. |
| Native ESP-IDF tools | 53 | Direct CMake/Ninja invocation, component discovery cache and local registry mirrors, Windows path/PCH fixes and incremental build improvements. Keep upstream converter. |
| Native API | 112 | Safer connection allocation, overflow lifetime fixes, bounded entity-list sending, protocol improvements, outgoing connections and onboarding wizard. Keep upstream. |
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

Both SDK fixes remain necessary on the 5.5.6 candidate:

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
  RAM options (`6580f4f314`, `ea76073d86`, `8060dadd6c`). The product's 600 ms
  buffer fits the official range. Remove duplicate downstream implementation.
- Keep official Sendspin codec preference, pairing, reconnect and metadata clearing
  improvements. Port the downstream mono-channel restriction, `artwork_url`, and
  artwork lifecycle around the current callback and frame-acknowledgement API.
- Retain the product's bounded hardware-JPEG/direct artwork path; the official
  image platform's storage and lifecycle are not automatically equivalent.

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
| Original 423 paths and official 2,900 commits inventoried | PASS |
| Core/Hosted/native focused tests | PASS: 562 passed, 2 skipped; see exact modules below |
| MIPI SDK adaptation tests | PASS: 5 passed; exact variants, rejection, idempotence and safe import |
| Complete 16-owner MIPI SDK replay | PASS on pristine 5.5.5, pristine 5.5.6 and captured partial 5.5.6 |
| Core Python lint | PASS for assigned Python changes, metadata, SDK patchers and new tests |
| Added metadata / profiler / legacy TLS changes | Source reviewed and focused Python lint passed; full build pending |
| Combined integration focused tests | PASS: 1,326 passed, 2 skipped at the initial official target; subsequent Wi-Fi/Ethernet checks are tracked separately |
| Complete native product build and resolved dependencies | Pending |
| Flash/hash confirmation for merged firmware | Pending |
| Device screenshots, visual/performance/memory scenarios | Pending |
| Current merged-firmware warmed/cold restarts | Pending; previous R25 results do not validate this firmware |
| Final merge ancestry | PASS: latest official target and original fork are ancestors of the follow-up merge |
| Remote publication verification | Pending |

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
