# Opt-in Number control host regressions

Run `test_entity_control_number.py`, `test_entity_control_number_codegen.py`, and `test_tile_number_routing.py` with the existing control tests. Supply `ESPHOME_TEST_CXX` (optional `ESPHOME_TEST_CXX_ARGS`) and `ESPHOME_TEST_ARDUINOJSON` as documented by `host_light_control.py`. No SDK, board, HA connection, network access, baseline checkout or machine-specific path is required.

The Number executable gets its explicit opt-in definitions through a temporary generated `esphome/core/defines.h`. Native-only tests link no Scalar unit; mixed tests link the real Scalar. The driver hashes production inputs and generated definitions before/after. Existing Component/Scalar assertions are compiled verbatim with the generic feature compiled but inactive. Their original default-off tests remain separate and unchanged. Codegen proves default-off feature definitions are absent.

Thirteen JSON fixtures mirror the HA `entity_control_v1` contract. Tests cover signed/fractional/min-anchored domains, unknown/off-grid values, semantic-generation renewal/ABA, captured step inputs, stale results, authoritative state ordering, strict hostile payloads and arena limits. Unit/title UTF-8 checks deliberately do not claim parity with every Python Unicode-printability category. Native font fallback is bounded; real glyph/geometry/visual QA is separate.

Tile tests add nine routing cases in a temporary copy of the existing83-case harness. They verify typed priority, no Number-to-Light/toggle fallback, captured kind and disable/re-enable cancellation. The existing source is hashed and remains unchanged.

Power-fence tests and sources are unchanged. Two mixed cases additionally prove held/deferred Power intent cannot survive a drained handoff into Number. Real Number input regression additionally uses caller-supplied `ESPHOME_TEST_LVGL`, `ESPHOME_TEST_CC` and optional `ESPHOME_TEST_CC_ARGS`, as the existing Power tests do. It covers disabled held buttons, first fresh contact, semantic cancellation, unchanged renewals, ignored stale packets and rejected revocations.

These host regressions do not establish P4 ABI, memory, latency, actual HA service/state, physical actuation or screenshot acceptance.
