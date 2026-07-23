#!/usr/bin/env bash
# Build libharness.so with the legacy GNU libstdc++/gnustl ABI used by
# libkrkr2.so. Requires android-ndk-r17c via KRKR2_LEGACY_NDK.

set -euo pipefail

# NDK r17c ships an x86_64-only Darwin host toolchain and its ndk-build
# wrapper rejects `uname -m == arm64` before Rosetta can translate the tools.
# Re-enter the same script under an x86_64 shell on Apple Silicon.
if [[ "$(uname -s)" == "Darwin" && "$(uname -m)" == "arm64" &&
      "${KRKR2_NDK_ROSETTA_REEXEC:-0}" != "1" ]]; then
    export KRKR2_NDK_ROSETTA_REEXEC=1
    exec arch -x86_64 /bin/bash "$0" "$@"
fi

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
