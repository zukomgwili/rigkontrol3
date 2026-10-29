#include "lifecycle.h"

static void emit(NIIPCProofImpl *impl, const NIIPCProofSink *sink,
                 NIIPCProofOperation operation, uint64_t argument,
                 uintptr_t source) {
    /* An owner can outlive a detached impl; zero snapshot is proof metadata. */
    NIIPCProofImpl absent = {0};
    if (impl == NULL) impl = &absent;
    NIIPCProofEvent event = {.operation = operation, .argument = argument,
        .port = impl->port, .source = source, .loop = impl->run_loop,
        .active = impl->active, .deferred = impl->deferred,
        .owner_impl_present = impl->owner != NULL &&
                              impl->owner->implementation != NULL};
    if (sink != NULL && sink->observe != NULL) sink->observe(sink->context, event);
}

/* Original1002eee70: signal is before active check and source creation. */
void ni_ipc_os_run(NIIPCProofImpl *impl, uintptr_t source_result,
                   uintptr_t current_loop, const NIIPCProofSink *sink) {
    emit(impl, sink, NI_IPC_SIGNAL_EVENT, 0, 0);
    if (impl->active != 1) return;
    emit(impl, sink, NI_IPC_CREATE_SOURCE, 0, 0);
    emit(impl, sink, NI_IPC_GET_CURRENT_LOOP, 0, 0);
    impl->run_loop = current_loop;
    emit(impl, sink, NI_IPC_ADD_SOURCE_COMMON_MODES, 0, source_result);
    emit(impl, sink, NI_IPC_RELEASE_SOURCE, 0, source_result);
    emit(impl, sink, NI_IPC_RUN_LOOP, 0, 0);
}

/* Original1002eec50: true skips wait/close and preserves thread storage. */
void ni_ipc_os_stop(NIIPCProofImpl *impl, bool deferred, const NIIPCProofSink *sink) {
    if (impl->active == 0) return;
    if (deferred) impl->deferred = true;
    impl->active = 0;
    emit(impl, sink, NI_IPC_INVALIDATE_PORT, 0, 0);
    emit(impl, sink, NI_IPC_STOP_LOOP, 0, 0);
    if (deferred) return;
    emit(impl, sink, NI_IPC_WAIT_TERMINATION, 0, 0);
    emit(impl, sink, NI_IPC_CLOSE_THREAD, 0, 0);
    impl->thread_handle = false;
    for (size_t i = 0; i < 4; ++i) impl->thread_storage[i] = 0;
}
static void dispose_holder(NIIPCProofOwner *owner, NIIPCProofImpl *impl,
                           const NIIPCProofSink *sink) {
    if (!owner->callback_holder) return;
    if (owner->callback_managed) emit(impl, sink, NI_IPC_DISPOSE_CALLBACK, 2, 0);
    owner->callback_holder = false;
}
/* Original1002ebc30: true detaches before OS stop; false cleans holder after. */
void ni_ipc_public_stop(NIIPCProofOwner *owner, bool deferred,
                        const NIIPCProofSink *sink) {
    NIIPCProofImpl *impl = owner->implementation;
    if (deferred) owner->implementation = NULL;
    ni_ipc_os_stop(impl, deferred, sink);
    if (!deferred) dispose_holder(owner, impl, sink);
}

