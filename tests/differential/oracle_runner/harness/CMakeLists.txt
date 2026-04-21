cmake_minimum_required(VERSION 3.16)
project(oracle_harness CXX)

# -----------------------------------------------------------------------------
# Single artifact: `libharness.so` — JNI shared library loaded by the
# repacked krkr2-harness.apk. HarnessActivity (extends Cocos2dxActivity)
# runs the normal cocos2d init path, so by the time it invokes
# Java_org_github_krkr2_HarnessActivity_runRpcServeFd the TVPScriptEngine
# global is populated and every NCB class is registered with TJS.
#
# Cross-compile via the NDK (direct clang invocation, faster than CMake):
#
#   export ANDROID_NDK=~/Library/Android/sdk/ndk/27.0.12077973
#   CLANG="$ANDROID_NDK/toolchains/llvm/prebuilt/$(ls $ANDROID_NDK/toolchains/llvm/prebuilt)/bin/aarch64-linux-android24-clang++"
#
#   "$CLANG" -O2 -fPIC -shared -static-libstdc++ \
#            harness.cpp jni_bridge.cpp -ldl -llog \
#            -o prebuilt/libharness.so
#
# The .so is then packaged by harness-apk/build.sh into krkr2-harness.apk.
# -----------------------------------------------------------------------------

add_library(harness_jni SHARED harness.cpp jni_bridge.cpp)
set_target_properties(harness_jni PROPERTIES OUTPUT_NAME "harness")
target_compile_options(harness_jni PRIVATE -fPIC)
target_link_options(harness_jni PRIVATE -static-libstdc++)
target_link_libraries(harness_jni dl log)

# Sanity: warn if built on non-aarch64 (will obviously not run on guest).
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    message(WARNING "harness must be cross-compiled for aarch64 (got ${CMAKE_SYSTEM_PROCESSOR})")
endif()

install(TARGETS harness_jni DESTINATION prebuilt)
