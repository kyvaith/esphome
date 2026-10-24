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

bool SendspinClient::create_inbound_ring() {
    // Created whatever roles are enabled: every admitted connection receives into it, including
    // the server/time replies whose receive stamps must be taken at the socket read, which the
    // one-at-a-time fallback hand-off would delay behind the protocol task. The enabled roles set
    // its size, down to two JSON messages without the player, the visualizer or artwork.
    InboundRingBudget budget;
    budget.time_burst_size = this->config_.time_burst_size;
    budget.time_burst_interval_ms = this->config_.time_burst_interval_ms;
    size_t player_message_bytes = 0;
    size_t visualizer_message_bytes = 0;
    bool artwork = false;
#ifdef SENDSPIN_ENABLE_PLAYER
    if (this->player_) {
        budget.audio_hold_bytes = this->player_->impl_->config.audio_buffer_capacity;
        // The uncapped share: the cap advertised_buffer_capacity() applies is the ring's own
        // largest item, which this derivation sets.
        player_message_bytes =
            inbound_held_message_bytes(this->player_->impl_->buffer_capacity_share());
    }
#endif
#ifdef SENDSPIN_ENABLE_VISUALIZER
    if (this->visualizer_) {
        budget.visualizer_hold_bytes = this->visualizer_->impl_->visualizer_support.buffer_capacity;
        budget.visualizer_stored_bytes_per_second =
            this->visualizer_->impl_->stored_frame_bytes_per_second();
        visualizer_message_bytes =
            inbound_held_message_bytes(this->visualizer_->impl_->advertised_buffer_capacity());
    }
#endif
#ifdef SENDSPIN_ENABLE_ARTWORK
    if (this->artwork_) {
        for (const auto& slot : this->artwork_->impl_->config.preferred_formats) {
            budget.artwork_images_stored_bytes +=
                inbound_artwork_image_stored_bytes(slot.max_image_bytes);
        }
        budget.artwork_hold_bytes =
            INBOUND_ARTWORK_IN_FLIGHT_IMAGES * budget.artwork_images_stored_bytes;
        artwork = true;
    }
#endif
    budget.largest_message_bytes =
        inbound_largest_message_bytes(player_message_bytes, visualizer_message_bytes, artwork);
    const size_t storage_bytes = derive_inbound_ring_bytes(budget);
    auto ring = std::make_unique<InboundRing>();
    if (!ring->create(storage_bytes, this->config_.inbound_ring_location,
                      budget.largest_message_bytes)) {
        return false;
    }
    ring->quota(InboundHolder::PLAYER).set_limit(budget.audio_hold_bytes);
    ring->quota(InboundHolder::VISUALIZER).set_limit(budget.visualizer_hold_bytes);
    ring->quota(InboundHolder::ARTWORK).set_limit(budget.artwork_hold_bytes);
    SS_LOGD(TAG, "Inbound ring: %zu bytes, messages up to %zu bytes", storage_bytes,
            ring->max_message_bytes());
    this->inbound_ring_ = std::move(ring);
    return true;
}
