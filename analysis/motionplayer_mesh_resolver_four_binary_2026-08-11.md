# MotionPlayer Eye/Eyebrow MeshResolver 四二进制审计（2026-08-11）

## 结论

`EmoteMeshResolver` 不是旧注释所说的“未移植、调用惰性”的旁路，而是 Eye 与 Eyebrow 两类控制器共同使用的实际热路径。两类控制器弹出一个 12 B 主 keyframe 后，都会把当前值、目标值、内嵌图状态和第二条 8 B value track 交给同一 resolver wrapper；wrapper 运行一个有界图搜索，选出严格最短的候选路径，再把候选的 `deque<pair<float,float>>` 复制到第二条 track。

四个当前 1.3.9 参考二进制共同证明：

- 路径元素是一个 8 B 的 `{float from, float to}` pair，不是旧注释声称的“扁平 `deque<float>`”；
- wrapper 的源码级原型是 `(self, startValue, endValue, valueTrack)`；32 位反编译里的 soft-float/寄存器展示不改变这一顺序；
- Android 使用 libstdc++，iOS 使用 libc++，因此 deque header、resolver 嵌入偏移和输出行大小各不相同，但控制流、容器元素、选择条件与边界行为一致；
- `visited` 删除 helper 删除所有相等项，而不是只删第一个；
- state 2/3 在挑选第一个未访问邻居前，必须先把所选 edge boundary 加入 `visited`；漏掉这一步会多生成一个可观察的零长度 `{boundary,boundary}` 段。

本轮据此修正了本地 resolver 的两个运行语义偏差、wrapper 参数结构、Eye/Eyebrow 过时注释和 Eyebrow reset 的旧地址式名称。

## 四平台函数映射

### Resolver wrapper、搜索 core 与调用者

| 参考二进制 | resolver wrapper | bounded search core | Eye step | Eyebrow step |
|---|---:|---:|---:|---:|
| Android ARM64 | `0x65F35C` | `0x65D408` | `0x660FBC` | `0x6629E0` |
| Android ARMv7 | `0x5514C8` | `0x550D80` | `0x552472` | `0x553280` |
| iOS ARM64 | `0x1001A15DC` | `0x1001A0E94` | `0x1001A27A0` | `0x1001A38C8` |
| iOS ARMv7 | `0x1A0768` | `0x1A00B8` | `0x1A19D8` | `0x1A2C56` |

四份 IDB 中采用的保守语义名为：

```cpp
void EmoteMeshResolver_resolve_guess(
    void *self, float startValue, float endValue, void *valueTrack);
void EmoteMeshResolver_search_guess(
    void *self, float startValue, float endValue);
void EmoteEyebrowController_step_guess(
    void *self, float *out, float dt);
```

四个 Eyebrow caller 定型后都直接显示以下源码级调用：

```cpp
EmoteMeshResolver_resolve_guess(
    controller + resolverOffset,
    controller->trackValue,
    keyframe.endValue,
    controller + secondaryTrackOffset);
```

这也解释了旧本地签名为何看似能工作：旧声明把 `valueTrack` 放在第二位，所有旧调用者又以相同的非原生顺序调用，所以内部行为可以自洽；但其源码结构和四个 caller 的共同 ABI 不一致。本轮把声明、定义和 Eye/Eyebrow 两个调用点统一恢复为原生源码顺序。

### 已恢复的搜索 helper

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| 查找首个包含 value 的 edge | core 内联 | core 内联 | `0x1001A0C50` | `0x19FEB4` |
| 删除 `visited` 中所有 value | core 内联 | core 内联 | `0x1001A0C98` | `0x19FEF4` |
| 查找包含 value 的首个 node row | core 内联 | `0x550AD4` | `0x1001A0D2C` | `0x19FF62` |
| node row 中首个未访问 value | core 内联 | `0x550C58` | `0x1001A0DA0` | `0x19FFCC` |
| 弹出末尾路径段并回退距离 | core 内联 | `0x550D04` | `0x1001A0E3C` | `0x1A0068` |
| 追加 `{from,to}` 路径段 | core 内联 | `0x550A86` | core/容器 helper 内联 | core/容器 helper 内联 |
| 复制当前路径并发射 output row | core/容器 helper 内联 | `0x551498` | core/容器 helper 内联 | core/容器 helper 内联 |

