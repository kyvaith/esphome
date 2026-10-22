"""Install the reviewed, default-off ESP-IDF P4 PIE context save/restore fix.

Enabled regions are exact copies of espressif/esp-idf commit
b25cb2906c7db70a86332dffbe77df64345d8a73 (Apache-2.0).
The disabled branch preserves each reviewed original region verbatim.
"""

from dataclasses import dataclass
import hashlib
import os
from pathlib import Path
import sys
import tempfile

OFFICIAL_COMMIT = "b25cb2906c7db70a86332dffbe77df64345d8a73"
REVIEWED_VERSIONS = frozenset({"v5.5.5", "v5.5.6"})
CONFIG_OPTION = "CONFIG_ESPHOME_ESP32P4_PIE_CONTEXT_FIX"

PIE_MACROS_ORIGINAL = r""".macro pie_save_regs frame=a0
    /* Save the 128-bit Q registers from the frame memory and then frame += 16 */
    esp.vst.128.ip  q0, \frame, 16
    esp.vst.128.ip  q1, \frame, 16
    esp.vst.128.ip  q2, \frame, 16
    esp.vst.128.ip  q4, \frame, 16
    esp.vst.128.ip  q5, \frame, 16
    esp.vst.128.ip  q6, \frame, 16
    esp.vst.128.ip  q7, \frame, 16
    /* Save the QACC_H and QACC_L registers, each being 256 bits big */
    esp.st.qacc.l.l.128.ip \frame, 16
    esp.st.qacc.l.h.128.ip \frame, 16
    esp.st.qacc.h.l.128.ip \frame, 16
    esp.st.qacc.h.h.128.ip \frame, 16
    /* UA_STATE register (128 bits) */
    esp.st.ua.state.ip \frame, 16
    /* XACC register (40 bits) */
    esp.st.u.xacc.ip \frame, 8
    /* The following registers will be stored in the same word */
    /* SAR register (6 bits) */
    esp.movx.r.sar a1
    slli a2, a1, 8
    /* SAR_BYTES register (4 bits) */
    esp.movx.r.sar.bytes a1
    slli a1, a1, 4
    or   a2, a2, a1
    /* FFT_BIT_WIDTH register (4 bits) */
    esp.movx.r.fft.bit.width a1
    or  a2, a2, a1
    sw  a2, (\frame)
.endm


.macro pie_restore_regs frame=a0
    /* Restore the 128-bit Q registers from the frame memory and then frame += 16 */
    esp.vld.128.ip  q0, \frame, 16
    esp.vld.128.ip  q1, \frame, 16
    esp.vld.128.ip  q2, \frame, 16
    esp.vld.128.ip  q4, \frame, 16
    esp.vld.128.ip  q5, \frame, 16
    esp.vld.128.ip  q6, \frame, 16
    esp.vld.128.ip  q7, \frame, 16
    /* Save the QACC_H and QACC_L registers, each being 256 bits big */
    esp.ld.qacc.l.l.128.ip \frame, 16
    esp.ld.qacc.l.h.128.ip \frame, 16
    esp.ld.qacc.h.l.128.ip \frame, 16
    esp.ld.qacc.h.h.128.ip \frame, 16
    /* UA_STATE register (128 bits) */
    esp.ld.ua.state.ip \frame, 16
    /* XACC register (40 bits) */
    esp.ld.xacc.ip \frame, 8
    /* The following registers are stored in the same word */
    lw  a2, (\frame)
    /* FFT_BIT_WIDTH register (4 bits) */
    andi a1, a2, 0xf
    esp.movx.w.sar a1
    /* SAR_BYTES register (4 bits) */
    srli a2, a2, 4
    andi a1, a2, 0xf
    esp.movx.w.sar.bytes a1
    /* SAR register (6 bits) */
    srli a2, a2, 4
    andi a1, a2, 0x3f
    esp.movx.w.fft.bit.width a1
.endm"""

