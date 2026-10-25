#include "AudioRing.h"
#include "AudioCodec.h"

#include <string.h>

#define NI_AUDIO_RING_SIGNATURE UINT32_C(0x4e495247)

static bool span_valid(const void *pointer, size_t bytes) {
    return pointer != NULL && bytes <= UINTPTR_MAX - (uintptr_t)pointer;
}
static bool overlaps(const void *a, size_t a_bytes, const void *b, size_t b_bytes) {
    uintptr_t a_start=(uintptr_t)a,b_start=(uintptr_t)b;
    return a_start < b_start+b_bytes && b_start < a_start+a_bytes;
}
static bool valid(const NIAudioRing *state) {
    return state != NULL && state->signature == NI_AUDIO_RING_SIGNATURE &&
        state->wire_capacity >= NI_AUDIO_RING_WIRE_BYTES &&
        state->packed_capacity >= NI_AUDIO_RING_PACKED_BYTES &&
        span_valid(state->wire,NI_AUDIO_RING_WIRE_BYTES) &&
        span_valid(state->packed,NI_AUDIO_RING_PACKED_BYTES) &&
        !overlaps(state->wire,NI_AUDIO_RING_WIRE_BYTES,state->packed,NI_AUDIO_RING_PACKED_BYTES) &&
        !overlaps(state,sizeof(*state),state->wire,NI_AUDIO_RING_WIRE_BYTES) &&
        !overlaps(state,sizeof(*state),state->packed,NI_AUDIO_RING_PACKED_BYTES);
}

NIAudioRingResult ni_audio_ring_init(NIAudioRing *state,
    uint8_t *wire, size_t wire_capacity, uint8_t *packed, size_t packed_capacity) {
    if(state == NULL || wire_capacity < NI_AUDIO_RING_WIRE_BYTES ||
       packed_capacity < NI_AUDIO_RING_PACKED_BYTES ||
       !span_valid(wire,NI_AUDIO_RING_WIRE_BYTES) ||
       !span_valid(packed,NI_AUDIO_RING_PACKED_BYTES) ||
       overlaps(wire,NI_AUDIO_RING_WIRE_BYTES,packed,NI_AUDIO_RING_PACKED_BYTES) ||
       overlaps(state,sizeof(*state),wire,NI_AUDIO_RING_WIRE_BYTES) ||
       overlaps(state,sizeof(*state),packed,NI_AUDIO_RING_PACKED_BYTES)) return NI_AUDIO_RING_INVALID;
    *state=(NIAudioRing){NI_AUDIO_RING_SIGNATURE,wire,packed,wire_capacity,packed_capacity,true,false};
    return ni_audio_ring_reset(state);
}

NIAudioRingResult ni_audio_ring_reset(NIAudioRing *state) {
    if(!valid(state)) return NI_AUDIO_RING_INVALID;
    memset(state->wire,0,NI_AUDIO_RING_WIRE_BYTES);
    memset(state->packed,0,NI_AUDIO_RING_PACKED_BYTES);
    /* Original fillErrorCorrectionPatternMode2 alternates tag1,tag0 at
     * byte planes2/6 for stereo group0. Clip never rebuilds these lanes. */
    for(size_t frame=0;frame<NI_AUDIO_RING_FRAME_COUNT;++frame) state->wire[frame*8+2]=1;
    state->priming=true;
    return NI_AUDIO_RING_OK;
}

NIAudioRingResult ni_audio_ring_set_priming(NIAudioRing *state, bool priming) {
    if(!valid(state)) return NI_AUDIO_RING_INVALID;
    state->priming=priming;
    return NI_AUDIO_RING_OK;
}

static void clip(NIAudioRing *state, size_t first, size_t frames) {
    static const unsigned payload_lanes[6]={0,1,3,4,5,7};
    for(size_t index=0;index<frames;++index) {
        size_t frame=(first+index)%NI_AUDIO_RING_FRAME_COUNT;
        size_t source=(frame*6+NI_AUDIO_RING_PACKED_BYTES-2)%NI_AUDIO_RING_PACKED_BYTES;
        for(unsigned byte=0;byte<6;++byte)
            state->wire[frame*8+payload_lanes[byte]]=
                state->packed[(source+byte)%NI_AUDIO_RING_PACKED_BYTES];
    }
}

NIAudioRingResult ni_audio_ring_clip_packed_at(NIAudioRing *state,
    uint64_t first_sample_frame, size_t frames) {
    if(!valid(state) || frames > NI_AUDIO_RING_FRAME_COUNT) return NI_AUDIO_RING_INVALID;
    state->stream_written=true;
    if(state->priming) return NI_AUDIO_RING_PRIMING;
    clip(state,(size_t)(first_sample_frame%NI_AUDIO_RING_FRAME_COUNT),frames);
    return NI_AUDIO_RING_OK;
}

static void write_segment(NIAudioRing *state, size_t first,
    const float *samples, size_t frames) {
    for(size_t index=0;index<frames;++index) {
        size_t frame=first+index;
        (void)ni_audio_pcm24_write(samples[index*2],NI_AUDIO_BIG_ENDIAN,state->packed+frame*6);
        (void)ni_audio_pcm24_write(samples[index*2+1],NI_AUDIO_BIG_ENDIAN,state->packed+frame*6+3);
    }
    clip(state,first,frames);
}

NIAudioRingResult ni_audio_ring_write_f32_at(NIAudioRing *state,
    uint64_t first_sample_frame, const float *samples, size_t frames) {
    if(!valid(state) || frames > NI_AUDIO_RING_FRAME_COUNT) return NI_AUDIO_RING_INVALID;
    size_t bytes=frames*2*sizeof(float);
    if(frames != 0 && (!span_valid(samples,bytes) ||
       overlaps(samples,bytes,state->wire,NI_AUDIO_RING_WIRE_BYTES) ||
       overlaps(samples,bytes,state->packed,NI_AUDIO_RING_PACKED_BYTES) ||
       overlaps(samples,bytes,state,sizeof(*state)))) return NI_AUDIO_RING_INVALID;
    state->stream_written=true;
    if(state->priming) return NI_AUDIO_RING_PRIMING;
    size_t first=(size_t)(first_sample_frame%NI_AUDIO_RING_FRAME_COUNT);
    size_t tail=NI_AUDIO_RING_FRAME_COUNT-first;
    if(tail > frames) tail=frames;
    /* IOAudioStream::clipIfNecessary splits at the ring boundary: the tail
     * segment's AppleClip conversion AND NI wire copy precede head conversion.
     * This matters when a full-ring write also replaces the preceding sample. */
    write_segment(state,first,samples,tail);
    if(frames > tail) write_segment(state,0,samples+tail*2,frames-tail);
    return NI_AUDIO_RING_OK;
}

NIAudioRingResult ni_audio_ring_clear_stream_original(NIAudioRing *state) {
    if(!valid(state)) return NI_AUDIO_RING_INVALID;
    /* Literal byte footprint checked against 0x75b0..0x7605. Do not silently
     * turn the original routine into a tag-preserving or full-stereo clear. */
    for(size_t frame=0;frame<NI_AUDIO_RING_FRAME_COUNT;++frame) memset(state->wire+frame*8,0,6);
    return NI_AUDIO_RING_OK;
}
