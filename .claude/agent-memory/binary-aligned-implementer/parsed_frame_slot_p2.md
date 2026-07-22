---
name: parsed-frame-slot-p2
description: M1/P2 historical decoded test model; deleted 2026-07-22, production raw parse/merge remains
metadata:
  type: project
---

**CURRENT CORRECTION 2026-07-22:** `PlayerFrameStep.{h,cpp}` 与 synthetic decoded tests
已删除；下文只记录历史分析，当前实现是生产 raw dispatch 路径。

M1/P2 (2026-05-30): ported the binary parsed-frame slot + parse/merge as
INDEPENDENT free functions, NOT wired to live frame-progress path (logo
differential untouched, 0-mismatch preserved).

**Files:** cpp/plugins/motionplayer/PlayerFrameStep.{h,cpp} (namespace
motion::detail). Test: tests/unit-tests/plugins/motionplayer-dll.cpp
"parseFrame/mergeFrameContent slot is binary-aligned (P2)". Added to
motionplayer/CMakeLists.txt + platforms/wasmtime/CMakeLists.txt.

**Function map:**
- 0x6926B4 Player_parseFrame -> parseFrameLike_0x6926B4
- 0x692AB0 Player_mergeFrameContent -> mergeFrameContentLike_0x692AB0
- 0x69260C Player_resetFrameSlot -> resetSlotLike_0x69260C
- TJS propGet helpers (all read iTJSDispatch2 PropGet, ported as PSB reads):
  0x662668 Motion_propGetDouble, 0x6635DC Motion_propGetInt,
  0x6636D4 Motion_propGetBool, 0x6695BC Motion_propGetIndexDouble,
  0x56C694 Motion_propGetCount, 0xA0FB64 = tTJSVariant copy (curve blocks).

**Slot = 536B node buffer @ node+320 / node+856 (struct ParsedFrameSlotLike_0x6926B4).**
Byte-verified offsets (slot base; v3=unsigned int* so v3[N]=4N, (double*)v3+N=8N):
+0 frameIndex, +8 time(d), +16 ti, +20 mask, +24 typeZeroFlag(invisible),
+25 interpFlag(type2=0/type3=1), +26 mergedFlag, +36 src variant, +44 blendMode(16),
+56 ox(d)/icon, +64 oy(d), +72 packedColors int32x4(0xFF808080), +88 opacity(255),
+96/104/112 coordXYZ(d), +120/121 fx/fy, +128 angle(d), +136/144 zx/zy(d),
+152/160 sx/sy(d), +168 ccc +188 occ +208 acc +228 zcc +248 scc +268 cp (variant
curve blocks), +288 act variant, +296.. mesh/bezierPatch float-pair vec(v3+80/82/84).

**Mask gates (mergeFrameContent, mask=v3[5]):** early-return if typeZeroFlag.
Reset block @692C70 BEFORE bits. src/icon gate = (1<<nodeType)&0x1849.
0x1 ox/oy; 0x2 coord. **0x20600 group** (if/elseif chain): 0x20000 bm;
then 0x200 color(4ch) ELIF (blendMode&0xF0)==0 -> ALL 4 packedColors=0xFFFFFFFF
(STP X9,X9 = 16 bytes), ELSE keep default; opa gated 0x400. 0x1FC group:
0xC fx/fy, 0x10 angle, 0x60 zx/zy, 0x180 sx/sy. **interpFlag(+25) gates** ti
(0x4000000), curves 0xF800 (0x800 ccc/0x8000 occ/0x1000 acc/0x2000 zcc/0x4000
scc), cp(extra 0x10000). 0x2000000 mesh. Subobjs each with own sub-mask:
0x80000 motion, 0x1000000 model, 0x100000 prt, 0x200000 camera, 0x800000
anchor, 0x8000000 feedback.

**DEFERRED (live-dispatch only):** icon-index handle (0x692CFC sub_A13878),
mesh/bezierPatch raw float-vector growth (operator new/memmove @693758). Not
reproducible from PSB dict; not needed for P2 scalar coverage.

**Trap:** parseFrame does NOT call mergeFrameContent itself — caller invokes
merge separately on the parsed slot. Port keeps them split (independently
testable, matches binary). merge reads slot.mask (v3[5]) which parseFrame must
have set first; a direct merge-only unit test must seed slot.mask.

**Why:** P2 of analysis/Player_progress_frame_stepping_M1_plan.md (P5/P6 wire-in
is high-risk, deferred). **How to apply:** when wiring node-deque frame stepping
(P4/P6), reuse this slot struct + these two functions; the live data source must
become the real iTJSDispatch2 motion tree (binary) — current PSB-source is the
P2 stand-in.
