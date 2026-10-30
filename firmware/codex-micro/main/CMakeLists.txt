idf_component_register(
    SRCS "codex_micro_usb.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES json esp_tinyusb fw_foundation usb_rpc esp_timer board_io device_services config_store firmware_update ble_transport usb_microphone
    )
