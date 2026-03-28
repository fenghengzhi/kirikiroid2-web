cmake_minimum_required(VERSION 3.19)
project(krkr2plugin)

add_library(${PROJECT_NAME} STATIC)

set(_PLUGIN_SOURCES
    scriptsEx.cpp
    win32dialog.cpp
    dirlist.cpp
    csvParser.cpp
    varfile.cpp
    saveStruct.cpp
    getabout.cpp
    addFont.cpp
    wutcwf.cpp
    windowEx.cpp
    getSample.cpp
    layerExPerspective.cpp
    fftgraph.cpp
    LayerExBase.cpp
    layerExMovie.cpp
    kirikiroid2.cpp
)
target_sources(${PROJECT_NAME} PUBLIC ${_PLUGIN_SOURCES})

add_library(krkr2plugins_ncbind STATIC
    xp3filter.cpp
    layerExImage/Main.cpp
    layerExImage/LayerExImage.cpp
    shrinkCopy.cpp
    layerExBTOA.cpp
    layerExRaster.cpp
    PackinOne.cpp
    DrawDeviceD3D.cpp
)
set_source_files_properties(
    layerExImage/Main.cpp
    layerExImage/LayerExImage.cpp
    PROPERTIES COMPILE_DEFINITIONS "NCB_MODULE_NAME=TJS_W(\"layerExImage.dll\")"
)
target_include_directories(krkr2plugins_ncbind PRIVATE
    ${KRKR2PLUGIN_PATH}
    ${KRKR2PLUGIN_PATH}/simplebinder}
    ${KRKR2PLUGIN_PATH}/layerExImage
)
target_link_libraries(krkr2plugins_ncbind PRIVATE krkr2core)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${KRKR2CORE_PATH}/
    ${KRKR2CORE_PATH}/base
    ${KRKR2CORE_PATH}/base/win32
    ${KRKR2CORE_PATH}/environ
    ${KRKR2CORE_PATH}/environ/win32
    ${KRKR2CORE_PATH}/environ/android
    ${KRKR2CORE_PATH}/environ/sdl
    ${KRKR2CORE_PATH}/msg
    ${KRKR2CORE_PATH}/msg/win32
    ${KRKR2CORE_PATH}/extension
    ${KRKR2CORE_PATH}/sound
    ${KRKR2CORE_PATH}/sound/win32
    ${KRKR2CORE_PATH}/tjs2
    ${KRKR2CORE_PATH}/utils
    ${KRKR2CORE_PATH}/utils/win32
    ${KRKR2CORE_PATH}/visual
    ${KRKR2CORE_PATH}/visual/ARM
    ${KRKR2CORE_PATH}/visual/win32
    ${KRKR2CORE_PATH}/visual/RenderScript/rs
)

add_definitions(-D__STDC_CONSTANT_MACROS -DTJS_TEXT_OUT_CRLF)

target_include_directories(${PROJECT_NAME} PUBLIC ${KRKR2PLUGIN_PATH} ${KRKR2PLUGIN_PATH}/simplebinder} ${KRKR2PLUGIN_PATH}/layerExImage)
target_link_libraries(${PROJECT_NAME} PUBLIC krkr2core)

add_subdirectory(psdfile)
add_subdirectory(psbfile)
add_subdirectory(layerex_draw)
add_subdirectory(motionplayer)

#add_subdirectory(json)
add_subdirectory(fstat)
#add_subdirectory(steam)
#add_subdirectory(DrawDeviceForSteam)
target_link_libraries(${PROJECT_NAME} PUBLIC
        psdfile
        psbfile
        layerExDraw
        motionplayer
        fstat
#        json steam DrawDeviceForSteam
)

find_package(fmt CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE fmt::fmt)

find_package(spdlog CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog)
