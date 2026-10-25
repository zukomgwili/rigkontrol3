#include "transport.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)

static unsigned scenarios;

typedef struct {
    NIIPCProofDataOps ops;
    CFDataRef owned[32];
    bool alive[32];
    size_t objects, releases, sends, invalidation_calls;
    SInt32 status, msgid;
    CFDataRef reply;
    CFDataRef length_override_object;
    CFIndex length_override, sent_count;
    uint8_t sent[64];
    CFTimeInterval send_timeout, receive_timeout;
    CFStringRef mode;
    bool return_slot_was_null;
} Fixture;

static CFDataRef create_data(void *context, const UInt8 *bytes, CFIndex count) {
    Fixture *f=context;
    CHECK(f->objects<32);
    CFDataRef data=CFDataCreate(NULL,bytes,count);
    CHECK(data);
    f->owned[f->objects]=data; f->alive[f->objects++]=true;
    return data;
}
static void release_data(void *context, CFDataRef data) {
    Fixture *f=context;
    for (size_t i=0;i<f->objects;i++) {
        if (f->owned[i]==data && f->alive[i]) {
            f->alive[i]=false; f->releases++; CFRelease(data); return;
        }
    }
    CHECK(false); /* Includes an unexpected or duplicate release. */
}
static CFIndex data_length(void *context, CFDataRef data) {
    Fixture *f=context;
    return data==f->length_override_object ? f->length_override : CFDataGetLength(data);
}
static const UInt8 *data_bytes(void *context, CFDataRef data) {
    (void)context; return CFDataGetBytePtr(data);
}
static SInt32 send_request(void *context, CFMessagePortRef port, SInt32 msgid,
                           CFDataRef data, CFTimeInterval send_timeout,
                           CFTimeInterval receive_timeout, CFStringRef mode,
                           CFDataRef *returned) {
    Fixture *f=context;
    CHECK(port && returned);
    f->sends++; f->msgid=msgid; f->sent_count=CFDataGetLength(data);
    CHECK(f->sent_count>=0 && f->sent_count<=64);
    memcpy(f->sent,CFDataGetBytePtr(data),(size_t)f->sent_count);
    f->send_timeout=send_timeout; f->receive_timeout=receive_timeout;
    f->mode=mode; f->return_slot_was_null=*returned==NULL;
    *returned=f->reply;
    return f->status;
}
static void init(Fixture *f) {
    scenarios++; memset(f,0,sizeof(*f));
    f->ops=(NIIPCProofDataOps){f,create_data,release_data,data_length,data_bytes,send_request};
}
static NIIPCProofSender sender(Fixture *f) {
    static const char fake_port=0;
    return (NIIPCProofSender){.port=(CFMessagePortRef)(const void *)&fake_port,.ops=&f->ops};
}
static void no_owned_data(Fixture *f) {
    for (size_t i=0;i<f->objects;i++) CHECK(!f->alive[i]);
}

/* Independent literal oracle: .19 NHL wrapper1002ec270 and OSsend1002eda40.
 * Pointer is deliberately unaligned; count6 excludes the control prefix. */
static void payload_and_id(void) {
    Fixture f; init(&f); NIIPCProofSender s=sender(&f);
    uint8_t storage[33]={0}; uint8_t *message=storage+1;
    const uint8_t input[]={6,0,0,0,24,0,0,0,0,0,0,0,0x80,0,0,0x80,0xa5,0x5a};
    const uint8_t expected[]={0x80,0,0,0x80,0xa5,0x5a};
    memcpy(message,input,sizeof(input));
    CHECK(ni_ipc_proof_nhl_send(&s,message));
    CHECK(f.sends==1 && f.sent_count==6 && memcmp(f.sent,expected,6)==0);
    CHECK(f.msgid==INT32_C(-2147483520));
    CHECK(f.send_timeout==2.0 && f.receive_timeout==2.0);
    CHECK(f.mode==NULL && f.return_slot_was_null);
    CHECK(f.releases==1); no_owned_data(&f);
}

/* Independent branches: OSsend1002edbd4..edc03/ee09c; original accepts
 * reply length == capacity, but rejects signed nonpositive/oversized values. */
