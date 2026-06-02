---
name: EmoteEngine controller-deque BUILDER located (M2 物理/状态机叶子前置)
description: 定位 EmoteEngine 6 个 controller-deque 的初始 builder。入口 EmoteObject_init@0x67DBAC -> applyMetadata@0x67D4D0 -> 6 个 per-category builder (eye/eyebrow/mouth/transition/selector + variableList)，每个 operator-new controller + push deque {ctl,label} + insert HM6@+1384 {type,index}。setVariable@0x671228 只是 READER。含 type tag→deque 映射、controller 字段表、save/restore 路径区分。
type: project
---

# EmoteEngine controller-deque 初始 BUILDER 定位报告

## TL;DR — 谁建 6 个 controller-deque

**builder 不在 EmoteEngine_ctor，不在 reloadVarsDispatch。** 真正的 builder 是按类目分散的 6 个函数，由
`EmoteObject_init@0x67DBAC` → `EmoteEngine_applyMetadata_buildControllers@0x67D4D0` 在 motion metadata 里逐 key 调度。

> **CORRECTION (2026-06-03, eye 垂直实装时复核 0x67DBAC)**：builder 读的是 **完整 `metadata` 字典**，**不是 `metadata["base"]`**。
> EmoteObject_init: `base = metadata["base"]`(0x67dd6c) **只**给 chara/motion 用；applyMetadata 收到的是 `metadata` 的 COPY(v28, 0x67dfa0)，
> eyeControl/variableList/mirror/scale 全部直接从 metadata 顶层读。下文"base metadata"措辞误导，以本 CORRECTION 为准。
> 另：HM#6@+1384 value = `{int32 type; int32 index}`(EmoteVarRef)，**不是 scalar/double**(buildEyeControl 0x66ca30 写 `*ret=4;ret[1]=idx`)。

每个 builder 干三件事（核心循环）：
1. `operator new(size)` 一个 controller 堆对象 + 它的 ctor（从 PSB dict 读字段）
2. `push_back` 进对应 EmoteEngine controller-deque：元素 = `{ controller_ptr@+0, ttstr label@+8 [, ttstr label2@+16 / byte@+16] }`
3. `EmoteEngine_HM6_findOrInsertVarRef@0x6885CC(engine+1384, label)` → 返回 value ptr，写 `value+0 = TYPE tag`、`value+4 = loop index`

`Player_setVariable@0x671228` 是 **READER**：hash label 查 HM6@+1384 → 读 `type@value+16`(注: setVariable 视角 value 在 result+16，即 node value 起点) / `index@value+20` → switch(type) 进对应 deque 按 index 取元素 → 调 element controller 的 setter。**setVariable 不 alloc/不 push**（任务前提正确）。

## 完整调用链

```
sub_67F978 (deserialize-from-save ctor; new EmoteObject 0x28)
└── EmoteObject_init @0x67DBAC
      ├── EmoteEngine_ctor @0x67E38C (new 0x5D8=1496B; 建内部 10 deque@0..720 + 7 controller@1072..1120; 不建 controller-deque)
      ├── (load resources, read PSB "metadata"->"base", "chara", "motion")
      ├── EmoteObject_applyChara_67F370 @0x67F370 -> sub_6B2AE8(Player, chara)  [Player层setCharacter, 不建deque]
      ├── Player_play(*(engine+1064), 1, &motion)
      └── EmoteEngine_applyMetadata_buildControllers @0x67D4D0  <<< BUILDER 调度入口
            ├── "variableList"      -> buildVariableList_guess @0x66A530   (timeline var defs -> HM5@+1328 + 20B/500-block deque; NOT controller-deque)
            ├── "bustControl"       -> sub_66B018           (物理参数, 写 engine 标量)
            ├── "hairControl"       -> sub_66B9D0(engine, engine+80, .,1)   (deque#2@80 物理node)
            ├── "partsControl"      -> sub_66B9D0(engine, engine+160,.,2)   (deque#3@160 物理node)
            ├── "eyeControl"        -> buildEyeControl_guess        @0x66C77C  ★ TYPE 4
            ├── "eyebrowControl"    -> buildEyebrowControl_guess    @0x66CB9C  ★ TYPE 5
            ├── "mouthControl"      -> buildMouthControl_guess      @0x66CFBC  ★ TYPE 6
            ├── "transitionControl" -> buildTransitionControl_guess @0x66D4C4  ★ TYPE 7
            ├── "selectorControl"   -> buildSelectorControl_guess   @0x66D8FC  ★ TYPE 8
            ├── "clampControl"      -> sub_66EE5C
            ├── "mirrorControl"     -> sub_66F364
            ├── "loopControl"       -> sub_66E480
            ├── "instantVariableList"-> sub_66F64C
            └── "timelineControl"   -> buildTimelineControl_guess   @0x66F80C  (HM3@engine+936 path, 无 controller-deque, 用 vector@a1[124..128])
```

