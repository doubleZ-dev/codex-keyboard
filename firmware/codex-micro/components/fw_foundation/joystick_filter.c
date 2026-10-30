#include "joystick_filter.h"
#include <stdlib.h>
#include <string.h>

void joystick_filter_init(joystick_filter_t *s)
{
    memset(s,0,sizeof(*s));
    s->center[0]=s->center[1]=2048;
}

void joystick_filter_update(joystick_filter_t *s,int raw_x,int raw_y,int16_t *x,int16_t *y)
{
    *x=*y=0;
    if (raw_x<0 || raw_x>4095 || raw_y<0 || raw_y>4095) {
        joystick_filter_init(s); return;
    }
    s->history[0][s->history_index]=(uint16_t)raw_x;
    s->history[1][s->history_index]=(uint16_t)raw_y;
    s->history_index=(s->history_index+1)%3;
    if (s->history_count<3 && ++s->history_count<3) return;
    int raw[2];
    for (unsigned i=0;i<2;++i) {
        int a=s->history[i][0],b=s->history[i][1],c=s->history[i][2];
        if (a>b) { int t=a; a=b; b=t; }
        if (b>c) b=c;
        raw[i]=a>b?a:b; /* Median of three rejects isolated ADC spikes. */
    }
    if (!s->ready) {
        for (unsigned i=0;i<2;++i) {
            if (!s->samples) s->minimum[i]=s->maximum[i]=raw[i];
            if (raw[i]<s->minimum[i]) s->minimum[i]=raw[i];
            if (raw[i]>s->maximum[i]) s->maximum[i]=raw[i];
            s->sum[i]+=(unsigned)raw[i];
        }
        if (++s->samples<JOYSTICK_CENTER_SAMPLES) return;
        uint16_t center[2];
        bool stable=true;
        for (unsigned i=0;i<2;++i) {
            center[i]=(s->sum[i]+JOYSTICK_CENTER_SAMPLES/2)/JOYSTICK_CENTER_SAMPLES;
            if (s->maximum[i]-s->minimum[i]>160 || center[i]<1024 || center[i]>3071) stable=false;
        }
        s->samples=0; s->sum[0]=s->sum[1]=0;
        if (!stable) return;
        for (unsigned i=0;i<2;++i) {
            s->center[i]=center[i]; s->filtered_q8[i]=(int32_t)center[i]*256;
        }
        s->ready=true;
        return;
    }
    int16_t *out[2]={x,y};
    for (unsigned i=0;i<2;++i) {
        /* EMA alpha=1/4: 5ms sampling, roughly 40ms to 90% of a step. */
        s->filtered_q8[i]+=((int32_t)raw[i]*256-s->filtered_q8[i])/4;
        int delta=(s->filtered_q8[i]+128)/256-s->center[i];
        int magnitude=abs(delta);
        if (s->active[i]) {
            if (magnitude<=JOYSTICK_DEAD_EXIT) s->active[i]=false;
        } else if (magnitude>JOYSTICK_DEAD_ENTER) s->active[i]=true;
        if (!s->active[i]) continue;
        int span=(delta<0?s->center[i]:4095-s->center[i])-JOYSTICK_DEAD_EXIT;
        int value=(magnitude-JOYSTICK_DEAD_EXIT)*32767/span;
        if (value>32767) value=32767;
        *out[i]=(int16_t)(delta<0?-value:value);
    }
}
