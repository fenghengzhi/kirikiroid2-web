#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Check EMSDK
if [ -z "${EMSDK:-}" ]; then
    echo "Error: EMSDK environment variable not set."
    echo "Please install Emscripten SDK and run: source \$EMSDK/emsdk_env.sh"
    exit 1
fi

# Check VCPKG_ROOT
if [ -z "${VCPKG_ROOT:-}" ]; then
    echo "Error: VCPKG_ROOT environment variable not set."
    exit 1
fi

BUILD_TYPE="${1:-Release}"

echo "=== Building krkr2 for Web (${BUILD_TYPE}) ==="
echo "  EMSDK:       $EMSDK"
echo "  VCPKG_ROOT:  $VCPKG_ROOT"
echo "  Project:     $PROJECT_ROOT"

BUILD_DIR="${PROJECT_ROOT}/out/web/$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')"

echo ""
echo "=== Step 1: CMake Configure ==="
emcmake cmake \
    -S "$PROJECT_ROOT" \
    -B "$BUILD_DIR" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DVCPKG_TARGET_TRIPLET=wasm32-emscripten \
    -DVCPKG_OVERLAY_TRIPLETS="${PROJECT_ROOT}/vcpkg/triplets" \
    -DENABLE_TESTS=OFF \
    -DBUILD_TOOLS=OFF \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

echo ""
echo "=== Step 2: Build ==="
cmake --build "$BUILD_DIR" -j$(nproc 2>/dev/null || sysctl -n hw.logicalcpu 2>/dev/null || echo 4)

echo ""
echo "=== Build Complete ==="
echo "Output files:"
ls -la "$BUILD_DIR/krkr2."* 2>/dev/null || echo "  (no output files found)"
echo ""
echo "To test locally, run a web server in the build directory:"
echo "  cd $BUILD_DIR && python3 -m http.server 8080"
echo "  Then open http://localhost:8080/krkr2.html"
