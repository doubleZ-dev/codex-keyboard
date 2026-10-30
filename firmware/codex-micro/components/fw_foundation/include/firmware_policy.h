#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Build profile, never a Host-controlled or persisted runtime switch. */
static inline bool firmware_policy_upgrade_raw_busy(bool bringup,int x,int y,bool queued)
{
    return !bringup && (x || y || queued);
}
static inline bool firmware_policy_rpc_allowed(bool bringup,const char *method)
{
    if (!bringup) return true;
    if (!method) return false;
    const char *read_only[]={"sys.version","device.status","sys.selftest","diag.inputs",
        "fs.list","fs.read","fs.readbin","diag.transport","diag.lights.status"};
    for (unsigned i=0;i<sizeof(read_only)/sizeof(read_only[0]);++i)
        if (!strcmp(method,read_only[i])) return true;
    return !strncmp(method,"upgrade.",8); /* Explicit USB maintenance, never input activation. */
}
static inline bool firmware_policy_service_rpc_allowed(bool bringup,bool services,const char *method)
{
    if (firmware_policy_rpc_allowed(bringup,method)) return true;
    if (!services || !method) return false;
    const char *allowed[]={"fs.write","fs.writebin","fs.txbegin","fs.txcommit",
        "diag.ble","diag.layer","diag.reboot","host.focused_app","v.oai.rgbcfg","v.oai.thstatus"};
    for (unsigned i=0;i<sizeof(allowed)/sizeof(allowed[0]);++i)
        if (!strcmp(method,allowed[i])) return true;
    return false;
}
static inline bool firmware_policy_hid_allowed(bool bringup,const uint8_t *report,size_t length)
{
    if (!bringup) return true;
    if (!report) return false;
    for (size_t i=0;i<length;++i) if (report[i]) return false;
    return true; /* Neutral keyboard/consumer reports release old Host state. */
}
