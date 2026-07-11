---
name: project_particle_lifecycle
description: nodeType=4 粒子完整生命周期(emit/age/die/erase) — child+1099=loopArmed 是死亡条件;由 progress_inner 在非循环 motion 播完时清 0;两条 erase 路径(loopArmed/越屏 in stepChildren, maxNum cap in emitterPass)
type: project
---

粒子(nodeType=4)生命周期全链反编译证据(回答 DRACU 粒子泄漏)。

## 三个核心函数
- Player_particleEmitterPass @0x6BF0DC (size 0x144c) — 发射 + 物理外层。读 eval mirror node+2224..2288(由 evaluateTimeline type-4 分支写)。
- Player_particleStepChildren @0x6C17A4 (= sub_6C17A4,被 emitterPass 调 4 处) — 两段 pass:段1 erase loop,段2 progress loop。
- Player_progress_inner @0x6C106C — child 推进,**唯一清 child+1099 的地方**。

## 死亡条件 = child+1099 (loopArmed)
- particleStepChildren 段1 erase loop(0x6C1858~0x6C1950)首指令 `LDRB W8,[X0,#0x44B]; CBZ -> erase`。**child+1099==0 → 立即 erase**。
- child+1099 != 0 时才进越屏检测:`LDRB [emitter+0x88C=node+2188]; CBZ -> skip(不删)`。node+2188 = particleDeleteOutside 门控。再比 child+0x98/0xA0/0xA8/0xB0 bounds vs emitter+0x350..0x35C,全越界才 erase。
- child+1099 写点(progress_inner @0x6C106C):
  - 0x6C13F4: forward 到末尾(+1128<=+1120) 且 loopTime(+1136)<0 → `child+1099=0`。**粒子主死亡路径**。
  - 0x6C1384: reverse 且 loopTime<0 → `child+1099=0`。
  - 循环 motion(+1136>=0)永不清 0 → 走 loop-wrap(LABEL_22/27)永远存活。
- play/initNonEmoteMotion 设 +1099=1, +481=1(firstFrame), +1128=motion["lastTime"], +1136=motion["loopTime"]。

## maxNum cap (第二条 erase,独立)
- emitterPass @0x6C0218:每发射一个粒子 add 后立即 `if(propGetCount(childArray) > node+2168) erase(0)`。node+2168 = node4[135].u32[2] = particleMaxNum。signed 比较;maxNum==0 时恒删。

## emitCount(W20/v68) 与 step 调用拓扑
- emit block(LABEL_96)每次 emitterPass 调用只 add **一个**粒子。
- 末尾 0x6C027C `if(v68<=1) particleStepChildren`:emitCount>1 时本帧跳过 stepChildren(越屏+loopArmed 删除被跳),但 maxNum cap 已在 add 后无条件跑。下一帧 emitCount<=1 时补 step。
- propGetCount==0(无 charamotion)分支 @0x6C02DC:`do v68-- ... ; particleStepChildren` emitCount 次。

## emit 速率
- node4[147].f64[1]=node+2360 发射计时器,每帧 `-= parent+592(dt)`,<=0 时累积 v68 并 reset,间隔由 node4[139].f64=node+2224/2232(prtFmin/prtFmax 发射间隔范围,eval mirror)随机。有间隔 → 发射有限速率。

## 本地偏差结论(DRACU 泄漏)
- 本地映射正确:child+1099=`_allplaying`(PlayerCore.cpp:205),particleMaxNum=node+2168,particleDeleteOutside=node+2188。段1 erase loop 结构正确(PlayerUpdateParticles.cpp:783-803 `else shouldErase=true` 对应 CBZ)。frameProgress 也复刻了 +1099=0(PlayerFrameProgress.cpp:2514/2532)。
- 永不删根因候选(`_allplaying` 永不变 false):(a) child `_loopTime` 不是 <0(particle motion loopTime 取错/默认>=0)→ 永走 loop-wrap 不清 +1099;(b) child `_deltaTime` 为 0 → frameTick 不前进永不到末尾;(c) child 走的 progress 路径未到 frameProgress LABEL-48 forward-at-end 分支。需运行时确认 child 的 _loopTime/_cachedTotalFrames/_deltaTime 实际值。
- 注:第二条 maxNum cap 本应兜底,但本地 760 行 `getParticleCount() > particleMaxNum` 调用 <500 次 vs 28000 创建 → cap 也几乎没跑,提示 emit 路径每帧没走到 add 后的 cap(或 emitCount>1 持续跳过 step 但 cap 在 add 内仍应跑——需查本地 add/cap 实际命中)。