static void reply_selection_and_ownership(void) {
    const uint8_t payload[]={0x80,0,0,0x80,0xa5,0x5a};
    const uint8_t response[]={9,8,7,6,5};
    for (unsigned combination=0;combination<4;combination++) {
        Fixture f; init(&f); NIIPCProofSender s=sender(&f);
        uint8_t destination[8]; memset(destination,0xcc,sizeof(destination));
        bool pointer=(combination&1)!=0, count=(combination&2)!=0;
        if (pointer && count) f.reply=create_data(&f,response,3);
        CHECK(ni_ipc_proof_send(&s,payload,6,pointer?destination:NULL,count?4:0));
        CHECK(f.mode==((pointer&&count)?kCFRunLoopDefaultMode:NULL));
        CHECK(f.return_slot_was_null && f.sends==1);
        if (pointer && count) CHECK(memcmp(destination,response,3)==0 && destination[3]==0xcc);
        else CHECK(destination[0]==0xcc);
        no_owned_data(&f);
    }
    const CFIndex lengths[]={4,0,5,-1};
    for (size_t i=0;i<4;i++) {
        Fixture f; init(&f); NIIPCProofSender s=sender(&f);
        f.reply=create_data(&f,response,5);
        f.length_override_object=f.reply; f.length_override=lengths[i];
        uint8_t destination[8]; memset(destination,0xcc,sizeof(destination));
        bool result=ni_ipc_proof_send(&s,payload,6,destination,4);
        CHECK(result==(i==0));
        if (i==0) CHECK(memcmp(destination,response,4)==0 && destination[4]==0xcc);
        else CHECK(destination[0]==0xcc);
        CHECK(f.releases==2); no_owned_data(&f);
    }
    Fixture missing; init(&missing); NIIPCProofSender s=sender(&missing);
    uint8_t destination[8]={0};
    CHECK(!ni_ipc_proof_send(&s,payload,6,destination,4));
    CHECK(missing.releases==1); no_owned_data(&missing);
    Fixture high; init(&high); s=sender(&high); high.reply=create_data(&high,response,3);
    CHECK(!ni_ipc_proof_send(&s,payload,6,destination,UINT64_C(0x8000000000000000)));
    CHECK(high.releases==2); no_owned_data(&high);
}
static void guards_short_counts_and_errors(void) {
    const uint8_t backing[]={0x80,0,0,0x80,0xa5,0x5a};
    const SInt32 statuses[]={-1,-2,-3,-4,-5,42};
    for (size_t i=0;i<6;i++) {
        Fixture f; init(&f); NIIPCProofSender s=sender(&f); f.status=statuses[i];
        CHECK(!ni_ipc_proof_send(&s,backing,6,NULL,0));
        CHECK(f.sends==1 && f.releases==1); no_owned_data(&f);
    }
    for (uint64_t count=1;count<=3;count++) {
        Fixture f; init(&f); NIIPCProofSender s=sender(&f);
        CHECK(ni_ipc_proof_send(&s,backing,count,NULL,0));
        CHECK(f.sent_count==(CFIndex)count && memcmp(f.sent,backing,(size_t)count)==0);
        CHECK(f.msgid==INT32_C(-2147483520)); no_owned_data(&f);
    }
    Fixture f; init(&f); NIIPCProofSender s=sender(&f);
    CHECK(!ni_ipc_proof_send(NULL,backing,6,NULL,0));
    CHECK(!ni_ipc_proof_send(&s,NULL,6,NULL,0));
    CHECK(!ni_ipc_proof_send(&s,backing,0,NULL,0));
    s.port=NULL; CHECK(!ni_ipc_proof_send(&s,backing,6,NULL,0));
    CHECK(f.sends==0 && f.objects==0);
}
static void nhl_reply_offset(void) {
    Fixture f; init(&f); NIIPCProofSender s=sender(&f);
    const uint8_t response[]={9,8,7,6}; f.reply=create_data(&f,response,4);
    uint8_t message[32]={6,0,0,0,24,0,0,0,4,0,0,0,0x80,0,0,0x80,0xa5,0x5a};
    CHECK(ni_ipc_proof_nhl_send(&s,message));
    CHECK(memcmp(message+24,response,4)==0 && message[0]==6 && message[8]==4);
    CHECK(f.sent_count==6 && f.mode==kCFRunLoopDefaultMode); no_owned_data(&f);
}


typedef struct {
    unsigned calls;
    uint64_t length;
    uint8_t *reply;
    uint64_t reply_count;
} Handler;
static bool handle(void *context, uint64_t count, uint8_t **bytes, uint64_t *length) {
    Handler *h=context; h->calls++; h->length=count;
    CHECK(*bytes==NULL && *length==0);
    *bytes=h->reply; *length=h->reply_count;
    return false; /* Original OS callback ignores the handler's bool result. */
}
/* Independent oracle: callback1002ee910's JGE rejects equality; missing and
 * rejected input still produce empty CFData. API-reported negative lengths
 * are injected without allocating an impossible CFData object. */
