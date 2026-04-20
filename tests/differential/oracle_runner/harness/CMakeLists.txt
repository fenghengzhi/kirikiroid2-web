cmake_minimum_required(VERSION 3.16)
project(oracle_harness CXX)

# -----------------------------------------------------------------------------
# Two artifacts:
#
#  1. `harness-aarch64` — standalone PIE executable. Used by Qiling paths
#     and by quick local smoke tests that don't need a JVM.
#  2. `libharness.so` — shared library loaded by HarnessMain.java (via
#     app_process) so libkrkr2 can see a live ART VM + ActivityThread.
#
# Cross-compile via the NDK (direct clang invocation, faster than CMake):
#
#   export ANDROID_NDK=~/Library/Android/sdk/ndk/27.0.12077973
#   CLANG="$ANDROID_NDK/toolchains/llvm/prebuilt/$(ls $ANDROID_NDK/toolchains/llvm/prebuilt)/bin/aarch64-linux-android24-clang++"
#
#   # Standalone ELF
#   "$CLANG" -O2 -fPIE -pie -static-libstdc++ \
#            harness.cpp -ldl -o prebuilt/harness-aarch64
#
#   # JNI shared library
#   "$CLANG" -O2 -fPIC -shared -static-libstdc++ \
#            harness.cpp jni_bridge.cpp -ldl -llog \
#            -o prebuilt/libharness.so
#
# And compile the Java launcher to dex (host-side, needs JDK + Android SDK):
#
#   javac --release 8 -d build-java -cp $ANDROID_HOME/platforms/android-34/android.jar \
#         HarnessMain.java
#   $ANDROID_HOME/build-tools/34.0.0/d8 --release --min-api 24 \
#                                       --output build-java \
#                                       build-java/org/krkr2/HarnessMain.class
#   cp build-java/classes.dex prebuilt/harness-launcher.dex
# -----------------------------------------------------------------------------

# Standalone PIE. `-static-libstdc++` because harness.cpp uses <exception>
# and the target device may not ship libc++_shared.so.
add_executable(harness harness.cpp)
target_compile_options(harness PRIVATE -fPIE)
target_link_options(harness PRIVATE -pie -static-libstdc++)
target_link_libraries(harness dl)

# JNI shared library for app_process launch path.
add_library(harness_jni SHARED harness.cpp jni_bridge.cpp)
set_target_properties(harness_jni PROPERTIES OUTPUT_NAME "harness")
target_compile_options(harness_jni PRIVATE -fPIC)
target_link_options(harness_jni PRIVATE -static-libstdc++)
target_link_libraries(harness_jni dl log)

# Sanity: warn if built on non-aarch64 (will obviously not run on guest).
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    message(WARNING "harness must be cross-compiled for aarch64 (got ${CMAKE_SYSTEM_PROCESSOR})")
endif()

install(TARGETS harness harness_jni DESTINATION prebuilt)
