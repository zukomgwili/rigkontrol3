/* Hardware-free fault injection. External MIDI topology, worker attachment,
 * audio transport and publication are replaced before including production.
 * Start/Stop execute unchanged bodies; their timers remain real CPU objects. */
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/MIDIDriver.h>
#include <CoreMIDI/MIDISetup.h>
#include <CoreMIDI/MIDIServices.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

static OSStatus mock_get_size(AudioObjectID,const AudioObjectPropertyAddress *,UInt32,const void *,UInt32 *);
static Boolean mock_has_property(AudioObjectID,const AudioObjectPropertyAddress *);
static OSStatus mock_set_midi_integer(MIDIObjectRef,CFStringRef,SInt32);
static OSStatus mock_get(AudioObjectID,const AudioObjectPropertyAddress *,UInt32,const void *,UInt32 *,void *);
static OSStatus mock_set(AudioObjectID,const AudioObjectPropertyAddress *,UInt32,const void *,UInt32,const void *);
static OSStatus mock_received(MIDIEndpointRef,const MIDIPacketList *);
static OSStatus mock_midi_get_data(MIDIObjectRef,CFStringRef,CFDataRef *);
static OSStatus mock_midi_set_data(MIDIObjectRef,CFStringRef,CFDataRef);
static CFRunLoopRef mock_io_loop(void);
static ItemCount mock_device_count(MIDIDeviceListRef);
static MIDIDeviceRef mock_list_device(MIDIDeviceListRef,ItemCount);
static OSStatus mock_get_string(MIDIObjectRef,CFStringRef,CFStringRef *);
static ItemCount mock_entity_count(MIDIDeviceRef);
static MIDIEntityRef mock_device_entity(MIDIDeviceRef,ItemCount);
static ItemCount mock_source_count(MIDIEntityRef);
static ItemCount mock_destination_count(MIDIEntityRef);
static MIDIEndpointRef mock_entity_source(MIDIEntityRef,ItemCount);
static MIDIEndpointRef mock_entity_destination(MIDIEntityRef,ItemCount);
static OSStatus mock_refcons(MIDIEndpointRef,void *,void *);
static MIDIDeviceListRef mock_driver_list(MIDIDriverRef);
static OSStatus mock_dispose_list(MIDIDeviceListRef);
static int mock_worker_create(pthread_t *,const pthread_attr_t *,void *(*)(void *),void *);
static int mock_worker_join(pthread_t,void **);
static void mock_attach_timer(CFRunLoopRef,CFRunLoopTimerRef,CFRunLoopMode);
#define RK3_MIDI_TESTING 1
#define AudioObjectGetPropertyDataSize mock_get_size
#define AudioObjectHasProperty mock_has_property
#define MIDIObjectSetIntegerProperty mock_set_midi_integer
#define AudioObjectGetPropertyData mock_get
#define AudioObjectSetPropertyData mock_set
#define MIDIReceived mock_received
#define MIDIObjectGetDataProperty mock_midi_get_data
#define MIDIObjectSetDataProperty mock_midi_set_data
#define MIDIGetDriverIORunLoop mock_io_loop
#define MIDIDeviceListGetNumberOfDevices mock_device_count
#define MIDIDeviceListGetDevice mock_list_device
#define MIDIObjectGetStringProperty mock_get_string
#define MIDIDeviceGetNumberOfEntities mock_entity_count
#define MIDIDeviceGetEntity mock_device_entity
#define MIDIEntityGetNumberOfSources mock_source_count
#define MIDIEntityGetNumberOfDestinations mock_destination_count
#define MIDIEntityGetSource mock_entity_source
#define MIDIEntityGetDestination mock_entity_destination
#define MIDIEndpointSetRefCons mock_refcons
#define MIDIGetDriverDeviceList mock_driver_list
#define MIDIDeviceListDispose mock_dispose_list
#define pthread_create mock_worker_create
#define pthread_join mock_worker_join
#define CFRunLoopAddTimer mock_attach_timer
#include "RK3MIDIDriver.c"
#undef AudioObjectGetPropertyDataSize
#undef AudioObjectHasProperty
#undef MIDIObjectSetIntegerProperty
#undef AudioObjectGetPropertyData
#undef AudioObjectSetPropertyData
#undef MIDIReceived
#undef MIDIObjectGetDataProperty
#undef MIDIObjectSetDataProperty
#undef MIDIGetDriverIORunLoop
#undef MIDIDeviceListGetNumberOfDevices
#undef MIDIDeviceListGetDevice
#undef MIDIObjectGetStringProperty
#undef MIDIDeviceGetNumberOfEntities
#undef MIDIDeviceGetEntity
#undef MIDIEntityGetNumberOfSources
#undef MIDIEntityGetNumberOfDestinations
#undef MIDIEntityGetSource
#undef MIDIEntityGetDestination
#undef MIDIEndpointSetRefCons
#undef MIDIGetDriverDeviceList
#undef MIDIDeviceListDispose
#undef pthread_create
#undef pthread_join
#undef CFRunLoopAddTimer

static bool restart_before_producer;
static bool toggle_listener_before_delivery;
void rk3_test_before_producer(void *context) {
    Driver *driver = context;
    if (restart_before_producer) {
        atomic_fetch_add(&driver->lifecycle_epoch,1);
        atomic_store(&driver->output_read,0);
        atomic_store(&driver->output_write,0);
        restart_before_producer = false;
    }
}

void rk3_test_before_delivery(void *context) {
    Driver *driver = context;
    if (toggle_listener_before_delivery) {
        toggle_listener_before_delivery = false;
        assert(enable_source(driver_ref(driver),driver->source,false) == noErr);
        assert(enable_source(driver_ref(driver),driver->source,true) == noErr);
    }
}

static RK3NativeReports input_batch;
static uint64_t info_generation = 7;
static uint32_t digital_snapshot=UINT32_C(0x80000000);
static bool press_release_before_snapshot;
static CFDataRef calibration_data;
static MIDIEndpointRef delivered_source;
static uint64_t expected_after;
static uint8_t delivered[64];
static size_t delivered_count, deliveries;
static uint64_t delivered_time;
static unsigned controller_cc_counts[128];
static uint8_t controller_cc_last[128];
static MIDIEndpointRef controller_cc_source[128];
static Driver *working_driver;
static size_t writes;
static OSStatus write_result;
static size_t writes_to_stop=1,failures_remaining;
static uint8_t attempted_notes[16];
static bool provider_present=true;
static uint32_t provider_flags=RK3_NATIVE_CONNECTED;
static unsigned offline_updates;
static SInt32 last_offline;
enum { CF_FAULT_NONE, CF_FAULT_TYPE, CF_FAULT_LENGTH, CF_FAULT_SIZE, CF_FAULT_NULL, CF_FAULT_STATUS };
static unsigned cf_fault;
static AudioObjectPropertySelector cf_fault_selector;
static unsigned info_queries,report_queries;

