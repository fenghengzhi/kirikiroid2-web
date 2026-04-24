cmake_minimum_required(VERSION 3.16)
project(oracle_harness_legacy NONE)

# libharness.so must be built with android-ndk-r17c + gnustl_static so its
# C++ ABI matches libkrkr2.so. This CMake file is a thin wrapper around the
# canonical ndk-build script; it deliberately does not use the host C++
# compiler or modern Android CMake toolchain.

set(KRKR2_LEGACY_NDK "$ENV{KRKR2_LEGACY_NDK}" CACHE PATH
    "Path to android-ndk-r17c")

if(NOT KRKR2_LEGACY_NDK)
    message(FATAL_ERROR
        "Set KRKR2_LEGACY_NDK to android-ndk-r17c, then build this target.")
endif()

set(HARNESS_SO
    ${CMAKE_CURRENT_SOURCE_DIR}/prebuilt/libharness.so
)

add_custom_command(
    OUTPUT ${HARNESS_SO}
    COMMAND ${CMAKE_COMMAND} -E env
            KRKR2_LEGACY_NDK=${KRKR2_LEGACY_NDK}
            ${CMAKE_CURRENT_SOURCE_DIR}/build_legacy.sh
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/harness.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/jni_bridge.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/Android.mk
        ${CMAKE_CURRENT_SOURCE_DIR}/Application.mk
        ${CMAKE_CURRENT_SOURCE_DIR}/build_legacy.sh
        ${CMAKE_CURRENT_SOURCE_DIR}/../check_harness_abi.py
    VERBATIM
    COMMENT "Building legacy-ABI libharness.so with ndk-build"
)

add_custom_target(harness_jni ALL DEPENDS ${HARNESS_SO})
