#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef bool (*stick_send_fn)(float angle, float distance, void *context);
typedef struct {
    int16_t x,y;
    bool pending, neutral_pending;
    uint64_t last_send;
} stick_stream_t;
void stick_stream_reset(stick_stream_t *s);
void stick_stream_update(stick_stream_t *s, int16_t x, int16_t y);
/* Latest vector at <=25Hz. Neutral barrier cannot be overwritten by next motion.
 * Failed submit retains state. +X=0, +Y=0.25; orientation is explicit policy. */
bool stick_stream_pump(stick_stream_t *s, uint64_t now, stick_send_fn send, void *ctx);
