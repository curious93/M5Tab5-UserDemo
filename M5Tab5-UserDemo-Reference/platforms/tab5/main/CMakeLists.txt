file(GLOB_RECURSE HAL_BASE_SRCS
    ../../../app/hal/*.cpp
)

file(GLOB_RECURSE MY_HAL_SRCS
    ./hal/*.c
    ./hal/*.cc
    ./hal/*.cpp
)

file(GLOB_RECURSE APP_SRCS
    ./apps/*.c
    ./apps/*.cc
    ./apps/*.cpp
)

idf_component_register(SRCS "app_main.cpp" "freertos_hooks.c" ${MY_HAL_SRCS} ${HAL_BASE_SRCS} ${APP_SRCS}
                    INCLUDE_DIRS "." "../../../app" "./apps"
                    REQUIRES m5stack_tab5 lvgl mooncake mooncake_log espressif__mdns power_monitor_ina226
                             sensor_bmi270 espressif__usb_host_hid usb esp_wifi esp_video esp_http_server imlib
                             nvs_flash esp_netif esp_event cspot_idf
                    EMBED_TXTFILES "../audio/canon_in_d.mp3" "../audio/startup_sfx.mp3" "../audio/shutdown_sfx.mp3")
