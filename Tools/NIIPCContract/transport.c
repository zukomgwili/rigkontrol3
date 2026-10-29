#include "transport.h"
#include <limits.h>
#include <string.h>

#if !defined(__arm64__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error This proof must be compiled for little-endian Apple ARM64.
#endif
_Static_assert(CHAR_BIT==8 && sizeof(uint32_t)==4 && sizeof(SInt32)==4,"NI ID width");
_Static_assert(sizeof(void *)==8 && sizeof(long)==8 && sizeof(CFIndex)==8,"NI LP64 boundary");
_Static_assert(sizeof(CFTimeInterval)==8,"NI timeout width");

static uint32_t word(const uint8_t *p) {
    uint32_t value; memcpy(&value,p,sizeof(value)); return value;
}

static int64_t signed_count(uint64_t bits) {
    int64_t value; memcpy(&value,&bits,sizeof(value)); return value;
}

/* Original OS send1002eda40, with the system boundary injected. */
static bool os_send(NIIPCProofSender *s, const uint8_t *bytes, uint64_t count,
                       uint8_t *reply_buffer, uint64_t capacity) {
    if (!s || !s->port || !bytes || !count) return false;
    const NIIPCProofDataOps *ops=s->ops;
    CFDataRef data=ops->create_data(ops->context,bytes,(CFIndex)signed_count(count));
    SInt32 msgid; memcpy(&msgid,bytes,sizeof(msgid));
    CFDataRef returned=NULL;
    bool wants_reply=reply_buffer && capacity;
    CFStringRef mode=wants_reply ? kCFRunLoopDefaultMode : NULL;
    SInt32 status=ops->send_request(ops->context,s->port,msgid,data,2.0,2.0,mode,&returned);
    ops->release_data(ops->context,data);
    if (status!=0) return false;
    if (!wants_reply) return true;
    if (!returned) return false;
    CFIndex length=ops->data_length(ops->context,returned);
    bool valid=length>0 && length<=signed_count(capacity);
    if (valid) memcpy(reply_buffer,ops->data_bytes(ops->context,returned),(size_t)length);
    ops->release_data(ops->context,returned);
    return valid;
}

/* SendPort wrapper1002eb940 dispatches invalidation AFTER the OS call. */
bool ni_ipc_proof_send(NIIPCProofSender *s, const uint8_t *bytes, uint64_t count,
                       uint8_t *reply_buffer, uint64_t capacity) {
    if (!s) return false; /* Absent implementation at the proof boundary. */
    bool result=os_send(s,bytes,count,reply_buffer,capacity);
    if (s->invalidated && s->on_invalidated) s->on_invalidated(s->callback_context);
    return result;
}

/* NHL wrapper1002ec270: these words are in-process control, not wire bytes. */
bool ni_ipc_proof_nhl_send(NIIPCProofSender *s, uint8_t *message) {
    uint32_t count=word(message), capacity=word(message+8);
    uint8_t *reply=capacity ? message+word(message+4) : NULL;
    return ni_ipc_proof_send(s,message+12,count,reply,capacity);
}

CFDataRef ni_ipc_proof_receive(const NIIPCProofDataOps *ops, NIIPCProofReceiver *r,
                              SInt32 msgid, CFDataRef data) {
    (void)msgid; /* Original callback ignores the separate CF message ID. */
    uint8_t *reply=NULL; uint64_t reply_count=0;
    if (r && data) {
        CFIndex length=ops->data_length(ops->context,data);
        if (length>0 && length<signed_count(r->capacity)) {
            memcpy(r->buffer,ops->data_bytes(ops->context,data),(size_t)length);
            (void)r->handler(r->context,(uint64_t)length,&reply,&reply_count);
            if (reply && reply_count)
                return ops->create_data(ops->context,reply,(CFIndex)signed_count(reply_count));
        }
    }
    return ops->create_data(ops->context,NULL,0);
}

size_t ni_ipc_proof_name_units(const char *name, uint64_t count, UniChar *units) {
    (void)count; /* ReceivePort's name copy used strlen, not CharSequence count. */
    size_t length=strlen(name);
    for (size_t i=0;i<length;i++) {
        uint8_t byte=(uint8_t)name[i];
        units[i]=(UniChar)((byte&0x80U) ? (0xff00U|byte) : byte);
    }
    units[length]=0;
    return length;
}
uint32_t ni_ipc_proof_allocation_count(uint32_t count) {
    if (count<UINT32_C(32768)) count=UINT32_C(32768);
    return count+UINT32_C(12);
}
