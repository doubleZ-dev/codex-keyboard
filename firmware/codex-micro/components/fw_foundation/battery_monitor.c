#include "battery_monitor.h"
void battery_monitor_sample(battery_monitor_t *s, bool calibrated, int adc_mv)
{
    /* ADC half-voltage range for this 1S design, with measurement margin.
     * Reject a disconnected/shorted/failed reading, never call it 0% charge. */
    if (!calibrated || adc_mv<1250 || adc_mv>2250) {
        s->valid=false; s->count=s->cursor=s->low_count=s->recovery_count=0;
        return; /* Existing warning remains until a valid recovery is measured. */
    }
    s->window[s->cursor]=adc_mv*2;
    s->cursor=(s->cursor+1)%5;
    if (s->count<5) ++s->count;
    if (s->count<5) { s->valid=false; return; }
    int sorted[5];
    for (unsigned i=0;i<5;++i) {
        sorted[i]=s->window[i];
        for (unsigned j=i;j>0 && sorted[j]<sorted[j-1];--j) {
            int t=sorted[j]; sorted[j]=sorted[j-1]; sorted[j-1]=t;
        }
    }
    s->millivolts=sorted[2]; s->valid=true;
    if (s->millivolts<=BATTERY_LOW_MV) {
        s->recovery_count=0;
        if (s->low_count<BATTERY_CONFIRM_SAMPLES) ++s->low_count;
        if (s->low_count==BATTERY_CONFIRM_SAMPLES) s->low=true;
    } else if (s->millivolts>=BATTERY_RECOVER_MV) {
        s->low_count=0;
        if (s->recovery_count<BATTERY_CONFIRM_SAMPLES) ++s->recovery_count;
        if (s->recovery_count==BATTERY_CONFIRM_SAMPLES) s->low=false;
    } else s->low_count=s->recovery_count=0;
}
