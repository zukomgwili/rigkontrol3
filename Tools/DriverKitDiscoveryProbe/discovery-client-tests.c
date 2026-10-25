#include "discovery-result.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    RK3DiscoveryEntry entries[3][4];
    unsigned count[3],calls[3];
    int32_t error[3];
    bool later_probe;
    RK3DiscoveryEntry later_entries[4];
    unsigned later_count;
    int32_t later_error;
} Fixture;
static RK3DiscoveryEntry entry(uint64_t identifier) {
    RK3DiscoveryEntry value={.entry_id=identifier,.metadata_valid=true,.marker=true,.identifier_match=true};
    strcpy(value.kernel_class,"IOUserService");strcpy(value.user_class,RK3_DISCOVERY_LEGACY_CLASS);
    strcpy(value.user_server,RK3_DISCOVERY_IDENTIFIER);strcpy(value.bundle_identifier,RK3_DISCOVERY_IDENTIFIER);
    strcpy(value.name,RK3_DISCOVERY_LEGACY_CLASS);return value;
}
static int32_t enumerate(void *context,RK3DiscoveryQuery query,RK3DiscoveryVisit visit,void *visitor) {
    Fixture *fixture=context;assert(query<=RK3_QUERY_LEGACY_NAME);++fixture->calls[query];
    if(query==RK3_QUERY_PROBE&&fixture->later_probe&&fixture->calls[query]>1) {
        for(unsigned i=0;i<fixture->later_count;++i)visit(visitor,&fixture->later_entries[i]);
        return fixture->later_error;
    }
    for(unsigned i=0;i<fixture->count[query];++i)visit(visitor,&fixture->entries[query][i]);
    return fixture->error[query];
}
static RK3DiscoveryResult run(Fixture *fixture) {
    memset(fixture->calls,0,sizeof(fixture->calls));
    RK3DiscoveryAdapter adapter={fixture,enumerate};return rk3_discovery_evaluate(&adapter);
}
static void disappearing_probe_is_inconclusive(void) {
    Fixture fixture={0};fixture.entries[0][0]=entry(42);fixture.count[0]=1;fixture.later_probe=true;
    RK3DiscoveryResult result=run(&fixture);
    assert(result.status==RK3_DISCOVERY_INCONCLUSIVE&&strcmp(result.reason,"PROBE_CHANGED_DURING_QUERY")==0);
    fixture.later_entries[0]=entry(43);fixture.later_count=1;result=run(&fixture);
    assert(result.status==RK3_DISCOVERY_INCONCLUSIVE);
    fixture.later_entries[0]=entry(42);fixture.later_error=-8;result=run(&fixture);
    assert(result.status==RK3_DISCOVERY_INCONCLUSIVE&&strcmp(result.reason,"PROBE_RECHECK_ERROR")==0&&result.query_error==-8);
    fixture.later_error=0;fixture.later_entries[0].metadata_valid=false;result=run(&fixture);
    assert(result.status==RK3_DISCOVERY_INCONCLUSIVE);
    fixture.later_entries[0]=entry(42);fixture.later_entries[1]=entry(43);fixture.later_count=2;result=run(&fixture);
    assert(result.status==RK3_DISCOVERY_INCONCLUSIVE);
    fixture.later_count=1;result=run(&fixture);
    assert(result.status==RK3_DISCOVERY_FAIL&&strcmp(result.reason,"CLASS_MISS")==0);
}
static void unreadable_class_identity_is_inconclusive(void) {
    Fixture fixture={0};fixture.entries[0][0]=entry(42);fixture.count[0]=1;
    fixture.entries[1][0]=entry(0);fixture.entries[1][0].metadata_valid=false;fixture.count[1]=1;
    RK3DiscoveryResult result=run(&fixture);
    assert(result.status==RK3_DISCOVERY_INCONCLUSIVE&&strcmp(result.reason,"CLASS_METADATA_ERROR")==0);
    fixture.entries[1][0]=entry(99);fixture.entries[1][0].metadata_valid=false;result=run(&fixture);
    assert(result.status==RK3_DISCOVERY_INCONCLUSIVE);
    fixture.entries[1][1]=entry(42);fixture.count[1]=2;result=run(&fixture);
    assert(result.status==RK3_DISCOVERY_PASS&&result.same_class_entry);
}
static void exact_entry(void) {
    Fixture fixture={0};fixture.entries[0][0]=fixture.entries[1][0]=entry(42);
    fixture.count[0]=fixture.count[1]=1;RK3DiscoveryResult result=run(&fixture);
    assert(result.status==RK3_DISCOVERY_PASS&&result.probe.entry_id==42&&result.same_class_entry);
    assert(fixture.calls[0]==1&&fixture.calls[1]==1&&!fixture.calls[2]);
    assert(strcmp(result.probe.kernel_class,"IOUserService")==0);
}
static void wrong_entry_is_not_pass(void) {
    Fixture fixture={0};fixture.entries[0][0]=entry(42);fixture.entries[1][0]=entry(99);
    fixture.count[0]=fixture.count[1]=1;RK3DiscoveryResult result=run(&fixture);
    assert(result.status==RK3_DISCOVERY_FAIL&&!result.same_class_entry);
    assert(strcmp(result.reason,"WRONG_ENTRY")==0);
}
static void name_only_is_not_class(void) {
    Fixture fixture={0};fixture.entries[0][0]=fixture.entries[2][0]=entry(42);
    fixture.count[0]=fixture.count[2]=1;RK3DiscoveryResult result=run(&fixture);
    assert(result.status==RK3_DISCOVERY_FAIL&&!result.same_class_entry&&result.same_name_entry);
    assert(strcmp(result.reason,"NAME_ONLY_MATCH")==0&&result.name_queried);
    fixture.entries[2][0].entry_id=99;result=run(&fixture);
    assert(result.status==RK3_DISCOVERY_FAIL&&!result.same_name_entry&&strcmp(result.reason,"CLASS_MISS")==0);
}
static void identity_and_errors(void) {
    Fixture fixture={0};fixture.entries[1][0]=entry(99);fixture.count[1]=1;
    RK3DiscoveryResult result=run(&fixture);
    assert(result.status==RK3_DISCOVERY_INCONCLUSIVE&&strcmp(result.reason,"NO_PROBE")==0&&!fixture.calls[1]);
    fixture.entries[0][0]=entry(42);fixture.count[0]=1;
    fixture.entries[0][0].marker=false;result=run(&fixture);
    assert(result.status==RK3_DISCOVERY_INCONCLUSIVE&&strcmp(result.reason,"NO_PROBE")==0);
    fixture.entries[0][0].marker=true;fixture.entries[0][0].identifier_match=false;result=run(&fixture);
    assert(result.status==RK3_DISCOVERY_INCONCLUSIVE&&strcmp(result.reason,"NO_PROBE")==0);
    fixture.entries[0][0]=entry(42);fixture.entries[0][1]=entry(43);fixture.count[0]=2;result=run(&fixture);
    assert(result.status==RK3_DISCOVERY_INCONCLUSIVE&&strcmp(result.reason,"AMBIGUOUS_PROBE")==0);
    fixture.count[0]=1;fixture.entries[0][0].metadata_valid=false;result=run(&fixture);
    assert(result.status==RK3_DISCOVERY_INCONCLUSIVE&&strcmp(result.reason,"PROBE_METADATA_ERROR")==0);
    fixture.entries[0][0]=entry(0);result=run(&fixture);
    assert(result.status==RK3_DISCOVERY_INCONCLUSIVE&&strcmp(result.reason,"PROBE_METADATA_ERROR")==0);
    fixture.entries[0][0]=entry(42);fixture.error[0]=-5;result=run(&fixture);
    assert(result.status==RK3_DISCOVERY_INCONCLUSIVE&&strcmp(result.reason,"PROBE_QUERY_ERROR")==0&&result.query_error==-5);
    fixture.error[0]=0;fixture.error[1]=-6;fixture.entries[1][0]=entry(42);result=run(&fixture);
    assert(result.status==RK3_DISCOVERY_INCONCLUSIVE&&strcmp(result.reason,"CLASS_QUERY_ERROR")==0&&result.query_error==-6);
    fixture.error[1]=0;fixture.entries[1][0].marker=false;result=run(&fixture);
    assert(result.status==RK3_DISCOVERY_INCONCLUSIVE&&strcmp(result.reason,"PROBE_CHANGED_DURING_QUERY")==0);
    fixture.entries[1][0]=entry(99);fixture.entries[1][1]=entry(42);fixture.count[1]=2;result=run(&fixture);
    assert(result.status==RK3_DISCOVERY_PASS&&result.same_class_entry&&result.class_count==2);
    fixture.count[1]=0;fixture.error[2]=-7;result=run(&fixture);
    assert(result.status==RK3_DISCOVERY_FAIL&&strcmp(result.reason,"CLASS_MISS")==0&&result.name_query_error==-7);
    result=rk3_discovery_evaluate(NULL);assert(result.status==RK3_DISCOVERY_INCONCLUSIVE);
}
int main(int argc,char **argv) {
    if(argc!=2||strcmp(argv[1],"--fixture")!=0){fputs("Usage: discovery-client-tests --fixture\n",stderr);return 2;}
    disappearing_probe_is_inconclusive();unreadable_class_identity_is_inconclusive();exact_entry();wrong_entry_is_not_pass();name_only_is_not_class();identity_and_errors();
    puts("FIXTURE ONLY: exact registry-entry correlation passed; no OS discovery was executed.");
    return 0;
}
