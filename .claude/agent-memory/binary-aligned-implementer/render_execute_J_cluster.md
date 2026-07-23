---
name: render-execute-J-cluster
description: motionplayer render pipeline 0x6C2334/0x6C4E28/0x6C7440 phase topology, current J1/J7/RM.bufLayer alignment, and differential-validation limits
metadata:
  type: project
---

# Render execute cluster (0x6C2334 / 0x6C4E28 / 0x6C7440) topology

**Local↔binary function map (Frida-confirmed, NOT what audit K text implies):**
- `0x6C2334` build = local `buildPreparedRenderItems` (PlayerRenderItems.cpp). Emits `build_items_*` trace. Writes item fields, NEVER +424/+20/leaf-copy.
- `0x6C4E28` Player_emitRenderItem_requireLayer = local `buildRenderCommands` (PlayerRenderExecute.cpp). Emits `build_commands_*` trace. **Frida `PLAYER_BUILD_COMMANDS_OFF=0x6C4E28`** (frida_motion_stage_agent.js) hooks build_commands_leave at 0x6C4E28's onLeave. So buildRenderCommands IS the 0x6C4E28 counterpart — the requireLayerId/item+20 latch living at PlayerRenderExecute.cpp:83+ is in the RIGHT binary function. Audit K's "moved to build loop" conflated buildRenderCommands with 0x6C2334; the P1-I3 "move back to execute" framing is partly a misread.
- `0x6C7440` Player_renderToCanvas = local `executeLayerRenderCommands` / `renderToCanvasLike_0x6C7440`.

**0x6C4E28 (fresh 2026-07-23) structure:** Loop A over mainList: `if(!item+19 drawFlag) skip`; clip=`a4(camera)∩paintBox`，仅 valid viewport 做 floor/ceil narrow，结果保留 FLOAT；非空且 !item+16 时写 item+21/+216..228，requireLayerId latch 后以 sub_6C6B48 获取 item+304。acquire out-byte 只在 true 时执行 descriptor→color→source→accessor、neutralColor=0、Real setSize、affine/Bezier/meshCopy。Loop B 以 group paintBox 为 seed union child **paintBox**，camera tuple只做 empty gate，viewport tuple驱动 Real setSize + argc5 fillRect 与 grp+216..228；child mask只以 child+21/leaf Variant tag门控，float 差到最终参数才 FCVTZS。

**0x6C6B48 absolute (J6):** a1=SLA(player+760). `absolute = SLA+160 + SLA+164` then `++SLA+164`, hitThreshold=256. NOT "node.x+node.y" (audit J6 imprecise). It's a per-SLA monotonic counter+base → local `_nextLayerAbsolute++` is ordering-equivalent (differs by const base, unobservable). Lives in leaf path.

**0x6C4E28 pass lifecycle (fresh 2026-07-23):** `0x6C4E74..0x6C4F14` swaps SLA active/retired Rb_trees and resets sequence before both loops; loop-local lazy SLA creation repeats the same initialization. `0x6C63B8 -> sub_6C72E4` only on normal tail Invalidates and deletes retired nodes not moved back by acquire. Exception landing pads skip this cleanup. Local therefore calls explicit `beginRenderLayerPassLike_0x6C4E28` at entry/lazy-create and `end...` only at normal tail; never use RAII for this pair.

**0x6C7440 (fresh re-decompiled 2026-07-23) THREE draw mechanisms：**
1. 0x6C4E28 LEAF (item+304): affineCopy onto PERSISTENT per-item leaf Layer in SLA Rb_tree.
2. 0x6C7440 BUFFERED (LABEL_63, blend 1..5 或 `completionType(player+1144)!=0 || item+264`): copy onto SHARED bufLayer=player+656 ResourceManager property (NOT item+304), ancestor-mask 后 operateRect。
3. 0x6C7440 DIRECT: operateAffine onto v370, -0.5,-0.5 world offset.
当前本地已拆开三者：0x6C4E28 leaf/group pre-walk 走 SLA ordered-map；0x6C7440 direct 提交 current source；buffered 对同一次 source resolve 使用 RM.bufLayer。旧 `buildItemOutput` 已删除；`Player::_renderLayerStates` 只残留于 HEADLESS accurate-SLA 诊断。

