#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include "ble_transport.h"
#include "firmware_policy.h"
#define MAINBOARD_BRINGUP false
static bool selected_ble=true,busy;
static unsigned sent,usb_sent;
static uint32_t unavailable_outputs;
static ble_transport_status_t state;
void ble_transport_status(ble_transport_status_t *out) { *out=state; }
bool ble_transport_send(uint8_t id,const void *p,size_t n)
{ (void)id;(void)p;(void)n;++sent;return !busy; }
static bool tud_hid_n_ready(unsigned instance) { assert(instance==1);return !busy; }
static bool tud_hid_n_report(unsigned instance,uint8_t id,const void *p,unsigned n)
{ assert(instance==1);(void)id;(void)p;(void)n;++usb_sent;return true; }
#include "standard_submit.inc"
int main(void)
{
    uint8_t neutral[8]={0},key[8]={0,0,4};
    state.connected=state.authorized=state.encrypted=state.ready=state.vendor_ready=true;
    assert(submit_standard(1,neutral,8,NULL));
    assert(submit_standard(2,neutral,2,NULL));
    assert(submit_standard(3,neutral,1,NULL));
    assert(!sent && !unavailable_outputs);
    assert(submit_standard(1,key,8,NULL) && unavailable_outputs==1 && !sent);
    assert(!submit_standard(1,key,2,NULL) && !submit_standard(6,key,8,NULL));
    state.authorized=false;assert(!submit_standard(1,neutral,8,NULL));state.authorized=true;
    state.keyboard_ready=true;busy=true;
    assert(!submit_standard(1,key,8,NULL) && sent==1); /* Congestion retries. */
    busy=false;assert(submit_standard(1,key,8,NULL) && sent==2);
    selected_ble=false;assert(submit_standard(1,key,8,NULL) && usb_sent==1);
    busy=true;assert(!submit_standard(1,key,8,NULL) && usb_sent==1);
    puts("Actual standard submit: independent BLE neutral/capability gates, authentication, congestion retry and unchanged USB passed");
}