enum { TEST_DEVICE_LIST=1000, TEST_DIN_INPUT=10, TEST_DIN_OUTPUT=11, TEST_CONTROLS=12 };
static bool topology_din=true,topology_controller=true,topology_controller_source=true;
static bool topology_no_driver_list;
static MIDIEndpointRef topology_din_source=123,topology_controller_source_ref=124;
typedef struct { MIDIEndpointRef endpoint; void *first,*second; } RefCons;
static RefCons topology_refcons[16];
static size_t topology_refcons_count,timer_attachments,worker_creates,worker_joins;
static Driver *mock_worker_owner;

static OSStatus return_wire(AudioObjectPropertySelector selector,UInt32 *size,void *data,const void *wire,size_t length) {
    assert(*size==sizeof(CFPropertyListRef));
    unsigned fault=selector==cf_fault_selector?cf_fault:CF_FAULT_NONE;
    if(fault==CF_FAULT_STATUS)return kAudioHardwareUnspecifiedError;
    CFPropertyListRef property=NULL;
    if(fault==CF_FAULT_TYPE) property=CFRetain(CFSTR("wrong property type"));
    else if(fault!=CF_FAULT_NULL) property=CFDataCreate(NULL,wire,(CFIndex)(length-(fault==CF_FAULT_LENGTH?1u:0u)));
    assert(fault==CF_FAULT_NULL||property!=NULL);
    memcpy(data,&property,sizeof(property));
    *size=fault==CF_FAULT_SIZE?0:(UInt32)sizeof(property);
    return noErr;
}

static void read_cf_wire(UInt32 size,const void *data,void *wire,size_t length) {
    assert(size==sizeof(CFPropertyListRef)&&data!=NULL);
    CFPropertyListRef property=NULL;memcpy(&property,data,sizeof(property));
    assert(property!=NULL&&CFGetTypeID(property)==CFDataGetTypeID());
    assert(CFDataGetLength((CFDataRef)property)==(CFIndex)length);
    memcpy(wire,CFDataGetBytePtr((CFDataRef)property),length);
}

static OSStatus mock_get_size(AudioObjectID object,const AudioObjectPropertyAddress *property,UInt32 qualifier_size,const void *qualifier,UInt32 *size) {
    (void)qualifier;assert(object==kAudioObjectSystemObject&&property->mSelector==kAudioHardwarePropertyDevices&&qualifier_size==0);
    *size=provider_present?sizeof(AudioDeviceID):0;return noErr;
}
static Boolean mock_has_property(AudioObjectID object,const AudioObjectPropertyAddress *property) {
    return object==42&&property->mSelector==RK3_PROPERTY_INFO;
}
static OSStatus mock_set_midi_integer(MIDIObjectRef object,CFStringRef property,SInt32 value) {
    assert(CFRunLoopGetCurrent()==CFRunLoopGetMain());
    assert((object==333||object==334)&&property==kMIDIPropertyOffline);
    ++offline_updates;last_offline=value;return noErr;
}
static OSStatus mock_get(AudioObjectID object,const AudioObjectPropertyAddress *property,
                         UInt32 qualifier_size,const void *qualifier,UInt32 *size,void *data) {
    if(object==kAudioObjectSystemObject) {
        assert(property->mSelector==kAudioHardwarePropertyDevices&&qualifier_size==0&&*size>=sizeof(AudioDeviceID));
        const AudioDeviceID device=42;memcpy(data,&device,sizeof(device));*size=sizeof(device);return noErr;
    }
    assert(object == 42);
    if (property->mSelector == RK3_PROPERTY_INFO) {
        ++info_queries;
        if(press_release_before_snapshot) {
            press_release_before_snapshot=false;
            input_batch=(RK3NativeReports){.version=1,.count=2,.next_sequence=2,.latest_sequence=2};
            input_batch.reports[0]=(RK3NativeReport){.sequence=1,.length=3,.bytes={4,1,0}};
            input_batch.reports[1]=(RK3NativeReport){.sequence=2,.length=3,.bytes={4,0,0}};
        }
        assert(qualifier_size == 0);
        RK3NativeInfo info = {.version=RK3_CONTROL_VERSION,.flags=provider_flags,
            .generation=info_generation,.midi_inputs=1,.midi_outputs=1,.digital_snapshot=digital_snapshot};
        strcpy(info.device_uid,RK3_NATIVE_DEVICE_UID);
        strcpy(info.product_name,"Rig Kontrol 3 Native");
        return return_wire(property->mSelector,size,data,&info,sizeof(info));
    }
    assert(property->mSelector == RK3_PROPERTY_REPORTS);
    ++report_queries;
    RK3NativeReportQuery query;read_cf_wire(qualifier_size,qualifier,&query,sizeof(query));
    assert(query.version == RK3_CONTROL_VERSION && (query.after_sequence == expected_after || query.after_sequence == 0));
    return return_wire(property->mSelector,size,data,&input_batch,sizeof(input_batch));
}

static OSStatus mock_set(AudioObjectID object,const AudioObjectPropertyAddress *property,
                         UInt32 qualifier_size,const void *qualifier,UInt32 size,const void *data) {
    assert(object == 42 && property->mSelector == RK3_PROPERTY_COMMAND);
    RK3NativeCommandLease lease;RK3NativeCommand command;
    read_cf_wire(qualifier_size,qualifier,&lease,sizeof(lease));
    read_cf_wire(size,data,&command,sizeof(command));
    assert(lease.version == RK3_CONTROL_VERSION && lease.generation == 7);
    assert(command.version == RK3_CONTROL_VERSION && command.length == 6);
    assert(command.bytes[0] == 7 && command.bytes[1] == 0 && command.bytes[2] == 3);
    if(writes<sizeof(attempted_notes))attempted_notes[writes]=command.bytes[4];
    ++writes;
    if(writes>=writes_to_stop)atomic_store(&working_driver->worker_stop,true);
    if(failures_remaining){--failures_remaining;return kAudioHardwareUnspecifiedError;}
    return write_result;
}

static OSStatus mock_received(MIDIEndpointRef source,const MIDIPacketList *list) {
    assert((source == 123 || source == 124 || source==topology_din_source ||
            source==topology_controller_source_ref) && list->numPackets == 1);
    delivered_source=source;
    assert(list->packet[0].length <= sizeof(delivered));
    delivered_count = list->packet[0].length;
    delivered_time = list->packet[0].timeStamp;
    memcpy(delivered,list->packet[0].data,delivered_count);
    ++deliveries;
    if(delivered_count==3&&delivered[0]==0xb0&&delivered[1]<128) {
        ++controller_cc_counts[delivered[1]];
        controller_cc_last[delivered[1]]=delivered[2];controller_cc_source[delivered[1]]=source;
    }
    return noErr;
}

