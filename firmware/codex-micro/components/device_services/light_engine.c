#include "light_engine.h"
#include <math.h>
#include <string.h>

static bool numeric(const cJSON *n, double min, double max, bool integer)
{
    return cJSON_IsNumber(n) && isfinite(n->valuedouble) && n->valuedouble>=min &&
           n->valuedouble<=max && (!integer || n->valuedouble==floor(n->valuedouble));
}

static bool style_read(light_style_t *style, const cJSON *obj, bool agent)
{
    if (!cJSON_IsObject(obj)) return false;
    unsigned seen=0;
    const char *names[]={"e","b","s","c","m","sk","sa","id"};
    const cJSON *v;
    cJSON_ArrayForEach(v,obj) {
        unsigned i;
        for (i=0;i<8;++i) if (v->string && !strcmp(v->string,names[i])) break;
        if (i==8 || (seen & (1U<<i)) || (i>=5 && !agent)) return false;
        seen |= 1U<<i;
        switch (i) {
        case 0: if (!numeric(v,0,6,true)) return false; style->effect=v->valueint; break;
        case 1: if (!numeric(v,0,1,false)) return false; style->brightness=v->valuedouble; break;
        case 2: if (!numeric(v,0,1,false)) return false; style->speed=v->valuedouble; break;
        case 3: if (!numeric(v,0,0xFFFFFF,true)) return false; style->color=v->valueint; break;
        case 4: if (!numeric(v,0,1,false)) return false; style->magic=v->valuedouble; break;
        case 5: if (!numeric(v,0,1,true)) return false; style->sync_keys=v->valueint; break;
        case 6: if (!numeric(v,0,1,true)) return false; style->sync_ambient=v->valueint; break;
        case 7: if (!numeric(v,0,5,true)) return false; break;
        }
    }
    return seen!=0;
}

bool light_style_from_config(light_style_t *out,const cJSON *obj)
{
    if (!out || !cJSON_IsObject(obj)) return false;
    const cJSON *effect=cJSON_GetObjectItemCaseSensitive(obj,"effect");
    const char *names[]={"off","solid","snake","rainbow","breath","gradient"};
    unsigned index=0;
    if (!cJSON_IsString(effect)) return false;
    for (;index<6;++index) if (!strcmp(effect->valuestring,names[index])) break;
    if (index==6) return false;
    light_style_t next={.effect=index,.color=0xFFFFFF,.brightness=1,.speed=.5f,.magic=1};
    const cJSON *b=cJSON_GetObjectItemCaseSensitive(obj,"brightness"),*s=cJSON_GetObjectItemCaseSensitive(obj,"speed"),
        *m=cJSON_GetObjectItemCaseSensitive(obj,"magic"),*c=cJSON_GetObjectItemCaseSensitive(obj,"color");
    if (!numeric(b,0,1,false) || !numeric(s,0,1,false) || !numeric(m,0,1,false) || (c && !numeric(c,0,0xFFFFFF,true))) return false;
    next.brightness=b->valuedouble; next.speed=s->valuedouble; next.magic=m->valuedouble;
    if (c) next.color=c->valueint;
    *out=next; return true;
}

void light_engine_init(light_engine_t *s)
{
    memset(s,0,sizeof(*s));
    /* Visible project fallback until the current Host sends its own style.
     * An explicit Host/config 'off' still takes precedence. */
    s->keys=(light_style_t){.effect=1,.color=0xFFFFFF,.brightness=0.4f};
    s->ambient=s->keys;
    for (unsigned i=0;i<6;++i) s->agents[i]=s->keys;
}