注意 `applyMetadata` 在 init 里也被 `sub_67D4D0(engine, base_metadata)` 直接调；这是初始 build。
`sub_678044 = reloadVarsDispatch`（"timeline/eye/eyebrow/mouth/transition/selector/base/outerforce" 8 类目）是 **save/restore RELOAD**：在已 build 好的 deque 里 **按 label 线性查找** 已有元素再 deserialize 状态（如 EmoteEngine_reloadEyeVars@0x678804、reloadTimelineVars@0x678454），**不 push/不 alloc**。reload 由 sub_67F978 在 init 之后调。区分清楚：build=applyMetadata(0x67D4D0)，reload=reloadVarsDispatch(0x678044)。

## 6 controller-deque type tag → deque 映射（byte-verified）

setVariable 的 this = **EmoteEngine** 本体（offset 全对齐）。deque 控制结构 80B；下表 offset 为 setVariable 读的 begin._M_cur 字段（= ctor memset 起点 + 16）。

| 类目 | builder | controller new size + ctor | deque begin@(setVariable) | elem | block | TYPE | setVariable case |
|---|---|---|---|---|---|---|---|
| eye | buildEyeControl 0x66C77C | 0x170 EmoteBlinkController_ctor@0x662968 | +256 (a1[32..39]) | 16B {ctl,label} | 512 | **4** | case4 -> sub_6638B0 |
| eyebrow | buildEyebrowControl 0x66CB9C | 0x150 EmoteBlinkController_ctor_slim@0x66480C | +336 (a1[40..49]) | 16B {ctl,label} | 512 | **5** | case5 -> sub_6652D4 |
| mouth | buildMouthControl 0x66CFBC | 0x70 EmoteMouthController_ctor@0x665C98 | +416 (a1[50..59]) | 24B {ctl,label,talkLabel} | 504 | **6** (×2 insert: label+talkLabel) | case6 -> sub_665E34 |
| transition | buildTransitionControl 0x66D4C4 | 0x80 EmoteVarController_ctor_20Bdeque(.,1) | +576 (a1[70..79]) | 24B {ctl,label,byte=1@+16} | 504 | **7** | case7 -> Animator_setKeyframes (gate elem+16) |
| selector | buildSelectorControl 0x66D8FC | 0x80 EmoteSelectorController_ctor@0x66E398 | +656 (a1[82..89]) | 48B {ctl,multi-label@+8..,0@+24/+32/+40} | 480 | **8** | case8 -> sub_6681E4 |

注: setVariable case 名义 offset 是 256/336/416/576/656；builder push 用 a1[36]/[46]/[56]/[76]/[86]（= 各 deque 的 end._M_cur，offset 288/368/448/608/688）。同一 deque 的不同控制字段。

mouth 是唯一一个 **同一 controller 插 2 个 HM6 项**（label 和 talkLabel 都映射到同一 deque index, type=6）。
selector 用 `sub_689188`（带 payload `(index<<32)|8`）而非 `sub_6885CC`，但语义同：HM6 写 type=8/index。

