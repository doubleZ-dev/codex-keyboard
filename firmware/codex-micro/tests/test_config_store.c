/* Run the actual store with fault-injecting NVS and a deterministic RTOS shim.
 * Including implementation lets this test simulate reboot (reset volatile state).
 * No duplicate storage algorithm is used by the assertions. */
#undef NDEBUG
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include "../components/config_store/config_store.c"
#include "../main/keymap_default.h"
struct mock_queue { size_t size; bool full; unsigned char value[64]; };
static void (*work)(void *);
static jmp_buf scheduler;
static bool commit_fail,init_fail;
static uint8_t durable[2][STORE_BYTES+HEADER],pending[STORE_BYTES+HEADER];
static size_t durable_size[2],pending_size;
static uint8_t legacy_bytes[2][STORE_BYTES+HEADER];
static size_t legacy_size[2];
static int pending_bank;
QueueHandle_t xQueueCreate(unsigned depth,size_t size)
{ assert(depth==1 && size<=64); QueueHandle_t q=calloc(1,sizeof(*q)); q->size=size; return q; }
int xQueueSend(QueueHandle_t q,const void *p,uint32_t timeout)
{ (void)timeout; if (q->full) return 0; memcpy(q->value,p,q->size); q->full=true; return 1; }
int xQueueReceive(QueueHandle_t q,void *p,uint32_t timeout)
{ if (!q->full) { if (timeout==portMAX_DELAY) longjmp(scheduler,1); return 0; }
  memcpy(p,q->value,q->size); q->full=false; return 1; }
int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle_out)
{ (void)name;(void)stack;(void)arg;(void)priority;(void)handle_out; work=fn;return 1; }
esp_err_t nvs_flash_init(void) { return init_fail?ESP_FAIL:ESP_OK; }
esp_err_t nvs_flash_init_partition(const char *p) { (void)p;return nvs_flash_init(); }
esp_err_t nvs_open_from_partition(const char *p,const char *n,int m,nvs_handle_t *h) { (void)p;(void)n;(void)m;*h=1;return ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; }
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *h)
{ (void)name;(void)mode;*h=2;return ESP_OK; }
esp_err_t nvs_get_blob(nvs_handle_t h,const char *name,void *p,size_t *n)
{
    unsigned b=!strcmp(name,"keymap_b");
    size_t size=h==2?legacy_size[b]:durable_size[b];
    const uint8_t *data=h==2?legacy_bytes[b]:durable[b];
    if (!size) return ESP_FAIL;
    if (!p) { *n=size; return ESP_OK; }
    if (*n<size) return ESP_FAIL;
    memcpy(p,data,size); *n=size;return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *name,const void *p,size_t n)
{ assert(h==1);pending_bank=!strcmp(name,"keymap_b"); memcpy(pending,p,n);pending_size=n;return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t h)
{ (void)h;if(commit_fail)return ESP_FAIL; memcpy(durable[pending_bank],pending,pending_size);
  durable_size[pending_bank]=pending_size;return ESP_OK; }
