#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Single protocol-task owner. Last pressed temporary layer wins. Permanent
 * selection changes the restoration destination while temporary keys are held. */
typedef struct { uint8_t base,order[6],refs[6],count; } layer_navigation_t;
void layer_navigation_reset(layer_navigation_t *s,unsigned base);
unsigned layer_navigation_current(const layer_navigation_t *s);
bool layer_navigation_select(layer_navigation_t *s,unsigned layer);
bool layer_navigation_hold(layer_navigation_t *s,unsigned layer,bool down);
