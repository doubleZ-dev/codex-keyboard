#include "light_engine.h"
#include "stick_stream.h"
#include "keymap_model.h"
#include "hid_state.h"
#include "../main/keymap_default.h"
#undef NDEBUG
#include <assert.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
static bool apply(light_engine_t *s,const char *method,const char *json)
{
    cJSON *p=cJSON_Parse(json); assert(p);
    bool ok=light_engine_apply(s,method,p); cJSON_Delete(p); return ok;
}
static bool busy;
static float angle,distance;
static unsigned sent;
static uint8_t hid_report[8],hid_id;
static bool hid_send(uint8_t id,const uint8_t *p,unsigned n,void *ctx)
{ (void)ctx; if (busy) return false; hid_id=id; memcpy(hid_report,p,n); return true; }
static bool send(float a,float d,void *ctx)
{ (void)ctx; if (busy) return false; angle=a; distance=d; ++sent; return true; }
int main(void)
{
    light_engine_t s; light_engine_init(&s);
    board_light_frame_t fallback;
    light_engine_render(&s,0,0,0,false,&fallback);
    assert(fallback.keys[0].r>0 && fallback.keys[12].b>0 && fallback.ambient[7].g>0);
    assert(apply(&s,"v.oai.rgbcfg","{\"keys\":{\"e\":0},\"ambient\":{\"e\":0}}"));
    light_engine_render(&s,0,0,0,false,&fallback);
    assert(!fallback.keys[0].r && !fallback.ambient[7].g); /* Explicit off wins. */
    light_engine_init(&s); /* New Host session starts with visible fallback. */
    assert(apply(&s,"v.oai.rgbcfg","{\"keys\":{\"e\":1,\"b\":1,\"c\":16711680},\"ambient\":{\"e\":1,\"b\":1,\"c\":255}}"));
    board_light_frame_t f;
    light_engine_render(&s,0,0,0,false,&f);
    assert(f.keys[12].r==255 && f.ambient[7].b==255 && f.brightness==64);
    light_engine_t before=s;
    assert(!apply(&s,"v.oai.rgbcfg","{\"keys\":{\"c\":0},\"ambient\":{\"b\":2}}"));
    assert(!memcmp(&s,&before,sizeof(s)));
    assert(!apply(&s,"v.oai.rgbcfg","{\"keys\":{\"e\":1,\"e\":2}}"));
    assert(!apply(&s,"v.oai.rgbcfg","{\"keys\":{\"m\":2}}"));
    assert(!apply(&s,"v.oai.thstatus","[{\"id\":0,\"c\":0},{\"id\":6}]"));
    assert(!memcmp(&s,&before,sizeof(s)));
    assert(apply(&s,"v.oai.thstatus","[{\"id\":2,\"c\":65280,\"b\":1,\"e\":1,\"sa\":1}]"));
    light_engine_render(&s,1000000,2,5,true,&f);
    assert(f.keys[2].g==255 && f.ambient[0].g==255 && f.brightness==64);
    assert(f.status_mask==0);
    light_engine_render(&s,1500000,2,5,true,&f);
    assert(f.status_mask==7 && f.brightness==64); /* Warning only; no dimming. */
    for (int effect=0;effect<=6;++effect) {
        s.ambient.effect=effect; s.ambient.speed=1;
        for (unsigned t=0;t<100;++t) light_engine_render(&s,(uint64_t)t*37000,0,0,false,&f);
    }
    stick_stream_t stick; stick_stream_reset(&stick);
    stick_stream_update(&stick,32767,0);
    busy=true; assert(!stick_stream_pump(&stick,40000,send,NULL) && stick.pending);
    busy=false; assert(stick_stream_pump(&stick,40000,send,NULL) && sent==1 && angle==0 && distance==1);
    stick_stream_update(&stick,0,0); stick_stream_update(&stick,0,32767);
    assert(stick_stream_pump(&stick,41000,send,NULL) && distance==0 && stick.pending);
    assert(stick_stream_pump(&stick,42000,send,NULL) && sent==2);
    assert(stick_stream_pump(&stick,81000,send,NULL) && sent==3 && fabsf(angle-0.25f)<0.00001f);
    stick_stream_update(&stick,-32768,-32768);
    assert(stick_stream_pump(&stick,121000,send,NULL) && distance==1 && angle>=0 && angle<=1);
    puts("Services: atomic light validation, rendering, sync, brightness and stick backpressure/neutral passed");
    static keymap_model_t model;
    cJSON *keymap=cJSON_Parse(KEYMAP_JSON); assert(keymap && keymap_compile(keymap,&model));
    assert(!strcmp(keymap_binding(&model,0,13),"KV_OAI_ACT10"));
    cJSON *profiles=cJSON_GetObjectItem(keymap,"profiles");
    cJSON *layers=cJSON_GetObjectItem(cJSON_GetArrayItem(profiles,0),"layers");
    for (unsigned i=1;i<6;++i) {
        cJSON *copy=cJSON_Duplicate(cJSON_GetArrayItem(layers,0),true);
        cJSON_SetNumberValue(cJSON_GetObjectItem(copy,"id"),i); cJSON_AddItemToArray(layers,copy);
    }
    assert(keymap_compile(keymap,&model) && model.profiles[0].layers==6);
    cJSON *layout=cJSON_GetObjectItem(cJSON_GetArrayItem(layers,5),"layout");
    cJSON *row=cJSON_GetArrayItem(cJSON_GetObjectItem(layout,"keymap"),0);
    cJSON_ReplaceItemInArray(row,0,cJSON_CreateString("KC_A"));
    assert(keymap_compile(keymap,&model) && !strcmp(keymap_binding(&model,5,0),"KC_A"));
    cJSON_ReplaceItemInArray(row,0,cJSON_CreateString("UNKNOWN_MACRO"));
    assert(!keymap_compile(keymap,&model)); cJSON_Delete(keymap);
    hid_state_t hid; hid_state_reset(&hid);
    assert(hid_state_key(&hid,"KC_A",true,hid_send,NULL) && hid_id==1 && hid_report[2]==4);
    assert(hid_state_key(&hid,"KC_A",true,hid_send,NULL));
    assert(hid_state_key(&hid,"KC_A",false,hid_send,NULL) && hid_report[2]==4);
    busy=true; assert(!hid_state_key(&hid,"KC_A",false,hid_send,NULL) && hid.usages[4]==1);
    busy=false; assert(hid_state_key(&hid,"KC_A",false,hid_send,NULL) && hid_report[2]==0);
    assert(hid_state_key(&hid,"KC_LCTL",true,hid_send,NULL) && hid_report[0]==1);
    assert(hid_state_key(&hid,"KC_MPLY",true,hid_send,NULL) && hid_id==2 && hid_report[0]==0xCD);
    assert(hid_state_release(&hid,hid_send,NULL));
    puts("Keymap/HID: six layers, schema rejection, reference counts, modifiers and consumer passed");
}
