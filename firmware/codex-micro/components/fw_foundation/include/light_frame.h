#pragma once
#include "board_definition.h"
typedef struct { uint8_t r, g, b; } board_rgb_t;
typedef struct {
    board_rgb_t keys[BOARD_KEY_LED_COUNT];
    board_rgb_t ambient[BOARD_AMBIENT_LED_COUNT];
    uint8_t brightness;
    uint8_t status_mask;
} board_light_frame_t;
