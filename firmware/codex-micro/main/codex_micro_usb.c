/*
 * Codex Micro — Gate 0A 验证固件
 *
 * 目标：让 ESP32-S3 通过 USB 模拟 OpenAI Codex Micro 键盘（Work Louder 制造）的
 * 最小设备接口，使 Windows ChatGPT Desktop 尝试原生识别该设备。
 *
 * 设备身份（协议事实，非本项目所有）：
 *   VID 0x303A / PID 0x8360，制造商 "Work Louder"，产品 "Codex Micro"，
 *   bcdDevice 0x0100（低两位为 0 = 有线 USB）。
 *   一个 vendor-defined HID 接口：usage page 0xFF00，Report ID 6，
 *   63-byte Input + 63-byte Output。
 *
 * 通信协议：JSON-RPC（标准 {"method","params","id"} 与紧凑 {"m","p","id"} 均接受）。
 *   帧格式（每个 64 字节报告，含报告 ID）：
 *     [0x06 report id][0x02 channel][len][UTF-8 JSON 片段，零填充]
 *   主机→设备：63 字节体 = [0x02][len][json...]，报告 ID 6 由 HID 层承载。
 *
 * 从Gate0 USB基线增量实现板级输入、灯光、配置、BLE及高级动作。
 * 当前实现与边界见README；整机Host互操作及PCB验收尚未执行。
 */

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "usb_rpc.h"
#include "board_io.h"
#include "default_actions.h"
#include "light_engine.h"
#include "light_demo.h"
#include "stick_stream.h"
#include "radial_runtime.h"
#include "layer_navigation.h"
#include "maintenance_frame.h"
#include "hid_state.h"
#include "config_store.h"
#include "config_rpc.h"
#include "firmware_update.h"
#include "action_runtime.h"
#include "smart_actions.h"
#include "ble_transport.h"
#include "esp_heap_caps.h"
#include "firmware_policy.h"
#include "usb_microphone.h"
#include <math.h>

#include "esp_log.h"
#include "esp_log_write.h"

#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "class/hid/hid_device.h"
#include "class/cdc/cdc_device.h"
#include "tusb.h"

#include "cJSON.h"
#include "keymap_default.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdatomic.h>

static const char *TAG = "codex";

#define CODEX_VERSION "v1.0.0-rc.7"
#ifdef CONFIG_CODEX_MAINBOARD_BRINGUP
#define MAINBOARD_BRINGUP true
#else
#define MAINBOARD_BRINGUP false
#endif
#ifdef CONFIG_CODEX_BOARD_SERVICES
#define BOARD_SERVICES true
#else
#define BOARD_SERVICES false
#endif
#define BLE_ENABLED (!MAINBOARD_BRINGUP || BOARD_SERVICES)
#define FIRMWARE_MODE (MAINBOARD_BRINGUP?(BOARD_SERVICES?"mainboard_services":"mainboard_bringup"):"full")

// ---- USB 身份 ---------------------------------------------------------
#define CODEX_VID        0x303A
#define CODEX_PID        0x8360
#define CODEX_BCD_DEVICE 0x0100

// ---- 报告布局 ---------------------------------------------------------
#define REPORT_ID       6
#define REPORT_CHANNEL  2     // 2 = RPC 通道
#define REPORT_BODY_LEN 63    // 不含报告 ID 的报告体
#define PAYLOAD_MAX     61    // 每个报告最多承载的 JSON 片段字节数


// ---- 设备描述符 -------------------------------------------------------
static const tusb_desc_device_t codex_device_descriptor = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
#ifdef CONFIG_CODEX_USB_MICROPHONE
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
#else
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
#endif
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = CODEX_VID,
    .idProduct          = CODEX_PID,
    .bcdDevice          = CODEX_BCD_DEVICE,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

// ---- 字符串描述符（索引 0 为语言 ID，esp_tinyusb 约定）---------------
static const char *codex_string_descriptor[] = {
    (char[]){0x09, 0x04},   // 0: 语言 English (0x0409)
    "Work Louder",          // 1: 制造商
    "Codex Micro",          // 2: 产品
    "123456",               // 3: 序列号
    "Codex Micro",          // 4: HID 接口
    "Codex Micro CDC",      // 5: CDC 接口
    "Codex Micro INMP441",  // 6: board microphone
};

// ---- HID 报告描述符（vendor 集合，Report ID 6）-----------------------
static const uint8_t codex_report_descriptor[] = {
    0x06, 0x00, 0xFF,  // Usage Page (Vendor Defined 0xFF00)
    0x09, 0x01,        // Usage (1)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x06,        //   Report ID (6)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x3F,        //   Report Count (63)
    0x09, 0x01,        //   Usage (1)
    0x81, 0x02,        //   Input (Data, Var, Abs)
    0x95, 0x3F,        //   Report Count (63)
    0x09, 0x02,        //   Usage (2)
    0x91, 0x02,        //   Output (Data, Var, Abs)
    0xC0,              // End Collection
};

// Separate standard HID interface; original Vendor Report6 descriptor is unchanged.
static const uint8_t standard_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(1)),
    TUD_HID_REPORT_DESC_CONSUMER(HID_REPORT_ID(2)),
    /* Standard Generic Desktop System Control, separate from MCU power policy. */
    0x05,0x01,0x09,0x80,0xa1,0x01,0x85,0x03,0x15,0x00,0x25,0x01,
    0x19,0x81,0x29,0x83,0x75,0x01,0x95,0x03,0x81,0x02,
    0x75,0x05,0x95,0x01,0x81,0x03,0xc0,
};

// ---- 配置描述符 -------------------------------------------------------
// 3 个接口：0 = HID（原有），1 = CDC Control，2 = CDC Data
#ifdef CONFIG_CODEX_USB_MICROPHONE
#define AUDIO_DESC_LEN TUD_AUDIO20_MIC_ONE_CH_DESC_LEN
#define CDC_DESC_LEN (TUD_CDC_DESC_LEN-7)
#define USB_INTERFACE_COUNT 6
#else
#define AUDIO_DESC_LEN 0
#define CDC_DESC_LEN TUD_CDC_DESC_LEN
#define USB_INTERFACE_COUNT 4
#endif
#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + 2*TUD_HID_DESC_LEN + CDC_DESC_LEN + AUDIO_DESC_LEN)

static const uint8_t codex_configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, USB_INTERFACE_COUNT, 0, TUSB_DESC_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    // 接口 0：HID，字符串 4，非 boot 协议，29 字节报告描述符，EP IN 0x81，64 字节包，10ms
    TUD_HID_DESCRIPTOR(0, 4, false, sizeof(codex_report_descriptor), 0x81, 64, 10),
    // 接口 1+2：CDC ACM（新增，日志）；通知 EP 0x82(8B)，数据 OUT 0x03 / IN 0x83(64B)
#ifdef CONFIG_CODEX_USB_MICROPHONE
    /* S3 allows five simultaneous IN endpoints INCLUDING EP0. Omit the
     * optional CDC notification EP; keep CDC line control and bulk data.
     * TinyUSB cdcd_open explicitly supports this CDC layout. */
    8,TUSB_DESC_INTERFACE_ASSOCIATION,1,2,TUSB_CLASS_CDC,2,0,0,
    9,TUSB_DESC_INTERFACE,1,0,0,TUSB_CLASS_CDC,2,0,5,
    5,TUSB_DESC_CS_INTERFACE,CDC_FUNC_DESC_HEADER,U16_TO_U8S_LE(0x0120),
    5,TUSB_DESC_CS_INTERFACE,CDC_FUNC_DESC_CALL_MANAGEMENT,0,2,
    4,TUSB_DESC_CS_INTERFACE,CDC_FUNC_DESC_ABSTRACT_CONTROL_MANAGEMENT,6,
    5,TUSB_DESC_CS_INTERFACE,CDC_FUNC_DESC_UNION,1,2,
    9,TUSB_DESC_INTERFACE,2,0,2,TUSB_CLASS_CDC_DATA,0,0,0,
    7,TUSB_DESC_ENDPOINT,0x03,TUSB_XFER_BULK,U16_TO_U8S_LE(64),0,
    7,TUSB_DESC_ENDPOINT,0x83,TUSB_XFER_BULK,U16_TO_U8S_LE(64),0,
#else
    TUD_CDC_DESCRIPTOR(1, 5, 0x82, 8, 0x03, 0x83, 64),
#endif
    TUD_HID_DESCRIPTOR(3, 0, false, sizeof(standard_report_descriptor), 0x84, 16, 5),
#ifdef CONFIG_CODEX_USB_MICROPHONE
    TUD_AUDIO20_MIC_ONE_CH_DESCRIPTOR(4,6,2,16,0x82,98),
#endif
};

