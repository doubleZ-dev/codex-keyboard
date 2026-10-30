#include "layer_navigation.h"
#include <string.h>
void layer_navigation_reset(layer_navigation_t *s,unsigned base)
{ memset(s,0,sizeof(*s)); s->base=base<6?base:0; }
unsigned layer_navigation_current(const layer_navigation_t *s)
{ return s->count?s->order[s->count-1]:s->base; }
bool layer_navigation_select(layer_navigation_t *s,unsigned layer)
{ if (layer>=6) return false; s->base=layer; return true; }
bool layer_navigation_hold(layer_navigation_t *s,unsigned layer,bool down)
{
    if (layer>=6) return false;
    if (down) {
        if (s->refs[layer]==255) return false;
        if (s->refs[layer]++) {
            unsigned i=0; while (i<s->count && s->order[i]!=layer) ++i;
            if (i<s->count) { memmove(s->order+i,s->order+i+1,s->count-i-1); --s->count; }
        }
        s->order[s->count++]=layer;
    } else if (s->refs[layer] && !--s->refs[layer]) {
        unsigned i=0; while (i<s->count && s->order[i]!=layer) ++i;
        if (i<s->count) { memmove(s->order+i,s->order+i+1,s->count-i-1); --s->count; }
    }
    return true;
}
