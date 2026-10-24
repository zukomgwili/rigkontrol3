#include "MIDIStream.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint8_t bytes[32][256];size_t lengths[32];uint64_t times[32];size_t count;bool fail; } Capture;
static bool capture(void *context,uint64_t time,const uint8_t *bytes,size_t length) {
    Capture *out=context;
    if(out->fail)return false;
    assert(out->count<32&&length>0&&length<=256);
    memcpy(out->bytes[out->count],bytes,length);out->lengths[out->count]=length;
    out->times[out->count++]=time;return true;
}
static void feed(RK3MIDIStream *s,const uint8_t *bytes,size_t n,uint64_t time,Capture *out) {
    assert(rk3_midi_stream_feed(s,bytes,n,time,capture,out)==RK3_MIDI_STREAM_OK);
}
static void check(Capture *out,size_t index,const uint8_t *bytes,size_t length) {
    assert(index<out->count&&out->lengths[index]==length&&memcmp(out->bytes[index],bytes,length)==0);
}
static void messages(void) {
    RK3MIDIStream s;rk3_midi_stream_reset(&s);Capture out={0};
    feed(&s,(uint8_t[]){0x90,60},2,10,&out);assert(out.count==0);
    feed(&s,(uint8_t[]){100,61},2,20,&out);assert(out.count==1);
    feed(&s,(uint8_t[]){110},1,30,&out);assert(out.count==2);
    check(&out,0,(uint8_t[]){0x90,60,100},3);check(&out,1,(uint8_t[]){0x90,61,110},3);
    assert(out.times[0]==10&&out.times[1]==20);
    feed(&s,(uint8_t[]){0xc2,5,6,0xd2,7,8},6,40,&out);
    check(&out,2,(uint8_t[]){0xc2,5},2);check(&out,3,(uint8_t[]){0xc2,6},2);
    check(&out,4,(uint8_t[]){0xd2,7},2);check(&out,5,(uint8_t[]){0xd2,8},2);
    feed(&s,(uint8_t[]){0x80,62,0xf8,0,0xf2,1},6,50,&out);
    check(&out,6,(uint8_t[]){0xf8},1);check(&out,7,(uint8_t[]){0x80,62,0},3);
    feed(&s,(uint8_t[]){2,0xf1,3,0xf3,4,0xf6,6,7},8,60,&out);
    check(&out,8,(uint8_t[]){0xf2,1,2},3);assert(out.times[8]==50);
    check(&out,9,(uint8_t[]){0xf1,3},2);check(&out,10,(uint8_t[]){0xf3,4},2);check(&out,11,(uint8_t[]){0xf6},1);
    assert(out.count==12); /* data after system-common cannot use channel running status */
}
static void sysex(void) {
    RK3MIDIStream s;rk3_midi_stream_reset(&s);Capture out={0};
    feed(&s,(uint8_t[]){0xf0,1,2},3,10,&out);assert(out.count==0);
    feed(&s,(uint8_t[]){0xf8,3,0xf7,0x90,60,100},6,20,&out);
    check(&out,0,(uint8_t[]){0xf0,1,2},3);check(&out,1,(uint8_t[]){0xf8},1);
    check(&out,2,(uint8_t[]){3,0xf7},2);check(&out,3,(uint8_t[]){0x90,60,100},3);
    assert(out.times[0]==10&&out.times[1]==20&&out.times[2]==20);
    rk3_midi_stream_reset(&s);memset(&out,0,sizeof(out));
    uint8_t long_message[602];long_message[0]=0xf0;
    for(size_t i=1;i<601;++i)long_message[i]=(uint8_t)(i&127);
    long_message[601]=0xf7;
    for(size_t i=0;i<sizeof(long_message);++i)feed(&s,long_message+i,1,i,&out);
    assert(out.count==3&&out.lengths[0]==256&&out.lengths[1]==256&&out.lengths[2]==90);
    assert(memcmp(out.bytes[0],long_message,256)==0&&memcmp(out.bytes[1],long_message+256,256)==0);
    assert(memcmp(out.bytes[2],long_message+512,90)==0);
    assert(out.times[0]==0&&out.times[1]==256&&out.times[2]==512);
    rk3_midi_stream_reset(&s);memset(&out,0,sizeof(out));
    feed(&s,(uint8_t[]){0xf0,1,0x90,60,100},5,1,&out);
    check(&out,0,(uint8_t[]){0xf0,1,0xf7},3);check(&out,1,(uint8_t[]){0x90,60,100},3);
}
static void reset_and_legacy(void) {
    RK3MIDIStream s;rk3_midi_stream_reset(&s);Capture out={0};
    feed(&s,(uint8_t[]){0x90,60,0xf9,0xfd,0xfe,100,0xf5,3,61,110,0xfa,0xfb,0xfc,0xff},14,1,&out);
    assert(s.cable==3&&out.count==5);
    check(&out,0,(uint8_t[]){0x90,60,100},3);check(&out,1,(uint8_t[]){0xfa},1);
    check(&out,2,(uint8_t[]){0xfb},1);check(&out,3,(uint8_t[]){0xfc},1);check(&out,4,(uint8_t[]){0xff},1);
    feed(&s,(uint8_t[]){0x90,60},2,2,&out);rk3_midi_stream_reset(&s);
    feed(&s,(uint8_t[]){100,61,110},3,3,&out);assert(out.count==5&&s.cable==0);
    feed(&s,(uint8_t[]){0xf0,1},2,4,&out);rk3_midi_stream_reset(&s);
    feed(&s,(uint8_t[]){2,0xf7,0xf4,3},4,5,&out);assert(out.count==5);
    out.fail=true;
    assert(rk3_midi_stream_feed(&s,(uint8_t[]){0xf6},1,6,capture,&out)==RK3_MIDI_STREAM_EMIT_FAILED);
    assert(s.running_status==0&&s.message_length==0&&!s.in_sysex);
    out.fail=false;RK3MIDIStream saved=s;
    assert(rk3_midi_stream_feed(&s,NULL,1,7,capture,&out)==RK3_MIDI_STREAM_INVALID_ARGUMENT);
    assert(memcmp(&s,&saved,sizeof(s))==0);
    s.sysex_length=256;
    assert(rk3_midi_stream_feed(&s,(uint8_t[]){1},1,8,capture,&out)==RK3_MIDI_STREAM_INVALID_ARGUMENT);
}
static void every_split(void) {
    const uint8_t bytes[]={0x90,60,100,61,110,0xf0,1,2,0xf8,3,0xf7,0xc0,4};
    RK3MIDIStream s;Capture expected={0};rk3_midi_stream_reset(&s);
    feed(&s,bytes,sizeof(bytes),10,&expected);
    for(size_t split=0;split<=sizeof(bytes);++split) {
        Capture out={0};rk3_midi_stream_reset(&s);feed(&s,bytes,split,10,&out);feed(&s,bytes+split,sizeof(bytes)-split,10,&out);
        assert(out.count==expected.count);
        for(size_t i=0;i<out.count;++i)check(&out,i,expected.bytes[i],expected.lengths[i]);
    }
}
int main(void) { messages();sysex();reset_and_legacy();every_split();puts("MIDIStream: complete messages, running status, realtime, SysEx, resets and bounds passed");return 0; }
