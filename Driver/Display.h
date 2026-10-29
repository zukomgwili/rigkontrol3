#ifndef RK3_DISPLAY_H
#define RK3_DISPLAY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RK3_CONTROLLER_DISPLAY_REQUEST_KEY "org.rigkontrol3.ni-arm.display.request"
#define RK3_CONTROLLER_DISPLAY_STATUS_KEY "org.rigkontrol3.ni-arm.display.status"

/* Transient native mailbox, not an NI wire ABI or saved display preference.
 * content[] is logical index order. Acknowledgement is mapper application,
 * never hardware readback or USB completion. Cached requests need a live lease. */
typedef struct {
    uint32_t version,index;
    uint64_t session,epoch,generation;
    uint32_t audio_device,value;
    uint64_t request_id;
} RK3DisplayRequest;
typedef struct {
    uint32_t version;
    int32_t status;
    uint64_t session,epoch,generation;
    uint32_t audio_device,reserved;
    uint64_t request_id;
    uint8_t content[4],reserved_bytes[4];
} RK3DisplayStatus;

static inline bool rk3_display_request_valid(const RK3DisplayRequest *r) {
    return r&&r->version==1&&r->index<4&&r->value<=255&&r->session&&
        r->epoch&&r->generation&&r->audio_device&&r->request_id;
}
static inline bool rk3_display_status_valid(const RK3DisplayStatus *s) {
    if(!s||s->version!=1||!s->session||s->reserved)return false;
    for(unsigned i=0;i<4;++i)if(s->reserved_bytes[i])return false;
    return s->status!=0||(s->epoch&&s->generation&&s->audio_device);
}
static inline bool rk3_display_same_lease(const RK3DisplayRequest *r,const RK3DisplayStatus *s) {
    return r&&s&&r->session==s->session&&r->epoch==s->epoch&&
        r->generation==s->generation&&r->audio_device==s->audio_device;
}
static inline bool rk3_display_same_status_lease(const RK3DisplayStatus *a,const RK3DisplayStatus *b) {
    return a&&b&&a->session==b->session&&a->epoch==b->epoch&&
        a->generation==b->generation&&a->audio_device==b->audio_device;
}

_Static_assert(sizeof(RK3DisplayRequest)==48,"Display request ABI");
_Static_assert(offsetof(RK3DisplayRequest,version)==0,"Display version ABI");
_Static_assert(offsetof(RK3DisplayRequest,index)==4,"Display index ABI");
_Static_assert(offsetof(RK3DisplayRequest,session)==8,"Display session ABI");
_Static_assert(offsetof(RK3DisplayRequest,epoch)==16,"Display epoch ABI");
_Static_assert(offsetof(RK3DisplayRequest,generation)==24,"Display generation ABI");
_Static_assert(offsetof(RK3DisplayRequest,audio_device)==32,"Display provider ABI");
_Static_assert(offsetof(RK3DisplayRequest,value)==36,"Display value ABI");
_Static_assert(offsetof(RK3DisplayRequest,request_id)==40,"Display request ID ABI");
_Static_assert(sizeof(RK3DisplayStatus)==56,"Display status ABI");
_Static_assert(offsetof(RK3DisplayStatus,status)==4,"Display errno ABI");
_Static_assert(offsetof(RK3DisplayStatus,session)==8,"Display status session ABI");
_Static_assert(offsetof(RK3DisplayStatus,epoch)==16,"Display status epoch ABI");
_Static_assert(offsetof(RK3DisplayStatus,generation)==24,"Display status generation ABI");
_Static_assert(offsetof(RK3DisplayStatus,audio_device)==32,"Display status provider ABI");
_Static_assert(offsetof(RK3DisplayStatus,reserved)==36,"Display reserved ABI");
_Static_assert(offsetof(RK3DisplayStatus,request_id)==40,"Display acknowledgement ABI");
_Static_assert(offsetof(RK3DisplayStatus,content)==48,"Display logical bytes ABI");
_Static_assert(offsetof(RK3DisplayStatus,reserved_bytes)==52,"Display padding ABI");
#endif
