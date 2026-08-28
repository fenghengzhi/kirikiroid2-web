# Player 参数表流水线四参考二进制联合恢复

日期：2026-08-27

## 1. 结论

四个参考二进制共同证明，普通 motion 初始化中的参数子系统由三类不同所有权对象组成：

1. `vector<MotionParameterEntry>` 独占、连续存放参数记录；
2. 每个 Player 的 `multimap<ttstr, MotionParameterEntry *>` 独占 key 和树节点，但只借用
   vector 元素地址；
3. `_selectedParameterEntry` 只是指向同一 vector 的单个非 owning alias。

参数对象先在 vector 尾部发布一个零初始化记录，再按固定顺序读取 `id`、
`discretization`、`rangeBegin`、`rangeEnd`、可选 `division`。初值先查当前/祖先 Player 的
HM2，再查父级 HM1 cascade cache；随后执行 signed-int32 离散化、ordered clamp 和
division-scaled normalization。list 完整解析后，所有记录无条件插入本 Player 及每个祖先
Player 的 multimap，空 key 和重复 key 都保留。

本轮也把销毁端闭环：`Player::~Player()` 的第一步会按 vector 元素地址精确擦除 self/ancestor
ramp map 中的借用节点，然后才 clear vector。相反，普通 motion 重载路径在取得新 motion
owners 后直接 clear parameter vector，并不会先 purge ramp map；若后续参数读取、索引选择或
节点构建抛异常，旧 map 中的悬空借用节点与 `_selectedParameterEntry` 都可能成为可观察的原生
边界。这不是本地防御性清理可以擅自修正的行为。

本地 `PlayerVariable.cpp`、`PlayerCore.cpp`、`RuntimeSupport.h` 与
`internal/player_containers.h` 已经匹配这份共同源码结构和边界，本轮不需要修改运行时 C++。

## 2. 四端函数映射与完整指令覆盖

### 2.1 主流水线

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| append entry | `0x6AEAF8`，230 | `0x57FA14`，152 | `0x100106D00`，116 | `0x104168`，217 |
| finalize table | `0x6AF2AC`，88 | `0x57FF44`，55 | `0x1001072C4`，38 | `0x1047FC`，82 |
| parse list | `0x6AF40C`，110 | `0x57FFE8`，72 | `0x100107370`，55 | `0x1048FC`，101 |
| read initial value | `0x6AEE9C`，258 | `0x57FCF0`，190 | `0x100106FEC`，152 | `0x10451C`，232 |
| normalize | inline after `0x6AED4C` | `0x57FC38`，49 | `0x100106F78`，29 | `0x10446C`，52 |
| select entry | inline at `0x6B0DF8..0x6B0E50` | `0x58175C`，26 | `0x1001090E8`，27 | `0x106944`，26 |

四端上述独立函数都 fresh decompile；所有独立函数的完整 disassembly cursor 均 `done=true`。
Android arm64 把 normalize 内联进 append，并把 select 内联进两个调用点；其 `initNonEmote`
完整 384 条指令已经在 companion playback-state 报告中闭合，本轮又读取选择区间并在 IDB
行注释中固定边界。这里没有把“没有独立函数”误判成语义缺失。

### 2.2 容器插入与析构 purge

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| specialized multimap insert | `0x6EEA8C`，58 | `0x5AC9F8`，27 | `0x100140858`，35 | `0x141B1C`，69 |
| purge borrowed entries | `0x6CB1F8`，63 | `0x592CD0`，67 | `0x10011DA9C`，63 | `0x11C3D0`，63 |
| Player destructor caller | `0x6CCEBC` | `0x593C24` | `0x10011F2A0` | `0x11DCC4` |

四个 insert helper 都只有对应 finalize 的一个 code xref；四个 purge helper 都只有 Player
destructor 的一个 code xref。它们在本轮 fresh decompile，独立函数 disassembly 全部完整；四个
destructor 也重新反编译以核对 purge -> vector clear -> variable deque clear 的先后顺序。

本轮读取上述 4×UTF-16 literal 原始字节，四端 cascade prefix 地址分别是
`0x14D619E`、`0xD85B3C`、`0x10195C78A`、`0x174EAEE`，开头均严格为：

```text
3A 00 3A 00 00 00
```

