# Player variable binder、HM1 重建与 parameter-ramp 写传播（四参考二进制，2026-08-27）

## 1. 范围与结论

本切片闭合 `Player::setVariable` raw callback 后面的共同 binder，而不是只停在
NCBind wrapper：

- 可分割 label 的 HM1 查找/插入、value 发布与一次性 cache rebuild；
- type-3 child 与 type-4 particle child 的 parameter-ramp 传播；
- 无条件 HM2 `operator[]` 写入；
- 本 Player 的 `ParameterRampMap::equal_range` 全重复项更新；
- Engine progress、clamp control、Player variable-track 与 script raw 入口四类 caller；
- 换 motion 时 HM1 value/cache/rebuild-gate 的生命周期边界。

四端的 source-level 数据流相同。审计同时确认早期 gap 中的“HM3 写传播”措辞不准确：
binder 不访问 Player 的 HM3 per-node join-snapshot map，也不访问 HM4 variable snapshot。
它访问的是 HM1、HM2 和独立的
`std::multimap<ttstr, MotionParameterEntry *> ParameterRampMap`。

本地 binder、重建器、equal-range updater 和 IEEE normalization 已经逐行吻合；但把旧 node
tree 清掉时，本地错误地执行了 `HM1.writeVal = 1.0`。四端实际都执行
`HM1.weight = 1.0` 并只清空 cached pointer vector 的逻辑长度。本轮已经修正这一行，并增加
回归测试验证最后 write value 保留、重建闸门从 0 重置为 1、下一次 bind 后回到 0。

## 2. 地址与完整指令证据

所有入口均在本轮 fresh decompile；下表反汇编都使用完整函数读取，`cursor.done=true`。

### 2.1 binder、cache rebuild 与 ramp updater

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| binder | `0x6C1A48`，490 | `0x58C4D8`，231 | `0x100116410`，188 | `0x113D54`，291 |
| HM1 cache rebuild | `0x6B6A30`，249 | `0x58466C`，159 | `0x10010D004`，132 | `0x10A930`，187 |
| equal-range ramp updater | binder 内联 | `0x585058`，40 | `0x10010DDE0`，43 | `0x10B708`，42 |

binder 共 1200 条独立指令，rebuild 共 727 条；三个保留的 updater 共 125 条。Android
arm64 在三处 call site 中把 updater 和 normalization 都内联，但 tree equal-range、mode write、
ordered IEEE normalize 和 next-node walk 与另外三端一致。

### 2.2 Engine caller closure

| 角色 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| paired clamp worker | `0x679C88`，253 | `0x55F9A0`，199 | `0x1001B3C9C`，175 | `0x1B38A0`，201 |
| bind-values + clamp driver | progress 内联；另有无 xref clone `0x67A07C`，33 | `0x55FC58`，35 | `0x1001B3F64`，32 | `0x1B3B58`，34 |

这八个函数/clone 共 962 条完整指令。Android arm64 active progress 在 `0x67A784..0x67A7D8`
内联 variable-value map bind loop，随后调用 paired clamp worker；`0x67A07C` 保留相同组合
形状却没有任何 xref，是产品内 dormant duplicate，不能算 root-reachable active caller。其余
三端的 35/32/34 条 driver 各有且只有一个 caller，均来自 Engine progress。

## 3. caller 分母

binder 的所有 code xref 已全量枚举：

| caller 类别 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---|---|---|---|
| Player variable-track interpolation | `0x6B9338` | `0x58613A` | `0x10010F1F4` | `0x10C9E4` |
| Player script raw callback | `0x6CE394` | `0x5946D4` | `0x10011FD70` | `0x11EA14` |
| Engine variable-value bind | active `0x67A7C8`; dormant `0x67A0DC` | `0x55FCA4` | `0x1001B3FC0` | `0x1B3BA2` |
| paired clamp LR/UD writes | `0x67A010/0x67A024` | `0x55FBE4/0x55FC04` | `0x1001B3F00/0x1001B3F14` | `0x1B3AF8/0x1B3B1A` |

