# MotionPlayer 时间线状态表语义命名与访问边界（四参考，2026-08-15）

## 结论

调查期名称 `HM3` 在 `EmoteEngine` 中表示的不是无语义“第三张 hash map”，而是
完整的“时间线标签 → 时间线运行状态”所有权表：

```cpp
struct EmoteTimelineState {
    std::unique_ptr<EmoteTimelineData> timelineData;
    std::unique_ptr<EmoteVarController> blendController;
    tjs_uint32 flags;
    tTJSVariant rawElement;
    double loopBegin;
    double loopEnd;
    double lastTime;
    double currentTime;
    float blendWeight;
    double autoStop;
    std::vector<int32_t> frameCursors;
};

using EmoteTimelineStateMap =
    std::unordered_map<ttstr, EmoteTimelineState,
                       ttstr_hash, ttstr_equal>;
```

当前源码因此执行三项纯语义迁移：`EmoteHM3Value` 改为
`EmoteTimelineState`，`EmoteHM3Map` 改为 `EmoteTimelineStateMap`，字段
`_compoundHM3_936` 改为 `_timelineStates`。容器类型、声明位置和行为不变。

## 四 ABI 容器与节点布局

| ABI | Engine 中 map 偏移 | node 分配 | mapped 起点/大小 | node 前缀/尾部 |
| --- | ---: | ---: | ---: | --- |
| Android arm64 | `+936` | `0x88` | `+0x10 / 112B` | next `+0`、key `+8`、hash `+0x80` |
| Android armv7 | `+468` | `0x70` | `+0x10 / 88B` | next `+0`、key `+8`、hash `+0x68`，含 EABI 对齐槽 |
| iOS arm64 | `+584` | `0x88` | `+0x18 / 112B` | next `+0`、hash `+8`、key `+0x10` |
| iOS armv7 | `+292` | `0x60` | `+0x0C / 84B` | next `+0`、hash `+4`、key `+8` |

64 位 mapped value 的字段偏移为：两个 owner `+0/+8`、flags `+0x10`、
raw Variant `+0x14`、四个时间 double `+0x28..+0x40`、blendWeight
`+0x48`、autoStop `+0x50`、游标 vector `+0x58`。32 位对应偏移为
`+0/+4/+8/+0x0C/+0x18..+0x30/+0x38/+0x40/+0x48`。

Android armv7 mapped value 因 ARM EABI 的 8 字节对齐自然扩到 88 字节，iOS
armv7 为 84 字节；这不是源字段差异。Android old-libstdc++ 与 iOS libc++ 的
hash-node 键/缓存哈希次序也不同，所以 Web 端只表达源级键值与所有权，不硬编码
任何一个参考目标的 node 布局。

## 新节点默认构造

四份 `operator[]` miss 路径共同执行：

1. 计算/复用 `ttstr` backing 上的缓存哈希并查找现有节点；
2. miss 时分配目标 ABI 的完整 hash node；
3. CopyRef 键字符串；
4. 默认构造 mapped value：两个 owner 为空，flags/时间标量为 0，raw Variant
   为空，游标 vector 为空；
5. 单独写 `blendWeight = 1.0f`；
6. 按各自 STL rehash/bucket 规则链接新节点并返回 mapped value。

hit 路径直接返回已有 mapped value，不重置 `timelineData`、blend controller、raw
metadata、游标或任何播放期标量。这是重复标签继续复用运行状态的根本原因。

| `operator[]` | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| 地址 | `0x685060` | `0x5669AC` | `0x1001A6938` | `0x1A6074` |

## 元数据到运行状态的数据流

时间线 metadata builder 在把标签追加到主/差分标签向量后，才调用
`timelineStates[label]` 并把完整原始 Variant 赋给 `rawElement`。它不清 map；
重复标签保留多个枚举项，但共同落入同一个 state，最后一次 raw Variant 赋值获胜，
原有的 owner 与播放标量继续存在。

