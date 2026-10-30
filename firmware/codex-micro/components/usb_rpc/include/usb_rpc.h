#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { RPC_REPORT_ID = 6, RPC_CHANNEL = 2, RPC_BODY_SIZE = 63,
       RPC_FRAGMENT_MAX = 61, RPC_MESSAGE_MAX = 6144, RPC_DEPTH_MAX = 16,
       RPC_TX_SLOTS = 8 };

/* Pure single-owner helpers. No TinyUSB, RTOS, allocation or hardware access. */
size_t rpc_empty_report(uint8_t *out, size_t capacity);
/* Canonicalize only the two explicit callback layouts: ID6 + body,
 * or ID0 + a full ID-prefixed 64-byte report. Never guess/strip twice. */
bool rpc_report_payload(uint8_t report_id, const uint8_t *report, size_t size,
                        const uint8_t **payload, size_t *length);

typedef void (*rpc_message_fn)(const uint8_t *bytes, size_t length, void *context);
typedef struct {
    uint8_t bytes[RPC_MESSAGE_MAX + 1];
    uint8_t closing[RPC_DEPTH_MAX];
    size_t length;
    unsigned depth;
    bool in_string, escaped, quarantine;
    uint64_t started_us, last_us;
    uint32_t messages, errors, timeouts;
    uint32_t idle_timeouts, lifetime_timeouts, backward_timeouts;
    uint64_t timeout_now_us, timeout_last_us, timeout_started_us;
    size_t timeout_length;
} rpc_rx_t;

void rpc_rx_reset(rpc_rx_t *rx); /* resets partial state, retains counters */
void rpc_rx_abort(rpc_rx_t *rx, uint64_t now_us);
void rpc_rx_tick(rpc_rx_t *rx, uint64_t now_us);
/* Callback borrows bytes only until it returns; must not reenter this parser.
 * This validates framing, not full JSON syntax/UTF-8; handler validates JSON. */
void rpc_rx_feed(rpc_rx_t *rx, const uint8_t *data, size_t size, uint64_t now_us,
                 rpc_message_fn callback, void *context);

typedef struct {
    uint8_t bytes[RPC_MESSAGE_MAX];
    size_t length;
} rpc_tx_entry_t;
typedef struct {
    rpc_tx_entry_t entries[RPC_TX_SLOTS];
    unsigned head, count;
    size_t offset;
} rpc_tx_t;

void rpc_tx_reset(rpc_tx_t *tx);
bool rpc_tx_has_space(const rpc_tx_t *tx);
bool rpc_tx_push(rpc_tx_t *tx, const uint8_t *json, size_t size); /* copies */
bool rpc_tx_peek(const rpc_tx_t *tx, uint8_t body[RPC_BODY_SIZE]);
/* Call only after the transport synchronously copied/accepted the peeked frame.
 * A failed/busy submit leaves the queue untouched. LF is appended once. */
bool rpc_tx_accept(rpc_tx_t *tx);
