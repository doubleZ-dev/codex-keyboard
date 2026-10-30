#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t candidate_since[16];
    uint64_t last_sample_us;
    uint32_t debounce_us;
    uint16_t candidate;
    uint16_t stable;
    bool sampled;
} matrix_debounce_t;

/* One input-task owner. No GPIO, protocol, allocation, gestures or persistence. */
void matrix_debounce_init(matrix_debounce_t *state, uint32_t debounce_us);
/* Returns false for invalid pointers or backwards time; no state change then.
 * pressed/released are physical position masks. Empty intersections are ignored.
 * The first held key must also remain stable for debounce_us before reporting. */
bool matrix_debounce_sample(matrix_debounce_t *state, uint16_t raw_pressed,
                            uint64_t now_us, uint16_t *pressed, uint16_t *released);
