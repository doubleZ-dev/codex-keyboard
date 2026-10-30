#include "ble_transport.h"
#include "ble_slot_policy.h"
#ifdef CODEX_BLE_HOST_TEST
#include "ble_sdk_mock.h"
#else
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "nvs.h"
#include "esp_timer.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#endif
#include <string.h>
#include <stdio.h>

void ble_store_config_init(void);
typedef struct { uint8_t state; ble_addr_t peer; } slot_t;
typedef struct { uint32_t magic; uint8_t selected,reserved[3]; slot_t slot[3]; } slot_record_t;
typedef struct { ble_command_kind_t kind; uint8_t slot; uint32_t token; } command_t;
typedef struct { uint32_t epoch; uint64_t deadline; uint8_t id,length,data[63]; } tx_t;
static QueueHandle_t commands,rx_queue,tx_queue;
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
static ble_transport_status_t view;
static uint32_t next_command;
static bool disconnect_requested;
/* All fields below are NimBLE host-task owned after init. */
static slot_record_t record;
static nvs_handle_t storage;
static bool storage_ok,synced,encrypted,authorized,disconnect_pending,subscribed[4];
static uint16_t connection=BLE_HS_CONN_HANDLE_NONE,mtu=23,in_handle[4];
static uint32_t epoch=1,rx_lost,last_command;
static int last_error;
static int security_error;
static uint8_t security_attempts;
static uint64_t security_retry_at,security_deadline;
static uint8_t own_type;
static uint64_t pair_until;
static const uint8_t *map;
static size_t map_size;
static uint8_t last_input[4][63];
static uint8_t last_output[63],keyboard_leds;
static uint32_t output_reads,output_writes;
static struct ble_npl_callout tick_callout;
static const uint8_t report_ref[][2]={{1,1},{2,1},{6,1},{6,2},{1,2},{3,1}};
static int gap_event(struct ble_gap_event *,void *);
static unsigned report_index(uint8_t id) { return id==1?0:id==2?1:id==3?3:2; }
static bool same(const ble_addr_t *a,const ble_addr_t *b)
{ return a->type==b->type && !memcmp(a->val,b->val,6); }
static void publish(void)
{
    ble_transport_status_t s={.initialized=synced,.epoch=epoch,.rx_lost=rx_lost,.last_command=last_command,
        .output_reads=output_reads,.output_writes=output_writes,
        .error=last_error,.security_error=security_error,.security_attempts=security_attempts,
        .slot=record.selected,.mtu=mtu,.connected=connection!=BLE_HS_CONN_HANDLE_NONE,
        .encrypted=encrypted,.authorized=authorized,
        .keyboard_ready=authorized && encrypted && subscribed[0],
        .consumer_ready=authorized && encrypted && subscribed[1],
        .system_ready=authorized && encrypted && subscribed[3],
        .vendor_ready=authorized && encrypted && subscribed[2] && mtu>=66,
        .pairing=pair_until>(uint64_t)esp_timer_get_time()};
    s.ready=s.keyboard_ready || s.consumer_ready || s.vendor_ready || s.system_ready;
    for (unsigned i=0;i<4;++i) if (subscribed[i]) s.subscription_mask|=1U<<i;
    for (unsigned i=0;i<3;++i) if (record.slot[i].state==1) s.paired_mask|=1U<<i;
    portENTER_CRITICAL(&mux); view=s; portEXIT_CRITICAL(&mux);
}
void ble_transport_status(ble_transport_status_t *out)
{ portENTER_CRITICAL(&mux); *out=view; portEXIT_CRITICAL(&mux); }
static bool persist(void)
{
    esp_err_t err=storage_ok?nvs_set_blob(storage,"slots",&record,sizeof(record)):ESP_ERR_INVALID_STATE;
    if (err==ESP_OK) err=nvs_commit(storage);
    if (err!=ESP_OK) { storage_ok=false; last_error=err; }
    return err==ESP_OK;
}
static void disconnect(void)
{
    authorized=false; encrypted=false; ++epoch; xQueueReset(tx_queue); xQueueReset(rx_queue);
    memset(last_output,0,sizeof(last_output)); keyboard_leds=0;
    if (connection!=BLE_HS_CONN_HANDLE_NONE) { disconnect_pending=true; ble_gap_terminate(connection,BLE_ERR_REM_USER_CONN_TERM); }
    publish();
}
static void advertise(void)
{
    if (!synced || !storage_ok || connection!=BLE_HS_CONN_HANDLE_NONE || ble_gap_adv_active()) return;
    if (record.slot[record.selected].state!=1 && !pair_until) return;
    char name[24]; snprintf(name,sizeof(name),"Codex Micro #%u",record.selected+1);
    ble_svc_gap_device_name_set(name);
    static const ble_uuid16_t uuid=BLE_UUID16_INIT(0x1812);
    struct ble_hs_adv_fields fields={0};
    fields.flags=BLE_HS_ADV_F_DISC_GEN|BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids16=(ble_uuid16_t *)&uuid; fields.num_uuids16=1; fields.uuids16_is_complete=1;
    fields.appearance=0x03c1; fields.appearance_is_present=1;
    int rc=ble_gap_adv_set_fields(&fields);
    struct ble_hs_adv_fields scan={0}; scan.name=(uint8_t *)name; scan.name_len=strlen(name); scan.name_is_complete=1;
    if (!rc) rc=ble_gap_adv_rsp_set_fields(&scan);
    struct ble_gap_adv_params params={0}; params.conn_mode=BLE_GAP_CONN_MODE_UND; params.disc_mode=BLE_GAP_DISC_MODE_GEN;
    params.itvl_min=160; params.itvl_max=240;
    if (!rc) rc=ble_gap_adv_start(own_type,NULL,BLE_HS_FOREVER,&params,gap_event,NULL);
    if (rc) last_error=rc;
}
static bool allowed(const struct ble_gap_conn_desc *desc)
{
    uint8_t states[3],peers[3][7],peer[7]; peer[0]=desc->peer_id_addr.type; memcpy(peer+1,desc->peer_id_addr.val,6);
    for (unsigned i=0;i<3;++i) { states[i]=record.slot[i].state; peers[i][0]=record.slot[i].peer.type; memcpy(peers[i]+1,record.slot[i].peer.val,6); }
    return ble_slot_allow(record.selected,states,peers,peer,pair_until>(uint64_t)esp_timer_get_time());
}
static void authenticate(void)
{
    if (connection==BLE_HS_CONN_HANDLE_NONE || !storage_ok || disconnect_pending) return;
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(connection,&desc) || !allowed(&desc)) { disconnect(); return; }
    encrypted=desc.sec_state.encrypted;
    if (!encrypted || !desc.sec_state.bonded) return;
    ble_addr_t peers[3]; int count=0;
    if (ble_store_util_bonded_peers(peers,&count,3)) return;
    bool stored=false;
    for (int i=0;i<count;++i) if (same(&peers[i],&desc.peer_id_addr)) stored=true;
    if (!stored) return; /* Don't publish a pairing before NimBLE persisted its keys. */
    slot_t *slot=&record.slot[record.selected];
    if (!slot->state) {
        slot->state=1; slot->peer=desc.peer_id_addr;
        if (!persist()) { disconnect(); return; }
    }
    pair_until=0; authorized=true; security_retry_at=security_deadline=0;
    last_error=0;
    /* The report map gained System Control. Notify bonded clients that cached
     * attributes must be rediscovered; no bond deletion is required. */
    ble_svc_gatt_changed(1,0xffff);
}
static void initiate_security(void)
{
    ++security_attempts;
    security_error=ble_gap_security_initiate(connection);
    if (!security_error || security_error==BLE_HS_EALREADY) { security_retry_at=0; return; }
    if ((security_error==BLE_HS_ENOMEM || security_error==BLE_HS_EBUSY) && security_attempts<5)
        security_retry_at=esp_timer_get_time()+250000;
    else { last_error=security_error; security_retry_at=0; disconnect(); }
}
static int gap_event(struct ble_gap_event *event,void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status) { last_error=event->connect.status; advertise(); break; }
        connection=event->connect.conn_handle; disconnect_pending=false; mtu=23; ++epoch;
        memset(subscribed,0,sizeof(subscribed)); memset(last_input,0,sizeof(last_input));
        memset(last_output,0,sizeof(last_output)); keyboard_leds=0;
        { struct ble_gap_conn_desc desc;
          if (ble_gap_conn_find(connection,&desc) || !allowed(&desc)) { disconnect(); break; } }
        encrypted=authorized=false; security_attempts=0; security_error=0;
        security_deadline=esp_timer_get_time()+15000000;
        initiate_security();
        if (disconnect_pending) break;
        ble_gattc_exchange_mtu(connection,NULL,NULL); break;
    case BLE_GAP_EVENT_DISCONNECT:
        connection=BLE_HS_CONN_HANDLE_NONE; encrypted=authorized=disconnect_pending=false; ++epoch;
        security_retry_at=security_deadline=0;
        memset(last_output,0,sizeof(last_output)); keyboard_leds=0;
        memset(subscribed,0,sizeof(subscribed)); xQueueReset(tx_queue); xQueueReset(rx_queue); advertise(); break;
    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.status) { last_error=event->enc_change.status; disconnect(); }
        else {
            bool before=encrypted; authenticate();
            if (!encrypted) authorized=false;
            if (before!=encrypted) { ++epoch; xQueueReset(tx_queue); xQueueReset(rx_queue); }
        }
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        for (unsigned i=0;i<4;++i) if (event->subscribe.attr_handle==in_handle[i] && subscribed[i]!=event->subscribe.cur_notify) {
            subscribed[i]=event->subscribe.cur_notify; ++epoch; xQueueReset(tx_queue); xQueueReset(rx_queue);
        }
        break;
    case BLE_GAP_EVENT_MTU: mtu=event->mtu.value; break;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* Never silently replace an existing slot/bond on host request. */
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    case BLE_GAP_EVENT_ADV_COMPLETE: advertise(); break;
    default: break;
    }
    publish(); return 0;
}
static int access(uint16_t conn,uint16_t attr,struct ble_gatt_access_ctxt *ctxt,void *arg)
{
    (void)attr; uintptr_t kind=(uintptr_t)arg;
    const uint8_t info[]={0x11,0x01,0x00,0x02};
    const uint8_t pnp[]={2,0x3a,0x30,0x60,0x83,0x01,0x01};
    if (ctxt->op==BLE_GATT_ACCESS_OP_READ_DSC) return os_mbuf_append(ctxt->om,arg,2)?BLE_ATT_ERR_INSUFFICIENT_RES:0;
    if (ctxt->op==BLE_GATT_ACCESS_OP_READ_CHR) {
        const void *data=NULL; size_t n=0;
        if ((kind==6 || kind==21) && (!authorized || !encrypted || conn!=connection))
            return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
        if (kind==10) { data=map; n=map_size; }
        else if (kind==11) { data=info; n=sizeof(info); }
        else if (kind==12) { data=pnp; n=sizeof(pnp); }
        else if (kind==6) { data=last_output; n=sizeof(last_output); ++output_reads; }
        else if (kind==21) { data=&keyboard_leds; n=1; ++output_reads; }
        else if (kind<4) { data=last_input[kind]; n=kind==0?8:kind==1?2:kind==2?63:1; }
        else return BLE_ATT_ERR_READ_NOT_PERMITTED;
        return os_mbuf_append(ctxt->om,data,n)?BLE_ATT_ERR_INSUFFICIENT_RES:0;
    }
    if (!authorized || !encrypted || conn!=connection) return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    uint16_t n=OS_MBUF_PKTLEN(ctxt->om);
    if (kind==20) return n==1?0:BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN; /* control point */
    if (kind==21) {
        if (n!=1) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        return ble_hs_mbuf_to_flat(ctxt->om,&keyboard_leds,1,NULL)?BLE_ATT_ERR_INSUFFICIENT_RES:0;
    }
    if (kind!=6 || (n!=63 && n!=64)) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    if (!subscribed[2] || mtu<66) return BLE_ATT_ERR_UNLIKELY;
    ble_transport_rx_t item={.epoch=epoch,.length=n};
    if (ble_hs_mbuf_to_flat(ctxt->om,item.bytes,sizeof(item.bytes),NULL)) return BLE_ATT_ERR_UNLIKELY;
    /* Native ATT body vs host Report-ID-prefixed BLE output are separate accepted forms. */
    if ((n==63 && item.bytes[0]!=2) || (n==64 && (item.bytes[0]!=6 || item.bytes[1]!=2))) return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    if (xQueueSend(rx_queue,&item,0)!=pdTRUE) { ++rx_lost; publish(); return BLE_ATT_ERR_INSUFFICIENT_RES; }
    /* HOGP reads return the last accepted Output value, without Report ID. */
    memcpy(last_output,item.bytes+(n==64),sizeof(last_output)); ++output_writes;
    return 0;
}
#define REPORT_DSC(index) (struct ble_gatt_dsc_def[]){{.uuid=BLE_UUID16_DECLARE(0x2908),.att_flags=BLE_ATT_F_READ,.access_cb=access,.arg=(void *)report_ref[index]},{0}}
#define INPUT_CHR(index,reference) {.uuid=BLE_UUID16_DECLARE(0x2a4d),.access_cb=access,.arg=(void *)(uintptr_t)(index),.val_handle=&in_handle[index],.flags=BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_READ_ENC|BLE_GATT_CHR_F_NOTIFY,.descriptors=REPORT_DSC(reference)}
static const struct ble_gatt_svc_def services[]={
    {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=BLE_UUID16_DECLARE(0x1812),.characteristics=(struct ble_gatt_chr_def[]){
        {.uuid=BLE_UUID16_DECLARE(0x2a4a),.access_cb=access,.arg=(void *)11,.flags=BLE_GATT_CHR_F_READ},
        {.uuid=BLE_UUID16_DECLARE(0x2a4b),.access_cb=access,.arg=(void *)10,.flags=BLE_GATT_CHR_F_READ},
        {.uuid=BLE_UUID16_DECLARE(0x2a4c),.access_cb=access,.arg=(void *)20,.flags=BLE_GATT_CHR_F_WRITE_NO_RSP|BLE_GATT_CHR_F_WRITE_ENC},
        INPUT_CHR(0,0),INPUT_CHR(1,1),INPUT_CHR(2,2),INPUT_CHR(3,5),
        {.uuid=BLE_UUID16_DECLARE(0x2a4d),.access_cb=access,.arg=(void *)6,.flags=BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_READ_ENC|BLE_GATT_CHR_F_WRITE|BLE_GATT_CHR_F_WRITE_NO_RSP|BLE_GATT_CHR_F_WRITE_ENC,.descriptors=REPORT_DSC(3)},
        {.uuid=BLE_UUID16_DECLARE(0x2a4d),.access_cb=access,.arg=(void *)21,.flags=BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_READ_ENC|BLE_GATT_CHR_F_WRITE|BLE_GATT_CHR_F_WRITE_NO_RSP|BLE_GATT_CHR_F_WRITE_ENC,.descriptors=REPORT_DSC(4)}, {0}}},
    {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=BLE_UUID16_DECLARE(0x180a),.characteristics=(struct ble_gatt_chr_def[]){
        {.uuid=BLE_UUID16_DECLARE(0x2a50),.access_cb=access,.arg=(void *)12,.flags=BLE_GATT_CHR_F_READ},{0}}}, {0}
};
static void tick(struct ble_npl_event *event)
{
    (void)event; uint64_t now=esp_timer_get_time(); command_t command;
    portENTER_CRITICAL(&mux); bool handoff=disconnect_requested; disconnect_requested=false; portEXIT_CRITICAL(&mux);
    if (handoff) disconnect();
    if (pair_until && now>=pair_until) { pair_until=0; if (!authorized) disconnect(); ble_gap_adv_stop(); }
    /* Durable delete intent is reconciled after power loss; no other slot is touched. */
    for (unsigned i=0;i<3;++i) if (record.slot[i].state==2 && storage_ok) {
        int rc=ble_store_util_delete_peer(&record.slot[i].peer);
        if (!rc || rc==BLE_HS_ENOENT) { memset(&record.slot[i],0,sizeof(slot_t)); persist(); }
        else last_error=rc;
    }
    if (xQueueReceive(commands,&command,0)==pdTRUE) {
        last_error=0;
        if (!storage_ok) last_error=ESP_ERR_INVALID_STATE;
        else if (command.kind==BLE_LINK_DISCONNECT) disconnect();
        else if (command.kind==BLE_SLOT_PAIR && record.slot[command.slot].state) last_error=ESP_ERR_INVALID_STATE;
        else if (command.kind==BLE_SLOT_FORGET) {
            if (record.slot[command.slot].state) {
                if (command.slot==record.selected) { pair_until=0; disconnect(); ble_gap_adv_stop(); }
                record.slot[command.slot].state=2; persist();
            }
        }
        else {
            disconnect(); ble_gap_adv_stop(); pair_until=0;
            if (command.kind==BLE_SLOT_SELECT) { record.selected=command.slot; persist(); }
            else if (command.kind==BLE_SLOT_PAIR) {
                record.selected=command.slot; if (persist()) pair_until=now+60000000;
            }
        }
        last_command=command.token;
    }
    if (!authorized && connection!=BLE_HS_CONN_HANDLE_NONE) authenticate();
    if (!authorized && connection!=BLE_HS_CONN_HANDLE_NONE && !disconnect_pending) {
        if (security_deadline && now>=security_deadline) { last_error=BLE_HS_ETIMEOUT; disconnect(); }
        else if (security_retry_at && now>=security_retry_at) initiate_security();
    }
    tx_t tx;
    if (xQueuePeek(tx_queue,&tx,0)==pdTRUE) {
        unsigned index=report_index(tx.id);
        if (tx.epoch!=epoch || !authorized || !encrypted) xQueueReceive(tx_queue,&tx,0);
        else if (now>tx.deadline) { last_error=BLE_HS_ETIMEOUT; disconnect(); }
        else if (subscribed[index] && mtu>=tx.length+3) {
            struct os_mbuf *om=ble_hs_mbuf_from_flat(tx.data,tx.length);
            if (om) {
                int rc=ble_gatts_notify_custom(connection,in_handle[index],om);
                if (!rc) { memcpy(last_input[index],tx.data,tx.length); xQueueReceive(tx_queue,&tx,0); }
                else if (rc!=BLE_HS_ENOMEM && rc!=BLE_HS_EBUSY) { last_error=rc; disconnect(); }
            }
        }
    }
    advertise(); publish(); ble_npl_callout_reset(&tick_callout,ble_npl_time_ms_to_ticks32(10));
}
static void on_sync(void)
{
    int rc=ble_hs_util_ensure_addr(0); if (!rc) rc=ble_hs_id_infer_auto(0,&own_type);
    if (rc) { last_error=rc; publish(); return; }
    synced=true;
    ble_addr_t peers[3]; int count=0;
    rc=ble_store_util_bonded_peers(peers,&count,3);
    if (rc) { storage_ok=false; last_error=rc; publish(); return; }
    /* Reconcile orphan keys after interrupted pairing without assigning another slot. */
    for (int p=0;p<count;++p) {
        bool mapped=false;
        for (unsigned i=0;i<3;++i) if (record.slot[i].state && same(&record.slot[i].peer,&peers[p])) mapped=true;
        if (!mapped) { rc=ble_store_util_delete_peer(&peers[p]); if (rc) { storage_ok=false; last_error=rc; } }
    }
    bool changed=false;
    for (unsigned i=0;i<3;++i) if (record.slot[i].state==1) {
        bool found=false; for (int p=0;p<count;++p) if (same(&record.slot[i].peer,&peers[p])) found=true;
        if (!found) { memset(&record.slot[i],0,sizeof(slot_t)); changed=true; }
    }
    if (changed) persist();
    if (!record.slot[record.selected].state) pair_until=esp_timer_get_time()+60000000;
    advertise(); publish(); ble_npl_callout_reset(&tick_callout,1);
}
static void on_reset(int reason)
{ last_error=reason; synced=false; connection=BLE_HS_CONN_HANDLE_NONE; authorized=encrypted=disconnect_pending=false; ++epoch; publish(); }
static int store_full(struct ble_store_status_event *event,void *arg)
{ (void)event; (void)arg; return BLE_HS_ESTORE_CAP; } /* No automatic oldest-bond eviction. */
static void host_task(void *arg) { (void)arg; nimble_port_run(); nimble_port_freertos_deinit(); }
esp_err_t codex_ble_init(const uint8_t *report_map,size_t length)
{
    map=report_map; map_size=length;
    commands=xQueueCreate(4,sizeof(command_t)); rx_queue=xQueueCreate(32,sizeof(ble_transport_rx_t)); tx_queue=xQueueCreate(16,sizeof(tx_t));
    if (!commands || !rx_queue || !tx_queue) return ESP_ERR_NO_MEM;
    esp_err_t err=nvs_open("codex_ble",NVS_READWRITE,&storage); if (err!=ESP_OK) return err;
    size_t n=sizeof(record); err=nvs_get_blob(storage,"slots",&record,&n);
    if (err==ESP_ERR_NVS_NOT_FOUND) { memset(&record,0,sizeof(record)); record.magic=0x434d4231; }
    else if (err!=ESP_OK || n!=sizeof(record) || record.magic!=0x434d4231 || record.selected>=3) return ESP_ERR_INVALID_STATE;
    for (unsigned i=0;i<3;++i) {
        if (record.slot[i].state>2 || (record.slot[i].state && record.slot[i].peer.type>3)) return ESP_ERR_INVALID_STATE;
        for (unsigned j=0;j<i;++j) if (record.slot[i].state && record.slot[j].state && same(&record.slot[i].peer,&record.slot[j].peer)) return ESP_ERR_INVALID_STATE;
    }
    storage_ok=true;
    err=nimble_port_init(); if (err!=ESP_OK) return err;
    ble_hs_cfg.sync_cb=on_sync; ble_hs_cfg.reset_cb=on_reset; ble_hs_cfg.store_status_cb=store_full;
    ble_hs_cfg.sm_io_cap=BLE_HS_IO_NO_INPUT_OUTPUT; ble_hs_cfg.sm_bonding=1; ble_hs_cfg.sm_sc=1;
    ble_hs_cfg.sm_our_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
    ble_store_config_init(); ble_svc_gap_init(); ble_svc_gatt_init();
    ble_att_set_preferred_mtu(128);
    int rc=ble_gatts_count_cfg(services); if (!rc) rc=ble_gatts_add_svcs(services);
    if (rc) return ESP_FAIL;
    ble_npl_callout_init(&tick_callout,nimble_port_get_dflt_eventq(),tick,NULL);
    nimble_port_freertos_init(host_task); return ESP_OK;
}
bool ble_transport_command(ble_command_kind_t kind,unsigned slot,uint32_t *token)
{
    ble_transport_status_t state; ble_transport_status(&state);
    if (!commands || slot>=3 || !state.initialized) return false;
    if (++next_command==0) ++next_command;
    command_t command={kind,slot,next_command};
    if (xQueueSend(commands,&command,0)!=pdTRUE) return false;
    if (token) *token=command.token;
    return true;
}
bool ble_transport_send(uint8_t id,const void *data,size_t length)
{
    ble_transport_status_t s; ble_transport_status(&s);
    size_t expected=id==1?8:id==2?2:id==3?1:id==6?63:0;
    bool ready=id==1?s.keyboard_ready:id==2?s.consumer_ready:id==3?s.system_ready:id==6?s.vendor_ready:false;
    if (!ready || !data || !expected || length!=expected || s.mtu<length+3) return false;
    tx_t tx={.epoch=s.epoch,.deadline=esp_timer_get_time()+2000000,.id=id,.length=length};
    memcpy(tx.data,data,length); return xQueueSend(tx_queue,&tx,0)==pdTRUE;
}
bool ble_transport_receive(ble_transport_rx_t *out) { return rx_queue && xQueueReceive(rx_queue,out,0)==pdTRUE; }
void ble_transport_discard_rx(void) { if (rx_queue) xQueueReset(rx_queue); }
void ble_transport_disconnect(void)
{ portENTER_CRITICAL(&mux); disconnect_requested=true; portEXIT_CRITICAL(&mux); }
