#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct {
    bool initialized, streaming;
    int error;
    uint32_t frames, dropped, underruns, read_errors, peak;
    uint32_t usb_packets, usb_bytes, fifo_bytes;
} usb_microphone_status_t;
esp_err_t usb_microphone_start(void);
void usb_microphone_link_down(void);
void usb_microphone_status(usb_microphone_status_t *out);
/* Protocol owner atomically reserves maintenance against USB stream opening. */
bool usb_microphone_upgrade_hold(void);
void usb_microphone_upgrade_release(void);
