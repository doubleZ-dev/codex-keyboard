idf_component_register(SRCS "update_engine.c" "firmware_update.c" INCLUDE_DIRS "include"
    REQUIRES json app_update esp_timer mbedtls)
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u codex_image_contract")