static void remove_port_mapping(NIIPCProofImpl *impl, const NIIPCProofSink *sink) {
    emit(impl, sink, NI_IPC_MAP_LOCK, 0, 0);
    if (impl->map_entry) {
        emit(impl, sink, NI_IPC_MAP_REMOVE, 0, 0);
        impl->map_entry = false;
    }
    emit(impl, sink, NI_IPC_MAP_UNLOCK, 0, 0);
}
/* Original1002eece0 clears callback BEFORE active/port guards. */
void ni_ipc_os_close(NIIPCProofImpl *impl, const NIIPCProofSink *sink) {
    emit(impl, sink, NI_IPC_CLEAR_INVALIDATION, 0, 0);
    if (impl->active != 0 || impl->port == 0) return;
    remove_port_mapping(impl, sink);
    emit(impl, sink, NI_IPC_RELEASE_PORT, 0, 0);
    impl->port = 0;
}
void ni_ipc_public_close(NIIPCProofOwner *owner, bool receive,
                         const NIIPCProofSink *sink) {
    NIIPCProofImpl *impl = owner->implementation;
    if (impl == NULL || impl->port == 0) return;
    ni_ipc_os_close(impl, sink);
    if (receive) {
        owner->buffer = 0;
        owner->capacity = 0;
    }
}
/* Original1002ed940: synthetic registry candidate, no actual map/locking. */
void ni_ipc_invalidated(NIIPCProofImpl *registry_candidate, uintptr_t incoming_port,
                        const NIIPCProofSink *sink) {
    NIIPCProofImpl *impl = registry_candidate;
    emit(impl, sink, NI_IPC_MAP_LOCK, 0, 0);
    emit(impl, sink, NI_IPC_MAP_FIND, incoming_port, 0);
    /* Original1002ed9d1..e1 guards the mapped implementation value. */
    if (impl != NULL && impl->map_entry && impl->port == incoming_port && impl->owner != NULL)
        impl->owner->invalidated = true;
    emit(impl, sink, NI_IPC_MAP_UNLOCK, 0, 0);
}

/* Original1002ed320: remote creation registers map, then invalidation callback. */
bool ni_ipc_os_open(NIIPCProofImpl *impl, NIIPCProofOwner *owner,
                     NIIPCProofCreateOutcome outcome, const NIIPCProofSink *sink) {
    if (owner == NULL || impl->port != 0) return false;
    impl->owner = owner;
    emit(impl, sink, NI_IPC_CREATE_NAME, 0, 0);
    if (!outcome.name_created) return false;
    emit(impl, sink, NI_IPC_CREATE_REMOTE, 0, 0);
    impl->port = outcome.created_port;
    emit(impl, sink, NI_IPC_RELEASE_NAME, 0, 0);
    if (impl->port == 0) return false;
    emit(impl, sink, NI_IPC_MAP_LOCK, 0, 0);
    emit(impl, sink, NI_IPC_MAP_INSERT, 0, 0);
    impl->map_entry = true;
    emit(impl, sink, NI_IPC_MAP_UNLOCK, 0, 0);
    emit(impl, sink, NI_IPC_INSTALL_INVALIDATION, 0, 0);
    return true;
}
/* Original1002ee4d0: source address identifies the recovered callback, not a
   callable x86 pointer. Context and opaque outcomes are never registered. */
bool ni_ipc_os_create(NIIPCProofImpl *impl, NIIPCProofOwner *owner,
                       NIIPCProofCreateOutcome outcome, const NIIPCProofSink *sink) {
    if (owner == NULL || impl->port != 0) return false;
    impl->owner = owner;
    emit(impl, sink, NI_IPC_CREATE_NAME, 0, 0);
    if (!outcome.name_created) return false;
    NIIPCProofEvent event = {.operation = NI_IPC_CREATE_LOCAL,
        .port = impl->port, .active = impl->active,
        .owner_impl_present = owner->implementation != NULL,
        .context_version = 0, .context_info = (uintptr_t)impl,
        .callback_source_address = UINT64_C(0x1002ee910),
        .context_callbacks_null = true, .should_free_info_null = true};
    if (sink != NULL && sink->observe != NULL) sink->observe(sink->context, event);
    impl->port = outcome.created_port;
    emit(impl, sink, NI_IPC_RELEASE_NAME, 0, 0);
    return impl->port != 0;
}
/* Original1002ebbc0 retains buffer/count even when OS create fails. */
bool ni_ipc_public_create(NIIPCProofOwner *owner, uintptr_t buffer, uint64_t capacity,
                           NIIPCProofCreateOutcome outcome, const NIIPCProofSink *sink) {
    if (buffer == 0 || capacity == 0) return false;
    owner->buffer = buffer;
    owner->capacity = capacity;
    return ni_ipc_os_create(owner->implementation, owner, outcome, sink);
}

