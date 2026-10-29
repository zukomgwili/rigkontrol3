/* Original-derived software-gain vectors. No hardware or system clients. */
#include "../InputGain.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static uint32_t bits(float x){uint32_t value;memcpy(&value,&x,4);return value;}
static float sample(uint32_t value){float x;memcpy(&x,&value,4);return x;}
static uint64_t pair(uint32_t l,uint32_t r){return ((uint64_t)r<<32)|l;}
static void bounds(void){
    static const struct {uint32_t inputs,outputs,set_count,get_count;} cases[]={
        {0,0,0,0},{0,2,2,0},{1,1,0,1},{3,0,2,3},{1,4,4,1},
        {2,2,2,2},{64,64,64,64},{65,1,64,64},{1,65,64,1},{UINT32_MAX,0,64,64}};
    for(size_t i=0;i<sizeof(cases)/sizeof(*cases);++i){
        RK3InputGain gain;rk3_input_gain_init(&gain);
        for(uint32_t c=0;c<66;++c){
            uint32_t value=UINT32_C(0x12345678);
            assert(rk3_input_gain_get(&gain,cases[i].inputs,c,&value)==(c<cases[i].get_count));
            assert(value==(c<cases[i].get_count?UINT32_C(0x3f800000):UINT32_C(0x12345678)));
            assert(rk3_input_gain_set(&gain,cases[i].inputs,cases[i].outputs,c,UINT32_C(0x40000000))==(c<cases[i].set_count));
        }
        uint32_t value=19;
        assert(!rk3_input_gain_get(&gain,cases[i].inputs,UINT32_MAX,&value)&&value==19);
        assert(!rk3_input_gain_set(&gain,cases[i].inputs,cases[i].outputs,UINT32_MAX,0));
    }
}
static void raw_values(void){
    static const uint32_t values[]={0,UINT32_C(0x80000000),UINT32_C(0x3f800000),UINT32_C(0xbf800000),
        UINT32_C(0x3f000000),UINT32_C(0x40000000),1,UINT32_C(0x80000001),UINT32_C(0x007fffff),
        UINT32_C(0x7f7fffff),UINT32_C(0xff7fffff),UINT32_C(0x7f800000),UINT32_C(0xff800000),
        UINT32_C(0x7fc01234),UINT32_C(0xffc01234),UINT32_C(0x7f800001),UINT32_C(0xff800001)};
    RK3InputGain gain;rk3_input_gain_init(&gain);
    for(uint32_t c=0;c<64;++c){uint32_t value=0;assert(rk3_input_gain_get(&gain,64,c,&value)&&value==UINT32_C(0x3f800000));}
    for(size_t i=0;i<sizeof(values)/sizeof(*values);++i){
        uint32_t value=0;
        assert(rk3_input_gain_set(&gain,64,64,63,values[i]));
        assert(rk3_input_gain_get(&gain,64,63,&value)&&value==values[i]);
        assert((uint32_t)(rk3_input_gain_snapshot(&gain,31)>>32)==values[i]);
        assert((uint32_t)rk3_input_gain_snapshot(&gain,31)==UINT32_C(0x3f800000));
    }
    uint32_t random=UINT32_C(0x12345678);
    for(unsigned i=0;i<10000;++i){
        random=random*UINT32_C(1664525)+UINT32_C(1013904223);uint32_t out=0;
        assert(rk3_input_gain_set(&gain,2,2,0,random));assert(rk3_input_gain_get(&gain,2,0,&out)&&out==random);
    }
    assert(!rk3_input_gain_set(NULL,2,2,0,0));assert(!rk3_input_gain_get(&gain,2,0,NULL));
}
static void arithmetic(void){
    uint32_t original[]={UINT32_C(0x7f800001),UINT32_C(0xffc01234),UINT32_C(0x80000000),0,1,UINT32_C(0x7f800000)};
    float raw[6];memcpy(raw,original,sizeof(raw));
    rk3_input_gain_apply(pair(UINT32_C(0x3f800000),UINT32_C(0x3f800000)),raw,3);
    assert(memcmp(raw,original,sizeof(raw))==0);
    float values[]={0.75f,-0.75f,0.25f,0.5f};
    rk3_input_gain_apply(pair(UINT32_C(0x40000000),UINT32_C(0xc0000000)),values,2);
    assert(values[0]==1.5f&&values[1]==1.5f&&values[2]==0.5f&&values[3]==-1.0f);
    float zero[]={1,-1};rk3_input_gain_apply(pair(UINT32_C(0x80000000),UINT32_C(0xbf800000)),zero,1);
    assert(bits(zero[0])==UINT32_C(0x80000000)&&zero[1]==1);
    float rounded[]={sample(UINT32_C(0x3f800001)),sample(UINT32_C(0x3f800001))};
    rk3_input_gain_apply(pair(UINT32_C(0x3f800001),UINT32_C(0x3f800001)),rounded,1);
    assert(bits(rounded[0])==UINT32_C(0x3f800002)&&bits(rounded[1])==UINT32_C(0x3f800002));
    float special[]={0,-1};rk3_input_gain_apply(pair(UINT32_C(0x7f800000),UINT32_C(0xff800000)),special,1);
    assert(isnan(special[0])&&isinf(special[1])&&!signbit(special[1]));
    float nonunity[]={sample(UINT32_C(0x7f800001)),2};
    rk3_input_gain_apply(pair(UINT32_C(0x3f800000),UINT32_C(0x3f000000)),nonunity,1);
    assert(isnan(nonunity[0])&&nonunity[1]==1); /* Both channels take multiplication. */
    float unchanged[]={1,2};rk3_input_gain_apply(0,unchanged,0);assert(unchanged[0]==1&&unchanged[1]==2);
    rk3_input_gain_apply(0,NULL,0);
}
int main(void){bounds();raw_values();arithmetic();puts("InputGain: original asymmetric bounds, raw Float32 bits, unity bypass and stereo multiply passed");return 0;}