static OSStatus mock_midi_get_data(MIDIObjectRef object,CFStringRef key,CFDataRef *data) {
    (void)object;assert(CFEqual(key,CFSTR(RK3_CONTROLLER_CALIBRATION_KEY)));
    if(calibration_data==NULL)return kMIDIUnknownProperty;
    *data=CFRetain(calibration_data);return noErr;
}
static OSStatus mock_midi_set_data(MIDIObjectRef object,CFStringRef key,CFDataRef data) {
    (void)object;assert(CFEqual(key,CFSTR(RK3_CONTROLLER_CALIBRATION_KEY)));
    if(calibration_data)CFRelease(calibration_data);calibration_data=CFRetain(data);return noErr;
}

static CFRunLoopRef mock_io_loop(void) { return CFRunLoopGetCurrent(); }
static ItemCount mock_device_count(MIDIDeviceListRef list) {
    assert(list==TEST_DEVICE_LIST);return (ItemCount)topology_din+(ItemCount)topology_controller;
}
static MIDIDeviceRef mock_list_device(MIDIDeviceListRef list,ItemCount index) {
    assert(list==TEST_DEVICE_LIST&&index<mock_device_count(list));
    return topology_din&&index==0?333:334;
}
static OSStatus mock_get_string(MIDIObjectRef object,CFStringRef key,CFStringRef *value) {
    assert(CFEqual(key,CFSTR(RK3_CONTROLLER_IDENTITY_KEY))&&value!=NULL);
    if(object==333)*value=CFRetain(CFSTR(RK3_NATIVE_DEVICE_UID));
    else if(object==334)*value=CFRetain(controller_identity());
    else { *value=NULL;return kMIDIUnknownProperty; }
    return noErr;
}
static ItemCount mock_entity_count(MIDIDeviceRef device) {
    assert(device==333||device==334);return device==333?2:1;
}
static MIDIEntityRef mock_device_entity(MIDIDeviceRef device,ItemCount index) {
    assert(index<mock_entity_count(device));
    return device==333?(index==0?TEST_DIN_INPUT:TEST_DIN_OUTPUT):TEST_CONTROLS;
}
static ItemCount mock_source_count(MIDIEntityRef entity) {
    assert(entity==TEST_DIN_INPUT||entity==TEST_DIN_OUTPUT||entity==TEST_CONTROLS);
    return entity==TEST_DIN_INPUT||(entity==TEST_CONTROLS&&topology_controller_source);
}
static ItemCount mock_destination_count(MIDIEntityRef entity) {
    assert(entity==TEST_DIN_INPUT||entity==TEST_DIN_OUTPUT||entity==TEST_CONTROLS);
    return entity==TEST_DIN_OUTPUT||entity==TEST_CONTROLS;
}
static MIDIEndpointRef mock_entity_source(MIDIEntityRef entity,ItemCount index) {
    assert(index==0&&mock_source_count(entity)==1);
    return entity==TEST_DIN_INPUT?topology_din_source:topology_controller_source_ref;
}
static MIDIEndpointRef mock_entity_destination(MIDIEntityRef entity,ItemCount index) {
    assert(index==0&&mock_destination_count(entity)==1);return entity==TEST_DIN_OUTPUT?126:125;
}
static RefCons *endpoint_refcons(MIDIEndpointRef endpoint) {
    for(size_t i=0;i<topology_refcons_count;++i)
        if(topology_refcons[i].endpoint==endpoint)return &topology_refcons[i];
    assert(topology_refcons_count<sizeof(topology_refcons)/sizeof(topology_refcons[0]));
    RefCons *result=&topology_refcons[topology_refcons_count++];
    *result=(RefCons){.endpoint=endpoint};return result;
}
static OSStatus mock_refcons(MIDIEndpointRef endpoint,void *first,void *second) {
    RefCons *record=endpoint_refcons(endpoint);record->first=first;record->second=second;return noErr;
}
static MIDIDeviceListRef mock_driver_list(MIDIDriverRef driver) {
    assert(driver!=NULL);return topology_no_driver_list?0:TEST_DEVICE_LIST;
}
static OSStatus mock_dispose_list(MIDIDeviceListRef list) {
    assert(list==TEST_DEVICE_LIST);return noErr;
}
static int mock_worker_create(pthread_t *thread,const pthread_attr_t *attributes,
                              void *(*entry)(void *),void *context) {
    assert(thread&&attributes==NULL&&entry==output_worker&&context&&mock_worker_owner==NULL);
    mock_worker_owner=context;*thread=(pthread_t)(uintptr_t)1;++worker_creates;return 0;
}
static int mock_worker_join(pthread_t thread,void **result) {
    assert(thread==(pthread_t)(uintptr_t)1&&mock_worker_owner!=NULL);
    Driver *owner=mock_worker_owner;mock_worker_owner=NULL;
    if(result)*result=NULL;
    ++worker_joins;(void)release_ref(owner);return 0;
}
static void mock_attach_timer(CFRunLoopRef loop,CFRunLoopTimerRef timer,CFRunLoopMode mode) {
    assert(loop==CFRunLoopGetCurrent()&&timer&&CFEqual(mode,kCFRunLoopDefaultMode));
    ++timer_attachments; /* Keep real timers unattached: no automatic I/O callbacks. */
}

static Driver *make_driver(void) {
    Driver *driver = RK3MIDIDriverFactory(NULL,kMIDIDriverTypeID);
    assert(driver != NULL);
    driver->started = true;
    driver->source = 123;driver->controller_source=124;driver->controller_destination=125;
    driver->source_enabled = true;
    atomic_store(&driver->online,true);
    atomic_store(&driver->audio_device,42);
    atomic_store(&driver->generation,7);
    return driver;
}

static void free_driver(Driver *driver) {
    driver->started = false;
    driver->source = 0;
    atomic_store(&driver->online,false);
    assert(release_ref(driver) == 0);
}

static void test_com(void) {
    assert(RK3MIDIDriverFactory(NULL,IUnknownUUID) == NULL);
    MIDIDriverRef driver = RK3MIDIDriverFactory(NULL,kMIDIDriverTypeID);
    assert(driver != NULL);
    void *other = (void *)(uintptr_t)1;
    assert((*driver)->QueryInterface(driver,CFUUIDGetUUIDBytes(kMIDIDriverInterfaceID),&other) == E_NOINTERFACE);
    assert(other == NULL);
    assert((*driver)->QueryInterface(driver,CFUUIDGetUUIDBytes(kMIDIDriverInterface2ID),&other) == S_OK);
    assert(other == driver && (*driver)->AddRef(driver) == 3);
    assert((*driver)->Release(driver) == 2 && (*driver)->Release(driver) == 1);
    assert((*driver)->FindDevices(driver,0) == noErr);
    assert((*driver)->Configure(driver,0) == noErr);
    assert((*driver)->Flush(driver,0,NULL,NULL) == noErr);
    assert((*driver)->Stop(driver) == noErr);
    assert((*driver)->Release(driver) == 0);
}

