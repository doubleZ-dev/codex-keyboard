#pragma once
#include "keymap_model.h"
#define SMART_BYTES 32768
#define SMART_NOTICE_BYTES 6144
#define SMART_DEFAULT "{\"version\":1,\"smartActions\":{},\"smartActionGroups\":[]}"
bool smart_actions_validate(const char *json);
bool smart_actions_references(const keymap_model_t *m,const char *json);
/* New owned notification or NULL. Caller deletes. No command executes on MCU. */
cJSON *smart_action_notification(const char *json,const char *token);
