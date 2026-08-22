# MotionPlayer MeshResolver DFS、回溯与候选所有权四参考纵切（2026-08-15）

## 裁决

本轮重新反编译四个当前 `reference/binaries/` 中 Eye/Eyebrow 共用的 mesh resolver，发现
本地实现并非只有 `LABEL_*`/`vNN` 可读性问题，而有三处可观察行为偏差：

1. 两条回溯路径都多弹了一项 `deque<float>` value stack；原生每次恰好弹一条 path
   segment 和一条与之配对的 value；
2. final end-side close 找到 node row、但在压入 end boundary 后没有未访问值时，四端先
   emit 一条 `dist=-1` row，再落入无条件 final emit，故得到两条相同 sentinel row；本地
   只生成一条；
3. wrapper 选出最短 row 后先把空目标 deque 调整到完整长度，再从 row 尾部向目标对应
   下标复制并逐项 `pop_back`；选中 row 留在 `outputRows`，但其 path 被消费为空。本地原来
   顺序 `push_back` 并保留了选中 path。

此外，本地在 state 1/2/3 中给 `findEdge == -1` 增加了不存在于四端的防御分支。正常入口
由 state 0 保证 start/end 位于 edge，后续值来自 graph；若内部状态或 graph 破坏，原生会
把 `-1` 当作 vector 下标继续访问，而不是回退到 0 或提前 sentinel。本轮删除这些伪保护，
保留真实边界。

## 四平台入口与 helper 映射

所有恢复名都带 `_guess`，表示 stripped 产物上的保守语义命名。

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| `EmoteMeshResolver_search_guess` | `0x65D408` | `0x550D80` | `0x1001A0E94` | `0x1A00B8` |
| search size | `0x1E00` | `0x6CE` | `0x6C4` | `0x610` |
| `EmoteMeshResolver_resolve_guess` | `0x65F35C` | `0x5514C8` | `0x1001A15DC` | `0x1A0768` |
| resolve size | `0x330` | `0x1A8` | `0x1AC` | `0x142` |
| `findEdge_guess` | 内联 | 内联 | `0x1001A0C50` | `0x19FEB4` |
| `eraseVisitedValueAll_guess` | 内联 | 内联 | `0x1001A0C98` | `0x19FEF4` |
| `findNodeRow_guess` | 内联 | `0x550AD4` | `0x1001A0D2C` | `0x19FF62` |
| `firstUnvisitedValue_guess` | 内联 | `0x550C58` | `0x1001A0DA0` | `0x19FFCC` |
| `appendSegment_guess` | 内联 | `0x550A86` | 内联 | 内联 |
| `popSegment_guess` | 内联 | `0x550D04` | `0x1001A0E3C` | `0x1A0068` |
| `emitRow_guess` | 内联 | `0x551498` | 内联 | 内联 |

helper 是否独立主要由优化器/inlining 决定；其元素访问和调用顺序四端一致。Android ARM64
把整套 helper 展开到 7680-byte search，因此函数体明显大于另外三端。

## 调用链与 caller 提交差异

```text
Eye/Blink state 0                         Eyebrow state 0
  pop primary 12B command                  retain primary front
  -> resolve_guess                         -> resolve_guess
       clear old candidate rows                 clear old candidate rows
       clear secondary 8B track                 clear secondary 8B track
       bounded DFS                              bounded DFS
       strict-min select / fallback             strict-min select / fallback
  write curve fields                        write curve fields
                                             pop primary 12B command
```

resolver 本身对两个 controller 完全共用；异常所有权差异来自 caller：Eye 在进入 resolver 前
已丢弃 primary command，Eyebrow 则在 resolver 和后续非抛 stores 成功后才 pop。resolver
一进入就先析构旧 `outputRows` 的每条 path，再清空 caller 的 secondary track，因此后续
搜索分配失败时，这两个旧输出已经不可恢复。

## graph 与 scratch 容器

resolver state 的逻辑字段顺序为：

1. `vector<pair<float,float>> edgeTable`：闭区间 `[lo,hi]`；
2. `deque<vector<float>> nodeRows`：每行按存储顺序列出 boundary/neighbor 值；
3. `vector<MeshPathRow> outputRows`：候选行；
4. `float trackResolvedSpan`：wrapper 最终选出的严格最小距离或 fallback 0。

每次 search 在栈上构造：

- `deque<pair<float,float>> pathSeg`：当前有序 segment path；
- `deque<float> valueStack`：每个下降 segment 的回溯 current value；
- `vector<float> visited`：精确 float visited 集；
- `float dist`：逐 segment 累计的分支式绝对差。

