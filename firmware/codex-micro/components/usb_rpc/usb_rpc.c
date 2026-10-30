#include "usb_rpc.h"
#include <string.h>

size_t rpc_empty_report(uint8_t *out, size_t capacity)
{
    if (!out || !capacity) return 0;
    size_t length = capacity < RPC_BODY_SIZE ? capacity : RPC_BODY_SIZE;
    memset(out, 0, length);
    out[0] = RPC_CHANNEL;
    return length;
}

bool rpc_report_payload(uint8_t report_id, const uint8_t *report, size_t size,
                        const uint8_t **payload, size_t *length)
{
    if (!report || !payload || !length) return false;
    if (report_id == 0 && size == RPC_BODY_SIZE + 1 && report[0] == RPC_REPORT_ID) {
        ++report;
        --size;
    } else if (report_id != RPC_REPORT_ID) {
        return false;
    }
    if (size < 2 || size > RPC_BODY_SIZE || report[0] != RPC_CHANNEL ||
        report[1] > RPC_FRAGMENT_MAX || report[1] > size - 2) return false;
    *payload = report + 2;
    *length = report[1];
    return true;
}

void rpc_rx_reset(rpc_rx_t *rx)
{
    rx->length = 0;
    rx->depth = 0;
    rx->in_string = false;
    rx->escaped = false;
    rx->quarantine = false;
    rx->started_us = rx->last_us = 0;
}

void rpc_rx_abort(rpc_rx_t *rx, uint64_t now_us)
{
    rpc_rx_reset(rx);
    rx->quarantine = true;
    rx->last_us = rx->started_us = now_us;
    ++rx->errors;
}

void rpc_rx_tick(rpc_rx_t *rx, uint64_t now_us)
{
    if ((rx->length || rx->quarantine) &&
        (now_us < rx->last_us || now_us - rx->last_us >= 500000 ||
         now_us - rx->started_us >= 5000000)) {
        rx->timeout_now_us=now_us; rx->timeout_last_us=rx->last_us;
        rx->timeout_started_us=rx->started_us; rx->timeout_length=rx->length;
        if (now_us<rx->last_us) ++rx->backward_timeouts;
        else if (now_us-rx->last_us>=500000) ++rx->idle_timeouts;
        else ++rx->lifetime_timeouts;
        rpc_rx_reset(rx);
        ++rx->timeouts;
    }
}

void rpc_rx_feed(rpc_rx_t *rx, const uint8_t *data, size_t size, uint64_t now_us,
                 rpc_message_fn callback, void *context)
{
    rpc_rx_tick(rx, now_us);
    for (size_t i = 0; i < size; ++i) {
        uint8_t c = data[i];
        rx->last_us = now_us;
        if (rx->quarantine) {
            if (c == '\n' || c == '\r') rpc_rx_reset(rx);
            continue;
        }
        if (!rx->length) {
            if (c == ' ' || c == '\r' || c == '\n' || c == '\t') continue;
            if (c != '{') { rpc_rx_abort(rx, now_us); continue; }
            rx->started_us = now_us;
        }
        if (rx->length == RPC_MESSAGE_MAX || c == 0) {
            rpc_rx_abort(rx, now_us);
            continue;
        }
        rx->bytes[rx->length++] = c;
        if (rx->in_string) {
            if (c < 0x20) { rpc_rx_abort(rx, now_us); continue; }
            if (rx->escaped) rx->escaped = false;
            else if (c == '\\') rx->escaped = true;
            else if (c == '"') rx->in_string = false;
        } else if (c == '"') {
            rx->in_string = true;
        } else if (c == '{' || c == '[') {
            if (rx->depth == RPC_DEPTH_MAX) { rpc_rx_abort(rx, now_us); continue; }
            rx->closing[rx->depth++] = c == '{' ? '}' : ']';
        } else if (c == '}' || c == ']') {
            if (!rx->depth || rx->closing[rx->depth - 1] != c) {
                rpc_rx_abort(rx, now_us);
                continue;
            }
            if (--rx->depth == 0) {
                rx->bytes[rx->length] = 0;
                ++rx->messages;
                if (callback) callback(rx->bytes, rx->length, context);
                rpc_rx_reset(rx);
            }
        }
    }
}

void rpc_tx_reset(rpc_tx_t *tx)
{
    tx->head = tx->count = 0;
    tx->offset = 0;
}

bool rpc_tx_has_space(const rpc_tx_t *tx) { return tx->count < RPC_TX_SLOTS; }

bool rpc_tx_push(rpc_tx_t *tx, const uint8_t *json, size_t size)
{
    if (!json || !size || size > RPC_MESSAGE_MAX || !rpc_tx_has_space(tx)) return false;
    rpc_tx_entry_t *entry = &tx->entries[(tx->head + tx->count) % RPC_TX_SLOTS];
    memcpy(entry->bytes, json, size);
    entry->length = size;
    ++tx->count;
    return true;
}

bool rpc_tx_peek(const rpc_tx_t *tx, uint8_t body[RPC_BODY_SIZE])
{
    if (!tx->count || !body) return false;
    const rpc_tx_entry_t *entry = &tx->entries[tx->head];
    size_t remaining = entry->length + 1 - tx->offset;
    size_t chunk = remaining < RPC_FRAGMENT_MAX ? remaining : RPC_FRAGMENT_MAX;
    memset(body, 0, RPC_BODY_SIZE);
    body[0] = RPC_CHANNEL;
    body[1] = (uint8_t)chunk;
    for (size_t i = 0; i < chunk; ++i) {
        size_t position = tx->offset + i;
        body[2 + i] = position < entry->length ? entry->bytes[position] : '\n';
    }
    return true;
}

bool rpc_tx_accept(rpc_tx_t *tx)
{
    if (!tx->count) return false;
    size_t total = tx->entries[tx->head].length + 1;
    size_t remaining = total - tx->offset;
    tx->offset += remaining < RPC_FRAGMENT_MAX ? remaining : RPC_FRAGMENT_MAX;
    if (tx->offset == total) {
        tx->head = (tx->head + 1) % RPC_TX_SLOTS;
        --tx->count;
        tx->offset = 0;
    }
    return true;
}
