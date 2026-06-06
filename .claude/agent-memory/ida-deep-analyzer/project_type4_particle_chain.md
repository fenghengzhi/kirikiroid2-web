---
name: type4-particle-chain
description: HM3 type-4 粒子链全钉死 — slot+424..488/slot+744/node+2224..2288/node+2296 数据流闭环 + eval两分支 + prt merge块 + 发射器消费者
metadata:
  type: project
---

type-4 粒子链 (pruneHM3 loop2 init/restore A类最后一条 DEFERRED 子路径) 已全钉死，**无真前置缺口**，旧 DEFERRED 注释被证伪。

**数据流闭环 (4环节):**
- eval 写 node+2224..2288 (唯一写点=evaluateTimeline@0x699AE4 两分支):
  - copy分支@0x699c30 (单slot,slot+344==0): node+2224..2288 ← slot+744..808 (LDR[X8,#0x2E8..#0x320]→STR[X19,#0x8B0..#0x8F0], 0x48=72B)
  - interp分支@0x69a0f8 (crossfade): node+2224..2288 ← lerp(slot[a]+424..488, slot[b]+424..488), out=b*r+a*(1-r), v103 gate 贝塞尔
- init HM3@0x6995dc: V+600..664 ← node+2224(4×OWORD)+node+2288(QWORD), gate slot+344==0
- restore HM3@0x699890: memcpy(slot+744 ← V+600, 0x48), gate V+32==0
- 下一帧 eval copy 再读 slot+744

**关键非对称**: interp分支源=slot+424..488(prt merge块); copy分支源=slot+744..808(restore回写区). 两套来源是 HM3 round-trip 的存在理由.

**slot+744..808 (0x2E8..0x320) 唯一读写点** (insn_query 0x690000-0x6D0000 + disasm 双确认):
- 唯一写者 = HM3_restoreValueToNode@0x6997F0 @0x699890 memcpy
- 唯一读者 = evaluateTimeline copy分支 @0x699c30

**mergeFrameContent prt块@0x693c64** (gate mask&0x100000, prtMask=PSB prt.mask): slot+416 trigger(bit1), slot+424/432 fmin/fmax(bit2), slot+440/448 vmin/vmax(bit4), slot+456/464 amin/amax(bit8), slot+472/480 zmin/zmax(bit0x10), slot+488 range(bit0x20). 复位默认@0x693d20: fmin/fmax={10,10}, zmin/zmax={1,1}.

**node+2296 particle Array dispatch**: V+672(=V+168 int*)=完整tTJSVariant持粒子Array(类比type3 V+544=childPlayerSnapshot). sub_A0FB64@0xA0FB64=tTJSVariant copy-assign(switch tag@a2+16). 消费者:
- Player_particleEmitterPass@0x6BF0DC (rename自sub, 找 nodeType==4 节点, 读 node4[139..147]=node+2224..2352 算 spawn count/vel/angle/zoom, 向 node+2296 "add" 子Player)
- Player_particleStepChildren@0x6C17A4 (遍历 node+2296 Array, erase 越界粒子, progress+updateLayers 活粒子)

**本地建模分歧 (非缺口)**: PlayerUpdateParticles.cpp:351+ 发射器直读 activeSlot().prtFmin/prtF/... (slot+424..488), 二进制读 node+2224..2288 镜像. crossfade 下等价, 单slot copy+HM3 restore 下不等价(丢 round-trip). 需补: PerNodeLayerState V+600..664(value_structs.h:308 DEFERRED) + ClipSlot slot+744..808 + eval copy 写镜像 + 发射器读镜像. NodeTree.cpp:273 particleArrayVar(tTJSVariant) 已对齐. 无平台边界(不像 V+44 srcDispatch→std::string).

stale 注释待删: PlayerFrameProgress.cpp:1960-1964/2021-2026, value_structs.h:308-310 ("eval type-4 branch unported, no source").
