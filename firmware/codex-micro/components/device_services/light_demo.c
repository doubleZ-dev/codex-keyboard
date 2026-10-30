#include "light_demo.h"
#include <string.h>

/* Exact pixels from selected state legend circles in the user's screenshots.
 * Sample coordinates/source hashes are in rc.6/palette-samples.json. */
static const uint32_t white=0xFFFFFF, blue=0x3079FF, green=0x4CD475,
                      amber=0xFFCC00, red=0xFF3D81;

bool light_demo_button(light_demo_t *s,bool down,bool allowed,uint64_t now)
{
    if (!s->button_initialized) {
        s->button_initialized=true; s->button_down=down; return false;
    }
    bool pressed=down && !s->button_down;
    s->button_down=down;
    if (!allowed) { s->pending_click=false; return false; }
    bool changed=false;
    if (s->pending_click && now>=s->clicked_us && now-s->clicked_us>LIGHT_DEMO_DOUBLE_US) {
        s->pending_click=false; s->active=!s->active; s->cumulative=false;
        if (s->active) s->started_us=now;
        changed=true;
    }
    if (!pressed) return changed;
    if (s->pending_click) {
        s->pending_click=false; s->active=true; s->cumulative=true; s->started_us=now;
        return true;
    }
    s->pending_click=true; s->clicked_us=now;
    return changed;
}

bool light_demo_owns_input(const input_event_t *e)
{ return e->kind==IN_KEY_EDGE && e->physical_id==LIGHT_DEMO_TRIGGER_MATRIX_ID; }

void light_demo_stop(light_demo_t *s) { s->active=false; s->pending_click=false; }

unsigned light_demo_phase(const light_demo_t *s,uint64_t now)
{
    uint64_t elapsed=now<s->started_us?0:now-s->started_us;
    if (s->cumulative) return elapsed/1000000>=4?4:(unsigned)(elapsed/1000000);
    return (unsigned)((elapsed/LIGHT_DEMO_STEP_US)%LIGHT_DEMO_PHASES);
}

static board_rgb_t rgb(uint32_t color,unsigned scale)
{
    return (board_rgb_t){((color>>16)&255)*scale/255,
                         ((color>>8)&255)*scale/255,(color&255)*scale/255};
}

bool light_demo_render(const light_demo_t *s,uint64_t now,board_light_frame_t *out)
{
    if (!s->active) return false;
    memset(out->keys,0,sizeof(out->keys));
    memset(out->ambient,0,sizeof(out->ambient));
    out->brightness=64; /* Existing hardware driver still clamps to 64/255. */
    unsigned phase=light_demo_phase(s,now);
    if (s->cumulative) {
        /* Upper two Agent keys, then first three of the second row.
         * Already lit keys stay on; sixth Agent and ambient stay dark. */
        const uint32_t colors[]={white,blue,green,amber,red};
        for (unsigned i=0;i<=phase;++i) out->keys[i]=rgb(colors[i],255);
        return true;
    }
    if (!phase) {
        const uint32_t mixed[]={green,amber,blue,white,blue,red};
        for (unsigned i=0;i<6;++i) out->keys[i]=rgb(mixed[i],255);
        return true; /* Initial mixed Agent keys, no ambient lighting. */
    }
    const uint32_t colors[]={white,blue,green,amber,red};
    /* LED1..6 = upper pair, then lower four, as verified in local PCB source. */
    const uint8_t agents[]={3,2,0,1,5};
    unsigned color_index=(phase-1)/2;
    uint32_t color=colors[color_index];
    out->keys[agents[color_index]]=rgb(color,255);
    if (!(phase&1)) {
        /* Second half of each color: all eight LEDs steady for a full 2s. */
        for (unsigned i=0;i<BOARD_AMBIENT_LED_COUNT;++i) out->ambient[i]=rgb(color,255);
        return true;
    }
    uint64_t elapsed=now<s->started_us?0:now-s->started_us;
    unsigned head=(elapsed%LIGHT_DEMO_STEP_US)*BOARD_AMBIENT_LED_COUNT/LIGHT_DEMO_STEP_US;
    const unsigned tail[]={255,96,24};
    for (unsigned i=0;i<3;++i)
        out->ambient[(head+BOARD_AMBIENT_LED_COUNT-i)%BOARD_AMBIENT_LED_COUNT]=rgb(color,tail[i]);
    return true;
}
