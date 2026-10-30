#pragma once
#include "keymap_model.h"
#include "action_runtime.h"
typedef bool (*radial_notice_fn)(float angle,float distance,int sector,unsigned operation,void *ctx);
typedef struct {
    bool open,held,closed_until_neutral;
    int held_sector;
    char captured[KEYMAP_TOKEN];
    unsigned agent;
    uint64_t last_notice;
} radial_runtime_t;
void radial_runtime_reset(radial_runtime_t *state);
/* Protocol-owner only. Output callbacks copy synchronously. Held sector tokens
 * and Agent are captured so releases survive configuration/layer changes. */
bool radial_runtime_tick(radial_runtime_t *state,const keymap_layer_t *layer,
    int16_t x,int16_t y,unsigned agent,uint64_t now,
    action_output_fn output,radial_notice_fn notice,void *ctx);
bool radial_runtime_cancel(radial_runtime_t *state,action_output_fn output,
                           radial_notice_fn notice,void *ctx);
