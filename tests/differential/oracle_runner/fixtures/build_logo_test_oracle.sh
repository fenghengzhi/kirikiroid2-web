#!/usr/bin/env bash
# Repack reference/xp3/logo_test_oracle.xp3 from its sources in the
# `reference` submodule. The output is a deterministic-playback variant
# of reference/xp3/logo_test.xp3 used by the motion_playback adapter
# for recording oracle goldens — see `tests/differential/oracle_runner/
# adapters/motion_playback.py`.
#
# Sources live in the submodule so they travel alongside the other
# reference assets (.mtn files, the original logo_test.xp3, etc):
#   reference/xp3/logo_test_oracle/startup.tjs       (custom startup)
#   reference/xp3/logo_test/{yuzulogo,m2logo}.mtn    (shared mtns)
#   reference/xp3/logo_test_oracle.xp3               (build output)
#
# Run from repo root:
#   tests/differential/oracle_runner/fixtures/build_logo_test_oracle.sh
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../../../.." && pwd)"
SRC_TJS="$REPO_ROOT/reference/xp3/logo_test_oracle/startup.tjs"
SRC_MTN_DIR="$REPO_ROOT/reference/xp3/logo_test"
OUT="$REPO_ROOT/reference/xp3/logo_test_oracle.xp3"
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

rm -f "$OUT"
# Flat arcpaths (startup.tjs at archive root, no directory prefix). The
# game's startup path lookup searches for "startup.tjs" at the xp3 root;
# shipping the file under `logo_test/startup.tjs` makes it invisible and
# cocos2d silently does nothing after mounting (reproducible: 0 Frida
# events, no "Loading startup script" log line).
"$XP3PACK" -o "$OUT" \
    --map \
        "startup.tjs=$SRC_TJS" \
        "yuzulogo.mtn=$SRC_MTN_DIR/yuzulogo.mtn" \
        "m2logo.mtn=$SRC_MTN_DIR/m2logo.mtn"

echo "Built $OUT"
