cmake_minimum_required(VERSION 3.28)

find_program(CCACHE_PROGRAM ccache)

if (CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "Ccache found: ${CCACHE_PROGRAM}")
endif ()

add_compile_definitions(
        $<$<CONFIG:Debug>:DEBUG>
        $<$<CONFIG:Debug>:_DEBUG>   # 对于 MSVC 兼容性
        $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

if(ANDROID)
    set(VCPKG_TARGET_ANDROID ON)
    if(ANDROID_NDK)
        set(ENV{ANDROID_NDK_HOME} "${ANDROID_NDK}")
    endif()
    add_compile_options(-Wno-inconsistent-missing-override)
    include(cmake/vcpkg_android.cmake)
elseif(VCPKG_TARGET_TRIPLET STREQUAL "wasm32-emscripten" OR EMSCRIPTEN)
    set(WEB ON)
    set(VCPKG_TARGET_TRIPLET "wasm32-emscripten")
    set(VCPKG_OVERLAY_TRIPLETS "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/triplets")
    if(CMAKE_TOOLCHAIN_FILE AND NOT CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg")
        set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_TOOLCHAIN_FILE}")
    endif()
    set(CMAKE_TOOLCHAIN_FILE $ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake)
else()
    set(CMAKE_TOOLCHAIN_FILE $ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake)
endif()

set(APP_NAME krkr2)

project(${APP_NAME})

option(ENABLE_TESTS "enable tests execute build(exclude android)" ON)
option(BUILD_TOOLS "build tools execute build(exclude android ios)" ON)
option(KRKR2_WEB_DEBUG_ENABLE_ASAN "enable AddressSanitizer in Web Debug builds" ON)
option(KRKR2_ENABLE_WASMTIME_HEADLESS "build the generic Wasmtime headless debug runner" OFF)
if(NOT DEFINED KRKR2_USE_STUB_SPDLOG)
    set(_KRKR2_USE_STUB_SPDLOG_DEFAULT OFF)
    if(WEB AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(_KRKR2_USE_STUB_SPDLOG_DEFAULT ON)
    endif()
    set(KRKR2_USE_STUB_SPDLOG ${_KRKR2_USE_STUB_SPDLOG_DEFAULT} CACHE BOOL
        "use the no-op spdlog stub package")
endif()

if(KRKR2_USE_STUB_SPDLOG)
    set(spdlog_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake/spdlog_stub" CACHE PATH
        "stub spdlog package path" FORCE)
    message(STATUS "Using no-op spdlog stub")
elseif(DEFINED spdlog_DIR AND spdlog_DIR MATCHES "/cmake/spdlog_stub$")
    unset(spdlog_DIR CACHE)
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(KRKR2CORE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cpp/core)
set(KRKR2PLUGIN_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cpp/plugins)

if(MSVC)
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/EHsc>" "/MP" "/utf-8")
    add_link_options("/ignore:4099" "/INCREMENTAL" "/DEBUG:FASTLINK")
    # set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
elseif(WEB)
    add_compile_options(
        -pthread
        -fwasm-exceptions
        -msimd128
        $<$<AND:$<CONFIG:Debug>,$<BOOL:${KRKR2_WEB_DEBUG_ENABLE_ASAN}>>:-fsanitize=address>
    )
    add_link_options($<$<AND:$<CONFIG:Debug>,$<BOOL:${KRKR2_WEB_DEBUG_ENABLE_ASAN}>>:-fsanitize=address>)
endif()

if(ANDROID)
    add_library(${PROJECT_NAME} SHARED ${CMAKE_CURRENT_SOURCE_DIR}/platforms/android/cpp/krkr2_android.cpp)
    find_package(unofficial-breakpad CONFIG REQUIRED)
    target_link_libraries(${PROJECT_NAME} PUBLIC unofficial::breakpad::libbreakpad_client)
elseif(WEB)
    add_executable(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/platforms/web/main.cpp)
    set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "index" SUFFIX ".html")
    target_link_options(${PROJECT_NAME} PRIVATE
        "SHELL:-s USE_SDL=2"
        "SHELL:-s USE_SDL_TTF=2"
        "SHELL:-s MAX_WEBGL_VERSION=2"
        "SHELL:-s FULL_ES2=1"
        "SHELL:-s ALLOW_MEMORY_GROWTH=1"
        "SHELL:-s INITIAL_MEMORY=67108864"
        "SHELL:-s STACK_SIZE=2097152"
        "$<$<CONFIG:Debug>:SHELL:-s STACK_OVERFLOW_CHECK=2>"
        "$<$<NOT:$<CONFIG:Debug>>:SHELL:-s STACK_OVERFLOW_CHECK=0>"
        "$<$<CONFIG:Debug>:SHELL:-s ASSERTIONS=2>"
        "SHELL:-s FORCE_FILESYSTEM=1"
        "SHELL:-s EXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
        # Thread pool size calculation for Web platform:
        # - 3 application threads (main player, video player, audio player)
        # - 4 FFmpeg video decoder threads (limited on Web)
        # - 4 OpenMP threads (for image processing, limited to 4 on Web)
        # - 2 AudioEngine threads (limited on Web)
        # - Some buffer for other tasks
        # Total: ~13-15 threads, so 24 provides good margin
        "$<$<CONFIG:Debug>:SHELL:-s PTHREAD_POOL_SIZE=64>"
        "$<$<NOT:$<CONFIG:Debug>>:SHELL:-s PTHREAD_POOL_SIZE=24>"
        "SHELL:-s DEFAULT_PTHREAD_STACK_SIZE=1048576"
        -fwasm-exceptions
        $<$<CONFIG:Debug>:--emit-symbol-map>
        $<$<CONFIG:Debug>:-g2>
        $<$<CONFIG:Debug>:-O1>
        -pthread
        -lopenal
        --shell-file ${CMAKE_CURRENT_BINARY_DIR}/shell.html
        --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/ui/cocos-studio@/
    )
    target_compile_options(${PROJECT_NAME} PRIVATE "SHELL:-s USE_SDL=2" "SHELL:-s USE_SDL_TTF=2" -fwasm-exceptions)
    # Local ZIP opener is visible by default in Debug, and can be enabled
    # explicitly in Release with ?pickZip=1.
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(KRKR2_LOCAL_ZIP_PICKER_DEFAULT_VISIBLE "true")
    else()
        set(KRKR2_LOCAL_ZIP_PICKER_DEFAULT_VISIBLE "false")
    endif()
    # PWA: only enabled for non-Debug builds
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        file(READ ${CMAKE_CURRENT_SOURCE_DIR}/platforms/web/pwa-head.html KRKR2_PWA_HEAD)
        file(READ ${CMAKE_CURRENT_SOURCE_DIR}/platforms/web/pwa-sw.html KRKR2_PWA_SCRIPT)
        # Generate build version timestamp for service worker cache busting
        string(TIMESTAMP KRKR2_BUILD_VERSION "%Y%m%d%H%M%S")
        configure_file(
            ${CMAKE_CURRENT_SOURCE_DIR}/platforms/web/sw.js
            ${CMAKE_CURRENT_BINARY_DIR}/sw.js
            @ONLY
        )
    else()
        set(KRKR2_PWA_HEAD "")
        # Unregister any previously installed service worker and clear caches
        file(READ ${CMAKE_CURRENT_SOURCE_DIR}/platforms/web/pwa-unregister.html KRKR2_PWA_SCRIPT)
    endif()
    # Process shell.html template (injects PWA content for Release, empty for Debug)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/platforms/web/shell.html
        ${CMAKE_CURRENT_BINARY_DIR}/shell.html
        @ONLY
    )
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_custom_command(
            TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/platforms/web/manifest.webmanifest
                ${CMAKE_CURRENT_BINARY_DIR}/sw.js
                $<TARGET_FILE_DIR:${PROJECT_NAME}>
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/platforms/web/pwa
                $<TARGET_FILE_DIR:${PROJECT_NAME}>/pwa
            COMMENT "Copy PWA manifest, service worker, and icons"
        )
    endif()
