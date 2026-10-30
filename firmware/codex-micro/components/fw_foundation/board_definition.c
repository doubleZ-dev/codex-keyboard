#include "board_definition.h"

const uint8_t board_matrix_rows[4] = {BOARD_ROW1, BOARD_ROW2, BOARD_ROW3, BOARD_ROW4};
const uint8_t board_matrix_cols[4] = {BOARD_COL1, BOARD_COL2, BOARD_COL3, BOARD_COL4};

bool board_definition_valid(void)
{
    const uint8_t pins[] = {
        BOARD_ROW1, BOARD_ROW2, BOARD_ROW3, BOARD_ROW4,
        BOARD_COL1, BOARD_COL2, BOARD_COL3, BOARD_COL4,
        BOARD_ENCODER_A, BOARD_ENCODER_B, BOARD_ENCODER_SW,
        BOARD_JOYSTICK_X, BOARD_JOYSTICK_Y, BOARD_JOYSTICK_SW,
        BOARD_TOUCH, BOARD_KEYS_RGB, BOARD_AMBIENT_RGB,
        BOARD_STATUS1, BOARD_STATUS2, BOARD_STATUS3, BOARD_BATTERY,
        BOARD_MIC_BCLK, BOARD_MIC_DIN, BOARD_MIC_WS,
        BOARD_USB_DM, BOARD_USB_DP, BOARD_BOOT
    };
    uint64_t used = 0;
    for (unsigned i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
        uint8_t pin = pins[i];
        if (pin > 48 || (pin >= 22 && pin <= 37) ||
            pin == 3 || pin == 45 || pin == 46) {
            return false;
        }
        uint64_t bit = UINT64_C(1) << pin;
        if (used & bit) {
            return false;
        }
        used |= bit;
    }
    return true;
}

_Static_assert(BOARD_MATRIX_MASK == (0xFFFFu & ~((1u << 2) | (1u << 3) | (1u << 12))),
               "Matrix physical IDs must keep the three holes");