即 `"::"` 加终止符。IDA 字符串 renderer 在部分库中只显示 `":"` 或合并成一个宽字符，不能
据此把本地两冒号前缀改成单冒号。

## 3. 恢复的记录结构与 ABI 差异

共同源码字段顺序为：

```cpp
struct MotionParameterEntry {
    ttstr id;              // owning CopyRef
    bool discretization;
    double rangeBegin;
    double rangeEnd;
    double division;
    double value;
    int mode;
};
```

自然 ABI record stride 不同：Android arm64 与 iOS arm64 都是 56 bytes，Android armv7 是
48 bytes，iOS armv7 是 44 bytes。这是标准库/对齐差异，不应通过 padding 写进便携源码。

`ParameterRampMap` 的共同抽象是：

```cpp
std::multimap<ttstr, MotionParameterEntry *, ttstr_utf16_less>
```

插入 helper 会为 key 的 `ttstr` backing 增加引用并把 entry 地址原样写入 mapped slot。树节点
销毁时释放 key，绝不析构或 delete mapped pointer。Android 与 iOS 的节点大小、header/sentinel
布局和 insertion helper 形状不同，但所有权、比较器和 in-order/equal-range 行为一致。

## 4. append 的发布顺序和可选 division 边界

四端共同伪代码：

```text
appendParameterEntry(parameter):
    if parameter.Type != Object:
        return

    parameterObject = owning strict accessor(copy(parameter))
    entries.emplace_back()                  // publish first
    entry = entries.back()

    entry.id             = strict GetValue("id")
    entry.discretization = strict GetValue("discretization")
    entry.rangeBegin     = strict GetValue("rangeBegin")
    entry.rangeEnd       = strict GetValue("rangeEnd")

    if PropGet(MUSTEXIST, "division") succeeds:
        entry.division = result.AsReal()
    else:
        entry.division = entry.rangeEnd - entry.rangeBegin
        if !(entry.division > 0):
            entry.division = 1

    raw = readInitialParameterValue(entry.id)
    normalizeParameterValue(entry, raw)
```

关键边界：

- Object accessor 持有参数 dispatch，贯穿五次 property read 和 normalize；re-entrant getter
  清掉 caller 的外部 Variant 不会使后续读取换 receiver。
- `emplace_back` 发生在第一项 property read 之前。任一严格 getter 抛出，已发布的部分记录保留；
  没有事务回滚。
- 四个必需字段顺序固定，且拥有不同 hint slot；`division` 使用
  `TJS_MEMBERMUSTEXIST` 且无 hint。
- `division` getter 返回失败时，即使恶意 dispatch 已经写 result，也走 missing 分支并忽略写入。
- 只有 missing 分支会把非正 range difference 改成 1。显式存在的 0、负数、NaN 或 infinity
  都原样存入，后续 normalization 自己决定结果。
- 默认值判断的机器语义是 `range > 0 ? range : 1`；NaN 因 unordered 进入 1 分支。

## 5. list 解析、finalize 和异常前沿

共同伪代码：

```text
parseParameterList(parameters):
    if parameters.Type == Void:
        return false

    list = owning strict accessor(copy(parameters))
    count = list.Count()                    // exactly once
    for index in [0, count):
        item = list[index]
        appendParameterEntry(item)          // non-Object item is skipped
    finalizeParameterTable()
    return true

finalizeParameterTable():
    for destination = this; destination != null;
        destination = destination.parent:
        for entry in this.entries:
            destination.rampMap.emplace(entry.id, &entry)
```

只有 exact Void 是无副作用的 `false` fast path。其他类型都进入严格 accessor 路径；非法
primitive 并不等价于空 list。Count 只读取一次，循环使用该 snapshot。某个 index getter 或
append 的 property getter 抛出时，已追加的 vector 前缀保留，finalize 不执行。

finalize 不 clear 目标 map、不去重、不跳过空 id，也不因为祖先已有相同 key/pointer 而抑制
插入。它始终遍历原始 `this->_parameterEntries`，却把每个 borrowed pointer 同时发布到
self 与整条 parent chain。因 list 的全部 append 在 finalize 前结束，发布后正常路径不会再
reallocate 该 vector，所有借用地址在此 motion 生命周期内稳定。

