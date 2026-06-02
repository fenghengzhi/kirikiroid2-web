---
name: eyebrow-deque5-slim-slice
description: 2026-06-03 M2 eyebrow/deque#5 切片逐行对齐裁决 ✅ + eye/eyebrow 值轨道偏移互换事实
metadata:
  type: project
---

2026-06-03 独立 fresh-decompile 审计 M2 eyebrow/deque#5 垂直切片：**全部 ✅ 逐行架构对齐**，无须修偏差。

**地址 ↔ 本地映射（已确证）：**
- `EmoteEyebrowController_ctor` (slim) @0x66480C ↔ EmoteEyebrowController.cpp ctor
- `EmoteEyebrowController_step` sub_665600 @0x665600 ↔ EmoteEyebrowController.cpp step
- `EmoteEngine_buildEyebrowControl` @0x66CB9C ↔ EmoteEngine.cpp:659 buildEyebrowControl
- progress deque#5 step @0x67d10c (EmoteEngine_progress 0x67D01C) ↔ EmoteEngine.cpp:804

**eye(0x170) vs eyebrow(slim 0x150) 结构差异（已确证，写入 IDB 注释）：**
- eyebrow ctor 只读 beginFrame(+328) + edge/node 数组；trackValue(+300)=(float)beginFrame。**无** endFrame/blinkIntervalMin/Max/blinkFrameCount/blinkEnabled，**无** +336 blink-state zero，**无** RNG(sub_9F1A08/sub_9F17D0)。0x20 小于 eye。
- eye ctor (0x662968) 读全部 blink 字段 + 调 RNG 算 nextBlinkFrame(+352)。
- **值轨道字段偏移在两类间互换**（关键，易读错字段）：
  - eyebrow(0x665600): accum=+312, span=+316, pow=+320, invDur=+324
  - eye    (0x663BDC): accum=+316, span=+312, pow=+324, invDur=+320
- eye step 有 `switch(*(a1+336))` blink 机(cases 0/A/B/C, case B 调 RNG) + final [beginFrame,endFrame] remap(`float numer/(double)(v44-v42)`)；**eyebrow step 二者皆无**，结尾 `*a2=*(a1+300)` 直接。
- 结论：独立类、不共享基类是忠实选择（强行共享会引入 eyebrow 不存在的 blink 区）。

**deque#5 容器事实：** engine +320 base（80B deque obj）；progress 读 begin-iter @+336，builder 写 end-iter @ a1[46]=+368。元素 16B {EmoteEyebrowController* ctl@0; ttstr label@8}，block 0x200=512。HM6 {type=5,index=loop v5}(findOrInsert a1+173)。HM7 upsert Player_HM2_upsert_labelToValue(+1440, elem+1), stride 16B。

**共享 SCOPE BOUNDARY（合法，不计偏差）：** sub_661F7C @0x661F7C(204 指令,4 参数 self+160/self+80/trackValue/endVal) = eye/eyebrow 共用 mesh resolver，未实装 → 8B-track 不重填 → value-track 插值 inert 但 trackValue 保持 popped 位置 + 标量输出忠实。**不影响 step 状态机本身逐行对齐**（状态机逻辑独立于 resolver 数据源）。

---
**CORRECTION (2026-06-03, commit 2316276):** this slice was NOT zero-deviation. trackPow(+320) was ported as int32_t + static_cast<float>, but the binary loads it as *(float*) (raw float bits, no SCVTF). Fixed to float + memcpy alongside eye. Lesson: scrutinize int-vs-float-bits on every pow/curve field.
