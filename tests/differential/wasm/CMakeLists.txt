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
        -std=c++17 -O1
        -sSTANDALONE_WASM=1
        -sEXPORTED_FUNCTIONS=['_run_psb_rl_decompress','_get_compressed_ptr','_get_decompressed_ptr','_get_decompressed_size_ptr']
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
        -std=c++17 -O1
        -sSTANDALONE_WASM=1
        -sEXPORTED_FUNCTIONS=['_run_bezier_curve','_get_curve_x_ptr','_get_curve_y_ptr','_get_result_ptr']
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
        -std=c++17 -O1
        -sSTANDALONE_WASM=1
        -sEXPORTED_FUNCTIONS=['_run_local_transform','_get_affine_in_ptr','_get_affine_out_ptr']
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
        -std=c++17 -O1
        -sSTANDALONE_WASM=1
        -sEXPORTED_FUNCTIONS=['_run_position_interp','_get_easing_x_ptr','_get_easing_y_ptr','_get_cp_x_ptr','_get_cp_y_ptr','_get_cp_t_ptr','_get_cp_seg_data_ptr','_get_cp_seg_sizes_ptr','_get_src_pos_ptr','_get_dst_pos_ptr','_get_out_pos_ptr']
        --no-entry
        -o ${POSITION_INTERP_WASM}
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/position_interp_wasm.cpp
    VERBATIM
    COMMENT "Building position_interp.wasm"
)
add_custom_target(position_interp_wasm DEPENDS ${POSITION_INTERP_WASM})

# --- motion_playback_wasmtime ---
#
# Full-engine-ish test module for Wasmtime. This is intentionally separate
# from the small single-file scalar wasm targets above: it links the TJS
# engine and MotionPlayer so logo_test_oracle.xp3 can execute its real
# startup.tjs path under WASI. It builds a narrow object set instead of
# linking the motionplayer/krkr2plugin CMake targets directly because those
# targets expose sources as PUBLIC and would pull Browser trace/MainScene
# objects into this headless runner.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_library(motion_playback_wasmtime_objects OBJECT
    ${KRKR2PLUGIN_PATH}/motionplayer/main.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/EmotePlayer.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/PlayerCore.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/PlayerFrameProgress.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/PlayerResource.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/PlayerRender.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/PlayerRenderPrepare.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/PlayerUpdateLayers.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/PlayerQuery.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/ResourceManager.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/RuntimeSupport.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/NodeTree.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/MotionNodeBridge.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/SeparateLayerAdaptor.cpp
    ${KRKR2PLUGIN_PATH}/psbfile/PSBValue.cpp
    ${KRKR2PLUGIN_PATH}/psbfile/PSBFile.cpp
    ${KRKR2PLUGIN_PATH}/psbfile/PSBMedia.cpp
    ${KRKR2PLUGIN_PATH}/psbfile/PSBMediaRegistry.cpp
    ${KRKR2PLUGIN_PATH}/psbfile/types/PimgType.cpp
    ${KRKR2PLUGIN_PATH}/psbfile/types/MotionType.cpp
    ${KRKR2PLUGIN_PATH}/psbfile/types/ScnType.cpp
    ${KRKR2PLUGIN_PATH}/psbfile/types/ImageType.cpp
    ${KRKR2PLUGIN_PATH}/psbfile/types/MmoType.cpp
    ${KRKR2PLUGIN_PATH}/psbfile/types/ArchiveType.cpp
    ${KRKR2PLUGIN_PATH}/psbfile/types/SoundArchiveType.cpp
    ${KRKR2PLUGIN_PATH}/psbfile/resources/ImageMetadata.cpp
    ${KRKR2PLUGIN_PATH}/xp3filter.cpp
)
target_include_directories(motion_playback_wasmtime_objects PRIVATE
    ${KRKR2CORE_PATH}
    ${KRKR2CORE_PATH}/common
    ${KRKR2CORE_PATH}/tjs2
    ${CMAKE_BINARY_DIR}/cpp/core/tjs2/gen
    ${KRKR2CORE_PATH}/base
    ${KRKR2CORE_PATH}/base/impl
    ${KRKR2CORE_PATH}/plugin
    ${KRKR2CORE_PATH}/visual
    ${KRKR2CORE_PATH}/visual/impl
    ${KRKR2CORE_PATH}/sound
    ${KRKR2CORE_PATH}/utils
    ${KRKR2CORE_PATH}/utils/win32
    ${KRKR2CORE_PATH}/environ
    ${KRKR2CORE_PATH}/environ/cocos2d
    ${KRKR2CORE_PATH}/environ/win32
    ${KRKR2PLUGIN_PATH}
    ${KRKR2PLUGIN_PATH}/motionplayer
    ${KRKR2PLUGIN_PATH}/psbfile
    ${KRKR2PLUGIN_PATH}/simplebinder
)
target_compile_definitions(motion_playback_wasmtime_objects PRIVATE
    TJS_TEXT_OUT_CRLF
    __STDC_CONSTANT_MACROS
    USE_UNICODE_FSTRING
)

