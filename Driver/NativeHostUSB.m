/* Current macOS binding only: no NI audio scheduling or sample conversion.
 * Apple SDK IOUSBHostObject.h: ioData/dataWithCapacity return mapped, fixed-size
 * controller-optimized IOBufferMemoryDescriptor storage that is not bounced.
 * IOUSBHostDefinitions.h: transaction offsets and Wrap represent ring ranges.
 * These contracts do not promise arbitrary writes concurrent with a DMA fetch.
 * Primary sources: https://developer.apple.com/documentation/iousbhost/iousbhostobject/iodata(withcapacity:)
 * https://developer.apple.com/documentation/iousbhost/iousbhostpipe
 */
#include "NativeHostUSB.h"
#import <Foundation/Foundation.h>
#import <IOUSBHost/IOUSBHost.h>
#include <libkern/OSByteOrder.h>
#include <mach/mach_time.h>
#include <os/log.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define RK3_HOST_PIPE_LIMIT 30
typedef struct IsoJob IsoJob;
/* Temporary .11 observation. Process-lifetime storage never owns USB objects.
 * The lifecycle word includes the cookie, so an old final releaser cannot
 * reclaim a slot already reserved for another job. One reservation attempt. */
enum {MEASURE_SLOTS=512,MR_RAW=1,MR_WORKER=2,MR_LAST=4,MR_FAULT=8};
enum {MM_READY=1,MM_R=2,MM_P0=4,MM_P1=8,MM_K=16,MM_W=32,MM_D=64,MM_ALL=127};
typedef struct {
    _Atomic uint64_t life;
    atomic_uint marks;
    uint8_t pipe;
    uint64_t interface_cookie,epoch,first,count,start;
    _Atomic uint64_t r,p0,p1,k,w,d;
} RK3MeasureRecord;
typedef struct {RK3MeasureRecord *record;uint64_t cookie;} RK3MeasureRef;
typedef struct {
    uint64_t cookie,interface_cookie,epoch,first,count,start,r,p0,p1,k,w,d;
    uint8_t pipe;unsigned marks;bool coherent;
} RK3MeasureSnapshot;
typedef struct {uint64_t begin,end,epoch;uint32_t depth;bool active,valid,nested;} RK3MeasureClock;
typedef struct {
    RK3MeasureSnapshot job;RK3MeasureClock clock;
    bool matched,complete,ordered,post_wait,overlap_valid;
    uint64_t pre,post,hold,overlap,uncovered;
} RK3MeasureFault;
static RK3MeasureRecord measure_bank[MEASURE_SLOTS];
static _Atomic uint64_t measure_next=1;
_Static_assert(sizeof(measure_bank)+sizeof(measure_next)<=65536,"bounded measurement bank");
_Static_assert(ATOMIC_LLONG_LOCK_FREE==2&&ATOMIC_INT_LOCK_FREE==2,"nonblocking measurement atomics");
static uint64_t measure_cookie(void) {
    uint64_t value=atomic_load(&measure_next);
    if(!value||value>=UINT64_MAX/16)return 0;
    return atomic_compare_exchange_strong(&measure_next,&value,value+1)?value:0;
}
static RK3MeasureRef measure_reserve(uint64_t interface_cookie,uint64_t epoch,uint8_t pipe,
    size_t first,size_t count,uint64_t start) {
    RK3MeasureRef ref={0};if(!interface_cookie||!epoch||epoch==UINT64_MAX)return ref;
    uint64_t cookie=measure_cookie();if(!cookie)return ref;
    RK3MeasureRecord *record=&measure_bank[cookie%MEASURE_SLOTS];uint64_t empty=0;
    if(!atomic_compare_exchange_strong(&record->life,&empty,(cookie<<4)|MR_RAW|MR_WORKER))return ref;
    record->interface_cookie=interface_cookie;record->epoch=epoch;record->pipe=pipe;
    record->first=first;record->count=count;record->start=start;
    atomic_store(&record->r,0);atomic_store(&record->p0,0);atomic_store(&record->p1,0);
    atomic_store(&record->k,0);atomic_store(&record->w,0);atomic_store(&record->d,0);
    atomic_store_explicit(&record->marks,MM_READY,memory_order_release);
    return (RK3MeasureRef){record,cookie};
}
static void measure_pin(RK3MeasureRef ref,unsigned pin) {
    if(ref.record)atomic_fetch_or(&ref.record->life,pin); /* caller already owns an interest */
}
static void measure_drop(RK3MeasureRef ref,unsigned interest) {
    if(!ref.record)return;
    atomic_fetch_and(&ref.record->life,~(uint64_t)interest);
    uint64_t empty=ref.cookie<<4;
    (void)atomic_compare_exchange_strong(&ref.record->life,&empty,0);
}
static void measure_mark(RK3MeasureRef ref,unsigned mark,uint64_t tick) {
    if(!ref.record)return;
    _Atomic uint64_t *field=NULL;
    switch(mark){case MM_R:field=&ref.record->r;break;case MM_P0:field=&ref.record->p0;break;
        case MM_P1:field=&ref.record->p1;break;case MM_K:field=&ref.record->k;break;
        case MM_W:field=&ref.record->w;break;case MM_D:field=&ref.record->d;break;default:return;}
    atomic_store_explicit(field,tick,memory_order_relaxed);
    atomic_fetch_or_explicit(&ref.record->marks,mark,memory_order_release);
}
static RK3MeasureSnapshot measure_snapshot(RK3MeasureRef ref) {
    RK3MeasureSnapshot s={0};if(!ref.record)return s;
    RK3MeasureRecord *r=ref.record;uint64_t life=atomic_load_explicit(&r->life,memory_order_acquire);
    if(life>>4!=ref.cookie)return s;
    unsigned marks=atomic_load_explicit(&r->marks,memory_order_acquire);if(!(marks&MM_READY))return s;
    s=(RK3MeasureSnapshot){.cookie=ref.cookie,.interface_cookie=r->interface_cookie,.epoch=r->epoch,
        .first=r->first,.count=r->count,.start=r->start,.pipe=r->pipe,.marks=marks};
    if(marks&MM_R)s.r=atomic_load(&r->r);if(marks&MM_P0)s.p0=atomic_load(&r->p0);
    if(marks&MM_P1)s.p1=atomic_load(&r->p1);if(marks&MM_K)s.k=atomic_load(&r->k);
    if(marks&MM_W)s.w=atomic_load(&r->w);if(marks&MM_D)s.d=atomic_load(&r->d);
    s.coherent=(atomic_load_explicit(&r->life,memory_order_acquire)>>4)==ref.cookie&&
        marks==atomic_load_explicit(&r->marks,memory_order_acquire);
    return s;
}
/* [DEBUG-rk3-timing] temporary observation only; no scheduling policy. */
typedef struct {
    uint8_t pipe_address;
    uint64_t start_frame,last_timestamp,raw_tick,worker_tick,delivery_tick;
    uint64_t cookie,interface_cookie,stream_epoch;
} RK3DebugIsoTiming;
struct RK3HostInterface { atomic_uint references; CFRunLoopRef worker; CFRunLoopSourceRef keepalive; void *state; atomic_bool *connected; };
struct RK3HostBuffer {
    atomic_uint references;
    RK3HostInterface *owner;
    void *object;
    void *bytes;
    size_t size;
};
struct RK3HostIsoList {
    atomic_uint references;
    RK3HostInterface *owner;
    RK3HostBuffer *backing;
    IOUSBHostIsochronousTransaction *entries;
    unsigned char *busy;
    size_t count;
};
@interface RK3HostState : NSObject {
@public
    IOUSBHostInterface *host;
    dispatch_queue_t queue;
    NSMutableArray *pipes;
    RK3HostPipeProperties properties[RK3_HOST_PIPE_LIMIT];
    uint8_t addresses[RK3_HOST_PIPE_LIMIT];
    uint8_t count;
    bool closed;
    atomic_bool connected;
    CFRunLoopRef interest_worker;
    size_t pending, mapped_buffers;
    IsoJob *iso_head[RK3_HOST_PIPE_LIMIT],*iso_tail[RK3_HOST_PIPE_LIMIT];
    bool iso_delivering[RK3_HOST_PIPE_LIMIT];
    RK3DebugIsoTiming debug_last_iso; /* worker-only; temporary [DEBUG-rk3-timing] */
    bool debug_too_old_logged;
    uint64_t debug_interface_cookie,debug_stream_epoch,debug_engine_seed;
    RK3MeasureRef debug_last_record,debug_fault_record;
    RK3MeasureClock debug_clock;
    RK3MeasureFault debug_fault;
}
@end
@implementation RK3HostState
- (void)dealloc {
    measure_drop(debug_last_record,MR_LAST);measure_drop(debug_fault_record,MR_FAULT);
    if(interest_worker) CFRelease(interest_worker);
}
@end