// USB callbacks copy reports; the protocol task alone owns JSON/RX/TX state.
typedef struct {
    uint32_t epoch;
    uint64_t arrival_us;
    uint16_t length;
    uint8_t report_id;
    uint8_t bytes[64];
} usb_rx_item_t;
static QueueHandle_t usb_rx_queue;
static portMUX_TYPE link_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t link_epoch, rx_losses;
static bool link_mounted;
static rpc_rx_t rpc_rx;
static rpc_tx_t rpc_tx;
static uint32_t rejected_requests;
static uint64_t reboot_after_us,rx_max_processing_lag_us;
static uint32_t usb_reports_received,usb_max_queued;
typedef struct { uint32_t epoch; uint64_t time_us; uint16_t length; char bytes[256]; } log_record_t;
static QueueHandle_t log_queue;
static uint32_t log_dropped;
typedef struct { uint32_t epoch; uint64_t time_us; uint16_t length; uint8_t bytes[MAINTENANCE_FRAME_MAX]; } maintenance_record_t;
static QueueHandle_t maintenance_tx;
static maintenance_rx_t maintenance_rx;
static bool upgrade_reply_cdc;
static QueueHandle_t input_events;
static default_actions_t actions;
static bool input_resync;
static uint32_t seen_input_losses;
static board_light_frame_t desired_lights = {.brightness=26};
static light_engine_t host_lights;
static light_demo_t agent_demo;
static stick_stream_t stick;
static hid_state_t standard_hid;
static uint8_t active_layer,active_profile;
static action_runtime_t advanced;
static int requested_profile=-1;
static char focused_process[128],focused_path[512];
static int requested_layer=-1;
static bool keyboard_neutral=true,consumer_neutral=true,system_neutral=true;
static radial_runtime_t radial;
static layer_navigation_t navigation;
static bool prefer_ble;
static int transport_request=-1;
static uint32_t pending_ble_preference;
static unsigned pending_ble_slot;
static bool handover_released;
static unsigned handover_neutral;
static float brightness_scale=1;
static uint32_t unavailable_outputs;
static uint32_t protocol_epoch,source_usb_epoch,source_ble_epoch;
static bool selected_ble;
static _Atomic esp_err_t ble_start_error;
static uint64_t action_event_time;
static uint64_t diagnostic_lights_until;
static uint64_t battery_warning_test_until;
static uint64_t next_light_frame;
static board_light_frame_t last_light_frame;
typedef struct { board_light_frame_t frame; uint64_t time_us; uint32_t host_generation; uint8_t layer,profile; } light_trace_t;
static light_trace_t light_trace[16];
static uint32_t light_trace_count;
static uint64_t last_light_command_us;
static bool last_light_command_agents;

static bool submit_standard(uint8_t id,const uint8_t *report,unsigned length,void *ctx)
{
    (void)ctx;
    if (!firmware_policy_hid_allowed(MAINBOARD_BRINGUP,report,length)) return false;
    if (selected_ble) {
        ble_transport_status_t state; ble_transport_status(&state);
        unsigned expected=id==1?8:id==2?2:id==3?1:0;
        if (!report || !expected || length!=expected) return false;
        bool ready=id==1?state.keyboard_ready:id==2?state.consumer_ready:state.system_ready;
        /* A missing subscription must not block another report's startup
         * neutral or Vendor RPC. Subscription changes advance the link epoch
         * and resynchronize held inputs. A ready but congested queue retries. */
        if (!ready) {
            if (!state.connected || !state.authorized || !state.encrypted) return false;
            for (unsigned i=0;i<length;++i) if (report[i]) { ++unavailable_outputs; break; }
            return true;
        }
    }
    return selected_ble?ble_transport_send(id,report,length):(tud_hid_n_ready(1) && tud_hid_n_report(1,id,report,length));
}
static const char *resolve_binding(unsigned slot,void *ctx)
{ (void)ctx; return config_store_model()->profiles[active_profile].layer[active_layer].tokens[slot]; }
static bool send_key_action(const char *,bool,unsigned,void *);
static bool send_radial_notice(float,float,int,unsigned,void *);

static bool internal_action(const char *key,bool down)
{
    int id=action_token_id(key,"KI_LM");
    if (id<0) id=action_token_id(key,"KI_LS");
    if (id>=1 && id<=6) {
        int layer=keymap_layer_at(&config_store_model()->profiles[active_profile],id-1);
        if (layer<0) return true;
        bool temporary=!strncmp(key,"KI_LM",5);
        if (temporary) { if (!layer_navigation_hold(&navigation,layer,down)) return false; }
        else if (down) layer_navigation_select(&navigation,layer);
        unsigned target=layer_navigation_current(&navigation);
        /* Releasing the old profile's temporary keys must not overwrite the
         * already captured destination of a Profile/AppSense transition. */
        if (target!=active_layer && requested_profile<0) { requested_layer=target; input_resync=true; }
        return true;
    }
    id=action_token_id(key,"KI_PS");
    if (id>=1 && id<=6) {
        if (down && id<=config_store_model()->count && (unsigned)(id-1)!=active_profile) {
            requested_profile=id-1;
            requested_layer=keymap_layer_at(&config_store_model()->profiles[id-1],0);
            input_resync=true;
        }
        return true;
    }
    if (!strcmp(key,"KI_BLUP") || !strcmp(key,"KI_BLDW")) {
        if (down) brightness_scale=fminf(1,fmaxf(0,brightness_scale+(!strcmp(key,"KI_BLUP")?0.1f:-0.1f)));
        return true;
    }
    if (!strcmp(key,"KI_CUSB")) {
        if (down) { pending_ble_preference=0; transport_request=0; handover_released=false; handover_neutral=0; }
        return true;
    }
    int pair=action_token_id(key,"KI_CBTP"); id=pair>=0?pair:action_token_id(key,"KI_CBT");
    if (id>=1 && id<=3) {
        if (!down) return true;
        uint32_t command;
        if (!ble_transport_command(pair>=0?BLE_SLOT_PAIR:BLE_SLOT_SELECT,id-1,&command)) return false;
        pending_ble_preference=command;pending_ble_slot=id-1;
        handover_released=false;handover_neutral=0;return true;
    }
    if (!strncmp(key,"KI_CS_",6)) {
        if (selected_ble) {
            ble_transport_status_t state; ble_transport_status(&state);
            if (!state.vendor_ready) { ++unavailable_outputs; return true; }
        }
        const char *method=!strcmp(key,"KI_CS_TOGGLE")?"kb.cs.toggle":
            (!strcmp(key,"KI_CS_HIDE") || (!strcmp(key,"KI_CS_SHOW_TMP") && !down))?"kb.cs.hide":"kb.cs.show";
        if (!down && strcmp(key,"KI_CS_SHOW_TMP")) return true;
        if (rpc_tx.count>=RPC_TX_SLOTS-1) return false;
        char json[160];
        int n=snprintf(json,sizeof(json),"{\"method\":\"%s\",\"params\":{\"profile\":%u,\"layer\":%d}}",method,
            active_profile,keymap_layer_position(&config_store_model()->profiles[active_profile],active_layer));
        return n>0 && n<(int)sizeof(json) && rpc_tx_push(&rpc_tx,(const uint8_t *)json,n);
    }
    if (!strcmp(key,"KI_X")) {
        if (!down) return true;
        if (!radial_runtime_cancel(&radial,send_key_action,send_radial_notice,NULL)) return false;
        radial.closed_until_neutral=true; return true;
    }
    return false;
}

// Candidate codec from FreeMicro PROTOCOL.md; not an official protocol guarantee.
static bool enqueue_vendor_hid(const char *key, bool down, unsigned agent)
{
    /* The installed official client consumes rotary detents only as act=2.
     * A release from a macro/mapped key must not generate a second detent. */
    bool rotation=!strcmp(key,"ENC_CW") || !strcmp(key,"ENC_CC");
    if (rotation && !down) return true;
    if (rpc_tx.count >= RPC_TX_SLOTS-1) return false; // reserve one response slot
    char json[128];
    int n=snprintf(json,sizeof(json),
        "{\"method\":\"v.oai.hid\",\"params\":{\"k\":\"%s\",\"act\":%d,\"ag\":%u}}",
        key,rotation?2:(down?1:0),agent);
    return n>0 && n<(int)sizeof(json) && rpc_tx_push(&rpc_tx,(const uint8_t *)json,n);
}

static bool send_leaf_action(const char *key, bool down, unsigned agent, void *context)
{
    (void)context;
    if (MAINBOARD_BRINGUP) return true;
    if (down && (transport_request>=0 || pending_ble_preference)) return false;
    if (keymap_internal_token(key)) return internal_action(key,down);
    if (!strncmp(key,"KV_OAI_",7)) key+=7;
    bool select=strlen(key)==4 && !strncmp(key,"AG0",3) && key[3]>='0' && key[3]<='5';
    if (select) agent=key[3]-'0';
    if (selected_ble && strncmp(key,"KC_",3)) {
        ble_transport_status_t state; ble_transport_status(&state);
        if (!state.vendor_ready) { ++unavailable_outputs; if (down && select) actions.selected_agent=agent; return true; }
    }
    if (action_token_id(key,"SA_")>=0) {
        if (!down) return true;
        if (rpc_tx.count>=RPC_TX_SLOTS-1) return false;
        cJSON *notice=smart_action_notification(config_store_smart(),key);
        char *text=notice?cJSON_PrintUnformatted(notice):NULL;
        bool sent=text && rpc_tx_push(&rpc_tx,(const uint8_t *)text,strlen(text));
        cJSON_free(text); cJSON_Delete(notice); return sent;
    }
    if (!strncmp(key,"KC_",3)) return hid_state_key(&standard_hid,key,down,submit_standard,NULL);
    bool sent=enqueue_vendor_hid(key,down,agent);
    if (sent && down && select) actions.selected_agent=agent;
    return sent;
}

