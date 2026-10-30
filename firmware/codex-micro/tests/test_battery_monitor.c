#include "battery_monitor.h"
#include <assert.h>
#include <stdio.h>
static void samples(battery_monitor_t *s,int adc,unsigned count)
{ while (count--) battery_monitor_sample(s,true,adc); }
int main(void)
{
    battery_monitor_t s={0};
    samples(&s,2100,4); assert(!s.valid && !s.low);
    samples(&s,2100,1); assert(s.valid && s.millivolts==4200 && !s.low);
    samples(&s,1500,1); assert(s.millivolts==4200 && !s.low); /* transient outlier */
    samples(&s,2100,5);
    samples(&s,1750,9); assert(!s.low); /* median transition then stable decisions */
    samples(&s,1750,1); assert(s.valid && s.low && s.millivolts==3500);
    samples(&s,1800,20); assert(s.low); /* hysteresis band cannot clear warning */
    samples(&s,1825,9); assert(s.low);
    samples(&s,1825,1); assert(!s.low && s.millivolts==3650);
    samples(&s,1700,20); assert(s.low);
    battery_monitor_sample(&s,false,0); assert(!s.valid && s.low);
    samples(&s,0,20); assert(!s.valid && s.low); /* short is not 0% or recovery */
    samples(&s,2300,20); assert(!s.valid && s.low);
    samples(&s,2100,11); assert(s.valid && s.low);
    samples(&s,2100,1); assert(!s.low && s.millivolts==4200);
    s=(battery_monitor_t){0};
    for (unsigned i=0;i<200;++i) {
        battery_monitor_sample(&s,true, i%5==0?1300:2050+(int)(i%3));
        assert(!s.low); /* repeated single-sample noise cannot trigger */
    }
    puts("Battery: H1 2:1 divider, median, debounce, hysteresis, invalid samples and recovery passed");
}
