---
name: clusterB-emoteengine-addrs
description: EmoteEngine (cluster B) 反编译确认的函数地址↔本地实现映射 + 2026-06-07 审计结论(已基本对齐)
type: reference
---

EmoteEngine.cpp/.h 的 libkrkr2.so 地址映射（2026-06-07 反编译确认）：

- progress @0x67D01C (真体; 0x530A5C=tail-jump thunk) ↔ EmoteEngine::progress
- applyVarControllers @0x6766E0 ↔ applyVarControllers_pos_scale_color_angle
- stepHairParts @0x67B748 / stepBust @0x67BCE8 ↔ 同名方法
- resolveShapeAnchor @0x67B970 ↔ resolveShapeAnchorLike_0x67B970 (file-local)
- ctor @0x67E38C / dtor @0x67F4B8
- setVariable @0x671228 (IDA 误名 Player_setVariable; this=EmoteEngine 非 Player)
- 6 deque step 调用序 @progress: #4@0x67d0a4 #5@d10c #6@d168 #9(656)@d1e0 #8(576)@d240 #10@d2a0
- 9 builders: eye 0x66C77C / eyebrow 0x66CB9C / mouth 0x66CFBC / selector 0x66D8FC /
  transition 0x66D4C4 / loop 0x66E480 / bust(deque#1) 0x66B018 / chain(deque#2/3) 0x66B9D0

确认的架构事实：
- angle: Player_getAngleDeg @0x6C1780 返回 DEGREES; Player_getAngleRad @0x6CD0C0 返回
  RAD(×0.0174532925). stepHairParts/stepBust 的 BL 目标是 0x6CD0C0=getAngleRad(disasm
  确认), 反编译体里 "Player_getAngleDeg" 是 IDA 残留误标. 本地 emoteGetAngleRadLike_0x6CD0C0 正确.
- ctor COLOR seed: xmmword_14D68D0 = {128,128,128,255}f (get_bytes 确认 00 00 00 43 ×3 + 00 00 7F 43).
  NOT 白色(1,1,1,1). 本地 _ctlColor 留零+猜白色 TODO = P2 偏差(现可确认值).
- 容器: 7 只 libstdc++ unordered container（HM1/HM2/HM4=set，其余=map）；
  4 vector<ttstr> @+800/992/1016/1040（mirror patterns + timeline normal/diff/active labels）；
  10 deque @+0..720。本地用同类 std::deque/unordered_map/unordered_set，ABI 偏移按平台边界处理。

2026-06-07 审计结论 = ⚠️ PARTIAL(near-complete). 2026-05-30 旧报告全部 P0(B1-B4)已解决,
代码从"多为 STUB"演进为逐行对齐. 残留: P2 COLOR seed; CB-1 Player_preProgress@0x67d060
调用本地省略(需验证等价或恢复); P1 dtor 注释过时(已反编译 0x67F4B8). 接受边界: HM7
bucket-order bind / libc++ map ABI / variant-vector Release inert.

未递归审计(out of EmoteEngine.cpp scope, 建议 cluster A/Player + controller-step 子审计):
sub_67C560/67C6B0/67C8A8(bind-loop+clamp callees), 6 controller step fn 内部逻辑,
EmotePhysics_springStep/EmoteBustChainSpring_step.
