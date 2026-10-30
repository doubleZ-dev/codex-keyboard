#include "radial_runtime.h"
#include "layer_navigation.h"
#include "../main/keymap_default.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
static bool blocked,notice_blocked;
static unsigned presses,releases,notices,operation,captured_agent;
static char captured[24];
static bool output(const char *token,bool down,unsigned agent,void *ctx)
{
    (void)ctx; if (blocked) return false;
    if (down) { ++presses; strcpy(captured,token); captured_agent=agent; }
    else { ++releases; assert(!strcmp(captured,token)); assert(captured_agent==agent); }
    return true;
}
static bool notice(float a,float d,int sector,unsigned op,void *ctx)
{ (void)a;(void)d;(void)sector;(void)ctx; if (notice_blocked) return false; ++notices; operation=op; return true; }
static keymap_model_t model;
int main(void)
{
    cJSON *root=cJSON_Parse(KEYMAP_JSON); assert(root);
    cJSON *layer=cJSON_GetArrayItem(cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(root,"profiles"),0),"layers"),0);
    cJSON *layout=cJSON_GetObjectItem(layer,"layout");
    cJSON_ReplaceItemInObject(layout,"joystick",cJSON_Parse("{\"type\":\"RADIAL\",\"sectors\":[{\"k\":\"KI_X\",\"a1\":0.1875,\"a2\":0.3125},{\"k\":\"KC_NONE\",\"a1\":0.3125,\"a2\":0.1875}]}"));
    assert(keymap_compile(root,&model));
    keymap_layer_t *l=&model.profiles[0].layer[0];
    assert(keymap_sector(l,0.1875f)==0 && keymap_sector(l,0.3125f)==1 && keymap_sector(l,1)==1);
    cJSON *sectors=cJSON_GetObjectItem(cJSON_GetObjectItem(layout,"joystick"),"sectors");
    cJSON_ReplaceItemInObject(cJSON_GetArrayItem(sectors,1),"a2",cJSON_CreateNumber(0.2));
    assert(!keymap_compile(root,&model)); cJSON_Delete(root);
    memset(l,0,sizeof(*l)); l->joystick_mode=JOYSTICK_RADIAL; l->sector_count=2;
    l->sectors[0]=(radial_sector_t){.token="KC_A",.start=0,.end=.5};
    l->sectors[1]=(radial_sector_t){.token="KI_X",.start=.5,.end=1};
    radial_runtime_t s; radial_runtime_reset(&s);
    assert(radial_runtime_tick(&s,l,16000,0,2,1,output,notice,NULL)); assert(!presses && operation==1);
    blocked=true; assert(!radial_runtime_tick(&s,l,30000,0,2,2,output,notice,NULL)); assert(!s.held && !presses);
    blocked=false; notice_blocked=true;
    assert(radial_runtime_tick(&s,l,30000,0,2,3,output,notice,NULL)); assert(s.held && presses==1);
    assert(!radial_runtime_tick(&s,l,30000,0,5,40002,output,notice,NULL)); assert(presses==1);
    notice_blocked=false; assert(radial_runtime_tick(&s,l,30000,0,5,40003,output,notice,NULL)); assert(operation==2);
    blocked=true; assert(!radial_runtime_tick(&s,l,18000,0,5,40004,output,notice,NULL)); assert(s.held);
    blocked=false; assert(radial_runtime_tick(&s,l,18000,0,5,40005,output,notice,NULL)); assert(!s.held && releases==1);
    assert(radial_runtime_tick(&s,l,0,-30000,0,40006,output,notice,NULL)); assert(s.closed_until_neutral && operation==0);
    assert(radial_runtime_tick(&s,l,30000,0,0,80007,output,notice,NULL)); assert(presses==1);
    assert(radial_runtime_tick(&s,l,0,0,0,80008,output,notice,NULL)); assert(!s.closed_until_neutral);
    assert(radial_runtime_tick(&s,l,30000,0,4,80009,output,notice,NULL)); assert(presses==2);
    notice_blocked=true; assert(!radial_runtime_cancel(&s,output,notice,NULL)); assert(releases==2 && !s.held);
    notice_blocked=false; assert(radial_runtime_cancel(&s,output,notice,NULL)); assert(releases==2 && !s.open);
    layer_navigation_t nav; layer_navigation_reset(&nav,0);
    assert(layer_navigation_hold(&nav,1,true)); assert(layer_navigation_current(&nav)==1);
    assert(layer_navigation_hold(&nav,2,true)); assert(layer_navigation_hold(&nav,2,true));
    assert(layer_navigation_hold(&nav,1,false)); assert(layer_navigation_current(&nav)==2);
    assert(layer_navigation_select(&nav,4)); assert(layer_navigation_current(&nav)==2);
    assert(layer_navigation_hold(&nav,2,false)); assert(layer_navigation_current(&nav)==2);
    assert(layer_navigation_hold(&nav,2,false)); assert(layer_navigation_current(&nav)==4);
    assert(!layer_navigation_hold(&nav,6,true));
    puts("RADIAL/navigation: real Input default, circular boundaries/overlap, preview/activation/release, captured ownership, congestion, heartbeat, close lock and nested momentary restoration passed");
}
