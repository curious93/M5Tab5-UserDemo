cmake_minimum_required(VERSION 3.16)

file(GLOB SOURCES "*.cpp")

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS "."
    REQUIRES mbedtls espressif__mdns m5stack_tab5 nvs_flash esp_event esp_wifi esp_http_server
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

# bell and cspot are non-IDF cmake targets — must explicitly enable exceptions
# (IDF disables exceptions globally; add_subdirectory targets don't inherit that)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -std=gnu++20 -fexceptions -Wno-overloaded-virtual)
target_compile_options(cspot PRIVATE
    -std=gnu++20 -fexceptions -Wno-overloaded-virtual -Wno-array-bounds -Wno-stringop-overflow)
target_compile_options(bell PRIVATE
    -std=gnu++20 -fexceptions -Wno-overloaded-virtual)
