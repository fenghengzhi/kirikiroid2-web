# CLUSTER I — Player render pipeline alignment audit (2026-05-30)

> Authoritative source: libkrkr2.so (IDB libkrkr2.so.i64). Read-only audit; IDB
> renames/comments applied + idb_save. No cpp/ edits.
> Method: per-function decompile -> pseudocode -> local counterpart -> compare.

> **2026-07-23 现状纠正（取代本文的 phase/container 旧裁决）：**
> `0x6C4E28` 是 `0x6C7440` 在 `!priorDraw` 时调用的 leaf/group pre-walk；
> 本地虽仍沿用 `buildRenderCommands` 这个历史函数名，但现在就在该调用点执行，
> `requireLayerId`、item+20/+21、leaf copy 与 group compose 均在这次 pre-walk 内完成，
> 不再存在本文所称的 build→execute 相位搬移。Loop B 以 group 自身 paintBox 为
> seed，union 的也是有效 child 的 paintBox，不是 child clipRect。Android item 的
> 432B 是 ARM64 ABI 结果，不是 wasm32 应硬凑的对象布局；当前权威实现裁决见
> `analysis/audit_motionplayer_2026-06-07/clusterJ_render_execute.md`。

## Draw-dispatch call graph (verified)

```
Player.draw (NCB)  = Player_draw_NCBWrapper @0x6818D0
    this = *(player+1064)            ; EmoteObject->Player chain
    -> Player_drawCompat @0x6D5FB8 (== sub_6D5FB8, the 3-way dispatcher)
         if arg is D3DAdaptor (cls dword_1AB8820): set player+909=1; Player_drawD3D@0x6D5B90
         elif arg is SLA (cls dword_1AB87F8):        Player_DrawSLA@0x6D5658
         else: sub_6D5164(build mainList v40 + boundsList p)
               if player+909 (wasD3DMode): D3DAdaptor path (setSize/visible/renderFromPlayer/captureCanvas)
               else: Player_applyTranslateOffset
                     Player_renderToCanvas_guess@0x6C7440(player, layerArg, mainList, boundsList)
                     Player_updateLayerAfterDraw_assignImages@0x6CE7D8
```

- **Player_drawD3D @0x6D5B90**: sub_6D5164 -> applyTranslateOffset -> D3DAdaptor_renderFromPlayer. Thin.
- **Player_DrawSLA @0x6D5658**: branches on `byte_1AB84F4` (config `ogl_accurate_render` OR `hasGPUAccel_guess`, cached via cxa_guard byte_1AB84F8).
  - accurate OFF: `Player_ResolveSLATarget_guess@0x6D5948` -> `Player_RenderMotionFrame_guess@0x6DE738(buf, w,h, ...)` -> Layer_UpdateRect.
  - accurate ON: `sub_6C9CA8` (which itself calls Player_emitRenderItem_requireLayer@0x6C4E28) + sub_6CE938.
- **Player_ResolveSLATarget_guess @0x6D5948**: resolves/creates PrivateMotionGLL child layer under ownerLayer (player+40 dispatch, g_PrivateMotionGLL_ClassID), SetSize(w,h). Lazy-creates via PrivateMotionGLL_CreateClass + cxa_guard byte_1AB8580.
- **Player_updateLayerAfterDraw_assignImages @0x6CE7D8**: gate `player+613` (copied to +612). If set: sub_6CE19C + dispatch `assignImages(arg)` on layer player+696.
- **Player_drawToLayerCompat @0x6D2D80**: gate `*(player+544)`. nodeType==3 recursive child-draw with fillRect + recursion over child Players (drawToLayerCompat self-call).

## CRITICAL re-mapping: sub_6C4E28 is renderToCanvas 的 pre-walk

`sub_6C4E28` (renamed **Player_emitRenderItem_requireLayer**) is a render pre-walk
called from:
  - Player_renderToCanvas_guess @0x6c756c  (non-accurate layer path)
  - sub_6C9CA8 @0x6c9e74                    (accurate SLA path)

It runs inside execute/renderToCanvas before the submit loop. Two loops:
  - Loop A (over a2 mainList): per item, clip vs paintBox(+184)/viewport(+200),
    if drawable -> set item+21=1, write clipRect item+216..228, requireLayerId
    (LABEL_28) -> item+424=layerId, item+20=1, then emit affineCopy/meshCopy/
    bezierPatchCopy via TJS dispatch on the acquired Layer.
  - Loop B (over a3 boundsList): per group item, seed with group paintBox and
    union visible child paintBox values, acquire
    composed Layer (item+324 via Window.mainWindow.Layer ctor), setSize/fillRect,
    alpha-mask child loop (Motion_doAlphaMaskOperation), set item+21/16/216..228.

