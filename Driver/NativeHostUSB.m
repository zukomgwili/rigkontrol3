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
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define RK3_HOST_PIPE_LIMIT 30
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
}
@end
@implementation RK3HostState
- (void)dealloc { if(interest_worker) CFRelease(interest_worker); }
@end

#ifdef RK3_HOST_USB_TESTING
static id (*rk3_host_test_factory)(io_service_t, dispatch_queue_t, NSError **, IOUSBHostInterestHandler);
static void (*rk3_host_test_after_publication)(void);
#endif
static RK3HostState *state_of(const RK3HostInterface *owner) {
    return owner ? (__bridge RK3HostState *)owner->state : nil;
}
static bool worker_thread(const RK3HostInterface *owner) {
    return owner && owner->worker == CFRunLoopGetCurrent();
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
        CFRunLoopAddSource(worker,owner->keepalive,kCFRunLoopDefaultMode);
        atomic_init(&state->connected,true);
        owner->connected=&state->connected;
        state->interest_worker=(CFRunLoopRef)CFRetain(worker);
        state->queue=dispatch_queue_create("org.rigkontrol3.ni-arm.usb-host",DISPATCH_QUEUE_SERIAL);
        state->pipes=[NSMutableArray new];
        owner->state=(__bridge_retained void *)state;
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

typedef struct {
    RK3HostInterface *owner;
    RK3HostBuffer *data;
    RK3HostIsoList *list;
    size_t first,count;
    RK3HostIsoCompletion completion;
    void *context;
} IsoJob;
static void retire_iso_job(IsoJob *job) {
    for(size_t index=job->first;index<job->first+job->count;++index) job->list->busy[index]=0;
    RK3HostState *state=state_of(job->owner);if(state->pending) --state->pending;
}
static void release_iso_job(IsoJob *job) {
    RK3HostInterface *owner=job->owner;
    rk3_host_buffer_release(job->data);rk3_host_iso_list_release(job->list);free(job);rk3_host_release(owner);
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
    *job=(IsoJob){owner,data,list,first,count,completion,context};
    buffer_retain(data);list_retain(list);rk3_host_retain(owner);++state->pending;
    for(size_t index=first;index<first+count;++index) list->busy[index]=1;
    NSMutableData *payload=(__bridge NSMutableData *)data->object;NSError *failure=nil;
    BOOL accepted=[object enqueueIORequestWithData:payload transactionList:list->entries+first transactionListCount:count
        firstFrameNumber:start_frame options:IOUSBHostIsochronousTransferOptionsNone error:&failure
        completionHandler:^(IOReturn status,IOUSBHostIsochronousTransaction *transactions){
            int32_t result=(int32_t)status;
            if(transactions!=job->list->entries+job->first) result=(int32_t)kIOReturnBadArgument;
            /* Same independent-loop pin as complete_bulk: retirement may free
             * every job/owner/mapping reference before publication returns. */
            CFRunLoopRef worker=(CFRunLoopRef)CFRetain(owner->worker);
            CFRunLoopPerformBlock(worker,kCFRunLoopDefaultMode,^{ @autoreleasepool {
                retire_iso_job(job);
                job->completion(job->context,result,job->list,job->first,job->count);
                release_iso_job(job);
            }});
#ifdef RK3_HOST_USB_TESTING
            if(rk3_host_test_after_publication)rk3_host_test_after_publication();
#endif
            CFRunLoopWakeUp(worker);CFRelease(worker);
        }];
    if(!accepted) {retire_iso_job(job);release_iso_job(job);return error_code(failure);}
    return 0;
}
}
