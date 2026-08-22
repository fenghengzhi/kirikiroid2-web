# MotionPlayer parameter 管线四参考二进制复原（2026-08-12）

## 结论

本轮以 `reference/binaries/` 的 Android ARM64、Android ARMv7、iOS ARM64、
iOS ARMv7 四个当前参考二进制共同为准，重新复原了 parameter 的解析、记录构造、
初值继承、归一化、ramp 注册、绑定传播、节点选择、Player 级选择与析构清理链。

最重要的旧端口偏差如下：

1. native Player **没有**内嵌的默认 `MotionParameterEntry`。构造器只清零一个
   `MotionParameterEntry *` 选中项指针；该指针借用 parameter vector 中的记录。
2. node 的 parameter 指针与 Player 级选中项是两条独立数据流。node 的
   `parameterize` 只有在 Variant 类型为 Integer 时才选择 vector 记录；任何其他类型
   都写 null，绝不会回退到 Player 级选中项或合成默认记录。
3. parameter ramp 容器是有序 `multimap<ttstr, MotionParameterEntry *>`，空 id 与重复
   id 均无条件保留；binder 以 `equal_range` 更新全部同名记录。
4. init 只清 parameter vector，不清 ramp multimap。反复 init 时旧 map 节点可继续借用
   已释放的 vector 地址；这是四端一致的 native 生命周期/悬空指针边界，端口不能擅自
   “修复”。
5. 旧窄字符串 split 会破坏 UTF-16 label；当前实现已改成 `ttstr` 上的第一次分隔符
   切分，保留空前缀、空后缀与连续分隔符。

## 四端函数映射

| 语义名（推测名保留 `_guess`） | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `Player_appendParameterEntry_guess` | `0x6AEAF8` | `0x57FA14` | `0x100106D00` | `0x104168` |
| `Player_parseParameterList_guess` | `0x6AF40C` | `0x57FFE8` | `0x100107370` | `0x1048FC` |
| `Player_finalizeParameterTable_guess` | `0x6AF2AC` | `0x57FF44` | `0x1001072C4` | `0x1047FC` |
| `Player_readInitialParameterValue_guess` | `0x6AEE9C` | `0x57FCF0` | `0x100106FEC` | `0x10451C` |
| `splitParameterLabel_guess` | `0x6CDFD4` | `0x594508` | `0x10011F8D0` | `0x11E494` |
| `Player_bindParameterValue_guess` | `0x6C1A48` | `0x58C4D8` | `0x100116410` | `0x113D54` |
| `Player_selectParameterEntry_guess` | 内联 | `0x58175C` | `0x1001090E8` | `0x106944` |
| `normalizeParameterValue_guess` | 内联 | `0x57FC38` | `0x100106F78` | `0x10446C` |
| `Player_applyParameterRamps_guess` | 内联 | `0x585058` | `0x10010DDE0` | `0x10B708` |
| `Player_purgeParameterRampMap_guess` | `0x6CB1F8` | `0x592CD0` | `0x10011DA9C` | `0x11C3D0` |
| `Player_initNonEmoteMotion_guess` | `0x6B0A3C` | `0x580C28` | `0x100108258` | `0x1058F8` |
| `Player_initNodeFields_guess` | `0x6B1058` | `0x580FA4` | `0x100108720` | `0x105E70` |
| `Player_ctor_guess` | `0x6CC110` | `0x5935C4` | `0x10011EC04` | `0x11D488` |
| `Player_dtor_guess` | `0x6CCEBC` | `0x593C24` | `0x10011F2A0` | `0x11DCC4` |

另有一个已存在、容易混淆的 `Player_readBoundParameterValue_guess`：

| Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---:|---:|---:|---:|
| `0x6CA77C` | `0x592810` | `0x10011D3D8` | `0x11BD50` |

它只读取当前 Player：可切分 key 走 HM1 的 `writeVal`，不可切分 key 走 HM2；它既是
`Motion.Player.getVariable` 的直接 target，也是 EmoteEngine facade 的最终 fallback，
不是 append 阶段的跨父链初值解析器。因此本轮将后者命名为
`Player_readInitialParameterValue_guess`，避免 IDB 名称碰撞与错误合并。

## ABI 与容器布局

