# MotionPlayer HM5 变量范围节点四参考二进制复原（2026-08-11）

## 结论

重新检查四个当前参考二进制中的 variable-list builder、HM5 查找/插入节点、
`getVariableRange` 和 metadata reset/clear 路径后，确认本地旧注释与实现存在两处
源级偏差：

1. HM5 的 mapped value 首字段不是永远为空的未知字符串。插入 miss 时，原版对
   同一个 label 连续做两次 CopyRef：一份成为 unordered_map key，另一份成为
   mapped value 自有的 label 副本。清空节点时先 Release mapped label，再
   Release key。因此即便查询不读取该字段，它仍是引用计数和对象生命周期的一部分。
2. frame 极值更新不是 `std::min/std::max` 的“相等时保留旧值”边界。四端都在
   相等或 unordered 比较时选择新 frame 值；这会保留新值的 signed-zero 符号，
   并让新输入 NaN 覆盖旧的有限值。

同时确认了一个看似异常、但四端完全一致的边界：节点构造只把第一组 double
写成 `DBL_MAX/-DBL_MAX`，builder 却只读写末尾第二组 double；末尾
`frameMin/frameMax` 没有构造期初始化。本地继续保留这一行为，没有用第一组值、
零或有限极值对它做防御性初始化。

## 四端函数映射

| 语义 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| `EmoteEngine_buildVariableList_guess` | `0x667910` | `0x555FC0` | `0x1001A73C0` | `0x1A693C` |
| `EmoteVariableRangeMap_find_guess` | `0x685398` | `0x566C72` | `0x1001BDF24` | `0x1BC9EC` |
| construct/insert wrapper | `0x685488` | `0x566D16` | caller inline | caller inline |
| node constructor | wrapper 内联 | `0x566DD6` | `0x1001BE034` | `0x1BCA98` |
| HM5 clear | `0x666B78` 内联于 `0x666C68..0x666CB4` | `0x564BF6` | `0x1001BDB94` | `0x1BC6D0` |
| destroy-node chain | 同 clear 内联 | 同 clear 内联 | `0x1001B883C` | `0x1B7FA0` |
| `EmotePlayer_getVariableRange_guess` | `0x670FCC` | `0x55AF8C` | `0x1001AE454` | `0x1ADC6C` |

表中带 `_guess` 的语义名均已写回四份 IDB；HM5 节点构造、clear 与浮点比较
位置也加入了字段初始化/释放顺序/相等与 unordered 选择边界注释。四份 IDB
均已强制重新反编译并保存。

HM5 keyed-find helper的全部直接调用者如下：

- Android ARM64：builder、`getVariableRange`，以及 speculative-node 插入
  wrapper 内部的重复键检查；
- 其余三端：builder 与 `getVariableRange`；
- 没有第三个业务读取者，也没有首组 double 的读取路径。

## ABI 节点布局

四端 mapped value 的源级字段顺序相同；STL hash-node 头和对齐位置不同：

| 字段 | Android ARM64 | Android ARMv7 | iOS ARM64 | iOS ARMv7 |
|---|---:|---:|---:|---:|
| node size | `0x40` | `0x40` | `0x40` | `0x30` |
| next | `+0` | `+0` | `+0` | `+0` |
| cached hash | `+56` | `+56` | `+8` | `+4` |
| key `ttstr` | `+8` | `+8` | `+16` | `+8` |
| mapped label `ttstr` | `+16` | `+16` | `+24` | `+12` |
| initialized min seed | `+24` | `+24` | `+32` | `+16` |
| initialized max seed | `+32` | `+32` | `+40` | `+24` |
| uninitialized `frameMin` | `+40` | `+40` | `+48` | `+32` |
| uninitialized `frameMax` | `+48` | `+48` | `+56` | `+40` |

四个 node constructor 都写入完全相同的两个 IEEE-754 常量：

```text
0x7FEFFFFFFFFFFFFF = DBL_MAX
0xFFEFFFFFFFFFFFFF = -DBL_MAX
```

它们都没有写最后 16 字节的 frame extrema。Android ARM64 的构造/插入
wrapper 先分配并构造完整 speculative node，再查重；重复键命中时会按
mapped-label、key 的顺序 Release 两份引用并删除 speculative node。其余端的
编译器拆分方式不同，但源级所有权相同。

