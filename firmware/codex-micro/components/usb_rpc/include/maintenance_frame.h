#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Project maintenance protocol, deliberately separate from official HID RPC.
 * 0 | COBS("CMUP",1,u16LE JSON length,JSON,u32LE IEEE CRC) | 0.
 * ASCII CDC logs can occur between complete frames. No dynamic allocation. */
enum { MAINTENANCE_JSON_MAX=2048, MAINTENANCE_FRAME_MAX=2080 };
typedef struct {
    uint8_t bytes[MAINTENANCE_FRAME_MAX];
    size_t length;
    bool collecting,overflow;
    uint32_t accepted,rejected;
} maintenance_rx_t;
typedef void (*maintenance_message_fn)(const uint8_t *,size_t,void *);
size_t maintenance_encode(const uint8_t *json,size_t length,uint8_t *frame,size_t capacity);
void maintenance_reset(maintenance_rx_t *s);
void maintenance_feed(maintenance_rx_t *s,const uint8_t *bytes,size_t length,maintenance_message_fn receive,void *ctx);
