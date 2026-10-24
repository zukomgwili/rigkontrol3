#include "AudioCodec.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const uint8_t samples0[12] = {0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b};
static const uint8_t samples1[12] = {0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2a,0x2b};
static const uint8_t mode3_frame0[16] = {
    0x10,0x11,0x12,0,0x13,0x14,0x15,1,0x20,0x21,0x22,2,0x23,0x24,0x25,3
};
static const uint8_t mode2_frame1[16] = {
    0x14,0x24,0x15,0x25,1,3,0x16,0x26,0x17,0x27,0x18,0x28,0,2,0x19,0x29
};

static void test_pcm24(void) {
    const uint8_t be[][3] = {{0x80,0,0},{0xc0,0,0},{0,0,0},{0x40,0,0},{0x7f,0xff,0xff}};
    const float values[] = {-1.0f,-0.5f,0.0f,0.5f,8388607.0f/8388608.0f};
    for (size_t i = 0; i < sizeof(values)/sizeof(values[0]); ++i) {
        uint8_t le[3] = {be[i][2],be[i][1],be[i][0]}, encoded[3];
        float decoded = 2;
        assert(ni_audio_pcm24_read(be[i],NI_AUDIO_BIG_ENDIAN,&decoded) == NI_AUDIO_OK);
        assert(decoded == values[i]);
        assert(ni_audio_pcm24_read(le,NI_AUDIO_LITTLE_ENDIAN,&decoded) == NI_AUDIO_OK);
        assert(decoded == values[i]);
        assert(ni_audio_pcm24_write(values[i],NI_AUDIO_BIG_ENDIAN,encoded) == NI_AUDIO_OK);
        assert(memcmp(encoded,be[i],3) == 0);
        assert(ni_audio_pcm24_write(values[i],NI_AUDIO_LITTLE_ENDIAN,encoded) == NI_AUDIO_OK);
        assert(memcmp(encoded,le,3) == 0);
    }
    uint8_t output[3] = {0xaa,0xaa,0xaa};
    const uint8_t one[3] = {0,0,1}, zero[3] = {0,0,0};
    assert(ni_audio_pcm24_write(1.0f/16777216.0f,NI_AUDIO_BIG_ENDIAN,output) == NI_AUDIO_OK);
    assert(memcmp(output,one,3) == 0);
    assert(ni_audio_pcm24_write(-1.0f/16777216.0f,NI_AUDIO_BIG_ENDIAN,output) == NI_AUDIO_OK);
    assert(memcmp(output,zero,3) == 0);
    assert(ni_audio_pcm24_write(INFINITY,NI_AUDIO_BIG_ENDIAN,output) == NI_AUDIO_OK);
    assert(memcmp(output,be[4],3) == 0);
    assert(ni_audio_pcm24_write(-INFINITY,NI_AUDIO_BIG_ENDIAN,output) == NI_AUDIO_OK);
    assert(memcmp(output,be[0],3) == 0);
    assert(ni_audio_pcm24_write(NAN,NI_AUDIO_BIG_ENDIAN,output) == NI_AUDIO_OK);
    assert(memcmp(output,zero,3) == 0);
    assert(ni_audio_pcm24_write(0.5f,(NIAudioByteOrder)9,output) == NI_AUDIO_INVALID_ARGUMENT);
    assert(memcmp(output,zero,3) == 0);
    float unchanged = 2.0f;
    assert(ni_audio_pcm24_read(NULL,NI_AUDIO_BIG_ENDIAN,&unchanged) == NI_AUDIO_INVALID_ARGUMENT);
    assert(unchanged == 2.0f);
}

static void test_recovered_output_vectors(void) {
    const uint8_t *rings[] = {samples0,samples1};
    NIAudioEncoder encoder;
    uint8_t wire[32];
    assert(ni_audio_encoder_init(&encoder,NI_AUDIO_MODE_3,2,12) == NI_AUDIO_OK);
    assert(ni_audio_encode(&encoder,rings,12,wire,sizeof(wire)) == NI_AUDIO_OK);
    assert(memcmp(wire,mode3_frame0,16) == 0);
    assert(wire[16] == 0x16 && wire[24] == 0x26 && encoder.frame == 0);
    assert(ni_audio_encoder_init(&encoder,NI_AUDIO_MODE_2,2,12) == NI_AUDIO_OK);
    assert(ni_audio_encode(&encoder,rings,12,wire,sizeof(wire)) == NI_AUDIO_OK);
    assert(memcmp(wire+16,mode2_frame1,16) == 0);
    /* Frame zero reads the previous ring frame's last two bytes. */
    assert(wire[0] == 0x1a && wire[1] == 0x2a && wire[2] == 0x1b && wire[3] == 0x2b);
    assert(encoder.frame == 0);
}

