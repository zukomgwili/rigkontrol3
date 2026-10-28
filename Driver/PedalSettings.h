#ifndef RK3_PEDAL_SETTINGS_H
#define RK3_PEDAL_SETTINGS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <math.h>

#define RK3_CONTROLLER_PEDAL_SETTINGS_KEY "org.rigkontrol3.ni-arm.pedals"
#define RK3_CONTROLLER_PEDAL_STATUS_KEY "org.rigkontrol3.ni-arm.pedals.applied"

/* Native CoreMIDI data properties, host byte order. These replace neither NI's
 * message ABI nor its preference domain. Endpoints have NI's saved Float32
 * precision; -1 denotes an uninitialized endpoint. */
typedef struct {
    uint32_t version,ring_mask,switch_mask,reserved;
    float low[3],high[3];
} RK3PedalSettings;
typedef struct {
    uint32_t version;
    int32_t status; /* native errno; zero only after application */
    uint64_t generation;
    uint32_t audio_device,reserved;
    RK3PedalSettings settings;
} RK3PedalStatus;

static inline RK3PedalSettings rk3_pedal_settings_initial(void) {
    return (RK3PedalSettings){.version=1,.low={-1,-1,0},.high={-1,-1,1}};
}
static inline bool rk3_pedal_settings_valid(const RK3PedalSettings *settings) {
    if(!settings||settings->version!=1||settings->reserved||
       (settings->ring_mask&~7u)||(settings->switch_mask&~7u))return false;
    for(unsigned i=0;i<3;++i)
        if(!isfinite(settings->low[i])||!isfinite(settings->high[i]))return false;
    return true;
}
static inline bool rk3_pedal_configuration_valid(const uint8_t *bytes,size_t length) {
    if(!bytes||length!=33||bytes[0]!=0x0f||bytes[1]!=2||
       (bytes[2]&~7u)||(bytes[3]&~7u))return false;
    for(size_t i=4;i<33;++i)if(bytes[i])return false;
    return true;
}
static inline void rk3_pedal_configuration(const RK3PedalSettings *settings,uint8_t bytes[33]) {
    for(size_t i=0;i<33;++i)bytes[i]=0;
    bytes[0]=0x0f;bytes[1]=2;
    bytes[2]=(uint8_t)settings->ring_mask;bytes[3]=(uint8_t)settings->switch_mask;
}
#if defined(__cplusplus)
static_assert(sizeof(RK3PedalSettings)==40,"Pedal settings ABI");
static_assert(sizeof(RK3PedalStatus)==64,"Pedal status ABI");
static_assert(offsetof(RK3PedalSettings,low)==16,"Pedal endpoint ABI");
static_assert(offsetof(RK3PedalSettings,high)==28,"Pedal endpoint ABI");
static_assert(offsetof(RK3PedalStatus,generation)==8,"Pedal generation ABI");
static_assert(offsetof(RK3PedalStatus,settings)==24,"Pedal snapshot ABI");
#else
_Static_assert(sizeof(RK3PedalSettings)==40,"Pedal settings ABI");
_Static_assert(sizeof(RK3PedalStatus)==64,"Pedal status ABI");
_Static_assert(offsetof(RK3PedalSettings,low)==16,"Pedal endpoint ABI");
_Static_assert(offsetof(RK3PedalSettings,high)==28,"Pedal endpoint ABI");
_Static_assert(offsetof(RK3PedalStatus,generation)==8,"Pedal generation ABI");
_Static_assert(offsetof(RK3PedalStatus,settings)==24,"Pedal snapshot ABI");
#endif
#endif
