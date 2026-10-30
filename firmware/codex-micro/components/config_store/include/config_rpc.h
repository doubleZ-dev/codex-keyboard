#pragma once
#include "cJSON.h"
#include <stdbool.h>
#include <stdint.h>
enum { CONFIG_RPC_UNHANDLED, CONFIG_RPC_HANDLED, CONFIG_RPC_DEFERRED };
/* Protocol-task owner; resp ownership transfers ONLY on DEFERRED. */
int config_rpc_handle(const char *method,const cJSON *params,cJSON *resp,uint32_t epoch);
void config_rpc_session(uint32_t epoch);
/* Called with TX capacity available. Returns owned response (may be NULL on stale
 * session); changed indicates a completed durable configuration publication. */
cJSON *config_rpc_poll(uint32_t epoch,bool *changed);
