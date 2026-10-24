/* Sends only to the native Controller feedback destination, never DIN. */
#include "../Driver/NativeControl.h"
#include <CoreMIDI/CoreMIDI.h>
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static MIDIEndpointRef feedback_destination(void){
    for(ItemCount i=0;i<MIDIGetNumberOfDestinations();++i){
        MIDIEndpointRef endpoint=MIDIGetDestination(i);MIDIEntityRef entity=0;MIDIDeviceRef device=0;CFStringRef identity=NULL;
        if(MIDIEndpointGetEntity(endpoint,&entity)||!entity||MIDIEntityGetDevice(entity,&device)||!device)continue;
        if(!MIDIObjectGetStringProperty(device,CFSTR(RK3_CONTROLLER_IDENTITY_KEY),&identity)&&identity){
            int match=CFEqual(identity,CFSTR(RK3_NATIVE_DEVICE_UID ".controller"));CFRelease(identity);if(match)return endpoint;
        }
    }
    return 0;
}
int main(int argc,char **argv){
    if(argc!=3){fprintf(stderr,"Usage: %s CC VALUE (0..127)\n",argv[0]);return 2;}
    char *end;unsigned long cc=strtoul(argv[1],&end,10);if(*end||cc>127)return 2;
    unsigned long value=strtoul(argv[2],&end,10);if(*end||value>127)return 2;
    MIDIClientRef client=0;MIDIPortRef port=0;
    if(MIDIClientCreate(CFSTR("RK3 feedback verification"),NULL,NULL,&client)||MIDIOutputPortCreate(client,CFSTR("Controller feedback"),&port))return 1;
    MIDIEndpointRef target=feedback_destination();if(!target){fprintf(stderr,"Native Controller feedback destination unavailable\n");MIDIPortDispose(port);MIDIClientDispose(client);return 1;}
    uint8_t bytes[]={0xb0,(uint8_t)cc,(uint8_t)value};MIDIPacketList list;MIDIPacket *p=MIDIPacketListInit(&list);
    p=MIDIPacketListAdd(&list,sizeof(list),p,mach_absolute_time(),sizeof(bytes),bytes);
    OSStatus error=p?MIDISend(port,target,&list):kMIDIMessageSendErr;
    MIDIPortDispose(port);MIDIClientDispose(client);
    if(error){fprintf(stderr,"MIDI feedback error: %d\n",(int)error);return 1;}
    printf("Controller feedback accepted: CC%lu=%lu (channel1)\n",cc,value);return 0;
}
