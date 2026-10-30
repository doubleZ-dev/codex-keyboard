#pragma once
#include <stdbool.h>
#include <stdint.h>
/* H1: VBATT -> 100k -> ADC -> 100k -> GND; 100nF across bottom resistor.
 * 250ms samples, median of five, eight consecutive threshold decisions.
 * Voltage validity does not establish battery presence or charging. */
enum { BATTERY_SAMPLE_US=250000, BATTERY_LOW_MV=3500,
       BATTERY_RECOVER_MV=3650, BATTERY_CONFIRM_SAMPLES=8 };
typedef struct {
    int window[5], millivolts;
    uint8_t count, cursor, low_count, recovery_count;
    bool valid, low;
} battery_monitor_t;
void battery_monitor_sample(battery_monitor_t *s, bool calibrated, int adc_mv);
