#include "matrix_debounce.h"
#include "board_definition.h"
#include "input_event.h"

void matrix_debounce_init(matrix_debounce_t *state, uint32_t debounce_us)
{
    if (!state) return;
    for (unsigned i = 0; i < 16; ++i) state->candidate_since[i] = 0;
    state->last_sample_us = 0;
    state->debounce_us = debounce_us;
    state->candidate = 0;
    state->stable = 0;
    state->sampled = false;
}

bool matrix_debounce_sample(matrix_debounce_t *state, uint16_t raw_pressed,
                            uint64_t now_us, uint16_t *pressed, uint16_t *released)
{
    if (!state || !pressed || !released || pressed == released) return false;
    *pressed = 0;
    *released = 0;
    if (state->sampled && now_us < state->last_sample_us) return false;
    state->sampled = true;
    state->last_sample_us = now_us;
    raw_pressed &= BOARD_MATRIX_MASK;
    for (unsigned i = 0; i < 16; ++i) {
        uint16_t bit = (uint16_t)(1u << i);
        if (!(BOARD_MATRIX_MASK & bit)) continue;
        if ((raw_pressed ^ state->candidate) & bit) {
            state->candidate ^= bit;
            state->candidate_since[i] = now_us;
        }
        if (((state->candidate ^ state->stable) & bit) &&
            now_us - state->candidate_since[i] >= state->debounce_us) {
            state->stable ^= bit;
            if (state->stable & bit) *pressed |= bit;
            else *released |= bit;
        }
    }
    return true;
}
