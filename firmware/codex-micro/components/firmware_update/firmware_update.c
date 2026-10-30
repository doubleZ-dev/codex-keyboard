#include "firmware_update.h"
#include "update_engine.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "mbedtls/sha256.h"
#include "mbedtls/base64.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

typedef enum { JOB_BEGIN, JOB_WRITE, JOB_FINISH, JOB_ACTIVATE, JOB_ABORT, JOB_CONFIRM, JOB_REBOOT } job_kind_t;
typedef struct {
    job_kind_t kind;
    uint32_t epoch, size, offset;
    size_t length;
    uint8_t digest[32], data[UPDATE_CHUNK_MAX];
} update_job_t;
typedef struct { bool ok; uint32_t epoch; } completion_t;
typedef struct {
    update_state_t state;
    uint32_t size, offset, capacity, running_address;
    bool pending_verify, available;
    int last_error;
} status_t;
static QueueHandle_t jobs, completions;
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
static uint32_t current_epoch;
static status_t published;
/* Protocol owner fields. */
static cJSON *pending_response;
/* Worker owns all OTA handles, hashing, engine and deadline fields. */
static update_engine_t engine;
static const esp_partition_t *destination;
static esp_ota_handle_t handle;
static bool handle_open, hash_open;
static mbedtls_sha256_context sha;
static int backend_error;
static uint64_t boot_deadline;
/* Fixed offset 24+8+256=288, per IDF custom application descriptor contract.
 * Board H1/N16R8, partition layout 3, updater ABI 1, configuration schema 3.
 * Compatibility metadata is not a cryptographic signature. */
const uint32_t codex_image_contract[8] __attribute__((used,section(".rodata_custom_desc"))) = {
    0x434d4831, 20261023, 0x00160008, 3, 1, 3, 0, 0
};

static uint32_t session(void)
{ portENTER_CRITICAL(&mux); uint32_t e=current_epoch; portEXIT_CRITICAL(&mux); return e; }
static status_t snapshot(void)
{ portENTER_CRITICAL(&mux); status_t s=published; portEXIT_CRITICAL(&mux); return s; }
static void publish(void)
{
    portENTER_CRITICAL(&mux);
    published.state=engine.state; published.offset=engine.offset; published.size=engine.size;
    published.pending_verify=boot_deadline!=0; published.last_error=backend_error;
    portEXIT_CRITICAL(&mux);
}
static void backend_abort(void *ctx)
{
    (void)ctx;
    if (handle_open) { esp_ota_abort(handle); handle_open=false; }
    if (hash_open) { mbedtls_sha256_free(&sha); hash_open=false; }
}
static bool backend_begin(uint32_t size,void *ctx)
{
    (void)ctx; (void)size;
    if (!destination || boot_deadline) { backend_error=ESP_ERR_INVALID_STATE; return false; }
    /* Incremental sector erase keeps a whole 3MB erase out of the begin request. */
    backend_error=esp_ota_begin(destination,OTA_WITH_SEQUENTIAL_WRITES,&handle);
    if (backend_error!=ESP_OK) return false;
    handle_open=true; mbedtls_sha256_init(&sha); hash_open=true;
    if (mbedtls_sha256_starts(&sha,0)) { backend_abort(NULL); backend_error=ESP_FAIL; return false; }
    return true;
}
static bool backend_write(const uint8_t *data,size_t size,void *ctx)
{
    (void)ctx; backend_error=esp_ota_write(handle,data,size);
    if (backend_error!=ESP_OK) return false;
    if (mbedtls_sha256_update(&sha,data,size)) { backend_error=ESP_FAIL; return false; }
    return true;
}
static bool backend_finish(const uint8_t expected[32],void *ctx)
{
    (void)ctx; uint8_t actual[32];
    if (mbedtls_sha256_finish(&sha,actual) || memcmp(actual,expected,32)) {
        backend_error=ESP_ERR_INVALID_CRC; backend_abort(NULL); return false;
    }
    mbedtls_sha256_free(&sha); hash_open=false;
    backend_error=esp_ota_end(handle); handle_open=false;
    if (backend_error!=ESP_OK) return false;
    esp_app_desc_t desc;
    backend_error=esp_ota_get_partition_description(destination,&desc);
    if (backend_error!=ESP_OK) return false;
    if (memcmp(desc.project_name,esp_app_get_description()->project_name,sizeof(desc.project_name))) {
        backend_error=ESP_ERR_INVALID_VERSION; return false;
    }
    uint32_t contract[8];
    backend_error=esp_partition_read(destination,sizeof(esp_image_header_t)+sizeof(esp_image_segment_header_t)+sizeof(esp_app_desc_t),contract,sizeof(contract));
    if (backend_error!=ESP_OK) return false;
    if (memcmp(contract,codex_image_contract,sizeof(contract))) {
        backend_error=ESP_ERR_INVALID_VERSION; return false;
    }
    return true;
}
static bool backend_activate(void *ctx)
{ (void)ctx; backend_error=esp_ota_set_boot_partition(destination); return backend_error==ESP_OK; }