#ifdef RK3_HOST_USB_TESTING
static id (*rk3_host_test_factory)(io_service_t, dispatch_queue_t, NSError **, IOUSBHostInterestHandler);
static void (*rk3_host_test_after_publication)(void);
static void (*rk3_host_test_after_postmark)(void);
#endif
static RK3HostState *state_of(const RK3HostInterface *owner) {
    return owner ? (__bridge RK3HostState *)owner->state : nil;
}
static bool worker_thread(const RK3HostInterface *owner) {
    return owner && owner->worker == CFRunLoopGetCurrent();
}
void rk3_host_measurement_begin_stream(RK3HostInterface *owner,uint64_t epoch) {
    if(!worker_thread(owner))return;RK3HostState *state=state_of(owner);if(!state||state->closed)return;
    measure_drop(state->debug_last_record,MR_LAST);state->debug_last_record=(RK3MeasureRef){0};
    state->debug_last_iso=(RK3DebugIsoTiming){0};state->debug_clock=(RK3MeasureClock){0};
    if(state->debug_stream_epoch!=UINT64_MAX)++state->debug_stream_epoch;
    state->debug_engine_seed=epoch; /* Engine seeds may repeat; the local epoch does not. */
}
void rk3_host_measurement_clock(RK3HostInterface *owner,bool beginning,uint64_t tick) {
    if(!worker_thread(owner))return;RK3HostState *state=state_of(owner);if(!state||state->closed)return;
    RK3MeasureClock *c=&state->debug_clock;
    if(beginning) {
        if(c->depth){if(c->depth!=UINT32_MAX)++c->depth;c->nested=true;c->valid=false;return;}
        *c=(RK3MeasureClock){.begin=tick,.epoch=state->debug_stream_epoch,.depth=1,.active=true};
    } else {
        if(c->depth)--c->depth;
        c->valid=c->active&&!c->depth&&!c->nested&&tick>=c->begin&&c->begin&&c->epoch==state->debug_stream_epoch;
        c->end=tick;c->active=c->depth!=0;
    }
}
static int32_t error_code(NSError *error) {
    return error ? (int32_t)(uint32_t)error.code : (int32_t)kIOReturnError;
}
static void set_error(int32_t *output, int32_t error) { if(output) *output=error; }
static void keep_loop_alive(void *context) {(void)context;}
static bool opened(RK3HostInterface *owner) {
    RK3HostState *state=state_of(owner);
    return worker_thread(owner) && state && !state->closed && state->host;
}
static NSMutableData *allocate_data(IOUSBHostInterface *host, size_t size, bool list, NSError **error) {
    if(@available(macOS 26.4, *))
        return [host dataWithCapacity:size options:list ? IOUSBHostObjectDataOptionsKernelUserShared : IOUSBHostObjectDataOptionsNone error:error];
    return [host ioDataWithCapacity:size error:error];
}
/* Availability of explicit shared list allocation, not a proof of its
 * primary-interrupt publication timing on the current controller. */
