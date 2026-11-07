bool SourceTask::start(ProtocolTask* protocol_task) {
    if (this->task_thread_.joinable()) {
        return false;
    }
    this->protocol_task_ = protocol_task;
    const SourceRoleConfig& config = this->role_->config;

    if (!this->event_flags_.is_created() && !this->event_flags_.create()) {
        SS_LOGE(TAG, "Couldn't create event flags");
        return false;
    }
    if (this->capture_ring_ == nullptr) {
        auto capture_ring = std::make_unique<OutboundRing>();
        if (!capture_ring->create(source_capture_ring_bytes(config), config.buffer_location)) {
            return false;  // Logged by the ring
        }
        this->capture_ring_ = std::move(capture_ring);
    }
    if (config.codec == SendspinCodecFormat::OPUS && this->opus_encoder_ == nullptr) {
#ifdef SENDSPIN_ENABLE_OPUS
        auto opus_encoder = std::make_unique<OpusSourceEncoder>();
        if (!opus_encoder->init(config)) {
            return false;  // Logged by the encoder
        }
        this->opus_encoder_ = std::move(opus_encoder);
#else
        return false;  // Unreachable: validate_config() refuses OPUS without the encoder
#endif
    }
    auto outbound_ring = std::make_unique<OutboundRing>();
    if (!outbound_ring->create(
            derive_outbound_ring_bytes(this->chunk_message_bytes_, OUTBOUND_RING_ITEM_COUNT),
            config.buffer_location)) {
        return false;  // Logged by the ring
    }
    this->outbound_ring_ = std::move(outbound_ring);

    // A fresh thread starts from a clean group, with nothing left over from the previous run.
    this->event_flags_.clear_all();
    this->capture_item_ = nullptr;
    this->chunk_item_ = nullptr;
    this->encoder_generation_ = 0;
    this->opus_warm_ = false;
    const size_t stack_size = config.codec == SendspinCodecFormat::OPUS
                                  ? SourceRoleConfig::DEFAULT_OPUS_SOURCE_TASK_STACK_SIZE
                                  : SourceRoleConfig::DEFAULT_SOURCE_TASK_STACK_SIZE;
    platform_configure_thread("SsSrc", stack_size, static_cast<int>(config.priority),
                              config.psram_stack);
    this->task_thread_ = std::thread(thread_entry, this);
    this->event_flags_.wait(SourceTaskBits::SOURCE_TASK_IDLE | SourceTaskBits::SOURCE_TASK_STOPPED,
                            false, false, UINT32_MAX);
    return true;
}

void SourceTask::stop() {
    if (!this->task_thread_.joinable()) {
        return;
    }
    // Flag first, then wake: the thread re-checks its flags after every take returns. An
    // outbound acquire in progress ends within its own chunk_duration_ms bound.
    this->event_flags_.set(SourceTaskBits::SOURCE_COMMAND_STOP);
    this->capture_ring_->wake_consumer();
    this->task_thread_.join();
    // Every lent item is back: see docs/internals.md "Source Streaming Pipeline".
    this->outbound_ring_.reset();
}