## 插入与重复键边界

四端 builder 在取得 `item.label` 后的共同逻辑是：

```cpp
auto it = hm5.find(label);
if (it == hm5.end()) {
    // key 与 mapped value 各自 CopyRef 同一 label
    it = hm5.emplace(label, EmoteVariableRange(label)).first;
}
EmoteVariableRange &range = it->second;
```

Android ARM64 在调用 `0x685488` 前把同一个 label handle 写入相邻的两个栈槽，
每个槽各 AddRef 一次；Android ARMv7 的 `0x556118..0x556168`、iOS ARM64 的
`0x1001A7548..0x1001A7598`、iOS ARMv7 的
`0x1A6AB8..0x1A6AF4` 都做相同的双 CopyRef。命中已有键时不会重建或覆盖
mapped value。

本地用下面的 portable C++ 表达这一构造和命中边界：

```cpp
hm5.try_emplace(label, label);
```

第一个 label 是 map key；第二个 label 传给 mapped value 的显式构造函数。
`try_emplace` 命中时不构造 mapped value，正好保持原版“已有节点不覆盖”的语义。

## frame 极值更新与浮点边界

四端共同分支不是标准库 `std::min/std::max` 的参数顺序边界，而是：

```cpp
range.frameMin = range.frameMin < frameValue
    ? range.frameMin
    : frameValue;
range.frameMax = frameValue < range.frameMax
    ? range.frameMax
    : frameValue;
```

普通有序且不相等的有限值仍得到数学意义上的 min/max，但以下边界可观察：

- `current == frameValue`：选择新 frame 值，而不是旧 current；
- `current=+0.0, frameValue=-0.0`：两个槽都得到新值 `-0.0`；
- 新 frame 为 NaN：两个比较均为 false，NaN 同时覆盖旧 min/max；
- 旧槽为 NaN、新 frame 有限：比较同样为 false，新有限值覆盖旧 NaN；
- 没有 `isfinite`、NaN 规整或 signed-zero 归一化。

这也说明为什么不能用 `std::fmin/std::fmax` 替代：它们的 NaN 选择规则同样不同。

## clear 与生命周期

metadata reset 会清 HM4 后清 HM5。HM5 每个节点的共同析构顺序为：

1. 保存 `next`；
2. Release mapped value 的 label 副本；
3. Release map key；
4. `operator delete(node)`；
5. 继续 next；
6. bucket 数组清零，element count 清零。

Android ARM64 在 `0x666B78` 内联这段循环；Android ARMv7 的 `0x564BF6`
显式调用 `Release(node+16)` 后 `Release(node+8)`；iOS ARM64 的
`0x1001B883C` 依次处理 `node+24`、`node+16`；iOS ARMv7 的 `0x1B7FA0`
依次处理 `node+12`、`node+8`。四端都没有释放四个 raw double。

因此旧端口把 mapped string 默认留空，虽然不改变 `getVariableRange` 的数值，
仍少了一次长期 label AddRef 和一次 clear-time Release，不满足对象生命周期
一比一复原要求。

## 本地修改与验证

源代码修改：

- `EmoteVariableRange::unknownString` 重命名为带推测后缀的
  `labelCopy_guess`，显式构造时 CopyRef label；
- 首组 double 重命名为 `unusedMinSeed_guess/unusedMaxSeed_guess`，仍仅在
  构造期写 `DBL_MAX/-DBL_MAX`；
- builder 改用 `try_emplace(label, label)`；
- frame extrema 改成四端相同的显式比较/选择，保留 signed-zero 与 NaN 边界；
- 单元测试覆盖 mapped label、首组常量、相等 signed-zero 选择和 unordered
  NaN 选择；
- 删除触及区域中把旧 `libkrkr2.so` 地址当作当前证据、以及把 rehash helper
  误写成 clearer 的过时注释。

验证结果：Web Debug 与 Wasmtime Debug 增量构建均成功（两者均已收敛到
`ninja: no work to do.`）；使用 Web Debug 的真实 Emscripten 编译参数对
`tests/unit-tests/plugins/motionplayer-dll.cpp` 做完整 `-fsyntax-only` 检查通过，
只有仓库既有的 `_tss` literal-operator deprecation warning；`git diff --check`
通过，仅报告工作树既有的 LF/CRLF 转换提示。
