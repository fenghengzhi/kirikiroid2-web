# MotionPlayer timeline contribution checked lookup、舍入与 caller 四参考复原（2026-08-15）

## 结论

`EmoteEngine::accumulateTimelineContribution_guess(label, value)` 把 parallel timeline
的普通 track输出叠加到一份调用者提供的 live double。2026-08-15 从四个当前参考重新
反编译后，确认旧文档与本地源码最关键的一条结论相反：这里使用 HM3 bounds-checked
`at`，不是 `operator[]`。active vector若含不存在的 timeline label，查询失败且 map不
插入默认 state；此前已经累加到 caller double的前缀保持不变，没有回滚。

## 四端函数映射与容器 ABI

| 目标 | contribution helper | 大小 | active vector | HM3 | track record / deque block |
|---|---:|---:|---:|---:|---|
| Android arm64-v8a | `0x679940` | `0x150` | Engine `+1040/+1048` | `+936` | 56B / 504B（9项） |
| Android armeabi-v7a | `0x55F860` | `0x9C` | `+520/+524` | `+468` | 28B / 504B（18项） |
| iOS arm64 | `0x1001B35F4` | `0x158` | `+672/+680` | `+584` | 56B / 4088B（73项） |
| iOS armv7 | `0x1B31B0` | `0xF6` | `+336/+340` | `+292` | 28B / 4088B（146项） |

Android 的 track deque 走旧 libstdc++ 504-byte node；iOS 走 libc++ 4088-byte block。
两者只是同一 `deque<TimelineTrack>` 的 ABI布局差异，track物理字段共同为 label、
instant byte、frame deque、controller owner、float output（64位 padding后56B，32位
28B）。

四端已统一保留原型：

```cpp
void EmoteEngine_accumulateTimelineContribution_guess(
    void *self, const void *label, double *value);
```

## checked lookup 的直接证据

每个 active label调用的都是此前已由 pass路径识别的 HM3 `at` specialization：

| 目标 | call site | at helper |
|---|---:|---:|
| Android arm64 | `0x679988` | `0x689514` |
| Android armv7 | `0x55F886` | `0x569DBC` |
| iOS arm64 | `0x1001B3644` | `0x1001B374C` |
| iOS armv7 | `0x1B31DE` | `0x1B32A8` |

四端反编译都在 call后直接把返回 mapped-value作为 state使用；函数中没有
default-emplace、hash-node allocation或miss分支。异常沿 caller传播，且 active label
只是 vector中借用的 `ttstr`，helper本身不复制/retain label owner。

这与相邻函数形成明确边界：

- pre-progress：`operator[]`，miss先物化，然后可能在空data解引用处失败；
- contribution：`at`，miss在任何state字段读取前失败且不插入；
- pass：同样为`at`；
- animating/play/blend：`find`，按各自语义静默跳过或记录日志。

因此不能把“active timeline总应存在于HM3”当理由统一容器API；错误状态下的可观察
插入、异常点与部分提交完全不同。

## 共同数据流与舍入点

```cpp
for (const ttstr &timelineLabel : activeTimelineLabels) {
    TimelineState &state = timelineStates.at(timelineLabel);
    if ((state.flags & 2) == 0)
        continue;

    for (TimelineTrack &track : state.timelineData->variableList) {
        if (track.instantVariable || track.frameList.empty())
            continue;
        if (track.label == label) {
            const float contribution =
                track.output * state.blendWeight;
            value += contribution;
        }
    }
}
```

精确边界：

- flags bit1（值2）在 data owner读取之前测试。mapped state存在、flags关闭、
  `timelineData == nullptr` 时安全跳过；flags开启且data为空则直接进入非法owner边界。
- instant track和空frame-list不贡献。是否有controller owner、controller是否活动、
  当前cursor、autoStop和active blend controller state均不参与判断。
- label比较使用ttstr语义：backing pointer同一时快速命中，否则做null/hash/content比较；
  不按裸指针身份限定。
- `track.output` 与 `blendWeight` 都是float，先执行float乘法并得到一个float结果；该单项
  随后提升为double，加到live accumulator。不会先把所有track贡献在float中求和，也不会
  把两个输入提升为double后再乘。
- 每次match都立即读改写 caller double。重复track label、active vector中的重复timeline
  label都逐次贡献；没有set、count或去重。
- active-label顺序优先，内部再按track deque物理顺序。浮点加法非结合，因此这一顺序是
  边界行为。

四端单项写回锚点为 `0x679A5C`、`0x55F8E2`、`0x1001B3714`、`0x1B328C`。

## 部分提交和异常前缀

`value` 是调用者借用的double引用，不是返回临时量。若 active顺序为
`[validA, staleB]`，validA所有匹配track已即时叠加；随后B的`at`失败时，map不插入B，
但A的数值前缀保留。这不是强异常保证，也没有本地shadow accumulator。

该差异在两个caller家族中表现不同：

- 普通HM7 bind把HM7 node的mapped double直接作为`value`传入，因此较晚异常会把较早
  contribution永久留在Engine HM7中，Player bind尚未执行。
- clamp为LR/UD各建立局部double，再调用contribution；异常前的累加只留在当前栈局部，
  后续归一化和Player bind不会发生。

## caller拓扑与 optimizer clones

贡献helper的源级caller只有两类：

1. `applyClampControls_guess`：每个clamp entry按LR、UD固定顺序各调用一次；
2. progress post-loop普通HM7 bind：每个HM7 node调用一次，随后mirror、mode-0 bind，
   全部node完成后执行clamp。

xref数量受优化器outline差异影响：

- Android A32、iOS A64/A32：两个clamp call + 一个outlined bind/clamp wrapper call，
  progress core再调用该wrapper。
- Android A64：progress core内联普通bind/clamp骨架，直接形成一个call；此外存在一份
  内容相同但当前IDB无入边的clone `0x67A07C`，再加两个clamp call，所以helper有四个
  code xref。

四份optimizer clone已统一命名
`EmoteEngine_bindVariableValuesAndClamp_compilerClone_guess`：

| 目标 | clone/outline | progress入边 |
|---|---:|---|
| Android arm64 | `0x67A07C` | 当前无xref；progress body内联同一骨架 |
| Android armv7 | `0x55FC58` | `0x55FF9A` |
| iOS arm64 | `0x1001B3F64` | `0x1001B43CC` |
| iOS armv7 | `0x1B3B58` | `0x1B3EC4` |

这个名字显式标记为编译器拓扑，不把三端outline错误恢复成一条必须存在于可移植源码
中的独立业务方法。共同源级顺序仍是 HM7 contribution -> mirror -> Player bind，循环
结束后 clamp。

## 本地修正与回归

源码把 `_timelineStates[timelineLabel]` 改为
`_timelineStates.at(timelineLabel)`，并删除“miss会materialize”的过时注释。

新增回归固定：

- flags2状态的两个普通同名track按物理顺序贡献；
- instant同名track与空frame-list同名track均跳过；
- 同一timeline label在active vector重复两次时，两个普通track也完整重复贡献；
- flags关闭且data为空的已映射状态安全跳过；
- valid timeline之后的stale label触发`std::out_of_range`、不插入map node，同时保留
  valid timeline已经写进double的前缀。