static void test_packet_queue(void) {
    Driver *driver = make_driver();
    MIDIPacketList list;
    MIDIPacket *packet = MIDIPacketListInit(&list);
    const uint8_t first[] = {0xf0,1,2,3,4}, second[] = {5,6,7,8,0xf7};
    packet = MIDIPacketListAdd(&list,sizeof(list),packet,100,5,first);
    assert(packet != NULL);
    assert(MIDIPacketListAdd(&list,sizeof(list),packet,200,5,second) != NULL);
    assert(send_packets(driver_ref(driver),&list,NULL,driver) == noErr);
    assert(atomic_load(&driver->output_write) == 2);
    const uint8_t expected0[] = {7,0,8,0xf0,1,2,3,4,5,6,7};
    const uint8_t expected1[] = {7,0,2,8,0xf7};
    assert(driver->output[0].command.length == sizeof(expected0));
    assert(memcmp(driver->output[0].command.bytes,expected0,sizeof(expected0)) == 0);
    assert(driver->output[1].command.length == sizeof(expected1));
    assert(memcmp(driver->output[1].command.bytes,expected1,sizeof(expected1)) == 0);
    assert(driver->output[0].generation == 7 && driver->output[0].audio_device == 42);
    atomic_store(&driver->output_write,255);
    assert(send_packets(driver_ref(driver),&list,NULL,driver) == kMIDIMessageSendErr);
    assert(atomic_load(&driver->output_write) == 255); /* all-or-nothing backpressure */
    atomic_store(&driver->output_read,1);
    assert(send_packets(driver_ref(driver),&list,NULL,driver) == noErr);
    assert(atomic_load(&driver->output_write) == 257);
    assert(memcmp(driver->output[255].command.bytes,expected0,sizeof(expected0)) == 0);
    assert(memcmp(driver->output[0].command.bytes,expected1,sizeof(expected1)) == 0);
    atomic_flag_test_and_set(&driver->output_producer);
    assert(send_packets(driver_ref(driver),&list,NULL,driver) == kMIDIMessageSendErr);
    atomic_flag_clear(&driver->output_producer);
    assert(send_packets(driver_ref(driver),&list,(void *)(uintptr_t)2,driver) == kMIDIUnknownEndpoint);
    restart_before_producer = true;
    assert(send_packets(driver_ref(driver),&list,NULL,driver) == kMIDINoConnection);
    assert(atomic_load(&driver->output_write) == 0); /* old Send cannot enter a new lifecycle */
    atomic_store(&driver->online,false);
    assert(send_packets(driver_ref(driver),&list,NULL,driver) == kMIDINoConnection);
    free_driver(driver);
}

static RK3NativeReport midi_report(uint64_t sequence,const uint8_t *bytes,size_t size) {
    RK3NativeReport report = {.sequence=sequence,.host_time=123456,.length=(uint32_t)(3+size),
                             .bytes={6,0,(uint8_t)size}};
    memcpy(report.bytes+3,bytes,size);
    return report;
}

static void test_input_history(void) {
    Driver *driver = make_driver();
    const uint8_t note[] = {0x90,60,100};
    input_batch = (RK3NativeReports){.version=RK3_CONTROL_VERSION,.count=2,.next_sequence=2,.latest_sequence=2};
    input_batch.reports[0] = (RK3NativeReport){.sequence=1,.length=3,.bytes={3,1,2}};
    input_batch.reports[1] = midi_report(2,note,sizeof(note));
    expected_after = 0;
    deliveries = 0;
    input_tick(NULL,driver);
    assert(deliveries == 1 && delivered_count == 3 && delivered_time == 123456);
    assert(memcmp(delivered,note,3) == 0 && driver->cursor == 2);
    driver->source_enabled = false;
    assert(enable_source(driver_ref(driver),123,true) == noErr && !driver->baseline_pending);
    input_batch.next_sequence = 4; input_batch.latest_sequence = 99;
    input_batch.reports[0].sequence = 3; input_batch.reports[1].sequence = 4;
    expected_after = 2;
    input_tick(NULL,driver);
    assert(deliveries == 2 && driver->cursor == 4 && !driver->baseline_pending);
    input_batch.count = 1; input_batch.next_sequence = 100; input_batch.latest_sequence = 100;
    input_batch.reports[0] = midi_report(100,note,sizeof(note));
    expected_after = 4;
    input_tick(NULL,driver);
    assert(deliveries == 3 && driver->cursor == 100);
    /* An overrun discards orphan running-status data, then resumes at a status. */
    const uint8_t after_loss[] = {60,0xf8,0x80,60,0};
    input_batch.dropped = 10; input_batch.next_sequence = 101; input_batch.latest_sequence = 101;
    input_batch.reports[0] = midi_report(101,after_loss,sizeof(after_loss));
    expected_after = 100;
    input_tick(NULL,driver);
    assert(deliveries == 5 && delivered_count == 3 && delivered[0] == 0x80);
    info_generation = 8;
    input_tick(NULL,driver);
    assert(deliveries == 5); /* do not read a new transport generation with an old cursor */
    info_generation = 7;
    input_batch.dropped = 0; input_batch.reports[0].length = 65;
    expected_after = 101; input_batch.next_sequence = input_batch.latest_sequence = 102;
    input_batch.reports[0].sequence = 102;
    input_tick(NULL,driver);
    assert(!atomic_load(&driver->online) && deliveries == 5);
    free_driver(driver);
}

static void test_complete_message_boundaries(void) {
    Driver *driver = make_driver();
    const uint8_t a[] = {0x90,60}, b[] = {100,61}, c[] = {110};
    input_batch = (RK3NativeReports){.version=RK3_CONTROL_VERSION,.count=3,.next_sequence=3,.latest_sequence=3};
    input_batch.reports[0] = midi_report(1,a,sizeof(a));
    input_batch.reports[1] = midi_report(2,b,sizeof(b));
    input_batch.reports[2] = midi_report(3,c,sizeof(c));
    expected_after = 0; deliveries = 0;
    input_tick(NULL,driver);
    const uint8_t expected[] = {0x90,61,110};
    assert(deliveries == 2 && delivered_count == 3 && memcmp(delivered,expected,3) == 0);
    free_driver(driver);
}