bool rk3_host_primary_lists_supported(void) {
    if(@available(macOS 26.4, *)) return true;
    return false;
}
static int32_t refresh_pipes(RK3HostInterface *owner) {
    RK3HostState *state=state_of(owner);
    const IOUSBConfigurationDescriptor *configuration=state->host.configurationDescriptor;
    const IOUSBInterfaceDescriptor *interface=state->host.interfaceDescriptor;
    if(!configuration || !interface || interface->bNumEndpoints>RK3_HOST_PIPE_LIMIT)
        return kIOReturnBadArgument;
    [state->pipes removeAllObjects];
    state->count=0;
    const IOUSBEndpointDescriptor *endpoint=NULL;
    while((endpoint=IOUSBGetNextEndpointDescriptor(configuration,interface,(const IOUSBDescriptorHeader *)endpoint))) {
        if(state->count>=RK3_HOST_PIPE_LIMIT) return kIOReturnBadArgument;
        unsigned index=state->count++;
        state->addresses[index]=endpoint->bEndpointAddress;
        state->properties[index]=(RK3HostPipeProperties){
            (uint8_t)((endpoint->bEndpointAddress>>7)&1),
            (uint8_t)(endpoint->bEndpointAddress&15),
            (uint8_t)(endpoint->bmAttributes&3), endpoint->bInterval,
            (uint16_t)(OSSwapLittleToHostInt16(endpoint->wMaxPacketSize)&0x7ff)
        };
        [state->pipes addObject:[NSNull null]];
    }
    return state->count==interface->bNumEndpoints ? 0 : kIOReturnBadArgument;
}
static IOUSBHostPipe *get_pipe(RK3HostInterface *owner, uint8_t pipe, int32_t *error) { @autoreleasepool {
    if(!opened(owner)) {set_error(error,kIOReturnNotOpen);return nil;}
    RK3HostState *state=state_of(owner);
    if(!pipe || pipe>state->count) {set_error(error,kIOReturnBadArgument);return nil;}
    id existing=state->pipes[pipe-1];
    if(existing != [NSNull null]) return (IOUSBHostPipe *)existing;
    NSError *failure=nil;
    IOUSBHostPipe *result=[state->host copyPipeWithAddress:state->addresses[pipe-1] error:&failure];
    if(!result) {set_error(error,error_code(failure));return nil;}
    state->pipes[pipe-1]=result;
    return result;
}
}
RK3HostInterface *rk3_host_open(io_service_t service, CFRunLoopRef worker, int32_t *error) {
    set_error(error,kIOReturnBadArgument);
    if(!service || !worker || worker!=CFRunLoopGetCurrent()) return NULL;
    @autoreleasepool {
        RK3HostInterface *owner=calloc(1,sizeof(*owner));
        if(!owner) {set_error(error,kIOReturnNoMemory);return NULL;}
        atomic_init(&owner->references,1);
        owner->worker=(CFRunLoopRef)CFRetain(worker);
        CFRunLoopSourceContext source_context={0};source_context.perform=keep_loop_alive;
        owner->keepalive=CFRunLoopSourceCreate(NULL,0,&source_context);
        RK3HostState *state=[RK3HostState new];
        if(!owner->keepalive || !state) {
            if(owner->keepalive) CFRelease(owner->keepalive);
            CFRelease(owner->worker);free(owner);set_error(error,kIOReturnNoMemory);return NULL;
        }
        state->debug_interface_cookie=measure_cookie();
        CFRunLoopAddSource(worker,owner->keepalive,kCFRunLoopDefaultMode);
        atomic_init(&state->connected,true);
        owner->connected=&state->connected;
        state->interest_worker=(CFRunLoopRef)CFRetain(worker);
        owner->state=(__bridge_retained void *)state;
        /* Classify raw completion service before the existing owner handoff. */
        dispatch_queue_attr_t attributes=dispatch_queue_attr_make_with_qos_class(
            DISPATCH_QUEUE_SERIAL,QOS_CLASS_USER_INTERACTIVE,0);
        if(!attributes) {set_error(error,kIOReturnBadArgument);rk3_host_release(owner);return NULL;}
        state->queue=dispatch_queue_create("org.rigkontrol3.ni-arm.usb-host",attributes);
        if(!state->queue) {set_error(error,kIOReturnNoMemory);rk3_host_release(owner);return NULL;}
        state->pipes=[NSMutableArray new];
        /* IOUSBHostObject.h documents generalInterest on a separate internal
         * serial queue. Only IOMessage.h's exact service-termination message is
         * removal: suspend/resume, power and requesting-close are not. A weak
         * state capture avoids a host -> handler -> state -> host retain cycle;
         * promoting it pins the atomic and retained runloop through this call. */
        __weak RK3HostState *weak_state=state;
        IOUSBHostInterestHandler interest=^(IOUSBHostObject *object,uint32_t message,void *argument) {
            (void)object;(void)argument;
            if(message!=kIOMessageServiceIsTerminated) return;
            RK3HostState *live_state=weak_state;
            if(!live_state) return;
            atomic_store_explicit(&live_state->connected,false,memory_order_release);
            CFRunLoopWakeUp(live_state->interest_worker);
        };
        NSError *failure=nil;
#ifdef RK3_HOST_USB_TESTING
        if(rk3_host_test_factory) state->host=rk3_host_test_factory(service,state->queue,&failure,interest);
        else
#endif
        state->host=[[IOUSBHostInterface alloc] initWithIOService:service options:IOUSBHostObjectInitOptionsNone
            queue:state->queue error:&failure interestHandler:interest];
        int32_t result=state->host ? refresh_pipes(owner) : error_code(failure);
        if(result) {set_error(error,result);rk3_host_release(owner);return NULL;}
        set_error(error,0);return owner;
    }
}
void rk3_host_retain(RK3HostInterface *owner) {
    if(owner) atomic_fetch_add_explicit(&owner->references,1,memory_order_relaxed);
}
void rk3_host_release(RK3HostInterface *owner) {
    if(!owner || atomic_fetch_sub_explicit(&owner->references,1,memory_order_acq_rel)!=1) return;
    @autoreleasepool {
        RK3HostState *state=state_of(owner);
        /* Outstanding jobs and mapped storage each retain owner, so the final
         * reference cannot disappear while their callbacks still need it. */
        atomic_store_explicit(&state->connected,false,memory_order_release);
        [state->host destroy];
        [state->pipes removeAllObjects];state->host=nil;state->queue=nil;
        (void)CFBridgingRelease(owner->state);
        if(owner->keepalive) {
            CFRunLoopRemoveSource(owner->worker,owner->keepalive,kCFRunLoopDefaultMode);
            CFRunLoopSourceInvalidate(owner->keepalive);CFRelease(owner->keepalive);
        }
        CFRelease(owner->worker);free(owner);
    }
}
int32_t rk3_host_close(RK3HostInterface *owner) { @autoreleasepool {
    if(!worker_thread(owner)) return kIOReturnBadArgument;
    RK3HostState *state=state_of(owner);
    if(state->closed) return 0;
    if(state->pending || state->mapped_buffers) return kIOReturnBusy;
    atomic_store_explicit(&state->connected,false,memory_order_release);
    [state->host destroy];[state->pipes removeAllObjects];state->host=nil;
    state->closed=true;return 0;
}
}
int32_t rk3_host_get_alt(RK3HostInterface *owner,uint8_t *alternate) { @autoreleasepool {
    if(!opened(owner)) return kIOReturnNotOpen;
    const IOUSBInterfaceDescriptor *descriptor=state_of(owner)->host.interfaceDescriptor;
    if(!alternate || !descriptor) return kIOReturnBadArgument;
    *alternate=descriptor->bAlternateSetting;return 0;
}
}
int32_t rk3_host_set_alt(RK3HostInterface *owner,uint8_t alternate) { @autoreleasepool {
    if(!opened(owner)) return kIOReturnNotOpen;
    RK3HostState *state=state_of(owner);
    if(state->pending || state->mapped_buffers) return kIOReturnBusy;
    NSError *failure=nil;
    if(![state->host selectAlternateSetting:alternate error:&failure]) return error_code(failure);
    return refresh_pipes(owner);
}
}
int32_t rk3_host_num_pipes(RK3HostInterface *owner,uint8_t *count) {
    if(!opened(owner)) return kIOReturnNotOpen;
    if(!count) return kIOReturnBadArgument;
    *count=state_of(owner)->count;return 0;
}
int32_t rk3_host_pipe_properties(RK3HostInterface *owner,uint8_t pipe,RK3HostPipeProperties *properties) {
    if(!opened(owner)) return kIOReturnNotOpen;
    RK3HostState *state=state_of(owner);
    if(!properties || !pipe || pipe>state->count) return kIOReturnBadArgument;
    *properties=state->properties[pipe-1];return 0;
}
int32_t rk3_host_abort(RK3HostInterface *owner,uint8_t pipe) { @autoreleasepool {
    int32_t result=0;IOUSBHostPipe *object=get_pipe(owner,pipe,&result);if(!object) return result;
    NSError *failure=nil;
    return [object abortWithOption:IOUSBHostAbortOptionAsynchronous error:&failure] ? 0 : error_code(failure);
}
}
int32_t rk3_host_clear_stall(RK3HostInterface *owner,uint8_t pipe) { @autoreleasepool {
    int32_t result=0;IOUSBHostPipe *object=get_pipe(owner,pipe,&result);if(!object) return result;
    NSError *failure=nil;
    return [object clearStallWithError:&failure] ? 0 : error_code(failure);
}
}
int32_t rk3_host_bus_frame(RK3HostInterface *owner,uint64_t *frame,uint64_t *host_time) { @autoreleasepool {
    if(!opened(owner)) return kIOReturnNotOpen;
    if(!frame || !host_time) return kIOReturnBadArgument;
    IOUSBHostTime stamp=0;uint64_t value=[state_of(owner)->host frameNumberWithTime:&stamp];
    if(!value) return kIOReturnError;
    *frame=value;*host_time=stamp;return 0;
}
}
size_t rk3_host_pending(const RK3HostInterface *owner) { return owner ? state_of(owner)->pending : 0; }
bool rk3_host_connected(const RK3HostInterface *owner) {
    return owner && atomic_load_explicit(owner->connected,memory_order_acquire);
}