iOS 两个独立 erase helper 都在命中后保持 cursor 指向压缩后的当前位置，继续和同一个 value 比较；只有不相等时才递增 cursor。Android 两份 core 给出同一内联删除控制流。因此源码语义是 erase-all：

```cpp
for (cursor = begin; cursor != end; ) {
    if (*cursor == value)
        end = erase_and_compact(cursor);
    else
        ++cursor;
}
```

三个独立 `firstUnvisitedValue` helper 都按 node row 原顺序线性扫描，对每个候选再线性扫描 `visited`，返回第一个没有精确 float 相等项的值；row 为空或全部已访问时返回 `-1.0f`。Android ARM64 将相同逻辑内联进大 core。

## ABI 布局与内部容器

### Resolver state

| ABI | edge vector | node-row deque | output-row vector | resolved span | `sizeof(MeshPathRow)` |
|---|---:|---:|---:|---:|---:|
| Android ARM64 / libstdc++ | `+0`, 24 B | `+24`, 80 B | `+104`, 24 B | `+128` | 88 B |
| Android ARMv7 / libstdc++ | `+0`, 12 B | `+12`, 40 B | `+52`, 12 B | `+64` | 44 B |
| iOS ARM64 / libc++ | `+0`, 24 B | `+24`, 48 B | `+72`, 24 B | `+96` | 56 B |
| iOS ARMv7 / libc++ | `+0`, 12 B | `+12`, 24 B | `+36`, 12 B | `+48` | 28 B |

逻辑成员顺序在四份二进制中相同：

```cpp
struct MeshPathRow {
    std::deque<std::pair<float, float>> path;
    float dist;
};

struct EmoteMeshResolverState {
    std::vector<std::pair<float, float>> edgeTable;
    std::deque<std::vector<float>> nodeRows;
    std::vector<MeshPathRow> outputRows;
    float trackResolvedSpan;
};
```

输出行的最后一个 float 分别位于行内 `+80/+40/+48/+24`，正好等于各 ABI 的 deque header 大小；wrapper 的 stride 分别为 `88/44/56/28`。四个 wrapper 复制路径时也都以 8 B 为一个元素复制两个 float。这排除了“native 只是偶然把两项扁平 float 配对解释”的可能。

### Controller 嵌入位置

| ABI | secondary `deque<pair<float,float>>` | resolver state |
|---|---:|---:|
| Android ARM64 | controller `+80` | controller `+160` |
| Android ARMv7 | controller `+40` | controller `+80` |
| iOS ARM64 | controller `+48` | controller `+96` |
| iOS ARMv7 | controller `+24` | controller `+48` |

Eye 与 Eyebrow 的 caller 在同一 ABI 上使用相同的这两个偏移。resolver state 本身没有 vptr，也不单独分配：它和两条 value track 一起由 controller 拥有。`edgeTable` 与 `nodeRows` 在 controller 构造时从 PSB 数据建立；`outputRows` 是跨调用保留容量、每次 resolve 清空元素的候选缓存；`trackResolvedSpan` 是 wrapper 写、controller step 读的标量。四端 controller 构造函数都没有初始化该标量；normal state-0 setup 必须先调用 wrapper，而 wrapper 的 success/fallback 两条返回路径都会写它，随后 step 才复制到 active span。

## Wrapper 共同控制流与边界

四个 wrapper 定型后的共同伪代码是：

```cpp
destroy_and_clear_each_nested_path(self->outputRows);
clear(valueTrack);
search(self, startValue, endValue);

float bestDist = 99999.0f;
int best = -1;
for (int i = 0; i < outputRows.size(); ++i) {
    float d = outputRows[i].dist;
    if (d != -1.0f && d < bestDist) {
        best = i;
        bestDist = d;
    }
}

if (best == -1) {
    valueTrack.push_back({endValue, endValue});
    trackResolvedSpan = 0.0f;
} else {
    trackResolvedSpan = bestDist;
    resize/copy outputRows[best].path into valueTrack;
}
```

