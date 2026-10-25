#include "AudioRing.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    NIAudioRing state;
    uint8_t wire[NI_AUDIO_RING_WIRE_BYTES+16];
    uint8_t packed[NI_AUDIO_RING_PACKED_BYTES+16];
} Fixture;

static void initialize(Fixture *fixture) {
    memset(fixture,0,sizeof(*fixture));
    memset(fixture->wire,0xa5,sizeof(fixture->wire));
    memset(fixture->packed,0x5a,sizeof(fixture->packed));
    assert(ni_audio_ring_init(&fixture->state,fixture->wire,sizeof(fixture->wire),
                             fixture->packed,sizeof(fixture->packed)) == NI_AUDIO_RING_OK);
}
static void guards(const Fixture *fixture) {
    for(size_t i=NI_AUDIO_RING_WIRE_BYTES;i<sizeof(fixture->wire);++i) assert(fixture->wire[i] == 0xa5);
    for(size_t i=NI_AUDIO_RING_PACKED_BYTES;i<sizeof(fixture->packed);++i) assert(fixture->packed[i] == 0x5a);
}
static void expect_frame(const Fixture *fixture, size_t frame, const uint8_t bytes[8]) {
    assert(memcmp(fixture->wire+frame*8,bytes,8) == 0);
}
static void test_reset_and_priming(void) {
    Fixture fixture;initialize(&fixture);
    for(size_t i=0;i<NI_AUDIO_RING_WIRE_BYTES;++i) assert(fixture.wire[i] == (i%8 == 2 ? 1 : 0));
    for(size_t i=0;i<NI_AUDIO_RING_PACKED_BYTES;++i) assert(fixture.packed[i] == 0);
    assert(fixture.state.priming && !fixture.state.stream_written);
    const float samples[2]={0.5f,-0.5f};
    assert(ni_audio_ring_write_f32_at(&fixture.state,29,samples,1) == NI_AUDIO_RING_PRIMING);
    assert(fixture.state.stream_written);
    assert(ni_audio_ring_clip_packed_at(&fixture.state,29,1) == NI_AUDIO_RING_PRIMING);
    for(size_t i=0;i<NI_AUDIO_RING_PACKED_BYTES;++i) assert(fixture.packed[i] == 0);
    for(size_t i=0;i<NI_AUDIO_RING_WIRE_BYTES;++i) assert(fixture.wire[i] == (i%8 == 2 ? 1 : 0));
    assert(ni_audio_ring_set_priming(&fixture.state,false) == NI_AUDIO_RING_OK);
    assert(ni_audio_ring_write_f32_at(&fixture.state,0,NULL,0) == NI_AUDIO_RING_OK);
    assert(ni_audio_ring_reset(&fixture.state) == NI_AUDIO_RING_OK);
    assert(fixture.state.priming && fixture.state.stream_written);
    guards(&fixture);
}
static void test_original_phase_and_cross_call(void) {
    Fixture fixture;initialize(&fixture);
    assert(ni_audio_ring_set_priming(&fixture.state,false) == NI_AUDIO_RING_OK);
    const uint8_t previous[6]={0xa1,0xa2,0xa3,0xa4,0xa5,0xa6};
    const uint8_t first[6]={0x11,0x22,0x33,0x44,0x55,0x66};
    const uint8_t second[6]={0x12,0x34,0x56,0x45,0x67,0x89};
    memcpy(fixture.packed+(NI_AUDIO_RING_FRAME_COUNT-1)*6,previous,6);
    memcpy(fixture.packed,first,6);
    assert(ni_audio_ring_clip_packed_at(&fixture.state,0,1) == NI_AUDIO_RING_OK);
    const uint8_t expected_first[8]={0xa5,0xa6,1,0x11,0x22,0x33,0,0x44};
    const uint8_t silent[8]={0,0,1,0,0,0,0,0};
    expect_frame(&fixture,0,expected_first);
    expect_frame(&fixture,1,silent); /* Last right low bytes are not pushed early. */
    memcpy(fixture.packed+6,second,6);
    fixture.wire[8+2]=0x9f;fixture.wire[8+6]=0x2e;
    assert(ni_audio_ring_clip_packed_at(&fixture.state,1,1) == NI_AUDIO_RING_OK);
    const uint8_t expected_second[8]={0x55,0x66,0x9f,0x12,0x34,0x56,0x2e,0x45};
    expect_frame(&fixture,1,expected_second);
    expect_frame(&fixture,0,expected_first);
    guards(&fixture);
}
static void test_live_descriptor_views_and_wrap(void) {
    Fixture fixture;initialize(&fixture);
    assert(ni_audio_ring_set_priming(&fixture.state,false) == NI_AUDIO_RING_OK);
    /* A synthetic future descriptor holds external ring views, not copies.
     * This tests the required CPU alias semantics only; it says nothing about
     * current IOUSBLib permission to modify an outstanding DMA buffer. */
    uint8_t *pending_tail=fixture.wire+(NI_AUDIO_RING_FRAME_COUNT-1)*8;
    uint8_t *pending_head=fixture.wire;
    uint8_t before[8];memcpy(before,pending_tail,8);
    const uint8_t prior[6]={0xa1,0xa2,0xa3,0xa4,0xa5,0xa6};
    const uint8_t last[6]={0xb1,0xb2,0xb3,0xb4,0xb5,0xb6};
    const uint8_t first[6]={0xc1,0xc2,0xc3,0xc4,0xc5,0xc6};
    memcpy(fixture.packed+(NI_AUDIO_RING_FRAME_COUNT-2)*6,prior,6);
    memcpy(fixture.packed+(NI_AUDIO_RING_FRAME_COUNT-1)*6,last,6);
    memcpy(fixture.packed,first,6);
    /* UINT64_MAX mod16384 is16383: normalize before multiplying. */
    assert(ni_audio_ring_clip_packed_at(&fixture.state,UINT64_MAX,2) == NI_AUDIO_RING_OK);
    const uint8_t tail[8]={0xa5,0xa6,1,0xb1,0xb2,0xb3,0,0xb4};
    const uint8_t head[8]={0xb5,0xb6,1,0xc1,0xc2,0xc3,0,0xc4};
    assert(memcmp(pending_tail,tail,8) == 0 && memcmp(pending_head,head,8) == 0);
    assert(memcmp(before,pending_tail,8) != 0);
    guards(&fixture);
}
static void test_addressed_float_conversion(void) {
    Fixture fixture;initialize(&fixture);
    assert(ni_audio_ring_set_priming(&fixture.state,false) == NI_AUDIO_RING_OK);
    const float samples[4]={0x112233/8388608.0f,0x445566/8388608.0f,
                            0x123456/8388608.0f,0x456789/8388608.0f};
    assert(ni_audio_ring_write_f32_at(&fixture.state,NI_AUDIO_RING_FRAME_COUNT-1,samples,2) == NI_AUDIO_RING_OK);
    const uint8_t packed_last[6]={0x11,0x22,0x33,0x44,0x55,0x66};
    const uint8_t packed_first[6]={0x12,0x34,0x56,0x45,0x67,0x89};
    assert(memcmp(fixture.packed+(NI_AUDIO_RING_FRAME_COUNT-1)*6,packed_last,6) == 0);
    assert(memcmp(fixture.packed,packed_first,6) == 0);
    const uint8_t tail[8]={0,0,1,0x11,0x22,0x33,0,0x44};
    const uint8_t head[8]={0x55,0x66,1,0x12,0x34,0x56,0,0x45};
    expect_frame(&fixture,NI_AUDIO_RING_FRAME_COUNT-1,tail);
    expect_frame(&fixture,0,head);
    /* An unrelated future host position does not depend on a FIFO cursor. */
    assert(ni_audio_ring_write_f32_at(&fixture.state,1234,samples,1) == NI_AUDIO_RING_OK);
    expect_frame(&fixture,1234,tail);
    expect_frame(&fixture,0,head);
    guards(&fixture);
}
static void test_split_host_blocks(void) {
    Fixture whole,split;initialize(&whole);initialize(&split);
    assert(ni_audio_ring_set_priming(&whole.state,false) == NI_AUDIO_RING_OK);
    assert(ni_audio_ring_set_priming(&split.state,false) == NI_AUDIO_RING_OK);
    float samples[258];
    for(size_t i=0;i<258;++i) samples[i]=(float)(i*1117+1)/8388608.0f;
    assert(ni_audio_ring_write_f32_at(&whole.state,16320,samples,129) == NI_AUDIO_RING_OK);
    assert(ni_audio_ring_write_f32_at(&split.state,16320,samples,17) == NI_AUDIO_RING_OK);
    assert(ni_audio_ring_write_f32_at(&split.state,16337,samples+34,47) == NI_AUDIO_RING_OK);
    assert(ni_audio_ring_write_f32_at(&split.state,16384,samples+128,65) == NI_AUDIO_RING_OK);
    assert(memcmp(whole.packed,split.packed,NI_AUDIO_RING_PACKED_BYTES) == 0);
    assert(memcmp(whole.wire,split.wire,NI_AUDIO_RING_WIRE_BYTES) == 0);
    guards(&whole);guards(&split);
}
static void test_framework_wrap_conversion_order(void) {
    Fixture fixture;initialize(&fixture);
    assert(ni_audio_ring_set_priming(&fixture.state,false) == NI_AUDIO_RING_OK);
    const uint8_t old_previous[6]={0xa1,0xa2,0xa3,0xa4,0xa5,0xa6};
    memcpy(fixture.packed+4*6,old_previous,6);
    float samples[NI_AUDIO_RING_FRAME_COUNT*2];
    for(size_t frame=0;frame<NI_AUDIO_RING_FRAME_COUNT;++frame) {
        samples[frame*2]=0x112233/8388608.0f;
        samples[frame*2+1]=0x445566/8388608.0f;
    }
    assert(ni_audio_ring_write_f32_at(&fixture.state,5,samples,NI_AUDIO_RING_FRAME_COUNT) == NI_AUDIO_RING_OK);
    /* IOAudioStream clips the tail segment before converting the wrapped head.
     * The first wire frame therefore used the OLD previous frame's right tail,
     * even though the head conversion later replaced packed frame4. */
    const uint8_t first_wire[8]={0xa5,0xa6,1,0x11,0x22,0x33,0,0x44};
    expect_frame(&fixture,5,first_wire);
    assert(fixture.packed[4*6+4] == 0x55 && fixture.packed[4*6+5] == 0x66);
    guards(&fixture);
}

