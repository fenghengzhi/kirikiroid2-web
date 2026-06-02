---
name: m3-getvariable-review-gaps
description: M3 getVariable/var-track/HM3 fresh-decompile review (2026-06-03) — confirmed claims + 2 open evidence gaps to close
metadata:
  type: project
---

2026-06-03 独立 fresh-decompile 复核 motionplayer M3（getVariable scope-router + var-track stream③ + HM1/HM3/HM4 write）。结论：commits 5b0f96f/66cd17a/8bd6629/9ddc25d/c153190 方向正确、有反编译支撑。本轮独立确认的地址：0x533E1C / 0x6CD16C / 0x6CD23C / 0x6CD39C / 0x6D0BF4 / 0x6B786C / 0x6B7A70 / 0x699510 / 0x6C4668 / 0x6B2D3C。

**应用方式：** 后续若要"完整闭合"M3 证据链，只需补这两个缺口（其余已确认，勿重复反编译）。

两处仍 open 的证据缺口（非反驳，是缺证）：
1. **0x6BBE20 / 0x69A754 未本轮独立反编译** — interpolateVarTrackValues 的 HOLD/LERP + interval 量化 + stride-3 cubic-bezier easing 逻辑（commit 22a69e5）当前仅引用上一对话证据。要 100% 闭环需重反编译这两个地址。
2. **HM3_initValueFromNode (0x699510) 的 slantY(node+1568) 缺读取证据** — 该函数尾部独立确认读了 node+1512/1528/1536/1544/1560（x,y,z,angle,scaleX/Y,slantX），但**未见 node+1568 的读取语句**（最后一条是 result=*(ldouble*)(a1+1560) 写 V+160）。本地 PlayerFrameProgress.cpp hm3InitValueFromNode 写了 v.slantY=c.slantY(V+168←node+1568)，可能是 port 多出字段。属 dead-data（HM3 无 reader），不影响 oracle，但 c153190 "20 实装"中 slantY 一项证据不足，需精读 0x699510 尾部确认。

已澄清的"伪矛盾"（勿再当 bug 上报）：
- HM1 writeVal/weight 偏移：WRITE(0x6C4968)写 node+32、READ(0x6CD634)读 node+48 — 不矛盾。WRITE 的 v30=*node+16（跳 8B next+8B key header），故 v30+32=node+48=READ 目标。analysis 表的 node+48/+56(绝对) 与 value_structs.h 的 V+32/+40(payload相对) 是同一字段。
- getVariable in-scope 路径：本地用 if-guard(`if(!inScope){查HM4}` 后两路都落 HM1-join) 替代二进制 if/else 双分支(0x533E88)，语义路径等价（in-scope 跳过 HM4 直 HM1-join）。可接受。
- loop3 mask 0x19D = bits{0,2,3,4,7,8}，本地 (1<<t)&0x19D && t<=8 完全一致。node+46 门本地缺（MotionNode 未暴露该字节），dead-data 不可观察，已注释 DEFERRED。
