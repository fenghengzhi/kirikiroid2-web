---
name: clusterD-main-d3d-namespace
description: motionplayer cluster D (main.cpp NCB reg / D3DEmotePlayer / Motion namespace / D3DAdaptor / D3DEmoteModule) binary addrs + 2026-06-07 alignment state
metadata:
  type: project
---

motionplayer.dll cluster D audit (2026-06-07). The 2026-05-30 "NEEDS ARCHITECTURAL REWORK"
verdict for D3DEmotePlayer is SUPERSEDED — current main.cpp已重建到二进制精确形状。

**Confirmed binary addr ↔ local map:**
- D3DEmotePlayer_ncb_registerMembers @0x52E504 = 54-entry table (4 const + 50 members).
  Local main.cpp:870-1022. 全部 NAME/order/6 个 name-callback alias 1:1。
  6 alias: clear→create, queing→getQueing(byte@+1161 flag), bustScale→getBustScale(double@+1200),
  setTimelineBlendRatio→setTimeline, pass→addPlayCallback, modified→getPlayCallback(RO).
  注意 queing/bustScale 此前被误判为 NAME/callback mismatch（IDB 把 cb 误标 getBustScale/
  getBodyScale），实际 name 与行为一致，已纠正。
- D3DEmotePlayer_ncb_register @0x541D98 = 类注册器，默认无参 ctor 占位 + native create
  via sub_542054(v8={*(a1+24),0})。本地绑 NCB_CONSTRUCTOR((ResourceManager))，native-create
  arg identity 未追（待 verify）。
- motionplayer_ncb_register @0x6D9B08 = Motion namespace 注册器。23 常量 + 11 子类
  (Point,Circle,Rect,Quad,LayerGetter,Player@MEMORY[0x14C1E9C][5],SourceCache,ObjSource,
  ResourceManager,SeparateLayerAdaptor,D3DAdaptor) + 2 namespace free-fn
  (doAlphaMaskOperation@0x6da1f0, getD3DAvailable@0x6da260) IN-FLOW via sub_6FCAAC after D3DAdaptor。
- D3DAdaptor_ncb_registerMembers @0x6ACE94 = 16 members (不是 cluster K 说的 19)。
  setPos/removeAllBg/removeAllCaption/registerBg/registerCaption/unloadUnusedTextures = 二进制
  nullsub_81..86 (本地空 stub 是忠实的)。removeAllTextures=sub_6AD8B8 二进制有真实体，本地空。
- D3DEmoteModule registrar sub_52DFA8 @0x52DFA8 = 8 members (ctor + maskMode/maskRegionClipping/
  mipMapEnabled/alphaOp/protectTranslucentTextureColor/pixelateDivision + setMaxTextureSize)。
  本地 main.cpp:839-868 1:1。4 常量已正确移到 D3DEmotePlayer。pixelateDivision 正确在两个类上
  (D3DEmoteModule+20 与 Player+912 default 100 是不同字段)。

**残留偏差 (4 个，全 P2/P3，无 P0/P1):**
- D-A(P2): D3DEmotePlayer #50 'progress' cb=**EmoteEngine_progress**@0x52f76c，非
  D3DEmotePlayer_progress。本地 NCB_METHOD(progress)→D3DEmotePlayer::progress(double)。NAME 一致，
  需确认 wrapper tail-call EmoteEngine progress。已加 IDB 注释 @0x52f76c。
- D-B(P3❓): D3DEmotePlayer ctor native-create arg 未追。
- D-C(P3): MaskModeStencil/Alpha 二进制在 Motion namespace(@0x6d9d24/0x6d9d3c) AND D3DEmotePlayer
  双处注册；本地 Motion block(main.cpp:623-653) 漏了，只在 D3DEmotePlayer。加两 Variant 即可。
- D-D(P3): D3DAdaptor removeAllTextures 二进制 sub_6AD8B8 有真实体，本地空 stub 无 PLATFORM_BOUNDARY 注释。

**M6 namespace-attach 回归已 RESOLVED (commit f50f197):** 根因=独立 NCB_ATTACH_FUNCTION 自动注册
单元时机过早→静默关渲染管线(0 events,无 trap)。修复=挪进 PostRegistCallback in-flow 注册，对齐
sub_6D9B08 "after subclasses" 时机。当前 main.cpp:751-755 即此形状，非 workaround。

**偏差模式 (本 cluster):** NCB member NAME 以二进制 ncb_addMember key 为权威；二进制故意用
name≠callback-semantics 的 alias，必须 1:1 复刻（CLAUDE.md 强制）。ncb member ORDER 对不同名成员
非 script-observable，源码分组导致的 emit 顺序差异是 cosmetic。namespace/subclass 经 ncbind deferred
attach，end-state 对象图一致即可。