static void test_listener_epoch(void) {
    Driver *driver = make_driver();
    const uint8_t note[] = {0x90,60,100};
    input_batch = (RK3NativeReports){.version=RK3_CONTROL_VERSION,.count=1,.next_sequence=1,.latest_sequence=1};
    input_batch.reports[0] = midi_report(1,note,sizeof(note));
    expected_after = 0; deliveries = 0;
    toggle_listener_before_delivery = true;
    input_tick(NULL,driver);
    assert(deliveries == 0 && !driver->baseline_pending);
    assert(atomic_load(&driver->listener_epoch) == 2);
    expected_after = 1; input_batch.next_sequence = input_batch.latest_sequence = 2;
    input_batch.reports[0].sequence = 2;
    input_tick(NULL,driver);
    assert(deliveries == 1 && !driver->baseline_pending && driver->cursor == 2);
    expected_after = 2; input_batch.next_sequence = input_batch.latest_sequence = 3;
    input_batch.reports[0].sequence = 3;
    input_tick(NULL,driver);
    assert(deliveries == 2);
    free_driver(driver);
}

static void controller_batch(Driver *driver,uint64_t sequence,uint16_t bits) {
    expected_after=driver->cursor;
    input_batch=(RK3NativeReports){.version=1,.count=1,.next_sequence=sequence,.latest_sequence=sequence};
    input_batch.reports[0]=(RK3NativeReport){.sequence=sequence,.host_time=9876,.length=3,
        .bytes={4,(uint8_t)bits,(uint8_t)(bits>>8)}};
    input_tick(NULL,driver);
}
static void test_controller_and_feedback(void) {
    Driver *driver=make_driver();atomic_store(&driver->controller_enabled,true);
    input_batch=(RK3NativeReports){.version=1};expected_after=0;deliveries=0;
    input_tick(NULL,driver); /* current physical snapshot establishes baseline */
    atomic_store(&driver->output_read,atomic_load(&driver->output_write));
    controller_batch(driver,1,1);
    assert(deliveries==1&&delivered_source==124&&delivered_time==9876);
    assert(memcmp(delivered,(uint8_t[]){0xb0,76,127},3)==0);
    uint64_t head=atomic_load(&driver->output_write);
    assert(driver->output[(head-1)%OUTPUT_SLOTS].command.bytes[0]==5);
    assert(driver->output[(head-1)%OUTPUT_SLOTS].command.bytes[5]==1);
    controller_batch(driver,2,0);assert(deliveries==1);
    assert(enable_source(driver_ref(driver),124,false)==noErr);
    controller_batch(driver,3,1);assert(deliveries==1&&driver->controller.button_values[0]==0);
    assert(enable_source(driver_ref(driver),124,true)==noErr); /* samples sequence3 now, not next poll */
    controller_batch(driver,4,0);controller_batch(driver,5,1);
    assert(deliveries==2&&delivered[2]==127); /* first new press is preserved */
    OutputEntry feedback={.audio_device=42,.generation=7,.lifecycle_epoch=0,.kind=1,
        .command={.version=1,.length=6,.bytes={7,0,3,0xb0,76,0}}};
    driver->feedback[0]=feedback;atomic_store(&driver->feedback_write,1);
    input_batch=(RK3NativeReports){.version=1,.next_sequence=5,.latest_sequence=5};expected_after=5;
    input_tick(NULL,driver);
    assert(deliveries==2&&driver->controller.button_values[0]==0);
    head=atomic_load(&driver->output_write);
    assert(driver->output[(head-1)%OUTPUT_SLOTS].command.bytes[5]==0);
    driver->controller_device=456;poll_calibration(driver);
    assert(calibration_data&&CFDataGetLength(calibration_data)==16);
    uint32_t calibration[]={1,2,103,921};
    CFRelease(calibration_data);calibration_data=CFDataCreate(NULL,(const UInt8 *)calibration,16);
    poll_calibration(driver);input_tick(NULL,driver);
    double low,high;assert(rk3_controller_get_calibration(&driver->controller,2,&low,&high)==RK3_CONTROLLER_OK);
    assert(low>0.100&&low<0.102&&high>0.900&&high<0.901);
    uint64_t saved=atomic_load(&driver->calibration);calibration[2]=900;calibration[3]=901;
    CFRelease(calibration_data);calibration_data=CFDataCreate(NULL,(const UInt8 *)calibration,16);
    poll_calibration(driver);assert(atomic_load(&driver->calibration)==saved);
    CFRelease(calibration_data);calibration_data=NULL;driver->controller_device=0;
    free_driver(driver);
}

static void test_enable_before_first_controller_poll(void) {
    Driver *driver=make_driver();
    input_batch=(RK3NativeReports){.version=1};expected_after=0;digital_snapshot=UINT32_C(0x80000000);deliveries=0;
    assert(enable_source(driver_ref(driver),124,true)==noErr);
    digital_snapshot=UINT32_C(0x80000001); /* first press occurs after Enable returns */
    controller_batch(driver,1,1);
    assert(deliveries==1&&delivered_source==124&&delivered[1]==76&&delivered[2]==127);
    /* A lost-history reset uses the current snapshot, not the old enable seed. */
    expected_after=driver->cursor;
    input_batch=(RK3NativeReports){.version=1,.count=1,.dropped=90,.next_sequence=100,.latest_sequence=100};
    input_batch.reports[0]=(RK3NativeReport){.sequence=100,.length=3,.bytes={4,1,0}};
    input_tick(NULL,driver);
    assert(deliveries==1&&driver->controller.button_values[0]==127);
    controller_batch(driver,101,0);controller_batch(driver,102,1);
    assert(deliveries==2&&delivered[2]==0);
    digital_snapshot=UINT32_C(0x80000000);free_driver(driver);
}

static void test_controller_activation_snapshot_barrier(void) {
    Driver *driver=make_driver();
    input_batch=(RK3NativeReports){.version=1};expected_after=0;digital_snapshot=UINT32_C(0x80000000);deliveries=0;
    press_release_before_snapshot=true;
    assert(enable_source(driver_ref(driver),124,true)==noErr);
    input_tick(NULL,driver);
    assert(deliveries==0); /* completed before enable: state catch-up, no replay */
    assert(driver->controller.raw_buttons==0&&driver->controller.button_values[0]==127);
    controller_batch(driver,3,1);
    assert(deliveries==1&&delivered_source==124&&delivered[1]==76&&delivered[2]==0);
    free_driver(driver);
}