正常“替换全部 metadata”的外层调用会先清整张 map；随后若活动标签向量仍保留
旧标签，某些使用 `operator[]` 的阶段会重新物化一个默认 state。这里必须区分
“builder 直接调用”与“完整 metadata replacement”两种生命周期。

相关 builder 地址：

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| build timeline metadata | `0x66CBEC` | `0x558EB4` | `0x1001ABA30` | `0x1AB18C` |
| initialize decoded state | `0x66D03C` | `0x5590E8` | `0x1001ABD5C` | `0x1AB4B0` |

初始化 decoded state 时，新建 `EmoteTimelineData` 和 blend controller，并通过
单指针 owner replacement 先安装新对象、再销毁旧对象。`timelineData` 内部拥有
`deque<EmoteTimelineTrack>`，每个 Track 又独占一个变量控制器；整棵所有权树由
map node 的 mapped value 根部管理。

## `find`、`at` 与 `operator[]` 不是可互换的

四参考时间线调用链故意混用三种访问方式：

- `playTimeline`、blend 设置、若干查询/list API 使用 `find`。miss 不插入；播放
  路径写普通的“timeline label not found”日志并正常返回，list 路径则跳过无 state
  的活动标签。若 play flags bit 0 已先清空活动向量，miss 日志不会回滚该副作用。
- `passTimelines` 使用 `at`。活动向量若含缺失 key，会保留原版的越界异常边界，
  不能静默物化默认 state。
- reset 与 pre-progress 使用 `operator[]`。stale 活动标签会物化默认 state；后续代码
  是否立即解引用空 `timelineData` 取决于各阶段自身的门条件。
- timeline contribution 与 pass 使用 bounds-checked `at`。stale 活动标签失败且不插入；
  contribution 在较晚 label失败前已经写入 caller double的前缀不会回滚。

| 关键访问 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| `playTimeline`（`find`） | `0x670350` | `0x55AA70` | `0x1001ADE0C` | `0x1AD53C` |
| map `at` helper | `0x689514` | `0x569DBC` | `0x1001B374C` | `0x1B32A8` |
| `passTimelines` | `0x67A100` | `0x55FCC4` | `0x1001B3FE4` | `0x1B3BBC` |
| `preProgress`（`operator[]`） | `0x66EB44` | `0x559F78` | `0x1001AD0DC` | `0x1AC844` |
| contribution（`at`） | `0x679940` | `0x55F860` | `0x1001B35F4` | `0x1B31B0` |

把所有访问统一成 `find` 或 `operator[]` 会改变缺失键的插入、异常和后续空 owner
边界，不符合一比一复原目标。

`preProgress` 对 materialize 后的默认 state没有整项guard；window阶段本身会对null
data只提交currentTime，但后续loop seek、parallel step或autoStop仍可能到达更窄的空
owner边界。其共享 residual、ordered loop gate 与 branch-specific erase边界详见
`analysis/motionplayer_pre_progress_shared_residual_ordered_erase_four_binary_2026-08-15.md`。
contribution 的 checked lookup 与部分提交边界见
`analysis/motionplayer_timeline_contribution_checked_lookup_rounding_callers_four_binary_2026-08-15.md`。

## 析构顺序

map clear 和 Engine 析构都沿 node chain 逐节点执行源级逆声明析构：

1. `frameCursors` vector；
2. `rawElement` Variant；
3. blend controller owner；
4. `timelineData` owner，其内部继续析构 Track deque 与每个 Track controller；
5. key `ttstr`；
6. hash node allocation。

因此 map 是全部 decoded timeline/track/controller 生命周期的真正根，而不是旁路
索引。新语义名使这条所有权关系直接体现在类型系统中。

## 地址命名边界

恢复 IDB 中的原始函数名无法从 stripped 产物证明，相关恢复名继续保留 `_guess`。
绝对地址、HM 调查编号和 ABI 偏移只记录在本文与 IDB；编译源码不再用单一
Android arm64 的 `+936` 作为字段身份。
