/* Fresh diagnostic transport derived from Apple's SDK and NI binary evidence.
 * See ReverseEngineering/DeviceProbe.md. No audio/control mapping is started. */
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/usb/USBSpec.h>
#include <mach/mach_error.h>
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../Driver/DeviceProtocol.h"

enum { RK3_VENDOR = 0x17cc, RK3_PRODUCT = 0x1940, REPLY_SIZE = 64,
       QUERY_TIMEOUT_MS = 3000, WRITE_TIMEOUT_MS = 200, DRAIN_TIMEOUT_MS = 2000,
       MAX_DEVICES = 64 };

static volatile sig_atomic_t interrupted;

typedef struct {
    IOUSBInterfaceInterface300 **interface;
    CFRunLoopSourceRef source;
    uint8_t buffer[REPLY_SIZE];
    UInt8 input_pipe;
    UInt8 output_pipe;
    UInt8 original_alt;
    bool opened;
    bool changed_alt;
    bool pending;
    bool stopping;
    bool received_info;
    bool failed;
    double deadline;
} Query;

static void signal_handler(int number) { (void)number; interrupted = 1; }

static double monotonic_seconds(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        perror("clock_gettime");
        exit(2);
    }
    return (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
}

static void io_error(const char *operation, IOReturn result) {
    fprintf(stderr, "%s: 0x%08x (%s)\n", operation, (unsigned int)result,
            mach_error_string(result));
}

static void print_bytes(const uint8_t *bytes, size_t length) {
    for (size_t index = 0; index < length; ++index)
        printf("%s%02x", index == 0 ? "" : " ", bytes[index]);
    putchar('\n');
}

static uint16_t little16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static IOUSBDeviceInterface300 **device_interface(io_service_t service) {
    IOCFPlugInInterface **plugin = NULL;
    IOUSBDeviceInterface300 **device = NULL;
    SInt32 score = 0;
    IOReturn result = IOCreatePlugInInterfaceForService(service,
        kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &plugin, &score);
    if (result != kIOReturnSuccess || plugin == NULL) {
        io_error("Create USB device plug-in", result);
        return NULL;
    }
    HRESULT status = (*plugin)->QueryInterface(plugin,
        CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID300), (LPVOID *)&device);
    (*plugin)->Release(plugin);
    if (status != S_OK || device == NULL) {
        fprintf(stderr, "Query USB device interface300: 0x%08x\n", (unsigned int)status);
        return NULL;
    }
    return device;
}

/* GetConfigurationDescriptorPtr returns cached USB-order descriptor bytes and
 * does not require an exclusive device open (Apple IOUSBLib.h). */
