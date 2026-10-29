#include "discovery-result.h"
typedef struct {RK3DiscoveryResult *result;RK3DiscoveryQuery query;bool bad_identity,changed_identity;} Collector;
static void collect(void *context,const RK3DiscoveryEntry *entry) {
    Collector *collector=context;RK3DiscoveryResult *result=collector->result;
    if(collector->query==RK3_QUERY_PROBE) {
        if(!entry->marker||!entry->identifier_match)return;
        ++result->probe_count;
        if(!entry->metadata_valid||!entry->entry_id)collector->bad_identity=true;
        if(result->probe_count==1)result->probe=*entry;
    } else if(collector->query==RK3_QUERY_LEGACY_CLASS) {
        ++result->class_count;
        if(!entry->metadata_valid||!entry->entry_id)collector->bad_identity=true;
        if(entry->entry_id==result->probe.entry_id) {
            if(entry->metadata_valid&&entry->marker&&entry->identifier_match)result->same_class_entry=true;
            else collector->changed_identity=true;
        }
    } else {
        ++result->name_count;
        if(entry->entry_id==result->probe.entry_id&&entry->metadata_valid&&entry->marker&&entry->identifier_match)
            result->same_name_entry=true;
    }
}
RK3DiscoveryResult rk3_discovery_evaluate(const RK3DiscoveryAdapter *adapter) {
    RK3DiscoveryResult result={.status=RK3_DISCOVERY_INCONCLUSIVE,.reason="ADAPTER_ERROR"};
    if(!adapter||!adapter->enumerate)return result;
    Collector collector={.result=&result,.query=RK3_QUERY_PROBE};
    result.query_error=adapter->enumerate(adapter->context,collector.query,collect,&collector);
    if(result.query_error){result.reason="PROBE_QUERY_ERROR";return result;}
    if(!result.probe_count){result.reason="NO_PROBE";return result;}
    if(result.probe_count!=1){result.reason="AMBIGUOUS_PROBE";return result;}
    if(collector.bad_identity){result.reason="PROBE_METADATA_ERROR";return result;}
    collector.query=RK3_QUERY_LEGACY_CLASS;
    result.query_error=adapter->enumerate(adapter->context,collector.query,collect,&collector);
    if(result.query_error){result.reason="CLASS_QUERY_ERROR";return result;}
    if(result.same_class_entry){result.status=RK3_DISCOVERY_PASS;result.reason="CLASS_MATCH";return result;}
    if(collector.bad_identity){result.reason="CLASS_METADATA_ERROR";return result;}
    if(collector.changed_identity){result.reason="PROBE_CHANGED_DURING_QUERY";return result;}
    /* A registry name is only a negative control. It can never rescue a miss
     * by the exact legacy IOServiceMatching class query. */
    result.status=RK3_DISCOVERY_FAIL;
    result.reason=result.class_count?"WRONG_ENTRY":"CLASS_MISS";
    result.name_queried=true;
    collector.query=RK3_QUERY_LEGACY_NAME;
    result.name_query_error=adapter->enumerate(adapter->context,collector.query,collect,&collector);
    if(result.name_query_error)result.same_name_entry=false;
    if(!result.class_count&&result.same_name_entry)result.reason="NAME_ONLY_MATCH";
    /* A miss is meaningful only while the independent probe still exists.
     * Removal or identity changes between queries must never prove failure. */
    RK3DiscoveryResult recheck={0};
    Collector verification={.result=&recheck,.query=RK3_QUERY_PROBE};
    int32_t error=adapter->enumerate(adapter->context,RK3_QUERY_PROBE,collect,&verification);
    if(error||recheck.probe_count!=1||verification.bad_identity||
       recheck.probe.entry_id!=result.probe.entry_id) {
        result.status=RK3_DISCOVERY_INCONCLUSIVE;
        result.reason=error?"PROBE_RECHECK_ERROR":"PROBE_CHANGED_DURING_QUERY";
        result.query_error=error;
    }
    return result;
}
const char *rk3_discovery_status_name(RK3DiscoveryStatus status) {
    switch(status){case RK3_DISCOVERY_PASS:return "PASS";case RK3_DISCOVERY_FAIL:return "FAIL";
        case RK3_DISCOVERY_INCONCLUSIVE:return "INCONCLUSIVE";default:return "INVALID";}
}
