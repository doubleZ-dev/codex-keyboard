#include "usb_rpc.h"
#undef NDEBUG
#include <assert.h>
#include <string.h>
#include <stdio.h>
static unsigned received;
static char message[RPC_MESSAGE_MAX + 1];
static void capture(const uint8_t *p, size_t n, void *ctx)
{ (void)ctx; memcpy(message, p, n); message[n] = 0; ++received; }
int main(void)
{
    rpc_rx_t rx = {0};
    static rpc_tx_t tx;
    const char *json = "{\"text\":\"中文 \\\" } [\",\"a\":[1,{}]}";
    for (size_t split = 0; split <= strlen(json); ++split) {
        rpc_rx_reset(&rx); received = 0;
        rpc_rx_feed(&rx, (const uint8_t *)json, split, 1, capture, NULL);
        rpc_rx_feed(&rx, (const uint8_t *)json + split, strlen(json)-split, 2, capture, NULL);
        assert(received == 1 && !strcmp(message, json));
    }
    received = 0;
    rpc_rx_feed(&rx, (const uint8_t *)"{}{}", 4, 3, capture, NULL);
    assert(received == 2);
    rpc_rx_feed(&rx, (const uint8_t *)"{]{}", 4, 4, capture, NULL);
    assert(rx.quarantine && received == 2);
    rpc_rx_feed(&rx, (const uint8_t *)"\n{}", 3, 5, capture, NULL);
    assert(received == 3);
    rpc_rx_feed(&rx, (const uint8_t *)"{", 1, 6, capture, NULL);
    rpc_rx_tick(&rx, 500006);
    assert(rx.length == 0 && rx.timeouts == 1);
    assert(rx.idle_timeouts==1 && rx.timeout_length==1 && rx.timeout_last_us==6);
    rpc_rx_feed(&rx,(const uint8_t *)"{",1,600000,capture,NULL);
    rpc_rx_tick(&rx,599999);
    assert(rx.backward_timeouts==1 && rx.timeout_now_us==599999);
    rpc_rx_feed(&rx,(const uint8_t *)"{",1,1,capture,NULL);
    for (uint64_t tick=400001;tick<5000000;tick+=400000)
        rpc_rx_feed(&rx,(const uint8_t *)" ",1,tick,capture,NULL);
    rpc_rx_tick(&rx,5000001);
    assert(rx.lifetime_timeouts==1 && rx.timeout_started_us==1);
    uint8_t oversized[RPC_MESSAGE_MAX+2];
    memset(oversized, ' ', sizeof oversized); oversized[0] = '{';
    rpc_rx_feed(&rx, oversized, sizeof oversized, 500007, capture, NULL);
    assert(rx.quarantine);
    rpc_rx_reset(&rx);
    memset(oversized, '{', 17);
    rpc_rx_feed(&rx, oversized, 17, 500008, capture, NULL);
    assert(rx.quarantine);
    for (size_t n = 0; n <= 65; ++n) {
        uint8_t guarded[67]; memset(guarded, 0xAA, sizeof guarded);
        size_t written = rpc_empty_report(guarded+1, n);
        assert(written == (n < 63 ? n : 63));
        assert(guarded[0] == 0xAA && guarded[written+1] == 0xAA);
    }
    uint8_t report[64] = {6,2,2,'{','}'};
    const uint8_t *payload; size_t length;
    assert(rpc_report_payload(0, report, 64, &payload, &length) && length == 2);
    assert(rpc_report_payload(6, report+1, 63, &payload, &length));
    assert(!rpc_report_payload(6, report, 64, &payload, &length));
    report[2] = 62;
    assert(!rpc_report_payload(0, report, 64, &payload, &length));
    for (size_t n = 1; n <= RPC_MESSAGE_MAX; ++n) {
        rpc_tx_reset(&tx); memset(oversized, 'x', n);
        assert(rpc_tx_push(&tx, oversized, n));
        size_t seen = 0; uint8_t a[63], b[63];
        while (rpc_tx_peek(&tx, a)) {
            assert(rpc_tx_peek(&tx, b) && !memcmp(a,b,63));
            for (unsigned i=0; i<a[1]; ++i) assert(a[i+2] == (seen++ < n ? 'x' : '\n'));
            assert(rpc_tx_accept(&tx));
        }
        assert(seen == n+1);
    }
    for (unsigned i=0; i<8; ++i) { uint8_t c = '0'+i; assert(rpc_tx_push(&tx,&c,1)); }
    assert(!rpc_tx_push(&tx,(const uint8_t *)"!",1));
    for (unsigned i=0; i<8; ++i) { uint8_t a[63]; assert(rpc_tx_peek(&tx,a) && a[2]=='0'+i); rpc_tx_accept(&tx); }
    assert(!rpc_tx_accept(&tx));
    printf("USB RPC: fragmentation, bounds, recovery, FIFO and %u TX lengths passed\n",RPC_MESSAGE_MAX);
}
