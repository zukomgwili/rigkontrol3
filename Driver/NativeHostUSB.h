#ifndef RK3_NATIVE_HOST_USB_H
#define RK3_NATIVE_HOST_USB_H

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOReturn.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RK3HostInterface RK3HostInterface;
typedef struct RK3HostBuffer RK3HostBuffer;
typedef struct RK3HostIsoList RK3HostIsoList;
typedef struct {
    uint8_t direction; /* USB direction: 0 OUT, 1 IN. */
    uint8_t number;
    uint8_t transfer_type; /* USB bmAttributes low two bits. */
    uint8_t interval;
    uint16_t max_packet_size;
} RK3HostPipeProperties;
typedef struct {
    int32_t status;
    uint32_t requested;
    uint32_t actual;
    uint64_t host_time;
} RK3HostIsoFrame;
#define RK3_HOST_ISO_PENDING ((int32_t)kIOReturnInvalid)

typedef void (*RK3HostBulkCompletion)(void *context, int32_t status, size_t actual);
typedef void (*RK3HostIsoCompletion)(void *context, int32_t status,
                                    RK3HostIsoList *list, size_t first, size_t count);

/* All calls except cached bytes/size, primary snapshots, feature query
 * and cached connected query belong to the supplied worker CFRunLoop. Successful
 * asynchronous submissions deliver exactly one completion on that loop; rejected
 * submissions deliver none.
 * Caller contexts and bulk read storage must survive through completion.
 * The adapter retains the interface and mapped ISO storage until delivery ends.
 * Open claims only this child interface, with no capture/seize option. A retained
 * legacy parent-device handle must be CLOSED before opening the child here. */
RK3HostInterface *rk3_host_open(io_service_t service, CFRunLoopRef worker, int32_t *error);
void rk3_host_retain(RK3HostInterface *interface);
void rk3_host_release(RK3HostInterface *interface);
int32_t rk3_host_close(RK3HostInterface *interface); /* Busy until completions and mapped storage drain. */
int32_t rk3_host_get_alt(RK3HostInterface *interface, uint8_t *alternate);
/* Alternate changes also require all mapped buffers/lists to be released. */
int32_t rk3_host_set_alt(RK3HostInterface *interface, uint8_t alternate);
int32_t rk3_host_num_pipes(RK3HostInterface *interface, uint8_t *count);
int32_t rk3_host_pipe_properties(RK3HostInterface *interface, uint8_t pipe,
                                 RK3HostPipeProperties *properties);
int32_t rk3_host_abort(RK3HostInterface *interface, uint8_t pipe);
int32_t rk3_host_clear_stall(RK3HostInterface *interface, uint8_t pipe);
int32_t rk3_host_bus_frame(RK3HostInterface *interface, uint64_t *frame, uint64_t *host_time);
size_t rk3_host_pending(const RK3HostInterface *interface);
/* Temporary first-fault observation only, on the existing worker. A stream
 * call establishes a fresh interface-owned diagnostic epoch even if a new
 * engine reuses its own seed. Clock marks surround actual callbacks only. */
void rk3_host_measurement_begin_stream(RK3HostInterface *,uint64_t stream_epoch);
void rk3_host_measurement_clock(RK3HostInterface *,bool beginning,uint64_t tick);
/* Cached atomic only: no Objective-C, I/O or allocation. Caller must pin owner
 * lifetime. False after service termination or successful close; suspend/resume
 * and transient pipe errors do not imply removal. A new open creates a new cache. */
bool rk3_host_connected(const RK3HostInterface *interface);

/* Pipe indices are 1-based endpoint-descriptor order, retaining IOUSBLib-style
 * numbering. Timeout is the total completion timeout in milliseconds; zero is
 * unlimited. Bulk writes snapshot caller command bytes, not audio ring data. */
int32_t rk3_host_bulk_read(RK3HostInterface *interface, uint8_t pipe,
                          void *bytes, size_t capacity, uint32_t timeout_ms,
                          RK3HostBulkCompletion completion, void *context);
int32_t rk3_host_bulk_write(RK3HostInterface *interface, uint8_t pipe,
                           const void *bytes, size_t length, uint32_t timeout_ms,
                           RK3HostBulkCompletion completion, void *context);

/* Fixed-size controller-optimized IOBufferMemoryDescriptor mappings, explicitly
 * non-bounced by IOUSBHostObject's documented allocation contract. Bytes access
 * performs no Objective-C call. Future queued payload updates still require the
 * caller's NI-derived safe sample-position/write-ahead and lifetime discipline;
 * this adapter makes no promise about arbitrary concurrent DMA writes/prefetch. */
RK3HostBuffer *rk3_host_buffer_create(RK3HostInterface *interface, size_t size, int32_t *error);
void *rk3_host_buffer_bytes(RK3HostBuffer *buffer);
size_t rk3_host_buffer_size(const RK3HostBuffer *buffer);
void rk3_host_buffer_release(RK3HostBuffer *buffer);
RK3HostIsoList *rk3_host_iso_list_create(RK3HostInterface *interface, size_t count, int32_t *error);
size_t rk3_host_iso_list_count(const RK3HostIsoList *list);
void rk3_host_iso_list_release(RK3HostIsoList *list);
/* The explicit KernelUserShared list allocation is available on macOS26.4+.
 * This query tests that allocation API's availability, not proven primary-
 * interrupt publication cadence. The older mapped fallback likewise has no
 * established primary-update parity; neither path proves interrupt timing. */
bool rk3_host_primary_lists_supported(void);
int32_t rk3_host_iso_list_set(RK3HostIsoList *list, size_t index,
                              uint32_t requested, uint32_t offset, bool wrap);
/* Snapshot has no Objective-C or USB call. The engine must pin list lifetime
 * and serialize its own snapshot/reset/reuse of the same entry. Kernel primary
 * completion updates use the SDK's status/actual-count contract; the public
 * API does not establish primary-interrupt publication timing. */
bool rk3_host_iso_list_snapshot(const RK3HostIsoList *list, size_t index,
                                RK3HostIsoFrame *frame);
/* No audio copy: offsets reference data's same mapped backing store. Wrap is
 * the public transaction option, including within one microframe; unsupported
 * controllers reject it. Entries cannot be reused until their callback begins.
 * Both list and data must belong to this interface. Successful ISO submissions
 * deliver callbacks in acceptance order per pipe, including aborted/error and
 * zero-count transfers. Endpoints are independent. A rejected submission takes
 * no position and delivers no callback. Raw system completions may arrive out
 * of order; mappings, pending counts and busy ranges stay pinned until ordered
 * delivery. The range is retired before its callback, permitting requeue. */
int32_t rk3_host_iso_enqueue(RK3HostInterface *interface, uint8_t pipe,
                             RK3HostBuffer *data, RK3HostIsoList *list,
                             size_t first, size_t count, uint64_t start_frame,
                             RK3HostIsoCompletion completion, void *context);

#ifdef __cplusplus
}
#endif
#endif