因此 arm64 Android 有 6 个机器 call-site，其中一个位于无 caller 的 dormant clone；其余
三端各 5 个，全部 active。没有遗漏的 registration thunk、vtable slot 或 data xref。

rebuild helper 每端各有两个 caller：binder 本身，以及 full-reseek tail 对 HM1 全表的 walk。
这解释了 `weight` 的含义：新 HM1 entry 或换树 reset 将它设为 1；第一次 binder/reseek
把它清 0 并构造 non-owning cache；稳定 node tree 上的后续 bind 复用 cache。

## 4. 共同 binder 伪代码

```text
bindParameterValue(key, mode, value):
    parts = splitParameterLabel(key)       # first "::", else first "/"

    if parts.split:
        cascadeKey = parts.scope + "::" + parts.suffix
        state = HM1.find(cascadeKey)
        if state is missing:
            state = HM1.emplace(cascadeKey, default EvalCascadeState)
            state.keyCopy = cascadeKey
            state.chainSegments = split(parts.scope, "/")
            state.weight = 1.0

        state.writeVal = value
        rebuildEvalCascadeEntry(state)

        for node in state.heapResult:
            if node.type == 4:
                for particleIndex in [0, node.particleCount):
                    child = node.particleChild(particleIndex)
                    applyRamps(child.ParameterRampMap,
                               parts.suffix, mode, value)
            else if node.type == 3:
                child = node.childPlayer
                applyRamps(child.ParameterRampMap,
                           parts.suffix, mode, value)

    HM2[key] = value
    applyRamps(this.ParameterRampMap, key, mode, value)
```

几个容易被“更安全”改写破坏的边界：

- empty scope、empty suffix 和 null-backed empty label 都按 splitter 的结果处理；slash 输入只在
  HM1 key 中规范成 `::`，descendant ramp lookup 使用 raw suffix。
- type-3/type-4 child 只更新该 child 的 ramp entries，不递归调用 child binder，因此不写 child
  HM1/HM2。
- null/malformed child 没有 defensive skip；原版在取得 native child 后直接访问其 ramp map。
- HM2 始终使用原始完整 key，在所有 HM1/child propagation 成功后才写。
- splittable label 仍然会更新本 Player 的 raw-full-key ramp；descendant 则用 suffix key。

## 5. HM1 value、容器和 rebuild 语义

共同 source value 为：

```cpp
struct EvalCascadeState {
    ttstr keyCopy;
    std::vector<ttstr> chainSegments;
    double writeVal;
    double weight;
    std::vector<MotionNode *> heapResult;
};
```

HM1 是 `unordered_map<ttstr, EvalCascadeState, ttstr_hash, ttstr_equal>`；Android/iOS 的
hash-node header、bucket ABI 和 mapped-value base 不同，但 `V+32=writeVal`、`V+40=weight`、
`V+48/+56/+64=heapResult` 在两个 64 位产品中完全一致，32 位端保持同一字段顺序。
heapResult 只借用 stable deque node 地址。

共同 rebuild：

```text
if state.weight == 0: return
state.weight = 0
state.heapResult.clear()                    # capacity retained
scratchChain = []

for nodeIndex = 1 .. nodes.size-1:
    skip type outside {3,4}
    index = nodeIndex
    repeat:
        chainNodeIndex = index
        scratchChain.insert(begin, nodes[index].layerName)
        if scratchChain.size > ref.size:
            scratchChain.pop_back()
        if scratchChain == ref:
            state.heapResult.push_back(&nodes[nodeIndex])
            break
        parent = nodes[chainNodeIndex].parentIndex
        if parent <= 0: break
        index = parent
```

`scratchChain` 故意跨 outer candidate 保留，不在每个 node 前 clear。ref 为空时，每个 eligible
node 插入一个 label 后立即 pop，空链相等，因此全部命中。weight 只对 exact `+0/-0` 走
fast return；NaN 会进入 rebuild 并被写成正 0。

