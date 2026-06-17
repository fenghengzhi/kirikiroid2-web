#!/usr/bin/env bash
# Repack reference/xp3/logo_test*.xp3 from their sources in the `reference`
# submodule. The live fixture is a minimal Senren logo repro that preserves the
# .ks-facing [ev]/[waitmovie] boundary and then delegates motion to the game's
# AffineSourceMotion.tjs. The oracle fixtures use the same
# KAG/AffineSourceMotion playback path with deterministic delta timing for
# differential recording.
#
# Sources live in the submodule so they travel alongside the other
# reference assets (.mtn files, the original logo_test.xp3, etc):
#   reference/xp3/logo_test/                         (live fixture tree)
#   reference/xp3/logo_test_oracle/startup.tjs       (oracle startup)
#   reference/xp3/logo_test.xp3                      (build output)
#   reference/xp3/logo_test_oracle.xp3               (build output)
#   reference/xp3/logo_test_oracle_yuzulogo.xp3      (single-motion output)
#   reference/xp3/logo_test_oracle_m2logo.xp3        (single-motion output)
#   reference/xp3/logo_test_oracle_yuzulogo_psb.xp3  (PSB v2 single-motion output)
#   reference/xp3/logo_test_oracle_m2logo_psb.xp3    (PSB v2 single-motion output)
#   reference/xp3/logo_test_oracle_title_bg.xp3      (single-motion output)
#
# Run from repo root:
#   tests/differential/oracle_runner/fixtures/build_logo_test_oracle.sh
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
SRC_ORACLE_DIR="$REPO_ROOT/reference/xp3/logo_test_oracle"
SRC_YUZU_DIR="$REPO_ROOT/reference/xp3/logo_test_oracle_yuzulogo"
SRC_M2_DIR="$REPO_ROOT/reference/xp3/logo_test_oracle_m2logo"
SRC_YUZU_PSB_DIR="$REPO_ROOT/reference/xp3/logo_test_oracle_yuzulogo_psb"
SRC_M2_PSB_DIR="$REPO_ROOT/reference/xp3/logo_test_oracle_m2logo_psb"
SRC_TITLE_BG_DIR="$REPO_ROOT/reference/xp3/logo_test_oracle_title_bg"
SRC_TJS="$SRC_ORACLE_DIR/startup.tjs"
SRC_LIVE_DIR="$REPO_ROOT/reference/xp3/logo_test"
SRC_MTN_DIR="$REPO_ROOT/reference/xp3/logo_test"
SRC_TITLE_BG_ASSET_DIR="$REPO_ROOT/reference/xp3/title_bg_motion"
OUT="$REPO_ROOT/reference/xp3/logo_test_oracle.xp3"
OUT_LIVE="$REPO_ROOT/reference/xp3/logo_test.xp3"
OUT_YUZU="$REPO_ROOT/reference/xp3/logo_test_oracle_yuzulogo.xp3"
OUT_M2="$REPO_ROOT/reference/xp3/logo_test_oracle_m2logo.xp3"
OUT_YUZU_PSB="$REPO_ROOT/reference/xp3/logo_test_oracle_yuzulogo_psb.xp3"
OUT_M2_PSB="$REPO_ROOT/reference/xp3/logo_test_oracle_m2logo_psb.xp3"
OUT_TITLE_BG="$REPO_ROOT/reference/xp3/logo_test_oracle_title_bg.xp3"
XP3PACK="${XP3PACK:-$REPO_ROOT/tools/bin/mac/rel/xp3pack}"

if [[ ! -x "$XP3PACK" ]]; then
    echo "xp3pack not found at $XP3PACK. Set XP3PACK env or build with" >&2
    echo "  cmake --preset 'MacOS Release Config' -DBUILD_TOOLS=ON \\" >&2
    echo "    -DBISON_EXECUTABLE=/opt/homebrew/opt/bison/bin/bison &&" >&2
    echo "  cmake --build out/macos/release --target xp3pack" >&2
    exit 1
fi

