/*
 * usb_iokit.h — IOKit/IOUSBLib transport for the RigKontrol 3.
 *
 * Adapted from mihaimetal/rk3-coreaudio, commit
 * d0679238ad06842f382ef89b995823e978caec9b (GPL-2.0; see LICENSE).
 * Local changes add control-only alt 0, explicit interface selection, native
 * error reporting, bounded argument validation, and ownership-aware cleanup.
 *
 * Opens the device (VID 0x17cc / PID 0x1940), changes its configuration only
 * when necessary, and explicitly selects interface #0. The control-only API
 * uses alternate setting 0 and requires only the two command pipes. The
 * original streaming API uses alternate setting 1 and requires all four:
 *
 *   cmd_out  -> EP 0x01 OUT bulk   (commands / MIDI out)
 *   cmd_in   -> EP 0x81 IN  bulk   (replies / MIDI in)
 *   iso_in   -> EP 0x82 IN  isoc   (audio capture)
 *   iso_out  -> EP 0x06 OUT isoc   (audio playback)
 *
 * Pipe references are discovered by matching (endpoint number, direction,
 * transfer type) rather than hard-coded indices, so the layer is robust
 * to descriptor ordering changes.
 *
 * Bulk helpers are synchronous. The owner must maintain a separate continuous
 * input reader during writes and must join that reader before closing USB.
 * This file does not start audio, publish MIDI, or issue protocol commands.
 */

#ifndef RK3_USB_IOKIT_H
#define RK3_USB_IOKIT_H

#include <IOKit/usb/IOUSBLib.h>
#include <stddef.h>
#include <stdint.h>

#define RK3_VID 0x17cc
#define RK3_PID 0x1940

struct rk3_usb {
	IOUSBDeviceInterface300    **dev;
	IOUSBInterfaceInterface300 **intf;

	/* IOKit pipe reference numbers (1-based) for the streaming alt. */
	uint8_t pipe_cmd_out;   /* EP 0x01 OUT bulk */
	uint8_t pipe_cmd_in;    /* EP 0x81 IN  bulk */
	uint8_t pipe_iso_in;    /* EP 0x82 IN  isoc */
	uint8_t pipe_iso_out;   /* EP 0x06 OUT isoc */

	uint16_t iso_in_maxpacket;
	uint16_t iso_out_maxpacket;

	uint8_t device_opened;
	uint8_t interface_opened;

	/* Open diagnostics, stable throughout the active session. Bulk helpers
	 * return their own statuses and never mutate these shared fields, so a
	 * reader and writer can use the interface concurrently without a race. */
	int32_t last_error;
	const char *last_operation;
};

/*
 * Find, open, configure the RK3 and open interface #0 at alt 1 (audio pipes
 * required). Pass a fresh or previously closed struct; open initializes it.
 * Returns 0 on success, RK3_ENODEVICE if absent, or a signed native status.
 * No diagnostics are printed to stderr.
 */
int  rk3_usb_open(struct rk3_usb *u);

/* Control-only variant: interface #0 alt 0, only bulk EP1 IN/OUT required. */
int  rk3_usb_open_controls(struct rk3_usb *u);

/* Close only successfully opened handles, then release their interfaces.
 * Safe on a zeroed/closed struct. Caller must stop and join all I/O first.
 * Open diagnostics are preserved, including on failure-path cleanup. */
void rk3_usb_close(struct rk3_usb *u);

/* Local sentinel errors cannot be confused with native IOKit error values. */
#define RK3_ETIMEOUT (-2)
#define RK3_ENODEVICE (-3)
#define RK3_EINVAL (-4)

/* Synchronous bounded bulk write to EP1 OUT. Returns 0 on full success,
 * RK3_EINVAL for invalid arguments, RK3_ENODEVICE if detached, or a signed
 * native IOReturn otherwise. Buffer/length must be nonzero, length <=
 * UINT32_MAX, timeout_ms > 0. Caller owns the buffer for the entire call. */
int  rk3_bulk_write(struct rk3_usb *u, const void *buf, size_t len,
		    unsigned timeout_ms);

/*
 * Synchronous bulk read from EP1 IN. *len is in/out: pass buffer capacity,
 * receives actual bytes read, or zero on every error/timeout. Returns 0 on
 * success, RK3_ETIMEOUT on an ordinary idle timeout, RK3_ENODEVICE if detached,
 * RK3_EINVAL for invalid arguments, or a signed native IOReturn otherwise.
 * Capacity and timeout must be nonzero; capacity <= UINT32_MAX. No partial
 * bytes are published on failure. Caller owns the buffer for the entire call.
 */
int  rk3_bulk_read(struct rk3_usb *u, void *buf, size_t *len,
		   unsigned timeout_ms);

/* Clear a halted (stalled) bulk pipe on both ends. Pass a pipe ref from
 * struct rk3_usb (e.g. u->pipe_cmd_out). No-op on a null pipe/interface. */
void rk3_clear_stall(struct rk3_usb *u, uint8_t pipe);

#endif /* RK3_USB_IOKIT_H */