PIE_MACROS_FIXED = r""".macro pie_save_regs frame=a0
    /* Save the 128-bit Q registers from the frame memory and then frame += 16 */
    esp.vst.128.ip  q0, \frame, 16
    esp.vst.128.ip  q1, \frame, 16
    esp.vst.128.ip  q2, \frame, 16
    esp.vst.128.ip  q3, \frame, 16
    esp.vst.128.ip  q4, \frame, 16
    esp.vst.128.ip  q5, \frame, 16
    esp.vst.128.ip  q6, \frame, 16
    esp.vst.128.ip  q7, \frame, 16
    /* Save the QACC_H and QACC_L registers, each being 256 bits big */
    esp.st.qacc.l.l.128.ip \frame, 16
    esp.st.qacc.l.h.128.ip \frame, 16
    esp.st.qacc.h.l.128.ip \frame, 16
    esp.st.qacc.h.h.128.ip \frame, 16
    /* UA_STATE register (128 bits) */
    esp.st.ua.state.ip \frame, 16
    /* XACC register (40 bits)
     *
     * Bit layout across two 32-bit words (total 64 bits, but only 40 bits are used):
     *
     * Word 0 (Lower 32 bits):
     *  +------+------+------+------+------+------+------+------+ 32
     *  | res  | res  | res  | res  | res  | res  | res  | res  |
     *  +------+------+------+------+------+------+------+------+ 24
     *  | res  | res  | sar  | sar  | sar  | sar  | sar  | sar  |
     *  +------+------+------+------+------+------+------+------+ 16
     *  | sarB | sarB | sarB | sarB | fftW | fftW | fftW | fftW |
     *  +------+------+------+------+------+------+------+------+ 8
     *  | xacc | xacc | xacc | xacc | xacc | xacc | xacc | xacc |
     *  +------+------+------+------+------+------+------+------+ 0
     *
     * Word 1 (Upper 32 bits, only lower 8 bits used):
     *  +------+------+------+------+------+------+------+------+ 32
     *  | xacc | xacc | xacc | xacc | xacc | xacc | xacc | xacc |
     *  +------+------+------+------+------+------+------+------+ 24
     *  | xacc | xacc | xacc | xacc | xacc | xacc | xacc | xacc |
     *  +------+------+------+------+------+------+------+------+ 16
     *  | xacc | xacc | xacc | xacc | xacc | xacc | xacc | xacc |
     *  +------+------+------+------+------+------+------+------+ 8
     *  | xacc | xacc | xacc | xacc | xacc | xacc | xacc | xacc |
     *  +------+------+------+------+------+------+------+------+ 0
     *
     * Legend:
     *  - `xacc`  = xacc bits
     *  - `sar`   = sar bits
     *  - `sarB`  = sar_bytes bits
     *  - `fftW`  = FFT bit width bits
     *  - `res`   = Reserved bits
     *
     */

    /* Pointer not increased to write data as tightly as possible (minimum increment is 8). */
    esp.st.u.xacc.ip \frame, 0

    /* SAR_BYTES and FFT_BIT_WIDTH registers
     * Prepare the 8-bit value: (SAR_BYTES << 4) | FFT_BIT_WIDTH */
    esp.movx.r.sar.bytes a1      /* Load SAR_BYTES register (4 bits) */
    slli  a2, a1, 4              /* a2 = (SAR_BYTES << 4) */

    esp.movx.r.fft.bit.width a1  /* Load FFT_BIT_WIDTH register (4 bits) */
    or    a2, a2, a1             /* a2 |= FFT_BIT_WIDTH */

    sb    a2, 5(\frame)          /* Store byte after the XACC data */

    /* SAR register (6 bits) */
    esp.movx.r.sar a1
    sb    a1, 6(\frame)          /* Store byte after the SAR_BYTES and FFT_BIT_WIDTH */
.endm


.macro pie_restore_regs frame=a0
    /* Restore the 128-bit Q registers from the frame memory and then frame += 16 */
    esp.vld.128.ip  q0, \frame, 16
    esp.vld.128.ip  q1, \frame, 16
    esp.vld.128.ip  q2, \frame, 16
    esp.vld.128.ip  q3, \frame, 16
    esp.vld.128.ip  q4, \frame, 16
    esp.vld.128.ip  q5, \frame, 16
    esp.vld.128.ip  q6, \frame, 16
    esp.vld.128.ip  q7, \frame, 16
    /* Save the QACC_H and QACC_L registers, each being 256 bits big */
    esp.ld.qacc.l.l.128.ip \frame, 16
    esp.ld.qacc.l.h.128.ip \frame, 16
    esp.ld.qacc.h.l.128.ip \frame, 16
    esp.ld.qacc.h.h.128.ip \frame, 16
    /* UA_STATE register (128 bits) */
    esp.ld.ua.state.ip \frame, 16
    /* XACC register (40 bits)
     * Pointer not increased because the minimum step is 8, preventing data loss */
    esp.ld.xacc.ip \frame, 0

    /* The following registers are packed in the same word (addr: frame + 4):
     * - XACC (upper byte)  [7..0]
     * - FFT_BIT_WIDTH      [11..8]
     * - SAR_BYTES          [15..12]
     * - SAR                [21..16]
     *
     * See pie_save_regs macro for more details.
     */
    lbu   a1, 5(\frame) /* Load packed FFT_BIT_WIDTH and SAR_BYTES from offset 5
                         * (frame points to the start of the 5-byte XACC) */

    /* FFT_BIT_WIDTH register (4 bits) */
    esp.movx.w.fft.bit.width a1 /* FFT_BIT_WIDTH[3:0] = rs1[3:0] */

    /* SAR_BYTES register (4 bits) */
    srli  a1, a1, 4
    esp.movx.w.sar.bytes a1     /* SAR_BYTE[3:0] = rs1[3:0] */

    /* SAR register (6 bits) */
    lbu   a1, 6(\frame)
    esp.movx.w.sar a1           /* SAR[5:0] = rs1[5:0] */
.endm"""

