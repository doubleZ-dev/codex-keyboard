#undef NDEBUG
#include "firmware_policy.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    assert(!firmware_policy_upgrade_raw_busy(true,-32768,32767,true));
    assert(!firmware_policy_upgrade_raw_busy(false,0,0,false));
    assert(firmware_policy_upgrade_raw_busy(false,1,0,false));
    assert(firmware_policy_upgrade_raw_busy(false,0,-1,false));
    assert(firmware_policy_upgrade_raw_busy(false,0,0,true));
    const char *reads[]={"sys.version","device.status","sys.selftest","diag.inputs","fs.list","fs.read","fs.readbin","upgrade.status","upgrade.begin","upgrade.confirm"};
    const char *blocked[]={"v.oai.hid","v.oai.rad","v.oai.rgbcfg","v.oai.thstatus","host.focused_app","diag.layer","diag.lights","diag.ble","fs.write","fs.writebin","fs.txbegin","fs.txcommit","sys.bootloader","kb.sa.exec","ui.active_screen"};
    for (unsigned i=0;i<sizeof(reads)/sizeof(reads[0]);++i) assert(firmware_policy_rpc_allowed(true,reads[i]));
    for (unsigned i=0;i<sizeof(blocked)/sizeof(blocked[0]);++i) {
        assert(!firmware_policy_rpc_allowed(true,blocked[i]));
        assert(firmware_policy_rpc_allowed(false,blocked[i]));
    }
    assert(!firmware_policy_rpc_allowed(true,NULL));
    for (unsigned i=0;i<sizeof(reads)/sizeof(reads[0]);++i)
        assert(firmware_policy_service_rpc_allowed(true,true,reads[i]));
    const char *services[]={"fs.write","fs.writebin","fs.txbegin","fs.txcommit","diag.ble","diag.reboot","diag.layer","host.focused_app","v.oai.rgbcfg","v.oai.thstatus"};
    for (unsigned i=0;i<sizeof(services)/sizeof(services[0]);++i) {
        assert(firmware_policy_service_rpc_allowed(true,true,services[i]));
        assert(!firmware_policy_service_rpc_allowed(true,false,services[i]));
    }
    assert(!firmware_policy_service_rpc_allowed(true,true,"v.oai.hid"));
    assert(!firmware_policy_service_rpc_allowed(true,true,"v.oai.rad"));
    assert(!firmware_policy_service_rpc_allowed(true,true,"diag.lights"));
    uint8_t keyboard[8]={0},consumer[2]={0};
    assert(firmware_policy_hid_allowed(true,keyboard,sizeof(keyboard)));
    assert(firmware_policy_hid_allowed(true,consumer,sizeof(consumer)));
    for (unsigned byte=0;byte<sizeof(keyboard);++byte) for (unsigned value=1;value<=255;++value) {
        keyboard[byte]=value;
        assert(!firmware_policy_hid_allowed(true,keyboard,sizeof(keyboard)));
        assert(firmware_policy_hid_allowed(false,keyboard,sizeof(keyboard)));
        keyboard[byte]=0;
    }
    for (unsigned byte=0;byte<sizeof(consumer);++byte) for (unsigned value=1;value<=255;++value) {
        consumer[byte]=value; assert(!firmware_policy_hid_allowed(true,consumer,sizeof(consumer)));consumer[byte]=0;
    }
    puts("Firmware policy: bring-up read-only RPC boundary and every nonzero keyboard/consumer byte blocked; neutral releases and full profile retained");
}
