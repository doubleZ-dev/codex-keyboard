idf_component_register(SRCS "usb_microphone.c" INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_driver_i2s fw_foundation espressif__esp_tinyusb)
