#pragma once
void ble_svc_gatt_changed(unsigned short start,unsigned short end);
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "nvs.h"
#include "freertos/queue.h"
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define pdTRUE 1
#define portMUX_TYPE int
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
int xQueuePeek(QueueHandle_t,void *,uint32_t);
int xQueueReset(QueueHandle_t);
int64_t esp_timer_get_time(void);
typedef struct { uint8_t type,val[6]; } ble_addr_t;
struct ble_gap_conn_desc { ble_addr_t peer_id_addr; struct { bool encrypted,bonded; } sec_state; };
struct ble_gap_event {
    int type;
    struct { int status;uint16_t conn_handle; } connect;
    struct { int status; } enc_change;
    struct { uint16_t attr_handle;bool cur_notify; } subscribe;
    struct { uint16_t value; } mtu;
};
enum { BLE_GAP_EVENT_CONNECT=1,BLE_GAP_EVENT_DISCONNECT,BLE_GAP_EVENT_ENC_CHANGE,
 BLE_GAP_EVENT_SUBSCRIBE,BLE_GAP_EVENT_MTU,BLE_GAP_EVENT_REPEAT_PAIRING,BLE_GAP_EVENT_ADV_COMPLETE };
enum { BLE_HS_CONN_HANDLE_NONE=65535,BLE_ERR_REM_USER_CONN_TERM=19,BLE_HS_ENOENT=2,
 BLE_HS_ENOMEM=6,BLE_HS_EBUSY=15,BLE_HS_ETIMEOUT=13,BLE_HS_ESTORE_CAP=30,BLE_HS_EALREADY=100,
 BLE_GAP_REPEAT_PAIRING_IGNORE=0,BLE_HS_IO_NO_INPUT_OUTPUT=3,
 BLE_SM_PAIR_KEY_DIST_ENC=1,BLE_SM_PAIR_KEY_DIST_ID=2 };
typedef struct { unsigned value; } ble_uuid_t;
typedef struct { ble_uuid_t u; } ble_uuid16_t;
#define BLE_UUID16_INIT(x) {{x}}
#define BLE_UUID16_DECLARE(x) (&(ble_uuid_t){x})
struct os_mbuf { uint16_t length;uint8_t bytes[512]; };
#define OS_MBUF_PKTLEN(p) ((p)->length)
struct ble_gatt_access_ctxt { int op;struct os_mbuf *om; };
typedef int (*access_fn)(uint16_t,uint16_t,struct ble_gatt_access_ctxt *,void *);
struct ble_gatt_dsc_def { const ble_uuid_t *uuid;unsigned att_flags;access_fn access_cb;void *arg; };
struct ble_gatt_chr_def { const ble_uuid_t *uuid;access_fn access_cb;void *arg;uint16_t *val_handle;unsigned flags;struct ble_gatt_dsc_def *descriptors; };
struct ble_gatt_svc_def { unsigned type;const ble_uuid_t *uuid;struct ble_gatt_chr_def *characteristics; };
enum { BLE_GATT_ACCESS_OP_READ_DSC=1,BLE_GATT_ACCESS_OP_READ_CHR,BLE_GATT_ACCESS_OP_WRITE_CHR,
 BLE_ATT_ERR_INSUFFICIENT_RES=17,BLE_ATT_ERR_READ_NOT_PERMITTED=2,BLE_ATT_ERR_INSUFFICIENT_AUTHEN=5,
 BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN=13,BLE_ATT_ERR_UNLIKELY=14,BLE_ATT_ERR_VALUE_NOT_ALLOWED=19 };
enum { BLE_ATT_F_READ=1,BLE_GATT_CHR_F_READ=1,BLE_GATT_CHR_F_READ_ENC=2,BLE_GATT_CHR_F_NOTIFY=4,
 BLE_GATT_CHR_F_WRITE=8,BLE_GATT_CHR_F_WRITE_NO_RSP=16,BLE_GATT_CHR_F_WRITE_ENC=32,BLE_GATT_SVC_TYPE_PRIMARY=1 };
struct ble_hs_adv_fields {
 uint8_t flags;ble_uuid16_t *uuids16;unsigned num_uuids16;bool uuids16_is_complete;
 uint16_t appearance;bool appearance_is_present;uint8_t *name;size_t name_len;bool name_is_complete;
};
struct ble_gap_adv_params { unsigned conn_mode,disc_mode,itvl_min,itvl_max; };
enum { BLE_HS_ADV_F_DISC_GEN=2,BLE_HS_ADV_F_BREDR_UNSUP=4,BLE_GAP_CONN_MODE_UND=1,BLE_GAP_DISC_MODE_GEN=1,BLE_HS_FOREVER=INT32_MAX };
struct ble_npl_event { int unused; };struct ble_npl_callout { int unused; };
struct ble_store_status_event { int unused; };
struct mock_hs_config {
 void (*sync_cb)(void);void (*reset_cb)(int);int (*store_status_cb)(struct ble_store_status_event *,void *);
 unsigned sm_io_cap,sm_bonding,sm_sc,sm_our_key_dist,sm_their_key_dist;
};
extern struct mock_hs_config ble_hs_cfg;
int ble_gap_terminate(uint16_t,unsigned);
bool ble_gap_adv_active(void);
int ble_gap_adv_stop(void);
int ble_svc_gap_device_name_set(const char *);
int ble_gap_adv_set_fields(const struct ble_hs_adv_fields *);
int ble_gap_adv_rsp_set_fields(const struct ble_hs_adv_fields *);
int ble_gap_adv_start(uint8_t,const void *,int32_t,const struct ble_gap_adv_params *,int (*)(struct ble_gap_event *,void *),void *);
int ble_gap_conn_find(uint16_t,struct ble_gap_conn_desc *);
int ble_gap_security_initiate(uint16_t);
int ble_gattc_exchange_mtu(uint16_t,void *,void *);
int ble_store_util_bonded_peers(ble_addr_t *,int *,int);
int ble_store_util_delete_peer(const ble_addr_t *);
int os_mbuf_append(struct os_mbuf *,const void *,size_t);
int ble_hs_mbuf_to_flat(struct os_mbuf *,void *,size_t,uint16_t *);
struct os_mbuf *ble_hs_mbuf_from_flat(const void *,size_t);
int ble_gatts_notify_custom(uint16_t,uint16_t,struct os_mbuf *);
int ble_hs_util_ensure_addr(int);
int ble_hs_id_infer_auto(int,uint8_t *);
void ble_npl_callout_reset(struct ble_npl_callout *,uint32_t);
uint32_t ble_npl_time_ms_to_ticks32(uint32_t);
void ble_npl_callout_init(struct ble_npl_callout *,void *,void (*)(struct ble_npl_event *),void *);
void *nimble_port_get_dflt_eventq(void);
int nimble_port_init(void);
void nimble_port_run(void);
void nimble_port_freertos_deinit(void);
void nimble_port_freertos_init(void (*)(void *));
void ble_svc_gap_init(void);void ble_svc_gatt_init(void);void ble_store_config_init(void);
int ble_att_set_preferred_mtu(unsigned);
int ble_gatts_count_cfg(const struct ble_gatt_svc_def *);
int ble_gatts_add_svcs(const struct ble_gatt_svc_def *);