bool light_engine_apply(light_engine_t *s, const char *method, const cJSON *params)
{
    if (!s || !method) return false;
    light_engine_t next=*s;
    if (!strcmp(method,"v.oai.rgbcfg")) {
        if (!cJSON_IsObject(params) || !params->child) return false;
        unsigned seen=0;
        const cJSON *item;
        cJSON_ArrayForEach(item,params) {
            bool keys=item->string && !strcmp(item->string,"keys");
            if (!keys && (!item->string || strcmp(item->string,"ambient"))) return false;
            unsigned bit=keys?1:2;
            if (seen & bit) return false;
            seen |= bit;
            if (!style_read(keys?&next.keys:&next.ambient,item,false)) return false;
        }
    } else if (!strcmp(method,"v.oai.thstatus")) {
        if (!cJSON_IsArray(params) || cJSON_GetArraySize(params)>6) return false;
        unsigned seen=0;
        const cJSON *item;
        cJSON_ArrayForEach(item,params) {
            const cJSON *id=cJSON_GetObjectItemCaseSensitive(item,"id");
            if (!numeric(id,0,5,true)) return false;
            unsigned bit=1U<<id->valueint;
            if (seen & bit) return false;
            seen |= bit;
            if (!style_read(&next.agents[id->valueint],item,true)) return false;
            next.valid_agents |= bit;
        }
    } else return false;
    ++next.generation;
    *s=next;
    return true;
}

static board_rgb_t hue(float h)
{
    h=(h-floorf(h))*6;
    unsigned sector=(unsigned)h;
    uint8_t up=(uint8_t)((h-sector)*255), down=255-up;
    const board_rgb_t colors[]={{255,up,0},{down,255,0},{0,255,up},
                                {0,down,255},{up,0,255},{255,0,down}};
    return colors[sector%6];
}

static board_rgb_t pixel(const light_style_t *s, uint64_t now, unsigned i, unsigned count)
{
    board_rgb_t rgb={(s->color>>16)&255,(s->color>>8)&255,s->color&255};
    if (!s->effect) return (board_rgb_t){0};
    /* Bounded clock avoids large-float phase precision loss on long uptime. */
    float phase=fmodf((now%3600000000ULL)/1000000.0f*s->speed*2,1);
    float scale=s->brightness;
    switch (s->effect) {
    case 2: scale *= ((unsigned)(phase*count)==i)?1:0.08f; break;
    case 3: rgb=hue(phase+(float)i/count); break;
    case 4: scale *= (1-cosf(phase*6.2831853f))*0.5f; break;
    case 5: rgb=hue((float)i/count*s->magic+phase); break;
    case 6: scale *= 0.75f-0.25f*cosf(phase*6.2831853f); break;
    default: break;
    }
    return (board_rgb_t){rgb.r*scale,rgb.g*scale,rgb.b*scale};
}

void light_engine_render(const light_engine_t *s, uint64_t now, unsigned selected,
                         uint8_t layer, bool low, board_light_frame_t *out)
{
    memset(out,0,sizeof(*out));
    const light_style_t *keys=&s->keys,*ambient=&s->ambient;
    /* Selected agent owns requested zone sync; others retain independent key LEDs. */
    if (selected<6 && (s->valid_agents & (1U<<selected))) {
        if (s->agents[selected].sync_keys) keys=&s->agents[selected];
        if (s->agents[selected].sync_ambient) ambient=&s->agents[selected];
    }
    for (unsigned i=0;i<BOARD_KEY_LED_COUNT;++i) out->keys[i]=pixel(keys,now,i,BOARD_KEY_LED_COUNT);
    for (unsigned i=0;i<BOARD_AMBIENT_LED_COUNT;++i) out->ambient[i]=pixel(ambient,now,i,BOARD_AMBIENT_LED_COUNT);
    /* Compact matrix order -> LED1..LED6; electrical chain fixed by H1.
     * Physical key-to-LED association remains an explicit board acceptance check. */
    for (unsigned i=0;i<6;++i) if (s->valid_agents & (1U<<i)) out->keys[i]=pixel(&s->agents[i],now,0,1);
    out->brightness=64; /* Low battery is an indication only, never power management. */
    out->status_mask=low?(((now/500000)&1)?7:0):((layer+1)&7);
}
