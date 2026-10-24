// Copyright 2026 Sendspin Contributors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/// @file inbound_ring.h
/// @brief The shared inbound ring: its item layout, the ring wrapper the transports and the
/// protocol task use, the intrusive per-consumer item list, the per-role quotas, the
/// per-connection inbound gate, and the ring's size derivation
///
/// Every inbound WebSocket message of an admitted connection lands in one ring item: an
/// InboundItemHeader followed by the message bytes as received, which the protocol task decrypts
/// in place. An item the protocol task hands to a consumer (a player audio chunk, a visualizer
/// frame, an artwork image part, or an item it writes itself) stays in the ring and is linked onto
/// that consumer's InboundItemList through its own header, so no descriptor storage exists outside
/// the ring items. The consumer returns the item when it is done with it. An unadmitted connection
/// never writes into the ring (see InboundGate).

#pragma once

// Reviewed 0.9.3 budget owners; patch source edits are applied before execution.
/// @brief Ring storage one item holding a message of `message_len` bytes occupies
static constexpr size_t inbound_item_stored_bytes(size_t message_len) {
    return SharedRingLayout::stored_size(sizeof(InboundItemHeader) + message_len);
}

/// @brief Ring storage below which a message of `largest_message_bytes` no longer fits: a
/// FreeRTOS no-split ring accepts an item of at most half its storage
/// (SharedRingLayout::max_item_size()), so the floor is two such items.
static constexpr size_t inbound_ring_min_storage_bytes(size_t largest_message_bytes) {
    return 2 * inbound_item_stored_bytes(largest_message_bytes);
}

/// The floor of a ring that accepts a maximal message (131,152 bytes).
static constexpr size_t INBOUND_RING_MIN_STORAGE_BYTES =
    inbound_ring_min_storage_bytes(INBOUND_MAX_MESSAGE_BYTES);
static_assert(SharedRingLayout::max_item_size(INBOUND_RING_MIN_STORAGE_BYTES) >=
                  sizeof(InboundItemHeader) + INBOUND_MAX_MESSAGE_BYTES,
              "the minimum ring must accept a maximal message");
static_assert(
    SharedRingLayout::max_item_size(inbound_ring_min_storage_bytes(INBOUND_JSON_MESSAGE_BYTES)) >=
        sizeof(InboundItemHeader) + INBOUND_JSON_MESSAGE_BYTES,
    "the minimum JSON-only ring must accept a maximal JSON message");

/// The baseline pass-through allowance every configuration pays, on top of the per-second budget
/// below: largest messages (JSON, or a protocol message the task returns at once) that can arrive
/// behind a held item. Two lets one arrive while the previous is still being processed.
static constexpr size_t INBOUND_PASSTHROUGH_MESSAGES = 2;

/// Bytes of an audio chunk message ahead of its encoded frame: the message type byte and the
/// 12-byte header (roles/player/v1.md "Audio Chunks (Binary)": 8-byte timestamp, 4-byte
/// send_ahead).
static constexpr size_t INBOUND_AUDIO_CHUNK_HEADER_BYTES = 13;

/// The smallest encoded audio frame the player's advertised share and the ring's hold window are
/// derived for: 20 ms of Opus at 64 kbit/s. PCM never goes below it (8 kHz mono 16-bit is 320 B
/// per 20 ms). Lower-rate Opus, and FLAC over near-silence, send smaller frames: the player's
/// quota then holds more seconds of audio than derived here, which pins more pass-through
/// traffic behind it (see derive_inbound_ring_bytes()).
static constexpr size_t INBOUND_MIN_AUDIO_FRAME_BYTES = 160;

/// Frames per second at INBOUND_MIN_AUDIO_FRAME_BYTES: one every 20 ms, Opus's default frame.
static constexpr size_t INBOUND_MIN_AUDIO_FRAMES_PER_SECOND = 50;

/// The lowest audio rate the library budgets for, in encoded bytes per second (64 kbit/s).
static constexpr size_t MIN_ADVERTISED_AUDIO_BYTES_PER_SECOND =
    INBOUND_MIN_AUDIO_FRAME_BYTES * INBOUND_MIN_AUDIO_FRAMES_PER_SECOND;

/// Ring storage per second of audio at MIN_ADVERTISED_AUDIO_BYTES_PER_SECOND: each frame stored
/// as one item with its chunk header and the stored overhead.
static constexpr size_t INBOUND_MIN_AUDIO_STORED_BYTES_PER_SECOND =
    (INBOUND_MIN_AUDIO_FRAME_BYTES + INBOUND_AUDIO_CHUNK_HEADER_BYTES +
     INBOUND_ITEM_STORED_OVERHEAD_BYTES) *
    INBOUND_MIN_AUDIO_FRAMES_PER_SECOND;