static void test_short_output_and_snapshot(NIAudioMode mode) {
    const uint8_t *rings[] = {samples0,samples1};
    uint8_t whole[81], fragmented[81];
    NIAudioEncoder encoder;
    assert(ni_audio_encoder_init(&encoder,mode,2,12) == NI_AUDIO_OK);
    assert(ni_audio_encode(&encoder,rings,12,whole,sizeof(whole)) == NI_AUDIO_OK);
    for (size_t split = 0; split <= sizeof(whole); ++split) {
        assert(ni_audio_encoder_init(&encoder,mode,2,12) == NI_AUDIO_OK);
        assert(ni_audio_encode(&encoder,rings,12,fragmented,split) == NI_AUDIO_OK);
        assert(ni_audio_encode(&encoder,rings,12,fragmented+split,sizeof(whole)-split) == NI_AUDIO_OK);
        assert(memcmp(whole,fragmented,sizeof(whole)) == 0);
    }
    assert(ni_audio_encoder_init(&encoder,mode,2,12) == NI_AUDIO_OK);
    for (size_t i = 0; i < sizeof(whole); ++i) {
        assert(ni_audio_encode(&encoder,rings,12,fragmented+i,1) == NI_AUDIO_OK);
    }
    assert(memcmp(whole,fragmented,sizeof(whole)) == 0);

    uint8_t changing0[12], changing1[12], snapshot[16];
    memcpy(changing0,samples0,12);memcpy(changing1,samples1,12);
    const uint8_t *changing[] = {changing0,changing1};
    assert(ni_audio_encoder_init(&encoder,mode,2,12) == NI_AUDIO_OK);
    assert(ni_audio_encode(&encoder,changing,12,snapshot,1) == NI_AUDIO_OK);
    memset(changing0,0,12);memset(changing1,0,12);
    assert(ni_audio_encode(&encoder,changing,12,snapshot+1,15) == NI_AUDIO_OK);
    assert(memcmp(snapshot,whole,16) == 0);
}

static void test_mode2_receive_phase(void) {
    /* Input begins with a check plane and then frame-zero right-channel bytes. */
    const uint8_t input[] = {1,3,0x13,0x23,0x14,0x24,0x15,0x25,0,2,0x16,0x26,0x17,0x27,0x18,0x28};
    uint8_t storage0[14] = {0}, storage1[14] = {0};
    storage0[0]=storage0[13]=storage1[0]=storage1[13]=0xa5;
    uint8_t *rings[] = {storage0+1,storage1+1};
    NIAudioDecoder decoder;
    assert(ni_audio_decoder_init(&decoder,NI_AUDIO_MODE_2,2,12,true) == NI_AUDIO_OK);
    assert(ni_audio_decoder_set_priming(&decoder,false) == NI_AUDIO_OK);
    for (size_t i=0;i<sizeof(input);++i) {
        assert(ni_audio_decode(&decoder,input+i,1,rings,12) == NI_AUDIO_OK);
    }
    assert(memcmp(rings[0]+3,samples0+3,6) == 0);
    assert(memcmp(rings[1]+3,samples1+3,6) == 0);
    assert(decoder.byte_cursor == 8 && decoder.check_plane && decoder.errors == 0);
    assert(storage0[0] == 0xa5 && storage0[13] == 0xa5 && storage1[0] == 0xa5 && storage1[13] == 0xa5);
}

