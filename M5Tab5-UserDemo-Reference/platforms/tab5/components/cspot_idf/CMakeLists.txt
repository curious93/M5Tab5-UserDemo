cmake_minimum_required(VERSION 3.16)

file(GLOB SOURCES "*.cpp")

# Generate generated_blob.h from host-side .secrets/cspot_blob.json (or
# ~/.claude/tab5/cspot_blob.json). Empty define if no backup exists —
# cspot falls back to NVS, then ZeroConf.
set(CSPOT_BLOB_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated_blob.h")
execute_process(
    COMMAND /usr/bin/python3 $ENV{HOME}/.claude/hooks/cspot_blob_gen_header.py "${CSPOT_BLOB_HEADER}"
)

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS "." "${CMAKE_CURRENT_BINARY_DIR}"
    REQUIRES mbedtls espressif__mdns m5stack_tab5 nvs_flash esp_event esp_wifi esp_http_server esp_http_client esp-tls
    EMBED_FILES test_tone.ogg
)

# Bell options — set BEFORE add_subdirectory so they propagate
# Note: use FORCE to override any cached values from previous builds
set(BELL_DISABLE_SINKS ON CACHE BOOL "" FORCE)
set(BELL_DISABLE_MQTT ON CACHE BOOL "" FORCE)
set(BELL_DISABLE_WEBSERVER OFF CACHE BOOL "" FORCE)
# Disable all codecs — Spotify stream uses Vorbis via cspot's own Tremor, not bell codecs
set(BELL_DISABLE_CODECS ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

add_subdirectory(
    "${CMAKE_CURRENT_SOURCE_DIR}/../cspot/cspot"
    "${CMAKE_CURRENT_BINARY_DIR}/cspot_lib"
)

target_link_libraries(${COMPONENT_LIB} PUBLIC cspot)

# cspot's CDNAudioFile now uses esp_http_client (IDF-native HTTP) for CDN
# range fetches. The cspot target is non-IDF, so plumb in the include path
# + link the library manually.
idf_component_get_property(HTTP_CLIENT_INCLUDES esp_http_client INCLUDE_DIRS)
idf_component_get_property(HTTP_CLIENT_DIR esp_http_client COMPONENT_DIR)
target_include_directories(cspot PRIVATE
    "${HTTP_CLIENT_DIR}/include"
    "${HTTP_CLIENT_DIR}")
target_link_libraries(cspot PRIVATE idf::esp_http_client idf::esp-tls)

# esp_crt_bundle_attach needs the include + the CA data object
idf_component_get_property(ESP_TLS_DIR esp-tls COMPONENT_DIR)
target_include_directories(cspot PRIVATE
    "${ESP_TLS_DIR}"
    "${ESP_TLS_DIR}/esp_crt_bundle")

# bell and cspot are non-IDF cmake targets — must explicitly enable exceptions
# (IDF disables exceptions globally; add_subdirectory targets don't inherit that)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -std=gnu++20 -fexceptions -Wno-overloaded-virtual)
target_compile_options(cspot PRIVATE
    -std=gnu++20 -fexceptions -Wno-overloaded-virtual -Wno-array-bounds -Wno-stringop-overflow)
target_compile_options(bell PRIVATE
    -std=gnu++20 -fexceptions -Wno-overloaded-virtual)