static void test_cf_transport_rejection(void) {
    assert(rk3_native_copy_info(42,NULL)==kAudioHardwareIllegalOperationError);
    assert(rk3_native_copy_reports(42,NULL,NULL)==kAudioHardwareIllegalOperationError);
    assert(rk3_native_send_command(42,NULL,NULL)==kAudioHardwareIllegalOperationError);
    for(unsigned fault=CF_FAULT_TYPE;fault<=CF_FAULT_STATUS;++fault) {
        Driver *driver=make_driver();RK3NativeInfo info,saved_info;
        RK3NativeReports reports,saved_reports;
        memset(&saved_info,0xa5,sizeof(saved_info));info=saved_info;
        memset(&saved_reports,0x5a,sizeof(saved_reports));reports=saved_reports;
        input_batch=(RK3NativeReports){.version=1};expected_after=0;deliveries=0;
        cf_fault=fault;cf_fault_selector=RK3_PROPERTY_INFO;
        assert(rk3_native_copy_info(42,&info)!=noErr&&memcmp(&info,&saved_info,sizeof(info))==0);
        assert(!read_info(42,&info));
        assert(enable_source(driver_ref(driver),124,true)==kMIDINoConnection);
        assert(!atomic_load(&driver->controller_enabled));
        cf_fault_selector=RK3_PROPERTY_REPORTS;
        const RK3NativeReportQuery query={RK3_CONTROL_VERSION,0,0};
        assert(rk3_native_copy_reports(42,&query,&reports)!=noErr&&memcmp(&reports,&saved_reports,sizeof(reports))==0);
        assert(enable_source(driver_ref(driver),124,true)==kMIDIMessageSendErr);
        assert(!atomic_load(&driver->controller_enabled));
        input_tick(NULL,driver);
        assert(!atomic_load(&driver->online)&&deliveries==0);
        cf_fault=CF_FAULT_NONE;cf_fault_selector=0;free_driver(driver);
    }
}

static void test_worker_generation_and_error(void) {
    for (unsigned fail = 0; fail < 2; ++fail) {
        Driver *driver = make_driver();
        RK3NativeCommand command = {.version=RK3_CONTROL_VERSION,.length=6,.bytes={7,0,3,0x90,60,100}};
        driver->output[0] = (OutputEntry){42,6,0,command,0}; /* stale, must never reach HAL */
        driver->output[1] = (OutputEntry){42,7,0,command,1};
        driver->output[2] = (OutputEntry){42,7,0,command,0};
        atomic_store(&driver->output_write,3);
        atomic_store(&driver->worker_stop,false);
        working_driver = driver; writes = 0;
        write_result = fail ? kAudioHardwareUnspecifiedError : noErr;
        add_ref(driver); /* production worker's lifetime reference */
        output_worker(driver);
        assert(writes == 1 && atomic_load(&driver->output_read) == 3);
        assert(atomic_load(&driver->feedback_write) == 1 && driver->feedback[0].kind == 1);
        assert(atomic_load(&driver->online));
        assert(atomic_load(&driver->output_failures)==fail);
        assert(atomic_load(&driver->failed_generation)==0);
        free_driver(driver);
    }
}

/* A lost EP1 write is not removal. NI's device MIDIWrite is void, and the
 * user-client returns success while the physical device is present. */
static void test_transient_output_does_not_disconnect_sources(void) {
    Driver *driver=make_driver();atomic_store(&driver->controller_enabled,true);
    info_generation=7;digital_snapshot=UINT32_C(0x80000000);provider_present=true;provider_flags=RK3_NATIVE_CONNECTED;
    input_batch=(RK3NativeReports){.version=1};expected_after=0;deliveries=0;
    input_tick(NULL,driver); /* baseline, with both native sources enabled */
    RK3NativeCommand command={.version=RK3_CONTROL_VERSION,.length=6,.bytes={7,0,3,0x90,60,100}};
    driver->output[0]=(OutputEntry){42,6,0,command,0}; /* stale: never reaches USB */
    driver->output[1]=(OutputEntry){42,7,0,command,0};
    command.bytes[4]=61;driver->output[2]=(OutputEntry){42,7,0,command,0};
    atomic_store(&driver->output_read,0);atomic_store(&driver->output_write,3);atomic_store(&driver->worker_stop,false);
    working_driver=driver;writes=0;writes_to_stop=2;failures_remaining=1;write_result=noErr;
    memset(attempted_notes,0,sizeof(attempted_notes));
    pthread_t worker;add_ref(driver);assert(pthread_create(&worker,NULL,output_worker,driver)==0);
    for(unsigned attempt=0;attempt<500&&atomic_load(&driver->output_read)<3;++attempt) {
        const struct timespec delay={0,1000000};(void)nanosleep(&delay,NULL);
    }
    atomic_store(&driver->worker_stop,true);assert(pthread_join(worker,NULL)==0);
    assert(writes==2&&attempted_notes[0]==60&&attempted_notes[1]==61); /* failed entry was not replayed */
    assert(atomic_load(&driver->online)&&atomic_load(&driver->generation)==7&&atomic_load(&driver->failed_generation)==0);
    assert(atomic_load(&driver->output_failures)==1);
    assert(atomic_load(&driver->source_enabled)&&atomic_load(&driver->controller_enabled));
    const uint8_t note[]={0x90,62,100};
    input_batch=(RK3NativeReports){.version=1,.count=1,.next_sequence=1,.latest_sequence=1};
    input_batch.reports[0]=midi_report(1,note,sizeof(note));expected_after=0;
    input_tick(NULL,driver);assert(deliveries==1&&delivered_source==123&&memcmp(delivered,note,3)==0);
    controller_batch(driver,2,0x10);
    assert(deliveries==2&&delivered_source==124&&memcmp(delivered,(uint8_t[]){0xb0,80,127},3)==0);
    /* Actual disappearance still publishes offline, on server main only. */
    driver->midi_device=333;driver->controller_device=334;driver->published_online=true;
    offline_updates=0;provider_present=false;refresh_topology(driver);
    assert(!atomic_load(&driver->online)&&offline_updates==2&&last_offline==1);
    provider_present=true;info_generation=8;refresh_topology(driver);
    assert(atomic_load(&driver->online)&&atomic_load(&driver->generation)==8&&offline_updates==4&&last_offline==0);
    if(calibration_data){CFRelease(calibration_data);calibration_data=NULL;}
    info_generation=7;writes_to_stop=1;failures_remaining=0;free_driver(driver);
}

