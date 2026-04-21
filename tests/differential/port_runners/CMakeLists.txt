cmake_minimum_required(VERSION 3.16)
project(PortRunners LANGUAGES CXX)

# Host-native CLI binaries that drive port code for differential oracles.
# Not built under the wasm/emscripten toolchain (host-only tools).

if(EMSCRIPTEN)
    return()
endif()

add_executable(motion_playback_port
    motion_playback_port.cpp
)

target_link_libraries(motion_playback_port
    PUBLIC
        krkr2plugin
        krkr2core
)

target_include_directories(motion_playback_port
    PRIVATE
        "${TEST_CONFIG_DIR}"
)
