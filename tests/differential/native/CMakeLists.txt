cmake_minimum_required(VERSION 3.19)
project(MotionPlaybackNativeDifferential LANGUAGES CXX)

if(EMSCRIPTEN)
    return()
endif()

set(KRKR2DIFF_NATIVE_PATH ${CMAKE_CURRENT_LIST_DIR})
if(NOT DEFINED KRKR2CORE_PATH)
    set(KRKR2CORE_PATH ${CMAKE_CURRENT_LIST_DIR}/../../../cpp/core)
endif()
if(NOT DEFINED KRKR2PLUGIN_PATH)
    set(KRKR2PLUGIN_PATH ${CMAKE_CURRENT_LIST_DIR}/../../../cpp/plugins)
endif()

set(KRKR2DIFF_NATIVE_INCLUDE_DIRS
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

add_executable(motion_playback_native EXCLUDE_FROM_ALL
    motion_playback_native.cpp
)
target_include_directories(motion_playback_native PRIVATE
    ${KRKR2DIFF_NATIVE_INCLUDE_DIRS}
)
target_compile_definitions(motion_playback_native PRIVATE
    TJS_TEXT_OUT_CRLF
    __STDC_CONSTANT_MACROS
    USE_UNICODE_FSTRING
    KRKR2_REPO_ROOT="${CMAKE_SOURCE_DIR}"
)
add_dependencies(motion_playback_native
    krkr2core
    krkr2plugin
    krkr2plugins_ncbind
)

find_package(spdlog CONFIG REQUIRED)
find_package(EXPAT REQUIRED)
find_package(unofficial-brotli CONFIG REQUIRED)

target_link_libraries(motion_playback_native PRIVATE
    krkr2core
    # Reuse the production source targets and their force-load contract.  The
    # former hand-maintained OBJECT list silently missed new motionplayer
    # translation units and could not represent the current runtime.
    krkr2plugin
    krkr2plugins_ncbind
    spdlog::spdlog
    EXPAT::EXPAT
    # The vcpkg FreeType build enables WOFF2, whose decoder dependency is not
    # carried by the legacy cocos2dx package's exported target.
    unofficial::brotli::brotlidec
)

if(APPLE)
    target_link_options(motion_playback_native PRIVATE
        "-Wl,-multiply_defined,suppress"
        "-Wl,-force_load,$<TARGET_FILE:krkr2plugins_ncbind>"
    )
elseif(MSVC)
    target_link_options(motion_playback_native PRIVATE "/FORCE:MULTIPLE")
else()
    target_link_options(motion_playback_native PRIVATE
        "-Wl,--allow-multiple-definition"
    )
endif()