| 目标 | Player 级选中指针 | parameter vector | ramp multimap | node parameter 指针 | entry 大小 |
|---|---:|---:|---:|---:|---:|
| Android ARM64 | `+376` | `+384` | `+408` | `node+8` | 56 B |
| Android ARMv7 | `+248` | `+252` | `+264` | `node+4` | 48 B |
| iOS ARM64 | `+288` | `+296` | `+320` | `node+8` | 56 B |
| iOS ARMv7 | `+200` | `+204` | `+216` | `node+4` | 44 B |

2026-08-15 fresh binder/helper 复核补充：binder 尾部写入的 Player HM2 并不在四端
共同 `+320`。实际偏移为 Android arm64 `+320`、Android armv7 `+220`、iOS arm64
`+248`、iOS armv7 `+180`。四端调用的 operator[] 与 Engine HM7、Player join variable
snapshot 复用同一 `LabelValueMap` specialization；caller receiver offset 才决定具体 map。
旧编译源码中的单端 HM2/upsert 地址注释已迁移，节点 ABI 和精确调用点见
`analysis/motionplayer_transform_constants_player_hm2_comment_migration_four_binary_2026-08-15.md`。

四端共同的源级 entry 字段序列是：

```cpp
ttstr id;
bool discretization;
double rangeBegin;
double rangeEnd;
double division;
double value;
int mode;
```

32 位端因 `ttstr`、指针与 double 对齐规则不同而具有 48 B/44 B 两种记录步长；不能把
ARM64 的 56 B 硬编码到源级算法。ramp map 的 mapped value 是对 vector 元素的借用指针，
不是拥有关系。

## 解析、追加与异常边界

### 2026-08-16 V145 fresh addendum：nested ncb source identity

本节原有伪代码与渐进提交结论继续成立，但“读取 `parameters.Count` / `parameter.field`”只描述
了值语义，没有闭合发布物的C++ source shape。V145重新反编译四端后确认：parse为一个复制/
强制/保活的root `ncbPropAccessor`，Count只读一次，indexed item使用typed flags-0 getter；
每个item Variant在append后立即析构，而finalize发生在root accessor释放之前。append另建一个
复制/强制/保活的item accessor，且它早于vector default append；四个required字段使用四次
typed flags-0 `GetValue`，其中 `id` hint与 `Player_getCommandList_guess`共享同一数据槽。

optional `division` 不是先probe再二次get，而是单次
`checkVariant(TJS_MEMBERMUSTEXIST, hint=null)`。失败HRESULT即使伴随usable写值仍走missing差值
分支，写出的scratch只参与随后析构；Count/required/indexed普通getter则忽略这种failure-after-
write HRESULT并继续转换。本地已移除这两函数的raw property wrapper，加入root/item重入保活、
共享hint、strict probe和finalize-before-root-dtor探针。地址、owner树、四端逐调用表与验证见
`analysis/motionplayer_parameter_parse_append_nested_ncb_accessor_strict_division_four_binary_2026-08-16.md`。

四端共同伪代码：

```text
parse(parameters):
    if parameters.Type == Void:
        return false
    count = parameters.Count
    for i in [0,count):
        append(parameters[i])
    finalize()
    return true

append(parameter):
    if parameter.Type != Object:
        return

    entries.emplace_back()       // 必须先扩容/零构造
    e = entries.back()
    e.id             = parameter.id
    e.discretization = parameter.discretization
    e.rangeBegin     = parameter.rangeBegin
    e.rangeEnd       = parameter.rangeEnd

    if parameter.division 存在:
        e.division = AsReal(parameter.division)
    else:
        e.division = e.rangeEnd - e.rangeBegin
        if e.division <= 0:
            e.division = 1

    e.value = normalize(e, readInitial(e.id))
```

重要边界：

- 非 Object 元素被静默跳过；parse 仍继续并最终返回 true。
- vector 增长发生在第一次属性读取之前。任一属性访问或转换抛异常时，新记录仍留在
  vector 中，且可能只写了一部分字段。
- 显式 `division` 不做 `<= 0`、NaN 或 infinity 修正；只有缺失 `division` 的推导路径
  才在差值 `<= 0` 时改为 1。
- parse 的非 Void 输入都会尝试 Count/按数字索引枚举，并不先要求 Object 类型。

## 归一化

```text
if begin == end or division <= 0:
    value = 0
else:
    v = discretization ? saturated-signed-int32-toward-zero(raw) : raw
    v = clamp(v, min(begin,end), max(begin,end))
    value = division * (v - begin) / (end - begin)
```

