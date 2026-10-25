#include "lifecycle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(value) do { if (!(value)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #value); exit(1); \
} } while (0)

typedef struct {
    NIIPCProofEvent events[128];
    size_t count;
    NIIPCProofOwner *detach_on_reset;
} Trace;
static void observe(void *context, NIIPCProofEvent event) {
    Trace *trace = context;
    CHECK(trace->count < 128);
    trace->events[trace->count++] = event;
    /* Synthetic guard perturbation only: resetMaster itself does NOT detach
       owner->implementation. This observer exercises the subsequent null check. */
    if (event.operation == NI_IPC_RESET_MASTER && trace->detach_on_reset != NULL)
        trace->detach_on_reset->implementation = NULL;
}
static NIIPCProofSink sink(Trace *trace) {
    return (NIIPCProofSink){trace, observe};
}
static void expect_trace(const Trace *trace, const NIIPCProofOperation *expected,
                         size_t count) {
    CHECK(trace->count == count);
    for (size_t i = 0; i < count; ++i) CHECK(trace->events[i].operation == expected[i]);
}
#define EXPECT(trace, ...) do { \
    const NIIPCProofOperation expected[] = {__VA_ARGS__}; \
    expect_trace(&(trace), expected, sizeof expected / sizeof expected[0]); \
} while (0)

