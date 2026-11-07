"""Checked startup for the exact Sendspin 0.9.3 memory-policy postimages."""

import hashlib
from pathlib import Path

GUARD = "CONFIG_ESPHOME_SENDSPIN_CHECKED_THREADS"
BEFORE = {
    "src/protocol_task.h": "e84d121bc830e9d6d438e24edf758c2291c616b24761cc955a69e8e1d437b876",
    "src/sync_task.h": "1dadab12ec7062d0ed9957a5caed4281853ec77c1650a208efd1280fc3c9ab3e",
    "src/source_task.h": "f482892de6b518374a6672e6769ee87ce79895858eb43497d2187b75f28268c4",
    "src/artwork_role_impl.h": "3f4744368d5f3536bafc19b4dc666264cbb9d9a4ce8725aeca0ccce3a68d9c27",
    "src/visualizer_role_impl.h": "38d5e3d4ab94a5f2bce947fb8ea2f8e20185b1cc75ab3387e3711102a6ab954a",
    "src/protocol_task.cpp": "b7ea557ebf6767a32f9a4cdd81fab602f2f783cfd4519e643767a96a7dadd6b8",
    "src/sync_task.cpp": "34a43ca4c76fc78a8b4a730f906118f390c77964386496955435aacb82d856cc",
    "src/source_task.cpp": "3b566a92826f40370bb28344d44a1d63730929e53dca10b313152fa2ff4fd598",
    "src/artwork_role.cpp": "f2585b9fcb5a61db199708f62e7f1c819da78dc2bf49a84d787c137e7337c109",
    "src/visualizer_role.cpp": "1300b333a1a1bcd9d05467d4188fa0f1e61f1dee12eb40412e6c61cb6412ff39",
    "src/client.cpp": "d6367099f67c5e94e37d325a03d1363cb3551114fbc2e7195e9e7a536ab6e4f8",
    "include/sendspin/client.h": "6d358471010eec011a436f9c1747ffb08d1a37ee23f4aad63c1028acd954bd6d",
}
AFTER = {
    "src/protocol_task.h": "863f55169858b028ab5bde26c73ec8b8edbaef2d1cc2ecddc16f2bfb02758ed8",
    "src/sync_task.h": "5a4f2237fe5420274fc3b873af789497ee4c81ca01e122166d7ac77cd7599481",
    "src/source_task.h": "79be1437a9602cb974a9beacb5d3165eb5b20c3ea12b5bfb358d00a50e9cb5b4",
    "src/artwork_role_impl.h": "a9b7b717557a0b128d5d85d7a5ae29089181cd62260468c57aa4bbad5f608955",
    "src/visualizer_role_impl.h": "ebc69cec9cbca2a8a1cb8074dc2e17ddd12a8990915a9b2cc9325c7a1c001246",
    "src/protocol_task.cpp": "b6cf4db68e006b73e31ded8aa32d299a17c6e1d8bef94b33d6001e8b8795f357",
    "src/sync_task.cpp": "9f37d18dd5b9aa56ccfa735f76b6b675fdd48a5c4aca3992da01b4a9b8a99a86",
    "src/source_task.cpp": "cb5e83f337d51ef376800e561c002fd50ccc72f5fc981b83842a9a414e6972c5",
    "src/artwork_role.cpp": "385600cf096bb6a79888c179dcc453ca0c29d256279385701bea0d118774ae48",
    "src/visualizer_role.cpp": "ecd98d8a118ed6a5f031a38773f75354ddef16c5e62917242baf2417134bc532",
    "src/client.cpp": "a2219e4a816576b8c13ee9386e3cb8761be1600b37d9a1fc79687a9becf7690a",
    "include/sendspin/client.h": "87d72773e83da102fac028aa9f8688206f30099336f3e04fa79a8ba9037eae00",
    "src/platform/fallible_thread.h": "81d421dbca3c67012db1a07ddcb202e458d7325758fa291c303b40de5cfa1831",
}


# Native109-117 checked workers: accept only these exact reviewed old outputs.
PREVIOUS_AFTER = {
    "src/protocol_task.cpp": "aae13fdcb66da6d2f8231931eb410b061bc372e85fccb328e937c4f324018f3b",
    "src/sync_task.cpp": "6ecf8b9ba74b9933e8791e60887d187c0b0688fbcc44d4fcd1dd860ac80977bd",
    "src/source_task.cpp": "776defbec4558d253fea7f5f2431f6c5e535a4a5694e113a0e8a1d9bb255149d",
    "src/artwork_role.cpp": "e57a0fdb133de97764d3d50837cf9ce70b548746c026dfd3429b81a331a08f73",
    "src/visualizer_role.cpp": "7a27072cc4a4fe7edd25178d10920ddd6848d30e6f1e48aa68572c889d556023",
}


