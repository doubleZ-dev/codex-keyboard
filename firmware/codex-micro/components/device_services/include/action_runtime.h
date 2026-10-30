#pragma once
#include "keymap_model.h"
typedef bool (*action_output_fn)(const char *,bool,unsigned,void *);
typedef struct { char token[KEYMAP_TOKEN]; unsigned count,agent; } action_held_t;
typedef struct {
    const macro_def_t *def; unsigned step,phase,agent;
    uint64_t due,deadline; action_held_t held[16]; bool cancelling;
} macro_run_t;
typedef struct {
    uint8_t state,refs,branch; unsigned agent; uint64_t due;
} multi_run_t;
typedef struct { macro_run_t macros[ACTION_CONCURRENT]; multi_run_t multis[ACTION_MULTIS]; } action_runtime_t;
void action_runtime_reset(action_runtime_t *s);
bool action_runtime_busy(const action_runtime_t *s);
bool action_runtime_event(action_runtime_t *s,const keymap_model_t *m,const char *token,bool down,unsigned agent,uint64_t now,action_output_fn output,void *ctx);
void action_runtime_tick(action_runtime_t *s,const keymap_model_t *m,uint64_t now,action_output_fn output,void *ctx);
/* Repeat until true when transport is busy. Reset without sending on disconnect. */
bool action_runtime_cancel(action_runtime_t *s,const keymap_model_t *m,action_output_fn output,void *ctx);