/* Original1002ee9c0; opaque creation outcomes describe synthetic GP results.
   Wait failure only changes diagnostics (not modeled), never this call order. */
void ni_ipc_os_start(NIIPCProofImpl *impl, NIIPCProofStartOutcome outcome,
                     const NIIPCProofSink *sink) {
    if (impl->active != 0) return;
    impl->active = 1;
    if (!impl->event_handle) {
        emit(impl, sink, NI_IPC_CREATE_EVENT, 0, 0);
        impl->event_handle = outcome.event_created;
    }
    emit(impl, sink, NI_IPC_PRIORITY_ENUM, 4, 0);
    if (!impl->thread_handle) {
        /* 1002eea15 constructs enum4; 1002eea26 loads its opaque result. */
        emit(impl, sink, NI_IPC_CREATE_THREAD, outcome.converted_priority, 0);
        impl->thread_handle = outcome.thread_created;
    }
    emit(impl, sink, NI_IPC_WAIT_EVENT, 5000, 0);
    (void)outcome.wait_succeeded;
    if (impl->event_handle) {
        emit(impl, sink, NI_IPC_DESTROY_EVENT, 0, 0);
        impl->event_handle = false;
    }
}
/* Original1002ebbf0: holder assignment precedes OS active guard. */
void ni_ipc_public_start(NIIPCProofOwner *owner, bool callback_present,
                         bool callback_managed, NIIPCProofStartOutcome outcome,
                         const NIIPCProofSink *sink) {
    NIIPCProofImpl *impl = owner->implementation;
    if (impl == NULL || impl->port == 0) return;
    emit(impl, sink, NI_IPC_ASSIGN_CALLBACK, callback_present ? 1 : 0, 0);
    owner->callback_holder = callback_present;
    owner->callback_managed = callback_managed;
    ni_ipc_os_start(impl, outcome, sink);
}

/* Original1002eee60 only nulls the reverse owner link. */
void ni_ipc_reset_master(NIIPCProofImpl *impl, const NIIPCProofSink *sink) {
    impl->owner = NULL;
    emit(impl, sink, NI_IPC_RESET_MASTER, 0, 0);
}
/* Original base1002eb750/send1002eb870/receive1002ebb20.
   Slot8 is an opaque command, never guessed to be a specific destructor. */
void ni_ipc_owner_destroy(NIIPCProofOwner *owner, NIIPCProofOwnerKind kind,
                          const NIIPCProofSink *sink) {
    if (kind != NI_IPC_BASE_OWNER)
        dispose_holder(owner, owner->implementation, sink);
    if (owner->implementation != NULL) {
        ni_ipc_reset_master(owner->implementation, sink);
        NIIPCProofImpl *impl = owner->implementation;
        if (impl != NULL) emit(impl, sink, NI_IPC_IMPL_VIRTUAL_SLOT8, 8, 0);
    }
    emit(owner->implementation, sink, NI_IPC_DESTROY_NAME, 0, 0);
}
/* Original1002ed170 D2,1002ed160 D1 tail,1002ed140 D0+operator delete.
   Release leaves the old field value untouched in the inspected D2 body. */
void ni_ipc_os_destroy(NIIPCProofImpl *impl, NIIPCProofDtorKind kind,
                       const NIIPCProofSink *sink) {
    if (impl->port != 0) {
        remove_port_mapping(impl, sink);
        emit(impl, sink, NI_IPC_RELEASE_PORT, 0, 0);
    }
    if (impl->owner != NULL && impl->owner->implementation == NULL)
        emit(impl, sink, NI_IPC_OWNER_VIRTUAL_SLOT8, 8, 0);
    emit(impl, sink, NI_IPC_DESTROY_THREAD_NAME, 0, 0);
    if (kind == NI_IPC_DTOR_DELETING)
        emit(impl, sink, NI_IPC_OPERATOR_DELETE, 0, 0);
}
