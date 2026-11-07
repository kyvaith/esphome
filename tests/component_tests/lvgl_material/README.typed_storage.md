# Typed active storage regressions

Run `test_entity_control_storage.py` with the existing owner, codec and public
LVGL tests. The host compiler and ArduinoJson dependency use the same explicit
environment variables documented in `host_light_control.py`.

`ControlSnapshot` retains the common Light fields and exactly one conditional
family payload. Legacy Light uses `std::monostate`; explicit Light detail and
Climate share the existing Detail type. With Number disabled, the optional
variant is absent. No protocol discriminator is derived from `variant.index()`.

Reading through `family_if<T>()` never constructs data. A decoder explicitly
emplaces the selected family in its local temporary only for GRANT/STATE,
validates the whole message, then atomically publishes the result. Legacy Light
validation rejects an active family; Detail validates its common Light fields
separately. Outgoing messages retain full decode-based validation and existing
arena/wire limits.

OPEN changes the requested identity before GRANT, and revocation retains the
last observation. Neither the requested kind nor a pending Detail classifier
proves that the corresponding payload is present. Owner and UI access must
check the actual alternative. Borrowed pointers are reacquired after state
replacement, callbacks and backend handoff. There is no mutable fallback model.

The additional tests cover:

- Thirteen feature combinations, including Light-only, Number-only and minimal
  Detail, with exceptions/RTTI disabled; nullable lookup, exact copy/equality,
  value initialization and no-throw/trivial value traits.
- One hundred ordered owner transitions across the eight existing kinds plus
  explicit Light detail and Climate, retained state during pending/revoked
  periods, wrong-tag atomic rejection and callback reentry guards.
- One hundred ordered parser replacements, rejection of duplicate and late
  unexpected fields without replacing the previous alternative, and rejection
  of every incompatible outgoing kind/payload combination.

Existing fixtures initialize families explicitly. Reads use typed lookup and
must never silently emplace a family to make a test pass. Existing wire fixtures,
maximum catalogs, generation/ABA tests, priority Stop and public LVGL scenarios
remain the behavioral regression suite.

Host results do not establish embedded ABI sizes, target stack margins, a linked
firmware artifact, hardware timing, audio continuity or screenshot acceptance.
