# MotionPlayer `animating` 过滤集合、所有权门与短路顺序四参考复核（2026-08-15）

## 本轮结论

这次从 `reference/binaries/` 的四个当前参考重新反编译 `getAnimating`，确认旧移植和
2026-08-11 文档都把一个重要所有权门放错了位置：活动 label 命中 timeline HM3
之后，若状态的 `timelineData` 仍为空，原版会跳过**整个活动项**，不会继续读取
`blendController`，也不会用负 `loopBegin` 判定活动。只有 parsed timeline data
存在时，才依次收集 track label、检查 blend controller、检查 `loopBegin < 0`。

这条门同时解释了一个真实的生命周期窗口：`resetControllers_guess()` 等流程可以让
active-label 向量与默认插入的空 timeline 状态同时存在；此时 `animating` 必须安全
返回 idle，而不是解引用尚未创建的 blend owner。

## 四端入口与函数边界

| 目标 | Engine 查询 | 大小 | D3D 一跳包装 | 大小 |
|---|---:|---:|---:|---:|
| Android arm64-v8a | `0x671378` | `0xCEC` | `0x530E18` | `0xC` |
| Android armeabi-v7a | `0x55B18C` | `0x2A0` | `0x495006` | `0x8` |
| iOS arm64 | `0x1001AE5D8` | `0x748` | `0x10023344C` | `0xC` |
| iOS armv7 | `0x1ADE54` | `0x69C` | `0x232196` | `0x8` |

四个 D3D 包装体都不检查空指针、不访问 `Player`，只是沿既有对象链取 Engine 后
尾调用同一查询：64 位为 `self+24 -> object+8`，32 位为
`self+16 -> object+4`。因此查询状态的唯一 owner 是 primary `EmoteObject` 所持的
`EmoteEngine`，D3D shell 没有另存一份 `animating` 状态。

## 空 `timelineData` 门的直接指令证据

四端虽然 STL ABI、HM3 node 包装和优化程度不同，控制流完全一致：

| 目标 | HM3/data 判断 | 空 data 的去向 | data 非空后的去向 |
|---|---|---|---|
| Android arm64 | `0x6715E8` find；`0x6715F0` node value；`0x6715F8` data load；`0x6715FC` `CBZ` | `0x671610` 设置 skip 状态，释放本地 label owner 后回 active-label 循环 | `0x671600..0x67160C` 载入 56B-track deque 边界并进入插入循环 |
| Android armv7 | `0x55B254` find；`0x55B25C` data load；`0x55B25E` `CBZ` | `0x55B2C8` 释放本地 label owner，随后递增 active-label iterator | `0x55B260..0x55B26A` 载入 track deque，完成后才到 `0x55B296` blend load |
| iOS arm64 | `0x1001AE6D0` find；`0x1001AE6D8` data load；`0x1001AE6DC` `CBZ` | `0x1001AE73C` 释放本地 label owner，再到 `0x1001AE9A4` 下一项 | `0x1001AE6E0` 保存 node/data，插入结束后 `0x1001AE960` 才载入 blend owner |
| iOS armv7 | `0x1ADF3E` find；`0x1ADF48` data load；`0x1ADF4A` `CBZ` | `0x1ADFA2` 释放本地 label owner并走下一 active label | `0x1ADF4C` 起载入 track deque；插入结束后才读取 blend/loop |

Android arm64 的反编译器把该路径表现为一个临时值 `3`，随后用
`(value & 3) == 3` 回到循环。这不是异常状态或 blend 活动码；结合 A32 与两份 iOS
的直接 `CBZ data -> next label`，可确定它只是优化后复用的本地控制流编码。

因此源级顺序应是：

```cpp
for (const ttstr &label : activeTimelineLabels) {
    auto found = timelineStates.find(label);
    if (found == timelineStates.end())
        continue;

    const TimelineState &state = found->second;
    if (!state.timelineData)
        continue;

    for (const TimelineTrack &track : state.timelineData->variableList)
        timelineDrivenLabels.insert(track.label);

    // timelineData 存在后，原版不再为空 blend owner 加 guard。
    if (controllerActive(state.blendController) || state.loopBegin < 0.0)
        return true;
}
```

可观察的边界为：

- HM3 miss：不插入、不收集、不判断活动。
- HM3 hit + null data：不收集、不判断活动；即使 blend owner 已活动或
  `loopBegin < 0`，仍跳过。
- HM3 hit + data + null blend：原版直接解引用，仍属于非法生命周期状态；本轮没有
  加本地容错。
- HM3 hit + data：先把所有 track label 插入集合，再检查 blend/loop；如果随后返回
  true，临时集合仍必须正常析构。

## 临时容器的结构与所有权

查询在栈上建立一次 `unordered_set<ttstr>`，持有 timeline 所驱动的输出标签：

- Android 两份参考使用旧 libstdc++ 哈希集合布局。A32 明确向默认构造辅助函数传入
  `10`；A64 也走约 10 个请求 bucket 到 prime bucket-count 的初始化逻辑。
- iOS 两份参考使用 libc++ 布局，先清零 bucket/list/size，并把
  `max_load_factor` 初始化为 `1.0f`，第一次 unique insert 时按 libc++ 规则扩容。
- 这只是同一源级临时 hash-set 在两套 STL ABI 下的布局差异，不应在共享源码里硬编码
  bucket 数或 node 结构。
- 每次插入都 CopyRef track 的 `ttstr` backing owner；重复 label 只保留一份 node。
  查询的全部 true/false 返回路径都经过集合析构，释放所有 retained label owner。
- active-label 本身也按值复制到本地查询 key；HM3 miss、null data、正常 data 和早退
  路径都会释放该临时 owner。

集合是**过滤集合**，不是“活动 timeline 数量”集合。一个 data 存在但没有 track 的
timeline 仍可由 blend/loop 令查询返回 true；一个 null-data timeline 则连 blend/loop
都不会检查。

## 完整短路顺序与过滤边界

四端共同的顺序是：

1. position controller；
2. scale controller；
3. angle controller；
4. 按 active-label 顺序扫描 timeline，收集 track labels，并检查 blend/loop；
5. selector deque；
6. transition deque；
7. eye deque；
8. eyebrow deque；
9. mouth deque；
10. 全部未命中才返回 false。

position、scale 和普通 variable blend controller 的活动条件都是 `state != 0` 或队列
非空；angle 读取自己的 state/queue 字段。color 位于 scale 与 angle 之间却被四端
共同跳过，不能因结构相邻把它补进查询。

timeline track label 会屏蔽同名 standalone controller：selector、transition、eye、
eyebrow 各查一个输出 label；mouth 依次查主 label 与 talk label，只有两者都未被
活动 timeline 驱动时才报告 standalone mouth 活动。过滤发生在 standalone controller
本身已经判定为活动之后；idle controller 不需要做 set lookup。

## 本地修正和回归边界

`EmoteEngine::getAnimating_guess()` 现把 `!state.timelineData` 的 `continue` 放在
track 遍历、blend 解引用和 loop 比较之前，并把注释限定为正确的生命周期范围：
只有 data 存在后才要求 blend owner 非空。

新增回归构造一个 active-label + 已 materialize HM3 state，但不创建 timeline data：

1. data/blend 都为空时返回 false且不崩溃；
2. 保持 data 为空，再创建活动 blend 并设 `loopBegin = -1`，仍返回 false；
3. 最后创建空 timeline data，同一状态立即进入 blend/loop 路径并返回 true。

这组断言同时防止未来把 null-data gate再次退回到“只包住 track 收集”的错误位置。