type 0/1/2 = 直接写 HM2-style 标量(setVariable 末尾 `Player_HM2_upsert_labelToValue(engine+1440)`)，无 controller-deque。

## EmoteVarController（transition 用的基础型, 0x80=128B）字段
来自 `EmoteVarController_ctor_20Bdeque@0x667030` + step@0x666BF8（见 EmoteEngine_controllers.md）：
- +0..79 std::deque<KeyValue20B {float endVal; float dur; u32 powCount; u64 pad}>, block 25×20=500
- +80 int count(通道数); +84 int state(0 idle/1 anim); +88 float* current[count*4]; +96 float* target; +104 float* start; +112 int powCount; +116 float phase; +120 float invDuration; +124 pad

## EmoteBlinkController（eye case4, 0x170=368B, ctor@0x662968）字段
- +0..79 deque(12B elem, AngleController-style 主轨道); +80..159 deque(sub_6827A8); +160 vector<{float,float}> edge表(v4); +168/+176 vector cursor; +184.. deque(504-block, node value rows)
- +300 float beginFrame(cur frame); +328 int beginFrame; +332 int endFrame; +340 float blinkIntervalMin; +344 float blinkIntervalMax; +348 float blinkFrameCount; +352 float nextBlinkFrame=min+(max-min)*rand; +356 float loopStart=beginFrame; +360 byte blinkEnabled
- eyebrow 的 slim 版 (0x150) 字段类似但少 0x20（少一个 vector 或 deque）

## HM6 节点结构（EmoteEngine+1384 = libstdc++ unordered_map<ttstr, VarRef>）
node = operator new(0x20)=32B: `{ next@0, ttstr key@8, VarRef value@16 }`
VarRef value: `int32 type@+0, int32 index@+4`（builder 写）。setVariable 从查到的 result 读 `type@result+16 / index@result+20`（result = node value 区 = node+16，故 +16/+20 = value+0/+4 的二次偏移... 实际 setVariable 的 result 已是 value 区指针，读 *(result+16)/+20 是因为 setVariable 把 result 当 node 起点；与 HM6_findOrInsert 返回 node+16 对齐：两者 type 偏移一致）。

## IDA 重命名记录 (2026-06-03)
- 0x67D4D0 -> EmoteEngine_applyMetadata_buildControllers
- 0x66A530 -> EmoteEngine_buildVariableList_guess
- 0x66C77C -> EmoteEngine_buildEyeControl_guess
- 0x66CB9C -> EmoteEngine_buildEyebrowControl_guess
- 0x66CFBC -> EmoteEngine_buildMouthControl_guess
- 0x66D4C4 -> EmoteEngine_buildTransitionControl_guess
- 0x66D8FC -> EmoteEngine_buildSelectorControl_guess
- 0x66F80C -> EmoteEngine_buildTimelineControl_guess
- 0x6885CC -> EmoteEngine_HM6_findOrInsertVarRef_guess
- 0x665C98 -> EmoteMouthController_ctor_guess
- 0x66E398 -> EmoteSelectorController_ctor_guess
- set_comments on 0x67D4D0/0x66C77C/0x66CB9C/0x66CFBC/0x66D4C4/0x66D8FC/0x6885CC/0x662968/0x67F978

## 待继续（下一切片）
- clampControl(0x66EE5C)/mirrorControl(0x66F364)/loopControl(0x66E480)/instantVariableList(0x66F64C) 的 type tag 与 deque（可能复用已有 deque 或写标量）
- EmoteMouthController(0x70)/EmoteSelectorController(0x80) 完整字段表
- hairControl/partsControl(sub_66B9D0) 物理 node deque#2/#3 的 build 细节（48B/56B node）
- step 函数 sub_6638B0(eye)/sub_6652D4(eyebrow)/sub_665E34(mouth)/sub_6681E4(selector) 的状态机语义（与 +296..+336 phase/frame/target 字段对应）