### 5.1 异常/部分提交顺序

- 新 key 的 hash node 先进入 HM1，随后才写 value 内的 keyCopy、chain vector、weight。
  后续复制/分配异常可以留下 default 或部分初始化的已发布 map value。
- existing/new state 的 writeVal 在 rebuild 前提交。
- rebuild 先清 weight 和 heapResult length；scratch ttstr 插入或 heapResult grow 抛出时，
  partial cache 保留、weight 已为 0，普通后续 bind 不会自动重试。
- child propagation 按 heapResult、particle index、equal-key tree order逐项提交；中途 TJS/native
  child 访问异常不回滚先前 entries，HM2 还未写。
- HM2 `operator[]` 分配/复制失败会阻止本 Player ramp 更新；成功后 raw double store 先于
  own equal-range walk。

Android arm64 的 landing、iOS armv7 SjLj cleanup 会释放本帧 ttstr/Variant/scratch owners；
Android armv7 与 iOS arm64 的当前优化形状没有额外可见 source-level rollback。四端都没有
事务性恢复上述容器状态。

## 6. ParameterRampMap 与数值传播

`ParameterRampMap` 是 owning-key / borrowed-value 的
`std::multimap<ttstr, MotionParameterEntry *, ttstr_utf16_less>`。`equal_range` 遍历所有重复
id；每个 entry 的顺序固定为：

```text
entry.mode = mode
entry.value = normalize(entry, rawValue)
```

normalize 不分配且不调用 TJS。equal endpoint 或 `division <= 0` 写正零；discretization 走
signed-int32 toward-zero/saturation；其余使用 ordered min/max/clamp，保留 NaN 与有符号零
边界。完整 IEEE、record owner 和 multimap node 生命周期由
`motionplayer_player_parameter_table_pipeline_four_binary_2026-08-27.md` 交叉闭合。

## 7. 换树生命周期修复

四端 `resetAndReleaseOldNodeTree` 的 HM1 loop 均执行：

```text
for every HM1 node:
    state.weight = 1.0
    state.heapResult.end = state.heapResult.begin
```

具体 mapped-value store：

- Android arm64：hash node `+0x38`，即 `V+40`；
- Android armv7：hash node `+0x28/+0x2C` 两 word，等价 mapped `weight`；
- iOS arm64：hash node `+0x40`，即 `V+40`；
- iOS armv7：hash node `+0x24/+0x28` 两 word，等价 mapped `weight`。

四个 reset 主体本轮再次完整读取 244/212/221/312 条指令。共同代码不写 writeVal，因而
换 motion 后 `getVariable(cascadeKey)` 仍返回最后一次 bind value；但旧 MotionNode 指针被
清空，并允许新树上的第一次 binder/full-reseek 重建。

本地原先将相邻 `writeVal` 设为 1，既破坏查询值又没有 rearm cache。这一行已经改为
`entry.second.weight = 1.0`；测试覆盖 weight `0 -> reset 1 -> next bind 0` 及 writeVal 保留。

## 8. 本地对照与验证

- `PlayerVariable.cpp` 的 splitter、HM1 emplace 字段顺序、writeVal/rebuild/child/HM2/ramp
  顺序、scratch-chain lifetime 和 equal-range updater均与四端一致，无需改写。
- `PlayerMotionLoad.cpp` 的 reset 字段选择已修正为 weight；声明顺序无需改变。
- `Player.h` 只增加只读 differential hook，用于精确观察 weight，不进入 NCB surface。
- `PlayerMotionLoad.cpp` 与 `PlayerVariable.cpp` 已用当前可用 clang/stub include 做 syntax-only
  检查，均通过；只有既有 `_tss` deprecation warning。
- unit TU 仍缺 Catch2，正式 CMake/Emscripten 工具链仍不存在，因此没有伪称已运行正式
  unit/Web build。
- 四个 IDB 已命名 binder/rebuild/updater/caller、添加语义注释/书签并保存。

