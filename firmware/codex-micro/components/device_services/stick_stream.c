#include "stick_stream.h"
#include <math.h>
#include <string.h>
void stick_stream_reset(stick_stream_t *s) { memset(s,0,sizeof(*s)); }
void stick_stream_update(stick_stream_t *s,int16_t x,int16_t y)
{
    if (s->x==x && s->y==y) return;
    s->x=x; s->y=y; s->pending=true;
    if (!x && !y) s->neutral_pending=true;
}
bool stick_stream_pump(stick_stream_t *s,uint64_t now,stick_send_fn send,void *ctx)
{
    if (s->neutral_pending) {
        if (!send(0,0,ctx)) return false;
        s->neutral_pending=false; s->pending=(s->x || s->y); s->last_send=now;
        return true;
    }
    if (!s->pending || (now>=s->last_send && now-s->last_send<40000)) return true;
    float x=s->x/32767.0f,y=s->y/32767.0f;
    float a=atan2f(y,x)/6.2831853f; if (a<0) a+=1;
    float d=fminf(1,sqrtf(x*x+y*y));
    if (!send(d?a:0,d,ctx)) return false;
    s->pending=false; s->last_send=now; return true;
}
