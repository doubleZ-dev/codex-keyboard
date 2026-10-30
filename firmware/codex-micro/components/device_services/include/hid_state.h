#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "key_codes.h"
typedef bool (*hid_submit_fn)(uint8_t id,const uint8_t *report,unsigned length,void *context);
typedef struct { uint8_t usages[256]; uint8_t consumer[KEY_CONSUMER_COUNT],system[3]; } hid_state_t;
void hid_state_reset(hid_state_t *s);
bool hid_state_key(hid_state_t *s,const char *token,bool down,hid_submit_fn submit,void *ctx);
bool hid_state_release(hid_state_t *s,hid_submit_fn submit,void *ctx);
