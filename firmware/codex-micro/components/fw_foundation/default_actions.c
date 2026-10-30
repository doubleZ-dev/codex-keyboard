#include "default_actions.h"
#include "board_definition.h"
#include <string.h>
static const char *const keys[17] = {
    "AG00","AG01",NULL,NULL,"AG02","AG03","AG04","AG05",
    "ACT06","ACT07","ACT08","ACT09",NULL,"ACT10","ACT11","ACT12","ENC_CLK"
};
void default_actions_session(default_actions_t *s, uint16_t matrix, bool encoder)
{
    memset(s,0,sizeof(*s));
    s->suppressed=(matrix & BOARD_MATRIX_MASK) | (encoder ? 1U<<16 : 0);
}
static const char *default_binding(unsigned slot,void *ctx)
{
    (void)ctx;
    if (slot==18) return "ENC_CC";
    if (slot==19) return "ENC_CW";
    return slot<17 && keys[slot]?keys[slot]:"KC_NO";
}
bool default_actions_event(default_actions_t *s,const input_event_t *e,action_send_fn send,void *ctx)
{ return default_actions_mapped_event(s,e,default_binding,NULL,send,ctx); }

bool default_actions_mapped_event(default_actions_t *s,const input_event_t *e,
    action_binding_fn binding,void *binding_ctx,action_send_fn send,void *ctx)
{
    if (!s || !e || !send || !binding) return false;
    if (e->kind==IN_ENCODER_DELTA) {
        if (s->pulse_pending) {
            if (!send(s->pulse_key,false,s->pulse_agent,ctx)) return false;
            s->pulse_pending=false; return true;
        }
        if (!e->value.encoder.detents) return true;
        if (e->value.encoder.detents!=1 && e->value.encoder.detents!=-1) return false;
        const char *token=binding(e->value.encoder.detents>0?19:18,binding_ctx);
        if (!token || strlen(token)>=24) return false;
        if (!strncmp(token,"KV_OAI_",7)) token+=7;
        if (!strcmp(token,"KC_NO")) return true;
        if (!send(token,true,s->selected_agent,ctx)) return false;
        if (!strcmp(token,"ENC_CC") || !strcmp(token,"ENC_CW")) return true;
        strcpy(s->pulse_key,token); s->pulse_agent=s->selected_agent; s->pulse_pending=true;
        if (!send(token,false,s->pulse_agent,ctx)) return false;
        s->pulse_pending=false; return true;
    }
    if (e->kind!=IN_KEY_EDGE) return true;
    unsigned slot=e->physical_id==BOARD_ENCODER_SW_ID?16:
                  e->physical_id==BOARD_JOYSTICK_SW_ID?17:e->physical_id;
    if (slot>=18 || (slot<16 && !(BOARD_MATRIX_MASK & (1U<<slot)))) return true;
    uint32_t bit=1U<<slot;
    bool down=e->value.edge.down;
    if (s->suppressed & bit) { if (!down) s->suppressed &= ~bit; return true; }
    if (down==((s->held & bit)!=0)) return true;
    const char *token=down?binding(slot,binding_ctx):s->captured_key[slot];
    if (!token || strlen(token)>=24) return false;
    if (!strncmp(token,"KV_OAI_",7)) token+=7;
    if (!strcmp(token,"KC_NO")) return true;
    unsigned agent=s->selected_agent;
    if (strlen(token)==4 && !strncmp(token,"AG0",3) && token[3]>='0' && token[3]<='5') agent=token[3]-'0';
    if (!down) agent=s->captured_agent[slot];
    if (!send(token,down,agent,ctx)) return false;
    if (down) {
        s->held |= bit; s->captured_agent[slot]=agent; strcpy(s->captured_key[slot],token);
        if (!strncmp(token,"AG0",3)) s->selected_agent=agent;
    } else s->held &= ~bit;
    return true;
}
static bool release_held(default_actions_t *s,action_send_fn send,void *ctx,bool layer_change)
{
    if (s->pulse_pending) {
        if (!send(s->pulse_key,false,s->pulse_agent,ctx)) return false;
        s->pulse_pending=false;
    }
    for (unsigned i=0;i<18;++i) if (s->held & (1U<<i)) {
        if (layer_change && (!strncmp(s->captured_key[i],"KI_LM",5) || !strncmp(s->captured_key[i],"KA_M",4))) continue;
        if (!send(s->captured_key[i],false,s->captured_agent[i],ctx)) return false;
        s->held &= ~(1U<<i);
    }
    return true;
}
bool default_actions_release(default_actions_t *s,action_send_fn send,void *ctx)
{ return release_held(s,send,ctx,false); }
bool default_actions_release_for_layer(default_actions_t *s,action_send_fn send,void *ctx)
{ return release_held(s,send,ctx,true); }
void default_actions_layer_session(default_actions_t *s,uint16_t matrix,bool encoder,bool joystick)
{
    uint32_t physical=(matrix & BOARD_MATRIX_MASK)|(encoder?1U<<16:0)|(joystick?1U<<17:0);
    /* Scanner supplies queued release edges after the transition. Retain all
     * captured LM keys, even if already physically up, so that edge restores. */
    s->suppressed=physical & ~s->held;
}
