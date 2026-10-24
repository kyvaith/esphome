"""Guarded, default-off memory policy for the reviewed Sendspin 0.9.3 sources."""

import hashlib
from pathlib import Path
import sys

GUARD = "CONFIG_ESPHOME_SENDSPIN_MEMORY_OPTIMIZATIONS"

# The originals are from the immutable sendspin/sendspin-cpp 0.9.3 component.
PRISTINE_SHA256 = {
    "src/inbound_ring.h": "489b86808a7558116dabbbc5745eaaa824eaa91a336e18d361b0edfd43ea2d38",
    "src/client.cpp": "d3154cf283ac33a38e51a281f8e4c020f115ffd58839f532fea04b17d60a347b",
    "src/artwork_role.cpp": "cda8071b86df55147b891d64eaa40d8b4602d904d0189468e754e65042670b43",
}
GUARDED_SHA256 = {
    "src/inbound_ring.h": "a39c7dcb3a073f925187d625ab2fb6d6b54a205c5dbb141dbc500379a904fd19",
    "src/client.cpp": "d6367099f67c5e94e37d325a03d1363cb3551114fbc2e7195e9e7a536ab6e4f8",
    "src/artwork_role.cpp": "f2585b9fcb5a61db199708f62e7f1c819da78dc2bf49a84d787c137e7337c109",
}

PCM_RATE = """/// PCM has a known payload rate; compressed or invalid formats retain the conservative budget.
static inline size_t inbound_min_audio_stored_bytes_per_second(
    const std::vector<AudioSupportedFormatObject>& formats) {
    size_t minimum = std::numeric_limits<size_t>::max();
    if (formats.empty()) {
        return INBOUND_MIN_AUDIO_STORED_BYTES_PER_SECOND;
    }
    for (const auto& format : formats) {
        if (format.codec != SendspinCodecFormat::PCM || format.sample_rate == 0 ||
            format.channels == 0 || format.bit_depth == 0 || format.bit_depth % 8 != 0) {
            return INBOUND_MIN_AUDIO_STORED_BYTES_PER_SECOND;
        }
        const uint64_t rate = static_cast<uint64_t>(format.sample_rate) * format.channels *
                              (format.bit_depth / 8);
        if (rate > std::numeric_limits<size_t>::max()) {
            return INBOUND_MIN_AUDIO_STORED_BYTES_PER_SECOND;
        }
        minimum = std::min(minimum, static_cast<size_t>(rate));
    }
    // Ignoring chunk overhead is conservative: the same quota holds no more PCM seconds.
    return minimum;
}

"""

CAPACITY_FLOOR = """/// Keep the original advertised audio capacity, including its ring-item size limit.
static constexpr size_t derive_inbound_ring_bytes_preserving_audio_capacity(
    const InboundRingBudget& budget, size_t player_capacity_share) {
    InboundRingBudget legacy = budget;
    legacy.audio_stored_bytes_per_second = INBOUND_MIN_AUDIO_STORED_BYTES_PER_SECOND;
    const size_t original = derive_inbound_ring_bytes(legacy);
    const size_t audio_floor = player_capacity_share == 0
                                  ? 0
                                  : inbound_ring_min_storage_bytes(player_capacity_share + AEAD_TAG_SIZE);
    return std::max(derive_inbound_ring_bytes(budget), std::min(original, audio_floor));
}

"""

ASSEMBLY_ADMISSION = """    // The previous pending image was discarded above; no callback retains this pointer.
    if (announce.total_size > 0 &&
        !assembly.buffer.allocate(announce.total_size, MemoryLocation::PREFER_EXTERNAL)) {
        SS_LOGW(TAG, "Cannot allocate %" PRIu32 " artwork bytes on channel %u; keeping current image",
                announce.total_size, slot);
        return;
    }
"""


def guarded(fixed: str, original: str = "") -> str:
    return (
        f"#if {GUARD}\n"
        + fixed
        + ("#else\n" + original if original else "")
        + "#endif\n"
    )


