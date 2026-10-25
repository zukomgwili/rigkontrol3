#ifndef NI_IPC_PROOF_TRANSPORT_H
#define NI_IPC_PROOF_TRANSPORT_H

#include <CoreFoundation/CoreFoundation.h>
#include <stdbool.h>
#include <stdint.h>

/* Offline proof only. Operations are injected; no live port implementation.
 * Callers provide physically valid backing spans, including four readable ID
 * bytes for short sends, as required by the unguarded original NI boundary. */
typedef struct {
    void *context;
    CFDataRef (*create_data)(void *, const UInt8 *, CFIndex);
    void (*release_data)(void *, CFDataRef);
    CFIndex (*data_length)(void *, CFDataRef);
    const UInt8 *(*data_bytes)(void *, CFDataRef);
    SInt32 (*send_request)(void *, CFMessagePortRef, SInt32, CFDataRef,
                           CFTimeInterval, CFTimeInterval, CFStringRef, CFDataRef *);
} NIIPCProofDataOps;

typedef struct {
    CFMessagePortRef port;
    const NIIPCProofDataOps *ops;
    bool invalidated;
    void (*on_invalidated)(void *);
    void *callback_context;
} NIIPCProofSender;

bool ni_ipc_proof_send(NIIPCProofSender *, const uint8_t *, uint64_t,
                       uint8_t *, uint64_t);
bool ni_ipc_proof_nhl_send(NIIPCProofSender *, uint8_t *);

typedef bool (*NIIPCProofHandler)(void *, uint64_t, uint8_t **, uint64_t *);
typedef struct {
    uint8_t *buffer;
    uint64_t capacity;
    NIIPCProofHandler handler;
    void *context;
} NIIPCProofReceiver;
CFDataRef ni_ipc_proof_receive(const NIIPCProofDataOps *, NIIPCProofReceiver *,
                              SInt32, CFDataRef);

/* Output must hold strlen(name)+1 units; sequence_count is intentionally ignored
 * by this recovered stored-name boundary. No allocation is performed here. */
size_t ni_ipc_proof_name_units(const char *, uint64_t, UniChar *);
uint32_t ni_ipc_proof_allocation_count(uint32_t);
#endif
