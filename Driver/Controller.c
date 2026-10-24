#include "Controller.h"
#include <math.h>
#include <string.h>

#define CONTROLLER_SIGNATURE UINT32_C(0x524b3343)
static const uint8_t axis_cc[3]={16,17,4};
static bool valid(const RK3ControllerState *state) {
    return state != NULL && state->signature == CONTROLLER_SIGNATURE;
}
static bool calibration_valid(double low,double high) {
    return isfinite(low) && isfinite(high) && low >= 0 && high <= 1 && high-low >= 0.1;
}
static uint8_t midi_value(double normalized) {
    /* Original Assignment::setValue uses MULSS followed by CVTSD2SI. Match its
     * normal nearest-even integer result with an explicit tie rule. */
    float scaled=(float)normalized*127.0f;
    uint8_t lower=(uint8_t)scaled;float fraction=scaled-lower;
    return (uint8_t)(lower+(fraction > 0.5f || (fraction == 0.5f && (lower&1) != 0)));
}
void rk3_controller_init(RK3ControllerState *state) {
    if(state == NULL) return;
    memset(state,0,sizeof(*state));state->signature=CONTROLLER_SIGNATURE;
    for(unsigned index=0;index<3;++index) {
        state->axes[index].low=state->axes[index].high=-1;
        state->axes[index].raw_max=index == 2 ? 1023 : 4095;
        state->axes[index].automatic=index != 2;
    }
    state->axes[2].low=0;state->axes[2].high=1;
    state->displays[1]=state->displays[2]=0x40;state->leds_dirty=true;
}
void rk3_controller_reset_inputs(RK3ControllerState *state) {
    if(!valid(state)) return;
    state->buttons_initialized=false;state->raw_buttons=0;state->leds_dirty=true;
    for(unsigned index=0;index<3;++index) {
        RK3ControllerAxis *axis=state->axes+index;
        if(axis->collecting) {axis->low=axis->saved_low;axis->high=axis->saved_high;axis->collecting=false;}
        axis->initialized=false;axis->has_raw=false;
    }
}
static bool pedal_value(RK3ControllerAxis *axis,uint32_t raw,double *result) {
    if(raw > axis->raw_max) raw=axis->raw_max;
    if(axis->has_raw && axis->last_raw == raw) return false;
    axis->last_raw=raw;axis->has_raw=true;
    double x=(double)raw/axis->raw_max;
    if(axis->collecting) {
        if(axis->low < 0 || x < axis->low) axis->low=x;
        if(axis->high < 0 || x > axis->high) axis->high=x;
        axis->initialized=true;return false;
    }
    if(!axis->initialized) {
        axis->initialized=true;
        if(axis->low < 0) axis->low=x;
        if(axis->high < 0) axis->high=x;
        axis->last_value=axis->high != axis->low ? (x-axis->low)/(axis->high-axis->low) : x;
        return false;
    }
    if(axis->automatic) {
        if(x < axis->low) axis->low=x;
        if(x > axis->high) axis->high=x;
    }
    double span=axis->high-axis->low;
    if(span < 0.1) return false;
    double y=(x-axis->low-0.025*span)/(0.95*span);
    if(y < 0.005) {
        y=0;if(axis->last_value == 0) return false;
    } else if(y > 0.995) {
        y=1;if(axis->last_value == 1) return false;
    } else if(fabs(y-axis->last_value) <= 0.005) return false;
    axis->last_value=y;*result=y;return true;
}
RK3ControllerResult rk3_controller_report(RK3ControllerState *state,
    const uint8_t *report,size_t length,RK3ControllerEmit emit,void *context) {
    if(!valid(state) || report == NULL || length == 0 || length > 64) return RK3_CONTROLLER_INVALID_ARGUMENT;
    if(report[0] != 3 && report[0] != 4) return RK3_CONTROLLER_NOT_CONTROLLER;
    if(emit == NULL || length < (report[0] == 3 ? 7u : 3u)) return RK3_CONTROLLER_INVALID_ARGUMENT;
    if(report[0] == 3) {
        for(unsigned index=0;index<3;++index) {
            RK3ControllerAxis *axis=state->axes+index,saved=*axis;double value;
            uint32_t raw=((uint32_t)report[1+index*2]<<8)|report[2+index*2];
            if(!pedal_value(axis,raw,&value)) continue;
            uint8_t midi=midi_value(value);
            if(midi == axis->midi_value) continue;
            const uint8_t message[3]={0xb0,axis_cc[index],midi};
            if(!emit(context,message)) {*axis=saved;return RK3_CONTROLLER_OUTPUT_FAILED;}
            axis->midi_value=midi;
        }
    } else {
        uint16_t buttons=(uint16_t)(report[1]|((uint16_t)report[2]<<8))&0x1ff;
        if(!state->buttons_initialized) {
            state->raw_buttons=buttons;state->buttons_initialized=true;return RK3_CONTROLLER_OK;
        }
        for(unsigned index=0;index<9;++index) {
            uint16_t bit=(uint16_t)(1u<<index);
            if(((buttons^state->raw_buttons)&bit) == 0) continue;
            if((buttons&bit) != 0) {
                uint8_t value=state->button_values[index] != 0 ? 0 : 127;
                const uint8_t message[3]={0xb0,index == 8 ? 64 : (uint8_t)(76+index),value};
                if(!emit(context,message)) return RK3_CONTROLLER_OUTPUT_FAILED;
                state->button_values[index]=value;state->leds_dirty=true;
            }
            state->raw_buttons=(uint16_t)((state->raw_buttons&~bit)|(buttons&bit));
        }
    }
    return RK3_CONTROLLER_OK;
}
RK3ControllerResult rk3_controller_feedback(RK3ControllerState *state,
    uint8_t status,uint8_t controller,uint8_t value) {
    if(!valid(state) || controller > 127 || value > 127) return RK3_CONTROLLER_INVALID_ARGUMENT;
    if(status != 0xb0) return RK3_CONTROLLER_NOT_CONTROLLER;
    for(unsigned index=0;index<3;++index) if(axis_cc[index] == controller) {
        state->axes[index].midi_value=value;return RK3_CONTROLLER_OK;
    }
    unsigned button=controller == 64 ? 8 : (unsigned)controller-76;
    if(button > 8 || (button == 8 && controller != 64)) return RK3_CONTROLLER_NOT_CONTROLLER;
    uint8_t assignment=value >= 64 ? 127 : 0;
    if(state->button_values[button] != assignment) {state->button_values[button]=assignment;state->leds_dirty=true;}
    return RK3_CONTROLLER_OK;
}
RK3ControllerResult rk3_controller_flush_leds(RK3ControllerState *state,
    bool force,RK3ControllerWrite write,void *context) {
    if(!valid(state) || write == NULL) return RK3_CONTROLLER_INVALID_ARGUMENT;
    if(!force && !state->leds_dirty) return RK3_CONTROLLER_OK;
    uint8_t report[9]={5,state->displays[0],state->displays[1],state->displays[2],state->displays[3],0,0,0,0};
    for(unsigned index=0;index<9;++index)
        if(state->button_values[index] != 0) report[5+index/8]|=(uint8_t)(1u<<(index%8));
    if(!write(context,report,sizeof(report))) {state->leds_dirty=true;return RK3_CONTROLLER_OUTPUT_FAILED;}
    state->leds_dirty=false;return RK3_CONTROLLER_OK;
}
RK3ControllerResult rk3_controller_set_calibration(RK3ControllerState *state,
    unsigned index,double low,double high) {
    if(!valid(state) || index >= 3 || !calibration_valid(low,high)) return RK3_CONTROLLER_INVALID_ARGUMENT;
    RK3ControllerAxis *axis=state->axes+index;
    axis->low=low;axis->high=high;axis->collecting=false;axis->initialized=false;axis->has_raw=false;
    return RK3_CONTROLLER_OK;
}
RK3ControllerResult rk3_controller_get_calibration(const RK3ControllerState *state,
    unsigned index,double *low,double *high) {
    if(!valid(state) || index >= 3 || low == NULL || high == NULL) return RK3_CONTROLLER_INVALID_ARGUMENT;
    *low=state->axes[index].low;*high=state->axes[index].high;return RK3_CONTROLLER_OK;
}
RK3ControllerResult rk3_controller_calibration_start(RK3ControllerState *state,unsigned index) {
    if(!valid(state) || index >= 3 || state->axes[index].collecting) return RK3_CONTROLLER_INVALID_ARGUMENT;
    RK3ControllerAxis *axis=state->axes+index;
    axis->saved_low=axis->low;axis->saved_high=axis->high;
    axis->low=axis->high=axis->has_raw ? (double)axis->last_raw/axis->raw_max : -1;
    axis->collecting=true;axis->initialized=false;return RK3_CONTROLLER_OK;
}
RK3ControllerResult rk3_controller_calibration_stop(RK3ControllerState *state,unsigned index) {
    if(!valid(state) || index >= 3 || !state->axes[index].collecting) return RK3_CONTROLLER_INVALID_ARGUMENT;
    RK3ControllerAxis *axis=state->axes+index;bool enough=calibration_valid(axis->low,axis->high);
    if(!enough) {axis->low=axis->saved_low;axis->high=axis->saved_high;}
    axis->collecting=false;axis->initialized=false;axis->has_raw=false;
    return enough ? RK3_CONTROLLER_OK : RK3_CONTROLLER_CALIBRATION_INSUFFICIENT;
}
