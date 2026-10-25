/* Hardware-free tests of the real adapter, using Objective-C protocol-shaped
 * fakes behind its private factory seam. No USB service is opened. These tests
 * verify binding references/ownership, not physical DMA late-write visibility. */
#define RK3_HOST_USB_TESTING 1
#include "../NativeHostUSB.m"
#include <assert.h>
#include <stdio.h>

@interface FakeHostRequest : NSObject {
@public
    NSMutableData *payload;
    IOUSBHostCompletionHandler bulk;
    IOUSBHostIsochronousTransactionCompletionHandler iso;
    IOUSBHostIsochronousTransaction *transactions;
    NSUInteger count;
    uint64_t frame;
    NSTimeInterval timeout;
}
@end
@implementation FakeHostRequest
@end
@interface FakeHostPipe : NSObject {
@public
    dispatch_queue_t queue;
    NSMutableArray<FakeHostRequest *> *requests;
    bool fail_enqueue,reverse_aborts;
    int32_t enqueue_error;
    unsigned clears,aborts;
}
- (BOOL)enqueueIORequestWithData:(NSMutableData *)data completionTimeout:(NSTimeInterval)timeout error:(NSError **)error completionHandler:(IOUSBHostCompletionHandler)completion;
- (BOOL)enqueueIORequestWithData:(NSMutableData *)data transactionList:(IOUSBHostIsochronousTransaction *)list transactionListCount:(NSUInteger)count firstFrameNumber:(uint64_t)frame options:(IOUSBHostIsochronousTransferOptions)options error:(NSError **)error completionHandler:(IOUSBHostIsochronousTransactionCompletionHandler)completion;
- (BOOL)abortWithOption:(IOUSBHostAbortOption)option error:(NSError **)error;
- (BOOL)clearStallWithError:(NSError **)error;
@end
@implementation FakeHostPipe
- (instancetype)init {self=[super init];if(self)requests=[NSMutableArray new];return self;}
- (BOOL)enqueueIORequestWithData:(NSMutableData *)data completionTimeout:(NSTimeInterval)seconds error:(NSError **)error completionHandler:(IOUSBHostCompletionHandler)completion {
    if(fail_enqueue) {if(error)*error=[NSError errorWithDomain:IOUSBHostErrorDomain code:(enqueue_error?enqueue_error:kIOReturnNoResources) userInfo:nil];return NO;}
    FakeHostRequest *request=[FakeHostRequest new];request->payload=data;request->bulk=[completion copy];request->timeout=seconds;
    [requests addObject:request];return YES;
}
- (BOOL)enqueueIORequestWithData:(NSMutableData *)data transactionList:(IOUSBHostIsochronousTransaction *)list transactionListCount:(NSUInteger)length firstFrameNumber:(uint64_t)start options:(IOUSBHostIsochronousTransferOptions)options error:(NSError **)error completionHandler:(IOUSBHostIsochronousTransactionCompletionHandler)completion {
    assert(options==IOUSBHostIsochronousTransferOptionsNone);
    if(fail_enqueue) {if(error)*error=[NSError errorWithDomain:IOUSBHostErrorDomain code:(enqueue_error?enqueue_error:kIOReturnNoResources) userInfo:nil];return NO;}
    FakeHostRequest *request=[FakeHostRequest new];request->payload=data;request->iso=[completion copy];request->transactions=list;request->count=length;request->frame=start;
    [requests addObject:request];return YES;
}
- (BOOL)abortWithOption:(IOUSBHostAbortOption)option error:(NSError **)error {
    (void)error;assert(option==IOUSBHostAbortOptionAsynchronous);++aborts;
    NSArray<FakeHostRequest *> *pending=[requests copy];[requests removeAllObjects];
    if(reverse_aborts)pending=[[pending reverseObjectEnumerator] allObjects];
    for(FakeHostRequest *request in pending) dispatch_async(queue,^{
        if(request->bulk) request->bulk(kIOReturnAborted,0);
        else request->iso(kIOReturnAborted,request->transactions);
    });
    return YES;
}
- (BOOL)clearStallWithError:(NSError **)error {(void)error;++clears;return YES;}
@end