需要保留的精确边界：

- 比较是严格 `<`，所以相同距离保留第一个 candidate；
- `-1.0f` 是唯一显式跳过的 sentinel；其它负值如果出现仍可参与最小值；
- `NaN < bestDist` 为 false，因此 NaN candidate 不会入选；
- 初值不是正无穷而是 `99999.0f`，所以所有有效距离都 `>= 99999` 时也走 fallback；
- wrapper 总是先清旧 destination，fallback 不是在旧 track 后追加；
- 清 `outputRows` 会逐个析构候选行拥有的 deque 节点；选择成功后是元素复制，candidate 与 controller track 不共享 deque 节点。

## Search core 的共同状态机

搜索期间存在四个 scratch 容器/标量：

```text
pathSeg : deque<pair<float,float>>
valStack: deque<float>
visited : vector<float>
dist    : float
```

`pathSeg`、`valStack` 和 `visited` 都在函数返回时析构。值得注意的是，某些 pass 会清 `pathSeg` 与 `valStack`，但 `visited` 不会在每个 restart 一并清空；它只通过精确值删除和函数退出改变生命周期。

共同状态机可写成：

```text
state 0 / init
  clear pathSeg, valStack; dist = 0
  如果 start/current 与 end 都落入某条闭区间 edge：state=1, carry=1
  否则发射 dist=-1 的 sentinel row

state 1 / same-edge close or choose direction
  找 current edge 与 end edge
  若 edge index 不同：carry==1 ? state=2 : state=3
  若相同：
    从 visited 删除该 edge.lo 与 edge.hi 的所有出现项
    dist += abs(end-current)
    pathSeg.push({current,end})
    如果没有相同 dist 的 row，则复制 pathSeg 发射
    根据 valStack 是否为空决定是否还有 pass

state 2 / high-boundary expansion
  boundary = edge(current).hi
  nodeIndex = first row containing boundary
  visited.push(boundary)             // 必须先做
  neighbour = first unvisited value in that row
  若 neighbour != -1：
    dist += abs(boundary-current)
    pathSeg.push({current,boundary})
    visited.push(neighbour)
    valStack.push(current)
    current = neighbour; state=1
  否则进入 backtrack

state 3 / low-boundary restart
  clear pathSeg, valStack
  boundary = edge(start).lo
  nodeIndex = first row containing boundary
  visited.push(boundary)             // 必须先做
  neighbour = first unvisited value in that row
  若 neighbour != -1：追加 {start,boundary}，保存 stack，回到 state 1
  否则进入 end-side close/sentinel 路径

backtrack
  从 valStack 恢复 current
  dist -= abs(pathSeg.back.to-pathSeg.back.from)
  pop pathSeg，并按原控制流额外 pop 一次 valStack（若非空）
  根据入口分支继续 state 2 或 state 3

end-side close
  以 edge(end).hi 查 node row
  做两次“首个未访问值”扫描，中间把 high boundary 加入 visited
  条件满足时追加最终 segment/stack/visited 项
  最终仍可发射 dist=-1 sentinel row
```

edge lookup 是按原顺序返回第一条满足 `edge.lo <= value && edge.hi >= value` 的闭区间；重叠 edge 不会寻找“最佳”区间。node lookup 同样返回第一条包含精确相等 float 的 row。所有 graph 比较都是精确 float 比较，没有 epsilon。

成功 close 只按 `dist` 精确相等去重，不比较路径内容。`-1` sentinel 的若干发射分支不经过成功-row dedup，因此 output vector 可以保留重复 sentinel 行；wrapper 会统一忽略它们。

