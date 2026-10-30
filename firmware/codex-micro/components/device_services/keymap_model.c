#include "keymap_model.h"
#include "key_codes.h"
#include <string.h>
#include <math.h>
int action_token_id(const char *t,const char *prefix)
{
    size_t n=strlen(prefix); if (strncmp(t,prefix,n) || !t[n]) return -1;
    if (t[n]=='0' && t[n+1]) return -1;
    unsigned value=0;
    for (t+=n;*t;++t) { if (*t<'0' || *t>'9' || value>6553) return -1; value=value*10+(*t-'0'); if (value>65535) return -1; }
    return (int)value;
}
static bool integer(const cJSON *n,int low,int high)
{ return cJSON_IsNumber(n) && isfinite(n->valuedouble) && n->valuedouble>=low && n->valuedouble<=high && n->valuedouble==n->valueint; }
bool keymap_internal_token(const char *t)
{
    if (!t) return false;
    /* KI_FP is only labelled in Input; its device semantics are not published.
     * Reject it at commit instead of accepting a key that cannot execute. */
    const char *fixed[]={"KI_BLUP","KI_BLDW","KI_CUSB","KI_X",
        "KI_CS_SHOW","KI_CS_SHOW_TMP","KI_CS_HIDE","KI_CS_TOGGLE"};
    for (unsigned i=0;i<sizeof(fixed)/sizeof(fixed[0]);++i) if (!strcmp(t,fixed[i])) return true;
    const char *prefix[]={"KI_LS","KI_LM","KI_PS","KI_CBT","KI_CBTP"};
    for (unsigned i=0;i<5;++i) {
        int id=action_token_id(t,prefix[i]);
        if (id>=1 && id<=(i<3?6:3)) return true;
    }
    return false;
}
bool keymap_token_supported(const char *t)
{
    if (!t) return false;
    if (keymap_internal_token(t)) return true;
    if (action_token_id(t,"KA_A")>=0 || action_token_id(t,"KA_M")>=0 || action_token_id(t,"SA_")>=0) return true;
    if (!strncmp(t,"KV_OAI_",7)) {
        t+=7;
        if (strlen(t)==4 && !strncmp(t,"AG0",3) && t[3]>='0' && t[3]<='5') return true;
        if (strlen(t)==5 && !strncmp(t,"ACT",3) &&
            ((!strncmp(t+3,"0",1) && t[4]>='6' && t[4]<='9') ||
             (t[3]=='1' && t[4]>='0' && t[4]<='2'))) return true;
        return !strcmp(t,"ENC_CLK") || !strcmp(t,"ENC_CW") || !strcmp(t,"ENC_CC");
    }
    uint8_t report; uint16_t usage;
    return key_code_lookup(t,&report,&usage);
}
static bool token_copy(char out[KEYMAP_TOKEN],const cJSON *n)
{
    if (!cJSON_IsString(n) || strlen(n->valuestring)>=KEYMAP_TOKEN || !keymap_token_supported(n->valuestring)) return false;
    strcpy(out,n->valuestring); return true;
}
static bool empty_optional(const cJSON *obj,const char *name)
{
    const cJSON *n=cJSON_GetObjectItemCaseSensitive(obj,name);
    return !n || (cJSON_IsArray(n) && cJSON_GetArraySize(n)==0);
}
static bool angle_value(const cJSON *v)
{ return cJSON_IsNumber(v) && isfinite(v->valuedouble) && v->valuedouble>=0 && v->valuedouble<=1; }
int keymap_sector(const keymap_layer_t *layer,float angle)
{
    if (!layer || !isfinite(angle)) return -1;
    angle-=floorf(angle);
    for (unsigned i=0;i<layer->sector_count;++i) {
        const radial_sector_t *s=&layer->sectors[i];
        if (s->start<s->end?(angle>=s->start && angle<s->end):(angle>=s->start || angle<s->end)) return (int)i;
    }
    return -1;
}
int keymap_layer_position(const keymap_profile_t *profile,unsigned id)
{
    if (!profile) return -1;
    for (unsigned i=0;i<profile->layers;++i) if (profile->layer_order[i]==id) return (int)i;
    return -1;
}
int keymap_layer_at(const keymap_profile_t *profile,unsigned position)
{ return profile && position<profile->layers?profile->layer_order[position]:-1; }
static bool sectors_compile(const cJSON *json,keymap_layer_t *layer)
{
    if (!cJSON_IsArray(json) || cJSON_GetArraySize(json)<2 || cJSON_GetArraySize(json)>KEYMAP_SECTORS) return false;
    const cJSON *item;
    cJSON_ArrayForEach(item,json) {
        const cJSON *a=cJSON_GetObjectItemCaseSensitive(item,"a1"),*b=cJSON_GetObjectItemCaseSensitive(item,"a2");
        radial_sector_t *s=&layer->sectors[layer->sector_count];
        if (!angle_value(a) || !angle_value(b) || a->valuedouble==b->valuedouble ||
            (a->valuedouble==1 && b->valuedouble==0) ||
            !token_copy(s->token,cJSON_GetObjectItemCaseSensitive(item,"k"))) return false;
        s->start=a->valuedouble; s->end=b->valuedouble;
        /* Each circular sector is at most two linear intervals. Reject
         * overlaps; touching boundaries and deliberate gaps are permitted. */
        float left[2]={s->start,0},right[2]={s->end,0};
        unsigned count=1;
        if (s->start>s->end) { right[0]=1; right[1]=s->end; count=2; }
        for (unsigned prior=0;prior<layer->sector_count;++prior) {
            const radial_sector_t *p=&layer->sectors[prior];
            float pl[2]={p->start,0},pr[2]={p->end,0}; unsigned pc=1;
            if (p->start>p->end) { pr[0]=1; pr[1]=p->end; pc=2; }
            for (unsigned i=0;i<count;++i) for (unsigned j=0;j<pc;++j)
                if (fminf(right[i],pr[j])-fmaxf(left[i],pl[j])>0.000001f) return false;
        }
        ++layer->sector_count;
    }
    return true;
}
const macro_def_t *keymap_macro(const keymap_model_t *m,unsigned id)
{ for (unsigned i=0;i<m->macro_count;++i) if (m->macros[i].id==id) return &m->macros[i]; return NULL; }
const multi_def_t *keymap_multi(const keymap_model_t *m,unsigned id)
{ for (unsigned i=0;i<m->multi_count;++i) if (m->multis[i].id==id) return &m->multis[i]; return NULL; }
static bool leaf(const char *t)
{ return keymap_token_supported(t) && strncmp(t,"KA_",3)!=0; }
static bool reference(const keymap_model_t *m,const char *t)
{
    int id=action_token_id(t,"KA_A"); if (id>=0) return keymap_macro(m,id)!=NULL;
    id=action_token_id(t,"KA_M"); if (id>=0) return keymap_multi(m,id)!=NULL;
    return keymap_token_supported(t);
}
static bool metadata_array(const cJSON *p,const char *key)
{ const cJSON *v=cJSON_GetObjectItemCaseSensitive(p,key); return !v || cJSON_IsArray(v); }
static bool compile_actions(const cJSON *json,keymap_model_t *out)
{
    const cJSON *list=cJSON_GetObjectItemCaseSensitive(json,"macros"),*item;
    if (list && !cJSON_IsArray(list)) return false;
    cJSON_ArrayForEach(item,list) {
        const cJSON *id=cJSON_GetObjectItemCaseSensitive(item,"id"),*steps=cJSON_GetObjectItemCaseSensitive(item,"actions"),*step;
        if (!integer(id,0,65535) || keymap_macro(out,id->valueint) || out->macro_count==ACTION_MACROS || !cJSON_IsArray(steps)) return false;
        macro_def_t *m=&out->macros[out->macro_count++]; m->id=id->valueint; m->first=out->step_count;
        unsigned duration=0;
        char held[16][KEYMAP_TOKEN]={{0}}; unsigned held_count[16]={0};
        cJSON_ArrayForEach(step,steps) {
            if (out->step_count==ACTION_STEPS) return false;
            macro_step_t *s=&out->steps[out->step_count++];
            const cJSON *act=cJSON_GetObjectItemCaseSensitive(step,"act"),*delay=cJSON_GetObjectItemCaseSensitive(step,"delay");
            if (!token_copy(s->token,cJSON_GetObjectItemCaseSensitive(step,"kc")) || !leaf(s->token) ||
                !integer(act,0,2) || !integer(delay,0,10000)) return false;
            s->act=act->valueint; s->delay_ms=delay->valueint; duration+=s->delay_ms; ++m->count;
            if (duration>60000) return false;
            int found=-1,empty=-1;
            for (unsigned i=0;i<16;++i) { if (held_count[i] && !strcmp(held[i],s->token)) found=i; if (!held_count[i]) empty=i; }
            if (s->act==1) {
                if (found<0) found=empty;
                if (found<0) return false;
                strcpy(held[found],s->token); ++held_count[found];
            } else if (s->act==0 && found>=0) --held_count[found];
            else if (s->act==2 && found<0 && empty<0) return false;
            unsigned held_total=0; for (unsigned i=0;i<16;++i) held_total+=held_count[i];
            if (held_total>16 || (s->act==2 && held_total==16)) return false;
        }
    }
    list=cJSON_GetObjectItemCaseSensitive(json,"multiActions");
    if (list && !cJSON_IsArray(list)) return false;
    cJSON_ArrayForEach(item,list) {
        const cJSON *id=cJSON_GetObjectItemCaseSensitive(item,"id"),*term=cJSON_GetObjectItemCaseSensitive(item,"tt");
        if (!integer(id,0,65535) || keymap_multi(out,id->valueint) || out->multi_count==ACTION_MULTIS || !integer(term,1,60000)) return false;
        multi_def_t *m=&out->multis[out->multi_count++]; m->id=id->valueint; m->term_ms=term->valueint;
        const char *names[]={"kcOnTap","kcOnDoubleTap","kcOnHold","kcOnTapHold"};
        for (unsigned i=0;i<4;++i) {
            if (!token_copy(m->branch[i],cJSON_GetObjectItemCaseSensitive(item,names[i]))) return false;
            if (!leaf(m->branch[i])) {
                int macro=action_token_id(m->branch[i],"KA_A");
                if (macro<0 || !keymap_macro(out,macro)) return false;
            }
        }
    }
    list=cJSON_GetObjectItemCaseSensitive(json,"linkedApps");
    if (list && !cJSON_IsArray(list)) return false;
    cJSON_ArrayForEach(item,list) {
        const cJSON *id=cJSON_GetObjectItemCaseSensitive(item,"id"),*process=cJSON_GetObjectItemCaseSensitive(item,"process"),*path=cJSON_GetObjectItemCaseSensitive(item,"path");
        if (!integer(id,0,65535) || out->app_count==ACTION_APPS || !cJSON_IsString(process) || !cJSON_IsString(path)) return false;
        for (unsigned i=0;i<out->app_count;++i) if (out->apps[i].id==id->valueint) return false;
        linked_app_t *a=&out->apps[out->app_count++]; a->id=id->valueint;
        if (strlen(process->valuestring)>=sizeof(a->process) || strlen(path->valuestring)>=sizeof(a->path) || (!*process->valuestring && !*path->valuestring)) return false;
        strcpy(a->process,process->valuestring); strcpy(a->path,path->valuestring);
    }
    return metadata_array(json,"macrosGroups") && metadata_array(json,"multiActionsGroups");
}
bool keymap_compile(const cJSON *json,keymap_model_t *out)
{
    if (!json || !out || !cJSON_IsObject(json)) return false;
    const cJSON *version=cJSON_GetObjectItemCaseSensitive(json,"version");
    const cJSON *active=cJSON_GetObjectItemCaseSensitive(json,"activeProfileId");
    const cJSON *profiles=cJSON_GetObjectItemCaseSensitive(json,"profiles");
    if (!integer(version,1,1) || !integer(active,0,65535) || !cJSON_IsArray(profiles)) return false;
    int count=cJSON_GetArraySize(profiles);
    if (count<1 || count>KEYMAP_PROFILES) return false;
    memset(out,0,sizeof(*out)); out->count=count; out->active=255;
    if (!compile_actions(json,out)) return false;
    for (int p=0;p<count;++p) {
        const cJSON *profile=cJSON_GetArrayItem(profiles,p);
        const cJSON *id=cJSON_GetObjectItemCaseSensitive(profile,"id");
        const cJSON *layers=cJSON_GetObjectItemCaseSensitive(profile,"layers");
        if (!integer(id,0,65535) || !cJSON_IsArray(layers) || cJSON_GetArraySize(layers)<1 ||
            cJSON_GetArraySize(layers)>KEYMAP_LAYERS || !metadata_array(profile,"macrosUsed") ||
            !metadata_array(profile,"multiActionsUsed")) return false;
        keymap_profile_t *dest=&out->profiles[p]; dest->id=id->valueint;
        for (int prior=0;prior<p;++prior) if (out->profiles[prior].id==dest->id) return false;
        if (id->valueint==active->valueint) out->active=p;
        const cJSON *layer;
        cJSON_ArrayForEach(layer,layers) {
            const cJSON *lid=cJSON_GetObjectItemCaseSensitive(layer,"id");
            if (!integer(lid,0,5) || dest->layer[lid->valueint].present) return false;
            keymap_layer_t *l=&dest->layer[lid->valueint]; l->present=true;
            const cJSON *lights=cJSON_GetObjectItemCaseSensitive(layer,"lights");
            if (lights && !cJSON_IsNull(lights)) {
                if (!cJSON_IsObject(lights) ||
                    !light_style_from_config(&l->backlight,cJSON_GetObjectItemCaseSensitive(lights,"backlight")) ||
                    !light_style_from_config(&l->underglow,cJSON_GetObjectItemCaseSensitive(lights,"underglow"))) return false;
                l->has_lights=true;
            }
            dest->layer_order[dest->layers++]=lid->valueint;
            l->linked_app=-1;
            const cJSON *link=cJSON_GetObjectItemCaseSensitive(layer,"linkedAppId");
            if (link && !cJSON_IsNull(link)) {
                if (!integer(link,0,65535)) return false;
                bool found=false;
                for (unsigned i=0;i<out->app_count;++i) if (out->apps[i].id==link->valueint) found=true;
                if (!found) return false;
                l->linked_app=link->valueint;
            }
            for (unsigned i=0;i<KEYMAP_BINDINGS;++i) strcpy(l->tokens[i],"KC_NO");
            const cJSON *layout=cJSON_GetObjectItemCaseSensitive(layer,"layout");
            const cJSON *matrix=cJSON_GetObjectItemCaseSensitive(layout,"keymap");
            if (!cJSON_IsArray(matrix) || cJSON_GetArraySize(matrix)!=4) return false;
            const unsigned widths[]={2,4,4,3},starts[]={0,4,8,13};
            for (unsigned row=0;row<4;++row) {
                const cJSON *r=cJSON_GetArrayItem(matrix,row);
                if (!cJSON_IsArray(r) || cJSON_GetArraySize(r)!=(int)widths[row]) return false;
                for (unsigned c=0;c<widths[row];++c)
                    if (!token_copy(l->tokens[starts[row]+c],cJSON_GetArrayItem(r,c))) return false;
            }
            const cJSON *enc=cJSON_GetObjectItemCaseSensitive(layout,"encoders");
            if (!cJSON_IsArray(enc) || cJSON_GetArraySize(enc)!=1) return false;
            enc=cJSON_GetArrayItem(enc,0);
            if (!cJSON_IsArray(enc) || cJSON_GetArraySize(enc)!=3 ||
                !token_copy(l->tokens[18],cJSON_GetArrayItem(enc,0)) ||
                !token_copy(l->tokens[19],cJSON_GetArrayItem(enc,1)) ||
                !token_copy(l->tokens[16],cJSON_GetArrayItem(enc,2))) return false;
            const cJSON *buttons=cJSON_GetObjectItemCaseSensitive(layout,"buttons");
            if (buttons) {
                if (!cJSON_IsArray(buttons) || cJSON_GetArraySize(buttons)!=1) return false;
                buttons=cJSON_GetArrayItem(buttons,0);
                if (!cJSON_IsArray(buttons) || cJSON_GetArraySize(buttons)!=1 ||
                    !token_copy(l->tokens[17],cJSON_GetArrayItem(buttons,0))) return false;
            }
            const cJSON *joystick=cJSON_GetObjectItemCaseSensitive(layout,"joystick");
            if (joystick) {
                const cJSON *type=cJSON_GetObjectItemCaseSensitive(joystick,"type");
                if (!cJSON_IsString(type)) return false;
                if (!strcmp(type->valuestring,"VENDOR")) {
                    if (!empty_optional(joystick,"sectors")) return false;
                } else if (!strcmp(type->valuestring,"RADIAL")) {
                    l->joystick_mode=JOYSTICK_RADIAL;
                    if (!sectors_compile(cJSON_GetObjectItemCaseSensitive(joystick,"sectors"),l)) return false;
                } else return false;
            }
            for (unsigned i=0;i<KEYMAP_BINDINGS;++i) if (!reference(out,l->tokens[i])) return false;
            for (unsigned i=0;i<l->sector_count;++i) if (!reference(out,l->sectors[i].token)) return false;
        }
        if (!dest->layer[0].present) return false;
    }
    return out->active!=255;
}
static bool windows_identity(const char *s)
{
    if (!s) return false;
    size_t n=strlen(s);
    if (n>=2 && s[1]==':') return true;
    if (strchr(s,'\\')) return true;
    return n>=4 && s[n-4]=='.' && (s[n-3]=='e'||s[n-3]=='E') &&
        (s[n-2]=='x'||s[n-2]=='X') && (s[n-1]=='e'||s[n-1]=='E');
}
static bool identity_equal(const char *a,const char *b)
{
    if (!a || !b) return false;
    bool windows=windows_identity(a) || windows_identity(b);
    while (*a && *b) {
        unsigned char x=*a++,y=*b++;
        if (windows) {
            if (x=='\\') x='/';
            if (y=='\\') y='/';
            if (x>='A' && x<='Z') x+='a'-'A';
            if (y>='A' && y<='Z') y+='a'-'A';
        }
        if (x!=y) return false;
    }
    return !*a && !*b;
}
bool keymap_focus(const keymap_model_t *m,const char *process,const char *path,unsigned *profile,unsigned *layer)
{
    /* Project policy: exact nonempty identity; both configured fields must match.
     * Prefer current profile, then stable profile/layer order. No name matching. */
    for (unsigned pass=0;pass<=m->count;++pass) {
        unsigned p=pass?pass-1:*profile;
        if (p>=m->count || (pass && p==*profile)) continue;
        for (unsigned l=0;l<6;++l) if (m->profiles[p].layer[l].present) {
            int id=m->profiles[p].layer[l].linked_app;
            for (unsigned i=0;i<m->app_count;++i) {
                const linked_app_t *a=&m->apps[i];
                if (id==a->id && (!a->process[0] || identity_equal(process,a->process)) &&
                    (!a->path[0] || identity_equal(path,a->path))) { *profile=p; *layer=l; return true; }
            }
        }
    }
    return false;
}
const char *keymap_binding(const keymap_model_t *m,unsigned layer,unsigned slot)
{
    if (!m || m->active>=m->count || layer>=6 || slot>=KEYMAP_BINDINGS ||
        !m->profiles[m->active].layer[layer].present) return "KC_NO";
    return m->profiles[m->active].layer[layer].tokens[slot];
}
