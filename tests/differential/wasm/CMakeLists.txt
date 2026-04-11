cmake_minimum_required(VERSION 3.16)
project(GeometryHitTestWasm LANGUAGES CXX)

set(GEOMETRY_HIT_TEST_WASM
    ${CMAKE_CURRENT_BINARY_DIR}/geometry_hit_test.wasm
)

add_custom_command(
    OUTPUT ${GEOMETRY_HIT_TEST_WASM}
    COMMAND ${CMAKE_CXX_COMPILER}
        ${CMAKE_CURRENT_SOURCE_DIR}/geometry_hit_test_wasm.cpp
        -std=c++17
        -O1
        -I${KRKR2PLUGIN_PATH}
        -sSTANDALONE_WASM=1
        -sEXPORTED_FUNCTIONS=['_krkr2_hit_test_run']
        --no-entry
        -o ${GEOMETRY_HIT_TEST_WASM}
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/geometry_hit_test_wasm.cpp
        ${KRKR2PLUGIN_PATH}/motionplayer/HitTestInternal.h
    VERBATIM
    COMMENT "Building geometry_hit_test.wasm"
)

add_custom_target(geometry_hit_test_wasm DEPENDS ${GEOMETRY_HIT_TEST_WASM})
