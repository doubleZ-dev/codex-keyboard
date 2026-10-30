idf_component_register(SRCS "config_store.c" "config_rpc.c" INCLUDE_DIRS "include"
    REQUIRES device_services nvs_flash esp_timer mbedtls)
