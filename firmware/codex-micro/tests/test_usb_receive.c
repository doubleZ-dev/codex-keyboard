#undef NDEBUG
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "microphone_pcm.h"
typedef int hid_report_type_t;
enum { HID_REPORT_TYPE_OUTPUT=2,pdTRUE=1 };
typedef struct { uint32_t epoch; uint64_t arrival_us; uint16_t length; uint8_t report_id; uint8_t bytes[64]; } usb_rx_item_t;
static void *usb_rx_queue=(void *)1;
static uint32_t rx_losses;
static uint32_t usb_reports_received,usb_max_queued;
static int link_mux;
static bool attached=true,queue_full;
static usb_rx_item_t received;
static uint64_t esp_timer_get_time(void) { return 123; }
static uint32_t link_snapshot(bool *mounted,uint32_t *losses)
{ *mounted=attached;*losses=rx_losses;return 7; }
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
static int xQueueSend(void *queue,const usb_rx_item_t *item,unsigned timeout)
{ (void)queue;(void)timeout;if(queue_full)return 0;received=*item;return pdTRUE; }
static unsigned uxQueueMessagesWaiting(void *queue) { (void)queue;return 1; }
/* Extracted verbatim from main, so this tests the actual TinyUSB callback. */
#include "usb_receive_callback.inc"
int main(void)
{
    uint8_t report[64]={6,2,1,'x'};
    tud_hid_set_report_cb(0,6,HID_REPORT_TYPE_OUTPUT,report,64);
    assert(received.epoch==7 && received.arrival_us==123 && received.length==64);
    assert(memcmp(received.bytes,report,64)==0);
    received.epoch=0;attached=false;
    tud_hid_set_report_cb(0,6,HID_REPORT_TYPE_OUTPUT,report,64);
    assert(received.epoch==0);
    attached=true;queue_full=true;
    tud_hid_set_report_cb(0,6,HID_REPORT_TYPE_OUTPUT,report,64);
    assert(rx_losses==1);queue_full=false;
    tud_hid_set_report_cb(1,6,HID_REPORT_TYPE_OUTPUT,report,64);
    tud_hid_set_report_cb(0,6,1,report,64);
    tud_hid_set_report_cb(0,6,HID_REPORT_TYPE_OUTPUT,report,65);
    tud_hid_set_report_cb(0,6,HID_REPORT_TYPE_OUTPUT,NULL,64);
    assert(received.epoch==0 && rx_losses==1);
    assert(microphone_pcm16(0,65536)==0);
    assert(microphone_pcm16(INT32_MAX,65536)==32767);
    assert(microphone_pcm16(INT32_MIN,65536)==-32768);
    assert(microphone_pcm16(0x40000000,65536)==16384);
    assert(microphone_pcm16(-0x40000000,65536)==-16384);
    assert(microphone_pcm16(INT32_MAX,0)==0);
    assert(microphone_pcm16(0x40000000,131072)==32767);
    assert(microphone_pcm16(-0x40000000,131072)==-32768);
    puts("Actual USB callback: attachment epoch, detach, overflow, bounds; INMP441 PCM sign/gain/mute/clipping passed");
}
