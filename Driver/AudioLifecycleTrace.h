#ifndef RK3_AUDIO_LIFECYCLE_TRACE_H
#define RK3_AUDIO_LIFECYCLE_TRACE_H
#include <stddef.h>
#include <stdint.h>

/* Diagnostic-only ARM64 host-order wire format. Not NativeInfo or an NI ABI.
 * Each snapshot is a bounded, non-atomic collection. Journal reservations do
 * not establish exact API execution order, ownership, PCM outcome or parity. */
enum {
    RK3_AUDIO_TRACE_PROPERTY=0x6e617472,
    RK3_AUDIO_TRACE_VERSION=1,
    RK3_AUDIO_TRACE_BUILD=17,
    RK3_AUDIO_TRACE_CLIENT_LIMIT=64,
    RK3_AUDIO_TRACE_EVENT_LIMIT=256,
    RK3_AUDIO_TRACE_EVENT_ADD=1,
    RK3_AUDIO_TRACE_EVENT_REMOVE=2,
    RK3_AUDIO_TRACE_EVENT_START_ENTER=3,
    RK3_AUDIO_TRACE_EVENT_START_EXIT=4,
    RK3_AUDIO_TRACE_EVENT_STOP_ENTER=5,
    RK3_AUDIO_TRACE_EVENT_STOP_EXIT=6,
    RK3_AUDIO_TRACE_EVENT_ACTIVITY=7,
    RK3_AUDIO_TRACE_READ=1,
    RK3_AUDIO_TRACE_WRITE=2,
    RK3_AUDIO_TRACE_OTHER=3,
    RK3_AUDIO_TRACE_FAULT_CLIENT_FULL=1,
    RK3_AUDIO_TRACE_FAULT_EVENT_FULL=2,
    RK3_AUDIO_TRACE_FAULT_REUSE=4,
    RK3_AUDIO_TRACE_FAULT_WRAP=8,
    RK3_AUDIO_TRACE_FAULT_BAD_METADATA=16,
    RK3_AUDIO_TRACE_IDENTITY_CLIENT=1,
    RK3_AUDIO_TRACE_IDENTITY_PID=2,
    RK3_AUDIO_TRACE_PID_UNKNOWN=0,
    RK3_AUDIO_TRACE_PID_PUBLISHING=1,
    RK3_AUDIO_TRACE_PID_READY=2
};
typedef struct {
    uint64_t key; /* UInt32 native client ID + 1; zero means unused. */
    int32_t pid;
    uint32_t pid_state;
    uint64_t adds,removes,io_read,io_write,io_other,will_read,will_write,will_other;
    uint32_t will_true_mask,last_read_stream,last_write_stream,reserved;
} RK3AudioTraceClient;
typedef struct {
    uint64_t reservation,host_time;
    uint32_t ready,kind,client_id,identity_flags;
    int32_t pid,status;
    uint32_t object_id,value;
    uint64_t io_read,io_write;
} RK3AudioTraceEvent;
typedef struct {
    uint32_t version,build,client_limit,event_limit,faults,pending_events,pending_pids,used_clients;
    /* Capped reservation counts bracketing the snapshot. Only journal[0,
     * events_begin) is copied; differing counts mean unstable/incomplete. */
    uint64_t events_begin,events_end;
    RK3AudioTraceClient clients[RK3_AUDIO_TRACE_CLIENT_LIMIT];
    RK3AudioTraceEvent events[RK3_AUDIO_TRACE_EVENT_LIMIT];
} RK3AudioTraceSnapshot;
_Static_assert(sizeof(RK3AudioTraceClient)==96,"trace client wire size");
_Static_assert(offsetof(RK3AudioTraceClient,pid)==8,"trace PID wire offset");
_Static_assert(offsetof(RK3AudioTraceClient,adds)==16,"trace counts wire offset");
_Static_assert(offsetof(RK3AudioTraceClient,will_true_mask)==80,"trace mask wire offset");
_Static_assert(offsetof(RK3AudioTraceClient,reserved)==92,"trace reserved wire offset");
_Static_assert(sizeof(RK3AudioTraceEvent)==64,"trace event wire size");
_Static_assert(offsetof(RK3AudioTraceEvent,ready)==16,"trace ready wire offset");
_Static_assert(offsetof(RK3AudioTraceEvent,pid)==32,"trace event PID offset");
_Static_assert(offsetof(RK3AudioTraceEvent,io_read)==48,"trace event counts offset");
_Static_assert(sizeof(RK3AudioTraceSnapshot)==22576,"trace snapshot wire size");
_Static_assert(offsetof(RK3AudioTraceSnapshot,events_begin)==32,"trace bounds wire offset");
_Static_assert(offsetof(RK3AudioTraceSnapshot,clients)==48,"trace client array offset");
_Static_assert(offsetof(RK3AudioTraceSnapshot,events)==6192,"trace event array offset");
#endif
