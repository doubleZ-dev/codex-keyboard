#include "ble_slot_policy.h"
#include <string.h>
bool ble_slot_allow(unsigned selected,const uint8_t states[3],const uint8_t peers[3][7],const uint8_t peer[7],bool pairing)
{
    if (selected>=3) return false;
    if (states[selected]==1) return !memcmp(peers[selected],peer,7);
    if (states[selected]!=0 || !pairing) return false;
    for (unsigned i=0;i<3;++i) if (states[i] && !memcmp(peers[i],peer,7)) return false;
    return true;
}