static void worker(void *ctx)
{
    (void)ctx; update_job_t job;
    uint32_t owner=session(); uint64_t touched=0;
    for (;;) {
        bool received=xQueueReceive(jobs,&job,pdMS_TO_TICKS(250))==pdTRUE;
        uint64_t now=esp_timer_get_time();
        if (boot_deadline && now>=boot_deadline) {
            /* Only OTA trial boots have a deadline. Bootloader performs rollback. */
            esp_restart();
        }
        uint32_t epoch=session();
        if (epoch!=owner || (touched && now-touched>60000000)) {
            update_engine_abort(&engine); owner=epoch; touched=0; publish();
        }
        if (!received) continue;
        bool ok=false; backend_error=ESP_OK;
        if (job.epoch==epoch) {
            touched=now;
            switch (job.kind) {
            case JOB_BEGIN: ok=update_engine_begin(&engine,job.size,published.capacity,job.digest); break;
            case JOB_WRITE: ok=update_engine_write(&engine,job.offset,job.data,job.length); break;
            case JOB_FINISH: ok=update_engine_finish(&engine); break;
            case JOB_ACTIVATE: ok=update_engine_activate(&engine); break;
            case JOB_ABORT: ok=update_engine_abort(&engine); break;
            case JOB_CONFIRM:
                backend_error=esp_ota_mark_app_valid_cancel_rollback();
                ok=backend_error==ESP_OK; if (ok) boot_deadline=0;
                break;
            case JOB_REBOOT:
                ok=engine.state==UPDATE_ACTIVATED || engine.state==UPDATE_BOOT_UNCERTAIN;
                break;
            }
        }
        if (!ok && backend_error==ESP_OK) backend_error=ESP_ERR_INVALID_STATE;
        publish(); completion_t done={ok,job.epoch};
        /* Single outstanding request; queue remains owned until protocol polls it. */
        xQueueSend(completions,&done,portMAX_DELAY);
        if (ok && job.kind==JOB_REBOOT) {
            vTaskDelay(pdMS_TO_TICKS(1500)); esp_restart();
        }
    }
}