`pathSeg/valueStack/dist` 在每个 state-0 pass 重置；`visited` 故意跨 pass 保留，只在成功
same-edge close 时删除该 edge 两个端点的所有相等项。这个持久 visited 是算法在后续 pass
枚举不同候选的关键，不是遗漏的 clear。

## 查找规则

- edge 查找从 index 0 正向扫描，取第一个满足 `lo <= value && hi >= value` 的闭区间；
  重叠 edge 不做最窄/最近选择；
- node 查找从 row 0 正向扫描，再从 row 首元素正向扫描，取第一个含有 `value` 的 row；
- first-unvisited 按 row 原顺序返回第一个不与 visited 中任何元素 `==` 的 float；找不到
  返回 `-1.0f`；
- `-1.0f` 同时是 missing sentinel，所以 graph 中真实的 -1 neighbor 也不会被下降分支
  接受；
- visited erase 删除所有精确相等项：每次命中后 memmove 后缀并停留在同一 cursor，直到
  新 end；
- 所有比较都是原始 float `==/< /<=/>=`，无 epsilon、排序、归一化或 NaN 特判。

## 四状态搜索器

### state 0：pass reset 与入口验证

清空 path/value stack，dist 写 0；依次检查 current 和 end 是否能找到 edge。两者有效则
写 `previousMode=1`、进入 state 1。任一失败就 emit 当前空 path 的 `dist=-1` row，停止
outer restart；end 只在 current 有效时才查找。

### state 1：same-edge close 或选择方向

分别查 current/end 的 first matching edge，并先根据 previousMode 写下一个 state：

- `previousMode==1` -> state 2（取 high endpoint）；
- 否则 -> state 3（取 low endpoint）。

edge index 不同则直接进入所选方向。相同则删除该 edge 的 lo/hi 所有 visited 项，追加
`{current,end}`，累加分支式 `abs(end-current)`，并仅在 outputRows 中不存在完全相等 dist
时复制当前 path 发出候选。

成功 close 后 `valueStack` 是否非空决定要不要从当前 current 再启一个 state-0 pass。
pass counter 使用后增比较：旧值 0..8 允许 restart，旧值 9 终止，因此最多完成十次这类
pass。distance 去重只看 float dist，不比较 path；`NaN == NaN` 为假，会重复 emit。

### state 2：沿 current edge 的 high boundary 下降

直接读取 `edge[findEdge(current)].hi`，找第一条包含该 boundary 的 node row；找到后先把
boundary push 到 visited，再找第一个未访问 neighbor。neighbor 不为 -1 时依次提交：

1. `dist += branchAbs(boundary-current)`；
2. path push `{current,boundary}`；
3. visited push neighbor；
4. valueStack push current；
5. current=neighbor，state=1。

找不到 row/neighbor 时：valueStack 空则写 `previousMode=-1` 并切 state 3；非空则读取
stack.back 为 current，pop 恰好一个 path segment 并从 dist 减去它的 branchAbs，再 pop
恰好一个 valueStack element，保持 state 2 重试。旧本地代码在这里多 pop 一项。

### state 3：沿 low boundary 回溯及 final end-side close

下降逻辑与 state 2 相同，但读取 `edge[findEdge(current)].lo`。失败且 valueStack 非空时，
同样只 pop 一对 path/value，保持 state 3 重试。

valueStack 为空时进入一次性的 end-side close：直接读取 end edge 的 high boundary，找包含
它的 node row。找到 row 后先做一次 first-unvisited 得 `closeValue`，再 push high boundary，
再做第二次 first-unvisited：

- 第二次得到普通值：追加 `{current,closeValue}`，push visited/valueStack 并改 current；
- 第二次得到 `-1`：立即复制当前 path 发出一条 `dist=-1` row；
- 无论上面哪条，随后都再复制当前 path 发出无条件 final `dist=-1` row，并返回。

所以“row 存在但第二次 scan exhausted”会得到两条 sentinel；“row 不存在”只得到无条件
的一条。这两条都被 wrapper 的 `d != -1` 过滤，但 `outputRows` 数量与析构成本可观察。

## wrapper 选择、转移与生命周期

search 返回后，wrapper 以 `bestDist=99999.0f`、`best=-1` 正向扫描 rows。仅当
`d != -1.0f && d < bestDist` 时更新，因此：

- 相等距离保留最早 row；
- `d==99999` 或更大不合格；
- NaN 的 `<` 为假，不合格；
- 没有合格 row 时 push `{endValue,endValue}`，成功后再写 span=0。

