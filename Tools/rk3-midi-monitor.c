/* Native Controller endpoint acceptance monitor; never opens USB directly. */
#include "../Driver/NativeControl.h"
#include <CoreMIDI/CoreMIDI.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static atomic_uint counts[128],minimum[128],maximum[128],last[128];
static void received(const MIDIPacketList *list,void *context,void *source){
    (void)context;(void)source;const MIDIPacket *packet=&list->packet[0];
    for(UInt32 p=0;p<list->numPackets;++p){
        for(UInt16 i=0;i+2<packet->length;){
            uint8_t status=packet->data[i];if(status!=0xb0){++i;continue;}
            uint8_t cc=packet->data[i+1],value=packet->data[i+2];i+=3;if(cc>127||value>127)continue;
            atomic_fetch_add(&counts[cc],1);atomic_store(&last[cc],value);
            unsigned old=atomic_load(&minimum[cc]);while(value<old&&!atomic_compare_exchange_weak(&minimum[cc],&old,value)){}
            old=atomic_load(&maximum[cc]);while(value>old&&!atomic_compare_exchange_weak(&maximum[cc],&old,value)){}
        }
        packet=MIDIPacketNext(packet);
    }
}
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static MIDIEndpointRef source_endpoint(void){
    for(ItemCount i=0;i<MIDIGetNumberOfSources();++i){
        MIDIEndpointRef source=MIDIGetSource(i);MIDIEntityRef entity=0;MIDIDeviceRef device=0;CFStringRef identity=NULL;
        if(MIDIEndpointGetEntity(source,&entity)||!entity||MIDIEntityGetDevice(entity,&device)||!device)continue;
        if(!MIDIObjectGetStringProperty(device,CFSTR(RK3_CONTROLLER_IDENTITY_KEY),&identity)&&identity){
            bool correct=CFEqual(identity,CFSTR(RK3_NATIVE_DEVICE_UID ".controller"));CFRelease(identity);if(correct)return source;
        }
    }
    return 0;
}
int main(int argc,char **argv){
    unsigned duration=30;
    if(argc==3&&strcmp(argv[1],"--seconds")==0){char *end;unsigned long n=strtoul(argv[2],&end,10);if(*end||n<1||n>180)return 2;duration=(unsigned)n;}
    else if(argc!=1){fprintf(stderr,"Usage:%s [--seconds 1..180]\n",argv[0]);return 2;}
    for(unsigned i=0;i<128;++i)atomic_init(&minimum[i],127);
    MIDIClientRef client=0;MIDIPortRef port=0;
    if(MIDIClientCreate(CFSTR("RK3 native verification"),NULL,NULL,&client)||MIDIInputPortCreate(client,CFSTR("Controller input"),received,NULL,&port))return 1;
    MIDIEndpointRef source=0;double deadline=now()+10;
    while(now()<deadline&&!source){source=source_endpoint();if(!source){struct timespec t={0,100000000};nanosleep(&t,NULL);}}
    if(!source||MIDIPortConnectSource(port,source,NULL)){fprintf(stderr,"Native Controller source unavailable\n");MIDIPortDispose(port);MIDIClientDispose(client);return 1;}
    printf("Native Controller monitor recording for %u seconds.\n",duration);fflush(stdout);
    deadline=now()+duration;while(now()<deadline){struct timespec t={0,100000000};nanosleep(&t,NULL);}
    MIDIPortDisconnectSource(port,source);MIDIPortDispose(port);MIDIClientDispose(client);
    unsigned total=0;
    for(unsigned i=0;i<128;++i){unsigned count=atomic_load(&counts[i]);if(!count)continue;total+=count;printf("CC%u count=%u min=%u max=%u last=%u\n",i,count,atomic_load(&minimum[i]),atomic_load(&maximum[i]),atomic_load(&last[i]));}
    printf("Total controller CC messages: %u\n",total);return total?0:1;
}