def guarded(new, old=""):
    return f"#if {GUARD}\n{new}" + (f"#else\n{old}" if old else "") + "#endif\n"


def replacements():
    result = {}
    workers = (
        ("protocol_task", "thread_"),
        ("sync_task", "sync_thread_"),
        ("source_task", "task_thread_"),
        ("artwork_role_impl", "drain_thread"),
        ("visualizer_role_impl", "drain_thread"),
    )
    for stem, member in workers:
        indent = "        " if stem.endswith("_impl") else "    "
        changes = [
            (
                "#include <thread>\n",
                '#include <thread>\n#include "platform/fallible_thread.h"\n',
            ),
            (
                f"{indent}std::thread {member};\n",
                guarded(
                    f"{indent}PlatformThread {member};\n",
                    f"{indent}std::thread {member};\n",
                ),
            ),
        ]
        if not stem.endswith("_impl"):
            changes.append(
                (
                    "public:\n",
                    "public:\n"
                    + guarded(
                        f"    void reset_start_failure() {{ this->{member}.clear_start_error(); }}\n"
                        f"    bool start_failure_retryable() const {{ return this->{member}.start_retryable(); }}\n"
                    ),
                )
            )
        result[f"src/{stem}.h"] = changes

    sites = (
        (
            "protocol_task",
            '"SsProto", stack_size, static_cast<int>(priority), stack_in_psram',
            "thread_",
            "thread_entry",
            "        this->tick_ = {};\n",
        ),
        (
            "sync_task",
            '"SsSync", SYNC_TASK_STACK_SIZE, static_cast<int>(priority),\n                              task_stack_in_psram',
            "sync_thread_",
            "thread_entry",
            "        this->inbound_.unbind();\n",
        ),
        (
            "source_task",
            '"SsSrc", stack_size, static_cast<int>(config.priority),\n                              config.psram_stack',
            "task_thread_",
            "thread_entry",
            "        this->outbound_ring_.reset();\n",
        ),
        (
            "artwork_role",
            '"SsArt", 4096, static_cast<int>(this->config.priority),\n                              this->config.psram_stack',
            "drain_task->drain_thread",
            "drain_thread_func",
            "        this->drain_task->inbound.unbind();\n        release_buffers();\n",
        ),
        (
            "visualizer_role",
            '"SsVis", 4096, static_cast<int>(this->config.priority),\n                              this->config.psram_stack',
            "drain_task->drain_thread",
            "drain_thread_func",
            "        this->drain_task->inbound.unbind();\n",
        ),
    )
    for stem, args, member, entry, cleanup in sites:
        gap = "\n" if stem == "sync_task" else ""
        original = (
            f"    platform_configure_thread({args});\n{gap}"
            f"    this->{member} = std::thread({entry}, this);\n"
        )
        name = args.split(",")[0]
        fixed = (
            f"    if (!this->{member}.start<{entry}>(this, {args})) {{\n"
            f'        SS_LOGE(TAG, "Worker %s start refused (phase=%u error=%d)", {name}, this->{member}.start_error_phase(), this->{member}.start_error());\n'
            + cleanup
            + "        return false;\n    }\n"
        )
        result[f"src/{stem}.cpp"] = [(original, guarded(fixed, original))]

    old_stop = """    if (!this->thread_.joinable()) {
        return;
    }
    // Setting the bit is the wake: the thread's wait returns on it and runs its final tick.
    this->event_flags_.set(COMMAND_STOP);
    this->thread_.join();
"""
    new_stop = """    // A failed start can still have a published snapshot. Never-running tasks
    // also discard it below; only a live worker is signalled and joined.
    if (this->thread_.joinable()) {
        this->event_flags_.set(COMMAND_STOP);
        this->thread_.join();
    }
    this->tick_ = {};
"""
    result["src/protocol_task.cpp"].append((old_stop, guarded(new_stop, old_stop)))

    reset = "    this->start_failure_retryable_ = false;\n    this->protocol_task_->reset_start_failure();\n"
    for role, member in (
        ("PLAYER", "sync_task->reset_start_failure()"),
        ("VISUALIZER", "drain_task->drain_thread.clear_start_error()"),
        ("ARTWORK", "drain_task->drain_thread.clear_start_error()"),
        ("SOURCE", "task->reset_start_failure()"),
    ):
        lower = role.lower()
        reset += f"#ifdef SENDSPIN_ENABLE_{role}\n    if (this->{lower}_) this->{lower}_->impl_->{member};\n#endif\n"
    client_changes = [
        (
            "bool SendspinClient::start() {\n",
            "bool SendspinClient::start() {\n" + guarded(reset),
        )
    ]
    for old, accessor in (
        (
            "this->player_->impl_->start(this->persistence_provider_, this->inbound_ring_.get());",
            "this->player_->impl_->sync_task->start_failure_retryable()",
        ),
        (
            "this->visualizer_->impl_->start(this->inbound_ring_.get());",
            "this->visualizer_->impl_->drain_task->drain_thread.start_retryable()",
        ),
        (
            "this->artwork_->impl_->start(this->inbound_ring_.get());",
            "this->artwork_->impl_->drain_task->drain_thread.start_retryable()",
        ),
        (
            "this->source_->impl_->start(this->protocol_task_.get());",
            "this->source_->impl_->task->start_failure_retryable()",
        ),
    ):
        client_changes.append(
            (
                old + "\n",
                old
                + "\n"
                + guarded(
                    f"        if (!roles_started) this->start_failure_retryable_ = {accessor};\n"
                ),
            )
        )
    client_changes.append(
        (
            '        SS_LOGE(TAG, "Failed to start the protocol task");\n',
            guarded(
                "        this->start_failure_retryable_ = this->protocol_task_->start_failure_retryable();\n"
            )
            + '        SS_LOGE(TAG, "Failed to start the protocol task");\n',
        )
    )
    client_changes.append(
        (
            "        (void)this->connection_manager_->finish_stop();\n",
            "        (void)this->connection_manager_->finish_stop();\n"
            + guarded(
                "        this->protocol_task_->stop();  // Discard the unconsumed published snapshot.\n"
            ),
        )
    )
    result["src/client.cpp"] = client_changes
    result["include/sendspin/client.h"] = [
        (
            "#pragma once\n",
            '#pragma once\n\n#ifdef ESP_PLATFORM\n#include "sdkconfig.h"\n#endif\n',
        ),
        (
            "    bool start();\n",
            "    bool start();\n"
            + guarded(
                "    /// Main-loop result of the most recent start; cleared before every attempt.\n"
                "    bool start_failure_retryable() const { return this->start_failure_retryable_; }\n"
            ),
        ),
        (
            "    std::atomic<LifecycleState> lifecycle_",
            guarded("    bool start_failure_retryable_{false};\n")
            + "    std::atomic<LifecycleState> lifecycle_",
        ),
    ]
    return result