## 6. 初始值的数据流和优先级

共同伪代码：

```text
readInitialParameterValue(id):
    cascadeSuffix = "::" + id
    current = this
    while current != null:
        if current.HM2 contains id:
            return current.HM2[id]

        parent = current.parent
        if parent == null:
            break

        for (cascadeKey, state) in parent.HM1 physical iteration:
            if cascadeKey.IndexOf(cascadeSuffix) < 0:
                continue
            for node in state.heapResult:
                if node.type == 3 and node.childPlayer == original_this:
                    return state.writeVal
                if node.type == 4:
                    for particle child in node:
                        if child == original_this:
                            return state.writeVal
        current = parent
    return +0.0
```

这里有几条容易误写的细节：

- 每层先查该层 HM2，随后才查父层 HM1；不是先一次性扫描所有 HM2。
- HM1 key 条件是 `IndexOf("::" + id) >= 0` 的 substring containment，不是 ends-with。
- type-3/type-4 child 比较对象始终是函数入口的原始 `this`，不是循环中的 `current`。
- `heapResult` 是 HM1 value 内缓存的 non-owning MotionNode pointer vector；函数不重建 cache。
- 第一个命中立即返回，结果依赖 unordered-map 和 cache 的物理迭代次序；本地不能排序以求
  “稳定”。
- 没有命中返回 bitwise 正零。

四端 read helper 的整体循环、type gate、child/particle 比较、writeVal load 和 owner cleanup
同构；ABI 只改变 HM1/HM2/Node/Variant 的 member offset 和 deque/vector stride。

## 7. normalization 的 IEEE 与转换边界

共同伪代码：

```text
normalize(entry, raw):
    if entry.rangeBegin == entry.rangeEnd or entry.division <= 0:
        entry.value = +0
        return

    value = entry.discretization ? signed_int32_toward_zero_saturated(raw)
                                 : raw
    lo = min_ordered(entry.rangeBegin, entry.rangeEnd)
    hi = max_ordered(entry.rangeBegin, entry.rangeEnd)
    value = clamp_ordered(value, lo, hi)
    entry.value = entry.division * (value - entry.rangeBegin)
                                    / (entry.rangeEnd - entry.rangeBegin)
```

四端先直接比较两个 endpoint，不先求差。因此 `+inf == +inf`、`-inf == -inf` 都进入 reset；
`+0 == -0` 也 reset，并写正零。division gate 使用 ordered nonpositive compare，NaN 不命中
reset，最后产生 NaN。

离散化是 ARM `FCVTZS` / VFP `VCVT.S32.F64` 的共同 profile：toward zero；NaN -> 0；正溢出
-> `INT32_MAX`；负溢出 -> `INT32_MIN`。之后再转回 double。ordered min/max/clamp 的 operand
次序与本地显式比较一致：raw NaN 在非离散化模式保留；`-0` 在 `[0,1]` 内可一路传播到
负零 normalized value；endpoint 本身 unordered 时不应替换成 `std::fmin/fmax` 的另一套
NaN 规则。

Android arm64 的 inlined 代码和三端独立 helper 使用同一序列。不能因为 Hex-Rays 在 A64
append 中丢失 floating return 临时量，就把 readInitial 的返回值解释成未初始化数据。

## 8. `parameterize` 选择和 partial commit

ordinary init 的两条分支为：

```text
if parameterize.Type == Object:
    append(parameterize)
    finalize()
    if entries is not empty:
        selected = &entries.front()
    // empty append preserves previous selected pointer
else:
    parameters = motion.parameter
    parse(parameters)
    if parameterize.Type == Integer:
        index = unsigned_narrow(parameterize.AsInteger())
        if index >= entries.size:
            throw "parameter id out of range."
        selected = &entries[index]
    else:
        selected = null
```

32 位与 iOS arm64 保留独立 select helper；Android arm64 内联。类型不是 Integer 时直接返回
null。Integer 会先经 native Integer conversion，再窄化成 unsigned 机器 index；负值因此变成
巨大正数并与超大正数一样抛 exact range error。返回地址按每端自然 stride 计算。

进入 parameterize 读取前，ordinary init 已经提交新的 loop/last/tag/priority/root-content
owners，并 clear node-label map 与 parameter-entry vector。此处不 purge ramp map，也不预先清
selected pointer：