elseif(LINUX)
    add_executable(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/platforms/linux/main.cpp)
elseif(WINDOWS)
    add_executable(${PROJECT_NAME}
        ${CMAKE_CURRENT_SOURCE_DIR}/platforms/windows/main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/platforms/windows/game.rc
    )
elseif(IOS)
#        list(APPEND GAME_HEADER
#                proj.ios_mac/ios/AppController.h
#                proj.ios_mac/ios/RootViewController.h
#        )
#        set(APP_UI_RES
#                proj.ios_mac/ios/LaunchScreen.storyboard
#                proj.ios_mac/ios/LaunchScreenBackground.png
#                proj.ios_mac/ios/Images.xcassets
#        )
#        list(APPEND GAME_SOURCE
#                proj.ios_mac/ios/main.m
#                proj.ios_mac/ios/AppController.mm
#                proj.ios_mac/ios/RootViewController.mm
#                proj.ios_mac/ios/Prefix.pch
#                ${APP_UI_RES}
#        )
elseif(MACOS)
    set(APP_UI_RES
            ${CMAKE_CURRENT_SOURCE_DIR}/platforms/apple/macos/Icon.icns
            ${CMAKE_CURRENT_SOURCE_DIR}/platforms/apple/macos/Info.plist
    )
    add_executable(${PROJECT_NAME}
            ${CMAKE_CURRENT_SOURCE_DIR}/platforms/apple/macos/main.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/platforms/apple/macos/Prefix.pch
            ${APP_UI_RES}
    )