static bool print_descriptors(IOUSBDeviceInterface300 **device) {
    UInt16 vendor = 0, product = 0, release = 0;
    UInt32 location = 0;
    UInt8 count = 0, current = 0;
    if ((*device)->GetDeviceVendor(device, &vendor) != kIOReturnSuccess ||
        (*device)->GetDeviceProduct(device, &product) != kIOReturnSuccess ||
        vendor != RK3_VENDOR || product != RK3_PRODUCT)
        return false;
    (void)(*device)->GetDeviceReleaseNumber(device, &release);
    (void)(*device)->GetLocationID(device, &location);
    printf("RK3 vendor=%04x product=%04x release=%04x location=0x%08x\n",
           vendor, product, release, (unsigned int)location);
    IOReturn result = (*device)->GetConfiguration(device, &current);
    if (result != kIOReturnSuccess) { io_error("GetConfiguration", result); return false; }
    result = (*device)->GetNumberOfConfigurations(device, &count);
    if (result != kIOReturnSuccess) { io_error("GetNumberOfConfigurations", result); return false; }
    printf("Current configuration=%u; configuration count=%u\n", current, count);
    for (unsigned int index = 0; index < count; ++index) {
        IOUSBConfigurationDescriptorPtr descriptor = NULL;
        result = (*device)->GetConfigurationDescriptorPtr(device, (UInt8)index, &descriptor);
        if (result != kIOReturnSuccess || descriptor == NULL) {
            io_error("GetConfigurationDescriptorPtr", result); return false;
        }
        const uint8_t *bytes = (const uint8_t *)descriptor;
        size_t total = little16(bytes + 2);
        if (bytes[0] < 9 || total < 9) {
            fprintf(stderr, "Malformed configuration header\n"); return false;
        }
        printf("Configuration index=%u value=%u total=%zu interfaces=%u attributes=0x%02x maxPower=%u\n",
               index, bytes[5], total, bytes[4], bytes[7], bytes[8]);
        for (size_t offset = 0; offset < total;) {
            if (total - offset < 2 || bytes[offset] < 2 || bytes[offset] > total - offset) {
                fprintf(stderr, "Malformed descriptor at offset %zu\n", offset); return false;
            }
            const uint8_t *entry = bytes + offset;
            if (entry[1] == kUSBInterfaceDesc && entry[0] >= 9)
                printf("  Interface=%u alt=%u endpoints=%u class=%02x subclass=%02x protocol=%02x\n",
                       entry[2], entry[3], entry[4], entry[5], entry[6], entry[7]);
            else if (entry[1] == kUSBEndpointDesc && entry[0] >= 7)
                printf("    Endpoint=0x%02x type=%u maxPacketRaw=0x%04x interval=%u\n",
                       entry[2], entry[3] & 3, little16(entry + 4), entry[6]);
            else
                printf("  Descriptor type=%u length=%u offset=%zu\n", entry[1], entry[0], offset);
            offset += entry[0];
        }
    }
    return true;
}

static bool configuration_has_alt1(IOUSBDeviceInterface300 **device, UInt8 configuration) {
    UInt8 count = 0;
    if (configuration == 0 || (*device)->GetNumberOfConfigurations(device, &count) != kIOReturnSuccess)
        return false;
    for (unsigned int index = 0; index < count; ++index) {
        IOUSBConfigurationDescriptorPtr descriptor = NULL;
        if ((*device)->GetConfigurationDescriptorPtr(device, (UInt8)index, &descriptor) != kIOReturnSuccess || descriptor == NULL)
            return false;
        const uint8_t *bytes = (const uint8_t *)descriptor;
        size_t total = little16(bytes + 2);
        if (bytes[0] < 9 || total < 9) return false;
        if (bytes[5] != configuration) continue;
        for (size_t offset = 0; offset < total;) {
            if (total - offset < 2 || bytes[offset] < 2 || bytes[offset] > total - offset) return false;
            const uint8_t *entry = bytes + offset;
            if (entry[1] == kUSBInterfaceDesc && entry[0] >= 9 && entry[2] == 0 && entry[3] == 1)
                return true;
            offset += entry[0];
        }
    }
    return false;
}

static bool prepare_configuration(IOUSBDeviceInterface300 **device, bool configure) {
    UInt8 current = 0;
    IOReturn result = (*device)->GetConfiguration(device, &current);
    if (result != kIOReturnSuccess) { io_error("GetConfiguration before query", result); return false; }
    if (current != 0) return true;
    if (!configure) {
        fprintf(stderr, "Device is unconfigured. Explicit --query-device --configure is required to select configuration1.\n");
        return false;
    }
    if (!configuration_has_alt1(device, 1)) {
        fprintf(stderr, "Configuration1 / interface0 / alternate1 was not advertised; refusing configuration change.\n");
        return false;
    }
    result = (*device)->USBDeviceOpen(device);
    if (result != kIOReturnSuccess) { io_error("Open device for configuration (no seizure)", result); return false; }
    result = (*device)->SetConfiguration(device, 1);
    IOReturn closed = (*device)->USBDeviceClose(device);
    if (result != kIOReturnSuccess) io_error("Select advertised configuration1", result);
    if (closed != kIOReturnSuccess) io_error("Close device after configuration", closed);
    if (result != kIOReturnSuccess || closed != kIOReturnSuccess) return false;
    current = 0;
    result = (*device)->GetConfiguration(device, &current);
    if (result != kIOReturnSuccess || current != 1) {
        fprintf(stderr, "Configuration1 selection could not be verified\n"); return false;
    }
    printf("Selected advertised configuration1; device closed before opening interface0. Configuration1 is retained.\n");
    return true;
}