add_executable(motion_playback_wasmtime EXCLUDE_FROM_ALL
    motion_playback_wasmtime.cpp
    $<TARGET_OBJECTS:motion_playback_wasmtime_objects>
)
target_include_directories(motion_playback_wasmtime PRIVATE
    ${KRKR2CORE_PATH}
    ${KRKR2CORE_PATH}/common
    ${KRKR2CORE_PATH}/tjs2
    ${CMAKE_BINARY_DIR}/cpp/core/tjs2/gen
    ${KRKR2CORE_PATH}/base
    ${KRKR2CORE_PATH}/base/impl
    ${KRKR2CORE_PATH}/plugin
    ${KRKR2CORE_PATH}/visual
    ${KRKR2CORE_PATH}/visual/impl
    ${KRKR2CORE_PATH}/sound
    ${KRKR2CORE_PATH}/utils
    ${KRKR2CORE_PATH}/utils/win32
    ${KRKR2CORE_PATH}/environ
    ${KRKR2CORE_PATH}/environ/cocos2d
    ${KRKR2CORE_PATH}/environ/win32
    ${KRKR2PLUGIN_PATH}
    ${KRKR2PLUGIN_PATH}/motionplayer
    ${KRKR2PLUGIN_PATH}/psbfile
    ${KRKR2PLUGIN_PATH}/simplebinder
)
target_compile_definitions(motion_playback_wasmtime PRIVATE
    TJS_TEXT_OUT_CRLF
    __STDC_CONSTANT_MACROS
    USE_UNICODE_FSTRING
)
set_target_properties(motion_playback_wasmtime PROPERTIES
    OUTPUT_NAME "motion_playback_wasmtime"
    SUFFIX ".wasm"
)
add_dependencies(motion_playback_wasmtime
    tjs2
    core_base_module
    core_plugin_module
    core_visual_module
    core_utils_module
    core_extension_module
    minizip
)
find_package(Boost COMPONENTS locale REQUIRED NO_MODULE)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(oniguruma CONFIG REQUIRED)
find_package(uchardet CONFIG REQUIRED)
find_package(unrar CONFIG REQUIRED)
find_package(LibArchive REQUIRED)
find_package(zstd CONFIG REQUIRED)
find_package(WebP CONFIG REQUIRED)
find_package(libjpeg-turbo CONFIG REQUIRED)
set(OpenCV_ROOT "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/opencv4")
find_package(OpenCV REQUIRED)
find_package(lz4 CONFIG REQUIRED)
target_link_libraries(motion_playback_wasmtime_objects PRIVATE
    tjs2
    spdlog::spdlog
)
target_link_libraries(motion_playback_wasmtime PRIVATE
    $<TARGET_FILE:core_base_module>
    $<TARGET_FILE:core_plugin_module>
    $<TARGET_FILE:core_visual_module>
    $<TARGET_FILE:core_utils_module>
    $<TARGET_FILE:core_extension_module>
    $<TARGET_FILE:tjs2>
    minizip
    uchardet::libuchardet
    unrar::unrar
    LibArchive::LibArchive
    zstd::libzstd
    WebP::webp
    WebP::webpdecoder
    WebP::webpdemux
    $<IF:$<TARGET_EXISTS:libjpeg-turbo::turbojpeg>,libjpeg-turbo::turbojpeg,libjpeg-turbo::turbojpeg-static>
    opencv_imgproc
    opencv_core
    lz4::lz4
    Boost::locale
    fmt::fmt
    spdlog::spdlog
    oniguruma::onig
)
target_link_options(motion_playback_wasmtime PRIVATE
    "SHELL:-sSTANDALONE_WASM=1"
    "SHELL:-sFORCE_FILESYSTEM=1"
    "SHELL:-sALLOW_MEMORY_GROWTH=1"
    "SHELL:-sINITIAL_MEMORY=134217728"
    "SHELL:-sSTACK_SIZE=8388608"
    "SHELL:-sWASM_BIGINT=1"
    "SHELL:-sUSE_LIBPNG=1"
    "SHELL:-sUSE_FREETYPE=1"
    "SHELL:-sEXPORTED_FUNCTIONS=['_mp_write_file','_mp_startup_from','_mp_get_trace_ptr','_mp_get_trace_len','_mp_get_error_ptr','_mp_get_error_len','_malloc','_free']"
    "SHELL:-Wl,--allow-multiple-definition"
    "-fwasm-exceptions"
    "SHELL:--profiling-funcs"
    "SHELL:--no-entry"
)
find_program(WASM_OPT_EXECUTABLE wasm-opt HINTS "$ENV{EMSDK}/upstream/bin")
if(NOT WASM_OPT_EXECUTABLE)
    message(FATAL_ERROR "wasm-opt is required for motion_playback_wasmtime")
endif()
add_custom_command(TARGET motion_playback_wasmtime POST_BUILD
    COMMAND ${WASM_OPT_EXECUTABLE}
        --enable-exception-handling
        --translate-to-exnref
        --debuginfo
        $<TARGET_FILE:motion_playback_wasmtime>
        -o $<TARGET_FILE:motion_playback_wasmtime>.exnref
    COMMAND ${CMAKE_COMMAND} -E rename
        $<TARGET_FILE:motion_playback_wasmtime>.exnref
        $<TARGET_FILE:motion_playback_wasmtime>
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/strip_wasm_export.py
        $<TARGET_FILE:motion_playback_wasmtime>
        __cpp_exception
    VERBATIM
    COMMENT "Converting motion_playback_wasmtime.wasm to exnref exceptions"
)
