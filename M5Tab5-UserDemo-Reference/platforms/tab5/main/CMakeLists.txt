file(GLOB_RECURSE HAL_BASE_SRCS
    ../../../app/hal/*.cpp
)

file(GLOB_RECURSE MY_HAL_SRCS
    ./hal/*.c
    ./hal/*.cc
    ./hal/*.cpp
)

idf_component_register(SRCS "app_main.cpp" ${MY_HAL_SRCS} ${HAL_BASE_SRCS}
                    INCLUDE_DIRS "." "../../../app"
                    EMBED_TXTFILES "../audio/canon_in_d.mp3" "../audio/startup_sfx.mp3" "../audio/shutdown_sfx.mp3")


