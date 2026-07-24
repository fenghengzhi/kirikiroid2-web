#!/usr/bin/env bash
# Build the deterministic 15 Hz motion_playback oracle fixtures.
#
# Small fixture scripts are tracked beside this file so the simulation cadence
# and KAG playback path are reviewable. Large game/system assets remain in the
# external reference tree and are copied into each XP3 by arcpath.
#
# Inputs:
#   tests/differential/oracle_runner/fixtures/logo_test_oracle*_15hz/
#   reference/xp3/logo_test/ (shared scripts, images and motion containers)
# Outputs:
#   reference/xp3/logo_test_oracle_15hz.xp3
#   reference/xp3/logo_test_oracle_yuzulogo_15hz.xp3
#   reference/xp3/logo_test_oracle_m2logo_15hz.xp3
#
# Run from the repository root:
#   tests/differential/oracle_runner/fixtures/build_logo_test_oracle.sh
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
FIXTURE_ROOT="$REPO_ROOT/tests/differential/oracle_runner/fixtures"
SRC_COMBINED_DIR="$FIXTURE_ROOT/logo_test_oracle_15hz"
SRC_YUZU_DIR="$FIXTURE_ROOT/logo_test_oracle_yuzulogo_15hz"
SRC_M2_DIR="$FIXTURE_ROOT/logo_test_oracle_m2logo_15hz"
REFERENCE_XP3_DIR="$REPO_ROOT/reference/xp3"
ASSET_DIR="$REFERENCE_XP3_DIR/logo_test"
OUT_COMBINED="$REFERENCE_XP3_DIR/logo_test_oracle_15hz.xp3"
OUT_YUZU="$REFERENCE_XP3_DIR/logo_test_oracle_yuzulogo_15hz.xp3"
OUT_M2="$REFERENCE_XP3_DIR/logo_test_oracle_m2logo_15hz.xp3"
XP3PACK="${XP3PACK:-$REPO_ROOT/tools/bin/mac/rel/xp3pack}"

if [[ ! -x "$XP3PACK" ]]; then
    echo "xp3pack not found at $XP3PACK. Set XP3PACK or build with:" >&2
    echo "  cmake --preset 'MacOS Release Config' -DBUILD_TOOLS=ON \\" >&2
    echo "    -DBISON_EXECUTABLE=/opt/homebrew/opt/bison/bin/bison" >&2
    echo "  cmake --build out/macos/release --target xp3pack" >&2
    exit 1
fi

for path in \
    "$SRC_COMBINED_DIR/startup.tjs" \
    "$SRC_YUZU_DIR/startup.tjs" \
    "$SRC_YUZU_DIR/logo.ks" \
    "$SRC_M2_DIR/startup.tjs" \
    "$SRC_M2_DIR/logo.ks" \
    "$ASSET_DIR/startup.tjs" \
    "$ASSET_DIR/logo.ks" \
    "$ASSET_DIR/yuzulogo.mtn" \
    "$ASSET_DIR/m2logo.mtn"; do
    if [[ ! -f "$path" ]]; then
        echo "required oracle fixture input missing: $path" >&2
        exit 1
    fi
done

BUILD_DIR="$(mktemp -d "$REFERENCE_XP3_DIR/.motion-oracle-15hz.XXXXXX")"
trap 'rm -rf "$BUILD_DIR"' EXIT

# Flat arcpaths are required: the startup lookup searches for startup.tjs at
# the archive root. When only_motion is set, retain exactly that motion payload
# while copying all shared scripts/images used by AffineSourceMotion.
build_oracle() {
    local out="$1"
    local src_dir="$2"
    local only_motion="${3:-}"
    local maps=("startup.tjs=$src_dir/startup.tjs")
    local file rel

    if [[ -f "$src_dir/logo.ks" ]]; then
        maps+=("logo.ks=$src_dir/logo.ks")
    fi

    while IFS= read -r -d '' file; do
        rel="${file#$ASSET_DIR/}"
        if [[ "$rel" == "startup.tjs" ]]; then
            continue
        fi
        if [[ "$rel" == "logo.ks" && -f "$src_dir/logo.ks" ]]; then
            continue
        fi
        if [[ -n "$only_motion" && ( "$rel" == *.mtn || "$rel" == *.psb ) \
              && "$rel" != "$only_motion" ]]; then
            continue
        fi
        maps+=("$rel=$file")
    done < <(find "$ASSET_DIR" -type f -print0 | sort -z)

    "$XP3PACK" -o "$out" --map "${maps[@]}"
}

build_oracle "$BUILD_DIR/combined.xp3" "$SRC_COMBINED_DIR"
build_oracle "$BUILD_DIR/yuzulogo.xp3" "$SRC_YUZU_DIR" "yuzulogo.mtn"
build_oracle "$BUILD_DIR/m2logo.xp3" "$SRC_M2_DIR" "m2logo.mtn"

# Publish only after all three archives were built successfully, so one failed
# pack never removes or partially refreshes the previous fixture set.
mv -f "$BUILD_DIR/combined.xp3" "$OUT_COMBINED"
mv -f "$BUILD_DIR/yuzulogo.xp3" "$OUT_YUZU"
mv -f "$BUILD_DIR/m2logo.xp3" "$OUT_M2"

echo "Built deterministic 15 Hz oracle fixtures:"
shasum -a 256 "$OUT_COMBINED" "$OUT_YUZU" "$OUT_M2"
