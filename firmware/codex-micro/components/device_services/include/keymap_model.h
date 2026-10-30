#pragma once
#include "cJSON.h"
#include "light_engine.h"
#include <stdbool.h>
#include <stdint.h>
enum { KEYMAP_PROFILES=6, KEYMAP_LAYERS=6, KEYMAP_BINDINGS=20, KEYMAP_TOKEN=24,
       KEYMAP_BYTES=131072, KEYMAP_SECTORS=16 };
/* slots0..15 matrix,16 encoder press,17 joystick press,18 CC,19 CW */
typedef enum { JOYSTICK_VENDOR,JOYSTICK_RADIAL } joystick_mode_t;
typedef struct { char token[KEYMAP_TOKEN]; float start,end; } radial_sector_t;
typedef struct {
    bool present; int32_t linked_app;
    char tokens[KEYMAP_BINDINGS][KEYMAP_TOKEN];
    joystick_mode_t joystick_mode; uint8_t sector_count;
    radial_sector_t sectors[KEYMAP_SECTORS];
    bool has_lights;
    light_style_t backlight,underglow;
} keymap_layer_t;
typedef struct { uint16_t id; uint8_t layers,layer_order[KEYMAP_LAYERS]; keymap_layer_t layer[KEYMAP_LAYERS]; } keymap_profile_t;
enum { ACTION_MACROS=32, ACTION_STEPS=512, ACTION_MULTIS=32, ACTION_APPS=32, ACTION_CONCURRENT=4 };
typedef struct { char token[KEYMAP_TOKEN]; uint16_t delay_ms; uint8_t act; } macro_step_t;
typedef struct { uint16_t id,first,count; } macro_def_t;
typedef struct { uint16_t id,term_ms; char branch[4][KEYMAP_TOKEN]; } multi_def_t;
typedef struct { uint16_t id; char process[128],path[512]; } linked_app_t;
typedef struct {
    uint8_t count,active,macro_count,multi_count,app_count;
    uint16_t step_count;
    keymap_profile_t profiles[KEYMAP_PROFILES];
    macro_def_t macros[ACTION_MACROS]; macro_step_t steps[ACTION_STEPS];
    multi_def_t multis[ACTION_MULTIS]; linked_app_t apps[ACTION_APPS];
} keymap_model_t;
int action_token_id(const char *token,const char *prefix);
const macro_def_t *keymap_macro(const keymap_model_t *m,unsigned id);
const multi_def_t *keymap_multi(const keymap_model_t *m,unsigned id);
bool keymap_focus(const keymap_model_t *m,const char *process,const char *path,unsigned *profile,unsigned *layer);
bool keymap_token_supported(const char *token);
bool keymap_internal_token(const char *token);
/* Half-open intervals, including wrap through zero; -1 means a gap. */
int keymap_sector(const keymap_layer_t *layer,float angle);
int keymap_layer_position(const keymap_profile_t *profile,unsigned layer_id);
int keymap_layer_at(const keymap_profile_t *profile,unsigned position);
/* Full validation. Caller supplies scratch output; publish only if true. */
bool keymap_compile(const cJSON *json,keymap_model_t *out);
const char *keymap_binding(const keymap_model_t *model,unsigned layer,unsigned slot);