typedef struct __attribute__((packed)) {
    IOUSBConfigurationDescriptor configuration;
    IOUSBInterfaceDescriptor alternate0;
    IOUSBEndpointDescriptor bulk0[2];
    IOUSBInterfaceDescriptor alternate1;
    IOUSBEndpointDescriptor all[4];
} FakeHostDescriptors;
@interface FakeHostInterface : NSObject {
@public
    FakeHostDescriptors descriptors;
    uint8_t alternate;
    dispatch_queue_t queue;
    NSMutableDictionary<NSNumber *,FakeHostPipe *> *pipes;
    unsigned destroys,shared_allocations,io_observations,allocations;
    IOUSBHostInterestHandler interest;
    dispatch_queue_t interest_queue;
    bool fail_allocation;
}
@property(readonly) const IOUSBConfigurationDescriptor *configurationDescriptor;
@property(readonly) const IOUSBInterfaceDescriptor *interfaceDescriptor;
- (FakeHostPipe *)copyPipeWithAddress:(NSUInteger)address error:(NSError **)error;
- (BOOL)selectAlternateSetting:(NSUInteger)setting error:(NSError **)error;
- (NSMutableData *)dataWithCapacity:(NSUInteger)capacity options:(IOUSBHostObjectDataOptions)options error:(NSError **)error;
- (NSMutableData *)ioDataWithCapacity:(NSUInteger)capacity error:(NSError **)error;
- (uint64_t)frameNumberWithTime:(IOUSBHostTime *)time;
- (void)destroy;
@end
@implementation FakeHostInterface
- (instancetype)init {
    self=[super init];if(!self)return nil;
    descriptors.configuration=(IOUSBConfigurationDescriptor){9,2,OSSwapHostToLittleInt16(sizeof(descriptors)),1,1,0,0x80,50};
    descriptors.alternate0=(IOUSBInterfaceDescriptor){9,4,0,0,2,0xff,0,0,0};
    descriptors.alternate1=(IOUSBInterfaceDescriptor){9,4,0,1,4,0xff,0,0,0};
    descriptors.bulk0[0]=(IOUSBEndpointDescriptor){7,5,1,2,OSSwapHostToLittleInt16(512),0};
    descriptors.bulk0[1]=(IOUSBEndpointDescriptor){7,5,0x81,2,OSSwapHostToLittleInt16(512),0};
    memcpy(descriptors.all,descriptors.bulk0,sizeof(descriptors.bulk0));
    descriptors.all[2]=(IOUSBEndpointDescriptor){7,5,0x82,1,OSSwapHostToLittleInt16(512),1};
    descriptors.all[3]=(IOUSBEndpointDescriptor){7,5,6,1,OSSwapHostToLittleInt16(512),1};
    pipes=[NSMutableDictionary new];return self;
}
- (const IOUSBConfigurationDescriptor *)configurationDescriptor {++io_observations;return &descriptors.configuration;}
- (const IOUSBInterfaceDescriptor *)interfaceDescriptor {++io_observations;return alternate ? &descriptors.alternate1 : &descriptors.alternate0;}
- (FakeHostPipe *)copyPipeWithAddress:(NSUInteger)address error:(NSError **)error {
    (void)error;++io_observations;NSNumber *key=@(address);FakeHostPipe *pipe=pipes[key];
    if(!pipe) {pipe=[FakeHostPipe new];pipe->queue=queue;pipes[key]=pipe;}
    return pipe;
}
- (BOOL)selectAlternateSetting:(NSUInteger)setting error:(NSError **)error {
    ++io_observations;
    if(setting>1){if(error)*error=[NSError errorWithDomain:IOUSBHostErrorDomain code:kIOReturnBadArgument userInfo:nil];return NO;}
    alternate=(uint8_t)setting;return YES;
}
- (NSMutableData *)dataWithCapacity:(NSUInteger)capacity options:(IOUSBHostObjectDataOptions)options error:(NSError **)error {
    (void)error;++allocations;if(options&IOUSBHostObjectDataOptionsKernelUserShared)++shared_allocations;
    return fail_allocation ? nil : [NSMutableData dataWithLength:capacity];
}
- (NSMutableData *)ioDataWithCapacity:(NSUInteger)capacity error:(NSError **)error {
    return [self dataWithCapacity:capacity options:IOUSBHostObjectDataOptionsNone error:error];
}
- (uint64_t)frameNumberWithTime:(IOUSBHostTime *)time {++io_observations;*time=1234567;return 4567;}
- (void)destroy {++destroys;}
@end
static FakeHostInterface *last_interface;
static bool fail_open;
static id factory(io_service_t service,dispatch_queue_t queue,NSError **error,IOUSBHostInterestHandler interest) {
    assert(service==42);
    if(fail_open){if(error)*error=[NSError errorWithDomain:IOUSBHostErrorDomain code:kIOReturnExclusiveAccess userInfo:nil];return nil;}
    last_interface=[FakeHostInterface new];last_interface->queue=queue;
    last_interface->interest=[interest copy];
    last_interface->interest_queue=dispatch_queue_create("org.rigkontrol3.test-interest",DISPATCH_QUEUE_SERIAL);
    return last_interface;
}
static CFRunLoopRef owner_loop;
static unsigned bulk_calls,iso_calls;
static int32_t delivered_status;
static size_t delivered_actual;
static void bulk_done(void *context,int32_t status,size_t actual) {
    assert(CFRunLoopGetCurrent()==owner_loop&&context==(void *)0x1234);++bulk_calls;delivered_status=status;delivered_actual=actual;
}
static void iso_done(void *context,int32_t status,RK3HostIsoList *list,size_t first,size_t count) {
    assert(CFRunLoopGetCurrent()==owner_loop&&context==(void *)0x5678);++iso_calls;delivered_status=status;
    assert(first==1&&count==2);RK3HostIsoFrame frame;
    assert(rk3_host_iso_list_snapshot(list,first,&frame));
    assert(frame.requested==4&&frame.actual==4&&frame.host_time==7000);
    /* Ownership ends before the client callback, permitting safe requeue. */
    assert(rk3_host_iso_list_set(list,first,4,14,true)==0);
}
static void pump(void) {(void)CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.01,false);}
static void finish_at(FakeHostPipe *pipe,size_t index,int32_t status,size_t actual,bool invalid_pointer) {
    assert(index<pipe->requests.count);FakeHostRequest *request=pipe->requests[index];[pipe->requests removeObjectAtIndex:index];
    dispatch_semaphore_t sent=dispatch_semaphore_create(0);
    dispatch_async(pipe->queue,^{
        assert(CFRunLoopGetCurrent()!=owner_loop);
        if(request->bulk)request->bulk(status,actual);
        else request->iso(status,invalid_pointer ? NULL : request->transactions);
        dispatch_semaphore_signal(sent);
    });
    assert(dispatch_semaphore_wait(sent,dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC))==0);
}
static void finish(FakeHostPipe *pipe,int32_t status,size_t actual,bool invalid_pointer) {
    finish_at(pipe,0,status,actual,invalid_pointer);
}
static RK3HostInterface *open_owner(void) {
    int32_t error=99;RK3HostInterface *owner=rk3_host_open(42,owner_loop,&error);
    assert(owner&&error==0&&rk3_host_connected(owner));return owner;
}
static void test_open_and_properties(void) {
    int32_t error=0;assert(!rk3_host_open(0,owner_loop,&error)&&error==kIOReturnBadArgument);
    fail_open=true;assert(!rk3_host_open(42,owner_loop,&error)&&error==kIOReturnExclusiveAccess);fail_open=false;
    RK3HostInterface *owner=open_owner();uint8_t count=0,alternate=9;
    assert(rk3_host_get_alt(owner,&alternate)==0&&alternate==0);
    assert(rk3_host_num_pipes(owner,&count)==0&&count==2);
    assert(rk3_host_set_alt(owner,1)==0&&rk3_host_num_pipes(owner,&count)==0&&count==4);
    RK3HostPipeProperties properties;
    assert(rk3_host_pipe_properties(owner,3,&properties)==0&&properties.direction==1&&properties.number==2&&properties.transfer_type==1&&properties.max_packet_size==512&&properties.interval==1);
    assert(rk3_host_pipe_properties(owner,0,&properties)==kIOReturnBadArgument);
    assert(rk3_host_set_alt(owner,2)==kIOReturnBadArgument);
    uint64_t frame=0,time=0;assert(rk3_host_bus_frame(owner,&frame,&time)==0&&frame==4567&&time==1234567);
    assert(rk3_host_clear_stall(owner,2)==0&&last_interface->pipes[@0x81]->clears==1);
    assert(rk3_host_close(owner)==0&&last_interface->destroys==1&&!rk3_host_connected(owner));
    assert(rk3_host_get_alt(owner,&alternate)==kIOReturnNotOpen&&rk3_host_close(owner)==0);
    rk3_host_release(owner);
}
static void test_bulk_and_abort(void) {
    RK3HostInterface *owner=open_owner();unsigned previous=bulk_calls;uint8_t bytes[64];memset(bytes,0xa5,sizeof(bytes));
    assert(rk3_host_bulk_read(owner,2,bytes,sizeof(bytes),1250,bulk_done,(void *)0x1234)==0);
    FakeHostPipe *pipe=last_interface->pipes[@0x81];FakeHostRequest *request=pipe->requests[0];
    assert(request->timeout==1.25&&rk3_host_pending(owner)==1&&rk3_host_close(owner)==kIOReturnBusy);
    assert(rk3_host_set_alt(owner,1)==kIOReturnBusy);
    const uint8_t sample[]={4,0x10,0};memcpy(request->payload.mutableBytes,sample,sizeof(sample));
    finish(pipe,0,sizeof(sample),false);assert(bulk_calls==previous&&bytes[0]==0xa5);pump();
    assert(bulk_calls==previous+1&&delivered_status==0&&delivered_actual==3&&memcmp(bytes,sample,3)==0&&bytes[3]==0xa5&&rk3_host_pending(owner)==0);
    assert(rk3_host_bulk_write(owner,1,sample,3,500,bulk_done,(void *)0x1234)==0);
    assert(rk3_host_bulk_write(owner,2,sample,3,500,bulk_done,(void *)0x1234)==kIOReturnBadArgument);
    assert(rk3_host_abort(owner,1)==0);pump();assert(delivered_status==kIOReturnAborted&&rk3_host_pending(owner)==0);
    pipe=last_interface->pipes[@1];pipe->fail_enqueue=true;
    assert(rk3_host_bulk_write(owner,1,sample,3,0,bulk_done,(void *)0x1234)==kIOReturnNoResources&&rk3_host_pending(owner)==0);
    assert(rk3_host_bulk_read(owner,2,bytes,sizeof(bytes),0,bulk_done,(void *)0x1234)==0);
    pipe=last_interface->pipes[@0x81];finish(pipe,0,65,false);pump();assert(delivered_status==kIOReturnOverrun&&delivered_actual==65);
    assert(rk3_host_close(owner)==0);rk3_host_release(owner);
}
static void test_shared_iso_and_lifetime(void) {
    RK3HostInterface *owner=open_owner();assert(rk3_host_set_alt(owner,1)==0);
    int32_t error=0;RK3HostBuffer *data=rk3_host_buffer_create(owner,16,&error);
    RK3HostIsoList *list=rk3_host_iso_list_create(owner,4,&error);assert(data&&list&&error==0);
    if(rk3_host_primary_lists_supported())assert(last_interface->shared_allocations==1);
    assert(rk3_host_buffer_size(data)==16&&rk3_host_iso_list_count(list)==4);
    assert(rk3_host_close(owner)==kIOReturnBusy&&rk3_host_set_alt(owner,0)==kIOReturnBusy);
    assert(rk3_host_iso_list_set(list,1,4,14,false)==0);
    assert(rk3_host_iso_enqueue(owner,4,data,list,1,2,1100,iso_done,(void *)0x5678)==kIOReturnBadArgument);
    assert(rk3_host_iso_list_set(list,1,4,14,true)==0&&rk3_host_iso_list_set(list,2,4,2,false)==0);
    assert(rk3_host_iso_enqueue(owner,4,data,list,1,2,1100,iso_done,(void *)0x5678)==0);
    FakeHostPipe *pipe=last_interface->pipes[@6];FakeHostRequest *request=pipe->requests[0];
    assert(request->payload.mutableBytes==rk3_host_buffer_bytes(data)&&request->transactions==list->entries+1&&request->count==2&&request->frame==1100);
    assert(request->transactions[0].offset==14&&(request->transactions[0].options&IOUSBHostIsochronousTransactionOptionsWrap));
    /* A post-enqueue CPU update remains in the same object handed to Apple;
     * this verifies that the binding introduces no snapshot, not actual DMA. */
    ((uint8_t *)rk3_host_buffer_bytes(data))[14]=0x55;assert(((uint8_t *)request->payload.bytes)[14]==0x55);
    assert(rk3_host_iso_list_set(list,1,4,14,true)==kIOReturnBusy);
    assert(rk3_host_iso_enqueue(owner,4,data,list,1,2,1101,iso_done,(void *)0x5678)==kIOReturnBusy);
    request->transactions[0].status=0;request->transactions[0].completeCount=4;request->transactions[0].timeStamp=7000;
    RK3HostIsoFrame snapshot;assert(rk3_host_iso_list_snapshot(list,1,&snapshot)&&snapshot.status==0&&snapshot.actual==4&&snapshot.host_time==7000);
    unsigned previous=iso_calls;
    rk3_host_buffer_release(data);rk3_host_iso_list_release(list);
    assert(rk3_host_close(owner)==kIOReturnBusy);
    finish(pipe,0,0,false);assert(iso_calls==previous);pump();
    assert(iso_calls==previous+1&&delivered_status==0&&rk3_host_pending(owner)==0);
    assert(rk3_host_close(owner)==0);rk3_host_release(owner);
}
static void close_in_bulk_done(void *context,int32_t status,size_t actual) {
    RK3HostInterface *owner=context;assert(status==0&&actual==1&&CFRunLoopGetCurrent()==owner_loop);
    assert(rk3_host_pending(owner)==0&&rk3_host_close(owner)==0);++bulk_calls;
}
static void test_owner_retained_through_delivery(void) {
    RK3HostInterface *owner=open_owner();uint8_t bytes[1];unsigned previous=bulk_calls;
    assert(rk3_host_bulk_read(owner,2,bytes,1,0,close_in_bulk_done,owner)==0);
    FakeHostPipe *pipe=last_interface->pipes[@0x81];
    rk3_host_release(owner); /* The queued job now owns the only reference. */
    finish(pipe,0,1,false);pump();
    assert(bulk_calls==previous+1&&last_interface->destroys>=1);
}
static void test_rejected_iso_and_allocation(void) {
    RK3HostInterface *owner=open_owner();assert(rk3_host_set_alt(owner,1)==0);
    int32_t error=0;assert(!rk3_host_buffer_create(owner,0,&error)&&error==kIOReturnBadArgument);
    last_interface->fail_allocation=true;assert(!rk3_host_buffer_create(owner,16,&error)&&error==kIOReturnNoMemory);last_interface->fail_allocation=false;
    RK3HostBuffer *data=rk3_host_buffer_create(owner,16,&error);RK3HostIsoList *list=rk3_host_iso_list_create(owner,4,&error);
    assert(rk3_host_iso_list_set(list,1,4,1,true)==0&&rk3_host_iso_list_set(list,2,4,2,false)==0);
    assert(rk3_host_iso_enqueue(owner,4,data,list,1,2,1100,iso_done,(void *)0x5678)==kIOReturnBadArgument);
    assert(rk3_host_iso_list_set(list,1,4,14,true)==0);
    FakeHostPipe *pipe=last_interface->pipes[@6];assert(pipe);pipe->fail_enqueue=true;
    assert(rk3_host_iso_enqueue(owner,4,data,list,1,2,1100,iso_done,(void *)0x5678)==kIOReturnNoResources&&rk3_host_pending(owner)==0);
    assert(rk3_host_iso_list_set(list,1,4,14,true)==0);
    rk3_host_buffer_release(data);rk3_host_iso_list_release(list);assert(rk3_host_close(owner)==0);rk3_host_release(owner);
}
typedef struct {unsigned id;int32_t status;size_t first,count;} OrderedContext;
static unsigned ordered_ids[16],ordered_calls;
static void ordered_done(void *context,int32_t status,RK3HostIsoList *list,size_t first,size_t count) {
    OrderedContext *expected=context;
    assert(CFRunLoopGetCurrent()==owner_loop&&list&&status==expected->status&&first==expected->first&&count==expected->count);
    assert(ordered_calls<sizeof(ordered_ids)/sizeof(ordered_ids[0]));
    ordered_ids[ordered_calls++]=expected->id;
}
static void test_iso_callbacks_follow_acceptance_per_pipe(void) {
    RK3HostInterface *owner=open_owner();assert(rk3_host_set_alt(owner,1)==0);int32_t error=0;
    RK3HostBuffer *data=rk3_host_buffer_create(owner,16,&error);
    RK3HostIsoList *list=rk3_host_iso_list_create(owner,4,&error);assert(data&&list);
    for(size_t index=0;index<4;++index)assert(rk3_host_iso_list_set(list,index,4,(uint32_t)(index*4),false)==0);
    OrderedContext contexts[]={{1,0,0,1},{2,kIOReturnUnderrun,1,1},{10,0,2,1},{3,0,3,1}};
    ordered_calls=0;
    for(unsigned index=0;index<4;++index)
        assert(rk3_host_iso_enqueue(owner,index==2?3:4,data,list,index,1,1100+index,ordered_done,contexts+index)==0);
    FakeHostPipe *out=last_interface->pipes[@6],*in=last_interface->pipes[@0x82];
    finish_at(out,1,kIOReturnUnderrun,0,false);pump();
    assert(ordered_calls==0&&rk3_host_pending(owner)==4);
    assert(rk3_host_iso_list_set(list,1,4,4,false)==kIOReturnBusy);
    finish(in,0,0,false);pump();
    assert(ordered_calls==1&&ordered_ids[0]==10&&rk3_host_pending(owner)==3); /* endpoints independent */
    finish_at(out,1,0,0,false);pump();assert(ordered_calls==1&&rk3_host_pending(owner)==3);
    finish(out,0,0,false);pump();
    assert(ordered_calls==4&&ordered_ids[1]==1&&ordered_ids[2]==2&&ordered_ids[3]==3&&rk3_host_pending(owner)==0);
    rk3_host_iso_list_release(list);rk3_host_buffer_release(data);assert(rk3_host_close(owner)==0);rk3_host_release(owner);
}
static void test_rejected_iso_has_no_ordered_position(void) {
    RK3HostInterface *owner=open_owner();assert(rk3_host_set_alt(owner,1)==0);int32_t error=0;
    RK3HostBuffer *data=rk3_host_buffer_create(owner,16,&error);
    RK3HostIsoList *list=rk3_host_iso_list_create(owner,3,&error);assert(data&&list);
    for(size_t index=0;index<3;++index)assert(rk3_host_iso_list_set(list,index,4,(uint32_t)(index*4),false)==0);
    OrderedContext contexts[]={{1,0,0,1},{2,0,1,1},{3,0,2,1}};ordered_calls=0;
    assert(rk3_host_iso_enqueue(owner,4,data,list,0,1,1100,ordered_done,contexts)==0);
    FakeHostPipe *pipe=last_interface->pipes[@6];pipe->fail_enqueue=true;
    assert(rk3_host_iso_enqueue(owner,4,data,list,1,1,1101,ordered_done,contexts+1)==kIOReturnNoResources);
    assert(rk3_host_pending(owner)==1&&rk3_host_iso_list_set(list,1,4,4,false)==0);
    pipe->fail_enqueue=false;
    assert(rk3_host_iso_enqueue(owner,4,data,list,2,1,1102,ordered_done,contexts+2)==0);
    finish_at(pipe,1,0,0,false);pump();assert(ordered_calls==0&&rk3_host_pending(owner)==2);
    finish(pipe,0,0,false);pump();assert(ordered_calls==2&&ordered_ids[0]==1&&ordered_ids[1]==3);
    /* Rejection when the provisional job is the only head must unlink tail too. */
    pipe->fail_enqueue=true;
    assert(rk3_host_iso_list_set(list,0,4,0,false)==0);
    assert(rk3_host_iso_enqueue(owner,4,data,list,0,1,1103,ordered_done,contexts)==kIOReturnNoResources);
    pipe->fail_enqueue=false;
    assert(rk3_host_iso_enqueue(owner,4,data,list,1,1,1104,ordered_done,contexts+1)==0);
    finish(pipe,0,0,false);pump();assert(ordered_calls==3&&ordered_ids[2]==2&&rk3_host_pending(owner)==0);
    rk3_host_iso_list_release(list);rk3_host_buffer_release(data);assert(rk3_host_close(owner)==0);rk3_host_release(owner);
}
static RK3HostInterface *reentrant_owner;
static RK3HostBuffer *reentrant_data;
static OrderedContext reentrant_third={3,0,0,1};
static void reentrant_ordered_done(void *context,int32_t status,RK3HostIsoList *list,size_t first,size_t count) {
    ordered_done(context,status,list,first,count);OrderedContext *expected=context;
    if(expected->id!=1)return;
    assert(rk3_host_iso_list_set(list,first,4,0,false)==0); /* retired before callback */
    assert(rk3_host_iso_enqueue(reentrant_owner,4,reentrant_data,list,first,1,1102,ordered_done,&reentrant_third)==0);
    finish(last_interface->pipes[@6],0,0,false);pump();
    assert(ordered_calls==1); /* nested worker entry cannot recurse into later callbacks */
}
static void test_reentrant_iso_enqueue_and_nested_delivery(void) {
    RK3HostInterface *owner=open_owner();assert(rk3_host_set_alt(owner,1)==0);int32_t error=0;
    RK3HostBuffer *data=rk3_host_buffer_create(owner,16,&error);
    RK3HostIsoList *list=rk3_host_iso_list_create(owner,2,&error);assert(data&&list);
    assert(rk3_host_iso_list_set(list,0,4,0,false)==0&&rk3_host_iso_list_set(list,1,4,4,false)==0);
    OrderedContext contexts[]={{1,0,0,1},{2,0,1,1}};ordered_calls=0;reentrant_owner=owner;reentrant_data=data;
    assert(rk3_host_iso_enqueue(owner,4,data,list,0,1,1100,reentrant_ordered_done,contexts)==0);
    assert(rk3_host_iso_enqueue(owner,4,data,list,1,1,1101,ordered_done,contexts+1)==0);
    FakeHostPipe *pipe=last_interface->pipes[@6];finish_at(pipe,1,0,0,false);pump();assert(ordered_calls==0);
    finish(pipe,0,0,false);pump();
    assert(ordered_calls==3&&ordered_ids[0]==1&&ordered_ids[1]==2&&ordered_ids[2]==3&&rk3_host_pending(owner)==0);
    reentrant_owner=NULL;reentrant_data=NULL;
    rk3_host_iso_list_release(list);rk3_host_buffer_release(data);assert(rk3_host_close(owner)==0);rk3_host_release(owner);
}
static void test_reverse_abort_retains_until_ordered_drain(void) {
    __weak RK3HostState *released_state;ordered_calls=0;
    OrderedContext contexts[]={{1,kIOReturnAborted,0,1},{2,kIOReturnAborted,1,1},{3,kIOReturnAborted,2,1}};
    @autoreleasepool {
        RK3HostInterface *owner=open_owner();released_state=state_of(owner);
        assert(rk3_host_set_alt(owner,1)==0);int32_t error=0;
        RK3HostBuffer *data=rk3_host_buffer_create(owner,16,&error);
        RK3HostIsoList *list=rk3_host_iso_list_create(owner,3,&error);assert(data&&list);
        for(size_t index=0;index<3;++index) {
            assert(rk3_host_iso_list_set(list,index,4,(uint32_t)(index*4),false)==0);
            assert(rk3_host_iso_enqueue(owner,4,data,list,index,1,1100+index,ordered_done,contexts+index)==0);
        }
        last_interface->pipes[@6]->reverse_aborts=true;
        assert(rk3_host_abort(owner,4)==0);
        rk3_host_iso_list_release(list);rk3_host_buffer_release(data);rk3_host_release(owner);
        /* Accepted aborted requests now own every interface/mapping reference. */
        pump();assert(ordered_calls==3&&ordered_ids[0]==1&&ordered_ids[1]==2&&ordered_ids[2]==3);
    }
    assert(!released_state&&last_interface->destroys>=1);
}
static void test_zero_count_request_is_ordered_with_payload(void) {
    RK3HostInterface *owner=open_owner();assert(rk3_host_set_alt(owner,1)==0);int32_t error=0;
    RK3HostBuffer *data=rk3_host_buffer_create(owner,16,&error);
    RK3HostIsoList *list=rk3_host_iso_list_create(owner,9,&error);assert(data&&list);
    for(size_t index=0;index<8;++index)assert(rk3_host_iso_list_set(list,index,0,8,false)==0);
    assert(rk3_host_iso_list_set(list,8,4,0,false)==0);
    OrderedContext contexts[]={{1,0,0,8},{2,0,8,1}};ordered_calls=0;
    assert(rk3_host_iso_enqueue(owner,4,data,list,0,8,1100,ordered_done,contexts)==0);
    assert(rk3_host_iso_enqueue(owner,4,data,list,8,1,1101,ordered_done,contexts+1)==0);
    FakeHostPipe *pipe=last_interface->pipes[@6];finish_at(pipe,1,0,0,false);pump();
    assert(ordered_calls==0&&rk3_host_pending(owner)==2&&rk3_host_close(owner)==kIOReturnBusy);
    rk3_host_iso_list_release(list);rk3_host_buffer_release(data);
    finish(pipe,0,0,false);pump();assert(ordered_calls==2&&ordered_ids[0]==1&&ordered_ids[1]==2&&rk3_host_pending(owner)==0);
    assert(rk3_host_close(owner)==0);rk3_host_release(owner);
}
static void test_first_too_old_observation_preserves_order_and_is_bounded(void) {
    RK3HostInterface *owner=open_owner();assert(rk3_host_set_alt(owner,1)==0);int32_t error=0;
    rk3_host_measurement_begin_stream(owner,1);
    RK3HostBuffer *data=rk3_host_buffer_create(owner,16,&error);
    RK3HostIsoList *list=rk3_host_iso_list_create(owner,1,&error);assert(data&&list);
    assert(rk3_host_iso_list_set(list,0,4,0,false)==0);
    OrderedContext context={1,0,0,1};ordered_calls=0;
    assert(rk3_host_iso_enqueue(owner,4,data,list,0,1,1100,ordered_done,&context)==0);
    FakeHostPipe *pipe=last_interface->pipes[@6];pipe->requests[0]->transactions[0].timeStamp=12345;
    finish(pipe,0,0,false);pump();
    RK3HostState *state=state_of(owner);RK3DebugIsoTiming last=state->debug_last_iso;
    assert(last.cookie&&last.interface_cookie&&last.stream_epoch);
    assert(last.pipe_address==6&&last.start_frame==1100&&last.last_timestamp==12345);
    assert(last.raw_tick&&last.raw_tick<=last.worker_tick&&last.worker_tick<=last.delivery_tick);
    assert(!state->debug_too_old_logged&&ordered_calls==1&&rk3_host_pending(owner)==0);
    pipe->fail_enqueue=true;pipe->enqueue_error=kIOReturnIsoTooOld;
    unsigned observations=last_interface->io_observations;
    assert(rk3_host_iso_list_set(list,0,4,0,false)==0);
    assert(rk3_host_iso_enqueue(owner,4,data,list,0,1,1101,ordered_done,&context)==kIOReturnIsoTooOld);
    assert(state->debug_too_old_logged&&last_interface->io_observations==observations+1);
    assert(rk3_host_iso_enqueue(owner,4,data,list,0,1,1102,ordered_done,&context)==kIOReturnIsoTooOld);
    assert(last_interface->io_observations==observations+1&&ordered_calls==1&&rk3_host_pending(owner)==0);
    assert(state->debug_last_iso.start_frame==1100); /* rejection is not a delivered job */
    rk3_host_iso_list_release(list);rk3_host_buffer_release(data);assert(rk3_host_close(owner)==0);rk3_host_release(owner);
}
static void zero_iso_done(void *context,int32_t status,RK3HostIsoList *list,size_t first,size_t count) {
    RK3HostInterface *owner=context;
    assert(CFRunLoopGetCurrent()==owner_loop&&status==0&&first==0&&count==8&&rk3_host_pending(owner)==0);
    for(size_t index=0;index<count;++index) {
        RK3HostIsoFrame frame;assert(rk3_host_iso_list_snapshot(list,index,&frame));
        assert(frame.status==0&&frame.requested==0&&frame.actual==0);
        assert(rk3_host_iso_list_set(list,index,0,8,false)==0);
    }
    ++iso_calls;
}
static void test_zero_count_iso_transactions(void) {
    RK3HostInterface *owner=open_owner();assert(rk3_host_set_alt(owner,1)==0);int32_t error=0;
    RK3HostBuffer *data=rk3_host_buffer_create(owner,16,&error);
    RK3HostIsoList *list=rk3_host_iso_list_create(owner,8,&error);assert(data&&list);
    for(size_t index=0;index<8;++index)assert(rk3_host_iso_list_set(list,index,0,8,false)==0);
    unsigned previous=iso_calls;
    assert(rk3_host_iso_enqueue(owner,4,data,list,0,8,1100,zero_iso_done,owner)==0);
    FakeHostPipe *pipe=last_interface->pipes[@6];FakeHostRequest *request=pipe->requests[0];
    assert(request->count==8&&request->payload.length==16&&request->payload.mutableBytes==rk3_host_buffer_bytes(data));
    for(size_t index=0;index<8;++index) {
        IOUSBHostIsochronousTransaction *transaction=request->transactions+index;
        assert(transaction->requestCount==0&&transaction->offset==8&&transaction->options==IOUSBHostIsochronousTransactionOptionsNone);
        transaction->status=0;transaction->completeCount=0;
    }
    assert(rk3_host_pending(owner)==1&&rk3_host_close(owner)==kIOReturnBusy);
    assert(rk3_host_iso_list_set(list,0,0,8,false)==kIOReturnBusy);
    finish(pipe,0,0,false);assert(iso_calls==previous&&rk3_host_pending(owner)==1);pump();
    assert(iso_calls==previous+1&&rk3_host_pending(owner)==0);
    rk3_host_iso_list_release(list);rk3_host_buffer_release(data);
    assert(rk3_host_close(owner)==0);rk3_host_release(owner);
}
static dispatch_semaphore_t publication_reached,retirement_finished;
static void pause_before_wake(void) {
    dispatch_semaphore_signal(publication_reached);
    assert(dispatch_semaphore_wait(retirement_finished,dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC))==0);
}
static void early_iso_done(void *context,int32_t status,RK3HostIsoList *list,size_t first,size_t count) {
    assert(context==(void *)0x5678&&status==0&&list&&first==1&&count==2&&CFRunLoopGetCurrent()==owner_loop);
    ++iso_calls;
}
static void test_retirement_before_completion_wake(void) {
    for(unsigned kind=0;kind<3;++kind) {
        bool iso=kind!=0,after_post=kind==2;
        RK3HostInterface *owner=open_owner();FakeHostInterface *fake=last_interface;
        RK3MeasureRef measurement={0};
        __weak RK3HostState *released_state;
        unsigned previous=iso?iso_calls:bulk_calls;
        @autoreleasepool {
            released_state=state_of(owner);
            if(iso) {
                assert(rk3_host_set_alt(owner,1)==0);int32_t error=0;
                rk3_host_measurement_begin_stream(owner,1);
                RK3HostBuffer *data=rk3_host_buffer_create(owner,16,&error);
                RK3HostIsoList *list=rk3_host_iso_list_create(owner,4,&error);assert(data&&list);
                assert(rk3_host_iso_list_set(list,1,4,0,false)==0&&rk3_host_iso_list_set(list,2,4,4,false)==0);
                assert(rk3_host_iso_enqueue(owner,4,data,list,1,2,1100,early_iso_done,(void *)0x5678)==0);
                measurement=state_of(owner)->iso_tail[3]->measurement;assert(measurement.record);
                rk3_host_buffer_release(data);rk3_host_iso_list_release(list);
            } else {
                const uint8_t sample=42;
                assert(rk3_host_bulk_write(owner,1,&sample,1,0,close_in_bulk_done,owner)==0);
            }
            rk3_host_release(owner); /* only job/mapping references remain */
        }
        FakeHostPipe *pipe=fake->pipes[iso?@6:@1];
        FakeHostRequest *request=pipe->requests[0];[pipe->requests removeObjectAtIndex:0];
        publication_reached=dispatch_semaphore_create(0);retirement_finished=dispatch_semaphore_create(0);
        dispatch_semaphore_t callback_returned=dispatch_semaphore_create(0);
        rk3_host_test_after_publication=after_post?NULL:pause_before_wake;
        rk3_host_test_after_postmark=after_post?pause_before_wake:NULL;
        dispatch_async(pipe->queue,^{
            if(iso)request->iso(0,request->transactions);else request->bulk(0,1);
            dispatch_semaphore_signal(callback_returned);
        });
        assert(dispatch_semaphore_wait(publication_reached,dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC))==0);
        pump(); /* worker retires/frees owner while completion is paused before WakeUp */
        assert((iso?iso_calls:bulk_calls)==previous+1&&fake->destroys>=1&&!released_state);
        if(iso) {
            RK3MeasureSnapshot pending=measure_snapshot(measurement);
            assert(pending.coherent&&(pending.marks&(MM_W|MM_D))==(MM_W|MM_D));
            assert(!(pending.marks&MM_K)&&!!(pending.marks&MM_P1)==after_post);
            assert((atomic_load(&measurement.record->life)&15)==MR_RAW);
        }
        dispatch_semaphore_signal(retirement_finished);
        assert(dispatch_semaphore_wait(callback_returned,dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC))==0);
        rk3_host_test_after_publication=NULL;
        rk3_host_test_after_postmark=NULL;
        if(iso)assert(atomic_load(&measurement.record->life)==0);
    }
}
static void send_interest(IOUSBHostInterestHandler interest,dispatch_queue_t queue,uint32_t message) {
    dispatch_semaphore_t sent=dispatch_semaphore_create(0);
    dispatch_async(queue,^{
        assert(CFRunLoopGetCurrent()!=owner_loop);
        interest((IOUSBHostObject *)last_interface,message,NULL);
        dispatch_semaphore_signal(sent);
    });
    assert(dispatch_semaphore_wait(sent,dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC))==0);
}
static void test_removal_interest_and_late_delivery(void) {
    assert(!rk3_host_connected(NULL));
    RK3HostInterface *owner=open_owner();
    FakeHostInterface *fake=last_interface;
    const unsigned observations=fake->io_observations,allocations=fake->allocations;
    for(unsigned attempt=0;attempt<1000;++attempt)assert(rk3_host_connected(owner));
    assert(fake->io_observations==observations&&fake->allocations==allocations);
    const uint32_t non_removal[]={kIOMessageServiceIsSuspended,kIOMessageServiceIsResumed,
        kIOMessageDeviceWillPowerOff,kIOMessageDeviceHasPoweredOn,
        kIOMessageServiceIsRequestingClose,kIOMessageServicePropertyChange,UINT32_MAX};
    for(size_t index=0;index<sizeof(non_removal)/sizeof(non_removal[0]);++index) {
        send_interest(fake->interest,fake->interest_queue,non_removal[index]);
        assert(rk3_host_connected(owner));
    }
    send_interest(fake->interest,fake->interest_queue,kIOMessageServiceIsTerminated);
    assert(!rk3_host_connected(owner));
    send_interest(fake->interest,fake->interest_queue,kIOMessageServiceIsResumed);
    assert(!rk3_host_connected(owner)); /* a dead service cannot resume this owner */
    assert(rk3_host_close(owner)==0);rk3_host_release(owner);
    IOUSBHostInterestHandler late_interest;
    dispatch_queue_t late_queue;
    __weak RK3HostState *released_state;
    @autoreleasepool {
        RK3HostInterface *released_owner=open_owner();
        released_state=state_of(released_owner);
        late_interest=[last_interface->interest copy];late_queue=last_interface->interest_queue;
        rk3_host_release(released_owner); /* destroy de-registers, but exercise an already queued event */
    }
    assert(!released_state); /* neither handler nor fake host retains adapter state */
    send_interest(late_interest,late_queue,kIOMessageServiceIsTerminated);
    send_interest(late_interest,late_queue,kIOMessageServiceIsResumed);
}
static void test_measurement_interpretation(void) {
    RK3MeasureSnapshot s={.cookie=1,.interface_cookie=2,.epoch=3,.pipe=0x82,.first=8,.count=8,.start=1000,
        .r=100,.p0=200,.p1=300,.k=400,.w=1000,.d=1010,.marks=MM_ALL,.coherent=true};
    RK3MeasureClock c={.begin=500,.end=800,.epoch=3,.valid=true};
    RK3MeasureFault f=measure_assess(s,c,2,3,0x82,8,8,1064);
    assert(f.matched&&f.complete&&f.ordered&&f.post_wait&&f.overlap_valid);
    assert(f.pre==100&&f.post==600&&f.hold==10&&f.overlap==300&&f.uncovered==300);
    s.w=250;s.d=260;f=measure_assess(s,c,2,3,0x82,8,8,1064);
    assert(f.ordered&&!f.post_wait&&!f.overlap_valid&&f.post==0); /* W before P+ is legal overlap. */
    s.marks&=~MM_P1;f=measure_assess(s,c,2,3,0x82,8,8,1064);
    assert(f.matched&&!f.complete&&!f.ordered); /* Old R/W/D-only evidence is inconclusive. */
    s.marks=MM_READY|MM_R|MM_W|MM_D;f=measure_assess(s,c,2,3,0x82,8,8,1064);assert(!f.complete);
    s.marks=MM_ALL;s.coherent=false;f=measure_assess(s,c,2,3,0x82,8,8,1064);assert(!f.matched);
    s.coherent=true;s.p1=150;f=measure_assess(s,c,2,3,0x82,8,8,1064);assert(!f.ordered);
    s.p1=300;s.w=1000;s.d=1010;c.active=true;c.valid=false;
    f=measure_assess(s,c,2,3,0x82,8,8,1064);assert(f.post_wait&&!f.overlap_valid);
    c.active=false;c.valid=true;c.begin=1100;c.end=1200;
    f=measure_assess(s,c,2,3,0x82,8,8,1064);assert(f.overlap_valid&&!f.overlap&&f.uncovered==600);
    assert(!measure_assess(s,c,2,4,0x82,8,8,1064).matched);
    assert(!measure_assess(s,c,9,3,0x82,8,8,1064).matched);
    assert(!measure_assess(s,c,2,3,0x82,16,8,1064).matched);
    assert(!measure_assess(s,c,2,3,0x82,8,8,1065).matched);
    assert(!measure_assess(s,c,2,3,6,8,8,1064).matched);
}
static void test_measurement_bank_lifetimes_and_budget(void) {
    assert(sizeof(measure_bank)<=65536);
    for(unsigned raw_first=0;raw_first<2;++raw_first) {
        RK3MeasureRef ref=measure_reserve(17,1,0x82,0,8,1100);assert(ref.record);
        measure_pin(ref,MR_LAST);measure_pin(ref,MR_FAULT);
        measure_drop(ref,raw_first?MR_RAW:MR_WORKER);
        measure_drop(ref,raw_first?MR_WORKER:MR_RAW);
        assert((atomic_load(&ref.record->life)&15)==(MR_LAST|MR_FAULT));
        measure_drop(ref,MR_LAST);assert((atomic_load(&ref.record->life)&15)==MR_FAULT);
        measure_drop(ref,MR_FAULT);assert(atomic_load(&ref.record->life)==0);
        assert(!measure_snapshot(ref).coherent);
    }
    RK3MeasureRef refs[MEASURE_SLOTS];
    for(unsigned i=0;i<MEASURE_SLOTS;++i){refs[i]=measure_reserve(17,2,0x82,0,8,1100+i);assert(refs[i].record);}
    assert(!measure_reserve(17,2,0x82,0,8,9999).record); /* one attempt, no wait/scan */
    /* Telemetry exhaustion cannot reject an otherwise accepted real-adapter
     * submission, retain USB resources longer, or suppress its completion. */
    RK3HostInterface *owner=open_owner();assert(rk3_host_set_alt(owner,1)==0);rk3_host_measurement_begin_stream(owner,1);
    int32_t error=0;RK3HostBuffer *data=rk3_host_buffer_create(owner,16,&error);
    RK3HostIsoList *list=rk3_host_iso_list_create(owner,1,&error);assert(data&&list);
    assert(rk3_host_iso_list_set(list,0,4,0,false)==0);OrderedContext context={1,0,0,1};ordered_calls=0;
    assert(rk3_host_iso_enqueue(owner,3,data,list,0,1,1100,ordered_done,&context)==0);
    finish(last_interface->pipes[@0x82],0,0,false);pump();
    assert(ordered_calls==1&&!state_of(owner)->debug_last_iso.cookie&&rk3_host_pending(owner)==0);
    rk3_host_iso_list_release(list);rk3_host_buffer_release(data);assert(rk3_host_close(owner)==0);rk3_host_release(owner);
    for(unsigned i=0;i<MEASURE_SLOTS;++i){measure_drop(refs[i],MR_RAW);measure_drop(refs[i],MR_WORKER);}
    RK3MeasureRef reused=measure_reserve(17,3,0x82,0,8,10000);assert(reused.record);
    for(unsigned i=0;i<MEASURE_SLOTS;++i)assert(!measure_snapshot(refs[i]).coherent);
    measure_drop(reused,MR_WORKER);measure_drop(reused,MR_RAW);
    for(unsigned i=0;i<MEASURE_SLOTS;++i){refs[i]=measure_reserve(17,4,0x82,0,8,12000+i);assert(refs[i].record);}
    RK3MeasureRef *shared=refs;dispatch_group_t releases=dispatch_group_create();
    dispatch_group_async(releases,dispatch_get_global_queue(QOS_CLASS_DEFAULT,0),^{
        for(unsigned i=0;i<MEASURE_SLOTS;++i)measure_drop(shared[i],MR_RAW);
    });
    dispatch_group_async(releases,dispatch_get_global_queue(QOS_CLASS_DEFAULT,0),^{
        for(unsigned i=0;i<MEASURE_SLOTS;++i)measure_drop(shared[i],MR_WORKER);
    });
    assert(dispatch_group_wait(releases,dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC))==0);
    for(unsigned i=0;i<MEASURE_SLOTS;++i)assert(!atomic_load(&refs[i].record->life));
}
static void test_measurement_stream_clock_and_fault(void) {
    RK3HostInterface *owner=open_owner();assert(rk3_host_set_alt(owner,1)==0);RK3HostState *state=state_of(owner);
    rk3_host_measurement_begin_stream(owner,1);uint64_t first_epoch=state->debug_stream_epoch;
    rk3_host_measurement_clock(owner,true,10);rk3_host_measurement_clock(owner,true,11);
    rk3_host_measurement_clock(owner,false,12);rk3_host_measurement_clock(owner,true,13);
    rk3_host_measurement_clock(owner,false,14);assert(state->debug_clock.active&&!state->debug_clock.valid);
    rk3_host_measurement_clock(owner,false,15);assert(!state->debug_clock.active&&!state->debug_clock.valid);
    rk3_host_measurement_clock(owner,true,20);rk3_host_measurement_clock(owner,false,30);assert(state->debug_clock.valid);
    rk3_host_measurement_begin_stream(owner,1);assert(state->debug_stream_epoch==first_epoch+1&&!state->debug_clock.valid);
    int32_t error=0;RK3HostBuffer *data=rk3_host_buffer_create(owner,16,&error);
    RK3HostIsoList *list=rk3_host_iso_list_create(owner,1,&error);assert(data&&list);
    assert(rk3_host_iso_list_set(list,0,4,0,false)==0);OrderedContext context={1,0,0,1};ordered_calls=0;
    assert(rk3_host_iso_enqueue(owner,3,data,list,0,1,1100,ordered_done,&context)==0);
    FakeHostPipe *pipe=last_interface->pipes[@0x82];finish(pipe,0,0,false);pump();
    RK3MeasureSnapshot s=measure_snapshot(state->debug_last_record);assert(s.coherent&&(s.marks&MM_ALL)==MM_ALL);
    rk3_host_measurement_clock(owner,true,s.k);rk3_host_measurement_clock(owner,false,s.w);
    pipe->fail_enqueue=true;pipe->enqueue_error=kIOReturnIsoTooOld;
    assert(rk3_host_iso_list_set(list,0,4,0,false)==0);
    assert(rk3_host_iso_enqueue(owner,3,data,list,0,1,1164,ordered_done,&context)==kIOReturnIsoTooOld);
    assert(state->debug_fault.matched&&state->debug_fault.complete&&state->debug_fault.ordered);
    assert(state->debug_fault_record.cookie==s.cookie&&(atomic_load(&state->debug_fault_record.record->life)&MR_FAULT));
    RK3MeasureFault frozen=state->debug_fault;
    assert(rk3_host_iso_enqueue(owner,3,data,list,0,1,1165,ordered_done,&context)==kIOReturnIsoTooOld);
    assert(!memcmp(&frozen,&state->debug_fault,sizeof(frozen)));
    rk3_host_iso_list_release(list);rk3_host_buffer_release(data);assert(rk3_host_close(owner)==0);rk3_host_release(owner);
}
static void test_measurement_cookie_exhaustion(void) {
    atomic_store(&measure_next,UINT64_MAX/16);
    assert(!measure_cookie()&&!measure_reserve(17,1,0x82,0,8,1100).record);
    RK3HostInterface *owner=open_owner();assert(!state_of(owner)->debug_interface_cookie);
    assert(rk3_host_close(owner)==0);rk3_host_release(owner);
}
static RK3HostBuffer *pending_fault_data;
static void reject_during_pending_publication(void *context,int32_t status,RK3HostIsoList *list,size_t first,size_t count) {
    RK3HostInterface *owner=context;assert(!status&&first==0&&count==1);
    FakeHostPipe *pipe=last_interface->pipes[@0x82];pipe->fail_enqueue=true;pipe->enqueue_error=kIOReturnIsoTooOld;
    assert(rk3_host_iso_list_set(list,0,4,0,false)==0);
    assert(rk3_host_iso_enqueue(owner,3,pending_fault_data,list,0,1,1164,reject_during_pending_publication,owner)==kIOReturnIsoTooOld);
}
static void test_measurement_pending_fault_freezes_without_waiting(void) {
    RK3HostInterface *owner=open_owner();assert(rk3_host_set_alt(owner,1)==0);rk3_host_measurement_begin_stream(owner,1);
    int32_t error=0;pending_fault_data=rk3_host_buffer_create(owner,16,&error);
    RK3HostIsoList *list=rk3_host_iso_list_create(owner,1,&error);assert(pending_fault_data&&list);
    assert(rk3_host_iso_list_set(list,0,4,0,false)==0);
    assert(rk3_host_iso_enqueue(owner,3,pending_fault_data,list,0,1,1100,reject_during_pending_publication,owner)==0);
    FakeHostPipe *pipe=last_interface->pipes[@0x82];FakeHostRequest *request=pipe->requests[0];[pipe->requests removeObjectAtIndex:0];
    publication_reached=dispatch_semaphore_create(0);retirement_finished=dispatch_semaphore_create(0);
    dispatch_semaphore_t returned=dispatch_semaphore_create(0);rk3_host_test_after_publication=pause_before_wake;
    dispatch_async(pipe->queue,^{request->iso(0,request->transactions);dispatch_semaphore_signal(returned);});
    assert(dispatch_semaphore_wait(publication_reached,dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC))==0);
    pump();RK3HostState *state=state_of(owner);RK3MeasureFault frozen=state->debug_fault;
    assert(frozen.matched&&!frozen.complete&&!frozen.ordered&&!(frozen.job.marks&(MM_P1|MM_K)));
    assert(rk3_host_pending(owner)==0); /* original logical job already retired */
    dispatch_semaphore_signal(retirement_finished);
    assert(dispatch_semaphore_wait(returned,dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC))==0);
    rk3_host_test_after_publication=NULL;
    assert(!memcmp(&frozen,&state->debug_fault,sizeof(frozen)));
    assert((measure_snapshot(state->debug_fault_record).marks&MM_ALL)==MM_ALL);
    rk3_host_iso_list_release(list);rk3_host_buffer_release(pending_fault_data);pending_fault_data=NULL;
    assert(rk3_host_close(owner)==0);rk3_host_release(owner);
}
int main(void) {@autoreleasepool {
    owner_loop=CFRunLoopGetCurrent();rk3_host_test_factory=factory;
    test_open_and_properties();test_bulk_and_abort();test_shared_iso_and_lifetime();test_owner_retained_through_delivery();test_rejected_iso_and_allocation();test_removal_interest_and_late_delivery();test_retirement_before_completion_wake();test_zero_count_iso_transactions();test_iso_callbacks_follow_acceptance_per_pipe();test_rejected_iso_has_no_ordered_position();test_reentrant_iso_enqueue_and_nested_delivery();test_reverse_abort_retains_until_ordered_drain();test_zero_count_request_is_ordered_with_payload();test_first_too_old_observation_preserves_order_and_is_bounded();
    test_measurement_interpretation();test_measurement_bank_lifetimes_and_budget();test_measurement_stream_clock_and_fault();
    test_measurement_pending_fault_freezes_without_waiting();
    test_measurement_cookie_exhaustion();
    last_interface=nil;
    puts("NativeHostUSB: mapped backing, offsets/wrap, worker callbacks, pending ranges, abort/drain, removal interest and lifetime mocks passed.");
    return 0;
}}