static bool send_key_action(const char *key,bool down,unsigned agent,void *ctx)
{ return action_runtime_event(&advanced,config_store_model(),key,down,agent,action_event_time?action_event_time:esp_timer_get_time(),send_leaf_action,ctx); }

static bool send_stick(float angle, float distance, void *context)
{
    (void)context;
    if (MAINBOARD_BRINGUP) return true;
    if (selected_ble) { ble_transport_status_t state; ble_transport_status(&state); if (!state.vendor_ready) return true; }
    if (rpc_tx.count>=RPC_TX_SLOTS-2) return false;
    char json[128];
    int n=snprintf(json,sizeof(json),
        "{\"method\":\"v.oai.rad\",\"params\":{\"a\":%.5f,\"d\":%.5f}}",angle,distance);
    return n>0 && n<(int)sizeof(json) && rpc_tx_push(&rpc_tx,(const uint8_t *)json,n);
}

static bool send_radial_notice(float angle,float distance,int sector,unsigned operation,void *ctx)
{
    (void)ctx;
    if (MAINBOARD_BRINGUP) return true;
    if (selected_ble) { ble_transport_status_t state; ble_transport_status(&state); if (!state.vendor_ready) return true; }
    if (rpc_tx.count>=RPC_TX_SLOTS-2) return false;
    int layer=keymap_layer_position(&config_store_model()->profiles[active_profile],active_layer);
    char json[192];
    int n=snprintf(json,sizeof(json),
        "{\"method\":\"kb.radial\",\"params\":{\"a\":%.5f,\"d\":%.5f,\"s\":%d,\"o\":%u,\"p\":%u,\"l\":%d}}",
        angle,distance,sector,operation,active_profile,layer);
    return n>0 && n<(int)sizeof(json) && rpc_tx_push(&rpc_tx,(const uint8_t *)json,n);
}

static void input_session_reset(void)
{
    board_input_snapshot_t state; board_inputs_resync(&state);
    default_actions_session(&actions,state.matrix_down,state.encoder_down);
    if (state.joystick_down) actions.suppressed |= 1U<<17;
    seen_input_losses=state.events_lost;
    input_resync=false;
    action_runtime_reset(&advanced);
    radial_runtime_reset(&radial);
    stick_stream_reset(&stick);
    stick.neutral_pending=true;
    layer_navigation_reset(&navigation,active_layer);
}

static uint32_t link_snapshot(bool *mounted, uint32_t *losses)
{
    portENTER_CRITICAL(&link_mux);
    uint32_t epoch = link_epoch;
    *mounted = link_mounted;
    *losses = rx_losses;
    portEXIT_CRITICAL(&link_mux);
    return epoch;
}

static uint32_t protocol_link_check(bool *mounted,uint32_t *losses)
{
    bool usb; uint32_t lost; uint32_t ue=link_snapshot(&usb,&lost);
    ble_transport_status_t ble; ble_transport_status(&ble);
    *mounted=selected_ble?((!usb || prefer_ble) && ble.ready):usb; *losses=selected_ble?ble.rx_lost:lost;
    bool valid=selected_ble?((!usb || prefer_ble) && ble.epoch==source_ble_epoch):(usb && ue==source_usb_epoch);
    return valid?protocol_epoch:UINT32_MAX;
}

static void codex_link_event(tinyusb_event_t *event, void *arg)
{
    (void)arg;
    if (event->id != TINYUSB_EVENT_ATTACHED && event->id != TINYUSB_EVENT_DETACHED) return;
    portENTER_CRITICAL(&link_mux);
    ++link_epoch;
    link_mounted = event->id == TINYUSB_EVENT_ATTACHED;
    portEXIT_CRITICAL(&link_mux);
    if (event->id == TINYUSB_EVENT_DETACHED) usb_microphone_link_down();
}

static void add_error(cJSON *resp, int code, const char *msg)
{
    cJSON *err = cJSON_CreateObject();
    cJSON_AddNumberToObject(err, "code", code);
    cJSON_AddStringToObject(err, "message", msg);
    cJSON_AddItemToObject(resp, "error", err);
}

static void add_ok(cJSON *resp)
{
    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "ok", 1);
    cJSON_AddItemToObject(resp, "result", result);
}

static void add_microphone_status(cJSON *result)
{
    usb_microphone_status_t mic; usb_microphone_status(&mic);
    cJSON *obj=cJSON_AddObjectToObject(result,"microphone");
    cJSON_AddStringToObject(obj,"model","INMP441");
    cJSON_AddStringToObject(obj,"channel","left");
    cJSON_AddNumberToObject(obj,"sample_rate",48000);
    cJSON_AddBoolToObject(obj,"initialized",mic.initialized);
    cJSON_AddBoolToObject(obj,"streaming",mic.streaming);
    cJSON_AddNumberToObject(obj,"init_error",mic.error);
    cJSON_AddNumberToObject(obj,"frames",mic.frames);
    cJSON_AddNumberToObject(obj,"peak",mic.peak);
    cJSON_AddNumberToObject(obj,"dropped",mic.dropped);
    cJSON_AddNumberToObject(obj,"underruns",mic.underruns);
    cJSON_AddNumberToObject(obj,"read_errors",mic.read_errors);
    cJSON_AddNumberToObject(obj,"usb_packets",mic.usb_packets);
    cJSON_AddNumberToObject(obj,"usb_bytes",mic.usb_bytes);
    cJSON_AddNumberToObject(obj,"fifo_bytes",mic.fifo_bytes);
}

static const char *get_method(cJSON *req)
{
    cJSON *m = cJSON_GetObjectItem(req, "method");
    if (cJSON_IsString(m)) return m->valuestring;
    m = cJSON_GetObjectItem(req, "m");
    if (cJSON_IsString(m)) return m->valuestring;
    return NULL;
}

static bool maintenance_reply(const char *json)
{
    maintenance_record_t frame={.epoch=source_usb_epoch,.time_us=esp_timer_get_time()};
    frame.length=maintenance_encode((const uint8_t *)json,strlen(json),frame.bytes,sizeof(frame.bytes));
    return frame.length && xQueueSend(maintenance_tx,&frame,0)==pdTRUE;
}

