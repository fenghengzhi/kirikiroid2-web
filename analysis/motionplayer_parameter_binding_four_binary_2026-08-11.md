# MotionPlayer parameter binding：四参考二进制交叉复原

日期：2026-08-11

本页只记录 `reference/binaries/` 中四个当前参考二进制的结果。旧
`libkrkr2.so` 地址不再作为证据；编译源文件中的实现名统一使用
`_guess`，地址集中保留在这里。

## 1. 函数映射

| 语义 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| `Player_bindParameterValue_guess` | `0x6C1A48` | `0x58C4D8` | `0x100116410` | `0x113D54` |
| `splitParameterLabel_guess` | `0x6CDFD4` | `0x594508` | `0x10011F8D0` | `0x11E494` |
| `splitTtstrByDelimiter_guess` | `0x695114` | `0x571C50` | `0x1000F52D0` | `0xF1D20` |
| `Player_setVariableCompat_guess` | `0x6CE250` | `0x594680` | `0x10011FD04` | `0x11E978` |
| `Player_readBoundParameterValue_guess` | `0x6CA77C` | `0x592810` | `0x10011D3D8` | `0x11BD50` |
| `Player_appendParameterEntry_guess` | `0x6AEAF8` | `0x57FA14` | `0x100106D00` | `0x104168` |
| `Player_applyParameterRamps_guess` | 内联于 binder | `0x585058` | `0x10010DDE0` | `0x10B708` |
| `normalizeParameterValue_guess` | 内联于 append/binder | `0x57FC38` | `0x100106F78` | `0x10446C` |

Android arm64 IDB 原先把 `0x6CDFD4` 的 split helper、异常清理块以及
`setVariable` raw callback 错并为一个函数。本次按两个独立 prologue 和 return
边界修复为：

- split helper：`[0x6CDFD4, 0x6CE250)`；
- raw callback：`[0x6CE250, 0x6CE3F8)`。

修复后 raw callback 的反编译控制流与另外三端逐项一致。

## 2. 唯一的 native binder

四端都只有一个本体，逻辑签名为：

```cpp
void Player_bindParameterValue_guess(
    Player *player, const ttstr &rawLabel, int mode, double rawValue);
```

本地此前错误地分裂成两条数据流：脚本使用的 `std::string` 重载包含 HM1
heapResult 和 child ramp，而 Engine/var-track 使用的 `ttstr` 重载只写 HM1/HM2。
参考二进制不支持这种分裂；三个调用来源都进入同一个函数：

1. Engine 的 HM7 bind-loop 与 clamp-control binder，`mode=0`；
2. Player var-track 插值结果，`mode=0`；
3. `Motion.Player.setVariable(label, value, mode=0)` raw callback，使用脚本给出的
   mode。

共有伪代码如下：

```text
parts = splitParameterLabel(rawLabel)
if parts.split:
    joined = parts.scope + "::" + parts.suffix
    state = HM1.find_or_insert(joined)
    if newly inserted:
        state.keyCopy = joined
        state.chainSegments = split(parts.scope, "/")
        state.weight = 1.0
    state.writeVal = rawValue
    rebuildHeapResult(state)

    for node in state.heapResult:
        if node.type == 4:
            for each particle child Player:
                applyParameterRamps(child, parts.suffix, mode, rawValue)
        else if node.type == 3:
            child = resolve native child Player
            applyParameterRamps(child, parts.suffix, mode, rawValue)

HM2[rawLabel] = rawValue
applyParameterRamps(player, rawLabel, mode, rawValue)
```

执行顺序是可观察的：descendant ramp 在 HM2 写入之前，own-player ramp 在 HM2
写入之后。HM1 只在 split 成功时运行；HM2 与 own ramp 始终运行。

## 3. label 拆分和边界

`splitParameterLabel_guess` 的规则不是“取最靠右的分隔符”，也不是在两类
分隔符中取最早者，而是有固定优先级：

1. 从位置 0 搜索第一次出现的 `"::"`；只要存在就选它；
2. 完全不存在 `"::"` 时，才搜索第一次出现的 `'/'`；
3. 两者都不存在时返回 false。

因此：

| 输入 | split | scope | suffix | joined HM1 key |
|---|---|---|---|---|
| `plain` | false | — | — | — |
| `a::b::c` | true | `a` | `b::c` | `a::b::c` |
| `a/b/c` | true | `a` | `b/c` | `a::b/c` |
| `a/b::c` | true | `a/b` | `c` | `a/b::c` |
| `::tail` | true | empty | `tail` | `::tail` |
| `scope::` | true | `scope` | empty | `scope::` |
| `/tail` | true | empty | `tail` | `::tail` |

前导、尾随分隔符都算成功拆分；binder 不以 `suffix.empty()` 代替 split flag。
空 raw label 也没有早退：它跳过 HM1，但仍 upsert HM2 的空 key，并对 own ramp
map 做一次空 key 的 `equal_range`。

HM1 新节点的 `chainSegments` 使用另一个通用 splitter，并明确传入 `/`，不是
`"::"`。通用 splitter 会保留前导、尾随以及连续分隔符产生的空元素：

```text
split("", "/")       -> [""]
split("/a/", "/")    -> ["", "a", ""]
split("a//b", "/")   -> ["a", "", "b"]
```

## 4. 容器和 ABI 布局

Player 内的三张 binder 容器在四端的偏移如下：

| 容器 | Android arm64 | Android armv7 | iOS arm64 | iOS armv7 |
|---|---:|---:|---:|---:|
| HM1：joined key → cascade state | `+264` | `+192` | `+208` | `+160` |
| HM2：raw label → double | `+320` | `+220` | `+248` | `+180` |
| parameter ramp multimap | `+408` | `+264` | `+320` | `+216` |
| parameter-entry vector | `+384` | `+252` | `+296` | `+204` |

