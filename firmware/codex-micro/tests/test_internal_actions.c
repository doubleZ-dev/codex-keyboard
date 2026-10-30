#include "keymap_model.h"
#include "layer_navigation.h"
#include "radial_runtime.h"
#include "usb_rpc.h"
#include "ble_transport.h"
#include "default_actions.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#undef NDEBUG
#include <assert.h>
static keymap_model_t model;
static uint8_t active_profile,active_layer;
static int requested_profile=-1,requested_layer=-1;
static bool input_resync,prefer_ble,selected_ble,ble_busy;
static int transport_request=-1;
static uint32_t pending_ble_preference;
static unsigned pending_ble_slot,handover_neutral,neutral_count,stick_count;
static bool handover_released,neutral_blocked;
static default_actions_t actions;
static action_runtime_t advanced;
static float brightness_scale=1;
static uint32_t unavailable_outputs;
static layer_navigation_t navigation;
static radial_runtime_t radial;
static rpc_tx_t rpc_tx;
static ble_transport_status_t ble;
static int last_ble_kind,last_ble_slot;
static unsigned releases;
static const keymap_model_t *config_store_model(void) { return &model; }
void ble_transport_status(ble_transport_status_t *s) { *s=ble; }
bool ble_transport_command(ble_command_kind_t kind,unsigned slot,uint32_t *token)
{ if(ble_busy)return false;if(token)*token=42;last_ble_kind=kind;last_ble_slot=slot;return true; }
static bool send_key_action(const char *key,bool down,unsigned agent,void *ctx)
{ (void)key;(void)agent;(void)ctx;if(!down)++releases;return true; }
static bool send_radial_notice(float a,float d,int sector,unsigned op,void *ctx)
{ (void)a;(void)d;(void)sector;(void)op;(void)ctx;return true; }
static bool send_leaf_action(const char *key,bool down,unsigned agent,void *ctx)
{ return send_key_action(key,down,agent,ctx); }
static bool send_stick(float angle,float distance,void *ctx)
{ (void)angle;(void)distance;(void)ctx;++stick_count;return rpc_tx_push(&rpc_tx,(const uint8_t *)"{}",2); }
static bool config_store_busy(void) { return false; }
static bool firmware_update_busy(void) { return false; }
static bool submit_standard(uint8_t id,const uint8_t *bytes,unsigned length,void *ctx)
{ (void)ctx;assert(id==neutral_count+1 && length==(id==1?8:id==2?2:1) && !bytes[0]);if(neutral_blocked)return false;++neutral_count;return true; }
#include "internal_action.inc"
#include "transport_selection.inc"
static cJSON *notice(unsigned index)
{ return cJSON_ParseWithLength((const char *)rpc_tx.entries[index].bytes,rpc_tx.entries[index].length); }
int main(void)
{
    model.count=2;model.profiles[0].layers=3;
    model.profiles[0].layer_order[0]=0;model.profiles[0].layer_order[1]=3;model.profiles[0].layer_order[2]=5;
    model.profiles[1].layers=1;model.profiles[1].layer_order[0]=0;
    layer_navigation_reset(&navigation,0);
    assert(internal_action("KI_LM2",true) && input_resync && requested_layer==3);
    active_layer=3;input_resync=false;requested_layer=-1;
    assert(internal_action("KI_LS3",true) && !input_resync && navigation.base==5);
    assert(internal_action("KI_LM2",false) && requested_layer==5 && input_resync);
    active_layer=5;input_resync=false;requested_layer=-1;
    assert(internal_action("KI_LS6",true) && !input_resync); /* Missing target is bounded no-op. */
    assert(internal_action("KI_LM2",true));active_layer=3;
    assert(internal_action("KI_PS2",true) && requested_profile==1 && requested_layer==0);
    assert(internal_action("KI_LM2",false) && requested_layer==0);
    active_layer=5;
    requested_profile=requested_layer=-1;input_resync=false;
    assert(internal_action("KI_CS_SHOW",true));cJSON *r=notice(0);assert(r);
    assert(!strcmp(cJSON_GetObjectItem(r,"method")->valuestring,"kb.cs.show"));
    cJSON *p=cJSON_GetObjectItem(r,"params");assert(cJSON_GetObjectItem(p,"profile")->valueint==0 && cJSON_GetObjectItem(p,"layer")->valueint==2);cJSON_Delete(r);
    assert(internal_action("KI_CS_SHOW",false) && rpc_tx.count==1);
    assert(internal_action("KI_CS_SHOW_TMP",true));assert(internal_action("KI_CS_SHOW_TMP",false));
    r=notice(2);assert(!strcmp(cJSON_GetObjectItem(r,"method")->valuestring,"kb.cs.hide"));cJSON_Delete(r);
    rpc_tx.count=RPC_TX_SLOTS-1;assert(!internal_action("KI_CS_TOGGLE",true));rpc_tx_reset(&rpc_tx);
    selected_ble=true;assert(internal_action("KI_CS_TOGGLE",true) && unavailable_outputs==1 && !rpc_tx.count);selected_ble=false;
    ble_busy=true;assert(!internal_action("KI_CBTP3",true) && !prefer_ble);
    ble_busy=false;assert(internal_action("KI_CBTP3",true) && !prefer_ble && pending_ble_preference==42 && last_ble_kind==BLE_SLOT_PAIR && last_ble_slot==2);
    ble.last_command=41;transport_selection_step(true,&ble);assert(!prefer_ble && pending_ble_preference==42);
    ble.last_command=42;ble.slot=2;neutral_blocked=true;transport_selection_step(true,&ble);
    assert(!prefer_ble && handover_released && !handover_neutral && stick_count==1);
    neutral_blocked=false;
    for(unsigned i=0;i<3;++i)transport_selection_step(true,&ble);
    assert(!prefer_ble && neutral_count==3 && rpc_tx.count==1 && stick_count==1);
    rpc_tx_reset(&rpc_tx);transport_selection_step(true,&ble);assert(prefer_ble && transport_request<0);
    selected_ble=true;assert(internal_action("KI_CUSB",true) && prefer_ble && transport_request==0);
    transport_selection_step(false,&ble);assert(!prefer_ble && transport_request<0);selected_ble=false;
    assert(internal_action("KI_CBT2",true) && pending_ble_preference==42 && last_ble_kind==BLE_SLOT_SELECT && last_ble_slot==1);
    ble.slot=1;ble.error=6;input_resync=false;transport_selection_step(true,&ble);
    assert(!prefer_ble && !pending_ble_preference && input_resync);
    for(unsigned i=0;i<20;++i)assert(internal_action("KI_BLDW",true));assert(brightness_scale==0);
    for(unsigned i=0;i<20;++i)assert(internal_action("KI_BLUP",true));assert(brightness_scale==1);
    radial_runtime_reset(&radial);radial.held=radial.open=true;strcpy(radial.captured,"KC_A");
    assert(internal_action("KI_X",true) && releases==1 && !radial.held && radial.closed_until_neutral);
    assert(!keymap_token_supported("KI_FP"));
    puts("Actual internal action dispatch: sparse layer indices, temporary restoration, profiles, CS RPC/temporary hide/backpressure, BLE selection/pairing, USB preference, brightness caps and radial close passed");
}