这里的第一个 guard 是端点之间的直接 `==`，不是先算 `end - begin` 再比较零；因此
同号的两个 infinity 也会走归零分支。`division <= 0` 是有序比较，NaN 不会被归零。
四端的 discretization 都是带饱和边界的 signed-int32 向零转换：NaN 转 0，正负溢出
分别转 `INT32_MAX`/`INT32_MIN`。min/max/clamp 也按参考指令的 operand order 显式实现，
从而保留 raw NaN 与 `-0.0`。完整指令对照与回归矩阵见
`motionplayer_parameter_normalization_ieee_conversion_four_binary_2026-08-16.md`。

## 初值解析的数据流

append 完成元数据读取后，不直接把 value 设为 0，而是调用跨 Player 链解析器：

```text
suffix = "::" + id
original = this

for current = this; current != null; current = current.parent:
    if current.HM2 contains raw id:
        return current.HM2[id]

    parent = current.parent
    if parent == null:
        break

    for each (key,state) in parent.HM1:
        if key contains suffix as substring:
            for each node in state.heapResult:
                if node is type 3 and node.childPlayer == original:
                    return state.writeVal
                if node is type 4 and any particle child == original:
                    return state.writeVal

return 0
```

注意 HM1 key 判定是“包含 `::id` 子串”，不是后缀相等或完整 key 相等；首次命中按
容器迭代顺序返回。heapResult 必须直接指向原始调用 Player 对应的 child/particle，
不是任意祖先 Player。

## finalize、绑定与 multimap 行为

finalize 对本 Player 的每一条 entry，从本 Player 一直走到祖先链顶端，并在每一层
ramp multimap 中执行：

```text
destination.rampMap.emplace(entry.id, &entry)
```

不存在空字符串过滤或去重。由此可观察到：

- 空 id 会形成合法 map 节点；
- 重复 id 会形成多个等价 key；
- ancestor map 可以同时持有多个后代 Player 的同名 entry 指针；
- binder 的 `equal_range` 会更新全部重复项。

label split 只找第一个分隔符：优先第一个 `::`，完全不存在时才找第一个 `/`。成功时
保留左右空字符串；失败时 native 的两个输出参数保持原值。端口使用返回结构表达成功/
失败，但同样不对空半边做过滤。

共同 binder 伪代码：

```text
bind(rawKey, mode, value):
    if split(rawKey, scope, suffix):
        cascadeKey = scope + "::" + suffix
        state = HM1.find_or_insert(cascadeKey)
        if first insertion:
            state.keyCopy = cascadeKey
            state.chainSegments = split(scope, '/')
            state.weight = 1
        state.writeVal = value
        rebuildHeapResult(state)

        for each direct child Player selected by state.heapResult:
            applyRamps(child.rampMap, suffix, mode, value)

    HM2[rawKey] = value
    applyRamps(this.rampMap, rawKey, mode, value)

applyRamps(map,key,mode,value):
    for each entry in map.equal_range(key):
        entry.mode = mode
        normalize(entry,value)
```

因此带 split 的 bind 同时写 HM1 和 HM2；无 split 只写 HM2。无论是否 split，最后都以
raw key 更新本 Player ramp。descendant ramp 则使用 suffix。`mode` 只写命中的 entry，
不进入 HM1/HM2 数值容器。

## init、Player 级选择与 node 级选择

### Player init

四端 init 顺序一致：先取得 motion 内容各 Variant owner，再清 node-label map 与
parameter vector；ramp multimap 不清。

```text
parameterize = content.parameterize
if parameterize.Type == Object:
    append(parameterize)
    finalize()
    if entries is non-empty:
        selected = &entries[0]
    // 空边界不写 selected，保留旧值
else:
    parse(content.parameter)
    if parameterize.Type == Integer:
        selected = select(parameterize)
    else:
        selected = null
```

`select` 使用 unsigned index 与 vector size 比较，所以负整数同样越界，并抛出固定文本
`parameter id out of range.`。object 分支只有在 append 确实产生记录后才写 selected；
属性读取异常发生在写指针之前，会保留旧指针值。

### Player 构造与选中项所有权

四端构造器在以下位置把选中项指针清零：

- Android ARM64 `0x6CC544`；
- Android ARMv7 `0x59387C`；
- iOS ARM64 `0x10011EECC`；
- iOS ARMv7 `0x11D936`。

构造器没有初始化任何独立 default entry。Player progress 只在 selected 非 null 时读取
`selected->value` 驱动 Player 游标；普通时间轴路径在 selected 为 null 时进入。

### Node 初始化