The current local `renderToCanvasLike_0x6C7440` calls `buildRenderCommands` only
under the same `!priorDraw` gate; that helper now owns the two `0x6C4E28` loops.
Thus the old P1-I3 phase-divergence verdict below is superseded; the helper name
does not move the operation to the earlier render-item construction function
`0x6C2334`.

`Player_buildRenderTree_guess @0x6CBCE4` is mis-named: it is actually
**get/acquireLayerById** — a std::map<int,LayerVariant> (Rb_tree keyed by layerId)
lookup-or-create that builds a `Layer` TJS object with absolute=node.x+y,
hitThreshold=256, and caches at map node+40. Suggest rename
`Player_acquireLayerById`. (Not renamed yet — left as _guess pending local symbol.)

## skipFlag1 (item+18) VERDICT — ALIGNED

> **2026-07-23 superseded correction：**下文关于“本地反相存储 `skipFlag1`”、
> `_renderItemInheritedFlag18` 侧挂以及 harness 再反相的描述只是 2026-05-30 历史状态，
> 已被当前实现取代。现在 `appendPreparedRenderItems(..., inheritedFlag18)` 直接携带
> a6，子 Player 递归传 `inheritedFlag18 || ownerNode.priorDraw != 0`；item+18 按原极性
> 写入历史遗留名称 `skipFlag1`，harness 直接输出该 bool，无侧挂、无反相层。
> 同时，raw consumer 仍是 `player+1096 && !item+18`，但 +1096 的 NCB
> 字面名是 `priorDraw`；`preview` 是 +1092。下文的“+1096 preview”也是旧命名。

Binary primary item-write @0x6c3380-0x6c33c0 (leaf render item v352 in sub_6C2334):
```
v298 = 1;                                   // default
item+17 = ((preview?1097:1089) & (1<<node+28)) == 0;   // skipFlag0
item+16 = node+201;                                     // rawFlag16
if ((a6 & 1) == 0) v298 = (node+48 != 0);   // node+48 = priorDraw
item+18 = v298;                              // == inheritedFlag18 || (node+48!=0)
```
- `node+48` PROVEN = priorDraw: sub_6BC4F0 @0x6bc6c4 writes
  `node+48 = sub_6636D4(emoteEdit,"priorDraw") & 1` only when node+1996(forceVisible),
  else 0 @0x6bc67c. Byte 0/1.
- Consumed in Player_renderToCanvas_guess @0x6c7630: `if(!item+18) skip`, reached
  ONLY when player+1096(preview) set (LABEL_16 gated by a1+1096). item+18=1 means draw.
- Local PlayerRenderItems.cpp:477 `skipFlag1 = !(inheritedFlag18 || (node.priorDraw != 0))`
  — EXACT negation match (local stores skip-sense). priorDraw write gate
  (PlayerUpdateGeometry.cpp:144-154) matches forceVisible+emoteEdit gate. a6
  propagation `_renderItemInheritedFlag18 || nodePriorDraw` (line 259) matches
  binary recursive a6 `(a6&1)||node+48`.
- VERDICT: skipFlag1 formula + node+48 semantics + a6 propagation ALL aligned.
  The residual m2logo items[1] frame12+ build_flow mismatch is NOT a skipFlag1
  formula bug; it must originate from WHICH node/order the item is built for, or
  the build-vs-execute phase placement of layer materialization (P1-I3), or a
  node+1996/priorDraw value timing difference between frames. Needs runtime trace
  of node.priorDraw + inheritedFlag18 + forceVisible at m2logo items[1] frame12.

## rawFlag20 (item+20) VERDICT — ALIGNED in the 0x6C4E28 pre-walk

Binary: item+20 latched =1 ONLY at LABEL_28 in Player_emitRenderItem_requireLayer
(execute phase), gated `drawFlag19 && drawable(v80<v84 && v83<v85 && !item+16) &&
item+20==0`. item+424=layerId from requireLayerId dispatch. Other paths never
touch item+20.
Local: latched by `buildRenderCommands`, which is now the local source-level body
for the `0x6C4E28` pre-walk and is called from `renderToCanvasLike_0x6C7440` under
the same `!priorDraw` gate. The historical function name is not a phase change.

