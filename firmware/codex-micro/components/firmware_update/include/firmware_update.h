#pragma once
#include "esp_err.h"
#include "cJSON.h"
#include <stdbool.h>
#include <stdint.h>
/* Called only by protocol owner, except init (app_main). No callback borrows JSON. */
esp_err_t firmware_update_init(void);
void firmware_update_session(uint32_t epoch);
bool firmware_update_busy(void);
/* true: service owns response until poll; false: caller sends/frees it. */
bool firmware_update_request(const char *method,const cJSON *params,cJSON *response,uint32_t epoch);
cJSON *firmware_update_poll(uint32_t epoch);
