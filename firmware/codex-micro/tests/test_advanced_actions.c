#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "action_runtime.h"
#include "hid_state.h"
#include "smart_actions.h"
#include "ble_slot_policy.h"
#include "../main/keymap_default.h"
static keymap_model_t model;
static action_runtime_t runtime;
static unsigned presses,releases; static bool blocked;
static char last[24];
static unsigned last_agent;
static hid_state_t keyboard;
static uint8_t keyboard_report[8];
static bool submit_hid(uint8_t id,const uint8_t *bytes,unsigned length,void *ctx)
{ (void)ctx;assert(id==1 && length==8);if(blocked)return false;memcpy(keyboard_report,bytes,length);return true; }
static bool output_hid(const char *token,bool down,unsigned agent,void *ctx)
{ (void)agent;(void)ctx;return hid_state_key(&keyboard,token,down,submit_hid,NULL); }
static bool output(const char *t,bool down,unsigned agent,void *ctx)
{ (void)ctx;if (blocked) return false;last_agent=agent;strcpy(last,t);if(down)++presses;else++releases;return true; }
static bool event(const char *t,bool down,uint64_t at)
{ return action_runtime_event(&runtime,&model,t,down,0,at,output,NULL); }
static void tick(uint64_t at) { action_runtime_tick(&runtime,&model,at,output,NULL); }
static void reset(void) { action_runtime_reset(&runtime);presses=releases=0;blocked=false;last[0]=0; }
static cJSON *make_config(void)
{
    cJSON *root=cJSON_Parse(KEYMAP_JSON);
    cJSON_ReplaceItemInObject(root,"macros",cJSON_Parse("[{\"id\":4,\"actions\":[{\"kc\":\"KC_LCTL\",\"act\":1,\"delay\":5},{\"kc\":\"KC_A\",\"act\":2,\"delay\":0},{\"kc\":\"KC_LCTL\",\"act\":0,\"delay\":0}]}]"));
    cJSON_ReplaceItemInObject(root,"multiActions",cJSON_Parse("[{\"id\":9,\"tt\":250,\"kcOnTap\":\"KC_A\",\"kcOnDoubleTap\":\"KC_B\",\"kcOnHold\":\"KC_C\",\"kcOnTapHold\":\"KC_D\"}]"));
    cJSON_ReplaceItemInObject(root,"linkedApps",cJSON_Parse("[{\"id\":8,\"name\":\"Editor\",\"process\":\"Code.exe\",\"path\":\"\"}]"));
    cJSON *profile=cJSON_GetArrayItem(cJSON_GetObjectItem(root,"profiles"),0);
    cJSON *layers=cJSON_GetObjectItem(profile,"layers"),*l=cJSON_Duplicate(cJSON_GetArrayItem(layers,0),true);
    cJSON_ReplaceItemInObject(l,"id",cJSON_CreateNumber(1)); cJSON_AddNumberToObject(l,"linkedAppId",8); cJSON_AddItemToArray(layers,l);
    return root;
}
int main(void)
{
    cJSON *root=make_config(); assert(keymap_compile(root,&model));
    reset();hid_state_reset(&keyboard);
    assert(hid_state_key(&keyboard,"KC_LCTL",true,submit_hid,NULL));
    for(unsigned i=0;i<ACTION_CONCURRENT;++i)assert(event("KA_A4",true,0));
    assert(!event("KA_A4",true,0));
    action_runtime_tick(&runtime,&model,0,output_hid,NULL);assert(keyboard.usages[224]==ACTION_CONCURRENT+1);
    blocked=true;assert(!action_runtime_cancel(&runtime,&model,output_hid,NULL));assert(keyboard.usages[224]==ACTION_CONCURRENT+1);
    blocked=false;assert(action_runtime_cancel(&runtime,&model,output_hid,NULL));assert(keyboard.usages[224]==1 && keyboard_report[0]==1);
    assert(hid_state_key(&keyboard,"KC_LCTL",false,submit_hid,NULL));assert(!keyboard_report[0]);
    reset();assert(event("KA_A4",true,0));tick(0);assert(presses==1 && !strcmp(last,"KC_LCTL"));
    tick(4999);assert(presses==1);blocked=true;tick(5000);assert(presses==1);blocked=false;
    tick(5001);assert(presses==2);tick(5002);assert(releases==1);tick(5003);assert(releases==2);tick(5004);assert(!action_runtime_busy(&runtime));
    reset();assert(event("KA_A4",true,0));tick(0);blocked=true;assert(!action_runtime_cancel(&runtime,&model,output,NULL));
    blocked=false;assert(action_runtime_cancel(&runtime,&model,output,NULL));assert(presses==1 && releases==1);
    reset();assert(event("KA_M9",true,0));assert(event("KA_M9",false,10000));tick(259999);assert(!presses);
    tick(260000);assert(presses==1 && !strcmp(last,"KC_A"));blocked=true;tick(260001);assert(!releases);blocked=false;tick(260002);assert(releases==1);
    reset();assert(event("KA_M9",true,0));assert(event("KA_M9",false,10000));assert(event("KA_M9",true,100000));assert(event("KA_M9",false,110000));tick(110001);assert(!strcmp(last,"KC_B") && presses==1);tick(110002);assert(releases==1);
    reset();assert(event("KA_M9",true,0));tick(250000);assert(presses==1 && !strcmp(last,"KC_C"));assert(event("KA_M9",false,300000));tick(300001);assert(releases==1);
    reset();assert(event("KA_M9",true,0));assert(event("KA_M9",false,10000));assert(event("KA_M9",true,100000));tick(350000);assert(presses==1 && !strcmp(last,"KC_D"));
    assert(action_runtime_cancel(&runtime,&model,output,NULL));assert(releases==1);
    /* Delayed loop sees release after hold deadline: cannot misclassify as tap. */
    reset();assert(event("KA_M9",true,0));assert(event("KA_M9",false,300000));tick(300001);assert(presses==1 && releases==1 && !strcmp(last,"KC_C"));
    /* Two physical controls sharing a multi action release only on last release. */
    reset();assert(event("KA_M9",true,0));assert(event("KA_M9",true,100));tick(250000);
    assert(event("KA_M9",false,300000));tick(300001);assert(!releases);assert(event("KA_M9",false,310000));tick(310001);assert(releases==1);
    unsigned p=0,l=0; assert(keymap_focus(&model,"Code.exe",NULL,&p,&l) && p==0 && l==1);
    assert(!keymap_focus(&model,"Other.exe",NULL,&p,&l));
    assert(keymap_focus(&model,"code.EXE",NULL,&p,&l) && l==1);
    strcpy(model.apps[0].path,"C:\\Program Files\\Editor\\Code.exe");
    assert(keymap_focus(&model,"CODE.EXE","c:/program files/editor/code.exe",&p,&l));
    assert(!keymap_focus(&model,"CODE.EXE","C:/Other/Code.exe",&p,&l));model.apps[0].path[0]=0;
    cJSON *macro=cJSON_GetArrayItem(cJSON_GetObjectItem(root,"macros"),0),*step=cJSON_GetArrayItem(cJSON_GetObjectItem(macro,"actions"),0);
    cJSON_ReplaceItemInObject(step,"kc",cJSON_CreateString("KA_A4"));assert(!keymap_compile(root,&model));cJSON_Delete(root);
    root=make_config();assert(keymap_compile(root,&model));cJSON_Delete(root);
    const char *smart="{\"version\":1,\"smartActions\":{\"SA_7\":{\"type\":\"TEXT_STEP\",\"payload\":{\"text\":\"hello\"}}}}";
    assert(smart_actions_validate(smart));strcpy(model.profiles[0].layer[0].tokens[0],"SA_7");
    assert(smart_actions_references(&model,smart));assert(!smart_actions_references(&model,SMART_DEFAULT));
    cJSON *notice=smart_action_notification(smart,"SA_7");assert(notice);
    assert(!strcmp(cJSON_GetObjectItem(notice,"method")->valuestring,"kb.sa.inserttext"));cJSON_Delete(notice);
    assert(!smart_action_notification(smart,"SA_8"));
    const char *types[]={"TEXT_STEP","CMD_STEP","APP_STEP","URL_STEP"};
    const char *fields[]={"text","cmd","path","url"};
    const char *methods[]={"kb.sa.inserttext","kb.sa.exec","kb.sa.openapp","kb.sa.openurl"};
    for(unsigned i=0;i<4;++i) {
        char json[256];snprintf(json,sizeof(json),"{\"version\":1,\"smartActions\":{\"SA_2\":{\"type\":\"%s\",\"payload\":{\"%s\":\"test\"}}}}",types[i],fields[i]);
        assert(smart_actions_validate(json));notice=smart_action_notification(json,"SA_2");assert(notice);
        assert(!strcmp(cJSON_GetObjectItem(notice,"method")->valuestring,methods[i]));
        assert(!strcmp(cJSON_GetObjectItem(cJSON_GetObjectItem(notice,"params"),fields[i])->valuestring,"test"));cJSON_Delete(notice);
    }
    uint8_t states[3]={1,0,2},peers[3][7]={{0,1},{0,2},{0,3}},peer[7]={0,1};
    assert(ble_slot_allow(0,states,peers,peer,false));assert(!ble_slot_allow(1,states,peers,peer,true));
    peer[1]=4;assert(!ble_slot_allow(0,states,peers,peer,true));assert(!ble_slot_allow(1,states,peers,peer,false));
    assert(ble_slot_allow(1,states,peers,peer,true));assert(!ble_slot_allow(2,states,peers,peer,true));
    char keyfile[KEYMAP_BYTES+1],smartfile[SMART_BYTES+1];
    FILE *f=fopen("tests/fixtures/advanced_keymap.json","rb");assert(f);size_t n=fread(keyfile,1,KEYMAP_BYTES,f);fclose(f);keyfile[n]=0;
    f=fopen("tests/fixtures/advanced_smart_actions.json","rb");assert(f);n=fread(smartfile,1,SMART_BYTES,f);fclose(f);smartfile[n]=0;
    root=cJSON_Parse(keyfile);assert(keymap_compile(root,&model));assert(smart_actions_validate(smartfile) && smart_actions_references(&model,smartfile));
    cJSON *layers=cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(root,"profiles"),0),"layers");
    for (unsigned i=2;i<6;++i) { cJSON *layer=cJSON_Duplicate(cJSON_GetArrayItem(layers,0),true);cJSON_ReplaceItemInObject(layer,"id",cJSON_CreateNumber(i));cJSON_AddItemToArray(layers,layer); }
    char *six=cJSON_PrintUnformatted(root);assert(strlen(six)<=KEYMAP_BYTES && keymap_compile(root,&model));cJSON_free(six);cJSON_Delete(root);
    reset();assert(event("KA_M0",true,0));assert(event("KA_M0",false,1000));assert(event("KA_M0",true,2000));tick(252000);
    tick(252001);assert(presses==1 && !strcmp(last,"KC_H"));assert(action_runtime_cancel(&runtime,&model,output,NULL));assert(releases==1);
    memset(&model,0,sizeof(model));model.macro_count=1;model.macros[0]=(macro_def_t){.id=33,.count=4};
    strcpy(model.steps[0].token,"KV_OAI_ACT06");model.steps[0].act=1;
    strcpy(model.steps[1].token,"KV_OAI_AG02");model.steps[1].act=2;
    strcpy(model.steps[2].token,"KV_OAI_ACT06");model.steps[2].act=0;
    strcpy(model.steps[3].token,"KV_OAI_ACT07");model.steps[3].act=2;
    reset();assert(event("KA_A33",true,0));tick(0);assert(last_agent==0);
    tick(1);assert(last_agent==2);tick(2);assert(last_agent==2);
    tick(3);assert(last_agent==0 && !strcmp(last,"KV_OAI_ACT06"));
    tick(4);assert(last_agent==2);tick(5);tick(6);assert(presses==3 && releases==3 && !action_runtime_busy(&runtime));
    puts("Advanced actions: macro timing/cancel/captured Agent release, four gestures/shared holds, AppSense, Smart Actions and BLE slot isolation passed");
}
