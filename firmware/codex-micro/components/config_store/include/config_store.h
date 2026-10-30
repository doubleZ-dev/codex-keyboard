#pragma once
#include "keymap_model.h"
#include "esp_err.h"
/* Init once before USB. Thereafter all API calls except worker are protocol-task only. */
esp_err_t config_store_init(const char *defaults);
const char *config_store_json(void);
const char *config_store_smart(void);
const keymap_model_t *config_store_model(void);
uint32_t config_store_generation(void);
bool config_store_writable(void);
bool config_store_busy(void);
/* Copies candidate; validates before worker writes alternate NVS bank. */
esp_err_t config_store_submit(const char *json);
esp_err_t config_store_submit_files(const char *keymap,const char *smart);
/* Nonblocking; returns true once a completion is available and runtime published. */
bool config_store_poll(esp_err_t *result);
