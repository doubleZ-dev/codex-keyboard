#pragma once
#include <stdint.h>
/* INMP441: signed 24-bit Philips I2S in the high bits of a 32-bit slot.
 * Unity gain drops the low eight meaningful bits for PCM16. */
static inline int16_t microphone_pcm16(int32_t slot, int32_t gain_q16)
{
    int64_t sample=((int64_t)(slot >> 8)*gain_q16)/16777216;
    return sample>32767?32767:sample< -32768?-32768:(int16_t)sample;
}
