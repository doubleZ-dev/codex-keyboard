#pragma once
#include "light_frame.h"
#include "input_event.h"

enum { LIGHT_DEMO_TRIGGER_MATRIX_ID=8, LIGHT_DEMO_PHASES=11,
       LIGHT_DEMO_STEP_US=2000000, LIGHT_DEMO_DOUBLE_US=350000 };
/* Protocol-task owned. Host lighting stays independent and keeps updating. */
typedef struct {
    bool active, button_initialized, button_down, pending_click, cumulative;
    uint64_t started_us, clicked_us;
} light_demo_t;
/* Debounced board snapshot. Startup-held and disallowed presses never trigger. */
bool light_demo_button(light_demo_t *demo,bool down,bool allowed,uint64_t now_us);
bool light_demo_owns_input(const input_event_t *event);
void light_demo_stop(light_demo_t *demo);
unsigned light_demo_phase(const light_demo_t *demo,uint64_t now_us);
/* Replaces RGB only; preserves three independent status indicators. */
bool light_demo_render(const light_demo_t *demo,uint64_t now_us,board_light_frame_t *frame);
