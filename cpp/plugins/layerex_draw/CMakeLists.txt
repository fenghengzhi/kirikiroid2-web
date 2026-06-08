cmake_minimum_required(VERSION 3.19)
project(layerExDraw LANGUAGES CXX C)

add_library(${PROJECT_NAME} STATIC)
target_link_libraries(${PROJECT_NAME} PRIVATE krkr2plugin)

if(WINDOWS)
    target_compile_definitions(${PROJECT_NAME} PRIVATE -DUNICODE -D_UNICODE)
    target_sources(${PROJECT_NAME} PUBLIC
            windows/main.cpp
            windows/LayerExDraw.cpp
    )
# EMSCRIPTEN 走与 Linux/macOS 相同的 general 实现(libgdiplus+cairo+glib+freetype),
# 这正是 libkrkr2.so layerExDraw 的后端选型(IDA 确认 NCB 0x42CD30 tag "GdiPlus")。
else()
    target_sources(${PROJECT_NAME} PUBLIC
            general/main.cpp
            general/LayerExDraw.cpp
            general/DrawPath.cpp
    )

    find_package(libgdiplus CONFIG REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE libgdiplus::libgdiplus)

    set(VCPKG_INSTALL_DIR "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}")

    target_include_directories(${PROJECT_NAME} PUBLIC "${VCPKG_INSTALL_DIR}/lib/glib-2.0/include")
    target_include_directories(${PROJECT_NAME} PUBLIC "${VCPKG_INSTALL_DIR}/include/glib-2.0")

endif ()
