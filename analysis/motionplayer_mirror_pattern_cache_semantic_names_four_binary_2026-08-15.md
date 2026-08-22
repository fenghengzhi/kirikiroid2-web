# MotionPlayer 镜像模式/缓存三容器语义命名（四参考，2026-08-15）

## 结论

`EmoteEngine` 的十个控制器 deque 之后紧接三个镜像标签判定容器：

1. `_mirrorVariablePatterns`：从 PSB `variableMatchList` 逐项追加的
   `std::vector<ttstr>`；
2. `_mirrorMatchCache`：已经判定为匹配的标签集合；
3. `_mirrorMissCache`：已经判定为不匹配的标签集合。

旧字段名 `_variableMatchList800`、`_mirrorMatchSetHM1_824` 和
`_mirrorMissSetHM2_880` 混合了 Android arm64 偏移与调查期 HM 编号。
四个参考二进制中的偏移不同，但容器顺序、数据流和边界完全一致，所以当前
源码和测试改用上述用途名。

## 四 ABI 布局

| 容器 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| 模式向量 | `+800` | `+400` | `+480` | `+240` |
| 命中缓存 | `+824` | `+412` | `+504` | `+252` |
| 未命中缓存 | `+880` | `+440` | `+544` | `+272` |

模式向量在 64 位 ABI 上是 24 字节、在 32 位 ABI 上是 12 字节。
两个缓存都是 `unordered_set<ttstr>`，但 STL ABI 不同：Android 的旧
libstdc++ 集合头分别占 56/28 字节，iOS libc++ 集合头分别占 40/20 字节。
这正是 HM 偏移后缀不能作为跨平台字段名的直接证据。

## 函数映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
| --- | ---: | ---: | ---: | ---: |
| `EmoteEngine_buildMirrorControl_guess` | `0x66C744` | `0x558C24` | `0x1001AB4F4` | `0x1AABCC` |
| `EmoteEngine_shouldMirrorLabel_guess` | `0x679A90` | `0x55F8FC` | `0x1001B37C4` | `0x1B3394` |
| 清空模式与缓存辅助函数 | `0x666A98` | `0x5559C8` | `0x1001A6664` | `0x1A5D54` |
| 元数据总重置 | `0x666D08` | `0x555AD8` | `0x1001A67BC` | `0x1A5F4C` |

`_mirrorChanged` 判定门字节在四 ABI 中分别位于
`+1158/+590/+790/+406`。

## `variableMatchList` 构建数据流

四个 `buildMirrorControl` 都通过同一个专用属性提示槽读取
`variableMatchList`，随后：

1. 取得数组计数；
2. 按升序索引读取每个 Variant；
3. 把 Variant 转换为 `ttstr`；
4. 在模式向量尾部复制追加。

该 builder 本身没有 `enabled` 门、空串过滤、去重或预清空，也不触碰两个
缓存。因此直接重复调用会保留旧模式并继续追加；重复项和空字符串也原样保存。
正常元数据替换在更外层先调用总重置，所以通常观察到的是新列表，但这个
builder-local 边界不能被改写成“每次先 clear”。

## 镜像判定的精确调用链

`shouldMirrorLabel` 的顺序在四份实现中一致：

1. `_mirrorChanged == 0`：立即返回 `false`，不计算哈希、不查缓存。
2. `_mirrorMatchCache` 命中：立即返回 `true`。
3. `_mirrorMissCache` 命中：立即返回 `false`。
4. 按模式向量顺序扫描，调用 `label.IndexOf(pattern, 0)`。
5. 只有该调用返回 `>= 1` 才算匹配；插入命中缓存并返回 `true`。
6. 全部失败时插入未命中缓存并返回 `false`。

这里的条件不是常见的 `>= 0`。下列行为都是原版可见边界：

- 模式第一次出现在位置 0 时算不匹配；
- 即使同一模式在标签稍后再次出现，`IndexOf` 仍只返回第一次出现的位置，
  因而“前缀一次、内部再一次”的标签仍会进入未命中缓存；
- 命中/未命中结果跨 `_mirrorChanged` 开关切换保留；门关闭时只是绕过查询；
- 直接改变模式向量不会使已有缓存失效，必须走元数据重置才会一起清除。

两个集合使用 `ttstr` backing 上的缓存哈希和字符串相等比较。集合插入路径在
分配节点前再次检查目标集合；Android 节点布局为 `{next,key,hash}`，iOS 为
`{next,hash,key}`，节点尺寸分别随指针宽度为 24/12 字节。这是底层 STL ABI
差异，Web 侧只复原键语义、缓存生命周期和查询顺序，不假装对象字节布局相同。

## 清空、容量和析构

元数据总重置调用专用清空辅助函数，严格按以下顺序执行：

1. 释放模式向量中的每个 `ttstr`，把 `end` 回卷到 `begin`，保留向量容量；
2. 清空命中缓存，释放所有键/节点但保留 bucket 分配、bucket count、负载因子
   和 rehash policy；
3. 以相同方式清空未命中缓存。

正常对象析构按成员逆声明顺序执行：未命中缓存、命中缓存、模式向量。

## 源码迁移

| 旧名 | 新名 |
| --- | --- |
| `_variableMatchList800` | `_mirrorVariablePatterns` |
| `_mirrorMatchSetHM1_824` | `_mirrorMatchCache` |
| `_mirrorMissSetHM2_880` | `_mirrorMissCache` |

本轮未改变容器类型、声明顺序、匹配表达式、缓存次序或清空时机。绝对地址和
ABI 偏移只保留在恢复记录及 IDB 中；编译源码只表达四份参考共同成立的用途。