endif()

target_include_directories(${PROJECT_NAME} PUBLIC ${KRKR2CORE_PATH}/environ/cocos2d)

# external lib
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/cpp/external)

# build library
add_subdirectory(${KRKR2CORE_PATH})
add_subdirectory(${KRKR2PLUGIN_PATH})
if(WEB)
    add_subdirectory(tests/differential/wasmtime)
    if(KRKR2_ENABLE_WASMTIME_HEADLESS)
        add_subdirectory(platforms/wasmtime)
    endif()
endif()

target_link_libraries(${PROJECT_NAME} PUBLIC
    krkr2plugin krkr2core
)

if(APPLE AND NOT EMSCRIPTEN)
    target_link_libraries(${PROJECT_NAME} PUBLIC
        -Wl,-force_load,$<TARGET_FILE:krkr2plugins_ncbind>
    )
else()
    target_link_libraries(${PROJECT_NAME} PUBLIC
        -Wl,--whole-archive krkr2plugins_ncbind -Wl,--no-whole-archive
    )
endif()

## resources
if(NOT ANDROID AND NOT WEB)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/CocosBuildHelpers.cmake")

    set(GAME_RES_FOLDER "${CMAKE_CURRENT_SOURCE_DIR}/ui/cocos-studio")

    if(WINDOWS OR APPLE)
        cocos_mark_multi_resources(common_res_files RES_TO "Resources" FOLDERS ${GAME_RES_FOLDER})
    endif()

    # mark app resources
    setup_cocos_app_config(${APP_NAME})

    if(APPLE)
        set_target_properties(${APP_NAME} PROPERTIES RESOURCE "${APP_UI_RES}")
        target_sources(${APP_NAME} PRIVATE ${common_res_files})
        if(MACOSX)
            set_target_properties(${APP_NAME} PROPERTIES
                    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/apple/macos/Info.plist"
            )
        elseif(IOS)
#            cocos_pak_xcode(${APP_NAME} INFO_PLIST "iOSBundleInfo.plist.in")
#            set_xcode_property(${APP_NAME} ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon")
#            set_xcode_property(${APP_NAME} DEVELOPMENT_TEAM "")
#            set_xcode_property(${APP_NAME} CODE_SIGN_IDENTITY "iPhone Developer")
        endif()
    elseif(WINDOWS)
        target_sources(${PROJECT_NAME} PRIVATE ${common_res_files})
        cocos_copy_target_dll(${APP_NAME})
    endif()

    if(LINUX OR WINDOWS)
        cocos_get_resource_path(APP_RES_DIR ${APP_NAME})
        cocos_copy_target_res(${APP_NAME} LINK_TO ${APP_RES_DIR} FOLDERS ${GAME_RES_FOLDER})
    endif()
endif()

if(ENABLE_TESTS AND NOT (IOS OR ANDROID OR WEB))
    enable_testing()
    add_subdirectory(tests)
endif()

if(BUILD_TOOLS AND NOT (IOS OR ANDROID OR WEB))
    add_subdirectory(tools)
endif()
