bool SyncTask::start(InboundRing* ring, bool task_stack_in_psram, unsigned priority) {
    if (!this->is_initialized()) {
        SS_LOGE(TAG, "Sync task not initialized (call init() first or set audio sink)");
        return false;
    }

    if (this->sync_thread_.joinable()) {
        SS_LOGW(TAG, "Sync task thread already started");
        return false;
    }

    if (!this->inbound_.bind(ring, InboundHolder::PLAYER)) {
        SS_LOGE(TAG, "Couldn't create the encoded item list");
        return false;
    }

    // A fresh thread starts from a clean group: no stale task state and no command signalled
    // between the previous join and this start (cleanup() on a stopped task).
    this->event_flags_.clear_all();

    platform_configure_thread("SsSync", SYNC_TASK_STACK_SIZE, static_cast<int>(priority),
                              task_stack_in_psram);

    this->sync_thread_ = std::thread(thread_entry, this);

    // Wait for the thread to reach IDLE before returning
    this->event_flags_.wait(EventGroupBits::TASK_IDLE | EventGroupBits::TASK_STOPPED, false, false,
                            UINT32_MAX);

    return true;
}

void SyncTask::stop() {
    if (!this->sync_thread_.joinable()) {
        return;
    }

    // Set the flag before waking: the thread re-checks its command flags after every
    // take return, so this ordering guarantees it observes the stop no matter which
    // wait it was parked in (event flags or item list take).
    this->event_flags_.set(EventGroupBits::COMMAND_STOP);
    this->inbound_.items().wake_receiver();
    this->sync_thread_.join();

    // A stop mid-stream leaves TASK_RUNNING set (only the idle transition clears it). The player's
    // sync-idle gate reads is_running() to decide when a STREAM_END may fire, so a stopped task
    // must read as idle or the stop-time on_stream_end() would wait for a thread that is gone.
    this->event_flags_.clear(EventGroupBits::TASK_RUNNING);

    this->inbound_.unbind();
}