64 位 HM1 value 的共同布局为：

| value offset | 内容 |
|---:|---|
| `+0` | joined-key `ttstr` 副本 |
| `+8/+16/+24` | `vector<ttstr> chainSegments` |
| `+32` | `double writeVal` |
| `+40` | `double weight` |
| `+48/+56/+64` | `vector<MotionNode *> heapResult` |

参数记录在两个 64 位目标上是 56 字节；Android armv7 为 48 字节，iOS armv7
为 44 字节。共同语义字段顺序是：

```text
ttstr id
bool discretization
double rangeBegin
double rangeEnd
double division
double value
int mode
```

32 位 Android/iOS 的 double 字段均从 `+8` 开始，依次位于 `+8/+16/+24/+32`，
mode 位于 `+40`。64 位字段依次位于 `+16/+24/+32/+40`，mode 位于 `+48`。

multimap mapped value 是参数 vector 元素的裸指针。`equal_range` 会遍历同 id 的
全部记录；原生不检查 mapped pointer 是否为空。child ramp 调用同样不检查解析后
的 child Player 是否为空。因此本地原先的两个 null-skip 都会把原生崩溃边界静默
改成成功，现已移除。

## 5. division 与 ramp 数学

参数记录创建时：

```text
if metadata has "division":
    entry.division = AsReal(metadata.division)  // 原样保留，包括 0/负数
else:
    entry.division = rangeEnd - rangeBegin
    if entry.division <= 0:
        entry.division = 1.0
```

本地旧实现保存的是 `division / (rangeEnd-rangeBegin)`，虽然正常正区间下经另一处
漏除法后经常数值相消，但内部记录并不一致。原生的每次归一化为：

```text
result = 0
if rangeBegin != rangeEnd && division > 0:
    v = discretization ? double(saturated_int32_toward_zero(rawValue)) : rawValue
    v = clamp(v, min(rangeBegin, rangeEnd), max(rangeBegin, rangeEnd))
    result = division * (v - rangeBegin) / (rangeEnd - rangeBegin)
entry.value = result
```

每个匹配项都会先写 `entry.mode = mode`，再写归一化后的 value。range 相等、
division 为 0 或负数时 value 为 `0.0`，但 mode 仍然更新。

2026-08-16 的四端指令复核补齐了 IEEE/转换边界：range guard 直接比较两个端点，
所以 `(+Inf,+Inf)` 与 `(-Inf,-Inf)` 都写 `+0.0`；division 为 NaN 时有序的
`<= 0` 不成立，计算继续。discretization 的目标固定为 signed int32，NaN 转 0，
超界值向 `INT32_MIN`/`INT32_MAX` 饱和；这不能用对超界 double 行为未定义的普通
C++ cast 表示。min/max/clamp 的 operand identity、raw NaN 和 signed-zero 行为见
`motionplayer_parameter_normalization_ieee_conversion_four_binary_2026-08-16.md`。

## 6. read-bound-value 路径

`Player_readBoundParameterValue_guess` 是参数记录创建时读取当前原始值的 helper：

- label 可拆分：构造相同 joined key，只查当前 Player 的 HM1，命中返回
  `state.writeVal`；
- label 不可拆分：只查当前 Player 的 HM2，命中返回 double；
- miss 返回 `0.0`。

它不沿 `_parentPlayer` 链搜索，也不尝试“HM2 full key 后再 HM2 suffix”的本地旧
fallback。尾随分隔符会走 HM1，包括空 suffix。

补充：`Motion.Player.getVariable` 四端都直接注册到这个 helper。EmotePlayer/D3D
表面先经过独立的 EmoteEngine scope/HM4 路由，详见
`motionplayer_get_variable_routing_four_binary_2026-08-14.md`。

## 7. `Motion.Player.setVariable` raw callback

四端共同边界：

```text
if numparams < 2: return TJS_E_BADPARAMCOUNT
label = ttstr(param[0])
mode  = (numparams == 2) ? 0 : param[2].AsInteger()
value = param[1].AsReal()
Player_bindParameterValue(player, label, mode, value)
return TJS_S_OK
```

它不写 `EmoteEngine::_dirty`，不维护第二份 eval-result list，也没有本地旧实现的
`param[0]/param[1] == nullptr` 防御分支。NCB 层负责提供有效的参数指针；原生 callback
直接解引用。

## 8. 本地落地与验证

本次源代码调整：

- 合并两套 binder 为 `Player::bindParameterValue_guess(ttstr,int,double)`；
- Engine、var-track、raw callback 全部改走同一个入口；
- 删除只被旧脚本重载使用、且不属于 native 数据流的 `_evalResultList` 及索引；
- 修复 `::`/`/` 优先级、第一次出现、尾随分隔符和 `/` chain split；
- 恢复 raw-label HM2 的无条件 upsert、descendant/own ramp 顺序和 null 崩溃边界；
- 参数记录改存原始 `division`，恢复 `division * delta / range`；
- `readBoundParameterValue_guess` 改为 HM1/HM2 两分支，不再递归 parent 或读 suffix
  HM2。

验证结果：

- Web Debug 完整链接通过；
- Wasmtime Debug 受影响目标完整重编译通过；
- `tests/unit-tests/plugins/motionplayer-dll.cpp` 使用真实 Web/Emscripten 编译参数
  执行 `-fsyntax-only` 通过，仅保留仓库既有 `_tss` 警告；
- 新增 binder/raw-callback 回归覆盖 plain key、尾随 `::`、多段 `/` 以及
  `TJS_E_BADPARAMCOUNT`；
- 四个 IDB 均写入上述函数名并保存；Android arm64 的错误函数边界已修复。
