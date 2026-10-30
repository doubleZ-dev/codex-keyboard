#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    IN_KEY_EDGE, IN_ENCODER_DELTA, IN_JOYSTICK_VECTOR,
    IN_TOUCH_EDGE, IN_RESYNC_REQUIRED
} input_kind_t;

typedef struct {
    uint64_t time_us;
    uint32_t sequence;
    uint16_t physical_id;
    uint8_t kind;
    uint8_t flags;
    union {
        struct { bool down; } edge;
        struct { int16_t detents; } encoder;
        struct { int16_t x_q15, y_q15; } joystick;
    } value;
} input_event_t;

_Static_assert(sizeof(input_event_t) <= 32, "Input queue event budget exceeded");
