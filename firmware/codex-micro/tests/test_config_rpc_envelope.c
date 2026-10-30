#undef NDEBUG
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "cJSON.h"

typedef int esp_err_t;
enum { ESP_OK=0, CONFIG_RPC_HANDLED=1, CONFIG_RPC_DEFERRED=2 };
static bool started[2];
static char staging[2][8];
static cJSON *pending_response;
static uint32_t pending_epoch;
static int admission_error;
static bool discarded;
static const char *config_store_json(void) { return "{}"; }
static const char *config_store_smart(void) { return "{}"; }
static esp_err_t config_store_submit_files(const char *keymap,const char *smart)
{ assert(keymap && smart); return admission_error; }
static void discard(void) { discarded=true; }
/* Verbatim functions extracted from config_rpc.c by the test runner. */
#include "config_rpc_envelope.inc"

int main(void)
{
    cJSON *response=cJSON_CreateObject();
    cJSON_AddNumberToObject(response,"id",631);
    cJSON_AddItemToObject(response,"result",cJSON_CreateObject());
    admission_error=-1;
    assert(persist(response,9)==CONFIG_RPC_HANDLED);
    assert(discarded && !pending_response);
    assert(!cJSON_HasObjectItem(response,"result"));
    assert(cJSON_IsNumber(cJSON_GetObjectItem(cJSON_GetObjectItem(response,"error"),"code")));
    assert(cJSON_GetObjectItem(response,"id")->valueint==631);
    cJSON_Delete(response);
    response=cJSON_CreateObject();
    cJSON_AddItemToObject(response,"result",cJSON_CreateObject());
    admission_error=0; discarded=false;
    assert(persist(response,10)==CONFIG_RPC_DEFERRED);
    assert(discarded && pending_response==response && pending_epoch==10);
    assert(cJSON_HasObjectItem(response,"result") && !cJSON_HasObjectItem(response,"error"));
    cJSON_Delete(response);
    puts("Config RPC: rejected writebin commit has error only; accepted acknowledgement remains deferred");
    return 0;
}
