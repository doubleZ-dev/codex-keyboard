#include "action_runtime.h"
#include <string.h>
void action_runtime_reset(action_runtime_t *s) { memset(s,0,sizeof(*s)); }
bool action_runtime_busy(const action_runtime_t *s)
{
    for (unsigned i=0;i<ACTION_CONCURRENT;++i) if (s->macros[i].def) return true;
    for (unsigned i=0;i<ACTION_MULTIS;++i) if (s->multis[i].state) return true;
    return false;
}
static bool release_macro(macro_run_t *r,action_output_fn output,void *ctx)
{
    for (unsigned i=0;i<16;++i) while (r->held[i].count) {
        if (!output(r->held[i].token,false,r->held[i].agent,ctx)) return false;
        --r->held[i].count;
    }
    memset(r,0,sizeof(*r)); return true;
}
static bool macro_key(macro_run_t *r,const char *t,bool down,action_output_fn output,void *ctx)
{
    int found=-1,free_slot=-1;
    const char *key=!strncmp(t,"KV_OAI_",7)?t+7:t;
    bool select=strlen(key)==4 && !strncmp(key,"AG0",3) && key[3]>='0' && key[3]<='5';
    unsigned agent=select?(unsigned)(key[3]-'0'):r->agent;
    for (unsigned i=0;i<16;++i) {
        if (r->held[i].count && !strcmp(r->held[i].token,t) && (!down || r->held[i].agent==agent)) found=i;
        if (!r->held[i].count) free_slot=i;
    }
    if (!down && found<0) return true; /* Never release a different producer's key. */
    if (down && found<0) found=free_slot;
    if (found<0) { r->cancelling=true; return false; }
    if (!down) agent=r->held[found].agent;
    if (!output(t,down,agent,ctx)) return false;
    if (down) { strcpy(r->held[found].token,t); r->held[found].agent=agent; ++r->held[found].count; if (select) r->agent=agent; }
    else --r->held[found].count;
    return true;
}
static bool multi_output(action_runtime_t *s,const keymap_model_t *m,const char *t,bool down,unsigned agent,uint64_t now,action_output_fn output,void *ctx)
{
    if (action_token_id(t,"KA_A")>=0) return action_runtime_event(s,m,t,down,agent,now,output,ctx);
    return output(t,down,agent,ctx);
}
bool action_runtime_event(action_runtime_t *s,const keymap_model_t *m,const char *t,bool down,unsigned agent,uint64_t now,action_output_fn output,void *ctx)
{
    int id=action_token_id(t,"KA_A");
    if (id>=0) {
        if (!down) return true;
        const macro_def_t *def=keymap_macro(m,id); if (!def) return false;
        for (unsigned i=0;i<ACTION_CONCURRENT;++i) if (!s->macros[i].def) {
            macro_run_t *r=&s->macros[i]; memset(r,0,sizeof(*r)); r->def=def; r->agent=agent;
            r->due=now; r->deadline=now+60000000; return true;
        }
        return false;
    }
    id=action_token_id(t,"KA_M");
    if (id>=0) {
        action_runtime_tick(s,m,now,output,ctx);
        const multi_def_t *def=keymap_multi(m,id); if (!def) return false;
        multi_run_t *r=&s->multis[def-m->multis];
        if ((r->state==1 || r->state==3) && now>=r->due) return false; /* due hold blocked by output */
        if (down) {
            if (r->refs) { if (r->refs==255) return false; ++r->refs; return true; }
            if (r->state!=0 && r->state!=2) return false;
            if (r->state==2 && now>=r->due) return false; /* tick emits old single tap first */
            r->state=r->state==2?3:1; r->refs=1; r->agent=agent; r->due=now+(uint64_t)def->term_ms*1000;
        } else {
            if (!r->refs || --r->refs) return true;
            if (r->state==1) { r->state=2; r->due=now+(uint64_t)def->term_ms*1000; }
            else if (r->state==3) { r->branch=1; r->state=6; }
            else if (r->state==4 || r->state==5) r->state=8;
        }
        return true;
    }
    return output(t,down,agent,ctx);
}
void action_runtime_tick(action_runtime_t *s,const keymap_model_t *m,uint64_t now,action_output_fn output,void *ctx)
{
    for (unsigned i=0;i<ACTION_CONCURRENT;++i) {
        macro_run_t *r=&s->macros[i]; if (!r->def) continue;
        if (now>=r->deadline || r->cancelling || r->step==r->def->count) { r->cancelling=true; release_macro(r,output,ctx); continue; }
        if (now<r->due) continue;
        const macro_step_t *step=&m->steps[r->def->first+r->step];
        bool down=step->act==1 || (step->act==2 && r->phase==0);
        if (!macro_key(r,step->token,down,output,ctx)) continue;
        if (step->act==2 && r->phase==0) r->phase=1;
        else { r->phase=0; ++r->step; r->due=now+(uint64_t)step->delay_ms*1000; }
    }
    for (unsigned i=0;i<m->multi_count;++i) {
        multi_run_t *r=&s->multis[i]; const multi_def_t *d=&m->multis[i];
        if ((r->state==1 || r->state==3) && now>=r->due) {
            unsigned branch=r->state==1?2:3;
            if (multi_output(s,m,d->branch[branch],true,r->agent,now,output,ctx)) { r->branch=branch; r->state=branch==2?4:5; }
        } else if (r->state==2 && now>=r->due) { r->branch=0; r->state=6; }
        if (r->state==6 && multi_output(s,m,d->branch[r->branch],true,r->agent,now,output,ctx)) r->state=7;
        else if ((r->state==7 || r->state==8) && multi_output(s,m,d->branch[r->branch],false,r->agent,now,output,ctx)) memset(r,0,sizeof(*r));
    }
}
bool action_runtime_cancel(action_runtime_t *s,const keymap_model_t *m,action_output_fn output,void *ctx)
{
    bool complete=true;
    for (unsigned i=0;i<ACTION_CONCURRENT;++i) if (s->macros[i].def) {
        s->macros[i].cancelling=true;
        if (!release_macro(&s->macros[i],output,ctx)) complete=false;
    }
    for (unsigned i=0;i<m->multi_count;++i) {
        multi_run_t *r=&s->multis[i];
        if (r->state==4 || r->state==5 || r->state==7 || r->state==8) {
            r->state=8;
            if (!multi_output(s,m,m->multis[i].branch[r->branch],false,r->agent,0,output,ctx)) { complete=false; continue; }
        }
        memset(r,0,sizeof(*r));
    }
    return complete;
}
