idf_component_register(SRCS "ble_transport.c" "ble_slot_policy.c" INCLUDE_DIRS "include"
    REQUIRES bt nvs_flash esp_timer)