static void buffer_retain(RK3HostBuffer *buffer) {atomic_fetch_add_explicit(&buffer->references,1,memory_order_relaxed);}
static RK3HostBuffer *create_buffer(RK3HostInterface *owner,size_t size,bool shared_list,int32_t *error) {
    set_error(error,kIOReturnBadArgument);
    if(!opened(owner)) {set_error(error,kIOReturnNotOpen);return NULL;}
    if(!size || size>UINT32_MAX) return NULL;
    @autoreleasepool {
        RK3HostBuffer *buffer=calloc(1,sizeof(*buffer));
        if(!buffer) {set_error(error,kIOReturnNoMemory);return NULL;}
        NSError *failure=nil;
        NSMutableData *object=allocate_data(state_of(owner)->host,size,shared_list,&failure);
        if(!object || object.length!=size || !object.mutableBytes) {
            free(buffer);set_error(error,failure ? error_code(failure) : kIOReturnNoMemory);return NULL;
        }
        atomic_init(&buffer->references,1);buffer->owner=owner;rk3_host_retain(owner);
        buffer->object=(__bridge_retained void *)object;buffer->bytes=object.mutableBytes;buffer->size=size;
        ++state_of(owner)->mapped_buffers;
        set_error(error,0);return buffer;
    }
}
RK3HostBuffer *rk3_host_buffer_create(RK3HostInterface *owner,size_t size,int32_t *error) {
    return create_buffer(owner,size,false,error);
}
void *rk3_host_buffer_bytes(RK3HostBuffer *buffer) {return buffer ? buffer->bytes : NULL;}
size_t rk3_host_buffer_size(const RK3HostBuffer *buffer) {return buffer ? buffer->size : 0;}
void rk3_host_buffer_release(RK3HostBuffer *buffer) {
    if(!buffer || atomic_fetch_sub_explicit(&buffer->references,1,memory_order_acq_rel)!=1) return;
    RK3HostInterface *owner=buffer->owner;(void)CFBridgingRelease(buffer->object);
    --state_of(owner)->mapped_buffers;free(buffer);rk3_host_release(owner);
}
RK3HostIsoList *rk3_host_iso_list_create(RK3HostInterface *owner,size_t count,int32_t *error) {
    set_error(error,kIOReturnBadArgument);
    if(!count || count>UINT32_MAX/sizeof(IOUSBHostIsochronousTransaction)) return NULL;
    if(!opened(owner)) {set_error(error,kIOReturnNotOpen);return NULL;}
    RK3HostIsoList *list=calloc(1,sizeof(*list));
    if(!list) {set_error(error,kIOReturnNoMemory);return NULL;}
    list->backing=create_buffer(owner,count*sizeof(IOUSBHostIsochronousTransaction),true,error);
    list->busy=calloc(count,1);
    if(!list->backing || !list->busy) {
        if(list->backing) {rk3_host_buffer_release(list->backing);set_error(error,kIOReturnNoMemory);}
        free(list->busy);free(list);return NULL;
    }
    atomic_init(&list->references,1);list->owner=owner;list->count=count;
    list->entries=rk3_host_buffer_bytes(list->backing);
    for(size_t index=0;index<count;++index) list->entries[index].status=RK3_HOST_ISO_PENDING;
    return list;
}
size_t rk3_host_iso_list_count(const RK3HostIsoList *list) {return list ? list->count : 0;}
static void list_retain(RK3HostIsoList *list) {atomic_fetch_add_explicit(&list->references,1,memory_order_relaxed);}
void rk3_host_iso_list_release(RK3HostIsoList *list) {
    if(!list || atomic_fetch_sub_explicit(&list->references,1,memory_order_acq_rel)!=1) return;
    rk3_host_buffer_release(list->backing);free(list->busy);free(list);
}
int32_t rk3_host_iso_list_set(RK3HostIsoList *list,size_t index,uint32_t requested,uint32_t offset,bool wrap) {
    if(!list || !worker_thread(list->owner) || index>=list->count) return kIOReturnBadArgument;
    if(list->busy[index]) return kIOReturnBusy;
    list->entries[index]=(IOUSBHostIsochronousTransaction){
        .status=RK3_HOST_ISO_PENDING,.requestCount=requested,.offset=offset,
        .completeCount=0,.timeStamp=0,
        .options=wrap ? IOUSBHostIsochronousTransactionOptionsWrap : IOUSBHostIsochronousTransactionOptionsNone
    };
    return 0;
}
bool rk3_host_iso_list_snapshot(const RK3HostIsoList *list,size_t index,RK3HostIsoFrame *frame) {
    if(!list || !frame || index>=list->count) return false;
    const volatile IOUSBHostIsochronousTransaction *entry=list->entries+index;
    IOReturn status=entry->status;
    atomic_thread_fence(memory_order_acquire);
    RK3HostIsoFrame result={(int32_t)status,entry->requestCount,entry->completeCount,entry->timeStamp};
    if(status!=entry->status) return false;
    *frame=result;return true;
}

