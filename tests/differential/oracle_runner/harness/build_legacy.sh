#!/usr/bin/env bash
# Build libharness.so with the legacy GNU libstdc++/gnustl ABI used by
# libkrkr2.so. Requires android-ndk-r17c via KRKR2_LEGACY_NDK.

set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$HERE/../../../.." && pwd)

LEGACY_NDK="${KRKR2_LEGACY_NDK:-${ANDROID_NDK:-}}"
if [[ -z "$LEGACY_NDK" ]]; then
    echo "KRKR2_LEGACY_NDK must point to android-ndk-r17c" >&2
    exit 1
fi

NDK_BUILD="$LEGACY_NDK/ndk-build"
if [[ ! -x "$NDK_BUILD" ]]; then
    echo "missing ndk-build: $NDK_BUILD" >&2
    exit 1
fi

OUT_DIR="$HERE/build/legacy"
LIBS_DIR="$OUT_DIR/libs"
OBJ_DIR="$OUT_DIR/obj"
mkdir -p "$HERE/prebuilt" "$LIBS_DIR" "$OBJ_DIR"

"$NDK_BUILD" \
    -C "$HERE" \
    NDK_PROJECT_PATH="$HERE" \
    APP_BUILD_SCRIPT="$HERE/Android.mk" \
    NDK_APPLICATION_MK="$HERE/Application.mk" \
    NDK_OUT="$OBJ_DIR" \
    NDK_LIBS_OUT="$LIBS_DIR" \
    -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

cp "$LIBS_DIR/arm64-v8a/libharness.so" "$HERE/prebuilt/libharness.so"

python3 "$HERE/../check_harness_abi.py" \
    --harness "$HERE/prebuilt/libharness.so" \
    --libkrkr2 "$REPO_ROOT/reference/libkrkr2/libkrkr2.so"

echo "Output: $HERE/prebuilt/libharness.so"