static Driver *lifecycle_driver(void) {
    assert(mock_worker_owner==NULL);
    info_generation=7;provider_present=true;provider_flags=RK3_NATIVE_CONNECTED;
    digital_snapshot=UINT32_C(0x80000000);press_release_before_snapshot=false;
    cf_fault=CF_FAULT_NONE;cf_fault_selector=0;deliveries=0;
    memset(controller_cc_counts,0,sizeof(controller_cc_counts));
    memset(controller_cc_last,0,sizeof(controller_cc_last));
    memset(controller_cc_source,0,sizeof(controller_cc_source));
    input_batch=(RK3NativeReports){.version=RK3_CONTROL_VERSION};expected_after=0;
    topology_din=topology_controller=topology_controller_source=true;
    topology_no_driver_list=false;topology_din_source=123;topology_controller_source_ref=124;
    topology_refcons_count=0;timer_attachments=worker_creates=worker_joins=0;
    if(calibration_data){CFRelease(calibration_data);calibration_data=NULL;}
    Driver *driver=RK3MIDIDriverFactory(NULL,kMIDIDriverTypeID);assert(driver);
    return driver;
}
static void lifecycle_start(Driver *driver) {
    expected_after=0;
    assert(start_driver(driver_ref(driver),TEST_DEVICE_LIST)==noErr);
    assert(driver->started&&driver->input_timer&&driver->discovery_timer);
    assert(CFRunLoopTimerIsValid(driver->input_timer)&&CFRunLoopTimerIsValid(driver->discovery_timer));
    if(topology_din)assert(endpoint_refcons(topology_din_source)->second==driver);
    if(topology_controller)assert(endpoint_refcons(topology_controller_source_ref)->second==driver);
}
static void lifecycle_stop(Driver *driver) {
    assert(stop_driver(driver_ref(driver))==noErr);
    assert(!driver->started&&!atomic_load(&driver->online));
    assert(driver->input_timer==NULL&&driver->discovery_timer==NULL&&mock_worker_owner==NULL);
    assert(driver->source==0&&driver->controller_source==0);
    assert(atomic_load(&driver->references)==1); /* worker and real timer leases released */
}
static void lifecycle_enable(Driver *driver) {
    assert(enable_source(driver_ref(driver),123,true)==noErr);
    assert(enable_source(driver_ref(driver),124,true)==noErr);
    input_tick(NULL,driver);
}
static void lifecycle_din(Driver *driver,uint64_t sequence) {
    const uint8_t note[]={0x90,60,100};expected_after=driver->cursor;
    input_batch=(RK3NativeReports){.version=1,.count=1,.next_sequence=sequence,.latest_sequence=sequence};
    input_batch.reports[0]=midi_report(sequence,note,sizeof(note));input_tick(NULL,driver);
}
static void lifecycle_pedal(Driver *driver,uint64_t sequence,uint16_t raw) {
    expected_after=driver->cursor;
    input_batch=(RK3NativeReports){.version=1,.count=1,.next_sequence=sequence,.latest_sequence=sequence};
    input_batch.reports[0]=(RK3NativeReport){.sequence=sequence,.host_time=9876,.length=7,
        .bytes={3,0,0,0,0,(uint8_t)(raw>>8),(uint8_t)raw}};input_tick(NULL,driver);
}
static void lifecycle_done(Driver *driver) {
    lifecycle_stop(driver);assert(worker_creates==worker_joins);free_driver(driver);
    if(calibration_data){CFRelease(calibration_data);calibration_data=NULL;}
    provider_present=true;topology_din=topology_controller=topology_controller_source=true;
    topology_no_driver_list=false;topology_din_source=123;topology_controller_source_ref=124;
}

static void test_start_stop_start_keeps_listener_intent(void) {
    Driver *driver=lifecycle_driver();lifecycle_start(driver);lifecycle_enable(driver);
    controller_batch(driver,1,0x10);assert(deliveries==1&&delivered[1]==80&&delivered[2]==127);
    lifecycle_din(driver,2);assert(deliveries==2&&delivered_source==123);
    uint64_t listener=atomic_load(&driver->controller_listener_epoch);
    lifecycle_stop(driver);
    assert(endpoint_refcons(123)->second==NULL&&endpoint_refcons(124)->second==NULL);
    lifecycle_start(driver); /* CoreMIDI does not resend EnableSource. */
    input_tick(NULL,driver); /* discard the pre-stop DIN history / reset controller baseline */
    assert(deliveries==2);
    controller_batch(driver,3,0);controller_batch(driver,4,0x10);
    fprintf(stderr,"LIFECYCLE_RESUME controller callbacks=%zu expected=3\n",deliveries);
    assert(deliveries==3&&delivered_source==124&&delivered[1]==80&&delivered[2]==0);
    lifecycle_din(driver,5);assert(deliveries==4&&delivered_source==123);
    unsigned wah=controller_cc_counts[4];lifecycle_pedal(driver,6,0);lifecycle_pedal(driver,7,1023);
    assert(controller_cc_counts[4]==wah+1&&controller_cc_source[4]==124&&controller_cc_last[4]==127);
    assert(atomic_load(&driver->controller_listener_epoch)==listener);
    assert(worker_creates==2&&timer_attachments==4);
    lifecycle_stop(driver);assert(worker_joins==2);free_driver(driver);
    if(calibration_data){CFRelease(calibration_data);calibration_data=NULL;}
}

static void test_stopped_disable_wins_and_does_not_query_hal(void) {
    Driver *driver=lifecycle_driver();lifecycle_start(driver);lifecycle_enable(driver);
    lifecycle_stop(driver);
    unsigned infos=info_queries,reports=report_queries;
    assert(enable_source(driver_ref(driver),123,false)==noErr);
    assert(enable_source(driver_ref(driver),124,false)==noErr);
    assert(enable_source(driver_ref(driver),999,true)==kMIDIUnknownEndpoint);
    assert(info_queries==infos&&report_queries==reports);
    lifecycle_start(driver);input_tick(NULL,driver);
    controller_batch(driver,1,0x10);lifecycle_din(driver,2);
    assert(deliveries==0&&driver->controller.button_values[4]==127);
    assert(!atomic_load(&driver->source_enabled)&&!atomic_load(&driver->controller_enabled));
    lifecycle_done(driver);
}

static void test_stopped_reenable_and_repeated_stop_keep_held_baseline(void) {
    Driver *driver=lifecycle_driver();lifecycle_start(driver);lifecycle_enable(driver);
    controller_batch(driver,1,0x10);controller_batch(driver,2,0);
    controller_batch(driver,3,0x10);controller_batch(driver,4,0);controller_batch(driver,5,0x10);
    assert(deliveries==3&&delivered[2]==127);
    lifecycle_stop(driver);
    unsigned infos=info_queries,reports=report_queries;
    assert(enable_source(driver_ref(driver),124,false)==noErr);
    assert(enable_source(driver_ref(driver),124,true)==noErr);
    assert(info_queries==infos&&report_queries==reports); /* no zero snapshot is captured */
    lifecycle_stop(driver); /* a second I/O Stop must not erase listener intent */
    digital_snapshot=UINT32_C(0x80000010);
    /* The retained history contains multiple old edges. A bogus zero enable
     * snapshot/sequence would replay two toggles, despite ending held. */
    input_batch=(RK3NativeReports){.version=1,.count=5,.next_sequence=5,.latest_sequence=5};
    for(unsigned index=0;index<5;++index)
        input_batch.reports[index]=(RK3NativeReport){.sequence=index+1,.host_time=9876,.length=3,
            .bytes={4,index%2==0?0x10:0,0}};
    lifecycle_start(driver);input_tick(NULL,driver);
    assert(deliveries==3&&driver->controller.raw_buttons==0x10);
    controller_batch(driver,6,0);assert(deliveries==3&&driver->controller.button_values[4]==127);
    controller_batch(driver,7,0x10);
    assert(deliveries==4&&delivered[1]==80&&delivered[2]==0);
    lifecycle_din(driver,8);assert(deliveries==5&&delivered_source==123);
    unsigned wah=controller_cc_counts[4];lifecycle_pedal(driver,9,0);lifecycle_pedal(driver,10,1023);
    assert(controller_cc_counts[4]==wah+1&&controller_cc_source[4]==124&&controller_cc_last[4]==127);
    lifecycle_done(driver);digital_snapshot=UINT32_C(0x80000000);
}

