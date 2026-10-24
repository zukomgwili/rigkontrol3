#include "../DeviceProtocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void commands(void) {
    static const struct {
        uint32_t rate;
        uint8_t bits;
        uint16_t capacity;
        uint8_t expected[6];
    } vectors[] = {
        {44100,24,128,{9,0,2,128,0,1}},
        {48000,24,136,{9,1,2,136,0,1}},
        {88200,24,176,{9,4,2,176,0,1}},
        {96000,16,184,{9,2,1,184,0,1}},
        {192000,24,280,{9,3,2,24,1,1}}
    };
    uint8_t command[6];
    for (size_t i = 0; i < sizeof(vectors)/sizeof(vectors[0]); ++i) {
        assert(rk3_audio_parameters(vectors[i].rate, vectors[i].bits,
                                    vectors[i].capacity, command));
        assert(memcmp(command, vectors[i].expected, 6) == 0);
    }
    const uint8_t stop[] = {9,255,255,128,0,1};
    assert(rk3_audio_stop(128,command));
    assert(memcmp(command,stop,6)==0);
    memset(command,0xaa,6);
    assert(!rk3_audio_parameters(32000,24,128,command));
    assert(!rk3_audio_parameters(48000,32,128,command));
    assert(!rk3_audio_parameters(48000,24,513,command));
    assert(!rk3_audio_stop(0,command));
    assert(!rk3_audio_stop(128,NULL));
    for(size_t i=0;i<6;++i) assert(command[i]==0xaa);
    uint16_t capacity=0;
    assert(rk3_audio_capacity(48000,2,2,2,&capacity) && capacity==136);
    assert(rk3_audio_capacity(44100,0,2,2,&capacity) && capacity==96);
    assert(rk3_audio_capacity(192000,3,8,8,&capacity) && capacity==512);
    assert(!rk3_audio_capacity(48000,1,2,2,&capacity));
    assert(!rk3_audio_capacity(48000,2,1,2,&capacity));
    assert(!rk3_audio_capacity(48000,2,0,0,&capacity));
}

static void info(void) {
    /* Synthetic descriptor exercises summed channels and exact NI offsets. */
    uint8_t reply[64]={1,0x34,0x12,0x56,0,0,0,0,2,2,4,6,1,3,2};
    rk3_device_info output={0};
    assert(rk3_device_info_parse(reply,15,&output));
    assert(output.input_channels==8 && output.output_channels==6);
    assert(output.wire_mode==2 && output.midi_inputs==3 && output.midi_outputs==1);
    assert(output.raw[0]==0x34 && output.raw[13]==2);
    for(size_t n=0;n<15;++n) assert(!rk3_device_info_parse(reply,n,&output));
    assert(!rk3_device_info_parse(reply,65,&output));
    reply[0]=3;
    assert(!rk3_device_info_parse(reply,15,&output));
    assert(!rk3_device_info_parse(NULL,15,&output));
    assert(output.input_channels==8);
    const uint8_t ack[]={9,1};
    uint8_t result=0;
    assert(rk3_audio_ack(ack,2,&result) && result==1);
    assert(!rk3_audio_ack(ack,1,&result));
    assert(!rk3_audio_ack(reply,15,&result));
}

int main(void) {
    commands(); info();
    puts("DeviceProtocol: NI command vectors, descriptor bounds and capacity checks passed");
    return 0;
}
