# LINGXIN SDK component for v2.6.6 xiaozhi project
# Adapter layer (lingxin_audio_record.c, lingxin_buffer_play.c, lingxin_local_play.c, lingxin_device_info.c)
# are rewritten to bridge with v2.6.6 AudioService instead of ESP-GMF.

file(GLOB_RECURSE SOURCES
  "adapter_LingXin/*.c"
  "AI2T_LingXinEngine/src/*.c"
  "AI2T_LingXinEngine/src/**/*.c"
)

# Exclude test and usecase files from SDK
list(FILTER SOURCES EXCLUDE REGEX "testBuild/.*")
list(FILTER SOURCES EXCLUDE REGEX "tests/.*")
list(FILTER SOURCES EXCLUDE REGEX "usecases/.*")

idf_component_register(SRCS ${SOURCES}
  INCLUDE_DIRS "AI2T_LingXinEngine/inc" "AI2T_LingXinEngine/inc/func" "AI2T_LingXinEngine/inc/system_adapter"
  PRIV_INCLUDE_DIRS "AI2T_LingXinEngine/src/inc" "AI2T_LingXinEngine/src/inc/state_module_manager" "AI2T_LingXinEngine/src/inc/task_state_strategies"
  REQUIRES freertos esp_ringbuf mbedtls esp_websocket_client esp_http_client json main esp_wifi esp_system
)
