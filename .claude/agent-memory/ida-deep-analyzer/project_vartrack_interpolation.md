---
name: vartrack-interpolation
description: var-track item+16 插值函数 Player_interpolateVarTrackValues@0x6BBE20 + bezier easing@0x69A754，hold/lerp 公式、interval 量化、调用时机
metadata:
  type: project
---

motion::Player var-track item+16（当前插值变量值，HM4 读取源）的唯一写入者 = **Player_interpolateVarTrackValues @0x6BBE20**（原 sub_6BBE20）。

**调用时机**：在 `Player_resetMotionState_clearAndRebuild`(0x6B2B7C) 开头调用一次（0x6b2bc0），在 loop2 读 item+16→HM4 之前。**不是每帧**——progress_inner(0x6C106C) 从不调它；它只在 reset/play 路径跑。reset 链：playImpl→resetMotionState→interpolateVarTrackValues→(loop2 读 item+16)。

**每项逻辑**（item=var-track deque @Player+1296，160B/项；cursor=*(int*)(item+8)）：
- active slot S = item+48+56*cursor；other slot O = item+48+56*((cursor&1)==0)
- GATE：S.typeZeroFlag(slot+20)!=0 → skip（type==0 无值贡献）
- HOLD：S.interpFlag(slot+21)==0 OR O.typeZeroFlag!=0 → item+16 = S.value(slot+24)
- LERP：prevTime=S.time(slot+8); interval=S.interval(slot+16); d=clampedEvalTime(player+456)-prevTime;
  if interval!=0: d=floor(d/interval)*interval（量化到 interval 网格）;
  Vp=S.value, Vo=O.value; if Vo==Vp → hold;
  else t=d/(O.time-prevTime); if S.easingPresent(slot+48)!=0: t=Player_applyBezierEasing(S.easing@slot+32, t);
  **item+16 = Vo*t + Vp*(1-t)**
- 之后 Player_bindParameterValue_writesHM1_HM2(player,item,0,item+16)

cursor=0 时 active=slot0=prev(下帧)，other=slot1=next(上帧)，由 reseekTimelineCursors 的 sub_6B786C(slot0,v41)/sub_6B786C(slot1,v41+1) + cursor=0 确定。

**easing 类型（byte-verified via 0x69A754 逻辑）**：slot+32 是 TJS dict variant `{x:[...], y:[...]}`（三次贝塞尔控制点，count 为 3 的倍数），由 merge 函数 sub_6B7A70 经 PropGet("easing")+sub_A0FB64 复制。**不是 raw double**。Player_applyBezierEasing@0x69A754：clamp t 到 [x[0],x[n-1]] 取对应 y；否则 stride-3 扫描定位段，算 B(t)=(1-t)^3*y0+3(1-t)^2*t*y1+3(1-t)*t^2*y2+t^3*y3。

slot 字段偏移确认（disasm 0x6bbf10-0x6bbfd8）：slot+8=time slot+16=interval slot+20=typeZeroFlag slot+21=interpFlag slot+24=value slot+32=easing(dict variant) slot+48=easingPresent flag。
