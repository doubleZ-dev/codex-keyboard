#include "default_actions.h"
#include "board_definition.h"
#undef NDEBUG
#include <assert.h>
#include <string.h>
#include <stdio.h>
static bool busy;
static unsigned count, last_agent;
static bool last_down;
static char last_key[16];
static const char *mapped_token="KC_A";
static bool block_release;
static const char *binding(unsigned slot,void *ctx)
{ (void)slot;(void)ctx;return mapped_token; }
static bool send(const char *key, bool down, unsigned agent, void *ctx)
{
    (void)ctx;
    if (busy || (block_release && !down)) return false;
    ++count; strcpy(last_key,key); last_down=down; last_agent=agent; return true;
}
static bool edge(default_actions_t *s, unsigned id, bool down)
{
    input_event_t e={.kind=IN_KEY_EDGE,.physical_id=id}; e.value.edge.down=down;
    return default_actions_event(s,&e,send,NULL);
}
int main(void)
{
    default_actions_t s;
    default_actions_session(&s,1,true);
    assert(edge(&s,0,true) && count==0);
    assert(edge(&s,0,false) && count==0);
    busy=true; assert(!edge(&s,0,true) && !s.held);
    busy=false; assert(edge(&s,0,true) && count==1 && !strcmp(last_key,"AG00"));
    assert(edge(&s,0,true) && count==1);
    assert(edge(&s,8,true) && last_agent==0 && !strcmp(last_key,"ACT06"));
    assert(edge(&s,7,true) && last_agent==5);
    assert(edge(&s,8,false) && last_agent==0 && !last_down);
    assert(edge(&s,9,true) && last_agent==5);
    busy=true; uint32_t held=s.held;
    assert(!default_actions_release(&s,send,NULL) && s.held==held);
    busy=false; assert(default_actions_release(&s,send,NULL) && !s.held);
    unsigned previous=count;
    assert(edge(&s,2,true) && edge(&s,BOARD_JOYSTICK_SW_ID,true) && count==previous);
    assert(edge(&s,BOARD_ENCODER_SW_ID,true) && count==previous);
    assert(edge(&s,BOARD_ENCODER_SW_ID,false));
    assert(edge(&s,BOARD_ENCODER_SW_ID,true) && !strcmp(last_key,"ENC_CLK"));
    default_actions_session(&s,0,false);
    assert(!s.held && !s.suppressed);
    input_event_t input={.kind=IN_KEY_EDGE,.physical_id=0}; input.value.edge.down=true;
    assert(default_actions_mapped_event(&s,&input,binding,NULL,send,NULL));
    mapped_token="KC_B";input.value.edge.down=false;
    assert(default_actions_mapped_event(&s,&input,binding,NULL,send,NULL) && !strcmp(last_key,"KC_A"));
    input=(input_event_t){.kind=IN_ENCODER_DELTA,.physical_id=BOARD_ENCODER_ROT_ID}; input.value.encoder.detents=1;
    block_release=true;
    assert(!default_actions_mapped_event(&s,&input,binding,NULL,send,NULL) && s.pulse_pending);
    previous=count;block_release=false;
    assert(default_actions_mapped_event(&s,&input,binding,NULL,send,NULL) && count==previous+1 && !last_down && !s.pulse_pending);
    default_actions_session(&s,0,false); mapped_token="KI_LM2";
    input=(input_event_t){.kind=IN_KEY_EDGE,.physical_id=0}; input.value.edge.down=true;
    assert(default_actions_mapped_event(&s,&input,binding,NULL,send,NULL));
    assert(default_actions_release_for_layer(&s,send,NULL) && s.held==1);
    default_actions_layer_session(&s,1,false,false); mapped_token="KC_C";
    input.value.edge.down=false;
    assert(default_actions_mapped_event(&s,&input,binding,NULL,send,NULL));
    assert(!s.held && !last_down && !strcmp(last_key,"KI_LM2"));
    mapped_token="KI_LM3";input.value.edge.down=true;
    assert(default_actions_mapped_event(&s,&input,binding,NULL,send,NULL));
    assert(default_actions_release(&s,send,NULL) && !s.held); /* Detach never preserves LM. */
    puts("Default actions: backpressure, dedup, captured release, layer-transition momentary ledger, resync and session suppression passed");
}