esp_err_t firmware_update_init(void)
{
    const esp_partition_t *running=esp_ota_get_running_partition();
    destination=esp_ota_get_next_update_partition(NULL);
    published.available=destination && running && destination->address!=running->address;
    published.capacity=published.available?destination->size:0;
    published.running_address=running?running->address:0;
    esp_ota_img_states_t state;
    if (running && esp_ota_get_state_partition(running,&state)==ESP_OK && state==ESP_OTA_IMG_PENDING_VERIFY)
        boot_deadline=esp_timer_get_time()+120000000;
    update_engine_init(&engine,(update_backend_t){backend_begin,backend_write,backend_finish,backend_activate,backend_abort,NULL});
    publish();
    jobs=xQueueCreate(1,sizeof(update_job_t)); completions=xQueueCreate(1,sizeof(completion_t));
    if (!jobs || !completions) return ESP_ERR_NO_MEM;
    return xTaskCreate(worker,"fw_update",6144,NULL,2,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;
}
void firmware_update_session(uint32_t epoch)
{ portENTER_CRITICAL(&mux); current_epoch=epoch; portEXIT_CRITICAL(&mux); }
bool firmware_update_busy(void)
{
    status_t s=snapshot();
    return pending_response || s.state==UPDATE_WRITING || s.state==UPDATE_READY || s.state==UPDATE_ACTIVATED || s.state==UPDATE_BOOT_UNCERTAIN;
}
static void error(cJSON *r,int code,const char *text)
{ cJSON *e=cJSON_CreateObject(); cJSON_AddNumberToObject(e,"code",code); cJSON_AddStringToObject(e,"message",text); cJSON_AddItemToObject(r,"error",e); }
static void result(cJSON *r)
{
    status_t s=snapshot(); const char *states[]={"idle","writing","ready","activated","failed","boot_uncertain"};
    cJSON *o=cJSON_CreateObject();
    cJSON_AddStringToObject(o,"state",states[s.state]);
    cJSON_AddBoolToObject(o,"available",s.available);
    cJSON_AddBoolToObject(o,"busy",pending_response!=NULL);
    cJSON_AddBoolToObject(o,"pending_verify",s.pending_verify);
    cJSON_AddNumberToObject(o,"offset",s.offset); cJSON_AddNumberToObject(o,"size",s.size);
    cJSON_AddNumberToObject(o,"capacity",s.capacity); cJSON_AddNumberToObject(o,"chunk_max",UPDATE_CHUNK_MAX);
    cJSON_AddNumberToObject(o,"running_address",s.running_address);
    cJSON_AddNumberToObject(o,"last_error",s.last_error);
    cJSON_AddStringToObject(o,"version",esp_app_get_description()->version);
    cJSON *contract=cJSON_AddArrayToObject(o,"image_contract");
    for (unsigned i=0;i<8;++i) cJSON_AddItemToArray(contract,cJSON_CreateNumber(codex_image_contract[i]));
    char elf_hash[65];
    for (unsigned i=0;i<32;++i) snprintf(elf_hash+i*2,3,"%02x",esp_app_get_description()->app_elf_sha256[i]);
    cJSON_AddStringToObject(o,"running_elf_sha256",elf_hash);
    cJSON_AddItemToObject(r,"result",o);
}
static bool number(const cJSON *p,const char *key,uint32_t *out)
{
    const cJSON *n=cJSON_GetObjectItemCaseSensitive(p,key);
    if (!cJSON_IsNumber(n) || !isfinite(n->valuedouble) || n->valuedouble<0 || n->valuedouble>UINT32_MAX || floor(n->valuedouble)!=n->valuedouble) return false;
    *out=(uint32_t)n->valuedouble; return true;
}
static int nibble(char c)
{ if (c>='0' && c<='9') return c-'0'; if (c>='a' && c<='f') return c-'a'+10; return -1; }
static bool fields(const cJSON *p,const char *a,const char *b)
{
    if (!a) return !p || (cJSON_IsObject(p) && !p->child);
    if (!cJSON_IsObject(p)) return false;
    unsigned count=0;
    for (const cJSON *v=p->child;v;v=v->next) {
        if (!strcmp(v->string,a)) { if (count&1) return false; count|=1; }
        else if (b && !strcmp(v->string,b)) { if (count&2) return false; count|=2; }
        else return false;
    }
    return count==(b?3:1);
}
bool firmware_update_request(const char *method,const cJSON *p,cJSON *r,uint32_t epoch)
{
    if (!strcmp(method,"upgrade.status")) { result(r); return false; }
    if (pending_response) { error(r,-32000,"Firmware worker busy; query upgrade.status"); return false; }
    update_job_t job={.epoch=epoch}; bool valid=true;
    if (!strcmp(method,"upgrade.begin")) {
        job.kind=JOB_BEGIN;
        const cJSON *hash=cJSON_GetObjectItemCaseSensitive(p,"sha256");
        valid=fields(p,"size","sha256") && number(p,"size",&job.size) && cJSON_IsString(hash) && strlen(hash->valuestring)==64;
        for (unsigned i=0;valid && i<32;++i) {
            int a=nibble(hash->valuestring[i*2]),b=nibble(hash->valuestring[i*2+1]);
            if (a<0 || b<0) valid=false; else job.digest[i]=(uint8_t)(a*16+b);
        }
    } else if (!strcmp(method,"upgrade.write")) {
        job.kind=JOB_WRITE;
        const cJSON *data=cJSON_GetObjectItemCaseSensitive(p,"data");
        valid=fields(p,"offset","data") && number(p,"offset",&job.offset) && cJSON_IsString(data) &&
            strlen(data->valuestring)<=1368 && !mbedtls_base64_decode(job.data,sizeof(job.data),&job.length,
            (const unsigned char *)data->valuestring,strlen(data->valuestring)) && job.length>0;
    } else {
        if (!strcmp(method,"upgrade.finish")) job.kind=JOB_FINISH;
        else if (!strcmp(method,"upgrade.activate")) job.kind=JOB_ACTIVATE;
        else if (!strcmp(method,"upgrade.abort")) job.kind=JOB_ABORT;
        else if (!strcmp(method,"upgrade.confirm")) job.kind=JOB_CONFIRM;
        else if (!strcmp(method,"upgrade.reboot")) job.kind=JOB_REBOOT;
        else { error(r,-32601,"Unknown project upgrade method"); return false; }
        valid=fields(p,NULL,NULL);
    }
    if (!valid) { error(r,-32602,"Invalid upgrade parameters"); return false; }
    if (xQueueSend(jobs,&job,0)!=pdTRUE) { error(r,-32000,"Firmware queue busy"); return false; }
    pending_response=r; return true;
}
cJSON *firmware_update_poll(uint32_t epoch)
{
    completion_t done;
    if (!pending_response || xQueueReceive(completions,&done,0)!=pdTRUE) return NULL;
    cJSON *r=pending_response; pending_response=NULL;
    if (done.epoch!=epoch) { cJSON_Delete(r); return NULL; }
    if (done.ok) result(r); else error(r,-32001,"Firmware operation rejected or failed; query upgrade.status");
    return r;
}
