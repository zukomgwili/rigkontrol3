#ifndef RK3_DISCOVERY_RESULT_H
#define RK3_DISCOVERY_RESULT_H
#include <stdbool.h>
#include <stdint.h>

#define RK3_DISCOVERY_IDENTIFIER "org.rigkontrol3.ni-discovery-probe"
#define RK3_DISCOVERY_MARKER_KEY "RK3DiscoveryProbe"
#define RK3_DISCOVERY_IDENTIFIER_KEY "RK3DiscoveryProbeIdentifier"
#define RK3_DISCOVERY_LEGACY_CLASS "de_caiaq_driver_NIUSBAudioDevice"

typedef enum { RK3_QUERY_PROBE, RK3_QUERY_LEGACY_CLASS, RK3_QUERY_LEGACY_NAME } RK3DiscoveryQuery;
typedef struct {
    uint64_t entry_id;
    bool metadata_valid,marker,identifier_match;
    char kernel_class[128],user_class[256],user_server[256],bundle_identifier[256],name[128];
} RK3DiscoveryEntry;
typedef void (*RK3DiscoveryVisit)(void *,const RK3DiscoveryEntry *);
typedef struct {
    void *context;
    /* Enumerate the query's entries, returning0 only for a complete query.
     * The visitor borrows each snapshot only for the duration of the call. */
    int32_t (*enumerate)(void *,RK3DiscoveryQuery,RK3DiscoveryVisit,void *);
} RK3DiscoveryAdapter;
typedef enum { RK3_DISCOVERY_PASS=0,RK3_DISCOVERY_FAIL=1,RK3_DISCOVERY_INCONCLUSIVE=2 } RK3DiscoveryStatus;
typedef struct {
    RK3DiscoveryStatus status;
    const char *reason;
    int32_t query_error,name_query_error;
    uint64_t probe_count,class_count,name_count;
    bool same_class_entry,same_name_entry,name_queried;
    RK3DiscoveryEntry probe;
} RK3DiscoveryResult;

/* This evaluates discovery evidence only. It opens no user-client and proves
 * no selector, NI application, USB, audio, MIDI or production compatibility. */
RK3DiscoveryResult rk3_discovery_evaluate(const RK3DiscoveryAdapter *);
const char *rk3_discovery_status_name(RK3DiscoveryStatus);
#endif
