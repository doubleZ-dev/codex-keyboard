#include "matrix_debounce.h"
#include "board_definition.h"
static matrix_debounce_t state;

__declspec(dllexport) void test_reset(void)
{
    matrix_debounce_init(&state, 5000);
}

__declspec(dllexport) uint32_t test_sample(uint16_t raw, uint64_t now)
{
    uint16_t down, up;
    if (!matrix_debounce_sample(&state, raw, now, &down, &up)) return UINT32_MAX;
    return (uint32_t)down | ((uint32_t)up << 16);
}

__declspec(dllexport) int test_board(void)
{
    return board_definition_valid() && !BOARD_LOW_POWER_SUPPORTED &&
           BOARD_MATRIX_MASK == 0xEFF3;
}
