#include "hid_state.h"
#include <string.h>
#include <stdlib.h>
void hid_state_reset(hid_state_t *s) { memset(s,0,sizeof(*s)); }
bool hid_state_key(hid_state_t *s,const char *t,bool down,hid_submit_fn submit,void *ctx)
{
    uint8_t id; uint16_t u;
    if (!key_code_lookup(t,&id,&u)) return false;
    if (!id) return true;
    hid_state_t next=*s;
    if (id==3) {
        if (down) { if (next.system[u]==255) return false; ++next.system[u]; }
        else if (next.system[u]) --next.system[u];
        uint8_t report=0;
        for (unsigned i=0;i<3;++i) if (next.system[i]) report|=1U<<i;
        if (!submit(3,&report,1,ctx)) return false;
        *s=next; return true;
    }
    for (unsigned i=0;id==2 && i<KEY_CONSUMER_COUNT;++i) if (u==key_code_consumer_usage(i)) {
        if (down) { if (next.consumer[i]==255) return false; ++next.consumer[i]; }
        else if (next.consumer[i]) --next.consumer[i];
        uint16_t active=0;
        for (unsigned j=0;j<KEY_CONSUMER_COUNT;++j) if (next.consumer[j]) active=key_code_consumer_usage(j);
        uint8_t report[]={active&255,active>>8};
        if (!submit(2,report,2,ctx)) return false;
        *s=next; return true;
    }
    if (id!=1) return false;
    if (down) { if (next.usages[u]==255) return false; ++next.usages[u]; }
    else if (next.usages[u]) --next.usages[u];
    uint8_t report[8]={0}; unsigned count=0;
    for (unsigned i=224;i<232;++i) if (next.usages[i]) report[0]|=1U<<(i-224);
    for (unsigned i=4;i<224;++i) if (next.usages[i]) { if (count<6) report[2+count]=i; ++count; }
    if (count>6) memset(report+2,1,6); /* HID ErrorRollOver, retains internal held set */
    if (!submit(1,report,8,ctx)) return false;
    *s=next; return true;
}
bool hid_state_release(hid_state_t *s,hid_submit_fn submit,void *ctx)
{
    uint8_t zero[8]={0};
    bool keyboard=false,consumer=false,system=false;
    for (unsigned i=0;i<256;++i) keyboard|=s->usages[i]!=0;
    for (unsigned i=0;i<KEY_CONSUMER_COUNT;++i) consumer|=s->consumer[i]!=0;
    for (unsigned i=0;i<3;++i) system|=s->system[i]!=0;
    if (keyboard) { if (!submit(1,zero,8,ctx)) return false; memset(s->usages,0,sizeof(s->usages)); }
    if (consumer) { if (!submit(2,zero,2,ctx)) return false; memset(s->consumer,0,sizeof(s->consumer)); }
    if (system) { if (!submit(3,zero,1,ctx)) return false; memset(s->system,0,sizeof(s->system)); }
    return true;
}