有 best 时先把 span 写 bestDist，再把已清空的 secondary track resize 到 best path 的
完整长度。随后从 path.back 开始，复制到目标相同的倒序 index，每复制一项就 pop best
path。结果是目标保持原 segment 顺序、选中 row 的 path 为空、未选 rows 不变。

这个顺序也定义异常边界：span 在 resize 前已提交；resize 分配失败时 best row 尚未 pop；
resize 成功后 pair 复制和 `pop_back` 均不抛。原本的逐项 `push_back` 会在中途分配失败时
留下部分目标并保留完整 source row，与四端不同。

## ABI 容器边界

- `MeshPathRow` stride：Android 64/32 为 88/44 B，iOS 64/32 为 56/28 B；差异来自
  libstdc++ 与 libc++ 的 deque header 和尾部对齐；
- 8B path deque block：Android libstdc++ 为 64 elements/512 B，iOS libc++ 为
  512 elements/4096 B；
- 4B value-stack deque block：Android 为 128 elements/512 B，iOS 为
  1024 elements/4096 B；
- node-row deque 的 element 是平台宽度对应的 `vector<float>`：Android 64/32 block 分别
  容纳 21/42 rows，iOS 64/32 block 分别容纳 170/341 rows；
- outputRows 是连续 vector；扩容或 clear 会按 row 顺序构造/析构其拥有的 path deque。

这些 block 数只用于解释 native cursor/free 边界；Web 继续使用目标 libc++ 的标准容器，
保持逻辑元素、所有权和操作顺序，不伪造 Android header。

## NaN、越界与其他未防御行为

- start/end 为 NaN 时 state 0 的 edge 查找失败，产生 sentinel/fallback；
- node row 中的 NaN 与任何 visited NaN 都不相等，因此可被 first-unvisited 选中；随后
  current=NaN，state 1/2/3 的 edge 查找可能返回 -1 并直接越界索引 edge；
- state 1 若被破坏到 current/end 都找不到 edge，两次查找都为 -1，index 相等，随后也会
  直接访问 `edge[-1]`；
- lo>hi 的 edge 不会匹配普通值；重叠闭区间端点取 first row/edge；
- segment absolute value不是 `fabsf`，而是 `delta=to-from; if(delta<0) delta=-delta`；
  NaN 不翻转，signed zero 也不额外正规化；
- 所有 scratch/output 分配都可抛；没有事务、回滚或 null 参数检查。

## 本地、测试与 IDB 更新

- `EmoteMeshResolver.cpp` 删除整片 `LABEL_*`、`vNN` 和重复手写 row scan，恢复明确的
  state 0/1/2/3 switch、单项回溯、跨 pass visited 与 double-sentinel final close；
- wrapper 改为预 resize、倒序 copy、逐项 pop selected row；公共 stripped 入口统一命名为
  `EmoteMeshResolver_resolve_guess`，Eye/Eyebrow caller 与测试同步；
- 回归新增：selected row path 被消费、两层下降后的单项回溯不插入 `{2,2}`、end-side
  exhausted row 产生两条 sentinel；
- 四份 IDB 在 search entry、单项回溯、第一/第二 sentinel 与 destructive selected-row
  transfer 处加入注释；double sentinel 与 selected transfer 加入书签。

## 验证

- `motionplayer-dll.cpp` Emscripten 单翻译单元 `-fsyntax-only`：通过，仅有既有 `_tss`
  deprecated warning；
- 独立 Emscripten/Node resolver 行为探针：通过，实际验证深回溯 path、双 sentinel、严格
  最短输出及 selected row 为空；
- `cmake --build --preset "Web Debug Build"`：通过；重新编译 resolver、Eye/Blink、Eyebrow
  与受 header 影响的 caller，成功链接 motionplayer、最终 Wasm 和 `index.html`。输出只有
  既有 `_tss`、imagepacker attributes、pthread memory-growth、JSPI/internal-symbol 警告；
- 限定本纵切文件的 `git diff --check` 与行尾空白扫描：通过；resolver source/header 的
  `LABEL_*`、IDA `vNN`、`sub_*`、旧产物名、绝对地址与裸 `*((...))` 扫描为空；
- 四份 recovery IDB 已强制刷新 search/resolve 与所有已恢复 helper 的 decompile cache、
  保存并回读。search 尺寸回读为 `0x1E00/0x6CE/0x6C4/0x610`，resolve 为
  `0x330/0x1A8/0x1AC/0x142`；单项回溯、双 sentinel 与 destructive transfer 注释/书签
  均可解析。
