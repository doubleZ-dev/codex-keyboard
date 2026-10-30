idf_component_register(SRCS "board_inputs.c" "board_lights.c"
    INCLUDE_DIRS "include"
    REQUIRES fw_foundation esp_driver_gpio esp_driver_rmt esp_driver_touch_sens esp_adc esp_timer)
