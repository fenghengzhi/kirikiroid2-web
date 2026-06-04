---
name: local-motion-playback-differential-unreliable
description: LOCAL run_motion_playback_wasmtime.py reports m2logo=100 frames (FAIL vs spec 93) on this macOS-arm64 machine, but CI is GREEN (port 93 = oracle 93). Local motion_playback differential is NOT a reliable oracle here — trust CI's motion-playback-compare job, not local frame counts.
metadata:
  type: project
---

2026-06-04. Confirmed by independent main-loop investigation after two implementer agents
and I all misread a LOCAL anomaly as a code regression.

## The trap
- LOCAL `python3 tests/differential/python/run_motion_playback_wasmtime.py --case m2logo`
  (both `--only-structural` AND CI's `--skip-golden-diff` mode) aborts with:
  "Wasmtime segment 0 (m2logo) has 100 frames; spec requires exactly 93."
- This reproduces on clean HEAD f4cdc66 (stash all changes + rebuild krkr2_wasmtime_guest + run)
  AND with session changes — i.e. it is INDEPENDENT of any motionplayer edit.
- BUT CI run 26944928172 (push of f4cdc66, dev/motion) is GREEN: the `motion-playback-compare`
  job's `compare_motion_playback_traces.py` reports `| m2logo | ok | 93 | 93 | 31 | 31 | 0 |`
  and `| yuzulogo | ok | 243 | 243 | ... | 0 |` — port frames == oracle frames, 0 mismatches.

## Conclusion
- The committed code is CORRECT; CI (port=93=oracle) is the authoritative guard.
- The local 100-frame result is an ENVIRONMENT divergence on this macOS-arm64 dev machine
  (host wasm build and/or this machine's local `wasmtime` python runtime executes the port to
  100 frames; CI's ubuntu-22.04 x86_64 to 93). Same source, same emsdk 4.0.23 — cause not
  pinned (candidates: host-arch codegen leak, stale local incremental build dir, local wasmtime
  package version vs CI). NOT a frame-count flag-mode artifact (both runner modes give 100).

## How to apply
- Do NOT treat a local `run_motion_playback_wasmtime.py` frame-count FAIL as a code regression.
- Do NOT use the local motion_playback differential as a green/red verification oracle on this
  machine. For motion-subsystem non-regression, push the branch and read CI's
  `motion-playback-compare` job (`gh run view --repo fenghengzhi/kirikiroid2-web --job <id> --log`),
  or `gh run list`. Local `cmake --build` (clean compile) is still valid; only the local
  differential frame count is not.
- The reseek/advanceNodeFrames convergence work this session builds clean and is inert by
  construction; its authoritative verification is CI, not local frame counts.
- If the local-vs-CI divergence itself needs fixing (so local differential becomes usable),
  triage separately: compare local vs CI wasmtime runtime version, and try a from-scratch
  (non-incremental) out/wasmtime/debug rebuild first.
