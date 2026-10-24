/*
 * usb_iokit.c — IOKit/IOUSBLib transport for the RigKontrol 3.
 * Adapted from mihaimetal/rk3-coreaudio at
 * d0679238ad06842f382ef89b995823e978caec9b; see LICENSE and usb_iokit.h.
 * Local changes: control-only open, explicit interface 0, configuration reuse,
 * native startup errors, owned-handle cleanup and validated bounded bulk I/O.
 */

#include "usb_iokit.h"

#include <limits.h>
#include <string.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>

#define STREAMING_ALT 1
#define CONTROLS_ALT 0

/* Startup diagnostics are written only before the owner starts I/O threads. */
static int open_error(struct rk3_usb *u, int32_t error, const char *operation)
{
	u->last_error = error;
	u->last_operation = operation;
	return error;
}

static int native_open_error(struct rk3_usb *u, IOReturn error,
			     const char *operation)
{
	return open_error(u, (int32_t)error, operation);
}

/* ---- device lookup -------------------------------------------------- */

static int find_device(struct rk3_usb *u, io_service_t *service)
{
	*service = IO_OBJECT_NULL;
	CFMutableDictionaryRef matching = IOServiceMatching(kIOUSBDeviceClassName);
	if (!matching)
		return native_open_error(u, kIOReturnNoMemory, "IOServiceMatching");

	SInt32 vid = RK3_VID, pid = RK3_PID;
	CFNumberRef vid_ref = CFNumberCreate(NULL, kCFNumberSInt32Type, &vid);
	CFNumberRef pid_ref = CFNumberCreate(NULL, kCFNumberSInt32Type, &pid);
	if (!vid_ref || !pid_ref) {
		if (vid_ref) CFRelease(vid_ref);
		if (pid_ref) CFRelease(pid_ref);
		CFRelease(matching);
		return native_open_error(u, kIOReturnNoMemory, "CFNumberCreate");
	}
	CFDictionarySetValue(matching, CFSTR(kUSBVendorID), vid_ref);
	CFDictionarySetValue(matching, CFSTR(kUSBProductID), pid_ref);
	CFRelease(vid_ref);
	CFRelease(pid_ref);

	io_iterator_t iterator = IO_OBJECT_NULL;
	kern_return_t result = IOServiceGetMatchingServices(
		kIOMainPortDefault, matching, &iterator); /* consumes matching */
	if (result != KERN_SUCCESS)
		return open_error(u, (int32_t)result, "IOServiceGetMatchingServices");
	*service = IOIteratorNext(iterator);
	IOObjectRelease(iterator);
	if (!*service)
		return open_error(u, RK3_ENODEVICE, "find RigKontrol3 device");
	return 0;
}

static int open_device(struct rk3_usb *u, io_service_t service)
{
	IOCFPlugInInterface **plugin = NULL;
	SInt32 score = 0;
	IOReturn result = IOCreatePlugInInterfaceForService(service,
		kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &plugin, &score);
	if (result != kIOReturnSuccess || !plugin)
		return native_open_error(u, result == kIOReturnSuccess ? kIOReturnError : result,
			"IOCreatePlugInInterfaceForService(device)");

	HRESULT queried = (*plugin)->QueryInterface(plugin,
		CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID300), (LPVOID *)&u->dev);
	IODestroyPlugInInterface(plugin);
	if (queried != S_OK || !u->dev)
		return open_error(u, queried == S_OK ? (int32_t)kIOReturnError : (int32_t)queried,
			"QueryInterface(device300)");
	return 0;
}

/* ---- interface + pipe resolution ------------------------------------ */