static IOUSBInterfaceInterface300 **find_interface_zero(IOUSBDeviceInterface300 **device) {
    IOUSBFindInterfaceRequest request = { kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare,
                                       kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare };
    io_iterator_t iterator = IO_OBJECT_NULL;
    IOReturn result = (*device)->CreateInterfaceIterator(device, &request, &iterator);
    if (result != kIOReturnSuccess) { io_error("CreateInterfaceIterator", result); return NULL; }
    IOUSBInterfaceInterface300 **selected = NULL;
    io_service_t service;
    while ((service = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
        IOCFPlugInInterface **plugin = NULL;
        IOUSBInterfaceInterface300 **interface = NULL;
        SInt32 score = 0;
        result = IOCreatePlugInInterfaceForService(service, kIOUSBInterfaceUserClientTypeID,
                                                  kIOCFPlugInInterfaceID, &plugin, &score);
        IOObjectRelease(service);
        if (result != kIOReturnSuccess || plugin == NULL) continue;
        HRESULT status = (*plugin)->QueryInterface(plugin,
            CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID300), (LPVOID *)&interface);
        (*plugin)->Release(plugin);
        if (status != S_OK || interface == NULL) continue;
        UInt8 number = 255;
        result = (*interface)->GetInterfaceNumber(interface, &number);
        if (result == kIOReturnSuccess && number == 0 && selected == NULL) selected = interface;
        else (*interface)->Release(interface);
    }
    IOObjectRelease(iterator);
    return selected;
}

static bool arm_read(Query *query);

static void read_complete(void *context, IOReturn result, void *argument) {
    Query *query = context;
    query->pending = false;
    if (query->stopping) return;
    if (result != kIOReturnSuccess) {
        io_error("EP1-IN completion", result); query->failed = true; return;
    }
    uintptr_t length = (uintptr_t)argument;
    if (length > sizeof(query->buffer)) {
        fprintf(stderr, "Invalid completion length: %" PRIuPTR "\n", length);
        query->failed = true; return;
    }
    printf("EP1-IN received %" PRIuPTR " bytes: ", length);
    print_bytes(query->buffer, (size_t)length);
    if (length != 0 && query->buffer[0] == 1) {
        rk3_device_info info;
        if (!rk3_device_info_parse(query->buffer, (size_t)length, &info)) {
            fprintf(stderr, "Malformed GET_DEVICE_INFO reply (minimum 15 bytes)\n");
            query->failed = true; return;
        }
        printf("GET_DEVICE_INFO validated: audio inputs=%u outputs=%u wire_mode=%u\n",
               info.input_channels, info.output_channels, info.wire_mode);
        printf("Device-reported MIDI inputs=%u outputs=%u (roles not inferred)\n",
               info.midi_inputs, info.midi_outputs);
        printf("Recovered 14-byte device-info payload: "); print_bytes(info.raw, sizeof(info.raw));
        query->received_info = true;
        return;
    }
    if (!interrupted && monotonic_seconds() < query->deadline && !arm_read(query)) query->failed = true;
}

static bool arm_read(Query *query) {
    if (query->pending || query->stopping) return false;
    query->pending = true;
    IOReturn result = (*query->interface)->ReadPipeAsyncTO(query->interface, query->input_pipe,
        query->buffer, sizeof(query->buffer), QUERY_TIMEOUT_MS, QUERY_TIMEOUT_MS,
        read_complete, query);
    if (result != kIOReturnSuccess) {
        query->pending = false; io_error("Arm EP1-IN read", result); return false;
    }
    return true;
}

