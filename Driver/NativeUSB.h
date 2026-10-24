#ifndef RK3_NATIVE_USB_H
#define RK3_NATIVE_USB_H

#include "DeviceProtocol.h"
#include "NativeControl.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct rk3_usb_session rk3_usb_session;

typedef enum {
    RK3_USB_OPENING, RK3_USB_READY, RK3_USB_STARTING, RK3_USB_RUNNING,
    RK3_USB_STOPPING, RK3_USB_FAILED, RK3_USB_CLOSED
} rk3_usb_state;

typedef struct {
    uint64_t host_time; /* mach_absolute_time(), receipt time on USB worker */
    uint64_t sequence;
    uint8_t length;
    uint8_t bytes[RK3_COMMAND_MAX_BYTES];
} rk3_usb_report;

typedef struct {
    rk3_usb_state state;
    int32_t error_code;
    char error[192];
    rk3_device_info info;
    uint64_t reports_received;
    uint64_t reports_dropped;
    uint64_t commands_completed;
} rk3_usb_status;

/* Creates the dedicated thread and returns without waiting for hardware.
 * It repeatedly opens exactly one RK3, reapplies advertised cfg1 like NI
 * initialization, opens interface0/alt1 without seizure, and validates the
 * actual device-info reply. Native audio supports verified stereo/mode2 only.
 * Returns NULL only for allocation/thread creation failure. Disconnected and
 * failed state is observable; reconnection increments info.generation.
 * No previous implementation is used. */
rk3_usb_session *rk3_usb_open(char *error, size_t error_size);

/* Initiates stop/abort/drain and relinquishes this caller's session ownership.
 * Do not use the handle afterward. The worker retains all callback storage
 * until I/O quiesces; it never exits the host process to force cancellation.
 * Callers must finish concurrent API calls before closing the handle. */
void rk3_usb_close(rk3_usb_session *session);
/* Initiates close, waits up to timeout_ms (capped at 30000), relinquishes the
 * caller's handle on either result. True means the worker fully drained and
 * reached CLOSED. A false result retains worker-owned pending storage. */
bool rk3_usb_close_wait(rk3_usb_session *session, uint32_t timeout_ms);
bool rk3_usb_get_status(rk3_usb_session *session, rk3_usb_status *status);
bool rk3_usb_info(rk3_usb_session *session, RK3NativeInfo *info);
bool rk3_usb_reports(rk3_usb_session *session, const RK3NativeReportQuery *query,
                     RK3NativeReports *reports);
typedef struct {
    uint64_t sample_frame;
    uint64_t host_time;
    uint64_t seed;
} rk3_usb_clock_snapshot;
bool rk3_usb_clock(rk3_usb_session *session, rk3_usb_clock_snapshot *clock);

/* Bounded synchronous request to the serial USB worker. True means the actual
 * USB MIDI write completed. Internal real-format command9 requires reply[1]==1;
 * its rate-ff stop sentinel accepts a bounded reply after a successful write.
 * Caller timeout is 1..5000ms; an expired command/start not yet begun is
 * cancelled. Stop cleanup may finish after its caller's timeout.
 * Exposes NI MIDI command7 and only the exact nine-byte controller LED frame
 * [05,00,40,40,00,mask,toe(0|1),00,00], with the same generation lease.
 * Audio command9/info command1 belong to the worker; arbitrary writes and
 * automatic report commands remain unavailable. */
bool rk3_usb_command(rk3_usb_session *session, const uint8_t *bytes,
                     size_t length, uint32_t timeout_ms);
bool rk3_usb_command_for_generation(rk3_usb_session *session, const uint8_t *bytes,
                     size_t length, uint64_t generation, uint32_t timeout_ms);
bool rk3_usb_write_midi(rk3_usb_session *session, uint8_t port,
                        const uint8_t *bytes, size_t length, uint32_t timeout_ms);
/* First hardware format: 48000 Hz, stereo Float32 host PCM, NI wire mode2.
 * Start returns zero only after the real USB audio startup completes.
 * Data functions are bounded/nonblocking; they return actual frames queued
 * or captured; unavailable capture frames are zero-filled. A zero return
 * means no data/capacity, never synthetic success. */
/* Return zero on success, an IOReturn/errno-compatible nonzero error otherwise. */
int32_t rk3_usb_audio_start(rk3_usb_session *session, uint32_t timeout_ms);
int32_t rk3_usb_audio_stop(rk3_usb_session *session, uint32_t timeout_ms);
size_t rk3_usb_render(rk3_usb_session *session, const float *interleaved,
                      size_t frames);
size_t rk3_usb_capture(rk3_usb_session *session, float *interleaved,
                       size_t frames);

#endif
