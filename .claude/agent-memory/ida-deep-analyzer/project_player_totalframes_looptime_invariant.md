---
name: player-totalframes-looptime-invariant
description: Player+1128(totalFrames)=motion["lastTime"] / +1136(loopTime)=motion["loopTime"] 全二进制各只一处写入(initNonEmoteMotion),同源配对; loop-wrap 不变量 loopTime<lastTime
metadata:
  type: project
---

Player+1128 与 +1136 的全二进制写入点(.text 全段 STR 立即数 0x468/0x470 扫描已穷举):

- **+1128 (totalFrames)**: 唯一写者 = Player_initNonEmoteMotion@0x6B372C, `STR D0,[X19,#0x468]`, D0 = motion["lastTime"](double, 字段名是 "lastTime" 不是 "totalFrames")。
- **+1136 (loopTime)**: 唯一写者 = Player_initNonEmoteMotion@0x6B370C, `STR D0,[X19,#0x470]`, D0 = motion["loopTime"](double)。
- 两者紧邻、来自**同一个 motion dict**(临时 Motion 包装 &off_19FD968)，必然配对。

排除的假命中(偏移碰巧 0x468/0x470 但 base 不是 Player this):
- Player_startWind_populate@0x670A24/A80/A88: a1 是 **EmoteEngine**, engine+1128=windObj 指针、engine+1136/1140=wind xy float cache。函数名带 Player_ 前缀但 a1=engine(注释已注明)。
- EmoteWindEmitter_init / sub_681A38 / cocos2d/TVPWindowLayer/Camera 等: 全是别的类同偏移。

**关键结论**: 二进制里**不存在**"用 playing timeline 的 max totalFrames 覆盖 +1128"的写入(本地 PlayerMotionLoad.cpp 的 maxTF 循环在二进制无对应)。motion["lastTime"] 本身就是 motion 文件里权威的聚合 end-time。

**play / onFindMotion 路径同走 initNonEmoteMotion**: Player_play@0x6B21E8 → Player_playImpl@0x6B2284, content type!=1(non-emote) 时调 Player_initNonEmoteMotion@0x6B26F0。子 motion(childMotionPass/sub_6BE0C0@0x6BE46C/0x6BE65C 的 Player_play(v16,...)) 同理 → 子 player 自己的 lastTime/loopTime 配对设置, 机制相同。

**reseekTimelineCursors@0x6B86C8 不写 +1128/+1136**(只读 +456,写 +916/920/928/568/576/584 游标)。emote 路径 Player_initEmoteMotion 也不写这两个字段。

**loop-wrap 不变量 loopTime < lastTime**: progress_inner@0x6C14CC forward do-while `v7 += +1136 - +1128 while(+1128<=v7)`; childMotionPass@0x6BE4B8 `for(i=+1128; v28>=i; v28 = v28-i+v29)` (v29=+1136)。两处都无 in-loop guard, loopTime>=lastTime 即无限循环。二进制靠"同源配对 + motion 数据本身 loopTime<lastTime"保证不变量。本地 onFindMotion 把 +1128 设 maxTF 但不更新 +1136 → 可能 maxTF<残留 loopTime → 破坏不变量 → 卡死。对齐方向: +1128/+1136 必须同源配对从同一 motion 的 lastTime/loopTime 设, 绝不单独覆盖其一。

**全零未播放 child 不卡死的真正机制 = loopArmed 门控, NOT loopTime 符号 (2026-06-06 核实)**:
- Player_ctor@0x6CED30 **不初始化 +1128/+1136**(593 insn 全扫: 无 STR 到 [X19,#0x468]/[X19,#0x470], 无 memset 覆盖 1128/1136)。operator new(0x568)@Player_factory 0x6F6DF4 不清零。未播放 child 的 +1136 是堆残留/don't-care, **不是被设为 -1 或 0**。其唯一合法写入仍是 initNonEmoteMotion。
- ctor 确设: +480=1 / +481(firstFrame)=0 / +1098=0 / **+1099(loopArmed)=0** / +376(activeTimeline)=0 / +384==+392(空 renderList) / +1168=1.0 / +456=0。
- progress_inner@0x6C106C **入口无 play-flag/null guard**(本地 `if(!_speed)return` 在二进制无对应)。真正门控在 0x6C10E4: 当 +376==0 时判 `+481==0 && +1099==0`→ 成立则 `if(+384==+392) return`(renderList-empty 早返回), **永不到达 LABEL_48(0x6C1330) 的 loop-wrap do-while**。
- **LABEL_48 loop-wrap 仅当 `+481!=0 || +1099!=0` 可达**, 而这两标志仅由 initNonEmoteMotion 置位(+481=1@0x6B3AC0, +1099=1@0x6B3A74)。即"已 play"⟺ loopArmed=1 且 +1128/+1136 已填。
- 修复方向: (b) 本地入口 `if(!_speed)return` 是错误近似; 应复刻 loopArmed 门控拓扑(loop-wrap 必须嵌在 firstFrame||loopArmed 分支内), 并严守 +1099 生命周期(ctor=0, 仅 initNonEmoteMotion 置1)。**禁止 (a) 把 +1136 默认设负**(无反编译依据, 且会被 initNonEmoteMotion 覆盖)。5553dd2 注释"non-looping loopTime<0→STOP"判错根因: 二进制不靠 loopTime 符号区分, 靠 loopArmed+空 renderList。