if [[ ! -f "$SRC_TJS" ]]; then
    echo "custom startup.tjs missing at $SRC_TJS" >&2
    echo "Initialise the reference submodule: git submodule update --init reference" >&2
    exit 1
fi
if [[ ! -f "$SRC_LIVE_DIR/startup.tjs" ]]; then
    echo "live startup.tjs missing at $SRC_LIVE_DIR/startup.tjs" >&2
    echo "Initialise the reference submodule: git submodule update --init reference" >&2
    exit 1
fi
if [[ ! -f "$SRC_TITLE_BG_ASSET_DIR/title_bg.mtn" ]]; then
    echo "title_bg.mtn missing at $SRC_TITLE_BG_ASSET_DIR/title_bg.mtn" >&2
    echo "Initialise the reference submodule: git submodule update --init reference" >&2
    exit 1
fi
for dir in "$SRC_YUZU_DIR" "$SRC_M2_DIR" "$SRC_YUZU_PSB_DIR" "$SRC_M2_PSB_DIR" "$SRC_TITLE_BG_DIR"; do
    if [[ ! -f "$dir/startup.tjs" || ! -f "$dir/logo.ks" ]]; then
        echo "single-motion oracle source missing in $dir" >&2
        echo "Expected startup.tjs and logo.ks in the reference submodule." >&2
        exit 1
    fi
done

rm -f "$OUT" "$OUT_LIVE" "$OUT_YUZU" "$OUT_M2" "$OUT_YUZU_PSB" "$OUT_M2_PSB" "$OUT_TITLE_BG"
# Flat arcpaths (startup.tjs at archive root, no directory prefix). The
# game's startup path lookup searches for "startup.tjs" at the xp3 root;
# shipping the file under `logo_test/startup.tjs` makes it invisible and
# cocos2d silently does nothing after mounting (reproducible: 0 Frida
# events, no "Loading startup script" log line).
build_oracle() {
    local out="$1"
    local src_dir="$2"
    # only_motion: when set, ship exactly this one motion container and drop
    # every other .mtn/.psb in the asset tree. logo_test carries several motion
    # payloads side by side: the v3 .mtn re-exports (yuzulogo.mtn, m2logo.mtn)
    # and the v2 PSB originals shipped under a .mtn name (yuzulogo_v2.mtn,
    # m2logo_v2.mtn — PSB v2 content routed through Motion.Player by extension).
    # The filter spans both extensions so a future .psb asset is dropped too.
    local only_motion="${3:-}"
    local asset_dir="${4:-$SRC_LIVE_DIR}"
    local maps=("startup.tjs=$src_dir/startup.tjs")
    local rel

    if [[ -f "$src_dir/logo.ks" ]]; then
        maps+=("logo.ks=$src_dir/logo.ks")
    fi

    while IFS= read -r -d '' file; do
        rel="${file#$asset_dir/}"
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
    done < <(find "$asset_dir" -type f -print0 | sort -z)
    "$XP3PACK" -o "$out" --map "${maps[@]}"
}

build_oracle "$OUT" "$SRC_ORACLE_DIR"
build_oracle "$OUT_YUZU" "$SRC_YUZU_DIR" "yuzulogo.mtn"
build_oracle "$OUT_M2" "$SRC_M2_DIR" "m2logo.mtn"
build_oracle "$OUT_YUZU_PSB" "$SRC_YUZU_PSB_DIR" "yuzulogo_v2.mtn"
build_oracle "$OUT_M2_PSB" "$SRC_M2_PSB_DIR" "m2logo_v2.mtn"
build_oracle "$OUT_TITLE_BG" "$SRC_TITLE_BG_DIR" "title_bg.mtn" "$SRC_TITLE_BG_ASSET_DIR"

(
    cd "$SRC_LIVE_DIR"
    "$XP3PACK" -o "$OUT_LIVE" --recursive .
)

echo "Built $OUT"
echo "Built $OUT_YUZU"
echo "Built $OUT_M2"
echo "Built $OUT_YUZU_PSB"
echo "Built $OUT_M2_PSB"
echo "Built $OUT_TITLE_BG"
echo "Built $OUT_LIVE"