def replacements() -> dict[str, tuple[tuple[str, str], ...]]:
    eager = """    for (size_t i = 0; i < this->config.preferred_formats.size(); ++i) {
        const uint32_t cap = this->config.preferred_formats[i].max_image_bytes;
        if (cap > 0 && !this->drain_task->assemblies[i].buffer.allocate(
                           cap, MemoryLocation::PREFER_EXTERNAL)) {
            SS_LOGE(TAG, "Failed to allocate the %" PRIu32 " byte artwork buffer for channel %zu",
                    cap, i);
            release_buffers();
            return false;
        }
    }
"""
    return {
        "src/inbound_ring.h": (
            (
                '#include "platform/shared_ring_buffer.h"\n',
                (
                    '#include "platform/shared_ring_buffer.h"\n'
                    '#ifdef ESP_PLATFORM\n#include "sdkconfig.h"\n#endif\n'
                ),
            ),
            (
                "/// Ring storage budgeted per server/time reply:",
                guarded(PCM_RATE) + "/// Ring storage budgeted per server/time reply:",
            ),
            (
                "    size_t audio_hold_bytes{0};\n",
                "    size_t audio_hold_bytes{0};\n"
                + guarded(
                    "    size_t audio_stored_bytes_per_second{INBOUND_MIN_AUDIO_STORED_BYTES_PER_SECOND};\n"
                ),
            ),
            (
                (
                    "    const size_t player_hold_seconds =\n"
                    "        inbound_hold_seconds(budget.audio_hold_bytes, INBOUND_MIN_AUDIO_STORED_BYTES_PER_SECOND);\n"
                ),
                guarded(
                    "    const size_t player_hold_seconds =\n"
                    "        inbound_hold_seconds(budget.audio_hold_bytes, budget.audio_stored_bytes_per_second);\n",
                    "    const size_t player_hold_seconds =\n"
                    "        inbound_hold_seconds(budget.audio_hold_bytes, INBOUND_MIN_AUDIO_STORED_BYTES_PER_SECOND);\n",
                ),
            ),
            (
                "}  // namespace sendspin\n",
                guarded(CAPACITY_FLOOR) + "}  // namespace sendspin\n",
            ),
        ),
        "src/client.cpp": (
            (
                "    size_t player_message_bytes = 0;\n",
                "    size_t player_message_bytes = 0;\n"
                + guarded("    size_t player_capacity_share = 0;\n"),
            ),
            (
                "        budget.audio_hold_bytes = this->player_->impl_->config.audio_buffer_capacity;\n",
                "        budget.audio_hold_bytes = this->player_->impl_->config.audio_buffer_capacity;\n"
                + guarded(
                    "        budget.audio_stored_bytes_per_second = inbound_min_audio_stored_bytes_per_second(\n"
                    "            this->player_->impl_->config.audio_formats);\n"
                    "        player_capacity_share = this->player_->impl_->buffer_capacity_share();\n"
                ),
            ),
            (
                "    const size_t storage_bytes = derive_inbound_ring_bytes(budget);\n",
                guarded(
                    "    const size_t storage_bytes =\n"
                    "        derive_inbound_ring_bytes_preserving_audio_capacity(budget, player_capacity_share);\n",
                    "    const size_t storage_bytes = derive_inbound_ring_bytes(budget);\n",
                ),
            ),
        ),
        "src/artwork_role.cpp": (
            (
                '#include "artwork_role_impl.h"\n',
                (
                    '#include "artwork_role_impl.h"\n'
                    '#ifdef ESP_PLATFORM\n#include "sdkconfig.h"\n#endif\n'
                ),
            ),
            (eager, f"#if !{GUARD}\n" + eager + "#endif\n"),
            (
                "    assembly.state = ArtworkAssembly::State::ASSEMBLING;\n",
                guarded(ASSEMBLY_ADMISSION)
                + "    assembly.state = ArtworkAssembly::State::ASSEMBLING;\n",
            ),
            (
                "    this->drain_task->assemblies[slot].state = ArtworkAssembly::State::IDLE;\n",
                "    this->drain_task->assemblies[slot].state = ArtworkAssembly::State::IDLE;\n"
                + guarded("    this->drain_task->assemblies[slot].buffer.reset();\n"),
            ),
            (
                "        assembly.state = ArtworkAssembly::State::IDLE;\n        return;\n",
                "        assembly.state = ArtworkAssembly::State::IDLE;\n"
                + guarded("        assembly.buffer.reset();\n")
                + "        return;\n",
            ),
            (
                "    // Hand off the timestamp to the main loop. Skip if the stream ended while we were\n",
                guarded(
                    "    // Encoded input is borrowed only for the synchronous decode callback.\n"
                    "    assembly.buffer.reset();\n"
                )
                + "    // Hand off the timestamp to the main loop. Skip if the stream ended while we were\n",
            ),
        ),
    }


def transform_source(source: str, changes: tuple[tuple[str, str], ...]) -> str:
    for old, new in changes:
        if source.count(old) != 1:
            raise RuntimeError("Sendspin owner changed; review the 0.9.3 memory patch")
        source = source.replace(old, new, 1)
    return source


def prepare_sources(component_dir: Path) -> dict[Path, bytes]:
    manifest = (component_dir / "idf_component.yml").read_text(encoding="utf-8")
    if "version: 0.9.3\n" not in manifest:
        raise RuntimeError(
            "Sendspin memory policy requires the reviewed 0.9.3 component"
        )
    outputs = {}
    # Validate every pre/postimage before writing any owner.
    for name, changes in replacements().items():
        target = component_dir / name
        original = target.read_bytes()
        digest = hashlib.sha256(original).hexdigest()
        if digest == GUARDED_SHA256[name]:
            continue
        if digest != PRISTINE_SHA256[name]:
            raise RuntimeError(f"Unreviewed Sendspin source: {name}")
        output = transform_source(original.decode("utf-8"), changes).encode("utf-8")
        if hashlib.sha256(output).hexdigest() != GUARDED_SHA256[name]:
            raise RuntimeError(f"Unreviewed Sendspin postimage: {name}")
        outputs[target] = output
    return outputs


def patch_component(component_dir: Path) -> int:
    outputs = prepare_sources(component_dir)
    for target, data in outputs.items():
        target.write_bytes(data)
    return len(outputs)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch_sendspin_memory.py SENDSPIN_COMPONENT_DIR")
    print(
        f"ESPHome Sendspin memory patch: {patch_component(Path(sys.argv[1]))} guarded owners installed"
    )
