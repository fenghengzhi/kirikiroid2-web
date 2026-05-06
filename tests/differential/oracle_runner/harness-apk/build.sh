#!/usr/bin/env bash
# Rebuild krkr2-harness.apk: decompile reference/apk/krkr2 1.4.4.apk,
# inject our HarnessActivity class + manifest entry + libharness.so, then
# resign with the checked-in test keystore.
#
# Outputs prebuilt/krkr2-harness.apk. Requires:
#   - apktool (brew install apktool)
#   - Android SDK build-tools 34.0.0 at $ANDROID_HOME
#   - OpenJDK 11+ (openjdk@21 on macOS homebrew is fine)
#   - Prebuilt libharness.so at ../harness/prebuilt/libharness.so
#
# CI picks this up from $ANDROID_HOME and $JAVA_HOME exported in the
# workflow. Local dev: the script auto-detects openjdk@21 from homebrew.

set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$HERE/../../../.." && pwd)

ORIGINAL_APK="${ORIGINAL_APK:-$REPO_ROOT/reference/apk/krkr2 1.4.4.apk}"
LIBHARNESS_SO="${LIBHARNESS_SO:-$HERE/../harness/prebuilt/libharness.so}"

# Toolchain discovery
ANDROID_HOME="${ANDROID_HOME:-$HOME/Library/Android/sdk}"
BUILD_TOOLS="${BUILD_TOOLS:-$ANDROID_HOME/build-tools/34.0.0}"
D8="$BUILD_TOOLS/d8"
ZIPALIGN="$BUILD_TOOLS/zipalign"
APKSIGNER="$BUILD_TOOLS/apksigner"
ANDROID_JAR_DIR="$ANDROID_HOME/platforms"
ANDROID_JAR=$(ls -d "$ANDROID_JAR_DIR"/android-*/android.jar 2>/dev/null | sort -V | tail -1)
APKTOOL="${APKTOOL:-apktool}"

if [[ -z "${JAVA_HOME:-}" ]] && [[ -d /opt/homebrew/opt/openjdk@21 ]]; then
    export JAVA_HOME=/opt/homebrew/opt/openjdk@21
    export PATH="$JAVA_HOME/bin:$PATH"
fi

for tool in "$APKTOOL" "$D8" "$ZIPALIGN" "$APKSIGNER"; do
    if ! command -v "$tool" >/dev/null && [[ ! -x "$tool" ]]; then
        echo "missing tool: $tool" >&2
        exit 1
    fi
done
[[ -f "$ORIGINAL_APK" ]] || { echo "missing original APK: $ORIGINAL_APK" >&2; exit 1; }
[[ -f "$LIBHARNESS_SO" ]] || { echo "missing libharness.so: $LIBHARNESS_SO — build it first" >&2; exit 1; }
[[ -n "$ANDROID_JAR" ]] || { echo "no android.jar in $ANDROID_JAR_DIR" >&2; exit 1; }

BUILD="$HERE/build"
rm -rf "$BUILD"
mkdir -p "$BUILD" "$HERE/prebuilt"

echo "[1/6] Decoding APK with apktool..."
# --no-src skips baksmali on classes.dex — we keep the original classes.dex
# intact and ship our new class as classes2.dex (multidex).
"$APKTOOL" d -f --no-src -o "$BUILD/decoded" "$ORIGINAL_APK"

echo "[2/6] Compiling HarnessActivity.java..."
mkdir -p "$BUILD/java"
# --release 8 because KR2Activity/Cocos2dxActivity are Java 8 bytecode
# compiled with older toolchains; staying at the same level keeps bytecode
# compatibility bulletproof. (d8 doesn't mind higher targets but javac does.)
#
# Classpath needs the KR2Activity symbol. Extract from the original APK
# to a temporary jar so javac can see it. Using the original classes.dex
# via d2j would be circular; simpler: extract via apktool's decoded jar.
# apktool 3.x by default produces $decoded/dist/*.jar — in --no-src mode
# we don't get that, so fall back to unzipping classes.dex and running
# d8 in "reverse": no. Simplest: build a stub KR2Activity we compile
# against, since we only need the method signatures (onWindowFocusChanged
# + onCreate). The APK's own classes.dex provides the runtime impl.
STUB_DIR="$BUILD/java/stub"
mkdir -p "$STUB_DIR/org/tvp/kirikiri2"
cat > "$STUB_DIR/org/tvp/kirikiri2/KR2Activity.java" <<'EOF'
// Compile-time-only stub — at runtime the real class from classes.dex
// is used. We only declare the surface area HarnessActivity touches.
package org.tvp.kirikiri2;
public class KR2Activity extends android.app.Activity {
    public void onWindowFocusChanged(boolean hasFocus) {}
}
EOF

javac --release 8 -d "$BUILD/java" -cp "$ANDROID_JAR" \
    "$STUB_DIR/org/tvp/kirikiri2/KR2Activity.java" \
    "$HERE/HarnessActivity.java"

echo "[3/6] Dexing HarnessActivity.class..."
# Only dex the real HarnessActivity; the KR2Activity stub is compile-only.
# d8 produces classes.dex by default — rename to classes2.dex.
mkdir -p "$BUILD/dex"
"$D8" --release --min-api 29 --output "$BUILD/dex" \
    "$BUILD/java/org/github/krkr2/HarnessActivity.class"
mv "$BUILD/dex/classes.dex" "$BUILD/decoded/classes2.dex"
python3 - "$BUILD/decoded/classes2.dex" <<'PY'
import sys
from pathlib import Path

dex = Path(sys.argv[1]).read_bytes()
for needle in (
    b"libharness loaded; starting RPC server",
    b"classLoad",
    b"server thread started from ",
    b"onCreate",
    b"onWindowFocusChanged",
    b"listening on 127.0.0.1:",
):
    if needle not in dex:
        raise SystemExit(f"classes2.dex missing marker: {needle!r}")
print("classes2.dex contains HarnessActivity startup markers")
PY

echo "[4/6] Patching AndroidManifest.xml + copying libharness.so..."
python3 "$HERE/patch_manifest.py" \
    "$BUILD/decoded/AndroidManifest.xml" \
    "$HERE/manifest-patch.xml"
mkdir -p "$BUILD/decoded/lib/arm64-v8a"
cp "$LIBHARNESS_SO" "$BUILD/decoded/lib/arm64-v8a/libharness.so"

echo "[5/6] Rebuilding + zipaligning..."
"$APKTOOL" b -o "$BUILD/unaligned.apk" "$BUILD/decoded"
"$ZIPALIGN" -p -f 4 "$BUILD/unaligned.apk" "$BUILD/aligned.apk"

echo "[6/6] Signing..."
PASS="$(cat "$HERE/keystore/harness-test.password")"
"$APKSIGNER" sign \
    --ks "$HERE/keystore/harness-test.jks" \
    --ks-pass "pass:$PASS" \
    --key-pass "pass:$PASS" \
    --out "$HERE/prebuilt/krkr2-harness.apk" \
    "$BUILD/aligned.apk"
"$APKSIGNER" verify --verbose "$HERE/prebuilt/krkr2-harness.apk" | head -5

echo
echo "Output: $HERE/prebuilt/krkr2-harness.apk"
ls -la "$HERE/prebuilt/krkr2-harness.apk"