static void drain_until(Query *query, double deadline) {
    while (query->pending && monotonic_seconds() < deadline)
        (void)CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.02, true);
}

static bool cleanup_query(Query *query) {
    bool clean = true;
    query->stopping = true;
    if (query->pending) {
        IOReturn result = (*query->interface)->AbortPipe(query->interface, query->input_pipe);
        if (result != kIOReturnSuccess) io_error("Abort EP1-IN", result);
        drain_until(query, monotonic_seconds() + (double)DRAIN_TIMEOUT_MS / 1000.0);
    }
    if (query->pending) {
        /* Never release a pending callback's context, buffer or interface. Closing
         * can produce the final cancellation; give that completion one bounded drain. */
        IOReturn closed = (*query->interface)->USBInterfaceClose(query->interface);
        if (closed == kIOReturnSuccess) query->opened = false;
        else { io_error("Close interface during cancellation", closed); clean = false; }
        drain_until(query, monotonic_seconds() + (double)DRAIN_TIMEOUT_MS / 1000.0);
        if (query->pending) {
            fprintf(stderr, "Cancellation did not complete; retaining callback state until process teardown.\n");
            fflush(NULL);
            _Exit(2);
        }
        if (!query->opened && query->changed_alt) {
            /* The callback is now quiescent. Reopen without seizure only to
             * restore the previous alternate; never reopen while pending. */
            IOReturn reopened = (*query->interface)->USBInterfaceOpen(query->interface);
            if (reopened == kIOReturnSuccess) query->opened = true;
            else { io_error("Reopen interface to restore alternate", reopened); clean = false; }
        }
    }
    if (query->opened && query->changed_alt) {
        IOReturn result = (*query->interface)->SetAlternateInterface(query->interface, query->original_alt);
        if (result != kIOReturnSuccess) { io_error("Restore original alternate", result); clean = false; }
    }
    if (query->opened) {
        IOReturn result = (*query->interface)->USBInterfaceClose(query->interface);
        if (result != kIOReturnSuccess) { io_error("Close USB interface", result); clean = false; }
    }
    if (query->source != NULL) {
        CFRunLoopRemoveSource(CFRunLoopGetCurrent(), query->source, kCFRunLoopDefaultMode);
        CFRelease(query->source);
    }
    if (query->interface != NULL) (*query->interface)->Release(query->interface);
    free(query);
    return clean;
}

