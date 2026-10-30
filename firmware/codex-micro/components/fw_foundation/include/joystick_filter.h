#pragma once
#include <stdbool.h>
#include <stdint.h>

/* One scan-task owner. Raw ADC12 samples every 5ms. Keep the stick released
 * during startup; calibration is volatile and never writes configuration. */
enum { JOYSTICK_CENTER_SAMPLES=64, JOYSTICK_DEAD_ENTER=240, JOYSTICK_DEAD_EXIT=160 };
typedef struct {
    uint32_t sum[2];
    uint16_t minimum[2], maximum[2], center[2];
    uint16_t history[2][3];
    int32_t filtered_q8[2];
    unsigned samples;
    unsigned history_count, history_index;
    bool ready, active[2];
} joystick_filter_t;
void joystick_filter_init(joystick_filter_t *state);
/* Outputs zero until a stable, plausible center is acquired. */
void joystick_filter_update(joystick_filter_t *state, int raw_x, int raw_y,
                            int16_t *x, int16_t *y);
