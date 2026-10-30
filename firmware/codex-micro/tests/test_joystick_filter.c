#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include "joystick_filter.h"

static void sample(joystick_filter_t *s,int rx,int ry,unsigned count,int16_t *x,int16_t *y)
{ for (unsigned n=0;n<count;++n) joystick_filter_update(s,rx,ry,x,y); }
int main(void)
{
    joystick_filter_t s; int16_t x,y;
    joystick_filter_init(&s);
    sample(&s,2000,1780,65,&x,&y);
    assert(!s.ready && !x && !y);
    sample(&s,2000,1780,1,&x,&y);
    assert(s.ready && s.center[0]==2000 && s.center[1]==1780 && !x && !y);
    for (int n=0;n<1000;++n) {
        joystick_filter_update(&s,2000+n%101-50,1780+n%121-60,&x,&y);
        assert(!x && !y);
    }
    /* A one-sample rail spike must not become a host direction event. */
    joystick_filter_update(&s,4095,0,&x,&y); assert(!x && !y);
    sample(&s,2000,1780,8,&x,&y); assert(!x && !y);
    sample(&s,4095,1780,12,&x,&y); assert(x>30000 && !y);
    sample(&s,2000,1780,16,&x,&y); assert(!x && !y);
    sample(&s,2000,0,100,&x,&y); assert(!x && y==-32767);
    sample(&s,2000,4095,100,&x,&y); assert(!x && y==32767);
    sample(&s,2000,1780,16,&x,&y); assert(!x && !y);
    /* Hysteresis: 200 counts won't enter, but retains an existing direction;
     * returning inside 160 counts must release it exactly to zero. */
    sample(&s,2200,1780,100,&x,&y); assert(!x && !y);
    sample(&s,2300,1780,100,&x,&y); assert(x>0 && !y);
    sample(&s,2200,1780,100,&x,&y); assert(x>0 && !y);
    sample(&s,2150,1780,100,&x,&y); assert(!x && !y);
    /* Unstable or rail-held startup must never learn a usable center. */
    joystick_filter_init(&s);
    for (unsigned n=0;n<256;++n) {
        joystick_filter_update(&s,(n%8<4)?1500:2500,1780,&x,&y);
        assert(!s.ready && !x && !y);
    }
    joystick_filter_init(&s);
    sample(&s,4095,1780,200,&x,&y); assert(!s.ready && !x && !y);
    sample(&s,2000,1780,140,&x,&y); assert(s.ready && !x && !y);
    joystick_filter_update(&s,-1,1780,&x,&y); assert(!s.ready && !x && !y);
    puts("Joystick: center offset/noise/spike suppression, real motion latency, endpoints, release, hysteresis and invalid startup passed");
    return 0;
}