static int open_interface_zero(struct rk3_usb *u)
{
	IOUSBFindInterfaceRequest request = {
		.bInterfaceClass = kIOUSBFindInterfaceDontCare,
		.bInterfaceSubClass = kIOUSBFindInterfaceDontCare,
		.bInterfaceProtocol = kIOUSBFindInterfaceDontCare,
		.bAlternateSetting = kIOUSBFindInterfaceDontCare,
	};
	io_iterator_t iterator = IO_OBJECT_NULL;
	IOReturn result = (*u->dev)->CreateInterfaceIterator(u->dev, &request, &iterator);
	if (result != kIOReturnSuccess)
		return native_open_error(u, result, "CreateInterfaceIterator");

	int32_t failure = (int32_t)kIOReturnNotFound;
	const char *operation = "find interface 0";
	io_service_t service;
	while ((service = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
		IOCFPlugInInterface **plugin = NULL;
		IOUSBInterfaceInterface300 **candidate = NULL;
		SInt32 score = 0;
		result = IOCreatePlugInInterfaceForService(service,
			kIOUSBInterfaceUserClientTypeID, kIOCFPlugInInterfaceID, &plugin, &score);
		IOObjectRelease(service);
		if (result != kIOReturnSuccess || !plugin) {
			failure = (int32_t)(result == kIOReturnSuccess ? kIOReturnError : result);
			operation = "IOCreatePlugInInterfaceForService(interface)";
			continue;
		}
		HRESULT queried = (*plugin)->QueryInterface(plugin,
			CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID300), (LPVOID *)&candidate);
		IODestroyPlugInInterface(plugin);
		if (queried != S_OK || !candidate) {
			if (candidate) (*candidate)->Release(candidate);
			failure = queried == S_OK ? (int32_t)kIOReturnError : (int32_t)queried;
			operation = "QueryInterface(interface300)";
			continue;
		}

		UInt8 number = 0xff;
		result = (*candidate)->GetInterfaceNumber(candidate, &number);
		if (result == kIOReturnSuccess && number == 0) {
			u->intf = candidate;
			IOObjectRelease(iterator);
			return 0;
		}
		(*candidate)->Release(candidate);
		if (result != kIOReturnSuccess) {
			failure = (int32_t)result;
			operation = "GetInterfaceNumber";
		}
	}
	IOObjectRelease(iterator);
	return open_error(u, failure, operation);
}

static int resolve_pipes(struct rk3_usb *u, int require_audio)
{
	UInt8 count = 0;
	IOReturn result = (*u->intf)->GetNumEndpoints(u->intf, &count);
	if (result != kIOReturnSuccess)
		return native_open_error(u, result, "GetNumEndpoints");

	for (unsigned index = 1; index <= count; index++) {
		UInt8 direction = 0, number = 0, type = 0, interval = 0;
		UInt16 max_packet = 0;
		UInt8 pipe = (UInt8)index;
		result = (*u->intf)->GetPipeProperties(u->intf, pipe, &direction,
			&number, &type, &max_packet, &interval);
		if (result != kIOReturnSuccess)
			return native_open_error(u, result, "GetPipeProperties");
		if (type == kUSBBulk && direction == kUSBOut && number == 1)
			u->pipe_cmd_out = pipe;
		else if (type == kUSBBulk && direction == kUSBIn && number == 1)
			u->pipe_cmd_in = pipe;
		else if (type == kUSBIsoc && direction == kUSBIn && number == 2) {
			u->pipe_iso_in = pipe;
			u->iso_in_maxpacket = max_packet;
		} else if (type == kUSBIsoc && direction == kUSBOut && number == 6) {
			u->pipe_iso_out = pipe;
			u->iso_out_maxpacket = max_packet;
		}
	}
	if (!u->pipe_cmd_out || !u->pipe_cmd_in)
		return native_open_error(u, kIOReturnNotFound, "resolve command pipes 0x01/0x81");
	if (require_audio && (!u->pipe_iso_in || !u->pipe_iso_out))
		return native_open_error(u, kIOReturnNotFound, "resolve audio pipes 0x82/0x06");
	return 0;
}

/* ---- public API ----------------------------------------------------- */

static int open_at_alternate(struct rk3_usb *u, UInt8 alternate, int require_audio)
{
	if (!u) return RK3_EINVAL;
	memset(u, 0, sizeof(*u));
	io_service_t service = IO_OBJECT_NULL;
	int result = find_device(u, &service);
	if (result != 0) return result;
	result = open_device(u, service);
	IOObjectRelease(service);
	if (result != 0) goto fail;

	IOReturn status = (*u->dev)->USBDeviceOpen(u->dev);
	if (status != kIOReturnSuccess) {
		result = native_open_error(u, status, "USBDeviceOpen");
		goto fail;
	}
	u->device_opened = 1;

	IOUSBConfigurationDescriptorPtr configuration = NULL;
	status = (*u->dev)->GetConfigurationDescriptorPtr(u->dev, 0, &configuration);
	if (status != kIOReturnSuccess || !configuration) {
		result = native_open_error(u, status == kIOReturnSuccess ? kIOReturnError : status,
			"GetConfigurationDescriptorPtr");
		goto fail;
	}
	UInt8 current_configuration = 0;
	status = (*u->dev)->GetConfiguration(u->dev, &current_configuration);
	if (status != kIOReturnSuccess) {
		result = native_open_error(u, status, "GetConfiguration");
		goto fail;
	}
	if (current_configuration != configuration->bConfigurationValue) {
		status = (*u->dev)->SetConfiguration(u->dev, configuration->bConfigurationValue);
		if (status != kIOReturnSuccess) {
			result = native_open_error(u, status, "SetConfiguration");
			goto fail;
		}
	}

	result = open_interface_zero(u);
	if (result != 0) goto fail;
	status = (*u->intf)->USBInterfaceOpen(u->intf);
	if (status != kIOReturnSuccess) {
		result = native_open_error(u, status, "USBInterfaceOpen(interface 0)");
		goto fail;
	}
	u->interface_opened = 1;
	status = (*u->intf)->SetAlternateInterface(u->intf, alternate);
	if (status != kIOReturnSuccess) {
		result = native_open_error(u, status, alternate == CONTROLS_ALT
			? "SetAlternateInterface(0)" : "SetAlternateInterface(1)");
		goto fail;
	}
	result = resolve_pipes(u, require_audio);
	if (result != 0) goto fail;
	u->last_operation = alternate == CONTROLS_ALT ? "controller USB open" : "streaming USB open";
	return 0;

fail:
	rk3_usb_close(u);
	return result;
}