static void receive_boundaries_and_empty_reply(void) {
    const CFIndex counts[]={3,4,0,-1,5,3,3,3,3};
    const uint64_t capacities[]={4,4,4,4,4,0,UINT64_C(0x8000000000000000),4,4};
    const uint8_t input[]={0xa5,0x5a,0x11,0x22,0x33};
    for (size_t i=0;i<9;i++) {
        Fixture f; init(&f);
        CFDataRef data=CFDataCreate(NULL,input,5); CHECK(data);
        f.length_override_object=data; f.length_override=counts[i];
        uint8_t buffer[8]; memset(buffer,0xcc,sizeof(buffer));
        Handler h={0}; NIIPCProofReceiver r={buffer,capacities[i],handle,&h};
        CFDataRef result=ni_ipc_proof_receive(&f.ops,i==7?NULL:&r,123456,i==8?NULL:data);
        CHECK(result && CFDataGetLength(result)==0);
        CHECK(h.calls==(i==0?1U:0U));
        if (i==0) CHECK(h.length==3 && memcmp(buffer,input,3)==0 && buffer[3]==0xcc);
        else CHECK(buffer[0]==0xcc);
        CHECK(f.releases==0); /* Incoming data and returned data remain borrowed/owned respectively. */
        release_data(&f,result); no_owned_data(&f); CFRelease(data);
    }
    for (unsigned combination=0;combination<4;combination++) {
        Fixture f; init(&f); uint8_t response[]={9,8,7};
        Handler h={.reply=(combination&1)?response:NULL,.reply_count=(combination&2)?3:0};
        uint8_t buffer[8]={0}; NIIPCProofReceiver r={buffer,8,handle,&h};
        CFDataRef data=CFDataCreate(NULL,input,3); CHECK(data);
        CFDataRef result=ni_ipc_proof_receive(&f.ops,&r,INT32_MIN,data);
        CHECK(result && h.calls==1 && h.length==3 && memcmp(buffer,input,3)==0);
        CHECK(CFDataGetLength(result)==(combination==3?3:0));
        if (combination==3) {
            response[0]=0; /* CFDataCreate must have copied handler-owned bytes. */
            CHECK(CFDataGetBytePtr(result)[0]==9);
        }
        release_data(&f,result); no_owned_data(&f); CFRelease(data);
    }
}


/* Original name constructors1002eba50/1002ae600, not a Unicode decoder. */
static void name_case(const char *name, uint64_t supplied, const UniChar *expected, size_t count) {
    scenarios++; UniChar units[32];
    for (size_t i=0;i<32;i++) units[i]=0xcccc;
    CHECK(ni_ipc_proof_name_units(name,supplied,units)==count);
    CHECK(memcmp(units,expected,(count+1)*sizeof(UniChar))==0);
    CHECK(units[count+1]==0xcccc);
    CFStringRef string=CFStringCreateWithCharacters(NULL,units,(CFIndex)count); CHECK(string);
    CHECK(CFStringGetLength(string)==(CFIndex)count);
    for (size_t i=0;i<count;i++) CHECK(CFStringGetCharacterAtIndex(string,(CFIndex)i)==expected[i]);
    CFRelease(string);
}
static void names_and_wrapping_count(void) {
    const UniChar main_name[]={0x4e,0x49,0x48,0x57,0x4d,0x61,0x69,0x6e,0x48,0x61,0x6e,0x64,0x6c,0x65,0x72,0};
    name_case("NIHWMainHandler",1,main_name,15);
    const char high[]={'A',(char)0x80,(char)0xff,0};
    const UniChar widened[]={0x0041,0xff80,0xffff,0}; name_case(high,1,widened,3);
    const char embedded[]={'A',0,'B',0}; const UniChar shortened[]={0x0041,0};
    name_case(embedded,3,shortened,1);
    const UniChar empty[]={0}; name_case("",42,empty,0);
    const uint32_t requested[]={0,1,32767,32768,32769,UINT32_C(0xfffffff8),UINT32_MAX};
    const uint32_t expected[]={32780,32780,32780,32780,32781,4,11};
    for (size_t i=0;i<7;i++) { scenarios++; CHECK(ni_ipc_proof_allocation_count(requested[i])==expected[i]); }
}


static void invalidated(void *context) {
    Fixture *f=context;
    CHECK(f->releases==f->sends); /* Original wrapper callback follows OSsend release. */
    f->invalidation_calls++;
}
static void invalidation_dispatch_preserves_result(void) {
    const uint8_t payload[]={1,2,3,4};
    for (unsigned failure=0;failure<2;failure++) {
        Fixture f; init(&f); NIIPCProofSender s=sender(&f);
        s.invalidated=true; s.on_invalidated=invalidated; s.callback_context=&f;
        f.status=failure?-1:0;
        CHECK(ni_ipc_proof_send(&s,payload,4,NULL,0)==!failure);
        CHECK(f.invalidation_calls==1 && s.invalidated);
        CHECK(ni_ipc_proof_send(&s,payload,4,NULL,0)==!failure);
        CHECK(f.invalidation_calls==2 && s.invalidated); no_owned_data(&f);
    }
    Fixture f; init(&f); NIIPCProofSender s=sender(&f);
    s.on_invalidated=invalidated; s.callback_context=&f;
    CHECK(ni_ipc_proof_send(&s,payload,4,NULL,0) && f.invalidation_calls==0);
    s.invalidated=true; s.port=NULL;
    CHECK(!ni_ipc_proof_send(&s,payload,4,NULL,0) && f.invalidation_calls==1);
    CHECK(s.invalidated); no_owned_data(&f);
}

int main(void) {
    payload_and_id();
    reply_selection_and_ownership();
    guards_short_counts_and_errors();
    nhl_reply_offset();
    receive_boundaries_and_empty_reply();
    names_and_wrapping_count();
    invalidation_dispatch_preserves_result();
    printf("PASS transport: %u scenarios\n",scenarios);
    return 0;
}
