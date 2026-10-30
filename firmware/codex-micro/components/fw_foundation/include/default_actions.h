#pragma once
#include "input_event.h"
typedef bool (*action_send_fn)(const char *key, bool down, unsigned agent, void *context);
typedef struct {
    uint32_t held, suppressed;
    uint8_t captured_agent[18];
    uint8_t selected_agent;
    char captured_key[18][24];
    char pulse_key[24];
    bool pulse_pending;
    uint8_t pulse_agent;
} default_actions_t;
typedef const char *(*action_binding_fn)(unsigned slot,void *context);
bool default_actions_mapped_event(default_actions_t *state,const input_event_t *event,
    action_binding_fn binding,void *binding_context,action_send_fn send,void *send_context);
/* Single protocol-task owner. Physical matrix -> existing Gate0 default layout.
 * Callback true means notification copied to TX, false means retry unchanged.
 * Slots 0..15=matrix, 16=encoder button. No fabricated joystick/touch key IDs. */
bool default_actions_event(default_actions_t *state, const input_event_t *event,
                           action_send_fn send, void *context);
/* Release old ledger incrementally. Retry until true before taking fresh events. */
bool default_actions_release(default_actions_t *state, action_send_fn send, void *context);
/* Layer changes retain physical momentary-layer keys, including their original
 * release binding. A disconnect/configuration reset always uses full release. */
bool default_actions_release_for_layer(default_actions_t *state,action_send_fn send,void *context);
void default_actions_layer_session(default_actions_t *state,uint16_t matrix_down,bool encoder_down,bool joystick_down);
void default_actions_session(default_actions_t *state, uint16_t matrix_down, bool encoder_down);