static void run_worker(void) { if (!setjmp(scheduler)) work(NULL); }
static void reboot(void)
{
    free(commands); free(completions); commands=completions=NULL;
    writable=busy=incompatible_schema=false;generation=0;current_bank=-1;
    assert(config_store_init(KEYMAP_JSON)==ESP_OK);
}
int main(void)
{
    assert(config_store_init(KEYMAP_JSON)==ESP_OK && config_store_generation()==0);
    cJSON *json=cJSON_Parse(KEYMAP_JSON);
    cJSON *layers=cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(json,"profiles"),0),"layers");
    cJSON *matrix=cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetArrayItem(layers,0),"layout"),"keymap");
    cJSON_ReplaceItemInArray(cJSON_GetArrayItem(matrix,0),0,cJSON_CreateString("KC_A"));
    char *changed=cJSON_PrintUnformatted(json);cJSON_Delete(json);
    assert(config_store_submit("{}")!=ESP_OK && !config_store_busy());
    assert(config_store_submit(changed)==ESP_OK && config_store_busy());
    assert(!strcmp(config_store_json(),KEYMAP_JSON)); /* no premature runtime publication */
    assert(config_store_submit(changed)!=ESP_OK);
    run_worker();esp_err_t result;
    assert(config_store_poll(&result) && result==ESP_OK && config_store_generation()==1);
    assert(!strcmp(keymap_binding(config_store_model(),0,0),"KC_A"));
    reboot(); assert(config_store_generation()==1 && !strcmp(config_store_json(),changed));
    commit_fail=true;assert(config_store_submit(KEYMAP_JSON)==ESP_OK);run_worker();
    assert(config_store_poll(&result) && result!=ESP_OK && config_store_generation()==1);
    reboot(); assert(config_store_generation()==1 && !strcmp(config_store_json(),changed));
    commit_fail=false;assert(config_store_submit(KEYMAP_JSON)==ESP_OK);run_worker();
    assert(config_store_poll(&result) && result==ESP_OK && config_store_generation()==2);
    durable[1][HEADER+10]^=1; /* newest blob corrupt, old bank survives */
    reboot();assert(config_store_generation()==1 && !strcmp(config_store_json(),changed));
    put32(durable[1]+4,99);reboot();assert(!config_store_writable());
    durable_size[0]=durable_size[1]=0;reboot();assert(config_store_generation()==0);
    const char *smart="{\"version\":1,\"smartActions\":{\"SA_1\":{\"type\":\"TEXT_STEP\",\"payload\":{\"text\":\"atomic\"}}}}";
    cJSON *linked=cJSON_Parse(changed);
    cJSON *lay=cJSON_GetArrayItem(cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(linked,"profiles"),0),"layers"),0);
    cJSON_ReplaceItemInArray(cJSON_GetArrayItem(cJSON_GetObjectItem(cJSON_GetObjectItem(lay,"layout"),"keymap"),0),0,cJSON_CreateString("SA_1"));
    char *with_smart=cJSON_PrintUnformatted(linked);cJSON_Delete(linked);
    assert(config_store_submit_files(with_smart,SMART_DEFAULT)!=ESP_OK);
    assert(config_store_submit_files(with_smart,smart)==ESP_OK);
    assert(!strcmp(config_store_smart(),SMART_DEFAULT));run_worker();
    assert(config_store_poll(&result) && result==ESP_OK);reboot();
    assert(!strcmp(config_store_json(),with_smart) && !strcmp(config_store_smart(),smart));
    commit_fail=true;assert(config_store_submit_files(KEYMAP_JSON,SMART_DEFAULT)==ESP_OK);run_worker();
    assert(config_store_poll(&result) && result!=ESP_OK);reboot();
    assert(!strcmp(config_store_json(),with_smart) && !strcmp(config_store_smart(),smart));commit_fail=false;cJSON_free(with_smart);
    durable_size[0]=durable_size[1]=0;
    size_t old_length=strlen(KEYMAP_JSON);uint8_t *old=legacy_bytes[0];legacy_size[0]=HEADER+old_length;
    put32(old,0x434d4346);put32(old+4,1);put32(old+8,7);put32(old+12,old_length);
    put32(old+16,crc32((const uint8_t *)KEYMAP_JSON,old_length));memcpy(old+HEADER,KEYMAP_JSON,old_length);
    reboot();assert(config_store_generation()==7 && !strcmp(config_store_smart(),SMART_DEFAULT));
    assert(config_store_submit(changed)==ESP_OK);run_worker();assert(config_store_poll(&result) && result==ESP_OK);
    reboot();assert(config_store_generation()==8 && !strcmp(config_store_json(),changed));assert(get32(old+8)==7);
    init_fail=true;reboot();assert(!config_store_writable() && config_store_submit(changed)!=ESP_OK);
    cJSON_free(changed);
    puts("Config store: atomic keymap+smart commit/reboot/failure, legacy migration, corrupt bank and read-only passed");
}