typedef struct {
    RK3HostInterface *owner;
    void *payload;
    void *read_bytes;
    size_t capacity;
    RK3HostBulkCompletion completion;
    void *context;
} BulkJob;
static void complete_bulk(BulkJob *job,int32_t status,size_t actual) {
    RK3HostInterface *owner=job->owner;
    /* Publication can immediately retire job and its final owner reference on
     * an already-running worker. Pin the loop separately before publishing;
     * never dereference owner/job again on this completion thread afterward. */
    CFRunLoopRef worker=(CFRunLoopRef)CFRetain(owner->worker);
    CFRunLoopPerformBlock(worker,kCFRunLoopDefaultMode,^{ @autoreleasepool {
        RK3HostState *state=state_of(owner);if(state->pending) --state->pending;
        size_t copied=actual>job->capacity ? job->capacity : actual;
        int32_t delivered_status=actual>job->capacity ? (int32_t)kIOReturnOverrun : status;
        if(job->read_bytes && copied) {
            NSMutableData *payload=(__bridge NSMutableData *)job->payload;
            memcpy(job->read_bytes,payload.bytes,copied);
        }
        job->completion(job->context,delivered_status,actual);
        (void)CFBridgingRelease(job->payload);free(job);rk3_host_release(owner);
    }});
#ifdef RK3_HOST_USB_TESTING
    if(rk3_host_test_after_publication)rk3_host_test_after_publication();
#endif
    CFRunLoopWakeUp(worker);CFRelease(worker);
}
static int32_t bulk(RK3HostInterface *owner,uint8_t pipe,void *read_bytes,
    const void *write_bytes,size_t length,uint32_t timeout_ms,RK3HostBulkCompletion completion,void *context) { @autoreleasepool {
    if(!length || length>64 || (!read_bytes&&!write_bytes) || !completion) return kIOReturnBadArgument;
    int32_t result=0;IOUSBHostPipe *object=get_pipe(owner,pipe,&result);if(!object) return result;
    RK3HostState *state=state_of(owner);RK3HostPipeProperties properties=state->properties[pipe-1];
    if(properties.transfer_type!=2 || properties.direction!=(read_bytes ? 1 : 0)) return kIOReturnBadArgument;
    @autoreleasepool {
        BulkJob *job=calloc(1,sizeof(*job));if(!job) return kIOReturnNoMemory;
        NSMutableData *payload=read_bytes ? [NSMutableData dataWithLength:length] : [NSMutableData dataWithBytes:write_bytes length:length];
        if(!payload) {free(job);return kIOReturnNoMemory;}
        *job=(BulkJob){owner,(__bridge_retained void *)payload,read_bytes,length,completion,context};
        rk3_host_retain(owner);++state->pending;NSError *failure=nil;
        BOOL accepted=[object enqueueIORequestWithData:payload completionTimeout:timeout_ms/1000.0 error:&failure
            completionHandler:^(IOReturn status,NSUInteger actual){complete_bulk(job,(int32_t)status,actual);}];
        if(!accepted) {
            --state->pending;(void)CFBridgingRelease(job->payload);free(job);rk3_host_release(owner);
            return error_code(failure);
        }
        return 0;
    }
}
}
int32_t rk3_host_bulk_read(RK3HostInterface *owner,uint8_t pipe,void *bytes,size_t capacity,uint32_t timeout_ms,RK3HostBulkCompletion completion,void *context) {
    return bulk(owner,pipe,bytes,NULL,capacity,timeout_ms,completion,context);
}
int32_t rk3_host_bulk_write(RK3HostInterface *owner,uint8_t pipe,const void *bytes,size_t length,uint32_t timeout_ms,RK3HostBulkCompletion completion,void *context) {
    return bulk(owner,pipe,NULL,bytes,length,timeout_ms,completion,context);
}

