#pragma once
#include "esp_err.h"
#include "input_event.h"
#include "light_frame.h"
#include "board_definition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef struct {
    uint16_t matrix_down;
    bool encoder_down, joystick_down, touch_down;
    int16_t joystick_x, joystick_y;
    uint16_t joystick_raw_x, joystick_raw_y, joystick_center_x, joystick_center_y;
    bool joystick_calibrated;
    int battery_mv;
    uint16_t battery_adc_raw, battery_adc_mv;
    bool battery_valid, low_voltage;
    uint32_t events_lost, adc_errors, touch_errors, scan_overruns;
} board_input_snapshot_t;
/* Call once from app_main. Events are copied into caller-owned queue.
 * Queue overflow increments loss counter; consumer must resync from snapshot.
 * Snapshot getter is safe from tasks, not ISR. No protocol in this driver. */
esp_err_t board_inputs_start(QueueHandle_t events);
void board_inputs_snapshot(board_input_snapshot_t *out);
/* Task-only: serialize with an entire scan, clear queue and capture one boundary. */
void board_inputs_resync(board_input_snapshot_t *out);

esp_err_t board_lights_start(void);
/* Copies full desired state; latest frame wins. Nonblocking task-only API.
 * No claim of current measurement: brightness cap is software only. */
esp_err_t board_lights_submit(const board_light_frame_t *frame);
uint32_t board_lights_faults(void);
