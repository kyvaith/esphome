bool VisualizerRole::Impl::start(InboundRing* ring) {
    // roles/visualizer/v1.md "client/state visualizer object": rate_max is a positive integer,
    // and a types list containing 'spectrum' without a spectrum object is a protocol error the
    // server SHOULD close the connection for. The configuration is reported as given, so a
    // config the spec forbids refuses to start here rather than being discovered as an
    // unexplained disconnect, the same posture the player takes on its format list.
    const VisualizerStreamConfig& stream = this->config.stream;
    if (stream.rate_max == 0 && !stream.types.empty()) {
        SS_LOGE(TAG, "VisualizerStreamConfig::rate_max must be positive while types are requested");
        return false;
    }
    // A quota that advertises less than one frame (or cannot store one) drops every frame;
    // refuse it as the configuration error it is.
    if (this->visualizer_support.buffer_capacity < MIN_BUFFER_CAPACITY) {
        SS_LOGE(TAG, "VisualizerSupportObject::buffer_capacity must be at least %zu bytes",
                MIN_BUFFER_CAPACITY);
        return false;
    }
    if (!stream.spectrum.has_value() &&
        std::find(stream.types.begin(), stream.types.end(), VisualizerDataType::SPECTRUM) !=
            stream.types.end()) {
        SS_LOGE(TAG, "VisualizerStreamConfig::spectrum is required to request the spectrum type");
        return false;
    }
    if (this->drain_task->drain_thread.joinable()) {
        return true;  // Already running
    }
    if (!this->drain_task->inbound.bind(ring, InboundHolder::VISUALIZER)) {
        SS_LOGE(TAG, "Failed to create the visualizer item list");
        return false;
    }

    // So a restart inherits no command from the previous run.
    this->drain_task->inbound.items().clear_signals();

    platform_configure_thread("SsVis", 4096, static_cast<int>(this->config.priority),
                              this->config.psram_stack);
    this->drain_task->drain_thread = std::thread(drain_thread_func, this);
    return true;
}

bool VisualizerRole::Impl::signal_stop() const {
    if (!this->drain_task || !this->drain_task->drain_thread.joinable()) {
        return false;
    }
    this->drain_task->inbound.items().signal(COMMAND_STOP);
    return true;
}

void VisualizerRole::Impl::stop() const {
    if (!this->signal_stop()) {
        return;
    }
    this->drain_task->drain_thread.join();
    this->drain_task->inbound.unbind();
}