struct IsoJob {
    RK3HostInterface *owner;
    RK3HostBuffer *data;
    RK3HostIsoList *list;
    size_t first,count;
    RK3HostIsoCompletion completion;
    void *context;
    uint8_t pipe;
    IsoJob *next;
    bool completed;
    int32_t result;
    uint64_t start_frame,debug_raw_tick,debug_worker_tick;
    RK3MeasureRef measurement;
};
static void retire_iso_job(IsoJob *job) {
    for(size_t index=job->first;index<job->first+job->count;++index) job->list->busy[index]=0;
    RK3HostState *state=state_of(job->owner);if(state->pending) --state->pending;
}
static void release_iso_job(IsoJob *job) {
    RK3HostInterface *owner=job->owner;
    measure_drop(job->measurement,MR_WORKER);
    rk3_host_buffer_release(job->data);rk3_host_iso_list_release(job->list);free(job);rk3_host_release(owner);
}
static void append_iso_job(IsoJob *job) {
    RK3HostState *state=state_of(job->owner);unsigned pipe=job->pipe-1;
    if(state->iso_tail[pipe])state->iso_tail[pipe]->next=job;else state->iso_head[pipe]=job;
    state->iso_tail[pipe]=job;
}
static void unlink_iso_job(IsoJob *job) {
    RK3HostState *state=state_of(job->owner);unsigned pipe=job->pipe-1;
    IsoJob **link=&state->iso_head[pipe],*previous=NULL;
    while(*link&&*link!=job){previous=*link;link=&(*link)->next;}
    if(*link){*link=job->next;if(state->iso_tail[pipe]==job)state->iso_tail[pipe]=previous;}
    job->next=NULL;
}
static void deliver_iso_prefix(RK3HostInterface *owner,uint8_t pipe_index) {
    RK3HostState *state=state_of(owner);unsigned pipe=pipe_index-1;
    if(state->iso_delivering[pipe])return;
    /* The final job may release the last mapping/client owner reference. Pin
     * the interface across the whole drain and clear the reentrancy gate before
     * releasing it. Completion blocks are worker-owned, not raw-arrival ordered. */
    rk3_host_retain(owner);state->iso_delivering[pipe]=true;
    while(state->iso_head[pipe]&&state->iso_head[pipe]->completed) {
        IsoJob *job=state->iso_head[pipe];
        state->iso_head[pipe]=job->next;
        if(!state->iso_head[pipe])state->iso_tail[pipe]=NULL;
        job->next=NULL; /* detach before a user callback can reenter enqueue */
        retire_iso_job(job);
        /* [DEBUG-rk3-timing] cheap scalar observation before list reuse. All
         * state stays worker-only; raw framework arrival was captured by value. */
        state->debug_last_iso=(RK3DebugIsoTiming){.pipe_address=state->addresses[pipe],.start_frame=job->start_frame,
            .last_timestamp=job->list->entries[job->first+job->count-1].timeStamp,.raw_tick=job->debug_raw_tick,
            .worker_tick=job->debug_worker_tick,.delivery_tick=mach_absolute_time()};
        measure_mark(job->measurement,MM_D,state->debug_last_iso.delivery_tick);
        measure_pin(job->measurement,MR_LAST); /* worker interest still prevents reuse */
        measure_drop(state->debug_last_record,MR_LAST);state->debug_last_record=job->measurement;
        if(job->measurement.record) {
            state->debug_last_iso.cookie=job->measurement.cookie;
            state->debug_last_iso.interface_cookie=job->measurement.record->interface_cookie;
            state->debug_last_iso.stream_epoch=job->measurement.record->epoch;
        }
        job->completion(job->context,job->result,job->list,job->first,job->count);
        release_iso_job(job);
    }
    state->iso_delivering[pipe]=false;rk3_host_release(owner);
}
/* No cause labels: publication can overlap worker execution. A completed
 * sampled clock span can undercount its callback tail/rearm/preemption. */
