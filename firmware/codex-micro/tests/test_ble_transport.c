#undef NDEBUG
#define CODEX_BLE_HOST_TEST 1
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../components/ble_transport/ble_transport.c"
struct mock_hs_config ble_hs_cfg;
struct mock_queue { unsigned depth,head,count;size_t size;uint8_t *bytes; };
static uint64_t clock_us;
static bool advertising,commit_failure,notify_busy;
static unsigned terminated,notifications;
static int security_result;
static uint8_t durable_record[128],staged_record[128];
static size_t durable_length,staged_size;
static ble_addr_t bonds[3];static int bond_count;
static struct ble_gap_conn_desc peer;
QueueHandle_t xQueueCreate(unsigned depth,size_t size)
{ QueueHandle_t q=calloc(1,sizeof(*q));q->depth=depth;q->size=size;q->bytes=calloc(depth,size);return q; }
int xQueueSend(QueueHandle_t q,const void *p,uint32_t wait)
{ (void)wait;if(q->count==q->depth)return 0;memcpy(q->bytes+((q->head+q->count)%q->depth)*q->size,p,q->size);++q->count;return 1; }
int xQueuePeek(QueueHandle_t q,void *p,uint32_t wait)
{ (void)wait;if(!q->count)return 0;memcpy(p,q->bytes+q->head*q->size,q->size);return 1; }
int xQueueReceive(QueueHandle_t q,void *p,uint32_t wait)
{ if(!xQueuePeek(q,p,wait))return 0;q->head=(q->head+1)%q->depth;--q->count;return 1; }
int xQueueReset(QueueHandle_t q) { q->count=q->head=0;return 1; }
int64_t esp_timer_get_time(void) { return clock_us; }
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *h) { (void)name;(void)mode;*h=1;return 0; }
esp_err_t nvs_get_blob(nvs_handle_t h,const char *name,void *p,size_t *n)
{ (void)h;(void)name;if(!durable_length)return ESP_ERR_NVS_NOT_FOUND;if(*n<durable_length)return ESP_FAIL;memcpy(p,durable_record,durable_length);*n=durable_length;return 0; }
esp_err_t nvs_set_blob(nvs_handle_t h,const char *name,const void *p,size_t n)
{ (void)h;(void)name;assert(n<=sizeof(staged_record));memcpy(staged_record,p,n);staged_size=n;return 0; }
esp_err_t nvs_commit(nvs_handle_t h)
{ (void)h;if(commit_failure)return ESP_FAIL;memcpy(durable_record,staged_record,staged_size);durable_length=staged_size;return 0; }
int ble_gap_terminate(uint16_t h,unsigned reason) { (void)h;(void)reason;++terminated;return 0; }
bool ble_gap_adv_active(void) { return advertising; }
int ble_gap_adv_stop(void) { advertising=false;return 0; }
int ble_svc_gap_device_name_set(const char *name) { assert(strstr(name,"Codex Micro #"));return 0; }
int ble_gap_adv_set_fields(const struct ble_hs_adv_fields *p) { (void)p;return 0; }
int ble_gap_adv_rsp_set_fields(const struct ble_hs_adv_fields *p) { (void)p;return 0; }
int ble_gap_adv_start(uint8_t own,const void *a,int32_t duration,const struct ble_gap_adv_params *p,int (*cb)(struct ble_gap_event *,void *),void *ctx)
{ (void)own;(void)a;(void)duration;(void)p;(void)cb;(void)ctx;advertising=true;return 0; }
int ble_gap_conn_find(uint16_t h,struct ble_gap_conn_desc *out) { (void)h;*out=peer;return 0; }
int ble_gap_security_initiate(uint16_t h) { (void)h;return security_result; }
int ble_gattc_exchange_mtu(uint16_t h,void *cb,void *ctx) { (void)h;(void)cb;(void)ctx;return 0; }
int ble_store_util_bonded_peers(ble_addr_t *out,int *count,int limit)
{ assert(bond_count<=limit);memcpy(out,bonds,bond_count*sizeof(*out));*count=bond_count;return 0; }
int ble_store_util_delete_peer(const ble_addr_t *address)
{ for(int i=0;i<bond_count;++i)if(same(address,&bonds[i])){bonds[i]=bonds[--bond_count];return 0;}return BLE_HS_ENOENT; }
int os_mbuf_append(struct os_mbuf *om,const void *p,size_t n)
{ if(n>sizeof(om->bytes)-om->length)return -1;memcpy(om->bytes+om->length,p,n);om->length+=n;return 0; }
int ble_hs_mbuf_to_flat(struct os_mbuf *om,void *p,size_t n,uint16_t *actual)
{ if(n<om->length)return -1;memcpy(p,om->bytes,om->length);if(actual)*actual=om->length;return 0; }
struct os_mbuf *ble_hs_mbuf_from_flat(const void *p,size_t n)
{ struct os_mbuf *om=calloc(1,sizeof(*om));assert(!os_mbuf_append(om,p,n));return om; }
int ble_gatts_notify_custom(uint16_t h,uint16_t attr,struct os_mbuf *om)
{ (void)h;assert(attr);free(om);if(notify_busy)return BLE_HS_ENOMEM;++notifications;return 0; }
int ble_hs_util_ensure_addr(int a) { (void)a;return 0; }
int ble_hs_id_infer_auto(int a,uint8_t *out) { (void)a;*out=0;return 0; }
void ble_npl_callout_reset(struct ble_npl_callout *c,uint32_t n) { (void)c;(void)n; }
uint32_t ble_npl_time_ms_to_ticks32(uint32_t n) { return n; }
void ble_npl_callout_init(struct ble_npl_callout *c,void *q,void (*cb)(struct ble_npl_event *),void *ctx) { (void)c;(void)q;(void)cb;(void)ctx; }
void *nimble_port_get_dflt_eventq(void) { return NULL; }
int nimble_port_init(void) { return 0; }
void nimble_port_run(void) {}
void nimble_port_freertos_deinit(void) {}
void nimble_port_freertos_init(void (*fn)(void *)) { (void)fn; }
void ble_svc_gap_init(void) {}
void ble_svc_gatt_init(void) {}
void ble_svc_gatt_changed(unsigned short start,unsigned short end) { assert(start==1 && end==0xffff); }
void ble_store_config_init(void) {}
int ble_att_set_preferred_mtu(unsigned n) { assert(n==128);return 0; }
int ble_gatts_count_cfg(const struct ble_gatt_svc_def *p) { (void)p;return 0; }
int ble_gatts_add_svcs(const struct ble_gatt_svc_def *p) { (void)p;for(unsigned i=0;i<4;++i)in_handle[i]=100+i;return 0; }
static void gap(int type)
{ struct ble_gap_event e={.type=type};e.connect.conn_handle=7;assert(!gap_event(&e,NULL)); }
static void subscribe(unsigned index,bool enabled)
{ struct ble_gap_event e={.type=BLE_GAP_EVENT_SUBSCRIBE};e.subscribe.attr_handle=in_handle[index];e.subscribe.cur_notify=enabled;assert(!gap_event(&e,NULL)); }
static void run_command(ble_command_kind_t kind,unsigned slot)
{ uint32_t token;assert(ble_transport_command(kind,slot,&token));tick(NULL);assert(view.last_command==token); }
static void connect_peer(uint8_t id)
{ memset(&peer,0,sizeof(peer));peer.peer_id_addr.val[0]=id;advertising=false;gap(BLE_GAP_EVENT_CONNECT); }
static void pair_peer(uint8_t id)
{
    connect_peer(id);assert(!view.ready);
    peer.sec_state.encrypted=peer.sec_state.bonded=true;
    gap(BLE_GAP_EVENT_ENC_CHANGE);assert(!authorized); /* Bond is not in persistent SDK table yet. */
    bonds[bond_count++]=peer.peer_id_addr;tick(NULL);assert(authorized);
    subscribe(0,true);assert(view.ready && view.keyboard_ready && !view.consumer_ready);subscribe(1,true);assert(view.ready);
    subscribe(2,true);assert(!view.vendor_ready);
    struct ble_gap_event e={.type=BLE_GAP_EVENT_MTU};e.mtu.value=128;gap_event(&e,NULL);assert(view.vendor_ready);
}
int main(void)
{
    static const uint8_t descriptor[]={0};assert(codex_ble_init(descriptor,sizeof(descriptor))==ESP_OK);on_sync();
    for (unsigned i=0;i<2;++i) {
        const struct ble_gatt_chr_def *chr=&services[0].characteristics[7+i];
        assert((uintptr_t)chr->arg==(i?21:6));
        unsigned required=BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_READ_ENC|BLE_GATT_CHR_F_WRITE|BLE_GATT_CHR_F_WRITE_ENC;
        assert((chr->flags & required)==required);
    }
    assert(view.pairing && view.slot==0 && !view.paired_mask && advertising);
    uint8_t report[63]={2};assert(!ble_transport_send(6,report,63));
    security_result=BLE_HS_EALREADY;pair_peer(10);security_result=0;assert(view.paired_mask==1 && durable_length==sizeof(record));
    /* Windows may subscribe Vendor without keyboard/consumer on cold boot.
     * Exercise real GAP callbacks, authenticated Output RX and notification TX. */
    subscribe(0,false);subscribe(1,false);
    assert(view.ready && view.vendor_ready && !view.keyboard_ready && !view.consumer_ready && view.subscription_mask==4);
    assert(!ble_transport_send(1,report,8) && !ble_transport_send(2,report,2));
    assert(ble_transport_send(6,report,63));tick(NULL);assert(notifications==1);notifications=0;
    struct os_mbuf isolated={.length=63};isolated.bytes[0]=2;
    struct ble_gatt_access_ctxt isolated_ctxt={.op=BLE_GATT_ACCESS_OP_WRITE_CHR,.om=&isolated};
    ble_transport_rx_t isolated_rx;
    assert(!access(7,0,&isolated_ctxt,(void *)6) && ble_transport_receive(&isolated_rx));
    struct ble_gap_event small_mtu={.type=BLE_GAP_EVENT_MTU};small_mtu.mtu.value=23;gap_event(&small_mtu,NULL);
    assert(!view.ready && !view.vendor_ready && !ble_transport_send(6,report,63));
    subscribe(0,true);assert(view.ready && view.keyboard_ready && !view.consumer_ready);
    assert(ble_transport_send(1,report,8));tick(NULL);assert(notifications==1);notifications=0;
    small_mtu.mtu.value=128;gap_event(&small_mtu,NULL);
    encrypted=false;publish();assert(!view.ready && !ble_transport_send(6,report,63));
    encrypted=true;publish();subscribe(1,true);
    assert(!ble_transport_send(3,report,1));subscribe(3,true);assert(view.system_ready);
    assert(ble_transport_send(3,report,1));tick(NULL);assert(notifications==1);notifications=0;
    uint32_t first_epoch=view.epoch;assert(ble_transport_send(6,report,63));notify_busy=true;tick(NULL);assert(tx_queue->count==1 && !notifications);
    notify_busy=false;tick(NULL);assert(!tx_queue->count && notifications==1);
    assert(ble_transport_send(6,report,63));subscribe(2,false);assert(view.epoch!=first_epoch && !tx_queue->count && !view.vendor_ready);subscribe(2,true);
    struct os_mbuf om={.length=64};om.bytes[0]=6;om.bytes[1]=2;
    struct ble_gatt_access_ctxt ctxt={.op=BLE_GATT_ACCESS_OP_WRITE_CHR,.om=&om};
    assert(access(7,0,&ctxt,(void *)6)==0);ble_transport_rx_t received;assert(ble_transport_receive(&received) && received.length==64 && received.bytes[0]==6);
    struct os_mbuf readback={0};
    struct ble_gatt_access_ctxt read_ctxt={.op=BLE_GATT_ACCESS_OP_READ_CHR,.om=&readback};
    assert(access(7,0,&read_ctxt,(void *)6)==0 && readback.length==63 && readback.bytes[0]==2);
    uint32_t writes=output_writes;
    om.bytes[0]=5;assert(access(7,0,&ctxt,(void *)6)==BLE_ATT_ERR_VALUE_NOT_ALLOWED);
    assert(output_writes==writes && last_output[0]==2);
    om.length=63;memset(om.bytes,0,63);om.bytes[0]=2;om.bytes[1]=7;
    assert(!access(7,0,&ctxt,(void *)6));assert(ble_transport_receive(&received) && received.length==63);
    readback.length=0;assert(!access(7,0,&read_ctxt,(void *)6));assert(readback.length==63 && readback.bytes[1]==7);
    encrypted=false;readback.length=0;
    assert(access(7,0,&read_ctxt,(void *)6)==BLE_ATT_ERR_INSUFFICIENT_AUTHEN && !readback.length);
    encrypted=true;om.length=1;om.bytes[0]=3;assert(!access(7,0,&ctxt,(void *)21));
    readback.length=0;assert(!access(7,0,&read_ctxt,(void *)21) && readback.length==1 && readback.bytes[0]==3);
    unsigned before=terminated;run_command(BLE_SLOT_PAIR,0);assert(view.error && terminated==before); /* Refused pair doesn't disconnect occupied slot. */
    run_command(BLE_SLOT_PAIR,1);assert(record.selected==1 && terminated>before);gap(BLE_GAP_EVENT_DISCONNECT);
    assert(last_output[0]==0 && keyboard_leds==0); /* No value from previous Host. */
    connect_peer(10);assert(!authorized && terminated>before+1);gap(BLE_GAP_EVENT_DISCONNECT); /* Other slot cannot steal this slot. */
    pair_peer(20);assert(view.paired_mask==3);
    run_command(BLE_SLOT_PAIR,2);gap(BLE_GAP_EVENT_DISCONNECT);pair_peer(30);assert(view.paired_mask==7 && bond_count==3);
    before=terminated;run_command(BLE_SLOT_FORGET,0);assert(terminated==before && record.slot[0].state==2);
    /* Power loss after delete intent: durable tombstone is replayed on startup. */
    memcpy(&record,durable_record,sizeof(record));on_sync();tick(NULL);assert(record.slot[0].state==0 && bond_count==2 && view.paired_mask==6);
    run_command(BLE_SLOT_PAIR,0);gap(BLE_GAP_EVENT_DISCONNECT);pair_peer(40);assert(bond_count==3 && view.paired_mask==7);
    assert(ble_transport_send(6,report,63));notify_busy=true;clock_us+=2000001;tick(NULL);assert(!authorized && view.error==BLE_HS_ETIMEOUT && !tx_queue->count);
    gap(BLE_GAP_EVENT_DISCONNECT);notify_busy=false;
    before=terminated; security_result=BLE_HS_ENOMEM; connect_peer(40);
    assert(terminated==before && security_attempts==1 && security_retry_at);
    clock_us+=250000;tick(NULL);assert(security_attempts==2 && terminated==before);
    security_result=0;clock_us+=250000;tick(NULL);assert(security_attempts==3 && !security_retry_at);
    peer.sec_state.encrypted=peer.sec_state.bonded=true;gap(BLE_GAP_EVENT_ENC_CHANGE);assert(authorized);
    for(unsigned i=0;i<4;++i)assert(ble_transport_command(BLE_SLOT_PAIR,0,NULL));
    assert(!ble_transport_command(BLE_SLOT_PAIR,0,NULL));
    before=terminated;ble_transport_disconnect();tick(NULL);assert(!authorized && terminated>before && !disconnect_requested);
    xQueueReset(commands);gap(BLE_GAP_EVENT_DISCONNECT);
    before=terminated;security_result=BLE_HS_ENOMEM;connect_peer(40);
    for(unsigned i=0;i<4;++i){clock_us+=250000;tick(NULL);}
    assert(security_attempts==5 && terminated==before+1 && view.error==BLE_HS_ENOMEM);
    gap(BLE_GAP_EVENT_DISCONNECT);security_result=0;
    commit_failure=true;run_command(BLE_SLOT_SELECT,1);assert(view.error==ESP_FAIL && !storage_ok);
    /* Key storage isn't evicted just because the bond table is full. */
    assert(store_full(NULL,NULL)==BLE_HS_ESTORE_CAP && bond_count==3);
    puts("BLE transport: real callbacks/queues, three peers, encryption/subscription gates, framing, congestion, deletion replay and storage failure passed");
}
