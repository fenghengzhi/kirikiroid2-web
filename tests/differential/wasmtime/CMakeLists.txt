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
        -g3
        -O0
        -I${KRKR2PLUGIN_PATH}
        -sSTANDALONE_WASM=1
        -sEXPORTED_FUNCTIONS=['_krkr2_hit_test_run']
        --profiling-funcs
        --no-entry
        -o ${GEOMETRY_HIT_TEST_WASM}
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/geometry_hit_test_wasm.cpp
        ${KRKR2PLUGIN_PATH}/motionplayer/HitTestInternal.h
    VERBATIM
    COMMENT "Building geometry_hit_test.wasm"
)

add_custom_target(geometry_hit_test_wasm DEPENDS ${GEOMETRY_HIT_TEST_WASM})

# --- psb_rl_decompress ---
set(PSB_RL_DECOMPRESS_WASM ${CMAKE_CURRENT_BINARY_DIR}/psb_rl_decompress.wasm)
add_custom_command(
    OUTPUT ${PSB_RL_DECOMPRESS_WASM}
    COMMAND ${CMAKE_CXX_COMPILER}
        ${CMAKE_CURRENT_SOURCE_DIR}/psb_rl_decompress_wasm.cpp
        -std=c++17 -g3 -O0 --profiling-funcs
        -sSTANDALONE_WASM=1
        -sEXPORTED_FUNCTIONS=['_run_psb_rl_decompress','_get_compressed_ptr']
        --no-entry
        -o ${PSB_RL_DECOMPRESS_WASM}
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/psb_rl_decompress_wasm.cpp
    VERBATIM
    COMMENT "Building psb_rl_decompress.wasm"
)
add_custom_target(psb_rl_decompress_wasm DEPENDS ${PSB_RL_DECOMPRESS_WASM})

# --- bezier_curve ---
set(BEZIER_CURVE_WASM ${CMAKE_CURRENT_BINARY_DIR}/bezier_curve.wasm)
add_custom_command(
    OUTPUT ${BEZIER_CURVE_WASM}
    COMMAND ${CMAKE_CXX_COMPILER}
        ${CMAKE_CURRENT_SOURCE_DIR}/bezier_curve_wasm.cpp
        -std=c++17 -g3 -O0 --profiling-funcs
        -sSTANDALONE_WASM=1
        -sEXPORTED_FUNCTIONS=['_run_bezier_curve','_get_curve_x_ptr','_get_curve_y_ptr']
        --no-entry
        -o ${BEZIER_CURVE_WASM}
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/bezier_curve_wasm.cpp
    VERBATIM
    COMMENT "Building bezier_curve.wasm"
)
add_custom_target(bezier_curve_wasm DEPENDS ${BEZIER_CURVE_WASM})

# --- local_transform ---
set(LOCAL_TRANSFORM_WASM ${CMAKE_CURRENT_BINARY_DIR}/local_transform.wasm)
add_custom_command(
    OUTPUT ${LOCAL_TRANSFORM_WASM}
    COMMAND ${CMAKE_CXX_COMPILER}
        ${CMAKE_CURRENT_SOURCE_DIR}/local_transform_wasm.cpp
        -std=c++17 -g3 -O0 --profiling-funcs
        -sSTANDALONE_WASM=1
        -sEXPORTED_FUNCTIONS=['_run_local_transform','_get_affine_in_ptr']
        --no-entry
        -o ${LOCAL_TRANSFORM_WASM}
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/local_transform_wasm.cpp
    VERBATIM
    COMMENT "Building local_transform.wasm"
)
add_custom_target(local_transform_wasm DEPENDS ${LOCAL_TRANSFORM_WASM})

# --- position_interp ---
set(POSITION_INTERP_WASM ${CMAKE_CURRENT_BINARY_DIR}/position_interp.wasm)
add_custom_command(
    OUTPUT ${POSITION_INTERP_WASM}
    COMMAND ${CMAKE_CXX_COMPILER}
        ${CMAKE_CURRENT_SOURCE_DIR}/position_interp_wasm.cpp
        -std=c++17 -g3 -O0 --profiling-funcs
        -sSTANDALONE_WASM=1
        -sEXPORTED_FUNCTIONS=['_run_position_interp','_get_easing_x_ptr','_get_easing_y_ptr','_get_cp_x_ptr','_get_cp_y_ptr','_get_cp_t_ptr','_get_cp_seg_data_ptr','_get_cp_seg_sizes_ptr','_get_src_pos_ptr','_get_dst_pos_ptr']
        --no-entry
        -o ${POSITION_INTERP_WASM}
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/position_interp_wasm.cpp
    VERBATIM
    COMMENT "Building position_interp.wasm"
)
add_custom_target(position_interp_wasm DEPENDS ${POSITION_INTERP_WASM})