static RK3MeasureFault measure_assess(RK3MeasureSnapshot s,RK3MeasureClock clock,uint64_t interface_cookie,
    uint64_t epoch,uint8_t pipe,size_t first,size_t count,uint64_t requested) {
    RK3MeasureFault f={.job=s,.clock=clock};
    f.matched=s.coherent&&s.interface_cookie==interface_cookie&&s.epoch==epoch&&(s.pipe&0x80)&&s.pipe==pipe&&
        s.first==first&&s.count==count&&requested>=64&&s.start==requested-64;
    f.complete=f.matched&&(s.marks&MM_ALL)==MM_ALL;
    f.ordered=f.complete&&s.r<=s.p0&&s.p0<=s.p1&&s.p1<=s.k&&s.p0<=s.w&&s.w<=s.d;
    f.clock.valid=clock.valid&&!clock.active&&!clock.nested&&clock.epoch==epoch&&clock.end>=clock.begin;
    if(f.ordered) {
        f.pre=s.p0-s.r;f.hold=s.d-s.w;
        if(s.w>=s.k) {
            f.post_wait=true;f.post=s.w-s.k;
            if(f.clock.valid) {
                uint64_t low=s.k>clock.begin?s.k:clock.begin,high=s.w<clock.end?s.w:clock.end;
                f.overlap=high>low?high-low:0;f.uncovered=f.post-f.overlap;f.overlap_valid=true;
            }
        }
    }
    return f;
}
/* [DEBUG-rk3-timing] first actual TooOld enqueue rejection per interface.
 * Bus query and log occur only after rejection, never on the steady path. */