static void test_restart_without_listener_stays_silent(void) {
    Driver *driver=lifecycle_driver();lifecycle_start(driver);input_tick(NULL,driver);
    lifecycle_stop(driver);lifecycle_start(driver);input_tick(NULL,driver);
    controller_batch(driver,1,0x10);lifecycle_din(driver,2);
    assert(deliveries==0&&driver->controller.button_values[4]==127);
    assert(!atomic_load(&driver->source_enabled)&&!atomic_load(&driver->controller_enabled));
    lifecycle_done(driver);
}

static void test_absent_hardware_is_not_removed_registered_endpoint(void) {
    Driver *driver=lifecycle_driver();lifecycle_start(driver);lifecycle_enable(driver);
    lifecycle_stop(driver);provider_present=false;
    lifecycle_start(driver);
    assert(!atomic_load(&driver->online));
    assert(atomic_load(&driver->source_enabled)&&atomic_load(&driver->controller_enabled));
    unsigned infos=info_queries,reports=report_queries;
    input_tick(NULL,driver);assert(deliveries==0&&info_queries==infos&&report_queries==reports);
    lifecycle_stop(driver);provider_present=true;
    lifecycle_start(driver);input_tick(NULL,driver);
    controller_batch(driver,1,0x10);assert(deliveries==1&&delivered_source==124);
    lifecycle_din(driver,2);assert(deliveries==2&&delivered_source==123);
    lifecycle_done(driver);
}

static void test_missing_registered_endpoint_drops_only_its_listener(void) {
    for(unsigned missing_din=0;missing_din<2;++missing_din) {
        Driver *driver=lifecycle_driver();lifecycle_start(driver);lifecycle_enable(driver);
        lifecycle_stop(driver);
        topology_din=!missing_din;topology_controller=missing_din!=0;
        provider_present=false; /* no dynamic registration while enumerating the missing setup */
        topology_no_driver_list=true;
        lifecycle_start(driver);
        assert(atomic_load(&driver->source_enabled)==!missing_din);
        assert(atomic_load(&driver->controller_enabled)==(missing_din!=0));
        lifecycle_stop(driver);
        assert(enable_source(driver_ref(driver),missing_din?123:124,true)==kMIDIUnknownEndpoint);
        lifecycle_done(driver);
    }
}

static void test_replaced_registered_sources_do_not_inherit_listeners(void) {
    Driver *driver=lifecycle_driver();lifecycle_start(driver);lifecycle_enable(driver);
    lifecycle_stop(driver);topology_din_source=223;topology_controller_source_ref=224;
    lifecycle_start(driver);input_tick(NULL,driver);
    assert(!atomic_load(&driver->source_enabled)&&!atomic_load(&driver->controller_enabled));
    controller_batch(driver,1,0x10);lifecycle_din(driver,2);assert(deliveries==0);
    lifecycle_stop(driver);
    unsigned infos=info_queries,reports=report_queries;
    assert(enable_source(driver_ref(driver),123,true)==kMIDIUnknownEndpoint);
    assert(enable_source(driver_ref(driver),124,true)==kMIDIUnknownEndpoint);
    assert(enable_source(driver_ref(driver),223,true)==noErr);
    assert(enable_source(driver_ref(driver),224,true)==noErr);
    assert(info_queries==infos&&report_queries==reports);
    lifecycle_start(driver);input_tick(NULL,driver);
    controller_batch(driver,3,0x10);assert(deliveries==1&&delivered_source==224);
    lifecycle_din(driver,4);assert(deliveries==2&&delivered_source==223);
    lifecycle_done(driver);
}

static void test_failed_start_is_inactive_then_same_endpoint_resumes(void) {
    Driver *driver=lifecycle_driver();lifecycle_start(driver);lifecycle_enable(driver);
    lifecycle_stop(driver);topology_controller_source=false;
    assert(start_driver(driver_ref(driver),TEST_DEVICE_LIST)==kMIDISetupFormatErr);
    assert(!driver->started&&!atomic_load(&driver->online));
    assert(endpoint_refcons(124)->second==NULL);
    unsigned infos=info_queries,reports=report_queries;
    input_tick(NULL,driver);
    assert(deliveries==0&&info_queries==infos&&report_queries==reports);
    /* An incomplete Start is not a listener disconnect. Once the same
     * registered source binds successfully, no new EnableSource is required. */
    topology_controller_source=true;lifecycle_start(driver);input_tick(NULL,driver);
    controller_batch(driver,1,0x10);assert(deliveries==1&&delivered_source==124);
    lifecycle_din(driver,2);assert(deliveries==2&&delivered_source==123);
    unsigned wah=controller_cc_counts[4];lifecycle_pedal(driver,3,0);lifecycle_pedal(driver,4,1023);
    assert(controller_cc_counts[4]==wah+1&&controller_cc_source[4]==124&&controller_cc_last[4]==127);
    lifecycle_done(driver);
}

int main(void) {
    assert(CFPlugInRegisterFactoryFunction(factory_id(),RK3MIDIDriverFactory));
    test_com(); test_packet_queue(); test_input_history(); test_complete_message_boundaries(); test_listener_epoch(); test_controller_and_feedback(); test_enable_before_first_controller_poll(); test_controller_activation_snapshot_barrier(); test_cf_transport_rejection(); test_worker_generation_and_error(); test_transient_output_does_not_disconnect_sources();
    test_start_stop_start_keeps_listener_intent();
    test_stopped_disable_wins_and_does_not_query_hal();test_stopped_reenable_and_repeated_stop_keep_held_baseline();
    test_restart_without_listener_stays_silent();test_absent_hardware_is_not_removed_registered_endpoint();
    test_missing_registered_endpoint_drops_only_its_listener();test_replaced_registered_sources_do_not_inherit_listeners();
    test_failed_start_is_inactive_then_same_endpoint_resumes();
    assert(CFPlugInUnregisterFactory(factory_id()));
    puts("RK3 CoreMIDI: COM, bounded queue, generation lease, history cursor and input routing passed");
    return 0;
}