static bool query_device(IOUSBDeviceInterface300 **device, bool keep_current_alt, bool configure) {
    if (!prepare_configuration(device, configure)) return false;
    UInt8 current = 0;
    if ((*device)->GetConfiguration(device, &current) != kIOReturnSuccess) return false;
    if (!keep_current_alt && !configuration_has_alt1(device, current)) {
        fprintf(stderr, "Configured interface0 alt1 was not advertised; refusing to change configuration.\n");
        return false;
    }
    Query *query = calloc(1, sizeof(*query));
    if (query == NULL) { perror("calloc"); return false; }
    query->interface = find_interface_zero(device);
    bool successful = false;
    if (query->interface == NULL) { fprintf(stderr, "Interface0 unavailable\n"); goto done; }
    IOReturn result = (*query->interface)->GetAlternateSetting(query->interface, &query->original_alt);
    if (result != kIOReturnSuccess) { io_error("GetAlternateSetting", result); goto done; }
    printf("Interface0 observed alternate=%u\n", query->original_alt);
    result = (*query->interface)->USBInterfaceOpen(query->interface);
    if (result != kIOReturnSuccess) { io_error("Open interface0 (no seizure)", result); goto done; }
    query->opened = true;
    if (!keep_current_alt && query->original_alt != 1) {
        result = (*query->interface)->SetAlternateInterface(query->interface, 1);
        if (result != kIOReturnSuccess) { io_error("Select interface0 alternate1", result); goto done; }
        query->changed_alt = true;
    }
    UInt8 endpoints = 0;
    result = (*query->interface)->GetNumEndpoints(query->interface, &endpoints);
    if (result != kIOReturnSuccess) { io_error("GetNumEndpoints", result); goto done; }
    for (unsigned int pipe = 1; pipe <= endpoints; ++pipe) {
        UInt8 direction = 0, number = 0, type = 0, interval = 0;
        UInt16 maximum = 0;
        result = (*query->interface)->GetPipeProperties(query->interface, (UInt8)pipe,
            &direction, &number, &type, &maximum, &interval);
        if (result != kIOReturnSuccess) { io_error("GetPipeProperties", result); goto done; }
        printf("Pipe ref=%u endpoint=%u direction=%s type=%u maxPacket=%u interval=%u\n",
               pipe, number, direction == kUSBIn ? "IN" : "OUT", type, maximum, interval);
        if (number == 1 && type == kUSBBulk) {
            if (direction == kUSBIn) query->input_pipe = (UInt8)pipe;
            if (direction == kUSBOut) query->output_pipe = (UInt8)pipe;
        }
    }
    if (query->input_pipe == 0 || query->output_pipe == 0) {
        fprintf(stderr, "Required bulk endpoint1 IN/OUT pair not present; no command sent.\n"); goto done;
    }
    result = (*query->interface)->CreateInterfaceAsyncEventSource(query->interface, &query->source);
    if (result != kIOReturnSuccess || query->source == NULL) {
        io_error("CreateInterfaceAsyncEventSource", result); goto done;
    }
    CFRunLoopAddSource(CFRunLoopGetCurrent(), query->source, kCFRunLoopDefaultMode);
    query->deadline = monotonic_seconds() + (double)QUERY_TIMEOUT_MS / 1000.0;
    if (!arm_read(query)) goto done;
    /* NI initHardware arms its 64-byte bulk reader before sendCommand(1).
     * sendCommand emits precisely one command byte with 200/200ms timeouts. */
    uint8_t command = 1;
    printf("EP1-IN armed; sending GET_DEVICE_INFO byte 01 only\n");
    result = (*query->interface)->WritePipeTO(query->interface, query->output_pipe,
        &command, sizeof(command), WRITE_TIMEOUT_MS, WRITE_TIMEOUT_MS);
    if (result != kIOReturnSuccess) { io_error("GET_DEVICE_INFO write", result); goto done; }
    while (!query->received_info && !query->failed && !interrupted && monotonic_seconds() < query->deadline)
        (void)CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.02, true);
    successful = query->received_info && !query->failed && !interrupted;
    if (!successful && !query->failed)
        fprintf(stderr, "%s\n", interrupted ? "Interrupted" : "GET_DEVICE_INFO reply deadline reached");
done:
    return cleanup_query(query) && successful;
}

static void usage(const char *program) {
    printf("Usage: %s [--descriptors | --query-device] [--configure] [--current-alt] [--location HEX]\n"
           "Default: cached descriptors only; no exclusive open or command.\n"
           "--query-device: open interface0 and send only command1 with a persistent reader.\n"
           "--configure: with --query-device, select advertised configuration1 only if currently unconfigured.\n"
           "--current-alt: query the observed alternate instead of selecting advertised alt1.\n", program);
}

