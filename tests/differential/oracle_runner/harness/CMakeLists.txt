cmake_minimum_required(VERSION 3.16)
project(oracle_harness C)

# -----------------------------------------------------------------------------
# Cross-compile for aarch64 Android (bionic) via the NDK:
#
#   cmake -S . -B build \
#         -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
#         -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24
#   cmake --build build
#   cp build/harness prebuilt/harness-aarch64
#
# Or compile directly with NDK clang, bypassing CMake:
#   "$ANDROID_NDK/toolchains/llvm/prebuilt/<host>/bin/aarch64-linux-android24-clang" \
#     -O2 -fPIE -pie harness.c -o prebuilt/harness-aarch64 -ldl
# -----------------------------------------------------------------------------

add_executable(harness harness.c)

# Android aarch64 linker64 requires PIE for all executables.
target_compile_options(harness PRIVATE -fPIE)
target_link_options(harness PRIVATE -pie)
target_link_libraries(harness dl)

# Sanity: warn if built on non-aarch64 (will obviously not run on guest).
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    message(WARNING "harness must be cross-compiled for aarch64 (got ${CMAKE_SYSTEM_PROCESSOR})")
endif()

install(TARGETS harness DESTINATION prebuilt)