外层 pass 计数使用后置递增条件 `counter++ <= 8`。counter 从 0 开始，因此在没有更早终止的最坏情况下允许第 9 次完成后再启动一次，至多经历 10 次 pass completion；把它概括为“9 passes”会少算最后一次失败检查。

## 本地偏差与修正

### 1. `visitedErase` 必须删除所有重复项

旧本地实现找到第一个相等项后立即返回。当前 iOS ARM64/ARMv7 的独立 helper 和两份 Android 内联路径都在删除后不前移 cursor，继续检查刚压缩到当前位置的元素。本地已改为 `visitedEraseAll`，循环只在当前值不相等时递增。

这不仅是复杂度或容器写法差异：重复 boundary 会改变后续 `firstUnvisitedValue` 的结果，进而改变 DFS 的分支顺序与候选路径。

### 2. state 2 必须先把 boundary 加入 `visited`

旧本地 state 2 先扫描 row，再只把选中的 neighbour 加入 `visited`。四份 current core 的共同顺序是：

```cpp
boundary = edge[current].hi;
nodeIndex = findNodeRow(boundary);
visited.push_back(boundary);
neighbour = firstUnvisitedValue(nodeIndex, visited);
```

本地已在 neighbour 扫描前补回 `eng.visited.push_back(v175)`。state 3 本来就有同一步骤，因此没有再改。

### 3. Wrapper 源码签名与调用点

本地声明/定义/两个 caller 已统一为：

```cpp
EmoteMeshResolver_resolve(
    &controller.mesh,
    controller.trackValue,
    keyframe.endRad,
    &controller.valueTrack8B);
```

这不改变参数成对自洽的旧 Web 结果，但恢复了四个 native caller 共同证明的函数形状和调用链。

### 4. 已证伪的旧注释

以下说法已从编译源中删除或改正：

- resolver “NOT ported / INERT”；
- 路径是“flattened `deque<float>`”；
- 只有旧 `libkrkr2.so` Android ARM64 地址与 88 B row 才代表实现；
- Eyebrow 的 resolved span 会永远保持 ctor 的 0；
- Eyebrow reset 的函数名必须携带旧单文件地址。

## 回归覆盖

`tests/unit-tests/plugins/motionplayer-dll.cpp` 新增三组无外部 fixture 的确定性结构测试：

1. 空 graph：旧 destination 被清掉，得到单个 `{end,end}`，span 为 0；
2. start/end 位于同一 edge：得到单个直接 segment，span 等于绝对差；
3. bridge graph：

```text
edges = [0,1], [2,3]
node  = [1,2]
start = 0.5
end   = 2.5
```

正确 current-source 输出为：

```text
row 0: dist=1.0, path={0.5,1.0},{2.0,2.5}
row 1: dist=0.5, path={2.0,2.5}
selected track: {2.0,2.5}
```

临时移除 native boundary push 后，同一 graph 的 row 0 会变成三个 segment，并在中间多出 `{1.0,1.0}`；恢复修正后 standalone Emscripten/Node smoke 通过。这个回归用候选 row 的精确拓扑约束了修正，不能被“最终 selected track 恰好相同”的表面结果掩盖。

## 验证状态

- 四份 wrapper/core 已在应用正确原型和语义名后重新反编译；
- 四个 Eyebrow caller 均重新反编译并确认参数顺序与 ABI 嵌入偏移；
- iOS 两份 erase-all helper、Android ARMv7 与 iOS 两份 find-node/first-unvisited helper 均重新反编译；Android ARM64 对应逻辑为 core 内联；
- standalone Emscripten/Node resolver smoke：通过；
- `cmake --build --preset "Web Debug Build"`：通过，resolver/Blink/Eyebrow/Engine 均实际重编译并完成最终链接；
- `cmake --build --preset "Wasmtime Headless Debug Build"`：通过，普通 motionplayer 与 wasmtime guest object 两条 resolver 路径均实际重编译并完成最终链接；
- 完整 `motionplayer-dll.cpp` Emscripten `-fsyntax-only`：通过，只有仓库既有 `_tss` 警告；
- 四份 IDB：已原位保存成功。