static void test_original_inactive_clear(void) {
    Fixture fixture;initialize(&fixture);
    assert(ni_audio_ring_set_priming(&fixture.state,false) == NI_AUDIO_RING_OK);
    assert(ni_audio_ring_clip_packed_at(&fixture.state,0,0) == NI_AUDIO_RING_OK);
    for(size_t i=0;i<NI_AUDIO_RING_WIRE_BYTES;++i) fixture.wire[i]=(uint8_t)(0xa0+i%8);
    memset(fixture.packed,0x77,NI_AUDIO_RING_PACKED_BYTES);
    assert(ni_audio_ring_clear_stream_original(&fixture.state) == NI_AUDIO_RING_OK);
    for(size_t i=0;i<NI_AUDIO_RING_WIRE_BYTES;++i) assert(fixture.wire[i] == (i%8 < 6 ? 0 : 0xa0+i%8));
    for(size_t i=0;i<NI_AUDIO_RING_PACKED_BYTES;++i) assert(fixture.packed[i] == 0x77);
    assert(fixture.state.stream_written); /* Original caller clears this separately. */
    guards(&fixture);
}
static void test_invalid_bounds(void) {
    Fixture fixture;initialize(&fixture);NIAudioRing prior=fixture.state;
    assert(ni_audio_ring_init(&fixture.state,fixture.wire,NI_AUDIO_RING_WIRE_BYTES-1,
                             fixture.packed,sizeof(fixture.packed)) == NI_AUDIO_RING_INVALID);
    assert(memcmp(&fixture.state,&prior,sizeof(prior)) == 0);
    assert(ni_audio_ring_init(&fixture.state,fixture.wire,sizeof(fixture.wire),
                             fixture.wire,NI_AUDIO_RING_PACKED_BYTES) == NI_AUDIO_RING_INVALID);
    assert(ni_audio_ring_init((NIAudioRing *)(void *)fixture.wire,fixture.wire,sizeof(fixture.wire),
                             fixture.packed,sizeof(fixture.packed)) == NI_AUDIO_RING_INVALID);
    assert(ni_audio_ring_set_priming(&fixture.state,false) == NI_AUDIO_RING_OK);
    assert(ni_audio_ring_clip_packed_at(&fixture.state,0,NI_AUDIO_RING_FRAME_COUNT+1) == NI_AUDIO_RING_INVALID);
    assert(ni_audio_ring_write_f32_at(&fixture.state,0,NULL,1) == NI_AUDIO_RING_INVALID);
    assert(ni_audio_ring_write_f32_at(&fixture.state,0,(const float *)(const void *)fixture.wire,1) == NI_AUDIO_RING_INVALID);
    assert(ni_audio_ring_write_f32_at(&fixture.state,0,(const float *)(const void *)fixture.packed,1) == NI_AUDIO_RING_INVALID);
    assert(!fixture.state.stream_written);
    assert(ni_audio_ring_clip_packed_at(&fixture.state,0,NI_AUDIO_RING_FRAME_COUNT) == NI_AUDIO_RING_OK);
    for(size_t i=0;i<NI_AUDIO_RING_WIRE_BYTES;++i) assert(fixture.wire[i] == (i%8 == 2 ? 1 : 0));
    guards(&fixture);
}
int main(void) {
    test_reset_and_priming();test_original_phase_and_cross_call();
    test_live_descriptor_views_and_wrap();test_addressed_float_conversion();
    test_split_host_blocks();test_framework_wrap_conversion_order();test_original_inactive_clear();test_invalid_bounds();
    puts("AudioRing: NI mode2 sample placement, cross-call carry, live views, wrap, priming and original clear passed.");
    return 0;
}
