cmake_minimum_required(VERSION 3.16)
set(PROJECT_VER "1.0.0-rc.7")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(codex_micro_gate0a)

# Bound recursion for both streamed RPC and decoded configuration files.
# This changes a build option, never the managed SDK source.
idf_component_get_property(codex_json_lib json COMPONENT_LIB)
target_compile_definitions(${codex_json_lib} PRIVATE CJSON_NESTING_LIMIT=32)

# Apply one audio class configuration to TinyUSB and every consumer without
# modifying managed dependencies or replacing the verified esp_tinyusb port.
idf_build_set_property(COMPILE_OPTIONS
    "-include${CMAKE_CURRENT_LIST_DIR}/components/usb_microphone/include/codex_audio_config.h" APPEND)