- Object 分支 append 因非 Object/异常未产生 entry 时，旧 selected pointer保留；
- Integer 越界时 selected 保留旧值；
- parse/append/finalize 中途抛出时，vector/map 保留各自已发布前缀；
- 只有 non-Integer、non-Object 的正常 else 尾才明确写 selected=null。

这些状态直到之后的 node-tree 和 variable initializer 才继续演进，不能用局部 RAII transaction
或“先清所有索引”的防御式重构改变。

## 9. purge、借用节点与析构顺序

共同伪代码：

```text
purgeParameterRampMap():
    for destination = this; destination != null;
        destination = destination.parent:
        for entry in this.entries:
            range = destination.rampMap.equal_range(entry.id)
            for it in range:
                if it->second == &entry:
                    it = destination.rampMap.erase(it)
                else:
                    ++it

Player::~Player():
    purgeParameterRampMap()
    parameterEntries.clear()
    variableLabelScopes.clear()
    resetAndReleaseOldNodeTree()
    ...
```

value-pointer equality是必要条件；相同 key 下属于其他 child vector 的节点必须保留。每次 erase
只释放 multimap node 与 owning key，不释放 mapped entry。purge 完成时 vector 元素仍全部存活，
所以 key comparison 与 address matching 都安全。随后 clear vector 才析构每个 `ttstr id`。

四端 purge 都只有析构 caller；普通 replay 没有该 call。这也说明 parent ramp map 的 child
借用项依赖 child Player 析构主动摘除，而不是由 parent map value destructor递归处理。

## 10. 本地源码逐行对照

- `cpp/plugins/motionplayer/PlayerVariable.cpp:66`：endpoint/division gate、signed-int32
  saturation、ordered clamp 和最终浮点运算顺序一致。
- `cpp/plugins/motionplayer/PlayerVariable.cpp:116`：append-first、五项 getter 顺序、strict
  division probe、missing fallback 和 owner lifetime 一致。
- `cpp/plugins/motionplayer/PlayerVariable.cpp:159`：exact Void fast path、Count snapshot、index
  loop、finalize 时点一致。
- `cpp/plugins/motionplayer/PlayerVariable.cpp:176`：self/ancestor 多重插入和 borrowed pointer
  一致。
- `cpp/plugins/motionplayer/PlayerVariable.cpp:186`：equal-range 加 mapped-address 精确 purge
  一致。
- `cpp/plugins/motionplayer/PlayerVariable.cpp:263`：HM2/HM1 逐层优先级、`"::"` substring、
  original-this child comparison和正零 fallback 一致。
- `cpp/plugins/motionplayer/PlayerCore.cpp:668`：ordinary init 的 clear/parameterize/select/partial
  commit 顺序一致。
- `cpp/plugins/motionplayer/RuntimeSupport.h:32` 与
  `cpp/plugins/motionplayer/internal/player_containers.h:75`：源码字段顺序、vector owner、
  multimap key owner/value borrower模型一致，且没有写入 ABI padding。

现有单元测试已经覆盖 retained NCB source、strict division failure、append-before-read、parse root
owner、finalize-before-root-release，以及 equal infinities、NaN、signed zero、int32 saturation 等
normalization 边界。正式 CMake/Emscripten 工具链在当前环境仍不可用，所以本轮没有声称正式
unit/Web build 通过。

## 11. IDB 固化与后续顺序

四库已写入 domain function names、函数注释、关键 bookmark；Android arm64 的两个 inline
位置另有行注释。四个 IDB 均已原位保存。

本 slice 关闭参数表 pipeline。下一执行依赖是 `buildNodeTree`：它消费刚刚选出的
`MotionParameterEntry*` 并把 parameter alias 写入 MotionNode；节点树完成后再审
`initVariables`，从而保持 ordinary initializer 的真实调用顺序。

后续 `buildNodeTree` slice已证明 node initializer与ordinary initializer在三端共享独立 selector、
Android arm64两处内联；本地因此提取了共同 `internal::selectParameterEntry_guess`，不再复制
ordinary分支的索引逻辑。该后续结构化修改不改变本报告恢复的 unsigned index/error语义。
