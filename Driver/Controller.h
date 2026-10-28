#ifndef RK3_CONTROLLER_H
#define RK3_CONTROLLER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    RK3_CONTROLLER_OK=0,
    RK3_CONTROLLER_INVALID_ARGUMENT,
    RK3_CONTROLLER_NOT_CONTROLLER,
    RK3_CONTROLLER_OUTPUT_FAILED,
    RK3_CONTROLLER_CALIBRATION_INSUFFICIENT
} RK3ControllerResult;

typedef struct {
    double low,high,last_value;
    uint32_t last_raw;
    uint16_t raw_max;
    uint8_t midi_value;
    bool initialized,has_raw,automatic,collecting;
    bool sample_tip,as_switch,switch_first,switch_inverted,switch_value;
} RK3ControllerAxis;
typedef struct {
    uint32_t signature;
    RK3ControllerAxis axes[3]; /* Raw inputs0/1 external,2 built-in. */
    uint16_t raw_buttons;
    uint8_t button_values[9],displays[4];
    bool buttons_initialized,leds_dirty;
} RK3ControllerState;

/* State belongs to one serialized caller; no allocation, I/O or threading.
 * Fresh reconstruction from ControllerMapping.md. Built-in full-range initial
 * calibration is an explicit new-host fallback until real calibration is set. */
void rk3_controller_init(RK3ControllerState *state);
void rk3_controller_reset_inputs(RK3ControllerState *state);
typedef bool (*RK3ControllerEmit)(void *context,const uint8_t message[3]);
typedef bool (*RK3ControllerWrite)(void *context,const uint8_t *report,size_t length);
/* 03: three BE16 pairs after command;04: nine low-order bits after command.
 * Actual length must be7/3 minimum,64 maximum. Padding is allowed. Malformed
 * reports leave state untouched. First physical snapshots emit no MIDI.
 * Emit failure may leave preceding successful changes committed. */
RK3ControllerResult rk3_controller_report(RK3ControllerState *state,
    const uint8_t *report,size_t length,RK3ControllerEmit emit,void *context);
RK3ControllerResult rk3_controller_feedback(RK3ControllerState *state,
    uint8_t status,uint8_t controller,uint8_t value);
/* Snapshot command05 with preserved startup displays00,40,40,00; LED raw bits
 * 0..7/toe bit8; trailing00,00. Dirty remains set on failed synchronous write. */
RK3ControllerResult rk3_controller_flush_leds(RK3ControllerState *state,
    bool force,RK3ControllerWrite write,void *context);
/* NI's per-pedal setters assign flags/endpoints without resetting calculation
 * state. sample_tip selects hardware sampling; report pairs already contain
 * that selected signal. The serialized owner applies the hardware setting.
 * Native arguments reject invalid axes/nonfinite endpoints; finite sentinel,
 * equal and reversed endpoints retain the original setter semantics. */
RK3ControllerResult rk3_controller_set_pedal_mode(RK3ControllerState *state,
    unsigned axis,bool sample_tip,bool as_switch);
RK3ControllerResult rk3_controller_set_calibration(RK3ControllerState *state,
    unsigned axis,double low,double high);
RK3ControllerResult rk3_controller_get_calibration(const RK3ControllerState *state,
    unsigned axis,double *low,double *high);
/* Original explicit recalibration seeds endpoints from the stored continuous
 * value, preserves the raw cache, and suppresses events while collecting.
 * stop only clears collection: usable-range checks belong to a host utility.
 * reset_inputs is the separate ARM history/lifetime baseline boundary. */
RK3ControllerResult rk3_controller_calibration_start(RK3ControllerState *state,unsigned axis);
RK3ControllerResult rk3_controller_calibration_stop(RK3ControllerState *state,unsigned axis);
#endif
