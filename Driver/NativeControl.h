#ifndef RK3_NATIVE_CONTROL_H
#define RK3_NATIVE_CONTROL_H
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
#define RK3_CONTROL_STATIC_ASSERT static_assert
#else
#define RK3_CONTROL_STATIC_ASSERT _Static_assert
#endif

/* Private cross-process transport between the two native driver bundles.
 * Fixed-width values only. Host byte order (both bundles are ARM64).
 * The structs below are wrapped in CFDataRef objects for CoreAudio transport;
 * see NativeClient.h. Access these selectors on the audio DEVICE object, global scope/main element.
 * No former app, virtual MIDI bridge, socket or MainStage dependency. */
enum {
    RK3_CONTROL_VERSION = 1,
    RK3_PROPERTY_INPUT_GAIN = 0x6e676169, /* ngai; raw Float32 software input gain */
    RK3_PROPERTY_DIRECT_MONITOR = 0x6e646d6f, /* ndmo; original raw-byte direct monitor */
    RK3_PROPERTY_USB_IDENTITY = 0x6e757362, /* nusb; physical USB metadata snapshot */
    RK3_PROPERTY_SPEC = 0x6e737063,     /* nspc; cached raw14 specification */
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

/* Device-level operation, independent of audio-engine running state.
 * SET sends the low byte of mode once; GET returns a byte, including NI's
 * ambiguous 0xff timeout value. Neither SET success nor GET implies a named
 * physical routing mode. There is no startup/reconnect persistence or replay. */
typedef struct { uint32_t version,reserved; uint64_t generation; } RK3NativeMonitorQuery;
typedef struct { uint32_t version,mode; uint64_t generation; } RK3NativeMonitorValue;
RK3_CONTROL_STATIC_ASSERT(sizeof(RK3NativeMonitorQuery)==16,"Monitor query ABI");
RK3_CONTROL_STATIC_ASSERT(offsetof(RK3NativeMonitorQuery,version)==0,"Monitor query version ABI");
RK3_CONTROL_STATIC_ASSERT(offsetof(RK3NativeMonitorQuery,reserved)==4,"Monitor query reserved ABI");
RK3_CONTROL_STATIC_ASSERT(offsetof(RK3NativeMonitorQuery,generation)==8,"Monitor query generation ABI");
RK3_CONTROL_STATIC_ASSERT(sizeof(RK3NativeMonitorValue)==16,"Monitor value ABI");
RK3_CONTROL_STATIC_ASSERT(offsetof(RK3NativeMonitorValue,version)==0,"Monitor value version ABI");
RK3_CONTROL_STATIC_ASSERT(offsetof(RK3NativeMonitorValue,mode)==4,"Monitor mode ABI");
RK3_CONTROL_STATIC_ASSERT(offsetof(RK3NativeMonitorValue,generation)==8,"Monitor value generation ABI");

/* Zero-based original channel; all Float32 bit patterns are valid values.
 * This is a native functional property, not an NI user-client selector ABI. */
typedef struct { uint32_t version,channel; uint64_t generation; } RK3NativeGainQuery;
typedef struct {
    uint32_t version,channel;
    uint64_t generation;
    uint32_t value_bits,reserved;
} RK3NativeGainValue;
RK3_CONTROL_STATIC_ASSERT(sizeof(RK3NativeGainQuery)==16,"Gain query ABI");
RK3_CONTROL_STATIC_ASSERT(sizeof(RK3NativeGainValue)==24,"Gain value ABI");
RK3_CONTROL_STATIC_ASSERT(offsetof(RK3NativeGainValue,generation)==8,"Gain generation ABI");
RK3_CONTROL_STATIC_ASSERT(offsetof(RK3NativeGainValue,value_bits)==16,"Gain bits ABI");

/* Native functional cache publication; status is errno, not NI IOReturn. */
typedef struct { uint32_t version,reserved; uint64_t generation; } RK3NativeSpecQuery;
typedef struct {
    uint32_t version;
    int32_t status;
    uint64_t generation;
    uint8_t raw[14],reserved[2];
} RK3NativeSpec;
RK3_CONTROL_STATIC_ASSERT(sizeof(RK3NativeSpecQuery)==16,"Spec query ABI");
RK3_CONTROL_STATIC_ASSERT(offsetof(RK3NativeSpecQuery,version)==0,"Spec query version ABI");
RK3_CONTROL_STATIC_ASSERT(offsetof(RK3NativeSpecQuery,reserved)==4,"Spec query reserved ABI");
RK3_CONTROL_STATIC_ASSERT(offsetof(RK3NativeSpecQuery,generation)==8,"Spec query generation ABI");
RK3_CONTROL_STATIC_ASSERT(sizeof(RK3NativeSpec)==32,"Spec reply ABI");
RK3_CONTROL_STATIC_ASSERT(offsetof(RK3NativeSpec,version)==0,"Spec version ABI");
RK3_CONTROL_STATIC_ASSERT(offsetof(RK3NativeSpec,status)==4,"Spec status ABI");
RK3_CONTROL_STATIC_ASSERT(offsetof(RK3NativeSpec,generation)==8,"Spec generation ABI");
RK3_CONTROL_STATIC_ASSERT(offsetof(RK3NativeSpec,raw)==16,"Spec raw ABI");
RK3_CONTROL_STATIC_ASSERT(offsetof(RK3NativeSpec,reserved)==30,"Spec reserved ABI");

/* Whole-operation status indices; storage order does not reorder USB calls. */
enum {
    RK3_USB_IDENTITY_VENDOR_ID = 0,
    RK3_USB_IDENTITY_PRODUCT_ID,
    RK3_USB_IDENTITY_PRODUCT_INDEX,
    RK3_USB_IDENTITY_MANUFACTURER_INDEX,
    RK3_USB_IDENTITY_SERIAL_INDEX,
    RK3_USB_IDENTITY_PRODUCT_READER,
    RK3_USB_IDENTITY_MANUFACTURER_READER,
    RK3_USB_IDENTITY_SERIAL_READER,
    RK3_USB_IDENTITY_STEP_COUNT,
    RK3_USB_IDENTITY_STRING_BYTES = 31,
    RK3_USB_IDENTITY_GETTER_MASK = 0x1f,
    RK3_USB_IDENTITY_ALL_MASK = 0xff
};
/* Native errno describes snapshot availability; raw step statuses are IOReturn.
 * A status0 record may have undefined fields. Only defined bytes are data. */
typedef struct { uint32_t version,reserved; uint64_t generation; } RK3NativeUSBIdentityQuery;
typedef struct {
    uint32_t version;
    int32_t status;
    uint64_t generation;
    uint32_t attempted_mask,defined_mask;
    uint16_t vendor_id,product_id;
    uint8_t product_index,manufacturer_index,serial_index,reserved0;
    int32_t step_status[RK3_USB_IDENTITY_STEP_COUNT];
    uint8_t product[RK3_USB_IDENTITY_STRING_BYTES];
    uint8_t manufacturer[RK3_USB_IDENTITY_STRING_BYTES];
    uint8_t serial[RK3_USB_IDENTITY_STRING_BYTES];
    uint8_t reserved[3];
} RK3NativeUSBIdentity;
RK3_CONTROL_STATIC_ASSERT(sizeof(RK3NativeUSBIdentityQuery)==16,"USB identity query ABI");
RK3_CONTROL_STATIC_ASSERT(offsetof(RK3NativeUSBIdentityQuery,version)==0,"USB identity query version ABI");
RK3_CONTROL_STATIC_ASSERT(offsetof(RK3NativeUSBIdentityQuery,reserved)==4,"USB identity query reserved ABI");
RK3_CONTROL_STATIC_ASSERT(offsetof(RK3NativeUSBIdentityQuery,generation)==8,"USB identity query generation ABI");
RK3_CONTROL_STATIC_ASSERT(sizeof(RK3NativeUSBIdentity)==160,"USB identity reply ABI");
RK3_CONTROL_STATIC_ASSERT(offsetof(RK3NativeUSBIdentity,version)==0,"USB identity version ABI");
RK3_CONTROL_STATIC_ASSERT(offsetof(RK3NativeUSBIdentity,status)==4,"USB identity status ABI");
RK3_CONTROL_STATIC_ASSERT(offsetof(RK3NativeUSBIdentity,generation)==8,"USB identity generation ABI");
RK3_CONTROL_STATIC_ASSERT(offsetof(RK3NativeUSBIdentity,attempted_mask)==16,"USB identity attempted ABI");
RK3_CONTROL_STATIC_ASSERT(offsetof(RK3NativeUSBIdentity,defined_mask)==20,"USB identity defined ABI");
RK3_CONTROL_STATIC_ASSERT(offsetof(RK3NativeUSBIdentity,vendor_id)==24,"USB identity vendor ABI");
RK3_CONTROL_STATIC_ASSERT(offsetof(RK3NativeUSBIdentity,product_id)==26,"USB identity product ABI");
RK3_CONTROL_STATIC_ASSERT(offsetof(RK3NativeUSBIdentity,product_index)==28,"USB identity product index ABI");
RK3_CONTROL_STATIC_ASSERT(offsetof(RK3NativeUSBIdentity,manufacturer_index)==29,"USB identity manufacturer index ABI");
RK3_CONTROL_STATIC_ASSERT(offsetof(RK3NativeUSBIdentity,serial_index)==30,"USB identity serial index ABI");
RK3_CONTROL_STATIC_ASSERT(offsetof(RK3NativeUSBIdentity,reserved0)==31,"USB identity reserved byte ABI");
RK3_CONTROL_STATIC_ASSERT(offsetof(RK3NativeUSBIdentity,step_status)==32,"USB identity operation status ABI");
RK3_CONTROL_STATIC_ASSERT(offsetof(RK3NativeUSBIdentity,product)==64,"USB identity product bytes ABI");
RK3_CONTROL_STATIC_ASSERT(offsetof(RK3NativeUSBIdentity,manufacturer)==95,"USB identity manufacturer bytes ABI");
RK3_CONTROL_STATIC_ASSERT(offsetof(RK3NativeUSBIdentity,serial)==126,"USB identity serial bytes ABI");
RK3_CONTROL_STATIC_ASSERT(offsetof(RK3NativeUSBIdentity,reserved)==157,"USB identity reserved ABI");

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
 * COMMAND accepts NI command7 MIDI output, the exact display-preserving
 * nine-byte command5 LED update, and the fixed33-byte type2 command0f pedal
 * configuration (three-bit ring/switch masks; zero padding), validated by the owner;
 * hardware write completion is synchronous/bounded and failures propagate.
 * COMMAND requires a CommandLease qualifier; the worker validates generation
 * immediately before the write, so stale queued data cannot cross reconnects. */
RK3_CONTROL_STATIC_ASSERT(sizeof(RK3NativeInfo)==240,"Native info ABI");
RK3_CONTROL_STATIC_ASSERT(sizeof(RK3NativeReport)==88,"Native report ABI");
RK3_CONTROL_STATIC_ASSERT(sizeof(RK3NativeReportQuery)==16,"Native report query ABI");
RK3_CONTROL_STATIC_ASSERT(sizeof(RK3NativeReports)==2848,"Native report batch ABI");
RK3_CONTROL_STATIC_ASSERT(sizeof(RK3NativeCommandLease)==16,"Native command lease ABI");
RK3_CONTROL_STATIC_ASSERT(sizeof(RK3NativeCommand)==72,"Native command ABI");
#undef RK3_CONTROL_STATIC_ASSERT
#endif
