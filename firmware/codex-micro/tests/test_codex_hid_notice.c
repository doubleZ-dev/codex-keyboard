#undef NDEBUG
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "usb_rpc.h"

static rpc_tx_t rpc_tx;
/* Actual serializer extracted from the firmware translation unit by the runner. */
#include "vendor_hid_notice.inc"

static void expect(const char *key,bool down,unsigned agent,unsigned act)
{
    char expected[128];
    int n=snprintf(expected,sizeof(expected),
        "{\"method\":\"v.oai.hid\",\"params\":{\"k\":\"%s\",\"act\":%u,\"ag\":%u}}",
        key,act,agent);
    rpc_tx_reset(&rpc_tx);
    assert(enqueue_vendor_hid(key,down,agent));
    assert(rpc_tx.count==1 && rpc_tx.entries[0].length==(size_t)n);
    assert(!memcmp(rpc_tx.entries[0].bytes,expected,n));
}

int main(void)
{
    expect("ENC_CW",true,0,2);
    expect("ENC_CC",true,5,2);
    const char *edges[]={"ACT10","ACT11","ACT12","ENC_CLK","AG00","AG05"};
    for (unsigned i=0;i<sizeof(edges)/sizeof(edges[0]);++i) {
        expect(edges[i],true,3,1);
        expect(edges[i],false,3,0);
    }
    rpc_tx_reset(&rpc_tx);
    assert(enqueue_vendor_hid("ENC_CW",false,0) && !rpc_tx.count);
    assert(enqueue_vendor_hid("ENC_CC",false,0) && !rpc_tx.count);
    /* Congestion rejects before enqueue; the caller can retry the same detent. */
    for (unsigned i=0;i<RPC_TX_SLOTS-1;++i) assert(enqueue_vendor_hid("ENC_CW",true,0));
    assert(!enqueue_vendor_hid("ENC_CC",true,0) && rpc_tx.count==RPC_TX_SLOTS-1);
    assert(enqueue_vendor_hid("ENC_CC",false,0) && rpc_tx.count==RPC_TX_SLOTS-1);
    uint8_t frame[RPC_BODY_SIZE];
    while (rpc_tx.count) { assert(rpc_tx_peek(&rpc_tx,frame)); assert(rpc_tx_accept(&rpc_tx)); }
    assert(enqueue_vendor_hid("ENC_CC",true,0) && rpc_tx.count==1);
    puts("Actual Codex HID serializer: rotary act=2, release suppression, command edges and congestion retry passed");
}