/// Ring storage budgeted per server/time reply: its JSON, well under 256 bytes, in one item.
static constexpr size_t INBOUND_TIME_REPLY_STORED_BYTES =
    SharedRingLayout::stored_size(sizeof(InboundItemHeader) + 256 + AEAD_TAG_SIZE);

/// Pass-through JSON budgeted per second apart from time replies: about one 1 KB message a second
/// (server/state for metadata, controller or color, group/update, server/command). Track metadata
/// with long strings is larger but arrives once per track.
static constexpr size_t INBOUND_STATE_BYTES_PER_SECOND = 1024;

/// The shortest track the artwork budget assumes: one track change, and so one new image per
/// artwork channel, every 30 seconds. An assumption about listening, not a protocol bound: a
/// listener skipping tracks faster than this pins more images than budgeted, which the transport
/// reports as "ring pinned behind held items" when it closes the connection.
static constexpr size_t INBOUND_MIN_TRACK_SECONDS = 30;

/// Images per artwork channel the artwork decode thread's quota covers: one in flight. The decode
/// thread copies each part into its channel's assembly buffer as it takes it and returns the item
/// at once, gate open or closed, so its parts wait in the ring only while the thread is busy (in
/// on_image_decode()), and the server sends one transfer at a time. The quota is one image: if a
/// second image for a channel arrives behind a complete one still queued while the thread is
/// inside a long decode, its parts go over the quota and it is dropped, and the channel keeps its
/// current image until the server sends the next one.
///
/// A third image arriving while the decode callback is still inside the first one is dropped at
/// the quota too, but its refused parts stay pinned behind the queued second image. With a hold
/// window of INBOUND_MIN_TRACK_SECONDS or more (the default player's) the ring carries further
/// images behind the held items (see derive_inbound_ring_bytes()). With none (artwork alone, or
/// a visualizer holding under INBOUND_MIN_TRACK_SECONDS) the ring has about one baseline message
/// of room behind the pinned parts, so a stall long enough for time replies and state JSON to
/// arrive behind them can exhaust the ring, and the connection closes after
/// INBOUND_ACQUIRE_TIMEOUT_MS. That needs a decode callback blocked for about two image cadences
/// (around a minute at one image per INBOUND_MIN_TRACK_SECONDS) or track skips faster than
/// INBOUND_MIN_TRACK_SECONDS, which the derivation treats as beyond its budget.
static constexpr size_t INBOUND_ARTWORK_IN_FLIGHT_IMAGES = 1;

/// Bytes of an artwork part message ahead of its image data: the type byte, then the flags byte
/// (roles/artwork/v1.md "Artwork (Binary)").
static constexpr size_t INBOUND_ARTWORK_PART_HEADER_BYTES = 2;

/// The most ring storage an artwork part costs beyond its image data: the part header and the
/// stored overhead of its item.
static constexpr size_t INBOUND_ARTWORK_PART_STORED_OVERHEAD_BYTES =
    INBOUND_ITEM_STORED_OVERHEAD_BYTES + INBOUND_ARTWORK_PART_HEADER_BYTES;

/// The smallest image part, in data bytes, the artwork quota is derived for. The quota charges
/// each part its stored size (the item headers, the type and flags bytes, the Noise tag and the
/// alignment padding: INBOUND_ARTWORK_PART_STORED_OVERHEAD_BYTES), so an image within
/// ImageSlotPreference::max_image_bytes split into parts smaller than this costs more than the
/// quota and is dropped while the decode thread is busy. The reference server sends maximal parts
/// (65,517 bytes).
static constexpr size_t INBOUND_ARTWORK_MIN_PART_BYTES = 4096;

/// @brief Ring storage one image of up to `max_image_bytes` occupies, sent in parts of at least
/// INBOUND_ARTWORK_MIN_PART_BYTES (the last one may be shorter): its data plus a part's stored
/// overhead for each of at most ceil(max_image_bytes / INBOUND_ARTWORK_MIN_PART_BYTES) parts.
static constexpr size_t inbound_artwork_image_stored_bytes(size_t max_image_bytes) {
    const size_t max_parts =
        (max_image_bytes + INBOUND_ARTWORK_MIN_PART_BYTES - 1) / INBOUND_ARTWORK_MIN_PART_BYTES;
    return max_image_bytes + max_parts * INBOUND_ARTWORK_PART_STORED_OVERHEAD_BYTES;
}