每个 node 单独读取它自己的 `layer.parameterize`：

```text
if parameterize.Type == Integer:
    if unsigned(index) >= entries.size:
        throw "parameter id out of range."
    node.parameterEntry = &entries[index]
else:
    node.parameterEntry = null
```

node 不读取 Player.selected。后续 timeline seek、child-motion mode、particle timer 等消费
者都以 node pointer 是否为 null 分流：有 pointer 读 entry 的 `value`/`mode`；无 pointer
使用 Player 当前时间或 mode 0。旧端口的合成 `_defaultParameterEntry` 会把无
`parameterize` 的 node 错误地当成 parameterized node，改变 seek、事件触发、child
crossfade 和 particle timer，现已删除。

`mode` 还有一个此前遗漏的生命周期边界：它只在一次成功走到尾部的 `updateLayers` 中
有效，type-3 child-motion pass 消费后，caller 遍历同一 parameter vector 把所有 mode
归零，而保留 value 和其余字段。四端尾部与本地伪 per-node vector 的纠正见
`analysis/motionplayer_update_layers_parameter_mode_reset_four_binary_2026-08-14.md`。

## 析构与清理

Player 析构器在 parameter vector 析构前调用 purge。purge 对本 Player 每个 entry 与
本 Player/全部祖先执行：

```text
range = destination.rampMap.equal_range(entry.id)
for node in range:
    if node.mappedPointer == &entry:
        erase(node)
```

它不会按 key 整段删除，因为同一 key 可能还映射兄弟/其他后代 Player 的 entry。精确
指针清理必须发生在 vector 释放存储之前。四端析构入口为表中的四个地址，purge 均是
当前 Player 析构体的首个 parameter 生命周期动作。

## 源码修正

本轮涉及：

- `cpp/plugins/motionplayer/PlayerVariable.cpp`
  - append 改为先 `emplace_back`、再原位逐字段写入；
  - parse/finalize/purge/read-initial/split/bind 改为当前四端语义名；
  - 删除 finalize 的空 id 过滤；
  - 恢复父链 HM2/HM1 初值解析；
  - split 全程使用 `ttstr`，并复用 UTF-16 scope segment splitter；
  - normalize 原位写 entry，ramp 通过 multimap `equal_range` 更新重复项。
- `cpp/plugins/motionplayer/PlayerCore.cpp`
  - init 使用当前 parse/finalize 语义；
  - 删除合成 default entry 的构造/init 重置；
  - Player 级字段只保留借用 vector 的 `_selectedParameterEntry`。
- `cpp/plugins/motionplayer/PlayerInternal.h`
  - node resolver 在无整数索引时返回 null，不再回退到 Player 级选中项或合成记录。
- `cpp/plugins/motionplayer/NodeTree.cpp`、`MotionNode.h`、
  `PlayerFrameProgress.cpp`、`PlayerUpdateChildMotion.cpp`
  - 对齐 node/Player 两种 parameter 指针的数据流并清理过时注释。
- `tests/unit-tests/plugins/motionplayer-dll.cpp`
  - 增加无整数 `parameterize` 的 node resolver 必须返回 null 的回归断言。
- differential trace 脚本
  - trace 符号同步为 `Player_parseParameterList_guess`。

## IDB 改进

四个 IDB 均已：

- 为 append、parse、finalize、purge、read-initial、split、bind 建立语义名、函数类型与
  生命周期/异常/容器注释；
- 在显式存在的三端为 select、normalize、apply-ramps 建立语义名；Android ARM64 保留
  内联事实；
- 将四端 node initializer 命名为 `Player_initNodeFields_guess`，补充 node pointer 与
  Player selected pointer 完全独立的注释；
- 在四端构造器及 selected-pointer 清零指令处注明“没有 embedded default entry”；
- force recompile 后保存回各自 `.i64`。

## 验证

- Web Debug：`motionplayer` 静态库构建通过；
- Wasmtime Debug：`motionplayer` 静态库构建通过；
- 完整 `tests/unit-tests/plugins/motionplayer-dll.cpp` 使用 Web Debug 的真实 Emscripten
  参数执行 `-fsyntax-only` 通过，仅有仓库既有 `_tss` 弃用警告；
- Web Debug `krkr2` 完整链接通过；
- Wasmtime Debug `krkr2_wasmtime_guest` 完整链接通过；首次调用的 shell 观察窗口超时
  时 ninja 仍在后台继续，等待其结束后增量复验返回 `ninja: no work to do.`。
