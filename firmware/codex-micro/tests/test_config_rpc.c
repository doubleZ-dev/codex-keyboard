#include "config_rpc.h"
#include "config_store.h"
#include "smart_actions.h"
#include "mbedtls/base64.h"
#include "../main/keymap_default.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
static char current[KEYMAP_BYTES+1]="{\"old\":true}",candidate[KEYMAP_BYTES+1];
static char smart_current[SMART_BYTES+1]=SMART_DEFAULT,smart_candidate[SMART_BYTES+1];
static bool busy,reject,fail_completion;
static bool validate_config;
static unsigned commits;
static uint64_t clock_us=1;
int64_t esp_timer_get_time(void) { return clock_us; }
const char *config_store_json(void) { return current; }
const char *config_store_smart(void) { return smart_current; }
bool config_store_busy(void) { return busy; }
esp_err_t config_store_submit_files(const char *key,const char *smart)
{
    if (reject) return ESP_ERR_INVALID_ARG;
    if (validate_config) {
        static keymap_model_t model;
        cJSON *root=cJSON_Parse(key);
        bool valid=keymap_compile(root,&model) && smart_actions_validate(smart) && smart_actions_references(&model,smart);
        cJSON_Delete(root); if (!valid) return ESP_ERR_INVALID_ARG;
    }
    assert(!busy && strlen(key)<sizeof(candidate));
    strcpy(candidate,key);strcpy(smart_candidate,smart);busy=true;++commits;return ESP_OK;
}
bool config_store_poll(esp_err_t *err)
{
    if (!busy) return false;
    busy=false; *err=fail_completion?ESP_FAIL:ESP_OK;
    if (!fail_completion) { strcpy(current,candidate);strcpy(smart_current,smart_candidate); }
    return true;
}
static cJSON *call(const char *method,cJSON *p,int expected)
{
    cJSON *r=cJSON_CreateObject();
    assert(config_rpc_handle(method,p,r,1)==expected);cJSON_Delete(p);return r;
}
static cJSON *chunk(const char *file,const char *bytes,size_t n,unsigned offset,unsigned tx,bool done)
{
    unsigned char encoded[4097];size_t written;
    assert(!mbedtls_base64_encode(encoded,sizeof(encoded),&written,(const unsigned char *)bytes,n));
    cJSON *p=cJSON_CreateObject();cJSON_AddStringToObject(p,"file",file);cJSON_AddStringToObject(p,"data",(const char *)encoded);
    cJSON_AddNumberToObject(p,"offset",offset);cJSON_AddNumberToObject(p,"tx",tx);
    cJSON_AddBoolToObject(p,"append",true);cJSON_AddBoolToObject(p,"completed",done);return p;
}
static void no_error(cJSON *r) { assert(!cJSON_GetObjectItem(r,"error"));cJSON_Delete(r); }
static void has_error(cJSON *r) { assert(cJSON_GetObjectItem(r,"error") && !cJSON_GetObjectItem(r,"result"));cJSON_Delete(r); }
int main(int argc,char **argv)
{
    if (argc==2 && !strcmp(argv[1],"--serve")) {
        validate_config=true;strcpy(current,KEYMAP_JSON);
        char line[8192];
        while (fgets(line,sizeof(line),stdin)) {
            cJSON *request=cJSON_Parse(line),*r=cJSON_CreateObject();assert(request && r);
            cJSON *method=cJSON_GetObjectItem(request,"method"),*p=cJSON_GetObjectItem(request,"params"),*id=cJSON_GetObjectItem(request,"id");
            assert(cJSON_IsString(method));if (id)cJSON_AddItemToObject(r,"id",cJSON_Duplicate(id,true));
            int handled=config_rpc_handle(method->valuestring,p,r,1);assert(handled!=CONFIG_RPC_UNHANDLED);
            if (handled==CONFIG_RPC_DEFERRED) { bool changed;assert(config_rpc_poll(1,&changed)==r); }
            char *json=cJSON_PrintUnformatted(r);assert(json);puts(json);fflush(stdout);
            cJSON_free(json);cJSON_Delete(r);cJSON_Delete(request);
        }
        return 0;
    }
    static char file[10020];strcpy(file,"{\"text\":\"");memset(file+9,'x',10000);strcpy(file+10009,"\"}");
    cJSON *r=call("fs.txbegin",NULL,CONFIG_RPC_HANDLED);
    unsigned tx=cJSON_GetObjectItem(cJSON_GetObjectItem(r,"result"),"tx")->valueint;no_error(r);
    has_error(call("fs.writebin",chunk("keymap.json",file,3072,0,tx+1,false),CONFIG_RPC_HANDLED));
    no_error(call("fs.writebin",chunk("keymap.json",file,3072,0,tx,false),CONFIG_RPC_HANDLED));
    has_error(call("fs.writebin",chunk("keymap.json",file,3072,0,tx,false),CONFIG_RPC_HANDLED));
    assert(!strcmp(current,"{\"old\":true}") && !commits);
    for (size_t pos=3072;pos<strlen(file);pos+=3072) {
        size_t n=strlen(file)-pos;if(n>3072)n=3072;
        no_error(call("fs.writebin",chunk("keymap.json",file+pos,n,pos,tx,pos+n==strlen(file)),CONFIG_RPC_HANDLED));
    }
    no_error(call("fs.writebin",chunk("smart_actions.json",SMART_DEFAULT,strlen(SMART_DEFAULT),0,tx,true),CONFIG_RPC_HANDLED));
    cJSON *p=cJSON_CreateObject();cJSON_AddNumberToObject(p,"tx",tx);
    r=call("fs.txcommit",p,CONFIG_RPC_DEFERRED);assert(!cJSON_GetObjectItem(r,"result") && commits==1 && busy);
    bool changed;assert(config_rpc_poll(1,&changed)==r && changed);no_error(r);assert(!strcmp(current,file));
    p=cJSON_CreateObject();cJSON_AddStringToObject(p,"file","keymap.json");cJSON_AddNumberToObject(p,"offset",3072);cJSON_AddNumberToObject(p,"len",3072);
    r=call("fs.readbin",p,CONFIG_RPC_HANDLED);cJSON *result=cJSON_GetObjectItem(r,"result");
    const char *data=cJSON_GetObjectItem(result,"data")->valuestring;assert(strlen(data)==4096);
    unsigned char decoded[3072];size_t count;
    assert(!mbedtls_base64_decode(decoded,sizeof(decoded),&count,(const unsigned char *)data,strlen(data)) && count==3072 && !memcmp(decoded,file+3072,count));no_error(r);
    fail_completion=true;r=call("fs.writebin",chunk("keymap.json","{}",2,0,0,true),CONFIG_RPC_DEFERRED);
    assert(config_rpc_poll(1,&changed)==r && !changed);has_error(r);assert(!strcmp(current,file));fail_completion=false;
    r=call("fs.writebin",chunk("keymap.json","{}",2,0,0,true),CONFIG_RPC_DEFERRED);
    config_rpc_session(2);assert(!config_rpc_poll(2,&changed) && changed); /* ACK stale; durable job may finish. */
    assert(!strcmp(current,"{}"));
    char nested[100];memset(nested,'[',40);nested[40]='0';memset(nested+41,']',40);nested[81]=0;
    assert(!cJSON_Parse(nested)); /* Same bounded cJSON option as firmware. */
    puts("Actual config RPC + SDK Base64/SHA1: Input 3072-byte chunks, token isolation, duplicate offsets, two-file commit, exact readback, failed Flash publication and stale-session ACK passed");
}