## DONE 2026-06-07 (verified non-regressing)
- **J9** priorDraw opacity halving (0x6c764c-0x6c7668): source-level signed `opa/2`，不 clamp、不在折半后重新 gate。Ported exactly into executeLayerRenderCommands top-level loop。
- **J4** removed redundant `renderLayer->Update(false)` in execute tail (0x6c8fcc has setClip(argc0)+release only; `L"Update"`=0 in whole 0x6C7440). Update belongs to wrapper (renderToLayer:1225 / updateLayerAfterDraw 0x6CE7D8). Was firing Update twice/draw. PNG byte-identical after removal.

## DONE 2026-06-07: J1/J7/J6 leaf-tier relocation (user override of prior STOP)
- **J1/J7 PORTED**: leaf-copy emit 已从旧 `buildItemOutput` 递归迁到独立 `buildRenderCommands`（0x6C4E28 counterpart），通过 SLA ordered-map/reuse pool 物化 item+304；Loop B 物化 item+324。0x6C7440 自身 flat-submit：direct 使用 current source；buffered 也从同一次 current source resolve 复制到 RM.bufLayer，再用 ancestor leaf/composed 作为 mask，绝不把预建 leaf/composed 当 buffered final source。旧 `buildItemOutput` 已删除，`Player::_renderLayerStates` 只留在 HEADLESS accurate-SLA 诊断。
- **J6 absolute** already structurally aligned in SLA: binary `absolute=SLA+160+SLA+164;++SLA+164` == SLA `_absolute+_assignSequence;++_assignSequence`. No change needed.
- **Loop B precision TRAPS confirmed vs 0x6C4E28**: (1) union accumulates child PAINTBOX (child+184..196=v101[46..49]) NOT child clipRect; (2) TWO rect tuples — EMPTY test uses CAMERA-clamped (v102>v100||v97>v105) but composed SIZE + grp+216..228 write use VIEWPORT-NARROWED (v109/v108/v107/v106); (3) **2026-07-23 纠错**：`child+320` 是 `child+304 leafLayer tTJSVariant` 的 type tag，不是恒零独立字段；本地现以 `leafLayer.Type()!=tvtVoid` 恢复可达 alpha-mask body。`grp+340` 同理是 composedLayer@+324 的 tag。
- **验证边界**：现有 logo fixture 不覆盖 0x6C4E28 leaf/group 或 parent-buffered 链，PNG/trace 只能作为 direct 非回归，不是这些 inert 分支的正确性证明。
- **2026-07-23 已关闭**：RM.bufLayer submit、完整 float clip/Real setSize、completionType/parent direct gate、Layer-class receiver/target objthis、leaf neutralColor 顺序、execute descriptor/color/source/accessor 生命周期、TJS 异常 unwind、priorDraw pre-walk gate。仍开放：J8 accurate-SLA tree；0x6C6B48 caller-local command payload专用值类型；0x6C7440 wrapper/executor 源码函数拆分。

## Differential baseline (the green this cluster protects)
- Locally reproducible green = (a) trace compare PASS, (b) render-steps **build_flow item-field mismatch=0** (the ITEM_FIELDS per-item semantic diff incl flags.layerResolved20/layerIds/clipRect/buildClipRect/sortKey64/leafBuilt/composedBuilt/executedDirect). NOT the full render-steps run.
- Full render-steps compare FAILS locally on execute_post/post_draw rgbaSha256 + render_prepare_shape — those are STALE committed oracle pixel hashes (2026-05-10) vs current GL provider; CI records fresh device oracle each run. They are NOT this task's signal and can't be matched locally.
- Local pixel-regression self-guard: wasmtime guest writes 1008 execute_post PNGs (`--record-render-step-checkpoints`); snapshot shasum before/after, diff for direct-path regressions.
- Run: build `cmake --build out/wasmtime/debug --target krkr2_wasmtime_guest`; then per case `run_motion_playback_wasmtime.py --record-render-stages --record-render-step-checkpoints --checkpoint-render-only`. `timeout` cmd absent on macOS — don't wrap.
- web/debug cache can hold a STALE truncated `CMAKE_TOOLCHAIN_FILE=/upstream/emscripten/...` (missing $EMSDK prefix) from a prior-session env → configure fails with "could not find Emscripten.cmake". Fix: `rm out/web/debug/CMakeCache.txt CMakeFiles` + re-run `cmake --preset "Web Debug Config"`. Not a code error.