int rk3_usb_open(struct rk3_usb *u)
{
	return open_at_alternate(u, STREAMING_ALT, 1);
}

int rk3_usb_open_controls(struct rk3_usb *u)
{
	return open_at_alternate(u, CONTROLS_ALT, 0);
}

void rk3_usb_close(struct rk3_usb *u)
{
	if (!u) return;
	if (u->intf) {
		if (u->interface_opened) (*u->intf)->USBInterfaceClose(u->intf);
		(*u->intf)->Release(u->intf);
		u->intf = NULL;
	}
	u->interface_opened = 0;
	if (u->dev) {
		if (u->device_opened) (*u->dev)->USBDeviceClose(u->dev);
		(*u->dev)->Release(u->dev);
		u->dev = NULL;
	}
	u->device_opened = 0;
	u->pipe_cmd_in = u->pipe_cmd_out = u->pipe_iso_in = u->pipe_iso_out = 0;
	u->iso_in_maxpacket = u->iso_out_maxpacket = 0;
}

void rk3_clear_stall(struct rk3_usb *u, uint8_t pipe)
{
	if (u && u->intf && u->interface_opened && pipe)
		(*u->intf)->ClearPipeStallBothEnds(u->intf, pipe);
}

static int bulk_status(IOReturn status)
{
	if (status == kIOReturnNoDevice || status == kIOReturnNotAttached)
		return RK3_ENODEVICE;
	return (int32_t)status;
}

int rk3_bulk_write(struct rk3_usb *u, const void *buf, size_t len,
		   unsigned timeout_ms)
{
	if (!u || !buf || len == 0 || len > UINT32_MAX || timeout_ms == 0)
		return RK3_EINVAL;
	if (!u->intf || !u->interface_opened || !u->pipe_cmd_out)
		return (int32_t)kIOReturnNotOpen;
	IOReturn status = (*u->intf)->WritePipeTO(u->intf, u->pipe_cmd_out,
		(void *)buf, (UInt32)len, timeout_ms, timeout_ms);
	if (status == kIOUSBPipeStalled) {
		/* Preserve upstream's single stall-clear/retry, without configuration
		 * resets. The independent input reader must remain active throughout. */
		rk3_clear_stall(u, u->pipe_cmd_out);
		status = (*u->intf)->WritePipeTO(u->intf, u->pipe_cmd_out,
			(void *)buf, (UInt32)len, timeout_ms, timeout_ms);
		if (status == kIOUSBPipeStalled) rk3_clear_stall(u, u->pipe_cmd_out);
	}
	return bulk_status(status);
}

int rk3_bulk_read(struct rk3_usb *u, void *buf, size_t *len,
		  unsigned timeout_ms)
{
	if (!len) return RK3_EINVAL;
	size_t capacity = *len;
	*len = 0;
	if (!u || !buf || capacity == 0 || capacity > UINT32_MAX || timeout_ms == 0)
		return RK3_EINVAL;
	if (!u->intf || !u->interface_opened || !u->pipe_cmd_in)
		return (int32_t)kIOReturnNotOpen;
	UInt32 size = (UInt32)capacity;
	IOReturn status = (*u->intf)->ReadPipeTO(u->intf, u->pipe_cmd_in,
		buf, &size, timeout_ms, timeout_ms);
	if (status == kIOReturnTimeout || status == kIOUSBTransactionTimeout)
		return RK3_ETIMEOUT;
	if (status == kIOUSBPipeStalled) rk3_clear_stall(u, u->pipe_cmd_in);
	if (status != kIOReturnSuccess) return bulk_status(status);
	if (size > capacity) return (int32_t)kIOReturnOverrun;
	*len = size;
	return 0;
}