PIE_FIELD_ORIGINAL = r"""/* This register contains SAR, SAR_BYTES and FFT_BIT_WIDTH in this order (from top to low) */
STRUCT_FIELD  (long,    4,  RV_PIE_MISC,      misc)"""

PIE_FIELD_FIXED = r"""/* misc field contains registers: XACC (upper byte) [7:0], FFT_BIT_WIDTH [11:8], SAR_BYTES [15:12], and SAR [21:16] */
STRUCT_FIELD  (long, 4,  RV_PIE_MISC,      misc)"""


@dataclass(frozen=True)
class ReviewedSource:
    path: str
    pristine_sha256: str
    guarded_sha256: str
    fixed_sha256: str
    before: str
    enabled: str


SOURCES = (
    ReviewedSource(
        path="components/freertos/FreeRTOS-Kernel/portable/riscv/portasm.S",
        pristine_sha256="0bfd43a09e877654b2e6c53de6a7dd97ac98139c603c68b895509b9fd027e479",
        guarded_sha256="964ee4a1b4c9c89c61ff4b13406b23ad0ccde5849a55b370e1b504f577f0dfb6",
        fixed_sha256="65fca25175c22bbeb0d659e82a012e8b46216775572081a94a128ef64175e3a2",
        before=PIE_MACROS_ORIGINAL,
        enabled=PIE_MACROS_FIXED,
    ),
    ReviewedSource(
        path="components/riscv/include/riscv/rvruntime-frames.h",
        pristine_sha256="a5a493c78dd574b995fd7442816902176b0a52bec841b057b863a1f0ff0813c0",
        guarded_sha256="7b25f3cec64031fab73f1892561e98062dd24246314c33363dcfa13561beebeb",
        fixed_sha256="6eb34b2c5d5c22bf1c1745fdfc6c345810f42ac0623d466c1a0eeaab87b77066",
        before=PIE_FIELD_ORIGINAL,
        enabled=PIE_FIELD_FIXED,
    ),
)


