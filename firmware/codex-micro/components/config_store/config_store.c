#include "config_store.h"
#include "smart_actions.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define CONFIG_RAM EXT_RAM_BSS_ATTR
#else
#define CONFIG_RAM
#endif

enum { HEADER=20, STORE_BYTES=4+KEYMAP_BYTES+SMART_BYTES };
static const char *banks[]={"keymap_a","keymap_b"};
static nvs_handle_t handle;
static bool writable,busy;
static bool incompatible_schema;
static int current_bank=-1;
static uint32_t generation;
static CONFIG_RAM char current_json[KEYMAP_BYTES+1];
static CONFIG_RAM char current_smart[SMART_BYTES+1];
static CONFIG_RAM keymap_model_t current_model, candidate_model;
static QueueHandle_t commands, completions;
typedef struct { char *json; char *smart; uint32_t generation; int bank; esp_err_t result; } job_t;
static uint32_t crc32(const uint8_t *p,size_t n)
{
    uint32_t c=UINT32_MAX;
    for (size_t i=0;i<n;++i) { c^=p[i]; for (unsigned j=0;j<8;++j) c=(c>>1)^((c&1)?0xEDB88320U:0); }
    return ~c;
}
static void put32(uint8_t *p,uint32_t v) { for (unsigned i=0;i<4;++i) p[i]=v>>(i*8); }
static uint32_t get32(const uint8_t *p)
{ return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static bool compile_text(const char *text,keymap_model_t *model)
{
    if (!text || strlen(text)>KEYMAP_BYTES) return false;
    const char *end=NULL;
    cJSON *root=cJSON_ParseWithOpts(text,&end,true);
    bool ok=root && keymap_compile(root,model);
    cJSON_Delete(root); return ok;
}
static bool read_bank(int bank,char *json,char *smart,uint32_t *gen)
{
    size_t n=0;
    if (nvs_get_blob(handle,banks[bank],NULL,&n)!=ESP_OK || n<HEADER || n>STORE_BYTES+HEADER) return false;
    uint8_t *bytes=malloc(n); if (!bytes) return false;
    bool ok=nvs_get_blob(handle,banks[bank],bytes,&n)==ESP_OK;
    uint32_t schema=ok?get32(bytes+4):0;
    if (ok && get32(bytes)==0x434D4346 && schema!=1 && schema!=2 && schema!=3) incompatible_schema=true;
    ok=ok && get32(bytes)==0x434D4346 && (schema==1 || schema==2 || schema==3) && get32(bytes+12)==n-HEADER &&
        get32(bytes+8)!=0 && get32(bytes+16)==crc32(bytes+HEADER,n-HEADER);
    if (ok) {
        size_t prefix=schema>=2?4:0;
        size_t keylen=schema>=2 && n>=HEADER+4?get32(bytes+HEADER):n-HEADER;
        ok=n>=HEADER+prefix && keylen<=KEYMAP_BYTES && keylen<=n-HEADER-prefix;
        size_t smartlen=ok?n-HEADER-prefix-keylen:0;
        ok=ok && smartlen<=SMART_BYTES && !memchr(bytes+HEADER+prefix,0,n-HEADER-prefix);
        if (ok) {
            memcpy(json,bytes+HEADER+prefix,keylen); json[keylen]=0;
            if (schema==1) strcpy(smart,SMART_DEFAULT);
            else { memcpy(smart,bytes+HEADER+prefix+keylen,smartlen); smart[smartlen]=0; }
            *gen=get32(bytes+8);
        }
    }
    free(bytes); return ok;
}
static esp_err_t write_bank(const job_t *job)
{
    size_t keylen=strlen(job->json),smartlen=strlen(job->smart),n=4+keylen+smartlen;
    uint8_t *bytes=malloc(n+HEADER);
    char *readback=malloc(KEYMAP_BYTES+1),*smart=malloc(SMART_BYTES+1);
    if (!bytes || !readback || !smart) { free(bytes); free(readback); free(smart); return ESP_ERR_NO_MEM; }
    put32(bytes,0x434D4346); put32(bytes+4,3); put32(bytes+8,job->generation); put32(bytes+12,n);
    put32(bytes+HEADER,keylen); memcpy(bytes+HEADER+4,job->json,keylen); memcpy(bytes+HEADER+4+keylen,job->smart,smartlen);
    put32(bytes+16,crc32(bytes+HEADER,n));
    esp_err_t err=nvs_set_blob(handle,banks[job->bank],bytes,n+HEADER);
    if (err==ESP_OK) err=nvs_commit(handle);
    uint32_t gen;
    if (err==ESP_OK && (!read_bank(job->bank,readback,smart,&gen) || gen!=job->generation || strcmp(readback,job->json) || strcmp(smart,job->smart))) err=ESP_FAIL;
    free(bytes); free(readback); free(smart); return err;
}
static void worker(void *arg)
{
    (void)arg;
    job_t job;
    for (;;) if (xQueueReceive(commands,&job,portMAX_DELAY)==pdTRUE) {
        job.result=write_bank(&job);
        xQueueSend(completions,&job,portMAX_DELAY);
    }
}
esp_err_t config_store_init(const char *defaults)
{
    if (!compile_text(defaults,&current_model)) return ESP_ERR_INVALID_ARG;
    strcpy(current_json,defaults); strcpy(current_smart,SMART_DEFAULT);
    esp_err_t err=nvs_flash_init();
    /* Never auto-erase NVS on corruption or a newer schema. Defaults remain usable. */
    if (err==ESP_OK) err=nvs_flash_init_partition("config");
    if (err==ESP_OK) err=nvs_open_from_partition("config","codex_config",NVS_READWRITE,&handle);
    if (err==ESP_OK) {
        char *temp=malloc(KEYMAP_BYTES+1),*smart=malloc(SMART_BYTES+1);
        if (!temp || !smart) { free(temp); free(smart); return ESP_ERR_NO_MEM; }
        for (int i=0;i<2;++i) {
            uint32_t gen;
            if (read_bank(i,temp,smart,&gen) && gen>generation && compile_text(temp,&candidate_model) && smart_actions_validate(smart) && smart_actions_references(&candidate_model,smart)) {
                strcpy(current_json,temp); strcpy(current_smart,smart); current_model=candidate_model; generation=gen; current_bank=i;
            }
        }
        if (!generation && !incompatible_schema) {
            nvs_handle_t new_handle=handle,legacy;
            if (nvs_open("codex_config",NVS_READONLY,&legacy)==ESP_OK) {
                handle=legacy;
                for (int i=0;i<2;++i) {
                    uint32_t gen;
                    if (read_bank(i,temp,smart,&gen) && gen>generation && compile_text(temp,&candidate_model) &&
                        smart_actions_validate(smart) && smart_actions_references(&candidate_model,smart)) {
                        strcpy(current_json,temp); strcpy(current_smart,smart); current_model=candidate_model; generation=gen;
                    }
                }
                nvs_close(legacy); handle=new_handle;
                /* Lazy migration: first user commit writes new bank, never erases legacy. */
                current_bank=-1;
            }
        }
        free(temp); free(smart); writable=!incompatible_schema;
    }
    commands=xQueueCreate(1,sizeof(job_t)); completions=xQueueCreate(1,sizeof(job_t));
    if (!commands || !completions || xTaskCreate(worker,"config_store",4096,NULL,2,NULL)!=pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}
const char *config_store_json(void) { return current_json; }
const char *config_store_smart(void) { return current_smart; }
const keymap_model_t *config_store_model(void) { return &current_model; }
uint32_t config_store_generation(void) { return generation; }
bool config_store_writable(void) { return writable; }
bool config_store_busy(void) { return busy; }
esp_err_t config_store_submit(const char *json) { return config_store_submit_files(json,current_smart); }
esp_err_t config_store_submit_files(const char *json,const char *smart)
{
    if (!writable || busy || generation==UINT32_MAX) return ESP_ERR_INVALID_STATE;
    if (!compile_text(json,&candidate_model) || !smart_actions_validate(smart) || !smart_actions_references(&candidate_model,smart)) return ESP_ERR_INVALID_ARG;
    job_t job={.json=strdup(json),.smart=strdup(smart),.generation=generation+1,.bank=current_bank==0?1:0};
    if (!job.json || !job.smart) { free(job.json); free(job.smart); return ESP_ERR_NO_MEM; }
    if (xQueueSend(commands,&job,0)!=pdTRUE) { free(job.json); free(job.smart); return ESP_FAIL; }
    busy=true; return ESP_OK;
}
bool config_store_poll(esp_err_t *result)
{
    job_t job;
    if (!busy || xQueueReceive(completions,&job,0)!=pdTRUE) return false;
    if (job.result==ESP_OK) {
        strcpy(current_json,job.json); strcpy(current_smart,job.smart); current_model=candidate_model;
        generation=job.generation; current_bank=job.bank;
    }
    free(job.json); free(job.smart); busy=false; *result=job.result; return true;
}
