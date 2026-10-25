#ifndef RK3_NATIVE_CONTROL_H
#define RK3_NATIVE_CONTROL_H
#include <stdint.h>
#include <stddef.h>

/* Private cross-process transport between the two native driver bundles.
 * Fixed-width values only. Host byte order (both bundles are ARM64).
 * The structs below are wrapped in CFDataRef objects for CoreAudio transport;
 * see NativeClient.h. Access these selectors on the audio DEVICE object, global scope/main element.
 * No former app, virtual MIDI bridge, socket or MainStage dependency. */
enum {
    RK3_CONTROL_VERSION = 1,
    RK3_PROPERTY_INPUT_GAIN = 0x6e676169, /* ngai; raw Float32 software input gain */
    RK3_PROPERTY_INFO = 0x6e696e66,     /* ninf; get RK3NativeInfo */
    RK3_PROPERTY_REPORTS = 0x6e727074,  /* nrpt; get RK3NativeReports */
    RK3_PROPERTY_COMMAND = 0x6e636d64,  /* ncmd; set RK3NativeCommand */
    RK3_NATIVE_REPORT_LIMIT = 32,
    RK3_NATIVE_CONNECTED = 1,
    RK3_NATIVE_AUDIO_RUNNING = 2,
    RK3_NATIVE_FAILED = 4
};
#define RK3_CONTROLLER_IDENTITY_KEY "org.rigkontrol3.ni-arm.midi.identity"
#define RK3_CONTROLLER_CALIBRATION_KEY "org.rigkontrol3.ni-arm.calibration"
#define RK3_NATIVE_DEVICE_UID "org.rigkontrol3.ni-arm.usb.17cc1940"

/* Zero-based original channel; all Float32 bit patterns are valid values.
 * This is a native functional property, not an NI user-client selector ABI. */
typedef struct { uint32_t version,channel; uint64_t generation; } RK3NativeGainQuery;
typedef struct {
    uint32_t version,channel;
    uint64_t generation;
    uint32_t value_bits,reserved;
} RK3NativeGainValue;
_Static_assert(sizeof(RK3NativeGainQuery)==16,"Gain query ABI");
_Static_assert(sizeof(RK3NativeGainValue)==24,"Gain value ABI");
_Static_assert(offsetof(RK3NativeGainValue,generation)==8,"Gain generation ABI");
_Static_assert(offsetof(RK3NativeGainValue,value_bits)==16,"Gain bits ABI");

typedef struct {
    uint32_t version;
    uint32_t flags;
    uint64_t generation;
    int32_t last_error;
    uint32_t sample_rate;
    uint32_t audio_inputs;
    uint32_t audio_outputs;
    uint32_t midi_inputs;
    uint32_t midi_outputs;
    uint32_t wire_mode;
    uint32_t digital_snapshot; /* bit31 valid; low9 bits current raw switches */
    char device_uid[128];
    char product_name[64];
} RK3NativeInfo;

typedef struct {
    uint64_t sequence;
    uint64_t host_time;
    uint32_t length;
    uint32_t reserved;
    uint8_t bytes[64];
} RK3NativeReport;

typedef struct {
    uint32_t version;
    uint32_t reserved;
    uint64_t after_sequence;
} RK3NativeReportQuery;

typedef struct {
    uint32_t version;
    uint32_t count;
    uint64_t next_sequence;
    uint64_t latest_sequence;
    uint64_t dropped;
    RK3NativeReport reports[RK3_NATIVE_REPORT_LIMIT];
} RK3NativeReports;

typedef struct {
    uint32_t version;
    uint32_t reserved;
    uint64_t generation;
} RK3NativeCommandLease;

typedef struct {
    uint32_t version;
    uint32_t length;
    uint8_t bytes[64];
} RK3NativeCommand;

/* REPORTS requires a CFData-wrapped exact Query as property qualifier. Start after_sequence
 * at zero on discovery/generation change. Set it to next_sequence after each
 * successful read; latest_sequence may exceed next_sequence when more wait.
 * dropped reports a history overrun, requiring per-port parser reset.
 * The history is non-destructive so independent clients have separate cursors.
 * COMMAND accepts NI command7 MIDI output and the exact display-preserving
 * nine-byte command5 LED update, validated by the owner;
 * hardware write completion is synchronous/bounded and failures propagate.
 * COMMAND requires a CommandLease qualifier; the worker validates generation
 * immediately before the write, so stale queued data cannot cross reconnects. */
_Static_assert(sizeof(RK3NativeInfo)==240,"Native info ABI");
_Static_assert(sizeof(RK3NativeReport)==88,"Native report ABI");
_Static_assert(sizeof(RK3NativeReportQuery)==16,"Native report query ABI");
_Static_assert(sizeof(RK3NativeReports)==2848,"Native report batch ABI");
_Static_assert(sizeof(RK3NativeCommandLease)==16,"Native command lease ABI");
_Static_assert(sizeof(RK3NativeCommand)==72,"Native command ABI");
#endif
