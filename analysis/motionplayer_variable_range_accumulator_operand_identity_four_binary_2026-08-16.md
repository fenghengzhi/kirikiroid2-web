# MotionPlayer variable-range 累计 operand identity（四参考二进制，2026-08-16）

## 结论

`Player_foldVariableRangeRecursive_guess` 的四次浮点选择不是可随意嵌套的
`std::min/std::max`。每个匹配 parameter entry 先正规化两个 endpoint，再折入递归
共享的 extrema：

```text
entryMin = rangeEnd < rangeBegin ? rangeEnd : rangeBegin
minValue = minValue < entryMin ? minValue : entryMin

entryMax = rangeBegin < rangeEnd ? rangeEnd : rangeBegin
maxValue = entryMax < maxValue ? maxValue : entryMax
```

两层的 equality/unordered identity 不同：

| 选择层 | 相等或 unordered 时选择 |
| --- | --- |
| entry endpoint 正规化 | `rangeBegin` |
| recursive accumulator | 当前 entry 的新 candidate |

本地旧代码的内层 `std::min(rangeBegin,rangeEnd)` / `std::max(...)` 正确，但外层
`std::min(minValue,entryMin)` / `std::max(maxValue,entryMax)` 在相等/unordered 时保留
旧 accumulator，改变了 NaN、signed zero 和遍历顺序边界。

最终 `Player_getVariableRange_guess` 也只在有序 `minValue < maxValue` 时创建
Dictionary。它的 Void 条件应写成 `!(minValue < maxValue)`，不能写成对 NaN 返回
false 的 `minValue >= maxValue`。

## 四端函数映射

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
| --- | ---: | ---: | ---: | ---: |
| range folder | `0x6D3B4C` | `0x597D54` | `0x100124380` | `0x1236BC` |
| range query | `0x6D3970` | `0x597C00` | `0x1001241FC` | `0x1234D8` |
| 四选择序列 | `0x6D3C08..0x6D3C44` | `0x597D8C..0x597DD4` | `0x1001243E4..0x100124420` | `0x123742..0x12378A` |
| ordered-validity gate | `0x6D39B0..0x6D39B4` | `0x597C42..0x597C4A` | `0x100124230..0x100124234` | `0x12354E..0x123556` |

表内绝对地址只作为 recovery IDB 证据索引。函数的原始 stripped C++ 拼写仍不可
恢复，因此恢复名继续保留 `_guess`。

## 四次选择的逐指令归约

Android ARM64 的循环体最直接：

```text
FCMP rangeEnd, rangeBegin
CSEL entryMinPtr, rangeEndPtr, rangeBeginPtr, MI

FCMP minValue, entryMin
CSEL selectedMinPtr, minValuePtr, entryMinPtr, MI

FCMP rangeBegin, rangeEnd
CSEL entryMaxPtr, rangeEndPtr, rangeBeginPtr, MI

FCMP entryMax, maxValue
CSEL selectedMaxPtr, maxValuePtr, entryMaxPtr, MI
```

AArch64 浮点 unordered flags 不满足 `MI`。因此每个 `CSEL` 都取 false operand：
endpoint 层取 `rangeBegin`，outer 层取新 candidate。iOS ARM64 是同构的四个
`FCMP/CSEL ... MI`。

两个 ARMv7 参考依次使用 `VCMPE.F64`、`VMRS APSR_nzcv,FPSCR`、`IT MI` 和
`VMOVMI`，operand 顺序与两份 ARM64 完全一致；unordered 同样不会执行条件 move。
这排除了“某个 ABI 使用 fmin/fmax”或“32/64 位端点顺序不同”的可能。

## 最终 Dictionary gate

四端都把最终 `minValue` 与 `maxValue` 比较后，以 `PL` 跳到 Void 返回：

```text
compare minValue, maxValue
if not MI: return Void
else:      build fresh Dictionary
```

`PL` 在 equal、greater 和 FP unordered 时都成立；Dictionary 路径只对应 ordered
less-than。反编译器把该分支显示成 `min >= max` 时丢失了 unordered 条件，源级应
保留正向判定 `if (!(minValue < maxValue)) return Void`。

## 可观察边界

- 两个数值相等的 lower candidate 会由后遍历 entry 覆盖；`+0.0` 与 `-0.0` 的
  sign bit 因而按最后一条匹配记录（以及递归 child 顺序）决定。
- `rangeBegin=NaN` 时，endpoint 两个候选都保留该 NaN；若它是最后一个匹配 entry，
  两个 extrema 变为 NaN，最终 ordered gate 返回 Void。
- 若 NaN entry 后还有 finite entry，outer unordered compare 选择新的 finite
  candidate，extrema 可恢复，正常发布 Dictionary。
- `rangeEnd=NaN`、`rangeBegin` finite 时，两次 endpoint 选择都保留
  `rangeBegin`；该 entry 表现为一个 finite 单点 candidate。
- 由于 folder 先扫本 Player，再按共享 child visitor 顺序递归，child 中更晚的
  equal/unordered candidate 可以覆盖 parent 或较早 child 的位级结果。

## 端口与回归

本轮修改：

- `cpp/plugins/motionplayer/PlayerVariable.cpp` 用四个显式 ternary 恢复 exact
  operand identity，并把 final gate 改为 ordered `min < max`；
- `cpp/plugins/motionplayer/Player.h` 把 public boundary 注释修正为 unordered 也
  返回 Void；
- `tests/unit-tests/plugins/motionplayer-dll.cpp` 覆盖：finite 后跟 NaN -> Void、
  NaN 后跟 finite -> 恢复 Dictionary、equal lower bound 采用后条目的 `-0.0`。

四个 recovery IDB 在 folder 选择序列和 final gate 处均补充了注释与书签并保存。
