#include "smart_actions.h"
#include <string.h>
static const char *method(const cJSON *entry)
{
    const cJSON *t=cJSON_GetObjectItemCaseSensitive(entry,"type");
    if (!cJSON_IsString(t)) return NULL;
    const char *types[]={"TEXT_STEP","CMD_STEP","APP_STEP","URL_STEP"};
    const char *methods[]={"kb.sa.inserttext","kb.sa.exec","kb.sa.openapp","kb.sa.openurl"};
    const char *fields[]={"text","cmd","path","url"};
    const cJSON *p=cJSON_GetObjectItemCaseSensitive(entry,"payload");
    if (!cJSON_IsObject(p)) return NULL;
    for (unsigned i=0;i<4;++i) if (!strcmp(t->valuestring,types[i])) {
        const cJSON *v=cJSON_GetObjectItemCaseSensitive(p,fields[i]);
        if (!cJSON_IsString(v) || strlen(v->valuestring)>4096) return NULL;
        const cJSON *name=cJSON_GetObjectItemCaseSensitive(p,"name");
        if (i==2 && name && !cJSON_IsString(name)) return NULL;
        char *serialized=cJSON_PrintUnformatted(p);
        bool fits=serialized && strlen(serialized)<=SMART_NOTICE_BYTES-96;
        cJSON_free(serialized);
        return fits?methods[i]:NULL;
    }
    return NULL;
}
static cJSON *parse(const char *text)
{ if (!text || strlen(text)>SMART_BYTES) return NULL; return cJSON_ParseWithOpts(text,NULL,true); }
bool smart_actions_validate(const char *text)
{
    cJSON *root=parse(text); if (!root) return false;
    const cJSON *version=cJSON_GetObjectItemCaseSensitive(root,"version"),*actions=cJSON_GetObjectItemCaseSensitive(root,"smartActions"),*item;
    bool ok=cJSON_IsNumber(version) && version->valuedouble==1 && cJSON_IsObject(actions);
    unsigned count=0;
    cJSON_ArrayForEach(item,actions) {
        if (++count>64 || !item->string || action_token_id(item->string,"SA_")<0 || !method(item)) ok=false;
        for (const cJSON *prior=actions->child;prior!=item;prior=prior->next) if (!strcmp(prior->string,item->string)) ok=false;
    }
    cJSON_Delete(root); return ok;
}
static bool exists(const cJSON *actions,const char *token)
{ return action_token_id(token,"SA_")<0 || method(cJSON_GetObjectItemCaseSensitive(actions,token))!=NULL; }
bool smart_actions_references(const keymap_model_t *m,const char *json)
{
    cJSON *root=parse(json); if (!root) return false;
    const cJSON *a=cJSON_GetObjectItemCaseSensitive(root,"smartActions"); bool ok=true;
    for (unsigned p=0;p<m->count;++p) for (unsigned l=0;l<6;++l) if (m->profiles[p].layer[l].present) {
        const keymap_layer_t *layer=&m->profiles[p].layer[l];
        for (unsigned b=0;b<KEYMAP_BINDINGS;++b) if (!exists(a,layer->tokens[b])) ok=false;
        for (unsigned b=0;b<layer->sector_count;++b) if (!exists(a,layer->sectors[b].token)) ok=false;
    }
    for (unsigned i=0;i<m->step_count;++i) if (!exists(a,m->steps[i].token)) ok=false;
    for (unsigned i=0;i<m->multi_count;++i) for (unsigned b=0;b<4;++b) if (!exists(a,m->multis[i].branch[b])) ok=false;
    cJSON_Delete(root); return ok;
}
cJSON *smart_action_notification(const char *text,const char *token)
{
    cJSON *root=parse(text); if (!root) return NULL;
    const cJSON *a=cJSON_GetObjectItemCaseSensitive(root,"smartActions"),*entry=cJSON_GetObjectItemCaseSensitive(a,token);
    const char *name=method(entry); cJSON *out=NULL;
    if (name) {
        out=cJSON_CreateObject(); cJSON_AddStringToObject(out,"method",name);
        cJSON *payload=cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(entry,"payload"),true);
        if (!out || !payload || !cJSON_AddItemToObject(out,"params",payload)) { cJSON_Delete(payload); cJSON_Delete(out); out=NULL; }
    }
    cJSON_Delete(root); return out;
}
