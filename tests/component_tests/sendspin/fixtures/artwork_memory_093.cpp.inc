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

// Reviewed 0.9.3 owners; patch source edits are applied before execution.
void ArtworkRole::Impl::begin_assembly(uint8_t slot, void* item) {
    // roles/artwork/v1.md "Artwork (Binary)": "An announce discards that channel's pending
    // image." A channel has one current image and at most one pending one, and only the pending
    // one can be in the buffer (a READY image, or one still assembling), so the new image takes
    // the buffer over.
    this->drop_assembly(slot);

    const InboundItemHeader* header = inbound_item_header(item);
    ArtworkAnnounce announce;
    std::memcpy(&announce, inbound_item_data(item), sizeof(announce));
    ArtworkAssembly& assembly = this->drain_task->assemblies[slot];
    assembly.timestamp = announce.timestamp;
    assembly.total_size = announce.total_size;
    assembly.received = 0;
    assembly.epoch = announce.epoch;
    assembly.generation = header->generation;
    this->drain_task->inbound.return_item(item);

    assembly.state = ArtworkAssembly::State::ASSEMBLING;
    // An empty image is complete at its announce.
    if (announce.total_size == 0) {
        this->deliver(slot);
    }
}

void ArtworkRole::Impl::add_part(uint8_t slot, void* item) {
    ArtworkAssembly& assembly = this->drain_task->assemblies[slot];
    // Copied out whatever the channel's gate says, and the item returned at once: a complete
    // image whose gate is closed parks in the buffer (deliver()).
    if (assembly.state == ArtworkAssembly::State::ASSEMBLING) {
        // The protocol task bounds every part by the announced total_size, which it bounds by
        // the channel's max_image_bytes, the buffer's size.
        const uint32_t len = inbound_item_header(item)->data_len;
        std::memcpy(assembly.buffer.data() + assembly.received, inbound_item_data(item), len);
        assembly.received += len;
    }
    // Otherwise no image is in progress on the channel: a marker or a newer announce dropped the
    // one this part belonged to, and the part is returned unused.
    this->drain_task->inbound.return_item(item);
    if (assembly.state == ArtworkAssembly::State::ASSEMBLING &&
        assembly.received == assembly.total_size) {
        this->deliver(slot);
    }
}

void ArtworkRole::Impl::drop_assembly(uint8_t slot, bool release_delivered) const {
    this->drain_task->assemblies[slot].state = ArtworkAssembly::State::IDLE;
    std::lock_guard<std::mutex> lock(this->drain_task->slot_mutex);
    auto& gate = this->drain_task->slot_gates[slot];
    gate.has_parked = false;
    if (release_delivered && gate.ack_state == SlotAckState::DECODE_DELIVERED) {
        gate.ack_state = SlotAckState::IDLE;
    }
}

void ArtworkRole::Impl::deliver(uint8_t slot) {
    ArtworkAssembly& assembly = this->drain_task->assemblies[slot];
    // The channel moved past this image after its announce (a cancel, a newer announce, a stream
    // restart or end) and the marker that says so is still behind it on the list: dropped
    // undecoded, as it would have been had the marker come first.
    if (assembly.epoch != this->slot_epochs[slot].load(std::memory_order_relaxed)) {
        assembly.state = ArtworkAssembly::State::IDLE;
        return;
    }
    {
        // Ack gate: a slot with require_frame_done set allows only one un-acked delivery in
        // flight. If one is outstanding the complete image parks here (READY), latest-wins,
        // until the gate reopens; otherwise the gate is armed (DECODE_DELIVERED) before the
        // decode, so a later image for this slot parks rather than delivering behind this
        // un-acked one. Arming gates on ack_enabled() alone, matching drain_events() and
        // clear_every_channel(); the listener is set before start() (see set_listener), and the
        // callback below is the crash-guard for that pointer.
        std::lock_guard<std::mutex> lock(this->drain_task->slot_mutex);
        auto& gate = this->drain_task->slot_gates[slot];
        if (this->ack_enabled(slot)) {
            if (gate.ack_state != SlotAckState::IDLE) {
                assembly.state = ArtworkAssembly::State::READY;
                gate.has_parked = true;
                return;
            }
            gate.ack_state = SlotAckState::DECODE_DELIVERED;
        }
    }
    assembly.state = ArtworkAssembly::State::IDLE;

    // A per-channel clear (an empty image) has nothing to decode. Everything else is deliberately
    // shared with a frame: the same slot-epoch staleness check, the same ack gate (a clear is a
    // delivery owing exactly one frame_done()), and the same timestamp-scheduled hand-off to the
    // main loop, which fires on_image_clear() rather than on_image_display() at the deadline.
    const bool is_clear = assembly.total_size == 0;
    if (!is_clear && this->listener) {
        this->listener->on_image_decode(slot, assembly.buffer.data(), assembly.total_size,
                                        this->image_format(slot));
    }

    // Hand off the timestamp to the main loop. Skip if the stream ended while we were
    // decoding so the main loop doesn't fire a display after on_image_clear. The delta
    // carries just this slot's bit; merge_artwork_display_update ORs it into whatever the
    // main loop hasn't drained out of display_slot yet.
    if (this->stream_active.load(std::memory_order_acquire)) {
        ArtworkDisplayUpdate delta{};
        delta.timestamps[slot] = assembly.timestamp;
        // The epoch this decode was validated under: lets the main-loop deadline check drop
        // the display if the stream is replaced after this hand-off (see held_display_epoch).
        delta.epochs[slot] = assembly.epoch;
        delta.valid_mask = static_cast<uint8_t>(1U << slot);
        if (is_clear) {
            delta.clear_mask = static_cast<uint8_t>(1U << slot);
        }
        // NOLINTNEXTLINE(performance-move-const-arg): merge() takes the delta as T&&
        this->event_state->display_slot.merge(merge_artwork_display_update, std::move(delta),
                                              assembly.generation);
    }
}