static void note_iso_rejection(RK3HostInterface *owner,uint8_t pipe,size_t first,size_t count,
    uint64_t requested,uint64_t before,uint64_t after,int32_t result) {
    RK3HostState *state=state_of(owner);
    if(result!=kIOReturnIsoTooOld||state->debug_too_old_logged)return;
    state->debug_too_old_logged=true;
    uint64_t current=0,host_time=0;
    int32_t query=rk3_host_bus_frame(owner,&current,&host_time);
    RK3DebugIsoTiming last=state->debug_last_iso;
    RK3MeasureSnapshot snapshot=measure_snapshot(state->debug_last_record);
    RK3MeasureFault fault=measure_assess(snapshot,state->debug_clock,state->debug_interface_cookie,
        state->debug_stream_epoch,state->addresses[pipe-1],first,count,requested);
    if(fault.matched){measure_pin(state->debug_last_record,MR_FAULT);state->debug_fault_record=state->debug_last_record;}
    state->debug_fault=fault;
    unsigned validity=(fault.matched?1u:0u)|(fault.complete?2u:0u)|(fault.ordered?4u:0u)|
        (fault.post_wait?8u:0u)|(fault.clock.valid?16u:0u)|(fault.overlap_valid?32u:0u);
    os_log_error(OS_LOG_DEFAULT,"[DEBUG-rk3-timing] TooOld pipe=%{public}u first=%{public}zu count=%{public}zu requested=%{public}llu current=%{public}llu busStatus=%{public}d enqueueBefore=%{public}llu enqueueAfter=%{public}llu lastPipe=%{public}u lastStart=%{public}llu lastTransaction=%{public}llu rawArrival=%{public}llu workerEntry=%{public}llu orderedDelivery=%{public}llu"
        " measureCookie=%{public}llu interfaceEpoch=%{public}llu streamEpoch=%{public}llu engineSeed=%{public}llu measureMarks=%{public}u measureValidity=%{public}u postBefore=%{public}llu postAfter=%{public}llu wakeDone=%{public}llu prePostTicks=%{public}llu postWakeTicks=%{public}llu orderedHoldTicks=%{public}llu clockBegin=%{public}llu clockEndSample=%{public}llu clockOverlapTicks=%{public}llu uncoveredTicks=%{public}llu",
        (unsigned)state->addresses[pipe-1],first,count,(unsigned long long)requested,
        (unsigned long long)current,(int)query,(unsigned long long)before,(unsigned long long)after,
        (unsigned)last.pipe_address,(unsigned long long)last.start_frame,(unsigned long long)last.last_timestamp,
        (unsigned long long)last.raw_tick,(unsigned long long)last.worker_tick,(unsigned long long)last.delivery_tick,
        (unsigned long long)snapshot.cookie,(unsigned long long)snapshot.interface_cookie,(unsigned long long)snapshot.epoch,
        (unsigned long long)state->debug_engine_seed,snapshot.marks,validity,
        (unsigned long long)snapshot.p0,(unsigned long long)snapshot.p1,(unsigned long long)snapshot.k,
        (unsigned long long)fault.pre,(unsigned long long)fault.post,(unsigned long long)fault.hold,
        (unsigned long long)fault.clock.begin,(unsigned long long)fault.clock.end,
        (unsigned long long)fault.overlap,(unsigned long long)fault.uncovered);
}
int32_t rk3_host_iso_enqueue(RK3HostInterface *owner,uint8_t pipe,RK3HostBuffer *data,RK3HostIsoList *list,
    size_t first,size_t count,uint64_t start_frame,RK3HostIsoCompletion completion,void *context) { @autoreleasepool {
    if(!data || !list || data->owner!=owner || list->owner!=owner || !completion || !count ||
       first>=list->count || count>list->count-first) return kIOReturnBadArgument;
    int32_t result=0;IOUSBHostPipe *object=get_pipe(owner,pipe,&result);if(!object) return result;
    RK3HostState *state=state_of(owner);
    if(state->properties[pipe-1].transfer_type!=1) return kIOReturnBadArgument;
    for(size_t index=first;index<first+count;++index) {
        const IOUSBHostIsochronousTransaction *entry=list->entries+index;
        if(list->busy[index]) return kIOReturnBusy;
        uint64_t end=(uint64_t)entry->offset+entry->requestCount;
        bool wrap=(entry->options&IOUSBHostIsochronousTransactionOptionsWrap)!=0;
        if(entry->requestCount>state->properties[pipe-1].max_packet_size || entry->offset>data->size ||
           (entry->requestCount && entry->offset==data->size) || entry->requestCount>data->size ||
           (wrap ? end<=data->size : end>data->size)) return kIOReturnBadArgument;
    }
    IsoJob *job=calloc(1,sizeof(*job));if(!job) return kIOReturnNoMemory;
    *job=(IsoJob){.owner=owner,.data=data,.list=list,.first=first,.count=count,
        .completion=completion,.context=context,.pipe=pipe,.start_frame=start_frame};
    job->measurement=measure_reserve(state->debug_interface_cookie,state->debug_stream_epoch,
        state->addresses[pipe-1],first,count,start_frame);
    buffer_retain(data);list_retain(list);rk3_host_retain(owner);++state->pending;
    for(size_t index=first;index<first+count;++index) list->busy[index]=1;
    append_iso_job(job); /* rejection unlinks; successful submission owns its position */
    NSMutableData *payload=(__bridge NSMutableData *)data->object;NSError *failure=nil;
    uint64_t enqueue_before=mach_absolute_time(); /* temporary [DEBUG-rk3-timing] */
    BOOL accepted=[object enqueueIORequestWithData:payload transactionList:list->entries+first transactionListCount:count
        firstFrameNumber:start_frame options:IOUSBHostIsochronousTransferOptionsNone error:&failure
        completionHandler:^(IOReturn status,IOUSBHostIsochronousTransaction *transactions){
            uint64_t raw_tick=mach_absolute_time(); /* [DEBUG-rk3-timing] captured scalar */
            RK3MeasureRef measurement=job->measurement;
            measure_mark(measurement,MM_R,raw_tick);
            int32_t result=(int32_t)status;
            if(transactions!=job->list->entries+job->first) result=(int32_t)kIOReturnBadArgument;
            /* Same independent-loop pin as complete_bulk: retirement may free
             * every job/owner/mapping reference before publication returns. */
            CFRunLoopRef worker=(CFRunLoopRef)CFRetain(owner->worker);
            measure_mark(measurement,MM_P0,mach_absolute_time());
            CFRunLoopPerformBlock(worker,kCFRunLoopDefaultMode,^{ @autoreleasepool {
                job->debug_worker_tick=mach_absolute_time();job->debug_raw_tick=raw_tick;
                measure_mark(measurement,MM_W,job->debug_worker_tick);
                job->result=result;job->completed=true;
                deliver_iso_prefix(owner,job->pipe);
            }});
#ifdef RK3_HOST_USB_TESTING
            if(rk3_host_test_after_publication)rk3_host_test_after_publication();
#endif
            /* Only process-lifetime telemetry and the cached loop are used
             * here: the worker may already have freed job/owner/list. */
            measure_mark(measurement,MM_P1,mach_absolute_time());
#ifdef RK3_HOST_USB_TESTING
            if(rk3_host_test_after_postmark)rk3_host_test_after_postmark();
#endif
            CFRunLoopWakeUp(worker);
            measure_mark(measurement,MM_K,mach_absolute_time());
            measure_drop(measurement,MR_RAW);CFRelease(worker);
        }];
    uint64_t enqueue_after=mach_absolute_time(); /* temporary [DEBUG-rk3-timing] */
    if(!accepted) {
        int32_t rejected=error_code(failure);
        note_iso_rejection(owner,pipe,first,count,start_frame,enqueue_before,enqueue_after,rejected);
        measure_drop(job->measurement,MR_RAW); /* rejected submissions never callback */
        unlink_iso_job(job);retire_iso_job(job);release_iso_job(job);return rejected;
    }
    return 0;
}
}
