#pragma once
#include <stdint.h>
#include <stdbool.h>
/* Identity byte0=address type, bytes1..6=address; state 0 empty,1 bound,2 deleting. */
bool ble_slot_allow(unsigned selected,const uint8_t states[3],const uint8_t peers[3][7],const uint8_t peer[7],bool pairing);
