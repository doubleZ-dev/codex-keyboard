#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UPDATE_CHUNK_MAX 1024
typedef enum { UPDATE_IDLE, UPDATE_WRITING, UPDATE_READY, UPDATE_ACTIVATED, UPDATE_FAILED, UPDATE_BOOT_UNCERTAIN } update_state_t;
typedef struct {
    bool (*begin)(uint32_t size, void *ctx);
    bool (*write)(const uint8_t *data, size_t length, void *ctx);
    /* Finish consumes backend handle even on failure; verifies SHA256 and image. */
    bool (*finish)(const uint8_t digest[32], void *ctx);
    bool (*activate)(void *ctx);
    void (*abort)(void *ctx);
    void *ctx;
} update_backend_t;
typedef struct {
    update_state_t state;
    uint32_t size, offset;
    uint8_t digest[32];
    update_backend_t backend;
} update_engine_t;
void update_engine_init(update_engine_t *u, update_backend_t backend);
bool update_engine_begin(update_engine_t *u, uint32_t size, uint32_t capacity, const uint8_t digest[32]);
bool update_engine_write(update_engine_t *u, uint32_t offset, const uint8_t *data, size_t length);
bool update_engine_finish(update_engine_t *u);
bool update_engine_activate(update_engine_t *u);
/* Once activated, abort cannot undo the durable boot selection. */
bool update_engine_abort(update_engine_t *u);
