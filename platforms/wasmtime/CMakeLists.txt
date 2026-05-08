cmake_minimum_required(VERSION 3.28)
project(krkr2_wasmtime LANGUAGES CXX)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_library(krkr2_wasmtime_guest_objects OBJECT
    ${KRKR2PLUGIN_PATH}/motionplayer/main.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/EmotePlayer.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/PlayerCore.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/PlayerFrameProgress.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/PlayerResource.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/PlayerMotionLoad.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/PlayerVariable.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/PlayerTimeline.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/PlayerLayerQuery.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/PlayerDrawDispatch.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/PlayerRender.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/PlayerRenderInternal.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/PlayerRenderItems.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/PlayerRenderExecute.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/PlayerRenderTargets.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/PlayerUpdateLayers.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/PlayerUpdateLayerEval.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/PlayerUpdateGeometry.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/PlayerUpdateChildMotion.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/PlayerUpdateParticles.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/PlayerUpdateAnchor.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/ResourceManager.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/SourceCache.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/RuntimeSupport.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/NodeTree.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/MotionNodeBridge.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/PrivateMotionGLL.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/SeparateLayerAdaptor.cpp
    ${KRKR2PLUGIN_PATH}/motionplayer/D3DAdaptor.cpp
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

set(KRKR2_WASMTIME_INCLUDE_DIRS
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
    ${KRKR2CORE_PATH}/sound/win32
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

target_include_directories(krkr2_wasmtime_guest_objects PRIVATE
    ${KRKR2_WASMTIME_INCLUDE_DIRS}
)
target_compile_definitions(krkr2_wasmtime_guest_objects PRIVATE
    TJS_TEXT_OUT_CRLF
    __STDC_CONSTANT_MACROS
    USE_UNICODE_FSTRING
    KRKR2_WASMTIME_HEADLESS=1
    KRKR2_NO_7ZIP
    KRKR2_NO_FFMPEG
)
target_compile_options(krkr2_wasmtime_guest_objects PRIVATE
    -g3
    -O0
    "SHELL:-sUSE_FREETYPE=1"
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
find_package(cocos2dx CONFIG REQUIRED)
find_package(Vorbis CONFIG REQUIRED)
find_package(Opus CONFIG REQUIRED)
find_package(OpusFile CONFIG REQUIRED)
find_package(FFMPEG COMPONENTS
    avutil
    avfilter
    avcodec
    avformat
    swscale
    swresample
    REQUIRED
)

target_link_libraries(krkr2_wasmtime_guest_objects PRIVATE
    tjs2
    spdlog::spdlog
)

add_executable(krkr2_wasmtime_guest EXCLUDE_FROM_ALL
    guest/krkr2_wasmtime_guest.cpp
    $<TARGET_OBJECTS:krkr2_wasmtime_guest_objects>
)
target_include_directories(krkr2_wasmtime_guest PRIVATE
    ${KRKR2_WASMTIME_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/guest
)
target_compile_definitions(krkr2_wasmtime_guest PRIVATE
    TJS_TEXT_OUT_CRLF
    __STDC_CONSTANT_MACROS
    USE_UNICODE_FSTRING
    KRKR2_WASMTIME_HEADLESS=1
    KRKR2_WASMTIME_USE_REAL_RENDER_MANAGER=1
    KRKR2_NO_7ZIP
    KRKR2_NO_FFMPEG
)
target_compile_options(krkr2_wasmtime_guest PRIVATE -g3 -O0)
set_target_properties(krkr2_wasmtime_guest PROPERTIES
    OUTPUT_NAME "krkr2_wasmtime_guest"
    SUFFIX ".wasm"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
add_dependencies(krkr2_wasmtime_guest
    tjs2
    core_base_module
    core_environ_module
    core_plugin_module
    core_visual_module
    core_utils_module
    core_sound_module
    core_movie_module
    core_extension_module
    minizip
)
target_link_libraries(krkr2_wasmtime_guest PRIVATE
    $<TARGET_FILE:core_base_module>
    $<TARGET_FILE:core_environ_module>
    $<TARGET_FILE:core_plugin_module>
    $<TARGET_FILE:core_visual_module>
    $<TARGET_FILE:core_utils_module>
    $<TARGET_FILE:core_sound_module>
    $<TARGET_FILE:core_movie_module>
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
    Vorbis::vorbis
    Vorbis::vorbisfile
    Vorbis::vorbisenc
    Opus::opus
    OpusFile::opusfile
    ${FFMPEG_LIBRARIES}
    Boost::locale
    fmt::fmt
    spdlog::spdlog
    oniguruma::onig
    cocos2dx::cocos2d
    cocos2dx::external
    cocos2dx::ext_xxhash
)
target_link_options(krkr2_wasmtime_guest PRIVATE
    "SHELL:-sUSE_SDL=2"
    "SHELL:-sUSE_SDL_TTF=2"
    "SHELL:-sFORCE_FILESYSTEM=1"
    "SHELL:-sALLOW_UNIMPLEMENTED_SYSCALLS=0"
    "SHELL:-sERROR_ON_UNDEFINED_SYMBOLS=0"
    "SHELL:-sALLOW_MEMORY_GROWTH=1"
    "SHELL:-sINITIAL_MEMORY=268435456"
    "SHELL:-sSTACK_SIZE=8388608"
    "SHELL:-sWASM_BIGINT=1"
    "SHELL:-sUSE_LIBPNG=1"
    "SHELL:-sUSE_FREETYPE=1"
    "SHELL:-sEXPORTED_FUNCTIONS=['_krkr2_wasm_init','_krkr2_wasm_startup_from','_krkr2_wasm_tick','_krkr2_wasm_send_input','_krkr2_wasm_get_error_ptr','_krkr2_wasm_get_error_len','_krkr2_wasm_get_framebuffer_ptr','_krkr2_wasm_get_framebuffer_width','_krkr2_wasm_get_framebuffer_height','_krkr2_wasm_get_framebuffer_pitch','_krkr2_wasm_get_framebuffer_format','_krkr2_wasm_get_framebuffer_frame_no','_malloc','_free']"
    "-fwasm-exceptions"
    "-lopenal"
    "SHELL:-Wl,--allow-multiple-definition"
    "SHELL:-Wl,--export-memory"
    "SHELL:--profiling-funcs"
    "SHELL:--no-entry"
    "-g3"
    "-O0"
)

find_program(WASM_OPT_EXECUTABLE wasm-opt HINTS "$ENV{EMSDK}/upstream/bin")
if(NOT WASM_OPT_EXECUTABLE)
    message(FATAL_ERROR "wasm-opt is required for krkr2_wasmtime_guest")
endif()
add_custom_command(TARGET krkr2_wasmtime_guest POST_BUILD
    COMMAND ${WASM_OPT_EXECUTABLE}
        --enable-exception-handling
        --translate-to-exnref
        --debuginfo
        $<TARGET_FILE:krkr2_wasmtime_guest>
        -o $<TARGET_FILE:krkr2_wasmtime_guest>.exnref
    COMMAND ${CMAKE_COMMAND} -E rename
        $<TARGET_FILE:krkr2_wasmtime_guest>.exnref
        $<TARGET_FILE:krkr2_wasmtime_guest>
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tests/differential/wasmtime/strip_wasm_export.py
        $<TARGET_FILE:krkr2_wasmtime_guest>
        __cpp_exception
    VERBATIM
    COMMENT "Converting krkr2_wasmtime_guest.wasm to exnref exceptions"
)
