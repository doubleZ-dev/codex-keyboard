#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
    uint32_t epoch,rx_lost,last_command;
    uint32_t output_reads,output_writes;
    int error;
    int security_error;
    uint8_t security_attempts;
    uint16_t mtu;
    uint8_t slot,paired_mask,subscription_mask; /* bit 0 keyboard, 1 consumer, 2 vendor, 3 system */
    /* ready means at least one usable report, never all optional reports. */
    bool initialized,connected,encrypted,authorized,ready,keyboard_ready,consumer_ready,system_ready,vendor_ready,pairing;
} ble_transport_status_t;
typedef struct { uint32_t epoch; uint8_t length,bytes[64]; } ble_transport_rx_t;
typedef enum { BLE_SLOT_SELECT, BLE_SLOT_PAIR, BLE_SLOT_FORGET, BLE_LINK_DISCONNECT } ble_command_kind_t;
esp_err_t codex_ble_init(const uint8_t *report_map,size_t length);
void ble_transport_status(ble_transport_status_t *out);
bool ble_transport_command(ble_command_kind_t kind,unsigned slot,uint32_t *token);
bool ble_transport_send(uint8_t report_id,const void *data,size_t length);
bool ble_transport_receive(ble_transport_rx_t *out);
void ble_transport_discard_rx(void);
/* Coalesced high-priority handoff request, independent of command queue capacity. */
void ble_transport_disconnect(void);
