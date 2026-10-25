#ifndef NI_IPC_PROOF_LIFECYCLE_H
#define NI_IPC_PROOF_LIFECYCLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Proof-only semantic state, NOT NI C++ object layout. No operation executes. */
typedef struct NIIPCProofImpl NIIPCProofImpl;
typedef struct {
    NIIPCProofImpl *implementation;
    uintptr_t buffer;
    uint64_t capacity;
    bool callback_holder;
    bool callback_managed;
    bool invalidated;
} NIIPCProofOwner;
struct NIIPCProofImpl {
    NIIPCProofOwner *owner;
    uintptr_t port;
    uintptr_t run_loop;
    uint32_t active;
    bool deferred;
    bool event_handle;
    bool thread_handle;
    uint64_t thread_storage[4];
    bool map_entry;
};

typedef enum {
    NI_IPC_SIGNAL_EVENT,
    NI_IPC_CREATE_SOURCE,
    NI_IPC_GET_CURRENT_LOOP,
    NI_IPC_ADD_SOURCE_COMMON_MODES,
    NI_IPC_RELEASE_SOURCE,
    NI_IPC_RUN_LOOP,
    NI_IPC_CREATE_EVENT,
    NI_IPC_PRIORITY_ENUM,
    NI_IPC_CREATE_THREAD,
    NI_IPC_WAIT_EVENT,
    NI_IPC_DESTROY_EVENT,
    NI_IPC_ASSIGN_CALLBACK,
    NI_IPC_DISPOSE_CALLBACK,
    NI_IPC_INVALIDATE_PORT,
    NI_IPC_STOP_LOOP,
    NI_IPC_WAIT_TERMINATION,
    NI_IPC_CLOSE_THREAD,
    NI_IPC_CLEAR_INVALIDATION,
    NI_IPC_MAP_LOCK,
    NI_IPC_MAP_INSERT,
    NI_IPC_MAP_FIND,
    NI_IPC_MAP_REMOVE,
    NI_IPC_MAP_UNLOCK,
    NI_IPC_RELEASE_PORT,
    NI_IPC_CREATE_NAME,
    NI_IPC_CREATE_REMOTE,
    NI_IPC_CREATE_LOCAL,
    NI_IPC_RELEASE_NAME,
    NI_IPC_INSTALL_INVALIDATION,
    NI_IPC_RESET_MASTER,
    NI_IPC_IMPL_VIRTUAL_SLOT8,
    NI_IPC_OWNER_VIRTUAL_SLOT8,
    NI_IPC_DESTROY_NAME,
    NI_IPC_DESTROY_THREAD_NAME,
    NI_IPC_OPERATOR_DELETE
} NIIPCProofOperation;

typedef struct {
    NIIPCProofOperation operation;
    uint64_t argument;
    uintptr_t port, source, loop;
    uint32_t active;
    bool owner_impl_present, deferred;
    intptr_t context_version;
    uintptr_t context_info, callback_source_address;
    bool context_callbacks_null, should_free_info_null;
} NIIPCProofEvent;
typedef struct {
    void *context;
    void (*observe)(void *context, NIIPCProofEvent event);
} NIIPCProofSink;

typedef struct {
    bool name_created;
    uintptr_t created_port;
} NIIPCProofCreateOutcome;
typedef struct {
    bool event_created;
    bool thread_created;
    bool wait_succeeded;
    /* Opaque GP priority-constructor result; enum4 is NOT assumed identical. */
    uint32_t converted_priority;
} NIIPCProofStartOutcome;

typedef enum { NI_IPC_BASE_OWNER, NI_IPC_SEND_OWNER, NI_IPC_RECEIVE_OWNER } NIIPCProofOwnerKind;
typedef enum { NI_IPC_DTOR_BASE, NI_IPC_DTOR_COMPLETE, NI_IPC_DTOR_DELETING } NIIPCProofDtorKind;

void ni_ipc_os_run(NIIPCProofImpl *impl, uintptr_t source_result,
                   uintptr_t current_loop, const NIIPCProofSink *sink);
void ni_ipc_os_stop(NIIPCProofImpl *impl, bool deferred, const NIIPCProofSink *sink);
/* As original, public stop requires a live implementation pointer. */
void ni_ipc_public_stop(NIIPCProofOwner *owner, bool deferred,
                        const NIIPCProofSink *sink);
void ni_ipc_os_close(NIIPCProofImpl *impl, const NIIPCProofSink *sink);
void ni_ipc_public_close(NIIPCProofOwner *owner, bool receive,
                         const NIIPCProofSink *sink);
void ni_ipc_invalidated(NIIPCProofImpl *registry_candidate, uintptr_t incoming_port,
                        const NIIPCProofSink *sink);
bool ni_ipc_os_open(NIIPCProofImpl *impl, NIIPCProofOwner *owner,
                     NIIPCProofCreateOutcome outcome, const NIIPCProofSink *sink);
bool ni_ipc_os_create(NIIPCProofImpl *impl, NIIPCProofOwner *owner,
                       NIIPCProofCreateOutcome outcome, const NIIPCProofSink *sink);
bool ni_ipc_public_create(NIIPCProofOwner *owner, uintptr_t buffer, uint64_t capacity,
                           NIIPCProofCreateOutcome outcome, const NIIPCProofSink *sink);
void ni_ipc_os_start(NIIPCProofImpl *impl, NIIPCProofStartOutcome outcome,
                     const NIIPCProofSink *sink);
void ni_ipc_public_start(NIIPCProofOwner *owner, bool callback_present,
                         bool callback_managed, NIIPCProofStartOutcome outcome,
                         const NIIPCProofSink *sink);
void ni_ipc_reset_master(NIIPCProofImpl *impl, const NIIPCProofSink *sink);
void ni_ipc_owner_destroy(NIIPCProofOwner *owner, NIIPCProofOwnerKind kind,
                          const NIIPCProofSink *sink);
void ni_ipc_os_destroy(NIIPCProofImpl *impl, NIIPCProofDtorKind kind,
                       const NIIPCProofSink *sink);
#endif