def transform(source, changes):
    for old, new in changes:
        if source.count(old) != 1:
            raise RuntimeError(
                "Sendspin thread owner changed; review the 0.9.3 startup patch"
            )
        source = source.replace(old, new, 1)
    return source


def prepare_thread_sources(
    component_dir: Path, pending: dict[Path, bytes]
) -> dict[Path, bytes]:
    outputs = dict(pending)
    for name, changes in replacements().items():
        path = component_dir / name
        data = outputs.get(path, path.read_bytes())
        digest = hashlib.sha256(data).hexdigest()
        if digest == AFTER[name]:
            continue
        if digest == PREVIOUS_AFTER.get(name):
            # A failed startup is an error even when a bounded retry later recovers.
            # Preserve its worker/phase/errno under the normal IDF ERROR log level.
            fixed = transform(
                data.decode("utf-8"),
                [
                    (
                        'SS_LOGW(TAG, "Worker %s start refused',
                        'SS_LOGE(TAG, "Worker %s start refused',
                    )
                ],
            ).encode("utf-8")
        elif digest == BEFORE[name]:
            fixed = transform(data.decode("utf-8"), changes).encode("utf-8")
        else:
            raise RuntimeError(f"Unreviewed Sendspin thread preimage: {name}")
        if hashlib.sha256(fixed).hexdigest() != AFTER[name]:
            raise RuntimeError(f"Unreviewed Sendspin thread postimage: {name}")
        outputs[path] = fixed
    name = "src/platform/fallible_thread.h"
    payload = Path(__file__).with_name("fallible_thread.h").read_bytes()
    if hashlib.sha256(payload).hexdigest() != AFTER[name]:
        raise RuntimeError("Unreviewed checked-thread payload")
    path = component_dir / name
    if path.exists():
        if path.read_bytes() != payload:
            raise RuntimeError("Unreviewed existing checked-thread header")
    else:
        outputs[path] = payload
    return outputs
