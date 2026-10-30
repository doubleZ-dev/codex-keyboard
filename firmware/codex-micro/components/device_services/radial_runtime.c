#include "radial_runtime.h"
#include <math.h>
#include <string.h>
void radial_runtime_reset(radial_runtime_t *s)
{ memset(s,0,sizeof(*s)); s->held_sector=-1; }
static bool release(radial_runtime_t *s,action_output_fn output,void *ctx)
{
    if (s->held && !output(s->captured,false,s->agent,ctx)) return false;
    s->held=false; s->held_sector=-1; return true;
}
bool radial_runtime_cancel(radial_runtime_t *s,action_output_fn output,radial_notice_fn notice,void *ctx)
{
    if (!release(s,output,ctx)) return false;
    if (s->open && !notice(0,0,-1,0,ctx)) return false;
    radial_runtime_reset(s); return true;
}
bool radial_runtime_tick(radial_runtime_t *s,const keymap_layer_t *layer,
    int16_t xq,int16_t yq,unsigned agent,uint64_t now,
    action_output_fn output,radial_notice_fn notice,void *ctx)
{
    if (!s || !layer || !output || !notice) return false;
    if (layer->joystick_mode!=JOYSTICK_RADIAL) return radial_runtime_cancel(s,output,notice,ctx);
    float x=xq/32767.0f,y=yq/32767.0f;
    float distance=fminf(1,sqrtf(x*x+y*y));
    if (distance<=0.10f) return radial_runtime_cancel(s,output,notice,ctx);
    if (s->closed_until_neutral) return true;
    float angle=atan2f(y,x)/6.283185307f; if (angle<0) angle+=1;
    int sector=keymap_sector(layer,angle);
    if (s->held && (distance<=0.60f || sector!=s->held_sector))
        if (!release(s,output,ctx)) return false;
    if (!s->held && distance>=0.80f && sector>=0) {
        const char *token=layer->sectors[sector].token;
        if (!strcmp(token,"KI_X")) {
            if (!notice(angle,distance,sector,0,ctx)) return false;
            s->open=false; s->closed_until_neutral=true; return true;
        }
        if (!output(token,true,agent,ctx)) return false;
        strcpy(s->captured,token); s->agent=agent; s->held=true; s->held_sector=sector;
    }
    /* Periodic frames keep Input's 3-second auto-close alive while held. */
    if (!s->open || now<s->last_notice || now-s->last_notice>=40000) {
        if (!notice(angle,distance,s->held?s->held_sector:-1,s->held?2:1,ctx)) return false;
        s->open=true; s->last_notice=now;
    }
    return true;
}