## Render-item offset table (binary item = operator new(0x1B0)=432B; sub_6C2334 alloc @0x6c2754)

| bin off | type | binary write site / meaning | local field (NativeRenderItemFields / PreparedRenderItem) | status |
|---------|------|------------------------------|-----------------------------------------------------------|--------|
| +0  | _OWORD | =*(node+0) variant/source ref (0x6c3374, memset 0 @0x6c27a0) | srcRef (semantic) | ⚠ STL |
| +8  | tTJSVariant* | source key, AddRef'd (0x6c52a0) | srcRef/sourceKey | ⚠ STL |
| +16 | byte | rawFlag16 = node+201 (0x6c33a8) | rawFlag16 | ✅ |
| +17 | byte | skipFlag0 = ((preview?1097:1089)&(1<<nodeType))==0 (0x6c33a0) | skipFlag0 | ✅ |
| +18 | byte | skipFlag1 = inheritedFlag18||(node+48!=0) (0x6c33c0) | skipFlag1 (negated sense) | ✅ |
| +19 | byte | drawFlag = node+1960?1:node+1961 (build path) | drawFlag | ✅ (local adds needsGroupEntry term) |
| +20 | byte | rawFlag20 latch=1 at requireLayerId LABEL_28 (0x6c5240) | rawFlag20 | ✅ value / ⚠ phase (P1-I3) |
| +21 | byte | rawFlag21 drawable-clip-valid (0x6c4f88 set / 0x6c5e6c clear) | rawFlag21 | ✅ |
| +24/+32 | ptr/ptr | childItems vector begin/end (0x6c2768 memset) | childItems std::vector<PreparedRenderItem*> | ⚠ STL |
| +48 | int | blendMode = clip+364 (node mesh/composite) | blendMode | ✅ (default 16) |
| +52 | int | = node+16 (0x6c341c) | (objTriPriority region) | ❓ verify |
| +56 | int | = node+20 (0x6c3428) | — | ❓ |
| +136..164 | float[8] | corner vertices = node+1856..1884 (0x6c530c via v250[3..4] -> +136) | corners / localCorners | ✅ |
| +168..180 | int[4] | RGBA packed colors | packedColors | ✅ |
| +184..196 | float[4] | paintBox = node+1888..1900 (v248 0x6c52bc) | paintBox | ✅ |
| +200..212 | float[4] | viewport = node+1936 chain (v249 0x6c52c8) default (1,1,-1,-1) | viewport | ✅ |
| +216..228 | float[4] | clipRect (drawable: v80/v83/v84/v85 0x6c4f8c) | `std::array<float,4>` | ✅ 2026-07-23 已纠正原 int 类型 |
| +232 | int | opacity = node+1576 (read @0x6c7634) | opacity | ✅ |
| +244 | int | stencilComposite = node+52/stencilType (0x6c2a90); consumed (item+244&4),(item+244&3)==1 | stencilComposite | ✅ |
| +248 | tTJSVariant | context = player+1012 (0x6c33fc) | contextVariant | ✅ |
| +256 | ptr | -> node+200 source deque (loadSource arg @0x6c5664 uses +256+4) | (sourceKey path) | ⚠ STL |
| +264 | ptr | parent render node (semantic item +264) | parentItem | ⚠ STL ptr |
| +272/+276 | int | meshDivX/meshDivY (0x6c5864/0x6c5874) | meshDivX/meshDivY | ✅ |
| +280 | int | meshType = node+2000 (0x6c2684); switch 0/1/2 | meshType | ✅ |
| +300 | int | (memset 0 @0x6c2770) | — | ❓ |
| +304 | tTJSVariant | leaf layer (sub_A0FB64 v79+304 @0x6c533c) | leafLayer | ✅ |
| +320 | `tTJSVariant` internal tag | `item+304` leafLayer 的 type/tag word；0x6C62E0 测试后 CopyRef 同一 +304 Variant | `leafLayer.Type()` | ✅ 2026-07-23 纠错：不是独立 flag/count |
| +324 | tTJSVariant | composed Layer (Loud B sub_A0FB64 v94+324 @0x6c6114) | composedLayer | ✅ |
| +340 | `tTJSVariant` internal tag | `item+324` composedLayer 的 type/tag word；`!item+340` 即 Variant 未初始化 | `composedLayer.Type()` | ✅ 2026-07-23 纠错：不是独立 bool |
| +344..  | deque | mesh vertices (meshCopy src node+344) | meshPoints | ⚠ STL |
| +364 | int | (memset region) | — | — |
| +368 | int | bezier precision = node+368 (0x6c5bac) | meshPoints precision | ❓ |
| +376/+392/+408 | _OWORD | mesh control point deques (memset @0x6c2778..) | localMeshPoints | ⚠ STL |
| +400 | deque | mesh base points (bezier src node+400) | meshPoints | ⚠ STL |
| +424 | int | layerId = requireLayerId result (0x6c5234) | layerId | ✅ |

