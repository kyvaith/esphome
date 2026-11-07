bool ProtocolTask::start(Tick tick, size_t stack_size, unsigned priority, bool stack_in_psram) {
    if (this->thread_.joinable()) {
        SS_LOGW(TAG, "Protocol task already running");
        return false;
    }
    if (!this->event_flags_.is_created()) {
        SS_LOGE(TAG, "Protocol task has no event flags; not starting");
        return false;
    }
    // A restart inherits nothing: no stop or wake left over from the previous run.
    this->event_flags_.clear_all();
    this->tick_ = std::move(tick);

    platform_configure_thread("SsProto", stack_size, static_cast<int>(priority), stack_in_psram);
    this->thread_ = std::thread(thread_entry, this);
    return true;
}

void ProtocolTask::stop() {
    if (!this->thread_.joinable()) {
        return;
    }
    // Setting the bit is the wake: the thread's wait returns on it and runs its final tick.
    this->event_flags_.set(COMMAND_STOP);
    this->thread_.join();

    // Joined. A snapshot the final tick did not take describes a run that is over. Commands
    // pushed and requests posted after the final tick stay for the joining thread
    // (take_command(), take_requests(), drop_commands()), which releases what a command holds
    // off this queue's lock.
    std::optional<ClientStateMessage> dropped;
    {
        std::lock_guard<std::mutex> lock(this->command_mutex_);
        dropped.swap(this->latest_state_);
    }
}