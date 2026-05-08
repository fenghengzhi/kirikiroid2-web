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

add_library(motion_playback_native_objects OBJECT
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
    ${KRKR2PLUGIN_PATH}/motionplayer/MotionTraceWeb.cpp
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

target_include_directories(motion_playback_native_objects PRIVATE
    ${KRKR2DIFF_NATIVE_INCLUDE_DIRS}
)
target_compile_definitions(motion_playback_native_objects PRIVATE
    TJS_TEXT_OUT_CRLF
    __STDC_CONSTANT_MACROS
    USE_UNICODE_FSTRING
)

add_executable(motion_playback_native EXCLUDE_FROM_ALL
    motion_playback_native.cpp
    $<TARGET_OBJECTS:motion_playback_native_objects>
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
)

find_package(spdlog CONFIG REQUIRED)
find_package(EXPAT REQUIRED)

target_link_libraries(motion_playback_native_objects PRIVATE
    tjs2
    spdlog::spdlog
)
target_link_libraries(motion_playback_native PRIVATE
    krkr2core
    spdlog::spdlog
    EXPAT::EXPAT
)

if(APPLE)
    target_link_options(motion_playback_native PRIVATE
        "-Wl,-multiply_defined,suppress"
    )
elseif(MSVC)
    target_link_options(motion_playback_native PRIVATE "/FORCE:MULTIPLE")
else()
    target_link_options(motion_playback_native PRIVATE
        "-Wl,--allow-multiple-definition"
    )
endif()