static void test_receive_chunk_invariance(NIAudioMode mode) {
    const uint8_t mode2_input[] = {
        1,3,0x13,0x23,0x14,0x24,0x15,0x25,0,2,0x16,0x26,0x17,0x27,0x18,0x28,
        1,3,0x19,0x29,0x1a,0x2a,0x1b,0x2b,0,2,0x10,0x20,0x11,0x21,0x12,0x22
    };
    const uint8_t mode3_input[] = {
        0x10,0x11,0x12,0,0x13,0x14,0x15,1,0x20,0x21,0x22,2,0x23,0x24,0x25,3,
        0x16,0x17,0x18,0,0x19,0x1a,0x1b,1,0x26,0x27,0x28,2,0x29,0x2a,0x2b,3
    };
    const uint8_t *input = mode == NI_AUDIO_MODE_2 ? mode2_input : mode3_input;
    uint8_t expected[2][12] = {{0}}, split_rings[2][12];
    uint8_t *whole_ptrs[] = {expected[0],expected[1]}, *split_ptrs[] = {split_rings[0],split_rings[1]};
    NIAudioDecoder decoder, whole;
    assert(ni_audio_decoder_init(&whole,mode,2,12,true) == NI_AUDIO_OK);
    assert(ni_audio_decoder_set_priming(&whole,false) == NI_AUDIO_OK);
    assert(ni_audio_decode(&whole,input,32,whole_ptrs,12) == NI_AUDIO_OK);
    assert(whole.errors == 0 && whole.ring_wraps == 1);
    for (size_t split=0;split<=32;++split) {
        memset(split_rings,0,sizeof(split_rings));
        assert(ni_audio_decoder_init(&decoder,mode,2,12,true) == NI_AUDIO_OK);
        assert(ni_audio_decoder_set_priming(&decoder,false) == NI_AUDIO_OK);
        assert(ni_audio_decode(&decoder,input,split,split_ptrs,12) == NI_AUDIO_OK);
        assert(ni_audio_decode(&decoder,input+split,32-split,split_ptrs,12) == NI_AUDIO_OK);
        assert(memcmp(split_rings,expected,sizeof(expected)) == 0);
        assert(decoder.byte_cursor == whole.byte_cursor && decoder.group == whole.group);
        assert(decoder.errors == whole.errors && decoder.ring_wraps == whole.ring_wraps);
    }
}

static void test_bad_checks_and_priming(void) {
    uint8_t storage[2][12] = {{0}};
    uint8_t *rings[] = {storage[0],storage[1]};
    NIAudioDecoder decoder;
    const uint8_t mode2_bad[] = {0x81,3,0x13,0x23,0x14,0x24,0x15,0x25,5,2,0xee,0xdd};
    assert(ni_audio_decoder_init(&decoder,NI_AUDIO_MODE_2,2,12,true) == NI_AUDIO_OK);
    assert(ni_audio_decoder_set_priming(&decoder,false) == NI_AUDIO_OK);
    assert(ni_audio_decode(&decoder,mode2_bad,sizeof(mode2_bad),rings,12) == NI_AUDIO_OK);
    assert(decoder.errors == (NI_AUDIO_ERROR_SYNC | NI_AUDIO_ERROR_DEVICE));
    assert(storage[0][3] == 0x13 && storage[0][6] == 0 && storage[1][6] == 0);
    /* Correction disabled preserves data while reporting the same latched error. */
    assert(ni_audio_decoder_init(&decoder,NI_AUDIO_MODE_2,2,12,false) == NI_AUDIO_OK);
    assert(ni_audio_decoder_set_priming(&decoder,false) == NI_AUDIO_OK);
    assert(ni_audio_decode(&decoder,mode2_bad,sizeof(mode2_bad),rings,12) == NI_AUDIO_OK);
    assert(storage[0][6] == 0xee && storage[1][6] == 0xdd && (decoder.errors & NI_AUDIO_ERROR_SYNC));

    uint8_t bad3[32];memcpy(bad3,mode3_frame0,16);memcpy(bad3+16,mode3_frame0,16);
    bad3[3] = 7;
    memset(storage,0,sizeof(storage));
    assert(ni_audio_decoder_init(&decoder,NI_AUDIO_MODE_3,2,12,true) == NI_AUDIO_OK);
    assert(ni_audio_decode(&decoder,bad3,16,rings,12) == NI_AUDIO_OK);
    assert(decoder.errors == NI_AUDIO_ERROR_SYNC);
    assert(memcmp(storage[0]+6,samples0,6) == 0); /* Error is latched after this group's audio. */
    assert(memcmp(storage[1]+6,(uint8_t[6]){0},6) == 0);
    assert(ni_audio_decode(&decoder,bad3+16,16,rings,12) == NI_AUDIO_OK);
    assert(memcmp(storage[0],(uint8_t[6]){0},6) == 0);

    memcpy(bad3,mode3_frame0,16);bad3[7]=0xfe;bad3[15]=0xfe;
    assert(ni_audio_decoder_init(&decoder,NI_AUDIO_MODE_3,2,12,true) == NI_AUDIO_OK);
    assert(ni_audio_decode(&decoder,bad3,16,rings,12) == NI_AUDIO_OK);
    assert(decoder.errors == 0); /* Odd tags ignored during priming. */
    assert(ni_audio_decoder_set_priming(&decoder,false) == NI_AUDIO_OK);
    assert(ni_audio_decode(&decoder,bad3,16,rings,12) == NI_AUDIO_OK);
    assert(decoder.errors == NI_AUDIO_ERROR_SYNC);
}

