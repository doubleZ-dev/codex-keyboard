#include "config_rpc.h"
#include "config_store.h"
#include "smart_actions.h"
#include "esp_timer.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha1.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define CONFIG_RAM EXT_RAM_BSS_ATTR
#else
#define CONFIG_RAM
#endif

static CONFIG_RAM char staging_keymap[KEYMAP_BYTES+1],staging_smart[SMART_BYTES+1];
static char *const staging[]={staging_keymap,staging_smart};
static size_t staged_length[2];
static bool transaction,complete[2],started[2];
static uint32_t transaction_id,session;
static uint64_t touched;
static cJSON *pending_response;
static uint32_t pending_epoch;
static void error(cJSON *r,int code,const char *message)
{
    /* writebin prepares its acknowledgement before commit admission. A rejected
     * commit must not leave both result and error in the JSON-RPC envelope. */
    cJSON_DeleteItemFromObjectCaseSensitive(r,"result");
    cJSON *e=cJSON_CreateObject(); cJSON_AddNumberToObject(e,"code",code); cJSON_AddStringToObject(e,"message",message); cJSON_AddItemToObject(r,"error",e);
}
static void ok(cJSON *r)
{ cJSON *v=cJSON_CreateObject(); cJSON_AddBoolToObject(v,"ok",true); cJSON_AddItemToObject(r,"result",v); }
static int file_index(const cJSON *p)
{
    const cJSON *f=cJSON_GetObjectItemCaseSensitive(p,"file");
    if (!cJSON_IsString(f)) return -1;
    const char *s=f->valuestring; if (*s=='/') ++s;
    if (!strcmp(s,"keymap.json")) return 0;
    if (!strcmp(s,"smart_actions.json")) return 1;
    return -1;
}
static bool valid_staged(int file)
{
    if (file==1) return smart_actions_validate(staging[1]);
    cJSON *root=cJSON_ParseWithOpts(staging[0],NULL,true);
    /* Staging acknowledgement isn't a commit. Full schema and cross-file
     * references are checked exactly once by config_store_submit_files. */
    bool valid=cJSON_IsObject(root);
    cJSON_Delete(root); return valid;
}
static void discard(void)
{
    transaction=false;
    memset(complete,0,sizeof(complete)); memset(started,0,sizeof(started)); memset(staged_length,0,sizeof(staged_length));
    staging[0][0]=staging[1][0]=0;
}
void config_rpc_session(uint32_t epoch)
{
    session=epoch; discard();
    /* Accepted Flash write may finish after disconnect; never free worker data here. */
}
static int persist(cJSON *resp,uint32_t epoch)
{
    esp_err_t err=config_store_submit_files(started[0]?staging[0]:config_store_json(),started[1]?staging[1]:config_store_smart());
    if (err!=ESP_OK) { error(resp,-3,"Configuration validation or storage failed"); discard(); return CONFIG_RPC_HANDLED; }
    pending_response=resp; pending_epoch=epoch; discard(); return CONFIG_RPC_DEFERRED;
}
static bool unsigned_param(const cJSON *p,const char *name,size_t fallback,size_t *out)
{
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(p,name);
    if (!v) { *out=fallback; return true; }
    if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble<0 || v->valuedouble>UINT32_MAX ||
        v->valuedouble!=floor(v->valuedouble)) return false;
    *out=(size_t)v->valuedouble; return true;
}
int config_rpc_handle(const char *method,const cJSON *p,cJSON *resp,uint32_t epoch)
{
    if (strncmp(method,"fs.",3)) return CONFIG_RPC_UNHANDLED;
    if (epoch!=session) config_rpc_session(epoch);
    uint64_t now=esp_timer_get_time();
    if (touched && now-touched>30000000) discard();
    touched=now;
    int file=file_index(p);
    const char *text=file==1?config_store_smart():config_store_json(); size_t length=strlen(text);
    if (!strcmp(method,"fs.list")) {
        cJSON *list=cJSON_CreateArray();
        const char *names[]={"keymap.json","smart_actions.json"};
        for (unsigned i=0;i<2;++i) {
            const char *content=i?config_store_smart():config_store_json();
            uint8_t hash[20]; char hex[41],size[16];
            if (mbedtls_sha1((const unsigned char *)content,strlen(content),hash)) { cJSON_Delete(list); error(resp,-3,"Checksum failed"); return CONFIG_RPC_HANDLED; }
            for (unsigned j=0;j<20;++j) snprintf(hex+j*2,3,"%02x",hash[j]);
            snprintf(size,sizeof(size),"%u",(unsigned)strlen(content));
            cJSON *entry=cJSON_CreateObject(); cJSON_AddStringToObject(entry,"name",names[i]);
            cJSON_AddStringToObject(entry,"size",size); cJSON_AddStringToObject(entry,"checksum",hex); cJSON_AddItemToArray(list,entry);
        }
        cJSON_AddItemToObject(resp,"result",list);
    } else if (!strcmp(method,"fs.read") || !strcmp(method,"fs.readbin")) {
        if (file<0) error(resp,-2,"File does not exist");
        else if (!strcmp(method,"fs.read")) {
            if (length>5600) error(resp,-32602,"Use fs.readbin for this file size");
            else cJSON_AddItemToObject(resp,"result",cJSON_Parse(text));
        }
        else {
            size_t offset,want;
            if (!unsigned_param(p,"offset",0,&offset) || !unsigned_param(p,"len",384,&want)) error(resp,-32602,"Invalid range");
            else {
                if (offset>length) offset=length;
                if (want>3072) want=3072;
                if (want>length-offset) want=length-offset;
                unsigned char *encoded=calloc(1,4097); size_t written=0;
                if (!encoded) { error(resp,-3,"No memory"); return CONFIG_RPC_HANDLED; }
                if (want && mbedtls_base64_encode(encoded,4097,&written,(const unsigned char *)text+offset,want)) {
                    free(encoded); error(resp,-3,"Encoding failed"); return CONFIG_RPC_HANDLED;
                }
                cJSON *result=cJSON_CreateObject(); cJSON_AddNumberToObject(result,"total_size",length);
                cJSON_AddStringToObject(result,"data",(const char *)encoded); cJSON_AddItemToObject(resp,"result",result);
                free(encoded);
            }
        }
    } else if (config_store_busy()) error(resp,-32000,"Configuration commit in progress");
    else if (!strcmp(method,"fs.txbegin")) {
        if (transaction) error(resp,-32000,"Transaction already active");
        else {
            discard(); transaction=true; if (++transaction_id==0) ++transaction_id;
            cJSON *result=cJSON_CreateObject(); cJSON_AddNumberToObject(result,"tx",transaction_id);
            cJSON_AddItemToObject(resp,"result",result);
        }
    } else if (!strcmp(method,"fs.txcommit")) {
        size_t tx;
        if (!transaction || (!started[0] && !started[1]) || (started[0] && !complete[0]) || (started[1] && !complete[1]) || !unsigned_param(p,"tx",transaction_id,&tx) || tx!=transaction_id)
            error(resp,-32602,"No complete transaction or mismatched token");
        else return persist(resp,epoch);
    } else if (!strcmp(method,"fs.write") || !strcmp(method,"fs.writebin")) {
        if (file<0) { error(resp,-2,"Unsupported file"); return CONFIG_RPC_HANDLED; }
        size_t tx;
        if (!unsigned_param(p,"tx",transaction?transaction_id:0,&tx) ||
            tx!=(transaction?transaction_id:0)) {
            error(resp,-32602,"Mismatched transaction token"); return CONFIG_RPC_HANDLED;
        }
        size_t limit=file?SMART_BYTES:KEYMAP_BYTES;
        if (!strcmp(method,"fs.write")) {
            const cJSON *data=cJSON_GetObjectItemCaseSensitive(p,"data");
            char *serialized=cJSON_IsObject(data)?cJSON_PrintUnformatted(data):NULL;
            if (!serialized || strlen(serialized)>limit) { cJSON_free(serialized); error(resp,-32602,"Invalid keymap object or size"); return CONFIG_RPC_HANDLED; }
            strcpy(staging[file],serialized); staged_length[file]=strlen(staging[file]); cJSON_free(serialized); started[file]=true; complete[file]=valid_staged(file);
        } else {
            const cJSON *data=cJSON_GetObjectItemCaseSensitive(p,"data");
            const cJSON *done=cJSON_GetObjectItemCaseSensitive(p,"completed");
            const cJSON *append=cJSON_GetObjectItemCaseSensitive(p,"append");
            size_t offset;
            if (!unsigned_param(p,"offset",staged_length[file],&offset) ||
                (append && !cJSON_IsTrue(append)) || offset!=staged_length[file]) {
                error(resp,-32602,"Out-of-order chunk or unsupported append mode"); return CONFIG_RPC_HANDLED;
            }
            if (!cJSON_IsString(data) || (done && !cJSON_IsBool(done)) || complete[file]) {
                error(resp,-32602,"Invalid chunk or completed transaction"); return CONFIG_RPC_HANDLED;
            }
            size_t decoded=0; unsigned char *chunk=malloc(4096);
            if (!chunk) { error(resp,-3,"No memory"); return CONFIG_RPC_HANDLED; }
            int result=mbedtls_base64_decode(chunk,4096,&decoded,(const unsigned char *)data->valuestring,strlen(data->valuestring));
            if (result || decoded>limit-staged_length[file] || memchr(chunk,0,decoded)) {
                free(chunk); discard(); error(resp,-32602,"Invalid base64 or file too large"); return CONFIG_RPC_HANDLED;
            }
            memcpy(staging[file]+staged_length[file],chunk,decoded); free(chunk); staged_length[file]+=decoded; staging[file][staged_length[file]]=0; started[file]=true;
            if (!cJSON_IsTrue(done)) {
                cJSON *r=cJSON_CreateObject(); cJSON_AddNumberToObject(r,"data_written",decoded); cJSON_AddItemToObject(resp,"result",r);
                return CONFIG_RPC_HANDLED;
            }
            complete[file]=valid_staged(file);
            /* Preserve writebin's response shape even for delayed durable completion. */
            cJSON *r=cJSON_CreateObject(); cJSON_AddNumberToObject(r,"data_written",decoded); cJSON_AddItemToObject(resp,"result",r);
        }
        if (!complete[file]) {
            cJSON_DeleteItemFromObjectCaseSensitive(resp,"result"); discard(); error(resp,-32602,"Unsupported or invalid keymap");
        } else if (transaction) { if (!cJSON_GetObjectItem(resp,"result")) ok(resp); }
        else return persist(resp,epoch);
    } else error(resp,-32601,"Filesystem operation not implemented");
    return CONFIG_RPC_HANDLED;
}
cJSON *config_rpc_poll(uint32_t epoch,bool *changed)
{
    *changed=false; esp_err_t err;
    if (!config_store_poll(&err)) return NULL;
    *changed=err==ESP_OK;
    cJSON *resp=pending_response; pending_response=NULL;
    if (!resp) return NULL;
    if (pending_epoch!=epoch) { cJSON_Delete(resp); return NULL; }
    if (err==ESP_OK) { if (!cJSON_GetObjectItem(resp,"result")) ok(resp); }
    else { cJSON_DeleteItemFromObjectCaseSensitive(resp,"result"); error(resp,-3,"Persistent commit failed; old runtime retained"); }
    return resp;
}