/// @brief The configuration figures the ring size is derived from
///
/// A holder's quota counts each item's stored overhead (SharedRingLayout::ITEM_HEADER_BYTES,
/// the InboundItemHeader, the Noise type byte, the in-band timestamp and the AEAD tag) as well
/// as its payload, so the payload a role can have outstanding is its quota less that overhead
/// per item. The share of its buffer a role advertises to the server must therefore be derived
/// from that stored overhead against a stated minimum chunk size, not assumed to fit.
struct InboundRingBudget {
    /// The player's quota: PlayerRoleConfig::audio_buffer_capacity. 0 without the player role.
    size_t audio_hold_bytes{0};
    /// The visualizer's quota: VisualizerSupportObject::buffer_capacity. 0 without the visualizer
    /// role.
    size_t visualizer_hold_bytes{0};
    /// Ring storage the visualizer's requested frames arrive at per second: every requested type
    /// at rate_max, each frame at its stored size (VisualizerRole's
    /// stored_frame_bytes_per_second()). 0 without the visualizer role.
    size_t visualizer_stored_bytes_per_second{0};
    /// Ring storage one image per artwork channel takes: the sum over the channels of
    /// inbound_artwork_image_stored_bytes(ImageSlotPreference::max_image_bytes). 0 without the
    /// artwork role.
    size_t artwork_images_stored_bytes{0};
    /// The artwork role's quota: INBOUND_ARTWORK_IN_FLIGHT_IMAGES * artwork_images_stored_bytes. 0
    /// without the artwork role.
    size_t artwork_hold_bytes{0};
    /// SendspinClientConfig::time_burst_size: server/time replies per burst.
    size_t time_burst_size{SendspinClientConfig::DEFAULT_BURST_SIZE};
    /// SendspinClientConfig::time_burst_interval_ms: milliseconds between bursts.
    int64_t time_burst_interval_ms{SendspinClientConfig::DEFAULT_BURST_INTERVAL_MS};
    /// The longest message the enabled roles need in one ring item
    /// (inbound_largest_message_bytes()), which sets the ring's floor and its pass-through
    /// allowance without artwork.
    size_t largest_message_bytes{INBOUND_MAX_MESSAGE_BYTES};
};

/// @brief The longest a holder keeps its oldest item, in whole seconds: `held_bytes` of quota
/// filled at `stored_bytes_per_second`, rounded up; 0 for a holder that is not enabled.
static constexpr size_t inbound_hold_seconds(size_t held_bytes, size_t stored_bytes_per_second) {
    return stored_bytes_per_second == 0
               ? 0
               : (held_bytes + stored_bytes_per_second - 1) / stored_bytes_per_second;
}