static void test_invalid_and_maximum(void) {
    NIAudioEncoder encoder={0}, saved_encoder;
    NIAudioDecoder decoder={0}, saved_decoder;
    uint8_t output[257];memset(output,0xa5,sizeof(output));
    uint8_t ring[6]={1,2,3,4,5,6};
    const uint8_t *input_rings[NI_AUDIO_MAX_GROUPS];
    uint8_t maximum_rings[NI_AUDIO_MAX_GROUPS][6]={{0}};
    uint8_t *dest_rings[NI_AUDIO_MAX_GROUPS];
    for (unsigned g=0;g<NI_AUDIO_MAX_GROUPS;++g) {input_rings[g]=ring;dest_rings[g]=maximum_rings[g];}
    assert(ni_audio_encode(&encoder,input_rings,6,output,1) == NI_AUDIO_INVALID_STATE);
    assert(ni_audio_decode(&decoder,output,1,dest_rings,6) == NI_AUDIO_INVALID_STATE);
    assert(ni_audio_encoder_init(&encoder,(NIAudioMode)0,2,12) == NI_AUDIO_INVALID_ARGUMENT);
    assert(ni_audio_encoder_init(&encoder,NI_AUDIO_MODE_2,33,12) == NI_AUDIO_INVALID_ARGUMENT);
    assert(ni_audio_decoder_init(&decoder,NI_AUDIO_MODE_3,0,12,true) == NI_AUDIO_INVALID_ARGUMENT);
    assert(ni_audio_decoder_init(&decoder,NI_AUDIO_MODE_2,1,7,true) == NI_AUDIO_INVALID_ARGUMENT);
    assert(ni_audio_encoder_init(&encoder,NI_AUDIO_MODE_3,32,6) == NI_AUDIO_OK);
    saved_encoder=encoder;
    assert(ni_audio_encode(&encoder,input_rings,5,output,256) == NI_AUDIO_RING_TOO_SMALL);
    assert(memcmp(&encoder,&saved_encoder,sizeof(encoder)) == 0 && output[0] == 0xa5);
    assert(ni_audio_encode(&encoder,input_rings,6,output,256) == NI_AUDIO_OK);
    assert(output[255] == 63 && output[256] == 0xa5);
    assert(ni_audio_decoder_init(&decoder,NI_AUDIO_MODE_3,32,6,true) == NI_AUDIO_OK);
    saved_decoder=decoder;
    assert(ni_audio_decode(&decoder,NULL,1,dest_rings,6) == NI_AUDIO_INVALID_ARGUMENT);
    assert(memcmp(&decoder,&saved_decoder,sizeof(decoder)) == 0);
    assert(ni_audio_decode(&decoder,output,256,dest_rings,6) == NI_AUDIO_OK);
    assert(decoder.ring_wraps == 1 && decoder.errors == 0);
    for(unsigned g=0;g<32;++g) assert(memcmp(maximum_rings[g],ring,6) == 0);
    assert(ni_audio_encode(&encoder,input_rings,6,NULL,0) == NI_AUDIO_OK);
    assert(ni_audio_decode(&decoder,NULL,0,dest_rings,6) == NI_AUDIO_OK);
    encoder.staged_size=1;
    assert(ni_audio_encode(&encoder,input_rings,6,output,1) == NI_AUDIO_INVALID_STATE);
    decoder.pending_size=8;
    assert(ni_audio_decode(&decoder,output,1,dest_rings,6) == NI_AUDIO_INVALID_STATE);
}

int main(void) {
    test_pcm24();
    test_recovered_output_vectors();
    test_short_output_and_snapshot(NI_AUDIO_MODE_2);
    test_short_output_and_snapshot(NI_AUDIO_MODE_3);
    test_mode2_receive_phase();
    test_receive_chunk_invariance(NI_AUDIO_MODE_2);
    test_receive_chunk_invariance(NI_AUDIO_MODE_3);
    test_bad_checks_and_priming();
    test_invalid_and_maximum();
    puts("AudioCodec: PCM24, NI vectors, streaming splits, wrap, corruption and bounds passed.");
    return 0;
}
