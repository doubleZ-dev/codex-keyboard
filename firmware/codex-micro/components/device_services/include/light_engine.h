#pragma once
#include "light_frame.h"
#include "cJSON.h"
typedef struct {
    uint32_t color;
    float brightness, speed,magic;
    uint8_t effect;
    bool sync_keys, sync_ambient;
} light_style_t;
typedef struct {
    light_style_t keys, ambient, agents[6];
    uint8_t valid_agents;
    uint32_t generation;
} light_engine_t;
/* Pure single-owner state. Invalid input leaves the WHOLE state unchanged.
 * Effect IDs follow community evidence; visual curves are project implementations. */
void light_engine_init(light_engine_t *state);
/* Input persisted DTO: string effect, numeric RGB color, normalized controls. */
bool light_style_from_config(light_style_t *style,const cJSON *json);
bool light_engine_apply(light_engine_t *state, const char *method, const cJSON *params);
void light_engine_render(const light_engine_t *state, uint64_t now_us,
                         unsigned selected_agent, uint8_t layer, bool low_voltage,
                         board_light_frame_t *frame);
