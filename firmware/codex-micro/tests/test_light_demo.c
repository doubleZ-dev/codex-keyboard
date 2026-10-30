#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "light_demo.h"

static uint32_t packed(board_rgb_t p) { return ((uint32_t)p.r<<16)|(p.g<<8)|p.b; }
static unsigned count_keys(const board_light_frame_t *f)
{
    unsigned count=0;
    for (unsigned i=0;i<BOARD_KEY_LED_COUNT;++i) if (packed(f->keys[i])) ++count;
    return count;
}

int main(void)
{
    light_demo_t d={0};
    board_light_frame_t f,original;
    memset(&original,0x25,sizeof(original)); f=original;
    assert(!light_demo_render(&d,0,&f) && !memcmp(&f,&original,sizeof(f)));
    assert(!light_demo_button(&d,true,true,0) && !d.active); /* Held at boot. */
    assert(!light_demo_button(&d,false,true,100));
    assert(!light_demo_button(&d,true,false,200) && !d.active); /* Maintenance. */
    assert(!light_demo_button(&d,true,true,300) && !d.active);
    assert(!light_demo_button(&d,false,true,400));
    const uint64_t start=10000000000ULL;
    assert(!light_demo_button(&d,true,true,start-LIGHT_DEMO_DOUBLE_US-1) && !d.active);
    assert(light_demo_button(&d,true,true,start) && d.active && !d.cumulative);
    assert(!light_demo_button(&d,true,true,start+1)); /* Never retrigger on hold. */
    assert(!light_demo_button(&d,false,true,start+2));

    f=original;
    assert(light_demo_render(&d,start,&f) && count_keys(&f)==6);
    const uint32_t mixed[]={0x4CD475,0xFFCC00,0x3079FF,0xFFFFFF,0x3079FF,0xFF3D81};
    for (unsigned i=0;i<6;++i) assert(packed(f.keys[i])==mixed[i]);
    for (unsigned i=6;i<BOARD_KEY_LED_COUNT;++i) assert(!packed(f.keys[i]));
    for (unsigned i=0;i<BOARD_AMBIENT_LED_COUNT;++i) assert(!packed(f.ambient[i]));
    assert(f.status_mask==original.status_mask && f.brightness==64);
    assert(light_demo_phase(&d,start+1999999)==0 && light_demo_phase(&d,start+2000000)==1);

    const uint8_t targets[]={3,2,0,1,5};
    const uint32_t colors[]={0xFFFFFF,0x3079FF,0x4CD475,0xFFCC00,0xFF3D81};
    for (unsigned phase=1;phase<11;++phase) {
        unsigned color_index=(phase-1)/2;
        unsigned heads=0;
        for (unsigned step=0;step<8;++step) {
            f=original;
            uint64_t now=start+(uint64_t)phase*LIGHT_DEMO_STEP_US+step*250000ULL;
            assert(light_demo_render(&d,now,&f) && light_demo_phase(&d,now)==phase);
            assert(count_keys(&f)==1 && packed(f.keys[targets[color_index]])==colors[color_index]);
            assert(f.status_mask==original.status_mask && f.brightness==64);
            unsigned lit=0;
            for (unsigned i=0;i<8;++i) {
                if (packed(f.ambient[i])) ++lit;
                if (packed(f.ambient[i])==colors[color_index]) heads|=1U<<i;
                if (!(phase&1)) assert(packed(f.ambient[i])==colors[color_index]);
            }
            assert(lit==((phase&1)?3U:8U) && packed(f.ambient[step])==colors[color_index]);
        }
        assert(heads==255); /* Every ambient LED participates in every state. */
    }
    assert(light_demo_phase(&d,start+21999999)==10);
    assert(light_demo_phase(&d,start+22000000)==0);
    assert(light_demo_phase(&d,start+22000000ULL*1000000)==0);
    assert(light_demo_phase(&d,start-1)==0); /* No clock-underflow wrap. */
    assert(!light_demo_button(&d,true,true,start+12649999));
    assert(light_demo_button(&d,true,true,start+13000000) && !d.active);
    f=original;
    assert(!light_demo_render(&d,start+13000001,&f) && !memcmp(&f,&original,sizeof(f)));
    assert(!light_demo_button(&d,false,true,start+13000002));
    assert(!light_demo_button(&d,true,true,start+13649999));
    assert(light_demo_button(&d,true,true,start+14000000) && d.active);
    assert(light_demo_phase(&d,start+14000000)==0); /* Restart from mixed. */
    light_demo_stop(&d);
    assert(!d.active && !light_demo_button(&d,true,true,start+15000000));
    assert(!light_demo_button(&d,false,true,start+15000001));
    assert(!light_demo_button(&d,true,true,start+16000000));
    assert(!light_demo_button(&d,false,true,start+16050000));
    const uint64_t cumulative_start=start+16200000;
    assert(light_demo_button(&d,true,true,cumulative_start) && d.active && d.cumulative);
    for (unsigned phase=0;phase<5;++phase) {
        f=original;
        assert(light_demo_render(&d,cumulative_start+phase*1000000ULL,&f));
        assert(count_keys(&f)==phase+1 && light_demo_phase(&d,cumulative_start+phase*1000000ULL)==phase);
        for (unsigned i=0;i<5;++i) assert(packed(f.keys[i])==(i<=phase?colors[i]:0));
        for (unsigned i=0;i<8;++i) assert(!packed(f.ambient[i]));
        assert(f.status_mask==original.status_mask);
    }
    assert(light_demo_phase(&d,cumulative_start+999999)==0);
    assert(light_demo_phase(&d,cumulative_start+999999999999ULL)==4); /* Hold final scene. */
    assert(!light_demo_button(&d,false,true,cumulative_start+5000000));
    assert(!light_demo_button(&d,true,true,cumulative_start+6000000));
    assert(light_demo_button(&d,false,true,cumulative_start+6350001) && !d.active);
    /* Interrupted double click cannot activate after maintenance. */
    assert(!light_demo_button(&d,true,true,cumulative_start+7000000));
    assert(!light_demo_button(&d,false,false,cumulative_start+7100000));
    assert(!light_demo_button(&d,false,true,cumulative_start+8000000) && !d.active);
    input_event_t e={.kind=IN_KEY_EDGE,.physical_id=8};
    assert(light_demo_owns_input(&e));
    e.value.edge.down=true; assert(light_demo_owns_input(&e));
    e.physical_id=9; assert(!light_demo_owns_input(&e));
    e.physical_id=8; e.kind=IN_TOUCH_EDGE; assert(!light_demo_owns_input(&e));
    puts("Agent demos: screenshot RGB/positions, 22s original cycle, double FAST cumulative 1s steps/final hold, single exit, maintenance suppression and status preservation passed");
}
