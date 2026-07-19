---
name: advancenodeframes-hang-rootcause
description: RESOLVED 2026-06-05. m2logo CI hang from the advanceNodeFrames (0x6B7E44) convergence — diagnosed (per-case xp3 + proc_exit bitmask probes) and FIXED via fix A. Root cause = the standalone literal transcription of 0x6B7E44's backward seek hit the live frame-parser's negative-index reset, landing the active slot on a different frame ({done=true,src=empty}) than the proven shared helper ({done=false,src=non-empty}), flipping the child-play gate → null-motion child spun PlayerFrameProgress.cpp:2100. Fix = advanceNodeFramesLike delegates to advanceNodeFrameSelectionLike_0x6926B4 (shared seek+tail) with events off + clampedEvalTime forwarded.
metadata:
  type: project
---

> **2026-07-19 纠正：** 本文出现的 `_activeMotion=null` 是旧诊断字段，不是当前 Player
> 生命周期；loaded/content gate 现只读 +528 raw Variant，matched key 只读 +1012。
>
> **2026-07-19 再纠正：** 下文第 5 点“`parameterEntry!=null` 但
> `parameterizeIndex<0`”是旧本地实现制造出的不可能状态，不能再作为二进制结论引用。
> fresh decompile `Player_initNodeFields@0x6B3C78` 显示：仅当 `parameterize` variant
> 类型为 Integer 时才把 56B 参数项地址写入 node+8，否则在 `0x6B3EA0` 明确写 null。
> 本地已恢复该条件写入。透传 `clampedEvalTime` 仍是共享 helper 对非参数 inline
> 路径的正确参数，但不再用这个伪状态解释。

# advanceNodeFrames (0x6B7E44) 收敛 → m2logo CI 挂起:已诊断 + 已修复（2026-06-05）

## 状态
✅ RESOLVED。fix A 已落地，m2logo PASS 93 / yuzulogo PASS 243（本地 per-case xp3，
LLDB tracer，无挂起）。见 [[advancenodeframes-0x6B7E44-convergence]] 的最终实现。

## 诊断方法（关键）
- **本地验证必须用 per-case xp3** `reference/xp3/logo_test_oracle_<case>.xp3`，
  不能用 runner 默认 xp3，见 [[local-motion-playback-differential-per-case-xp3]]。
- **wasmtime guest 的 fd1/fd2(stdout/stderr)在本 runner 里被丢弃**（LLDB driver 路径），
  fprintf/fflush 都看不到。唯一可用诊断通道是 **guest `proc_exit(N)`**——driver 会回显
  "guest called proc_exit(N)"，N 是原始 i32（可 >255，实测回显 600201）。把差分编码进退出码。
- 差分手法:在 progressSeekNodeSlotsLike 路由处，对参数化节点先 `MotionNode probe = node`
  深拷贝，新路径跑 probe、旧路径跑真 node（真 node 留旧路径→不挂），再用 proc_exit 位掩码
  回显两侧 activeSlotIndex/frameIndex/done/src 的差异。

## 死循环精确位置（未改，忠实）
`PlayerFrameProgress.cpp:2100`（progress_inner 前向 loop-wrap，复刻 0x6C14C4）：
`do { v7 += _loopTime - _cachedTotalFrames; } while(_cachedTotalFrames <= v7);`
当 totalFrames=loopTime=0 → 零步长永不退出。**该循环忠实，不改**;二进制不挂是因其流程
永不带 totalFrames=0 走到这里。

## 根因（已钉死）
1. 被回退的 9f2a112 把参数化节点的帧 seek 写成一个**独立 literal 转写**
   advanceNodeFramesLike_0x6B7E44，逐字复刻 0x6B7E44 的 forward+backward seek。
2. 二进制 0x6B7E44 的 **backward seek（0x6B7FA4）** 调
   `Player_parseFrame(other, cur.fi - 1)` **无下界 guard**，靠数据不变量
   `target >= frame0.time` 保证永不真的解析负 index。
3. 但 live 帧解析器 `populateClipSlotFromFrameLike_0x6926B4` 对负 index 是
   **resetClipSlot → 默认 {done=true, src=empty}**。于是 literal 转写在 m2logo 的
   参数化节点 "レイヤ1" 上把 active slot 落到 idx0/frame0 的默认态 {done=true,src=empty}，
   而 proven 的 `advanceNodeFrameSelectionLike_0x6926B4`（带 `active.fi>0` 背向 guard +
   `other.fi>=0` 前向 guard，忠实编码同一数据不变量）落到 idx1/frame1 的可见帧
   {done=false,src=non-empty}。
4. done/src 差异翻转下游 child-motion 播放门（PlayerUpdateChildMotion.cpp:45 读
   activeSlot().done、:66 读 activeSlot().src）：literal 路径让某帧 child 被 play 但
   motion 加载失败 → child `_speed=1` / `_activeMotion=null` / `totalFrames=0`；后续帧
   不再停它 → 持续被 frameProgress → 2100 空转。
5. **历史第二个坑（结论已纠正）**：delegation 把 `currentTime` 硬编成 `0.0` 会改变
   非参数 inline 路径的 seek target，因此共享 helper 必须接收真实
   `clampedEvalTime`。旧文把现象归因于 `parameterEntry!=null && parameterizeIndex<0`；
   `Player_initNodeFields@0x6B3C78` 已证明二进制不会产生该状态。

## 反编译证据（0x6B7E44 尾部 = inline 尾部，逐字段一致）
0x6B7E44 与 Player_advanceRootAndNodes 的非参数化 inline 兄弟路径（0x6B72BC..0x6B7338）
共用同一段 seek + state-establish 尾部:`node+44=1` → 2× 门控 Player_mergeFrameContent
(node+346/+882) → 门控 `Motion_Player_findSource(node+200, player, activeSlot+356/src,
+348/icon)`（写 active-slot 的 done@node+200+0 / src(texture)@+24）。唯一差异:参数化路径
**不 push 任何 per-node onAction**。所以 0x6B7E44 的忠实行为 == 共享 seek+尾部 + 事件抑制。
(agent a72c0cd4 反编译 + disasm 锁定 2026-06-05。)

## fix A（已落地）
`advanceNodeFramesLike_0x6B7E44(node, currentTime)` →
`advanceNodeFrameSelectionLike_0x6926B4(node, currentTime, /*events=*/nullptr)`。
路由:`if(parameterEntry) advanceNodeFramesLike(node, clampedEvalTime); else
advanceNodeFrameSelectionLike(node, clampedEvalTime, &_pendingEvents);`。
行为与 green 基线（reseek-only）**完全一致**——收敛的净增量 = 显式 node+8 路由 split +
命名的 0x6B7E44 函数边界 + 完整反编译证据注释。不另写 literal 转写(它与 live 解析器的
负 index 语义冲突;补上数据不变量 guard 后就与共享 helper 字节等价了)。

## 仍 open（步骤 2/3，与本挂起无关）
advanceRootAndNodes @0x6B6ADC（4-stream 前向 walk）+ rewindRootAndNodes @0x6B9A3C（反向），
以及把 inline 非参数化 seek 拆到自己的 0x6B73D0 边界、删 PlayerFrameStepping.cpp mock。
