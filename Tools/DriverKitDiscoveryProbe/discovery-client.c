/* Read-only discovery diagnostic. --check is the sole live-registry path.
 * No user-client, NI selector, USB, audio or MIDI operation exists here. */
#include "discovery-result.h"
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOKitKeys.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static void string_property(io_service_t service,CFStringRef key,char *out,size_t capacity) {
    CFTypeRef value=IORegistryEntryCreateCFProperty(service,key,NULL,0);
    bool copied=value&&CFGetTypeID(value)==CFStringGetTypeID()&&
        CFStringGetCString((CFStringRef)value,out,(CFIndex)capacity,kCFStringEncodingUTF8);
    if(!copied)snprintf(out,capacity,"<absent-or-nonstring>");
    if(value)CFRelease(value);
}
static RK3DiscoveryEntry snapshot(io_service_t service) {
    RK3DiscoveryEntry entry={0};io_name_t name={0},class_name={0};
    kern_return_t identifier=IORegistryEntryGetRegistryEntryID(service,&entry.entry_id);
    kern_return_t class_result=IOObjectGetClass(service,class_name);
    entry.metadata_valid=identifier==KERN_SUCCESS&&class_result==KERN_SUCCESS&&entry.entry_id!=0;
    snprintf(entry.kernel_class,sizeof(entry.kernel_class),"%s",class_result==KERN_SUCCESS?class_name:"<unavailable>");
    kern_return_t name_result=IORegistryEntryGetName(service,name);
    snprintf(entry.name,sizeof(entry.name),"%s",name_result==KERN_SUCCESS?name:"<unavailable>");
    string_property(service,CFSTR("IOUserClass"),entry.user_class,sizeof(entry.user_class));
    string_property(service,CFSTR("IOUserServerName"),entry.user_server,sizeof(entry.user_server));
    string_property(service,CFSTR("CFBundleIdentifier"),entry.bundle_identifier,sizeof(entry.bundle_identifier));
    CFTypeRef marker=IORegistryEntryCreateCFProperty(service,CFSTR(RK3_DISCOVERY_MARKER_KEY),NULL,0);
    entry.marker=marker&&CFGetTypeID(marker)==CFBooleanGetTypeID()&&CFBooleanGetValue((CFBooleanRef)marker);
    if(marker)CFRelease(marker);
    CFTypeRef identity=IORegistryEntryCreateCFProperty(service,CFSTR(RK3_DISCOVERY_IDENTIFIER_KEY),NULL,0);
    entry.identifier_match=identity&&CFGetTypeID(identity)==CFStringGetTypeID()&&CFEqual(identity,CFSTR(RK3_DISCOVERY_IDENTIFIER));
    if(identity)CFRelease(identity);
    return entry;
}
static int32_t live_enumerate(void *context,RK3DiscoveryQuery query,RK3DiscoveryVisit visit,void *visitor) {
    (void)context;
    CFMutableDictionaryRef matching=NULL;
    if(query==RK3_QUERY_PROBE) {
        /* Independent of both the legacy class and registry name. The explicit
         * marker's CFBoolean type and identifier are checked again per entry. */
        matching=IOServiceMatching("IOService");
        if(!matching)return kIOReturnNoMemory;
        CFMutableDictionaryRef properties=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
        if(!properties){CFRelease(matching);return kIOReturnNoMemory;}
        CFDictionarySetValue(properties,CFSTR(RK3_DISCOVERY_MARKER_KEY),kCFBooleanTrue);
        CFDictionarySetValue(properties,CFSTR(RK3_DISCOVERY_IDENTIFIER_KEY),CFSTR(RK3_DISCOVERY_IDENTIFIER));
        CFDictionarySetValue(matching,CFSTR(kIOPropertyMatchKey),properties);CFRelease(properties);
    } else if(query==RK3_QUERY_LEGACY_CLASS) {
        matching=IOServiceMatching(RK3_DISCOVERY_LEGACY_CLASS);
    } else if(query==RK3_QUERY_LEGACY_NAME) {
        matching=IOServiceNameMatching(RK3_DISCOVERY_LEGACY_CLASS);
    } else return kIOReturnBadArgument;
    if(!matching)return kIOReturnNoMemory;
    io_iterator_t iterator=IO_OBJECT_NULL;
    /* IOServiceGetMatchingServices consumes matching, including on failure. */
    kern_return_t result=IOServiceGetMatchingServices(kIOMainPortDefault,matching,&iterator);
    if(result!=KERN_SUCCESS)return result;
    if(iterator==IO_OBJECT_NULL)return kIOReturnError;
    io_service_t service;
    while((service=IOIteratorNext(iterator))!=IO_OBJECT_NULL) {
        RK3DiscoveryEntry entry=snapshot(service);visit(visitor,&entry);IOObjectRelease(service);
    }
    bool valid=IOIteratorIsValid(iterator);IOObjectRelease(iterator);
    return valid?0:kIOReturnError;
}
static void json_string(const char *text) {
    putchar('"');
    for(const unsigned char *p=(const unsigned char *)text;*p;++p) {
        if(*p=='"'||*p=='\\'){putchar('\\');putchar(*p);}
        else if(*p<32)printf("\\u%04x",(unsigned)*p);
        else putchar(*p);
    }
    putchar('"');
}
static void print_result(const RK3DiscoveryResult *result) {
    printf("{\"diagnostic\":\"DriverKit discovery only\",\"source\":\"live IOKit\",\"status\":");
    json_string(rk3_discovery_status_name(result->status));printf(",\"reason\":");json_string(result->reason);
    printf(",\"query_error\":%" PRId32 ",\"name_query_error\":%" PRId32 ",\"probe_count\":%" PRIu64
        ",\"class_count\":%" PRIu64 ",\"name_count\":%" PRIu64 ",\"same_class_entry\":%s,\"same_name_entry\":%s,\"name_queried\":%s",
        result->query_error,result->name_query_error,result->probe_count,result->class_count,result->name_count,
        result->same_class_entry?"true":"false",result->same_name_entry?"true":"false",result->name_queried?"true":"false");
    printf(",\"probe\":{\"entry_id\":%" PRIu64 ",\"kernel_class\":",result->probe.entry_id);json_string(result->probe.kernel_class);
    printf(",\"IOUserClass\":");json_string(result->probe.user_class);
    printf(",\"IOUserServerName\":");json_string(result->probe.user_server);
    printf(",\"CFBundleIdentifier\":");json_string(result->probe.bundle_identifier);
    printf(",\"name\":");json_string(result->probe.name);
    puts("},\"limit\":\"Exact legacy lookup only; no NI selectors or NI application compatibility are tested. A miss does not rule out an adapted ARM driver client.\"}");
}
int main(int argc,char **argv) {
    if(argc==2&&strcmp(argv[1],"--check")==0) {
        const RK3DiscoveryAdapter adapter={NULL,live_enumerate};
        RK3DiscoveryResult result=rk3_discovery_evaluate(&adapter);print_result(&result);return result.status;
    }
    fputs("Usage: discovery-client --check\n"
        "Read-only live registry check; no user-client, USB, audio or MIDI calls.\n"
        "Exit0: exact class query returned the marked probe's entryID.\n"
        "Exit1: visible unique probe missed the exact class query.\n"
        "Exit2: no probe, ambiguous identity, query/metadata error or usage.\n"
        "Offline tests are discovery-client-tests --fixture; building does not run --check.\n",stderr);
    return argc==2&&strcmp(argv[1],"--help")==0?0:2;
}
