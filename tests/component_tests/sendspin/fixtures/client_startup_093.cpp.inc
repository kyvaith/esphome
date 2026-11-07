bool SendspinClient::start() {
    switch (this->lifecycle_.load(std::memory_order_relaxed)) {
        case LifecycleState::RUNNING:
            return true;
        case LifecycleState::STOPPING:
            SS_LOGW(TAG, "start() ignored: called from a callback while stop() is in progress");
            return false;
        case LifecycleState::STOPPED:
            break;
    }

    // Fail closed, like a failed identity: a Pairing PSK any peer could hold admits every server
    // as a pairing peer, and a malformed static pairing code (pairing.md "Static Pairing Code
    // Flow") fails every attempt with a code mismatch.
    if (this->config_.pairing_psk.has_value() &&
        !RecordStore::is_usable_pairing_psk(this->config_.pairing_psk->bytes)) {
        SS_LOGE(TAG, "Configured Pairing PSK is all zero or the published Sentinel PSK; cannot "
                     "start");
        return false;
    }
    if (this->config_.static_pairing_code.has_value() &&
        !is_valid_static_pairing_code(this->config_.static_pairing_code.value())) {
        SS_LOGE(TAG, "Configured static pairing code is not %d decimal digits; cannot start",
                STATIC_PAIRING_CODE_DIGITS);
        return false;
    }

    // lifecycle_ stays STOPPED until every step below has succeeded. Marking the client running
    // up front would report it started even when this function returns false (e.g. identity
    // generation failed and identity_ is left null), which is precisely the state connect_to()
    // must refuse to build a connection in.
    //
    // Create the record store (needs the persistence provider, so done here rather than at
    // construction) and load or generate the static X25519 identity. Both must exist before the
    // connection manager can hand them out to any connection (connection_manager_->start() below
    // arms the ws_server, and connect_to() may be called any time after start() RETURNS TRUE).
    // A restart keeps both: the store already holds the records the last run persisted, and the
    // identity is fixed for the lifetime of the stored keypair. They are rebuilt only when the
    // persistence provider changed since they were built (each reads the provider once, at
    // construction, so a provider set between a stop and the next start would otherwise never
    // be consulted) or when the previous start failed part-way (a store without an identity).
    if (this->record_store_ == nullptr || this->identity_ == nullptr ||
        this->identity_provider_ != this->persistence_provider_) {
        this->identity_.reset();
        this->record_store_ =
            std::make_unique<RecordStore>(this->persistence_provider_, this->config_);
        if (!this->load_or_generate_identity()) {
            // identity_ is left null: there is no safe key to fall back to (see
            // load_or_generate_identity()'s doc comment), so the client must not start.
            return false;
        }
        this->identity_provider_ = this->persistence_provider_;
    }

    // A store that came up owing a write (a duplicate slot cleared at load) flushes on the first
    // tick, through the same deferred path a pairing uses: the provider is main-loop-only and the
    // store never calls it itself outside that flush.
    if (this->record_store_->has_pending_writes()) {
        this->request_persist();
    }

    // Load persisted state
    this->load_last_played_server();

    // The inbound ring every admitted connection receives into and the role threads consume
    // from; the role starts below bind their item lists to it.
    if (!this->create_inbound_ring()) {
        return false;
    }

    // Start the role threads. A failure part-way stops the roles that did start, so the client
    // is back in the stopped state and a corrected retry begins clean.
    bool roles_started = true;
#ifdef SENDSPIN_ENABLE_PLAYER
    if (roles_started && this->player_) {
        roles_started =
            this->player_->impl_->start(this->persistence_provider_, this->inbound_ring_.get());
    }
#endif
#ifdef SENDSPIN_ENABLE_VISUALIZER
    if (roles_started && this->visualizer_) {
        roles_started = this->visualizer_->impl_->start(this->inbound_ring_.get());
    }
#endif
#ifdef SENDSPIN_ENABLE_ARTWORK
    if (roles_started && this->artwork_) {
        roles_started = this->artwork_->impl_->start(this->inbound_ring_.get());
    }
#endif
#ifdef SENDSPIN_ENABLE_SOURCE
    if (roles_started && this->source_) {
        roles_started = this->source_->impl_->start(this->protocol_task_.get());
    }
#endif
    if (!roles_started) {
        this->stop_role_threads();
        this->release_inbound_ring();
        return false;
    }

    // A command queued or a request posted after the last stop() finished (a call racing it on
    // another thread) belongs to the run that ended; this run begins with an empty queue and no
    // request waiting. Admission reopens in connection_manager_->start() below.
    this->protocol_task_->drop_commands();

    // The first client/state snapshot, taken by the protocol task's first tick: every
    // connection's client/state is built from the newest snapshot the task holds.
    this->protocol_task_->publish_state(this->build_client_state());

    // The network provider is polled on the main loop only, here and in loop().
    this->poll_network_provider();

    // Open admission and create the WebSocket server, started here when the network is already
    // up. Before the protocol task starts: everything the manager writes here is the task's from
    // then on, and an accept the server delivers meanwhile waits in the command queue for the
    // task's first tick.
    this->connection_manager_->start();

    // The protocol task. Undersized stacks are clamped to the documented minimum, the measured
    // stack of its deepest call chain, as the transport tasks' are.
    size_t protocol_stack = this->config_.protocol_task_stack_size;
    if (protocol_stack < SendspinClientConfig::DEFAULT_PROTOCOL_TASK_STACK_SIZE) {
        SS_LOGW(TAG, "protocol_task_stack_size %u below minimum %u; clamping",
                static_cast<unsigned>(protocol_stack),
                static_cast<unsigned>(SendspinClientConfig::DEFAULT_PROTOCOL_TASK_STACK_SIZE));
        protocol_stack = SendspinClientConfig::DEFAULT_PROTOCOL_TASK_STACK_SIZE;
    }
    if (!this->protocol_task_->start([this]() { return this->protocol_tick(); }, protocol_stack,
                                     this->config_.protocol_task_priority,
                                     this->config_.protocol_task_psram_stack)) {
        SS_LOGE(TAG, "Failed to start the protocol task");
        // No task ever ran: close admission, stop the server (joining its transport threads),
        // and only then drop the accepts it queued, whose connections no transport reaches any
        // more.
        this->connection_manager_->close_admission();
        (void)this->connection_manager_->finish_stop();
        this->protocol_task_->drop_commands();
        this->stop_role_threads();
        this->release_inbound_ring();
        return false;
    }

    this->lifecycle_.store(LifecycleState::RUNNING, std::memory_order_release);
    return true;
}

void SendspinClient::stop_role_threads() {
#ifdef SENDSPIN_ENABLE_PLAYER
    if (this->player_) {
        this->player_->impl_->stop();
    }
#endif
#ifdef SENDSPIN_ENABLE_VISUALIZER
    if (this->visualizer_) {
        this->visualizer_->impl_->stop();
    }
#endif
#ifdef SENDSPIN_ENABLE_ARTWORK
    if (this->artwork_) {
        this->artwork_->impl_->stop();
    }
#endif
#ifdef SENDSPIN_ENABLE_SOURCE
    if (this->source_) {
        this->source_->impl_->stop();
    }
#endif
}