/* Independent literal sequence: original run1002eee70, not a generated oracle. */
static void entry_ack_precedes_listener(void) {
    Trace trace = {0}; NIIPCProofSink output = sink(&trace);
    NIIPCProofOwner owner = {0};
    NIIPCProofImpl impl = {.owner = &owner, .port = 17, .active = 1};
    owner.implementation = &impl;
    ni_ipc_os_run(&impl, 23, 29, &output);
    EXPECT(trace, NI_IPC_SIGNAL_EVENT, NI_IPC_CREATE_SOURCE,
           NI_IPC_GET_CURRENT_LOOP, NI_IPC_ADD_SOURCE_COMMON_MODES,
           NI_IPC_RELEASE_SOURCE, NI_IPC_RUN_LOOP);
    CHECK(trace.events[0].loop == 0);
    CHECK(trace.events[1].argument == 0);
    CHECK(trace.events[3].source == 23 && trace.events[3].loop == 29);
    CHECK(impl.run_loop == 29);
}
/* Original public1002ebc30 and OS1002eec50: these traces differ on purpose. */
static void ordinary_stop_joins_before_holder_cleanup(void) {
    Trace trace = {0}; NIIPCProofSink output = sink(&trace);
    NIIPCProofOwner owner = {.callback_holder = true, .callback_managed = true};
    NIIPCProofImpl impl = {.owner = &owner, .port = 17, .run_loop = 29,
        .active = 1, .thread_handle = true, .thread_storage = {1, 2, 3, 4}};
    owner.implementation = &impl;
    ni_ipc_public_stop(&owner, false, &output);
    EXPECT(trace, NI_IPC_INVALIDATE_PORT, NI_IPC_STOP_LOOP,
           NI_IPC_WAIT_TERMINATION, NI_IPC_CLOSE_THREAD, NI_IPC_DISPOSE_CALLBACK);
    CHECK(trace.events[0].active == 0 && trace.events[0].owner_impl_present);
    CHECK(owner.implementation == &impl && !owner.callback_holder);
    CHECK(!impl.thread_handle && !impl.active && !impl.deferred);
    const uint64_t zero[4] = {0, 0, 0, 0};
    CHECK(memcmp(impl.thread_storage, zero, sizeof zero) == 0);
}
static void deferred_stop_detaches_and_does_not_join(void) {
    Trace trace = {0}; NIIPCProofSink output = sink(&trace);
    NIIPCProofOwner owner = {.callback_holder = true, .callback_managed = true};
    NIIPCProofImpl impl = {.owner = &owner, .port = 17, .run_loop = 29,
        .active = 1, .thread_handle = true, .thread_storage = {1, 2, 3, 4}};
    owner.implementation = &impl;
    ni_ipc_public_stop(&owner, true, &output);
    EXPECT(trace, NI_IPC_INVALIDATE_PORT, NI_IPC_STOP_LOOP);
    CHECK(trace.events[0].active == 0 && trace.events[0].deferred);
    CHECK(!trace.events[0].owner_impl_present);
    CHECK(owner.implementation == NULL && owner.callback_holder);
    CHECK(impl.thread_handle && impl.thread_storage[0] == 1 && impl.thread_storage[3] == 4);
}
/* Original OSclose1002eece0 and publiccloses1002eb990/1002ebca0. */
static void inactive_close_releases_under_original_guard(void) {
    Trace trace = {0}; NIIPCProofSink output = sink(&trace);
    NIIPCProofImpl impl = {.port = 17, .map_entry = true};
    ni_ipc_os_close(&impl, &output);
    EXPECT(trace, NI_IPC_CLEAR_INVALIDATION, NI_IPC_MAP_LOCK,
           NI_IPC_MAP_REMOVE, NI_IPC_MAP_UNLOCK, NI_IPC_RELEASE_PORT);
    CHECK(impl.port == 0 && !impl.map_entry);
    CHECK(trace.events[4].port == 17);
}
static void active_close_does_not_stop_or_release(void) {
    Trace trace = {0}; NIIPCProofSink output = sink(&trace);
    NIIPCProofOwner owner = {.buffer = 47, .capacity = 8};
    NIIPCProofImpl impl = {.owner = &owner, .port = 17, .active = 1,
        .map_entry = true, .thread_handle = true};
    owner.implementation = &impl;
    ni_ipc_public_close(&owner, true, &output);
    EXPECT(trace, NI_IPC_CLEAR_INVALIDATION);
    CHECK(impl.port == 17 && impl.active == 1 && impl.thread_handle && impl.map_entry);
    CHECK(owner.buffer == 0 && owner.capacity == 0);
    trace.count = 0;
    ni_ipc_public_close(&owner, false, &output);
    EXPECT(trace, NI_IPC_CLEAR_INVALIDATION);
}
static void public_close_guards_and_local_close_without_map_entry(void) {
    Trace trace = {0}; NIIPCProofSink output = sink(&trace);
    NIIPCProofOwner owner = {.buffer = 47, .capacity = 8};
    ni_ipc_public_close(&owner, true, &output);
    CHECK(trace.count == 0 && owner.buffer == 47 && owner.capacity == 8);
    NIIPCProofImpl impl = {.owner = &owner}; owner.implementation = &impl;
    ni_ipc_public_close(&owner, true, &output);
    CHECK(trace.count == 0 && owner.buffer == 47);
    ni_ipc_os_close(&impl, &output);
    EXPECT(trace, NI_IPC_CLEAR_INVALIDATION);
    trace.count = 0; impl.port = 17;
    ni_ipc_os_close(&impl, &output);
    EXPECT(trace, NI_IPC_CLEAR_INVALIDATION, NI_IPC_MAP_LOCK,
           NI_IPC_MAP_UNLOCK, NI_IPC_RELEASE_PORT);
    CHECK(impl.port == 0);
}
static void invalidation_marks_owner_only_for_matching_registry_link(void) {
    Trace trace = {0}; NIIPCProofSink output = sink(&trace);
    NIIPCProofOwner owner = {0};
    NIIPCProofImpl impl = {.owner = &owner, .port = 17, .map_entry = true};
    owner.implementation = &impl;
    ni_ipc_invalidated(&impl, 19, &output);
    EXPECT(trace, NI_IPC_MAP_LOCK, NI_IPC_MAP_FIND, NI_IPC_MAP_UNLOCK);
    CHECK(!owner.invalidated && impl.port == 17 && impl.map_entry);
    trace.count = 0;
    ni_ipc_invalidated(&impl, 17, &output);
    EXPECT(trace, NI_IPC_MAP_LOCK, NI_IPC_MAP_FIND, NI_IPC_MAP_UNLOCK);
    CHECK(owner.invalidated && impl.port == 17 && impl.map_entry);
    owner.invalidated = false; trace.count = 0; impl.owner = NULL;
    ni_ipc_invalidated(&impl, 17, &output);
    EXPECT(trace, NI_IPC_MAP_LOCK, NI_IPC_MAP_FIND, NI_IPC_MAP_UNLOCK);
    CHECK(!owner.invalidated);
}
/* Original1002ed9d1..e1: a NULL registry value is a guarded lookup miss. */
static void null_registry_candidate_is_a_lookup_miss(void) {
    Trace trace = {0}; NIIPCProofSink output = sink(&trace);
    ni_ipc_invalidated(NULL, 17, &output);
    EXPECT(trace, NI_IPC_MAP_LOCK, NI_IPC_MAP_FIND, NI_IPC_MAP_UNLOCK);
    CHECK(trace.events[1].argument == 17);
}
/* Original1002ed320 vs1002ee4d0: only remote registers map/invalidation. */
static void remote_open_and_local_create_have_distinct_ownership(void) {
    Trace trace = {0}; NIIPCProofSink output = sink(&trace);
    NIIPCProofOwner owner = {0}; NIIPCProofImpl impl = {0};
    owner.implementation = &impl;
    CHECK(ni_ipc_os_open(&impl, &owner, (NIIPCProofCreateOutcome){true, 17}, &output));
    EXPECT(trace, NI_IPC_CREATE_NAME, NI_IPC_CREATE_REMOTE, NI_IPC_RELEASE_NAME,
           NI_IPC_MAP_LOCK, NI_IPC_MAP_INSERT, NI_IPC_MAP_UNLOCK, NI_IPC_INSTALL_INVALIDATION);
    CHECK(impl.port == 17 && impl.owner == &owner && impl.map_entry);
    trace.count = 0; impl = (NIIPCProofImpl){0};
    CHECK(ni_ipc_public_create(&owner, 47, 8, (NIIPCProofCreateOutcome){true, 17}, &output));
    EXPECT(trace, NI_IPC_CREATE_NAME, NI_IPC_CREATE_LOCAL, NI_IPC_RELEASE_NAME);
    CHECK(impl.port == 17 && impl.owner == &owner && !impl.map_entry);
    CHECK(owner.buffer == 47 && owner.capacity == 8);
    const NIIPCProofEvent local = trace.events[1];
    CHECK(local.context_version == 0 && local.context_info == (uintptr_t)&impl);
    CHECK(local.callback_source_address == UINT64_C(0x1002ee910));
    CHECK(local.context_callbacks_null && local.should_free_info_null);
}
static void creation_failure_keeps_original_buffer_and_owner_fields(void) {
    Trace trace = {0}; NIIPCProofSink output = sink(&trace);
    NIIPCProofOwner owner = {0}; NIIPCProofImpl impl = {0}; owner.implementation = &impl;
    CHECK(!ni_ipc_public_create(&owner, 47, 8, (NIIPCProofCreateOutcome){true, 0}, &output));
    EXPECT(trace, NI_IPC_CREATE_NAME, NI_IPC_CREATE_LOCAL, NI_IPC_RELEASE_NAME);
    CHECK(owner.buffer == 47 && owner.capacity == 8 && impl.owner == &owner && impl.port == 0);
    trace.count = 0;
    CHECK(!ni_ipc_os_open(&impl, &owner, (NIIPCProofCreateOutcome){false, 17}, &output));
    EXPECT(trace, NI_IPC_CREATE_NAME);
    CHECK(impl.owner == &owner && impl.port == 0);
    trace.count = 0;
    CHECK(!ni_ipc_os_create(&impl, &owner, (NIIPCProofCreateOutcome){false, 17}, &output));
    EXPECT(trace, NI_IPC_CREATE_NAME);
}
static void creation_rejects_original_guard_inputs_without_calls(void) {
    Trace trace = {0}; NIIPCProofSink output = sink(&trace);
    NIIPCProofOwner owner = {0}; NIIPCProofImpl impl = {0}; owner.implementation = &impl;
    CHECK(!ni_ipc_public_create(&owner, 0, 8, (NIIPCProofCreateOutcome){true, 17}, &output));
    CHECK(!ni_ipc_public_create(&owner, 47, 0, (NIIPCProofCreateOutcome){true, 17}, &output));
    CHECK(!ni_ipc_os_create(&impl, NULL, (NIIPCProofCreateOutcome){true, 17}, &output));
    CHECK(!ni_ipc_os_open(&impl, NULL, (NIIPCProofCreateOutcome){true, 17}, &output));
    CHECK(trace.count == 0);
    impl.port = 19;
    CHECK(!ni_ipc_os_open(&impl, &owner, (NIIPCProofCreateOutcome){true, 17}, &output));
    CHECK(!ni_ipc_os_create(&impl, &owner, (NIIPCProofCreateOutcome){true, 17}, &output));
    CHECK(trace.count == 0 && impl.port == 19);
}
/* Original1002ee9c0 raw wait/priority arguments; no GP scheduling inference. */
static void start_preserves_raw_arguments_and_guards(void) {
    Trace trace = {0}; NIIPCProofSink output = sink(&trace);
    NIIPCProofOwner owner = {0}; NIIPCProofImpl impl = {.owner = &owner, .port = 17};
    owner.implementation = &impl;
    ni_ipc_public_start(&owner, true, true,
        (NIIPCProofStartOutcome){.event_created = true, .thread_created = true,
            .wait_succeeded = true, .converted_priority = 42}, &output);
    EXPECT(trace, NI_IPC_ASSIGN_CALLBACK, NI_IPC_CREATE_EVENT,
           NI_IPC_PRIORITY_ENUM, NI_IPC_CREATE_THREAD,
           NI_IPC_WAIT_EVENT, NI_IPC_DESTROY_EVENT);
    CHECK(trace.events[1].active == 1 && trace.events[2].argument == 4);
    CHECK(trace.events[3].argument == 42);
    CHECK(trace.events[4].argument == 5000);
    CHECK(impl.active == 1 && !impl.event_handle && impl.thread_handle);
    CHECK(owner.callback_holder && owner.callback_managed);
    trace.count = 0;
    ni_ipc_public_start(&owner, false, false,
        (NIIPCProofStartOutcome){.event_created = true, .thread_created = true,
            .wait_succeeded = true, .converted_priority = 42}, &output);
    EXPECT(trace, NI_IPC_ASSIGN_CALLBACK);
    CHECK(!owner.callback_holder);
    trace.count = 0; owner.implementation = NULL;
    ni_ipc_public_start(&owner, true, true,
        (NIIPCProofStartOutcome){.event_created = true, .thread_created = true,
            .wait_succeeded = true, .converted_priority = 42}, &output);
    CHECK(trace.count == 0);
    owner.implementation = &impl; impl.port = 0;
    ni_ipc_public_start(&owner, true, true,
        (NIIPCProofStartOutcome){.event_created = true, .thread_created = true,
            .wait_succeeded = true, .converted_priority = 42}, &output);
    CHECK(trace.count == 0);
}
static void failed_wait_has_same_event_cleanup_without_retry(void) {
    Trace trace = {0}; NIIPCProofSink output = sink(&trace);
    NIIPCProofOwner owner = {0};
    NIIPCProofImpl impl = {.owner = &owner, .event_handle = true, .thread_handle = true};
    owner.implementation = &impl;
    ni_ipc_os_start(&impl, (NIIPCProofStartOutcome){.event_created = false, .thread_created = false,
            .wait_succeeded = false, .converted_priority = 73}, &output);
    EXPECT(trace, NI_IPC_PRIORITY_ENUM, NI_IPC_WAIT_EVENT, NI_IPC_DESTROY_EVENT);
    CHECK(impl.active == 1 && impl.thread_handle && !impl.event_handle);
    trace.count = 0; impl.active = 0; impl.thread_handle = false;
    ni_ipc_os_start(&impl, (NIIPCProofStartOutcome){.event_created = false, .thread_created = false,
            .wait_succeeded = false, .converted_priority = 73}, &output);
    EXPECT(trace, NI_IPC_CREATE_EVENT, NI_IPC_PRIORITY_ENUM,
           NI_IPC_CREATE_THREAD, NI_IPC_WAIT_EVENT);
    CHECK(!impl.event_handle && !impl.thread_handle && impl.active == 1);
}
static void inactive_run_still_acknowledges_entry(void) {
    Trace trace = {0}; NIIPCProofSink output = sink(&trace);
    NIIPCProofImpl impl = {0};
    ni_ipc_os_run(&impl, 23, 29, &output);
    EXPECT(trace, NI_IPC_SIGNAL_EVENT);
    CHECK(impl.run_loop == 0);
    trace.count = 0; impl.active = 2;
    ni_ipc_os_run(&impl, 23, 29, &output);
    EXPECT(trace, NI_IPC_SIGNAL_EVENT);
}
/* Original owning D2s/baseD1 call resetMaster, re-read, then qualified slot8. */
static void owning_teardown_does_not_invent_stop_or_virtual_target(void) {
    Trace trace = {0}; NIIPCProofSink output = sink(&trace);
    NIIPCProofOwner owner = {.callback_holder = true, .callback_managed = true};
    NIIPCProofImpl impl = {.owner = &owner, .port = 17, .active = 1,
        .thread_handle = true}; owner.implementation = &impl;
    ni_ipc_owner_destroy(&owner, NI_IPC_RECEIVE_OWNER, &output);
    EXPECT(trace, NI_IPC_DISPOSE_CALLBACK, NI_IPC_RESET_MASTER,
           NI_IPC_IMPL_VIRTUAL_SLOT8, NI_IPC_DESTROY_NAME);
    CHECK(!owner.callback_holder && impl.owner == NULL);
    CHECK(impl.active == 1 && impl.thread_handle && impl.port == 17);
    trace.count = 0; owner.callback_holder = true; impl.owner = &owner;
    ni_ipc_owner_destroy(&owner, NI_IPC_SEND_OWNER, &output);
    EXPECT(trace, NI_IPC_DISPOSE_CALLBACK, NI_IPC_RESET_MASTER,
           NI_IPC_IMPL_VIRTUAL_SLOT8, NI_IPC_DESTROY_NAME);
}
static void owning_teardown_keeps_post_reset_null_guard(void) {
    Trace trace = {0}; NIIPCProofSink output = sink(&trace);
    NIIPCProofOwner owner = {0}; NIIPCProofImpl impl = {.owner = &owner};
    owner.implementation = &impl; trace.detach_on_reset = &owner;
    ni_ipc_owner_destroy(&owner, NI_IPC_BASE_OWNER, &output);
    EXPECT(trace, NI_IPC_RESET_MASTER, NI_IPC_DESTROY_NAME);
    CHECK(owner.implementation == NULL && impl.owner == NULL);
    trace.count = 0; owner.callback_holder = true; owner.callback_managed = true;
    ni_ipc_owner_destroy(&owner, NI_IPC_RECEIVE_OWNER, &output);
    EXPECT(trace, NI_IPC_DISPOSE_CALLBACK, NI_IPC_DESTROY_NAME);
}
/* Original OSD0/D1/D2: no active guard, invalidate, stop or implicit join. */
static void os_destructor_variants_preserve_qualified_dispatch(void) {
    for (int variant = NI_IPC_DTOR_BASE; variant <= NI_IPC_DTOR_DELETING; ++variant) {
        Trace trace = {0}; NIIPCProofSink output = sink(&trace);
        NIIPCProofOwner owner = {0};
        NIIPCProofImpl impl = {.owner = &owner, .port = 17, .active = 1,
            .thread_handle = true, .map_entry = true};
        ni_ipc_os_destroy(&impl, (NIIPCProofDtorKind)variant, &output);
        if (variant == NI_IPC_DTOR_DELETING)
            EXPECT(trace, NI_IPC_MAP_LOCK, NI_IPC_MAP_REMOVE, NI_IPC_MAP_UNLOCK,
                   NI_IPC_RELEASE_PORT, NI_IPC_OWNER_VIRTUAL_SLOT8,
                   NI_IPC_DESTROY_THREAD_NAME, NI_IPC_OPERATOR_DELETE);
        else
            EXPECT(trace, NI_IPC_MAP_LOCK, NI_IPC_MAP_REMOVE, NI_IPC_MAP_UNLOCK,
                   NI_IPC_RELEASE_PORT, NI_IPC_OWNER_VIRTUAL_SLOT8,
                   NI_IPC_DESTROY_THREAD_NAME);
        CHECK(impl.port == 17 && impl.active == 1 && impl.thread_handle && !impl.map_entry);
    }
    Trace trace = {0}; NIIPCProofSink output = sink(&trace);
    NIIPCProofOwner owner = {0}; NIIPCProofImpl impl = {.owner = &owner};
    owner.implementation = &impl;
    ni_ipc_os_destroy(&impl, NI_IPC_DTOR_COMPLETE, &output);
    EXPECT(trace, NI_IPC_DESTROY_THREAD_NAME);
    trace.count = 0; impl.owner = NULL;
    ni_ipc_os_destroy(&impl, NI_IPC_DTOR_BASE, &output);
    EXPECT(trace, NI_IPC_DESTROY_THREAD_NAME);
}
static void inactive_stop_preserves_public_asymmetry(void) {
    Trace trace = {0}; NIIPCProofSink output = sink(&trace);
    NIIPCProofOwner owner = {.callback_holder = true, .callback_managed = true};
    NIIPCProofImpl impl = {.owner = &owner}; owner.implementation = &impl;
    ni_ipc_public_stop(&owner, false, &output);
    EXPECT(trace, NI_IPC_DISPOSE_CALLBACK);
    CHECK(!owner.callback_holder && owner.implementation == &impl);
    trace.count = 0; owner.callback_holder = true;
    ni_ipc_public_stop(&owner, true, &output);
    CHECK(trace.count == 0 && owner.implementation == NULL && owner.callback_holder);
    CHECK(!impl.deferred && !impl.active);
}
int main(void) {
    entry_ack_precedes_listener();
    ordinary_stop_joins_before_holder_cleanup();
    deferred_stop_detaches_and_does_not_join();
    inactive_close_releases_under_original_guard();
    active_close_does_not_stop_or_release();
    public_close_guards_and_local_close_without_map_entry();
    invalidation_marks_owner_only_for_matching_registry_link();
    null_registry_candidate_is_a_lookup_miss();
    remote_open_and_local_create_have_distinct_ownership();
    creation_failure_keeps_original_buffer_and_owner_fields();
    creation_rejects_original_guard_inputs_without_calls();
    start_preserves_raw_arguments_and_guards();
    failed_wait_has_same_event_cleanup_without_retry();
    inactive_run_still_acknowledges_entry();
    owning_teardown_does_not_invent_stop_or_virtual_target();
    owning_teardown_keeps_post_reset_null_guard();
    os_destructor_variants_preserve_qualified_dispatch();
    inactive_stop_preserves_public_asymmetry();
    puts("PASS lifecycle: 18 cases");
    return 0;
}