/**
 * @brief Storage for the shared inbound ring, derived from the configuration
 *
 * Reclamation is in ring order (see shared_ring_buffer.h), so every byte that arrives while the
 * oldest held item is outstanding stays unreclaimable until it is returned: JSON, time replies,
 * visualizer frames returned at their display time, artwork parts returned once copied, and
 * dropped items alike. The ring therefore holds:
 *  - the player's quota (audio_hold_bytes), the visualizer's (visualizer_hold_bytes) and the
 *    artwork role's (artwork_hold_bytes, one image per channel in flight to its decode thread;
 *    see INBOUND_ARTWORK_IN_FLIGHT_IMAGES);
 *  - the pass-through traffic arriving inside the longest hold window: the state JSON budget and
 *    every time-burst reply in it, and the visualizer frames the player's window carries, since a
 *    frame returned at its display time stays pinned behind audio held far longer. The player
 *    holds its oldest chunk for its quota at INBOUND_MIN_AUDIO_STORED_BYTES_PER_SECOND; the
 *    visualizer its oldest frame for its quota at rate_max, a lower bound, since rate_max is a
 *    cap: a sparser stream (a beat-only stream at two frames a second against a rate_max of 30)
 *    pins the pass-through longer than budgeted, and assuming a minimum rate instead would
 *    derive an absurd ring for a sparse stream. The codec headers and markers the protocol task
 *    writes for the holders are uncharged (InboundConsumer::hand()) and fall inside this term;
 *  - the baseline, INBOUND_PASSTHROUGH_MESSAGES items of largest_message_bytes, which can always
 *    arrive and sit behind a held item, whatever the holders' windows;
 *  - with the artwork role, the artwork that window carries: one image per channel per
 *    INBOUND_MIN_TRACK_SECONDS, copied out and returned but pinned behind the held items. With
 *    no hold window of INBOUND_MIN_TRACK_SECONDS or more (artwork alone, or a visualizer holding
 *    under INBOUND_MIN_TRACK_SECONDS) this term is 0: a third image arriving while the decode
 *    callback is still inside the first one is dropped at the quota, but its refused parts stay
 *    pinned behind the queued second image, so a stall long enough for time replies and state
 *    JSON to arrive behind them can exhaust the ring and close the connection (see
 *    INBOUND_ARTWORK_IN_FLIGHT_IMAGES). That needs a decode callback blocked for about two image
 *    cadences or track skips faster than INBOUND_MIN_TRACK_SECONDS, beyond this budget;
 * and never less than two items of largest_message_bytes (inbound_ring_min_storage_bytes()),
 * rounded up to the 4-byte multiple FreeRTOS requires. Unadmitted connections never write into
 * the ring (InboundGate), so they add nothing.
 *
 * The largest item follows the enabled roles (inbound_largest_message_bytes()): a maximal Noise
 * frame with the artwork role or a player advertising a buffer of a frame or more, which floors
 * the ring at 131,152 bytes; a 16 KiB JSON message with neither, which floors it at 32,880 bytes;
 * and the player's longest chunk in between. A longer message goes through the connection's
 * fallback buffer (InboundGate). A chunk or frame not in a ring item is copied into one bounded
 * by the ring's largest item alone (InboundRing::max_item_message_bytes(), half the storage):
 * audio and visualizer messages up to one Noise frame always fit, and each stream role
 * advertises a buffer of at most that bound (PlayerRole::Impl::advertised_buffer_capacity()), so
 * only a server over its advertised buffer sends one reassembled from several frames that is
 * longer, which is dropped with a warning naming that.
 *
 * Traffic beyond this budget (a burst of large JSON, a lower audio rate or a sparser visualizer
 * stream than budgeted, faster track changes) waits in the transport's acquire and, after
 * INBOUND_ACQUIRE_TIMEOUT_MS, closes the connection with a warning naming the held items. The
 * derivation assumes a server's visualizer lead never exceeds its audio lead: otherwise audio
 * returned as it plays would stay pinned behind the oldest visualizer frame, which nothing in the
 * configuration bounds.
 *
 * With the default configuration (a 1,000,000-byte player quota, no visualizer or artwork) this
 * is 1,000,000 + 111,552 held pass-through bytes (87 s at 1,024 B/s, plus 9 bursts of 8 time
 * replies at 312 stored bytes) + 131,152 for two maximal messages = 1,242,704 bytes, against
 * separate buffers of 1,000,000 bytes of encoded audio plus one maximal payload buffer per open
 * connection (1,065,535 bytes with one connection).
 */
static constexpr size_t derive_inbound_ring_bytes(const InboundRingBudget& budget) {
    const size_t player_hold_seconds =
        inbound_hold_seconds(budget.audio_hold_bytes, INBOUND_MIN_AUDIO_STORED_BYTES_PER_SECOND);
    const size_t hold_seconds = std::max(
        player_hold_seconds, inbound_hold_seconds(budget.visualizer_hold_bytes,
                                                  budget.visualizer_stored_bytes_per_second));
    const size_t bursts =
        hold_seconds > 0 && budget.time_burst_interval_ms > 0
            ? hold_seconds * 1000 / static_cast<size_t>(budget.time_burst_interval_ms) + 1
            : 0;
    const size_t held_passthrough =
        hold_seconds * INBOUND_STATE_BYTES_PER_SECOND +
        player_hold_seconds * budget.visualizer_stored_bytes_per_second +
        bursts * budget.time_burst_size * INBOUND_TIME_REPLY_STORED_BYTES;
    const size_t baseline =
        INBOUND_PASSTHROUGH_MESSAGES * inbound_item_stored_bytes(budget.largest_message_bytes);
    const size_t artwork_window =
        hold_seconds / INBOUND_MIN_TRACK_SECONDS * budget.artwork_images_stored_bytes;
    const size_t total = budget.audio_hold_bytes + budget.visualizer_hold_bytes +
                         budget.artwork_hold_bytes + held_passthrough + baseline + artwork_window;
    return SharedRingLayout::align(
        std::max(total, inbound_ring_min_storage_bytes(budget.largest_message_bytes)));
}
