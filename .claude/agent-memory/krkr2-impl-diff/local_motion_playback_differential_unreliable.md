---
name: local-motion-playback-differential-per-case-xp3
description: LOCAL motion_playback verification MUST use the per-case xp3 (reference/xp3/logo_test_oracle_<case>.xp3) that CI uses — NOT the runner's default --startup-xp3 (reference/xp3/logo_test_oracle.xp3, a different/combined file). Using the default gives wrong frame counts (m2logo 100 vs correct 93) and never exercises the real per-case path, masking hangs/regressions.
metadata:
  type: project
---

2026-06-04. CORRECTED root cause (earlier version of this note WRONGLY blamed a
"macOS-arm64 environment divergence" — that was wrong).

## The real trap
`run_motion_playback_wasmtime.py` defaults `--startup-xp3` to
`reference/xp3/logo_test_oracle.xp3` (a combined/legacy file). But CI
(`.github/workflows/differential.yml`, the wasmtime job loop) runs each case with a
PER-CASE xp3:
```
for case in yuzulogo m2logo; do
  xp3="reference/xp3/logo_test_oracle_${case}.xp3"
  python3 ... run_motion_playback_wasmtime.py --startup-xp3 "$xp3" --case "$case" --skip-golden-diff ...
done
```
Files present locally: logo_test_oracle.xp3 (default, WRONG for per-case),
logo_test_oracle_m2logo.xp3, logo_test_oracle_yuzulogo.xp3, logo_test_oracle_title_bg.xp3.

## Symptoms when you use the WRONG (default) xp3
- m2logo reports 100 frames → runner aborts "has 100 frames; spec requires exactly 93"
  (the frame-count gate fires even in `--skip-golden-diff` mode).
- This is NOT a code regression and NOT an env/arch difference — it is the wrong input.
- With the CORRECT `--startup-xp3 reference/xp3/logo_test_oracle_m2logo.xp3`, m2logo
  matches CI: f4cdc66 → 93 frames PASS.

## How to apply (ALWAYS, for local motion_playback verification)
```
python3 tests/differential/python/run_motion_playback_wasmtime.py \
  --case m2logo  --startup-xp3 reference/xp3/logo_test_oracle_m2logo.xp3  --only-structural
python3 tests/differential/python/run_motion_playback_wasmtime.py \
  --case yuzulogo --startup-xp3 reference/xp3/logo_test_oracle_yuzulogo.xp3 --only-structural
```
- Local IS a reliable oracle WITH the per-case xp3. Build wasmtime guest first:
  `cmake --build out/wasmtime/debug --target krkr2_wasmtime_guest`.
- This mistake (using default xp3) burned multiple agents + the main loop on 2026-06-04:
  it both produced the bogus "100-frame regression" scare AND hid a real m2logo HANG in the
  advanceNodeFrames (0x6B7E44) convergence — yuzulogo passed, m2logo timed out (1230s) in CI
  because the default-xp3 local run never executed m2logo's real per-case path. See
  [[advancenodeframes-0x6b7e44-convergence]].
- Instruct any implementer/auditor agent that touches frame-progress to verify BOTH cases
  with their per-case xp3 locally before claiming "logo-inert".
