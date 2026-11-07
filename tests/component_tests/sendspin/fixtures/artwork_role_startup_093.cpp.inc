bool ArtworkRole::Impl::start(InboundRing* ring) {
    if (this->drain_task->drain_thread.joinable()) {
        return true;  // Already running
    }
    // One assembly buffer per configured channel, at its cap, held for the run: each image is
    // copied into it part by part and decoded from it, so nothing is allocated per image. Image
    // data prefers SPIRAM, where it is decoded from once and never touched on a timing-critical
    // path. stop() hands the buffers back, so a stopped role holds no image memory.
    auto release_buffers = [this]() {
        for (auto& assembly : this->drain_task->assemblies) {
            assembly.buffer = PlatformBuffer{};
        }
    };
#if !CONFIG_ESPHOME_SENDSPIN_MEMORY_OPTIMIZATIONS
    for (size_t i = 0; i < this->config.preferred_formats.size(); ++i) {
        const uint32_t cap = this->config.preferred_formats[i].max_image_bytes;
        if (cap > 0 && !this->drain_task->assemblies[i].buffer.allocate(
                           cap, MemoryLocation::PREFER_EXTERNAL)) {
            SS_LOGE(TAG, "Failed to allocate the %" PRIu32 " byte artwork buffer for channel %zu",
                    cap, i);
            release_buffers();
            return false;
        }
    }
#endif
    if (!this->drain_task->inbound.bind(ring, InboundHolder::ARTWORK)) {
        SS_LOGE(TAG, "Failed to create the artwork item list");
        release_buffers();
        return false;
    }

    // So a restart inherits no command from the previous run.
    this->drain_task->inbound.items().clear_signals();

    platform_configure_thread("SsArt", 4096, static_cast<int>(this->config.priority),
                              this->config.psram_stack);
    this->drain_task->drain_thread = std::thread(drain_thread_func, this);
    return true;
}

bool ArtworkRole::Impl::signal_stop() const {
    if (!this->drain_task || !this->drain_task->drain_thread.joinable()) {
        return false;
    }
    this->drain_task->inbound.items().signal(COMMAND_STOP);
    return true;
}

void ArtworkRole::Impl::stop() const {
    if (!this->signal_stop()) {
        return;
    }
    this->drain_task->drain_thread.join();

    // Joined: the thread returns every item as soon as it has read it, so what is left on the
    // list goes back to the ring here, and a restart does not decode the previous session's images.
    this->drain_task->inbound.unbind();
    for (auto& assembly : this->drain_task->assemblies) {
        assembly.buffer = PlatformBuffer{};
    }
}