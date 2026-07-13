---
name: progress_inner_firstframe_block
description: progress_inner@0x6C106C firstFrame 块(0x6C1108)的方向性 seed + reverse-from-end 种子 + 必须 return(非 fall-through)的对齐契约;以及 +481 seed 缺口
metadata:
  type: project
---

progress_inner@0x6C106C 有 **两个**独立的 firstFrame 子块，按 +376(activeTimeline) 分流：
- **+376!=0**（0x6C10B8）：简单 seed `+1120=+456=*(activeTimeline+40)`; 清 +481; `return reseekTimelineCursors`(0x6C10E0)。
- **+376==0 且 +481!=0**（真实入口 0x6C1108，分支顶在 0x6C10E4）：这是普通时间轴的审计 #4 块。CORRECTION 2026-07-13：本端并非恒走此路径；`_defaultParameterEntryPtr` 是 +376 的语义等价字段，非空时必须先走 0x6C10B8 专路并 return。

firstFrame 块(0x6C1108..0x6C132C)精确拓扑：
1. `v8=+592(deltaTime)`; 清 +481。
2. **(b) reverse-from-end 种子** 0x6C1120：`if(v8<0 && +1120==0){ +456=+1128; +1120=+1128; }`
3. **(a) +609(reverseSeekFlag) 方向性 seek** 0x6C1130：set 时清 +609，然后
   - forward(v8>=0,0x6C13AC)：save +456 → +456=0 → reseek → (syncWaiting/+483 早退) → restore → advanceRoot → (syncWaiting 早退)
   - reverse(v8<0,0x6C1144)：`只在 +1128>+1120 时` save +456 → +456=+1128 → reseek → 早退 → restore → rewind → 早退；`+1128<=+1120` 直接 goto LABEL_48
   - else(+609==0,0x6C131C)：plain reseek → syncWaiting 早退
4. 0x6C1328 `if(+483)return`，**fall-through 到 LABEL_48(0x6C1330)**。

**关键陷阱：fall-through 的净效果 = "仅 reseek 后 return"。** 此帧 gate(+480=_queuing)仍=1，LABEL_48：gated clamp(0x6C1338)跳过；forward not-at-end 落 `else if(!gate)` gate=1 → `return result`(0x6C13A4)。LABEL_48 无任何可观察副作用。
CORRECTION 2026-07-12: 错位的 0x671764 调用已从 frameProgress 删除并恢复到 EmoteEngine::progress；0x671764 的 this 是 EmoteEngine。firstFrame 块尾 `return` 现在只对应 queuing=1 时 LABEL_48 落到 0x6C13A4 的净返回，不再承担绕开错位调用的职责。

**字段(全 disasm 直读)**：+456=_clampedEvalTime(0x1C8)、+592=_deltaTime(0x250)、+609=_reverseSeekFlag(0x261,writer 0x6BE4F8 STRB 当 !self+480)、+1120=_frameTickCount(0x460)、+1128=_cachedTotalFrames(0x468)、+481=_firstFrame(0x1E1)、+480=_queuing(0x1E0)。

**连带修复 PlayerCore initNonEmoteMotion(0x6B3A8C)**：非-chain 分支 0x6B3AAC `STRH 0x0101` 同置 +480/+481；chain 分支 0x6B3AC0 `STRB 1` 只置 +481。port 此前只写 _queuing,从不 seed _firstFrame → progress_inner firstFrame 块(gated on +481)在 play() 路径永不触发。两处都补 _firstFrame=true 后 firstFrame 块才生效。这是 firstFrame 对齐的**前置依赖**。
