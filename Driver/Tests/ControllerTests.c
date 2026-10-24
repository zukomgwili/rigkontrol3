#include "Controller.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {uint8_t messages[64][3],led[9];size_t count,writes;bool fail;} Output;
static bool emit(void *context,const uint8_t message[3]) {
    Output *out=context;if(out->fail) return false;
    assert(out->count < 64);memcpy(out->messages[out->count++],message,3);return true;
}
static bool write_led(void *context,const uint8_t *report,size_t length) {
    Output *out=context;assert(length == 9);if(out->fail) return false;
    memcpy(out->led,report,9);++out->writes;return true;
}
static void report(RK3ControllerState *state,Output *out,const uint8_t *bytes,size_t length) {
    assert(rk3_controller_report(state,bytes,length,emit,out) == RK3_CONTROLLER_OK);
}
static void analog(RK3ControllerState *state,Output *out,unsigned a,unsigned b,unsigned c) {
    const uint8_t bytes[]={3,(uint8_t)(a>>8),(uint8_t)a,(uint8_t)(b>>8),(uint8_t)b,(uint8_t)(c>>8),(uint8_t)c};
    report(state,out,bytes,sizeof(bytes));
}
static void digital(RK3ControllerState *state,Output *out,unsigned bits) {
    const uint8_t bytes[]={4,(uint8_t)bits,(uint8_t)(bits>>8)};report(state,out,bytes,sizeof(bytes));
}
static void message(Output *out,size_t at,uint8_t cc,uint8_t value) {
    const uint8_t expected[]={0xb0,cc,value};assert(at < out->count && memcmp(out->messages[at],expected,3) == 0);
}
static void test_buttons_and_reconnect(void) {
    RK3ControllerState state;rk3_controller_init(&state);Output out={0};
    digital(&state,&out,0x1ff);assert(out.count == 0); /* Already-held snapshot is baseline. */
    digital(&state,&out,0);assert(out.count == 0);
    digital(&state,&out,0x1ff);assert(out.count == 9);
    for(unsigned i=0;i<9;++i) message(&out,i,i == 8 ? 64 : (uint8_t)(76+i),127);
    digital(&state,&out,0x1ff);digital(&state,&out,0);assert(out.count == 9);
    digital(&state,&out,0x1ff);assert(out.count == 18);
    for(unsigned i=0;i<9;++i) message(&out,9+i,i == 8 ? 64 : (uint8_t)(76+i),0);
    assert(rk3_controller_feedback(&state,0xb0,76,127) == RK3_CONTROLLER_OK);
    rk3_controller_reset_inputs(&state);digital(&state,&out,0x1ff);assert(out.count == 18);
    digital(&state,&out,0);digital(&state,&out,1);message(&out,18,76,0);
}
static void test_axes(void) {
    RK3ControllerState state;rk3_controller_init(&state);Output out={0};
    analog(&state,&out,0,0,0);assert(out.count == 0);
    analog(&state,&out,4095,4095,1023);assert(out.count == 3);
    message(&out,0,16,127);message(&out,1,17,127);message(&out,2,4,127);
    analog(&state,&out,4095,4095,512);message(&out,3,4,64);
    analog(&state,&out,4095,4095,513);assert(out.count == 4); /* Original0.005 movement deadband. */
    analog(&state,&out,4095,4095,1023);message(&out,4,4,127);
    analog(&state,&out,65535,65535,65535);assert(out.count == 5); /* Clamp raw maxima. */
    assert(state.axes[0].low == 0 && state.axes[0].high == 1);
    rk3_controller_init(&state);memset(&out,0,sizeof(out));
    assert(rk3_controller_set_calibration(&state,2,0.2,0.8) == RK3_CONTROLLER_OK);
    analog(&state,&out,0,0,205);assert(out.count == 0);
    analog(&state,&out,0,0,512);message(&out,0,4,64); /* Recovered inset formula. */
    analog(&state,&out,0,0,0);message(&out,1,4,0);
    analog(&state,&out,0,0,1023);message(&out,2,4,127);
    rk3_controller_init(&state);memset(&out,0,sizeof(out));
    double high=0.1+(512.0/1023-0.1)/(0.025+0.95*(62.5/127));
    assert(rk3_controller_set_calibration(&state,2,0.1,high) == RK3_CONTROLLER_OK);
    analog(&state,&out,0,0,0);analog(&state,&out,0,0,512);
    message(&out,0,4,62); /* NI Float32 scaling plus normal nearest-even conversion. */
}
static void test_calibration(void) {
    RK3ControllerState state;rk3_controller_init(&state);Output out={0};double low,high;
    assert(rk3_controller_set_calibration(&state,2,0.25,0.75) == RK3_CONTROLLER_OK);
    analog(&state,&out,0,0,512);
    assert(rk3_controller_calibration_start(&state,2) == RK3_CONTROLLER_OK);
    assert(rk3_controller_calibration_stop(&state,2) == RK3_CONTROLLER_CALIBRATION_INSUFFICIENT);
    assert(rk3_controller_get_calibration(&state,2,&low,&high) == RK3_CONTROLLER_OK && low == 0.25 && high == 0.75);
    assert(rk3_controller_calibration_start(&state,2) == RK3_CONTROLLER_OK);
    analog(&state,&out,0,0,100);analog(&state,&out,0,0,900);assert(out.count == 0);
    assert(rk3_controller_calibration_stop(&state,2) == RK3_CONTROLLER_OK);
    assert(rk3_controller_get_calibration(&state,2,&low,&high) == RK3_CONTROLLER_OK);
    assert(low == 100.0/1023 && high == 900.0/1023);
    analog(&state,&out,0,0,100);assert(out.count == 0);
    analog(&state,&out,0,0,900);message(&out,0,4,127);
    assert(rk3_controller_calibration_start(&state,2) == RK3_CONTROLLER_OK);
    analog(&state,&out,0,0,0);rk3_controller_reset_inputs(&state);
    assert(state.axes[2].low == low && state.axes[2].high == high && !state.axes[2].collecting);
}
static void test_feedback_and_leds(void) {
    RK3ControllerState state;rk3_controller_init(&state);Output out={0};
    assert(rk3_controller_flush_leds(&state,false,write_led,&out) == RK3_CONTROLLER_OK);
    const uint8_t initial[]={5,0,0x40,0x40,0,0,0,0,0};assert(memcmp(out.led,initial,9) == 0);
    assert(rk3_controller_flush_leds(&state,false,write_led,&out) == RK3_CONTROLLER_OK && out.writes == 1);
    assert(rk3_controller_feedback(&state,0xb0,76,64) == RK3_CONTROLLER_OK);
    assert(rk3_controller_feedback(&state,0xb0,64,127) == RK3_CONTROLLER_OK);
    assert(rk3_controller_feedback(&state,0xb0,4,93) == RK3_CONTROLLER_OK && state.axes[2].midi_value == 93);
    assert(rk3_controller_flush_leds(&state,false,write_led,&out) == RK3_CONTROLLER_OK);
    const uint8_t on[]={5,0,0x40,0x40,0,1,1,0,0};assert(memcmp(out.led,on,9) == 0);
    out.fail=true;assert(rk3_controller_flush_leds(&state,true,write_led,&out) == RK3_CONTROLLER_OUTPUT_FAILED && state.leds_dirty);
    out.fail=false;assert(rk3_controller_flush_leds(&state,false,write_led,&out) == RK3_CONTROLLER_OK && !state.leds_dirty);
    RK3ControllerState saved=state;
    assert(rk3_controller_feedback(&state,0xb1,76,0) == RK3_CONTROLLER_NOT_CONTROLLER);
    assert(rk3_controller_feedback(&state,0xb0,75,0) == RK3_CONTROLLER_NOT_CONTROLLER);
    assert(rk3_controller_feedback(&state,0xb0,84,0) == RK3_CONTROLLER_NOT_CONTROLLER);
    assert(memcmp(&state,&saved,sizeof(state)) == 0);
}
static void test_bounds_and_failures(void) {
    RK3ControllerState state;rk3_controller_init(&state);Output out={0};RK3ControllerState saved=state;
    uint8_t bytes[65]={3};
    for(size_t length=0;length<7;++length)
        assert(rk3_controller_report(&state,bytes,length,emit,&out) == RK3_CONTROLLER_INVALID_ARGUMENT);
    bytes[0]=4;
    for(size_t length=1;length<3;++length)
        assert(rk3_controller_report(&state,bytes,length,emit,&out) == RK3_CONTROLLER_INVALID_ARGUMENT);
    assert(rk3_controller_report(&state,bytes,65,emit,&out) == RK3_CONTROLLER_INVALID_ARGUMENT);
    assert(rk3_controller_report(&state,NULL,3,emit,&out) == RK3_CONTROLLER_INVALID_ARGUMENT);
    bytes[0]=6;assert(rk3_controller_report(&state,bytes,3,emit,&out) == RK3_CONTROLLER_NOT_CONTROLLER);
    assert(rk3_controller_set_calibration(&state,3,0,1) == RK3_CONTROLLER_INVALID_ARGUMENT);
    assert(rk3_controller_set_calibration(&state,2,NAN,1) == RK3_CONTROLLER_INVALID_ARGUMENT);
    assert(rk3_controller_set_calibration(&state,2,0.8,0.2) == RK3_CONTROLLER_INVALID_ARGUMENT);
    assert(rk3_controller_set_calibration(&state,2,0,0.05) == RK3_CONTROLLER_INVALID_ARGUMENT);
    assert(rk3_controller_feedback(&state,0xb0,76,128) == RK3_CONTROLLER_INVALID_ARGUMENT);
    assert(memcmp(&state,&saved,sizeof(state)) == 0);
    digital(&state,&out,0);out.fail=true;bytes[0]=4;bytes[1]=1;bytes[2]=0;
    assert(rk3_controller_report(&state,bytes,3,emit,&out) == RK3_CONTROLLER_OUTPUT_FAILED);
    assert(state.raw_buttons == 0 && state.button_values[0] == 0);
    out.fail=false;digital(&state,&out,1);message(&out,0,76,127);
    analog(&state,&out,0,0,0);out.fail=true;
    const uint8_t half[]={3,0,0,0,0,2,0};
    assert(rk3_controller_report(&state,half,7,emit,&out) == RK3_CONTROLLER_OUTPUT_FAILED);
    out.fail=false;report(&state,&out,half,7);message(&out,1,4,64);
}
int main(void) {
    test_buttons_and_reconnect();test_axes();test_calibration();test_feedback_and_leds();test_bounds_and_failures();
    puts("Controller: original CCs, input baselines, calibration, feedback/LEDs, bounds and failure retries passed.");return 0;
}