(❓ = offset present in binary, local mapping not yet positively confirmed this pass.)

## Findings

| id | func@addr | local file:line | sev | one-line |
|----|-----------|-----------------|-----|----------|
| I1 | Player_emitRenderItem_requireLayer@0x6C4E28 | PlayerRenderExecute/Targets | CLOSED 2026-07-23 | local helper name remains `buildRenderCommands`, but it is invoked from `0x6C7440`'s `!priorDraw` pre-walk and now contains requireLayerId/item+20 plus leaf/group emission |
| I2 | sub_6C2334@0x6c33c0 | PlayerRenderItems.cpp:477 | OK | skipFlag1 = !(inherited18||priorDraw) EXACT; node+48=priorDraw PROVEN @0x6bc6c4 |
| I3 | sub_6C2334@0x6c5240/0x6c5e6c | RuntimeSupport.h:257 | OK | rawFlag20 latch gate matches; value-aligned (trace 0-diff) |
| I4 | sub_6C2334 item alloc 0x1B0 | RuntimeSupport.h / MotionNode owner | ABI evidence | 0x1B0 is Android ARM64 object size, not a cross-ABI layout target；container topology must be judged from constructors/mutators, not byte size alone |
| I5 | item+216..228 clipRect | RuntimeSupport.h | CLOSED 2026-07-23 | binary 与 local 均为 float[4]；harness 同步使用 `std::array<float,4>` |
| I6 | Player_drawCompat@0x6D5FB8 D3D path | PlayerDrawDispatch.cpp | P1? | player+909(wasD3DMode) re-render-to-D3DAdaptor + captureCanvas path — verify local has this branch (Web D3D stub) |
| I7 | Player_DrawSLA@0x6D5658 | SeparateLayerAdaptor/PlayerRenderTargets | P1 | two SLA sub-paths gated by ogl_accurate_render (byte_1AB84F4): RenderMotionFrame vs sub_6C9CA8+sub_6CE938 — verify local distinguishes accurate vs non-accurate |
| I8 | Player_buildRenderTree_guess@0x6CBCE4 | (n/a IDB) | P3 | mis-named: actually acquireLayerById (Rb_tree<int,LayerVariant>, absolute=x+y, hitThreshold=256) — rename pending local symbol |
| I9 | Player_renderToCanvas@0x6c7440 first skip | PlayerRenderExecute.cpp:~462-528 | OK→verify | skip = item+17||item+16||!item+232; item+18 skip preview-gated @a1+1096 — confirm local replicates preview gate |
| I10| Player_emitRenderItem mesh switch | PlayerRenderExecute.cpp | P2 | meshType 0/1/2 -> affineCopy/bezierPatchCopy/meshCopy via TJS dispatch; verify local emits all three (analysis notes only affineCopy historically) |

## 2026-07-23 current open items（取代旧 MISSING 清单）

- `0x6C9CA8` accurate-SLA persistent child-Layer tree remains unimplemented outside
  HEADLESS diagnostics.
- `0x6C5264..0x6C532C` caller-local payload passed to `0x6C6B48` still has only a
  value-initialized local placeholder, not a reconstructed dedicated value type.
- `0x6C7440` remains split into a wrapper and executor helper locally; this is a
  source-structure gap even though the current submit dataflow is aligned.

## Architecture-level（2026-07-23 correction）

- 432B is the NDK/ARM64 compiler's object layout evidence. The wasm32 source port
  must reproduce C++ fields, ownership and container selection, not `_padN` or
  `offsetof` values. Current standing gaps are the acquire caller payload,
  accurate-SLA tree, and wrapper/executor source-function split—not ARM64 padding
  and not a requireLayerId phase move.

## IDB changes applied (idb_save done)

- rename 0x6C4E28 -> Player_emitRenderItem_requireLayer
- comments @0x6c33c0 (item+18), @0x6bc6c4 (node+48=priorDraw), @0x6c5240 (item+20),
  @0x6c756c (emitRenderItem call-site role)