def guarded_source(original: bytes, source: ReviewedSource) -> bytes:
    """Accept only exact reviewed pristine or already guarded owner files."""
    digest = hashlib.sha256(original).hexdigest()
    if digest == source.guarded_sha256:
        return original
    if digest != source.pristine_sha256:
        raise RuntimeError(
            f"ESP32-P4 PIE source changed: {source.path}; review this backport"
        )
    text = original.decode("utf-8")
    if text.count(source.before) != 1:
        raise RuntimeError(f"Reviewed PIE owner region missing: {source.path}")
    fixed = text.replace(source.before, source.enabled, 1).encode("utf-8")
    if hashlib.sha256(fixed).hexdigest() != source.fixed_sha256:
        raise RuntimeError(f"Official PIE region postimage mismatch: {source.path}")
    replacement = (
        f"#if {CONFIG_OPTION}\n{source.enabled}\n#else\n{source.before}\n#endif"
    )
    result = text.replace(source.before, replacement, 1).encode("utf-8")
    if hashlib.sha256(result).hexdigest() != source.guarded_sha256:
        raise RuntimeError(f"Guarded PIE postimage mismatch: {source.path}")
    return result


def _stage_bytes(target: Path, data: bytes) -> Path:
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(
            dir=target.parent, prefix=target.name + ".", suffix=".tmp", delete=False
        ) as stream:
            temporary = Path(stream.name)
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
    except BaseException:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
        raise
    return temporary


def _write_transaction(updates: list[tuple[Path, bytes, bytes]]) -> None:
    """Stage both files before commit; restore replaced originals on failure."""
    staged = []
    temporary_paths = []
    committed = []
    recovery_paths = set()
    try:
        for target, before, after in updates:
            rollback = _stage_bytes(target, before)
            temporary_paths.append(rollback)
            replacement = _stage_bytes(target, after)
            temporary_paths.append(replacement)
            staged.append((target, before, after, rollback, replacement))
        # A concurrent source change must be reviewed, not overwritten.
        for target, before, _after, _rollback, _replacement in staged:
            if target.read_bytes() != before:
                raise RuntimeError(f"PIE source changed while staging: {target}")
        for target, _before, _after, rollback, replacement in staged:
            replacement.replace(target)
            committed.append((target, rollback))
        for target, _before, after, _rollback, _replacement in staged:
            if target.read_bytes() != after:
                raise RuntimeError(f"PIE source changed during commit: {target}")
    except BaseException as error:
        rollback_errors = []
        for target, rollback in reversed(committed):
            try:
                rollback.replace(target)
            except OSError as rollback_error:
                recovery_paths.add(rollback)
                rollback_errors.append(
                    f"{target}: {rollback_error}; original retained at {rollback}"
                )
        if rollback_errors:
            raise RuntimeError(
                "PIE patch rollback failed; stop SDK use and recover originals: "
                + "; ".join(rollback_errors)
            ) from error
        raise
    finally:
        for temporary in temporary_paths:
            if temporary not in recovery_paths:
                temporary.unlink(missing_ok=True)


def patch_framework(framework_dir: Path, idf_target: str) -> str:
    """Validate target, version and both full source images before any mutation."""
    if idf_target != "esp32p4":
        raise RuntimeError("PIE context backport requires reviewed target esp32p4")
    version = (framework_dir / "version.txt").read_text(encoding="utf-8").strip()
    if version not in REVIEWED_VERSIONS:
        raise RuntimeError(
            "PIE context backport requires reviewed ESP-IDF 5.5.5 or 5.5.6"
        )
    updates = []
    for source in SOURCES:
        target = framework_dir / source.path
        original = target.read_bytes()
        guarded = guarded_source(original, source)
        if guarded != original:
            updates.append((target, original, guarded))
    if not updates:
        return "already guarded"
    _write_transaction(updates)
    return "installed guarded PIE context save/restore"


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("Usage: patch_pie_context.py ESP_IDF_PATH IDF_TARGET")
    print(
        "ESPHome ESP32-P4 PIE context patch: "
        + patch_framework(Path(sys.argv[1]), sys.argv[2])
    )


if __name__ == "__main__":
    main()