int main(int argc, char **argv) {
    bool query = false, descriptors = false, configure = false, current_alt = false, have_location = false;
    UInt32 wanted_location = 0;
    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--descriptors") == 0) { descriptors = true; continue; }
        if (strcmp(argv[index], "--query-device") == 0) { query = true; continue; }
        if (strcmp(argv[index], "--configure") == 0) { configure = true; continue; }
        if (strcmp(argv[index], "--current-alt") == 0) { current_alt = true; continue; }
        if (strcmp(argv[index], "--help") == 0) { usage(argv[0]); return 0; }
        if (strcmp(argv[index], "--location") == 0 && index + 1 < argc) {
            char *end = NULL;
            errno = 0;
            unsigned long value = strtoul(argv[++index], &end, 16);
            if (errno != 0 || end == argv[index] || *end != '\0' || value > UINT32_MAX) {
                fprintf(stderr, "Invalid location\n"); return 2;
            }
            wanted_location = (UInt32)value; have_location = true; continue;
        }
        usage(argv[0]); return 2;
    }
    if (descriptors && query) { fprintf(stderr, "--descriptors and --query-device are mutually exclusive\n"); return 2; }
    if (configure && !query) { fprintf(stderr, "--configure requires --query-device\n"); return 2; }
    if (current_alt && !query) { fprintf(stderr, "--current-alt requires --query-device\n"); return 2; }
    (void)signal(SIGINT, signal_handler);
    (void)signal(SIGTERM, signal_handler);
    uint64_t seen[MAX_DEVICES];
    size_t seen_count = 0, selected_count = 0;
    IOUSBDeviceInterface300 **selected = NULL;
    bool failed = false;
    /* Modern Apple Silicon host services plus the SDK's legacy compatibility
     * class; registry IDs prevent duplicate queries if both match a service. */
    const char *classes[] = { "IOUSBHostDevice", kIOUSBDeviceClassName };
    for (size_t class_index = 0; class_index < sizeof(classes) / sizeof(classes[0]); ++class_index) {
        CFMutableDictionaryRef matching = IOServiceMatching(classes[class_index]);
        if (matching == NULL) { failed = true; break; }
        int32_t vendor = RK3_VENDOR, product = RK3_PRODUCT;
        CFNumberRef vendor_value = CFNumberCreate(NULL, kCFNumberSInt32Type, &vendor);
        CFNumberRef product_value = CFNumberCreate(NULL, kCFNumberSInt32Type, &product);
        if (vendor_value == NULL || product_value == NULL) {
            if (vendor_value != NULL) CFRelease(vendor_value);
            if (product_value != NULL) CFRelease(product_value);
            CFRelease(matching); failed = true; break;
        }
        CFDictionarySetValue(matching, CFSTR(kUSBVendorID), vendor_value);
        CFDictionarySetValue(matching, CFSTR(kUSBProductID), product_value);
        CFRelease(vendor_value); CFRelease(product_value);
        io_iterator_t iterator = IO_OBJECT_NULL;
        IOReturn result = IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iterator);
        if (result != kIOReturnSuccess) { io_error("Match RK3 USB services", result); failed = true; continue; }
        io_service_t service;
        while ((service = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
            uint64_t entry_id = 0;
            result = IORegistryEntryGetRegistryEntryID(service, &entry_id);
            bool duplicate = false;
            for (size_t index = 0; index < seen_count; ++index)
                if (seen[index] == entry_id) duplicate = true;
            if (result != kIOReturnSuccess || duplicate) { IOObjectRelease(service); continue; }
            if (seen_count == MAX_DEVICES) { IOObjectRelease(service); failed = true; break; }
            seen[seen_count++] = entry_id;
            IOUSBDeviceInterface300 **device = device_interface(service);
            IOObjectRelease(service);
            if (device == NULL) { failed = true; continue; }
            UInt32 location = 0;
            result = (*device)->GetLocationID(device, &location);
            if (have_location && (result != kIOReturnSuccess || location != wanted_location)) {
                (*device)->Release(device); continue;
            }
            if (!print_descriptors(device)) { (*device)->Release(device); failed = true; continue; }
            ++selected_count;
            if (selected == NULL) selected = device;
            else (*device)->Release(device);
        }
        IOObjectRelease(iterator);
    }
    if (selected_count == 0) { fprintf(stderr, "No accessible matching RK3 device\n"); failed = true; }
    if (query) {
        if (selected_count != 1 || failed || interrupted) {
            fprintf(stderr, "Query refused: require one unambiguous device and successful descriptor inventory.\n");
            failed = true;
        } else if (!query_device(selected, current_alt, configure)) failed = true;
    }
    if (selected != NULL) (*selected)->Release(selected);
    return failed || interrupted ? 1 : 0;
}
