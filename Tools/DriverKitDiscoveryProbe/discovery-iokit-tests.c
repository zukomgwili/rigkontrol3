/* Exercise the actual IOKit adapter offline. Every matching/iterator call is
 * injected; no live registry lookup or service snapshot is performed. */
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <assert.h>
#include <stdio.h>

static kern_return_t matching_result;
static io_iterator_t matching_iterator;
static boolean_t iterator_valid;
static unsigned matching_calls,next_calls,validity_calls,release_calls,visits;
static CFMutableDictionaryRef fake_matching(const char *name) {
    assert(name);
    return CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
}
static kern_return_t fake_get_matching(mach_port_t port,CFDictionaryRef matching,io_iterator_t *iterator) {
    assert(port==kIOMainPortDefault&&matching&&iterator);
    ++matching_calls;CFRelease(matching);*iterator=matching_iterator;
    return matching_result;
}
static io_object_t fake_next(io_iterator_t iterator) {
    assert(iterator==matching_iterator&&iterator!=IO_OBJECT_NULL);++next_calls;
    return IO_OBJECT_NULL;
}
static boolean_t fake_is_valid(io_iterator_t iterator) {
    assert(iterator==matching_iterator&&iterator!=IO_OBJECT_NULL);++validity_calls;
    return iterator_valid;
}
static kern_return_t fake_release(io_object_t object) {
    assert(object==matching_iterator&&object!=IO_OBJECT_NULL);++release_calls;
    return KERN_SUCCESS;
}
#define IOServiceMatching fake_matching
#define IOServiceNameMatching fake_matching
#define IOServiceGetMatchingServices fake_get_matching
#define IOIteratorNext fake_next
#define IOIteratorIsValid fake_is_valid
#define IOObjectRelease fake_release
#define main rk3_discovery_cli_main
#include "discovery-client.c"
#undef main

static void count_visit(void *context,const RK3DiscoveryEntry *entry) {
    (void)context;(void)entry;++visits;
}
static void reset(kern_return_t result,io_iterator_t iterator,boolean_t valid) {
    matching_result=result;matching_iterator=iterator;iterator_valid=valid;
    matching_calls=next_calls=validity_calls=release_calls=visits=0;
}
int main(int argc,char **argv) {
    if(argc!=2||strcmp(argv[1],"--fixture")!=0)return 2;
    /* SDK contract: KERN_SUCCESS plus NULL is a successful empty result. */
    reset(KERN_SUCCESS,IO_OBJECT_NULL,false);
    assert(live_enumerate(NULL,RK3_QUERY_PROBE,count_visit,NULL)==0);
    assert(matching_calls==1&&next_calls==0&&validity_calls==0&&release_calls==0&&visits==0);
    reset(kIOReturnNotPermitted,IO_OBJECT_NULL,false);
    assert(live_enumerate(NULL,RK3_QUERY_PROBE,count_visit,NULL)==kIOReturnNotPermitted);
    assert(next_calls==0&&validity_calls==0&&release_calls==0&&visits==0);
    reset(KERN_SUCCESS,123,true);
    assert(live_enumerate(NULL,RK3_QUERY_PROBE,count_visit,NULL)==0);
    assert(next_calls==1&&validity_calls==1&&release_calls==1&&visits==0);
    reset(KERN_SUCCESS,123,false);
    assert(live_enumerate(NULL,RK3_QUERY_PROBE,count_visit,NULL)==kIOReturnError);
    assert(next_calls==1&&validity_calls==1&&release_calls==1&&visits==0);
    puts("PASS offline IOKit adapter empty/error/validity contracts; no registry query");
    return 0;
}