static void codex_handle_request(const uint8_t *json, size_t len, void *context)
{
    bool cdc=context!=NULL;
    // Reserve response capacity before any future side effect can execute.
    if (cdc?(!maintenance_tx || uxQueueSpacesAvailable(maintenance_tx)==0):!rpc_tx_has_space(&rpc_tx)) {
        ++rejected_requests;
        return; // overload: host times out; never execute then lose its response
    }
    cJSON *req = cJSON_ParseWithLength((const char *)json, len);
    if (req == NULL || !cJSON_IsObject(req)) {
        cJSON_Delete(req);
        ESP_LOGW(TAG, "request JSON parse failed");
        return;
    }

    cJSON *resp = cJSON_CreateObject();
    if (resp == NULL) {
        cJSON_Delete(req);
        return;
    }

    // 回显 id（数字/字符串），无 id 则回 null
    cJSON *id = cJSON_GetObjectItem(req, "id");
    if (id != NULL) {
        cJSON_AddItemToObject(resp, "id", cJSON_Duplicate(id, 1));
    } else {
        cJSON_AddNullToObject(resp, "id");
    }

    const char *method = get_method(req);
    cJSON *params=cJSON_GetObjectItemCaseSensitive(req,"params");
    if (!params) params=cJSON_GetObjectItemCaseSensitive(req,"p");
    // Bound correlation data so every side-effect response fits its reserved TX slot.
    if (id && !(cJSON_IsNull(id) || cJSON_IsNumber(id) ||
        (cJSON_IsString(id) && strlen(id->valuestring)<=96))) {
        cJSON_Delete(resp); cJSON_Delete(req); return;
    }
    if (cdc && (!method || (strncmp(method,"upgrade.",8) && strcmp(method,"sys.selftest") && strcmp(method,"sys.version") && strcmp(method,"diag.transport")))) {
        add_error(resp,-32601,"CDC maintenance accepts upgrade and health diagnostics only"); goto reply;
    }
    if (!firmware_policy_service_rpc_allowed(MAINBOARD_BRINGUP,BOARD_SERVICES,method)) {
        add_error(resp,-32002,"Physical input actions and hardware light diagnostics disabled in this build");
        goto reply;
    }
    if (method && !strncmp(method,"fs.",3)) {
        bool mutation=strcmp(method,"fs.list") && strcmp(method,"fs.read") && strcmp(method,"fs.readbin");
        board_input_snapshot_t inputs; board_inputs_snapshot(&inputs);
        int handled;
        if (mutation && !usb_microphone_upgrade_hold()) {
            add_error(resp,-32000,"Stop microphone recording before configuration writes"); handled=CONFIG_RPC_HANDLED;
        } else if (mutation && (reboot_after_us || transport_request>=0 || pending_ble_preference || firmware_update_busy() || actions.held || actions.pulse_pending || radial.held || action_runtime_busy(&advanced) ||
                   (!MAINBOARD_BRINGUP && (inputs.joystick_x || inputs.joystick_y || uxQueueMessagesWaiting(input_events))))) {
            add_error(resp,-32000,"Release controls before editing configuration"); handled=CONFIG_RPC_HANDLED;
        } else handled=config_rpc_handle(method,params,resp,protocol_epoch);
        if (handled==CONFIG_RPC_DEFERRED) { cJSON_Delete(req); return; }
        goto reply;
    }
    if (method && selected_ble && !strncmp(method,"upgrade.",8)) { add_error(resp,-32601,"Upgrade requires USB"); goto reply; }
    if (method && !strncmp(method,"upgrade.",8)) {
        bool mutation=strcmp(method,"upgrade.status")!=0;
        if (!strcmp(method,"upgrade.begin") && !usb_microphone_upgrade_hold()) {
            add_error(resp,-32000,"Stop microphone recording before upgrade"); goto reply;
        }
        board_input_snapshot_t state; board_inputs_snapshot(&state);
        if (mutation && (reboot_after_us || transport_request>=0 || pending_ble_preference || config_store_busy() || actions.held || actions.pulse_pending || radial.held || action_runtime_busy(&advanced) ||
            (!strcmp(method,"upgrade.begin") && firmware_policy_upgrade_raw_busy(
                MAINBOARD_BRINGUP,state.joystick_x,state.joystick_y,uxQueueMessagesWaiting(input_events)!=0))))
            add_error(resp,-32000,"Release controls and finish configuration before upgrade");
        else if (firmware_update_request(method,params,resp,protocol_epoch)) { upgrade_reply_cdc=cdc; cJSON_Delete(req); return; }
        goto reply;
    }

    if (method == NULL) {
        add_error(resp, -32601, "Method not found");
    } else if (strcmp(method, "sys.version") == 0) {
        cJSON *result = cJSON_CreateObject();
        cJSON_AddStringToObject(result, "version", CODEX_VERSION);
        cJSON_AddStringToObject(result,"firmware_mode",FIRMWARE_MODE);
        cJSON_AddItemToObject(resp, "result", result);
    } else if (strcmp(method,"diag.transport")==0) {
        cJSON *r=cJSON_CreateObject();
        portENTER_CRITICAL(&link_mux);
        uint32_t reports=usb_reports_received,peak=usb_max_queued;
        portEXIT_CRITICAL(&link_mux);
        cJSON_AddNumberToObject(r,"usb_reports",reports);
        cJSON_AddNumberToObject(r,"usb_queue_peak",peak);
        cJSON_AddNumberToObject(r,"rx_max_processing_lag_us",rx_max_processing_lag_us);
        cJSON_AddNumberToObject(r,"messages",rpc_rx.messages);
        cJSON_AddNumberToObject(r,"errors",rpc_rx.errors);
        cJSON_AddNumberToObject(r,"timeouts",rpc_rx.timeouts);
        cJSON_AddNumberToObject(r,"idle_timeouts",rpc_rx.idle_timeouts);
        cJSON_AddNumberToObject(r,"lifetime_timeouts",rpc_rx.lifetime_timeouts);
        cJSON_AddNumberToObject(r,"backward_timeouts",rpc_rx.backward_timeouts);
        cJSON_AddNumberToObject(r,"timeout_now_us",rpc_rx.timeout_now_us);
        cJSON_AddNumberToObject(r,"timeout_last_us",rpc_rx.timeout_last_us);
        cJSON_AddNumberToObject(r,"timeout_started_us",rpc_rx.timeout_started_us);
        cJSON_AddNumberToObject(r,"timeout_length",rpc_rx.timeout_length);
        cJSON_AddNumberToObject(r,"usb_epoch",source_usb_epoch);
        cJSON_AddNumberToObject(r,"protocol_epoch",protocol_epoch);
        cJSON_AddNumberToObject(r,"cdc_frames",maintenance_rx.accepted);
        cJSON_AddNumberToObject(r,"cdc_rejected",maintenance_rx.rejected);
        cJSON_AddNumberToObject(r,"unavailable_outputs",unavailable_outputs);
        cJSON_AddNumberToObject(r,"transport_request",transport_request);
        cJSON_AddNumberToObject(r,"pending_ble_command",pending_ble_preference);
        cJSON_AddNumberToObject(r,"handover_neutral",handover_neutral);
        cJSON_AddItemToObject(resp,"result",r);
    } else if (strcmp(method,"diag.reboot")==0) {
        if (reboot_after_us || config_store_busy() || firmware_update_busy() || !usb_microphone_upgrade_hold())
            add_error(resp,-32000,"Stop recording and finish maintenance before reboot");
        else { reboot_after_us=esp_timer_get_time()+1000000; add_ok(resp); }
    } else if (strcmp(method, "diag.inputs") == 0) {
        board_input_snapshot_t state;
        board_inputs_snapshot(&state);
        cJSON *result = cJSON_CreateObject();
        cJSON_AddNumberToObject(result, "matrix_down", state.matrix_down);
        cJSON_AddBoolToObject(result, "encoder_down", state.encoder_down);
        cJSON_AddBoolToObject(result, "joystick_down", state.joystick_down);
        cJSON_AddBoolToObject(result, "touch_down", state.touch_down);
        cJSON_AddNumberToObject(result, "joystick_x", state.joystick_x);
        cJSON_AddNumberToObject(result, "joystick_y", state.joystick_y);
        cJSON_AddNumberToObject(result,"joystick_raw_x",state.joystick_raw_x);
        cJSON_AddNumberToObject(result,"joystick_raw_y",state.joystick_raw_y);
        cJSON_AddNumberToObject(result,"joystick_center_x",state.joystick_center_x);
        cJSON_AddNumberToObject(result,"joystick_center_y",state.joystick_center_y);
        cJSON_AddBoolToObject(result,"joystick_calibrated",state.joystick_calibrated);
        cJSON_AddBoolToObject(result,"battery_valid",state.battery_valid);
        cJSON_AddNumberToObject(result,"battery_adc_raw",state.battery_adc_raw);
        cJSON_AddNumberToObject(result,"battery_adc_mv",state.battery_adc_mv);
        if (state.battery_valid) cJSON_AddNumberToObject(result, "battery_mv", state.battery_mv);
        else cJSON_AddNullToObject(result, "battery_mv");
        cJSON_AddBoolToObject(result, "low_voltage", state.low_voltage);
        cJSON_AddNumberToObject(result, "events_lost", state.events_lost);
        cJSON_AddNumberToObject(result, "adc_errors", state.adc_errors);
        cJSON_AddNumberToObject(result, "scan_overruns", state.scan_overruns);
        cJSON_AddItemToObject(resp, "result", result);
    } else if (strcmp(method,"diag.battery.warning_test")==0) {
        /* Project diagnostic: exercise indication for 10s without faking ADC,
         * changing low_voltage, writing Flash or enabling power management. */
        battery_warning_test_until=esp_timer_get_time()+10000000;
        diagnostic_lights_until=0; next_light_frame=0; add_ok(resp);
    } else if (strcmp(method,"diag.lights.status")==0) {
        cJSON *r=cJSON_CreateObject(),*frames=cJSON_AddArrayToObject(r,"frames");
        cJSON_AddNumberToObject(r,"host_generation",host_lights.generation);
        cJSON_AddNumberToObject(r,"last_host_command_us",last_light_command_us);
        cJSON_AddStringToObject(r,"last_host_command",last_light_command_agents?"v.oai.thstatus":"v.oai.rgbcfg");
        cJSON_AddNumberToObject(r,"keys_effect",host_lights.keys.effect);
        cJSON_AddNumberToObject(r,"ambient_effect",host_lights.ambient.effect);
        cJSON_AddNumberToObject(r,"submitted_frames",light_trace_count);
        cJSON_AddNumberToObject(r,"driver_faults",board_lights_faults());
        cJSON_AddNumberToObject(r,"brightness_scale",brightness_scale);
        cJSON_AddNumberToObject(r,"status_mask",last_light_frame.status_mask);
        cJSON_AddBoolToObject(r,"battery_warning_test_active",(uint64_t)esp_timer_get_time()<battery_warning_test_until);
        cJSON_AddBoolToObject(r,"demo_active",agent_demo.active);
        cJSON_AddNumberToObject(r,"demo_phase",light_demo_phase(&agent_demo,esp_timer_get_time()));
        cJSON_AddStringToObject(r,"demo_mode",agent_demo.cumulative?"cumulative":"status_cycle");
        cJSON_AddNumberToObject(r,"demo_step_ms",LIGHT_DEMO_STEP_US/1000);
        cJSON_AddStringToObject(r,"demo_trigger","third-row-left/Fast (local override)");
        unsigned count=light_trace_count<8?light_trace_count:8;
        for (unsigned i=0;i<count;++i) {
            const light_trace_t *t=&light_trace[(light_trace_count-count+i)%16];
            cJSON *entry=cJSON_CreateObject(),*keys=cJSON_AddArrayToObject(entry,"keys"),*ambient=cJSON_AddArrayToObject(entry,"ambient");
            cJSON_AddNumberToObject(entry,"time_us",t->time_us);
            cJSON_AddNumberToObject(entry,"host_generation",t->host_generation);
            cJSON_AddNumberToObject(entry,"brightness",t->frame.brightness);
            cJSON_AddNumberToObject(entry,"profile",t->profile);cJSON_AddNumberToObject(entry,"layer",t->layer);
            for (unsigned n=0;n<BOARD_KEY_LED_COUNT;++n) {
                board_rgb_t p=t->frame.keys[n];cJSON_AddItemToArray(keys,cJSON_CreateNumber((p.r<<16)|(p.g<<8)|p.b));
            }
            for (unsigned n=0;n<BOARD_AMBIENT_LED_COUNT;++n) {
                board_rgb_t p=t->frame.ambient[n];cJSON_AddItemToArray(ambient,cJSON_CreateNumber((p.r<<16)|(p.g<<8)|p.b));
            }
            cJSON_AddItemToArray(frames,entry);
        }
        cJSON_AddItemToObject(resp,"result",r);
    } else if (strcmp(method, "diag.lights") == 0) {
        // Project-local diagnostic RPC, NOT a claimed Work Louder protocol.
        cJSON *params=cJSON_GetObjectItem(req,"params");
        if (!params) params=cJSON_GetObjectItem(req,"p");
        const char *names[]={"chain","index","r","g","b","brightness","status_mask"};
        int values[7]; bool valid=true;
        const int limits[]={1,12,255,255,255,255,7};
        for (unsigned i=0;i<7;++i) {
            cJSON *v=cJSON_GetObjectItem(params,names[i]);
            if (!cJSON_IsNumber(v) || v->valuedouble<0 || v->valuedouble>limits[i] ||
                v->valuedouble!=v->valueint) { valid=false; break; }
            values[i]=v->valueint;
        }
        if (valid && values[0]==1 && values[1]>=BOARD_AMBIENT_LED_COUNT) valid=false;
        if (!valid) add_error(resp,-32602,"Invalid diagnostic light parameters");
        else {
            board_rgb_t *pixels=values[0]?desired_lights.ambient:desired_lights.keys;
            pixels[values[1]]=(board_rgb_t){values[2],values[3],values[4]};
            desired_lights.brightness=values[5]; desired_lights.status_mask=values[6];
            diagnostic_lights_until=esp_timer_get_time()+10000000;
            if (board_lights_submit(&desired_lights)==ESP_OK) add_ok(resp);
            else add_error(resp,-32603,"Lights unavailable");
        }
    } else if (strcmp(method, "device.status") == 0) {
        cJSON *result = cJSON_CreateObject();
        add_microphone_status(result);
        cJSON_AddStringToObject(result, "version", CODEX_VERSION);
        cJSON_AddStringToObject(result,"firmware_mode",FIRMWARE_MODE);
        cJSON_AddBoolToObject(result,"input_reporting_enabled",!MAINBOARD_BRINGUP);
        cJSON_AddBoolToObject(result,"lighting_enabled",!MAINBOARD_BRINGUP);
        cJSON_AddBoolToObject(result,"ble_enabled",BLE_ENABLED);
        cJSON_AddBoolToObject(result,"config_service_enabled",!MAINBOARD_BRINGUP || BOARD_SERVICES);
        cJSON_AddNumberToObject(result, "profile_index", active_profile);
        int layer_position=keymap_layer_position(&config_store_model()->profiles[active_profile],active_layer);
        cJSON_AddNumberToObject(result, "layer_index", layer_position+1);
        cJSON_AddNumberToObject(result,"selectedProfileIndex",active_profile);
        cJSON_AddNumberToObject(result,"selectedLayerIndex",layer_position+1);
        // Input accepts these optional fields only as number/bool, not null.
        // Omit unknown percentage/charging: this PCB supplies voltage only.
        board_input_snapshot_t battery; board_inputs_snapshot(&battery);
        if (battery.battery_valid) cJSON_AddNumberToObject(result,"battery_mv",battery.battery_mv);
        else cJSON_AddNullToObject(result,"battery_mv");
        cJSON_AddBoolToObject(result,"low_voltage",battery.low_voltage);
        cJSON_AddNumberToObject(result,"config_generation",config_store_generation());
        cJSON_AddBoolToObject(result,"config_writable",config_store_writable());
        cJSON_AddItemToObject(resp, "result", result);
    } else if (strcmp(method, "v.oai.rgbcfg") == 0 || strcmp(method,"v.oai.thstatus")==0) {
        cJSON *params=cJSON_GetObjectItemCaseSensitive(req,"params");
        if (!params) params=cJSON_GetObjectItemCaseSensitive(req,"p");
        if (light_engine_apply(&host_lights,method,params)) {
            last_light_command_us=esp_timer_get_time();last_light_command_agents=!strcmp(method,"v.oai.thstatus");
            diagnostic_lights_until=0; next_light_frame=0; add_ok(resp);
        } else add_error(resp,-32602,"Invalid or unsupported lighting parameters");
    } else if (strcmp(method,"diag.ble")==0) {
        ble_transport_status_t state; ble_transport_status(&state);
        const cJSON *op=cJSON_GetObjectItemCaseSensitive(params,"op");
        uint32_t token=0;
        if (op) {
            const cJSON *slot=cJSON_GetObjectItemCaseSensitive(params,"slot");
            int kind=cJSON_IsString(op)?(!strcmp(op->valuestring,"select")?BLE_SLOT_SELECT:!strcmp(op->valuestring,"pair")?BLE_SLOT_PAIR:!strcmp(op->valuestring,"forget")?BLE_SLOT_FORGET:-1):-1;
            if (kind<0 || !cJSON_IsNumber(slot) || slot->valuedouble!=slot->valueint || slot->valueint<0 || slot->valueint>2)
                add_error(resp,-32602,"Expected op select/pair/forget and slot 0..2");
            else if (reboot_after_us || transport_request>=0 || pending_ble_preference || actions.held || actions.pulse_pending || action_runtime_busy(&advanced) || firmware_update_busy() || config_store_busy())
                add_error(resp,-32000,"Release controls before BLE operation");
            else if (!ble_transport_command(kind,slot->valueint,&token)) add_error(resp,-32000,"BLE command queue unavailable");
        }
        if (!cJSON_GetObjectItem(resp,"error")) {
            cJSON *result=cJSON_CreateObject();
            cJSON_AddNumberToObject(result,"accepted_command",token); cJSON_AddNumberToObject(result,"last_command",state.last_command);
            cJSON_AddNumberToObject(result,"error",ble_start_error?ble_start_error:state.error); cJSON_AddNumberToObject(result,"slot",state.slot);
            cJSON_AddNumberToObject(result,"security_error",state.security_error);
            cJSON_AddNumberToObject(result,"security_attempts",state.security_attempts);
            cJSON_AddNumberToObject(result,"output_reads",state.output_reads);
            cJSON_AddNumberToObject(result,"output_writes",state.output_writes);
            cJSON_AddNumberToObject(result,"paired_mask",state.paired_mask); cJSON_AddNumberToObject(result,"mtu",state.mtu);
            cJSON_AddBoolToObject(result,"connected",state.connected); cJSON_AddBoolToObject(result,"ready",state.ready);
            cJSON_AddBoolToObject(result,"vendor_ready",state.vendor_ready);
            cJSON_AddBoolToObject(result,"keyboard_ready",state.keyboard_ready);
            cJSON_AddBoolToObject(result,"consumer_ready",state.consumer_ready);
            cJSON_AddBoolToObject(result,"system_ready",state.system_ready);
            cJSON_AddBoolToObject(result,"encrypted",state.encrypted);
            cJSON_AddBoolToObject(result,"authorized",state.authorized);
            cJSON_AddNumberToObject(result,"subscription_mask",state.subscription_mask);
            cJSON_AddBoolToObject(result,"pairing",state.pairing); cJSON_AddBoolToObject(result,"active_transport",selected_ble);
            cJSON_AddItemToObject(resp,"result",result);
        }
    } else if (strcmp(method,"host.focused_app")==0) {
        const cJSON *process=cJSON_GetObjectItemCaseSensitive(params,"process"),*path=cJSON_GetObjectItemCaseSensitive(params,"path");
        if (!cJSON_IsObject(params) || (process && !cJSON_IsString(process)) || (path && !cJSON_IsString(path)) ||
            (process && strlen(process->valuestring)>=sizeof(focused_process)) || (path && strlen(path->valuestring)>=sizeof(focused_path)))
            add_error(resp,-32602,"Invalid application identity");
        else if (config_store_busy() || firmware_update_busy()) add_error(resp,-32000,"Configuration or update busy");
        else {
            const char *pr=process?process->valuestring:"",*pa=path?path->valuestring:"";
            if (strcmp(pr,focused_process) || strcmp(pa,focused_path)) {
                strcpy(focused_process,pr); strcpy(focused_path,pa);
                unsigned profile=active_profile,layer=active_layer;
                if (keymap_focus(config_store_model(),pr,pa,&profile,&layer) && (profile!=active_profile || layer!=active_layer)) {
                    requested_profile=profile; requested_layer=layer; input_resync=true;
                }
            }
            add_ok(resp);
        }
    } else if (strcmp(method,"sys.selftest")==0) {
        board_input_snapshot_t state; board_inputs_snapshot(&state);
        cJSON *result=cJSON_CreateObject();
        add_microphone_status(result);
#ifdef CONFIG_CODEX_USB_MICROPHONE
        cJSON_AddBoolToObject(result,"usb_microphone_required",true);
#else
        cJSON_AddBoolToObject(result,"usb_microphone_required",false);
#endif
        cJSON_AddStringToObject(result,"firmware_mode",FIRMWARE_MODE);
        cJSON_AddBoolToObject(result,"input_reporting_enabled",!MAINBOARD_BRINGUP);
        cJSON_AddBoolToObject(result,"lighting_enabled",!MAINBOARD_BRINGUP);
        cJSON_AddBoolToObject(result,"ble_required",BLE_ENABLED);
        cJSON_AddBoolToObject(result,"board_definition_valid",board_definition_valid());
        cJSON_AddNumberToObject(result,"input_events_lost",state.events_lost);
        cJSON_AddNumberToObject(result,"adc_errors",state.adc_errors);
        cJSON_AddNumberToObject(result,"touch_errors",state.touch_errors);
        cJSON_AddNumberToObject(result,"rpc_errors",rpc_rx.errors);
        cJSON_AddNumberToObject(result,"lighting_faults",board_lights_faults());
        cJSON_AddNumberToObject(result,"lighting_generation",host_lights.generation);
        portENTER_CRITICAL(&link_mux); uint32_t dropped=log_dropped; portEXIT_CRITICAL(&link_mux);
        cJSON_AddNumberToObject(result,"cdc_log_dropped",dropped);
        ble_transport_status_t ble; ble_transport_status(&ble);
        cJSON_AddBoolToObject(result,"ble_initialized",ble.initialized);
        cJSON_AddNumberToObject(result,"ble_error",ble_start_error?ble_start_error:ble.error);
        cJSON_AddNumberToObject(result,"heap_free",heap_caps_get_free_size(MALLOC_CAP_8BIT));
        cJSON_AddNumberToObject(result,"heap_minimum",heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
        cJSON_AddNumberToObject(result,"heap_internal_free",heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
        cJSON_AddNumberToObject(result,"heap_psram_free",heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
        cJSON_AddStringToObject(result,"scope","software diagnostics; not hardware acceptance");
        cJSON_AddItemToObject(resp,"result",result);
    } else if (strcmp(method, "appmgr.list_active") == 0 ||
               strcmp(method, "appmgr.list_installed") == 0) {
        cJSON_AddItemToObject(resp, "result", cJSON_CreateArray());
    } else if (strcmp(method,"diag.layer")==0) {
        cJSON *layer=cJSON_GetObjectItemCaseSensitive(params,"layer");
        if (!cJSON_IsNumber(layer) || layer->valuedouble!=layer->valueint || layer->valueint<0 || layer->valueint>=6 ||
            !config_store_model()->profiles[active_profile].layer[layer->valueint].present)
            add_error(resp,-32602,"Layer not configured");
        else if (actions.held || actions.pulse_pending || action_runtime_busy(&advanced) || config_store_busy() || firmware_update_busy())
            add_error(resp,-32000,"Release controls before switching layer");
        else { active_layer=layer->valueint; input_session_reset(); add_ok(resp); }
    } else {
        add_error(resp, -32601, "Method not found");
    }

reply:
    char *out = cJSON_PrintUnformatted(resp);
    if (out && strlen(out)>RPC_MESSAGE_MAX) {
        cJSON_free(out);
        cJSON_DeleteItemFromObjectCaseSensitive(resp,"result");
        add_error(resp,-32602,"Response too large; use fs.readbin for files");
        out=cJSON_PrintUnformatted(resp);
    }
    if (out != NULL) {
        if (!(cdc?maintenance_reply(out):rpc_tx_push(&rpc_tx, (const uint8_t *)out, strlen(out)))) ++rejected_requests;
        cJSON_free(out);
    }
    cJSON_Delete(resp);
    cJSON_Delete(req);
}

static void transport_selection_step(bool usb,const ble_transport_status_t *ble)
{
    if (pending_ble_preference && ble->last_command==pending_ble_preference) {
        if (!ble->error && ble->slot==pending_ble_slot) transport_request=1;
        else input_resync=true;
        pending_ble_preference=0;
    }
    if (transport_request>=0 && !config_store_busy() && !firmware_update_busy()) {
        bool target_ble=!usb || transport_request==1;
        bool old_connected=selected_ble?ble->ready:usb;
        bool done=target_ble==selected_ble || !old_connected;
        if (!done && !handover_released) {
            handover_released=default_actions_release(&actions,send_key_action,NULL) &&
                radial_runtime_cancel(&radial,send_key_action,send_radial_notice,NULL) &&
                action_runtime_cancel(&advanced,config_store_model(),send_leaf_action,NULL) && send_stick(0,0,NULL);
        }
        if (!done && handover_released) {
            uint8_t zero[8]={0};
            const unsigned lengths[]={8,2,1};
            if (handover_neutral<3 && submit_standard(handover_neutral+1,zero,lengths[handover_neutral],NULL)) ++handover_neutral;
            done=handover_neutral==3 && !rpc_tx.count;
        }
        if (done) {
            prefer_ble=transport_request==1;transport_request=-1;
            handover_released=false;handover_neutral=0;
            /* Old-host neutral reports are accepted before routing changes.
             * The normal epoch transition suppresses physical held inputs. */
        }
    }
}

static void codex_protocol_task(void *arg)
{
    (void)arg;
    uint32_t active_epoch = UINT32_MAX, seen_losses = 0;
    uint64_t last_diagnostic_us = 0;
    bool was_updating=false;
    uint64_t touch_since=0; bool touch_long_handled=false;
    for (;;) {
        bool mounted;
        uint32_t losses;
        bool usb; uint32_t usb_losses; uint32_t usb_epoch=link_snapshot(&usb,&usb_losses);
        ble_transport_status_t ble; ble_transport_status(&ble);
        transport_selection_step(usb,&ble);
        bool use_ble=!usb || prefer_ble; mounted=use_ble?ble.ready:usb; losses=use_ble?ble.rx_lost:usb_losses;
        bool changed=active_epoch==UINT32_MAX || use_ble!=selected_ble ||
            (use_ble?ble.epoch!=source_ble_epoch:usb_epoch!=source_usb_epoch);
        if (changed) {
            if (selected_ble && !use_ble) ble_transport_disconnect();
            selected_ble=use_ble; source_usb_epoch=usb_epoch; source_ble_epoch=ble.epoch;
            ++protocol_epoch; ble_transport_discard_rx(); seen_losses=losses;
        }
        uint32_t epoch=protocol_epoch;
        if (epoch != active_epoch) {
            rpc_rx_reset(&rpc_rx);
            rpc_tx_reset(&rpc_tx);
            maintenance_reset(&maintenance_rx); upgrade_reply_cdc=false;
            active_epoch = epoch;
            protocol_epoch=epoch;
            config_rpc_session(epoch);
            firmware_update_session(epoch);
            light_engine_init(&host_lights);
            diagnostic_lights_until=0;
            battery_warning_test_until=0;
            hid_state_reset(&standard_hid);
            keyboard_neutral=consumer_neutral=system_neutral=true;
            requested_layer=requested_profile=-1;
            focused_process[0]=focused_path[0]=0;
            input_session_reset();
        }
        if (losses != seen_losses) {
            // Missing fragments cannot safely be joined; quarantine until LF/idle.
            rpc_rx_abort(&rpc_rx, esp_timer_get_time());
            seen_losses = losses;
            xQueueReset(usb_rx_queue);
        }
        usb_rx_item_t item;
        if (!selected_ble && mounted && maintenance_tx && uxQueueSpacesAvailable(maintenance_tx)>0) {
            uint8_t bytes[64];
            for (unsigned i=0;i<8 && tud_cdc_available() && uxQueueSpacesAvailable(maintenance_tx)>0;++i) {
                uint32_t n=tud_cdc_read(bytes,sizeof(bytes));
                maintenance_feed(&maintenance_rx,bytes,n,codex_handle_request,(void *)1);
            }
        }
        if (xQueueReceive(usb_rx_queue, &item, 1) == pdTRUE) {
            epoch = protocol_link_check(&mounted, &losses);
            if (!selected_ble && mounted && item.epoch == source_usb_epoch && epoch == active_epoch) {
                const uint8_t *payload;
                size_t length;
                if (rpc_report_payload(item.report_id, item.bytes, item.length, &payload, &length)) {
                    uint64_t handled_us=esp_timer_get_time();
                    if (handled_us>=item.arrival_us && handled_us-item.arrival_us>rx_max_processing_lag_us)
                        rx_max_processing_lag_us=handled_us-item.arrival_us;
                    rpc_rx_feed(&rpc_rx, payload, length, item.arrival_us, codex_handle_request, NULL);
                } else {
                    rpc_rx_abort(&rpc_rx, item.arrival_us);
                }
            }
        } else {
            rpc_rx_tick(&rpc_rx, esp_timer_get_time());
        }
        epoch = protocol_link_check(&mounted, &losses);
        if (selected_ble && mounted && epoch==active_epoch) {
            ble_transport_rx_t input;
            for (unsigned i=0;i<8 && ble_transport_receive(&input);++i) if (input.epoch==source_ble_epoch) {
                const uint8_t *payload; size_t length;
                if (rpc_report_payload(input.length==64?0:6,input.bytes,input.length,&payload,&length))
                    rpc_rx_feed(&rpc_rx,payload,length,esp_timer_get_time(),codex_handle_request,NULL);
                else rpc_rx_abort(&rpc_rx,esp_timer_get_time());
            }
        }
        if (mounted && epoch == active_epoch) {
            uint8_t body[RPC_BODY_SIZE];
            if (rpc_tx_peek(&rpc_tx,body) && (selected_ble?ble_transport_send(6,body,sizeof(body)):
                (tud_hid_ready() && tud_hid_report(REPORT_ID,body,sizeof(body))))) rpc_tx_accept(&rpc_tx);
        }
        if (rpc_tx_has_space(&rpc_tx)) {
            bool changed;
            cJSON *response=config_rpc_poll(epoch,&changed);
            if (changed) { active_layer=0; active_profile=config_store_model()->active; focused_process[0]=focused_path[0]=0; input_session_reset(); }
            if (response) {
                char *json=cJSON_PrintUnformatted(response);
                if (json) { rpc_tx_push(&rpc_tx,(const uint8_t *)json,strlen(json)); cJSON_free(json); }
                cJSON_Delete(response);
            }
        }
        if (upgrade_reply_cdc?uxQueueSpacesAvailable(maintenance_tx)>0:rpc_tx_has_space(&rpc_tx)) {
            cJSON *response=firmware_update_poll(epoch);
            if (response) {
                char *json=cJSON_PrintUnformatted(response);
                if (json) {
                    if (upgrade_reply_cdc) maintenance_reply(json);
                    else rpc_tx_push(&rpc_tx,(const uint8_t *)json,strlen(json));
                    cJSON_free(json);
                }
                cJSON_Delete(response);
                upgrade_reply_cdc=false;
            }
        }
        bool updating=firmware_update_busy();
        if (!updating && !config_store_busy() && !reboot_after_us) usb_microphone_upgrade_release();
        if (was_updating && !updating) input_session_reset();
        was_updating=updating;
        if (mounted && epoch==active_epoch) {
            uint8_t zero[8]={0};
            if (keyboard_neutral && submit_standard(1,zero,8,NULL)) keyboard_neutral=false;
            else if (consumer_neutral && !keyboard_neutral && submit_standard(2,zero,2,NULL)) consumer_neutral=false;
            else if (system_neutral && !keyboard_neutral && !consumer_neutral && submit_standard(3,zero,1,NULL)) system_neutral=false;
        }
        board_input_snapshot_t snapshot; board_inputs_snapshot(&snapshot);
        if (updating && agent_demo.active) { light_demo_stop(&agent_demo); next_light_frame=0; }
        if (light_demo_button(&agent_demo,(snapshot.matrix_down&(1U<<LIGHT_DEMO_TRIGGER_MATRIX_ID))!=0,
                !MAINBOARD_BRINGUP && !updating && !config_store_busy() && !reboot_after_us,esp_timer_get_time()))
            next_light_frame=0;
        if (MAINBOARD_BRINGUP || !mounted) input_session_reset();
        else if (epoch==active_epoch && transport_request<0 && !pending_ble_preference && !keyboard_neutral && !consumer_neutral && !system_neutral && !config_store_busy() && !updating) {
            if (snapshot.events_lost!=seen_input_losses) input_resync=true;
            if (input_resync) {
                bool layer_change=requested_layer>=0 && requested_profile<0 && snapshot.events_lost==seen_input_losses;
                bool released=layer_change?default_actions_release_for_layer(&actions,send_key_action,NULL):default_actions_release(&actions,send_key_action,NULL);
                if (released && radial_runtime_cancel(&radial,send_key_action,send_radial_notice,NULL) &&
                    (layer_change || action_runtime_cancel(&advanced,config_store_model(),send_leaf_action,NULL)) && send_stick(0,0,NULL)) {
                    if (requested_profile>=0) active_profile=requested_profile;
                    requested_profile=-1;
                    if (requested_layer>=0) active_layer=requested_layer;
                    requested_layer=-1;
                    if (layer_change) {
                        board_inputs_snapshot(&snapshot);
                        default_actions_layer_session(&actions,snapshot.matrix_down,snapshot.encoder_down,snapshot.joystick_down);
                        stick_stream_reset(&stick); stick.neutral_pending=true; input_resync=false;
                    } else input_session_reset();
                }
            } else {
                input_event_t input;
                // Peek until admitted: an endpoint busy condition must never lose a release.
                for (unsigned i=0; i<32 && xQueuePeek(input_events,&input,0)==pdTRUE; ++i) {
                    /* User-reserved physical Fast key: never forward down or up to Host. */
                    if (light_demo_owns_input(&input)) { xQueueReceive(input_events,&input,0); continue; }
                    if (input.kind==IN_TOUCH_EDGE && !input.value.edge.down && touch_long_handled) {
                        touch_long_handled=false; xQueueReceive(input_events,&input,0); continue;
                    }
                    if (input.kind==IN_TOUCH_EDGE && !input.value.edge.down) {
                        const keymap_profile_t *profile=&config_store_model()->profiles[active_profile];
                        int position=keymap_layer_position(profile,active_layer);
                        int layer=keymap_layer_at(profile,(position+1)%profile->layers);
                        if (layer>=0) {
                            layer_navigation_select(&navigation,layer);
                            requested_layer=layer_navigation_current(&navigation);
                        }
                        xQueueReceive(input_events,&input,0); input_resync=true; break;
                    } else if (input.kind==IN_JOYSTICK_VECTOR) {
                        stick_stream_update(&stick,input.value.joystick.x_q15,input.value.joystick.y_q15);
                    } else {
                        action_event_time=input.time_us;
                        bool accepted=default_actions_mapped_event(&actions,&input,resolve_binding,NULL,send_key_action,NULL);
                        action_event_time=0; if (!accepted) break;
                    }
                    xQueueReceive(input_events,&input,0);
                    if (input_resync || transport_request>=0 || pending_ble_preference) break;
                }
            }
        }
        uint64_t now = esp_timer_get_time();
        if (reboot_after_us && now>=reboot_after_us && !rpc_tx.count && !updating && !config_store_busy()) esp_restart();
        if (!MAINBOARD_BRINGUP && snapshot.touch_down) {
            if (!touch_since) { touch_since=now; touch_long_handled=false; }
            if (!touch_long_handled && now-touch_since>=2000000 && transport_request<0 && !pending_ble_preference && !updating && !config_store_busy()) {
                unsigned next=(ble.slot+1)%3;
                if (ble_transport_command((ble.paired_mask&(1U<<next))?BLE_SLOT_SELECT:BLE_SLOT_PAIR,next,NULL)) touch_long_handled=true;
            }
        } else touch_since=0;
        if (!MAINBOARD_BRINGUP && mounted && epoch==active_epoch && transport_request<0 && !pending_ble_preference && !input_resync && !updating) action_runtime_tick(&advanced,config_store_model(),now,send_leaf_action,NULL);
        if (!MAINBOARD_BRINGUP && mounted && epoch==active_epoch && transport_request<0 && !pending_ble_preference && !input_resync && !updating && uxQueueMessagesWaiting(input_events)==0) {
            const keymap_layer_t *layer=&config_store_model()->profiles[active_profile].layer[active_layer];
            if (layer->joystick_mode==JOYSTICK_RADIAL)
                radial_runtime_tick(&radial,layer,stick.x,stick.y,actions.selected_agent,now,send_key_action,send_radial_notice,NULL);
            else stick_stream_pump(&stick,now,send_stick,NULL);
        }
        if (!MAINBOARD_BRINGUP && now>=next_light_frame) {
            next_light_frame=now+25000;
            board_light_frame_t frame;
            if (now<diagnostic_lights_until) frame=desired_lights;
            else {
                light_engine_t style=host_lights;
                const keymap_layer_t *layer=&config_store_model()->profiles[active_profile].layer[active_layer];
                if (layer->has_lights) {
                    style.keys=layer->backlight; style.ambient=layer->underglow;
                    /* Input layers own their configured zones; Codex Agent
                     * overlays apply to the vendor/ChatGPT layer only. */
                    if (layer->joystick_mode==JOYSTICK_RADIAL) style.valid_agents=0;
                }
                light_engine_render(&style,now,actions.selected_agent,active_layer,
                    snapshot.low_voltage || now<battery_warning_test_until,&frame);
            }
            for (unsigned i=0;i<BOARD_KEY_LED_COUNT;++i) {
                frame.keys[i].r*=brightness_scale;frame.keys[i].g*=brightness_scale;frame.keys[i].b*=brightness_scale;
            }
            if (now>=diagnostic_lights_until) light_demo_render(&agent_demo,now,&frame);
            if (selected_ble && !snapshot.low_voltage && now>=battery_warning_test_until)
                frame.status_mask=ble.pairing && ((now/250000)&1)?0:1U<<ble.slot;
            if (memcmp(&frame,&last_light_frame,sizeof(frame))) {
                if (board_lights_submit(&frame)==ESP_OK) {
                    last_light_frame=frame;
                    light_trace[light_trace_count++%16]=(light_trace_t){.frame=frame,.time_us=now,
                        .host_generation=host_lights.generation,.layer=active_layer,.profile=active_profile};
                }
            }
        }
        if (now - last_diagnostic_us >= 5000000) {
            usb_microphone_status_t mic; usb_microphone_status(&mic);
            ESP_LOGI("microphone","initialized=%d streaming=%d frames=%lu peak=%lu dropped=%lu underruns=%lu errors=%lu init_error=%d",
                mic.initialized,mic.streaming,(unsigned long)mic.frames,(unsigned long)mic.peak,
                (unsigned long)mic.dropped,(unsigned long)mic.underruns,(unsigned long)mic.read_errors,mic.error);
            ESP_LOGI(TAG,"mode=%s input_reporting=%d lighting=%d",FIRMWARE_MODE,!MAINBOARD_BRINGUP,!MAINBOARD_BRINGUP);
            ESP_LOGI(TAG, "rpc rx=%lu invalid=%lu timeout=%lu overload=%lu rx_loss=%lu tx=%u",
                     (unsigned long)rpc_rx.messages, (unsigned long)rpc_rx.errors,
                     (unsigned long)rpc_rx.timeouts, (unsigned long)rejected_requests,
                     (unsigned long)losses, rpc_tx.count);
            board_input_snapshot_t state; board_inputs_snapshot(&state);
            ESP_LOGI("inputs", "keys=%04x joy=%d,%d battery_mv=%d valid=%d low=%d lost=%lu",
                     state.matrix_down,state.joystick_x,state.joystick_y,state.battery_mv,
                     state.battery_valid,state.low_voltage,(unsigned long)state.events_lost);

            last_diagnostic_us = now;
        }
    }
}

// ---- TinyUSB 回调 ------------------------------------------------------

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    return instance==0?codex_report_descriptor:instance==1?standard_report_descriptor:NULL;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
    if (instance == 0 && report_id == REPORT_ID && report_type == HID_REPORT_TYPE_INPUT) {
        return (uint16_t)rpc_empty_report(buffer, reqlen);
    }
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
    if (instance != 0 || report_type != HID_REPORT_TYPE_OUTPUT || !usb_rx_queue) return;
    if (!buffer || bufsize > 64) return;
    usb_rx_item_t item = {.arrival_us = esp_timer_get_time(), .length = bufsize, .report_id = report_id};
    bool mounted;
    uint32_t losses;
    /* Queue items carry the USB attachment epoch, not the protocol epoch.
     * Snapshot only lock-protected USB state in the TinyUSB task: protocol
     * selection and its epoch belong to codex_protocol_task. */
    item.epoch = link_snapshot(&mounted, &losses);
    if (!mounted) return;
    memcpy(item.bytes, buffer, bufsize);
    if (xQueueSend(usb_rx_queue, &item, 0) != pdTRUE) {
        portENTER_CRITICAL(&link_mux);
        ++rx_losses;
        portEXIT_CRITICAL(&link_mux);
    }
    uint32_t queued=uxQueueMessagesWaiting(usb_rx_queue);
    portENTER_CRITICAL(&link_mux);
    ++usb_reports_received;
    if (queued>usb_max_queued) usb_max_queued=queued;
    portEXIT_CRITICAL(&link_mux);
}

// ---- CDC 日志输出 ------------------------------------------------------
static vprintf_like_t s_orig_vprintf = NULL;

// ESP_LOG 输出钩子：UART0 原样输出（默认控制台回退；本 PCB 没有 CH340）+ CDC 输出（非阻塞）。
static int codex_log_vprintf(const char *fmt, va_list args)
{
    va_list a1;
    va_copy(a1, args);
    int ret = (s_orig_vprintf != NULL) ? s_orig_vprintf(fmt, a1) : 0;
    va_end(a1);

    if (log_queue && tud_cdc_connected()) {
        log_record_t record;
        bool mounted; uint32_t losses;
        record.epoch=link_snapshot(&mounted,&losses);
        record.time_us=esp_timer_get_time();
        va_list a2;
        va_copy(a2, args);
        int len = vsnprintf(record.bytes, sizeof(record.bytes), fmt, a2);
        va_end(a2);
        if (len > 0) {
            record.length=(size_t)len<sizeof(record.bytes)?len:sizeof(record.bytes)-1;
            if (xQueueSend(log_queue,&record,0)!=pdTRUE) {
                portENTER_CRITICAL(&link_mux); ++log_dropped; portEXIT_CRITICAL(&link_mux);
            }
        }
    }
    return ret;
}

/* Only this task writes CDC. Never log inside the sink (would recurse).
 * Producers copy bounded records and never wait for a host to drain CDC. */
static void cdc_log_task(void *arg)
{
    (void)arg; log_record_t record; maintenance_record_t control;
    for (;;) {
        if (xQueueReceive(maintenance_tx,&control,0)==pdTRUE) {
            unsigned offset=0;
            while (offset<control.length) {
                bool mounted; uint32_t losses; uint32_t epoch=link_snapshot(&mounted,&losses);
                if (!mounted || epoch!=control.epoch || !tud_cdc_connected() ||
                    (uint64_t)esp_timer_get_time()-control.time_us>2000000) break;
                offset+=tud_cdc_write(control.bytes+offset,control.length-offset);
                tud_cdc_write_flush(); if (offset<control.length) vTaskDelay(1);
            }
            continue; /* Never interleave log bytes inside a control frame. */
        }
        if (xQueueReceive(log_queue,&record,pdMS_TO_TICKS(10))!=pdTRUE) continue;
        unsigned offset=0;
        while (offset<record.length) {
            bool mounted; uint32_t losses;
            uint32_t epoch=link_snapshot(&mounted,&losses);
            if (!mounted || epoch!=record.epoch || !tud_cdc_connected() ||
                (uint64_t)esp_timer_get_time()-record.time_us>100000) break;
            uint32_t n=tud_cdc_write(record.bytes+offset,record.length-offset);
            offset+=n; tud_cdc_write_flush();
            if (offset<record.length) vTaskDelay(1);
        }
        if (offset<record.length) {
            portENTER_CRITICAL(&link_mux); ++log_dropped; portEXIT_CRITICAL(&link_mux);
        }
    }
}

// ---- 应用 --------------------------------------------------------------

static void *json_allocate(size_t size)
{
    return heap_caps_malloc_prefer(size,2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT,
                                    MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
}

void app_main(void)
{
    /* JSON trees and large configuration snapshots are task-owned data; keep
     * them out of the internal DMA/USB/BLE heap. Hooks are set before parsing. */
    cJSON_Hooks json_hooks={.malloc_fn=json_allocate,.free_fn=heap_caps_free};
    cJSON_InitHooks(&json_hooks);
    ESP_LOGI(TAG, "Codex Micro %s mode=%s — boot",CODEX_VERSION,FIRMWARE_MODE);

    ESP_ERROR_CHECK(config_store_init(KEYMAP_JSON));
    active_profile=config_store_model()->active;
    ESP_ERROR_CHECK(firmware_update_init());
    light_engine_init(&host_lights);
    input_events=xQueueCreate(128,sizeof(input_event_t));
    ESP_ERROR_CHECK(input_events ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(board_inputs_start(input_events));
#ifdef CONFIG_CODEX_USB_MICROPHONE
    esp_err_t mic_error=usb_microphone_start();
    if (mic_error!=ESP_OK) ESP_LOGE(TAG,"Microphone unavailable: %s; HID/CDC remain available",esp_err_to_name(mic_error));
#endif
    if (!MAINBOARD_BRINGUP) ESP_ERROR_CHECK(board_lights_start());
    usb_rx_queue = xQueueCreate(64, sizeof(usb_rx_item_t));
    ESP_ERROR_CHECK(usb_rx_queue ? ESP_OK : ESP_ERR_NO_MEM);
    maintenance_tx=xQueueCreate(4,sizeof(maintenance_record_t));
    ESP_ERROR_CHECK(maintenance_tx?ESP_OK:ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(codex_protocol_task, "codex_rpc", 12288, NULL, 4, NULL) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    tusb_cfg.event_cb = codex_link_event;
    tusb_cfg.descriptor.device = &codex_device_descriptor;
    tusb_cfg.descriptor.full_speed_config = codex_configuration_descriptor;
    tusb_cfg.descriptor.string = codex_string_descriptor;
    tusb_cfg.descriptor.string_count =
        sizeof(codex_string_descriptor) / sizeof(codex_string_descriptor[0]);
#if (TUD_OPT_HIGH_SPEED)
    tusb_cfg.descriptor.high_speed_config = codex_configuration_descriptor;
#endif

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    static uint8_t ble_map[sizeof(standard_report_descriptor)+sizeof(codex_report_descriptor)];
    memcpy(ble_map,standard_report_descriptor,sizeof(standard_report_descriptor));
    memcpy(ble_map+sizeof(standard_report_descriptor),codex_report_descriptor,sizeof(codex_report_descriptor));
    if (BLE_ENABLED) ble_start_error=codex_ble_init(ble_map,sizeof(ble_map));
    if (ble_start_error!=ESP_OK) ESP_LOGE(TAG,"BLE unavailable: %s; USB remains available",esp_err_to_name(ble_start_error));
    log_queue=xQueueCreate(16,sizeof(log_record_t));
    ESP_ERROR_CHECK(log_queue?ESP_OK:ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(cdc_log_task,"cdc_log",6144,NULL,1,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM);

    // 日志同时走 UART0（保留）与 CDC（新增）；未连接 CDC 时自动只走 UART0。
    s_orig_vprintf = esp_log_set_vprintf(codex_log_vprintf);

    ESP_LOGI(TAG, "USB ready — enumerating as Codex Micro HID+CDC (VID %04X PID %04X)",
             CODEX_VID, CODEX_PID);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
